// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"
#include "msg.hh"
#include "process.hh"
#include "rpc_client.hh"
#include "thread.hh"
#include "time.hh"
#include "trace.hh"

#include "client.hh"

using namespace Disk ;
using namespace Time ;

ClientSockFd g_server_fd ;

static Bool3 is_reverse_video( Fd in_fd , Fd out_fd ) {
	using Event = Epoll<NewType>::Event ;
	Trace trace("is_reverse_video",in_fd,out_fd) ;
	struct ::stat in_stat  ;
	struct ::stat out_stat ;
	::fstat(in_fd ,&in_stat ) ;
	::fstat(out_fd,&out_stat) ;
	// we need to send commands to out_fd and receive replies from in_fd, verify that they are both tty's and refer to the same one
	if ( !S_ISCHR(in_stat.st_mode) || !S_ISCHR(out_stat.st_mode) ) return Maybe ;               // one is not a tty
	if (          in_stat.st_dev   !=          out_stat.st_dev   ) return Maybe ;               // not the same xxx, not the same tty
	if (          in_stat.st_ino   !=          out_stat.st_ino   ) return Maybe ;               // .
	if (          in_stat.st_rdev  !=          out_stat.st_rdev  ) return Maybe ;               // .
	//
	Bool3          res       = Maybe ;
	struct termios old_attrs ;
	struct termios new_attrs ;
	//
	::tcgetattr( in_fd , &old_attrs ) ;
	//
	new_attrs              = old_attrs       ;
	new_attrs.c_lflag     &= ~ECHO & ~ICANON ;                                                  // no echo (as they would appear on the terminal) & do not wait for \n that will never come
	new_attrs.c_cc[VMIN ]  = 0               ;                                                  // polling mode, blocking and timeout is managed with epoll as timeout here is not always enforced
	new_attrs.c_cc[VTIME]  = 0               ;                                                  // .
	//
	try {
		BlockedSig blocked{{SIGINT}} ;
		::tcsetattr( in_fd , TCSANOW , &new_attrs ) ;
		//
		// prefer to do manual I/O rather than going through getline & co (which should do the job) as all this part is quite tricky
		//
		//                   background      foreground
		::string reqs[2] = { "\x1b]11;?\a" , "\x1b]10;?\a" } ;                                  // sequence to ask for color
		uint32_t lum [2] = { 0             , 0             } ;
		Epoll    epoll   { New }                             ;                                  // timeout set with ::tcsetattr does not always work, so use epoll for that, in case tty does not answer
		epoll.add_read(in_fd) ;
		for( bool fg : {false,true}) {
			::string reply ;
			for( const char c : reqs[fg] )
				if (::write(out_fd,&c,1)!=1) throw "cannot send request"s ;
			trace("sent",STR(fg),mk_printable(reqs[fg])) ;
			for(;;) {
				char            c      = 0/*garbage*/           ;
				::vector<Event> events = epoll.wait(Delay(0.5)) ;                               // normal reaction time is 20-50ms
				SWEAR( events.size()<=1 , events.size() ) ;
				throw_unless( events.size() , "timeout" ) ;                                     // there is a single fd, there may not be more than 1 event
				SWEAR( events[0].fd()==in_fd , events[0].fd() , in_fd ) ;
				if (::read(in_fd,&c,1)!=1) throw "cannot read reply"s ;                         // this is the only possible fd
				if (c=='\a'              ) break                      ;
				reply.push_back(c) ;
			}
			trace("got",STR(fg),mk_printable(reply)) ;
			size_t   pfx_len = reqs[fg].find(';')+1       ;                                     // up to ;, including it
			::string pfx     = reqs[fg].substr(0,pfx_len) ;
			size_t   pos     = reply.find(pfx)            ;                                     // ignore leading char's that may be sent as echo of user input just before executing command
			throw_unless( pos!=Npos                           , "no ; in reply" ) ;             // reply should have same format with ? substituted by actual values
			throw_unless( reply.substr(pos  ,pfx_len)==pfx    , "bad prefix"    ) ;             // .
			throw_unless( reply.substr(pos+pfx_len,4)=="rgb:" , "no rgb:"       ) ;             // then rgb:
			::vector_s t = split(reply.substr(pos+pfx_len+4),'/') ;
			throw_unless( t.size()==3 , "bad format" ) ;
			//
			for( int i : iota(3) ) lum[fg] += from_string<uint32_t>(t[i],true/*empty_ok*/,16) ; // add all 3 components as a rough approximation of the luminance
		}
		res = lum[true/*foreground*/]>lum[false/*foreground*/] ? Yes : No ;
		trace("found",lum[0],lum[1],res) ;
	} catch (::string const& e) { trace("catch",e) ; }
	trace("restore") ;
	::tcsetattr( in_fd , TCSANOW , &old_attrs ) ;
	return res ;
}

static void _out_thread_func(ReqRpcReply const& rrr) {
	switch (rrr.proc) {
		case ReqRpcReplyProc::Stderr : Fd::Stderr.write(rrr.txt) ; break ;
		case ReqRpcReplyProc::Stdout : Fd::Stdout.write(rrr.txt) ; break ;
	DF}                                                                    // NO_COV
}

Rc _out_proc( ::vector_s* /*out*/ files , ReqProc proc , bool read_only , bool refresh , ReqSyntax const& syntax , ReqCmdLine const& cmd_line , OutProcCb const& cb ) {
	Trace trace("_out_proc") ;
	//
	if (  cmd_line.flags[ReqFlag::Job] && cmd_line.args.size()!=1       ) syntax.usage("can process several files, but a single job"        ) ;
	if ( !cmd_line.flags[ReqFlag::Job] && cmd_line.flags[ReqFlag::Rule] ) syntax.usage("can only force a rule to identify a job, not a file") ;
	//
	bool       sync           = cmd_line.flags[ReqFlag::Sync] ;
	::vector_s cmd_line_files ;                                 try { cmd_line_files = cmd_line.files() ; } catch (::string const& e) { syntax.usage(e) ; }
	//
	Bool3    rv     = Maybe/*garbage*/ ;
	::string rv_str ;
	if (!rv_str) { rv_str = cmd_line.flag_args[+ReqFlag::Video] ; trace("cmd_line",rv_str) ; }
	if (!rv_str) { rv_str = get_env("LMAKE_VIDEO")              ; trace("env"     ,rv_str) ; }
	switch (rv_str[0]) {
		case 'n' : case 'N' : rv = No                                     ; break ;
		case 'r' : case 'R' : rv = Yes                                    ; break ;
		case 'f' : case 'F' : rv = Maybe                                  ; break ;                                                                // force no color
		default  :            rv = is_reverse_video(Fd::Stdin,Fd::Stdout) ;
	}
	trace("reverse_video",rv) ;
	//
	ReqRpcReq rrr        { proc , cmd_line_files , { rv , cmd_line } } ;
	Rc        rc         = Rc::ServerCrash                             ;
	pid_t     server_pid = 0                                           ;
	//
	::vector_s server_cmd_line = { *g_lmake_root_s+"_bin/lmakeserver" , "-d"/*no_daemon*/ , "-c"+*g_startup_dir_s } ;
	if (!refresh ) server_cmd_line.emplace_back("-r") ;                                                                                            // -r means no refresh
	if (read_only) server_cmd_line.emplace_back("-R") ;                                                                                            // -R means read-only
	//
	try                           { tie(g_server_fd,server_pid) = connect_to_server( !read_only , LmakeServerMagic , ::move(server_cmd_line) ) ; } // if read-only and we connect to an old server, ...
	catch (::pair_s<Rc> const& e) { exit( e.second   , e.first ) ;                                                                               } // ... it could write for us but should not
	catch (::string     const& e) { exit( Rc::System , e       ) ;                                                                               }
	trace("starting",g_server_fd,rrr) ;
	cb(true/*start*/) ;                                                            // block INT once server is initialized so as to be interruptible at all time
	QueueThread<ReqRpcReply,true/*Flush*/> out_thread { 'O' , _out_thread_func } ; // /!\ must be after call to cb so INT can be blocked before creating threads
	OMsgBuf(rrr).send(g_server_fd) ;
	trace("started") ;
	bool    received = false/*garbage*/ ;                                          // for trace only
	IMsgBuf buf      ;
	try {
		for(;;) {
			received = false ;
			ReqRpcReply rrr = buf.receive<ReqRpcReply>( g_server_fd , No/*once*/ ) ;
			received = true ;
			switch (rrr.proc) {
				case ReqRpcReplyProc::None   : trace("done"          ) ;                                            goto Return ;
				case ReqRpcReplyProc::Status : trace("status",rrr.rc ) ; rc = rrr.rc ;                              goto Return ; // XXX! : why is it necessary to goto Return here ? ...
				case ReqRpcReplyProc::File   : trace("file"  ,rrr.txt) ; SWEAR(files) ; files->push_back(rrr.txt) ; break       ; // ... we should receive None when server closes stream
				case ReqRpcReplyProc::Stderr :
				case ReqRpcReplyProc::Stdout : out_thread.push(::move(rrr)) ;                                       break       ; // queue reports to avoid blocking server if stderr/stdout is blocked
			DF}                                                                                                                   // NO_COV
		}
	}
	catch (::string    const& e) { trace("disconnected1",STR(received),e       ) ; }
	catch (::exception const& e) { trace("disconnected2",STR(received),e.what()) ; }
	catch (...                 ) { trace("disconnected3",STR(received)         ) ; }
Return :
	trace("exiting") ;
	cb(false/*start*/) ;
	g_server_fd.close() ;                                                                                                         // ensure server stops living because of us
	if (sync) { SWEAR( server_pid ) ; waitpid( server_pid , nullptr , 0 ) ; }
	trace("done",rc) ;
	return rc ;
}
