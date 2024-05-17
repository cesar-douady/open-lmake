// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <termios.h>

#include "disk.hh"
#include "rpc_client.hh"
#include "trace.hh"

struct AutoCloseFdPair {
	// cxtors & casts
	AutoCloseFdPair(                         ) = default ;
	AutoCloseFdPair( AutoCloseFdPair&& acfdp ) : in{::move(acfdp.in)} , out{::move(acfdp.out)} {}
	AutoCloseFdPair( Fd in_fd , Fd out_fd    ) : in{       in_fd    } , out{       out_fd    } {}
	AutoCloseFdPair( ClientSockFd&& fd       ) : in{       fd.fd    } , out{       fd.fd     } { fd.detach() ; }
	//
	AutoCloseFdPair& operator=(AutoCloseFdPair&&) = default ;
	// data
	Fd          in  ;                                                          // close only once
	AutoCloseFd out ;
} ;

extern AutoCloseFdPair g_server_fds ;

inline Rc mk_rc(Bool3 ok) {
	switch (ok) {
		case Yes   : return Rc::Ok     ;
		case Maybe : return Rc::Format ;
		case No    : return Rc::Fail   ;
	DF}
}

using OutProcCb = ::function<void(bool start)> ;

Bool3/*ok*/ _out_proc( ::vector_s* files , ReqProc , bool refresh , ReqSyntax const& , ReqCmdLine const& , OutProcCb const& ) ;
//
inline Bool3/*ok*/ out_proc( ::vector_s& fs , ReqProc p , bool r , ReqSyntax const& s , ReqCmdLine const& cl , OutProcCb const& cb=[](bool)->void{} ) { return _out_proc(&fs    ,p,r,s,cl,cb) ; }
inline Bool3/*ok*/ out_proc(                  ReqProc p , bool r , ReqSyntax const& s , ReqCmdLine const& cl , OutProcCb const& cb=[](bool)->void{} ) { return _out_proc(nullptr,p,r,s,cl,cb) ; }
