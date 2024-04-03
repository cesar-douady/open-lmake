// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/resource.h>
#include <linux/limits.h> // ARG_MAX

#include "app.hh"
#include "disk.hh"
#include "fd.hh"
#include "hash.hh"
#include "re.hh"
#include "rpc_job.hh"
#include "thread.hh"
#include "time.hh"
#include "trace.hh"

#include "autodep/gather.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Re   ;
using namespace Time ;

static constexpr int NConnectionTrials = 3 ; // number of times to try connect when connecting to server

struct PatternDict {
	static constexpr MatchFlags NotFound = {} ;
	// services
	MatchFlags const& at(::string const& x) const {
		if ( auto it=knowns.find(x) ; it!=knowns.end() )     return it->second ;
		for( auto const& [p,r] : patterns ) if (+p.match(x)) return r          ;
		/**/                                                 return NotFound   ;
	}
	void add( bool star , ::string const& key , MatchFlags const& val ) {
		if (star) patterns.emplace_back( RegExpr(key,true/*fast*/,true/*no_group*/) , val ) ;
		else      knowns  .emplace     (         key                                , val ) ;
	}
	// data
	::umap_s<MatchFlags>       knowns   = {} ;
	::vmap<RegExpr,MatchFlags> patterns = {} ;
} ;

ServerSockFd   g_server_fd     ;
Gather         g_gather        { New }        ;
JobRpcReply    g_start_info    ;
::string       g_service_start ;
::string       g_service_mngt  ;
::string       g_service_end   ;
SeqId          g_seq_id        = 0/*garbage*/ ;
JobIdx         g_job           = 0/*garbage*/ ;
::atomic<bool> g_killed        = false        ; // written by thread S and read by main thread
PatternDict    g_match_dct     ;
NfsGuard       g_nfs_guard     ;
::vector_s     g_washed        ;

void kill_thread_func(::stop_token stop) {
	t_thread_key = 'K' ;
	Trace trace("kill_thread_func",g_start_info.kill_sigs) ;
	for( size_t i=0 ;; i++ ) {
		int sig = i<g_start_info.kill_sigs.size() ? g_start_info.kill_sigs[i] : -1 ; // -1 means best effort
		trace("sig",sig) ;
		g_gather.kill(sig) ;
		if (!Delay(1.).sleep_for(stop)) {
			trace("done") ;
			return ;                                                                 // job_exec has ended
		}
	}
}

void kill_job() {
	Trace trace("kill_job") ;
	static ::jthread kill_thread{kill_thread_func} ; // launch job killing procedure while continuing to behave normally
}

bool/*keep_fd*/ handle_server_req( JobServerRpcReq&& jsrr , SlaveSockFd const& ) {
	Trace trace("handle_server_req",jsrr) ;
	switch (jsrr.proc) {
		case JobServerRpcProc::Heartbeat :
			if (jsrr.seq_id!=g_seq_id) {               // report that the job the server tries to connect to no longer exists
				trace("bad_id",g_seq_id,jsrr.seq_id) ;
				try {
					OMsgBuf().send(
						ClientSockFd( g_service_end , NConnectionTrials )
					,	JobRpcReq( JobProc::End , jsrr.seq_id , jsrr.job , {.status=Status::LateLost} )
					) ;
				} catch (::string const& e) {}         // if server is dead, no harm
			}
		break ;
		case JobServerRpcProc::Kill :
			g_killed = true ;
			if (jsrr.seq_id==g_seq_id) kill_job() ;    // else server is not sending its request to us
		break ;
	DF}
	return false ;
}

::map_ss prepare_env(JobRpcReq& end_report) {
	::map_ss res ;
	::string abs_cwd = g_start_info.autodep_env.root_dir ;
	if (+g_start_info.cwd_s) {
		append_to_string(abs_cwd,'/',g_start_info.cwd_s) ; abs_cwd.pop_back() ;
	}
	res["PWD"        ] = abs_cwd                           ;
	res["ROOT_DIR"   ] = g_start_info.autodep_env.root_dir ;
	res["SEQUENCE_ID"] = to_string(g_seq_id             )  ;
	res["SMALL_ID"   ] = to_string(g_start_info.small_id)  ;
	for( auto& [k,v] : g_start_info.env ) {
		if (v!=EnvPassMrkr) {
			res[k] = env_decode(::move(v)) ;
		} else if (has_env(k)) {                                                                             // if value is special illegal value, use value from environement (typically from slurm)
			::string v = get_env(k) ;
			end_report.dynamic_env.emplace_back(k,env_encode(::copy(v))) ;
			res[k] =                                         ::move(v)   ;
		}
	}
	if ( g_start_info.keep_tmp || !res.contains("TMPDIR") )
		res["TMPDIR"] = mk_abs( g_start_info.autodep_env.tmp_dir , g_start_info.autodep_env.root_dir+'/' ) ; // if we keep tmp, we force the tmp directory
	g_start_info.autodep_env.tmp_dir = res["TMPDIR"] ;
	if (+g_start_info.autodep_env.tmp_view) res["TMPDIR"] = g_start_info.autodep_env.tmp_view ;              // job must use the job view
	//
	Trace trace("prepare_env",res) ;
	//
	try {
		unlnk_inside(g_start_info.autodep_env.tmp_dir) ;                                                     // ensure tmp dir is clean
	} catch (::string const&) {
		try                       { mkdir(g_start_info.autodep_env.tmp_dir) ; }                              // ensure tmp dir exists
		catch (::string const& e) { throw "cannot create tmp dir : "+e ;      }
	}
	return res ;
}

struct Digest {
	::vmap_s<TargetDigest> targets ;
	::vmap_s<DepDigest   > deps    ;
	::vector<NodeIdx     > crcs    ; // index in targets of entry for which we need to compute a crc
	::string               msg     ;
} ;

Digest analyze( bool at_end , bool killed=false ) {
	Trace trace("analyze",STR(at_end),g_gather.accesses.size()) ;
	Digest  res              ; res.deps.reserve(g_gather.accesses.size()) ;                    // typically most of accesses are deps
	NodeIdx prev_parallel_id = 0                                     ;
	Pdate   relax            = Pdate(New)+g_start_info.network_delay ;
	//
	for( auto const& [file,info] : g_gather.accesses ) {
		MatchFlags   flags = g_match_dct.at(file) ;
		AccessDigest ad    = info.digest          ;
		switch (flags.is_target) {
			// if Ignore is in flags, it is there since the beginning
			case Yes   : ad.tflags |= flags.tflags() ; ad.extra_tflags |= flags.extra_tflags() ; if (flags.extra_tflags()[ExtraTflag::Ignore]) { ad.accesses = {} ; ad.write = No ; } break ;
			case No    : ad.dflags |= flags.dflags() ; ad.extra_dflags |= flags.extra_dflags() ; if (flags.extra_dflags()[ExtraDflag::Ignore])   ad.accesses = {} ;                   break ;
			case Maybe :                                                                       ;                                                                                      break ;
		DF}
		//
		if (ad.write==Yes)                                                                     // ignore reads after earliest confirmed write
			for( Access a : All<Access> )
				if (info.read[+a]>info.write) ad.accesses &= ~a ;
		bool is_dep = ad.dflags[Dflag::Static] ;
		if (flags.is_target!=Yes)                                                              // if a target or a side target, it is a target since the beginning
			for( Access a : All<Access> )
				if ( ad.accesses[a] && info.read[+a]<=info.target ) { is_dep = true ; break ; }
		bool is_tgt =
			ad.write!=No
		||	(	(  flags.is_target==Yes || info.target!=Pdate::Future         )
			&&	!( !ad.tflags[Tflag::Target] && ad.tflags[Tflag::Incremental] )                // fast path : no matching, no pollution, no washing => forget it
			)
		;
		// handle deps
		if (is_dep) {
			DepDigest dd { ad.accesses , info.crc_date , ad.dflags } ;
			//
			if ( ad.accesses[Access::Stat] && ad.extra_dflags[ExtraDflag::StatReadData] ) dd.accesses = ~Accesses() ;
			//
			dd.parallel      = info.parallel_id && info.parallel_id==prev_parallel_id ;
			prev_parallel_id = info.parallel_id                                       ;
			//
			if ( +dd.accesses && dd.is_date ) {                                                // try to transform date into crc as far as possible
				if      ( info.seen==Pdate::Future||info.seen>info.write ) dd.crc(Crc::None) ; // the whole job has been executed without seeing the file (before possibly writing to it)
				else if ( !dd.date()                                     ) dd.crc({}       ) ; // file was not present initially but was seen, it is incoherent even if not present finally
				else if ( ad.write!=No                                   ) {}                  // cannot check stability as we wrote to it, clash will be detecte in server if any
				else if ( FileInfo dfi{file} ; dfi.date!=dd.date()       ) dd.crc({}       ) ; // file dates are incoherent from first access to end of job, dont know what has been read
				else if ( !dfi                                           ) dd.crc({}       ) ; // file is awkward
				else if ( !Crc::s_sense(dd.accesses,dfi.tag())           ) dd.crc(dfi.tag()) ; // just record the tag if enough to match (e.g. accesses==Lnk and tag==Reg)
			}
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.deps.emplace_back(file,dd) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("dep   ",dd,flags,file) ;
		}
		if (!at_end) continue ;                                                                // we are handling chk_deps and we only care about deps
		// handle targets
		if (is_tgt) {
			bool         unlnk = !is_target(file)                                    ;
			TargetDigest td    { .tflags=ad.tflags , .extra_tflags=ad.extra_tflags } ;
			//
			if (is_dep                        ) td.tflags   |= Tflag::Incremental              ;                         // if is_dep, previous target state is guaranteed by being a dep, use it
			if (!td.tflags[Tflag::Incremental]) td.polluted  = info.crc_date.seen(ad.accesses) ;                         // polluted means that target was seen as existing before execution
			if ( is_dep && !unlnk ) {
				 trace("dep_and_target",ad,flags) ;
				 append_to_string( res.msg , "read as dep before being known to be a target : " , mk_file(file) ,'\n') ;
				 ad.tflags |= Tflag::Incremental ;                                                                       // file will have a predictible content, no reason to wash it
			} else switch (flags.is_target) {
				case Yes   : break ;
				case Maybe :
					if (unlnk) break ;                               // it is ok to write and unlink temporary files
				[[fallthrough]] ;
				case No :
					if (ad.write==No                      ) break ;  // it is ok to attempt writing as long as attempt does not succeed
					if (ad.extra_tflags[ExtraTflag::Allow]) break ;  // it is ok if explicitly allowed by user
					trace("bad access",ad,flags) ;
					if (ad.write==Maybe    ) append_to_string( res.msg , "maybe "                      ) ;
					/**/                     append_to_string( res.msg , "unexpected "                 ) ;
					                         append_to_string( res.msg , unlnk?"unlink ":"write to "   ) ;
					if (flags.is_target==No) append_to_string( res.msg , "dep "                        ) ;
					/**/                     append_to_string( res.msg , mk_file(file,No|!unlnk) , '\n') ;
				break ;
			}
			switch (ad.write) {
				case No    : break ;
				// /!\ if a write is interrupted, it may continue past the end of the process when accessing a network disk
				case Maybe : relax.sleep_until() ; [[fallthrough]] ; // no need to optimize (could compute other crcs while waiting) as this is exceptional
				case Yes   :
					if      ( unlnk                               )   td.crc = Crc::None ;
					else if ( killed || !td.tflags[Tflag::Target] ) { FileInfo fi{file} ; td.crc = Crc(fi.tag()) ; td.date = fi.date ; } // no crc if meaningless
					else                                              res.crcs.emplace_back(res.targets.size()) ; // record index in res.targets for deferred (parallel) crc computation
				break ;
			DF}
			if ( td.tflags[Tflag::Target] && !td.tflags[Tflag::Phony] ) {
				if (td.tflags[Tflag::Static]) {
					if (unlnk        ) append_to_string( res.msg , "missing static target " , mk_file(file,No/*exists*/) , '\n' ) ;
				} else {
					if (ad.write==Yes) { if (unlnk           ) td.tflags &= ~Tflag::Target ; }                    // unless static or phony, a target loses its official status if not actually produced
					else               { if (!is_target(file)) td.tflags &= ~Tflag::Target ; }
				}
			}
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.targets.emplace_back(file,td) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("target",ad,td,STR(unlnk),file) ;
		}
	}
	for( ::string const& t : g_washed ) if (!g_gather.access_map.contains(t)) {
		trace("wash",t) ;
		res.targets.emplace_back( t , TargetDigest{.extra_tflags=ExtraTflag::Wash,.crc=Crc::None} ) ;
	}
	trace("done",res.deps.size(),res.targets.size(),res.crcs.size(),res.msg) ;
	return res ;
}

Fd/*reply*/ server_cb(JobExecRpcReq&& jerr) {
	JobRpcReq jrr ;
	if (jerr.proc==JobExecRpcProc::ChkDeps) jrr = JobRpcReq( JobProc::ChkDeps , g_seq_id , g_job , analyze(false/*at_end*/).deps ) ;
	else                                    jrr = JobRpcReq(                    g_seq_id , g_job , ::move(jerr)                  ) ;
	Trace trace("server_cb",jerr.proc,jrr.digest.deps.size()) ;
	ClientSockFd fd{g_service_mngt} ;
	//    vvvvvvvvvvvvvvvvvvvvvv
	try { OMsgBuf().send(fd,jrr) ; }
	//    ^^^^^^^^^^^^^^^^^^^^^^
	catch (...) { return {} ; } // server is dead, do as if there is no server
	return fd ;
}

::vector_s cmd_line() {
	::vector_s cmd_line = ::move(g_start_info.interpreter) ;                                                     // avoid copying as interpreter is used only here
	if ( g_start_info.use_script || (g_start_info.cmd.first.size()+g_start_info.cmd.second.size())>ARG_MAX/2 ) { // env+cmd line must not be larger than ARG_MAX, keep some margin for env
		::string cmd_file = to_string(g_start_info.remote_admin_dir,"/job_cmds/",g_start_info.small_id) ;
		OFStream(dir_guard(cmd_file)) << g_start_info.cmd.first << g_start_info.cmd.second ;
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
	static ::string live_out_buf ;           // used to store incomplete last line to have line coherent chunks
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

void crc_thread_func( size_t id , vmap_s<TargetDigest>* targets , ::vector<NodeIdx> const* crcs , ::string* msg , Mutex<MutexLvl::JobExec>* msg_mutex ) {
	static ::atomic<NodeIdx> crc_idx = 0 ;
	t_thread_key = '0'+id ;
	Trace trace("crc",targets->size(),crcs->size()) ;
	NodeIdx cnt = 0 ;                                           // cnt is for trace only
	for( NodeIdx ci=0 ; (ci=crc_idx++)<crcs->size() ; cnt++ ) {
		::pair_s<TargetDigest>& e      = (*targets)[(*crcs)[ci]] ;
		Pdate                   before = New                     ;
		e.second.crc = Crc( e.second.date/*out*/ , e.first , g_start_info.hash_algo ) ;
		trace("crc_date",ci,before,Pdate(New)-before,e.second.crc,e.second.date,e.first) ;
		if (!e.second.crc.valid()) {
			Lock lock{*msg_mutex} ;
			append_to_string(*msg,"cannot compute crc for ",e.first) ;
		}
	}
	trace("done",cnt) ;
}

::string compute_crcs(Digest& digest) {
	size_t                            n_threads = thread::hardware_concurrency() ;
	if (n_threads<1                 ) n_threads = 1                              ;
	if (n_threads>8                 ) n_threads = 8                              ;
	if (n_threads>digest.crcs.size()) n_threads = digest.crcs.size()             ;
	//
	Trace trace("compute_crcs",digest.crcs.size(),n_threads) ;
	::string msg       ;
	Mutex<MutexLvl::JobExec> msg_mutex ;
	{	::vector<jthread> crc_threads ; crc_threads.reserve(n_threads) ;
		for( size_t i=0 ; i<n_threads ; i++ )
			crc_threads.emplace_back( crc_thread_func , i , &digest.targets , &digest.crcs , &msg , &msg_mutex ) ; // just constructing and destructing the threads will execute & join them
	}
	return msg ;
}

int main( int argc , char* argv[] ) {
	Pdate start_overhead = Pdate(New) ;
	//
	swear_prod(argc==8,argc) ;                       // syntax is : job_exec server:port/*start*/ server:port/*mngt*/ server:port/*end*/ seq_id job_idx root_dir trace_file
	g_service_start =                     argv[1]  ;
	g_service_mngt  =                     argv[2]  ;
	g_service_end   =                     argv[3]  ;
	g_seq_id        = from_string<SeqId >(argv[4]) ;
	g_job           = from_string<JobIdx>(argv[5]) ;
	g_root_dir      = new ::string       (argv[6]) ; // passed early so we can chdir and trace early
	g_trace_file    = new ::string       (argv[7]) ; // .
	//
	JobRpcReq end_report { JobProc::End , g_seq_id , g_job , {.status=Status::EarlyErr,.end_date=start_overhead} } ; // prepare to return an error, so we can goto End anytime
	//
	if (::chdir(g_root_dir->c_str())!=0) {
		append_to_string(end_report.msg,"cannot chdir to root : ",*g_root_dir) ;
		goto End ;
	}
	Trace::s_sz = 10<<20 ;        // this is more than enough
	unlnk(*g_trace_file) ;        // ensure that if another job is running to the same trace, its trace is unlinked to avoid clash
	app_init(No/*chk_version*/) ;
	{
		Trace trace("main",Pdate(New),::vector_view(argv,8)) ;
		trace("pid",::getpid(),::getpgrp()) ;
		trace("start_overhead",start_overhead) ;
		//
		ServerThread<JobServerRpcReq> server_thread{'S',handle_server_req} ;
		//
		JobRpcReq req_info     { JobProc::Start , g_seq_id , g_job , server_thread.fd.port() } ;
		bool      found_server = false                                                         ;
		try {
			ClientSockFd fd {g_service_start,NConnectionTrials} ;
			found_server = true ;
			//             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			/**/           OMsgBuf().send                ( fd , req_info ) ;
			g_start_info = IMsgBuf().receive<JobRpcReply>( fd            ) ;
			//             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			trace("no_server",g_service_start,STR(found_server),e) ;
			if (found_server) exit(Rc::Fail                                                          ) ; // this is typically a ^C
			else              exit(Rc::Fail,"cannot communicate with server ",g_service_start," : ",e) ; // this may be a server config problem, better to report
		}
		trace("g_start_info"  ,g_start_info  ) ;
		if (!g_start_info.proc) return 0 ;                                                               // silently exit if told to do so
		g_nfs_guard.reliable_dirs = g_start_info.autodep_env.reliable_dirs ;
		//
		switch (g_start_info.proc) {
			case JobProc::None  : return 0 ;                          // server ask us to give up
			case JobProc::Start : break    ;                          // normal case
		DF}
		//
		for( auto const& [d ,digest] : g_start_info.deps           ) if (digest.dflags[Dflag::Static]) g_match_dct.add( false/*star*/ , d  , digest.dflags ) ;
		for( auto const& [dt,mf    ] : g_start_info.static_matches )                                   g_match_dct.add( false/*star*/ , dt , mf            ) ;
		for( auto const& [p ,mf    ] : g_start_info.star_matches   )                                   g_match_dct.add( true /*star*/ , p  , mf            ) ;
		//
		::map_ss cmd_env ;
		try                       { cmd_env = prepare_env(end_report) ; }
		catch (::string const& e) { end_report.msg += e ; goto End ;    }
		//
		/**/                       g_gather.addr         = g_start_info.addr        ;
		/**/                       g_gather.autodep_env  = g_start_info.autodep_env ;
		/**/                       g_gather.chroot       = g_start_info.chroot      ;
		/**/                       g_gather.as_session   = true                     ;
		/**/                       g_gather.cwd          = g_start_info.cwd_s       ; if (+g_gather.cwd) g_gather.cwd.pop_back() ;
		/**/                       g_gather.env          = &cmd_env                 ;
		/**/                       g_gather.kill_sigs    = g_start_info.kill_sigs   ;
		if (g_start_info.live_out) g_gather.live_out_cb  = live_out_cb              ;
		/**/                       g_gather.method       = g_start_info.method      ;
		/**/                       g_gather.server_cb    = server_cb                ;
		/**/                       g_gather.timeout      = g_start_info.timeout     ;
		/**/                       g_gather.kill_job_cb  = kill_job                 ;
		//
		trace("wash",g_start_info.pre_actions) ;
		::pair_s<bool/*ok*/> wash_report = do_file_actions( g_washed , ::move(g_start_info.pre_actions) , g_nfs_guard , g_start_info.hash_algo ) ;
		end_report.msg += wash_report.first ;
		if (!wash_report.second) { end_report.digest.status = Status::LateLostErr ; goto End ; }
		g_gather.new_deps( start_overhead , ::move(g_start_info.deps) , g_start_info.stdin ) ;
		for( auto const& [t,f] : g_match_dct.knowns ) if (f.is_target==Yes) g_gather.new_unlnk(start_overhead,t) ;
		//
		Fd child_stdin ;
		if (+g_start_info.stdin) child_stdin = open_read(g_start_info.stdin) ;
		else                     child_stdin = open_read("/dev/null"       ) ;
		child_stdin.no_std() ;
		Fd child_stdout = Child::Pipe ;
		if (+g_start_info.stdout) {
			child_stdout = open_write(g_start_info.stdout) ;
			g_gather.new_target( start_overhead , g_start_info.stdout , "<stdout>" ) ;
			child_stdout.no_std() ;
		}
		//
		Pdate         start_job = New ;                               // as late as possible before child starts
		//                        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		Status        status    = g_gather.exec_child( cmd_line() , child_stdin , child_stdout , Child::Pipe ) ;
		//                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		bool          killed    = g_killed ;                          // sample g_killed to ensure coherence (even if status is correct, it may mean we were waiting for stdout/stderr)
		Pdate         end_job   = New      ;                          // as early as possible after child ends
		struct rusage rsrcs     ; getrusage(RUSAGE_CHILDREN,&rsrcs) ;
		trace("start_job",start_job,"end_job",end_job) ;
		//
		Digest digest = analyze(true/*at_end*/,killed) ;
		//
		end_report.msg += compute_crcs(digest) ;
		//
		if (!g_start_info.autodep_env.reliable_dirs) {                                                           // fast path : avoid listing targets & guards if reliable_dirs
			for( auto const& [t,_] : digest.targets  ) g_nfs_guard.change(t) ;                                   // protect against NFS strange notion of coherence while computing crcs
			for( auto const&  f    : g_gather.guards ) g_nfs_guard.change(f) ;                                   // .
			g_nfs_guard.close() ;
		}
		//
		if ( g_gather.seen_tmp && !g_start_info.keep_tmp )
			try { unlnk_inside(g_start_info.autodep_env.tmp_dir) ; } catch (::string const&) {}                  // cleaning is done at job start any way, so no harm
		//
		if      ( killed                            ) { trace("killed"      ) ; status = Status::Killed ; }
		else if ( status==Status::Ok && +digest.msg ) { trace("analysis_err") ; status = Status::Err    ; }
		append_to_string( end_report.msg , g_gather.msg , digest.msg ) ;
		end_report.digest = {
			.status       = status
		,	.targets      { ::move(digest.targets ) }
		,	.deps         { ::move(digest.deps    ) }
		,	.stderr       { ::move(g_gather.stderr) }
		,	.stdout       { ::move(g_gather.stdout) }
		,	.wstatus      = g_gather.wstatus
		,	.end_date     = end_job
		,	.stats{
				.cpu { Delay(rsrcs.ru_utime) + Delay(rsrcs.ru_stime) }
			,	.job { end_job      - start_job                      }
			,	.mem = size_t(rsrcs.ru_maxrss<<10)
			}
		} ;
	}
End :
	Trace trace("end",end_report.digest.status) ;
	try {
		ClientSockFd fd           { g_service_end , NConnectionTrials } ;
		Pdate        end_overhead = New                                 ;
		end_report.digest.stats.total = end_overhead - start_overhead ;                                          // measure overhead as late as possible
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		OMsgBuf().send( fd , end_report ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		trace("done",end_overhead) ;
	} catch (::string const& e) { exit(Rc::Fail,"after job execution : ",e) ; }
	//
	return 0 ;
}
