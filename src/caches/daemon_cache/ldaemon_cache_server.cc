// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "app.hh"
#include "process.hh"

#include "caches/daemon_cache.hh"

using namespace Caches ;

struct CacheServer : AutoServer<CacheServer> {
	using Proc     = DaemonCacheRpcProc  ;
	using RpcReq   = DaemonCacheRpcReq   ;
	using RpcReply = DaemonCacheRpcReply ;
	using Item     = RpcReq              ;
	static constexpr uint64_t Magic = DaemonCache::Magic ; // any random improbable value!=0 used as a sanity check when client connect to server
	// cxtors & casts
	using AutoServer<CacheServer>::AutoServer ;
	// injection
	bool/*done*/ process_item( Fd fd , RpcReq const& crr ) {
		Trace trace("process_item",fd,crr) ;
		switch (crr.proc) { //!                                                                                                                          done
			case Proc::None     :                                                                                                                 return true  ;
			case Proc::Download : OMsgBuf( RpcReply{ .proc=Proc::Download , .digest={.hit_info=CacheHitInfo::NoRule} } ).send( fd , {}/*key*/ ) ; return false ; // from lmake_server
			case Proc::Upload   : OMsgBuf( RpcReply{ .proc=Proc::Upload   , .file="/dev/null" , .upload_key=1        } ).send( fd , {}/*key*/ ) ; return true  ; // from job_exec
			case Proc::Commit   :                                                                                                                 return false ; // from lmake_server
			case Proc::Dismiss  :                                                                                                                 return false ; // .
		DF}
	}
} ;

static CacheServer _g_server { ServerMrkr } ;

int main( int argc , char** argv ) {
	//
	app_init({
		.chk_version  = Maybe
	,	.cd_root      = false                                            // daemon is always launched at root
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
	//
	block_sigs({SIGPIPE}) ;                                       // SIGPIPE : to generate error on write rather than a signal when reading end is dead ...
	Trace trace("main",getpid(),*g_lmake_root_s,*g_repo_root_s) ; // ... must be done before any thread is launched so that all threads block the signal
	for( int i : iota(argc) ) trace("arg",i,argv[i]) ;
	try {
		_g_server.is_daemon = is_daemon ;
		_g_server.writable  = true      ;
		_g_server.start() ;
	} catch (::pair_s<Rc> const& e) {
		if (+e.first) exit( e.second , "cannot start ",*g_exe_name," : ",e.first ) ;
		else          exit( Rc::Ok                                               ) ;
	}
	bool interrupted = _g_server.event_loop() ;
	trace("done",STR(interrupted),New) ;
	return interrupted ;
}
