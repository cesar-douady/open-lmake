// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/resource.h>
#include <linux/limits.h>              // ARG_MAX

#include "app.hh"
#include "disk.hh"
#include "fd.hh"
#include "hash.hh"
#include "pycxx.hh"
#include "rpc_job.hh"
#include "thread.hh"
#include "time.hh"
#include "trace.hh"

#include "autodep/gather_deps.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

static constexpr int NConnectionTrials = 3 ;               // number of times to try connect when connecting to server

ServerSockFd               g_server_fd       ;
GatherDeps                 g_gather_deps     { New }        ;
JobRpcReply                g_start_info      ;
::string                   g_service_start   ;
::string                   g_service_mngt    ;
::string                   g_service_end     ;
SeqId                      g_seq_id          = 0/*garbage*/ ;
JobIdx                     g_job             = 0/*garbage*/ ;
::atomic<bool>             g_killed          = false        ;                  // written by thread S and read by main thread
::umap_s<          Tflags> g_known_targets   ;
::vmap<Py::Pattern,Tflags> g_target_patterns ;
NfsGuard                   g_nfs_guard       ;

Tflags tflags(::string const& target) {
	auto it = g_known_targets.find(target) ;      if (it!=g_known_targets.end()) return it->second       ;
	for( auto const& [p,tf] : g_target_patterns ) if (+p.match(target)         ) return tf               ;
	/**/                                                                         return UnexpectedTflags ;
}

void kill_thread_func(::stop_token stop) {
	t_thread_key = 'K' ;
	Trace trace("kill_thread_func",g_start_info.kill_sigs) ;
	for( size_t i=0 ;; i++ ) {
		int sig = i<g_start_info.kill_sigs.size() ? g_start_info.kill_sigs[i] : -1 ; // -1 means best effort
		trace("sig",sig) ;
		if (!g_gather_deps.kill(sig)  ) return ;                               // job is already dead, if no pid, job did not start yet, wait until job_exec dies or we can kill job
		if (!Delay(1.).sleep_for(stop)) return ;                               // job_exec has ended
	}
}

void kill_job() {
	Trace trace("kill_job") ;
	static ::jthread kill_thread{kill_thread_func} ;                           // launch job killing procedure while continuing to behave normally
}

bool/*keep_fd*/ handle_server_req( JobServerRpcReq&& jsrr , Fd ) {
	Trace trace("handle_server_req",jsrr) ;
	switch (jsrr.proc) {
		case JobServerRpcProc::Heartbeat :
			if (jsrr.seq_id!=g_seq_id) {                                       // report that the job the server tries to connect to no longer exists
				trace("bad_id",g_seq_id,jsrr.seq_id) ;
				try {
					OMsgBuf().send(
						ClientSockFd( g_service_end , NConnectionTrials )
					,	JobRpcReq( JobProc::End , jsrr.seq_id , jsrr.job , {.status=Status::LateLost} )
					) ;
				} catch (::string const& e) {}                                 // if server is dead, no harm
			}
		break ;
		case JobServerRpcProc::Kill :
			g_killed = true ;
			if (jsrr.seq_id==g_seq_id) kill_job() ;                            // else server is not sending its request to us
		break ;
		default : FAIL(jsrr.proc) ;
	}
	return false ;
}

::pair_s<bool/*ok*/> wash(Pdate start) {
	Trace trace("wash",start,g_start_info.pre_actions) ;
	::pair<vector_s/*unlinks*/,pair_s<bool/*ok*/>> actions = do_file_actions( g_start_info.pre_actions , g_nfs_guard , g_start_info.hash_algo ) ;
	if (actions.second.second/*ok*/) for( ::string const& f : actions.first ) g_gather_deps.new_unlink(start,f) ;
	else                             trace(actions.second) ;
	return actions.second ;
}

::map_ss prepare_env() {
	::map_ss res ;
	::string abs_cwd = g_start_info.autodep_env.root_dir ;
	if (+g_start_info.cwd_s) {
		append_to_string(abs_cwd,'/',g_start_info.cwd_s) ; abs_cwd.pop_back() ;
	}
	res["PWD"        ] = abs_cwd                           ;
	res["ROOT_DIR"   ] = g_start_info.autodep_env.root_dir ;
	res["SEQUENCE_ID"] = to_string(g_seq_id             )  ;
	res["SMALL_ID"   ] = to_string(g_start_info.small_id)  ;
	for( auto&& [k,v] : g_start_info.env ) {
		if      (v!=EnvPassMrkr) res[k] = env_decode(::move(v)) ;
		else if (has_env(k)    ) res[k] = get_env(k)            ;              // if value is special illegal value, use value from environement (typically from slurm)
	}
	if ( g_start_info.keep_tmp || !res.contains("TMPDIR") )
		res["TMPDIR"] = mk_abs( g_start_info.autodep_env.tmp_dir , g_start_info.autodep_env.root_dir+'/' ) ; // if we keep tmp, we force the tmp directory
	g_start_info.autodep_env.tmp_dir = res["TMPDIR"] ;
	if (+g_start_info.autodep_env.tmp_view) res["TMPDIR"] = g_start_info.autodep_env.tmp_view ; // job must use the job view
	//
	Trace trace("prepare_env",res) ;
	//
	try {
		unlink_inside(g_start_info.autodep_env.tmp_dir) ;                      // ensure tmp dir is clean
	} catch (::string const&) {
		try                       { mkdir(g_start_info.autodep_env.tmp_dir) ; } // ensure tmp dir exists
		catch (::string const& e) { throw "cannot create tmp dir : "+e ;      }
	}
	return res ;
}

::pair< vmap_s<TargetDigest> , vmap_s<DepDigest> > analyze( ::string& msg , bool at_end ) {
	Trace trace("analyze",STR(at_end)) ;
	::pair< vmap_s<TargetDigest> , vmap_s<DepDigest> > res ; res.second.reserve(g_gather_deps.accesses.size()) ; // typically most of accesses are deps
	NodeIdx prev_parallel_id = 0              ;
	Ddate   target_now       = Ddate::s_now() ;
	for( auto const& [file,info] : g_gather_deps.accesses ) {
		JobExecRpcReq::AccessDigest const& ad = info.digest ;
		Accesses                           a  = ad.accesses ;  if (!info.tflags[Tflag::Stat]) a &= ~Access::Stat ;
		if (info.is_dep()) {
			bool      parallel = info.parallel_id && info.parallel_id==prev_parallel_id ;
			DepDigest dd       { a , ad.dflags , parallel }                             ;
			prev_parallel_id = info.parallel_id ;
			if (+a) {
				dd.date(info.file_date) ;
				dd.garbage = file_date(file)!=info.file_date ;                 // file date is not coherent from first access to end of job, we do not know what we have read
			}
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.second.emplace_back(file,dd) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("dep   ",dd,file) ;
		} else if (at_end) {                                                   // else we are handling chk_deps and we only care about deps
			if (!info.file_date) a = Accesses::None ;
			try                       { chk(info.tflags) ;                                                       }
			catch (::string const& e) { append_to_string( msg , "bad flags (",e,") ",mk_file(file)) ; continue ; } // dont know what to do with such an access
			TargetDigest td{
				a
			,	info.tflags
			,	ad.write
			,	ad.prev_unlink && ad.unlink ? Crc::None : Crc::Unknown                                         // prepare crc in case it is not computed
			,	ad.prev_unlink && ad.unlink ? target_now : info.tflags[Tflag::Crc] ? Ddate() : file_date(file) // if !date, compute crc and associated date
			} ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.first.emplace_back(file,td) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("target",td,STR(ad.unlink),info.tflags,info.file_date,file) ;
		}
	}
	return res ;
}

Fd/*reply*/ server_cb(JobExecRpcReq&& jerr) {
	JobRpcReq jrr ;
	::string  _   ;
	switch (jerr.proc) {
		case JobExecRpcProc::ChkDeps :
			jrr = JobRpcReq( JobProc::ChkDeps , g_seq_id , g_job , analyze(_,false/*at_end*/).second/*deps*/ ) ;
		break ;
		case JobExecRpcProc::DepInfos : {
			::vmap_s<DepDigest> ds ; ds.reserve(jerr.files.size()) ;
			for( auto&& [dep,date] : jerr.files ) ds.emplace_back( ::move(dep) , DepDigest(jerr.digest.accesses,jerr.digest.dflags,true/*parallel*/,date) ) ;
			jrr = JobRpcReq( JobProc::DepInfos , g_seq_id , g_job , ::move(ds) ) ;
		} break ;
		default : FAIL(jerr.proc) ;
	}
	Trace trace("server_cb",jerr.proc,jrr.digest.deps.size()) ;
	ClientSockFd fd{g_service_mngt} ;
	//    vvvvvvvvvvvvvvvvvvvvvv
	try { OMsgBuf().send(fd,jrr) ; }
	//    ^^^^^^^^^^^^^^^^^^^^^^
	catch (...) { return {} ; }                                                // server is dead, do as if there is no server
	return fd ;
}

::vector_s cmd_line() {
	::vector_s cmd_line = ::move(g_start_info.interpreter) ;                                                     // avoid copying as interpreter is used only here
	if ( g_start_info.use_script || (g_start_info.cmd.first.size()+g_start_info.cmd.second.size())>ARG_MAX/2 ) { // env+cmd line must not be larger than ARG_MAX, keep some margin for env
		::string cmd_file   = to_string(g_start_info.remote_admin_dir,"/job_cmds/",g_start_info.small_id) ;
		OFStream cmd_stream { dir_guard(cmd_file) }                                                       ;
		cmd_stream << g_start_info.cmd.first << g_start_info.cmd.second ;
		cmd_line.reserve(cmd_line.size()+1) ;
		cmd_line.push_back(::move(cmd_file)) ;
	} else {
		cmd_line.reserve(cmd_line.size()+2) ;
		cmd_line.push_back( "-c"                                             ) ;
		cmd_line.push_back( g_start_info.cmd.first + g_start_info.cmd.second ) ;
	}
	return cmd_line ;
}

void live_out_cb(::string_view const& txt) {
	static ::string live_out_buf ;                                             // used to store incomplete last line to have line coherent chunks
	// could be slightly optimized, but when generating live output, we have very few jobs, no need to optimize
	live_out_buf.append(txt) ;
	size_t pos = live_out_buf.rfind('\n') ;
	Trace trace("live_out_cb",live_out_buf.size(),txt.size(),pos+1) ;
	if (pos==Npos) return ;
	pos++ ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	OMsgBuf().send( ClientSockFd(g_service_mngt) , JobRpcReq( JobProc::LiveOut , g_seq_id , g_job , live_out_buf.substr(0,pos) ) ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	live_out_buf = live_out_buf.substr(pos) ;
}

void crc_thread_func( size_t id , ::vmap_s<TargetDigest>* targets ) {
	static ::atomic<NodeIdx> target_idx = 0 ;
	t_thread_key =
		id<10 ? '0'+id    :
		id<36 ? 'a'+id-10 :
		id<62 ? 'A'+id-36 :
		/**/    '>'
	;
	Trace trace("crc") ;
	NodeIdx cnt = 0 ;
	for( NodeIdx ti ; (ti=target_idx++)<targets->size() ; cnt++ ) {            // cnt is for trace only
		auto& [tn,td] = (*targets)[ti] ; if (+td.date) continue ;              // crc is already computed or not supposed to be computed
		td.crc = Crc( td.date/*out*/ , tn , g_start_info.hash_algo ) ;
		trace("crc_date",ti,td.crc,td.date,tn) ;
	}
	trace("done",cnt) ;
}

void compute_crcs(::vmap_s<TargetDigest>& targets) {
	size_t            n_threads   = ::min( size_t(::max(1u,thread::hardware_concurrency())) , targets.size() ) ;
	::vector<jthread> crc_threads ; crc_threads.reserve(n_threads) ;
	for( size_t i=0 ; i<n_threads ; i++       ) crc_threads.emplace_back(crc_thread_func,i,&targets) ; // just constructing a destructing the threads will execute & join them
	if (!g_start_info.autodep_env.reliable_dirs) {                              // fast path : avoid listing targets & guards if reliable_dirs
		for( auto const& [t,_] : targets              ) g_nfs_guard.change(t) ; // protect against NFS strange notion of coherence while computing crcs
		for( auto const&  f    : g_gather_deps.guards ) g_nfs_guard.change(f) ; // .
		g_nfs_guard.close() ;
	}
}

int main( int argc , char* argv[] ) {
	//
	Pdate start_overhead = Pdate::s_now() ;
	//
	swear_prod(argc==6,argc) ;                                                 // syntax is : job_exec server:port/*start*/ server:port/*mngt*/ server:port/*end*/ seq_id job_idx is_remote
	g_service_start =                    argv[1]  ;
	g_service_mngt  =                    argv[2]  ;
	g_service_end   =                    argv[3]  ;
	g_seq_id        = from_chars<SeqId >(argv[4]) ;
	g_job           = from_chars<JobIdx>(argv[5]) ;
	//
	ServerThread<JobServerRpcReq> server_thread{'S',handle_server_req} ;
	//
	JobRpcReq req_info   { JobProc::Start , g_seq_id , g_job , server_thread.fd.port()                             } ;
	JobRpcReq end_report { JobProc::End   , g_seq_id , g_job , {.status=Status::EarlyErr,.end_date=start_overhead} } ; // prepare to return an error, so we can goto End anytime
	try {
		ClientSockFd fd {g_service_start,NConnectionTrials} ;
		//                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv                                          // once connection is established, everything should be smooth
		try {                OMsgBuf().send                ( fd , req_info ) ; } catch(::string const&) { exit(3) ; } // maybe normal in case ^C was hit
		try { g_start_info = IMsgBuf().receive<JobRpcReply>( fd            ) ; } catch(::string const&) { exit(4) ; } // .
		//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	} catch (::string const&) { exit(5) ; }                                    // maybe normal in case ^C was hit
	g_nfs_guard.reliable_dirs = g_start_info.autodep_env.reliable_dirs ;
	//
	switch (g_start_info.proc) {
		case JobProc::None  : return 0 ;                                       // server ask us to give up
		case JobProc::Start : break    ;                                       // normal case
		default : FAIL(g_start_info.proc) ;
	}
	//
	if (::chdir(g_start_info.autodep_env.root_dir.c_str())!=0) {
		end_report.backend_msg = to_string("cannot chdir to root : ",g_start_info.autodep_env.root_dir) ;
		goto End ;
	}
	{	if (g_start_info.trace_n_jobs) {
			g_trace_file = new ::string{to_string(g_start_info.remote_admin_dir,"/job_trace/",g_seq_id%g_start_info.trace_n_jobs)} ;
			//
			Trace::s_sz = 10<<20 ;                                             // this is more than enough
			::unlink(g_trace_file->c_str()) ;                                  // ensure that if another job is running to the same trace, its trace is unlinked to avoid clash
		}
		app_init() ;
		Py::init() ;
		//
		Trace trace("main",g_service_start,g_service_mngt,g_service_end,g_seq_id,g_job) ;
		trace("start_overhead",start_overhead) ;
		trace("g_start_info"  ,g_start_info  ) ;
		//
		for( auto const& [d,_] : g_start_info.static_deps )
			g_known_targets.emplace(d,UnexpectedTflags) ;                      // if a target happens to be a static dep, it is necessarily unexpected
		for( auto const& [p,tf] : g_start_info.targets )
			if (tf[Tflag::Star]) g_target_patterns.emplace_back(p,tf) ;
			else                 g_known_targets  .emplace     (p,tf) ;
		//
		::map_ss cmd_env ;
		try                       { cmd_env = prepare_env() ;               }
		catch (::string const& e) { end_report.backend_msg = e ; goto End ; }
		//
		/**/                       g_gather_deps.addr         = g_start_info.addr        ;
		/**/                       g_gather_deps.autodep_env  = g_start_info.autodep_env ;
		/**/                       g_gather_deps.chroot       = g_start_info.chroot      ;
		/**/                       g_gather_deps.create_group = true                     ;
		/**/                       g_gather_deps.cwd          = g_start_info.cwd_s       ; if (+g_gather_deps.cwd) g_gather_deps.cwd.pop_back() ;
		/**/                       g_gather_deps.env          = &cmd_env                 ;
		/**/                       g_gather_deps.kill_sigs    = g_start_info.kill_sigs   ;
		if (g_start_info.live_out) g_gather_deps.live_out_cb  = live_out_cb              ;
		/**/                       g_gather_deps.method       = g_start_info.method      ;
		/**/                       g_gather_deps.server_cb    = server_cb                ;
		/**/                       g_gather_deps.tflags_cb    = tflags                   ;
		/**/                       g_gather_deps.timeout      = g_start_info.timeout     ;
		/**/                       g_gather_deps.kill_job_cb  = kill_job                 ;
		//
		::pair_s<bool/*ok*/> wash_report = wash(start_overhead) ;
		end_report.backend_msg = wash_report.first ;
		if (!wash_report.second) { end_report.digest.status = Status::Manual ; goto End ; }
		g_gather_deps.new_static_deps( start_overhead , g_start_info.static_deps , g_start_info.stdin ) ; // ensure static deps are generated first
		//
		Fd child_stdin ;
		if (+g_start_info.stdin) child_stdin = open_read(g_start_info.stdin) ;
		else                     child_stdin = open_read("/dev/null"       ) ;
		child_stdin.no_std() ;
		Fd child_stdout = Child::Pipe ;
		if (+g_start_info.stdout) {
			child_stdout = open_write(g_start_info.stdout) ;
			g_gather_deps.new_target( start_overhead , g_start_info.stdout , {}/*neg_tflags*/ , {}/*pos_tflags*/ , "<stdout>" ) ;
			child_stdout.no_std() ;
		}
		//
		Pdate         start_job = Pdate::s_now() ;                                                                  // as late as possible before child starts
		//                        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		Status        status    = g_gather_deps.exec_child( cmd_line() , child_stdin , child_stdout , Child::Pipe ) ;
		//                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		Pdate         end_job   = Pdate::s_now() ;                                                                  // as early as possible after child ends
		struct rusage rsrcs     ; getrusage(RUSAGE_CHILDREN,&rsrcs) ;
		trace("start_job",start_job,"end_job",end_job) ;
		//
		::string msg ;
		auto [targets,deps] = analyze(msg,true/*at_end*/) ;
		//
		compute_crcs(targets) ;
		//
		if ( g_gather_deps.seen_tmp && !g_start_info.keep_tmp )
			try { unlink_inside(g_start_info.autodep_env.tmp_dir) ; } catch (::string const&) {} // cleaning is done at job start any way, so no harm
		//
		if      (+msg    ) status = Status::Err    ;
		else if (g_killed) status = Status::Killed ;
		end_report.backend_msg = msg ;
		end_report.digest = {
			.status       = status
		,	.targets      { ::move(targets             ) }
		,	.deps         { ::move(deps                ) }
		,	.stderr       { ::move(g_gather_deps.stderr) }
		,	.stdout       { ::move(g_gather_deps.stdout) }
		,	.wstatus      = g_gather_deps.wstatus
		,	.end_date     = end_job
		,	.stats{
				.cpu { Delay(rsrcs.ru_utime) + Delay(rsrcs.ru_stime) }
			,	.job { end_job      - start_job                      }
			,	.mem = size_t(rsrcs.ru_maxrss<<10)
			}
		} ;
	}
End :
	try {
		ClientSockFd fd           { g_service_end , NConnectionTrials } ;
		Pdate        end_overhead = Pdate::s_now()                      ;
		Trace trace("end",end_overhead,end_report.digest.status) ;
		end_report.digest.stats.total = end_overhead - start_overhead ;            // measure overhead as late as possible
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		OMsgBuf().send( fd , end_report ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		trace("done") ;
	} catch (::string const& e) { exit(2,"after job execution : ",e) ; }
	//
	return 0 ;
}
