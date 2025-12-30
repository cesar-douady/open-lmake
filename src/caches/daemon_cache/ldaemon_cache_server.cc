// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "app.hh"
#include "hash.hh"
#include "process.hh"

#include "caches/daemon_cache.hh"
#include "daemon_cache_utils.hh"

#include "engine.hh"

using namespace Caches ;
using namespace Disk   ;
using namespace Hash   ;

SmallIds<uint64_t> _g_upload_keys  ;
::vector<DiskSz>   _g_reserved_szs ; // indexed by upload_key

DaemonCache::RpcReply download(DaemonCache::RpcReq const& crr) {
	Trace trace("download",crr) ;
	DaemonCache::RpcReply      res    { .proc=DaemonCache::Proc::Download , .hit_info=CacheHitInfo::NoJob } ;
	Cjob                       job    { crr.job }                                                           ; if (!job) { trace("no_job") ; return res ; }
	CompileDigest              deps   = compile( crr.repo_deps , true/*for_download*/ )                     ; SWEAR( deps.n_statics==job->n_statics , crr.job,job ) ;
	::pair<Crun,CacheHitInfo > digest = job->match( deps.deps , deps.dep_crcs )                             ;
	//
	/**/                                 res.hit_info = digest.second               ;
	if (res.hit_info<CacheHitInfo::Miss) res.dir_s    = digest.first->name(job)+'/' ;
	trace(res.hit_info) ;
	return res ;
}

DaemonCache::RpcReply upload(DaemonCache::RpcReq const& crr) {
	Trace trace("upload",crr) ;
	//
	if (!mk_room(crr.reserved_sz)) return {.proc=DaemonCache::Proc::Upload} ; // no upload possible
	//
	uint64_t upload_key = _g_upload_keys.acquire() ;
	grow(_g_reserved_szs,upload_key) = crr.reserved_sz ;
	return { .proc=DaemonCache::Proc::Upload , .upload_key = upload_key } ;
}

void commit(DaemonCache::RpcReq const& crr) {
	Trace trace("commit",crr) ;
	//
	release_room(_g_reserved_szs[crr.upload_key]) ;
	_g_upload_keys.release(crr.upload_key) ;
	_g_reserved_szs[crr.upload_key] = 0 ;
	//
	NfsGuard      nfs_guard    { g_config.file_sync }                                            ;
	::string      rf           = DaemonCache::s_reserved_file(crr.upload_key)                    ;
	CompileDigest deps         = compile( crr.job_info.end.digest.deps , false/*for_download*/ ) ;
	Cjob          job          { New , crr.job , deps.n_statics }                                ;
	::string      job_info_str = serialize(crr.job_info)                                         ;
	DiskSz        sz           = run_sz( crr.job_info , job_info_str , deps )                    ;
	//
	::pair<Crun,CacheHitInfo > digest = job->insert(
		deps.deps , deps.dep_crcs                                                                                         // to search entry
	,	crr.repo_key , true/*key_is_last*/ , New/*last_access*/ , sz , rate(g_config,sz,crr.job_info.end.digest.exe_time) // to create entry
	) ;
	//
	if (digest.second<CacheHitInfo::Miss) {
		unlnk( rf , {.nfs_guard=&nfs_guard} ) ;
	} else {
		::string run_name = digest.first->name(job)                                                                                            ;
		AcFd     fd       { run_name+"/info" , {.flags=O_WRONLY|O_CREAT|O_TRUNC,.mod=0666,.perm_ext=g_config.perm_ext,.nfs_guard=&nfs_guard} } ;
		fd.write(job_info_str)                                    ;
		rename( rf , run_name+"/data" , {.nfs_guard=&nfs_guard} ) ;
	}
}

void dismiss(DaemonCache::RpcReq const& crr) {
	Trace trace("dismiss",crr) ;
	unlnk(DaemonCache::s_reserved_file(crr.upload_key)) ;
	release_room(_g_reserved_szs[crr.upload_key])       ;
	//
	_g_upload_keys.release(crr.upload_key) ;
	_g_reserved_szs[crr.upload_key] = 0 ;
}

struct CacheServer : AutoServer<CacheServer> {
	using Proc     = DaemonCache::Proc     ;
	using RpcReq   = DaemonCache::RpcReq   ;
	using RpcReply = DaemonCache::RpcReply ;
	using Item     = RpcReq                ;
	static constexpr uint64_t Magic = DaemonCache::Magic ; // any random improbable value!=0 used as a sanity check when client connect to server
	// cxtors & casts
	using AutoServer<CacheServer>::AutoServer ;
	// injection
	bool/*done*/ process_item( Fd fd , RpcReq const& crr ) {
		Trace trace("process_item",fd,crr) ;
		switch (crr.proc) { //!                                                                                   key            done
			case Proc::None     :                                                                                         return true  ;
			case Proc::Config   : OMsgBuf( DaemonCache::RpcReply{.proc=Proc::Config,.config=g_config} ).send( fd , {} ) ; return false ; // from lmake_server
			case Proc::Download : OMsgBuf( download(crr)                                              ).send( fd , {} ) ; return false ; // from lmake_server
			case Proc::Upload   : OMsgBuf( upload  (crr)                                              ).send( fd , {} ) ; return true  ; // from job_exec
			case Proc::Commit   :          commit  (crr)                                                                ; return false ; // from lmake_server
			case Proc::Dismiss  :          dismiss (crr)                                                                ; return false ; // .
		DF}                                                                                                                              // NO_COV
	}
} ;

static CacheServer _g_server { ServerMrkr } ;

int main( int argc , char** argv ) {
	app_init({
		.chk_version  = Maybe
	,	.cd_root      = false // daemon is always launched at root
	,	.read_only_ok = false
	,	.root_mrkrs   = { cat(AdminDirS,"config.py") }
	,	.version      = Version::DaemonCache
	}) ;
	Py::init(*g_lmake_root_s) ;
	//
	bool is_daemon = true ;
	for( int i : iota(1,argc) ) {
		if (argv[i][0]=='-')
			switch (argv[i][1]) {
				case 'd' : is_daemon = false ; if (argv[i][2]==0) continue ; break ;
				case '-' :                     if (argv[i][2]==0) continue ; break ;
			DN}
		exit(Rc::Usage,"unrecognized argument : ",argv[i],"\nsyntax :",*g_exe_name," [-d/*no_daemon*/]") ;
	}
	Trace trace("main",*g_lmake_root_s,*g_repo_root_s) ;
	for( int i : iota(argc) ) trace("arg",i,argv[i]) ;
	//
	try                       { g_config = New ;                                                                                  }
	catch (::string const& e) { exit( Rc::Usage , "while configuring ",*g_exe_name," in dir ",*g_repo_root_s,rm_slash," : ",e ) ; }
	try {
		_g_server.is_daemon = is_daemon ;
		_g_server.writable  = true      ;
		_g_server.start() ;
	} catch (::pair_s<Rc> const& e) {
		if (+e.first) exit( e.second , "cannot start ",*g_exe_name," : ",e.first ) ;
		else          exit( Rc::Ok                                               ) ;
	}
	//
	mk_dir_empty_s(cat(AdminDirS,"reserved/")) ;
	//
	daemon_cache_init(_g_server.rescue) ;
	bool interrupted = _g_server.event_loop() ;
	daemon_cache_finalize() ;
	//
	trace("done",STR(interrupted),New) ;
	return interrupted ;
}
