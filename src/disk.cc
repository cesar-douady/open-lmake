// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/mman.h>
#include <sys/sendfile.h>

#include "disk.hh"
#include "hash.hh"

using namespace filesystem ;

// ENUM macro does not work inside namespace's
ENUM( CanonState
,	First
,	Empty
,	Dot
,	DotDot
,	Plain
)

namespace Disk {

	//
	// path name library
	//

	bool is_canon(::string const& path) {
		bool       accept_dot_dot = true              ;
		CanonState state          = CanonState::First ;
		for( char c : path ) {
			switch (c) {
				case '\0' : return false ;                                                    // file names are not supposed to contain any nul char
				case '/' :
					switch (state) {
						case CanonState::Empty  :                      return false ;
						case CanonState::Dot    :                      return false ;
						case CanonState::DotDot : if (!accept_dot_dot) return false ; break ;
						case CanonState::First  :                                             // seen from /, First is like Plain
						case CanonState::Plain  : accept_dot_dot = false ;            break ; // .. is only accepted as relative prefix
					DF}
					state = CanonState::Empty ;
				break ;
				case '.' :
					switch (state) {
						case CanonState::First  :                                             // seen from ., First is like Empty
						case CanonState::Empty  : state = CanonState::Dot    ; break ;
						case CanonState::Dot    : state = CanonState::DotDot ; break ;
						case CanonState::DotDot : state = CanonState::Plain  ; break ;
						case CanonState::Plain  :                              break ;
					DF}
				break ;
				default :
					state = CanonState::Plain ;
			}
		}
		switch (state) {
			case CanonState::First  :                                                         // an empty path
			case CanonState::Empty  : return true  ;                                          // a directory ending with /
			case CanonState::Dot    : return false ;
			case CanonState::DotDot : return false ;
			case CanonState::Plain  : return true  ;
		DF}
		return true ;
	}

	::string mk_canon(::string const& path) {
		::string   res   ;
		CanonState state = CanonState::First ;
		for( char c : path ) {
			switch (c) {
				case '\0' : throw "file contains nul char : "+path ;           // file names are not supposed to contain any nul char, cannot canonicalize
				case '/' :
					switch (state) {
						case CanonState::Empty  :                  continue ;  // suppress empty components
						case CanonState::Dot    : res.pop_back() ; continue ;  // suppress . components
						case CanonState::DotDot : {
							if (res.size()==2)              break    ;         // keep initial .. , keep it
							if (res.size()==3) { res = {} ; continue ; }       // keep initial /.., suppress it
							size_t slash = res.rfind('/',res.size()-4) ;
							size_t slash1 = slash==Npos ? 0 : slash+1  ;
							if (res.substr(slash1,res.size()-3)=="..") break ; // keep .. after ..
							res = res.substr(0,slash1) ;                       // suppress prev component
							continue ;
						}
						case CanonState::First  :
						case CanonState::Plain  : break ;
					DF}
					state = CanonState::Empty ;
				break ;
				case '.' :
					switch (state) {
						case CanonState::First  :
						case CanonState::Empty  : state = CanonState::Dot    ; break ;
						case CanonState::Dot    : state = CanonState::DotDot ; break ;
						case CanonState::DotDot : state = CanonState::Plain  ; break ;
						case CanonState::Plain  :                              break ;
					DF}
				break ;
				default :
					state = CanonState::Plain ;
			}
			res.push_back(c) ;
		}
		return res ;
	}

	::string mk_lcl( ::string const& file , ::string const& dir_s ) {
		SWEAR( is_dirname(dir_s)             ,        dir_s ) ;
		SWEAR( is_abs(file)==is_abs_s(dir_s) , file , dir_s ) ;
		size_t last_slash1 = 0 ;
		for( size_t i : iota(file.size()) ) {
			if (file[i]!=dir_s[i]) break ;
			if (file[i]=='/'     ) last_slash1 = i+1 ;
		}
		::string res ;
		for( char c : ::string_view(dir_s).substr(last_slash1) ) if (c=='/') res += "../" ;
		res += ::string_view(file).substr(last_slash1) ;
		return res ;
	}

	::string mk_glb( ::string const& file , ::string const& dir_s ) {
		if (is_abs(file)) return file ;
		::string_view d_sv = dir_s ;
		::string_view f_v  = file  ;
		for(; f_v.starts_with("../") ; f_v = f_v.substr(3) ) {
			if (!d_sv) break ;
			d_sv = d_sv.substr(0,d_sv.size()-1) ;                                         // suppress ending /
			size_t last_slash = d_sv.rfind('/') ;
			if (last_slash==Npos) { SWEAR(+d_sv) ; d_sv = d_sv.substr(0,0           ) ; }
			else                  {                d_sv = d_sv.substr(0,last_slash+1) ; } // keep new ending /
		}
		return ""s+d_sv+f_v ;
	}

	::string mk_file( ::string const& f , FileDisplay fd , Bool3 exists ) {
		::string pfx(2+sizeof(FileNameIdx),FileMrkr) ;
		pfx[1] = char(fd) ;
		encode_int<FileNameIdx>(&pfx[2],f.size()) ;
		switch (exists) {
			case Yes : { if (!is_target(f)) return "(not existing) "+pfx+f ; } break ;
			case No  : { if ( is_target(f)) return "(existing) "    +pfx+f ; } break ;
		DN}
		return pfx+f ;
	}

	::string _localize( ::string const& txt , ::string const& dir_s , size_t first_file ) {
		size_t   pos = first_file        ;
		::string res = txt.substr(0,pos) ;
		while (pos!=Npos) {
			/**/                 pos++    ;                                                     // clobber marker
			FileDisplay fd = FileDisplay(txt[pos++]) ;
			SWEAR(txt.size()>=pos+sizeof(FileNameIdx)) ;                                        // ensure we have enough room to find file length
			FileNameIdx len = decode_int<FileNameIdx>(&txt[pos]) ; pos += sizeof(FileNameIdx) ;
			SWEAR(txt.size()>=pos+len) ;                                                        // ensure we have enough room to read file
			switch (fd) {
				case FileDisplay::None      : res +=              mk_rel(txt.substr(pos,len),dir_s)  ; break ;
				case FileDisplay::Printable : res += mk_printable(mk_rel(txt.substr(pos,len),dir_s)) ; break ;
				case FileDisplay::Shell     : res += mk_shell_str(mk_rel(txt.substr(pos,len),dir_s)) ; break ;
				case FileDisplay::Py        : res += mk_py_str   (mk_rel(txt.substr(pos,len),dir_s)) ; break ;
				case FileDisplay::Json      : res += mk_json_str (mk_rel(txt.substr(pos,len),dir_s)) ; break ;
			}
			pos += len ;
			size_t new_pos = txt.find(FileMrkr,pos) ;
			res += txt.substr(pos,new_pos-pos) ;
			pos  = new_pos ;
		}
		return res ;
	}

	//
	// disk access library
	//

	vector_s lst_dir_s( Fd at , ::string const& dir_s , ::string const& prefix ) {
		Fd dir_fd { at , dir_s , Fd::Dir } ;
		if (!dir_fd) throw "cannot open dir "+(at==Fd::Cwd?""s:"@"s+at.fd+':')+dir_s+" : "+::strerror(errno) ;
		//
		DIR* dir_fp = ::fdopendir(dir_fd) ;
		if (!dir_fp) throw "cannot list dir "+(at==Fd::Cwd?""s:"@"s+at.fd+':')+dir_s+" : "+::strerror(errno) ;
		//
		::vector_s res ;
		while ( struct dirent* entry = ::readdir(dir_fp) ) {
			if (entry->d_name[0]!='.') goto Ok  ;
			if (entry->d_name[1]==0  ) continue ; // ignore .
			if (entry->d_name[1]!='.') goto Ok  ;
			if (entry->d_name[2]==0  ) continue ; // ignore ..
		Ok :
			res.emplace_back(prefix+entry->d_name) ;
		}
		if (dir_fd!=at) ::closedir(dir_fp) ;
		return res ;
	}

	void unlnk_inside_s( Fd at , ::string const& dir_s , bool abs_ok , bool force ) {
		if (!abs_ok) SWEAR( is_lcl_s(dir_s) , dir_s ) ;                                                                                  // unless certain, prevent accidental non-local unlinks
		if (force) [[maybe_unused]] int _ = ::fchmodat( at , no_slash(dir_s).c_str() , S_IRWXU|S_IRWXG|S_IRWXO , AT_SYMLINK_NOFOLLOW ) ; // ignore return code as we cannot do much about it
		for( ::string const& f : lst_dir_s(at,dir_s,dir_s) ) unlnk(at,f,true/*dir_ok*/,abs_ok,force) ;
	}

	bool/*done*/ unlnk( Fd at , ::string const& file , bool dir_ok , bool abs_ok , bool force ) {
		/**/         SWEAR( +file || at!=Fd::Cwd  , file,at,abs_ok ) ;                            // do not unlink cwd
		if (!abs_ok) SWEAR( !file || is_lcl(file) , file           ) ;                            // unless certain, prevent accidental non-local unlinks
		if (::unlinkat(at,file.c_str(),0)==0) return true /*done*/ ;
		if (errno==ENOENT                   ) return false/*done*/ ;
		throw_unless( dir_ok        , "cannot unlink "     ,file ) ;
		throw_unless( errno==EISDIR , "cannot unlink file ",file ) ;
		//
		unlnk_inside_s(at,with_slash(file),abs_ok,force) ;
		//
		if (::unlinkat(at,file.c_str(),AT_REMOVEDIR)<0) throw "cannot unlink dir " +file ;
		return true/*done*/ ;
	}

	bool can_uniquify( Fd at , ::string const& file ) { // return true is file is a candidate to be uniquified, i.e. if it has several links to it
		SWEAR(+file) ;                                  // cannot unlink at without file
		struct ::stat st ;
		int           rc = ::fstatat(at,file.c_str(),&st,AT_SYMLINK_NOFOLLOW) ;
		return rc==0 && st.st_nlink>1 ;
	}

	bool/*done*/ uniquify( Fd at , ::string const& file ) {                                      // uniquify file so as to ensure modifications do not alter other hard links
		SWEAR(+file) ;                                                                           // cannot unlink at without file
		const char*   f   = file.c_str() ;
		const char*   msg = nullptr      ;
		{
			struct ::stat st  ;
			int           src = ::fstatat(at,f,&st,AT_SYMLINK_NOFOLLOW)            ; if (src!=0        )                                     return false/*done*/ ;
			/**/                                                                     if (st.st_nlink<=1)                                     return false/*done*/ ;
			AcFd          rfd = ::openat  (at,f,O_RDONLY|O_NOFOLLOW)               ; if (!rfd          ) { msg = "cannot open for reading" ; goto Bad             ; }
			int           urc = ::unlinkat(at,f,0)                                 ; if (urc!=0        ) { msg = "cannot unlink"           ; goto Bad             ; }
			AcFd          wfd = ::openat  (at,f,O_WRONLY|O_CREAT,st.st_mode&07777) ; if (!wfd          ) { msg = "cannot open for writing" ; goto Bad             ; }
			//
			for(;;) {
				char    buf[4096] ;
				ssize_t cnt       = ::read( rfd , buf , sizeof(buf) ) ;
				if (cnt==0) break ;
				if (cnt<0 ) throw "cannot read "+file ;
				wfd.write({buf,sizeof(buf)}) ;
			}
			struct ::timespec times[2] = { {.tv_sec=0,.tv_nsec=UTIME_OMIT} , st.ST_MTIMESPEC } ;
			::futimens(wfd,times) ;                                                              // maintain original date
			//
			return true/*done*/ ;
		}
	Bad :
		if (at==Fd::Cwd) throw ::string(msg)+' '           +file ;
		else             throw ::string(msg)+" @"+at.fd+':'+file ;
	}

	void rmdir_s( Fd at , ::string const& dir_s ) {
		if (::unlinkat(at,no_slash(dir_s).c_str(),AT_REMOVEDIR)!=0) throw "cannot rmdir "+dir_s ;
	}

	static void _walk( ::vector_s& res , Fd at , ::string const& file , ::string const& prefix ) {
		if (FileInfo(at,file).tag()!=FileTag::Dir) {
			res.push_back(prefix) ;
			return ;
		}
		::vector_s lst    ;
		::string   file_s = with_slash(file) ;
		try                     { lst = lst_dir_s(at,file_s) ; }
		catch (::string const&) { return ;                     } // list only accessible files
		::string prefix_s = prefix+'/' ;
		for( ::string const& f : lst ) _walk( res , at,file_s+f , prefix_s+f ) ;
	}
	::vector_s walk( Fd at , ::string const& file , ::string const& prefix ) {
		::vector_s res ;
		_walk( res , at , file , prefix ) ;
		return res ;
	}

	static size_t/*pos*/ _mk_dir_s( Fd at , ::string const& dir_s , NfsGuard* nfs_guard , bool unlnk_ok ) {
		::vector_s  to_mk_s { dir_s }              ;
		const char* msg     = nullptr              ;
		size_t      res     = dir_s[0]=='/'?0:Npos ;                                                           // return the pos of the / between existing and new components
		while (+to_mk_s) {
			::string const& d_s = to_mk_s.back() ;                                                             // parents are after children in to_mk
			if (nfs_guard) { SWEAR(at==Fd::Cwd) ; nfs_guard->change(d_s) ; }
			if (::mkdirat(at,no_slash(d_s).c_str(),0777)==0) {
				res++ ;
				to_mk_s.pop_back() ;
				continue ;
			}                                                                                                  // done
			switch (errno) {
				case EEXIST :
					if ( unlnk_ok && !is_dir(at,no_slash(d_s)) )   unlnk(at,no_slash(d_s)) ;                   // retry
					else                                         { res = d_s.size()-1 ; to_mk_s.pop_back() ; } // done
				break ;
				case ENOENT  :
				case ENOTDIR :
					if (has_dir(d_s))   to_mk_s.push_back(dir_name_s(d_s)) ;                                   // retry after parent is created
					else              { msg = "cannot create top dir" ; goto Bad ; }                           // if ENOTDIR, a parent is not a dir, it will not be fixed up
				break  ;
				default :
					msg = "cannot create dir" ;
				Bad :
					if (at==Fd::Cwd) throw ""s+msg+' '           +no_slash(d_s) ;
					else             throw ""s+msg+" @"+at.fd+':'+no_slash(d_s) ;
			}
		}
		return res ;
	}
	size_t/*pos*/ mk_dir_s( Fd at , ::string const& dir_s ,                       bool unlnk_ok ) { return _mk_dir_s(at,dir_s,nullptr   ,unlnk_ok) ; }
	size_t/*pos*/ mk_dir_s( Fd at , ::string const& dir_s , NfsGuard& nfs_guard , bool unlnk_ok ) { return _mk_dir_s(at,dir_s,&nfs_guard,unlnk_ok) ; }

	void dir_guard( Fd at , ::string const& file ) {
		if (has_dir(file)) mk_dir_s(at,dir_name_s(file)) ;
	}

	FileTag cpy( Fd dst_at , ::string const& dst_file , Fd src_at , ::string const& src_file , bool unlnk_dst , bool mk_read_only ) {
		FileInfo fi { src_at , src_file } ;
		FileTag tag = fi.tag()            ;
		if (unlnk_dst) unlnk(dst_at,dst_file)                                 ;
		else           SWEAR( !is_target(dst_at,dst_file) , dst_at,dst_file ) ;
		switch (tag) {
			case FileTag::None : break ;
			case FileTag::Reg  :
			case FileTag::Exe  : {
				dir_guard(dst_at,dst_file) ;
				AcFd rfd {             src_at , src_file }                                                                                                                             ;
				AcFd wfd { ::openat( dst_at , dst_file.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_TRUNC , 0777 & ~(tag==FileTag::Exe?0000:0111) & ~(mk_read_only?0222:0000) ) } ;
				::sendfile( wfd , rfd , nullptr , fi.sz ) ;
			}
			break ;
			case FileTag::Lnk : {
				::string target = read_lnk(src_at,src_file) ;
				dir_guard(dst_at,dst_file) ;
				lnk( dst_at , dst_file , target ) ;
			}
			break ;
		DF}
		return tag ;
	}

	//
	// FileInfo
	//

	::string& operator+=( ::string& os , FileInfo const& fi ) {
		/**/     os << "FileInfo("           ;
		if (+fi) os << fi.sz <<','<< fi.date ;
		return   os << ')'                   ;
	}

	FileTag FileInfo::_s_tag(Stat const& st) {
		if (S_ISREG(st.st_mode)) {
			if (st.st_mode&S_IXUSR) return FileTag::Exe   ;
			if (!st.st_size       ) return FileTag::Empty ;
			/**/                    return FileTag::Reg   ;
		}
		if (S_ISLNK(st.st_mode)) return FileTag::Lnk  ;
		if (S_ISDIR(st.st_mode)) return FileTag::Dir  ;
		/**/                     return FileTag::None ; // awkward file, ignore
	}

	FileInfo::FileInfo(Stat const& st) {
		FileTag tag = _s_tag(st) ;
		if (tag==FileTag::Dir)   date = Ddate(   tag) ;
		else                   { date = Ddate(st,tag) ; sz = st.st_size ; }
	}

	FileInfo::FileInfo( Fd at , ::string const& name , bool no_follow ) {
		Stat st ;
		if (::fstatat( at , name.c_str() , &st , AT_EMPTY_PATH|(no_follow?AT_SYMLINK_NOFOLLOW:0) )<0) return ;
		self = st ;
	}

	//
	// FileSig
	//

	::string& operator+=( ::string& os , FileSig const& sig ) {
		return os<< "FileSig(" << to_hex(sig._val>>NBits<FileTag>) <<':'<< sig.tag() <<')' ;
	}

	FileSig::FileSig( FileInfo const& fi ) {
		_val = +fi.tag() ;
		if (!fi) return ;
		Hash::Xxh h ;
		h.update(fi.date) ;
		h.update(fi.sz  ) ;
		_val |= (+h.digest()<<NBits<FileTag>) ;
	}

	//
	// SigDate
	//

	::string& operator+=( ::string& os , SigDate const& sd ) { return os <<'('<< sd.sig <<','<< sd.date <<')' ; }

	//
	// FileMap
	//

	FileMap::FileMap( Fd at , ::string const& filename ) {
		_fd = Fd(at,filename) ;
		if (!_fd) return ;
		sz = FileInfo(_fd,{},false/*no_follow*/).sz ;
		if (sz) {
			data = static_cast<const uint8_t*>(::mmap( nullptr , sz , PROT_READ , MAP_PRIVATE , _fd , 0 )) ;
			if (data==MAP_FAILED) {
				_fd.detach() ;      // report error
				data = nullptr ;    // avoid garbage info
				return ;
			}
		}
		_ok = true ;
	}

	//
	// RealPath
	//

	::string& operator+=( ::string& os , RealPathEnv const& rpe ) {
		/**/                    os << "RealPathEnv(" << rpe.lnk_support ;
		if ( rpe.reliable_dirs) os << ",reliable_dirs"                  ;
		/**/                    os <<','<< rpe.repo_root_s              ;
		if (+rpe.tmp_dir_s    ) os <<','<< rpe.tmp_dir_s                ;
		if (+rpe.src_dirs_s   ) os <<','<< rpe.src_dirs_s               ;
		return                  os <<')'                                ;
	}

	::string& operator+=( ::string& os , RealPath::SolveReport const& sr ) {
		return os << "SolveReport(" << sr.real <<','<< sr.file_loc <<','<< sr.lnks <<')' ;
	}

	::string& operator+=( ::string& os , RealPath const& rp ) {
		/**/                     os << "RealPath("             ;
		if (+rp.pid            ) os << rp.pid <<','            ;
		/**/                     os <<      rp._cwd            ;
		/**/                     os <<','<< rp._admin_dir      ;
		if (+rp._abs_src_dirs_s) os <<','<< rp._abs_src_dirs_s ;
		return                   os <<')'                      ;
	}

	// /!\ : this code must be in sync with RealPath::solve
	FileLoc RealPathEnv::file_loc(::string const& real) const {
		::string abs_real   = mk_abs(real,repo_root_s) ;
		if (abs_real.starts_with(tmp_dir_s )) return FileLoc::Tmp  ;
		if (abs_real.starts_with("/proc/"  )) return FileLoc::Proc ;
		if (abs_real.starts_with(repo_root_s)) {
			if ((mk_lcl(abs_real,repo_root_s)+'/').starts_with(AdminDirS)) return FileLoc::Admin ;
			else                                                           return FileLoc::Repo  ;
		} else {
			::string lcl_real = mk_lcl(real,repo_root_s) ;
			for( ::string const& sd_s : src_dirs_s )
				if ((is_abs_s(sd_s)?abs_real:lcl_real).starts_with(sd_s)) return FileLoc::SrcDir ;
			return FileLoc::Ext ;
		}
	}

	void RealPath::_Dvg::update( ::string const& domain , ::string const& chk ) {
		size_t start = dvg ;
		ok  = domain.size() <= chk.size()     ;
		dvg = ok ? domain.size() : chk.size() ;
		if (start<dvg)
			for( size_t i : iota(start,dvg) )
				if (domain[i]!=chk[i]) {
					ok  = false ;
					dvg = i     ;
					return ;
				}
		if ( domain.size() < chk.size() ) ok = chk[domain.size()]=='/' ;
	}

	RealPath::RealPath( RealPathEnv const& rpe , pid_t p ) {
		SWEAR( is_abs(rpe.repo_root_s) , rpe.repo_root_s ) ;
		SWEAR( is_abs(rpe.tmp_dir_s  ) , rpe.tmp_dir_s   ) ;
		//
		pid           = p                                   ;
		_env          = &rpe                                ;
		_admin_dir    = no_slash(rpe.repo_root_s+AdminDirS) ;
		_repo_root_sz = _env->repo_root_s.size()            ;
		//
		chdir() ; // initialize _cwd
		//
		for ( ::string const& sd_s : rpe.src_dirs_s ) _abs_src_dirs_s.push_back(mk_glb(sd_s,rpe.repo_root_s)) ;
	}

	size_t RealPath::_find_src_idx(::string const& real) const {
		for( size_t i : iota(_abs_src_dirs_s.size()) ) if (real.starts_with(_abs_src_dirs_s[i])) return i    ;
		/**/                                                                                     return Npos ;
	}

	// strong performance efforts have been made :
	// - avoid ::string copying as much as possible
	// - do not support links outside repo & tmp, except from /proc (which is meaningful)
	// - note that besides syscalls, this algo is very fast and caching intermediate results could degrade performances (checking the cache could take as long as doing the job)
	static int _get_symloop_max() {            // max number of links to follow before decreting it is a loop
		int res = ::sysconf(_SC_SYMLOOP_MAX) ;
		if (res>=0) return res                ;
		else        return _POSIX_SYMLOOP_MAX ;
	}
	RealPath::SolveReport RealPath::solve( Fd at , ::string_view  file , bool no_follow ) {
		static ::string const s_proc       = "/proc"            ;
		static int      const s_n_max_lnks = _get_symloop_max() ;
		//
		::vector_s lnks ;
		//
		::string      local_file[2] ;          // ping-pong used to keep a copy of input file if we must modify it (avoid upfront copy as it is rarely necessary)
		bool          ping          = false/*garbage*/                                 ; // ping-pong state
		bool          exists        = true                                             ; // if false, we have seen a non-existent component and there cannot be symlinks within it
		::string_view cur           = file                                             ; // points to the current file : input file or local copy local_file
		size_t        pos           = file[0]=='/'                                     ;
		::string      repo_root     =                  no_slash(_env->repo_root_s)     ;
		::string      tmp_dir       = +_env->tmp_dir_s?no_slash(_env->tmp_dir_s  ):""s ;
		::string      real          ; real.reserve(file.size()) ;                        // canonical (link free, absolute, no ., .. nor empty component). Empty instead of '/'. Anticipate no link
		if (!pos) {                                                                      // file is relative, meaning relative to at
			if      (at==Fd::Cwd) real = cwd()                                 ;
			else if (pid        ) real = read_lnk(s_proc+'/'+pid+"/fd/"+at.fd) ;
			else                  real = read_lnk(s_proc+"/self/fd/"   +at.fd) ;
			//
			if (!is_abs(real) ) return {} ;                                              // user code might use the strangest at, it will be an error but we must support it
			if (real.size()==1) real.clear() ;                                           // if '/', we must substitute the empty string to enforce invariant
		}
		_Dvg in_repo   { repo_root  , real }         ;                                   // keep track of where we are w.r.t. repo       , track symlinks according to lnk_support policy
		_Dvg in_tmp    { tmp_dir    , real }         ;                                   // keep track of where we are w.r.t. tmp        , always track symlinks
		_Dvg in_admin  { _admin_dir , real }         ;                                   // keep track of where we are w.r.t. repo/LMAKE , never track symlinks, like files in no domain
		_Dvg in_proc   { s_proc     , real }         ;                                   // keep track of where we are w.r.t. /proc      , always track symlinks
		bool is_in_tmp = +_env->tmp_dir_s && +in_tmp ;
		// loop INVARIANT : accessed file is real+'/'+cur.substr(pos)
		// when pos>cur.size(), we are done and result is real
		size_t   end       ;
		int      n_lnks    = 0 ;
		::string last_lnk  ;
		for (
		;	pos <= cur.size()
		;		pos = end+1
			,	in_repo.update(repo_root,real)                                           // for all domains except admin, they start only when inside, i.e. the domain root is not part of the domain
			,	in_tmp .update(tmp_dir  ,real)                                           // .
			,	in_proc.update(s_proc   ,real)                                           // .
			,	is_in_tmp = +_env->tmp_dir_s && +in_tmp
		) {
			end = cur.find( '/', pos ) ;
			bool last = end==Npos ;
			if (last    ) end = cur.size() ;
			if (end==pos) continue ;                                                     // empty component, ignore
			if (cur[pos]=='.') {
				if ( end==pos+1                    ) continue ;                          // component is .
				if ( end==pos+2 && cur[pos+1]=='.' ) {                                   // component is ..
					if (+real) real.resize(real.rfind('/')) ;
					continue ;
				}
			}
			size_t prev_real_size = real.size() ;
			real.push_back('/') ;
			real.append(cur,pos,end-pos) ;
			in_admin.update(_admin_dir,real) ;                                           // for the admin domain, it starts at itself, i.e. the admin dir is part of the domain
			if ( !exists           ) continue       ;                                    // if !exists, no hope to find a symbolic link but continue cleanup of empty, . and .. components
			if ( no_follow && last ) continue       ;                                    // dont care about last component if no_follow
			if ( is_in_tmp         ) goto HandleLnk ;                                    // note that tmp can lie within repo or admin
			if ( +in_admin         ) continue       ;
			if ( +in_proc          ) goto HandleLnk ;
			if ( !in_repo          ) continue       ;
			//
			if ( !last && !_env->reliable_dirs )                                                        // at last level, dirs are rare and NFS does the coherence job
				if ( +AcFd(::open(real.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NOATIME)) ) continue ; // sym links are rare, so this has no significant perf impact ...
			//
			switch (_env->lnk_support) {
				case LnkSupport::None :                                 continue ;
				case LnkSupport::File : if (last) goto HandleLnk ; else continue ;                      // only handle sym links as last component
				case LnkSupport::Full :           goto HandleLnk ;
			DF}
		HandleLnk :
			::string& nxt = local_file[(ping=!ping)] ;                                                  // bounce, initially, when cur is neither local_file's, any buffer is ok
			nxt = read_lnk(real) ;                                                                      // XXX : optimize by leveraging dir fd computed on previous loop
			if ( !is_in_tmp && !in_proc ) {
				if (+in_repo) {
					if      ( real.size()<_repo_root_sz              ) continue ;                                                                         // at repo root, no sym link to handle
					else if ( +nxt                                   ) lnks.push_back(real.substr(_repo_root_sz)) ;
				} else {
					if      ( size_t i=_find_src_idx(real) ; i==Npos ) continue ;
					else if (                                +nxt    ) lnks.push_back( _env->src_dirs_s[i] + (real.c_str()+_abs_src_dirs_s[i].size()) ) ; // real lie in a source dir
				}
			}
			if (!nxt) {
				if (errno==ENOENT) exists = false ;
				// do not generate dep for intermediate dir that are not links as we indirectly depend on them through the last components
				// for example if a/b/c is a link to d/e and we access a/b/c/f, we generate the link a/b/c :
				// - a & a/b will be indirectly depended on through a/b/c
				// - d & d/e will be indrectly depended on through caller depending on d/e/f (the real accessed file returned as the result)
				continue ;
			}
			if (n_lnks++>=s_n_max_lnks) return {{},::move(lnks)} ; // link loop detected, same check as system
			if (!last) {                                           // append unprocessed part
				nxt.push_back('/'      ) ;
				nxt.append   (cur,end+1) ;                         // avoiding this copy is very complex (would require to manage a stack) and links to dir are uncommon
			}
			if (nxt[0]=='/') { end =  0 ; prev_real_size = 0 ; }   // absolute link target : flush real
			else               end = -1 ;                          // end must point to the /, invent a virtual one before the string
			real.resize(prev_real_size) ;                          // links are relative to containing dir, suppress last component
			cur = nxt ;
		}
		// admin is in repo, tmp might be, repo root is in_repo
		if (is_in_tmp) //!                                                                                                                     file_accessed
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No        , FileLoc::Tmp    } ;
		if (+in_proc)
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No        , FileLoc::Proc   } ;
		if (+in_repo) {
			if (real.size()<_repo_root_sz                                                               ) return { ::move(real) , ::move(lnks) , No        , FileLoc::Ext    } ;
			real = real.substr(_repo_root_sz) ;
			if ( +in_admin                                                                              ) return { ::move(real) , ::move(lnks) , No        , FileLoc::Admin  } ;
			if ( _env->lnk_support>=LnkSupport::File && !no_follow                                      ) return { ::move(real) , ::move(lnks) , Yes       , FileLoc::Repo   } ;
			if ( _env->lnk_support>=LnkSupport::Full && real.find('/')!=Npos                            ) return { ::move(real) , ::move(lnks) , Maybe     , FileLoc::Repo   } ;
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No        , FileLoc::Repo   } ;
		}
		SWEAR(!in_admin) ;
		if ( size_t i=_find_src_idx(real) ; i!=Npos ) {
			real = _env->src_dirs_s[i] + (real.c_str()+_abs_src_dirs_s[i].size()) ;
			if ( _env->lnk_support>=LnkSupport::File && !no_follow                                      ) return { ::move(real) , ::move(lnks) , Yes       , FileLoc::SrcDir } ;
			if ( _env->lnk_support>=LnkSupport::Full && real.find('/',_env->src_dirs_s[i].size())!=Npos ) return { ::move(real) , ::move(lnks) , Maybe     , FileLoc::SrcDir } ;
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No        , FileLoc::SrcDir } ;
		}
		{
			/**/                                                                                          return { ::move(real) , ::move(lnks) , No        , FileLoc::Ext    } ;
		}
	}

	::vmap_s<Accesses> RealPath::exec(SolveReport& sr) {
		::vmap_s<Accesses> res         ;
		// from tmp, we can go back to repo
		for( int i=0 ; i<=4 ; i++ ) {                                                        // interpret #!<interpreter> recursively (4 levels as per man execve)
			for( ::string& l : sr.lnks ) res.emplace_back(::move(l),Accesses(Access::Lnk)) ;
			//
			if (sr.file_loc>FileLoc::Dep && sr.file_loc!=FileLoc::Tmp) break ;               // if we escaped from the repo, there is no more deps to gather
			//
			Accesses a = Access::Reg ; if (sr.file_accessed==Yes) a |= Access::Lnk ;
			if (sr.file_loc<=FileLoc::Dep) res.emplace_back(sr.real,a) ;
			//
			AcFd     hdr_fd { mk_abs(sr.real,_env->repo_root_s) }  ; if (!hdr_fd               ) break ;
			::string hdr    = hdr_fd.read(false/*no_file_ok*/,256) ; if (!hdr.starts_with("#!")) break ;
			size_t   pos    = hdr.find('\n')                       ; if (pos!=Npos             ) hdr.resize(pos) ;
			/**/     pos    = hdr.find(' ' )                       ; if (pos==0                ) break ;
			// recurse
			sr = solve( hdr.substr(2,pos-2) , false/*no_follow*/ ) ;                         // interpreter starts after #! until first space or end of line
		}
		return res ;
	}

	void RealPath::chdir() {
		if (pid)   _cwd = read_lnk("/proc/"s+pid+"/cwd") ;
		else     { _cwd = no_slash(cwd_s())              ; _cwd_pid = ::getpid() ; }
	}

}
