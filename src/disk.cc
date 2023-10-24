// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <dirent.h>
#include <sys/mman.h>

#include <filesystem>

#include "disk.hh"

using namespace filesystem ;

namespace Disk {

	::ostream& operator<<( ::ostream& os , FileInfo const& fi ) {
		/**/     os<< "FileInfo("<<fi.tag ;
		if (+fi) os<<','<<fi.sz           ;
		return   os<<')'                  ;
	}

	::ostream& operator<<( ::ostream& os , FileInfoDate const& fid ) {
		/**/      os << "FileInfoDate("<< fid.tag    ;
		if (+fid) os <<','<< fid.sz <<','<< fid.date ;
		return    os <<')'                           ;
	}

	FileInfo::FileInfo(struct ::stat const& st) {
		switch (errno) {
			case 0       :                       break  ;
			case ENOENT  :
			case ENOTDIR : tag = FileTag::None ; return ;                  // no available info
			default      : tag = FileTag::Err  ; return ;                  // .
		}
		if      (S_ISREG(st.st_mode)) { tag = st.st_mode&S_IXUSR ? FileTag::Exe : FileTag::Reg ;          }
		else if (S_ISLNK(st.st_mode)) { tag =                      FileTag::Lnk                ;          }
		else if (S_ISDIR(st.st_mode)) { tag =                      FileTag::Dir                ; return ; } // no reliable info
		else                          { tag =                      FileTag::Err                ; return ; } // .
		sz = st.st_size ;
	}

	FileInfoDate::FileInfoDate( Fd at , ::string const& name ) {
		const char* n      = name.c_str() ;
		AutoCloseFd new_at ;                                                   // declare outside if so fd is not closed before stat call
		if (!name.empty()) {
			new_at = ::openat(at,n,O_RDONLY|O_NOFOLLOW) ;                      // if we want the date, we must ensure we do an open so this may work with NFS
			if (+new_at) {
				n  = ""     ;
				at = new_at ;
			}
		}
		struct ::stat st = _s_stat(at,n) ;
		static_cast<FileInfo&>(*this) = FileInfo(st) ;
		if (+*this) {
			date = Ddate(st.st_ctim) ;
			sz   = st.st_size        ;
		}
	}

	FileMap::FileMap( Fd at , ::string const& filename) {
		FileInfo fi{at,filename} ;
		if (!fi.is_reg()) return ;
		sz = fi.sz ;
		if (!sz) {
			_ok = true ;
			return ;
		}
		_fd = open_read(at,filename) ;
		if (!_fd) return ;
		data = static_cast<const uint8_t*>(::mmap( nullptr , sz , PROT_READ , MAP_PRIVATE , _fd , 0 )) ;
		if (data==MAP_FAILED) {
			_fd.detach() ;                                                     // report error
			data = nullptr ;                                                   // avoid garbage info
			return ;
		}
		_ok = true ;
	}

	vector_s lst_dir( Fd at , ::string const& dir , ::string const& prefix ) {
		Fd dir_fd = at ;
		if      (!dir.empty()) dir_fd = ::openat( at , dir.c_str() , O_RDONLY|O_DIRECTORY ) ;
		else if (at==Fd::Cwd ) dir_fd = ::openat( at , "."         , O_RDONLY|O_DIRECTORY ) ;
		if (!dir_fd) throw to_string("cannot open dir ",at==Fd::Cwd?"":to_string('@',at,':'),dir) ;
		//
		DIR* dir_fp = ::fdopendir(dir_fd) ;
		if (!dir_fp) throw to_string("cannot list dir ",at==Fd::Cwd?"":to_string('@',at,':'),dir) ;
		//
		::vector_s res ;
		while ( struct dirent* entry = ::readdir(dir_fp) ) {
			if (entry->d_name[0]!='.') goto Ok  ;
			if (entry->d_name[1]==0  ) continue ;                              // ignore .
			if (entry->d_name[1]!='.') goto Ok  ;
			if (entry->d_name[2]==0  ) continue ;                              // ignore ..
		Ok :
			res.emplace_back(prefix+entry->d_name) ;
		}
		if (dir_fd!=at) ::closedir(dir_fp) ;
		return res ;
	}

	void unlink_inside( Fd at , ::string const& dir ) {
		::string dir_s = dir.empty() ? ""s : dir+'/' ;
		for( ::string const& f : lst_dir(at,dir,dir_s) ) unlink(at,f) ;
	}

	void unlink( Fd at , ::string const& file ) {
		if (::unlinkat(at,file.c_str(),0)==0           ) return ;
		if (errno==ENOENT                              ) return ;
		if (errno!=EISDIR                              ) throw to_string("cannot unlink file ",file) ;
		unlink_inside(at,file) ;
		if (::unlinkat(at,file.c_str(),AT_REMOVEDIR)!=0) throw to_string("cannot unlink dir " ,file) ;
	}

	::vector_s read_lines(::string const& filename) {
		::ifstream file_stream{filename} ;
		if (!file_stream) return {} ;
		//
		::vector_s res            ;
		char       line[PATH_MAX] ;
		while (file_stream.getline(line,sizeof(line))) res.push_back(line) ;
		return res ;
	}

	::string read_content(::string const& file) {
		::ifstream file_stream{file} ;
		if (!file_stream) return {} ;
		//
		::string res       ;
		char     buf[4096] ;
		while (file_stream.read(buf,sizeof(buf))) res.append(buf,sizeof(buf)) ;
		res.append(buf,file_stream.gcount()) ;
		return res ;
	}

	void write_lines( ::string const& file , ::vector_s const& lines ) {
		OFStream file_stream{file} ;
		if (!lines.empty()) SWEAR_PROD(bool(file_stream)) ;
		for( ::string const& l : lines ) file_stream << l << '\n' ;
	}

	void write_content( ::string const& file , ::string const& content ) {
		OFStream file_stream{file} ;
		if (!content.empty()) SWEAR_PROD(bool(file_stream)) ;
		file_stream << content ;
	}

	static void _walk( ::vector_s& res , Fd at , ::string const& file , ::string const& prefix ) {
		switch (FileInfo(at,file).tag) {
			case FileTag::Err :                         return ;
			case FileTag::Dir :                         break  ;
			default           : res.push_back(prefix) ; return ;
		}
		::vector_s lst ;
		try                     { lst = lst_dir(at,file) ; }
		catch (::string const&) { return ;                 } // list only accessible files
		::string   file_s   = file  +'/'       ;
		::string   prefix_s = prefix+'/'       ;
		for( ::string const& f : lst ) _walk( res , at,file_s+f , prefix_s+f ) ;
	}
	::vector_s walk( Fd at , ::string const& file , ::string const& prefix ) {
		::vector_s res ;
		_walk( res , at , file , prefix ) ;
		return res ;
	}

	void make_dir( Fd at , ::string const& dir , bool unlink_ok ) {
		::vector_s to_mk{dir} ;
		while (to_mk.size()) {
			::string const& d = to_mk.back() ;                                 // process by starting top most : as to_mk is ordered, parent necessarily appears before child
			if (::mkdirat(at,d.c_str(),0777)==0) {
				to_mk.pop_back() ;                                             // created, ok
			} else if (errno==EEXIST) {
				if      (is_dir(at,d)) to_mk.pop_back()                                                    ; // already exists, ok
				else if (unlink_ok) ::unlinkat(at,d.c_str(),0)                                             ;
				else                throw to_string("must unlink ",at==Fd::Cwd?"":to_string('@',at,':'),d) ;
			} else {
				::string dd = dir_name(d) ;
				if ( (errno!=ENOENT&&errno!=ENOTDIR) || dd.empty() )
					throw to_string("cannot create dir ",at==Fd::Cwd?"":to_string('@',at,':'),d) ; // if ENOTDIR, a parent is not a dir, it will be fixed up
				to_mk.push_back(::move(dd)) ;                                                      // retry after parent is created
			}
		}
	}

	::string dir_name(::string const& file) {
		size_t sep = file.rfind('/') ;
		if (sep==Npos) return {}                 ;
		else           return file.substr(0,sep) ;
	}

	::string base_name(::string const& file) {
		size_t sep = file.rfind('/') ;
		if (sep!=Npos) return file.substr(sep+1) ;
		else           return file               ;
	}

	::string const& dir_guard( Fd at , ::string const& file ) {
		::string dir = dir_name(file) ;
		if (!dir.empty()) make_dir(at,dir,true/*unlink_ok*/) ;
		return file ;
	}

	::string mk_lcl( ::string const& file , ::string const& dir_s ) {
		SWEAR( is_abs(file) == is_abs_s(dir_s)    , file , dir_s ) ;
		SWEAR( dir_s.empty() || dir_s.back()=='/' ,        dir_s ) ;
		size_t last_slash1 = 0 ;
		for( size_t i=0 ; i<file.size() ; i++ ) {
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
			SWEAR(!d_sv.empty()) ;
			d_sv = d_sv.substr(0,d_sv.size()-1) ;                              // suppress ending /
			size_t last_slash = d_sv.rfind('/') ;
			if (last_slash==Npos) { SWEAR(!d_sv.empty()) ; d_sv = d_sv.substr(0,0           ) ; }
			else                  {                        d_sv = d_sv.substr(0,last_slash+1) ; } // keep new ending /
		}
		return to_string(d_sv,f_v) ;
	}

	//
	// RealPath
	//

	::ostream& operator<<( ::ostream& os , RealPathEnv const& rpe ) {
		/**/                         os << "RealPathEnv(" << rpe.lnk_support ;
		/**/                         os <<','<< rpe.root_dir                 ;
		if (!rpe.tmp_dir   .empty()) os <<','<< rpe.tmp_dir                  ;
		if (!rpe.tmp_view  .empty()) os <<','<< rpe.tmp_view                 ;
		if (!rpe.src_dirs_s.empty()) os <<','<< rpe.src_dirs_s               ;
		return                       os <<')'                                ;
	}

	::ostream& operator<<( ::ostream& os , RealPath::SolveReport const& sr ) {
		return os << "SolveReport(" << sr.real <<','<< sr.kind <<','<< sr.lnks <<')' ;
	}

	void RealPath::init( RealPathEnv const& rpe , pid_t p ) {
		/**/                   SWEAR( is_abs(rpe.root_dir) , rpe.root_dir ) ;
		/**/                   SWEAR( is_abs(rpe.tmp_dir ) , rpe.tmp_dir  ) ;
		if (!tmp_view.empty()) SWEAR( is_abs(rpe.tmp_view) , rpe.tmp_view ) ;
		//
		static_cast<RealPathEnv&>(*this) = rpe ;
		pid                              = p   ;
		//
		/**/                 cwd_         = pid ? read_lnk(to_string("/proc/",pid,"/cwd")) : cwd() ;
		/**/                 _admin_dir   = to_string(root_dir,'/',AdminDir)                       ;
		/**/                 has_tmp_view = !tmp_view.empty()                                      ;
		if (tmp_dir.empty()) tmp_dir      = get_env("TMPDIR",P_tmpdir)                             ;
		if (!has_tmp_view  ) tmp_view     = tmp_dir                                                ;
		//
		for ( ::string const& sd_s : src_dirs_s ) _abs_src_dirs_s.push_back(mk_glb(sd_s,root_dir)) ;
	}

	::string RealPath::_find_src(::string const& real) const {
		for( size_t i=0 ; i<_abs_src_dirs_s.size() ; i++ )
			if (real.starts_with(_abs_src_dirs_s[i]))
				return src_dirs_s[i] + (real.c_str()+_abs_src_dirs_s[i].size()) ;
		return {} ;
	}

	// strong performance efforts have been made :
	// - avoid ::string copying as much as possible
	// - do not support links outside repo & tmp, except from /proc (which is meaningful)
	// - cache links to avoid superfluous syscalls (unfortunately, this cache cannot be shared betweewn processes)
	// - note that besides syscalls, this algo is very fast and caching intermediate results could degrade performances (checking the cache could take as long as doing the job)
	static inline int _get_symloop_max() {                                     // max number of links to follow before decreting it is a loop
		int res = ::sysconf(_SC_SYMLOOP_MAX) ;
		if (res>=0) return res                ;
		else        return _POSIX_SYMLOOP_MAX ;
	}
	// XXX : optimize by transforming cur into a const char*, avoiding a copy for caller
	// XXX : optimize by looking in /proc (after having opened the file) if file is a real path and provide direct answer in this case
	//       apply, as soon as we prepare to make a readlink (before that, we may lose time instead of saving), which arrives pretty soon when link support is full
	RealPath::SolveReport RealPath::solve( Fd at , ::string const& file , bool no_follow ) {
		static ::string const* const proc         = new ::string("/proc") ;
		static int             const s_n_max_lnks = _get_symloop_max()    ;

		::vector_s lnks ;

		::string        local_file[2] ;                                        // ping-pong used to keep a copy of input file if we must modify it (avoid upfront copy as it is rarely necessary)
		bool            exists        = true                      ;            // if false, we have seen a non-existent component and there cannot be symlinks within it
		bool            mapped        = false                     ;            // if true <=> tmp mapping has been used
		::string const* cur           = &file                     ;            // points to the current file : input file or local copy local_file
		size_t          pos           = file[0]=='/'              ;
		::string        real          ; real.reserve(file.size()) ;                        // canonical (link free, absolute, no ., .. nor empty component). Empty instead of '/'. Anticipate no link
		if (!pos) {                                                                        // file is relative, meaning relative to at
			if      (at==Fd::Cwd) real = cwd_                                            ;
			else if (pid        ) real = read_lnk(to_string(*proc,'/',pid,"/fd/",at.fd)) ;
			else                  real = read_lnk(to_string(*proc,"/self/fd/"   ,at.fd)) ;
			if ( has_tmp_view && real.starts_with(tmp_dir) ) {
				real   = tmp_view + (real.c_str()+tmp_dir.size()) ;
				mapped = true                                     ;
			}
			if (!is_abs(real) ) return {} ;                                    // user code might use the strangest at, it will be an error but we must support it
			if (real.size()==1) real.clear() ;
		}
		_Dvg in_repo ( root_dir                         , real ) ;             // keep track of where we are w.r.t. repo       , track symlinks according to lnk_support policy
		_Dvg in_tmp  ( tmp_view                         , real ) ;             // keep track of where we are w.r.t. tmp        , always track symlinks
		_Dvg in_admin( to_string(root_dir,'/',AdminDir) , real ) ;             // keep track of where we are w.r.t. repo/LMAKE , never track symlinks, like files in no domain
		_Dvg in_proc ( *proc                            , real ) ;             // keep track of where we are w.r.t. /proc      , always track symlinks

		// loop INVARIANT : accessed file is real+'/'+cur->substr(pos)
		// when pos>cur->size(), we are done and result is real
		size_t end    ;
		int    n_lnks = 0 ;
		for (
		;	pos <= cur->size()
		;		pos = end+1
			,	in_repo.update(root_dir,real)                                  // for all domains except admin, they start only when inside, i.e. the domain root is not part of the domain
			,	in_tmp .update(tmp_view,real)                                  // .
			,	in_proc.update(*proc   ,real)                                  // .
		) {
			end = cur->find( '/', pos ) ;
			bool last = end==Npos ;
			if (last    ) end = cur->size() ;
			if (end==pos) continue ;                                           // empty component, ignore
			if ((*cur)[pos]=='.') {
				if ( end==pos+1                       ) continue ;             // component is .
				if ( end==pos+2 && (*cur)[pos+1]=='.' ) {                      // component is ..
					if (!real.empty()) real.resize(real.rfind('/')) ;
					continue ;
				}
			}
			size_t prev_real_size = real.size() ;
			real.push_back('/') ;
			real.append(*cur,pos,end-pos) ;
			in_admin.update(_admin_dir,real) ;                                 // for the admin domain, it starts at itself, i.e. the admin dir is part of the domain
			if ( !exists || ( no_follow && last ) ) continue     ;             // if !exists, no hope to find a symbolic link but continue cleanup of empty, . and .. components
			if ( in_tmp                           ) goto Proceed ;             // note that tmp can lie within repo or admin
			if ( in_admin                         ) continue     ;
			if ( in_proc                          ) goto Proceed ;
			if ( !in_repo                         ) continue     ;
			switch (lnk_support) {                                             // in repo
				case LnkSupport::None : continue ;
				case LnkSupport::File : if (last) break ; else continue ;
				case LnkSupport::Full : break ;
				default               : FAIL(lnk_support) ;
			}
		Proceed :
			// manage links
			::string* nxt = &local_file[cur!=&local_file[1]] ;                 // bounce, initially, when cur is neither local_file's, any buffer is ok
			//
			if ( has_tmp_view && in_tmp ) { *nxt = read_lnk( tmp_dir + (real.c_str()+tmp_view.size()) ) ; mapped = true ; }
			else                          { *nxt = read_lnk(            real                          ) ;                 }
			if (nxt->empty()) {
				exists = errno!=ENOENT ;
				// do not generate dep for intermediate dir that are not links as we indirectly depend on them through the last components
				// for example if a/b/c is a link to d/e and we access a/b/c/f, we generate the link a/b/c :
				// - a & a/b will be indirectly depended on through a/b/c
				// - d & d/e will be indrectly depended on through caller depending on d/e/f (the real accessed file returned as the result)
				continue ;
			}
			exists = true ;
			if (!(in_tmp||in_admin||in_proc)) {
				if (in_repo) {
					if (real.size()>root_dir.size()) lnks.push_back(real.substr(root_dir.size()+1)) ;
				} else {
					::string src = _find_src(real) ;
					if (!src.empty()) lnks.push_back(src) ;
				}
			}
			if (n_lnks++>=s_n_max_lnks) return {{},::move(lnks)} ;             // link loop detected, same check as system
			// avoiding the following copy is very complex (would require to manage a stack) and links to dir are uncommon
			if (!last) {                                                       // append unprocessed part
				nxt->push_back('/'       ) ;
				nxt->append   (*cur,end+1) ;
			}
			if ((*nxt)[0]=='/') { end =  0 ; prev_real_size = 0 ; }            // absolute link target : flush real
			else                { end = -1 ;                      }            // end must point to the /, invent a virtual one before the string
			real.resize(prev_real_size) ;                                      // links are relative to containing dir, suppress last component
			cur = nxt ;
		}
		// admin is typically in repo, tmp might be, repo root is in_repo
		if (in_tmp) {
			if (has_tmp_view) return { tmp_dir + (real.c_str()+tmp_view.size()) , ::move(lnks) , Kind::Tmp , true/*mapped*/ } ; // avoid calling substr that creates a temporary string
			else              return { ::move(real)                             , ::move(lnks) , Kind::Tmp , mapped         } ; // if no tmp view, no need for real path
		} else {
			if (in_proc ) return { ::move(real) , ::move(lnks) , Kind::Proc  , mapped } ;
			if (in_admin) return { ::move(real) , ::move(lnks) , Kind::Admin , mapped } ;
			if (in_repo ) {
				if (real.size()>root_dir.size()) return { real.substr(root_dir.size()+1) , ::move(lnks) , Kind::Repo , mapped } ;
				else                             return { ::move(real)                   , ::move(lnks) , Kind::Root , mapped } ;
			}
			else {
				::string src = _find_src(real) ;
				if (src.empty()) return { ::move(real) , ::move(lnks) , Kind::Ext     , mapped } ;
				else             return { ::move(src ) , ::move(lnks) , Kind::SrcDirs , mapped } ;
			}
		}
	}

	::vmap_s<Accesses> RealPath::exec(SolveReport& sr) {
		::vmap_s<Accesses> res         ;
		::string           interpreter ; interpreter.reserve(256) ;
		//
		// from tmp, we can go back to repo
		for( int i=0 ; i<=4 ; i++ ) {                                          // interpret #!<interpreter> recursively (4 levels as per man execve)
			if (sr.kind>Kind::Dep && sr.kind!=Kind::Tmp) break ;               // if we escaped from the repo, there is no more deps to gather
			//
			if (sr.mapped) throw to_string("executing ",sr.real," with mapped files along its interpreter path from ",tmp_view," to ",tmp_dir," would require to modify the file content") ;
			//
			for( ::string& l : sr.lnks ) res.emplace_back(::move(l),Accesses(Access::Lnk)) ;
			::ifstream real_stream { mk_abs(sr.real,root_dir) } ;
			Accesses   a           = Access::Reg                ;
			if (sr.kind<=Kind::Dep) res.emplace_back(sr.real,a) ;
			//
			char hdr[2] ;
			if (!real_stream.read(hdr,sizeof(hdr))) break ;
			if (strncmp(hdr,"#!",2)!=0            ) break ;
			interpreter.resize(256) ;
			real_stream.getline(interpreter.data(),interpreter.size()) ;       // man execve specifies that data beyond 255 chars are ignored
			if (!real_stream.gcount()) break ;
			size_t pos = interpreter.find(' ') ;
			if      (pos!=Npos               ) interpreter.resize(pos                   ) ; // interpreter is the first word
			else if (interpreter.back()=='\n') interpreter.resize(real_stream.gcount()-1) ; // or the entire line if it is composed of a single word
			else                               interpreter.resize(real_stream.gcount()  ) ; // .
			// recurse
			sr = solve(interpreter,false/*no_follow*/) ;
		}
		return res ;
	}

}
