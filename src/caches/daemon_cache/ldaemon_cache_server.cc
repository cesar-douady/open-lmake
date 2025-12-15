// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "app.hh"
#include "hash.hh"
#include "process.hh"

#include "caches/daemon_cache.hh"

#include "engine.hh"

using namespace Caches ;
using namespace Disk   ;
using namespace Hash   ;
using namespace Py     ;

FileSync     g_file_sync = {} ;
PermExt      g_perm_ext  = {} ;
Disk::DiskSz g_max_sz    = 0  ;

SmallIds<uint64_t> _g_upload_keys  ;
::vector<DiskSz>   _g_reserved_szs ; // indexed by upload_key

::string reserved_file(uint64_t upload_key) {
	return cat(AdminDirS,"reserved/",upload_key) ;
}

struct CompileDigest {
	NodeIdx         n_statics = 0 ;
	::vector<Cnode> deps      ;
	::vector<Crc  > dep_crcs  ;
} ;

CompileDigest _compile( ::vmap_s<DepDigest> const& repo_deps , bool for_download ) {
	struct Dep {
		// services
		bool operator<(Dep const& other) const { return ::pair(bucket,+node) < ::pair(other.bucket,+other.node) ; }
		// data
		int   bucket = 0/*garbage*/ ;
		Cnode node   ;
		Crc   crc    ;
	} ;
	CompileDigest res  ;
	::vector<Dep> deps ;
	for( auto const& [n,dd] : repo_deps ) {
		Cnode node ;
		if (for_download) { node = {       n } ; if (!node) continue ; } // if it is not known in cache, it has no impact on matching
		else                node = { New , n } ;
		res.n_statics += dd.dflags[Dflag::Static] ;
		Crc crc = dd.crc() ;
		deps.push_back({
			.bucket = dd.dflags[Dflag::Static] ? 0 : crc!=Crc::None ? 1 : 2
		,	.node   = node
		,	.crc    = crc
		}) ;
	}
	::sort(deps) ;
	for( Dep const& dep : deps )                              res.deps    .push_back(dep.node) ;
	for( Dep const& dep : deps ) { if (dep.bucket==2) break ; res.dep_crcs.push_back(dep.crc ) ; }
	return res ;
}

DaemonCacheRpcReply download(DaemonCacheRpcReq const& crr) {
	Trace trace("download",crr) ;
	DaemonCacheRpcReply        res    { .proc=DaemonCacheRpcProc::Download }                     ;
	Cjob                       job    { crr.job }                                                ; if (!job) { res.digest.hit_info=CacheHitInfo::NoJob ; return res ; }
	CompileDigest              deps   = _compile( crr.repo_deps , true/*for_download*/ )         ;
	::pair<Crun,CacheHitInfo > digest = job->match( deps.n_statics , deps.deps , deps.dep_crcs ) ;
	//
	res.digest.hit_info = digest.second ;
	if (res.digest.hit_info>=CacheHitInfo::Miss) return res ;
	//
	::string run_name = digest.first->name(job) ;
	/**/                                         res.digest.job_info = deserialize<JobInfo>(AcFd(run_name+"/job_info").read()) ;
	if (res.digest.hit_info==CacheHitInfo::Hit ) res.file            =                           run_name+"/data"              ;
	return res ;
}

DaemonCacheRpcReply upload(DaemonCacheRpcReq const& crr) {
	Trace trace("upload",crr) ;
	mk_room(crr.reserved_sz) ;
	uint64_t upload_key = _g_upload_keys.acquire() ;
	grow(_g_reserved_szs,upload_key) = crr.reserved_sz ;
	DaemonCacheRpcReply res { .proc=DaemonCacheRpcProc::Upload , .file=mk_glb(reserved_file(upload_key),*g_repo_root_s) , .upload_key=1 } ;
	return res ;
}

static DiskSz _run_sz(JobInfo info) {
	return info.end.total_z_sz ;
}

static Rate _run_rate(JobInfo info) {
	float r = ::ldexpf(
		::logf( 1e9 * float(info.end.digest.exe_time) / _run_sz(info) ) // fastest throughput is 1GB/s (beyond that, cache is obviously needless)
	,	4
	) ;
	if (r<0      ) r = 0        ;
	if (r>=NRates) r = NRates-1 ;
	return r ;
}

void commit(DaemonCacheRpcReq const& crr) {
	Trace trace("commit",crr) ;
	//
	release_room(_g_reserved_szs[crr.upload_key]) ;
	_g_upload_keys.release(crr.upload_key) ;
	_g_reserved_szs[crr.upload_key] = 0 ;
	//
	::string                   rf     = reserved_file(crr.upload_key)                                                                                      ;
	Cjob                       job    { New , crr.job }                                                                                                    ;
	CompileDigest              deps   = _compile( crr.repo_deps , false/*for_download*/ )                                                                  ;
	::pair<Crun,CacheHitInfo > digest = job->insert( crr.repo_key , _run_sz(crr.info) , _run_rate(crr.info) , deps.n_statics , deps.deps , deps.dep_crcs ) ;
	//
	if (digest.second<CacheHitInfo::Miss) {
		unlnk(rf) ;
	} else {
		::string run_name = digest.first->name(job)                                                                   ;
		AcFd     fd       { run_name+"/job_info" , {.flags=O_WRONLY|O_CREAT|O_TRUNC,.mod=0666,.perm_ext=g_perm_ext} } ;
		fd.write(serialize(crr.info)) ;
		rename( rf , run_name+"/data" ) ;
	}
}

void dismiss(DaemonCacheRpcReq const& crr) {
	Trace trace("dismiss",crr) ;
	unlnk (reserved_file(crr.upload_key)) ;
	release_room(_g_reserved_szs[crr.upload_key]) ;
	//
	_g_upload_keys.release(crr.upload_key) ;
	_g_reserved_szs[crr.upload_key] = 0 ;
}

struct CacheServer : AutoServer<CacheServer> {
	using Proc     = DaemonCacheRpcProc  ;
	using RpcReq   = DaemonCacheRpcReq   ;
	using RpcReply = DaemonCacheRpcReply ;
	using Item     = RpcReq              ;
	static constexpr uint64_t Magic = DaemonCache::Magic ;                                  // any random improbable value!=0 used as a sanity check when client connect to server
	// cxtors & casts
	using AutoServer<CacheServer>::AutoServer ;
	// injection
	bool/*done*/ process_item( Fd fd , RpcReq const& crr ) {
		Trace trace("process_item",fd,crr) ;
		switch (crr.proc) { //!                                      key            done
			case Proc::None     :                                            return true  ;
			case Proc::Download : OMsgBuf( download(crr) ).send( fd , {} ) ; return false ; // from lmake_server
			case Proc::Upload   : OMsgBuf( upload  (crr) ).send( fd , {} ) ; return true  ; // from job_exec
			case Proc::Commit   :          commit  (crr)                   ; return false ; // from lmake_server
			case Proc::Dismiss  :          dismiss (crr)                   ; return false ; // .
		DF}
	}
} ;

static CacheServer _g_server { ServerMrkr } ;

void config() {
	Trace trace("config") ;
	//
	::string  config_file = ADMIN_DIR_S "config.py" ;
	AcFd      config_fd   { config_file }           ;
	Gil       gil         ;
	for( auto const& [key,val] : ::vmap_ss(*py_run(config_fd.read())) ) {
		try {
			switch (key[0]) {
				case 'f' : if (key=="file_sync") { g_file_sync = mk_enum<FileSync>    (val) ; continue ; } break ;
				case 'i' : if (key=="inf"      ) {                                            continue ; } break ;
				case 'n' : if (key=="nan"      ) {                                            continue ; } break ;
				case 'p' : if (key=="perm"     ) { g_perm_ext  = mk_enum<PermExt >    (val) ; continue ; } break ;
				case 's' : if (key=="size"     ) { g_max_sz    = from_string_with_unit(val) ; continue ; } break ;
			DN}
		} catch (::string const& e) { trace("bad_val",key,val) ; throw cat("wrong value for entry ",key," : ",val) ; }
		trace("bad_cache_key",key) ;
		throw cat("wrong key (",key,") in ",config_file) ;
	}
	throw_unless( g_max_sz , "size must be defined as non-zero" ) ;
	trace("done") ;
}

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
	try                       { config() ;                                                                                        }
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
	//
	bool interrupted = _g_server.event_loop() ;
	//
	trace("done",STR(interrupted),New) ;
	return interrupted ;
}
