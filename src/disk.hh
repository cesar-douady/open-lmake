// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <dirent.h>
#include <sys/xattr.h>

#include "fd.hh"
#include "serialize.hh"
#include "time.hh"

#if HAS_ACL                            // test must be performed after a local .hh file has been included to have HAS_ACL defined
	#include <linux/posix_acl.h>
	#include <linux/posix_acl_xattr.h>
#else                                  // default to internal copies if official files are not available
	#include "ext/linux/posix_acl.h"
	#include "ext/linux/posix_acl_xattr.h"
#endif

// Special files and dirs
// can be tailored to fit neeeds
#define ADMIN_DIR_S            "LMAKE/"
#define PRIVATE_ADMIN_SUBDIR_S "lmake/"

// must not be touched to fit needs
#define PRIVATE_ADMIN_DIR_S ADMIN_DIR_S PRIVATE_ADMIN_SUBDIR_S
static constexpr char AdminDirS       [] = ADMIN_DIR_S         ;
static constexpr char PrivateAdminDirS[] = PRIVATE_ADMIN_DIR_S ;

// START_OF_VERSIONING CACHE JOB REPO
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
,	{ Access::Err  , 'E' }
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

namespace Disk {
	using DiskSz      = uint64_t                 ;
	using FileNameIdx = Uint<n_bits(PATH_MAX+1)> ; // filenames are limited to PATH_MAX

	static constexpr DiskSz DiskBufSz = 1<<17 ; // buffer size to use when reading or writing disk, mimic cp on ubuntu24.04

	//
	// path name library
	//

	bool     is_canon( ::string const& , bool ext_ok=true , bool empty_ok=false , bool has_pfx=false , bool has_sfx=false ) ; // is has_pfx or has_sfx, return false if cannot be canon for any pfx/sfx
	::string mk_canon( ::string const&                                                                                    ) ;
	//
	inline bool has_dir(::string const& file) {
		if (file.size()<3) return false                          ;                                                            // we must have at least 2 components and a / to have a dir component
		else               return file.find('/',1)<file.size()-2 ;                                                            // search a / at neither ends of file
	}
	//
	inline bool is_dir_name(::string const& file) { return !file || file.back()=='/' ; }
	//
	template<bool DoBase> ::string _dir_base_name( ::string const& file , FileNameIdx n=1 ) {
		bool is_dir = is_dir_name(file) ;
		if (!n) {
			if (!is_dir) goto Bad    ;
			if (DoBase ) return {}   ;
			else         return file ;
		}
		if (!file) goto Bad ;
		{	size_t slash = file.size()-is_dir ;
			for(; n ; n-- ) {
				if (slash==Npos)                  goto Bad ;
				if (slash==0   ) { slash = Npos ; continue ; }
				size_t prev_slash = slash ;
				slash = file.rfind('/',slash-1) ;
				if (slash==Npos) { if (substr_view(file,0    ,prev_slash      )==".." ) goto Bad ; }
				else             { if (substr_view(file,slash,prev_slash-slash)=="/..") goto Bad ; }
			}
			if (DoBase) return slash==Npos ? file : file.substr(slash+1        ) ;
			else        return slash==Npos ? ""s  : file.substr(0      ,slash+1) ;
		}
	Bad :
		if (n==1) throw cat("cannot walk uphill from "           ,file) ;
		else      throw cat("cannot walk uphill ",n," dirs from ",file) ;
	}
	inline ::string dir_name_s( ::string const& file , FileNameIdx n=1 ) { return _dir_base_name<false>(file,n) ; }           // INVARIANT : dir_name_s(file,n)+base_name(file,n)==file
	inline ::string base_name ( ::string const& file , FileNameIdx n=1 ) { return _dir_base_name<true >(file,n) ; }           // .

	inline bool is_abs(::string const& file) { return          file[0]=='/' ; }
	inline bool is_abs(::string_view   file) { return +file && file[0]=='/' ; } // string_view's have no guaranteed terminating null
	//
	inline bool is_lcl(::string const& file ) {
		return !( is_abs(file) || file.starts_with("../") || file==".." ) ;
	}
	inline size_t uphill_lvl(::string const& dir) {
		for( size_t l=0 ;; l++ ) {
			::string_view sv = substr_view( dir , 3*l ) ;
			if (sv.starts_with("../")) continue ;
			if (sv==".."             ) l++ ;
			return l ;
		}
	}

	::string mk_lcl( ::string const& path , ::string const& dir_s ) ; // return file (passed as from dir_s origin) as seen from dir_s
	::string mk_glb( ::string const& path , ::string const& dir_s ) ; // return file (passed as from dir_s       ) as seen from dir_s origin
	//
	inline ::string mk_rel( ::string const& file  , ::string const& ref_dir_s ) {
		if (is_abs(file)==is_abs(ref_dir_s)) return mk_lcl(file,ref_dir_s) ;
		else                                 return        file            ;
	}

	bool lies_within( ::string const& file , ::string const& dir ) ; // assumes canonic args

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
			if( !self && !fs ) return true          ; // consider Dir and None as identical
			else               return _val==fs._val ;
		}
		//
		bool    operator+() const { return tag()>=FileTag::Target                ; }
		FileTag tag      () const { return FileTag(_val&lsb_msk(NBits<FileTag>)) ; }
		bool    exists   () const { return tag()>=FileTag::Target                ; }
		// data
	private :
		uint64_t _val = 0 ;                           // by default, no file
	} ;

	inline FileSig FileInfo::sig() const { return FileSig(self) ; }

	struct SigDate {
		friend ::string& operator+=( ::string& , SigDate const& ) ;
		// cxtors & casts
		SigDate() = default ;
		SigDate( FileSig s , Time::Pdate d=New ) : sig{s} , date{d} {}
		// accesses
		bool operator==(SigDate const&) const = default ;
		bool operator+ (              ) const { return +date || +sig ; }
		// data
		FileSig     sig  ;
		Time::Pdate date ;
	} ;

	::vector_s      lst_dir_s( FileRef dir_s=Fd::Cwd , ::string const& pfx={} , NfsGuard*   =nullptr ) ; // list files within dir with pfx in front of each entry
	size_t/*pos*/   mk_dir_s ( FileRef dir_s         ,                          _CreatAction={}      ) ;
	::string const& dir_guard( FileRef file          ,                          _CreatAction={}      ) ;

	struct _UnlnkAction {
		bool      abs_ok    = false   ; // unless abs_ok, absolute paths are not accepted to avoid catastrophic unlinks when dir_ok
		bool      dir_ok    = false   ; // if dir_ok <=> unlink whole dir if it is one
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
	// prune is called on each directory which is not traversed if return value is true
	::vmap_s<FileTag> walk(
		FileRef                                     = Fd::Cwd
	,	FileTags                                    = ~FileTags()
	,	::string const&                       pfx   = {}
	,	::function<bool(::string const& dir)> prune = [](::string const&)->bool { return false ; }
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

	//
	// misc
	//

	using AclEntry = struct ::posix_acl_xattr_entry ;

	::vector<AclEntry> acl_entries( FileRef         dir_s                       ) ;
	mode_t             auto_umask ( ::string const& dir_s , ::string const& msg ) ;

	struct VersionAction {
		Bool3    chk       = Yes ; // Maybe means it is ok to initialize
		::string key       = {}  ; // short id (e.g. repo)
		::string init_msg  = {}  ;
		::string clean_msg = {}  ;
		mode_t   umask     = -1  ; // right to apply if initializing
		uint64_t version   ;
	} ;

	void chk_version( ::string const& dir_s , VersionAction const& action ) ;

}
