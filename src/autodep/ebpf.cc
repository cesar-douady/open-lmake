// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// Job side of the ebpf autodep method.
// The kernel (ebpf.bpf.c) reports, for each syscall of the job accessing files of interest :
// - captures : resolved paths (relative to the repo or to a source dir) together with file info before the syscall modifies anything, and traversed symlinks with their target
// - an End event which closes them and carries the syscall number, its result and its args
// Here, we turn that into the same access reports as the other autodep methods do (cf. record.cc, the semantic of each syscall mirrors what is done there).
// Unlike them, we are not synchronous with the job : we may process an access long after it occurred, when files may have been modified, unlinked or renamed.
// So the rule is that we do not look at the disk : what must be known about files comes from the kernel.
// The exceptions are marked with DISK, they are all corner cases (failed syscalls for which the kernel resolved nothing and the error does not tell whether file exists).

#include <poll.h>
#include <sys/param.h>  // MAXSYMLINKS
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <linux/io_uring.h>

#include <bpf/libbpf.h> // only for the ring_buffer consumer API ; the programs are loaded by the daemon, not here

#if HAS_32
	#include "world_32.h"
#endif

#include "app.hh"
#include "process.hh"

#include "record.hh"
#include "syscall_tab.hh"
#include "gather.hh"

#include "ebpf_daemon.hh"
#include "ebpf_event.h"

#include "ebpf.hh"

#ifndef O_PATH
	#define O_PATH 0    // no check for O_PATH if it does not exist
#endif
#ifndef O_TMPFILE
	#define O_TMPFILE 0 // no check for O_TMPFILE if it does not exist
#endif

using namespace Disk ;
using namespace Time ;

namespace AutodepEbpf {

	using Proc = JobExecProc ;

	// per-job state obtained from the daemon (a single job per job_exec / lautodep process)
	static Fd               _g_sock   ;      // kept open for the whole job : closing it tells the daemon to release the job
	static Fd               _g_rb_fd  ;      // this job's ring buffer, received via SCM_RIGHTS
	static uint32_t         _g_job_id =  0 ; // carried to the kernel by the child's self-registration marker
	static ::vector<size_t> _g_roots  ;      // roots registered in the kernel : Npos for the repo, else the index in src_dirs_s

	//
	// consumer
	//

	struct Solved {                                                  // what Record::Solve provides
		::string const& real_write() const { return real0 | real ; }
		// data
		::string real     ;
		::string real0    ;                                          // real in case reading and writing is to different files because of overlays
		Accesses accesses ;                                          // Access::Lnk if real was accessed as a sym link
		FileLoc  loc      = FileLoc::Unknown ;
		FileLoc  loc0     = FileLoc::Unknown ;
		FileInfo info     ;                                          // info of real before the syscall
	} ;

	struct Lnk {                                 // a traversed symlink
		::string real       ;
		::string abs        ;                    // as seen by the job
		FileLoc  loc        = FileLoc::Unknown ;
		FileInfo info       ;
		::string target     ;
		bool     has_target = false ;
	} ;

	struct Obj {                                                 // a path accessed by a syscall
		bool is_dir() const { return exists && S_ISDIR(mode) ; }
		// data
		uint8_t  kind       = 0     ;
		bool     raw        = false ;                            // if true <=> path could not be resolved by the kernel
		bool     exists     = false ;
		bool     created    = false ;
		uint32_t open_flags = 0     ;
		mode_t   mode       = 0     ;
		mode_t   other_mode = 0     ;                            // for renames, mode of the other file (which may not be reported), 0 if it does not exist
		::string real       = {}    ;                            // if !raw
		FileLoc  loc        = FileLoc::Unknown ;                 // .
		FileInfo info       = {}    ;                            // .
		int      slot       = 0     ;                            // if  raw, index of path in syscall
		::string dir        = {}    ;                            // .      , dir path is relative to, as seen by the job
		::string path       = {}    ;                            // .      , path as provided to the syscall
	} ;

	struct Thread {
		// data
		pid_t         pid  = 0 ;
		pid_t         tid  = 0 ;
		::vector<Lnk> lnks ;
		::vector<Obj> objs ;
	} ;

	static ::string _mk_path( uint8_t const* data , uint32_t len ) { // components are stored leaf first, each followed by a null
		::string res ;
		uint32_t end = len ;
		while (end) {
			uint32_t start = end-1 ;                                 // skip terminating null
			while ( start && data[start-1] ) start-- ;
			if (+res) res += '/' ;
			res.append( reinterpret_cast<const char*>(data+start) , end-1-start ) ;
			end = start ;
		}
		return res ;
	}

	static FileInfo _mk_info(EbpfEvent const& ev) {
		if (!(ev.flags&EBPF_F_EXISTS)) return {} ;
		FileStat st = {} ;
		st.st_mode         = ev.mode                   ;
		st.st_size         = ev.size                   ;
		st.st_mtim.tv_sec  = ev.mtime_ns/1'000'000'000 ;
		st.st_mtim.tv_nsec = ev.mtime_ns%1'000'000'000 ;
		return FileInfo(st) ;
	}

	static bool _no_follow(int flags) { return (flags&O_NOFOLLOW) || ( (flags&O_CREAT) && (flags&O_EXCL) )                     ; } // cf. record.cc
	static bool _do_stat  (int flags) { return                       ( (flags&O_CREAT) && (flags&O_EXCL) ) || !(flags&O_CREAT) ; }
	static bool _do_read  (int flags) { return ( !(flags&O_PATH) && (flags&O_ACCMODE)!=O_WRONLY ) && !(flags&O_TRUNC)          ; }
	static bool _do_write (int flags) { return ( !(flags&O_PATH) && (flags&O_ACCMODE)!=O_RDONLY ) ||  (flags&O_TRUNC)          ; }

	struct Consumer {
		// cxtors & co
		Consumer() : _env{Record::s_autodep_env()} {
			for( ::string const& sd_s : _env.src_dirs_s ) _abs_src_dirs_s.push_back(mk_glb(sd_s,_env.repo_root_s)) ;
			for( size_t r : _g_roots ) {
				if (r==Npos) { _root_pfxs.push_back({}                ) ; _root_abss.push_back(no_slash(_env.repo_root_s)) ; }
				else         { _root_pfxs.push_back(_env.src_dirs_s[r]) ; _root_abss.push_back(no_slash(_abs_src_dirs_s[r])) ; }
			}
			// kernel dates accesses with CLOCK_TAI, which only differs from CLOCK_REALTIME by a whole number of seconds
			struct ::timespec tai  ; ::clock_gettime(CLOCK_TAI     ,&tai ) ;
			struct ::timespec real ; ::clock_gettime(CLOCK_REALTIME,&real) ;
			int64_t off_ns = (int64_t(tai.tv_sec)-int64_t(real.tv_sec))*1'000'000'000l + (int64_t(tai.tv_nsec)-int64_t(real.tv_nsec)) ;
			_tai_offset_s = (off_ns+(off_ns>=0?500'000'000l:-500'000'000l))/1'000'000'000l ;
		}
		// services
	private :
		// name of a file and where it lies, from its absolute path as seen by the job (canonical, empty for /)
		// /!\ must be in sync with RealPath::solve
		::pair_s<FileLoc> _mk_real(::string&& abs) const {
			::string_view repo_root = substr_view(_env.repo_root_s,0,_env.repo_root_s.size()-1) ;
			if (abs==repo_root                     ) return { {} , FileLoc::RepoRoot } ;
			if (abs.starts_with(_env.repo_root_s)  ) { abs.erase(0,_env.repo_root_s.size()) ; FileLoc fl = _env.file_loc(abs) ; return { ::move(abs) , fl } ; }
			for( size_t i : iota(_abs_src_dirs_s.size()) ) {
				::string const& asd_s = _abs_src_dirs_s[i] ;
				if (abs.size()+1==asd_s.size() && asd_s.starts_with(abs)) return { no_slash(_env.src_dirs_s[i])                     , FileLoc::SrcDir } ;
				if (abs.starts_with(asd_s)                              ) return { _env.src_dirs_s[i]+substr_view(abs,asd_s.size()) , FileLoc::SrcDir } ;
			}
			if (!abs) abs = "/" ;
			FileLoc fl = _env.file_loc(abs) ;
			return { ::move(abs) , fl } ;
		}
		::pair_s<FileLoc> _mk_real( EbpfEvent const& ev , ::string* /*out*/ abs=nullptr ) const {
			uint32_t l0   = ::min<uint32_t>( ev.len[0] , EBPF_DATA_SZ ) ;
			::string path = _mk_path( ev.data , l0 )                    ;
			if (ev.root==EBPF_ROOT_ABS) {
				if (+path) path = '/'+path ;
				if (abs  ) *abs = path     ;
				return _mk_real(::move(path)) ;
			}
			throw_unless( ev.root<_g_roots.size() , "unexpected root ",uint(ev.root) ) ;
			if (abs) { *abs = _root_abss[ev.root] ; if (+path) *abs << '/'<<path ; }
			if (_g_roots[ev.root]==Npos) {
				if (!path) return { {} , FileLoc::RepoRoot } ;
				FileLoc fl = _env.file_loc(path) ;                                    // manage admin and tmp dirs
				return { ::move(path) , fl } ;
			} else {
				if (!path) return { no_slash(_root_pfxs[ev.root]) , FileLoc::SrcDir } ;
				else       return { _root_pfxs[ev.root]+path      , FileLoc::SrcDir } ;
			}
		}
		// the absolute path (as seen by the job, canonical, empty for /) designated by path relative to dir, given the links traversed by the kernel
		// links lying in a root are reported by the kernel, the other ones are read where RealPath::solve follows them
		// return nothing in case of link loop
		::optional_s _lexical( Thread const& th , ::string const& dir , ::string const& path , bool no_follow ) {
			::string   res    = is_abs(path) ? ""s : dir ;
			::vector_s todo   ;                                                       // components to process, in reverse order
			size_t     n_lnks = 0                        ;
			auto push = [&](::string_view p) {
				size_t end = p.size() ;
				while (end) {
					size_t start = p.rfind('/',end-1) ;
					if (start==Npos) start = 0       ;
					else             start = start+1 ;
					if (end>start) todo.emplace_back(p.substr(start,end-start)) ;
					if (!start   ) break ;
					end = start-1 ;
				}
			} ;
			push(path) ;
			while (+todo) {
				::string c = ::move(todo.back()) ; todo.pop_back() ;
				if (c==".") continue ;
				if (c=="..") {
					if (+res) res.resize(res.rfind('/')) ;
					continue ;
				}
				::string cand = res+'/'+c ;
				if ( no_follow && !todo ) { res = ::move(cand) ; continue ; }         // dont care about last component if no_follow
				Lnk const* lnk = nullptr ;
				for( Lnk const& l : th.lnks ) if (l.abs==cand) { lnk = &l ; break ; }
				if ( !lnk && +_env.views_s ) {                                        // within a view which is a bind mount, the kernel reports the link where it physically lies
					::string real = _mk_real(::copy(cand)).first ;
					for( auto const& [view_s,phys_s] : _env.views_s ) {
						if ( phys_s.size()!=1 || !lies_within(real,view_s) ) continue ;
						real = phys_s[0] + substr_view(real,view_s.size()) ;
						for( Lnk const& l : th.lnks ) if (l.real==real) { lnk = &l ; break ; }
						break ;
					}
				}
				::string target ;
				if (lnk) target = _target(*lnk)         ;
				else     target = _ext_target( th , cand ) ;
				if (!target) { res = ::move(cand) ; continue ; }                      // not a link
				if (n_lnks++>=MAXSYMLINKS) return {} ;                                // link loop detected, same check as system
				if (is_abs(target)) res.clear() ;
				push(target) ;
			}
			return res ;
		}
		// the kernel reports no link outside roots, in particular in the tmp dir (from where one can go back to the repo), cf. RealPath::solve for where links are followed
		// these are only necessary for paths the kernel could not resolve
		::string _ext_target( Thread const& th , ::string const& file ) {
			::string_view tmp_dir_s = +_env.tmp_dir_s ? ::string_view(_env.tmp_dir_s) : ::string_view(P_tmpdir "/") ;
			::string      f         = file                                                                          ;
			if      (file.starts_with(tmp_dir_s)            ) {}
			else if (file.starts_with("/dev/")              ) {}
			else if (file.starts_with("/proc/self/")        ) f = cat("/proc/",th.pid,                substr_view(file,10)) ; // /proc/self is ours, not that of the job
			else if (file.starts_with("/proc/thread-self/") ) f = cat("/proc/",th.pid,"/task/",th.tid,substr_view(file,17)) ;
			else if (file.starts_with("/proc/")             ) {}
			else if (_env.lnk_support==LnkSupport::FullExt  ) {}
			else                                              return {} ;
			return read_lnk(f) ;                                                                                              // DISK
		}
		// the target of a link is provided by the kernel for short links, else it must be read
		// this is safe as a symlink cannot be modified, it can only be replaced, which is detected by comparing info
		::string _target(Lnk const& lnk) {
			if (lnk.has_target) return lnk.target ;
			Fd        repo_root_fd = Record::s_repo_root_fd() ;
			::vector_s cands       ;                                                                        // if link lies in a view, it is one of the physical files
			for( auto const& [view_s,phys_s] : _env.views_s ) {
				if (!phys_s                      ) continue ;
				if (!lies_within(lnk.real,view_s)) continue ;
				for( ::string const& phy_s : phys_s ) cands.push_back( phy_s + substr_view(lnk.real,view_s.size()) ) ;
				break ;
			}
			cands.push_back(lnk.real) ;
			for( ::string const& cand : cands ) {
				FileRef  f      { repo_root_fd , cand } ;
				::string target = read_lnk(f)           ;                                                   // DISK
				if ( +target && FileInfo(f)==lnk.info ) return target ;
			}
			throw cat("symbolic link ",lnk.real," has been modified while job was using it") ;
		}
		//
		// reporting, cf. Record::report_access
		// info is that of file before syscall, it is always provided as it must not be read from disk
		void _report( FileLoc fl , ::string&& file , AccessDigest ad , FileInfo const& fi , Comment c , CommentExts ces={} ) {
			if (fl>FileLoc::Dep ) return ;
			if (fl>FileLoc::Repo) ad.write = No ;
			if (!ad             ) return ;
			_rec.report_cached({ .proc=Proc::Access , .comment=c , .comment_exts=ces , .digest=ad , .date=_date , .files={{::move(file),fi}} }) ;
		}
		void _tick() { _date++ ; }                                                                          // following reports are deemed to occur after preceding ones
		void _guard( FileLoc fl , ::string const& file ) {
			if (fl<=FileLoc::Repo) _rec.report_direct({ .proc=Proc::Guard , .date=_date , .files={{file,{}}} }) ;
		}
		// cf. handle_dep in Record::Solve
		// if s, store result in it
		void _handle_dep( Solved* /*out*/ s , FileLoc fl , ::string&& file , Accesses a , FileInfo const& fi , bool read , Comment c , CommentExts ces , CommentExt ext ) {
			if (+_env.views_s) {
				Fd repo_root_fd = Record::s_repo_root_fd() ;
				for( auto const& [view_s,phys_s] : _env.views_s ) {
					if (!phys_s                  ) continue ;                                               // empty phys do not represent a view
					if (!lies_within(file,view_s)) continue ;
					for( size_t i : iota(phys_s.size()) ) {
						bool     last  = i==phys_s.size()-1                                               ;
						::string f     = phys_s[i] + substr_view(file,view_s.size())                      ;
						FileInfo pfi   = last ? fi : FileInfo({repo_root_fd,f}) ;                           // DISK if several physical dirs : XXX : use the backing file reported by the kernel
						bool     found = pfi.exists() || !read                  ;                           // if not reading, assume file is found in upper
						if ( found && !last ) pfi = fi ;                                                    // this is the accessed file, its info is known
						FileLoc  pfl   = _env.file_loc(f)                                                 ;
						if (s) {
							if      (last ) { s->real  = f ; s->loc  = pfl ; s->info = pfi ; }
							else if (found) { s->real  = f ; s->loc  = pfl ; s->info = pfi ; }
							else if (i==0 ) { s->real0 = f ; s->loc0 = pfl ;                 }
						}
						if      (last ) { if (+a) { _report( pfl , ::move(f) , {.accesses=a             } , pfi , c , ces|ext ) ; _tick() ; } return ; }
						else if (found) {           _report( pfl , ::move(f) , {.accesses=a|Access::Stat} , pfi , c , ces|ext ) ; _tick() ;   return ; }
						else                      { _report( pfl , ::move(f) , {.accesses=  Access::Stat} , pfi , c , ces|ext ) ; _tick() ;            }
					}
					return ;
				}
			}
			// when no views match, process as if last
			if (s) {
				s->real = file ;
				s->loc  = fl   ;
				s->info = fi   ;
			}
			if (+a) { _report( fl , ::move(file) , {.accesses=a} , fi , c , ext ) ; _tick() ; }
		}
		// cf. Record::Solve
		// err is the errno of the syscall if o could not be resolved, if ambiguous, error may be due to something else than o
		Solved _solve( Thread const& th , Obj const* o , bool no_follow , bool read , Comment c , CommentExts ces={} , int err=0 , bool ambiguous=false ) {
			Solved s ;
			if (!o) return s ;
			::string real      ;
			FileLoc  loc       ;
			FileInfo info      ;
			bool     info_ok   = true ;
			if (o->raw) {
				::optional_s abs = _lexical(th,o->dir,o->path,no_follow) ; if (!abs) return s ;                      // user code might use the strangest paths, nothing is accessed
				::tie(real,loc) = _mk_real(::move(*abs)) ;
				if ( !ambiguous && err==ENOENT ) info    = {}    ;                                                   // file does not exist
				else                                             info_ok = false ;                                   // kernel did not get to the file and error says nothing about it
			} else {
				real = o->real ;
				loc  = o->loc  ;
				info = o->info ;
			}
			if ( !info_ok && loc<=FileLoc::Dep ) info = FileInfo({Record::s_repo_root_fd(),real}) ;                  // DISK
			// file may have been accessed as a symlink, cf. RealPath::solve
			Bool3 file_accessed = No ;
			if (loc==FileLoc::Repo) {
				if      ( _env.lnk_support>=LnkSupport::File && !no_follow           ) file_accessed = Yes   ;
				else if ( _env.lnk_support>=LnkSupport::Full && real.find('/')!=Npos ) file_accessed = Maybe ;
			} else if (loc==FileLoc::SrcDir) {
				size_t pfx_sz = 0 ; for( ::string const& sd_s : _env.src_dirs_s ) if (real.starts_with(sd_s)) { pfx_sz = sd_s.size() ; break ; }
				if      ( _env.lnk_support>=LnkSupport::File && !no_follow                  ) file_accessed = Yes   ;
				else if ( _env.lnk_support>=LnkSupport::Full && real.find('/',pfx_sz)!=Npos ) file_accessed = Maybe ;
			}
			if (file_accessed==Yes) s.accesses = Access::Lnk ;
			if ( !read && file_accessed==Maybe && has_dir(real) ) {                                                  // real dir is not protected by real
				::string dir = no_slash(dir_name_s(real)) ;
				FileInfo dfi = o->raw ? FileInfo({Record::s_repo_root_fd(),dir}) : FileInfo(FileTag::Dir) ;          // DISK if not resolved, else kernel has gone through it, so it is a dir
				_handle_dep( nullptr , loc , ::move(dir) , Access::Lnk , dfi , read , c , ces , CommentExt::Last ) ;
			}
			_handle_dep( &s , loc , ::move(real) , {} , info , read , c , ces , CommentExt::File ) ;
			return s ;
		}
		void _dep( Solved const& s , Accesses a , Comment c , CommentExts ces={} ) {                                 // cf. Record::Solve::report_dep
			_report( s.loc , ::copy(s.real) , {.accesses=s.accesses|a} , s.info , c , ces ) ;
		}
		void _update( Solved const& s , Accesses a , bool ok , Comment c , CommentExts ces={} ) {                    // cf. Record::SolveModify::report_update, access is known to be done or not
			AccessDigest ad { .write=No|ok , .accesses=s.accesses|a } ;
			if (+s.real0) {
				AccessDigest read_ad = ad ; read_ad.write = No ;
				_report( s.loc  , ::copy(s.real ) , read_ad , s.info , c , ces ) ;
				_report( s.loc0 , ::copy(s.real0) , ad      , {}     , c , ces ) ;
			} else {
				_report( s.loc  , ::copy(s.real ) , ad      , s.info , c , ces ) ;
			}
		}
		//
		// syscalls
		//
		void _open( Thread const& th , Obj const* o , int flags , int64_t rc , Comment c ) {                         // cf. Record::Open
			bool   no_follow = _no_follow(flags)                                                  ;
			bool   do_stat   = _do_stat  (flags)                                                  ;
			bool   do_read   = _do_read  (flags)                                                  ;
			bool   do_write  = _do_write (flags)                                                  ;
			Solved s         = _solve( th , o , no_follow , do_read , c , {} , rc<0?-rc:0 )       ;
			if ( !o                                  ) return ;
			if ( flags&(O_DIRECTORY|O_TMPFILE)       ) return ;                                                      // we already solved, this is enough
			if ( s.loc>FileLoc::Dep                  ) return ;
			if (!( do_stat || do_read || do_write )  ) return ;
			//
			CommentExts ces ;
			if ( no_follow              )   ces |= CommentExt::NoFollow ;
			if ( do_write               )   ces |= CommentExt::Write    ;
			if ( do_read                ) { ces |= CommentExt::Read     ; s.accesses |= Access::Reg  ; if (!do_write) s.accesses |= DataAccesses ; }
			if ( do_stat                ) {                                                            if (!do_write) s.accesses |= DataAccesses ; }
			if ( do_stat && !s.accesses ) { ces |= CommentExt::Stat     ; s.accesses |= Access::Stat ;                                             }
			//
			if (do_write) _update( s , {} , rc>=0 , c , ces ) ;
			else          _dep   ( s , {} ,         c , ces ) ;
		}
		void _stat( Thread const& th , Obj const* o , bool no_follow , Accesses a , int64_t rc , Comment c ) {       // cf. Record::Stat
			if (_env.ignore_stat) return ;
			CommentExts ces ; if (no_follow) ces |= CommentExt::NoFollow ;
			Solved      s   = _solve( th , o , no_follow , true/*read*/ , c , {} , rc<0?-rc:0 ) ;
			if (o) _dep( s , a , c , ces ) ;
		}
		void _rename( Thread const& th , Obj const* osrc , Obj const* odst , bool exchange , bool no_replace , int64_t rc , Comment c ) { // cf. Record::Rename
			if ( !osrc && !odst ) return ;
			int    err = rc<0 ? -rc : 0                                                                                           ;
			Solved src = _solve( th , osrc , true/*no_follow*/ , true/*read*/ , c , CommentExt::Read  , err , true/*ambiguous*/ ) ;
			Solved dst = _solve( th , odst , true/*no_follow*/ , exchange     , c , CommentExt::Write , err , true/*ambiguous*/ ) ;
			if ( osrc && odst && src.real==dst.real ) return ;                                                                            // posix says in this case, it is nop
			// a side lying outside the repo and source dirs (e.g. in the tmp dir) is not reported by the kernel : nothing is to be reported for it, but the other side must be
			// in that case, the reported side tells what the other one is
			struct Kind { bool exists = false ; bool is_dir = false ; } ;
			auto kind = [&]( Obj const* o , Obj const* other , Solved const& s )->Kind {
				if      (!o    ) return { other->other_mode!=0 , S_ISDIR(other->other_mode)    } ;
				else if (o->raw) return { s.info.exists()      , s.info.tag()==FileTag::Dir    } ;                                        // could not be resolved, info comes from disk
				else             return { o->exists            , o->is_dir()                   } ;
			} ;
			Kind src_kind = kind(osrc,odst,src) ;
			Kind dst_kind = kind(odst,osrc,dst) ;
			if ( rc>=0 && ( src_kind.is_dir || (exchange&&dst_kind.is_dir) ) && ( src.loc<=FileLoc::Dep || dst.loc<=FileLoc::Dep ) )
				throw cat("renaming a dir (",osrc?src.real:"<outside repo>"s," -> ",odst?dst.real:"<outside repo>"s,") is not supported yet with autodep method ebpf") ; // XXX : implement
			// files are read and unlinked in the source dir, their coresponding files in the destination dir are written
			::vmap_s<FileInfo>                      reads     ;
			::vmap_s<FileInfo>                      stats     ;
			::umap_s<::pair<bool/*read*/,FileInfo>> unlnk_map ;                                              // files listed here are unlinked and read if value is true
			::vmap_s<FileInfo>                      unlnks    ;
			::vmap_s<FileInfo>                      writes    ;
			auto do1 = [&]( Solved const& src , Kind src_kind , Solved const& dst ) {
				if ( !src_kind.exists || src_kind.is_dir ) return ;                                          // only files are renamed here
				if (+src.real0) {
					if      (src.loc0<=FileLoc::Repo) unlnk_map.try_emplace (src.real0,false/*read*/,FileInfo()) ;
					if      (src.loc <=FileLoc::Dep ) reads    .emplace_back(src.real ,src.info                ) ;
				} else {
					if      (src.loc<=FileLoc::Repo) unlnk_map.try_emplace (src.real,true/*read*/,src.info) ;
					else if (src.loc<=FileLoc::Dep ) reads    .emplace_back(src.real,src.info             ) ;
				}
				if (no_replace) { if (dst.loc <=FileLoc::Dep ) stats .emplace_back(dst.real ,dst.info  ) ; } // probe existence of destination
				if (+dst.real0) { if (dst.loc0<=FileLoc::Repo) writes.emplace_back(dst.real0,FileInfo()) ; }
				else            { if (dst.loc <=FileLoc::Repo) writes.emplace_back(dst.real ,dst.info  ) ; }
			} ;
			/**/          do1(src,src_kind,dst) ;
			if (exchange) do1(dst,dst_kind,src) ;
			//
			for( auto const& [w,wfi] : writes ) {
				auto it = unlnk_map.find(w) ;
				if (it==unlnk_map.end()) continue ;
				reads.emplace_back(w,it->second.second) ;                                                    // if a file is read, unlinked and written, it is actually not unlinked
				unlnk_map.erase(it) ;
			}
			for( auto const& [u,r_fi] : unlnk_map )
				if (r_fi.first) unlnks.emplace_back( u , r_fi.second ) ;
				else            writes.emplace_back( u , r_fi.second ) ;                                     // if not read, unlnk is like write
			//
			::uset_s guards ;
			for( auto const& [w,_] : writes ) if (+w) guards.insert(no_slash(dir_name_s(w))) ;
			for( auto const& [u,_] : unlnks ) if (+u) guards.insert(no_slash(dir_name_s(u))) ;
			for( ::string const& g : guards ) _guard( FileLoc::Repo , g ) ;
			//
			Bool3 w = No|(rc>=0) ;
			if (+reads ) _rec.report_cached({ .proc=Proc::Access , .comment=c , .comment_exts=CommentExt::Read   , .digest={         .accesses=DataAccesses} , .date=_date , .files=::move(reads ) }) ;
			if (+stats ) _rec.report_cached({ .proc=Proc::Access , .comment=c , .comment_exts=CommentExt::Stat   , .digest={         .accesses=Access::Stat} , .date=_date , .files=::move(stats ) }) ;
			if (+unlnks) _rec.report_cached({ .proc=Proc::Access , .comment=c , .comment_exts=CommentExt::Unlink , .digest={.write=w,.accesses=DataAccesses} , .date=_date , .files=::move(unlnks) }) ;
			if ( +writes && w==Yes ) _rec.report_cached({ .proc=Proc::Access , .comment=c , .comment_exts=CommentExt::Write , .digest={.write=w} , .date=_date , .files=::move(writes) }) ;
		}
		void _end( Thread const& th , EbpfEvent const& ev ) ;
	public :
		void handle( void* data , size_t sz ) ;
		// data
		::string err  ;
		bool     done = false ;
	private :
		AutodepEnv const&    _env            ;
		::vector_s           _abs_src_dirs_s ;
		::vector_s           _root_pfxs      ;                          // prefix of the files lying in each root
		::vector_s           _root_abss      ;                          // absolute path of each root, as seen by the job
		int64_t              _tai_offset_s   = 0 ;
		Record               _rec            { New , Yes/*enabled*/ } ; // only used to report accesses
		Pdate                _date           ;                          // date of the syscall being processed
		Pdate                _last_date      ;
		::umap<pid_t,Thread> _threads        ;
		::uset<pid_t>        _live           ;                          // threads of the job that are alive, so that processes surviving the job can be killed, XXX : not used yet
	} ;

	static Comment _io_comment(int opcode) {
		switch (opcode) {
			case IORING_OP_OPENAT    : return Comment::openat    ;
			case IORING_OP_OPENAT2   : return Comment::openat2   ;
			case IORING_OP_STATX     : return Comment::statx     ;
			case IORING_OP_RENAMEAT  : return Comment::renameat2 ;
			case IORING_OP_UNLINKAT  : return Comment::unlinkat  ;
			case IORING_OP_MKDIRAT   : return Comment::mkdirat   ;
			case IORING_OP_SYMLINKAT : return Comment::symlinkat ;
			case IORING_OP_LINKAT    : return Comment::linkat    ;
			default                  : return Comment::None      ;
		}
	}

	// the syscall is over : interpret captures
	void Consumer::_end( Thread const& th , EbpfEvent const& ev ) {
		Comment c ;
		if      (ev.flags&EBPF_F_IO                         ) c = _io_comment(ev.nr)                   ;
		else if ( ev.nr<0 || ev.nr>=SyscallDescr::NSyscalls ) c = Comment::None                        ;
		#if HAS_32
			else if (ev.is32)                                 c = SyscallDescr::s_tab32[ev.nr].comment ;
		#endif
		else                                                  c = SyscallDescr::s_tab  [ev.nr].comment ;
		// date
		if (ev.stamp_ns      )   _date = Pdate( New , Pdate::Tick(ev.stamp_ns-_tai_offset_s*1'000'000'000l) ) ;
		else                     _date = New                                                                  ;
		if (_date<=_last_date) { _date = _last_date ; _date++ ; }                                               // ensure syscalls are seen in order, even if clock is coarse
		//
		int64_t         rc   = ev.rc          ;
		int             err  = rc<0 ? -rc : 0 ;
		uint64_t const* args = ev.args        ;
		// accessors
		auto first = [&](uint8_t kind)->Obj const* { for( Obj const& o : th.objs          ) if ( !o.raw && o.kind==kind                   ) return &o          ; return nullptr ; } ;
		auto last  = [&](uint8_t kind)->Obj const* { for( size_t i=th.objs.size() ; i-- ; ) if ( !th.objs[i].raw && th.objs[i].kind==kind ) return &th.objs[i] ; return nullptr ; } ;
		auto raw   = [&](int     slot)->Obj const* { for( size_t i=th.objs.size() ; i-- ; ) if (  th.objs[i].raw && th.objs[i].slot==slot ) return &th.objs[i] ; return nullptr ; } ;
		auto obj   = [&]( uint8_t kind , int slot=0 )->Obj const* { if ( Obj const* o=last(kind) ; o ) return o ; return raw(slot) ; } ;                      // prefer what kernel has resolved
		// links traversed during path resolution, whatever the syscall
		if (_env.lnk_support!=LnkSupport::None)
			for( Lnk const& l : th.lnks ) _handle_dep( nullptr , l.loc , ::copy(l.real) , Access::Lnk , l.info , true/*read*/ , c , {} , CommentExt::Link ) ; // a link may lie in a view
		//
		auto solve_only = [&]( uint8_t kind , bool no_follow ) { _solve( th , obj(kind) , no_follow , false/*read*/ , c , {} , err ) ; } ;
		switch (c) {
			// open
			case Comment::open    :
			case Comment::openat  :
			case Comment::openat2 :
			case Comment::creat   : {
				Obj const* o = first(EBPF_OPEN) ;                                         // following ones, if any, are internal to the kernel (e.g. backing files)
				if (o) {
					Obj o2 = *o ; if (o2.created) o2.info = {} ;                          // file did not exist before open
					_open( th , &o2 , int(ev.open_flags) , rc , c ) ;
				} else {
					_open( th , raw(0) , int(ev.open_flags) , rc , c ) ;
				}
			} break ;
			case Comment::truncate : _open( th , obj(EBPF_LOOKUP) , args[1]?O_RDWR:O_WRONLY , rc , c ) ; break ;
			// stat
			case Comment::stat       :
			case Comment::stat64     : //!                            no_follow
			case Comment::oldstat    : _stat( th , obj(EBPF_LOOKUP) , false                             , FullAccesses , rc , c ) ; break ;
			case Comment::lstat      :
			case Comment::lstat64    :
			case Comment::oldlstat   : _stat( th , obj(EBPF_LOOKUP) , true                              , FullAccesses , rc , c ) ; break ;
			case Comment::fstatat64  :
			case Comment::newfstatat : _stat( th , obj(EBPF_LOOKUP) , bool(args[3]&AT_SYMLINK_NOFOLLOW) , FullAccesses , rc , c ) ; break ;
			case Comment::statx      : {
				#if defined(STATX_TYPE) && defined(STATX_SIZE) && defined(STATX_BLOCKS) && defined(STATX_MODE)
					uint     msk = args[3] ;
					Accesses a   ;
					if      (msk&(STATX_TYPE|STATX_SIZE|STATX_BLOCKS)) a = FullAccesses ; // user can distinguish all content
					else if (msk& STATX_MODE                         ) a = Access::Reg  ; // user can distinguish executable files, which is part of crc for regular files
				#else
					Accesses a = FullAccesses ;                                           // if access macros are not defined, be pessimistic
				#endif
				_stat( th , obj(EBPF_LOOKUP) , bool(args[2]&AT_SYMLINK_NOFOLLOW) , a , rc , c ) ;
			} break ;
			case Comment::access     :
			case Comment::faccessat  :
			case Comment::faccessat2 : {
				bool at        = c!=Comment::access                                         ;
				bool no_follow = c==Comment::faccessat2 && (args[3]&AT_SYMLINK_NOFOLLOW)    ;
				if (args[at+1]&X_OK) _stat     ( th , obj(EBPF_LOOKUP) , no_follow , Access::Reg , rc , c ) ;                                    // exe bit is part of file crc
				else                 solve_only(      EBPF_LOOKUP      , no_follow                        ) ;
			} break ;
			// mere path accesses
			case Comment::statfs            :
			case Comment::statfs64          :
			case Comment::umount            :
			case Comment::umount2           :
			case Comment::utime             :
			case Comment::utimes            : solve_only( EBPF_LOOKUP , false                           ) ; break ;
			case Comment::utimensat         : solve_only( EBPF_LOOKUP , args[3]&AT_SYMLINK_NOFOLLOW     ) ; break ;
			case Comment::open_tree         : solve_only( EBPF_LOOKUP , args[2]&AT_SYMLINK_NOFOLLOW     ) ; break ;
			case Comment::name_to_handle_at : solve_only( EBPF_LOOKUP , true                            ) ; break ;
			case Comment::mknod             :
			case Comment::mknodat           : solve_only( EBPF_CREATE , true                            ) ; break ;
			//
			case Comment::chdir : {                                                                                                              // cf. Record::Chdir
				Obj const* o = obj(EBPF_LOOKUP)                                                ;
				Solved     s = _solve( th , o , false/*no_follow*/ , false/*read*/ , c , {} , err ) ;
				if (o) _dep( s , {} , c ) ;
			} break ;
			case Comment::chmod    :
			case Comment::fchmodat : {                                                                                                           // cf. Record::Chmod
				bool       at        = c==Comment::fchmodat                                     ;
				bool       exe       = args[at+1]&S_IXUSR                                       ;
				bool       no_follow = at && (args[3]&AT_SYMLINK_NOFOLLOW)                      ;
				Obj const* o         = obj(EBPF_LOOKUP)                                         ;
				Solved     s         = _solve( th , o , no_follow , true/*read*/ , c , {} , err ) ;                                              // behave like a read-modify-write
				if ( o && s.loc<=FileLoc::Dep && s.info.exists() && exe!=(s.info.tag()==FileTag::Exe) ) _update( s , Access::Reg , rc>=0 , c ) ; // only consider as a target if exe bit changes
			} break ;
			case Comment::chroot     :
			case Comment::mount      :
			case Comment::move_mount : {                                                              // cf. Record::Chroot and Record::Mount, XXX : mounting outside repo is not seen
				Obj const* o = obj( EBPF_LOOKUP , c==Comment::move_mount?1:0 )                    ;
				Solved     s = _solve( th , o , true/*no_follow*/ , false/*read*/ , c , {} , err ) ;
				if ( o && rc>=0 ) _rec.report_direct({ .proc=c==Comment::chroot?Proc::Chroot:Proc::Mount , .comment=c , .files={{s.real,c==Comment::chroot?FileInfo():s.info}} }) ;
			} break ;
			case Comment::execve   :
			case Comment::execveat : {                                                                // cf. Record::Exec
				// the kernel opens the file and its interpreters (recursively), and what it cannot open is reported as well
				for( Obj const& o : th.objs ) {
					if ( !o.raw && o.kind!=EBPF_OPEN ) continue ;
					Solved s = _solve( th , &o , false/*no_follow*/ , true/*read*/ , c , {} , err ) ; // if an open failed, it is what makes execve fail
					_dep( s , Access::Reg , c ) ;
					_tick() ;
				}
			} break ;
			case Comment::getdents   :
			case Comment::getdents64 :
			case Comment::readdir    : {                                                                                                           // cf. Record::ReadDir
				Obj const* o = last(EBPF_DIR) ;
				Solved     s = _solve( th , o , false/*no_follow*/ , false/*read*/ , c ) ;
				if ( o && rc>=0 && !_env.readdir_ok ) {
					if (s.loc==FileLoc::RepoRoot) _report( FileLoc::Repo , "."            , {.read_dir=true} , {} , c ) ;                          // repo root must be analyzed when reading it
					else                          _report( s.loc         , ::copy(s.real) , {.read_dir=true} , {} , c ) ;
				}
			} break ;
			case Comment::link   :
			case Comment::linkat : {                                                                                                               // cf. Record::Lnk
				bool       no_follow = c==Comment::linkat && (args[4]&AT_SYMLINK_NOFOLLOW)                                    ;
				Obj const* osrc      = obj(EBPF_LOOKUP,0)                                                                     ;
				Obj const* odst      = obj(EBPF_CREATE,1)                                                                     ;
				Solved     src       = _solve( th , osrc , no_follow         , true /*read*/ , c , CommentExt::Read  , err , true/*ambiguous*/ ) ;
				Solved     dst       = _solve( th , odst , true/*no_follow*/ , false/*read*/ , c , CommentExt::Write , err , true/*ambiguous*/ ) ;
				if ( osrc && odst && src.real==dst.real ) break ;                                                                                  // posix says it is nop in that case
				Accesses sa = Access::Reg ; if (no_follow) sa |= Access::Lnk ;                                                                     // if no_follow, a sym link may be hard linked
				if (osrc) _dep   ( src , sa                   , c , CommentExt::Read  ) ;
				if (odst) _update( dst , Access::Stat , rc>=0 , c , CommentExt::Write ) ;                                                          // writing to dst is sensitive to existence
			} break ;
			case Comment::mkdir   :
			case Comment::mkdirat : {                                                                             // cf. Record::Mkdir
				Obj const* o = obj(EBPF_CREATE)                                                  ;
				Solved     s = _solve( th , o , true/*no_follow*/ , false/*read*/ , c , {} , err ) ;
				if (!o) break ;
				_guard( s.loc , s.real )      ;                                                                   // although dirs are not considered targets, it stays that disk has been modified
				_dep  ( s , Access::Stat , c ) ;                                                                  // fails if file exists, hence sensitive to existence
			} break ;
			case Comment::readlink   :
			case Comment::readlinkat : {                                                                          // cf. Record::Readlink, backdoor is filtered out by the kernel
				Obj const* o = obj(EBPF_LOOKUP)                                                 ;
				Solved     s = _solve( th , o , true/*no_follow*/ , true/*read*/ , c , {} , err ) ;
				if (o) _dep( s , Access::Lnk , c ) ;
			} break ;
			case Comment::rename    :
			case Comment::renameat  :
			case Comment::renameat2 : {
				uint flags = c==Comment::renameat2 ? uint(args[4]) : 0 ;
				#ifdef RENAME_EXCHANGE
					bool exchange = flags&RENAME_EXCHANGE ;
				#else
					bool exchange = false                 ;
				#endif
				#ifdef RENAME_NOREPLACE
					bool no_replace = flags&RENAME_NOREPLACE ;
				#else
					bool no_replace = false                  ;
				#endif
				_rename( th , obj(EBPF_RENAME_SRC,0) , obj(EBPF_RENAME_DST,1) , exchange , no_replace , rc , c ) ;
			} break ;
			case Comment::symlink   :
			case Comment::symlinkat : {                                                                           // cf. Record::Symlink
				Obj const* o = obj(EBPF_CREATE)                                                  ;
				Solved     s = _solve( th , o , true/*no_follow*/ , false/*read*/ , c , {} , err ) ;
				if (o) _update( s , Access::Stat , rc>=0 , c ) ;                                                  // fail if file exists, hence sensitive to existence
			} break ;
			case Comment::rmdir    :
			case Comment::unlink   :
			case Comment::unlinkat : {                                                                            // cf. Record::Unlnk
				bool       remove_dir = c==Comment::rmdir || ( c==Comment::unlinkat && (args[2]&AT_REMOVEDIR) ) ;
				Obj const* o          = obj(EBPF_UNLINK)                                                        ;
				Solved     s          = _solve( th , o , true/*no_follow*/ , false/*read*/ , c , {} , err )     ;
				if (!o) break ;
				if (remove_dir) _guard ( s.loc , s.real_write()          ) ;
				else            _update( s , Access::Stat , rc>=0 , c    ) ;                                      // fail if file does not exist, hence sensitive to existence
			} break ;
			default : ;                                                                                           // not a tracked syscall, traversed links have been reported and this is enough
		}
		_rec.send_report() ;
		_last_date = _date ;
	}

	void Consumer::handle( void* data , size_t sz ) {
		if (sz<EBPF_HDR_SZ) return ;
		EbpfEvent const& ev = *static_cast<EbpfEvent const*>(data) ;
		try {
			switch (ev.kind) {
				case EBPF_JOB_END      : done = true ;          break           ;
				case EBPF_PROC_START   : _live.insert(ev.pid) ; [[fallthrough]] ;
				case EBPF_THREAD_START : {
					Thread& th = _threads[ev.tid] ;
					th.tid = ev.tid ;
					th.pid = ev.pid ;
				} break ;
				case EBPF_PROC_END   : _live   .erase(ev.pid) ; [[fallthrough]] ;
				case EBPF_THREAD_END : _threads.erase(ev.tid) ; break           ;
				case EBPF_LINK : {
					Lnk lnk ;
					::tie(lnk.real,lnk.loc) = _mk_real( ev , &lnk.abs ) ;
					lnk.info = _mk_info(ev) ;
					if ( (ev.flags&EBPF_F_TARGET) && ev.len[1] ) {
						lnk.target     = ::string( reinterpret_cast<const char*>(ev.data+ev.len[0]) , ev.len[1]-1/*null*/ ) ;
						lnk.has_target = true                                                                               ;
					}
					_threads.at(ev.tid).lnks.push_back(::move(lnk)) ;
				} break ;
				case EBPF_LOOKUP     :
				case EBPF_OPEN       :
				case EBPF_CREATE     :
				case EBPF_UNLINK     :
				case EBPF_RENAME_SRC :
				case EBPF_RENAME_DST :
				case EBPF_DIR        : {
					Obj o { .kind=ev.kind , .exists=bool(ev.flags&EBPF_F_EXISTS) , .created=bool(ev.flags&EBPF_F_CREATED) , .open_flags=ev.open_flags , .mode=ev.mode } ;
					if ( ev.kind==EBPF_RENAME_SRC || ev.kind==EBPF_RENAME_DST ) o.other_mode = ev.open_flags ;
					::tie(o.real,o.loc) = _mk_real(ev) ;
					o.info = _mk_info(ev) ;
					_threads.at(ev.tid).objs.push_back(::move(o)) ;
				} break ;
				case EBPF_RAW : {
					Obj o { .kind=ev.kind , .raw=true , .slot=ev.nr } ;
					_mk_real( ev , &o.dir ) ;
					if (ev.len[1]) o.path = ::string( reinterpret_cast<const char*>(ev.data+ev.len[0]) , ev.len[1]-1/*null*/ ) ;
					_threads.at(ev.tid).objs.push_back(::move(o)) ;
				} break ;
				case EBPF_END : {
					Thread& th = _threads.at(ev.tid) ;
					if (!(ev.flags&EBPF_F_DISCARD)) _end( th , ev ) ;
					th = {} ;
				} break ;
				case EBPF_PANIC :
					throw ::string( reinterpret_cast<const char*>(ev.data) , ev.len[0]-1/*null*/ ) ;
			DF}
		} catch (::string const& e) {
			if (!err) err = e ;
		}
	}

	static int _handle_event( void* ctx , void* data , size_t sz ) {
		static_cast<Consumer*>(ctx)->handle(data,sz) ;
		return 0 ;
	}

	//
	// daemon protocol
	//

	// send hello and the root dir fds as an SCM_RIGHTS ancillary message
	static bool/*ok*/ _send_hello( int fd , EbpfDaemon::Hello const& hello , ::vector<AcFd> const& root_fds ) {
		const char* p    = reinterpret_cast<const char*>(&hello) ;
		size_t      sz   = sizeof(hello)                         ;
		bool        first = true                                 ;
		while (sz) {
			struct iovec  iov                                          = { .iov_base=const_cast<char*>(p) , .iov_len=sz } ;
			char          cbuf[CMSG_SPACE(EBPF_MAX_ROOTS*sizeof(int))] = {}                                               ;
			struct msghdr msg                                          = {}                                               ; msg.msg_iov = &iov ; msg.msg_iovlen = 1 ;
			if ( first && +root_fds ) {
				msg.msg_control    = cbuf                                    ;
				msg.msg_controllen = CMSG_SPACE(root_fds.size()*sizeof(int)) ;
				//
				struct cmsghdr* cm = CMSG_FIRSTHDR(&msg) ;
				cm->cmsg_level = SOL_SOCKET                             ;
				cm->cmsg_type  = SCM_RIGHTS                             ;
				cm->cmsg_len   = CMSG_LEN(root_fds.size()*sizeof(int))  ;
				for( size_t i : iota(root_fds.size()) ) ::memcpy( CMSG_DATA(cm)+i*sizeof(int) , &root_fds[i].fd , sizeof(int) ) ;
			}
			ssize_t n = ::sendmsg( fd , &msg , MSG_NOSIGNAL ) ; if (n<=0) return false ;
			p     += n         ;
			sz    -= size_t(n) ;
			first  = false     ;
		}
		return true ;
	}

	// receive the reply and the ring buffer fd sent as an SCM_RIGHTS ancillary message
	static ::pair<AcFd/*ring_buf*/,uint32_t/*job_id*/> _recv_reply(Fd fd) {
		::pair<AcFd/*ring_buf*/,uint32_t/*job_id*/> res ;
		//
		EbpfDaemon::Reply reply                         ;
		::iovec           iov                           = { .iov_base=&reply , .iov_len=sizeof(reply)                                                                                            } ;
		char              cbuf[CMSG_SPACE(sizeof(int))] = {}                                                                                                                                       ;
		::msghdr          msg                           = { .msg_name=nullptr , .msg_namelen=0 , .msg_iov=&iov , .msg_iovlen=1 , .msg_control=cbuf , .msg_controllen=sizeof(cbuf) , .msg_flags=0 } ;
		//
		throw_unless( ::recvmsg(fd,&msg,MSG_CMSG_CLOEXEC)==ssize_t(sizeof(reply)) , "the ebpf daemon does not answer" ) ;
		//
		for( struct cmsghdr* cm=CMSG_FIRSTHDR(&msg) ; cm ; cm=CMSG_NXTHDR(&msg,cm) )
			if ( cm->cmsg_level==SOL_SOCKET && cm->cmsg_type==SCM_RIGHTS ) { SWEAR(!res.first) ; ::memcpy( &res.first.fd , CMSG_DATA(cm) , sizeof(int) ) ; }
		throw_unless( +res.first && reply.job_id , "the ebpf daemon could not register the job (cf. its log)" ) ;
		res.second = reply.job_id ;
		return res ;
	}

	// tell the kernel that dir is not of interest, although it may lie in the repo
	static bool/*ok*/ _exclude(::string const& dir) {
		int dir_fd = ::open( dir.c_str() , O_PATH|O_DIRECTORY|O_CLOEXEC ) ; if (dir_fd<0) return true ; // nothing can be accessed there
		EbpfDaemon::ReqKey req                           = EbpfDaemon::ReqKey::Exclude     ;
		struct iovec       iov                           = { .iov_base=&req , .iov_len=1 } ;
		char               cbuf[CMSG_SPACE(sizeof(int))] = {}                              ;
		struct msghdr      msg                           = {}                              ; msg.msg_iov=&iov ; msg.msg_iovlen=1 ; msg.msg_control=cbuf ; msg.msg_controllen=sizeof(cbuf) ;
		struct cmsghdr*    cm = CMSG_FIRSTHDR(&msg)      ;
		cm->cmsg_level = SOL_SOCKET            ;
		cm->cmsg_type  = SCM_RIGHTS            ;
		cm->cmsg_len   = CMSG_LEN(sizeof(int)) ;
		::memcpy( CMSG_DATA(cm) , &dir_fd , sizeof(int) ) ;
		bool ok = ::sendmsg(_g_sock,&msg,MSG_NOSIGNAL)==1 ;
		::close(dir_fd) ;                                                                               // daemon keeps it open
		char reply = 0 ;
		return ok && ::recv(_g_sock,&reply,1,0/*flags*/)==1 && reply ;
	}

	//
	// hooks called by Gather (set by load())
	//

	// runs before child is spawned, once namespaces have been entered
	// the tmp dir usually lies in the repo (LMAKE/auto_tmp) and may be seen elsewhere by the job (tmp_view), in which case its files could be mistaken for repo files
	static void _start(AutodepEnv const& autodep_env) {
		if (+autodep_env.tmp_dir_s) throw_unless( _exclude(no_slash(autodep_env.tmp_dir_s)) , "cannot exclude tmp dir ",autodep_env.tmp_dir_s," from ebpf tracking" ) ;
	}


	// runs in the child, just before execve (must be malloc-free) : self-register (with our job_id) via a marker syscall, so that the kernel knows us with the tid it sees
	static int/*rc*/ _prepare_child(void*) {
		char c ;
		::syscall( SYS_readlinkat , EBPF_REGISTER_FD , "" , &c , long(_g_job_id) ) ; // job_id carried in arg 3 ; returns EBADF, harmless
		return 0 ;
	}

	// runs in the tracer thread : consume this job's ring buffer until the child exits, then return its wstatus
	static int/*wstatus*/ _process(pid_t child_pid) {
		Lock           lock { Record::s_mutex }                                                        ; // single consumer thread, but Record code asserts the lock is held (like the ptrace tracer)
		Consumer       cons ;
		::ring_buffer* rb   = ::ring_buffer__new( _g_rb_fd , _handle_event , &cons , nullptr/*opts*/ ) ; throw_unless( rb , "cannot open ebpf ring buffer" ) ;
		//
		// wait for both events and end of child, so that the latter is seen at once (else, each job would last the polling period)
		int      wstatus = 0                                                                                                      ;
		AcFd     pid_fd  { int(::syscall( SYS_pidfd_open , child_pid , 0 ))                                                     } ; // if not available, child is polled
		::pollfd pfds[2] { {.fd=::ring_buffer__epoll_fd(rb),.events=POLLIN,.revents=0} , {.fd=pid_fd,.events=POLLIN,.revents=0} } ;
		for(;;) {
			int   r = ::poll( pfds , +pid_fd?2:1 , +pid_fd?-1:10/*ms*/ ) ; if ( r<0 && errno!=EINTR ) break ;
			pid_t w = ::waitpid( child_pid , &wstatus , WNOHANG )        ;
			::ring_buffer__consume(rb) ;
			if ( w==child_pid        ) break ;           // child is done
			if ( w<0 && errno!=EINTR ) break ;           // ECHILD or other : give up waiting
		}
		::ring_buffer__free(rb) ;
		throw_if    ( +cons.err , cons.err                                      ) ;
		throw_unless( cons.done , "some accesses could not be recorded by ebpf" ) ;
		return wstatus ;
	}

	// connect to the daemon, upload our pid namespace, the tracked syscalls and the dirs of interest, and receive this job's ring buffer
	void load( bool deps_in_system , ::string const& phy_repo_root_s , ::vector_s const& src_dirs_s ) {
		if (+_g_sock) return ;                                                                          // already connected (defensive)
		//
		::string sock_name = EbpfDaemon::get_sock_name()                                      ;
		AcFd     s         { ::socket( AF_UNIX , SOCK_STREAM|SOCK_CLOEXEC , 0/*protocol*/ ) } ; throw_unless( +s , "cannot create ebpf socket" ) ;
		//
		struct sockaddr_un addr = {} ; addr.sun_family=AF_UNIX ; ::strncpy(addr.sun_path,sock_name.c_str(),sizeof(addr.sun_path)-1) ;
		for( int _ : iota(EbpfDaemon::NTrials) ) {
			if (::connect(s,reinterpret_cast<struct sockaddr*>(&addr),sizeof(addr))==0) goto Connected ;
			Child child {
				.as_session = true
			,	.cmd_line   = { *g_lmake_root_s+"_bin/lmake_bpfd" }
			,	.stdout     = Child::PipeFd
			} ;
			child.spawn() ;
			::string reply = child.stdout.read() ;
			switch (reply[0]) {
				case '0' : child.wait     () ; break ;                                                  // daemon is already running, wait for this new one that will terminate shortly
				case '1' : child.mk_daemon() ; break ;                                                  // daemon is starting, let it live its daemon life
				default  : child.wait     () ; throw_unless( +reply ,"cannot launch ebpf daemon" ) ;
			}
		}
		throw cat("cannot reach the ebpf daemon on ",sock_name," after ",EbpfDaemon::NTrials," trials") ;
	Connected :
		// a stuck daemon must not block the job forever, daemon answers within a few rcu grace periods
		{	struct timeval tv = { .tv_sec=60 , .tv_usec=0 } ;
			::setsockopt( s , SOL_SOCKET , SO_RCVTIMEO , &tv , sizeof(tv) ) ;
			::setsockopt( s , SOL_SOCKET , SO_SNDTIMEO , &tv , sizeof(tv) ) ;
		}
		// hello : our pid namespace
		EbpfDaemon::Hello hello = {} ;
		{	struct ::stat st ;
			if (::stat("/proc/self/ns/pid",&st)==0) {
				hello.pidns_dev = st.st_dev ;
				hello.pidns_ino = st.st_ino ;
			}
		}
		hello.deps_in_system = deps_in_system ;
		// dirs of interest : the repo and source dirs lying outside the repo (when inside, they are part of the repo)
		// they are opened here so that the daemon can make the kernel know them, independently of the way the job will see them (chroot, views, ...)
		::vector<AcFd> root_fds ;                                                                                                        // daemon keeps them open
		auto add_root = [&]( ::string const& dir_s , size_t r ) {
			throw_unless( root_fds.size()<EBPF_MAX_ROOTS , "too many source dirs for autodep method ebpf (max ",EBPF_MAX_ROOTS-1,')' ) ;
			AcFd fd { ::open( dir_s.c_str() , O_PATH|O_DIRECTORY|O_CLOEXEC ) } ; if (!fd) return ;                                       // nothing can be accessed there
			root_fds.push_back(::move(fd)) ;
			_g_roots.push_back(       r  ) ;
		} ;
		add_root( phy_repo_root_s , Npos ) ;
		throw_unless( +root_fds , "cannot open repo root ",phy_repo_root_s ) ;
		for( size_t i : iota(src_dirs_s.size()) ) {
			::string abs_sd_s = mk_glb(src_dirs_s[i],phy_repo_root_s) ;
			if (!abs_sd_s.starts_with(phy_repo_root_s)) add_root( abs_sd_s , i ) ;
		}
		hello.n_roots = root_fds.size() ;
		//
		bool ok = _send_hello(s,hello,root_fds) ; throw_unless( ok , "cannot talk to the ebpf daemon" ) ;
		::pair<AcFd/*ring_buf*/,uint32_t/*job_id*/> rb_ji = _recv_reply(s) ;
		//
		_g_sock   = s          .detach() ; // keep the connection open : closing it releases the job in the daemon
		_g_rb_fd  = rb_ji.first.detach() ;
		_g_job_id = rb_ji.second         ;
		//
		Gather::s_ebpf_start         = _start         ;
		Gather::s_ebpf_prepare_child = _prepare_child ;
		Gather::s_ebpf_process       = _process       ;
	}

}
