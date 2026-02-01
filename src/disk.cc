// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <endian.h>

#include "hash.hh"
#include "process.hh"

#include "disk.hh"

namespace Disk {
	using namespace Hash ;
	using namespace Time ;

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
			throw_if( c==0 , "file contains nul char : ",path ) ; // filenames are not supposed to contain any nul char, cannot canonicalize
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
			throw_if( c=='\0' , "file contains nul char : ",path ) ;                    // filenames are not supposed to contain any nul char, cannot canonicalize
			handle(c) ;
		}
		if (path.back()!='/') {
			handle('/') ;
			if (+res) res.pop_back (   ) ;
			else      res.push_back('.') ;                                              // /!\ res = "." generates a warning (and hence an error with -Werror) when compiling for 32 bits with gcc-12
		}
		return res ;
	}

	::string mk_lcl( ::string const& path , ::string const& dir_s ) {
		SWEAR( is_dir_name(dir_s)          ,        dir_s ) ;
		SWEAR( is_abs(path)==is_abs(dir_s) , path , dir_s ) ;
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

	::string mk_glb( ::string const& path , ::string const& dir_s ) {
		if (is_abs  (path)) return path ;
		::string_view d_s = dir_s ;
		::string_view p   = path  ;
		for(; p.starts_with("../") ; p.remove_prefix(3) ) {
			if (!d_s) break ;
			d_s.remove_suffix(1) ;                                                     // suppress ending /
			size_t last_slash = d_s.rfind('/') ;
			if (last_slash==Npos) { SWEAR(+d_s) ; d_s = {}                         ; }
			else                  {               d_s = d_s.substr(0,last_slash+1) ; } // keep new ending /
		}
		return cat(d_s,p) ;
	}

	bool lies_within( ::string const& file , ::string const& dir ) {
		::string_view d ;
		if (is_dir_name(dir)) { if (!dir    ) return is_lcl(file) && file!="." ; d = substr_view(dir,0,dir.size()-1) ; }
		else                  { if (dir==".") return is_lcl(file) && file!="." ; d = dir                             ; }
		//
		if ( !d.ends_with("/..") && d!=".." ) return file.starts_with(d) && file[d.size()]=='/' ; // else dir_s is a series of .. as it is canonic
		if ( file[0]=='/'                   ) return false                                      ; // absolute file does not lie within relative dir_s
		if ( !file.starts_with(d)           ) return true                                       ; // must contain at least as many .. to escape from dir_s
		::string_view sv = substr_view(file,d.size()) ;
		/**/                                  return !sv.starts_with("/../") && sv!="/.."       ; // check if we have an additional ..
	}

	::string mk_file( ::string const& f , FileDisplay fd , Bool3 exists ) {
		::string pfx(2+sizeof(FileNameIdx),FileMrkr) ;
		pfx[1] = char(fd) ;
		encode_int<FileNameIdx>(&pfx[2],f.size()) ;
		switch (exists) {
			case Yes : { if (!FileInfo(f).exists()) return "(not existing) "+pfx+f ; } break ;
			case No  : { if ( FileInfo(f).exists()) return "(existing) "    +pfx+f ; } break ;
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
			pos  = new_pos                     ;
		}
		return res ;
	}

	//
	// disk access library
	//

	vector_s lst_dir_s( FileRef dir_s , ::string const& prefix , NfsGuard* nfs_guard ) {
		AcFd dir_fd { dir_s , {.flags=O_RDONLY|O_DIRECTORY,.nfs_guard=nfs_guard} } ;
		//
		DIR* dir_fp = ::fdopendir(dir_fd) ;
		throw_unless( dir_fp , "cannot list dir ",dir_s," : ",StrErr() ) ;
		dir_fd.detach() ;                                                  // ownership is transferred to dir_fp
		//
		::vector_s res ;
		while ( struct dirent* entry = ::readdir(dir_fp) ) {
			if (entry->d_name[0]!='.') goto Ok  ;
			if (entry->d_name[1]==0  ) continue ;                          // ignore .
			if (entry->d_name[1]!='.') goto Ok  ;
			if (entry->d_name[2]==0  ) continue ;                          // ignore ..
		Ok :
			res.emplace_back(prefix+entry->d_name) ;
		}
		::closedir(dir_fp) ;
		return res ;
	}

	void unlnk_inside_s( FileRef dir_s , _UnlnkAction action ) {
		if (!action.abs_ok) SWEAR( is_lcl(dir_s.file) , dir_s ) ; // unless certain, prevent accidental non-local unlinks
		if (action.force) {
			if (+dir_s.file) [[maybe_unused]] int _ = ::fchmodat( dir_s.at , dir_s.file.c_str() , S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH , AT_SYMLINK_NOFOLLOW ) ; // best effort
			else             [[maybe_unused]] int _ = ::fchmod  ( dir_s.at ,                      S_IRWXU|S_IRGRP|S_IXGRP|S_IROTH|S_IXOTH                       ) ; // .
		}
		::string e ;
		action.dir_ok = true ;
		for( ::string const& f : lst_dir_s(dir_s,dir_s.file,action.nfs_guard) ) {
			try                        { unlnk({dir_s.at,f},action) ; }                                                                                             // remove all removable files
			catch (::string const& e2) { e = e2 ;                     }
		}
		if (+e) throw e ;
	}

	bool/*done*/ unlnk( FileRef file , _UnlnkAction action ) {
		/**/                                            SWEAR( +file                           , action.abs_ok ) ; // do not unlink cwd
		if (!action.abs_ok                            ) SWEAR( !file.file || is_lcl(file.file) , file          ) ; // unless certain, prevent accidental non-local unlinks
		if (::unlinkat(file.at,file.file.c_str(),0)==0) return true /*done*/ ;
		if (errno==ENOENT                             ) return false/*done*/ ;
		//
		if ( !action.dir_ok || errno!=EISDIR ) throw cat("cannot unlink file (",StrErr(),") ",file ) ;
		//
		unlnk_inside_s({file.at,with_slash(file.file)},action) ;
		//
		if (action.nfs_guard) action.nfs_guard->change(file) ;
		if (::unlinkat(file.at,file.file.c_str(),AT_REMOVEDIR)!=0) throw cat("cannot unlink dir ",file) ;
		return true/*done*/ ;
	}

	void rmdir_s( FileRef dir_s , NfsGuard* nfs_guard ) {
		SWEAR(+dir_s.file) ;
		if (nfs_guard) nfs_guard->change(dir_s) ;
		if (::unlinkat(dir_s.at,dir_s.file.c_str(),AT_REMOVEDIR)!=0) throw cat("cannot rmdir ",dir_s) ;
	}

	void touch( FileRef path , Pdate date , NfsGuard* nfs_guard ) {
		TimeSpec time_specs[2] { {.tv_sec=0,.tv_nsec=UTIME_OMIT} , {.tv_sec=time_t(date.sec()),.tv_nsec=int32_t(date.nsec_in_s())} } ;
		::utimensat( path.at , path.file.c_str() , time_specs , AT_SYMLINK_NOFOLLOW ) ;
		if (nfs_guard) nfs_guard->change(path) ;
	}

	void mk_dir_empty_s( FileRef dir_s , _UnlnkAction action ) {
		try                     { unlnk_inside_s(dir_s,action                       ) ; }
		catch (::string const&) { mk_dir_s      (dir_s,{.nfs_guard=action.nfs_guard}) ; } // ensure tmp dir exists
	}

	void sym_lnk( FileRef file , ::string const& target , _CreatAction action ) {
		bool first = true ;
		if (action.nfs_guard) action.nfs_guard->change(file) ;
	Retry :
		if (::symlinkat(target.c_str(),file.at,file.file.c_str())!=0) {
			if ( errno==ENOENT && first ) {
				first = false ;              // ensure we retry at most once
				dir_guard( file , action ) ;
				goto Retry/*BACKWARD*/ ;
			}
			throw cat("cannot create symlink from ",file," to ",target) ;
		}
	}

	// path and pfx are restored after execution
	static void _walk( ::vmap_s<FileTag>& res , Fd at , ::string& path , FileTags file_tags , ::string& pfx , ::function<bool(::string const&)> prune ) {
		FileTag tag = FileInfo({at,path}).tag() ;
		//
		if (file_tags[tag]   ) res.emplace_back(pfx,tag) ;
		if (tag!=FileTag::Dir) return ;
		if (prune(path)      ) return ;
		//
		path += '/' ;
		::vector_s lst ;
		try                     { lst = lst_dir_s({at,path}) ; }
		catch (::string const&) { path.pop_back() ; return ;   } // list only accessible files
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
	::vmap_s<FileTag> walk( FileRef path , FileTags file_tags , ::string const& pfx , ::function<bool(::string const&)> prune ) {
		SWEAR(+path) ;
		::vmap_s<FileTag> res ;
		_walk( res , path.at , ::ref(no_slash(path.file)) , file_tags , ::ref(::copy(pfx)) , prune ) ;
		return res ;
	}

	size_t/*pos*/ mk_dir_s( FileRef dir_s , _CreatAction action ) {
		if (!dir_s.file) return Npos ;                                                           // nothing to create
		//
		::vector_s  to_mk_s { dir_s.file }              ;
		const char* msg     = nullptr                   ;
		size_t      pos     = dir_s.file[0]=='/'?0:Npos ;                                        // return the pos of the / between existing and new components
		while (+to_mk_s) {
			::string& d_s = to_mk_s.back() ;                                                     // parents are after children in to_mk
			if (action.nfs_guard                    ) action.nfs_guard->change({dir_s.at,d_s}) ;
			if (::mkdirat(dir_s.at,d_s.c_str(),0777/*mod*/)==0) {
				if (+action.perm_ext) {
					static mode_t umask_ = get_umask() ;
					switch (action.perm_ext) {
						case PermExt::Other : if (!(umask_     )) goto PermOk ; break ;
						case PermExt::Group : if (!(umask_&0770)) goto PermOk ; break ;
					DN}
					//
					if (action.nfs_guard) action.nfs_guard->access_dir_s({dir_s.at,d_s}) ;
					FileStat st ;
					if ( ::fstatat(dir_s.at,d_s.c_str(),&st,0/*flags*/)!=0 ) throw cat("cannot stat (",StrErr(),") to extend permissions : ",File(dir_s.at,no_slash(::move(d_s)))) ;
					//
					mode_t usr_mod = (st.st_mode>>6)&07 ;
					mode_t new_mod = st.st_mode         ;
					switch (action.perm_ext) {
						case PermExt::Other : new_mod |= usr_mod    ; [[fallthrough]] ;
						case PermExt::Group : new_mod |= usr_mod<<3 ; break           ;
					DN}
					if (new_mod!=st.st_mode)
						if ( ::fchmodat(dir_s.at,d_s.c_str(),new_mod,0/*flags*/)!=0 ) throw cat("cannot chmod (",StrErr(),") to extend permissions : ",File(dir_s.at,no_slash(::move(d_s)))) ;
				}
			PermOk :
				pos++ ;
				to_mk_s.pop_back() ;
				continue ;
			}                                                                                    // done
			switch (errno) {
				case EEXIST :
					if ( action.force && FileInfo({dir_s.at,d_s},{.nfs_guard=action.nfs_guard}).tag()!=FileTag::Dir )   unlnk({dir_s.at,d_s},{.abs_ok=true,.nfs_guard=action.nfs_guard} ) ;   // retry
					else                                                                                              { pos = d_s.size()-1 ; to_mk_s.pop_back() ;                           } // done
				break ;
				case ENOENT  :
				case ENOTDIR :
					if (has_dir(d_s))   to_mk_s.push_back(dir_name_s(d_s)) ;         // retry after parent is created
					else              { msg = "cannot create top dir" ; goto Bad ; } // if ENOTDIR, a parent is not a dir, it will not be fixed up
				break  ;
				default :
					msg = "cannot create dir" ;
				Bad :
					throw cat(msg," (",StrErr(),") ",File(dir_s.at.fd,no_slash(::move(d_s)))) ;
			}
		}
		return pos ;
	}

	::string read_lnk( FileRef file , NfsGuard* nfs_guard ) {
		if (nfs_guard) nfs_guard->access(file) ;
		char    buf[PATH_MAX] ;
		ssize_t cnt           = ::readlinkat(file.at,file.file.c_str(),buf,PATH_MAX) ;
		if ( cnt<0 || cnt>=PATH_MAX ) return {} ;
		return {buf,size_t(cnt)} ;
	}

	FileTag cpy( FileRef src , FileRef dst , _CreatAction action ) {
		FileInfo fi  {src }     ;
		FileTag  tag = fi.tag() ;
		//
		SWEAR( !FileInfo(dst).exists() , dst ) ;
		//
		switch (tag) {
			case FileTag::None  :
			case FileTag::Dir   : break ;                                                                                    // dirs are like no file
			case FileTag::Empty :                                                                                            // fast path : no need to access empty src
				AcFd( dst , {.flags=O_WRONLY|O_TRUNC|O_CREAT|O_NOFOLLOW,.force=action.force,.nfs_guard=action.nfs_guard} ) ;
			break ;
			case FileTag::Reg :
			case FileTag::Exe : {
				AcFd rfd { src , {                                                                                    .force=action.force,.nfs_guard=action.nfs_guard} } ;
				AcFd wfd { dst , {.flags=O_WRONLY|O_TRUNC|O_CREAT|O_NOFOLLOW,.mod=mode_t(tag==FileTag::Exe?0777:0666),.force=action.force,.nfs_guard=action.nfs_guard} } ;
				int  rc  = ::sendfile( wfd , rfd , nullptr/*offset*/ , fi.sz )                                                                                           ;
				if (rc!=0) throw cat("cannot copy ",src," to ",dst) ;
			} break ;
			case FileTag::Lnk :
				sym_lnk( dst , read_lnk(src,action.nfs_guard) , {.force=action.force,.nfs_guard=action.nfs_guard} ) ;
			break ;
		DF}                                                                                                                  // NO_COV
		return tag ;
	}

	void rename( FileRef src , FileRef dst , _CreatAction action ) {
		int first = true ;
		if (action.nfs_guard) {
			action.nfs_guard->update(src) ;
			action.nfs_guard->change(dst) ;
		}
	Retry :
		int rc = ::renameat( src.at , src.file.c_str() , dst.at , dst.file.c_str() ) ;
		if (rc!=0) {
			if ( errno==ENOENT && first ) {
				dir_guard( dst , action ) ; // hope error is due to destination (else penalty is just retrying once, not a big deal)
				first = false ;
				goto Retry ;
			}
			throw cat("cannot rename (",StrErr(),") ",src," into ",dst) ;
		}
	}

	//
	// FileInfo
	//

	::string& operator+=( ::string& os , FileInfo const& fi ) { // START_OF_NO_COV
		/**/     os << "FileInfo("           ;
		if (+fi) os << fi.sz <<','<< fi.date ;
		return   os << ')'                   ;
	}                                                           // END_OF_NO_COV

	FileInfo::FileInfo(FileStat const& st) {
		FileTag tag = FileTag::None/*garbage*/ ;
		if (S_ISREG(st.st_mode)) {
			if      ( st.st_mode&S_IXUSR) tag = FileTag::Exe   ;
			else if (!st.st_size        ) tag = FileTag::Empty ;
			else                          tag = FileTag::Reg   ;
		} else if (S_ISLNK(st.st_mode)) {
			/**/                          tag = FileTag::Lnk   ;
		} else {
			if (S_ISDIR(st.st_mode)) date = Ddate(FileTag::Dir) ; // else it is an awkward file which we ignore
			return ;
		}
		date = Ddate(st,tag) ;
		sz   = st.st_size    ;
	}

	FileInfo::FileInfo( FileRef path , Action action ) {
		if (action.nfs_guard) action.nfs_guard->access(path) ;
		FileStat st ;
		if      (+path.file      ) { if (::fstatat( path.at , path.file.c_str() , &st , action.no_follow?AT_SYMLINK_NOFOLLOW:0 )!=0) return ; }
		else if (path.at.fd>=0   ) { if (::fstat  ( path.at ,                     &st                                          )!=0) return ; }
		else if (path.at==Fd::Cwd) { if (::stat   (           "."               , &st                                          )!=0) return ; }
		else                                                                                                                         return ;
		self = st ;
	}

	//
	// FileSig
	//

	::string& operator+=( ::string& os , FileSig const& sig ) {                              // START_OF_NO_COV
		return os<< "FileSig(" << to_hex(sig._val>>NBits<FileTag>) <<':'<< sig.tag() <<')' ;
	}                                                                                        // END_OF_NO_COV

	// START_OF_VERSIONING CACHE REPO
	FileSig::FileSig(FileInfo const& fi) : FileSig{fi.tag()} {
		switch (fi.tag()) {
			case FileTag::None    :
			case FileTag::Unknown :
			case FileTag::Dir     :                                                                break ;
			case FileTag::Empty   : _val |= fi.date.val() & msb_msk<Ddate::Tick>(NBits<FileTag>) ; break ; // improve traceability when size is predictible, 8ns granularity is more than enough
			case FileTag::Lnk     :
			case FileTag::Reg     :
			case FileTag::Exe     : {
				Xxh h ;
				h    += fi.date                       ;
				h    += fi.sz                         ;
				_val |= +h.digest() << NBits<FileTag> ;
			} break ;
		DF}                                                                                                // NO_COV
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

	FileMap::FileMap(FileRef file) {
		_fd = Fd( file , {.err_ok=true} ) ;
		if (!_fd) return ;
		sz = FileInfo(_fd,{.no_follow=false}).sz ;
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
	// ACL
	//

	::vector<AclEntry> acl_entries(FileRef dir_s) {
		::vector<AclEntry> res ;
		AcFd               fd  { dir_s , {.flags=O_PATH} }                                           ;
		ssize_t            sz  = ::fgetxattr(fd,"system.posix_acl_default",nullptr/*buf*/,0/*size*/) ; if (sz<0                                            ) return {} ;
		::string           buf ;
		sz = ::lgetxattr(".","system.posix_acl_default",nullptr/*buf*/,0/*size*/) ;                    if (sz<0                                            ) return {} ;
		buf.resize(sz) ;
		sz = ::lgetxattr(".","system.posix_acl_default",buf.data(),buf.size()) ;                       if (sz<0                                            ) return {} ;
		struct ::posix_acl_xattr_header* hdr = reinterpret_cast<posix_acl_xattr_header*>(buf.data()) ; if (le32toh(hdr->a_version)!=POSIX_ACL_XATTR_VERSION) return {} ;
		static_assert(sizeof(hdr->a_version)==4) ;
		for (
			posix_acl_xattr_entry* entry = reinterpret_cast<posix_acl_xattr_entry*>(hdr+1)
		;	reinterpret_cast<char*>(entry+1) <= buf.data()+buf.size()
		;	entry++
		) {
			res.push_back(*entry) ;
		}
		return res ;
	}

	PermExt auto_perm_ext( ::string const& dir_s, ::string const& msg ) {
		// auto-config permissions by analyzing dir permissions
		PermExt  res ;
		FileStat st  ; if (::lstat(dir_s.c_str(),&st)!=0) FAIL() ; SWEAR( S_ISDIR(st.st_mode) ) ;
		//
		throw_unless( (st.st_mode&S_IRWXU)==S_IRWXU , msg," must have full read/write/execute access to ",dir_s,rm_slash ) ;
		//
		if      ((st.st_mode&S_IRWXG)!=S_IRWXG) res = PermExt::None  ;
		else if ((st.st_mode&S_IRWXO)!=S_IRWXO) res = PermExt::Group ;
		else                                    res = PermExt::Other ;
		//
		if (res==PermExt::Group)
			throw_unless(
				st.st_mode&S_ISGID
			,	msg," must have its setgid set for group level accesses to ",dir_s,rm_slash,". Consider : find ",dir_s,rm_slash," -type d -exec chmod g+s {} +"
			) ;
		//
		bool group_ok = false ;
		bool other_ok = false ;
		bool mask_ok  = true  ;
		for( AclEntry const& acl : acl_entries(dir_s) )
			switch (acl.e_tag) {
				case ACL_GROUP_OBJ : if ((acl.e_perm&7)==7) group_ok = true  ; break ;
				case ACL_OTHER     : if ((acl.e_perm&7)==7) other_ok = true  ; break ;
				case ACL_MASK      : if ((acl.e_perm&7)!=7) mask_ok  = false ; break ;
			DN}
		group_ok &= mask_ok ;
		if      (!group_ok          ) {}
		else if ( other_ok          ) res = PermExt::None ;
		else if (res<=PermExt::Group) res = PermExt::None ;
		//
		return res ;
	}
}
