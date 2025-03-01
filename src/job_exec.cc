// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sched.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <unistd.h>       // sysconf

#include "app.hh"
#include "disk.hh"
#include "fd.hh"
#include "hash.hh"
#include "re.hh"
#include "thread.hh"
#include "time.hh"
#include "trace.hh"

#include "autodep/gather.hh"

#include "rpc_job.hh"
#include "rpc_job_exec.hh"

using namespace Caches ;
using namespace Disk   ;
using namespace Hash   ;
using namespace Re     ;
using namespace Time   ;

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
		if (star) patterns.emplace_back( key , val ) ;
		else      knowns  .emplace     ( key , val ) ;
	}
	// data
	::umap_s<MatchFlags>       knowns   = {} ;
	::vmap<RegExpr,MatchFlags> patterns = {} ;
} ;

::vector<ExecTraceEntry>* g_exec_trace      = nullptr      ;
Gather                    g_gather          ;
JobIdx                    g_job             = 0/*garbage*/ ;
PatternDict               g_match_dct       ;
NfsGuard                  g_nfs_guard       ;
SeqId                     g_seq_id          = 0/*garbage*/ ;
::string                  g_phy_repo_root_s ;
::string                  g_service_start   ;
::string                  g_service_mngt    ;
::string                  g_service_end     ;
JobStartRpcReply          g_start_info      ;
SeqId                     g_trace_id        = 0/*garbage*/ ;
::vector_s                g_washed          ;

struct Digest {
	::vmap_s<TargetDigest> targets ;
	::vmap_s<DepDigest   > deps    ;
	::vector<NodeIdx     > crcs    ; // index in targets of entry for which we need to compute a crc
	::string               msg     ;
} ;

JobStartRpcReply get_start_info(ServerSockFd const& server_fd) {
	Trace trace("get_start_info",g_service_start) ;
	bool             found_server = false ;
	JobStartRpcReply res          ;
	try {
		ClientSockFd fd {g_service_start,NConnectionTrials} ;
		fd.set_timeout(Delay(100)) ;                                                              // ensure we dont stay stuck in case server is in the coma ...
		found_server = true ;                                                                     //  ... 100 = 100 simultaneous connections, 10 jobs/s
		//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		/**/  OMsgBuf().send                     ( fd , JobStartRpcReq({g_seq_id,g_job},server_fd.port()) ) ;
		res = IMsgBuf().receive<JobStartRpcReply>( fd                                                     ) ;
		//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	} catch (::string const& e) {
		trace("no_start_info",STR(found_server),e) ;
		if (found_server) exit(Rc::Fail                                                       ) ; // this is typically a ^C
		else              exit(Rc::Fail,"cannot communicate with server",g_service_start,':',e) ; // this may be a server config problem, better to report
	}
	g_exec_trace->emplace_back(New,"received_info_from_server") ;
	trace(res) ;
	return res ;
}

Digest analyze(Status status=Status::New) {                                                                                                    // status==New means job is not done
	Trace trace("analyze",status,g_gather.accesses.size()) ;
	Digest res             ; res.deps.reserve(g_gather.accesses.size()) ;                                                                      // typically most of accesses are deps
	Pdate  prev_first_read ;
	Pdate  relax           = Pdate(New)+g_start_info.network_delay ;
	//
	for( auto& [file,info] : g_gather.accesses ) {
		MatchFlags    flags = g_match_dct.at(file) ;
		AccessDigest& ad    = info.digest          ;
		switch (flags.is_target) {
			// manage ignore flag if mentioned in the rule
			case Yes   : ad.tflags |= flags.tflags() ; ad.extra_tflags |= flags.extra_tflags() ; if (flags.extra_tflags()[ExtraTflag::Ignore]) { ad.accesses = {} ; ad.write = No ; } break ;
			case No    : ad.dflags |= flags.dflags() ; ad.extra_dflags |= flags.extra_dflags() ; if (flags.extra_dflags()[ExtraDflag::Ignore])   ad.accesses = {} ;                   break ;
			case Maybe :                                                                       ;                                                                                      break ;
		DF}
		//
		if (ad.write==Yes)                                                                                                                     // ignore reads after earliest confirmed write
			for( Access a : iota(All<Access>) )
				if ( info.read[+a]>info.write || info.read[+a]>info.target ) ad.accesses &= ~a ;
		::pair<Pdate,Accesses> first_read = info.first_read()                                                                                ;
		bool                   ignore_err = ad.dflags[Dflag::IgnoreError]||ad.extra_dflags[ExtraDflag::Ignore]                               ;
		bool                   is_read    = +ad.accesses || info.digest_required || !ignore_err                                              ;
		bool                   is_dep     = ad.dflags[Dflag::Static] || ( flags.is_target!=Yes && is_read && first_read.first<=info.target ) ; // if a (side) target, it is so since the beginning
		bool is_tgt =
			ad.write!=No
		||	(	(  flags.is_target==Yes || info.target!=Pdate::Future         )
			&&	!( !ad.tflags[Tflag::Target] && ad.tflags[Tflag::Incremental] )                           // fast path : no matching, no pollution, no washing => forget it
			)
		;
		// handle deps
		if (is_dep) {
			DepDigest dd { ad.accesses , info.dep_info , ad.dflags } ;
			//
			// if file is not old enough, we make it hot and server will ensure job producing dep was done before this job started
			dd.hot          = info.dep_info.kind==DepInfoKind::Info && !info.dep_info.info().date.avail_at(first_read.first,g_start_info.ddate_prec) ;
			dd.parallel     = +first_read.first && first_read.first==prev_first_read                                                                 ;
			prev_first_read = first_read.first                                                                                                       ;
			// try to transform date into crc as far as possible
			bool unstable = false ;
			if      ( dd.is_crc                                 )   {}                                    // already a crc => nothing to do
			else if ( !is_read                                  )   {}                                    // no access     => nothing to do
			else if ( !info.digest_seen || info.seen>info.write ) { dd.crc(Crc::None) ; dd.hot=false  ; } // job has been executed without seeing the file (before possibly writing to it)
			else if ( !dd.sig()                                 ) { dd.crc({}       ) ; unstable=true ; } // file was not present initially but was seen, it is incoherent even if not present finally
			else if ( ad.write!=No                              )   {}                                    // cannot check stability as we wrote to it, clash will be detected in server if any
			else if ( FileSig sig{file} ; sig!=dd.sig()         ) { dd.crc({}       ) ; unstable=true ; } // file dates are incoherent from first access to end of job, dont know what has been read
			else if ( !sig                                      ) { dd.crc({}       ) ; unstable=true ; } // file is awkward
			else if ( !Crc::s_sense(dd.accesses,sig.tag())      )   dd.crc(sig.tag()) ;                   // just record the tag if enough to match (e.g. accesses==Lnk and tag==Reg)
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.deps.emplace_back(file,dd) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (status!=Status::New) {                                               // only trace for user at end of job as intermediate analyses are of marginal interest for user
				if      (unstable) g_exec_trace->emplace_back(New,"unstable",file) ;
				else if (dd.hot  ) g_exec_trace->emplace_back(New,"hot"     ,file) ;
			}
			if (dd.hot) trace("dep_hot",dd,info.dep_info,first_read,g_start_info.ddate_prec,file) ;
			else        trace("dep    ",dd,                                                 file) ;
		}
		if (status==Status::New) continue ;                                          // we are handling chk_deps and we only care about deps
		// handle targets
		if (is_tgt) {
			if (ad.write==Maybe) relax.sleep_until() ;                               // /!\ if a write is interrupted, it may continue past the end of the process when accessing a network disk ...
			//                                                                       // ... no need to optimize (could compute other crcs while waiting) as this is exceptional
			bool    written = ad.write==Yes ;
			FileSig sig     ;
			Crc     crc     ;                                                        // lazy evaluated (not in parallel, but need is exceptional)
			if (ad.write==Maybe) {                                                   // if we dont know if file has been written, detect file update from disk
				if (info.dep_info.kind==DepInfoKind::Crc) { crc = Crc(file,/*out*/sig) ; written |= info.dep_info.crc()!=crc ; } // solve lazy evaluation
				else                                                                     written |= info.dep_info.sig()!=sig ;
			}
			if (!crc) sig = file ;                                                                // sig is computed at the same time as crc, but we need it in all cases
			//
			TargetDigest td       { .tflags=ad.tflags , .extra_tflags=ad.extra_tflags } ;
			bool unlnk    = !sig  ;
			bool reported = false ;
			//
			if (is_dep                        ) td.tflags    |= Tflag::Incremental              ; // if is_dep, previous target state is guaranteed by being a dep, use it
			if (!td.tflags[Tflag::Incremental]) td.pre_exist  = info.dep_info.seen(ad.accesses) ;
			switch (flags.is_target) {
				case Yes   : break ;
				case Maybe :
					if (unlnk) break ;                                                            // it is ok to write and unlink temporary files
				[[fallthrough]] ;
				case No :
					if (!written                          ) break ;                               // it is ok to attempt writing as long as attempt does not succeed
					if (ad.extra_tflags[ExtraTflag::Allow]) break ;                               // it is ok if explicitly allowed by user
					trace("bad_access",ad,flags) ;
					if (ad.write==Maybe    ) res.msg << "maybe "                        ;
					/**/                     res.msg << "unexpected "                   ;
					/**/                     res.msg << (unlnk?"unlink ":"write to ")   ;
					if (flags.is_target==No) res.msg << "dep "                          ;
					/**/                     res.msg << mk_file(file,No|!unlnk) << '\n' ;
					reported = true ;
				break ;
			}
			if ( is_dep && !unlnk ) {
				g_exec_trace->emplace_back(New,"dep_and_target",file) ;
				if (!reported) {                                                                  // if dep and unexpected target, prefer unexpected message rather than this one
					const char* read = nullptr ;
					if      (ad.dflags[Dflag::Static]       ) read = "a static dep" ;
					else if (first_read.second[Access::Reg ]) read = "read"         ;
					else if (first_read.second[Access::Lnk ]) read = "readlink'ed"  ;
					else if (first_read.second[Access::Stat]) read = "stat'ed"      ;
					else if (ad.dflags[Dflag::Required]     ) read = "required"     ;
					SWEAR(read) ;
					res.msg << "file was "<<read<<" and later declared as target : "<<mk_file(file)<<'\n' ;
				}
			}
			if (written) {
				if      ( unlnk                                               )                  td.crc = Crc::None    ;
				else if ( status==Status::Killed || !td.tflags[Tflag::Target] ) { td.sig = sig ; td.crc = td.sig.tag() ; } // no crc if meaningless
				else if ( +crc                                                ) { td.sig = sig ; td.crc = crc          ; } // we already have a crc
				//
				if (!crc.valid()) res.crcs.emplace_back(res.targets.size()) ;                                              // record index in res.targets for deferred (parallel) crc computation
			}
			if (
				td.tflags[Tflag::Target] && !td.tflags[Tflag::Phony] && td.tflags[Tflag::Static] && !td.extra_tflags[ExtraTflag::Optional] // target is expected
			&&	unlnk                                                                                                                      // but not produced
			&&	status==Status::Ok                                                                                                         // and there no more important reason
			)
				res.msg << "missing static target " << mk_file(file,No/*exists*/) << '\n' ;                                                // warn specifically
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.targets.emplace_back(file,td) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("target ",ad,td,STR(unlnk),file) ;
		} else if (!is_dep) {
			trace("ignore ",ad,file) ;
		}
	}
	for( ::string const& t : g_washed ) if (!g_gather.access_map.contains(t)) {
		using ETF = ExtraTflag ;
		trace("wash",t) ;
		MatchFlags flags = g_match_dct.at(t) ;
		if      (flags.is_target!=Yes             ) res.targets.emplace_back( t , TargetDigest{                          .extra_tflags=                     ETF::Wash , .crc=Crc::None } ) ;
		else if (flags.extra_tflags()[ETF::Ignore]) {}
		else                                        res.targets.emplace_back( t , TargetDigest{ .tflags=flags.tflags() , .extra_tflags=flags.extra_tflags()|ETF::Wash , .crc=Crc::None } ) ;
	}
	g_exec_trace->emplace_back(New,"analyzed") ;
	trace("done",res.deps.size(),res.targets.size(),res.crcs.size(),res.msg) ;
	return res ;
}

::vmap_s<DepDigest> cur_deps_cb() { return analyze().deps ; }

::string g_to_unlnk ;                                                                                           // XXX> : suppress when CentOS7 bug is fixed
::vector_s cmd_line() {
	static const size_t ArgMax = ::sysconf(_SC_ARG_MAX) ;
	::vector_s res = ::move(g_start_info.interpreter) ;                                                         // avoid copying as interpreter is used only here
	if ( g_start_info.use_script || (g_start_info.cmd.first.size()+g_start_info.cmd.second.size())>ArgMax/2 ) { // env+cmd line must not be larger than ARG_MAX, keep some margin for env
		// XXX> : fix the bug with CentOS7 where the write seems not to be seen and old script is executed instead of new one
		// correct code :
		// ::string cmd_file = PrivateAdminDirS+"cmds/"s+g_start_info.small_id ;
		::string cmd_file = PrivateAdminDirS+"cmds/"s+g_seq_id ;
		AcFd( dir_guard(cmd_file) , Fd::Write ).write(g_start_info.cmd.first+g_start_info.cmd.second) ;
		res.reserve(res.size()+1) ;
		res.push_back(mk_abs(cmd_file,*g_repo_root_s)) ;                                                        // provide absolute script so as to support cwd
		g_to_unlnk = ::move(cmd_file) ;
	} else {
		res.reserve(res.size()+2) ;
		res.push_back( "-c"                                             ) ;
		res.push_back( g_start_info.cmd.first + g_start_info.cmd.second ) ;
	}
	return res ;
}

void crc_thread_func( size_t id , vmap_s<TargetDigest>* targets , ::vector<NodeIdx> const* crcs , ::string* msg , Mutex<MutexLvl::JobExec>* msg_mutex , ::vector<FileInfo>* target_fis , size_t* sz ) {
	static ::atomic<NodeIdx> crc_idx = 0 ;
	t_thread_key = '0'+id ;
	Trace trace("crc_thread_func",targets->size(),crcs->size()) ;
	NodeIdx cnt = 0 ;                                             // cnt is for trace only
	*sz = 0 ;
	for( NodeIdx ci=0 ; (ci=crc_idx++)<crcs->size() ; cnt++ ) {
		NodeIdx                 ti     = (*crcs)[ci]    ;
		::pair_s<TargetDigest>& e      = (*targets)[ti] ;
		Pdate                   before = New            ;
		FileInfo                fi     ;
		//             vvvvvvvvvvvvvvvvvvvvvvvvvv
		e.second.crc = Crc( e.first , /*out*/fi ) ;
		//             ^^^^^^^^^^^^^^^^^^^^^^^^^^
		e.second.sig       = fi.sig() ;
		(*target_fis)[ti]  = fi       ;
		*sz               += fi.sz    ;
		trace("crc_date",ci,before,Pdate(New)-before,e.second.crc,e.second.sig,e.first) ;
		if (!e.second.crc.valid()) {
			Lock lock{*msg_mutex} ;
			*msg<<"cannot compute crc for "<<e.first ;
		}
	}
	trace("done",cnt) ;
}

::string/*msg*/ compute_crcs( Digest& digest , ::vector<FileInfo>&/*out*/ target_fis , size_t&/*out*/ total_sz ) {
	size_t                            n_threads = thread::hardware_concurrency() ;
	if (n_threads<1                 ) n_threads = 1                              ;
	if (n_threads>8                 ) n_threads = 8                              ;
	if (n_threads>digest.crcs.size()) n_threads = digest.crcs.size()             ;
	//
	Trace trace("compute_crcs",digest.crcs.size(),n_threads) ;
	::string                 msg       ;
	Mutex<MutexLvl::JobExec> msg_mutex ;
	::vector<size_t>         szs       ( n_threads ) ;
	target_fis.resize(digest.targets.size()) ;
	{	::vector<::jthread> crc_threads ; crc_threads.reserve(n_threads) ;
		for( size_t i  : iota(n_threads) ) crc_threads.emplace_back( crc_thread_func , i , &digest.targets , &digest.crcs , &msg , &msg_mutex , &target_fis , &szs[i] ) ;
	}
	total_sz = 0 ;
	for( size_t s : szs ) total_sz += s ;
	g_exec_trace->emplace_back(New,"computed_crc") ;
	return msg ;
}

int main( int argc , char* argv[] ) {
	Pdate        start_overhead = Pdate(New) ;
	ServerSockFd server_fd      { New }      ;         // server socket must be listening before connecting to server and last to the very end to ensure we can handle heartbeats
	::string     upload_key     ;                      // key used to identify temporary data uploaded to the cache
	//
	swear_prod(argc==8,argc) ;                         // syntax is : job_exec server:port/*start*/ server:port/*mngt*/ server:port/*end*/ seq_id job_idx repo_root trace_file
	g_service_start   =                     argv[1]  ;
	g_service_mngt    =                     argv[2]  ;
	g_service_end     =                     argv[3]  ;
	g_seq_id          = from_string<SeqId >(argv[4]) ;
	g_job             = from_string<JobIdx>(argv[5]) ;
	g_phy_repo_root_s = with_slash         (argv[6]) ; // passed early so we can chdir and trace early
	g_trace_id        = from_string<SeqId >(argv[7]) ;
	//
	g_repo_root_s = &g_phy_repo_root_s ;               // no need to search for it
	//
	g_trace_file = new ::string{g_phy_repo_root_s+PrivateAdminDirS+"trace/job_exec/"+g_trace_id} ;
	//
	JobEndRpcReq end_report { {g_seq_id,g_job} , {.end_date=start_overhead,.status=Status::EarlyErr} } ;                     // prepare to return an error, so we can goto End anytime
	g_exec_trace = &end_report.exec_trace ;
	g_exec_trace->emplace_back(start_overhead,"start_overhead") ;
	//
	if (::chdir(no_slash(g_phy_repo_root_s).c_str())!=0) {
		get_start_info(server_fd) ;                                                                                          // getting start_info is useless, but necessary to be allowed to report end
		end_report.msg << "cannot chdir to root : "<<no_slash(g_phy_repo_root_s)<<'\n' ;
		goto End ;
	}
	Trace::s_sz = 10<<20 ;                                                                                                   // this is more than enough
	block_sigs({SIGCHLD}) ;                                                                                                  // necessary to capture it using signalfd
	app_init(false/*read_only_ok*/,No/*chk_version*/,Maybe/*cd_root*/) ;                                                     // dont cd, but check we are in a repo
	//
	{	Trace trace("main",Pdate(New),::span<char*>(argv,argc)) ;
		trace("pid",::getpid(),::getpgrp()) ;
		trace("start_overhead",start_overhead) ;
		//
		g_start_info = get_start_info(server_fd) ;
		if (!g_start_info) return 0 ;                                                                                        // server ask us to give up
		try                       { g_start_info.job_space.mk_canon(g_phy_repo_root_s) ; }
		catch (::string const& e) { end_report.msg += e ; goto End ;                     }
		//
		g_repo_root_s = new ::string{ g_start_info.job_space.repo_view_s | g_phy_repo_root_s } ;
		//
		g_nfs_guard.reliable_dirs = g_start_info.autodep_env.reliable_dirs ;
		//
		for( auto const& [d ,digest] : g_start_info.deps           ) if (digest.dflags[Dflag::Static]) g_match_dct.add( false/*star*/ , d  , digest.dflags ) ;
		for( auto const& [dt,mf    ] : g_start_info.static_matches )                                   g_match_dct.add( false/*star*/ , dt , mf            ) ;
		for( auto const& [p ,mf    ] : g_start_info.star_matches   )                                   g_match_dct.add( true /*star*/ , p  , mf            ) ;
		//
		{	::pair_s<bool/*ok*/> wash_report = do_file_actions( /*out*/g_washed , ::move(g_start_info.pre_actions) , g_nfs_guard ) ;
			end_report.msg += ensure_nl(::move(wash_report.first)) ;
			if (!wash_report.second) {
				end_report.digest.status = Status::LateLostErr ;
				goto End ;
			}
		}
		Pdate washed { New } ;
		g_exec_trace->emplace_back(washed,"washed") ;
		//
		SWEAR(!end_report.phy_tmp_dir_s,end_report.phy_tmp_dir_s) ;
		{	auto it = g_start_info.env.begin() ;
			for(; it!=g_start_info.env.end() ; it++ ) if (it->first=="TMPDIR") break ;
			if ( it==g_start_info.env.end() || +it->second ) {                                                               // if TMPDIR is set and empty, no tmp dir is prepared/cleaned
				if (g_start_info.keep_tmp) {
					end_report.phy_tmp_dir_s << g_phy_repo_root_s<<AdminDirS<<"tmp/"<<g_job<<'/' ;
				} else {
					if      (it==g_start_info.env.end()       ) {}
					else if (it->second!=EnvPassMrkr          ) end_report.phy_tmp_dir_s << with_slash(it->second       )<<g_start_info.key<<'/'<<g_start_info.small_id<<'/' ;
					else if (has_env("TMPDIR")                ) end_report.phy_tmp_dir_s << with_slash(get_env("TMPDIR"))<<g_start_info.key<<'/'<<g_start_info.small_id<<'/' ;
					if      (!end_report.phy_tmp_dir_s        ) end_report.phy_tmp_dir_s << g_phy_repo_root_s<<PrivateAdminDirS<<"tmp/"         <<g_start_info.small_id<<'/' ;
					else if (!is_abs(end_report.phy_tmp_dir_s)) {
						end_report.msg << "$TMPDIR ("<<end_report.phy_tmp_dir_s<<") must be absolute" ;
						goto End ;
					}
				}
			}
		}
		//
		::map_ss              cmd_env         ;
		::vmap_s<MountAction> enter_actions   ;
		::string              top_repo_root_s ;
		try {
			if (
				g_start_info.enter(
					/*out*/enter_actions
				,	/*out*/cmd_env
				,	/*out*/end_report.dynamic_env
				,	/*out*/g_gather.first_pid
				,	/*out*/top_repo_root_s
				,	       *g_lmake_root_s
				,	       g_phy_repo_root_s
				,	       end_report.phy_tmp_dir_s
				,	       g_seq_id
				)
			) {
				RealPath real_path { g_start_info.autodep_env } ;
				for( auto& [f,a] : enter_actions ) {
					RealPath::SolveReport sr = real_path.solve(f,true/*no_follow*/) ;
					for( ::string& l : sr.lnks )
						/**/                            g_gather.new_dep   ( washed , ::move(l      ) ,  Access::Lnk  , "mount_lnk"    ) ;
					if (sr.file_loc<=FileLoc::Dep) {
						if      (a==MountAction::Read ) g_gather.new_dep   ( washed , ::move(sr.real) , ~Access::Stat , "mount_src"    ) ;
						else if (sr.file_accessed==Yes) g_gather.new_dep   ( washed , ::move(sr.real) ,  Access::Lnk  , "mount_src"    ) ;
					}
					if (sr.file_loc<=FileLoc::Repo) {
						if      (a==MountAction::Write) g_gather.new_target( washed , ::move(sr.real) ,                 "mount_target" ) ;
					}
				}
				g_exec_trace->emplace_back(New,"entered_namespace") ;
			}
		} catch (::string const& e) {
			end_report.msg += e ;
			goto End ;
		}
		g_start_info.autodep_env.fast_host        = host()                                                                 ; // host on which fast_report_pipe works
		g_start_info.autodep_env.fast_report_pipe = top_repo_root_s+PrivateAdminDirS+"fast_reports/"+g_start_info.small_id ; // fast_report_pipe is a pipe and only works locally
		g_start_info.autodep_env.views            = g_start_info.job_space.flat_phys()                                     ;
		trace("prepared",g_start_info.autodep_env) ;
		//
		g_gather.addr             =        g_start_info.addr           ;
		g_gather.as_session       =        true                        ;
		g_gather.autodep_env      = ::move(g_start_info.autodep_env  ) ;
		g_gather.cur_deps_cb      =        cur_deps_cb                 ;
		g_gather.env              =        &cmd_env                    ;
		g_gather.exec_trace       =        g_exec_trace                ;
		g_gather.job              =        g_job                       ;
		g_gather.kill_sigs        = ::move(g_start_info.kill_sigs    ) ;
		g_gather.live_out         =        g_start_info.live_out       ;
		g_gather.method           =        g_start_info.method         ;
		g_gather.network_delay    =        g_start_info.network_delay  ;
		g_gather.no_tmp           =       !end_report.phy_tmp_dir_s    ;
		g_gather.seq_id           =        g_seq_id                    ;
		g_gather.server_master_fd = ::move(server_fd                 ) ;
		g_gather.service_mngt     =        g_service_mngt              ;
		g_gather.timeout          =        g_start_info.timeout        ;
		//
		if (!g_start_info.method)                                                           // if no autodep, consider all static deps are fully accessed as we have no precise report
			for( auto& [d,digest] : g_start_info.deps ) if (digest.dflags[Dflag::Static]) {
				digest.accesses = ~Accesses() ;
				if ( digest.is_crc && !digest.crc().valid() ) digest.sig(FileSig(d)) ;
			}
		//
		g_gather.new_deps( washed , ::move(g_start_info.deps) , g_start_info.stdin ) ;
		for( auto const& [t,f] : g_match_dct.knowns )
			if ( f.is_target==Yes && !f.extra_tflags()[ExtraTflag::Optional] )
				g_gather.new_unlnk(washed,t) ;                                              // always report non-optional static targets
		//
		if (+g_start_info.stdin) g_gather.child_stdin = Fd(g_start_info.stdin) ;
		else                     g_gather.child_stdin = Fd("/dev/null"       ) ;
		g_gather.child_stdin.no_std() ;
		g_gather.child_stderr = Child::PipeFd ;
		if (!g_start_info.stdout) {
			g_gather.child_stdout = Child::PipeFd ;
		} else {
			g_gather.child_stdout = Fd(dir_guard(g_start_info.stdout),Fd::Write) ;
			g_gather.new_target( washed , g_start_info.stdout , "<stdout>" ) ;
			g_gather.child_stdout.no_std() ;
		}
		g_gather.cmd_line = cmd_line() ;
		Status status ;
		//                                   vvvvvvvvvvvvvvvvvvvvv
		try                       { status = g_gather.exec_child() ; }
		//                                   ^^^^^^^^^^^^^^^^^^^^^
		catch (::string const& e) { end_report.msg += e ; goto End ; }
		struct rusage rsrcs ; ::getrusage(RUSAGE_CHILDREN,&rsrcs) ;
		//
		if (+g_to_unlnk) unlnk(g_to_unlnk) ;                                                // XXX> : suppress when CentOS7 bug is fixed
		//
		Digest digest = analyze(status) ;
		trace("analysis",g_gather.start_date,g_gather.end_date,status,g_gather.msg,digest.msg) ;
		//
		::vector<FileInfo> target_fis ;
		end_report.msg += compute_crcs( digest , /*out*/target_fis , /*out*/end_report.total_sz ) ;
		//
		if (g_start_info.cache) {
			upload_key = g_start_info.cache->upload( digest.targets , target_fis , g_start_info.z_lvl ) ;
			g_exec_trace->emplace_back( New , "uploaded_to_cache" , cat(g_start_info.cache->tag(),':',g_start_info.z_lvl) ) ;
			trace("cache",g_start_info.end_attrs.cache,upload_key) ;
		}
		//
		if (!g_start_info.autodep_env.reliable_dirs) {                                      // fast path : avoid listing targets & guards if reliable_dirs
			for( auto const& [t,_] : digest.targets  ) g_nfs_guard.change(t) ;              // protect against NFS strange notion of coherence while computing crcs
			for( auto const&  f    : g_gather.guards ) g_nfs_guard.change(f) ;              // .
			g_nfs_guard.close() ;
		}
		//
		if ( status==Status::Ok && ( +digest.msg || (+g_gather.stderr&&!g_start_info.allow_stderr) ) )
			status = Status::Err ;
		//
		/**/                        end_report.msg += g_gather.msg ;
		if (status!=Status::Killed) end_report.msg += digest  .msg ;
		JobStats stats {
			.cpu { Delay(rsrcs.ru_utime) + Delay(rsrcs.ru_stime) }
		,	.job { g_gather.end_date-g_gather.start_date         }
		,	.mem = size_t(rsrcs.ru_maxrss<<10)
		} ;
		end_report.digest = {
			.upload_key     =        upload_key
		,	.deps           { ::move(digest.deps           ) }
		,	.end_attrs      { ::move(g_start_info.end_attrs) }
		,	.end_date       =        g_gather.end_date
		,	.stats          { ::move(stats                 ) }
		,	.status         =        status
		,	.stderr         { ::move(g_gather.stderr       ) }
		,	.stdout         { ::move(g_gather.stdout       ) }
		,	.targets        { ::move(digest.targets        ) }
		,	.wstatus        =        g_gather.wstatus
		} ;
	}
End :
	{	Trace trace("end",end_report.digest.status) ;
		try {
			ClientSockFd fd           { g_service_end , NConnectionTrials } ;
			Pdate        end_overhead = New                                 ;
			g_exec_trace->emplace_back(end_overhead,"end_overhead") ;
			end_report.digest.stats.total = end_overhead - start_overhead ;                 // measure overhead as late as possible
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send( fd , end_report ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("done",end_overhead) ;
		} catch (::string const& e) {
			if (+upload_key) g_start_info.cache->dismiss(upload_key) ;                      // suppress temporary data if server cannot handle them
			exit(Rc::Fail,"after job execution : ",e) ;
		}
	}
	try                       { g_start_info.exit() ;                             }
	catch (::string const& e) { exit(Rc::Fail,"cannot cleanup namespaces : ",e) ; }
	//
	return 0 ;
}
