// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "re.hh"

#include "backdoor.hh"

using namespace Re   ;
using namespace Hash ;
using namespace Time ;

namespace Backdoor {

	::umap_s<Func> const& get_func_tab() {
		static ::umap_s<Func> s_tab = {
			{ Enable       ::Cmd , func<Enable       > }
		,	{ Regexpr      ::Cmd , func<Regexpr      > }
		,	{ Depend       ::Cmd , func<Depend       > }
		,	{ DependVerbose::Cmd , func<DependVerbose> }
		,	{ DependDirect ::Cmd , func<DependDirect > }
		,	{ Target       ::Cmd , func<Target       > }
		,	{ ChkDeps      ::Cmd , func<ChkDeps      > }
		,	{ List         ::Cmd , func<List         > }
		,	{ ListRootS    ::Cmd , func<ListRootS    > }
		,	{ Decode       ::Cmd , func<Decode       > }
		,	{ Encode       ::Cmd , func<Encode       > }
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

	::string Enable::descr(::string const& reason) const {
		switch (enable) {
			case No    : return cat("disable autodep"  ,reason) ;
			case Yes   : return cat("enable autodep"   ,reason) ;
			case Maybe : return cat("get autodep state",reason) ;
		DF}
	}

	//
	// Regexpr
	//

	::string& operator+=( ::string& os , Regexpr const& re ) {           // START_OF_NO_COV
		return os << "Regexpr(" << re.files << re.access_digest << ')' ;
	}                                                                    // END_OF_NO_COV

	::monostate Regexpr::process(Record& r) {
		::vmap_s<FileInfo> files_ ; for( ::string& f : files ) files_.emplace_back(::move(f),FileInfo()) ;
		r.report_sync( { .proc=JobExecProc::AccessPattern , .comment=+access_digest.write?Comment::Target:Comment::Depend , .digest=access_digest , .date=New , .files=::move(files_) } ) ;
		return {} ;
	}

	::string Regexpr::descr(::string const& reason) const {
		const char* kind = +access_digest.write ? "target" : "dep" ;
		if (files.size()==1) return cat(kind," regexpr" ,reason,' ',files[0]) ;
		else                 return cat(kind," regexprs",reason,' ',files   ) ;
	}

	//
	// AccessBase
	//

	::vmap_s<FileInfo> AccessBase::_mk_deps( Record& r , bool sync , ::vector<NodeIdx>* /*out*/ dep_idxs1 ) {
		::vmap_s<FileInfo> res ; res.reserve(files.size()) ;                                                   // typically all files are pertinent
		Accesses           as  ;
		if (dep_idxs1) dep_idxs1->reserve(files.size()) ;
		for( ::string& f : files ) {
			Record::Solve<false/*Send*/> sr { r , f , no_follow , bool(+access_digest.accesses) , false/*create*/ , Comment::Depend } ;
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

	::monostate Depend::process(Record& r) {
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
		JobExecRpcReq jerr {
			.proc         = JobExecProc::DepVerbose
		,	.sync         = Yes
		,	.comment      = Comment::Depend
		,	.comment_exts = CommentExt::Verbose
		,	.digest       = access_digest
		,	.date         = New
		,	.files        = _mk_deps( r , true/*sync*/ , &dep_idxs1 )
		} ;
		JobExecRpcReply reply ; if (+jerr.files) reply = r.report_sync(::move(jerr)) ;
		for( size_t i : iota(files.size()) )
			if (!dep_idxs1[i]) res.push_back({}                                         ) ; // 0 is reserved to mean no dep info
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
		,	.files        = _mk_deps( r , true/*sync*/ )
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

	::monostate Target::process(Record& r) {
		::vector<Record::Solve<false/*Send*/>> srs         ;         srs.reserve(files.size()) ;
		Accesses                               as          ;
		bool                                   has_overlay = false ;
		for( ::string& f : files ) {
			Record::Solve<false/*Send*/> sr { r , f , no_follow , bool(+access_digest.accesses) , true/*create*/ , Comment::Target } ;
			as          |= sr.accesses ;                  // seems pessimistic but sr.accesses does not actually depend on file, only on no_follow, read and write
			has_overlay |= +sr.real0   ;
			srs.push_back(::move(sr)) ;
		}
		access_digest.accesses |= as ;
		if (!has_overlay) {                               // fast path : only a single call to report_access (most common case)
			::vmap_s<FileInfo> targets ;
			for( Record::Solve<false/*Send*/>& sr : srs )
				if (sr.file_loc<=FileLoc::Repo)
					targets.emplace_back( ::move(sr.real) , FileInfo(sr.real) ) ;
			r.report_access( { .proc=JobExecProc::Access , .comment=Comment::Target , .digest=access_digest , .date=New , .files=::move(targets) } , true/*force*/ ) ;
		} else {
			Pdate now { New } ;                           // for perf and in trace, we will see all targets with same date, but after potential link accesses while solving
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

	::string List::descr(::string const& reason) const {
		::string res = "list" ;
		switch (write) {
			case No    : res << " deps"                  ; break ;
			case Yes   : res << " targets"               ; break ;
			case Maybe : res << " both deps and targets" ; break ;
		DF}
		/**/          res << reason                           ;
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

	//
	// Decode
	//

	::string& operator+=( ::string& os , Decode const& d ) {                  // START_OF_NO_COV
		return os << "Decode(" << d.file <<','<< d.ctx <<','<< d.code <<')' ;
	}                                                                         // END_OF_NO_COV

	static bool/*retry*/ _retry_codec( Record& r , Fd at , ::string const& file , ::string const& node ) {
		if( FileInfo(at,Codec::CodecFile::s_dir_s(file)).tag()==FileTag::Dir ) return false/*retry*/ ;     // if dir exists, it means codec db was initialized
		JobExecRpcReq jerr {
			.proc         = JobExecProc::DepDirect
		,	.sync         = Yes
		,	.comment      = Comment::Decode
		,	.comment_exts = CommentExt::Direct
		,	.digest       {}                                                                               // access to node is reported separately
		,	.date         = New
		,	.files        { {node,{}} }
		} ;
		r.report_sync(::move(jerr)) ;                                                                      // if we could not lock, codec db was not initialized, DepDirect will remedy to this
		return true/*retry*/ ;
	}

	::string Decode::process(Record& r) {
		NfsGuard                     nfs_guard { Record::s_autodep_env().file_sync                                                } ;
		Record::Solve<false/*Send*/> sr        { r , file , false/*no_follow*/ , true/*read*/ , false/*create*/ , Comment::Decode } ;
		::string                     node      = Codec::CodecFile( false/*Encode*/ , sr.real , ctx , code ).name()                  ;
		Fd                           rfd       = Record::s_repo_root_fd()                                                           ;
		::string                     res       ;
		//
		throw_unless( sr.file_loc<=FileLoc::Repo , "file ",file,"is outside repo" ) ;
		//
		if (+sr.accesses) r.report_access( { .comment=Comment::Decode , .digest={.accesses=sr.accesses} , .files={{sr.real,{}}} } , true/*force*/ ) ;
	Retry :
		try {
			res = AcFd(rfd,node,{.nfs_guard=&nfs_guard}).read() ;              // if node exists, it contains the reply
		} catch (::string const&) {                                            // if node does not exist, create a code
			throw_if( !_retry_codec(r,rfd,sr.real,node) , "code not found" ) ;
			goto Retry/*BACKWARD*/ ;
		}
		r.report_access( { .comment=Comment::Decode , .digest={.accesses=Access::Reg} , .files={{::move(node),{}}} } , true/*force*/ ) ;
		r.send_report() ;
		return res ;
	}

	::string Decode::descr(::string const& reason) const {
		return cat("decode ",reason," code ",code," with context ",ctx," in file ",file) ;
	}

	//
	// Encode
	//

	::string& operator+=( ::string& os , Encode const& e ) {                                          // START_OF_NO_COV
		return os << "Encode(" << e.file <<','<< e.ctx <<','<< e.val.size() <<','<< e.min_len <<')' ;
	}                                                                                                 // END_OF_NO_COV

	::string Encode::process(Record& r) {
		NfsGuard                     nfs_guard { Record::s_autodep_env().file_sync                                                } ;
		Record::Solve<false/*Send*/> sr        { r , file , false/*no_follow*/ , true/*read*/ , false/*create*/ , Comment::Encode } ;
		Crc                          crc       { New , val }                                                                        ;
		::string                     node      = Codec::CodecFile( sr.real , ctx , crc ).name()                                     ;
		Fd                           rfd       = Record::s_repo_root_fd()                                                           ;
		//
		throw_unless( sr.file_loc<=FileLoc::Repo , "file ",file,"is outside repo" ) ;
		//
		if (+sr.accesses) r.report_access( { .comment=Comment::Encode , .digest={.accesses=sr.accesses} , .files={{sr.real,{}}} } , true/*force*/ ) ;
		//
		::string    new_codes_file = Codec::CodecFile::s_new_codes_file(sr.real) ;
		::string    res            ;
		ExtraDflags edf            ;
		try {                                                                      // first try with share lock (light weight in case no update is necessary)
			res = AcFd(rfd,node,{.nfs_guard=&nfs_guard}).read() ;                  // if node exists, it contains the reply
		} catch (::string const&) {                                                // if node does not exist, create a code
			_retry_codec( r , rfd , sr.real , node ) ;                             // in all cases, retry with the lock
			::string         crc_str = crc.hex()                                 ;
			Codec::CodecLock lock    { rfd , sr.real , {.nfs_guard=&nfs_guard} } ; // must hold the lock as we probably need to create a code
			try {
				res = AcFd(rfd,node,{.nfs_guard=&nfs_guard}).read() ;              // repeat test with lock
			} catch (::string const&) {
				for( ::string code = crc_str.substr(0,min_len) ; code.size()<=crc_str.size() ; code.push_back(crc_str[code.size()]) ) { // look for shortest possible code
					::string decode_node = Codec::CodecFile( false/*encode*/ , sr.real , ctx , code ).name() ;
					if (FileInfo(rfd,decode_node,{.nfs_guard=&nfs_guard}).exists()) continue ;
					// must write to new_codes_file first to allow replay in case of creash
					::string tmp_sfx         = cat('.',host(),'.',::getpid(),".tmp") ;
					::string tmp_node        = node       +tmp_sfx                   ; // nodes must be always correct when they exist as there is no read lock
					::string tmp_decode_node = decode_node+tmp_sfx                   ; // .
					//
					AcFd( rfd , new_codes_file  , {.flags=O_WRONLY|O_CREAT|O_APPEND,.mod=0666,.nfs_guard=&nfs_guard} ).write( Codec::Entry(ctx,code,val).line(true/*with_nl*/) ) ;
					AcFd( rfd , tmp_node        , {.flags=O_WRONLY|O_CREAT|O_TRUNC ,.mod=0444,.nfs_guard=&nfs_guard} ).write( code                                             ) ;
					AcFd( rfd , tmp_decode_node , {.flags=O_WRONLY|O_CREAT|O_TRUNC ,.mod=0444,.nfs_guard=&nfs_guard} ).write( val                                              ) ;
					rename( rfd,tmp_node       /*src*/ , rfd,node       /*dst*/ , &nfs_guard ) ;
					rename( rfd,tmp_decode_node/*src*/ , rfd,decode_node/*dst*/ , &nfs_guard ) ;
					//
					res  = code                     ;
					edf |= ExtraDflag::CreateEncode ;
					break ;
				}
				nfs_guard.flush() ;                                                    // flush before lock is released
				throw_unless( edf[ExtraDflag::CreateEncode] , "no code available" ) ;
			}
		}
		r.report_access( { .comment=Comment::Encode , .digest={.accesses=Access::Reg,.flags{.extra_dflags=edf}} , .files={{::move(node),{}}} } , true/*force*/ ) ;
		r.send_report() ;
		return res ;
	}

	::string Encode::descr(::string const& reason) const {
		return cat("encode",reason," value of size ",val.size()," with checksum ",Crc(New,val).hex()," with context ",ctx," in file ",file) ;
	}

}
