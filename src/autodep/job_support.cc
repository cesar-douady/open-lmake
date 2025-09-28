// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "re.hh"
#include "time.hh"

#include "backdoor.hh"
#include "job_support.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Re   ;
using namespace Time ;

using Proc = JobExecProc ;

namespace JobSupport {

	static void _chk_files(::vector_s const& files) {
		for( ::string const& f : files ) throw_unless( f.size()<=PATH_MAX , "file name too long (",f.size()," characters)" ) ;
	}

	::pair<::vector<VerboseInfo>,bool/*ok*/> depend( Record const& r , ::vector_s&& files , AccessDigest ad , bool no_follow , bool regexpr ) {
		bool verbose    = ad.flags.dflags      [Dflag     ::Verbose]   ;
		bool direct     = ad.flags.extra_dflags[ExtraDflag::Direct ]   ;
		bool readdir_ok = ad.flags.extra_dflags[ExtraDflag::ReaddirOk] ;
		throw_if( verbose && direct , "verbose and direct are exclusive" ) ;
		if (regexpr) {
			SWEAR(ad.write==No) ;
			throw_if( !no_follow   , "regexpr and follow_symlinks are exclusive" ) ;
			throw_if( +ad.accesses , "regexpr and read are exclusive"            ) ;
			throw_if( verbose      , "regexpr and verbose are exclusive"         ) ;
			throw_if( direct       , "regexpr and direct are exclusive"          ) ;
			ad.flags.extra_dflags &= ~ExtraDflag::NoStar ;                           // it is meaningless to exclude regexpr when we are a regexpr
		}
		if (readdir_ok) {
			ad.flags.dflags &= ~Dflag::Required ;                                    // ReaddirOk means dep is expected to be a dir, it is non-sens to require it to be buidlable
			ad.read_dir     |= +ad.accesses     ;                                    // if reading and allow dir access, assume user meant reading a dir
		}
		_chk_files(files) ;
		//
		if (regexpr) {
			::vmap_s<FileInfo> deps ; for( ::string& f : files ) deps.emplace_back(::move(f),FileInfo()) ;
			r.report_direct( { .proc=JobExecProc::AccessPattern , .comment=Comment::Depend , .digest=ad , .date=New , .files=deps } , true/*force*/ ) ;
			return {{},true/*ok*/} ;                                                                                                        // dont send request if nothing to ask
		}
		bool               sync      = verbose || direct ;
		::vector<NodeIdx>  dep_idxs1 ;                     if (sync) dep_idxs1.reserve(files.size()) ;
		::vmap_s<FileInfo> deps      ;                               deps     .reserve(files.size()) ;                                      // typically all files are pertinent
		for( ::string& f : files ) {
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({ .file=::move(f) , .no_follow=no_follow , .read=true , .write=false , .comment=Comment::Depend }) ;
			if ( readdir_ok && sr.file_loc==FileLoc::RepoRoot ) { // when passing flag readdir_ok, we may want to report top-level dir
				sr.file_loc = FileLoc::Repo ;
				sr.real     = "."s          ;                     // /!\ a (warning) bug in gcc-12 is triggered if using "." here instead of "."s
			}
			if (sr.file_loc>FileLoc::Dep) {
				if (sync) dep_idxs1.push_back(0) ;                // 0 means no dep info
				continue ;
			}
			ad |= sr.accesses ;                                   // seems pessimistic but sr.accesses does not actually depend on file, only on no_follow, read and write
			if (sync) {
				sr.file_info = {} ;
				dep_idxs1.push_back(deps.size()+1) ;              // start at 1 as 0 is reserved to mean no dep info
			}
			deps.emplace_back(::move(sr.real),sr.file_info) ;
		}
		::pair<::vector<VerboseInfo>,bool/*ok*/> res { {} , true/*ok*/ } ;
		if (+deps) {
			JobExecRpcReq jerr { .comment=Comment::Depend , .digest=ad , .date=New , .files=deps } ;
			if (verbose) {
				jerr.proc            = JobExecProc::DepVerbose ;
				jerr.sync            = Yes                     ;
				jerr.comment_exts    = CommentExt::Verbose     ;
				jerr.digest.accesses = ~Accesses()             ;                                          // verbose captures full content of dep
				//
				JobExecRpcReply reply = r.report_sync( ::move(jerr) , true/*force*/ ) ;
				//
				res.first.reserve(files.size()) ;
				for( size_t i : iota(files.size()) )
					if (!dep_idxs1[i]) res.first.push_back({.ok=Maybe}                                ) ; // 0 is reserved to mean no dep info
					else               res.first.push_back(::move(reply.verbose_infos[dep_idxs1[i]-1])) ;
			} else if (direct) {
				jerr.proc         = JobExecProc::DepDirect ;
				jerr.sync         = Yes                    ;
				jerr.comment_exts = CommentExt::Direct     ;
				//
				JobExecRpcReply reply = r.report_sync( ::move(jerr) , true/*force*/ ) ;
				//
				res.second = reply.ok==Yes ;
			} else {
				r.report_access( ::move(jerr) , true/*force*/ ) ;
			}
		}
		return res ;
	}

	void target( Record const& r , ::vector_s&& files , AccessDigest ad , bool regexpr ) {
		using Reply = Backdoor::Solve::Reply ;
		SWEAR(ad.write!=Maybe) ;                                                                                         // useless, but if necessary, implement a confirmation mecanism
		if (regexpr) {
			throw_unless( ad.write==No , "regexpr and write are exclusive" ) ;
			ad.flags.extra_dflags &= ~ExtraDflag::NoStar ;                                                               // it is meaningless to exclude regexpr when we are a regexpr
		}
		_chk_files(files) ;
		if (regexpr) {
			::vmap_s<FileInfo> targets ; for( ::string& f : files ) targets.emplace_back( ::move(f) , FileInfo() ) ;
			r.report_direct( { .proc=JobExecProc::AccessPattern , .comment=Comment::Target , .digest=ad , .date=New , .files=targets } , true/*force*/ ) ;
			return ;
		}
		::vector<Reply> srs ; srs.reserve(files.size()) ;
		for( ::string& f : files ) {
			srs.push_back( Backdoor::call<Backdoor::Solve>({
				.file      = ::move(f)
			,	.no_follow = true
			,	.read      = false
			,	.write     = true
			,	.create    = true
			,	.comment   = Comment::Target
			}) ) ;
			ad |= srs.back().accesses ;                                                                                  // defensive only as sr.accesses is empty when no_follow
		}
		if (::all_of( srs , [](Reply const& sr) { return sr.file_loc<=FileLoc::Repo && !sr.real0 ; } )) {                // fast path : only a single call to report_access (most common case)
			::vmap_s<FileInfo> targets ; for( Reply& sr : srs ) targets.emplace_back( ::move(sr.real) , sr.file_info ) ;
			r.report_access( { .comment=Comment::Target , .digest=ad , .date=New , .files=targets } , true/*force*/ ) ;
		} else {
			Pdate now { New } ;    // for perf and in trace, we will see all targets with same date, but after potential link accesses while solving
			for( Reply& sr : srs )
				r.report_access(
					sr.file_loc
				,	{ .comment=Comment::Target , .digest=ad , .date=now , .files={{::move(sr.real),sr.file_info}} }
				,	sr.file_loc0
				,	::move(sr.real0)
				,	true/*force*/
				) ;
		}
	}

	Bool3 check_deps( Record const& r , Delay delay , bool sync ) {
		return r.report_sync({ .proc=Proc::ChkDeps , .sync=No|sync , .comment=Comment::ChkDeps , .date=Pdate(New)+delay }).ok ;
	}

	::vector_s list( Record const& r , Bool3 write , ::optional_s const& dir , ::optional_s const& regexpr ) {
		// report files as seen from cwd
		::string const& repo_root_s = Record::s_autodep_env().repo_root_s ;
		::optional_s    abs_dir_s   ;
		if (+dir) {
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({ .file=::copy(*dir) , .no_follow=true , .read=false , .write=false , .comment=Comment::List }) ;
			abs_dir_s = mk_glb_s( with_slash(sr.real) , repo_root_s ) ;
		}
		::vector_s          res       ;
		::string            abs_cwd_s = cwd_s() ;
		::optional_s        lcl_cwd_s ;           if ( abs_cwd_s.starts_with(repo_root_s) ) lcl_cwd_s = mk_lcl_s(abs_cwd_s,repo_root_s) ;
		::optional<RegExpr> re        ;           if ( +regexpr                           ) re        = *regexpr                        ;
		//
		for( ::string& f : r.report_sync({ .proc=Proc::List , .sync=Yes , .comment=Comment::List , .digest{.write=write} , .date=New }).files ) {
			::string abs_f = mk_glb( ::move(f) , repo_root_s ) ;
			//
			if ( +dir && !abs_f.starts_with(*abs_dir_s) ) continue ;
			//
			::string& user_f = abs_f ;                                          // reuse storage
			if ( +lcl_cwd_s && !is_abs(f) ) user_f = mk_rel( f , *lcl_cwd_s ) ; // else keep abs_f as is
			//
			if ( +regexpr && !re->match(user_f) ) continue ;
			//
			res.push_back(::move(user_f)) ;
		}
		//
		return res ;
	}

	::string list_root_s( ::string const& dir ) {
		// report dir as used as prefix when listing dir
		::string const&        repo_root_s = Record::s_autodep_env().repo_root_s                                                                                            ;
		Backdoor::Solve::Reply sr          = Backdoor::call<Backdoor::Solve>({ .file=::copy(dir) , .no_follow=true , .read=false , .write=false , .comment=Comment::List }) ;
		::string               dir_s       = with_slash(sr.real)                                                                                                            ;
		::string               abs_dir_s   = mk_glb_s( dir_s , repo_root_s )                                                                                                ;
		::string               abs_cwd_s   = cwd_s()                                                                                                                        ;
		//
		if ( abs_cwd_s.starts_with(repo_root_s) && !is_abs_s(dir_s) ) return mk_rel_s( dir_s , mk_lcl_s(abs_cwd_s,repo_root_s) ) ;
		else                                                          return abs_dir_s                                           ;
	}

	template<bool Encode> static ::pair_s<bool/*ok*/> _codec( Record const& r , ::string&& file , ::string&& ctx , ::string&& code_val , uint8_t min_len=0 ) {
		if (Encode) { //!                                                                                                          ok
			if (min_len<1            ) return { cat("min_len (",min_len,") must be at least 1"                                 ) , false } ;
			if (min_len>sizeof(Crc)*2) return { cat("min_len (",min_len,") must be at most checksum length (",sizeof(Crc)*2,')') , false } ; // codes are output in hex, 4 bits/digit
		}
		Comment comment = Encode ? Comment::Encode : Comment::Decode ;
		Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({ .file=::move(file) , .read=true , .write=true , .comment=comment }) ;
		//
		if (+sr.accesses) r.report_access( sr.file_loc , { .comment=comment , .digest={.accesses=sr.accesses} , .files={{::copy(sr.real),sr.file_info}} } , true/*force*/ ) ;
		// transport as sync to use the same fd as Encode/Decode
		JobExecRpcReq jerr { .sync=Yes   , .comment=comment , .date=New , .files={{::move(sr.real),{}},{::move(ctx),{}},{::move(code_val),{}}} } ;
		if (Encode) { jerr.proc = Proc::Encode ; jerr.min_len = min_len ; }
		else          jerr.proc = Proc::Decode ;
		//
		JobExecRpcReply reply = r.report_sync( ::move(jerr) , true/*force*/ ) ;
		return { ::move(reply.txt) , reply.ok==Yes } ;
	}

	//       ok                                                                                                                    Encode
	::pair_s<bool> decode( Record const& r , ::string&& file , ::string&& code , ::string&& ctx                   ) { return _codec<false>(r,::move(file),::move(ctx),::move(code)        ) ; }
	::pair_s<bool> encode( Record const& r , ::string&& file , ::string&& val  , ::string&& ctx , uint8_t min_len ) { return _codec<true >(r,::move(file),::move(ctx),::move(val ),min_len) ; }

}
