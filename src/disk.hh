// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "lib.hh"
#include "time.hh"

namespace Disk {
	using Date = Time::DiskDate ;

	ENUM_1( FileTag
	,	Fancy = Dir                                                            // >=Fancy means not an official target
	,	None
	,	Reg
	,	Exe
	,	Lnk
	,	Dir
	,	Other
	,	Err
	)

	struct FileInfoDate ;
	struct FileInfo {
		friend ::ostream& operator<<( ::ostream& , FileInfo const& ) ;
		friend FileInfoDate ;
	private :
		// statics
		static struct ::stat _s_stat( Fd at , const char* name ) {
			struct ::stat st ;
			errno = 0 ;
			::fstatat( at , name , &st , AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW ) ;
			return st ;
		}
		// cxtors & casts
	public :
		FileInfo(                              ) = default ;
		FileInfo( Fd at                        ) : FileInfo(        at     ,{}           ) {}
		FileInfo(         ::string const& name ) : FileInfo(        Fd::Cwd,name         ) {}
		FileInfo( Fd at , ::string const& name ) : FileInfo(_s_stat(at     ,name.c_str())) {}
	private :
		FileInfo(struct ::stat const&) ;                                       // /!\ errno must be valid and 0 if stat is ok
		// accesses
	public :
		bool operator+() const { return +tag && tag<FileTag::Fancy             ; } // i.e. sz & date are present
		bool operator!() const { return !+*this                                ; } // i.e. sz & date are not present
		bool is_reg   () const { return tag==FileTag::Reg || tag==FileTag::Exe ; }
		// data
		FileTag tag = FileTag::None ;
		size_t  sz  = 0             ;
	} ;

	struct FileInfoDate : FileInfo {
		friend ::ostream& operator<<( ::ostream& , FileInfoDate const& ) ;
		// cxtors & casts
		FileInfoDate(                              ) = default ;
		FileInfoDate( Fd at                        ) : FileInfoDate(at     ,{}  ) {} // opening file is important to guarantee attribute sync with NFS
		FileInfoDate(         ::string const& name ) : FileInfoDate(Fd::Cwd,name) {} // .
		FileInfoDate( Fd at , ::string const& name ) ;                               // .
		// services
		Date date_or_now() const { return +*this ? date : Time::DiskDate::s_now() ; }
		// data
		Date date ;
	} ;

	void unlink_inside(::string const& dir ) ;
	void unlink       (::string const& file) ;                                 // unlink whole dir if it is one
	// deep list files within dir with prefix in front of each entry, return a single entry {prefix} if file is not a dir (including if file does not exist)
	/**/          ::vector_s walk( Fd at , ::string const& file , ::string const& prefix={} ) ;
	static inline ::vector_s walk(         ::string const& file , ::string const& prefix={} ) { return walk(Fd::Cwd,file,prefix) ; }
	//
	void               dir_guard(         ::string const&                          ) ;
	::string           dir_name (         ::string const&                          ) ;
	::string           base_name(         ::string const&                          ) ;
	void               make_dir ( Fd at , ::string const&   , bool unlink_ok=false ) ;                                    // if unlink <=> unlink any file on the path if necessary to make dir
	static inline void make_dir (         ::string const& d , bool uok      =false ) { return make_dir(Fd::Cwd,d,uok) ; }
	//
	static inline void lnk( ::string const& target , ::string const& dst ) {
		if (::symlink(target.c_str(),dst.c_str())!=0) throw to_string("cannot create symlink from ",dst," to ",target) ;
	}

	static inline Fd open_read( ::string const& file_name , int flags=0 ) {
		return ::open( file_name.c_str() , O_RDONLY|O_CLOEXEC|flags , 0644 ) ;
	}

	static inline Fd open_write( ::string const& file_name , bool append=false ) {
		dir_guard(file_name) ;
		return ::open( file_name.c_str() , O_WRONLY|O_CREAT|O_CLOEXEC|(append?O_APPEND:O_TRUNC) , 0644 ) ;
	}

	::vector_s read_lines  ( ::string const& file                     ) ;
	::string   read_content( ::string const& file                     ) ;
	void       write_lines ( ::string const& file , ::vector_s const& ) ;

	static inline ::string read_lnk(::string const& file_name) {
		char    buf[PATH_MAX] ;
		ssize_t cnt           = ::readlink(file_name.c_str(),buf,PATH_MAX) ;
		if ( cnt<0 || cnt>PATH_MAX ) return {} ;
		return {buf,size_t(cnt)} ;
	}

	static inline bool is_reg   ( Fd at , ::string const& name={} ) { return  FileInfo    (at,name).is_reg()          ; }
	static inline bool is_lnk   ( Fd at , ::string const& name={} ) { return  FileInfo    (at,name).tag==FileTag::Lnk ; }
	static inline bool is_dir   ( Fd at , ::string const& name={} ) { return  FileInfo    (at,name).tag==FileTag::Dir ; }
	static inline bool is_target( Fd at , ::string const& name={} ) { return +FileInfo    (at,name)                   ; }
	static inline bool is_none  ( Fd at , ::string const& name={} ) { return !FileInfo    (at,name).tag               ; }
	static inline Date file_date( Fd at , ::string const& name={} ) { return  FileInfoDate(at,name).date              ; }
	//
	static inline bool is_reg   (         ::string const& name    ) { return is_reg   (Fd::Cwd,name) ; }
	static inline bool is_lnk   (         ::string const& name    ) { return is_lnk   (Fd::Cwd,name) ; }
	static inline bool is_dir   (         ::string const& name    ) { return is_dir   (Fd::Cwd,name) ; }
	static inline bool is_target(         ::string const& name    ) { return is_target(Fd::Cwd,name) ; }
	static inline bool is_none  (         ::string const& name    ) { return is_none  (Fd::Cwd,name) ; }
	static inline Date file_date(         ::string const& name    ) { return file_date(Fd::Cwd,name) ; }

	static inline ::string cwd() {
		char* buf = ::getcwd(nullptr,0) ;
		if (!buf) throw "cannot get cwd"s ;
		::string res{buf} ;
		::free(buf) ;
		SWEAR(res[0]=='/') ;
		if (res.size()==1) return {}  ;                                        // cwd_ contains components prefixed by /, if at root, it is logical for it to be empty
		else               return res ;
	}

	static inline bool is_abs_path(::string const& name) { return name.empty() || ( name.front()=='/' && name.back()!='/' ) ; } // empty represent / when used as abs path
	::string localize ( ::string const& file_name , ::string const& dir_s ) ;                                                   // return passed global file name as seen from dir_s
	::string globalize( ::string const& file_name , ::string const& dir_s ) ;                                                   // return passed local  file name as seen from root

	struct FileMap {
		FileMap(::string const&) ;
		operator bool() const { return _ok ; }
		// data
		const uint8_t* data = nullptr ;
		size_t         size = 0       ;
	private :
		AutoCloseFd _fd ;
		bool        _ok = false ;
	} ;

	struct RealPath {
		struct SolveReport {
			friend ::ostream& operator<<( ::ostream& , SolveReport const& ) ;
			// cxtors & casts
			SolveReport(                                                     ) = default ;
			SolveReport( ::string && r , ::vector_s&& ls , bool ir , bool it ) : real{::move(r)} , lnks{::move(ls)} , in_repo{ir} , in_tmp{it} {}
			// data
			::string   real    ;
			::vector_s lnks    ;
			bool       in_repo = false ;
			bool       in_tmp  = false ;
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
		static Bool3/*ok*/ _s_read_lnk( ::string& target/*out*/ , ::string const& real ) {
			char buf[PATH_MAX] ;
			ssize_t n = ::readlink( real.c_str() , buf , sizeof(buf) ) ;
			if (n<0) return Maybe & (errno!=ENOENT) ;
			buf[n] = 0 ;
			target = static_cast<const char*>(buf) ;                           // w/o cast, target size would be PATH_MAX
			return Yes ;
		}
		// cxtors & casts
	public :
		RealPath (                           ) = default ;
		RealPath ( LnkSupport ls , pid_t p=0 ) { init(ls,p) ; }
		void init( LnkSupport ls , pid_t p=0 ) {
			SWEAR( is_abs_path(*g_root_dir) && is_abs_path(*g_tmp_dir) ) ;
			_admin_dir   = to_string(*g_root_dir,'/',AdminDir)                ;
			cwd_         = p ? read_lnk(to_string("/proc/",p,"/cwd")) : cwd() ;
			pid          = p                                                  ;
			_lnk_support = ls                                                 ;
		}
		// services
	public :
		SolveReport solve( Fd at , ::string const&      , bool no_follow=false ) ;
		SolveReport solve(         ::string const& file , bool no_follow=false ) { return solve(Fd::Cwd,file,no_follow) ; }
		SolveReport solve( Fd at                        , bool no_follow=false ) { return solve(at     ,{}  ,no_follow) ; }

		// data
		::string cwd_      ;
		pid_t    pid       = 0 ;
	private :
		::string _admin_dir ;
		// cache a mapping real -> info where info is :
		// - target : target of the link if real is     a symlink
		// - exists : true if it exists  if real is not a symlink
		LnkSupport                                     _lnk_support = LnkSupport::Unknown ;

	} ;
	::ostream& operator<<( ::ostream& , RealPath::SolveReport const& ) ;

}
