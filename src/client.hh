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

static inline int mk_rc(Bool3 ok) {
	switch (ok) {
		case Yes : return 0 ;
		case No  : return 1 ;
		default  : return 2 ;
	}
}

Bool3/*ok*/ out_proc( ::ostream& , ReqProc , bool refresh , ReqSyntax const& , ReqCmdLine const& , ::function<void()> const& started_cb = []()->void{} ) ;
