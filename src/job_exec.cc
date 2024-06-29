// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <linux/limits.h> // ARG_MAX
#include <sched.h>
#include <sys/mount.h>
#include <sys/resource.h>


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

Gather            g_gather        ;
JobIdx            g_job           = 0/*garbage*/ ;
PatternDict       g_match_dct     ;
NfsGuard          g_nfs_guard     ;
SeqId             g_seq_id        = 0/*garbage*/ ;
::string          g_phy_root_dir  ;
::string          g_phy_tmp_dir   ;
::string          g_service_start ;
::string          g_service_mngt  ;
::string          g_service_end   ;
JobRpcReply       g_start_info    ;
SeqId             g_trace_id      = 0/*garbage*/ ;
::vector_s        g_washed        ;
::umap_s<FileSig> g_created_views ;

::map_ss prepare_env(JobRpcReq& end_report) {
	::map_ss res     ;
	::string abs_cwd = no_slash(*g_root_dir_s+g_start_info.cwd_s) ;
	/**/                                res["PWD"        ] = abs_cwd                            ;
	g_start_info.autodep_env.root_dir = res["ROOT_DIR"   ] = no_slash(*g_root_dir_s)            ;
	/**/                                res["SEQUENCE_ID"] = ::to_string(g_seq_id             ) ;
	/**/                                res["SMALL_ID"   ] = ::to_string(g_start_info.small_id) ;
	for( auto& [k,v] : g_start_info.env ) {
		if (v!=EnvPassMrkr) {
			res[k] = ::move(v) ;
		} else if (has_env(k)) {                                           // if value is special illegal value, use value from environement (typically from slurm)
			::string v = get_env(k) ;
			end_report.dynamic_env.emplace_back(k,v) ;
			res[k] = ::move(v) ;
		}
	}
	if      ( g_start_info.keep_tmp_dir                  ) g_phy_tmp_dir = g_phy_root_dir+'/'+AdminDirS       +"tmp/"+g_job                 ;
	else if ( auto it=res.find("TMPDIR") ; it!=res.end() ) g_phy_tmp_dir = it->second+'/'+g_start_info.key+'/'       +g_start_info.small_id ;
	else if ( g_start_info.tmp_sz_mb==Npos               ) g_phy_tmp_dir = g_phy_root_dir+'/'+PrivateAdminDirS+"tmp/"+g_start_info.small_id ;
	//
	if      ( +g_start_info.job_space.tmp_view && (+g_phy_tmp_dir||g_start_info.tmp_sz_mb) ) g_start_info.autodep_env.tmp_dir = res["TMPDIR"] = g_start_info.job_space.tmp_view ;
	else if ( +g_phy_tmp_dir                                                               ) g_start_info.autodep_env.tmp_dir = res["TMPDIR"] = g_phy_tmp_dir                   ;
	//
	Trace trace("prepare_env",g_start_info.autodep_env.tmp_dir,g_phy_tmp_dir,res) ;
	//
	try {
		if (+g_phy_tmp_dir) unlnk_inside(g_phy_tmp_dir,true/*force*/) ;    // ensure tmp dir is clean (force because g_phy_tmp_dir is absolute)
	} catch (::string const&) {
		try                       { mk_dir(g_phy_tmp_dir) ;              } // ensure tmp dir exists
		catch (::string const& e) { throw "cannot create tmp dir : "+e ; }
	}
	return res ;
}

struct Digest {
	::vmap_s<TargetDigest> targets ;
	::vmap_s<DepDigest   > deps    ;
	::vector<NodeIdx     > crcs    ; // index in targets of entry for which we need to compute a crc
	::string               msg     ;
} ;

Digest analyze(Status status=Status::New) {                                                                                                       // status==New means job is not done
	Trace trace("analyze",status,g_gather.accesses.size()) ;
	Digest res             ; res.deps.reserve(g_gather.accesses.size()) ;                                                                         // typically most of accesses are deps
	Pdate  prev_first_read ;
	Pdate  relax           = Pdate(New)+g_start_info.network_delay ;
	//
	for( auto& [file,info] : g_gather.accesses ) {
		MatchFlags    flags = g_match_dct.at(file) ;
		AccessDigest& ad    = info.digest          ;
		switch (flags.is_target) {
			// if Ignore is in flags, it is there since the beginning
			case Yes   : ad.tflags |= flags.tflags() ; ad.extra_tflags |= flags.extra_tflags() ; if (flags.extra_tflags()[ExtraTflag::Ignore]) { ad.accesses = {} ; ad.write = No ; } break ;
			case No    : ad.dflags |= flags.dflags() ; ad.extra_dflags |= flags.extra_dflags() ; if (flags.extra_dflags()[ExtraDflag::Ignore])   ad.accesses = {} ;                   break ;
			case Maybe :                                                                       ;                                                                                      break ;
		DF}
		//
		if (ad.write==Yes)                                                                                                                        // ignore reads after earliest confirmed write
			for( Access a : All<Access> )
				if (info.read[+a]>info.write) ad.accesses &= ~a ;
		::pair<Pdate,Access> first_read = info.first_read()                                                                                     ;
		bool                 is_dep     = ad.dflags[Dflag::Static] || ( flags.is_target!=Yes && +ad.accesses && first_read.first<=info.target ) ; // if a (side) target, it is since the beginning
		bool is_tgt =
			ad.write!=No
		||	(	(  flags.is_target==Yes || info.target!=Pdate::Future         )
			&&	!( !ad.tflags[Tflag::Target] && ad.tflags[Tflag::Incremental] )                                   // fast path : no matching, no pollution, no washing => forget it
			)
		;
		// handle deps
		if (is_dep) {
			DepDigest dd { ad.accesses , info.dep_info , ad.dflags } ;
			//
			if ( ad.accesses[Access::Stat] && ad.extra_dflags[ExtraDflag::StatReadData] ) dd.accesses = ~Accesses() ;
			//
			// if file is not old enough, we make it hot and server will ensure job producing dep was done before this job started
			dd.hot          = info.dep_info.kind==DepInfoKind::Info && !info.dep_info.info().date.avail_at(first_read.first,g_start_info.date_prec) ;
			dd.parallel     = +first_read.first && first_read.first==prev_first_read                                                                ;
			prev_first_read = first_read.first                                                                                                      ;
			//
			if ( +dd.accesses && !dd.is_crc ) {                                                                   // try to transform date into crc as far as possible
				if      ( info.seen==Pdate::Future||info.seen>info.write ) { dd.crc(Crc::None) ; dd.hot=false ; } // job has been executed without seeing the file (before possibly writing to it)
				else if ( !dd.sig()                                      )   dd.crc({}       ) ; // file was not present initially but was seen, it is incoherent even if not present finally
				else if ( ad.write!=No                                   )   {}                  // cannot check stability as we wrote to it, clash will be detected in server if any
				else if ( FileSig sig{file} ; sig!=dd.sig()              )   dd.crc({}       ) ; // file dates are incoherent from first access to end of job, dont know what has been read
				else if ( !sig                                           )   dd.crc({}       ) ; // file is awkward
				else if ( !Crc::s_sense(dd.accesses,sig.tag())           )   dd.crc(sig.tag()) ; // just record the tag if enough to match (e.g. accesses==Lnk and tag==Reg)
			}
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.deps.emplace_back(file,dd) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (dd.hot) trace("dep   ",dd,flags,info.dep_info,first_read,g_start_info.date_prec,file) ;
			else        trace("dep   ",dd,flags,                                                file) ;
		}
		if (status==Status::New) continue ;            // we are handling chk_deps and we only care about deps
		// handle targets
		if (is_tgt) {
			if (ad.write==Maybe) relax.sleep_until() ; // /!\ if a write is interrupted, it may continue past the end of the process when accessing a network disk ...
			//                                         // ... no need to optimize (could compute other crcs while waiting) as this is exceptional
			bool    written  = ad.write==Yes ;
			FileSig sig      ;
			Crc     crc      ;                         // lazy evaluated (not in parallel, but need is exceptional)
			if (ad.write==Maybe) {                     // if we dont know if file has been written, detect file update from disk
				if (info.dep_info.kind==DepInfoKind::Crc) { crc = Crc(sig,file,g_start_info.hash_algo) ; written |= info.dep_info.crc()!=crc ; } // solve lazy evaluation
				else                                                                                     written |= info.dep_info.sig()!=sig ;
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
					trace("bad access",ad,flags) ;
					if (ad.write==Maybe    ) res.msg << "maybe "                        ;
					/**/                     res.msg << "unexpected "                   ;
					                         res.msg << (unlnk?"unlink ":"write to ")   ;
					if (flags.is_target==No) res.msg << "dep "                          ;
					/**/                     res.msg << mk_file(file,No|!unlnk) << '\n' ;
					reported = true ;
				break ;
			}
			if ( is_dep && !unlnk ) {
				trace("dep_and_target",ad,flags) ;
				if (!reported) {                                                                  // if dep and unexpected target, prefer unexpected message rather than this one
					const char* read ;
					switch (first_read.second) {
						case Access::Lnk  : read = "readlink" ; break ;
						case Access::Stat : read = "stat"     ; break ;
						default           : read = "read"     ;
					}
					res.msg << read<<" as dep before being known as a target : "<<mk_file(file)<<'\n' ;
				}
				ad.tflags |= Tflag::Incremental ;                                                 // file will have a predictible content, no reason to wash it
			}
			if (written) {
				if      ( unlnk                                                          )                         td.crc = Crc::None    ;
				else if ( auto it=g_created_views.find(file) ; it!=g_created_views.end() ) { td.sig = it->second ; td.crc = FileTag::Reg ; } // file is mapped, it could not be modified
				else if ( status==Status::Killed || !td.tflags[Tflag::Target]            ) { td.sig = sig        ; td.crc = td.sig.tag() ; } // no crc if meaningless
				else if ( +crc                                                           ) { td.sig = sig        ; td.crc = crc          ; } // we already have a crc
					res.crcs.emplace_back(res.targets.size()) ; // record index in res.targets for deferred (parallel) crc computation
			}
			if ( td.tflags[Tflag::Target] && !td.tflags[Tflag::Phony] ) {
				if ( td.tflags[Tflag::Static] && !td.extra_tflags[ExtraTflag::Optional] ) {
					if ( unlnk && status==Status::Ok ) res.msg << "missing static target " << mk_file(file,No/*exists*/) << '\n' ; // if job not is ok, this is quite normal, else warn
				} else {
					if ( unlnk                       ) td.tflags &= ~Tflag::Target ; // unless static and non-optional or phony, a target loses its official status if not actually produced
				}
			}
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.targets.emplace_back(file,td) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("target",ad,td,STR(unlnk),file) ;
		} else if (!is_dep) {
			trace("ignore",ad,file) ;
		}
	}
	for( ::string const& t : g_washed ) if (!g_gather.access_map.contains(t)) {
		trace("wash",t) ;
		res.targets.emplace_back( t , TargetDigest{.extra_tflags=ExtraTflag::Wash,.crc=Crc::None} ) ;
	}
	trace("done",res.deps.size(),res.targets.size(),res.crcs.size(),res.msg) ;
	return res ;
}

::vmap_s<DepDigest> cur_deps_cb() { return analyze().deps ; }

::vector_s cmd_line() {
	::vector_s cmd_line = ::move(g_start_info.interpreter) ;                                                     // avoid copying as interpreter is used only here
	if ( g_start_info.use_script || (g_start_info.cmd.first.size()+g_start_info.cmd.second.size())>ARG_MAX/2 ) { // env+cmd line must not be larger than ARG_MAX, keep some margin for env
		::string cmd_file = PrivateAdminDirS+"cmds/"s+g_start_info.small_id ;
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

void crc_thread_func( size_t id , vmap_s<TargetDigest>* targets , ::vector<NodeIdx> const* crcs , ::string* msg , Mutex<MutexLvl::JobExec>* msg_mutex ) {
	static ::atomic<NodeIdx> crc_idx = 0 ;
	t_thread_key = '0'+id ;
	Trace trace("crc",targets->size(),crcs->size()) ;
	NodeIdx cnt = 0 ;                                           // cnt is for trace only
	for( NodeIdx ci=0 ; (ci=crc_idx++)<crcs->size() ; cnt++ ) {
		::pair_s<TargetDigest>& e      = (*targets)[(*crcs)[ci]] ;
		Pdate                   before = New                     ;
		e.second.crc = Crc( e.second.sig/*out*/ , e.first , g_start_info.hash_algo ) ;
		trace("crc_date",ci,before,Pdate(New)-before,e.second.crc,e.second.sig,e.first) ;
		if (!e.second.crc.valid()) {
			Lock lock{*msg_mutex} ;
			*msg<<"cannot compute crc for "<<e.first ;
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
	::string                 msg       ;
	Mutex<MutexLvl::JobExec> msg_mutex ;
	{	::vector<jthread> crc_threads ; crc_threads.reserve(n_threads) ;
		for( size_t i=0 ; i<n_threads ; i++ )
			crc_threads.emplace_back( crc_thread_func , i , &digest.targets , &digest.crcs , &msg , &msg_mutex ) ; // just constructing and destructing the threads will execute & join them
	}
	return msg ;
}

int main( int argc , char* argv[] ) {
	Pdate        start_overhead = Pdate(New) ;
	ServerSockFd server_fd      { New }      ;       // server socket must be listening before connecting to server and last to the very end to ensure we can handle heartbeats
	//
	swear_prod(argc==8,argc) ;                       // syntax is : job_exec server:port/*start*/ server:port/*mngt*/ server:port/*end*/ seq_id job_idx root_dir trace_file
	g_service_start =                     argv[1]  ;
	g_service_mngt  =                     argv[2]  ;
	g_service_end   =                     argv[3]  ;
	g_seq_id        = from_string<SeqId >(argv[4]) ;
	g_job           = from_string<JobIdx>(argv[5]) ;
	g_phy_root_dir  =                     argv[6]  ; // passed early so we can chdir and trace early
	g_trace_id      = from_string<SeqId >(argv[7]) ;
	//
	g_trace_file = new ::string{g_phy_root_dir+'/'+PrivateAdminDirS+"trace/job_exec/"+g_trace_id} ;
	//
	JobRpcReq end_report { JobRpcProc::End , g_seq_id , g_job , {.status=Status::EarlyErr,.end_date=start_overhead} } ; // prepare to return an error, so we can goto End anytime
	//
	if (::chdir(g_phy_root_dir.c_str())!=0) {
		end_report.msg << "cannot chdir to root : "<<g_phy_root_dir<<'\n' ;
		goto End ;
	}
	Trace::s_sz = 10<<20 ;                                                                            // this is more than enough
	block_sigs({SIGCHLD}) ;                                                                           // necessary to capture it using signalfd
	app_init(No/*chk_version*/) ;
	//
	{	Trace trace("main",Pdate(New),::vector_view(argv,8)) ;
		trace("pid",::getpid(),::getpgrp()) ;
		trace("start_overhead",start_overhead) ;
		//
		bool found_server = false ;
		try {
			ClientSockFd fd {g_service_start,NConnectionTrials} ;
			found_server = true ;
			//             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			/**/           OMsgBuf().send                ( fd , JobRpcReq{JobRpcProc::Start,g_seq_id,g_job,server_fd.port()} ) ;
			g_start_info = IMsgBuf().receive<JobRpcReply>( fd                                                                ) ;
			//             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			trace("no_server",g_service_start,STR(found_server),e) ;
			if (found_server) exit(Rc::Fail                                                       ) ; // this is typically a ^C
			else              exit(Rc::Fail,"cannot communicate with server",g_service_start,':',e) ; // this may be a server config problem, better to report
		}
		trace("g_start_info",Pdate(New),g_start_info) ;
		switch (g_start_info.proc) {
			case JobRpcProc::None  : return 0 ;                                                       // server ask us to give up
			case JobRpcProc::Start : break    ;                                                       // normal case
		DF}
		try                       { g_start_info.job_space.chk() ;   }
		catch (::string const& e) { end_report.msg += e ; goto End ; }
		//
		g_root_dir_s = new ::string{ ( +g_start_info.job_space.root_view ? g_start_info.job_space.root_view : g_phy_root_dir ) + '/' } ;
		//
		g_nfs_guard.reliable_dirs = g_start_info.autodep_env.reliable_dirs ;
		//
		for( auto const& [d ,digest] : g_start_info.deps           ) if (digest.dflags[Dflag::Static]) g_match_dct.add( false/*star*/ , d  , digest.dflags ) ;
		for( auto const& [dt,mf    ] : g_start_info.static_matches )                                   g_match_dct.add( false/*star*/ , dt , mf            ) ;
		for( auto const& [p ,mf    ] : g_start_info.star_matches   )                                   g_match_dct.add( true /*star*/ , p  , mf            ) ;
		//
		{	::pair_s<bool/*ok*/> wash_report = do_file_actions( g_washed , ::move(g_start_info.pre_actions) , g_nfs_guard , g_start_info.hash_algo ) ;
			end_report.msg += ensure_nl(::move(wash_report.first)) ;
			if (!wash_report.second) {
				end_report.digest.status = Status::LateLostErr ;
				goto End ;
			}
		}
		auto handle_entry = [&]( ::string const& f , bool is_view )->void {
			SWEAR(+f) ;
			if (!is_lcl(f)   )               return ;
			if (f.back()=='/') { mk_dir(f) ; return ; }
			FileInfo fi{f} ;
			g_start_info.deps.emplace_back(f,DepDigest(Access::Stat,fi)) ;
			if (+fi.tag()) return ;
			::close(::open(dir_guard(f).c_str(),O_RDWR|O_CREAT,0644)) ;
			if (!is_view) return ;
			g_created_views.emplace(f,FileSig(f)) ;
			trace("created",f) ;
		} ;
		//
		::map_ss             cmd_env    ;
		::vmap_s<::vector_s> flat_views = g_start_info.job_space.flat_views() ;
		for( auto const& [view,phys] : flat_views ) {
			/**/                              handle_entry(view,true /*is_view*/) ;
			for( ::string const& phy : phys ) handle_entry(phy ,false/*is_view*/) ;
		}
		try {
			cmd_env = prepare_env(end_report) ;
			::string chroot_dir = PrivateAdminDirS+"chroot/"s+g_start_info.small_id ;
			//
			bool entered = g_start_info.job_space.enter(
				g_phy_root_dir
			,	g_phy_tmp_dir
			,	g_start_info.tmp_sz_mb
			,	chroot_dir
			,	g_start_info.autodep_env.src_dirs_s
			,	g_start_info.method==AutodepMethod::Fuse
			) ;
			if (entered) {
				// find a good starting pid
				// the goal is to minimize risks of pid conflicts between jobs in case pid is used to generate unique file names as temporary file instead of using TMPDIR, which is quite common
				// to do that we spread pid's among the availale range by setting the first pid used by jos as apart from each other as possible
				// call phi the golden number and NPids the number of available pids
				// spreading is maximized by using phi*NPids as an elementary spacing and id (small_id) as an index modulo NPids
				// this way there is a conflict between job 1 and job 2 when (id2-id1)*phi is near an integer
				// because phi is the irrational which is as far from rationals as possible, and id's are as small as possible, this probability is minimized
				// note that this is over-quality : any more or less random number would do the job : motivation is mathematical beauty rather than practical efficiency
				static constexpr uint32_t FirstPid  = 300                                 ;           // apparently, pid's wrap around back to 300
				static constexpr uint64_t NPids     = MAX_PID - FirstPid                  ;           // number of available pid's
				static constexpr uint64_t delta_pid = (1640531527*NPids) >> n_bits(NPids) ;           // use golden number to ensure best spacing (see above), 1640531527 = (2-(1+sqrt(5))/2)<<32
				//
				g_gather.first_pid =FirstPid + ((g_start_info.small_id*delta_pid)>>(32-n_bits(NPids)))%NPids ; // delta_pid on 64 bits to avoid rare overflow in multiplication
			}
		} catch (::string const& e) {
			end_report.msg += e ; goto End ;
		}
		trace("prepared",g_start_info.autodep_env,g_phy_tmp_dir) ;
		//
		g_gather.addr              =        g_start_info.addr             ;
		g_gather.as_session        =        true                          ;
		g_gather.autodep_env       = ::move(g_start_info.autodep_env    ) ;
		g_gather.autodep_env.views = ::move(flat_views                  ) ;
		g_gather.cur_deps_cb       =        cur_deps_cb                   ;
		g_gather.cwd               =        no_slash(g_start_info.cwd_s)  ;
		g_gather.env               =        &cmd_env                      ;
		g_gather.job               =        g_job                         ;
		g_gather.kill_sigs         = ::move(g_start_info.kill_sigs      ) ;
		g_gather.live_out          =        g_start_info.live_out         ;
		g_gather.method            =        g_start_info.method           ;
		g_gather.network_delay     =        g_start_info.network_delay    ;
		g_gather.seq_id            =        g_seq_id                      ;
		g_gather.server_master_fd  = ::move(server_fd                   ) ;
		g_gather.service_mngt      =        g_service_mngt                ;
		g_gather.timeout           =        g_start_info.timeout          ;
		//
		if (!g_start_info.method)                                                                              // if no autodep, consider all static deps are fully accessed
			for( auto& [d,digest] : g_start_info.deps ) if (digest.dflags[Dflag::Static]) {
				digest.accesses = ~Accesses() ;
				if ( digest.is_crc && !digest.crc().valid() ) digest.sig(FileSig(d)) ;
			}
		//
		g_gather.new_deps( start_overhead , ::move(g_start_info.deps) , g_start_info.stdin ) ;
		for( auto const& [f,_] : g_created_views ) g_gather.new_target( start_overhead , f , "view" ) ;
		// non-optional static targets must be reported in all cases
		for( auto const& [t,f] : g_match_dct.knowns ) if ( f.is_target==Yes && !f.extra_tflags()[ExtraTflag::Optional] ) g_gather.new_unlnk(start_overhead,t) ;
		//
		if (+g_start_info.stdin) g_gather.child_stdin = open_read(g_start_info.stdin) ;
		else                     g_gather.child_stdin = open_read("/dev/null"       ) ;
		g_gather.child_stdin.no_std() ;
		g_gather.child_stdout = Child::Pipe ;
		g_gather.child_stderr = Child::Pipe ;
		if (+g_start_info.stdout) {
			g_gather.child_stdout = open_write(g_start_info.stdout) ;
			g_gather.new_target( start_overhead , g_start_info.stdout , "<stdout>" ) ;
			g_gather.child_stdout.no_std() ;
		}
		g_gather.cmd_line = cmd_line() ;
		//              vvvvvvvvvvvvvvvvvvvvv
		Status status = g_gather.exec_child() ;
		//              ^^^^^^^^^^^^^^^^^^^^^
		struct rusage rsrcs ; getrusage(RUSAGE_CHILDREN,&rsrcs) ;
		//
		Digest digest = analyze(status) ;
		trace("analysis",g_gather.start_date,g_gather.end_date,status,g_gather.msg,digest.msg) ;
		//
		end_report.msg += compute_crcs(digest) ;
		//
		if (!g_start_info.autodep_env.reliable_dirs) {                                                         // fast path : avoid listing targets & guards if reliable_dirs
			for( auto const& [t,_] : digest.targets  ) g_nfs_guard.change(t) ;                                 // protect against NFS strange notion of coherence while computing crcs
			for( auto const&  f    : g_gather.guards ) g_nfs_guard.change(f) ;                                 // .
			g_nfs_guard.close() ;
		}
		//
		if ( g_gather.seen_tmp && !g_start_info.keep_tmp_dir )
			try { unlnk_inside(g_gather.autodep_env.tmp_dir,true/*force*/) ; } catch (::string const&) {}      // cleaning is done at job start any way, so no harm (force because tmp_dir is absolute)
		//
		if ( status==Status::Ok && +digest.msg ) status = Status::Err ;
		/**/                        end_report.msg += g_gather.msg ;
		if (status!=Status::Killed) end_report.msg += digest  .msg ;
		end_report.digest = {
			.status       = status
		,	.targets      { ::move(digest.targets ) }
		,	.deps         { ::move(digest.deps    ) }
		,	.stderr       { ::move(g_gather.stderr) }
		,	.stdout       { ::move(g_gather.stdout) }
		,	.wstatus      = g_gather.wstatus
		,	.end_date     = g_gather.end_date
		,	.stats{
				.cpu { Delay(rsrcs.ru_utime) + Delay(rsrcs.ru_stime) }
			,	.job { g_gather.end_date-g_gather.start_date         }
			,	.mem = size_t(rsrcs.ru_maxrss<<10)
			}
		} ;
	}
End :
	Trace trace("end",end_report.digest.status) ;
	try {
		ClientSockFd fd           { g_service_end , NConnectionTrials } ;
		Pdate        end_overhead = New                                 ;
		end_report.digest.stats.total = end_overhead - start_overhead ;                                        // measure overhead as late as possible
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		OMsgBuf().send( fd , end_report ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		trace("done",end_overhead) ;
	} catch (::string const& e) { exit(Rc::Fail,"after job execution : ",e) ; }
	//
	return 0 ;
}
