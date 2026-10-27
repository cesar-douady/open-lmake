// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "app.hh"
#include "process.hh"

#include "cache_utils.hh"
#include "engine.hh"
#include "rpc_cache.hh"

using namespace Cache ;
using namespace Disk  ;

struct ConnEntry {
	Ckey                   key         = {} ;
	uint32_t               id          = 0  ;
	::uset<CacheUploadKey> upload_keys = {} ;
} ;

SmallIds<CacheUploadKey> _g_upload_keys  ;
::vector<DiskSz        > _g_reserved_szs ;
::umap<Fd,ConnEntry>     _g_conn_tab     ;
::umap<uint32_t,Fd>      _g_conn_fds     ;     // conn ids are not reused so that a late request from a job_exec cannot reach a new connection reusing the same fd
uint32_t                 _g_conn_id      = 0 ; // may wrap, to clash, we need 1000 lmake commands/s while a single one last more than 10 years, more than comfortable

static void _release_room(DiskSz sz) {
	CrunHdr& hdr = CrunData::s_hdr() ;
	Trace trace("_release_room",sz,hdr.total_sz,g_reserved_sz) ;
	SWEAR( g_reserved_sz>=sz , g_reserved_sz,sz ) ;
	g_reserved_sz -= sz ;
	SWEAR( hdr.total_sz+g_reserved_sz <= g_cache_config.max_sz , hdr.total_sz,g_reserved_sz,g_cache_config.max_sz ) ;
}

static CacheRpcReply _config( Fd fd , ::string const& repo_key ) {
	Trace trace("_config",fd,repo_key) ;
	Ckey     key      { New , repo_key }                                                       ;
	uint32_t conn_id  = _g_conn_id++                                                           ; if (!conn_id) conn_id = _g_conn_id++ ; // conn_id=0 is reserved to mean no id, even if it wraps
	bool     inserted = _g_conn_tab.try_emplace( fd , ConnEntry{.key=key,.id=conn_id} ).second ; SWEAR( inserted , fd,repo_key ) ;
	_g_conn_fds[conn_id] = fd ;
	// ensure lcache_repair can retrieve repo keys
	if (!key->ref_cnt) AcFd(cat(PrivateAdminDirS,"repo_keys"),{O_WRONLY|O_APPEND|O_CREAT}).write(cat(+key,' ',repo_key,'\n')) ;
	//
	key.inc() ;
	return { .proc=CacheRpcProc::Config , .config=g_cache_config , .conn_id=conn_id , .fqdn=fqdn(g_cache_config.domain_name) } ;
}

static CacheRpcReply _download(CacheRpcReq const& crr) {
	Trace trace("_download",crr) ;
	bool          is_name = crr.job.is_name()                                              ;
	CacheRpcReply res     { .proc=CacheRpcProc::Download , .hit_info=CacheHitInfo::NoJob } ;
	Cjob          job     = is_name ? Cjob(crr.job.name) : Cjob(crr.job.id)                ; if (!job) { trace("no_job") ; return res ; }
	CompileDigest deps    { crr.repo_deps , true/*for_download*/ , &res.dep_ids }          ; SWEAR_PROD( deps.n_statics==job->n_statics , is_name,crr.job,deps.n_statics,job,job->n_statics ) ;
	//
	::pair<Crun,CacheHitInfo > digest = job->match(deps) ;
	if (is_name) res.job_id = +job ;
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
	CompileDigest cd         { crr.repo_deps , false/*for_download*/ }                                    ;
	Cjob          job        = crr.job.is_name() ? Cjob(New,crr.job.name,cd.n_statics) : Cjob(crr.job.id) ; SWEAR_PROD( job->n_statics==cd.n_statics , job,job->n_statics,cd.n_statics ) ;
	DiskSz        sz         = run_sz( crr.total_z_sz , crr.job_info_sz , cd )                            ;
	ConnEntry&    conn_entry = _g_conn_tab.at(fd)                                                         ;
	::string      run_name   ;
	//
	conn_entry.upload_keys.erase(crr.upload_key) ;
	bool inserted = job->insert(
		cd                                                                                                                                                // to search entry
	,	conn_entry.key , crr.override_first?KeyIsLast::OverrideFirst:KeyIsLast::Plain , New/*last_access*/ , sz , to_rate(g_cache_config,sz,crr.exe_time) // to create entry
	,	crr.force , crr.targets_crc
	,	reserved_file(crr.upload_key) , &::ref(SyncGuard(g_file_sync))
	) ;
	trace("done",STR(inserted)) ;
}

static void _dismiss( Fd fd , CacheUploadKey upload_key ) {
	Trace trace("dismiss",fd,upload_key) ;
	auto it = _g_conn_tab.find(fd) ;
	if (it==_g_conn_tab.end()                    ) { trace("no_conn") ; return ; } // connection is closed and its uploads have already been dismissed
	if (!it->second.upload_keys.erase(upload_key)) { trace("no_key" ) ; return ; } // upload has already been committed or dismissed
	_release_room(_g_reserved_szs[upload_key]) ;
	_g_upload_keys.release(upload_key)         ;
	_g_reserved_szs[upload_key] = 0 ;
	//
	unlnk_run( reserved_file(upload_key) , &::ref(SyncGuard(g_file_sync)) ) ;
	trace("done") ;
}

struct CacheServer : AutoServer<CacheServer> {
	using Proc     = CacheRpcProc  ;
	using RpcReq   = CacheRpcReq   ;
	using RpcReply = CacheRpcReply ;
	using Item     = RpcReq        ;
	static constexpr uint64_t Magic = CacheMagic ;                                                                                   // AutoServer expects Magic definition
	// cxtors & co
	using AutoServer<CacheServer>::AutoServer ;
	// injection
	void end_connection(Fd fd) {
		if (n_connections()==1) cache_empty_trash() ;
		//
		auto       it         = _g_conn_tab.find(fd) ; if (it==_g_conn_tab.end()) return ;
		ConnEntry& conn_entry = it->second           ;
		//
		for( CacheUploadKey upload_key : mk_vector(conn_entry.upload_keys) ) _dismiss( fd , upload_key ) ;                           // copy upload_keys as _dismiss erases entries in it
		conn_entry.key.dec() ;
		_g_conn_fds.erase(conn_entry.id) ;                                                                                           // use conn_enty before it is erased from _g_conn_tab
		_g_conn_tab.erase(it           ) ;
	}
	Bool3/*done*/ process_item( Fd fd , RpcReq&& crr ) {
		Trace trace("process_item",fd,crr) ;
		Fd   conn_fd     = fd          ;
		bool from_remote = crr.conn_id ;
		if (from_remote) {                                                                                                           // get fd from conn_id when coming from job_exec
			auto it = _g_conn_fds.find(crr.conn_id) ;
			conn_fd = it==_g_conn_fds.end() ? Fd() : it->second ;                                                                    // connection may have been closed since job_exec was launched
		}
		switch (crr.proc) { //!                                                                                 key           done
			case Proc::None     :                                                                                       return Yes ;
			case Proc::Config   : SWEAR(!from_remote) ; OMsgBuf( _config  (conn_fd,crr.repo_key   ) ).send( fd , {} ) ; break      ;
			case Proc::Download : SWEAR(!from_remote) ; OMsgBuf( _download(        crr            ) ).send( fd , {} ) ; break      ;
			case Proc::Upload   : SWEAR( from_remote) ; OMsgBuf( _upload  (conn_fd,crr.reserved_sz) ).send( fd , {} ) ; break      ;
			case Proc::Commit   : SWEAR(!from_remote) ;          _commit  (conn_fd,crr            )                   ; break      ;
			case Proc::Dismiss  :                                _dismiss (conn_fd,crr.upload_key )                   ; break      ;
		DF}                                                                                                                          // NO_COV
		return No | from_remote ;                                                                                                    // connections from job_exec are terminated
	}
} ;

int main( int argc , char** argv ) {
	Trace::s_backup_trace = true ;
	//
	FileStat st ; if (::lstat(".",&st)!=0) FAIL_PROD() ; SWEAR( S_ISDIR(st.st_mode) ) ;
	::umask(~st.st_mode&0777) ;                                                         // ensure permissions on top-level dir are propagated to all underlying dirs and files
	//
	app_init({
		.cd_root      = false                                                           // daemon is always launched at root
	,	.chk_version  = Maybe
	,	.key          = "cache dir"
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
	CacheServer server { New } ;
	try {
		server.is_daemon = is_daemon ;
		server.writable  = true      ;
		server.start() ;
	} catch (::pair_s<Rc> const& e) {
		if (+e.first) exit( e.second , "cannot start ",*g_exe_name," : ",e.first ) ;
		else          exit( Rc::Ok                                               ) ;
	}
	//
	mk_dir_empty_s(cat(AdminDirS,"reserved/")) ;
	//
	cache_init(server.rescue) ;
	bool interrupted = server.event_loop() ;
	cache_finalize() ;
	//
	trace("done",STR(interrupted),New) ;
	return interrupted ;
}
