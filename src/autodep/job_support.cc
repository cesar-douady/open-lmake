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
		for( ::string const& f : files ) if (f.size()>PATH_MAX) throw "file name too long ("s+f.size()+" characters)" ;
	}

	::vector<pair<Bool3/*ok*/,Crc>> depend( Record const& r , ::vector_s&& files , AccessDigest ad , bool no_follow , bool verbose ) {
		::vmap_s<FileInfo> deps ;
		_chk_files(files) ;
		for( ::string& f : files ) {
			if (f.size()>PATH_MAX) throw "file name too long ("s+f.size()+" characters)" ;
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({.file=::move(f),.no_follow=no_follow,.read=true,.write=false,.comment="depend"}) ;
			if (sr.file_loc<=FileLoc::Dep) deps.emplace_back(::move(sr.real),sr.file_info) ;
		}
		ad.accesses = ~Accesses() ;
		if (verbose) {
			JobExecRpcReply reply = r.report_sync_access( JobExecRpcReq( Proc::DepVerbose , ::move(deps) , ad , true/*verbose*/ , "depend" ) ) ;
			return reply.dep_infos ;
		} else {
			r.report_sync_access( JobExecRpcReq( Proc::Access , 0 , ::move(deps) , ad , false/*verbose*/ , "depend" ) ) ;
			return {} ;
		}
	}

	void target( Record const& r , ::vector_s&& files , AccessDigest ad , bool no_follow ) {
		::vmap_s<FileInfo> targets ;
		_chk_files(files) ;
		for( ::string& f : files ) {
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({.file=::move(f),.no_follow=no_follow,.read=false,.write=true,.create=true,.comment="target"}) ;
			if (sr.file_loc<=FileLoc::Repo) targets.emplace_back(::move(sr.real),sr.file_info) ;
			/**/                            ad.accesses |= sr.accesses ;                         // pessimistic but in practice, sr.accesses is identical for all files (empty if no_follow, else Lnk)
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
