// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <signal.h>

#include <thread>

#include "app.hh"
#include "client.hh"
#include "disk.hh"
#include "rpc_client.hh"
#include "trace.hh"

using namespace Disk ;
using namespace Time ;

::atomic<bool> g_seen_int = false ;

static void _int_thread_func( ::stop_token stop , Fd int_fd ) {
	Trace::t_key = 'I' ;
	Trace trace ;
	::stop_callback stop_cb { stop , [&](){ kill_self(SIGINT) ; } } ;          // transform request_stop into an event we wait for
	trace("start") ;
	for(;;) {
		struct signalfd_siginfo _ ;
		ssize_t cnt = ::read(int_fd,&_,sizeof(_)) ;
		SWEAR( cnt==sizeof(_) , cnt ) ;
		if (stop.stop_requested()) {
			trace("done") ;
			return ;                                                           // not an interrupt, just normal exit
		}
		trace("send_int") ;
		OMsgBuf().send(g_server_fds.out,ReqRpcReq(ReqProc::Kill)) ;
		::cout << ::endl ;                                                     // output is nicer if ^C is on its own line
		g_seen_int = true ;
	}
}


int main( int argc , char* argv[] ) {
	struct Exit {
		~Exit() {                                                              // this must be executed after _int_thread_func has completed
			if (!g_seen_int) return ;
			unblock_sig(SIGINT) ;
			kill_self  (SIGINT) ;                                              // appear as being interrupted : important for shell scripts to actually stop
			kill_self  (SIGHUP) ;                                              // for some reason, the above kill_self does not work in some situations (e.g. if you type bash -c 'lmake&')
			fail_prod("lmake does not want to die") ;
		}
	} ;
	static Exit        exit   ;
	static AutoCloseFd int_fd = open_sig_fd(SIGINT,true/*block*/) ;            // must be closed after _int_thread_func has completed and before ~Exit code
	Trace::s_backup_trace = true ;
	app_init(true/*search_root*/,true/*cd_root*/) ;
	//
	ReqSyntax syntax{{
		{ ReqFlag::Archive         , { .short_name='a' , .has_arg=false , .doc="ensure all intermediate files are generated" } }
	,	{ ReqFlag::ForgetOldErrors , { .short_name='e' , .has_arg=false , .doc="assume old errors are transcient"            } }
	,	{ ReqFlag::Jobs            , { .short_name='j' , .has_arg=true  , .doc="max number of jobs"                          } }
	,	{ ReqFlag::Local           , { .short_name='l' , .has_arg=false , .doc="launch all jobs locally"                     } }
	,	{ ReqFlag::ManualOk        , { .short_name='m' , .has_arg=false , .doc="allow overwrite of manually modified files"  } }
	,	{ ReqFlag::LiveOut         , { .short_name='o' , .has_arg=false , .doc="generate live output for last job"           } }
	,	{ ReqFlag::SourceOk        , { .short_name='s' , .has_arg=false , .doc="allow overwrite of source files"             } }
	,	{ ReqFlag::KeepTmp         , { .short_name='t' , .has_arg=false , .doc="keep tmp dir after job execution"            } }
	,	{ ReqFlag::Verbose         , { .short_name='v' , .has_arg=false , .doc="generate backend execution info"             } }
	,	{ ReqFlag::Backend         , { .short_name='b' , .has_arg=true  , .doc="send arguments to backend"                   } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	long n_jobs = atol(cmd_line.flag_args[+ReqFlag::Jobs].c_str() ) ;
	if ( cmd_line.flags[ReqFlag::Jobs] && ( n_jobs<=0 || n_jobs>=::numeric_limits<JobIdx>::max() ) ) syntax.usage("cannot understand max number of jobs") ;
	// start interrupt handling thread once server is started
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( ::cout , ReqProc::Make , true/*refresh_makefiles*/ , cmd_line , [&]()->void { static ::jthread int_jt { _int_thread_func , Fd(int_fd) } ; } ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	return mk_rc(ok) ;
}
