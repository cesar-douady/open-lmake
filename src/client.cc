// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"
#include "msg.hh"
#include "process.hh"
#include "rpc_client.hh"
#include "time.hh"
#include "trace.hh"

#include "client.hh"

using namespace Disk ;
using namespace Time ;

ClientFdPair g_server_fds ;

static bool _server_ok( Fd fd , ::string const& tag ) {
	Trace trace("_server_ok",tag,fd) ;
	bool ok  = false                             ;
	int  cnt = ::read( fd , &ok , sizeof(bool) ) ;
	if (cnt!=sizeof(bool)) { trace("bad_answer",cnt) ; return false ; }
	trace("answer",STR(ok)) ;
	return ok ;
}

static pid_t _connect_to_server( bool read_only , bool refresh , bool sync ) { // if sync, ensure we launch our own server
	Trace trace("_connect_to_server",STR(refresh)) ;
	::string server_service  ;
	bool     server_is_local = false ;
	pid_t    server_pid      = 0     ;
	Pdate    now             = New   ;
	for ( int i : iota(10) ) {
		trace("try_old",i) ;
		if (!read_only) {                                                      // if we are read-only and we connect to an existing server, then it could write for us while we should not
			// try to connect to an existing server
			AcFd            server_mrkr_fd     { ServerMrkr }                ; if (!server_mrkr_fd) { trace("no_marker"  ) ; goto LaunchServer ; }
			::vector_s      lines              = server_mrkr_fd.read_lines() ; if (lines.size()!=2) { trace("bad_markers") ; goto LaunchServer ; }
			::string const& server_service_str = lines[0]                    ;
			::string const& pid_str            = lines[1]                    ;
			try {
				if (host()==SockFd::s_host(server_service_str)) {
					server_service  = SockFd::s_service( SockFd::s_addr_str(SockFd::LoopBackAddr) , SockFd::s_port(server_service_str) ) ; // dont use network if not necessary
					server_is_local = true                                                                                               ;
				}
				ClientSockFd req_fd { server_service , Delay(3)/*timeout*/ } ;
				if (_server_ok(req_fd,"old")) {
					g_server_fds = ::move(req_fd) ;
					if (sync) exit(Rc::Format,"server already exists") ;
					return 0 ;
				}
			} catch(::string const&) { trace("cannot_connect",server_service_str,server_service) ; }
			server_pid = from_string<pid_t>(pid_str) ;
			trace("server",server_pid) ;
			//
		}
	LaunchServer :
		// try to launch a new server
		// server calls ::setpgid(0,0) to create a new group by itself, after initialization, so during init, a ^C will propagate to server
		::vector_s cmd_line = {
			*g_lmake_root_s+"_bin/lmakeserver"
		,	"-d"/*no_daemon*/
		,	"-c"+*g_startup_dir_s
		} ;
		Pipe client_to_server{New,0/*flags*/,true/*no_std*/} ; client_to_server.read .cloexec(false) ; client_to_server.write.cloexec(true) ;
		Pipe server_to_client{New,0/*flags*/,true/*no_std*/} ; server_to_client.write.cloexec(false) ; server_to_client.read .cloexec(true) ;
		/**/           cmd_line.push_back("-i"s+client_to_server.read .fd) ;
		/**/           cmd_line.push_back("-o"s+server_to_client.write.fd) ;
		if (!refresh ) cmd_line.push_back("-r"                           ) ; // -r means no refresh
		if (read_only) cmd_line.push_back("-R"                           ) ; // -R means read-only
		/**/           cmd_line.push_back("--"                           ) ; // ensure no further option processing in case a file starts with a -
		trace("try_new",i,cmd_line) ;
		try {
			Child server { .as_session=true , .cmd_line=cmd_line } ;
			server.spawn() ;
			client_to_server.read .close() ;
			server_to_client.write.close() ;
			//
			if (_server_ok(server_to_client.read,"new")) {
				g_server_fds = ClientFdPair{ server_to_client.read , client_to_server.write } ;
				pid_t pid = server.pid ;
				server.mk_daemon() ;                                         // let process survive to server dxtor
				return pid ;
			}
			client_to_server.write.close() ;
			server_to_client.read .close() ;
			server.wait() ;                                                  // dont care about return code, we are going to relauch/reconnect anyway
		} catch (::string const& e) {
			exit(Rc::System,e) ;
		}
		// retry if not successful, may be a race between several clients trying to connect to/launch servers
		now += Delay(0.1) ;
		now.sleep_until() ;
	}
	::string kill_server_msg ;
	if ( +server_service && !server_is_local ) kill_server_msg << "ssh "<<SockFd::s_host(server_service)+' ' ;
	if ( server_pid                          ) kill_server_msg << "kill "<<server_pid                        ;
	if ( +kill_server_msg                    ) kill_server_msg =  '\t'+kill_server_msg+'\n'                  ;
	trace("cannot_connect",server_service,kill_server_msg) ;
	exit(Rc::Format
	,	"cannot connect to server, consider :\n"
	,	kill_server_msg
	,	"\trm "s+AdminDirS+"server\n"
	) ;
}

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
	Bool3          res       = Maybe                         ;
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

Bool3/*ok*/ _out_proc( ::vector_s* files , ReqProc proc , bool read_only , bool refresh , ReqSyntax const& syntax , ReqCmdLine const& cmd_line , OutProcCb const& cb=[](bool/*start*/)->void{} ) {
	Trace trace("out_proc") ;
	//
	if (  cmd_line.flags[ReqFlag::Job] && cmd_line.args.size()!=1       ) syntax.usage("can process several files, but a single job"        ) ;
	if ( !cmd_line.flags[ReqFlag::Job] && cmd_line.flags[ReqFlag::Rule] ) syntax.usage("can only force a rule to identify a job, not a file") ;
	//
	bool sync = cmd_line.flags[ReqFlag::Sync] ;
	//
	Bool3    rv     = Maybe/*garbage*/ ;
	::string rv_str ;
	if (!rv_str) { rv_str = cmd_line.flag_args[+ReqFlag::Video] ; trace("cmd_line",rv_str) ; }
	if (!rv_str) { rv_str = get_env("LMAKE_VIDEO")              ; trace("env"     ,rv_str) ; }
	switch (rv_str[0]) {
		case 'n' :
		case 'N' : rv = No                                     ; break ;
		case 'r' :
		case 'R' : rv = Yes                                    ; break ;
		case 'f' :
		case 'F' : rv = Maybe                                  ; break ;                                                         // force no color
		default  : rv = is_reverse_video(Fd::Stdin,Fd::Stdout) ;
	}
	trace("reverse_video",rv) ;
	//
	ReqRpcReq rrr        { proc , cmd_line.files() , { rv , cmd_line } } ;
	Bool3     rc         = Maybe                                         ;
	pid_t     server_pid = _connect_to_server(read_only,refresh,sync)    ;
	cb(true/*start*/) ;
	OMsgBuf().send(g_server_fds.out,rrr) ;
	try {
		for(;;) {
			using Proc = ReqRpcReplyProc ;
			ReqRpcReply report = IMsgBuf().receive<ReqRpcReply>(g_server_fds.in) ;
			switch (report.proc) {
				case Proc::None   : trace("done"                 ) ;                                               goto Return ;
				case Proc::Status : trace("status",STR(report.ok)) ; rc = No|report.ok ;                           goto Return ; // XXX! : why is it necessary to goto Return here ? ...
				case Proc::File   : trace("file"  ,report.txt    ) ; SWEAR(files) ; files->push_back(report.txt) ; break       ; // ... we should receive None when server closes stream
				case Proc::Stderr :                                  Fd::Stderr.write(report.txt) ;                break       ;
				case Proc::Stdout :                                  Fd::Stdout.write(report.txt) ;                break       ;
			DF}                                                                                                                  // NO_COV
		}
	} catch(...) {
		trace("disconnected") ;
	}
Return :
	cb(false/*start*/) ;
	g_server_fds.out.close() ;                                                                                                   // ensure server stops living because of us
	if (sync) waitpid( server_pid , nullptr , 0 ) ;
	return rc ;
}
