// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <dirent.h>

#include "types.hh"
#include "fd.hh"
#include "lib.hh"
#include "serialize.hh"
#include "time.hh"

// START_OF_VERSIONING
enum class Access : uint8_t {                                                         // in all cases, dirs are deemed non-existing
	Lnk                                                                               // file is accessed with readlink              , regular files are deemed non-existing
,	Reg                                                                               // file is accessed with open                  , symlinks      are deemed non-existing
,	Stat                                                                              // file is sensed for existence only
,	Err                                                                               // dep is sensitive to status (ok/err)
//
// aliases
,	Data = Err                                                                        // <= Data means refer to file content
} ;
// END_OF_VERSIONING
static constexpr ::amap<Access,char,N<Access>> AccessChars = {{
	{ Access::Lnk  , 'L' }
,	{ Access::Reg  , 'R' }
,	{ Access::Stat , 'T' }
,	{ Access::Err  , 'K' }
}} ;
static_assert(chk_enum_tab(AccessChars)) ;
using Accesses = BitMap<Access> ;                                                     // distinguish files as soon as they can be distinguished by one of the liste Access'es
static constexpr Accesses DataAccesses { Access::Lnk , Access::Reg                } ;
static constexpr Accesses FullAccesses { Access::Lnk , Access::Reg , Access::Stat } ;

enum class FileDisplay : uint8_t {
	None
,	Printable
,	Shell
,	Py
} ;

enum class FileLoc : uint8_t {
	Repo
,	SrcDir       // file has been found in source dirs
,	RepoRoot     // file is repo root
,	Tmp
,	Proc         // file is in /proc
,	Admin
,	Ext          // all other cases
,	Unknown
//
// aliases
,	Dep = SrcDir // <=Dep means that file must be reported as a dep
} ;

namespace Disk {
	using DiskSz      = uint64_t                 ;
	using FileNameIdx = Uint<n_bits(PATH_MAX+1)> ; // file names are limited to PATH_MAX

	static constexpr DiskSz DiskBufSz = 1<<17 ; // buffer size to use when reading or writing disk, mimic cp on ubuntu24.04

	//
	// path name library
	//
	// file  : a name not      ending with /
	// dir_s : a name          ending with /
	// path  : a name possibly ending with /

	bool     is_canon( ::string const& , bool ext_ok=true , bool empty_ok=false , bool has_pfx=false , bool has_sfx=false ) ; // is has_pfx or has_sfx, return false if cannot be canon for any pfx/sfx
	::string mk_canon( ::string const&                                                                                    ) ;
	//
	inline bool has_dir(::string const& path) {
		if (path.size()<3) return false ;                                                                                     // we must have at least 2 components and a / to have a dir component
		return path.find('/',1)<path.size()-2 ;                                                                               // search a / at neither ends of path
	}
	inline ::string dir_name_s( ::string const& path , FileNameIdx n=1 ) {
		SWEAR( n>=1 , path ) ;
		throw_unless( +path     , "no dir for empty path" ) ;
		throw_unless( path!="/" , "no dir for /"          ) ;
		size_t sep = path.size()-(path.back()=='/') ;
		for(; n ; n-- ) {
			throw_unless( sep!=Npos , "cannot walk uphill ",n," dirs from ",path ) ;
			sep = path.rfind('/',sep-1) ;
		}
		if (sep==Npos) return {}                   ;
		else           return path.substr(0,sep+1) ;
	}
	inline ::string base_name(::string const& path) {
		size_t sep = Npos ; if (path.size()>=2) sep = path.rfind('/',path.size()-2) ;
		//
		if (sep!=Npos) return path.substr(sep+1) ;
		throw_unless( +path     , "no base for empty path" ) ;
		throw_unless( path!="/" , "no base for /"          ) ;
		return path ;
	}
	inline bool is_dir_name(::string const& path) { return !path || path.back()=='/' ; }

	inline bool is_abs_s(::string const& dir_s) { return dir_s[0]=='/' ; }
	inline bool is_abs  (::string const& file ) { return file [0]=='/' ; }
	//
	inline bool   is_lcl_s    (::string const& dir_s) { return !( is_abs_s(dir_s) || dir_s.starts_with("../")               ) ;                               }
	inline bool   is_lcl      (::string const& file ) { return !( is_abs  (file ) || file .starts_with("../") || file==".." ) ;                               }
	inline size_t uphill_lvl_s(::string const& dir_s) { SWEAR(!is_abs_s(dir_s)) ; size_t l ; for( l=0 ; substr_view(dir_s,3*l,3)=="../" ; l++ ) {} return l ; }

	::string _mk_lcl( ::string const& path , ::string const& dir_s , bool path_is_dir ) ; // return file (passed as from dir_s origin) as seen from dir_s
	::string _mk_glb( ::string const& path , ::string const& dir_s , bool path_is_dir ) ; // return file (passed as from dir_s       ) as seen from dir_s origin
	//                                                                                                                path_is_dir
	inline ::string mk_lcl_s( ::string const& dir_s , ::string const& ref_dir_s ) { return _mk_lcl( dir_s , ref_dir_s , true    ) ;                                                  }
	inline ::string mk_lcl  ( ::string const& file  , ::string const& ref_dir_s ) { return _mk_lcl( file  , ref_dir_s , false   ) ;                                                  }
	inline ::string mk_glb_s( ::string const& dir_s , ::string const& ref_dir_s ) { return _mk_glb( dir_s , ref_dir_s , true    ) ;                                                  }
	inline ::string mk_glb  ( ::string const& file  , ::string const& ref_dir_s ) { return _mk_glb( file  , ref_dir_s , false   ) ;                                                  }
	inline ::string mk_rel_s( ::string const& dir_s , ::string const& ref_dir_s ) { if (is_abs_s(dir_s)==is_abs_s(ref_dir_s)) return mk_lcl_s(dir_s,ref_dir_s) ; else return dir_s ; }
	inline ::string mk_rel  ( ::string const& file  , ::string const& ref_dir_s ) { if (is_abs  (file )==is_abs_s(ref_dir_s)) return mk_lcl  (file ,ref_dir_s) ; else return file  ; }

	bool lies_within( ::string const& file , ::string const& dir_s ) ; // assumes canonic args

	// manage localization to user startup dir
	// the principle is to add a marker when file is generated, then this marker is recognized and file is localized when display
	// file format is : FileMrkr + FileDisplay + file length + file
	static constexpr char FileMrkr = 0 ;

	/**/   ::string mk_file( ::string const& f , FileDisplay fd , Bool3 exists=Maybe ) ;
	inline ::string mk_file( ::string const& f ,                  Bool3 exists=Maybe ) { return mk_file(f,FileDisplay::Printable,exists) ; }

	::string _localize( ::string const& txt , ::string const& dir_s , size_t first_file ) ;  // not meant to be used directly
	inline ::string localize( ::string const& txt , ::string const& dir_s={} ) {
		if ( size_t pos = txt.find(FileMrkr) ; pos==Npos ) return           txt            ; // fast path : avoid calling _localize
		else                                               return _localize(txt,dir_s,pos) ;
	}
	inline ::string localize( ::string&& txt , ::string const& dir_s={} ) {
		if ( size_t pos = txt.find(FileMrkr) ; pos==Npos ) return ::move   (txt          ) ; // fast path : avoid copy and calling _localize
		else                                               return _localize(txt,dir_s,pos) ;
	}

	//
	// disk access library
	//

	using FileStat = struct ::stat ;

	struct FileSig ;

	struct _FileInfoAction {
		bool      no_follow = true    ;
		NfsGuard* nfs_guard = nullptr ;
	} ;
	struct FileInfo {
		friend ::string& operator+=( ::string& , FileInfo const& ) ;
		using Action = _FileInfoAction ;
		// cxtors & casts
		FileInfo( FileTag tag=FileTag::Unknown ) : date{tag} {}
		FileInfo( FileRef path , Action ={}    ) ;
		FileInfo( FileStat const&              ) ;
		// accesses
		bool    operator==(FileInfo const&) const = default ;
		bool    operator+ (               ) const { return tag()!=FileTag::Unknown ; }
		bool    exists    (               ) const { return tag()>=FileTag::Target  ; }
		FileTag tag       (               ) const { return date.tag()              ; }
		FileSig sig       (               ) const ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , sz,date ) ;
		}
		// data
		DiskSz      sz   = 0 ;
		Time::Ddate date ;
	} ;

	struct FileSig {
		friend ::string& operator+=( ::string& , FileSig const& ) ;
		using Action = _FileInfoAction ;
		// cxtors & casts
	public :
		FileSig() = default ;
		FileSig( FileRef path , Action action={} ) : FileSig{FileInfo(path,action)} {}
		FileSig( FileStat const& fs              ) : FileSig{FileInfo(fs         )} {}
		FileSig( FileInfo const&                 ) ;
		FileSig( FileTag tag                     ) : _val{+tag}                     {}
		// accesses
	public :
		bool operator==(FileSig const& fs) const {
			if( !self && !fs ) return true          ;          // consider Dir and None as identical
			else               return _val==fs._val ;
		}
		//
		bool    operator+() const { return tag()>=FileTag::Target                ; }
		FileTag tag      () const { return FileTag(_val&lsb_msk(NBits<FileTag>)) ; }
		bool    exists   () const { return tag()>=FileTag::Target                ; }
		// data
	private :
		uint64_t _val = 0 ;                                    // by default, no file
	} ;

	inline FileSig FileInfo::sig() const { return FileSig(self) ; }

	struct SigDate {
		friend ::string& operator+=( ::string& , SigDate const& ) ;
		// cxtors & casts
		SigDate() = default ;
		SigDate( FileSig s , Time::Pdate d=New ) : sig{s} , date{d  } {}
		// accesses
		bool operator==(SigDate const&) const = default ;
		bool operator+ (              ) const { return +date || +sig ; }
		// data
		FileSig     sig  ;
		Time::Pdate date ;
	} ;


	struct _CreatAction {
		bool      force     = false   ; // unlink any file on the path to dst
		PermExt   perm_ext  = {}      ;
		NfsGuard* nfs_guard = nullptr ;
	} ;

	::vector_s    lst_dir_s( FileRef dir_s , ::string const& pfx={} , NfsGuard* nfs_guard=nullptr ) ; // list files within dir with prefix in front of each entry
	size_t/*pos*/ mk_dir_s ( FileRef dir_s ,                          _CreatAction action={}      ) ;
	//
	inline ::string const& dir_guard( FileRef file , _CreatAction action={} ) {
		if (has_dir(file.file)) mk_dir_s( {file.at,dir_name_s(file.file)} , action ) ;
		return file.file ;
	}

	struct _UnlnkAction {
		bool      dir_ok    = false   ; // if dir_ok <=> unlink whole dir if it is one
		bool      abs_ok    = false   ;
		bool      force     = false   ;
		NfsGuard* nfs_guard = nullptr ;
	} ;

	void         unlnk_inside_s( FileRef dir_s ,                          _UnlnkAction={}      ) ;
	bool/*done*/ unlnk         ( FileRef file  ,                          _UnlnkAction={}      ) ;
	void         rmdir_s       ( FileRef dir_s ,                          NfsGuard*   =nullptr ) ;
	void         mk_dir_empty_s( FileRef dir_s ,                          _UnlnkAction={}      ) ;
	void         sym_lnk       ( FileRef file  , ::string const& target , _CreatAction={}      ) ;
	void         touch         ( FileRef path  , Time::Pdate            , NfsGuard*   =nullptr ) ;
	//
	inline void touch( FileRef path , NfsGuard* ng=nullptr ) { touch( path , New , ng ) ; }

	::string read_lnk( FileRef file , NfsGuard* =nullptr ) ;

	// deep list files whose tag matches FileTags within dir with pfx in front of each entry, return a single entry {pfx} if file is not a dir (including if file does not exist)
	::vmap_s<FileTag> walk(
		FileRef                           path
	,	FileTags                          fts   = ~FileTags()
	,	::string const&                   pfx   = {}
	,	::function<bool(::string const&)> prune = [](::string const&)->bool { return false ; }
	) ;

	inline ::string cwd_s() {
		char cwd[PATH_MAX] ;                                   // use posix, not linux extension that allows to pass nullptr as argument and malloc's the returned string
		if (!::getcwd(cwd,PATH_MAX)) throw "cannot get cwd"s ;
		return with_slash(cwd) ;                               // cwd is "/" not empty when at root dir, so dont simply append '/'
	}

	FileTag cpy   ( FileRef src_file , FileRef dst_file , _CreatAction={} ) ;
	void    rename( FileRef src_file , FileRef dst_file , _CreatAction={} ) ;

	struct FileMap {
		// cxtors & casts
		FileMap() = default ;
		FileMap(FileRef) ;
		bool operator+() const { return _ok ; }
		// accesses
		template<class T> T const& get(size_t ofs=0) const { throw_unless( ofs+sizeof(T)<=sz , "object @",ofs,"out of file of size ",sz ) ; return *::launder(reinterpret_cast<T const*>(data+ofs)) ; }
		template<class T> T      & get(size_t ofs=0)       { throw_unless( ofs+sizeof(T)<=sz , "object @",ofs,"out of file of size ",sz ) ; return *::launder(reinterpret_cast<T      *>(data+ofs)) ; }
		// data
		const uint8_t* data = nullptr ;
		size_t         sz   = 0       ;
	private :
		AcFd _fd ;
		bool _ok = false ;
	} ;

	struct RealPathEnv {
		friend ::string& operator+=( ::string& , RealPathEnv const& ) ;
		// services
		FileLoc file_loc(::string const& file) const ;
		void    chk     (bool for_cache=false) const ;
		// data
		LnkSupport lnk_support = LnkSupport::Full ; // by default, be pessimistic
		FileSync   file_sync   = FileSync::Dflt   ;
		::string   repo_root_s = {}               ;
		::string   tmp_dir_s   = {}               ;
		::vector_s src_dirs_s  = {}               ;
	} ;

	struct RealPath {
		friend ::string& operator+=( ::string& , RealPath const& ) ;
		struct SolveReport {
			friend ::string& operator+=( ::string& , SolveReport const& ) ;
			// data
			::string   real          = {}           ;                     // real path relative to root if in_repo or in a relative src_dir or absolute if in an absolute src_dir, else empty
			::vector_s lnks          = {}           ;                     // links followed to get to real
			Bool3      file_accessed = No           ;                     // if True, file was accessed as sym link, if Maybe file dir was accessed as sym link
			FileLoc    file_loc      = FileLoc::Ext ;                     // do not process awkard files
		} ;
	private :
		// helper class to help recognize when we are in repo or in tmp
		struct _Dvg {
			// cxtors & casts
			_Dvg( ::string_view domain_s , ::string const& chk ) { update(domain_s,chk) ; }
			// accesses
			bool operator+() const { return ok ; }
			// services
			void update( ::string_view domain_s , ::string const& chk ) ; // udpate after domain_s & chk have been lengthened or shortened, but not modified internally
			// data
			bool   ok  = false ;
			size_t dvg = 0     ;
		} ;
		// cxtors & casts
	public :
		// src_dirs_s may be either absolute or relative, but must be canonic
		// tmp_dir_s must be absolute and canonic
		RealPath ( RealPathEnv const& rpe , pid_t p=0 ) ;
		// services
		FileLoc file_loc(::string const& real) const { return _env->file_loc(real) ; }
		//
		SolveReport solve( FileView , bool no_follow=false ) ;
		//
		::vmap_s<Accesses> exec(SolveReport&&) ;                          // arg is updated to reflect last interpreter
		//
		void chdir() ;
		::string cwd() {
			if ( !pid && ::getpid()!=_cwd_pid ) chdir() ;                 // refresh _cwd if it was updated in the child part of a clone
			return _cwd ;
		}
	private :
		size_t _find_src_idx(::string const& real) const ;
		// data
	public :
		pid_t pid = 0 ;
	private :
		RealPathEnv const* _env            = nullptr ;
		::vector_s         _abs_src_dirs_s ;                              // this is an absolute version of src_dirs
		size_t             _repo_root_sz   ;
		::string           _cwd            ;
		pid_t              _cwd_pid        = 0       ;                    // pid for which _cwd is valid if pid==0
		NfsGuard           _nfs_guard      ;
	} ;
	::string& operator+=( ::string& , RealPath::SolveReport const& ) ;

}
