// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "hash.hh"
#include "serialize.hh"

#include "autodep_support.hh"

using namespace Hash ;

const umap<JobExecRpcProc,Cmd> g_proc_tab = {
//	  proc                                sync   has_args   has_ok   has_crcs
	{ JobExecRpcProc::Deps            , { true , true     , false  , false    } }
,	{ JobExecRpcProc::Unlinks         , { true , true     , false  , false    } }
,	{ JobExecRpcProc::Targets         , { true , true     , false  , false    } }
,	{ JobExecRpcProc::CriticalBarrier , { true , false    , false  , false    } }
,	{ JobExecRpcProc::ChkDeps         , { true , false    , true   , false    } }
,	{ JobExecRpcProc::DepCrcs         , { true , true     , false  , true     } }
} ;

JobExecRpcReply AutodepSupport::req(JobExecRpcReq const& jerr) {

	// try backdoor
	// worst data dependent reply size is a CRC per file, rest is a (small) constant size overhead
	::string reply               ( sizeof(Crc)*jerr.files.size() + 100 , char(0) ) ;                                          // provide some margin for overhead
	int      rc [[maybe_unused]] = ::readlinkat( AT_BACKDOOR , OMsgBuf::s_send(jerr).data() , reply.data() , reply.size() ) ; // no rc from backdoor
	//
	size_t reply_sz = MsgBuf::s_sz(reply.data()) ;
	if (reply_sz) {
		if (jerr.sync) SWEAR(reply_sz<=reply.size()) ;                         // check there was no overflow
		return IMsgBuf::s_receive<JobExecRpcReply>(reply.data()) ;
	}

	// backdoor did not work, try direct connection to server
	if (has_env("LMAKE_AUTODEP_ENV")) {
		static bool s_inited [[maybe_unused]] = (RecordSock::s_init(),true) ;
		return RecordSock().backdoor(JobExecRpcReq(jerr)) ;
	}

	// nothing worked, try to mimic server as much as possible, but of course no crc is available
	if (jerr.proc==JobExecRpcProc::DepCrcs) return { jerr.proc , ::vector<Crc>(jerr.files.size()) } ;
	else                                    return {                                              } ;

}
