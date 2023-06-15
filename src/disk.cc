// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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
		os<< "FileInfo("<<fi.tag ;
		if ( fi.tag!=FileTag::None && fi.tag!=FileTag::Err ) os<<','<<fi.sz ;
		return os<<')' ;
	}

	::ostream& operator<<( ::ostream& os , FileInfoDate const& fid ) {
		os<< "FileInfoDate("<<fid.tag ;
		if ( fid.tag!=FileTag::None && fid.tag!=FileTag::Err ) os <<','<< fid.sz <<','<< fid.date ;
		return os<<')' ;
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
		else                          { tag =                      FileTag::Other              ; return ; } // .
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
			date = Date(st.st_ctim) ;
			sz   = st.st_size       ;
		}
	}

	FileMap::FileMap(::string const& file_name) {
		FileInfo fi = file_name ;
		if (!fi.is_reg()) return ;
		size = fi.sz ;
		if (!size) {
			_ok = true ;
			return ;
		}
		_fd = open_read(file_name) ;
		if (!_fd) return ;
		data = static_cast<const uint8_t*>(::mmap( nullptr , size , PROT_READ , MAP_PRIVATE , _fd , 0 )) ;
		if (data==MAP_FAILED) {
			_fd.detach() ;                                                     // report error
			data = nullptr ;                                                   // avoid garbage info
			return ;
		}
		_ok = true ;
	}

	static vector_s _lst_dir( Fd at , ::string const& dir ) {
		::vector_s  res    ;
		AutoCloseFd dir_fd = ::openat( at , dir.c_str() , O_RDONLY|O_DIRECTORY ) ; if (!dir_fd) throw to_string("cannot list dir ",at==Fd::Cwd?"":to_string('@',at,':'),dir) ;
		DIR*        dir_fp = ::fdopendir(dir_fd)                                 ; if (!dir_fp) throw to_string("cannot list dir ",at==Fd::Cwd?"":to_string('@',at,':'),dir) ;
		while ( struct dirent* entry = ::readdir(dir_fp) ) {
			if (entry->d_name[0]!='.') goto Ok  ;
			if (entry->d_name[1]==0  ) continue ;                               // ignore .
			if (entry->d_name[1]!='.') goto Ok  ;
			if (entry->d_name[2]==0  ) continue ;                               // ignore ..
		Ok :
			res.emplace_back(entry->d_name) ;
		}
		::closedir(dir_fp) ;
		return res ;
	}

	void unlink_inside(::string const& dir) {
		for( ::string const& f : _lst_dir(Fd::Cwd,dir) ) unlink(to_string(dir,'/',f)) ;
	}

	void unlink(::string const& file) {
		if (::unlink(file.c_str())==0) return ;
		if (errno==ENOENT            ) return ;
		if (errno!=EISDIR            ) throw to_string("cannot unlink file ",file) ;
		unlink_inside(file) ;
		if (::rmdir(file.c_str())!=0 ) throw to_string("cannot unlink dir " ,file) ;
	}

	::vector_s read_lines(::string const& file_name) {
		::ifstream file_stream{file_name} ;
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

	static void _walk( ::vector_s& res , Fd at , ::string const& file , ::string const& prefix ) {
		switch (FileInfo(at,file).tag) {
			case FileTag::Err  :                         return ;
			case FileTag::Dir  :                         break  ;
			default            : res.push_back(prefix) ; return ;
		}
		::vector_s lst      = _lst_dir(at,file) ;
		::string   file_s   = file  +'/'        ;
		::string   prefix_s = prefix+'/'        ;
		for( ::string const& f : lst ) _walk( res, at,file_s+f , prefix_s+f ) ;
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
			if (::mkdirat(at,d.c_str(),0755)==0) {
				to_mk.pop_back() ;                                             // created, ok
			} else if (errno==EEXIST) {
				if      (is_dir(d)) to_mk.pop_back()                  ;        // already exists, ok
				else if (unlink_ok) ::unlink(d.c_str())               ;
				else                throw to_string("must unlink ",d) ;
			} else {
				to_mk.push_back(dir_name(d)) ;                                                                               // retry after parent is created
				if (!( (errno==ENOENT||errno==ENOTDIR) && !to_mk.back().empty() )) throw to_string("cannot create dir ",d) ; // if ENOTDIR, a parent is not a dir, it will be fixed up
			}
		}
	}

	::string dir_name(::string const& file) {
		size_t sep = file.rfind('/') ;
		if (sep==NPos) return {}                 ;
		else           return file.substr(0,sep) ;
	}

	::string base_name(::string const& file) {
		size_t sep = file.rfind('/') ;
		if (sep!=NPos) return file.substr(sep+1) ;
		else           return file               ;
	}

	void dir_guard(::string const& file) {
		::string dir = dir_name(file) ;
		if (!dir.empty()) make_dir(dir,true/*unlink_ok*/) ;
	}

	::string localize( ::string const& file , ::string const& dir_s ) {
		if (is_abs_path(file)                    ) return file                      ;
		if (file.compare(0,dir_s.size(),dir_s)==0) return file.substr(dir_s.size()) ;
		size_t last_slash = NPos  ;
		size_t n_slash    = 0     ;
		bool   dvg        = false ;
		for( size_t i=0 ; i<dir_s.size() ; i++ ) {
			if      (dvg              ) { if (dir_s[i]=='/') n_slash++ ;         }
			else if (file[i]==dir_s[i]) { if (dir_s[i]=='/') last_slash = i    ; }
			else                        {                    dvg        = true ; }
		}
		// fast path's to avoid copies
		if (n_slash) { ::string res ; for( size_t i=0 ; i<n_slash ; i++ ) res += "../" ; if (last_slash==NPos) res += file ; else res += file.substr(last_slash+1) ; return res ; }
		else         {                                                                   if (last_slash==NPos) return file ; else return file.substr(last_slash+1) ;              }
	}

	::string globalize( ::string const& file , ::string const& dir_s ) {
		if (is_abs_path(file)) return                          file  ;
		else                   return beautify_file_name(dir_s+file) ;
	}

	//
	// RealPath
	//

	::ostream& operator<<( ::ostream& os , RealPath::SolveReport const& sr ) {
		os << "SolveReport(" << sr.real <<','<< sr.lnks ;
		if (sr.in_repo) os << ",repo" ;
		if (sr.in_tmp ) os << ",tmp"  ;
		return os <<')' ;
	}

	// strong performance efforts have been made :
	// - avoid ::string copying as much as possible
	// - do not support links outside repo & tmp, except from /proc (which is meaningful)
	// - cache links to avoid superfluous syscalls (unfortunately, this cache cannot be shared betweewn processes)
	// - note that besides syscalls, this algo is very fast and caching intermediate results could degrade performances (checking the cache could take as long as doing the job)
	static int _get_symloop_max() {                                            // max number of links to follow before decreting it is a loop
		int res = ::sysconf(_SC_SYMLOOP_MAX) ;
		if (res>=0) return res                ;
		else        return _POSIX_SYMLOOP_MAX ;
	}
	RealPath::SolveReport RealPath::solve( Fd at , ::string const& file , bool no_follow ) {
		static ::string const* const proc         = new ::string("/proc") ;
		static int             const s_n_max_lnks = _get_symloop_max()    ;
		if (file.empty()) return {} ;

		::vector_s lnks ;

		::string        local_file[2] ;                                        // ping-pong used to keep a copy of input file if we must modify it (avoid upfront copy as often, it is not necessary)
		bool            ping          = 0                         ;            // index indicating which local_file is used
		bool            exists        = true                      ;            // if false, we have seen a non-existent component and there cannot be symlinks within it
		::string const* file_p        = &file                     ;            // points to the current file : input file or local copy local_file
		size_t          pos           = file[0]=='/'              ;
		::string        real          ; real.reserve(file.size()) ;                        // canonical (link free, absolute, no ., .. nor empty component). Empty instead of '/'. Anticipate no link
		if (!pos) {                                                                        // file is relative, meaning relative to at
			if      (at==Fd::Cwd) real = cwd_                                            ;
			else if (pid        ) real = read_lnk(to_string(*proc,'/',pid,"/fd/",at.fd)) ;
			else                  real = read_lnk(to_string(*proc,"/self/fd/"   ,at.fd)) ;
			if (!is_abs_path(real)) return {} ;                                            // user code might use the strangest at, it will be an error but we must support it
			if (real.size()==1) real.clear() ;
		}
		_Dvg in_repo ( *g_root_dir                         , real ) ;          // keep track of where we are w.r.t. repo       , track symlinks according to _lnk_support policy
		_Dvg in_tmp  ( *g_tmp_dir                          , real ) ;          // keep track of where we are w.r.t. tmp        , always track symlinks
		_Dvg in_admin( to_string(*g_root_dir,'/',AdminDir) , real ) ;          // keep track of where we are w.r.t. repo/LMAKE , never track symlinks, like files in no domain
		_Dvg in_proc ( *proc                               , real ) ;          // keep track of where we are w.r.t. /proc      , always track symlinks

		// loop INVARIANT : accessed file is real+'/'+file_p->substr(pos)
		// when file is empty, we are done
		size_t end    ;
		int    n_lnks = 0 ;
		for (
		;	pos <= file_p->size()
		;		pos = end+1
			,	in_repo.update(*g_root_dir,real)                               // for all domains except admin, they start only when inside, i.e. the domain root is not part of the domain
			,	in_tmp .update(*g_tmp_dir ,real)                               // .
			,	in_proc.update(*proc      ,real)                               // .
		) {
			end = file_p->find( '/', pos ) ;
			bool last = end==NPos ;
			if (last    ) end = file_p->size() ;
			if (end==pos) continue ;                                           // empty component, ignore
			if ((*file_p)[pos]=='.') {
				if ( end==pos+1                          ) continue ;          // component is .
				if ( end==pos+2 && (*file_p)[pos+1]=='.' ) {                   // component is ..
					if (!real.empty()) real.resize(real.rfind('/')) ;
					continue ;
				}
			}
			size_t prev_real_size = real.size() ;
			real.push_back('/') ;
			real.append(*file_p,pos,end-pos) ;
			in_admin.update(_admin_dir,real) ;                                 // for the admin domain, it starts at itself, i.e. the admin dir is part of the domain
			if ( !exists || ( no_follow && last ) ) continue     ;             // if !exists, no hope to find a symbolic link but continue cleanup of empty, . and .. components
			if ( in_tmp                           ) goto Proceed ;             // note that tmp can lie within repo or admin
			if ( in_admin                         ) continue     ;
			if ( in_proc                          ) goto Proceed ;
			if ( !in_repo                         ) continue     ;
			switch (_lnk_support) {                                            // in repo
				case LnkSupport::None : continue ;
				case LnkSupport::File : if (last) break ; else continue ;
				case LnkSupport::Full : break ;
				default               : FAIL(_lnk_support) ;
			}
		Proceed :
			bool pong = !ping ;
			// manage links
			Bool3 lnk_ok =  _s_read_lnk( local_file[pong]/*out*/ , real ) ;
			exists = lnk_ok!=No ;
			// do not generate dep for intermediate dir that are not links as we indirectly depend on them through the last components
			// for example if a/b/c is a link to d/e and we access a/b/c/f, we generate the link a/b/c :
			// - a & a/b will be indirectly depended on through a/b/c
			// - d & d/e will be indrectly depended on through caller depending on d/e/f (the real accessed file returned as the result)
			if ( lnk_ok!=Yes            ) continue ;
			if ( !in_tmp && in_repo     ) lnks.emplace_back( real.substr(g_root_dir->size()+1) ) ;
			if ( n_lnks++>=s_n_max_lnks ) return {{},::move(lnks),false,false} ; // link loop detected, same check as system
			// avoiding the following copy is very complex (would require to manage a stack) and links to dir are uncommon
			if (!last) {                                                       // append unprocessed part
				local_file[pong].push_back('/'          ) ;
				local_file[pong].append   (*file_p,end+1) ;
			}
			file_p = &local_file[pong] ;
			ping   = pong              ;
			if ((*file_p)[0]=='/') { end =  0 ; prev_real_size = 0 ; }         // absolute link target : flush real
			else                   { end = -1 ;                      }         // end must point to the /, invent a virtual one before the string
			real.resize(prev_real_size) ;                                      // links are relative to containing dir, suppress last component
		}
		// admin is typically in repo, tmp might be, repo root is not actually in repo
		if ( !in_repo || in_tmp || in_admin || real.size()<=g_root_dir->size() ) return { {}                                , ::move(lnks) , false , in_tmp } ;
		else                                                                     return { real.substr(g_root_dir->size()+1) , ::move(lnks) , true  , false  } ;

	}

	::vmap_s<RealPath::ExecAccess> RealPath::exec( Fd at , ::string const& exe , bool no_follow ) {
		::vmap_s<ExecAccess> res         ;
		::string             interpreter ;
		const char*          cur_file    = exe.c_str() ;
		for( int i=0 ; i<=4 ; i++ ) {                                              // interpret #!<interpreter> recursively (4 levels as per man execve)
			SolveReport real = solve(at,cur_file,no_follow) ;
			for( ::string& l : real.lnks ) res.emplace_back(::move(l),ExecAccess(true/*as_lnk*/,false/*as_reg*/)) ;
			if (real.real.empty()) break ;
			::ifstream real_stream{to_string(*g_root_dir,'/',real.real)} ;
			res.emplace_back(::move(real.real),ExecAccess(!no_follow/*as_lnk*/,true/*as_reg*/)) ;
			char hdr[2] ;
			if (!real_stream.read(hdr,sizeof(hdr))) break ;
			if (strncmp(hdr,"#!",2)!=0            ) break ;
			interpreter.resize(256) ;
			real_stream.getline(interpreter.data(),interpreter.size()) ;           // man execve specifies that data beyond 255 chars are ignored
			if (!real_stream.gcount()) break ;
			size_t pos = interpreter.find(' ') ;
			if      (pos!=NPos               ) interpreter.resize(pos                   ) ; // interpreter is the first word
			else if (interpreter.back()=='\n') interpreter.resize(real_stream.gcount()-1) ; // or the entire line if it is composed of a single word
			else                               interpreter.resize(real_stream.gcount()  ) ; // .
			// recurse
			at        = Fd::Cwd             ;
			cur_file  = interpreter.c_str() ;
			no_follow = false               ;
		}
		return res ;
	}

}
