// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "codec_format.hh"
#include "disk.hh"
#include "hash.hh"

#include "rpc_job_exec.hh"

using namespace Disk ;
using namespace Hash ;

//
// AccessDigest
//

::string& operator+=( ::string& os , AccessDigest const& ad ) {                                      // START_OF_NO_COV
	First first ;
	/**/                                  os << "AccessDigest("                                    ;
	if (+ad.accesses                    ) os <<first("",",")<< ad.accesses                         ;
	if ( ad.read_dir                    ) os <<first("",",")<< "read_dir"                          ;
	if ( ad.flags!=AccessDigest().flags ) os <<first("",",")<< ad.flags                            ;
	if ( ad.write!=No                   ) os <<first("",",")<< "written"<<(ad.write==Maybe?"?":"") ;
	return                                os <<')'                                                 ;
}                                                                                                    // END_OF_NO_COV

AccessDigest& AccessDigest::operator|=(AccessDigest const& ad) {
	if (write!=Yes) accesses |= ad.accesses ;
	/**/            write    |= ad.write    ;
	/**/            flags    |= ad.flags    ;
	return self ;
}

//
// JobExecRpcReq
//

::string& operator+=( ::string& os , JobExecRpcReq const& jerr ) {                  // START_OF_NO_COV
	/**/                                      os << "JobExecRpcReq(" << jerr.proc ;
	if      (+jerr.date                     ) os <<','  << jerr.date              ;
	if      ( jerr.sync!=No                 ) os <<",S:"<< jerr.sync              ;
	if      (+jerr.digest                   ) os <<','  << jerr.digest            ;
	if      (+jerr.file                     ) os <<','  << jerr.file              ;
	if      ( jerr.proc==JobExecProc::Encode) os <<','  << jerr.min_len()         ; // Encode uses file_info to transport min_len
	else if (+jerr.file_info                ) os <<':'  << jerr.file_info         ;
	if      (+jerr.comment                  ) os <<','  << jerr.comment           ;
	if      (+jerr.comment_exts             ) os <<','  << jerr.comment_exts      ;
	return                                    os <<')'                            ;
}                                                                                   // END_OF_NO_COV

JobExecRpcReply JobExecRpcReq::mimic_server(MimicCtx&/*inout*/ mimic_ctx) && {
	switch (proc) {
		case Proc::CodecFile : mimic_ctx.codec_file =          ::move(file)  ; break ;
		case Proc::CodecCtx  : mimic_ctx.codec_ctx  =          ::move(file)  ; break ;
		case Proc::DepPush   : mimic_ctx.pushed_deps.push_back(::move(file)) ; break ;
		case Proc::DepVerbose : {
			::vector<VerboseInfo> verbose_infos ; for( ::string& f : mimic_ctx.pushed_deps ) verbose_infos.push_back({ .ok=Yes , .crc=Crc(f) }) ;
			mimic_ctx.pushed_deps.clear() ;
			return { .proc=proc , .verbose_infos=::move(verbose_infos) } ;
		} break ;
		case Proc::Encode : {
			/**/                      return { .proc=proc , .ok=Yes , .txt=Codec::encode(mimic_ctx.codec_file,mimic_ctx.codec_ctx,file/*val */) } ;
		}
		case Proc::Decode :
			try                     { return { .proc=proc , .ok=Yes , .txt=Codec::decode(mimic_ctx.codec_file,mimic_ctx.codec_ctx,file/*code*/) } ; }
			catch (::string const&) { return { .proc=proc , .ok=No                                                                              } ; }
	DN}
	return { .proc=proc , .ok=Yes } ;
}

//
// JobExecRpcReply
//

::string& operator+=( ::string& os , JobExecRpcReply const& jerr ) {                 // START_OF_NO_COV
	os << "JobExecRpcReply(" << jerr.proc ;
	switch (jerr.proc) {
		case JobExecProc::None       :                                     ; break ;
		case JobExecProc::ChkDeps    :
		case JobExecProc::DepDirect  : os <<','<< jerr.ok                  ; break ;
		case JobExecProc::DepVerbose : os <<','<< jerr.verbose_infos       ; break ;
		case JobExecProc::List       : os <<','<< jerr.files               ; break ;
		case JobExecProc::Decode     :
		case JobExecProc::Encode     : os <<','<< jerr.txt <<','<< jerr.ok ; break ;
	DF}                                                                              // NO_COV
	return os << ')' ;
}                                                                                    // END_OF_NO_COV
