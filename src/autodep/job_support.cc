// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "time.hh"

#include "backdoor.hh"
#include "job_support.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

using Proc = JobExecProc ;

namespace JobSupport {

	static void _chk_files(::vector_s const& files) {
		for( ::string const& f : files ) throw_unless( f.size()<=PATH_MAX , "file name too long (",f.size()," characters)" ) ;
	}

	::vector<pair<Bool3/*ok*/,Crc>> depend( Record const& r , ::vector_s&& files , AccessDigest ad , bool no_follow , bool verbose ) {
		::vmap_s<FileInfo> deps ;
		_chk_files(files) ;
		Pdate pd { New } ;                                                // all dates must be identical to be recognized as parallel deps
		for( ::string const& f : files ) {
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({
				.file      = ::copy(f)                                    // keep a copy in case there is no server and we must invent a reply
			,	.no_follow = no_follow
			,	.read      = true
			,	.write     = false
			,	.comment  = "depend"
			}) ;
			if (!verbose) {
				r.report_access( sr.file_loc , { .digest=ad|sr.accesses , .date=pd , .file=::move(sr.real) , .txt="depend" } , true/*force*/ ) ;
			} else if (sr.file_loc<=FileLoc::Dep) {
				FileInfo fi { sr.real } ;
				ad |= sr.accesses ;                                       // seems pessimistic but sr.accesses does not actually depend on file, only on no_follow, read and write
				// not actually sync, but transport as sync to use the same fd as DepVerbose
				r.report_cached( { .proc=Proc::DepVerbosePush , .sync=Maybe , .file=::move(sr.real) , .file_info=fi , .txt="depend.push" } , true/*force*/ ) ;
			}
		}
		if (!verbose) return {} ;
		// if verbose, we de facto fully access files
		JobExecRpcReply reply = r.report_sync( { .proc=Proc::DepVerbose , .sync=Yes , .digest=ad|~Accesses() , .date=pd , .txt="depend" } , true/*force*/ ) ;
		if (!reply) {
			::vector<pair<Bool3/*ok*/,Crc>> dep_infos ;
			for( ::string const& f : files ) dep_infos.emplace_back(Yes/*ok*/,Crc(f)) ;
		}
		return reply.dep_infos ;
	}

	void target( Record const& r , ::vector_s&& files , AccessDigest ad ) {
		::vmap_s<FileInfo> targets ;
		_chk_files(files) ;
		Pdate pd { New } ;           // for perf and in trace, we will see all targets with same date
		for( ::string& f : files ) {
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({
				.file      = ::move(f)
			,	.no_follow = true
			,	.read      = false
			,	.write     = true
			,	.create    = true
			,	.comment   = "target"
			}) ;
			// defensive only as sr.accesses is always empty when no_follow
			r.report_access( sr.file_loc , { .digest=ad|sr.accesses , .date=pd , .file=::move(sr.real) } , sr.file_loc0 , ::move(sr.real0) , true/*force*/ ) ;
		}
	}

	Bool3 check_deps( Record const& r , bool verbose ) {
		return r.report_sync({ .proc=Proc::ChkDeps , .sync=No|verbose , .date=New }).ok ;
	}

	::pair_s<bool/*ok*/> decode( Record const& r , ::string&& file , ::string&& code , ::string&& ctx ) {
		Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({.file=::move(file),.read=true,.write=true,.comment="decode"}) ;
		//
		r.report_access( sr.file_loc , { .digest={.accesses=sr.accesses} , .file=::copy(sr.real) , .file_info=sr.file_info } , "decode" ) ;
		//
		JobExecRpcReply reply = r.report_sync({ .proc=Proc::Decode , .sync=Yes , .file=::move(sr.real) , .txt=::move(code) , .ctx=::move(ctx) }) ;
		//
		return { ::move(reply.txt) , reply.ok==Yes } ;
	}

	::pair_s<bool/*ok*/> encode( Record const& r , ::string&& file , ::string&& val , ::string&& ctx , uint8_t min_len ) {
		Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({.file=::move(file),.read=true,.write=true,.comment="encode"}) ;
		//
		r.report_access( sr.file_loc , { .digest={.accesses=sr.accesses} , .file=::copy(sr.real) , .file_info=sr.file_info } , "encode" ) ;
		//
		JobExecRpcReply reply = r.report_sync({ .proc=Proc::Encode , .sync=Yes , .min_len=min_len , .file=::move(sr.real) , .txt=::move(val) , .ctx=::move(ctx) }) ;
		//
		return { ::move(reply.txt) , reply.ok==Yes } ;
	}

}
