// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"

#include "real_path.hh"

using namespace Disk ;

static FileLoc _lcl_file_loc(::string_view file) {
	static ::string_view s_private_admin_dir { PrivateAdminDirS , ::strlen(PrivateAdminDirS)-1 } ; // get rid of final /
	if (!file.starts_with(s_private_admin_dir)) return FileLoc::Repo ;
	switch (file[s_private_admin_dir.size()]) {
		case '/' :
		case 0   : return FileLoc::Admin ;
		default  : return FileLoc::Repo  ;
	}
}

//
// RealPathEnv
//

::string& operator+=( ::string& os , RealPathEnv const& rpe ) {    // START_OF_NO_COV
	/**/                 os << "RealPathEnv(" << rpe.lnk_support ;
	if (+rpe.file_sync ) os <<','<< rpe.file_sync                ;
	/**/                 os <<','<< rpe.repo_root_s              ;
	if (+rpe.tmp_dir_s ) os <<','<< rpe.tmp_dir_s                ;
	if (+rpe.src_dirs_s) os <<','<< rpe.src_dirs_s               ;
	return               os <<')'                                ;
}                                                                  // END_OF_NO_COV

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
		if ( lies_within( is_abs(sd_s)?abs_real:lcl_real , sd_s ) ) return FileLoc::SrcDir ;
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

//
// RealPath
//

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
	/**/                SWEAR( is_abs(rpe.repo_root_s) , rpe.repo_root_s ) ;
	if (+rpe.tmp_dir_s) SWEAR( is_abs(rpe.tmp_dir_s  ) , rpe.tmp_dir_s   ) ;
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
RealPath::SolveReport RealPath::solve( FileView file , bool no_follow ) {
	static constexpr int NMaxLnks = _POSIX_SYMLOOP_MAX ;                  // max number of links to follow before decreting it is a loop
	//
	::string_view tmp_dir_s = +_env->tmp_dir_s ? ::string_view(_env->tmp_dir_s) : ::string_view(P_tmpdir "/") ;
	//
	SolveReport res           ;
	::vector_s& lnks          = res.lnks          ;
	::string  & real          = res.real          ;                       // canonical (link free, absolute, no ., .. nor empty component), empty instead of '/'
	::string    local_file[2] ;                                           // ping-pong used to keep a copy of input file if we must modify it (avoid upfront copy as it is rarely necessary)
	bool        ping          = false/*garbage*/  ;                       // ping-pong state
	bool        exists        = true              ;                       // if false, we have seen a non-existent component and there cannot be symlinks within it
	size_t      pos           = is_abs(file.file) ;
	if (!pos) {                                                           // file is relative, meaning relative to at
		if (file.at==Fd::Cwd) {
			if (pid) real = read_lnk(cat("/proc/",pid,"/cwd")) ;
			else     real = cwd()                              ;
		} else {
			if (pid) real = read_lnk(cat("/proc/",pid,"/fd/",file.at.fd)) ;
			else     real = read_lnk(cat("/proc/self/fd/"   ,file.at.fd)) ;
		}
		//
		if (!real         ) return {} ;                                   // user code might use the strangest at, it will be an error but we must support it
		if (real.size()==1) real.clear() ;                                // if '/', we must substitute the empty string to enforce invariant
	}
	real.reserve(real.size()+1+file.file.size()) ;                        // anticipate no link
	_Dvg in_repo { _env->repo_root_s , real } ;                           // keep track of where we are w.r.t. repo , track symlinks according to lnk_support policy
	_Dvg in_tmp  { tmp_dir_s         , real } ;                           // keep track of where we are w.r.t. tmp  , always track symlinks
	_Dvg in_proc { "/proc/"          , real } ;                           // keep track of where we are w.r.t. /proc, always track symlinks
	// loop INVARIANT : accessed file is real+'/'+file.substr(pos)
	// when pos>file.size(), we are done and result is real
	size_t   end      ;
	int      n_lnks   = 0 ;
	::string last_lnk ;
	for (
	;	pos <= file.file.size()
	;		pos = end + 1
		,	in_repo.update( _env->repo_root_s , real )                    // all domains start only when inside, i.e. the domain root is not part of the domain
		,	in_tmp .update( tmp_dir_s         , real )                    // .
		,	in_proc.update( "/proc/"          , real )                    // .
	) {
		end = file.file.find( '/' , pos ) ;
		bool last = end==Npos ;
		if (last    ) end = file.file.size() ;
		if (end==pos) continue ;                                          // empty component, ignore
		if (file.file[pos]=='.') {
			if ( end==pos+1                          ) continue ;         // component is .
			if ( end==pos+2 && file.file[pos+1]=='.' ) {                  // component is ..
				if (+real) real.resize(real.rfind('/')) ;
				continue ;
			}
		}
		size_t prev_real_size = real.size() ;
		size_t src_idx        = Npos        ;                             // Npos means not in a source dir
		real.push_back('/')           ;
		real.append(file.file,pos,end-pos) ;
		if ( !exists             ) continue       ;                       // if !exists, no hope to find a symbolic link but continue cleanup of empty, . and .. components
		if ( no_follow && last   ) continue       ;                       // dont care about last component if no_follow
		if ( +in_tmp || +in_proc ) goto HandleLnk ;                       // note that tmp can lie within repo
		if ( +in_repo            ) {
			if (real.size()<_repo_root_sz) continue ;                     // at repo root, no sym link to handle
		} else {
			src_idx = _find_src_idx(real) ;
			if (src_idx==Npos) continue ;
		}
		//
		switch (_env->lnk_support) {
			case LnkSupport::None :            continue ;
			case LnkSupport::File : if (!last) continue ;                 // only handle sym links as last component
		DN}                                                               // NO_COV
	HandleLnk :
		::string& nxt = local_file[ping] ;                                // bounce, initially, when file is neither local_file's, any buffer is ok
		nxt = read_lnk( real , &_nfs_guard ) ;
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
		if (n_lnks++>=NMaxLnks) return {{},::move(lnks)} ;                // link loop detected, same check as system
		if (!last             )   nxt << '/'<<(file.file.data()+end+1) ;  // append unprocessed part, avoiding this copy would be very complex (would need a stack) and links to dir are uncommon
		if (nxt[0]=='/'       ) { end =  0 ; prev_real_size = 0 ; }       // absolute link target : flush real
		else                      end = -1 ;                              // end must point to the /, invent a virtual one before the string
		real.resize(prev_real_size) ;                                     // links are relative to containing dir, suppress last component
		ping      = !ping ;
		file.file = nxt   ;
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
			AcFd     hdr_fd { abs_real , {.err_ok=true} } ; if    ( !hdr_fd                                               ) break ;
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
