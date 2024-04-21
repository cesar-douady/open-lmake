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

Gather         g_gather        ;
JobIdx         g_job           = 0/*garbage*/ ;
PatternDict    g_match_dct     ;
NfsGuard       g_nfs_guard     ;
SeqId          g_seq_id        = 0/*garbage*/ ;
::string       g_phy_root_dir  ;
::string       g_phy_tmp_dir   ;
::string       g_service_start ;
::string       g_service_mngt  ;
::string       g_service_end   ;
JobRpcReply    g_start_info    ;
SeqId          g_trace_id      = 0/*garbage*/ ;
::vector_s     g_washed        ;

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
		} else if (has_env(k)) {                                           // if value is special illegal value, use value from environement (typically from slurm)
			::string v = get_env(k) ;
			end_report.dynamic_env.emplace_back(k,env_encode(::copy(v))) ;
			res[k] =                                         ::move(v)   ;
		}
	}
	if ( g_start_info.tmp_from_var || g_start_info.tmp_sz_mb ) {
		if      ( +g_start_info.autodep_env.tmp_dir          ) g_phy_tmp_dir = *g_root_dir + g_start_info.autodep_env.tmp_dir                                   ;
		else if ( !g_start_info.tmp_from_var                 ) {}
		else if ( auto it=res.find("TMPDIR") ; it!=res.end() ) g_phy_tmp_dir = it->second                                                                       ;
		else if ( +g_start_info.remote_tmp_dir               ) g_phy_tmp_dir = to_string(*g_root_dir,'/',g_start_info.remote_tmp_dir,'/',g_start_info.small_id) ;
		else                                                   g_phy_tmp_dir = to_string(*g_root_dir,'/',PrivateAdminDir,"/tmp"     ,'/',g_start_info.small_id) ;
		//
		if (+g_start_info.tmp_dir)                           g_start_info.autodep_env.tmp_dir = res["TMPDIR"] = g_start_info.tmp_dir ;
		else                       { SWEAR(+g_phy_tmp_dir) ; g_start_info.autodep_env.tmp_dir = res["TMPDIR"] = g_phy_tmp_dir        ; }
	} else {
		SWEAR(!g_start_info.tmp_dir,g_start_info.tmp_dir) ;
	}
	//
	Trace trace("prepare_env",g_start_info.autodep_env.tmp_dir,g_phy_tmp_dir,res) ;
	//
	try {
		if (+g_phy_tmp_dir) unlnk_inside(g_phy_tmp_dir) ;                  // ensure tmp dir is clean
	} catch (::string const&) {
		try                       { mk_dir(g_phy_tmp_dir) ;              } // ensure tmp dir exists
		catch (::string const& e) { throw "cannot create tmp dir : "+e ; }
	}
	return res ;
}

static void _bind_mount( ::string const& src , ::string const& dst ) {
	if (::mount( src.c_str() ,  dst.c_str() , nullptr/*type*/ , MS_BIND|MS_REC , nullptr/*data*/ )!=0) throw to_string("cannot bind mount ",src," onto ",dst," : ",strerror(errno)) ;
}
static void _tmp_mount( size_t sz_mb , ::string const& dst ) {
	SWEAR(sz_mb) ;
	if (::mount( "" ,  dst.c_str() , "tmpfs" , 0/*flags*/ , to_string(sz_mb,"m").c_str() )!=0) throw to_string("cannot mount tmpfs of size",sz_mb," MB onto ",dst," : ",strerror(errno)) ;
}
static void _chroot(::string const& dir) {
	if (::chroot(dir.c_str())!=0) throw to_string("cannot chroot to ",dir," : ",strerror(errno)) ;
}
static void _atomic_write( ::string const& file , ::string const& data ) {
	ssize_t cnt = ::write( AutoCloseFd(::open(file.c_str(),O_WRONLY)) , data.c_str() , data.size() ) ;
	if (cnt<0                  ) throw to_string("cannot write atomically ",data.size(), " bytes to ",file," : ",strerror(errno)          ) ;
	if (size_t(cnt)<data.size()) throw to_string("cannot write atomically ",data.size(), " bytes to ",file," : only ",cnt," bytes written") ;
}
void prepare_namespace() {
	Trace trace("prepare_namespace",g_start_info.chroot,g_start_info.root_dir,g_start_info.tmp_dir) ;
	//
	if ( !g_start_info.chroot && !g_start_info.root_dir && !g_start_info.tmp_dir ) return ;
	//
	int uid = getuid() ; // must be done before unshare that invents a new user
	int gid = getgid() ; // .
	//
	if (::unshare(CLONE_NEWUSER|CLONE_NEWNS)!=0) throw to_string("cannot create namespace : ",strerror(errno)) ;
	//
	bool            must_create_root = +g_start_info.root_dir && !is_dir(g_start_info.chroot+g_start_info.root_dir) ;
	bool            must_create_tmp  = +g_start_info.tmp_dir  && !is_dir(g_start_info.chroot+g_start_info.tmp_dir ) ;
	::string const* used_chroot      = &g_start_info.chroot                                                         ;
	::string        private_chroot   ;
	trace("create",STR(must_create_root),STR(must_create_tmp)) ;
	if ( must_create_root || must_create_tmp ) {                                                                                                    // we may mount directly in chroot dir
		::vector_s top_lvls = lst_dir(g_start_info.chroot,"/") ;
		private_chroot = to_string(PrivateAdminDir,"/chroot/",g_start_info.small_id) ;
		used_chroot    = &private_chroot ;
		mk_dir(private_chroot) ;
		unlnk_inside(private_chroot) ;
		trace("top_lvls",private_chroot,top_lvls) ;
		for( ::string const& f : top_lvls ) {
			::string src_f     = g_start_info.chroot+f ;
			::string private_f = private_chroot     +f ;
			switch (FileInfo(src_f).tag()) {
				case FileTag::Reg   :
				case FileTag::Empty :
				case FileTag::Exe   : OFStream{private_f                } ; break    ;                                                              // create file
				case FileTag::Dir   : mk_dir  (private_f                ) ; break    ;                                                              // create dir
				case FileTag::Lnk   : lnk     (private_f,read_lnk(src_f)) ; continue ;                                                              // copy symlink
				default             :                                       continue ;                                                              // exclude weird files
			}
			_bind_mount(src_f,private_f) ;
		}
		if (must_create_root) { SWEAR(g_start_info.root_dir.rfind('/')==0,g_start_info.root_dir) ; mk_dir(private_chroot+g_start_info.root_dir) ; } // XXX : handle cases where dir is not top level
		if (must_create_tmp ) { SWEAR(g_start_info.tmp_dir .rfind('/')==0,g_start_info.tmp_dir ) ; mk_dir(private_chroot+g_start_info.tmp_dir ) ; } // .
	}
	if ( +g_start_info.root_dir                  ) _bind_mount( g_phy_root_dir          , *used_chroot+g_start_info.root_dir ) ;
	if ( +g_start_info.tmp_dir && +g_phy_tmp_dir ) _bind_mount( g_phy_tmp_dir           , *used_chroot+g_start_info.tmp_dir  ) ;
	if ( +g_start_info.tmp_dir && !g_phy_tmp_dir ) _tmp_mount ( g_start_info.tmp_sz_mb  , *used_chroot+g_start_info.tmp_dir  ) ;
	//
	trace("chroot",*used_chroot) ;
	if (+*used_chroot) _chroot(*used_chroot) ;
	//
	_atomic_write( "/proc/self/uid_map"   , to_string(uid,' ',uid,' ',1,'\n') ) ;
	_atomic_write( "/proc/self/setgroups" , "deny"                            ) ;                                                                   // necessary to be allowed to write the gid_map
	_atomic_write( "/proc/self/gid_map"   , to_string(gid,' ',gid,' ',1,'\n') ) ;
	//
	if (::setuid(uid)!=0) throw to_string("cannot set uid as ",uid,strerror(errno)) ;
	if (::setgid(gid)!=0) throw to_string("cannot set gid as ",uid,strerror(errno)) ;
}

struct Digest {
	::vmap_s<TargetDigest> targets ;
	::vmap_s<DepDigest   > deps    ;
	::vector<NodeIdx     > crcs    ; // index in targets of entry for which we need to compute a crc
	::string               msg     ;
} ;

Digest analyze( bool at_end , bool killed=false ) {
	Trace trace("analyze",STR(at_end),g_gather.accesses.size()) ;
	Digest  res              ; res.deps.reserve(g_gather.accesses.size()) ;                                                                       // typically most of accesses are deps
	NodeIdx prev_parallel_id = 0                                     ;
	Pdate   relax            = Pdate(New)+g_start_info.network_delay ;
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
			&&	!( !ad.tflags[Tflag::Target] && ad.tflags[Tflag::Incremental] )                // fast path : no matching, no pollution, no washing => forget it
			)
		;
		// handle deps
		if (is_dep) {
			DepDigest dd { ad.accesses , info.dep_info , ad.dflags } ;
			//
			if ( ad.accesses[Access::Stat] && ad.extra_dflags[ExtraDflag::StatReadData] ) dd.accesses = ~Accesses() ;
			//
			// if file is not old enough, we make it hot and server will ensure job producing dep was done before this job started
			dd.hot           = info.dep_info.kind==DepInfoKind::Info && !info.dep_info.info().date.avail_at(first_read.first,g_start_info.date_prec) ;
			dd.parallel      = info.parallel_id && info.parallel_id==prev_parallel_id                                                                ;
			prev_parallel_id = info.parallel_id                                                                                                      ;
			//
			if ( +dd.accesses && !dd.is_crc ) {                                                // try to transform date into crc as far as possible
				if      ( info.seen==Pdate::Future||info.seen>info.write ) dd.crc(Crc::None) ; // the whole job has been executed without seeing the file (before possibly writing to it)
				else if ( !dd.sig()                                      ) dd.crc({}       ) ; // file was not present initially but was seen, it is incoherent even if not present finally
				else if ( ad.write!=No                                   ) {}                  // cannot check stability as we wrote to it, clash will be detecte in server if any
				else if ( FileSig sig{file} ; sig!=dd.sig()              ) dd.crc({}       ) ; // file dates are incoherent from first access to end of job, dont know what has been read
				else if ( !sig                                           ) dd.crc({}       ) ; // file is awkward
				else if ( !Crc::s_sense(dd.accesses,sig.tag())           ) dd.crc(sig.tag()) ; // just record the tag if enough to match (e.g. accesses==Lnk and tag==Reg)
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
			if (is_dep                        ) td.tflags   |= Tflag::Incremental              ;                    // if is_dep, previous target state is guaranteed by being a dep, use it
			if (!td.tflags[Tflag::Incremental]) td.polluted  = info.dep_info.seen(ad.accesses) ;                    // polluted means that target was seen as existing before execution
			if ( is_dep && !unlnk ) {
				trace("dep_and_target",ad,flags) ;
				const char* read ;
				switch (first_read.second) {
					case Access::Lnk  : read = "readlink" ; break ;
					case Access::Stat : read = "stat"     ; break ;
					default           : read = "read"     ;
				}
				append_to_string( res.msg , read," as dep before being known as a target : ",mk_file(file),'\n' ) ;
				ad.tflags |= Tflag::Incremental ;                                                                   // file will have a predictible content, no reason to wash it
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
					else if ( killed || !td.tflags[Tflag::Target] ) { FileSig sig{file} ; td.crc = Crc(sig.tag()) ; td.sig = sig ; } // no crc if meaningless
					else                                              res.crcs.emplace_back(res.targets.size()) ; // record index in res.targets for deferred (parallel) crc computation
				break ;
			DF}
			if ( td.tflags[Tflag::Target] && !td.tflags[Tflag::Phony] ) {
				if ( td.tflags[Tflag::Static] && !td.extra_tflags[ExtraTflag::Optional] ) {
					if (unlnk        ) append_to_string( res.msg , "missing static target " , mk_file(file,No/*exists*/) , '\n' ) ;
				} else {
					// unless static and non-optional or phony, a target loses its official status if not actually produced
					if (ad.write==Yes) { if (unlnk           ) td.tflags &= ~Tflag::Target ; }
					else               { if (!is_target(file)) td.tflags &= ~Tflag::Target ; }
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

::vmap_s<DepDigest> cur_deps_cb() { return analyze(false/*at_end*/).deps ; }

::vector_s cmd_line() {
	::vector_s cmd_line = ::move(g_start_info.interpreter) ;                                                     // avoid copying as interpreter is used only here
	if ( g_start_info.use_script || (g_start_info.cmd.first.size()+g_start_info.cmd.second.size())>ARG_MAX/2 ) { // env+cmd line must not be larger than ARG_MAX, keep some margin for env
		::string cmd_file = to_string(PrivateAdminDir,"/cmds/",g_start_info.small_id) ;
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
	g_trace_file = new ::string{to_string(g_phy_root_dir,'/',PrivateAdminDir,"/trace/job_exec/",g_trace_id)} ;
	//
	JobRpcReq end_report { JobProc::End , g_seq_id , g_job , {.status=Status::EarlyErr,.end_date=start_overhead} } ; // prepare to return an error, so we can goto End anytime
	//
	if (::chdir(g_phy_root_dir.c_str())!=0) {
		append_to_string(end_report.msg,"cannot chdir to root : ",g_phy_root_dir) ;
		goto End ;
	}
	Trace::s_sz = 10<<20 ;        // this is more than enough
	unlnk(*g_trace_file) ;        // ensure that if another job is running to the same trace, its trace is unlinked to avoid clash
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
			//             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			/**/           OMsgBuf().send                ( fd , JobRpcReq{JobProc::Start,g_seq_id,g_job,server_fd.port()} ) ;
			g_start_info = IMsgBuf().receive<JobRpcReply>( fd                                                             ) ;
			//             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			trace("no_server",g_service_start,STR(found_server),e) ;
			if (found_server) exit(Rc::Fail                                                          ) ; // this is typically a ^C
			else              exit(Rc::Fail,"cannot communicate with server ",g_service_start," : ",e) ; // this may be a server config problem, better to report
		}
		trace("g_start_info",Pdate(New),g_start_info) ;
		switch (g_start_info.proc) {
			case JobProc::None  : return 0 ;                                                             // server ask us to give up
			case JobProc::Start : break    ;                                                             // normal case
		DF}
		//
		bool keep_tmp = +g_start_info.autodep_env.tmp_dir ;
		//
		g_root_dir = +g_start_info.root_dir ? &g_start_info.root_dir : &g_phy_root_dir ;
		//
		g_nfs_guard.reliable_dirs = g_start_info.autodep_env.reliable_dirs ;
		//
		for( auto const& [d ,digest] : g_start_info.deps           ) if (digest.dflags[Dflag::Static]) g_match_dct.add( false/*star*/ , d  , digest.dflags ) ;
		for( auto const& [dt,mf    ] : g_start_info.static_matches )                                   g_match_dct.add( false/*star*/ , dt , mf            ) ;
		for( auto const& [p ,mf    ] : g_start_info.star_matches   )                                   g_match_dct.add( true /*star*/ , p  , mf            ) ;
		//
		::map_ss cmd_env ;
		try {
			cmd_env = prepare_env(end_report) ;
			prepare_namespace() ;
		} catch (::string const& e) {
			end_report.msg += e ; goto End ;
		}
		trace("prepared",g_start_info.autodep_env,g_phy_tmp_dir) ;
		//
		g_gather.addr             = g_start_info.addr          ;
		g_gather.as_session       = true                       ;
		g_gather.autodep_env      = g_start_info.autodep_env   ;
		g_gather.chroot           = g_start_info.chroot        ;
		g_gather.cur_deps_cb      = cur_deps_cb                ;
		g_gather.cwd              = g_start_info.cwd_s         ; if (+g_gather.cwd) g_gather.cwd.pop_back() ;
		g_gather.env              = &cmd_env                   ;
		g_gather.job              = g_job                      ;
		g_gather.kill_sigs        = g_start_info.kill_sigs     ;
		g_gather.live_out         = g_start_info.live_out      ;
		g_gather.method           = g_start_info.method        ;
		g_gather.network_delay    = g_start_info.network_delay ;
		g_gather.seq_id           = g_seq_id                   ;
		g_gather.server_master_fd = ::move(server_fd)          ;
		g_gather.service_mngt     = g_service_mngt             ;
		g_gather.timeout          = g_start_info.timeout       ;
		//
		trace("wash",g_start_info.pre_actions) ;
		::pair_s<bool/*ok*/> wash_report = do_file_actions( g_washed , ::move(g_start_info.pre_actions) , g_nfs_guard , g_start_info.hash_algo ) ;
		end_report.msg += wash_report.first ;
		if (!wash_report.second) { end_report.digest.status = Status::LateLostErr ; goto End ; }
		g_gather.new_deps( start_overhead , ::move(g_start_info.deps) , g_start_info.stdin ) ;
		// non-optional static targets must be reported in all cases
		for( auto const& [t,f] : g_match_dct.knowns ) if ( f.is_target==Yes && !f.extra_tflags()[ExtraTflag::Optional] ) g_gather.new_unlnk(start_overhead,t) ;
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
		//              vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		Status status = g_gather.exec_child( cmd_line() , child_stdin , child_stdout , Child::Pipe ) ;
		//              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		struct rusage rsrcs ; getrusage(RUSAGE_CHILDREN,&rsrcs) ;
		//
		Digest digest = analyze(true/*at_end*/,status==Status::Killed) ;
		trace("analysis",g_gather.start_time,g_gather.end_time,status,g_gather.msg,digest.msg) ;
		//
		end_report.msg += compute_crcs(digest) ;
		//
		if (!g_start_info.autodep_env.reliable_dirs) {                                                   // fast path : avoid listing targets & guards if reliable_dirs
			for( auto const& [t,_] : digest.targets  ) g_nfs_guard.change(t) ;                           // protect against NFS strange notion of coherence while computing crcs
			for( auto const&  f    : g_gather.guards ) g_nfs_guard.change(f) ;                           // .
			g_nfs_guard.close() ;
		}
		//
		if ( g_gather.seen_tmp && !keep_tmp )
			try { unlnk_inside(g_start_info.autodep_env.tmp_dir) ; } catch (::string const&) {}          // cleaning is done at job start any way, so no harm
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
		,	.end_date     = g_gather.end_time
		,	.stats{
				.cpu { Delay(rsrcs.ru_utime) + Delay(rsrcs.ru_stime) }
			,	.job { g_gather.end_time-g_gather.start_time         }
			,	.mem = size_t(rsrcs.ru_maxrss<<10)
			}
		} ;
	}
End :
	Trace trace("end",end_report.digest.status) ;
	try {
		ClientSockFd fd           { g_service_end , NConnectionTrials } ;
		Pdate        end_overhead = New                                 ;
		end_report.digest.stats.total = end_overhead - start_overhead ;                                  // measure overhead as late as possible
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		OMsgBuf().send( fd , end_report ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		trace("done",end_overhead) ;
	} catch (::string const& e) { exit(Rc::Fail,"after job execution : ",e) ; }
	//
	return 0 ;
}
