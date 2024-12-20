// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h> // fstatat, fchmodat

#include "types.hh"
#include "fd.hh"
#include "lib.hh"
#include "time.hh"

#ifndef O_NOATIME
	#define O_NOATIME 0 // this is just for perf, if not possible, no big deal
#endif

// ENUM macro does not work inside namespace's

ENUM(Access                                                            // in all cases, dirs are deemed non-existing
,	Lnk                                                                // file is accessed with readlink              , regular files are deemed non-existing
,	Reg                                                                // file is accessed with open                  , symlinks      are deemed non-existing
,	Stat                                                               // file is accessed with stat like (read inode), only distinguish tag
)
static constexpr ::amap<Access,char,N<Access>> AccessChars = {{
	{ Access::Lnk  , 'L' }
,	{ Access::Reg  , 'R' }
,	{ Access::Stat , 'T' }
}} ;
static_assert(chk_enum_tab(AccessChars)) ;
using Accesses = BitMap<Access> ;                                      // distinguish files as soon as they can be distinguished by one of the liste Access'es
static constexpr Accesses DataAccesses { Access::Lnk , Access::Reg } ;

ENUM_1(FileLoc
,	Dep = SrcDir // <=Dep means that file must be reported as a dep
,	Repo
,	SrcDir       // file has been found in source dirs
,	Tmp
,	Proc         // file is in /proc
,	Admin
,	Ext          // all other cases
,	Unknown
)

ENUM(FileDisplay
,	None
,	Printable
,	Shell
,	Py
,	Json
)

namespace Disk {
	using Ddate       = Time::Ddate              ;
	using DiskSz      = uint64_t                 ;
	using FileNameIdx = Uint<n_bits(PATH_MAX+1)> ; // file names are limited to PATH_MAX

	//
	// path name library
	//
	// file  : a name not      ending with /
	// dir_s : a name          ending with /
	// path  : a name possibly ending with /

	bool     is_canon(::string const&) ;
	::string mk_canon(::string const&) ;
	//
	inline bool has_dir(::string const& path) {
		if (path.size()<3) return false ;       // we must have at least 2 components and a / to have a dir component
		return path.find('/',1)<path.size()-2 ; // search a / at neither ends of path
	}
	inline ::string dir_name_s( ::string const& path , FileNameIdx n=1 ) {
		SWEAR( n>=1 , path ) ;
		throw_unless( +path     , "no dir for empty path" ) ;
		throw_unless( path!="/" , "no dir for /"          ) ;
		size_t sep = path.size()-(path.back()=='/') ;
		for(; n ; n-- ) {
			throw_unless( sep!=Npos , "cannot walk uphill "s+n+" dirs from "+path ) ;
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
	inline bool is_dirname( ::string const& path                         ) { return !path || path.back()=='/'                                                         ; }
	inline bool is_in_dir ( ::string const& path , ::string const& dir_s ) { return path.starts_with(dir_s) || (path.size()+1==dir_s.size()&&dir_s.starts_with(path)) ; }

	inline bool is_abs_s(::string const& name_s) { return          name_s[0]=='/' ; } // name_s is (<x>/)*    or /(<x>/)* with <x>=[^/]+, empty name_s is necessarily relative
	inline bool is_abs  (::string const& name  ) { return !name || name  [0]=='/' ; } // name   is <x>(/<x>)* or (/<x>)*  with <x>=[^/]+, empty name   is necessarily absolute
	//
	inline bool   is_lcl_s    (::string const& name_s) { return !( is_abs_s(name_s) || name_s.starts_with("../")               ) ;                          }
	inline bool   is_lcl      (::string const& name  ) { return !( is_abs  (name  ) || name  .starts_with("../") || name==".." ) ;                          }
	inline size_t uphill_lvl_s(::string const& name_s) { SWEAR(!is_abs_s(name_s)) ; size_t l ; for( l=0 ; name_s.substr(3*l,3)=="../" ; l++ ) {} return l ; }
	inline size_t uphill_lvl  (::string const& name  ) { return uphill_lvl_s(name+'/') ;                                                                    }

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

	struct FileSig ;

	struct FileInfo {
		friend ::string& operator+=( ::string& , FileInfo const& ) ;
		using Stat = struct ::stat ;
	private :
		// statics
		static FileTag _s_tag(Stat const& st) ;
		// cxtors & casts
	public :
		FileInfo(                                                    ) = default ;
		FileInfo( Fd at                                              ) : FileInfo{at     ,{}            } {}
		FileInfo(         ::string const& name , bool no_follow=true ) : FileInfo{Fd::Cwd,name,no_follow} {}
		FileInfo( Fd at , ::string const& name , bool no_follow=true ) ;
		FileInfo( Stat const&                                        ) ;
		// accesses
		bool    operator==(FileInfo const&) const = default ;
		//
		bool    operator+() const { return tag()>=FileTag::Target ; } // i.e. sz & date are present
		FileTag tag      () const { return date.tag()             ; }
		FileSig sig      () const ;
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

	struct NfsGuard {
		// statics
	private :
		void _s_protect(::string const& dir_s) {
			::close(::open(no_slash(dir_s).c_str(),O_DIRECTORY|O_NOATIME)) ;
		}
		// cxtors & casts
	public :
		NfsGuard(bool rd=false) : reliable_dirs{rd} {}                                      // if dirs are not reliable, i.e. close to open coherence does not encompass uphill dirs ...
		~NfsGuard() { close() ; }                                                           // ... uphill dirs must must be open/close to force reliable access to files and their inodes
		//service
		::string const& access(::string const& file) {                                      // return file, must be called before any access to file or its inode if not sure it was produced locally
			if ( !reliable_dirs && +file && has_dir(file) ) _access_dir(dir_name_s(file)) ;
			return file ;
		}
		::string const& change(::string const& path) {                                      // must be called before any modif to file or its inode if not sure it was produced locally
			if ( !reliable_dirs && has_dir(path) ) {
				::string dir_s = dir_name_s(path) ;
				_access_dir(dir_s) ;
				to_stamp_dirs_s.insert(::move(dir_s)) ;
			}
			return path ;
		}
		void close() {
			SWEAR( !to_stamp_dirs_s || !reliable_dirs ) ;                                   // cannot record dirs to stamp if reliable_dirs
			for( ::string const& d_s : to_stamp_dirs_s ) _s_protect(d_s) ;                  // close to force NFS close to open cohenrence, open is useless
			to_stamp_dirs_s.clear() ;
		}
	private :
		void _access_dir(::string const& dir_s) {
			access(no_slash(dir_s)) ;                                                       // we opend dir, we must ensure its dir is up-to-date w.r.t. NFS
			if (fetched_dirs_s.insert(dir_s).second) _s_protect(dir_s) ;                    // open to force NFS close to open coherence, close is useless
		}
		// data
	public :
		::uset_s fetched_dirs_s  ;
		::uset_s to_stamp_dirs_s ;
		bool     reliable_dirs = false/*garbage*/ ;
	} ;

	// list files within dir with prefix in front of each entry
	::vector_s lst_dir_s( Fd at , ::string const& dir_s={} , ::string const& prefix={} ) ;
	// deep list files within dir with prefix in front of each entry, return a single entry {prefix} if file is not a dir (including if file does not exist)
	::vector_s walk( Fd at , ::string const& file , ::string const& prefix={} ) ;
	//
	size_t/*pos*/ mk_dir_s      ( Fd at , ::string const& dir_s ,             bool unlnk_ok=false                          ) ; // if unlnk_ok <=> unlink any file on the path if necessary to make dir
	size_t/*pos*/ mk_dir_s      ( Fd at , ::string const& dir_s , NfsGuard& , bool unlnk_ok=false                          ) ; // if unlnk_ok <=> unlink any file on the path if necessary to make dir
	void          dir_guard     ( Fd at , ::string const& file                                                             ) ;
	void          unlnk_inside_s( Fd at , ::string const& dir_s                     , bool abs_ok=false , bool force=false ) ;
	bool/*done*/  unlnk         ( Fd at , ::string const& file  , bool dir_ok=false , bool abs_ok=false , bool force=false ) ; // if dir_ok <=> unlink whole dir if it is one
	bool          can_uniquify  ( Fd at , ::string const& file                                                             ) ;
	bool/*done*/  uniquify      ( Fd at , ::string const& file                                                             ) ;
	void          rmdir_s       ( Fd at , ::string const& dir_s                                                            ) ;
	//
	inline void lnk( Fd at , ::string const& file , ::string const& target ) {
		if (::symlinkat(target.c_str(),at,file.c_str())!=0) {
			::string at_str = at==Fd::Cwd ? ""s : "<"s+at.fd+">/" ;
			throw "cannot create symlink from "+at_str+file+" to "+target ;
		}
	}

	inline ::string read_lnk( Fd at , ::string const& file ) {
		char    buf[PATH_MAX] ;
		ssize_t cnt           = ::readlinkat(at,file.c_str(),buf,PATH_MAX) ;
		if ( cnt<0 || cnt>=PATH_MAX ) return {} ;
		return {buf,size_t(cnt)} ;
	}

	inline bool  is_dir   ( Fd at , ::string const& file={} , bool no_follow=true ) { return  FileInfo(at,file,no_follow).tag()==FileTag::Dir ; }
	inline bool  is_target( Fd at , ::string const& file={} , bool no_follow=true ) { return +FileInfo(at,file,no_follow)                     ; }
	inline bool  is_exe   ( Fd at , ::string const& file={} , bool no_follow=true ) { return  FileInfo(at,file,no_follow).tag()==FileTag::Exe ; }
	inline Ddate file_date( Fd at , ::string const& file={} , bool no_follow=true ) { return  FileInfo(at,file,no_follow).date                ; }

	inline ::vector_s      lst_dir_s     ( ::string const& dir_s , ::string const& prefix={}                                 ) { return lst_dir_s     (Fd::Cwd,dir_s,prefix              ) ; }
	inline ::vector_s      walk          ( ::string const& path  , ::string const& prefix={}                                 ) { return walk          (Fd::Cwd,path ,prefix              ) ; }
	inline size_t/*pos*/   mk_dir_s      ( ::string const& dir_s ,                bool unlnk_ok=false                        ) { return mk_dir_s      (Fd::Cwd,dir_s,   unlnk_ok         ) ; }
	inline size_t/*pos*/   mk_dir_s      ( ::string const& dir_s , NfsGuard& ng , bool unlnk_ok=false                        ) { return mk_dir_s      (Fd::Cwd,dir_s,ng,unlnk_ok         ) ; }
	inline ::string const& dir_guard     ( ::string const& path                                                              ) {        dir_guard     (Fd::Cwd,path) ; return path ;         }
	inline void            unlnk_inside_s( Fd at                                                         , bool force=false  ) {        unlnk_inside_s(at     ,{}          ,false ,force ) ; }
	inline void            unlnk_inside_s( ::string const& dir_s                     , bool abs_ok=false , bool force=false  ) {        unlnk_inside_s(Fd::Cwd,dir_s       ,abs_ok,force ) ; }
	inline bool/*done*/    unlnk         ( ::string const& file  , bool dir_ok=false , bool abs_ok=false , bool force=false  ) { return unlnk         (Fd::Cwd,file ,dir_ok,abs_ok,force ) ; }
	inline bool            can_uniquify  ( ::string const& file                                                              ) { return can_uniquify  (Fd::Cwd,file                      ) ; }
	inline bool/*done*/    uniquify      ( ::string const& file                                                              ) { return uniquify      (Fd::Cwd,file                      ) ; }
	inline void            rmdir_s       ( ::string const& dir_s                                                             ) {        rmdir_s       (Fd::Cwd,dir_s                     ) ; }
	inline void            lnk           ( ::string const& file  , ::string const& target                                    ) {        lnk           (Fd::Cwd,file ,target              ) ; }
	inline ::string        read_lnk      ( ::string const& file                                                              ) { return read_lnk      (Fd::Cwd,file                      ) ; }
	inline bool            is_dir        ( ::string const& file  , bool no_follow=true                                       ) { return is_dir        (Fd::Cwd,file ,no_follow           ) ; }
	inline bool            is_target     ( ::string const& file  , bool no_follow=true                                       ) { return is_target     (Fd::Cwd,file ,no_follow           ) ; }
	inline bool            is_exe        ( ::string const& file  , bool no_follow=true                                       ) { return is_exe        (Fd::Cwd,file ,no_follow           ) ; }
	inline Ddate           file_date     ( ::string const& file  , bool no_follow=true                                       ) { return file_date     (Fd::Cwd,file ,no_follow           ) ; }

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
		#define C const
		template<class T> T C& get(size_t ofs=0) C { throw_unless( ofs+sizeof(T)<=sz , "object @",ofs,"out of file of size ",sz ) ; return *reinterpret_cast<T C*>(data+ofs) ; }
		template<class T> T  & get(size_t ofs=0)   { throw_unless( ofs+sizeof(T)<=sz , "object @",ofs,"out of file of size ",sz ) ; return *reinterpret_cast<T  *>(data+ofs) ; }
		#undef C
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
		// data
		LnkSupport lnk_support   = LnkSupport::Full ; // by default, be pessimistic
		bool       reliable_dirs = false            ; // if true => dirs coherence is enforced when files are updated (unlike NFS)
		::string   repo_root_s   = {}               ;
		::string   tmp_dir_s     = {}               ;
		::vector_s src_dirs_s    = {}               ;
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
			_Dvg( ::string const& domain , ::string const& chk ) { update(domain,chk) ; }
			// accesses
			bool operator+() const { return ok ; }
			// services
			void update( ::string const& domain , ::string const& chk ) ; // udpate after domain & chk have been lengthened or shortened, but not modified internally
			// data
			bool   ok  = false ;
			size_t dvg = 0     ;
		} ;
		// statics
	private :
		// if No <=> no file, if Maybe <=> a regular file, if Yes <=> a link
		Bool3/*ok*/ _read_lnk( ::string& target/*out*/ , ::string const& real ) {
			::string t = read_lnk(real) ;
			if (!t) return Maybe & (errno!=ENOENT) ;
			target = ::move(t) ;
			return Yes ;
		}
		// cxtors & casts
	public :
		RealPath() = default ;
		// src_dirs_s may be either absolute or relative, but must be canonic
		// tmp_dir_s must be absolute and canonic
		RealPath ( RealPathEnv const& rpe , pid_t p=0 ) ;
		// services
		FileLoc file_loc( ::string const& real ) const { return _env->file_loc(real) ; }
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
		RealPathEnv const* _env            ;
		::string           _admin_dir      ;
		::vector_s         _abs_src_dirs_s ;                              // this is an absolute version of src_dirs
		size_t             _repo_root_sz   ;
		::string           _cwd            ;
		pid_t              _cwd_pid        = 0 ;                          // pid for which _cwd is valid if pid==0
	} ;
	::string& operator+=( ::string& , RealPath::SolveReport const& ) ;

}
