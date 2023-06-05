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

static void _int_thread_func( ::stop_token stop , Fd int_fd ) {
	Trace::t_key = 'I' ;
	Trace trace ;
	::stop_callback stop_cb { stop , [&](){ kill_self(SIGINT) ; } } ;          // transform request_stop into an event we wait for
	for(;;) {
		struct signalfd_siginfo _ ;
		SWEAR( ::read(int_fd,&_,sizeof(_)) == sizeof(_) ) ;
		if (stop.stop_requested()) {
			trace("done") ;
			return ;                                                           // not an interrupt, just normal exit
		}
		trace("send_int") ;
		OMsgBuf().send(g_server_fds.out,ReqRpcReq(ReqProc::Kill)) ;
		::cout << ::endl ;                                                     // output is nicer if ^C is on its own line
	}
}

int main( int argc , char* argv[] ) {
	Fd int_fd = open_sig_fd(SIGINT,true/*block*/) ;                            // must be done before app_init so that all threads block the signal
	Trace::s_backup_trace = true ;
	app_init(true/*search_root*/,true/*cd_root*/) ;
	//
	ReqSyntax syntax{{
		{ ReqFlag::Archive         , { .short_name='a' , .has_arg=false , .doc="ensure all intermediate files are generated" } }
	,	{ ReqFlag::ForgetOldErrors , { .short_name='e' , .has_arg=false , .doc="assume old errors are transcient"            } }
	,	{ ReqFlag::ManualOk        , { .short_name='m' , .has_arg=false , .doc="allow overwrite of manually modified files"  } }
	,	{ ReqFlag::LiveOut         , { .short_name='o' , .has_arg=false , .doc="generate live output for last job"           } }
	,	{ ReqFlag::SourceOk        , { .short_name='s' , .has_arg=false , .doc="allow overwrite of source files"             } }
	,	{ ReqFlag::KeepTmp         , { .short_name='t' , .has_arg=false , .doc="keep tmp dir after job execution"            } }
	}} ;
	ReqCmdLine cmd_line{syntax,argc,argv} ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Bool3 ok = out_proc( ReqProc::Make , cmd_line , [&]()->void { static ::jthread int_jt { _int_thread_func , int_fd } ; } ) ; // start interrupt handling thread once server is started
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	return mk_rc(ok) ;
}
