// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"
#include "rpc_client.hh"
#include "trace.hh"

#include "client.hh"

using namespace Disk ;

AutoCloseFdPair g_server_fds ;

static bool server_ok( Fd fd , ::string const& tag ) {
	bool ok  = false                                 ;
	int  cnt = ::read( fd , &ok , sizeof(bool) ) ;
	if (cnt!=sizeof(bool)) return false ;
	Trace trace(tag,STR(ok),fd) ;
	return ok ;
}

static void connect_to_server() {
	Trace trace("connect_to_server") ;
	for ( int i=0 ; i<10 ; i++ ) {
		trace("try_old",i) ;
		// try to connect to an existing server
		{	::ifstream   server_mrkr_stream{to_string(AdminDir,'/',ServerMrkr)} ; if (!server_mrkr_stream                        ) goto LaunchServer ;
			::string     server_service                                         ; if (!getline(server_mrkr_stream,server_service)) goto LaunchServer ;
			ClientSockFd req_fd            {server_service                    } ; if (!req_fd                                    ) goto LaunchServer ;
			//
			if (server_ok(req_fd,"old")) {
				g_server_fds = ::move(req_fd) ;
				return ;
			}
		}
	LaunchServer :
		trace("try_new",i) ;
		// try to launch a new server
		// server calls ::setpgid(0,0) to create a new group by itself, after initialization, so during init, a ^C will propagate to server
		Child server{ false/*as_group*/ , {*g_lmake_dir+"/_bin/lmakeserver",""} , Child::Pipe , Child::Pipe } ;
		//
		if (server_ok(server.stdout,"new")) {
			g_server_fds = AutoCloseFdPair{ server.stdout , server.stdin } ;
			server.mk_daemon() ;
			return ;
		}
		server.wait() ;                                                        // dont care about return code, we are going to relauch/reconnect anyway
		// retry if not successful, may be a race between several clients trying to connect to/launch servers
	}
	exit(2,"cannot connect to server") ;
}

static Bool3 is_reverse_video( Fd in_fd , Fd out_fd ) {
	struct stat in_stat  ;
	struct stat out_stat ;
	::fstat(in_fd ,&in_stat ) ;
	::fstat(out_fd,&out_stat) ;
	// we need to send commands to out_fd and receive replies from in_fd, verify that they are both tty's and refer to the same one
	if ( !S_ISCHR(in_stat.st_mode) || !S_ISCHR(out_stat.st_mode) ) return Maybe ; // one is not a tty
	if (          in_stat.st_dev   !=          out_stat.st_dev   ) return Maybe ; // not the same xxx, not the same tty
	if (          in_stat.st_ino   !=          out_stat.st_ino   ) return Maybe ; // .
	if (          in_stat.st_rdev  !=          out_stat.st_rdev  ) return Maybe ; // .
	//
	struct termios old_attrs ;
	struct termios new_attrs ;
	//
	::tcgetattr( in_fd , &old_attrs ) ;
	//
	new_attrs              = old_attrs       ;
	new_attrs.c_lflag     &= ~ECHO & ~ICANON ;                                 // no echo (as they would appear on the terminal) & do not wait for \n that will never come
	new_attrs.c_cc[VMIN ]  = 1               ;                                 // do not pack chars so they are available when they arrive
	new_attrs.c_cc[VTIME]  = 1               ;                                 // time-out = 1/10s, ensure we do not stay stuck if terminal is not ansi compatible
	//
	::tcsetattr( in_fd , TCSANOW , &new_attrs ) ;
	//
	// prefer to do manual I/O rather than going through getline & co (which should do the job) as all this part is quite tricky
	//
	//                   background      foreground
	::string reqs[2] = { "\x1b]11;?\a" , "\x1b]10;?\a" } ;                     // sequence to ask for color
	uint32_t lum [2] = { 0             , 0             } ;
	for( bool fg : {false,true}) {
		::string reply ;
		for( const char c : reqs[fg] )
			if (::write(out_fd,&c,1)!=1) goto Restore ;
		for(;;) {
			char c ;
			if (::read(in_fd,&c,1)!=1) goto Restore ;
			if (c=='\a') break ;
			reply.push_back(c) ;
		}
		size_t pos = reply.find(';') ;
		if (reply.substr(0    ,pos+1)!=reqs[fg].substr(0,pos+1)) goto Restore ;   // reply has same format with ? substituted by actual values
		if (reply.substr(pos+1,4)!="rgb:"                  ) goto Restore ;   // then rgb:
		//
		::vector_s t = split(reply.substr(pos+5),'/') ;
		if (t.size()!=3) goto Restore ;
		for( size_t i=0 ; i<3 ; i++ ) lum[fg] += strtol(t[i].c_str(),nullptr,16) ;          // add all 3 components as a rough approximation of the luminance
	}
	::tcsetattr( in_fd , TCSANOW , &old_attrs ) ;
	return lum[true/*foreground*/]>lum[false/*foreground*/] ? Yes : No ;
Restore :
	::tcsetattr( in_fd , TCSANOW , &old_attrs ) ;
	return Maybe ;
}

Bool3/*ok*/ out_proc( ReqProc proc , ReqCmdLine const& cmd_line , ::function<void()> const& started_cb ) {
	Trace trace("out_proc") ;
	ReqRpcReq rrr{ proc , cmd_line.files() , { is_reverse_video(Fd::Stdin,Fd::Stdout) , cmd_line } } ;
	connect_to_server() ;
	started_cb() ;
	OMsgBuf().send(g_server_fds.out,rrr) ;
	try {
		for(;;) {
			ReqRpcReply report = IMsgBuf().receive<ReqRpcReply>(g_server_fds.in) ;
			switch (report.kind) {
				case ReqKind::None   : trace("none"               ) ;  return Maybe            ;
				case ReqKind::Status : trace("done",STR(report.ok)) ;  return report.ok?Yes:No ;
				case ReqKind::Txt    : ::cout << report.txt << flush ; break                   ;
				default : FAIL(report.kind) ;
			}
		}
	} catch(...) {
		trace("disconnected") ;
		return Maybe ;                                                         // input has been closed by peer before reporting a status
	}
}
