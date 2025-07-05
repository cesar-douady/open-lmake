// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <dirent.h>
#include <fcntl.h>

#include "types.hh"
#include "fd.hh"
#include "lib.hh"
#include "time.hh"

#ifndef O_NOATIME
	#define O_NOATIME 0 // this is just for perf, if not possible, no big deal
#endif

enum class Access : uint8_t {                                          // in all cases, dirs are deemed non-existing
	Lnk                                                                // file is accessed with readlink              , regular files are deemed non-existing
,	Reg                                                                // file is accessed with open                  , symlinks      are deemed non-existing
,	Stat                                                               // file is accessed with stat like (read inode), only distinguish tag
} ;
static constexpr ::amap<Access,char,N<Access>> AccessChars = {{
	{ Access::Lnk  , 'L' }
,	{ Access::Reg  , 'R' }
,	{ Access::Stat , 'T' }
}} ;
static_assert(chk_enum_tab(AccessChars)) ;
using Accesses = BitMap<Access> ;                                      // distinguish files as soon as they can be distinguished by one of the liste Access'es
static constexpr Accesses DataAccesses { Access::Lnk , Access::Reg } ;

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

// START_OF_VERSIONING
// PER_FILE_SYNC : add entry here
enum class FileSync : uint8_t { // method used to ensure real close-to-open file synchronization (including file creation)
	None
,	Dir                         // close file directory after write, open it before read (in practice, open/close upon both events)
,	Sync                        // sync file after write
// aliases
,	Dflt = Dir
} ;
// END_OF_VERSIONING

namespace Disk {
	using Ddate       = Time::Ddate              ;
	using DiskSz      = uint64_t                 ;
	using FileNameIdx = Uint<n_bits(PATH_MAX+1)> ; // file names are limited to PATH_MAX

	static constexpr DiskSz DiskBufSz = 1<<16 ; // buffer size to use when reading or writing disk

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
		size_t sep = path.rfind('/',path.size()-2) ;
		if (sep!=Npos) return path.substr(sep+1) ;
		throw_unless( +path     , "no base for empty path" ) ;
		throw_unless( path!="/" , "no base for /"          ) ;
		return path ;
	}
	inline bool is_dir_name( ::string const& path                         ) { return !path || path.back()=='/'                                                         ; }
	inline bool is_in_dir  ( ::string const& path , ::string const& dir_s ) { return path.starts_with(dir_s) || (path.size()+1==dir_s.size()&&dir_s.starts_with(path)) ; }

	inline bool is_abs_s(::string const& name_s) { return          name_s[0]=='/' ; } // name_s is (<x>/)*    or /(<x>/)* with <x>=[^/]+, empty name_s is necessarily relative
	inline bool is_abs  (::string const& name  ) { return !name || name  [0]=='/' ; } // name   is <x>(/<x>)* or (/<x>)*  with <x>=[^/]+, empty name   is necessarily absolute
	//
	inline bool   is_lcl_s    (::string const& name_s) { return !( is_abs_s(name_s) || name_s.starts_with("../")               ) ;                               }
	inline bool   is_lcl      (::string const& name  ) { return !( is_abs  (name  ) || name  .starts_with("../") || name==".." ) ;                               }
	inline size_t uphill_lvl_s(::string const& name_s) { SWEAR(!is_abs_s(name_s)) ; size_t l ; for( l=0 ; substr_view(name_s,3*l,3)=="../" ; l++ ) {} return l ; }
	inline size_t uphill_lvl  (::string const& name  ) { return uphill_lvl_s(name+'/') ;                                                                         }

	/**/   ::string mk_lcl( ::string const& file , ::string const& dir_s ) ; // return file (passed as from dir_s origin) as seen from dir_s
	/**/   ::string mk_glb( ::string const& file , ::string const& dir_s ) ; // return file (passed as from dir_s       ) as seen from dir_s origin
	inline ::string mk_abs( ::string const& file , ::string const& dir_s ) { // return file (passed as from dir_s       ) as absolute
		SWEAR( is_abs_s(dir_s) , dir_s ) ;
		return mk_glb(file,dir_s) ;
	}
	inline ::string mk_rel( ::string const& file , ::string const& dir_s ) {
		if (is_abs(file)==is_abs_s(dir_s)) return mk_lcl(file,dir_s) ;
		else                               return file               ;
	}

	// manage localization to user startup dir
	// the principle is to add a marker when file is generated, then this marker is recognized and file is localized when display
	// file format is : FileMrkr + FileDisplay + file length + file
	static constexpr char FileMrkr = 0 ;

	/**/   ::string mk_file( ::string const& f , FileDisplay fd=FileDisplay::Printable , Bool3 exists=Maybe ) ;
	inline ::string mk_file( ::string const& f ,                                         Bool3 exists       ) { return mk_file(f,FileDisplay::None,exists) ; }

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

	struct FileInfo {
		friend ::string& operator+=( ::string& , FileInfo const& ) ;
	private :
		// statics
		static FileTag _s_tag(FileStat const& st) ;
		// cxtors & casts
	public :
		FileInfo( FileTag tag=FileTag::Unknown                       ) : date{tag}                        {}
		FileInfo( Fd at                                              ) : FileInfo{at     ,{}            } {}
		FileInfo(         ::string const& name , bool no_follow=true ) : FileInfo{Fd::Cwd,name,no_follow} {}
		FileInfo( Fd at , ::string const& name , bool no_follow=true ) ;
		FileInfo( FileStat const&                                    ) ;
		// accesses
		bool    operator==(FileInfo const&) const = default ;
		bool    operator+ (               ) const { return tag()!=FileTag::Unknown ; }
		bool    exists    (               ) const { return tag()>=FileTag::Target  ; }
		FileTag tag       (               ) const { return date.tag()              ; }
		FileSig sig       (               ) const ;
		// data
		DiskSz sz   = 0 ;
		Ddate  date ;
	} ;

	struct FileSig {
		friend ::string& operator+=( ::string& , FileSig const& ) ;
		// cxtors & casts
	public :
		FileSig(                                                    ) = default ;
		FileSig( Fd at                                              ) : FileSig{at     ,{}                 } {}
		FileSig(         ::string const& name , bool no_follow=true ) : FileSig{Fd::Cwd,name,no_follow     } {}
		FileSig( Fd at , ::string const& name , bool no_follow=true ) : FileSig{FileInfo(at,name,no_follow)} {}
		FileSig( FileInfo const&                                    ) ;
		FileSig( FileTag tag                                        ) : _val{+tag}                           {}
		// accesses
	public :
		bool    operator==(FileSig const& fs) const {
			if( !self && !fs ) return true          ;  // consider Dir and None as identical
			else                return _val==fs._val ;
		}
		//
		bool    operator+() const { return tag()>=FileTag::Target                ; }
		FileTag tag      () const { return FileTag(_val&lsb_msk(NBits<FileTag>)) ; }
		// data
	private :
		uint64_t _val = 0 ;                            // by default, no file
	} ;

	inline FileSig FileInfo::sig() const { return FileSig(self) ; }

	struct SigDate {
		friend ::string& operator+=( ::string& , SigDate const& ) ;
		using Pdate = Time::Pdate ;
		// cxtors & casts
		SigDate(                     ) = default ;
		SigDate( NewType             ) :          date{New} {}
		SigDate( FileSig s           ) : sig{s} , date{New} {}
		SigDate(             Pdate d ) :          date{d  } {}
		SigDate( FileSig s , Pdate d ) : sig{s} , date{d  } {}
		// accesses
		bool operator==(SigDate const&) const = default ;
		bool operator+ (              ) const { return +date || +sig ; }
		// data
		FileSig sig  ;
		Pdate   date ;
	} ;

	struct NfsGuardNone {
		::string const& access    (::string const& file ) { return file  ; }
		::string const& access_dir(::string const& dir_s) { return dir_s ; }
		::string const& change    (::string const& file ) { return file  ; }
	} ;
	struct NfsGuardDir {                                                 // open/close uphill dirs before read accesses and after write accesses
		// statics
	private :
		void _s_protect(::string const& dir_s) {
			::close(::open(no_slash(dir_s).c_str(),O_DIRECTORY|O_NOATIME)) ;
		}
		// cxtors & casts
	public :
		~NfsGuardDir() { close() ; }
		//services
		::string const& access(::string const& file) {
			if (has_dir(file)) access_dir(dir_name_s(file)) ;
			return file ;
		}
		::string const& change(::string const& file) {
			if (has_dir(file)) {
				::string dir_s = dir_name_s(file) ;
				access_dir(dir_s) ;
				to_stamp_dirs_s.insert(::move(dir_s)) ;
			}
			return file ;
		}
		void close() {
			for( ::string const& d_s : to_stamp_dirs_s ) _s_protect(d_s) ;
			to_stamp_dirs_s.clear() ;
		}
		::string const& access_dir(::string const& dir_s) {
			access(no_slash(dir_s)) ;                                    // we opend dir, we must ensure its dir is up-to-date w.r.t. NFS
			if (fetched_dirs_s.insert(dir_s).second) _s_protect(dir_s) ; // open to force NFS close to open coherence, close is useless
			return dir_s ;
		}
		// data
		::uset_s fetched_dirs_s  ;
		::uset_s to_stamp_dirs_s ;
	} ;
	struct NfsGuardSync {                                                // fsync file after they are written
		// cxtors & casts
		~NfsGuardSync() { close() ; }
		//services
		::string const& access    (::string const& file ) {                         return file  ; }
		::string const& access_dir(::string const& dir_s) {                         return dir_s ; }
		::string const& change    (::string const& file ) { to_stamp.insert(file) ; return file  ; }
		void close() {
			for( ::string const& f : to_stamp ) if ( AcFd fd{f} ; +fd ) ::fsync(fd) ;
			to_stamp.clear() ;
		}
		// data
		::uset_s to_stamp ;
	} ;
	struct NfsGuard : ::variant< NfsGuardNone , NfsGuardDir , NfsGuardSync > {
		// cxtors & casts
		NfsGuard(FileSync fs) {
			switch (fs) {                                                // PER_FILE_SYNC : add entry here
				case FileSync::None : emplace<0>() ; break ;
				case FileSync::Dir  : emplace<1>() ; break ;
				case FileSync::Sync : emplace<2>() ; break ;
			DF}
		}
		// services
		::string const& access(::string const& file) {                   // return file, must be called before any access to file or its inode if not sure it was produced locally
			switch (index()) {                                           // PER_FILE_SYNC : add entry here
				case 0 : return ::get<0>(self).access(file) ;
				case 1 : return ::get<1>(self).access(file) ;
				case 2 : return ::get<2>(self).access(file) ;
			DF}
		}
		::string const& access_dir(::string const& dir_s) {              // return file, must be called before any access to file or its inode if not sure it was produced locally
			switch (index()) {                                           // PER_FILE_SYNC : add entry here
				case 0 : return ::get<0>(self).access_dir(dir_s) ;
				case 1 : return ::get<1>(self).access_dir(dir_s) ;
				case 2 : return ::get<2>(self).access_dir(dir_s) ;
			DF}
		}
		::string const& change(::string const& file) {                   // return file, must be called before any write access to file or its inode if not sure it is going to be read only locally
			switch (index()) {                                           // PER_FILE_SYNC : add entry here
				case 0 : return ::get<0>(self).change(file) ;
				case 1 : return ::get<1>(self).change(file) ;
				case 2 : return ::get<2>(self).change(file) ;
			DF}
		}
		::string const& rename(::string const& file) {
			access(file) ;
			change(file) ;
			return file ;
		}
	} ;

	// list files within dir with prefix in front of each entry
	::vector_s lst_dir_s( Fd at , ::string const& dir_s={} , ::string const& prefix={} ) ;
	//
	size_t/*pos*/ mk_dir_s ( Fd at , ::string const& dir_s ,             bool unlnk_ok=false ) ; // if unlnk_ok <=> unlink any file on the path if necessary to make dir
	size_t/*pos*/ mk_dir_s ( Fd at , ::string const& dir_s , NfsGuard& , bool unlnk_ok=false ) ; // .
	void          dir_guard( Fd at , ::string const& file                                    ) ;
	//
	void          unlnk_inside_s( Fd at , ::string const& dir_s                     , bool abs_ok=false , bool force=false , bool ignore_errs=false ) ;
	bool/*done*/  unlnk         ( Fd at , ::string const& file  , bool dir_ok=false , bool abs_ok=false , bool force=false , bool ignore_errs=false ) ; // if dir_ok <=> unlink whole dir if it is one
	void          rmdir_s       ( Fd at , ::string const& dir_s                                                                                     ) ;
	void          mk_dir_empty_s( Fd at , ::string const& dir_s                     , bool abs_ok=false                                             ) ;
	//
	inline void lnk( Fd at , ::string const& file , ::string const& target ) {
		if (::symlinkat(target.c_str(),at,file.c_str())!=0) {
			::string at_str = at==Fd::Cwd ? ""s : cat('<',at.fd,">/") ;
			throw cat("cannot create symlink from ",at_str,file," to ",target) ;
		}
	}

	inline ::string read_lnk( Fd at , ::string const& file ) {
		char    buf[PATH_MAX] ;
		ssize_t cnt           = ::readlinkat(at,file.c_str(),buf,PATH_MAX) ;
		if ( cnt<0 || cnt>=PATH_MAX ) return {} ;
		return {buf,size_t(cnt)} ;
	}

	inline bool  is_dir_s ( Fd at , ::string const& dir_s={} , bool no_follow=true ) { return FileInfo(at,no_slash(dir_s),no_follow).tag()==FileTag::Dir ; }
	inline bool  is_target( Fd at , ::string const& file ={} , bool no_follow=true ) { return FileInfo(at,file           ,no_follow).exists()            ; }
	inline bool  is_exe   ( Fd at , ::string const& file ={} , bool no_follow=true ) { return FileInfo(at,file           ,no_follow).tag()==FileTag::Exe ; }
	inline Ddate file_date( Fd at , ::string const& file ={} , bool no_follow=true ) { return FileInfo(at,file           ,no_follow).date                ; }

	inline ::vector_s        lst_dir_s( ::string const& dir_s ,                            ::string const& pfx={} ) { return lst_dir_s(Fd::Cwd,dir_s,pfx        ) ;               }
	inline size_t/*pos*/     mk_dir_s ( ::string const& dir_s ,                bool unlnk_ok=false                ) { return mk_dir_s (Fd::Cwd,dir_s,   unlnk_ok) ;               }
	inline size_t/*pos*/     mk_dir_s ( ::string const& dir_s , NfsGuard& ng , bool unlnk_ok=false                ) { return mk_dir_s (Fd::Cwd,dir_s,ng,unlnk_ok) ;               }
	inline ::string const&   dir_guard( ::string const& path                                                      ) {        dir_guard(Fd::Cwd,path             ) ; return path ; }
	inline void              rmdir_s  ( ::string const& dir_s                                                     ) {        rmdir_s  (Fd::Cwd,dir_s            ) ;               }
	inline void              lnk      ( ::string const& file  , ::string const& target                            ) {        lnk      (Fd::Cwd,file ,target     ) ;               }
	inline ::string          read_lnk ( ::string const& file                                                      ) { return read_lnk (Fd::Cwd,file             ) ;               }
	inline bool              is_dir_s ( ::string const& dir_s , bool no_follow=true                               ) { return is_dir_s (Fd::Cwd,dir_s,no_follow  ) ;               }
	inline bool              is_target( ::string const& file  , bool no_follow=true                               ) { return is_target(Fd::Cwd,file ,no_follow  ) ;               }
	inline bool              is_exe   ( ::string const& file  , bool no_follow=true                               ) { return is_exe   (Fd::Cwd,file ,no_follow  ) ;               }
	inline Ddate             file_date( ::string const& file  , bool no_follow=true                               ) { return file_date(Fd::Cwd,file ,no_follow  ) ;               }

	// deep list files whose tag matches FileTags within dir with pfx in front of each entry, return a single entry {pfx} if file is not a dir (including if file does not exist)
	::vmap_s<FileTag> walk(
		Fd                                at
	,	::string const&                   path
	,	FileTags                          fts   = ~FileTags()
	,	::string const&                   pfx   = {}
	,	::function<bool(::string const&)> prune = [](::string const&)->bool { return false ; }
	) ;
	inline ::vmap_s<FileTag> walk(
		::string const&                   path
	,	FileTags                          fts   = ~FileTags()
	,	::string const&                   pfx   = {}
	,	::function<bool(::string const&)> prune = [](::string const&)->bool { return false ; }
	) {
		return walk( Fd::Cwd , path , fts , pfx , prune )  ;
	}

	inline void unlnk_inside_s( Fd at , bool force=false , bool ignore_errs=false ) {
		unlnk_inside_s( at , {} , false , force , ignore_errs ) ;
	}
	inline void unlnk_inside_s( ::string const& dir_s , bool abs_ok=false , bool force=false , bool ignore_errs=false ) {
		unlnk_inside_s( Fd::Cwd , dir_s , abs_ok , force , ignore_errs ) ;
	}
	inline bool/*done*/ unlnk( ::string const& file  , bool dir_ok=false , bool abs_ok=false , bool force=false , bool ignore_errs=false ) {
		return unlnk( Fd::Cwd , file , dir_ok , abs_ok , force , ignore_errs ) ;
	}
	inline void mk_dir_empty_s( ::string const& dir_s , bool abs_ok=false ) {
		return mk_dir_empty_s( Fd::Cwd , dir_s , abs_ok ) ;
	}

	inline ::string cwd_s() {
		char cwd[PATH_MAX] ;                                   // use posix, not linux extension that allows to pass nullptr as argument and malloc's the returned string
		if (!::getcwd(cwd,PATH_MAX)) throw "cannot get cwd"s ;
		return with_slash(cwd) ;                               // cwd is "/" not empty when at root dir, so dont simply append '/'
	}

	/**/   FileTag cpy( Fd dst_at , ::string const& dst_file , Fd src_at , ::string const& src_file , bool unlnk_dst=false , bool mk_read_only=false ) ;
	inline FileTag cpy(             ::string const& df       , Fd sat    , ::string const& sf       , bool ud       =false , bool ro          =false ) { return cpy(Fd::Cwd,df,sat    ,sf,ud,ro) ; }
	inline FileTag cpy( Fd dat    , ::string const& df       ,             ::string const& sf       , bool ud       =false , bool ro          =false ) { return cpy(dat    ,df,Fd::Cwd,sf,ud,ro) ; }
	inline FileTag cpy(             ::string const& df       ,             ::string const& sf       , bool ud       =false , bool ro          =false ) { return cpy(Fd::Cwd,df,Fd::Cwd,sf,ud,ro) ; }

	struct FileMap {
		// cxtors & casts
		FileMap(                        ) = default ;
		FileMap( Fd , ::string const&   ) ;
		FileMap(      ::string const& f ) : FileMap{Fd::Cwd,f} {}
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
		::string at_file(Fd at) {
			if      (at==Fd::Cwd) return cwd()                                    ;
			else if (pid        ) return read_lnk(cat("/proc/",pid,"/fd/",at.fd)) ;
			else                  return read_lnk(cat("/proc/self/fd/"   ,at.fd)) ;
		}
		FileLoc file_loc(::string const& real) const { return _env->file_loc(real) ; }
		//
		SolveReport solve( Fd at , ::string_view        , bool no_follow=false ) ;
		SolveReport solve( Fd at , ::string const& file , bool no_follow=false ) { return solve( at      , ::string_view(file) , no_follow ) ; }
		SolveReport solve( Fd at , const char*     file , bool no_follow=false ) { return solve( at      , ::string_view(file) , no_follow ) ; }
		SolveReport solve(         ::string_view   file , bool no_follow=false ) { return solve( Fd::Cwd ,               file  , no_follow ) ; }
		SolveReport solve(         ::string const& file , bool no_follow=false ) { return solve( Fd::Cwd ,               file  , no_follow ) ; }
		SolveReport solve(         const char*     file , bool no_follow=false ) { return solve( Fd::Cwd ,               file  , no_follow ) ; }
		SolveReport solve( Fd at ,                        bool no_follow=false ) { return solve( at      , ::string()          , no_follow ) ; }
		//
		vmap_s<Accesses> exec(SolveReport&) ;                             // arg is updated to reflect last interpreter
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
