// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "re.hh"

#include "backdoor.hh"

using namespace Re   ;
using namespace Time ;

namespace Backdoor {

	::umap_s<Func> const& get_func_tab() {
		static ::umap_s<Func> s_tab = {
			{ Enable                ::Cmd , func<Enable                > }
		,	{ Regexpr               ::Cmd , func<Regexpr               > }
		,	{ Depend                ::Cmd , func<Depend                > }
		,	{ DependVerbose         ::Cmd , func<DependVerbose         > }
		,	{ DependDirect          ::Cmd , func<DependDirect          > }
		,	{ Target                ::Cmd , func<Target                > }
		,	{ ChkDeps               ::Cmd , func<ChkDeps               > }
		,	{ List                  ::Cmd , func<List                  > }
		,	{ ListRootS             ::Cmd , func<ListRootS             > }
		,	{ Codec<false/*Encode*/>::Cmd , func<Codec<false/*Encode*/>> }
		,	{ Codec<true /*Encode*/>::Cmd , func<Codec<true /*Encode*/>> }
		} ;
		return s_tab ;
	}

	//
	// Enable
	//

	::string& operator+=( ::string& os , Enable const& e ) { // START_OF_NO_COV
		return os<<"Enable("<<e.enable<<')' ;
	}                                                        // END_OF_NO_COV

	bool/*enabled*/ Enable::process(Record& r) {
		bool res = r.enable ;
		if (enable!=Maybe) {
			r.enable = enable==Yes ;
			Record::s_enable_was_modified = true ; // if autodep=ptrace, managing enable is quite expansive and is only done if enable was manipulated, so it must be aware
		}
		return res ;
	}

	::string Enable::descr() const {
		switch (enable) {
			case No    : return "disable autodep"   ;
			case Yes   : return "enable autodep"    ;
			case Maybe : return "get autodep state" ;
		DF}
	}

	//
	// Regexpr
	//

	::string& operator+=( ::string& os , Regexpr const& re ) {           // START_OF_NO_COV
		return os << "Regexpr(" << re.files << re.access_digest << ')' ;
	}                                                                    // END_OF_NO_COV

	Void Regexpr::process(Record& r) {
		::vmap_s<FileInfo> files_ ; for( ::string& f : files ) files_.emplace_back(::move(f),FileInfo()) ;
		r.report_sync( { .proc=JobExecProc::AccessPattern , .comment=+access_digest.write?Comment::Target:Comment::Depend , .digest=access_digest , .date=New , .files=::move(files_) } ) ;
		return {} ;
	}

	::string Regexpr::descr() const {
		const char* kind = +access_digest.write ? "target" : "dep" ;
		if (files.size()==1) return cat(kind," regexpr " ,files[0]) ;
		else                 return cat(kind," regexprs ",files   ) ;
	}

	//
	// AccessBase
	//

	::vmap_s<FileInfo> AccessBase::_mk_deps( Record& r , bool sync , ::vector<NodeIdx>* /*out*/ dep_idxs1 , CommentExts ces ) {
		::vmap_s<FileInfo> res ; res.reserve(files.size()) ;                                                                    // typically all files are pertinent
		Accesses           as  ;
		if (dep_idxs1) dep_idxs1->reserve(files.size()) ;
		for( ::string& f : files ) {
			Record::Solve<false/*Send*/> sr { r , f , no_follow , bool(+access_digest.accesses) , false/*create*/ , Comment::Depend , ces } ;
			if ( access_digest.flags.extra_dflags[ExtraDflag::ReaddirOk] && sr.file_loc==FileLoc::RepoRoot ) { // when passing flag readdir_ok, we may want to report top-level dir
				sr.file_loc = FileLoc::Repo ;
				sr.real     = "."s          ;                                                                  // /!\ a (warning) bug in gcc-12 is triggered if using "." here instead of "."s
			}
			if (sr.file_loc>FileLoc::Dep) {
				if (dep_idxs1) dep_idxs1->push_back(0) ;                         // 0 means no dep info
				continue ;
			}
			as |= sr.accesses ;                                                  // seems pessimistic but sr.accesses does not actually depend on file, only on no_follow, read and write
			if (dep_idxs1) dep_idxs1->push_back(res.size()+1)                  ; // start at 1 as 0 is reserved to mean no dep info
			if (sync     ) res.emplace_back(::move(sr.real),FileInfo(       )) ;
			else           res.emplace_back(::move(sr.real),FileInfo(sr.real)) ;
		}
		access_digest.accesses |= as ;
		return res ;
	}

	//
	// Depend
	//

	::string& operator+=( ::string& os , Depend const& d ) {                   // START_OF_NO_COV
		/**/              os << "Depend(" << d.files <<','<< d.access_digest ;
		if ( d.no_follow) os << ",no_follow"                                 ;
		return            os << ')'                                          ;
	}                                                                          // END_OF_NO_COV

	Void Depend::process(Record& r) {
		JobExecRpcReq jerr {
			.proc    = JobExecProc::Access
		,	.comment = Comment::Depend
		,	.digest  = access_digest
		,	.date    = New
		,	.files   = _mk_deps( r , false/*sync*/ )
		} ;
		if (+jerr.files) {
			r.report_access( ::move(jerr) , true/*force*/ ) ;
			r.send_report() ;
		}
		return {} ;
	}

	//
	// DependVerbose
	//

	::string& operator+=( ::string& os , DependVerbose const& dv ) {                     // START_OF_NO_COV
		/**/               os << "DependVerbose(" << dv.files <<','<< dv.access_digest ;
		if ( dv.no_follow) os << ",no_follow"                                          ;
		return             os << ')'                                                   ;
	}                                                                                    // END_OF_NO_COV

	::vector<VerboseInfo> DependVerbose::process(Record& r) {
		::vector<NodeIdx    > dep_idxs1 ;
		::vector<VerboseInfo> res       ; res.reserve(files.size()) ;
		//
		access_digest.accesses = ~Accesses() ;                                              // verbose captures full content of dep
		JobExecRpcReq jerr {
			.proc         = JobExecProc::DepVerbose
		,	.sync         = Yes
		,	.comment      = Comment::Depend
		,	.comment_exts = CommentExt::Verbose
		,	.digest       = access_digest
		,	.date         = New
		,	.files        = _mk_deps( r , true/*sync*/ , &dep_idxs1 , CommentExt::Verbose )
		} ;
		JobExecRpcReply reply ; if (+jerr.files) reply = r.report_sync(::move(jerr)) ;
		for( size_t i : iota(files.size()) )
			if (!dep_idxs1[i]) res.push_back({.ok=Maybe}                                ) ; // 0 is reserved to mean no dep info
			else               res.push_back(::move(reply.verbose_infos[dep_idxs1[i]-1])) ;
		return res ;
	}

	//
	// DependDirect
	//

	::string& operator+=( ::string& os , DependDirect const& dd ) {                     // START_OF_NO_COV
		/**/               os << "DependDirect(" << dd.files <<','<< dd.access_digest ;
		if ( dd.no_follow) os << ",no_follow"                                         ;
		return             os << ')'                                                  ;
	}                                                                                   // END_OF_NO_COV

	bool/*ok*/ DependDirect::process(Record& r) {
		JobExecRpcReq jerr {
			.proc         = JobExecProc::DepDirect
		,	.sync         = Yes
		,	.comment      = Comment::Depend
		,	.comment_exts = CommentExt::Direct
		,	.digest       = access_digest
		,	.date         = New
		,	.files        = _mk_deps( r , true/*sync*/ , nullptr , CommentExt::Direct )
		} ;
		if (+jerr.files) return r.report_sync(::move(jerr)).ok==Yes ;
		else             return true/*ok*/                          ;
	}

	//
	// Target
	//

	::string& operator+=( ::string& os , Target const& t ) {                  // START_OF_NO_COV
		/**/             os << "Target(" << t.files <<','<< t.access_digest ;
		if (t.no_follow) os << ",no_follow"                                 ;
		return           os << ')'                                          ;
	}                                                                         // END_OF_NO_COV

	Void Target::process(Record& r) {
		::vector<Record::Solve<false/*Send*/>> srs         ;         srs.reserve(files.size()) ;
		Accesses                               as          ;
		bool                                   has_overlay = false ;
		for( ::string& f : files ) {
			Record::Solve<false/*Send*/> sr { r , f , no_follow , bool(+access_digest.accesses) , true/*create*/ , Comment::Target } ;
			as          |= sr.accesses ; // seems pessimistic but sr.accesses does not actually depend on file, only on no_follow, read and write
			has_overlay |= +sr.real0   ;
			srs.push_back(::move(sr)) ;
		}
		access_digest.accesses |= as ;
		if (!has_overlay) {              // fast path : only a single call to report_access (most common case)
			::vmap_s<FileInfo> targets ;
			for( Record::Solve<false/*Send*/>& sr : srs )
				if (sr.file_loc<=FileLoc::Repo)
					targets.emplace_back( ::move(sr.real) , FileInfo(sr.real) ) ;
			r.report_access( { .proc=JobExecProc::Access , .comment=Comment::Target , .digest=access_digest , .date=New , .files=::move(targets) } , true/*force*/ ) ;
		} else {
			Pdate now { New } ;          // for perf and in trace, we will see all targets with same date, but after potential link accesses while solving
			for( Record::Solve<false/*Send*/>& sr : srs )
				r.report_access(
					sr.file_loc
				,	{ .proc=JobExecProc::Access , .comment=Comment::Target , .digest=access_digest , .date=now , .files={{::move(sr.real),FileInfo(sr.real)}} }
				,	sr.file_loc0
				,	::move(sr.real0)
				,	true/*force*/
				) ;
		}
		r.send_report() ;
		return {} ;
	}

	//
	// ChkDeps
	//

	::string& operator+=( ::string& os , ChkDeps const& cd ) { // START_OF_NO_COV
		First first ;
		/**/           os << "ChkDeps("              ;
		if (+cd.delay) os <<first("",",")<< cd.delay ;
		if (+cd.sync ) os <<first("",",")<< "sync"   ;
		return         os << ')'                     ;
	}                                                          // END_OF_NO_COV

	Bool3 ChkDeps::process(Record& r) {
		return r.report_sync({ .proc=JobExecProc::ChkDeps , .sync=No|sync , .comment=Comment::ChkDeps , .date=Pdate(New)+delay }).ok ;
	}

	//
	// List
	//

	::string& operator+=( ::string& os , List const& l ) { // START_OF_NO_COV
		/**/            os << "List(" << l.write ;
		if (+l.dir    ) os <<','<< l.dir         ;
		if (+l.regexpr) os <<','<< l.regexpr     ;
		return          os << ')'                ;
	}                                                      // END_OF_NO_COV

	::vector_s List::process(Record& r) {
		// report files as seen from cwd
		::string const& repo_root_s = Record::s_autodep_env().repo_root_s ;
		::optional_s    abs_dir_s   ;
		if (+dir) {
			Record::Solve<false/*Send*/> sr { r , ::copy(*dir) , true/*no_follow*/ , false/*read*/ , false/*create*/ , Comment::List } ;
			abs_dir_s = mk_glb_s( with_slash(sr.real) , repo_root_s ) ;
		}
		//
		::vector_s          res       ;
		::string            abs_cwd_s = cwd_s() ;
		::optional_s        lcl_cwd_s ;           if ( abs_cwd_s.starts_with(repo_root_s) ) lcl_cwd_s = mk_lcl_s(abs_cwd_s,repo_root_s) ;
		::optional<RegExpr> re        ;           if ( +regexpr                           ) re        = *regexpr                        ;
		//
		for( ::string& f : r.report_sync({ .proc=JobExecProc::List , .sync=Yes , .comment=Comment::List , .digest{.write=write} , .date=New }).files ) {
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

	::string List::descr() const {
		::string res = "list" ;
		switch (write) {
			case No    : res << " deps"                  ; break ;
			case Yes   : res << " targets"               ; break ;
			case Maybe : res << " both deps and targets" ; break ;
		DF}
		if (+dir    ) res <<" in "                << *dir     ;
		if (+regexpr) res <<" satisfying regexpr "<< *regexpr ;
		return res ;
	}

	//
	// ListRootS
	//

	::string& operator+=( ::string& os , ListRootS const& lr_s ) { // START_OF_NO_COV
		return os << "ListRootS(" << lr_s.dir <<')' ;
	}                                                              // END_OF_NO_COV

	::string ListRootS::process(Record& r) {
		// report dir as used as prefix when listing dir
		::string const&              repo_root_s = Record::s_autodep_env().repo_root_s                                                     ;
		Record::Solve<false/*Send*/> sr          { r , ::move(dir) , true/*no_follow*/ , false/*read*/ , false/*create*/ , Comment::List } ;
		::string                     dir_s       = with_slash(sr.real)                                                                     ;
		::string                     abs_dir_s   = mk_glb_s( dir_s , repo_root_s )                                                         ;
		::string                     abs_cwd_s   = cwd_s()                                                                                 ;
		//
		r.send_report() ;
		if ( abs_cwd_s.starts_with(repo_root_s) && !is_abs_s(dir_s) ) return mk_rel_s( dir_s , mk_lcl_s(abs_cwd_s,repo_root_s) ) ;
		else                                                          return abs_dir_s                                           ;
	}

}
