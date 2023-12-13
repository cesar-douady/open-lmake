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

static constexpr int     NConnectionTrials = 3                                                ; // number of times to try connect when connecting to server
static constexpr uint8_t TraceNameSz       = JobHistorySz<=10 ? 1 : JobHistorySz<=100 ? 2 : 3 ;
static_assert(JobHistorySz<=1000) ;                                                             // above, it would be wise to make hierarchical names

ServerSockFd g_server_fd     ;
GatherDeps   g_gather_deps   { New }        ;
JobRpcReply  g_start_info    ;
::string     g_service_start ;
::string     g_service_mngt  ;
::string     g_service_end   ;
SeqId        g_seq_id        = 0/*garbage*/ ;
JobIdx       g_job           = 0/*garbage*/ ;
bool         g_killed        = false        ;

void kill_thread_func(::stop_token stop) {
	Trace::t_key = '~' ;
	Trace trace("kill_thread_func") ;
	for( size_t i=0 ;; i++ ) {
		int sig = i<g_start_info.kill_sigs.size() ? g_start_info.kill_sigs[i] : SIGKILL ;
		if (!g_gather_deps.kill(sig)  ) return ;                                          // job is already dead, if no pid, job did not start yet, wait until job_exec dies or we can kill job
		if (!Delay(1.).sleep_for(stop)) return ;                                          // job_exec has ended
	}
}

void kill_job() {
	static ::jthread kill_thread{kill_thread_func} ;                           // launch job killing procedure while continuing to behave normally
}

bool/*keep_fd*/ handle_server_req( JobServerRpcReq&& jsrr , Fd /*fd*/ ) {
	switch (jsrr.proc) {
		case JobServerRpcProc::Heartbeat :
			if (jsrr.seq_id!=g_seq_id)                                         // report that the job the server tries to connect to no longer exists
				try {
					OMsgBuf().send(
						ClientSockFd( g_service_end , NConnectionTrials )
					,	JobRpcReq( JobProc::End , jsrr.seq_id , jsrr.job , {.status=Status::LateLost} )
					) ;
				} catch (::string const& e) {}                                 // if server is dead, no harm
		break ;
		case JobServerRpcProc::Kill :
			g_killed = true ;
			if (jsrr.seq_id==g_seq_id) kill_job() ;                            // else server is not sending its request to us
		break ;
		default : FAIL(jsrr.proc) ;
	}
	return false ;
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
	ServerThread<JobServerRpcReq> server_thread{'-',handle_server_req} ;
	//
	JobRpcReq req_info   { JobProc::Start , g_seq_id , g_job , server_thread.fd.port()                             } ;
	JobRpcReq end_report { JobProc::End   , g_seq_id , g_job , {.status=Status::EarlyErr,.end_date=start_overhead} } ; // prepare to return an error
	try {
		ClientSockFd fd {g_service_start,NConnectionTrials} ;
		//                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv     // once connection is established, everything should be smooth
		try {                OMsgBuf().send                ( fd , req_info ) ; } catch(::string const&) { exit(3) ; } // maybe normal in case ^C was hit
		try { g_start_info = IMsgBuf().receive<JobRpcReply>( fd            ) ; } catch(::string const&) { exit(4) ; } // .
		//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	} catch (::string const&) { exit(5) ; }                                    // maybe normal in case ^C was hit

	switch (g_start_info.proc) {
		case JobProc::None  : return 0 ;                                       // server ask us to give up
		case JobProc::Start : break    ;                                       // normal case
		default : FAIL(g_start_info.proc) ;
	}

	if (::chdir(g_start_info.autodep_env.root_dir.c_str())!=0) {
		end_report.digest.stderr = to_string("cannot chdir to root : ",g_start_info.autodep_env.root_dir) ;
		goto End ;
	}
	{
		g_trace_file = new ::string{to_string(g_start_info.remote_admin_dir,"/job_trace/",::right,::setfill('0'),::setw(TraceNameSz),g_seq_id%JobHistorySz)} ;
		::unlink(g_trace_file->c_str()) ;                                      // ensure that if another job is running to the same trace, its trace is unlinked to avoid clash
		//
		app_init() ;
		Py::init() ;
		//
		::string cwd_    = g_start_info.cwd_s                ;
		::string abs_cwd = g_start_info.autodep_env.root_dir ;
		if (!g_start_info.cwd_s.empty()) {
			cwd_.pop_back() ;
			append_to_string(abs_cwd,'/',cwd_) ;
		}
		::map_ss cmd_env ;
		cmd_env["PWD"        ] = abs_cwd                           ;
		cmd_env["ROOT_DIR"   ] = g_start_info.autodep_env.root_dir ;
		cmd_env["SEQUENCE_ID"] = to_string(g_seq_id             )  ;
		cmd_env["SMALL_ID"   ] = to_string(g_start_info.small_id)  ;
		for( auto&& [k,v] : g_start_info.env ) {
			if      (v!=EnvPassMrkr) cmd_env[k] = env_decode(::move(v)) ;
			else if (has_env(k)    ) cmd_env[k] = get_env(k)            ;      // if value is special illegal value, use value from environement (typically from slurm)
		}
		if ( g_start_info.keep_tmp || !cmd_env.contains("TMPDIR") )
			cmd_env["TMPDIR"] = mk_abs( g_start_info.autodep_env.tmp_dir , g_start_info.autodep_env.root_dir+'/' ) ; // if we keep tmp, we force the tmp directory
		g_start_info.autodep_env.tmp_dir = cmd_env["TMPDIR"] ;
		if (!g_start_info.autodep_env.tmp_view.empty()) cmd_env["TMPDIR"] = g_start_info.autodep_env.tmp_view ; // job must use the job view
		//
		Trace trace("main",g_service_start,g_service_mngt,g_service_end,g_seq_id,g_job) ;
		trace("start_overhead",start_overhead) ;
		trace("g_start_info"  ,g_start_info  ) ;
		trace("cmd_env"       ,cmd_env       ) ;
		//
		try {
			unlink_inside(g_start_info.autodep_env.tmp_dir) ;                  // be certain that tmp dir is clean
		} catch (::string const&) {
			try {
				make_dir(g_start_info.autodep_env.tmp_dir) ;                   // and that it exists
			} catch (::string const& e) {
				end_report.digest.stderr = "cannot create tmp dir : "+e ;
				goto End ;
			}
		}
		Fd child_stdin  = Child::None ;
		Fd child_stdout = Child::Pipe ;
		//
		::vector_s& cmd_line = g_start_info.interpreter ;                                                            // avoid copying as interpreter is used only here
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
		trace("cmd_line",cmd_line) ;
		//
		::vector<Py::Pattern>  target_patterns ; target_patterns.reserve(g_start_info.targets.size()) ;
		for( VarIdx t=0 ; t<g_start_info.targets.size() ; t++ ) {
			TargetSpec const& tf = g_start_info.targets[t] ;
			if (tf.tflags[Tflag::Star]) target_patterns.emplace_back(tf.pattern) ;
			else                        target_patterns.emplace_back(          ) ;
		}
		//
		::vmap_s<TargetDigest>              targets      ;
		::vmap_s<DepDigest   >              deps         ;
		ThreadQueue<::pair<NodeIdx,string>> crc_queue    ;
		::vector<pair_ss>                   analysis_err ;
		//
		auto analyze = [&](bool at_end)->void {
			trace("analyze",STR(at_end)) ;
			NodeIdx prev_parallel_id = 0 ;
			for( auto const& [file,info] : g_gather_deps.accesses ) {
				JobExecRpcReq::AccessDigest const& ad = info.digest ;
				Accesses                           a  = ad.accesses ;  if (!info.tflags[Tflag::Stat]) a &= ~Access::Stat ;
				try                       { chk(info.tflags) ;                                                          }
				catch (::string const& e) { analysis_err.emplace_back(to_string("bad flags (",e,')'),file) ; continue ; } // dont know what to do with such an access
				if (info.is_dep()) {
					bool      parallel = info.parallel_id && info.parallel_id==prev_parallel_id ;
					DepDigest dd       { a , ad.dflags , parallel }                             ;
					prev_parallel_id = info.parallel_id ;
					if (+a) {
						dd.date(info.file_date) ;
						dd.garbage = file_date(file)!=info.file_date ;         // file date is not coherent from first access to end of job, we do not know what we have read
					}
					//vvvvvvvvvvvvvvvvvvvvvvvv
					deps.emplace_back(file,dd) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^
					trace("dep   ",dd,file) ;
				} else if (at_end) {                                                              // else we are handling chk_deps and we only care about deps
					if ( !info.file_date                                   ) a = Accesses::None ;
					if ( ad.write && !ad.unlink && info.tflags[Tflag::Crc] ) crc_queue.emplace(targets.size(),file) ; // defer crc computation to prepare for // computation
					TargetDigest td{a,ad.write,info.tflags,ad.unlink} ;
					targets.emplace_back( file , td ) ;
					trace("target",td,info.file_date,file) ;
				}
			}
		} ;
		//
		auto server_cb = [&](JobExecRpcReq&& jerr)->Fd/*reply*/ {
			deps.reserve(g_gather_deps.accesses.size()) ;                      // typically most of accesses are deps
			JobRpcReq jrr ;
			switch (jerr.proc) {
				case JobExecRpcProc::ChkDeps :
					analyze(false/*at_end*/) ;
					jrr = JobRpcReq( JobProc::ChkDeps , g_seq_id , g_job , ::move(deps) ) ;
					deps.clear() ;
				break ;
				case JobExecRpcProc::DepInfos : {
					::vmap_s<DepDigest> ds ; ds.reserve(jerr.files.size()) ;
					for( auto&& [dep,date] : jerr.files ) ds.emplace_back( ::move(dep) , DepDigest(jerr.digest.accesses,jerr.digest.dflags,true/*parallel*/,date) ) ;
					jrr = JobRpcReq( JobProc::DepInfos , g_seq_id , g_job , ::move(ds) ) ;
				} break ;
				default : FAIL(jerr.proc) ;
			}
			trace("server_cb",jerr.proc,jrr.digest.deps.size()) ;
			ClientSockFd fd{g_service_mngt} ;
			//    vvvvvvvvvvvvvvvvvvvvvv
			try { OMsgBuf().send(fd,jrr) ; }
			//    ^^^^^^^^^^^^^^^^^^^^^^
			catch (...) { return {} ; }                                        // server is dead, do as if there is no server
			return fd ;
		} ;
		//
		::uset_s static_deps = mk_key_uset(g_start_info.static_deps) ;
		auto     tflags_cb   = [&](::string const& file)->Tflags {
			if (static_deps.contains(file)) return UnexpectedTflags ;
			//
			for( VarIdx t=0 ; t<g_start_info.targets.size() ; t++ ) {
				TargetSpec const& spec = g_start_info.targets[t] ;
				if (spec.tflags[Tflag::Star]) { if (+target_patterns[t].match(file)) return spec.tflags ; }
				else                          { if (file==spec.pattern             ) return spec.tflags ; }
			}
			return UnexpectedTflags ;
		} ;
		//
		::string live_out_buf ;                                                // used to store incomplete last line to have line coherent chunks
		auto live_out_cb = [&](::string_view const& txt)->void {
			// could be slightly optimized, but when generating live output, we have a single job, no need to optimize
			live_out_buf.append(txt) ;
			size_t pos = live_out_buf.rfind('\n') ;
			trace("live_out_cb",live_out_buf.size(),txt.size(),pos+1) ;
			if (pos==Npos) return ;
			pos++ ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send( ClientSockFd(g_service_mngt) , JobRpcReq( JobProc::LiveOut , g_seq_id , g_job , live_out_buf.substr(0,pos) ) ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			live_out_buf = live_out_buf.substr(pos) ;
		} ;
		//
		/**/                       g_gather_deps.addr         = g_start_info.addr        ;
		/**/                       g_gather_deps.autodep_env  = g_start_info.autodep_env ;
		/**/                       g_gather_deps.chroot       = g_start_info.chroot      ;
		/**/                       g_gather_deps.create_group = true                     ;
		/**/                       g_gather_deps.cwd          = cwd_                     ;
		/**/                       g_gather_deps.env          = &cmd_env                 ;
		/**/                       g_gather_deps.kill_sigs    = g_start_info.kill_sigs   ;
		if (g_start_info.live_out) g_gather_deps.live_out_cb  = live_out_cb              ;
		/**/                       g_gather_deps.method       = g_start_info.method      ;
		/**/                       g_gather_deps.server_cb    = server_cb                ;
		/**/                       g_gather_deps.tflags_cb    = tflags_cb                ;
		/**/                       g_gather_deps.timeout      = g_start_info.timeout     ;
		/**/                       g_gather_deps.kill_job_cb  = kill_job                 ;
		//
		g_gather_deps.static_deps( start_overhead , g_start_info.static_deps , g_start_info.stdin ) ; // ensure static deps are generated first
		if (g_start_info.stdin.empty()) child_stdin = open_read("/dev/null"       ) ;
		else                            child_stdin = open_read(g_start_info.stdin) ;
		child_stdin.no_std() ;
		if (!g_start_info.stdout.empty()) {
			child_stdout = open_write(g_start_info.stdout) ;
			g_gather_deps.new_target( start_overhead , g_start_info.stdout , {}/*neg_tflags*/ , {}/*pos_tflags*/ , "<stdout>" ) ;
			child_stdout.no_std() ;
		}
		//
		Pdate start_job = Pdate::s_now() ;                                                              // as late as possible before child starts
		//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		Status status = g_gather_deps.exec_child( cmd_line , child_stdin , child_stdout , Child::Pipe ) ;
		//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		Pdate         end_job = Pdate::s_now() ;                                                        // as early as possible after child ends
		struct rusage rsrcs   ; getrusage(RUSAGE_CHILDREN,&rsrcs) ;
		trace("start_job",start_job,"end_job",end_job) ;
		//
		analyze(true/*at_end*/) ;
		//
		ThreadQueue<::string> spurious_unlink_queue  ;
		auto crc_thread_func = [&](size_t id) -> void {
			Trace::t_key =
				id<10 ? '0'+id    :
				id<36 ? 'a'+id-10 :
				id<62 ? 'A'+id-36 :
				/**/    '>'
			;
			for(;;) {
				auto [popped,crc_spec] = crc_queue.try_pop() ;
				if (!popped) return ;
				Crc crc{ crc_spec.second , g_start_info.hash_algo } ;
				if (crc==Crc::None) spurious_unlink_queue.push(crc_spec.second) ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				targets[crc_spec.first].second.crc = crc ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				trace("crc",id,crc,targets[crc_spec.first].first) ;
			}
		} ;
		{	size_t            n_threads   = ::min( size_t(::max(1u,thread::hardware_concurrency())) , crc_queue.size() ) ;
			::vector<jthread> crc_threads ; crc_threads.reserve(n_threads) ;
			for( size_t i=0 ; i<n_threads ; i++ ) crc_threads.emplace_back(crc_thread_func,i) ; // just constructing a destructing the threads will execute & join them, resulting in computed crc's
		}
		while (!spurious_unlink_queue.empty()) analysis_err.emplace_back("target was spuriously unlinked :",spurious_unlink_queue.pop()) ;
		//
		if ( g_gather_deps.seen_tmp && !g_start_info.keep_tmp )
			try                     { unlink_inside(g_start_info.autodep_env.tmp_dir) ; }
			catch (::string const&) {}                                                    // cleaning is done at job start any way, so no harm
		//
		if      (!analysis_err.empty()) status = Status::Err    ;
		else if (g_killed             ) status = Status::Killed ;
		trace("status",status) ;
		end_report.digest = {
			.status       = status
		,	.targets      { ::move(targets             ) }
		,	.deps         { ::move(deps                ) }
		,	.analysis_err { ::move(analysis_err        ) }
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
	Pdate end_overhead = Pdate::s_now() ;
	Trace trace("end",end_overhead) ;
	end_report.digest.stats.total = end_overhead - start_overhead ;            // measure overhead as late as possible
	try {
		ClientSockFd fd {g_service_end,NConnectionTrials} ;
		//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		try { OMsgBuf().send( fd , end_report ) ; }
		//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		catch(::string const& e) { exit(2,"cannot send end report to server : ",e) ; }
	} catch (::string const& e) { exit(2,"after job execution : ",e) ; }
	//
	trace("done") ;
	return 0 ;
}
