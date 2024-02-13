// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>  // fstatat
#include <sys/types.h>

#include "config.hh"
#include "fd.hh"
#include "lib.hh"
#include "time.hh"

namespace Disk {
	using Ddate  = Time::Ddate ;
	using DiskSz = uint64_t    ;

	ENUM(Access
	,	Lnk                                 // file is accessed with readlink
	,	Reg                                 // file is accessed with open
	,	Stat                                // file is accessed with stat like (read inode)
	)
	static constexpr char AccessChars[] = {
		'L'                                 // Lnk
	,	'R'                                 // Reg
	,	'T'                                 // Stat
	} ;
	static_assert(::size(AccessChars)==+Access::N) ;
	using Accesses = BitMap<Access> ;
	static constexpr Accesses DataAccesses { Access::Lnk , Access::Reg } ;

	ENUM_1( FileTag
	,	None
	,	Reg
	,	Exe // a regular file with exec permission
	,	Lnk
	,	Dir
	,	Err
	)

	bool     is_canon (::string const&) ;
	::string dir_name (::string const&) ;
	::string base_name(::string const&) ;

	struct FileInfo {
		friend ::ostream& operator<<( ::ostream& , FileInfo const& ) ;
		using Stat = struct ::stat ;
	private :
		// statics
		static Stat _s_stat( Fd at , const char* name ) {
			Stat st ;
			errno = 0 ;
			::fstatat( at , name , &st , AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW ) ;
			return st ;
		}
		// cxtors & casts
	public :
		FileInfo(                                                    ) = default ;
		FileInfo( Fd at                                              ) : FileInfo{at     ,{}            } {}
		FileInfo(         ::string const& name , bool no_follow=true ) : FileInfo{Fd::Cwd,name,no_follow} {}
		FileInfo( Fd at , ::string const& name , bool no_follow=true ) ;
		// accesses
		bool operator+() const {
			switch (tag) {
				case FileTag::Reg :
				case FileTag::Exe :
				case FileTag::Lnk : return true  ;
				default           : return false ;
			}
		}
		bool operator!() const { return !+*this                                ; } // i.e. sz & date are not present
		bool is_reg   () const { return tag==FileTag::Reg || tag==FileTag::Exe ; }
		// data
		DiskSz  sz   = 0             ;
		Ddate   date ;
		FileTag tag  = FileTag::None ;
	} ;

	struct NfsGuard {
		// statics
	private :
		void _s_protect(::string const& dir) {
			::close(::open(+dir?dir.c_str():".",O_DIRECTORY|O_NOATIME)) ;
		}
		// cxtors & casts
	public :
		NfsGuard(bool rd=false) : reliable_dirs{rd} {}                   // if dirs are not reliable, i.e. close to open coherence does not encompass uphill dirs ...
		~NfsGuard() { close() ; }                                        // ... uphill dirs must must be open/close to force reliable access to files and their inodes
		//service
		::string const& access(::string const& file) {                   // return file, must be called before any access to file or its inode if not sure it was produced locally
			if ( !reliable_dirs && +file ) _access_dir(dir_name(file)) ;
			return file ;
		}
		::string const& change(::string const& file) {                   // must be called before any modif to file or its inode if not sure it was produced locally
			if ( !reliable_dirs && +file ) {
				::string dir = dir_name(file) ;
				_access_dir(dir) ;
				to_stamp_dirs.insert(::move(dir)) ;
			}
			return file ;
		}
		void close() {
			SWEAR( !to_stamp_dirs || !reliable_dirs ) ;                  // cannot record dirs to stamp if reliable_dirs
			for( ::string const& d : to_stamp_dirs ) _s_protect(d) ;     // close to force NFS close to open cohenrence, open is useless
			to_stamp_dirs.clear() ;
		}
	private :
		void _access_dir(::string const& dir) {
			access(dir) ;                                                // we opend dir, we must ensure its dir is up-to-date w.r.t. NFS
			if (fetched_dirs.insert(dir).second) _s_protect(dir) ;       // open to force NFS close to open coherence, close is useless
		}
		// data
	public :
		::uset_s fetched_dirs  ;
		::uset_s to_stamp_dirs ;
		bool     reliable_dirs = false/*garbage*/ ;
	} ;

	::vector_s read_lines   ( ::string const& file                     ) ;
	::string   read_content ( ::string const& file                     ) ;
	void       write_lines  ( ::string const& file , ::vector_s const& ) ;
	void       write_content( ::string const& file , ::string   const& ) ;

	// list files within dir with prefix in front of each entry
	::vector_s lst_dir( Fd at , ::string const& dir={} , ::string const& prefix={} ) ;
	// deep list files within dir with prefix in front of each entry, return a single entry {prefix} if file is not a dir (including if file does not exist)
	::vector_s walk( Fd at , ::string const& file , ::string const& prefix={} ) ;
	//
	int/*n_dirs*/ mkdir        ( Fd at , ::string const& dir     ,             bool multi=true , bool unlink_ok=false ) ; // if unlink <=> unlink any file on the path if necessary to make dir
	int/*n_dirs*/ mkdir        ( Fd at , ::string const& dir     , NfsGuard& , bool multi=true , bool unlink_ok=false ) ; // if unlink <=> unlink any file on the path if necessary to make dir
	void          dir_guard    ( Fd at , ::string const& file                                                         ) ;
	void          unlink_inside( Fd at , ::string const& dir ={}                                                      ) ;
	bool/*done*/  unlink       ( Fd at , ::string const& file    , bool dir_ok=false                                  ) ; // if dir_ok <=> unlink whole dir if it is one
	bool/*done*/  uniquify     ( Fd at , ::string const& file                                                         ) ;
	void          rmdir        ( Fd at , ::string const& dir                                                          ) ;
	//
	static inline void lnk( Fd at , ::string const& file , ::string const& target ) {
		if (::symlinkat(target.c_str(),at,file.c_str())!=0) {
			::string at_str = at==Fd::Cwd ? ""s : to_string('<',int(at),">/") ;
			throw to_string("cannot create symlink from ",at_str,file," to ",target) ;
		}
	}

	static inline Fd open_read( Fd at , ::string const& filename ) {
		return ::openat( at , filename.c_str() , O_RDONLY|O_CLOEXEC , 0666 ) ;
	}

	static inline Fd open_write( Fd at , ::string const& filename , bool append=false , bool exe=false , bool read_only=false ) {
		dir_guard(at,filename) ;
		return ::openat( at , filename.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC|(append?O_APPEND:O_TRUNC) , 0777 & ~(exe?0000:0111) & ~(read_only?0222:0000) ) ;
	}

	static inline ::string read_lnk( Fd at , ::string const& file ) {
		char    buf[PATH_MAX] ;
		ssize_t cnt           = ::readlinkat(at,file.c_str(),buf,PATH_MAX) ;
		if ( cnt<0 || cnt>=PATH_MAX ) return {} ;
		return {buf,size_t(cnt)} ;
	}

	static inline bool  is_reg   ( Fd at , ::string const& file={} , bool no_follow=true ) { return  FileInfo(at,file,no_follow).is_reg()          ; }
	static inline bool  is_dir   ( Fd at , ::string const& file={} , bool no_follow=true ) { return  FileInfo(at,file,no_follow).tag==FileTag::Dir ; }
	static inline bool  is_target( Fd at , ::string const& file={} , bool no_follow=true ) { return +FileInfo(at,file,no_follow)                   ; }
	static inline bool  is_exe   ( Fd at , ::string const& file={} , bool no_follow=true ) { return  FileInfo(at,file,no_follow).tag==FileTag::Exe ; }
	static inline Ddate file_date( Fd at , ::string const& file={} , bool no_follow=true ) { return  FileInfo(at,file,no_follow).date              ; }

	static inline ::vector_s      lst_dir      ( ::string const& dir  , ::string const& prefix={}                                 ) { return lst_dir      (Fd::Cwd,dir ,prefix              ) ; }
	static inline ::vector_s      walk         ( ::string const& file , ::string const& prefix={}                                 ) { return walk         (Fd::Cwd,file,prefix              ) ; }
	static inline int/*n_dirs*/   mkdir        ( ::string const& dir  ,                bool multi=true , bool unlink_ok=false     ) { return mkdir        (Fd::Cwd,dir ,   multi,unlink_ok  ) ; }
	static inline int/*n_dirs*/   mkdir        ( ::string const& dir  , NfsGuard& ng , bool multi=true , bool unlink_ok=false     ) { return mkdir        (Fd::Cwd,dir ,ng,multi,unlink_ok  ) ; }
	static inline ::string const& dir_guard    ( ::string const& file                                                             ) {        dir_guard    (Fd::Cwd,file) ; return file ;        }
	static inline void            unlink_inside( ::string const& dir                                                              ) {        unlink_inside(Fd::Cwd,dir                      ) ; }
	static inline bool/*done*/    unlink       ( ::string const& file , bool dir_ok=false                                         ) { return unlink       (Fd::Cwd,file,dir_ok              ) ; }
	static inline bool/*done*/    uniquify     ( ::string const& file                                                             ) { return uniquify     (Fd::Cwd,file                     ) ; }
	static inline void            rmdir        ( ::string const& file                                                             ) {        rmdir        (Fd::Cwd,file                     ) ; }
	static inline void            lnk          ( ::string const& file , ::string const& target                                    ) {        lnk          (Fd::Cwd,file,target              ) ; }
	static inline Fd              open_read    ( ::string const& file                                                             ) { return open_read    (Fd::Cwd,file                     ) ; }
	static inline Fd              open_write   ( ::string const& file , bool append=false , bool exe=false , bool read_only=false ) { return open_write   (Fd::Cwd,file,append,exe,read_only) ; }
	static inline ::string        read_lnk     ( ::string const& file                                                             ) { return read_lnk     (Fd::Cwd,file                     ) ; }
	static inline bool            is_reg       ( ::string const& file , bool no_follow=true                                       ) { return is_reg       (Fd::Cwd,file,no_follow           ) ; }
	static inline bool            is_dir       ( ::string const& file , bool no_follow=true                                       ) { return is_dir       (Fd::Cwd,file,no_follow           ) ; }
	static inline bool            is_target    ( ::string const& file , bool no_follow=true                                       ) { return is_target    (Fd::Cwd,file,no_follow           ) ; }
	static inline bool            is_exe       ( ::string const& file , bool no_follow=true                                       ) { return is_exe       (Fd::Cwd,file,no_follow           ) ; }
	static inline Ddate           file_date    ( ::string const& file , bool no_follow=true                                       ) { return file_date    (Fd::Cwd,file,no_follow           ) ; }

	static inline ::string cwd() {
		char buf[PATH_MAX] ;                 // use posix, not linux extension that allows to pass nullptr as argument and malloc's the returned string
		char* cwd = ::getcwd(buf,PATH_MAX) ;
		if (!cwd) throw "cannot get cwd"s ;
		::string res{cwd} ;
		SWEAR( res[0]=='/' , res[0] ) ;
		if (res.size()==1) return {}  ;      // cwd contains components prefixed by /, if at root, it is logical for it to be empty
		else               return res ;
	}

	static inline bool is_abs  (::string const& name  ) { return !name || name  [0]=='/' ; } // name   is <x>(/<x>)* or (/<x>)*  with <x>=[^/]+, empty name   is necessarily absolute
	static inline bool is_abs_s(::string const& name_s) { return          name_s[0]=='/' ; } // name_s is (<x>/)*    or /(<x>/)* with <x>=[^/]+, empty name_s is necessarily relative
	//
	static inline bool is_lcl  (::string const& name  ) { return !( is_abs  (name  ) || name  .starts_with("../") || name==".." ) ; }
	static inline bool is_lcl_s(::string const& name_s) { return !( is_abs_s(name_s) || name_s.starts_with("../")               ) ; }
	//
	/**/          ::string mk_lcl( ::string const& file , ::string const& dir_s ) ;          // return file (passed as from dir_s origin) as seen from dir_s
	/**/          ::string mk_glb( ::string const& file , ::string const& dir_s ) ;          // return file (passed as from dir_s       ) as seen from dir_s origin
	static inline ::string mk_abs( ::string const& file , ::string const& dir_s ) {          // return file (passed as from dir_s       ) as absolute
		SWEAR( is_abs_s(dir_s) , dir_s ) ;
		return mk_glb(file,dir_s) ;
	}
	static inline ::string mk_rel( ::string const& file , ::string const& dir_s ) {
		if (is_abs(file)==is_abs_s(dir_s)) return mk_lcl(file,dir_s) ;
		else                               return file               ;
	}

	// manage strings containing file markers so as to be localized when displayed to user
	// file format is : FileMrkr + file length + file
	static constexpr char FileMrkr = 0 ;
	::string _localize( ::string const& txt , ::string const& dir_s , size_t first_file ) ;  // not meant to be used directly
	static inline ::string localize( ::string const& txt , ::string const& dir_s={} ) {
		if ( size_t pos = txt.find(FileMrkr) ; pos==Npos ) return           txt            ; // fast path : avoid calling localize
		else                                               return _localize(txt,dir_s,pos) ;
	}
	static inline ::string localize( ::string&& txt , ::string const& dir_s={} ) {
		if ( size_t pos = txt.find(FileMrkr) ; pos==Npos ) return ::move   (txt          ) ; // fast path : avoid copy
		else                                               return _localize(txt,dir_s,pos) ;
	}
	static inline ::string mk_file (::string const& f) {
		::string pfx(1+sizeof(FileNameIdx),FileMrkr) ;
		encode_int<FileNameIdx>(&pfx[1],f.size()) ;
		return pfx+f ;
	}

	struct FileMap {
		// cxtors & casts
		FileMap(                        ) = default ;
		FileMap( Fd , ::string const&   ) ;
		FileMap(      ::string const& f ) : FileMap{Fd::Cwd,f} {}
		bool operator+() const { return _ok     ; }
		bool operator!() const { return !+*this ; }
		// accesses
		template<class T> T const& get(size_t ofs=0) const { if (ofs+sizeof(T)>sz) throw to_string("object @",ofs,"out of file of size ",sz) ; return *reinterpret_cast<T const*>(data+ofs) ; }
		template<class T> T      & get(size_t ofs=0)       { if (ofs+sizeof(T)>sz) throw to_string("object @",ofs,"out of file of size ",sz) ; return *reinterpret_cast<T      *>(data+ofs) ; }
		// data
		const uint8_t* data = nullptr ;
		size_t         sz   = 0       ;
	private :
		AutoCloseFd _fd ;
		bool        _ok = false ;
	} ;

	ENUM_1(Kind
	,	Dep = SrcDirs // <=Dep means that file must be reported as a dep
	,	Repo
	,	SrcDirs       // file has been found in source dirs
	,	Root          // file is the root dir
	,	Tmp
	,	Proc          // file is in /proc
	,	Admin
	,	Ext           // all other cases
	)

	struct RealPathEnv {
		friend ::ostream& operator<<( ::ostream& , RealPathEnv const& ) ;
		LnkSupport lnk_support   = LnkSupport::Full ;                     // by default, be pessimistic
		bool       reliable_dirs = false            ;                     // if true => dirs coherence is enforced when files are updated (unlike NFS)
		::string   root_dir      = {}               ;
		::string   tmp_dir       = {}               ;
		::string   tmp_view      = {}               ;
		::vector_s src_dirs_s    = {}               ;
	} ;

	// XXX : avoid duplicating RealPathEnv by storing a pointer to it here rather than inheritance, which requires to cleanly separate global part (in RealPathEnv) and specific part (in RealPath)
	struct RealPath : RealPathEnv {
		struct SolveReport {
			friend ::ostream& operator<<( ::ostream& , SolveReport const& ) ;
			// data
			::string   real          = {}        ; // real path relative to root if in_repo or in a relative src_dir or absolute if in an absolute src_dir or mapped in tmp, else empty
			::vector_s lnks          = {}        ; // links followed to get to real
			::string   last_lnk      = {}        ; // last tried (and failed) link
			Accesses   last_accesses = {}        ; // Access::Lnk if file does not exist and !no_follow
			Kind       kind          = Kind::Ext ; // do not process awkard files
			bool       mapped        = false     ; // if true <=> tmp mapping has been used
		} ;
	private :
		// helper class to help recognize when we are in repo or in tmp
		struct _Dvg {
			_Dvg( ::string const& domain , ::string const& chk ) { update(domain,chk) ; }
			operator bool() const { return ok ; }
			// udpate after domain & chk have been lengthened or shortened, but not modified internally
			void update( ::string const& domain , ::string const& chk ) {
				size_t start = dvg ;;
				ok  = domain.size() <= chk.size()     ;
				dvg = ok ? domain.size() : chk.size() ;
				for( size_t i=start ; i<dvg ; i++ ) {
					if (domain[i]!=chk[i]) {
						ok  = false ;
						dvg = i     ;
						return ;
					}
				}
				if ( domain.size() < chk.size() ) ok = chk[domain.size()]=='/' ;
			}
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
		// tmp_dir and tmp_view must be absolute and canonic
		RealPath ( RealPathEnv const& rpe ,                  pid_t p=0 ) { init( rpe ,                                                  p ) ; }
		RealPath ( RealPathEnv const& rpe , ::string&& cwd , pid_t p=0 ) { init( rpe , ::move(cwd)                                    , p ) ; }
		void init( RealPathEnv const& rpe ,                  pid_t p=0 ) { init( rpe , p?read_lnk(to_string("/proc/",p,"/cwd")):cwd() , p ) ; }
		void init( RealPathEnv const&     , ::string&& cwd , pid_t  =0 ) ;
		// services
		SolveReport solve( Fd at , ::string const&      , bool no_follow=false ) ;
		SolveReport solve( Fd at , const char*     file , bool no_follow=false ) { return solve(at     ,::string(file),no_follow) ; } // ensure proper types
		SolveReport solve(         ::string const& file , bool no_follow=false ) { return solve(Fd::Cwd,         file ,no_follow) ; }
		SolveReport solve( Fd at ,                        bool no_follow=false ) { return solve(at     ,         {}   ,no_follow) ; }
		//
		vmap_s<Accesses> exec(SolveReport&) ;                                                                                         // arg is updated to reflect last interpreter
	private :
		::string _find_src(::string const& real) const ;
		// data
	public :
		pid_t    pid          = 0                ;
		bool     has_tmp_view = false/*garbage*/ ;
		::string cwd_         ;
	private :
		::string   _admin_dir      ;
		::vector_s _abs_src_dirs_s ;                                                                                                  // this is an absolute version of src_dirs
	} ;
	::ostream& operator<<( ::ostream& , RealPath::SolveReport const& ) ;

}
