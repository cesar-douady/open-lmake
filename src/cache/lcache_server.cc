// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "app.hh"
#include "hash.hh"
#include "process.hh"

#include "cache_utils.hh"
#include "engine.hh"
#include "rpc_cache.hh"

using namespace Cache ;
using namespace Disk  ;
using namespace Hash  ;

SmallIds<CacheUploadKey> _g_upload_keys  ;
::vector<DiskSz>         _g_reserved_szs ; // indexed by upload_key
::umap<Fd,Ckey>          _g_key_tab      ;

static void _release_room(DiskSz sz) {
	CrunHdr& hdr = CrunData::s_hdr() ;
	Trace trace("_release_room",sz,hdr.total_sz,g_reserved_sz) ;
	SWEAR( g_reserved_sz>=sz , g_reserved_sz,sz ) ;
	g_reserved_sz -= sz ;
	SWEAR( hdr.total_sz+g_reserved_sz <= g_cache_config.max_sz , hdr.total_sz,g_reserved_sz,g_cache_config.max_sz ) ;
}

static CacheRpcReply _config( Fd fd , CacheRpcReq const& crr ) {
	Trace trace("_config",fd,crr) ;
	Ckey key      { New , crr.repo_key }                      ;
	bool inserted = _g_key_tab.try_emplace( fd , key ).second ; SWEAR( inserted , fd,crr ) ;
	// ensure lcache_repair can retrieve repo keys
	if (!key->ref_cnt) AcFd(cat(PrivateAdminDirS,"repo_keys"),{.flags=O_WRONLY|O_APPEND|O_CREAT,.mod=0666,.perm_ext=g_cache_config.perm_ext}).write(cat(+key,' ',crr.repo_key,'\n')) ;
	//
	key.inc() ;
	return { .proc=CacheRpcProc::Config , .config=g_cache_config } ;
}

static CacheRpcReply _download(CacheRpcReq const& crr) {
	Trace trace("_download",crr) ;
	CacheRpcReply              res    { .proc=CacheRpcProc::Download , .hit_info=CacheHitInfo::NoJob } ;
	Cjob                       job    = crr.job.is_name() ? Cjob(crr.job.name) : Cjob(crr.job.id)      ; if (!job) { trace("no_job") ; return res ; }
	CompileDigest              deps   { crr.repo_deps , true/*for_download*/ , &res.dep_ids }          ; SWEAR( deps.n_statics==job->n_statics , crr.job,deps.n_statics,job,job->n_statics ) ;
	::pair<Crun,CacheHitInfo > digest = job->match( deps.deps , deps.dep_crcs )                        ;
	if (crr.job.is_name()) res.job_id = +job ;
	//
	res.hit_info = digest.second ;
	if (res.hit_info<CacheHitInfo::Miss) {
		res.key         = +digest.first->key         ;
		res.key_is_last =  digest.first->key_is_last ;
	}
	trace(res) ;
	return res ;
}

static CacheRpcReply _upload(CacheRpcReq const& crr) {
	Trace trace("_upload",crr) ;
	//
	try                       { mk_room(crr.reserved_sz) ;                       }
	catch (::string const& e) { return { .proc=CacheRpcProc::Upload , .msg=e } ; } // no upload possible
	//
	CacheUploadKey upload_key = _g_upload_keys.acquire() ;
	g_reserved_sz                    += crr.reserved_sz ;
	grow(_g_reserved_szs,upload_key)  = crr.reserved_sz ;
	return { .proc=CacheRpcProc::Upload , .upload_key = upload_key } ;
}

static void _commit( Fd fd , CacheRpcReq const& crr ) {
	Trace trace("_commit",crr) ;
	//
	_release_room(_g_reserved_szs[crr.upload_key]) ;
	_g_upload_keys.release(crr.upload_key)         ;
	_g_reserved_szs[crr.upload_key] = 0 ;
	//
	NfsGuard      nfs_guard { g_cache_config.file_sync }                                                   ;
	::string      rf        = reserved_file(crr.upload_key)                                                ;
	CompileDigest deps      { crr.repo_deps , false/*for_download*/ }                                      ;
	Cjob          job       = crr.job.is_name() ? Cjob(New,crr.job.name,deps.n_statics) : Cjob(crr.job.id) ; SWEAR( job->n_statics==deps.n_statics , job,deps.n_statics ) ;
	DiskSz        sz        = run_sz( crr.total_z_sz , crr.job_info_sz , deps )                            ;
	//
	::pair<Crun,CacheHitInfo> digest ;
	try {
		digest = job->insert(
			deps.deps , deps.dep_crcs                                                                                   // to search entry
		,	_g_key_tab.at(fd) , true/*key_is_last*/ , New/*last_access*/ , sz , to_rate(g_cache_config,sz,crr.exe_time) // to create entry
		) ;
	} catch (::string const&) {
		digest.second = {} ;                                                                                            // we dont report on commit, so just force dismiss
	}
	if (digest.second<CacheHitInfo::Miss) {
		unlnk( rf+"-data" , {.nfs_guard=&nfs_guard} ) ;
		unlnk( rf+"-info" , {.nfs_guard=&nfs_guard} ) ;
	} else {
		::string run_name = digest.first->name(job) ;
		// START_OF_VERSIONING CACHE
		rename( rf+"-data" , run_name+"-data" , {.nfs_guard=&nfs_guard} ) ;
		rename( rf+"-info" , run_name+"-info" , {.nfs_guard=&nfs_guard} ) ;
		// END_OF_VERSIONING
	}
}

static void _dismiss(CacheRpcReq const& crr) {
	Trace trace("dismiss",crr) ;
	_release_room(_g_reserved_szs[crr.upload_key]) ;
	_g_upload_keys.release(crr.upload_key)         ;
	_g_reserved_szs[crr.upload_key] = 0 ;
	//
	::string rf = reserved_file(crr.upload_key) ;
	unlnk(rf+"-data") ;
	unlnk(rf+"-info") ;
}

struct CacheServer : AutoServer<CacheServer> {
	using Proc     = CacheRpcProc  ;
	using RpcReq   = CacheRpcReq   ;
	using RpcReply = CacheRpcReply ;
	using Item     = RpcReq        ;
	static constexpr uint64_t Magic = CacheMagic ;                                            // any random improbable value!=0 used as a sanity check when client connect to server
	// cxtors & casts
	using AutoServer<CacheServer>::AutoServer ;
	// injection
	void start_connection(Fd) {
		if (n_connections()==1) cache_empty_trash() ;
	}
	void end_connection(Fd fd) {
		auto it = _g_key_tab.find(fd) ;
		if (it!=_g_key_tab.end()) it->second.dec() ;
	}
	Bool3/*done*/ process_item( Fd fd , RpcReq const& crr ) {
		Trace trace("process_item",fd,crr) ;
		switch (crr.proc) { //!                                          key           done
			case Proc::None     :                                                return Yes ;
			case Proc::Config   : OMsgBuf( _config  (fd,crr) ).send( fd , {} ) ; return No  ; // from lmake_server
			case Proc::Download : OMsgBuf( _download(   crr) ).send( fd , {} ) ; return No  ; // from lmake_server
			case Proc::Upload   : OMsgBuf( _upload  (   crr) ).send( fd , {} ) ; return Yes ; // from job_exec
			case Proc::Commit   :          _commit  (fd,crr)                   ; return No  ; // from lmake_server
			case Proc::Dismiss  :          _dismiss (   crr)                   ; return No  ; // .
		DF}                                                                                   // NO_COV
	}
} ;

static CacheServer _g_server { ServerMrkr } ;

int main( int argc , char** argv ) {
	app_init({
		.cd_root      = false // daemon is always launched at root
	,	.chk_version  = Maybe
	,	.clean_msg    = cache_clean_msg()
	,	.read_only_ok = false
	,	.root_mrkrs   = { cat(AdminDirS,"config.py") }
	,	.version      = Version::Cache
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
	cache_init(_g_server.rescue) ;
	bool interrupted = _g_server.event_loop() ;
	cache_finalize() ;
	//
	trace("done",STR(interrupted),New) ;
	return interrupted ;
}
