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

struct ConnEntry {
	Ckey                   key         = {} ;
	::uset<CacheUploadKey> upload_keys = {} ;
} ;

SmallIds<CacheUploadKey> _g_upload_keys  ;
::vector<CacheUploadKey> _g_reserved_szs ;
::umap<Fd,ConnEntry>     _g_conn_tab     ;

static void _release_room(DiskSz sz) {
	CrunHdr& hdr = CrunData::s_hdr() ;
	Trace trace("_release_room",sz,hdr.total_sz,g_reserved_sz) ;
	SWEAR( g_reserved_sz>=sz , g_reserved_sz,sz ) ;
	g_reserved_sz -= sz ;
	SWEAR( hdr.total_sz+g_reserved_sz <= g_cache_config.max_sz , hdr.total_sz,g_reserved_sz,g_cache_config.max_sz ) ;
}

static CacheRpcReply _config( Fd fd , ::string const& repo_key ) {
	Trace trace("_config",fd,repo_key) ;
	Ckey key      { New , repo_key }                                           ;
	bool inserted = _g_conn_tab.try_emplace( fd , ConnEntry{.key=key} ).second ; SWEAR( inserted , fd,repo_key ) ;
	// ensure lcache_repair can retrieve repo keys
	if (!key->ref_cnt) AcFd(cat(PrivateAdminDirS,"repo_keys"),{O_WRONLY|O_APPEND|O_CREAT}).write(cat(+key,' ',repo_key,'\n')) ;
	//
	key.inc() ;
	return { .proc=CacheRpcProc::Config , .config=g_cache_config , .conn_id=uint32_t(fd.fd+1) } ; // conn_id=0 is reserved to mean no id
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

static CacheRpcReply _upload( Fd fd , DiskSz reserved_sz ) {
	Trace trace("_upload",fd,reserved_sz) ;
	auto it = _g_conn_tab.find(fd) ;
	if (it==_g_conn_tab.end()) { trace("conn_not_found") ; return { .proc=CacheRpcProc::Upload , .msg="cache is diabled" } ; }
	//
	try                       { mk_room(reserved_sz) ;                                              }
	catch (::string const& e) { trace("throw",e) ; return { .proc=CacheRpcProc::Upload , .msg=e } ; } // no upload possible
	//
	CacheUploadKey upload_key = _g_upload_keys.acquire() ;
	it->second.upload_keys.insert(upload_key) ;
	g_reserved_sz                    += reserved_sz ;
	grow(_g_reserved_szs,upload_key)  = reserved_sz ;
	trace("done",upload_key) ;
	return { .proc=CacheRpcProc::Upload , .upload_key=upload_key } ;
}

static void _commit( Fd fd , CacheRpcReq const& crr ) {
	Trace trace("_commit",crr) ;
	//
	_release_room(_g_reserved_szs[crr.upload_key]) ;
	_g_upload_keys.release(crr.upload_key)         ;
	_g_reserved_szs[crr.upload_key] = 0 ;
	//
	NfsGuard                  nfs_guard  { g_cache_config.file_sync }                                                   ;
	::string                  rf         = reserved_file(crr.upload_key)                                                ;
	CompileDigest             deps       { crr.repo_deps , false/*for_download*/ }                                      ;
	Cjob                      job        = crr.job.is_name() ? Cjob(New,crr.job.name,deps.n_statics) : Cjob(crr.job.id) ; SWEAR( job->n_statics==deps.n_statics , job,job->n_statics,deps.n_statics ) ;
	DiskSz                    sz         = run_sz( crr.total_z_sz , crr.job_info_sz , deps )                            ;
	ConnEntry&                conn_entry = _g_conn_tab.at(fd)                                                           ;
	::pair<Crun,CacheHitInfo> digest     ;
	conn_entry.upload_keys.erase(crr.upload_key) ;
	try {
		digest = job->insert(
			deps.deps , deps.dep_crcs                                                                                                                         // to search entry
		,	conn_entry.key , crr.override_first?KeyIsLast::OverrideFirst:KeyIsLast::Plain , New/*last_access*/ , sz , to_rate(g_cache_config,sz,crr.exe_time) // to create entry
		) ;
	} catch (::string const&) {
		digest.second = {} ;    // we dont report on commit, so just force dismiss
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

static void _dismiss( Fd fd , CacheUploadKey upload_key ) {
	Trace trace("dismiss",upload_key) ;
	_release_room(_g_reserved_szs[upload_key]) ;
	_g_upload_keys.release(upload_key)         ;
	_g_reserved_szs[upload_key] = 0 ;
	_g_conn_tab.at(fd).upload_keys.erase(upload_key) ;
	//
	::string rf = reserved_file(upload_key) ;
	unlnk(rf+"-data") ;
	unlnk(rf+"-info") ;
	trace("done") ;
}

struct CacheServer : AutoServer<CacheServer> {
	using Proc     = CacheRpcProc  ;
	using RpcReq   = CacheRpcReq   ;
	using RpcReply = CacheRpcReply ;
	using Item     = RpcReq        ;
	static constexpr uint64_t Magic = CacheMagic ;                                                             // AutoServer expects Magic definition
	// cxtors & casts
	using AutoServer<CacheServer>::AutoServer ;
	// injection
	void end_connection(Fd fd) {
		if (n_connections()==1) cache_empty_trash() ;
		//
		auto       it         = _g_conn_tab.find(fd) ; if (it==_g_conn_tab.end()) return ;
		ConnEntry& conn_entry = it->second           ;
		//
		for( CacheUploadKey upload_key : mk_vector(conn_entry.upload_keys) ) _dismiss( fd , upload_key ) ;     // copy upload_keys as _dismiss erases entries in it
		conn_entry.key.dec() ;
		_g_conn_tab.erase(it) ;
	}
	Bool3/*done*/ process_item( Fd fd , RpcReq&& crr ) {
		Trace trace("process_item",fd,crr) ;
		Fd conn_fd = crr.conn_id ? Fd(crr.conn_id-1) : fd ;                                                    // get fd from conn_id when coming from job_exec
		switch (crr.proc) { //!                                                           key           done
			case Proc::None     :                                                                 return Yes ;
			case Proc::Config   : OMsgBuf( _config  (conn_fd,crr.repo_key   ) ).send( fd , {} ) ; return No  ; // from lmake_server
			case Proc::Download : OMsgBuf( _download(        crr            ) ).send( fd , {} ) ; return No  ; // from lmake_server
			case Proc::Upload   : OMsgBuf( _upload  (conn_fd,crr.reserved_sz) ).send( fd , {} ) ; return Yes ; // from job_exec
			case Proc::Commit   :          _commit  (conn_fd,crr            )                   ; return No  ; // from lmake_server
			case Proc::Dismiss  :          _dismiss (conn_fd,crr.upload_key )                   ; return No  ; // .
		DF}                                                                                                    // NO_COV
	}
} ;

static CacheServer _g_server { ServerMrkr } ;

int main( int argc , char** argv ) {
	Trace::s_backup_trace = true ;
	//
	FileStat st ; if (::lstat(".",&st)!=0) FAIL() ; SWEAR( S_ISDIR(st.st_mode) ) ;
	::umask(~st.st_mode) ;                                                         // ensure permissions on top-level dir are propagated to all underlying dirs and files
	//
	app_init({
		.cd_root      = false                                                      // daemon is always launched at root
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
