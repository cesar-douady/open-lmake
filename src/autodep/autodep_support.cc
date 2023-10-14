// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "hash.hh"
#include "serialize.hh"

#include "autodep_support.hh"
#include "record.hh"

using namespace Hash ;

JobExecRpcReply AutodepSupport::req(JobExecRpcReq const& jerr) {

	// try backdoor
	// worst data dependent reply size is a CRC per file, rest is a (small) constant size overhead
	::string reply               ( sizeof(Crc)*jerr.files.size() + 100 , char(0) ) ;                                       // provide some margin for overhead
	int      rc [[maybe_unused]] = ::readlinkat( Backdoor , OMsgBuf::s_send(jerr).data() , reply.data() , reply.size() ) ; // no rc from backdoor
	//
	size_t reply_sz = MsgBuf::s_sz(reply.data()) ;
	if (reply_sz) {
		SWEAR( reply_sz<=reply.size() , reply_sz , reply.size() ) ;            // check there was no overflow
		return IMsgBuf::s_receive<JobExecRpcReply>(reply.data()) ;
	}

	// backdoor did not work, try direct connection to server
	if (!get_env("LMAKE_AUTODEP_ENV","").empty()) {
		return RecordSock().backdoor(JobExecRpcReq(jerr)) ;
	}

	// nothing worked, try to mimic server as much as possible, but of course no crc is available
	if ( jerr.sync && jerr.proc==JobExecRpcProc::DepInfos ) return { jerr.proc , ::vector<pair<Bool3/*ok*/,Crc>>(jerr.files.size(),{Yes,{}}) } ;
	else                                                    return {                                                                         } ;

}
