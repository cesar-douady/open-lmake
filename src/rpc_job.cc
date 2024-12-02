// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/mount.h>

#include "disk.hh"
#include "hash.hh"
#include "trace.hh"

#include "rpc_job.hh"

using namespace Disk ;
using namespace Hash ;

//
// FileAction
//

::string& operator+=( ::string& os , FileAction const& fa ) {
	/**/                                os << "FileAction(" << fa.tag ;
	if (fa.tag<=FileActionTag::HasFile) os <<','<< fa.sig             ;
	return                              os <<')'                      ;
}

::pair_s<bool/*ok*/> do_file_actions( ::vector_s* unlnks/*out*/ , ::vmap_s<FileAction>&& pre_actions , NfsGuard& nfs_guard ) {
	::uset_s keep_dirs ;
	::string msg       ;
	bool     ok        = true ;
	//
	Trace trace("do_file_actions",pre_actions) ;
	if (unlnks) unlnks->reserve(unlnks->size()+pre_actions.size()) ;                                       // most actions are unlinks
	for( auto const& [f,a] : pre_actions ) {                                                               // pre_actions are adequately sorted
		SWEAR(+f) ;                                                                                        // acting on root dir is non-sense
		switch (a.tag) {
			case FileActionTag::Unlink         :
			case FileActionTag::UnlinkWarning  :
			case FileActionTag::UnlinkPolluted :
			case FileActionTag::None           : {
				FileSig sig { nfs_guard.access(f) } ;
				if (!sig) break ;                                                                          // file does not exist, nothing to do
				bool done       = true/*garbage*/                                                        ;
				bool quarantine = sig!=a.sig && (a.crc==Crc::None||!a.crc.valid()||!a.crc.match(Crc(f))) ;
				if (quarantine) {
					done = ::rename( f.c_str() , dir_guard(QuarantineDirS+f).c_str() )==0 ;
					if (done) msg<<"quarantined "         <<mk_file(f)<<'\n' ;
					else      msg<<"failed to quarantine "<<mk_file(f)<<'\n' ;
				} else {
					SWEAR(is_lcl(f)) ;
					try {
						done = unlnk(nfs_guard.change(f)) ;
						if (a.tag==FileActionTag::None) { if ( done) msg << "unlinked "           << mk_file(f) << '\n' ; }
						else                            { if (!done) msg << "file disappeared : " << mk_file(f) << '\n' ; }
						done = true ;
					} catch (::string const& e) {
						msg <<  e << '\n' ;
						done = false ;
					}
				}
				trace(STR(quarantine),STR(done),f) ;
				if ( done && unlnks ) unlnks->push_back(f) ;
				ok &= done ;
			} break ;
			case FileActionTag::NoUniquify : if (can_uniquify(nfs_guard.change(f))) msg<<"did not uniquify "<<mk_file(f)<<'\n' ; break ;
			case FileActionTag::Uniquify   : if (uniquify    (nfs_guard.change(f))) msg<<"uniquified "      <<mk_file(f)<<'\n' ; break ;
			case FileActionTag::Mkdir      : mk_dir_s(with_slash(f),nfs_guard) ;                                                 break ;
			case FileActionTag::Rmdir      :
				if (!keep_dirs.contains(f))
					try {
						rmdir_s(with_slash(nfs_guard.change(f))) ;
					} catch (::string const&) {                                                            // if a dir cannot rmdir'ed, no need to try those uphill
						keep_dirs.insert(f) ;
						for( ::string d_s=dir_name_s(f) ; +d_s ; d_s=dir_name_s(d_s) )
							if (!keep_dirs.insert(no_slash(d_s)).second) break ;
					}
			break ;
		DF}
	}
	trace("done",STR(ok),localize(msg)) ;
	return {msg,ok} ;
}

//
// JobReason
//

::string& operator+=( ::string& os , JobReason const& jr ) {
	os << "JobReason(" << jr.tag ;
	if (jr.tag>=JobReasonTag::HasNode) os << ',' << jr.node ;
	return os << ')' ;
}

//
// EndAttrs
//

::string& operator+=( ::string& os , EndAttrs const& ea ) {
	First first ;
	/**/                    os << "EndAttrs("                      ;
	if (+ea.cache_key     ) os <<first("",",")<< ea.cache_key      ;
	if (+ea.max_stderr_len) os <<first("",",")<< ea.max_stderr_len ;
	return                  os << ')'                              ;
}

//
// DepInfo
//

::string& operator+=( ::string& os , DepInfo const& di ) {
	switch (di.kind) {
		case DepInfoKind::Crc  : return os <<'('<< di.crc () <<')' ;
		case DepInfoKind::Sig  : return os <<'('<< di.sig () <<')' ;
		case DepInfoKind::Info : return os <<'('<< di.info() <<')' ;
	DF}
}

//
// JobSpace
//

::string& operator+=( ::string& os , JobSpace::ViewDescr const& vd ) {
	/**/             os <<"ViewDescr("<< vd.phys ;
	if (+vd.copy_up) os <<"CU:"<< vd.copy_up     ;
	return           os <<')'                    ;
}

::string& operator+=( ::string& os , JobSpace const& js ) {
	First first ;
	/**/                  os <<"JobSpace("                           ;
	if (+js.chroot_dir_s) os <<first("",",")<<"C:"<< js.chroot_dir_s ;
	if (+js.root_view_s ) os <<first("",",")<<"R:"<< js.root_view_s  ;
	if (+js.tmp_view_s  ) os <<first("",",")<<"T:"<< js.tmp_view_s   ;
	if (+js.views       ) os <<first("",",")<<"V:"<< js.views        ;
	return                os <<')'                                   ;
}

static void _chroot(::string const& dir_s) { Trace trace("_chroot",dir_s) ; if (::chroot(no_slash(dir_s).c_str())!=0) throw "cannot chroot to "+no_slash(dir_s)+" : "+::strerror(errno) ; }
static void _chdir (::string const& dir_s) { Trace trace("_chdir" ,dir_s) ; if (::chdir (no_slash(dir_s).c_str())!=0) throw "cannot chdir to " +no_slash(dir_s)+" : "+::strerror(errno) ; }

static void _mount_bind( ::string const& dst , ::string const& src ) { // src and dst may be files or dirs
	Trace trace("_mount_bind",dst,src) ;
	if (::mount( no_slash(src).c_str() , no_slash(dst).c_str() , nullptr/*type*/ , MS_BIND|MS_REC , nullptr/*data*/ )!=0)
		throw "cannot bind mount "+src+" onto "+dst+" : "+::strerror(errno) ;
}

static void _mount_tmp( ::string const& dst_s , size_t sz_mb ) {
	SWEAR(sz_mb) ;
	Trace trace("_mount_tmp",dst_s,sz_mb) ;
	if (::mount( "" ,  no_slash(dst_s).c_str() , "tmpfs" , 0/*flags*/ , ("size="+::to_string(sz_mb)+"m").c_str() )!=0)
		throw "cannot mount tmpfs of size "+to_string_with_units<'M'>(sz_mb)+"B onto "+no_slash(dst_s)+" : "+::strerror(errno) ;
}

static void _mount_overlay( ::string const& dst_s , ::vector_s const& srcs_s , ::string const& work_s ) {
	SWEAR(+srcs_s) ;
	SWEAR(srcs_s.size()>1,dst_s,srcs_s,work_s) ; // use bind mount in that case
	//
	Trace trace("_mount_overlay",dst_s,srcs_s,work_s) ;
	for( size_t i : iota(1,srcs_s.size()) )
		if (srcs_s[i].find(':')!=Npos)
			throw cat("cannot overlay mount ",dst_s," to ",srcs_s,"with embedded columns (:)") ;
	mk_dir_s(work_s) ;
	//
	::string                                data  = "userxattr"                      ;
	/**/                                    data += ",upperdir="+no_slash(srcs_s[0]) ;
	/**/                                    data += ",lowerdir="+no_slash(srcs_s[1]) ;
	for( size_t i : iota(2,srcs_s.size()) ) data += ':'         +no_slash(srcs_s[i]) ;
	/**/                                    data += ",workdir=" +no_slash(work_s   ) ;
	if (::mount( nullptr ,  no_slash(dst_s).c_str() , "overlay" , 0 , data.c_str() )!=0)
		throw "cannot overlay mount "+dst_s+" to "+data+" : "+::strerror(errno) ;
}

static void _atomic_write( ::string const& file , ::string const& data ) {
	Trace trace("_atomic_write",file,data) ;
	AcFd fd { file , Fd::Write } ;
	throw_unless( +fd , "cannot open ",file," for writing" ) ;
	ssize_t cnt = ::write( fd , data.c_str() , data.size() ) ;
	if (cnt<0                  ) throw "cannot write atomically "s+data.size()+" bytes to "+file+" : "+::strerror(errno)         ;
	if (size_t(cnt)<data.size()) throw "cannot write atomically "s+data.size()+" bytes to "+file+" : only "+cnt+" bytes written" ;
}

bool JobSpace::_is_lcl_tmp(::string const& f) const {
	if (is_lcl(f)  ) return true                      ;
	if (+tmp_view_s) return f.starts_with(tmp_view_s) ;
	/**/             return false                     ;
} ;

bool/*dst_ok*/ JobSpace::_create( ::vmap_s<MountAction>& deps , ::string const& dst , ::string const& src ) const {
	if (!_is_lcl_tmp(dst)) return false/*dst_ok*/ ;
	bool dst_ok = true ;
	if (is_dirname(dst)) {
		mk_dir_s(dst) ;
		deps.emplace_back(no_slash(dst),MountAction::Access) ;
	} else if (+FileInfo(dst).tag()) {
		deps.emplace_back(dst,MountAction::Access) ;
	} else if (+src) {
		/**/                        deps.emplace_back(src,MountAction::Read ) ;
		if ((dst_ok=+cpy(dst,src))) deps.emplace_back(dst,MountAction::Write) ;
		else                        dst_ok = false ;
	} else {
		AcFd fd { dir_guard(dst) , Fd::Write } ;
		if ((dst_ok=+fd)) deps.emplace_back(dst,MountAction::Write) ;
	}
	return dst_ok ;
}

bool/*entered*/ JobSpace::enter(
	::vmap_s<MountAction>& report
,	::string   const&      phy_root_dir_s
,	::string   const&      phy_tmp_dir_s
,	::string   const&      cwd_s
,	size_t                 tmp_sz_mb
,	::string   const&      work_dir_s
,	::vector_s const&      src_dirs_s
) {
	Trace trace("JobSpace::enter",self,phy_root_dir_s,phy_tmp_dir_s,tmp_sz_mb,work_dir_s,src_dirs_s) ;
	//
	if (!self) return false/*entered*/ ;
	//
	int uid = ::getuid() ;                                                          // must be done before unshare that invents a new user
	int gid = ::getgid() ;                                                          // .
	//
	if (::unshare(CLONE_NEWUSER|CLONE_NEWNS)!=0) throw "cannot create namespace : "s+::strerror(errno) ;
	//
	size_t   uphill_lvl = 0 ;
	::string highest_s  ;
	for( ::string const& d_s : src_dirs_s ) if (!is_abs_s(d_s))
		if ( size_t ul=uphill_lvl_s(d_s) ; ul>uphill_lvl ) {
			uphill_lvl = ul  ;
			highest_s  = d_s ;
		}
	//
	::string phy_super_root_dir_s ;                                                 // dir englobing all relative source dirs
	::string super_root_view_s    ;                                                 // .
	::string top_root_view_s      ;
	if (+root_view_s) {
		if (!( root_view_s.ends_with(cwd_s) && root_view_s.size()>cwd_s.size()+1 )) // ensure root_view_s has at least one more level than cwd_s
			throw
				"cannot map local repository dir to "+no_slash(root_view_s)+" appearing as "+no_slash(cwd_s)+" in top-level repository"
			+	", "
			+	"consider setting <rule>.root_view="+mk_py_str("/repo/"+no_slash(cwd_s))
			;
		phy_super_root_dir_s = phy_root_dir_s ; for( [[maybe_unused]] size_t _ : iota(uphill_lvl) ) phy_super_root_dir_s = dir_name_s(phy_super_root_dir_s) ;
		super_root_view_s    = root_view_s    ; for( [[maybe_unused]] size_t _ : iota(uphill_lvl) ) super_root_view_s    = dir_name_s(super_root_view_s   ) ;
		SWEAR(phy_super_root_dir_s!="/",phy_root_dir_s,uphill_lvl) ;                                                                                          // this should have been checked earlier
		if (!super_root_view_s)
			throw
				"cannot map repository dir to "+no_slash(root_view_s)+" with relative source dir "+no_slash(highest_s)
			+	", "
			+	"consider setting <rule>.root_view="+mk_py_str("/repo/"+no_slash(phy_root_dir_s.substr(phy_super_root_dir_s.size())+cwd_s))
			;
		if (root_view_s.substr(super_root_view_s.size())!=phy_root_dir_s.substr(phy_super_root_dir_s.size()))
			throw
				"last "s+uphill_lvl+" components do not match between physical root dir and root view"
			+	", "
			+	"consider setting <rule>.root_view="+mk_py_str("/repo/"+no_slash(phy_root_dir_s.substr(phy_super_root_dir_s.size())+cwd_s))
			;
		top_root_view_s = root_view_s.substr(0,root_view_s.size()-cwd_s.size()) ;
	}
	if ( +super_root_view_s && super_root_view_s.rfind('/',super_root_view_s.size()-2)!=0 ) throw "non top-level root_view not yet implemented"s ; // XXX : handle cases where dir is not top level
	if ( +tmp_view_s        && tmp_view_s       .rfind('/',tmp_view_s       .size()-2)!=0 ) throw "non top-level tmp_view not yet implemented"s  ; // .
	//
	::string chroot_dir       = chroot_dir_s                                                          ; if (+chroot_dir) chroot_dir.pop_back() ;
	bool     must_create_root = +super_root_view_s && !is_dir(chroot_dir+no_slash(super_root_view_s)) ;
	bool     must_create_tmp  = +tmp_view_s        && !is_dir(chroot_dir+no_slash(tmp_view_s       )) ;
	trace("create",STR(must_create_root),STR(must_create_tmp)) ;
	if ( must_create_root || must_create_tmp || +views )
		try { unlnk_inside_s(work_dir_s) ; } catch (::string const& e) {} // if we need a work dir, we must clean it first as it is not cleaned upon exit (ignore errors as dir may not exist)
	if ( must_create_root || must_create_tmp ) {                          // we cannot mount directly in chroot_dir
		if (!work_dir_s)
			throw
				"need a work dir to"s
			+	(	must_create_root ? " create root view"
				:	must_create_tmp  ? " create tmp view"
				:	                   " ???"
				)
			;
		::vector_s top_lvls        = lst_dir_s(+chroot_dir_s?chroot_dir_s:"/") ;
		::string   work_root_dir   = work_dir_s+"root"                         ;
		::string   work_root_dir_s = work_root_dir+'/'                         ;
		mk_dir_s      (work_root_dir_s) ;
		unlnk_inside_s(work_root_dir_s) ;
		trace("top_lvls",work_root_dir_s,top_lvls) ;
		for( ::string const& f : top_lvls ) {
			::string src_f     = (+chroot_dir_s?chroot_dir_s:"/"s) + f ;
			::string private_f = work_root_dir_s                   + f ;
			switch (FileInfo(src_f).tag()) {                                                                                   // exclude weird files
				case FileTag::Reg   :
				case FileTag::Empty :
				case FileTag::Exe   : AcFd    (        private_f    ,Fd::Write      ) ; _mount_bind(private_f,src_f) ; break ; // create file
				case FileTag::Dir   : mk_dir_s(with_slash(private_f)                ) ; _mount_bind(private_f,src_f) ; break ; // create dir
				case FileTag::Lnk   : lnk     (           private_f ,read_lnk(src_f)) ;                                break ; // copy symlink
			DN}
		}
		if (must_create_root) mk_dir_s(work_root_dir+super_root_view_s) ;
		if (must_create_tmp ) mk_dir_s(work_root_dir+tmp_view_s       ) ;
		chroot_dir = ::move(work_root_dir) ;
	}
	// mapping uid/gid is necessary to manage overlayfs
	_atomic_write( "/proc/self/setgroups" , "deny"                 ) ;                                                         // necessary to be allowed to write the gid_map (if desirable)
	_atomic_write( "/proc/self/uid_map"   , ""s+uid+' '+uid+" 1\n" ) ;
	_atomic_write( "/proc/self/gid_map"   , ""s+gid+' '+gid+" 1\n" ) ;
	//
	::string root_dir_s = +root_view_s ? top_root_view_s : phy_root_dir_s ;
	if (+root_view_s) _mount_bind( chroot_dir+super_root_view_s , phy_super_root_dir_s ) ;
	if (+tmp_view_s ) {
		if      (+phy_tmp_dir_s) _mount_bind( chroot_dir+tmp_view_s , phy_tmp_dir_s ) ;
		else if (tmp_sz_mb     ) _mount_tmp ( chroot_dir+tmp_view_s , tmp_sz_mb     ) ;
	}
	//
	if      (+chroot_dir ) _chroot(chroot_dir)    ;
	if      (+root_view_s) _chdir(root_view_s   ) ;
	else if (+chroot_dir ) _chdir(phy_root_dir_s) ;
	//
	size_t work_idx = 0 ;
	for( auto const& [view,descr] : views ) if (+descr) {                                                                      // empty descr does not represent a view
		::string   abs_view = mk_abs(view,root_dir_s) ;
		::vector_s abs_phys ;                           abs_phys.reserve(descr.phys.size()) ; for( ::string const& phy : descr.phys ) abs_phys.push_back(mk_abs(phy,root_dir_s)) ;
		/**/                                    _create(report,view) ;
		for( ::string const& phy : descr.phys ) _create(report,phy ) ;
		if (is_dirname(view)) {
			for( ::string const& cu : descr.copy_up ) {
				::string dst = descr.phys[0]+cu ;
				if (is_dirname(cu))
					_create(report,dst) ;
				else
					for( size_t i : iota(1,descr.phys.size()) )
						if (_create(report,dst,descr.phys[i]+cu)) break ;
			}
		}
		size_t          sz    = descr.phys.size() ;
		::string const& upper = descr.phys[0]     ;
		if (sz==1) {
			_mount_bind( abs_view , abs_phys[0] ) ;
		} else {
			::string work_s = is_lcl(upper) ? work_dir_s+"work_"+(work_idx++)+'/' : upper.substr(0,upper.size()-1)+".work/" ;  // if not in the repo, it must be in tmp
			mk_dir_s(work_s) ;
			_mount_overlay( abs_view , abs_phys , mk_abs(work_s,root_dir_s) ) ;
		}
	}
	trace("done") ;
	return true/*entered*/ ;
}

// XXX : implement recursive views
// for now, phys cannot englobe or lie within a view, but when it is to be implemented, it is here
::vmap_s<::vector_s> JobSpace::flat_phys() const {
	::vmap_s<::vector_s> res ; res.reserve(views.size()) ;
	for( auto const& [view,descr] : views ) res.emplace_back(view,descr.phys) ;
	return res ;
}

void JobSpace::mk_canon(::string const& phy_root_dir_s) {
	auto do_top = [&]( ::string& dir_s , bool slash_ok , ::string const& key )->void {
		if ( !dir_s                                     ) return ;
		if ( !is_canon(dir_s)                           ) dir_s = ::mk_canon(dir_s) ;
		if ( slash_ok && dir_s=="/"                     ) return ;
		if (             dir_s=="/"                     ) throw key+" cannot be /"                                           ;
		if ( !is_abs(dir_s)                             ) throw key+" must be absolute : "+no_slash(dir_s)                   ;
		if ( phy_root_dir_s.starts_with(dir_s         ) ) throw "repository cannot lie within "+key+' '+no_slash(dir_s)      ;
		if ( dir_s         .starts_with(phy_root_dir_s) ) throw key+' '+no_slash(dir_s)+" cannot be local to the repository" ;
	} ;
	//                   slash_ok
	do_top( chroot_dir_s , true  , "chroot dir" ) ;
	do_top( root_view_s  , false , "root view"  ) ;
	do_top( tmp_view_s   , false , "tmp view"   ) ;
	if ( +root_view_s && +tmp_view_s ) {
		if (root_view_s.starts_with(tmp_view_s )) throw "root view "+no_slash(root_view_s)+" cannot lie within tmp view " +no_slash(tmp_view_s ) ;
		if (tmp_view_s .starts_with(root_view_s)) throw "tmp view " +no_slash(tmp_view_s )+" cannot lie within root view "+no_slash(root_view_s) ;
	}
	//
	::string const& job_root_dir_s = +root_view_s ? root_view_s : phy_root_dir_s ;
	auto do_path = [&](::string& path)->void {
		if      (!is_canon(path)                 ) path = ::mk_canon(path)                   ;
		if      (path.starts_with("../")         ) path = mk_abs(path,job_root_dir_s)        ;
		else if (path.starts_with(job_root_dir_s)) path = path.substr(job_root_dir_s.size()) ;
	} ;
	for( auto& [view,_] : views ) {
		do_path(view) ;
		if (!view                           ) throw "cannot map the whole repository"s                  ;
		if (job_root_dir_s.starts_with(view)) throw "repository cannot lie within view "+no_slash(view) ;
	}
	//
	for( auto& [view,descr] : views ) {
		bool is_dir_view = is_dirname(view)  ;
		/**/                             if ( !is_dir_view && descr.phys.size()!=1                                     ) throw "cannot map non-dir " +no_slash(view)+" to an overlay" ;
		for( auto const& [v,_] : views ) if ( &v!=&view && view.starts_with(v) && (v.back()=='/'||view[v.size()]=='/') ) throw "cannot map "+no_slash(view)+" within "+v              ;
		bool lcl_view = _is_lcl_tmp(view) ;
		for( ::string& phy : descr.phys ) {
			do_path(phy) ;
			if ( !lcl_view && _is_lcl_tmp(phy)    ) throw "cannot map external view "+no_slash(view)+" to local or tmp "+no_slash(phy) ;
			if (  is_dir_view && !is_dirname(phy) ) throw "cannot map dir "          +no_slash(view)+" to file "        +no_slash(phy) ;
			if ( !is_dir_view &&  is_dirname(phy) ) throw "cannot map file "         +no_slash(view)+" to dir "         +no_slash(phy) ;
			if (+phy) {
				for( auto const& [v,_] : views ) {                                                                            // XXX : suppress this check when recursive maps are implemented
					if ( phy.starts_with(v  ) && (v  .back()=='/'||phy[v  .size()]=='/') ) throw "cannot map "+no_slash(view)+" to "+no_slash(phy)+" within "    +no_slash(v) ;
					if ( v  .starts_with(phy) && (phy.back()=='/'||v  [phy.size()]=='/') ) throw "cannot map "+no_slash(view)+" to "+no_slash(phy)+" containing "+no_slash(v) ;
				}
			} else {
				for( auto const& [v,_] : views )                                                                              // XXX : suppress this check when recursive maps are implemented
					if (!is_abs(v)) throw "cannot map "+no_slash(view)+" to full repository with "+no_slash(v)+" being map" ;
			}
		}
	}
}

//
// JobStartRpcReq
//

::string& operator+=( ::string& os , JobStartRpcReq const& jsrr ) {
	return os << "JobStartRpcReq(" << jsrr.seq_id <<','<< jsrr.job <<','<< jsrr.port <<','<< jsrr.msg <<')' ;
}

//
// JobEndRpcReq
//

::string& operator+=( ::string& os , TargetDigest const& td ) {
	const char* sep = "" ;
	/**/                    os << "TargetDigest("      ;
	if ( td.pre_exist   ) { os <<      "pre_exist"     ; sep = "," ; }
	if (+td.tflags      ) { os <<sep<< td.tflags       ; sep = "," ; }
	if (+td.extra_tflags) { os <<sep<< td.extra_tflags ; sep = "," ; }
	if (+td.crc         ) { os <<sep<< td.crc          ; sep = "," ; }
	if (+td.sig         )   os <<sep<< td.sig          ;
	return                  os <<')'                   ;
}

::string& operator+=( ::string& os , JobDigest const& jd ) {
	return os << "JobDigest(" << jd.end_attrs <<','<< jd.wstatus<<':'<<jd.status <<','<< jd.targets <<','<< jd.deps << ')' ;
}

::string& operator+=( ::string& os , JobEndRpcReq const& jerr ) {
	return os << "JobEndRpcReq(" << jerr.seq_id <<','<< jerr.job <<','<< jerr.digest <<','<< jerr.phy_tmp_dir_s <<','<< jerr.dynamic_env <<','<< jerr.msg <<')' ;
}

//
// JobStartRpcReply
//

::string& operator+=( ::string& os , MatchFlags const& mf ) {
	/**/             os << "MatchFlags(" ;
	switch (mf.is_target) {
		case Yes   : os << "target" ; if (+mf.tflags()) os<<','<<mf.tflags() ; if (+mf.extra_tflags()) os<<','<<mf.extra_tflags() ; break ;
		case No    : os << "dep,"   ; if (+mf.dflags()) os<<','<<mf.dflags() ; if (+mf.extra_dflags()) os<<','<<mf.extra_dflags() ; break ;
		case Maybe :                                                                                                                break ;
	DF}
	return           os << ')' ;
}

::string& operator+=( ::string& os , JobStartRpcReply const& jsrr ) {
	/**/                           os << "JobStartRpcReply("                ;
	/**/                           os <<','  << to_hex(jsrr.addr)           ;
	/**/                           os <<','  << jsrr.autodep_env            ;
	if (+jsrr.job_space          ) os <<','  << jsrr.job_space              ;
	if ( jsrr.keep_tmp           ) os <<','  << "keep"                      ;
	if ( jsrr.tmp_sz_mb==Npos    ) os <<",T:"<< "..."                       ;
	else                           os <<",T:"<< jsrr.tmp_sz_mb              ;
	if (+jsrr.cwd_s              ) os <<','  << jsrr.cwd_s                  ;
	if (+jsrr.date_prec          ) os <<','  << jsrr.date_prec              ;
	/**/                           os <<','  << mk_printable(cat(jsrr.env)) ; // env may contain the non-printable EnvPassMrkr value
	/**/                           os <<','  << jsrr.interpreter            ;
	/**/                           os <<','  << jsrr.kill_sigs              ;
	if (jsrr.live_out            ) os <<','  << "live_out"                  ;
	if (jsrr.allow_stderr        ) os <<','  << "allow_stderr"              ;
	/**/                           os <<','  << jsrr.method                 ;
	if (+jsrr.network_delay      ) os <<','  << jsrr.network_delay          ;
	if (+jsrr.pre_actions        ) os <<','  << jsrr.pre_actions            ;
	/**/                           os <<','  << jsrr.small_id               ;
	if (+jsrr.star_matches       ) os <<','  << jsrr.star_matches           ;
	if (+jsrr.deps               ) os <<'<'  << jsrr.deps                   ;
	if (+jsrr.end_attrs          ) os <<','  << jsrr.end_attrs              ;
	if (+jsrr.static_matches     ) os <<'>'  << jsrr.static_matches         ;
	if (+jsrr.stdin              ) os <<'<'  << jsrr.stdin                  ;
	if (+jsrr.stdout             ) os <<'>'  << jsrr.stdout                 ;
	if (+jsrr.timeout            ) os <<','  << jsrr.timeout                ;
	/**/                           os <<','  << jsrr.cmd                    ; // last as it is most probably multi-line
	return                         os <<')'                                 ;
}

bool/*entered*/ JobStartRpcReply::enter(
		::vmap_s<MountAction>& actions                                                                                  // out
	,	::map_ss             & cmd_env                                                                                  // .
	,	::string             & phy_tmp_dir_s                                                                            // .
	,	::vmap_ss            & dynamic_env                                                                              // .
	,	pid_t                & first_pid                                                                                // .
	,	JobIdx                 job                                                                                      // in
	,	::string        const& phy_root_dir_s                                                                           // .
	,	SeqId                  seq_id                                                                                   // .
) {
	Trace trace("JobStartRpcReply::enter",job,phy_root_dir_s,seq_id) ;
	//
	for( auto& [k,v] : env ) {
		if      (v!=EnvPassMrkr)                                                             cmd_env[k] = ::move(v) ;
		else if (has_env(k)    ) { ::string v = get_env(k) ; dynamic_env.emplace_back(k,v) ; cmd_env[k] = ::move(v) ; } // if special illegal value, use value from environment (typically from slurm)
	}
	//
	if ( auto it=cmd_env.find("TMPDIR") ; it!=cmd_env.end() ) {
		throw_unless( is_abs(it->second) , "$TMPDIR must be absolute but is ",it->second ) ;
		phy_tmp_dir_s = with_slash(it->second)+key+'/'+small_id+'/' ;
	} else if ( tmp_sz_mb==Npos || !job_space.tmp_view_s ) {
		phy_tmp_dir_s = phy_root_dir_s+PrivateAdminDirS+"tmp/"+small_id+'/' ;
	} else {
		phy_tmp_dir_s = {} ;
	}
	if      (keep_tmp      ) phy_tmp_dir_s         = phy_root_dir_s+AdminDirS+"tmp/"+job+'/' ;
	else if (+phy_tmp_dir_s) _tmp_dir_s_to_cleanup = phy_tmp_dir_s                           ;
	autodep_env.root_dir_s = +job_space.root_view_s ? job_space.root_view_s : phy_root_dir_s ;
	autodep_env.tmp_dir_s  = +job_space.tmp_view_s  ? job_space.tmp_view_s  : phy_tmp_dir_s  ;
	//
	try {
		if (+phy_tmp_dir_s) unlnk_inside_s(phy_tmp_dir_s,true/*abs_ok*/) ;             // ensure tmp dir is clean
	} catch (::string const&) {
		try                       { mk_dir_s(phy_tmp_dir_s) ;            }             // ensure tmp dir exists
		catch (::string const& e) { throw "cannot create tmp dir : "+e ; }
	}
	//
	cmd_env["ROOT_DIR"    ] = no_slash(autodep_env.root_dir_s+cwd_s) ;
	cmd_env["TOP_ROOT_DIR"] = no_slash(autodep_env.root_dir_s      ) ;
	cmd_env["SEQUENCE_ID" ] = ::to_string(seq_id  )                  ;
	cmd_env["SMALL_ID"    ] = ::to_string(small_id)                  ;
	if (PY_LD_LIBRARY_PATH[0]!=0) {
		auto [it,inserted] = cmd_env.try_emplace("LD_LIBRARY_PATH",PY_LD_LIBRARY_PATH) ;
		if (!inserted) it->second <<':'<< PY_LD_LIBRARY_PATH ;
	}
	//
	if (+autodep_env.tmp_dir_s) {
		cmd_env["TMPDIR"] = no_slash(autodep_env.tmp_dir_s) ;
	} else {
		SWEAR(!cmd_env.contains("TMPDIR")) ;                                           // if we have a TMPDIR env var, we should have a tmp dir
		autodep_env.tmp_dir_s = with_slash(P_tmpdir) ;                                 // detect accesses to P_tmpdir (usually /tmp) and generate an error
	}
	if (!cmd_env.contains("HOME")) cmd_env["HOME"] = no_slash(autodep_env.tmp_dir_s) ; // by default, set HOME to tmp dir as this cannot be set from rule
	//
	::string phy_work_dir_s = PrivateAdminDirS+"work/"s+small_id+'/'                                                                                    ;
	bool     entered        = job_space.enter( actions , phy_root_dir_s , phy_tmp_dir_s , cwd_s , tmp_sz_mb , phy_work_dir_s , autodep_env.src_dirs_s ) ;
	if (entered) {
		// find a good starting pid
		// the goal is to minimize risks of pid conflicts between jobs in case pid is used to generate unique file names as temporary file instead of using TMPDIR, which is quite common
		// to do that we spread pid's among the availale range by setting the first pid used by jos as apart from each other as possible
		// call phi the golden number and NPids the number of available pids
		// spreading is maximized by using phi*NPids as an elementary spacing and id (small_id) as an index modulo NPids
		// this way there is a conflict between job 1 and job 2 when (id2-id1)*phi is near an integer
		// because phi is the irrational which is as far from rationals as possible, and id's are as small as possible, this probability is minimized
		// note that this is over-quality : any more or less random number would do the job : motivation is mathematical beauty rather than practical efficiency
		static constexpr uint32_t FirstPid = 300                                 ;     // apparently, pid's wrap around back to 300
		static constexpr uint64_t NPids    = MAX_PID - FirstPid                  ;     // number of available pid's
		static constexpr uint64_t DelatPid = (1640531527*NPids) >> n_bits(NPids) ;     // use golden number to ensure best spacing (see above), 1640531527 = (2-(1+sqrt(5))/2)<<32
		first_pid = FirstPid + ((small_id*DelatPid)>>(32-n_bits(NPids)))%NPids ;       // DelatPid on 64 bits to avoid rare overflow in multiplication
	}
	return entered ;
}

void JobStartRpcReply::exit() {
	// work dir cannot be cleaned up as we may have chroot'ed inside
	Trace trace("JobStartRpcReply::exit",_tmp_dir_s_to_cleanup) ;
	if (+_tmp_dir_s_to_cleanup) unlnk_inside_s(_tmp_dir_s_to_cleanup,true/*abs_ok*/ ) ;
	job_space.exit() ;
}

//
// JobMngtRpcReq
//

::string& operator+=( ::string& os , JobMngtRpcReq const& jmrr ) {
	/**/                               os << "JobMngtRpcReq(" << jmrr.proc <<','<< jmrr.seq_id <<','<< jmrr.job <<','<< jmrr.fd ;
	switch (jmrr.proc) {
		case JobMngtProc::LiveOut    : os <<','<< jmrr.txt.size() ;                             break ;
		case JobMngtProc::ChkDeps    :
		case JobMngtProc::DepVerbose : os <<','<< jmrr.deps       ;                             break ;
		case JobMngtProc::Encode     : os <<','<< jmrr.min_len    ;                             [[fallthrough]] ;
		case JobMngtProc::Decode     : os <<','<< jmrr.ctx <<','<< jmrr.file <<','<< jmrr.txt ; break ;
		default                      :                                                          break ;
	}
	return                             os <<')' ;
}

//
// JobMngtRpcReply
//

::string& operator+=( ::string& os , JobMngtRpcReply const& jmrr ) {
	/**/                               os << "JobMngtRpcReply(" << jmrr.proc ;
	switch (jmrr.proc) {
		case JobMngtProc::ChkDeps    : os <<','<< jmrr.fd <<','<<                                   jmrr.ok ; break ;
		case JobMngtProc::DepVerbose : os <<','<< jmrr.fd <<','<< jmrr.dep_infos                            ; break ;
		case JobMngtProc::Decode     : os <<','<< jmrr.fd <<','<< jmrr.txt <<','<< jmrr.crc <<','<< jmrr.ok ; break ;
		case JobMngtProc::Encode     : os <<','<< jmrr.fd <<','<< jmrr.txt <<','<< jmrr.crc <<','<< jmrr.ok ; break ;
	DN}
	return                             os << ')' ;
}

//
// SubmitAttrs
//

::string& operator+=( ::string& os , SubmitAttrs const& sa ) {
	First first ;
	/**/               os << "SubmitAttrs("             ;
	if (+sa.tag      ) os <<first("",",")<< sa.tag      ;
	if ( sa.live_out ) os <<first("",",")<< "live_out"  ;
	if (+sa.pressure ) os <<first("",",")<< sa.pressure ;
	if (+sa.deps     ) os <<first("",",")<< sa.deps     ;
	if (+sa.reason   ) os <<first("",",")<< sa.reason   ;
	return             os <<')'                         ;
}

//
// JobInfoStart
//

::string& operator+=( ::string& os , JobInfoStart const& jis ) {
	return os << "JobInfoStart(" << jis.submit_attrs <<','<< jis.rsrcs <<','<< jis.pre_start <<','<< jis.start <<')' ;
}

//
// JobInfo
//

JobInfo::JobInfo(::string const& filename , Bool3 get_start , Bool3 get_end ) {
	Trace trace("JobInfo",filename,get_start,get_end) ;
	if ( get_start==No && get_end==No ) return ;                                                              // fast path : dont read filename
	::string      job_info ;            try { job_info = AcFd(filename).read() ; } catch (::string const&) {} // empty string in case of error, will processed later
	::string_view jis      = job_info ;
	try {
		if (get_start==No) deserialize( jis , ::ref(JobInfoStart()) ) ;                                       // even if we do not need start, we need to skip it
		else               deserialize( jis , start                 ) ;
		trace("start") ;
	} catch (...) {
		if ( get_start!=No                  ) start = {} ;                                                    // ensure start is either empty or full
		if ( get_start==Yes || get_end==Yes ) throw ;                                                         // if we cannot skip start, we cannot get end
		return ;                                                                                              // .
	}
	try {
		if (get_end==No) return ;
		deserialize( jis , end ) ;
		trace("end") ;
	} catch (...) {
		end = {} ;                                                                                            // ensure end is either empty or full
		if (get_end==Yes) throw ;
	}
}

void JobInfo::write(::string const& filename) const {
	AcFd os { dir_guard(filename) , Fd::Write } ;
	os.write(
		serialize(start)
	+	serialize(end  )
	) ;
}
//
// codec
//


namespace Codec {

	::string mk_decode_node( ::string const& file , ::string const& ctx , ::string const& code ) {
		return CodecPfx+mk_printable<'.'>(file)+".cdir/"+mk_printable<'.'>(ctx)+".ddir/"+mk_printable(code) ;
	}

	::string mk_encode_node( ::string const& file , ::string const& ctx , ::string const& val ) {
		return CodecPfx+mk_printable<'.'>(file)+".cdir/"+mk_printable<'.'>(ctx)+".edir/"+Xxh(val).digest().hex() ;
	}

	::string mk_file(::string const& node) {
		return parse_printable<'.'>(node,::ref(size_t(0))).substr(sizeof(CodecPfx)-1) ; // account for terminating null in CodecPfx
	}

}
