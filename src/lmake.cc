// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "client.hh"
#include "disk.hh"
#include "process.hh"
#include "rpc_client.hh"
#include "thread.hh"
#include "trace.hh"

using namespace Disk ;
using namespace Time ;

Atomic<bool> g_seen_int = false ;

static void _int_thread_func(::stop_token stop) {
	t_thread_key = 'I' ;
	::stop_callback stop_cb { stop , [&](){ Trace trace("stop") ; kill_self(SIGINT) ; } } ;                         // transform request_stop into an event we wait for
	Epoll           epoll   { New                                                       } ; epoll.add_sig(SIGINT) ;
	Trace trace("_int_thread_func") ;
	for(;;) {
		epoll.wait() ;
		bool stop_requested = stop.stop_requested() ;
		trace("int",STR(stop_requested)) ;
		if (stop_requested) break ;                                                                                 // not an interrupt, just normal exit
		OMsgBuf(ReqRpcReq(ReqProc::Kill)).send(g_server_fd) ;
		Fd::Stdout.write("\n") ;                                                                                    // output is nicer if ^C is on its own line
		g_seen_int = true ;
	}
	trace("done") ;
}

static void _handle_int(bool start) {
	struct Exit {
		Exit() {
			block_sigs({SIGINT}) ;
		}
		~Exit() {                                     // this must be executed after _int_thread_func has completed
			if (!g_seen_int) return ;
			unblock_sigs({SIGINT}) ;
			kill_self(SIGINT) ;                       // appear as being interrupted : important for shell scripts to actually stop
			kill_self(SIGHUP) ;                       // for some reason, the above kill_self does not work in some situations (e.g. if you type bash -c 'lmake&')
			fail_prod("lmake does not want to die") ; // NO_COV
		}
	} ;
	static ::jthread int_jt ;
	Trace trace("_handle_int",STR(start)) ;
	if (start) {
		if (is_blocked_sig(SIGINT)) return ;          // nothing to handle if ^C is blocked
		trace("set_up") ;
		static Exit exit ;
		int_jt = ::jthread(_int_thread_func) ;
	} else {
		if (int_jt.joinable()) {
			int_jt.request_stop() ;
			trace("wait") ;
			int_jt.join() ;
		}
	}
}

int main( int argc , char* argv[] ) {
	Trace::s_backup_trace = true ;
	app_init({ .read_only_ok=false , .chk_version=Maybe }) ;
	//
	ReqSyntax syntax {{
		{ ReqFlag::Archive         , { .short_name='a' , .has_arg=false , .doc="ensure all intermediate files are generated"   } }
	,	{ ReqFlag::CacheMethod     , { .short_name='c' , .has_arg=true  , .doc="cache method (none, download, check or plain)" } }
	,	{ ReqFlag::ForgetOldErrors , { .short_name='e' , .has_arg=false , .doc="assume old errors are transient"               } }
	,	{ ReqFlag::Ete             , { .short_name='E' , .has_arg=true  , .doc="estimated time of execution for scheduling"    } }
	,	{ ReqFlag::NoIncremental   , { .short_name='I' , .has_arg=false , .doc="ignore incremental flag on targets"            } }
	,	{ ReqFlag::Jobs            , { .short_name='j' , .has_arg=true  , .doc="max number of jobs"                            } }
	,	{ ReqFlag::Local           , { .short_name='l' , .has_arg=false , .doc="launch all jobs locally"                       } }
	,	{ ReqFlag::LiveOut         , { .short_name='o' , .has_arg=false , .doc="generate live output for last job"             } }
	,	{ ReqFlag::MaxRuns         , { .short_name='m' , .has_arg=true  , .doc="max runs on top of rule prescription"          } }
	,	{ ReqFlag::MaxSubmits      , { .short_name='M' , .has_arg=true  , .doc="max submits on top of rule prescription"       } }
	,	{ ReqFlag::Nice            , { .short_name='N' , .has_arg=true  , .doc="nice value to apply to jobs"                   } }
	,	{ ReqFlag::RetryOnError    , { .short_name='r' , .has_arg=true  , .doc="retry jobs in error"                           } }
	,	{ ReqFlag::SourceOk        , { .short_name='s' , .has_arg=false , .doc="allow overwrite of source files"               } }
	,	{ ReqFlag::KeepTmp         , { .short_name='t' , .has_arg=false , .doc="keep tmp dir after job execution"              } }
	,	{ ReqFlag::Backend         , { .short_name='b' , .has_arg=true  , .doc="send arguments to backend"                     } }
	}} ;
	// add args passed in environment
	SWEAR(argc>0) ;
	::vector_s            env_args = split(get_env("LMAKE_ARGS"))                   ;
	::vector<const char*> args     = {argv[0]} ; args.reserve(env_args.size()+argc) ;
	for( ::string const& a : env_args     ) args.push_back(a.c_str()) ;
	for( int             i : iota(1,argc) ) args.push_back(argv[i]  ) ;
	Trace trace("main",::span<char*>(argv,argc)) ;
	/**/  trace(       env_args                ) ;
	/**/  trace(       args                    ) ;
	//
	ReqCmdLine cmd_line { syntax , int(args.size()) , args.data() } ;
	//
	try                       { from_string<JobIdx>(cmd_line.flag_args[+ReqFlag::Jobs],true/*empty_ok*/) ;                                                         }
	catch (::string const& e) { syntax.usage("cannot understand max number of jobs ("+e+") : "+cmd_line.flag_args[+ReqFlag::Jobs]) ;                               }
	//
	try                       { from_string<JobIdx>(cmd_line.flag_args[+ReqFlag::RetryOnError],true/*empty_ok*/) ;                                                 }
	catch (::string const& e) { syntax.usage("cannot understand retry count ("+e+") : "+cmd_line.flag_args[+ReqFlag::RetryOnError]) ;                              }
	//
	try                       { uint8_t n = from_string<uint8_t>(cmd_line.flag_args[+ReqFlag::Nice],true/*empty_ok*/) ; throw_unless(n<=20,"must be at most 20") ; }
	catch (::string const& e) { syntax.usage("cannot understand nice value ("+e+") : "+cmd_line.flag_args[+ReqFlag::RetryOnError]) ;                               }
	//
	try                       { if (cmd_line.flags[ReqFlag::CacheMethod]) mk_enum<CacheMethod>(cmd_line.flag_args[+ReqFlag::CacheMethod]) ;                        }
	catch (::string const& e) { syntax.usage("unexpected cache method : "+cmd_line.flag_args[+ReqFlag::CacheMethod]) ;                                             }
	// start interrupt handling thread once server is started
	//      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Rc rc = out_proc( ReqProc::Make , false/*read_only*/ , true/*refresh_makefiles*/ , syntax , cmd_line , _handle_int ) ;
	//      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	trace("done",rc) ;
	exit(rc) ;
}
