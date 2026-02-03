// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "re.hh"

#include "backdoor.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Re   ;
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
			case No    : return cat("disable autodep "  ,reason) ;
			case Yes   : return cat("enable autodep "   ,reason) ;
			case Maybe : return cat("get autodep state ",reason) ;
		DF}                                                        // NO_COV
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
		if (files.size()==1) return cat(kind," regexpr " ,reason,' ',files[0]) ;
		else                 return cat(kind," regexprs ",reason,' ',files   ) ;
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
			abs_dir_s = mk_glb( with_slash(::move(sr.real)) , repo_root_s ) ;
		}
		//
		::vector_s          res       ;
		::string            abs_cwd_s = cwd_s() ;
		::optional_s        lcl_cwd_s ;           if ( abs_cwd_s.starts_with(repo_root_s) ) lcl_cwd_s = mk_lcl(abs_cwd_s,repo_root_s) ;
		::optional<RegExpr> re        ;           if ( +regexpr                           ) re        = *regexpr                      ;
		//
		for( ::string& f : r.report_sync({ .proc=JobExecProc::List , .sync=Yes , .comment=Comment::List , .digest{.write=write} , .date=New }).files ) {
			::string abs_f = mk_glb( f , repo_root_s ) ;
			//
			if ( +dir && !abs_f.starts_with(*abs_dir_s) ) continue ;
			//
			::string& user_f = abs_f ;                                          // reuse storage
			if ( +lcl_cwd_s && !is_abs(f) ) user_f = mk_lcl( f , *lcl_cwd_s ) ; // else keep abs_f as is
			//
			if ( +regexpr && !re->match(user_f) ) continue ;
			//
			res.push_back(::move(user_f)) ;
		}
		//
		return res ;
	}

	::string List::descr(::string const& reason) const {
		::string res = "list " ;
		switch (write) {
			case No    : res << "deps"                  ; break ;
			case Yes   : res << "targets"               ; break ;
			case Maybe : res << "both deps and targets" ; break ;
		DF}                                                       // NO_COV
		/**/          res <<' '<<reason                      ;
		if (+dir    ) res <<" in "                <<*dir     ;
		if (+regexpr) res <<" satisfying regexpr "<<*regexpr ;
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
		::string                     dir_s       = with_slash(::move(sr.real))                                                             ;
		::string                     abs_dir_s   = mk_glb( dir_s , repo_root_s )                                                           ;
		::string                     abs_cwd_s   = cwd_s()                                                                                 ;
		//
		r.send_report() ;
		if ( abs_cwd_s.starts_with(repo_root_s) && !is_abs(dir_s) ) return mk_lcl( dir_s , mk_lcl(abs_cwd_s,repo_root_s) ) ;
		else                                                        return abs_dir_s                                       ;
	}

	//
	// codec
	//

	struct RealDigest {
		::string real      = {}    ;
		PermExt  perm_ext  = {}    ;
		FileSync file_sync = {}    ;
		bool     is_dir    = false ;
	} ;

	static RealDigest _real( Record& r , ::string const& tab , Comment comment ) {
		throw_unless(            +tab  , "table cannot be empty"   ) ;
		throw_if    ( is_dir_name(tab) , "table cannot end with /" ) ;
		AutodepEnv const& autodep_env = Record::s_autodep_env()            ;
		RealDigest        res         { .file_sync=autodep_env.file_sync } ;
		if (tab.find('/')==Npos) {
			auto it = autodep_env.codecs.find(tab) ;
			if (it!=autodep_env.codecs.end()) {
				res.real      = it->second.tab        ;
				res.perm_ext  = it->second.perm_ext   ;
				res.file_sync = it->second.file_sync  ;
				res.is_dir    = is_dir_name(res.real) ;
				return res ;
			}
		}
		Record::Solve<false/*Send*/> sr { r , tab , false/*no_follow*/ , true/*read*/ , false/*create*/ , Comment::Encode } ;
		throw_unless( sr.file_loc<=FileLoc::Repo , "codec table file must be a local source file" ) ;
		if (+sr.accesses) r.report_access( { .comment=comment , .digest={.accesses=sr.accesses} , .files={{sr.real,{}}} } , true/*force*/ ) ;
		res.real = ::move(sr.real) ;
		return res ;
	}

	static bool/*retry*/ _retry_codec( Record& r , RealDigest real_digest , ::string const& node , Comment c ) { //!     retry
		if (real_digest.is_dir                                                                                  ) return false ; // no retry for external dir tables
		if (FileInfo({Record::s_repo_root_fd(),Codec::CodecFile::s_dir_s(real_digest.real)}).tag()==FileTag::Dir) return false ; // if dir exists, it means codec db was initialized
		JobExecRpcReq jerr {
			.proc         = JobExecProc::DepDirect
		,	.sync         = Yes
		,	.comment      = c
		,	.comment_exts = CommentExt::Direct
		,	.digest       {}                                                                                                     // access to node is reported separately
		,	.date         = New
		,	.files        { {node,{}} }
		} ;
		throw_unless( r.report_sync(::move(jerr)).ok==Yes , "no codec table" ) ;
		return true/*retry*/ ;
	}

	//
	// Decode
	//

	::string& operator+=( ::string& os , Decode const& d ) {                 // START_OF_NO_COV
		return os << "Decode(" << d.tab <<','<< d.ctx <<','<< d.code <<')' ;
	}                                                                        // END_OF_NO_COV

	// /!\ this function must stay in sync with Engine::_create in job_data.cc
	::string Decode::process(Record& r) {
		using namespace Codec ;
		RealDigest           rd        = _real( r , tab , Comment::Decode )       ;
		CodecFile            cf        { false/*Encode*/ , rd.real , ctx , code } ; cf.chk() ;
		::string             node      = cf.name()                                ;
		Fd                   rfd       = Record::s_repo_root_fd()                 ;
		NfsGuard             nfs_guard { rd.file_sync }                           ;
		AccessDigest         ad        { .accesses=Access::Lnk }                  ; ad.flags.extra_dflags |= ExtraDflag::NoHot ; // beware of default flags, ...
		FileInfo             fi        ;                                                                                         // ... we are guarded, so dep cannot be hot
		::optional<::string> res       ;
	Retry :
		try {
			fi  = { {rfd,node} }                                  ;
			res = AcFd({rfd,node},{.nfs_guard=&nfs_guard}).read() ;                                                              // if node exists, it contains the reply
		} catch (::string const&) {                                                                                              // if node does not exist, create a code
			if (_retry_codec(r,rd,node,Comment::Decode)) goto Retry/*BACKWARD*/ ;
		}
		//
		r.report_access( { .comment=Comment::Decode , .digest=ad , .files={{node,fi}} } , true/*force*/ ) ;                      // report access after possible update
		r.send_report() ;
		throw_unless( +res , "code not found" ) ;
		return *res ;
	}

	::string Decode::descr(::string const& reason) const {
		return cat("decode ",reason," code ",code," with context ",ctx," in table ",tab) ;
	}

	//
	// Encode
	//

	::string& operator+=( ::string& os , Encode const& e ) {                                         // START_OF_NO_COV
		return os << "Encode(" << e.tab <<','<< e.ctx <<','<< e.val.size() <<','<< e.min_len <<')' ;
	}                                                                                                // END_OF_NO_COV

	// /!\ this function must stay in sync with Engine::_create in job_data.cc
	::string Encode::process(Record& r) {
		using namespace Codec ;
		RealDigest   rd      = _real( r , tab , Comment::Encode ) ;
		CodecCrc     crc     { New  , val }                       ;
		::string     crc_str = crc.hex()                          ;
		CodecFile    cf      { rd.real , ctx , crc }              ; cf.chk() ;
		::string     node    = cf.name()                          ;
		AccessDigest ad      { .accesses=Access::Lnk }            ; ad.flags.extra_dflags |= ExtraDflag::NoHot ;        // beware of default flags, we are guarded, so dep cannot be hot
		FileInfo     fi      ;
		::string     res     ;
		::string     msg     ;
		CodecLock    lock    ;                                                                                          // for use with local to ensure server maintenance is not on-going
		try {
			NfsGuard nfs_guard { rd.file_sync }           ;
			Fd       rfd       = Record::s_repo_root_fd() ;
		Retry :
			fi  = { {rfd,node} }                      ;
			res = read_lnk( {rfd,node} , &nfs_guard ) ;
			if (+res) {
				throw_unless( res.ends_with(DecodeSfx) , "bad encode link" ) ;
				res.resize( res.size() - DecodeSfxSz )                       ;
			} else {
				if (_retry_codec(r,rd,node,Comment::Encode)) goto Retry/*BACKWARD*/ ;
				if ( !rd.is_dir && !lock ) {
					lock = {rfd,cf.file} ;
					lock.lock_shared( cat(host(),'-',::getpid()) ) ;                                                    // passed id is for debug only
					goto Retry ;
				}
				::string dir_s = with_slash(CodecFile::s_dir_s(rd.real)) ;
				creat_store( {rfd,dir_s} , crc_str , val , rd.perm_ext , &nfs_guard ) ;                                 // ensure data exist in store
				//
				CodecFile dcf       { false/*encode*/ , rd.real , ctx , crc_str.substr(0,min_len) } ;
				::string& code      = dcf.code()                                                    ;
				::string  ctx_dir_s = dir_name_s(node)                                              ;
				::string  rel_data  = mk_lcl( cat(dir_s,"store/",crc_str) , ctx_dir_s )             ;
				mk_dir_s( {rfd,ctx_dir_s} , {.perm_ext=rd.perm_ext} ) ;
				// find code
				for(; code.size()<crc_str.size() ; code.push_back(crc_str[code.size()]) ) {
					::string decode_node = dcf.name() ;
					try {
						sym_lnk( {rfd,decode_node} , rel_data       , {.nfs_guard=&nfs_guard,.perm_ext=rd.perm_ext} ) ;
						sym_lnk( {rfd,node       } , code+DecodeSfx , {.nfs_guard=&nfs_guard,.perm_ext=rd.perm_ext} ) ; // create the encode side
						//
						if (!rd.is_dir) {
							::string new_code = cat(dir_s,"new_codes/",CodecCrc(New,decode_node).hex()) ;
							sym_lnk( {rfd,new_code} , "../"+node , {.nfs_guard=&nfs_guard} ) ;                          // tell server
						}
						ad.flags.extra_dflags |= ExtraDflag::CreateEncode ;
						goto Found ;                                                                                    // if sym_lnk succeeds, we have created the code (atomicity works even on NFS)
					} catch (::string const& e) {
						::string tgt = read_lnk({rfd,decode_node}) ;
						if (tgt==rel_data) goto Found ;                                                     // if decode_node already exists with the correct content, it has been created concurrently
					}
				}
				throw "no available code"s ;
			Found :
				fi  = { {rfd,node} } ;
				res = ::move(code)   ;
			}
		} catch(::string const& e) {
			msg = e ;
		}
		r.report_access( { .comment=Comment::Encode , .digest=ad , .files={{node,fi}} } , true/*force*/ ) ; // report access after possible update
		r.send_report() ;                                                                                   // this includes deps gathered when solving file
		throw_unless( !msg , msg ) ;
		return res ;
	}

	::string Encode::descr(::string const& reason) const {
		return cat("encode ",reason," value of size ",val.size()," with checksum ",Codec::CodecCrc(New,val).hex()," with context ",ctx," in table ",tab) ;
	}

}
