// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"
#include "process.hh"
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

static void connect_to_server(bool refresh) {
	Trace trace("connect_to_server",STR(refresh)) ;
	::string server_service ;
	pid_t    server_pid     = 0 ;
	for ( int i=0 ; i<10 ; i++ ) {
		trace("try_old",i) ;
		// try to connect to an existing server
		{	::ifstream server_mrkr_stream { to_string(AdminDir,'/',ServerMrkr) } ;
			::string   pid_str            ;
			if (!server_mrkr_stream                          ) goto LaunchServer ;
			if (!::getline(server_mrkr_stream,server_service)) goto LaunchServer ;
			if (!::getline(server_mrkr_stream,pid_str       )) goto LaunchServer ;
			server_pid = from_chars<pid_t>(pid_str) ;
			ClientSockFd req_fd ;
			try {
				ClientSockFd req_fd{server_service} ;
				if (server_ok(req_fd,"old")) {
					g_server_fds = ::move(req_fd) ;
					return ;
				}
			} catch(::string const&) {}
			//
		}
	LaunchServer :
		trace("try_new",i) ;
		// try to launch a new server
		// server calls ::setpgid(0,0) to create a new group by itself, after initialization, so during init, a ^C will propagate to server
		Child server{ false/*as_group*/ , {*g_lmake_dir+"/_bin/lmakeserver","-d"/*no_daemon*/,refresh?"--"/*nop*/:"-r"/*no_refresh*/} , Child::Pipe , Child::Pipe } ;
		//
		if (server_ok(server.stdout,"new")) {
			g_server_fds = AutoCloseFdPair{ server.stdout , server.stdin } ;
			server.mk_daemon() ;
			return ;
		}
		server.wait() ;                                                        // dont care about return code, we are going to relauch/reconnect anyway
		// retry if not successful, may be a race between several clients trying to connect to/launch servers
	}
	::string kill_server_msg ;
	if (!server_service.empty()) {
		::string server_host = SockFd::s_host(server_service) ;
		if (server_host!=host()) kill_server_msg = to_string("ssh ",SockFd::s_host(server_service),' ') ;
	}
	if (server_pid              ) kill_server_msg += to_string("kill ",server_pid       ) ;
	if (!kill_server_msg.empty()) kill_server_msg  = to_string('\t',kill_server_msg,'\n') ;
	exit(2
	,	"cannot connect to server, consider :\n"
	,	kill_server_msg
	,	"\trm LMAKE/server\n"
	) ;
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
	new_attrs.c_cc[VMIN ]  = 0               ;                                 // polling mode, blocking and timeout is managed with epoll as timeout here is notalways enforced
	new_attrs.c_cc[VTIME]  = 0               ;                                 // .
	//
	::tcsetattr( in_fd , TCSANOW , &new_attrs ) ;
	//
	// prefer to do manual I/O rather than going through getline & co (which should do the job) as all this part is quite tricky
	//
	//                   background      foreground
	::string reqs[2] = { "\x1b]11;?\a" , "\x1b]10;?\a" } ;                     // sequence to ask for color
	uint32_t lum [2] = { 0             , 0             } ;
	Epoll    epoll   { New }                             ;                     // timeout set with ::tcsetattr does not always work, so use epoll for that, in case tty does not answer
	epoll.add_read( in_fd , 0/*unused*/ ) ;
	for( bool fg : {false,true}) {
		::string reply ;
		for( const char c : reqs[fg] )
			if (::write(out_fd,&c,1)!=1) goto Restore ;
		for(;;) {
			char c ;
			::vector<Epoll::Event> events = epoll.wait(100'000'000) ;          // 100ms
			SWEAR( events.size()<=1 , events.size() ) ;                        // there is a single fd, there may not be more than 1 event
			if (!events.size()) goto Restore ;                                 // timeout
			SWEAR( events[0].fd()==in_fd , events[0].fd() , in_fd ) ;          // this is the only possible fd
			if (::read(in_fd,&c,1)!=1) goto Restore ;                          // eof or err ? awkward, but give up in that case
			if (c=='\a') break ;
			reply.push_back(c) ;
			if (reply.size()>40) goto Restore ;                                // this is not an ansi terminal, whose answer is around 22 bytes
		}
		size_t pos = reply.find(';') ;
		if (reply.substr(0    ,pos+1)!=reqs[fg].substr(0,pos+1)) goto Restore ; // reply has same format with ? substituted by actual values
		if (reply.substr(pos+1,4)!="rgb:"                      ) goto Restore ; // then rgb:
		//
		::vector_s t = split(reply.substr(pos+5),'/') ;
		if (t.size()!=3) goto Restore ;
		for( size_t i=0 ; i<3 ; i++ ) lum[fg] += from_chars<uint32_t>(t[i],false/*empty_ok*/,16) ; // add all 3 components as a rough approximation of the luminance
	}
	::tcsetattr( in_fd , TCSANOW , &old_attrs ) ;
	return lum[true/*foreground*/]>lum[false/*foreground*/] ? Yes : No ;
Restore :
	::tcsetattr( in_fd , TCSANOW , &old_attrs ) ;
	return Maybe ;
}

Bool3/*ok*/ out_proc( ::ostream& os , ReqProc proc , bool refresh , ReqCmdLine const& cmd_line , ::function<void()> const& started_cb ) {
	Trace trace("out_proc") ;
	ReqRpcReq rrr{ proc , cmd_line.files() , { is_reverse_video(Fd::Stdin,Fd::Stdout) , cmd_line } } ;
	connect_to_server(refresh) ;
	started_cb() ;
	OMsgBuf().send(g_server_fds.out,rrr) ;
	try {
		for(;;) {
			ReqRpcReply report = IMsgBuf().receive<ReqRpcReply>(g_server_fds.in) ;
			switch (report.kind) {
				case ReqKind::None   : trace("none"               ) ;  return Maybe            ;
				case ReqKind::Status : trace("done",STR(report.ok)) ;  return report.ok?Yes:No ;
				case ReqKind::Txt    : os << report.txt << flush ; break                       ;
				default : FAIL(report.kind) ;
			}
		}
	} catch(...) {
		trace("disconnected") ;
		return Maybe ;                                                         // input has been closed by peer before reporting a status
	}
}
