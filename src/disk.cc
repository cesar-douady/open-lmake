// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"
#include "process.hh"

namespace Disk {

	//
	// path name library
	//

	// /!\ this code is derived from mk_canon
	bool is_canon( ::string const& path , bool ext_ok , bool empty_ok , bool has_pfx , bool has_sfx ) {
		if (!path) return empty_ok ;
		const char* p = path.data() ;
		//
		auto handle_slash = [&]() {
			size_t sz = p - path.data() ;                         // if test ok, else, [path:p) is (x is single wildcard, u is not ., y is not /, z is not / nor .) :
			if (sz==0     ) return true              ;            //                *x
			if (p[-1]=='/') return false             ;            //      */        *y
			if (p[-1]!='.') return true              ;            //      *z        *.
			if (sz==1     ) return has_pfx           ;            //       .       *x.
			if (p[-2]=='/') return false             ;            //     */.       *y.
			if (p[-2]!='.') return true              ;            //     *z.       *..
			if (sz==2     ) return            ext_ok ;            //      ..      *x..
			if (p[-3]!='/') return true              ;            //    *y..      */..
			if (sz==3     ) return has_pfx && ext_ok ;            //     /..     *x/.. absolute .. is not canon
			if (p[-4]!='.') return false             ;            //   *y/..     *./..
			if (sz==4     ) return has_pfx && ext_ok ;            //    ./..    *x./..
			if (p[-5]!='.') return false             ;            //  *u./..    *../..
			if (sz==5     ) return            ext_ok ;            //   ../..   *x../.. .. after .. is canon
			if (p[-6]=='/') return            ext_ok ;            // */../..   *y../.. .. after .. is canon
			/**/            return false             ;
		} ;
		for(; p!=path.data()+path.size() ; p++ ) {
			char c = *p ;
			throw_if( c==0 , "file contains nul char : ",path ) ; // file names are not supposed to contain any nul char, cannot canonicalize
			if ( c=='/' && !handle_slash() ) return false ;
		}
		return path.back()=='/' || has_sfx || handle_slash() ;
	}

	// /!\ is_canon is derived from this code
	::string mk_canon(::string const& path) {
		if (!path) return {} ;
		//
		::string res    ;                res.reserve(path.size()) ;
		bool     is_abs = path[0]=='/' ;
		//
		auto handle = [&](char c) {
			res.push_back(c) ;
			if (c!='/') return ;
			size_t      sz  = res.size()-1    ;
			const char* end = res.data() + sz ;
			//                                                                  // if test ok, else, res[:-1] is (x is single wildcard, u is not ., y is not /, z is not / nor .) :
			if (sz==0       ) { if (!is_abs) res.resize(sz  ) ; return      ; } //                *x if not abs, it must have been preceded by ./ which has been suppressed, it is an empty component
			if (end[-1]=='/') {              res.resize(sz  ) ; return      ; } //     */         *y suppress empty component
			if (end[-1]!='.')                                   return      ;   //     *z         *.
			if (sz==1       ) {              res.resize(sz-1) ; return      ; } //      .        *x. suppress leading ., if fragment => could be preceded by z, hence . must be kept
			if (end[-2]=='/') {              res.resize(sz-1) ; return      ; } //    */.        *y. suppress internal .
			if (end[-2]!='.')                                   return      ;   //    *z.        *..
			if (sz==2       )                                   return      ;   //     ..       *x..
			if (end[-3]!='/')                                   return      ;   //   *y..       */..
			if (sz==3       ) {              res.resize(sz-2) ; return      ; } //    /..      *x/.. suppress absolute ..
			if (end[-4]!='.')                                   goto DotDot ;   //  *y/..      *./..
			if (sz==4       )                                   goto DotDot ;   //   ./..     *x./..
			if (end[-5]!='.')                                   goto DotDot ;   // *u./..     *../..
			if (sz==5       )                                   return      ;   //  ../..    *x../.. .. after .. is canon
			if (end[-6]=='/')                                   return      ;   // y../..    */../.. .. after .. is canon
		DotDot :
			size_t pos = res.rfind('/',sz-4) ; if (pos==Npos) pos = 0 ; else pos += 1 ; // pos is char after / (or 0 if not found)
			res.resize(pos) ;                                                           // parent dir is plain (not ..), suppress it
		} ;
		for( char c : path ) {
			throw_if( c=='\0' , "file contains nul char : ",path ) ;                    // file names are not supposed to contain any nul char, cannot canonicalize
			handle(c) ;
		}
		if (path.back()!='/') {
			handle('/') ;
			if (+res) res.pop_back (   ) ;
			else      res.push_back('.') ;                                              // /!\ res = "." generates a warning (and hence an error with -Werror) when compiling for 32 bits with gcc-12
		}
		return res ;
	}

	::string _mk_lcl( ::string const& path , ::string const& dir_s , bool path_is_dir ) {
		SWEAR( is_dir_name(dir_s)            ,        dir_s ) ;
		if (path_is_dir) SWEAR( is_abs_s(path)==is_abs_s(dir_s) , path , dir_s ) ;
		else             SWEAR( is_abs  (path)==is_abs_s(dir_s) , path , dir_s ) ;
		size_t last_slash1 = 0 ;
		for( size_t i : iota(path.size()) ) {
			if (path[i]!=dir_s[i]) break ;
			if (path[i]=='/'     ) last_slash1 = i+1 ;
		}
		::string res ;
		for( char c : substr_view(dir_s,last_slash1) ) if (c=='/') res += "../" ;
		res += substr_view(path,last_slash1) ;
		return res ;
	}

	::string _mk_glb( ::string const& path , ::string const& dir_s , bool path_is_dir ) {
		if (path_is_dir) { if (is_abs_s(path)) return path ; }
		else             { if (is_abs  (path)) return path ; }
		::string_view d_sv = dir_s ;
		::string_view f_v  = path  ;
		for(; f_v.starts_with("../") ; f_v.remove_prefix(3) ) {
			if (!d_sv) break ;
			d_sv.remove_suffix(1) ;                                                       // suppress ending /
			size_t last_slash = d_sv.rfind('/') ;
			if (last_slash==Npos) { SWEAR(+d_sv) ; d_sv = {}                          ; }
			else                  {                d_sv = d_sv.substr(0,last_slash+1) ; } // keep new ending /
		}
		return ::string(d_sv)+f_v ;
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
			pos++ ;                                                                                                                                                 // clobber marker
			FileDisplay fd  = FileDisplay(txt[pos++])                                         ; SWEAR( txt.size()>=pos+sizeof(FileNameIdx) , txt.size(),pos     ) ; // ensure can read file length
			FileNameIdx len = decode_int<FileNameIdx>(&txt[pos]) ; pos += sizeof(FileNameIdx) ; SWEAR( txt.size()>=pos+len                 , txt.size(),pos,len ) ; // ensure can read file
			::string    f   = len ? mk_rel(txt.substr(pos,len),dir_s) : ""s                   ;
			switch (fd) {
				case FileDisplay::None      : res +=              f  ; break ;
				case FileDisplay::Printable : res += mk_printable(f) ; break ;
				case FileDisplay::Shell     : res += mk_shell_str(f) ; break ;
				case FileDisplay::Py        : res += mk_py_str   (f) ; break ;
			DF}                                                                                                                                                     // NO_COV
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
		Fd dir_fd { at , dir_s , {.flags=O_RDONLY|O_DIRECTORY} } ;
		//
		DIR* dir_fp = ::fdopendir(dir_fd) ;
		if (!dir_fp) throw cat("cannot list dir ",file_msg(at,dir_s)," : ",StrErr()) ;
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
		if (force) [[maybe_unused]] int _ = ::fchmodat( at , no_empty_s(dir_s).c_str() , S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH , 0 ) ; // best effort
		::string e ;
		for( ::string const& f : lst_dir_s(at,dir_s,dir_s) ) {
			try                        { unlnk(at,f,true/*dir_ok*/,abs_ok,force) ; }                                                     // remove all removable files
			catch (::string const& e2) { e = e2 ;                                  }
		}
		if (+e) throw e ;
	}

	bool/*done*/ unlnk( Fd at , ::string const& file , bool dir_ok , bool abs_ok , bool force ) {
		/**/                                  SWEAR( +file || at!=Fd::Cwd  , file,at,abs_ok ) ;   // do not unlink cwd
		if (!abs_ok                         ) SWEAR( !file || is_lcl(file) , file           ) ;   // unless certain, prevent accidental non-local unlinks
		if (::unlinkat(at,file.c_str(),0)==0) return true /*done*/ ;
		if (errno==ENOENT                   ) return false/*done*/ ;
		//
		if ( !dir_ok || errno!=EISDIR ) throw cat("cannot unlink file ",file_msg(at,file)," : ",StrErr() ) ;
		//
		unlnk_inside_s(at,with_slash(file),abs_ok,force) ;
		//
		if (::unlinkat(at,file.c_str(),AT_REMOVEDIR)!=0) throw "cannot unlink dir "+no_slash(file) ;
		return true/*done*/ ;
	}

	void rmdir_s( Fd at , ::string const& dir_s ) {
		SWEAR(+dir_s) ;
		if (::unlinkat(at,dir_s.c_str(),AT_REMOVEDIR)!=0) throw "cannot rmdir "+dir_s ;
	}

	// path and pfx are restored after execution
	static void _walk( ::vmap_s<FileTag>& res , Fd at , ::string& path , FileTags file_tags , ::string& pfx , ::function<bool(::string const&)> prune ) {
		FileTag tag = FileInfo(at,path).tag() ;
		//
		if (file_tags[tag]   ) res.emplace_back(pfx,tag) ;
		if (tag!=FileTag::Dir) return ;
		//
		path += '/' ;
		::vector_s lst ;
		try                     { lst = lst_dir_s(at,path) ; }
		catch (::string const&) { path.pop_back() ; return ; } // list only accessible files
		pfx += '/' ;
		size_t path_sz = path.size() ;
		size_t pfx_sz  = pfx .size() ;
		for( ::string const& f : lst ) {
			path += f ;
			pfx  += f ;
			_walk( res , at,path , file_tags , pfx , prune ) ;
			path.resize(path_sz) ;
			pfx .resize(pfx_sz ) ;
		}
		path.pop_back() ;
		pfx .pop_back() ;
	}
	::vmap_s<FileTag> walk( Fd at , ::string const& path , FileTags file_tags , ::string const& pfx , ::function<bool(::string const&)> prune ) {
		::vmap_s<FileTag> res ;
		_walk( res , at , ::ref(::copy(path)) , file_tags , ::ref(::copy(pfx)) , prune ) ;
		return res ;
	}

	size_t/*pos*/ _mk_dir_s( Fd at , ::string const& dir_s , NfsGuard* nfs_guard , PermExt perm_ext , bool unlnk_ok ) {
		if (!dir_s) return Npos ;                                                                                       // nothing to create
		//
		::vector_s  to_mk_s { dir_s }              ;
		const char* msg     = nullptr              ;
		size_t      pos     = dir_s[0]=='/'?0:Npos ;                                                                    // return the pos of the / between existing and new components
		while (+to_mk_s) {
			::string const& d_s = to_mk_s.back() ;                                                                      // parents are after children in to_mk
			if (nfs_guard) { SWEAR(at==Fd::Cwd) ; nfs_guard->change(d_s) ; }
			if (::mkdirat(at,d_s.c_str(),0777)==0) {
				if (+perm_ext) {
					static mode_t umask_ = get_umask() ;
					switch (perm_ext) {
						case PermExt::Other : if (!(umask_     )) goto PermOk ; break ;
						case PermExt::Group : if (!(umask_&0770)) goto PermOk ; break ;
					DN}
					//
					FileStat st ; if ( ::fstatat(at,d_s.c_str(),&st,0/*flags*/)!=0 ) throw cat("cannot stat (",StrErr(),") to extend permissions : ",file_msg(at,no_slash(d_s))) ;
					//
					mode_t usr_mod = (st.st_mode>>6)&07 ;
					mode_t new_mod = st.st_mode         ;
					switch (perm_ext) {
						case PermExt::Other : new_mod |= usr_mod    ; [[fallthrough]] ;
						case PermExt::Group : new_mod |= usr_mod<<3 ; break           ;
					DN}
					if (new_mod!=st.st_mode)
						if ( ::fchmodat(at,d_s.c_str(),new_mod,0/*flags*/)!=0 ) throw cat("cannot chmod (",StrErr(),") to extend permissions : ",file_msg(at,no_slash(d_s))) ;
				}
			PermOk :
				pos++ ;
				to_mk_s.pop_back() ;
				continue ;
			}                                                                                                           // done
			switch (errno) {
				case EEXIST :
					if ( unlnk_ok && !is_dir_s(at,d_s) )   unlnk(at,d_s,false/*dir_ok*/,true/*abs_ok*/) ;               // retry
					else                                 { pos = d_s.size()-1 ; to_mk_s.pop_back() ;      }             // done
				break ;
				case ENOENT  :
				case ENOTDIR :
					if (has_dir(d_s))   to_mk_s.push_back(dir_name_s(d_s)) ;                                            // retry after parent is created
					else              { msg = "cannot create top dir" ; goto Bad ; }                                    // if ENOTDIR, a parent is not a dir, it will not be fixed up
				break  ;
				default :
					msg = "cannot create dir" ;
				Bad :
					throw cat(msg,file_msg(at.fd,no_slash(d_s))) ;
			}
		}
		return pos ;
	}

	void mk_dir_empty_s( Fd at , ::string const& dir_s , bool abs_ok ) {
		try                     { unlnk_inside_s(at,dir_s,abs_ok) ; }
		catch (::string const&) { mk_dir_s(at,dir_s)              ; } // ensure tmp dir exists
	}

	FileTag cpy( Fd dst_at , ::string const& dst_file , Fd src_at , ::string const& src_file ) {
		FileInfo fi  { src_at , src_file } ;
		FileTag  tag = fi.tag()            ;
		//
		SWEAR( !is_target(dst_at,dst_file) , dst_at,dst_file ) ;
		//
		switch (tag) {
			case FileTag::None  :
			case FileTag::Dir   : break ;    // dirs are like no file
			case FileTag::Empty :            // fast path : no need to access empty src
				dir_guard(dst_at,dst_file) ;
				AcFd( dst_at , dst_file , {.flags=O_WRONLY|O_TRUNC|O_CREAT|O_NOFOLLOW,.mod=0666} ) ;
			break ;
			case FileTag::Reg :
			case FileTag::Exe : {
				dir_guard(dst_at,dst_file) ;
				AcFd rfd { src_at , src_file                                                                                             } ;
				AcFd wfd { dst_at , dst_file , { .flags=O_WRONLY|O_TRUNC|O_CREAT|O_NOFOLLOW , .mod=mode_t(tag==FileTag::Exe?0777:0666) } } ;
				int  rc  = ::sendfile( wfd , rfd , nullptr/*offset*/ , fi.sz ) ; if (rc!=0) throw cat("cannot copy ",file_msg(src_at,src_file)," to ",file_msg(dst_at,dst_file)) ;
			} break ;
			case FileTag::Lnk :
				dir_guard(dst_at,dst_file) ;
				lnk( dst_at , dst_file , read_lnk(src_at,src_file) ) ;
			break ;
		DF}                                  // NO_COV
		return tag ;
	}

	//
	// FileInfo
	//

	::string& operator+=( ::string& os , FileInfo const& fi ) { // START_OF_NO_COV
		/**/     os << "FileInfo("           ;
		if (+fi) os << fi.sz <<','<< fi.date ;
		return   os << ')'                   ;
	}                                                           // END_OF_NO_COV

	FileTag FileInfo::_s_tag(FileStat const& st) {
		if (S_ISREG(st.st_mode)) {
			if (st.st_mode&S_IXUSR) return FileTag::Exe   ;
			if (!st.st_size       ) return FileTag::Empty ;
			/**/                    return FileTag::Reg   ;
		}
		if (S_ISLNK(st.st_mode)) return FileTag::Lnk  ;
		if (S_ISDIR(st.st_mode)) return FileTag::Dir  ;
		/**/                     return FileTag::None ; // awkward file, ignore
	}

	FileInfo::FileInfo(FileStat const& st) {
		FileTag tag = _s_tag(st) ;
		switch (tag) {
			case FileTag::None :                                          break ;
			case FileTag::Dir  : date = Ddate(   tag) ;                   break ;
			default            : date = Ddate(st,tag) ; sz = st.st_size ; break ;
		}
	}

	FileInfo::FileInfo( Fd at , ::string const& name , bool no_follow ) {
		FileStat st ;
		if (+name) { if (::fstatat( at , name.c_str() , &st , no_follow?AT_SYMLINK_NOFOLLOW:0 )!=0) return ; }
		else       { if (::fstat  ( at ,                &st                                   )!=0) return ; }
		self = st ;
	}

	//
	// FileSig
	//

	::string& operator+=( ::string& os , FileSig const& sig ) {                              // START_OF_NO_COV
		return os<< "FileSig(" << to_hex(sig._val>>NBits<FileTag>) <<':'<< sig.tag() <<')' ;
	}                                                                                        // END_OF_NO_COV

	// START_OF_VERSIONING
	FileSig::FileSig(FileInfo const& fi) : FileSig{fi.tag()} {
		switch (fi.tag()) {
			case FileTag::None    :
			case FileTag::Unknown :
			case FileTag::Dir     :                                                                break ;
			case FileTag::Empty   : _val |= fi.date.val() & msb_msk<Ddate::Tick>(NBits<FileTag>) ; break ; // improve traceability when size is predictible, 8ns granularity is more than enough
			case FileTag::Lnk     :
			case FileTag::Reg     :
			case FileTag::Exe     : {
				Hash::Xxh h ;
				h    += fi.date                       ;
				h    += fi.sz                         ;
				_val |= +h.digest() << NBits<FileTag> ;
			} break ;
		DF}
	}
	// END_OF_VERSIONING

	//
	// SigDate
	//

	::string& operator+=( ::string& os , SigDate const& sd ) { // START_OF_NO_COV
		return os <<'('<< sd.sig <<','<< sd.date <<')' ;
	}                                                          // END_OF_NO_COV

	//
	// FileMap
	//

	FileMap::FileMap( Fd at , ::string const& file_name ) {
		_fd = Fd( at , file_name , true/*err_ok*/ ) ;
		if (!_fd) return ;
		sz = FileInfo(_fd,{},false/*no_follow*/).sz ;
		if (sz) {
			data = static_cast<const uint8_t*>(::mmap( nullptr/*addr*/ , sz , PROT_READ , MAP_PRIVATE , _fd , 0/*offset*/ )) ;
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

	::string& operator+=( ::string& os , RealPathEnv const& rpe ) {    // START_OF_NO_COV
		/**/                 os << "RealPathEnv(" << rpe.lnk_support ;
		if (+rpe.file_sync ) os <<','<< rpe.file_sync                ;
		/**/                 os <<','<< rpe.repo_root_s              ;
		if (+rpe.tmp_dir_s ) os <<','<< rpe.tmp_dir_s                ;
		if (+rpe.src_dirs_s) os <<','<< rpe.src_dirs_s               ;
		return               os <<')'                                ;
	}                                                                  // END_OF_NO_COV

	::string& operator+=( ::string& os , RealPath::SolveReport const& sr ) {               // START_OF_NO_COV
		return os << "SolveReport(" << sr.real <<','<< sr.file_loc <<','<< sr.lnks <<')' ;
	}                                                                                      // END_OF_NO_COV

	::string& operator+=( ::string& os , RealPath const& rp ) {  // START_OF_NO_COV
		/**/                     os << "RealPath("             ;
		if (+rp.pid            ) os << rp.pid <<','            ;
		/**/                     os <<      rp._cwd            ;
		if (+rp._abs_src_dirs_s) os <<','<< rp._abs_src_dirs_s ;
		return                   os <<')'                      ;
	}                                                            // END_OF_NO_COV

	static FileLoc _lcl_file_loc(::string_view file) {
		static ::string_view s_private_admin_dir { PrivateAdminDirS , ::strlen(PrivateAdminDirS)-1 } ; // get rid of final /
		if (!file.starts_with(s_private_admin_dir)) return FileLoc::Repo ;
		switch (file[s_private_admin_dir.size()]) {
			case '/' :
			case 0   : return FileLoc::Admin ;
			default  : return FileLoc::Repo  ;
		}
	}

	// /!\ : this code must be in sync with RealPath::solve
	FileLoc RealPathEnv::file_loc(::string const& real) const {
		::string abs_real = mk_glb(real,repo_root_s) ;
		if (abs_real.starts_with(tmp_dir_s                                      )) return FileLoc::Tmp  ;
		if (abs_real.starts_with("/proc/"                                       )) return FileLoc::Proc ;
		if (abs_real.starts_with(substr_view(repo_root_s,0,repo_root_s.size()-1)))
			switch (abs_real[repo_root_s.size()-1]) {
				case 0   : return FileLoc::RepoRoot                                       ;
				case '/' : return _lcl_file_loc(substr_view(abs_real,repo_root_s.size())) ;
			DN}
		::string lcl_real = mk_lcl(real,repo_root_s) ;
		for( ::string const& sd_s : src_dirs_s )
			if ((is_abs_s(sd_s)?abs_real:lcl_real).starts_with(sd_s)) return FileLoc::SrcDir ;
		return FileLoc::Ext ;
	}

	void RealPathEnv::chk(bool for_cache) const {
		/**/                                     throw_unless( lnk_support<All<LnkSupport>             , "bad link_support" ) ;
		/**/                                     throw_unless( file_sync  <All<FileSync  >             , "bad file_sync"    ) ;
		/**/                                     throw_unless( !tmp_dir_s   || tmp_dir_s  .back()=='/' , "bad tmp_dir"      ) ;
		for( ::string const& sd_s : src_dirs_s ) throw_unless( +sd_s        && sd_s       .back()=='/' , "bad src dir"      ) ;
		//
		if (for_cache) throw_unless( !repo_root_s                            , "bad repo_root" ) ;
		else           throw_unless( !repo_root_s || repo_root_s.back()=='/' , "bad repo_root" ) ;
	}

	void RealPath::_Dvg::update( ::string_view domain_s , ::string const& chk ) {
		if (!domain_s) return ;                                                   // always false
		SWEAR( domain_s.back()=='/' , domain_s ) ;
		size_t ds    = domain_s.size()-1 ;                                        // do not account for terminating /
		size_t start = dvg               ;
		ok  = ds <= chk.size()     ;
		dvg = ok ? ds : chk.size() ;
		if (start<dvg)
			for( size_t i : iota(start,dvg) )
				if (domain_s[i]!=chk[i]) {
					ok  = false ;
					dvg = i     ;
					return ;
				}
		if (ds<chk.size()) ok = chk[ds]=='/' ;
	}

	RealPath::RealPath( RealPathEnv const& rpe , pid_t p ) : pid{p} , _env{&rpe} , _repo_root_sz{_env->repo_root_s.size()} , _nfs_guard{rpe.file_sync} {
		/**/                SWEAR( is_abs_s(rpe.repo_root_s) , rpe.repo_root_s ) ;
		if (+rpe.tmp_dir_s) SWEAR( is_abs_s(rpe.tmp_dir_s  ) , rpe.tmp_dir_s   ) ;
		//
		chdir() ; // initialize _cwd
		//
		for ( ::string const& sd_s : rpe.src_dirs_s ) _abs_src_dirs_s.push_back(mk_glb_s(sd_s,rpe.repo_root_s)) ;
	}

	size_t RealPath::_find_src_idx(::string const& real) const {
		for( size_t i : iota(_abs_src_dirs_s.size()) ) if (real.starts_with(_abs_src_dirs_s[i])) return i    ;
		/**/                                                                                     return Npos ;
	}

	// strong performance efforts have been made :
	// - avoid ::string copying as much as possible
	// - do not support links outside repo & tmp, except from /proc (which is meaningful)
	// - note that besides syscalls, this algo is very fast and caching intermediate results could degrade performances (checking the cache could take as long as doing the job)
	RealPath::SolveReport RealPath::solve( Fd at , ::string_view file , bool no_follow ) {
		static constexpr int NMaxLnks = _POSIX_SYMLOOP_MAX ;                               // max number of links to follow before decreting it is a loop
		//
		::string_view tmp_dir_s = +_env->tmp_dir_s ? ::string_view(_env->tmp_dir_s) : ::string_view(P_tmpdir "/") ;
		//
		SolveReport res           ;
		::vector_s& lnks          = res.lnks              ;
		::string  & real          = res.real              ;             // canonical (link free, absolute, no ., .. nor empty component), empty instead of '/'
		::string    local_file[2] ;                                     // ping-pong used to keep a copy of input file if we must modify it (avoid upfront copy as it is rarely necessary)
		bool        ping          = false/*garbage*/      ;             // ping-pong state
		bool        exists        = true                  ;             // if false, we have seen a non-existent component and there cannot be symlinks within it
		size_t      pos           = +file && file[0]=='/' ;
		if (!pos) {                                                     // file is relative, meaning relative to at
			real = at_file(at) ;
			//
			if (!real         ) return {} ;                             // user code might use the strangest at, it will be an error but we must support it
			if (real.size()==1) real.clear() ;                          // if '/', we must substitute the empty string to enforce invariant
		}
		real.reserve(real.size()+1+file.size()) ;                       // anticipate no link
		_Dvg in_repo { _env->repo_root_s , real } ;                     // keep track of where we are w.r.t. repo , track symlinks according to lnk_support policy
		_Dvg in_tmp  { tmp_dir_s         , real } ;                     // keep track of where we are w.r.t. tmp  , always track symlinks
		_Dvg in_proc { "/proc/"          , real } ;                     // keep track of where we are w.r.t. /proc, always track symlinks
		// loop INVARIANT : accessed file is real+'/'+file.substr(pos)
		// when pos>file.size(), we are done and result is real
		size_t   end      ;
		int      n_lnks   = 0 ;
		::string last_lnk ;
		for (
		;	pos <= file.size()
		;		pos = end + 1
			,	in_repo.update( _env->repo_root_s , real )              // all domains start only when inside, i.e. the domain root is not part of the domain
			,	in_tmp .update( tmp_dir_s         , real )              // .
			,	in_proc.update( "/proc/"          , real )              // .
		) {
			end = file.find( '/' , pos ) ;
			bool last = end==Npos ;
			if (last    ) end = file.size() ;
			if (end==pos) continue ;                                    // empty component, ignore
			if (file[pos]=='.') {
				if ( end==pos+1                     ) continue ;        // component is .
				if ( end==pos+2 && file[pos+1]=='.' ) {                 // component is ..
					if (+real) real.resize(real.rfind('/')) ;
					continue ;
				}
			}
			size_t prev_real_size = real.size() ;
			size_t src_idx        = Npos        ;                       // Npos means not in a source dir
			real.push_back('/')           ;
			real.append(file,pos,end-pos) ;
			if ( !exists             ) continue       ;                 // if !exists, no hope to find a symbolic link but continue cleanup of empty, . and .. components
			if ( no_follow && last   ) continue       ;                 // dont care about last component if no_follow
			if ( +in_tmp || +in_proc ) goto HandleLnk ;                 // note that tmp can lie within repo
			if ( +in_repo            ) {
				if (real.size()<_repo_root_sz) continue ;               // at repo root, no sym link to handle
			} else {
				src_idx = _find_src_idx(real) ;
				if (src_idx==Npos) continue ;
			}
			//
			switch (_env->lnk_support) {
				case LnkSupport::None :            continue ;
				case LnkSupport::File : if (!last) continue ;           // only handle sym links as last component
			DN}                                                         // NO_COV
		HandleLnk :
			::string& nxt = local_file[ping] ;                          // bounce, initially, when file is neither local_file's, any buffer is ok
			nxt = read_lnk(_nfs_guard.access(real)) ;
			if (!nxt) {
				if (errno==ENOENT) exists = false ;
				// do not generate dep for intermediate dir that are not links as we indirectly depend on them through the last components
				// for example if a/b/c is a link to d/e and we access a/b/c/f, we generate the link a/b/c :
				// - a & a/b will be indirectly depended on through a/b/c
				// - d & d/e will be indrectly depended on through caller depending on d/e/f (the real accessed file returned as the result)
				continue ;
			}
			if ( !in_tmp && !in_proc ) {
				// if not in repo, real lie in a source dir
				if      (!in_repo                                                    ) lnks.push_back( _env->src_dirs_s[src_idx] + substr_view(real,_abs_src_dirs_s[src_idx].size()) ) ;
				else if (_lcl_file_loc(substr_view(real,_repo_root_sz))<=FileLoc::Dep) lnks.push_back( real.substr(_repo_root_sz)                                                    ) ;
			}
			if (n_lnks++>=NMaxLnks) return {{},::move(lnks)} ;          // link loop detected, same check as system
			if (!last             )   nxt << '/'<<(file.data()+end+1) ; // append unprocessed part, avoiding this copy would be very complex (would need a stack) and links to dir are uncommon
			if (nxt[0]=='/'       ) { end =  0 ; prev_real_size = 0 ; } // absolute link target : flush real
			else                      end = -1 ;                        // end must point to the /, invent a virtual one before the string
			real.resize(prev_real_size) ;                               // links are relative to containing dir, suppress last component
			ping = !ping ;
			file = nxt   ;
		}
		// /!\ : this code must be in sync with RealPathEnv::file_loc
		// tmp may be in_repo, repo root is in_repo
		if      (+in_tmp ) res.file_loc = FileLoc::Tmp  ;
		else if (+in_proc) res.file_loc = FileLoc::Proc ;
		else if (+in_repo) {
			if (real.size()<_repo_root_sz) {
				res.file_loc = FileLoc::RepoRoot ;
			} else {
				real.erase(0,_repo_root_sz) ;
				res.file_loc = _lcl_file_loc(real) ;
				if (res.file_loc==FileLoc::Repo) {
					if      ( _env->lnk_support>=LnkSupport::File && !no_follow           ) res.file_accessed = Yes   ;
					else if ( _env->lnk_support>=LnkSupport::Full && real.find('/')!=Npos ) res.file_accessed = Maybe ;
				}
			}
		} else if ( size_t i=_find_src_idx(real) ; i!=Npos ) {
			real         = _env->src_dirs_s[i] + ::string_view(real).substr(_abs_src_dirs_s[i].size()) ;
			res.file_loc = FileLoc::SrcDir                                                             ;
			if      ( _env->lnk_support>=LnkSupport::File && !no_follow                                      ) res.file_accessed = Yes   ;
			else if ( _env->lnk_support>=LnkSupport::Full && real.find('/',_env->src_dirs_s[i].size())!=Npos ) res.file_accessed = Maybe ;
		}
		return res ;
	}

	::vmap_s<Accesses> RealPath::exec(SolveReport&& sr) {
		::vmap_s<Accesses> res ;
		// from tmp, we can go back to repo
		for( int i=0 ; i<=4 ; i++ ) {                                                                          // interpret #!<interpreter> recursively (4 levels as per man execve)
			for( ::string& l : sr.lnks ) res.emplace_back(::move(l),Accesses(Access::Lnk)) ;
			//
			if ( sr.file_loc>FileLoc::Dep && sr.file_loc!=FileLoc::Tmp ) break ;                               // if we escaped from the repo, there is no more deps to gather
			//
			::string abs_real = mk_glb(sr.real,_env->repo_root_s) ;
			Accesses a        = Access::Reg                       ; if (sr.file_accessed==Yes) a |= Access::Lnk ;
			//
			if (sr.file_loc<=FileLoc::Dep) res.emplace_back( ::move(sr.real) , a ) ;
			try {
				AcFd     hdr_fd { abs_real , true/*err_ok*/ } ; if    ( !hdr_fd                                               ) break ;
				::string hdr    = hdr_fd.read(256)            ; if    ( !hdr.starts_with("#!")                                ) break ;
				size_t   eol    = hdr.find('\n')              ; if    ( eol!=Npos                                             ) hdr.resize(eol) ;
				size_t   pos1   = 2                           ; while ( pos1<hdr.size() &&  (hdr[pos1]==' '||hdr[pos1]=='\t') ) pos1++ ;
				size_t   pos2   = pos1                        ; while ( pos2<hdr.size() && !(hdr[pos2]==' '||hdr[pos2]=='\t') ) pos2++ ;
				//
				if (pos1!=pos2) sr = solve( ::string_view(hdr).substr(pos1,pos2-pos1) , false/*no_follow*/ ) ; // interpreter is first word
				// recurse by looping
			} catch (::string const&) {
				break ;                                                                                        // if hdr_fd is not readable (e.g. it is a dir), do as if it did not exist at all
			}
		}
		return res ;
	}

	void RealPath::chdir() {
		if (pid)   _cwd = read_lnk(cat("/proc/",pid,"/cwd")) ;
		else     { _cwd = no_slash(cwd_s())                  ; _cwd_pid = ::getpid() ; }
	}

}
