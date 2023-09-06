// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "app.hh"
#include "disk.hh"
#include "hash.hh"
#include "pycxx.hh"
#include "rpc_job.hh"
#include "trace.hh"

#include "autodep/gather_deps.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

static constexpr uint8_t TraceNameSz = JobHistorySz<= 10 ? 1 : JobHistorySz<=100 ? 2 : 3 ;
static_assert(JobHistorySz<=10000) ;                                                       // above, we need to make hierarchical names

int main( int argc , char* argv[] ) {
	using Date = ProcessDate ;

	static JobRpcReply start_info  ;                                           // start_info is made static as it holds g_tmp_dir, to avoid pointing to deallocated memory

	Date start_overhead = Date::s_now() ;
	//
	block_sig(SIGCHLD) ;
	swear_prod(argc==5,argc) ;                                                 // syntax is : job_exec server:port seq_id job_idx is_remote
	::string service   =      argv[1]         ;
	SeqId    seq_id    = atol(argv[2])        ;
	JobIdx   job       = atol(argv[3])        ;
	bool     is_remote = argv[4]=="remote"s   ; if (!is_remote) SWEAR(argv[4]=="local"s) ;
	::string host_     = is_remote?host():""s ;
	//
	GatherDeps gather_deps{ New }                                                                    ;
	JobRpcReq req_info    { JobProc::Start , seq_id , job , host_ , gather_deps.master_sock.port() } ;
	try {
		ClientSockFd fd{service} ;
		//           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		/**/         OMsgBuf().send                ( fd , req_info ) ;
		start_info = IMsgBuf().receive<JobRpcReply>( fd            ) ;
		//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	} catch(...) { return 2 ; }                                                // if server is dead, give up

	if (::chdir(start_info.autodep_env.root_dir.c_str())!=0) {
		JobRpcReq end_report = JobRpcReq(
			JobProc::End
		,	seq_id
		,	job
		,	host_
		,	{ .status =Status::Err , .stderr {to_string("cannot chdir to root : ",start_info.autodep_env.root_dir)} }
		) ;
		try         { OMsgBuf().send( ClientSockFd(service) , end_report ) ; }
		catch (...) {                                                        } // if server is dead, we cant do much about it
		return 2 ;
	}

	g_trace_file = new ::string{to_string(start_info.remote_admin_dir,"/job_trace/",::right,::setfill('0'),::setw(TraceNameSz),seq_id%JobHistorySz)} ;
	//
	::unlink(g_trace_file->c_str()) ;                                          // ensure that if another job is running to the same trace, its trace is unlinked to avoid clash
	//
	// set g_tmp_dir before calling app_init so it does not search TMPDIR env variable
	if (is_abs(start_info.job_tmp_dir)) g_tmp_dir = &start_info.job_tmp_dir                                                             ;
	else                                g_tmp_dir = new ::string{to_string(start_info.autodep_env.root_dir,'/',start_info.job_tmp_dir)} ;
	//
	app_init() ;                                                               // safer to call app_init once we are in repo
	Py::init() ;

	Trace trace("main",service,seq_id,job) ;
	trace("start_overhead",start_overhead) ;
	trace(start_info) ;
	//
	try                     { unlink_inside(*g_tmp_dir) ; }                    // be certain that tmp dir is clean
	catch (::string const&) { make_dir     (*g_tmp_dir) ; }                    // and that it exists
	//
	::string cwd_    = start_info.cwd_s ;
	::string abs_cwd = *g_root_dir      ;
	if (!start_info.cwd_s.empty()) {
		cwd_.pop_back() ;
		append_to_string(abs_cwd,'/',cwd_) ;
	}
	::map_ss cmd_env ;
	/**/                                      cmd_env["PWD"        ] =           abs_cwd                          ;
	/**/                                      cmd_env["ROOT_DIR"   ] =           *g_root_dir                      ;
	/**/                                      cmd_env["SEQUENCE_ID"] = to_string(seq_id             )             ;
	/**/                                      cmd_env["SMALL_ID"   ] = to_string(start_info.small_id)             ;
	/**/                                      cmd_env["TMPDIR"     ] =           *g_tmp_dir                       ; // TMPDIR is the standard environment variable to specify the temporary area
	for( auto const& [k,v] : start_info.env ) cmd_env[k            ] = glb_subst(v,start_info.local_mrkr,abs_cwd) ;
	//
	Fd       child_stdin  = Child::None                         ;
	Fd       child_stdout = Child::Pipe                         ;
	::uset_s static_deps  = mk_key_uset(start_info.static_deps) ;                           // copy to uset before moving with new_deps
	gather_deps.new_static_deps( start_overhead , start_info.static_deps , "static_dep" ) ; // ensure static deps are generated first
	if (!start_info.stdin.empty()) {
		child_stdin = open_read(start_info.stdin ) ;
		child_stdin.no_std() ;
		DiskDate dd = file_date(start_info.stdin) ;
		gather_deps.new_dep( start_overhead , start_info.stdin , dd , DFlag::Reg , "<stdin>" ) ;
	}
	if (!start_info.stdout.empty()) {
		child_stdout = open_write(start_info.stdout) ;
		child_stdout.no_std() ;
		gather_deps.new_target( start_overhead , start_info.stdout , TFlags() , TFlags() , "<stdout>" ) ;
	}
	//
	::vector_s args = start_info.interpreter ; args.reserve(args.size()+2) ;
	args.emplace_back("-c"          ) ;
	args.push_back   (start_info.cmd) ;
	//
	::vector<Py::Pattern>  target_patterns ; target_patterns.reserve(start_info.targets.size()) ;
	for( VarIdx t=0 ; t<start_info.targets.size() ; t++ ) {
		TargetSpec const& tf = start_info.targets[t] ;
		if (tf.flags[TFlag::Star]) target_patterns.emplace_back(tf.pattern) ;
		else                       target_patterns.emplace_back(          ) ;
	}
	//
	::vmap_s<TargetDigest>              targets     ;
	::vmap_s<DepDigest   >              deps        ;
	ThreadQueue<::pair<NodeIdx,string>> crc_queue   ;
	::string                            err_str     ;
	//
	auto analyze = [&](bool at_end)->void {
		trace("analyze",STR(at_end)) ;
		NodeIdx prev_parallel_id = 0 ;
		for( auto const& [file,info] : gather_deps.accesses ) {
			TFlags                           tfs = UnexpectedTFlags ;
			JobExecRpcReq::AccessInfo const& ai  = info.info        ;
			if (!static_deps.contains(file)) {
				for( VarIdx t=0 ; t<start_info.targets.size() ; t++ ) {
					TargetSpec const& spec = start_info.targets[t] ;
					if (spec.flags[TFlag::Star]) { if (+target_patterns[t].match(file)) { tfs = spec.flags ; break ; } }
					else                         { if (file==spec.pattern             ) { tfs = spec.flags ; break ; } }
				}
				try {
					TFlags tfs_cooked = (tfs&~ai.neg_tfs)|ai.pos_tfs ;
					chk(tfs_cooked) ;
					tfs = tfs_cooked ;
				} catch(::string const& e) {
					append_to_string( err_str , "bad flags for ",file," : ",e,'\n' ) ;
				}
			}
			DFlags dfs = ai.dfs ; if (!tfs[TFlag::Stat]) dfs &= ~DFlag::Stat ;
			if ( ai.idle() && tfs[TFlag::Dep] ) {
				bool      parallel = info.parallel_id && info.parallel_id==prev_parallel_id ;
				DepDigest dd       { dfs , parallel }                                       ;
				prev_parallel_id = info.parallel_id ;
				if (+(dfs&AccessDFlags)) {
					dd.date(info.file_date) ;
					dd.garbage = file_date(file)!=info.file_date ;             // file date is not coherent from first access to end of job, we do not know what we have read
if (dd.garbage) ::cerr<<"analyze1 "<<dd<<" "<<file<<" "<<file_date(file)<<" "<<info.file_date<<endl;
				}
				//vvvvvvvvvvvvvvvvvvvvvvvv
				deps.emplace_back(file,dd) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^
				trace("dep   ",dd,tfs,file) ;
			} else if (at_end) {                                                      // else we are handling chk_deps and we only care about deps
				if ( !info.file_date                           ) dfs = DFlags::None ;
				if ( ai.write && !ai.unlink && tfs[TFlag::Crc] ) crc_queue.emplace(targets.size(),file) ; // defer crc computation to prepare for // computation
				TargetDigest td{dfs,ai.write,tfs,ai.unlink} ;
				targets.emplace_back( file , td ) ;
				trace("target",td,info.file_date,file) ;
			}
		}
	} ;
	//
	auto server_cb = [&](JobExecRpcReq&& jerr)->Fd/*reply*/ {
		deps.reserve(gather_deps.accesses.size()) ;                            // typically most of accesses are deps
		JobRpcReq jrr ;
		switch (jerr.proc) {
			case JobExecRpcProc::ChkDeps :
				analyze(false/*at_end*/) ;
				jrr = JobRpcReq( JobProc::ChkDeps , seq_id , job , host_ , ::move(deps) ) ;
				deps.clear() ;
			break ;
			case JobExecRpcProc::DepInfos : {
				::vmap_s<DepDigest> ds ; ds.reserve(jerr.files.size()) ;
				for( auto&& [dep,date] : jerr.files ) ds.emplace_back( ::move(dep) , DepDigest(jerr.info.dfs,true/*parallel*/,date) ) ;
				jrr = JobRpcReq( JobProc::DepInfos , seq_id , job , host_ , ::move(ds) ) ;
			} break ;
			default : FAIL(jerr.proc) ;
		}
		trace("server_cb",jerr.proc,jrr.digest.deps.size()) ;
		ClientSockFd fd{service} ;
		//    vvvvvvvvvvvvvvvvvvvvvv
		try { OMsgBuf().send(fd,jrr) ; }
		//    ^^^^^^^^^^^^^^^^^^^^^^
		catch (...) { return {} ; }                                            // server is dead, do as if there is no server
		return fd ;
	} ;
	::string live_out_buf ;                                                    // used to store incomplete last line to have line coherent chunks
	auto live_out_cb = [&](::string_view const& txt)->void {
		// could be slightly optimized, but when generating live output, we have a single job, no need to optimize
		live_out_buf.append(txt) ;
		size_t pos = live_out_buf.rfind('\n') ;
		if (pos==Npos) return ;
		pos++ ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		OMsgBuf().send( ClientSockFd(service) , JobRpcReq( JobProc::LiveOut , seq_id , job , host_ , live_out_buf.substr(0,pos) ) ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		live_out_buf = live_out_buf.substr(pos) ;
	} ;
	/**/                     gather_deps.create_group = true                   ;
	/**/                     gather_deps.method       = start_info.method      ;
	/**/                     gather_deps.addr         = start_info.addr        ;
	/**/                     gather_deps.autodep_env  = start_info.autodep_env ;
	/**/                     gather_deps.server_cb    = server_cb              ;
	/**/                     gather_deps.timeout      = start_info.timeout     ;
	/**/                     gather_deps.kill_sigs    = start_info.kill_sigs   ;
	/**/                     gather_deps.chroot       = start_info.chroot      ;
	/**/                     gather_deps.cwd          = cwd_                   ;
	/**/                     gather_deps.env          = &cmd_env               ;
	if (start_info.live_out) gather_deps.live_out_cb  = live_out_cb            ;
	//
	Date start_job = Date::s_now() ;                                                          // as late as possible before child starts
	//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Status status = gather_deps.exec_child( args , child_stdin , child_stdout , Child::Pipe ) ;
	//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	Date          end_job = Date::s_now() ;                                                   // as early as possible after child ends
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
			Crc crc{ crc_spec.second , start_info.hash_algo } ;
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
	if (!spurious_unlink_queue.empty()) {
		append_to_string(err_str,"some targets were spuriously unlinked :\n") ;
		while (!spurious_unlink_queue.empty()) append_to_string(err_str,'\t',spurious_unlink_queue.pop(),'\n') ;
	}
	//
	if ( gather_deps.seen_tmp && !start_info.keep_tmp ) {
		trace("tmp",*g_tmp_dir) ;
		unlink_inside(*g_tmp_dir) ;
	}
	Date end_overhead = Date::s_now() ;
	trace("end_overhead",end_overhead) ;
	//
	if (!err_str.empty()) {
		status             |= Status::Err                          ;
		gather_deps.stderr  = err_str + ::move(gather_deps.stderr) ;
	}
	JobRpcReq end_report{
		JobProc::End
	,	seq_id
	,	job
	,	host_
	,	{
			.status   = status
		,	.targets  { targets                    }
		,	.deps     { deps                       }
		,	.stderr   { ::move(gather_deps.stderr) }
		,	.stdout   { ::move(gather_deps.stdout) }
		,	.wstatus  = gather_deps.wstatus
		,	.end_date = end_job
		,	.stats{
				.cpu  { Delay(rsrcs.ru_utime) + Delay(rsrcs.ru_stime) }
			,	.job  { end_job      - start_job                      }
			,	.total{ end_overhead - start_overhead                 }        // measure overhead as late as possible
			,	.mem  = size_t(rsrcs.ru_maxrss<<10)
			}
		}
	} ;
	//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	try { OMsgBuf().send(ClientSockFd(service),end_report) ; }
	//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	catch (...) { return 2 ; }                                                 // if server is dead, we cant do much about it
	//
	trace("end",status) ;
	return 0 ;
}
