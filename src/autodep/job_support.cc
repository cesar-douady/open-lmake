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
		::vmap_s<FileInfo> deps      ;
		Pdate              now       { New }             ;                           // all dates must be identical to be recognized as parallel deps
		NodeIdx            di        = 0                 ;
		::vector<NodeIdx>  dep_idxs1 ;
		bool               sync      = verbose || direct ;
		for( ::string& f : files ) {
			if (regexpr) {
				r.report_direct( { .proc=JobExecProc::AccessPattern , .comment=Comment::depend , .digest=ad , .date=now , .file=f } , true/*force*/ ) ;
				continue ;
			}
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({ .file=::move(f) , .no_follow=no_follow , .read=true , .write=false , .comment=Comment::depend }) ;
			if (!sync) {
				if ( readdir_ok && sr.file_loc==FileLoc::RepoRoot ) {                // when passing flag readdir_ok, we may want to report top-level dir
					sr.file_loc = FileLoc::Repo ;
					sr.real     = "."s          ;                                    // /!\ a (warning) bug in gcc-12 is triggered if using "." here instead of "."s
					sr.accesses = {}            ;                                    // besides passing ReadirOk flag, top-level dir is external
					ad.accesses = {}            ;                                    // .
				}
				r.report_access( sr.file_loc , { .comment=Comment::depend , .digest=ad|sr.accesses , .date=now , .file=::move(sr.real) , .file_info=sr.file_info } , true/*force*/ ) ;
			} else if (sr.file_loc<=FileLoc::Dep) {
				ad |= sr.accesses ;                                                  // seems pessimistic but sr.accesses does not actually depend on file, only on no_follow, read and write
				// not actually sync, but transport as sync to use the same fd as DepVerbose
				r.report_sync( { .proc=Proc::DepPush , .sync=Maybe , .file=::move(sr.real) , .file_info=sr.file_info } , true/*force*/ ) ;
				dep_idxs1.push_back(++di) ;                                                                                                // 0 is reserved to mean no dep info
			} else {
				dep_idxs1.push_back(0) ;                                                                                                   // .
			}
		}
		if ( !sync || !di ) return {{},true/*ok*/} ;                                                                                       // dont send request if nothing to ask
		//
		SWEAR(verbose!=direct) ;
		JobExecProc proc = verbose ? JobExecProc::DepVerbose : JobExecProc::DepDirect ;
		CommentExts ces  = verbose ? CommentExt::Verbose     : CommentExt::Direct     ;
		//
		if (verbose) ad.accesses |= ~Accesses() ;
		JobExecRpcReply                          reply = r.report_sync( { .proc=proc , .sync=Yes , .comment=Comment::depend , .comment_exts=ces , .digest=ad , .date=now } , true/*force*/ ) ;
		::pair<::vector<VerboseInfo>,bool/*ok*/> res   { {} , true/*ok*/ }                                                                                                                   ;
		SWEAR(verbose!=direct) ;
		if (verbose)
			for( size_t i : iota(files.size()) )
				if      (!dep_idxs1[i]) res.first.push_back({ .ok=Maybe                      }         ) ;                                 // 0 is reserved to mean no dep info
				else if (!reply       ) res.first.push_back({ .ok=Yes   , .crc=Crc(files[i]) }         ) ;                                 // there was no server, mimic it
				else                    res.first.push_back(::move(reply.verbose_infos[dep_idxs1[i]-1])) ;
		else
			res.second = reply.ok==Yes ;
		return res ;
	}

	void target( Record const& r , ::vector_s&& files , AccessDigest ad , bool regexpr ) {
		SWEAR(ad.write!=Maybe) ;                                                                                                    // useless, but if necessary, implement a confirmation mecanism
		if (regexpr) {
			throw_unless( ad.write==No , "regexpr and write are exclusive" ) ;
			ad.flags.extra_dflags &= ~ExtraDflag::NoStar ;                                                                          // it is meaningless to exclude regexpr when we are a regexpr
		}
		_chk_files(files) ;
		::vmap_s<FileInfo> targets ;
		Pdate              now     { New } ;                                                                                        // for perf and in trace, we will see all targets with same date
		for( ::string& f : files ) {
			if (regexpr) {
				r.report_direct( { .proc=JobExecProc::AccessPattern , .comment=Comment::target , .digest=ad , .date=now , .file=f } , true/*force*/ ) ;
				continue ;
			}
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({
				.file      = ::move(f)
			,	.no_follow = true
			,	.read      = false
			,	.write     = true
			,	.create    = true
			,	.comment   = Comment::target
			}) ;
			r.report_access(
				sr.file_loc
			,	{ .comment=Comment::target , .digest=ad|sr.accesses , .date=now , .file=::move(sr.real) , .file_info=sr.file_info } // sr.accesses is defensive only as it is empty when no_follow
			,	sr.file_loc0
			,	::move(sr.real0)
			,	true/*force*/
			) ;
		}
	}

	Bool3 check_deps( Record const& r , bool sync ) {
		return r.report_sync({ .proc=Proc::ChkDeps , .sync=No|sync , .comment=Comment::chkDeps , .date=New }).ok ;
	}

	::vector_s list( Record const& r , Bool3 write , ::optional_s const& dir , ::optional_s const& regexpr ) {
		// report files as seen from cwd
		::string const& repo_root_s = Record::s_autodep_env().repo_root_s ;
		::optional_s    abs_dir_s   ;
		if (+dir) {
			Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({ .file=::copy(*dir) , .no_follow=true , .read=false , .write=false , .comment=Comment::list }) ;
			abs_dir_s = mk_glb_s( with_slash(sr.real) , repo_root_s ) ;
		}
		::vector_s          res       ;
		::string            abs_cwd_s = cwd_s() ;
		::optional_s        lcl_cwd_s ;           if ( abs_cwd_s.starts_with(repo_root_s) ) lcl_cwd_s = mk_lcl_s(abs_cwd_s,repo_root_s) ;
		::optional<RegExpr> re        ;           if ( +regexpr                           ) re        = *regexpr                        ;
		//
		for( ::string& f : r.report_sync({ .proc=Proc::List , .sync=Yes , .comment=Comment::list , .digest{.write=write} , .date=New }).files ) {
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
		Backdoor::Solve::Reply sr          = Backdoor::call<Backdoor::Solve>({ .file=::copy(dir) , .no_follow=true , .read=false , .write=false , .comment=Comment::list }) ;
		::string               dir_s       = with_slash(sr.real)                                                                                                            ;
		::string               abs_dir_s   = mk_glb_s( dir_s , repo_root_s )                                                                                                ;
		::string               abs_cwd_s   = cwd_s()                                                                                                                        ;
		//
		if ( abs_cwd_s.starts_with(repo_root_s) && !is_abs_s(dir_s) ) return mk_rel_s( dir_s , mk_lcl_s(abs_cwd_s,repo_root_s) ) ;
		else                                                          return abs_dir_s                                           ;
	}

	template<bool Encode> static ::pair_s<bool/*ok*/> codec( Record const& r , ::string&& file , ::string&& code_val , ::string&& ctx , uint8_t min_len=0 ) {
		if (Encode) { //!                                                                                                           ok
			if (min_len<=1            ) return { cat("min_len (",min_len,") must be at least 1"                                 ) , false } ;
			if (min_len> sizeof(Crc)*2) return { cat("min_len (",min_len,") must be at most checksum length (",sizeof(Crc)*2,')') , false } ; // codes are output in hex, 4 bits/digit
		}
		Comment comment = Encode ? Comment::encode : Comment::decode ;
		Backdoor::Solve::Reply sr = Backdoor::call<Backdoor::Solve>({ .file=::move(file) , .read=true , .write=true , .comment=comment }) ;
		//
		r.report_access( sr.file_loc , { .comment=comment , .digest={.accesses=sr.accesses} , .file=::copy(sr.real) , .file_info=sr.file_info } , true/*force*/ ) ;
		// transport as sync to use the same fd as Encode/Decode
		r.report_sync(     { .proc=Proc::CodecFile                  , .sync=Maybe , .comment=comment , .file=::move(sr.real ) } , true/*force*/ ) ;
		r.report_sync(     { .proc=Proc::CodecCtx                   , .sync=Maybe , .comment=comment , .file=::move(ctx     ) } , true/*force*/ ) ;
		JobExecRpcReq jerr { .proc=Encode?Proc::Encode:Proc::Decode , .sync=Yes   , .comment=comment , .file=::move(code_val) } ;
		if (Encode) jerr.min_len() = min_len ;
		//
		JobExecRpcReply reply = r.report_sync( ::move(jerr) , true/*force*/ ) ;
		return { ::move(reply.txt) , reply.ok==Yes } ;
	}

	::pair_s<bool/*ok*/> decode(Record const& r,::string&& file,::string&& code,::string&& ctx                ) { return codec<false/*Encode*/>(r,::move(file),::move(code),::move(ctx)        ) ; }
	::pair_s<bool/*ok*/> encode(Record const& r,::string&& file,::string&& val ,::string&& ctx,uint8_t min_len) { return codec<true /*Encode*/>(r,::move(file),::move(val ),::move(ctx),min_len) ; }

}
