// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "hash.hh"
#include "serialize.hh"

#include "support.hh"
#include "record.hh"

using namespace Hash ;

JobExecRpcReply AutodepSupport::req(JobExecRpcReq const& jerr) {
	if (+get_env("LMAKE_AUTODEP_ENV","")   ) return Record().direct(::copy(jerr)) ;
	// not under lmake, try to mimic server as much as possible, but of course no real info available
	if (!jerr.sync                         ) return {                                                                         } ;
	if (jerr.proc==JobExecRpcProc::DepInfos) return { jerr.proc , ::vector<pair<Bool3/*ok*/,Crc>>(jerr.files.size(),{Yes,{}}) } ;
	// XXX : for Encode/Decode, we should interrogate the server or explore association file directly
	else                                     return {                                                                         } ;
}
