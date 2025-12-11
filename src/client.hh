// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <termios.h>

#include "disk.hh"
#include "fd.hh"
#include "rpc_client.hh"
#include "trace.hh"

extern ClientSockFd g_server_fd ;

using OutProcCb = ::function<void(bool start)> ;
inline void _out_proc_nop(bool/*start*/) {}

Rc _out_proc( ::vector_s* /*out*/ files , ReqProc , bool read_only , bool refresh , ReqSyntax const& , ReqCmdLine const& , OutProcCb const& =_out_proc_nop ) ;
//
inline Rc out_proc( ::vector_s& fs , ReqProc p , bool ro , bool r , ReqSyntax const& s , ReqCmdLine const& cl , OutProcCb const& cb=_out_proc_nop ) { return _out_proc(&fs    ,p,ro,r,s,cl,cb) ; }
inline Rc out_proc(                  ReqProc p , bool ro , bool r , ReqSyntax const& s , ReqCmdLine const& cl , OutProcCb const& cb=_out_proc_nop ) { return _out_proc(nullptr,p,ro,r,s,cl,cb) ; }
