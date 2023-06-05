// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "gather_deps.hh"
#include "record.hh"

#if HAS_PTRACE

	struct AutodepPtrace {
		// statics
		static void s_prepare_child() ;                                        // must be called from child
		// cxtors & casts
		AutodepPtrace() = default ;
		AutodepPtrace( int cp , AutodepEnv const& ade , Record::ReportCb rcb , Record::GetReplyCb grcb ) { _init(cp,ade,rcb,grcb) ; }
	private :
		void _init( int child_pid , AutodepEnv const& , Record::ReportCb , Record::GetReplyCb ) ;
		// services
		::pair<bool/*done*/,int/*wstatus*/> _changed( int pid , int wstatus ) ;
	public :
		int/*wstatus*/ process() {
			int  wstatus ;
			int  pid     ;
			bool done    ;
			while( (pid=wait(&wstatus))>=0 ) {
				::tie(done,wstatus) = _changed(pid,wstatus) ;
				if (done) return wstatus ;
			}
			fail("process ",child_pid," did not exit nor was signaled") ;
		}
		// data
		int                child_pid    ;
		Record::ReportCb   report_cb    ;
		Record::GetReplyCb get_reply_cb ;
	} ;

#else

	struct AutodepPtrace {
		// statics
		[[noreturn]] static void bad() { fail_prod("autodep method ptrace not supported") ; }
		static void s_prepare_child() { bad() ; }
		// cxtors & casts
		AutodepPtrace() = default ;
		AutodepPtrace( int /*child_pid*/ , AutodepEnv const& , Record::ReportCb , Record::GetReplyCb ) { bad() ; }
		int/*wstatus*/ process() { bad() ; }
	} ;

#endif
