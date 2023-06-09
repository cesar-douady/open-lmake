// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"

#include "autodep_support.hh"

int main( int argc , char** /*argv*/ ) {
	if (argc>1) exit(2,"lcritical_barrier takes no arguments") ;
	//
	JobExecRpcReply reply = AutodepSupport(New).req(JobExecRpcReq(JobExecRpcProc::CriticalBarrier,true/*sync*/)) ;
	return 0 ;
}
