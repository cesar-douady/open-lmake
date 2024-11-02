// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"

#include "backdoor.hh"
#include "job_support.hh"

using namespace Disk ;
using namespace Hash ;

using Proc = JobExecProc ;

namespace JobSupport {

	static void _chk_files(::vector_s const& files) {
		for( ::string const& f : files ) throw_unless( f.size()<=PATH_MAX , "file name too long (",f.size()," characters)" ) ;
	}

	::vector<pair<Bool3/*ok*/,Crc>> depend( Record const& r , ::vector_s&& files , AccessDigest ad , bool no_follow , bool verbose ) {
		::vmap_s<FileInfo> deps ;
		_chk_files(files) ;
		for( ::string& f : files ) {
			throw_unless( f.size()<=PATH_MAX , "file name too long (",f.size()," characters)" ) ;
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({.file=::move(f),.no_follow=no_follow,.read=true,.write=false,.comment="depend"}) ;
			if (sr.file_loc<=FileLoc::Dep) deps.emplace_back(::move(sr.real),sr.file_info) ;
		}
		// use sync reports even when no reply is necessary to ensure correct ordering with following requests using a different report Fd
		if (verbose) { JobExecRpcReply reply = r.report_sync_access( JobExecRpcReq( Proc::DepVerbose       , ::move(deps) , ad , "depend" ) ) ; return reply.dep_infos ; }
		else         {                         r.report_sync_access( JobExecRpcReq( Proc::Access , 0/*id*/ , ::move(deps) , ad , "depend" ) ) ; return {}              ; }
	}

	void target( Record const& r , ::vector_s&& files , AccessDigest ad ) {
		::vmap_s<FileInfo> targets ;
		_chk_files(files) ;
		for( ::string& f : files ) {
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({.file=::move(f),.no_follow=true,.read=false,.write=true,.create=true,.comment="target"}) ;
			if (sr.file_loc<=FileLoc::Repo) targets.emplace_back(::move(sr.real),sr.file_info) ;
			/**/                            ad.accesses |= sr.accesses ;                         // pessimistic but in practice, sr.accesses is empty for all files
		}
		r.report_async_access( JobExecRpcReq( Proc::Access , 0 , ::move(targets) , ad , "target" ) ) ;
	}

	Bool3 check_deps( Record const& r , bool verbose ) {
		return r.report_sync_direct(JobExecRpcReq(Proc::ChkDeps,verbose/*sync*/)).ok ;
	}

	::pair_s<bool/*ok*/> decode( Record const& r , ::string&& file , ::string&& code , ::string&& ctx ) {
		Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({.file=::move(file),.read=true,.write=true,.comment="decode"}) ;
		//
		r.report_async_access( JobExecRpcReq( Proc::Access , 0 , {{sr.real,sr.file_info}} , {.accesses=sr.accesses} , "decode" ) ) ;
		//
		JobExecRpcReply reply = r.report_sync_direct(JobExecRpcReq( Proc::Decode , ::move(sr.real) , ::move(code) , ::move(ctx) )) ;
		//
		return { ::move(reply.txt) , reply.ok==Yes } ;
	}

	::pair_s<bool/*ok*/> encode( Record const& r , ::string&& file , ::string&& val , ::string&& ctx , uint8_t min_len ) {
		Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({.file=::move(file),.read=true,.write=true,.comment="encode"}) ;
		//
		r.report_async_access( JobExecRpcReq( Proc::Access , 0 , {{sr.real,sr.file_info}} , {.accesses=sr.accesses} , "encode" ) ) ;
		//
		JobExecRpcReply reply = r.report_sync_direct(JobExecRpcReq( Proc::Encode , ::move(sr.real) , ::move(val) , ::move(ctx) , min_len )) ;
		//
		return { ::move(reply.txt) , reply.ok==Yes } ;
	}

}
