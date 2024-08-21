// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/mount.h>

#include "disk.hh"
#include "fuse.hh"
#include "hash.hh"
#include "trace.hh"

#include "rpc_job.hh"

using namespace Disk ;
using namespace Hash ;

//
// FileAction
//

::ostream& operator<<( ::ostream& os , FileAction const& fa ) {
	/**/                                os << "FileAction(" << fa.tag ;
	if (fa.tag<=FileActionTag::HasFile) os <<','<< fa.sig             ;
	return                              os <<')'                      ;
}

::pair_s<bool/*ok*/> do_file_actions( ::vector_s* unlnks/*out*/ , ::vmap_s<FileAction>&& pre_actions , NfsGuard& nfs_guard , Algo ha ) {
	::uset_s keep_dirs ;
	::string msg       ;
	bool     ok        = true ;
	//
	Trace trace("do_file_actions",pre_actions) ;
	for( auto const& [f,a] : pre_actions ) {                                                                  // pre_actions are adequately sorted
		SWEAR(+f) ;                                                                                           // acting on root dir is non-sense
		switch (a.tag) {
			case FileActionTag::Unlink         :
			case FileActionTag::UnlinkWarning  :
			case FileActionTag::UnlinkPolluted :
			case FileActionTag::None          : {
				FileSig sig { nfs_guard.access(f) } ;
				if (!sig) break ;                                                                             // file does not exist, nothing to do
				bool done       = true/*garbage*/                                                           ;
				bool quarantine = sig!=a.sig && (a.crc==Crc::None||!a.crc.valid()||!a.crc.match(Crc(f,ha))) ;
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
					} catch (::string const&) {                                                               // if a dir cannot rmdir'ed, no need to try those uphill
						keep_dirs.insert(f) ;
						for( ::string d_s=dir_name_s(f) ; +d_s ; d_s=dir_name_s(d_s) )
							if (!keep_dirs.insert(no_slash(d_s)).second) break ;
					}
			break ;
		DF}
	}
	trace("done",STR(ok),msg) ;
	return {msg,ok} ;
}

//
// JobReason
//

::ostream& operator<<( ::ostream& os , JobReason const& jr ) {
	os << "JobReason(" << jr.tag ;
	if (jr.tag>=JobReasonTag::HasNode) os << ',' << jr.node ;
	return os << ')' ;
}

//
// DepInfo
//

::ostream& operator<<( ::ostream& os , DepInfo const& di ) {
	switch (di.kind) {
		case DepInfoKind::Crc  : return os <<'('<< di.crc () <<')' ;
		case DepInfoKind::Sig  : return os <<'('<< di.sig () <<')' ;
		case DepInfoKind::Info : return os <<'('<< di.info() <<')' ;
	DF}
}

//
// JobSpace
//

::ostream& operator<<( ::ostream& os , JobSpace const& js ) {
	const char* sep = "" ;
	/**/                    os <<'('                         ;
	if (+js.chroot_dir_s) { os <<     "C:"<< js.chroot_dir_s ; sep = "," ; }
	if (+js.root_view_s ) { os <<sep<<"R:"<< js.root_view_s  ; sep = "," ; }
	if (+js.tmp_view_s  )   os <<sep<<"T:"<< js.tmp_view_s   ;
	return                  os <<')'                         ;
}

static void _chroot(::string const& dir_s) { Trace trace("_chroot",dir_s) ; if (::chroot(no_slash(dir_s).c_str())!=0) throw "cannot chroot to "+no_slash(dir_s)+" : "+strerror(errno) ; }
static void _chdir (::string const& dir_s) { Trace trace("_chdir" ,dir_s) ; if (::chdir (no_slash(dir_s).c_str())!=0) throw "cannot chdir to " +no_slash(dir_s)+" : "+strerror(errno) ; }

static void _mount_bind( ::string const& dst , ::string const& src ) { // src and dst may be files or dirs
	Trace trace("_mount_bind",dst,src) ;
	if (::mount( no_slash(src).c_str() , no_slash( dst).c_str() , nullptr/*type*/ , MS_BIND|MS_REC , nullptr/*data*/ )!=0)
		throw "cannot bind mount "+src+" onto "+dst+" : "+strerror(errno) ;
}
static void _mount_fuse( ::string const& dst_s , ::string const& src_s ) {
	Trace trace("_mount_fuse",dst_s,src_s) ;
	static Fuse::Mount fuse_mount{ dst_s , src_s } ;
}
static void _mount_tmp( ::string const& dst_s , size_t sz_mb ) {
	SWEAR(sz_mb) ;
	Trace trace("_mount_tmp",dst_s,sz_mb) ;
	if (::mount( "" ,  no_slash(dst_s).c_str() , "tmpfs" , 0/*flags*/ , (::to_string(sz_mb)+"m").c_str() )!=0)
		throw "cannot mount tmpfs of size"s+sz_mb+" MB onto "+dst_s+" : "+strerror(errno) ;
}
static void _mount_overlay( ::string const& dst_s , ::vector_s const& srcs_s , ::string const& work_s ) {
	SWEAR(+srcs_s) ;
	SWEAR(srcs_s.size()>1,dst_s,srcs_s,work_s) ;                       // use bind mount in that case
	//
	Trace trace("_mount_overlay",dst_s,srcs_s,work_s) ;
	for( size_t i=1 ; i<srcs_s.size() ; i++ )
		if (srcs_s[i].find(':')!=Npos)
			throw "cannot overlay mount "+dst_s+" to "+fmt_string(srcs_s)+"with embedded columns (:)" ;
	mk_dir_s(work_s) ;
	//
	::string                                  data  = "userxattr"                      ;
	/**/                                      data += ",upperdir="+no_slash(srcs_s[0]) ;
	/**/                                      data += ",lowerdir="+no_slash(srcs_s[1]) ;
	for( size_t i=2 ; i<srcs_s.size() ; i++ ) data += ':'         +no_slash(srcs_s[i]) ;
	/**/                                      data += ",workdir=" +no_slash(work_s   ) ;
	if (::mount( nullptr ,  no_slash(dst_s).c_str() , "overlay" , 0 , data.c_str() )!=0)
		throw "cannot overlay mount "+dst_s+" to "+data+" : "+strerror(errno) ;
}

static void _atomic_write( ::string const& file , ::string const& data ) {
	Trace trace("_atomic_write",file,data) ;
	AutoCloseFd fd = ::open(file.c_str(),O_WRONLY|O_TRUNC) ;
	if (!fd) throw "cannot open "+file+" for writing" ;
	ssize_t cnt = ::write( fd , data.c_str() , data.size() ) ;
	if (cnt<0                  ) throw "cannot write atomically "s+data.size()+" bytes to "+file+" : "+strerror(errno)           ;
	if (size_t(cnt)<data.size()) throw "cannot write atomically "s+data.size()+" bytes to "+file+" : only "+cnt+" bytes written" ;
}

static bool _is_lcl_tmp( ::string const& f , ::string const& tmp_view_s ) {
	if (is_lcl(f)  ) return true                      ;
	if (!tmp_view_s) return false                     ;
	/**/             return f.starts_with(tmp_view_s) ;
} ;

bool/*entered*/ JobSpace::enter(
	::string const&   phy_root_dir_s
,	::string const&   phy_tmp_dir_s
,	size_t            tmp_sz_mb
,	::string const&   work_dir_s
,	::vector_s const& src_dirs_s
,	bool              use_fuse
) const {
	Trace trace("enter",*this,phy_root_dir_s,phy_tmp_dir_s,tmp_sz_mb,work_dir_s,src_dirs_s,STR(use_fuse)) ;
	//
	if ( use_fuse && !root_view_s ) throw "cannot use fuse for autodep without root_view"s ;
	if ( !*this                   ) return false/*entered*/                                ;
	//
	int uid = ::getuid() ;                                                                           // must be done before unshare that invents a new user
	int gid = ::getgid() ;                                                                           // .
	//
	if (::unshare(CLONE_NEWUSER|CLONE_NEWNS)!=0) throw "cannot create namespace : "s+strerror(errno) ;
	//
	size_t   src_dirs_uphill_lvl = 0 ;
	::string highest             ;
	for( ::string const& d_s : src_dirs_s ) {
		if (!is_abs_s(d_s))
			if ( size_t ul=uphill_lvl_s(d_s) ; ul>src_dirs_uphill_lvl ) {
				src_dirs_uphill_lvl = ul  ;
				highest             = d_s ;
			}
	}
	//
	for( auto const& [view,_] : views ) {
		if ( +tmp_view_s && view.starts_with(tmp_view_s) ) continue     ;
		if ( !is_lcl(view)                               ) goto BadView ;
		if ( is_in_dir(view,AdminDirS)                   ) goto BadView ;
		/**/                                               continue     ;
	BadView :
		throw "cannot map "+view+" that must either be local in the repository or lie in tmp_view" ; // else we must guarantee canon after mapping, extend src_dirs to include their views, etc.
	}
	//
	::string phy_super_root_dir_s ;                                                                  // dir englobing all relative source dirs
	::string super_root_view_s    ;                                                                  // .
	if (+root_view_s) {
		phy_super_root_dir_s = phy_root_dir_s ; for( size_t i=0 ; i<src_dirs_uphill_lvl ; i++ ) phy_super_root_dir_s = dir_name_s(phy_super_root_dir_s) ;
		super_root_view_s    = root_view_s    ; for( size_t i=0 ; i<src_dirs_uphill_lvl ; i++ ) super_root_view_s    = dir_name_s(super_root_view_s   ) ;
		SWEAR(phy_super_root_dir_s!="/",phy_root_dir_s,src_dirs_uphill_lvl) ;                                                                             // this should have been checked earlier
		if (!super_root_view_s) {
			highest.pop_back() ;
			throw
				"cannot map repository dir to "+no_slash(root_view_s)+" with relative source dir "+highest
			+	", "
			+	"consider setting <rule>.root_view="+mk_py_str("/repo"+phy_root_dir_s.substr(phy_super_root_dir_s.size()-1))
			;
		}
		if (root_view_s.substr(super_root_view_s.size())!=phy_root_dir_s.substr(phy_super_root_dir_s.size()))
			throw
				"last "s+src_dirs_uphill_lvl+" components do not match between physical root dir and root view"
			+	", "
			+	"consider setting <rule>.root_view="+mk_py_str("/repo/"+phy_root_dir_s.substr(phy_super_root_dir_s.size()))
			;
	}
	if ( +super_root_view_s && super_root_view_s.rfind('/',super_root_view_s.size()-2)!=0 ) throw "non top-level root_view not yet implemented"s ; // XXX : handle cases where dir is not top level
	if ( +tmp_view_s        && tmp_view_s       .rfind('/',tmp_view_s       .size()-2)!=0 ) throw "non top-level tmp_view not yet implemented"s  ; // .
	//
	::string chroot_dir       ; if (+chroot_dir_s) chroot_dir = no_slash(chroot_dir_s) ;
	bool     must_create_root = +super_root_view_s && !is_dir(chroot_dir+no_slash(super_root_view_s)) ;
	bool     must_create_tmp  = +tmp_view_s        && !is_dir(chroot_dir+no_slash(tmp_view_s       )) ;
	trace("create",STR(must_create_root),STR(must_create_tmp)) ;
	if ( must_create_root || must_create_tmp ) {                                                                                      // we may not mount directly in chroot_dir
		if (!work_dir_s)
			throw
				"need a work dir to create"s
			+	( must_create_root                    ? " root view" : "" )
			+	( must_create_root && must_create_tmp ? " and"       : "" )
			+	(                     must_create_tmp ? " tmp view"  : "" )
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
			switch (FileInfo(src_f).tag()) {
				case FileTag::Reg   :
				case FileTag::Empty :
				case FileTag::Exe   : OFStream{           private_f                 } ; _mount_bind(private_f,src_f) ; break ;        // create file
				case FileTag::Dir   : mk_dir_s(with_slash(private_f)                ) ; _mount_bind(private_f,src_f) ; break ;        // create dir
				case FileTag::Lnk   : lnk     (           private_f ,read_lnk(src_f)) ;                                break ;        // copy symlink
				default             : ;                                                                                               // exclude weird files
			}
		}
		if (must_create_root) mk_dir_s(work_root_dir+super_root_view_s) ;
		if (must_create_tmp ) mk_dir_s(work_root_dir+tmp_view_s       ) ;
		chroot_dir = ::move(work_root_dir) ;
	}
	// mapping uid/gid is necessary to manage overlayfs
	_atomic_write( "/proc/self/setgroups" , "deny"                 ) ;                                                                // necessary to be allowed to write the gid_map (if desirable)
	_atomic_write( "/proc/self/uid_map"   , ""s+uid+' '+uid+" 1\n" ) ;
	_atomic_write( "/proc/self/gid_map"   , ""s+gid+' '+gid+" 1\n" ) ;
	//
	::string root_dir_s ;
	if (!root_view_s) {
		SWEAR(!use_fuse) ;                                                                                                            // need a view to mount repo with fuse
		root_dir_s = phy_root_dir_s ;
	} else {
		root_dir_s = chroot_dir+root_view_s ;
		if (use_fuse) _mount_fuse( chroot_dir+super_root_view_s , phy_super_root_dir_s ) ;
		else          _mount_bind( chroot_dir+super_root_view_s , phy_super_root_dir_s ) ;
	}
	if (+tmp_view_s) {
		if      (+phy_tmp_dir_s) _mount_bind( chroot_dir+tmp_view_s , phy_tmp_dir_s ) ;
		else if (tmp_sz_mb     ) _mount_tmp ( chroot_dir+tmp_view_s , tmp_sz_mb     ) ;
	}
	//
	if      (+chroot_dir ) _chroot(chroot_dir)    ;
	if      (+root_view_s) _chdir(root_view_s   ) ;
	else if (+chroot_dir ) _chdir(phy_root_dir_s) ;
	if (+views) {
		size_t i = 0 ;
		for( auto const& [view,phys] : views ) {
			::string   abs_view = mk_abs(view,root_dir_s) ;
			::vector_s abs_phys ; for( ::string const& phy : phys ) abs_phys.push_back(mk_abs(phy,root_dir_s)) ;
			//
			/**/                              if ( is_dirname(view) && _is_lcl_tmp(view,tmp_view_s) ) mk_dir_s(view) ;
			for( ::string const& phy : phys ) if ( is_dirname(phy ) && _is_lcl_tmp(phy ,tmp_view_s) ) mk_dir_s(phy ) ;
			//
			if (phys.size()==1) {
				_mount_bind( abs_view , abs_phys[0] ) ;
			} else {
				::string work_s = is_lcl(phys[0]) ? work_dir_s+"view_work/"+(i++)+'/' : phys[0].substr(0,phys[0].size()-1)+".work/" ; // if not in the repo, it must be in tmp
				mk_dir_s(work_s) ;
				_mount_overlay( abs_view , abs_phys , mk_abs(work_s,root_dir_s) ) ;
			}
		}
	}
	trace("done") ;
	return true/*entered*/ ;
}

::vmap_s<::vector_s> JobSpace::flat_views() const {
	// ves maps each view to { [upper,lower,...] , exceptions }
	// exceptions are immediate subfiles that are mapped elsewhere through another entry
	// if phys is empty, entry is just exists to record exceptions, but no mapping is associated to view
	::umap_s<::pair<::vector_s,::uset_s>> ves ;
	// invariant : ves is always complete and accurate, but may contains recursive entries
	// at the end, no more recursive entries are left
	//
	// first intialize ves
	for( auto const& [view,phys] : views ) {
		bool inserted = ves.try_emplace( view , phys , ::uset_s() ).second ; SWEAR(inserted) ;
		for( ::string f=view ; +f&&f!="/" ;) {
			::string b = base_name (f) ;
			f = dir_name_s(f) ;
			ves[f].second.insert(::move(b)) ;                                                     // record existence of a sub-mapping in each uphill dir
		}
	}
	// then iterate while recursive entries are found, until none is left
	for( bool changed=true ; changed ;) {
		changed = false ;
		for( auto& [view,phys_excs] : ves ) {
			::vector_s& phys     = phys_excs.first  ;
			::uset_s  & excs     = phys_excs.second ;
			::vector_s  new_phys ;
			for( ::string& phy : phys ) {
				if ( auto it = ves.find(phy) ; it!=ves.end() )                                    // found a recursive entry, replace by associated phys
					for( ::string const& e : it->second.second )
						if (excs.insert(e).second) {
							auto [it,inserted] = ves.try_emplace(view+e) ;
							if (inserted) {
								changed = true ;                                                  // a new entry has been created
								for( ::string const& p : phys ) it->second.first.push_back(p+e) ;
							}
						}
				for( ::string f=dir_name_s(phy) ; +f&&f!="/" ; f=dir_name_s(f) )
					if ( auto it = ves.find(f) ; it!=ves.end() ) {
						changed = true ;                                                          // the new inserted phys may be recursive
						::string b = phy.substr(f.size()) ;
						for( ::string const& p : it->second.first ) new_phys.push_back(p+b) ;
						goto NextPhy ;
					}
				new_phys.push_back(::move(phy)) ;                                                 // match found, copy old phy
			NextPhy : ;
			}
			phys_excs.first = new_phys ;
		}
	}
	// now, there are no more recursive entries
	::vmap_s<::vector_s> res ; for( auto& [view,phys_excs] : ves ) if (+phys_excs.first) res.emplace_back(view,phys_excs.first) ;
	return res ;
}

void JobSpace::chk() const {
	if ( +chroot_dir_s && !(is_abs(chroot_dir_s)&&is_canon(chroot_dir_s)) ) throw "chroot_dir must be a canonic absolute path : "+no_slash(chroot_dir_s) ;
	if ( +root_view_s  && !(is_abs(root_view_s )&&is_canon(root_view_s )) ) throw "root_view must be a canonic absolute path : " +no_slash(root_view_s ) ;
	if ( +tmp_view_s   && !(is_abs(tmp_view_s  )&&is_canon(tmp_view_s  )) ) throw "tmp_view must be a canonic absolute path : "  +no_slash(tmp_view_s  ) ;
	for( auto const& [view,phys] : views ) {
		bool lcl_view    = _is_lcl_tmp(view,tmp_view_s) ;
		bool is_dir_view = is_dirname(view)             ;
		/**/                             if ( !view                                                                    ) throw "cannot map empty view"s                          ;
		/**/                             if ( !is_canon(view)                                                          ) throw "cannot map non-canonic view "+view               ;
		/**/                             if ( !is_dir_view && phys.size()!=1                                           ) throw "cannot overlay map non-dir " +view               ;
		for( auto const& [v,_] : views ) if ( &v!=&view && view.starts_with(v) && (v.back()=='/'||view[v.size()]=='/') ) throw "cannot map "                 +view+" within "+v  ;
		for( ::string const& phy : phys ) {
			bool lcl_phy = _is_lcl_tmp(phy,tmp_view_s) ;
			if ( !phy                             ) throw "cannot map "              +view+" to empty location"        ;
			if ( !is_canon(phy)                   ) throw "cannot map "              +view+" to non-canonic view "+phy ;
			if ( !lcl_view && lcl_phy             ) throw "cannot map external view "+view+" to local or tmp "    +phy ;
			if (  is_dir_view && !is_dirname(phy) ) throw "cannot map dir "          +view+" to file "            +phy ;
			if ( !is_dir_view &&  is_dirname(phy) ) throw "cannot map file "         +view+" to dir "             +phy ;
		}
	}
}

//
// JobRpcReq
//

::ostream& operator<<( ::ostream& os , TargetDigest const& td ) {
	const char* sep = "" ;
	/**/                    os << "TargetDigest("      ;
	if ( td.pre_exist   ) { os <<      "pre_exist"     ; sep = "," ; }
	if (+td.tflags      ) { os <<sep<< td.tflags       ; sep = "," ; }
	if (+td.extra_tflags) { os <<sep<< td.extra_tflags ; sep = "," ; }
	if (+td.crc         ) { os <<sep<< td.crc          ; sep = "," ; }
	if (+td.sig         )   os <<sep<< td.sig          ;
	return                  os <<')'                   ;
}

::ostream& operator<<( ::ostream& os , JobDigest const& jd ) {
	return os << "JobDigest(" << jd.wstatus<<':'<<jd.status <<','<< jd.targets <<','<< jd.deps << ')' ;
}

::ostream& operator<<( ::ostream& os , JobRpcReq const& jrr ) {
	/**/                      os << "JobRpcReq(" << jrr.proc <<','<< jrr.seq_id <<','<< jrr.job ;
	switch (jrr.proc) {
		case JobRpcProc::Start : os <<','<< jrr.port                           ; break ;
		case JobRpcProc::End   : os <<','<< jrr.digest <<','<< jrr.dynamic_env ; break ;
		default                :                                                 break ;
	}
	return                    os <<','<< jrr.msg <<')' ;
}

//
// JobRpcReply
//

::ostream& operator<<( ::ostream& os , MatchFlags const& mf ) {
	/**/             os << "MatchFlags(" ;
	switch (mf.is_target) {
		case Yes   : os << "target" ; if (+mf.tflags()) os<<','<<mf.tflags() ; if (+mf.extra_tflags()) os<<','<<mf.extra_tflags() ; break ;
		case No    : os << "dep,"   ; if (+mf.dflags()) os<<','<<mf.dflags() ; if (+mf.extra_dflags()) os<<','<<mf.extra_dflags() ; break ;
		case Maybe :                                                                                                                break ;
	DF}
	return           os << ')' ;
}

::ostream& operator<<( ::ostream& os , JobRpcReply const& jrr ) {
	/**/                                   os << "JobRpcReply(" << jrr.proc ;
	switch (jrr.proc) {
		case JobRpcProc::Start :
			/**/                           os <<','  << hex<<jrr.addr<<dec                ;
			/**/                           os <<','  << jrr.autodep_env                   ;
			if      (+jrr.job_space      ) os <<','  << jrr.job_space                     ;
			if      ( jrr.keep_tmp_dir   ) os <<','  << "keep"                            ;
			else if ( jrr.tmp_sz_mb==Npos) os <<",T:"<< "..."                             ;
			else                           os <<",T:"<< jrr.tmp_sz_mb                     ;
			if      (+jrr.cwd_s          ) os <<','  << jrr.cwd_s                         ;
			if      (+jrr.date_prec      ) os <<','  << jrr.date_prec                     ;
			/**/                           os <<','  << mk_printable(fmt_string(jrr.env)) ; // env may contain the non-printable EnvPassMrkr value
			/**/                           os <<','  << jrr.interpreter                   ;
			/**/                           os <<','  << jrr.kill_sigs                     ;
			if      (jrr.live_out        ) os <<','  << "live_out"                        ;
			/**/                           os <<','  << jrr.method                        ;
			if      (+jrr.network_delay  ) os <<','  << jrr.network_delay                 ;
			if      (+jrr.pre_actions    ) os <<','  << jrr.pre_actions                   ;
			/**/                           os <<','  << jrr.small_id                      ;
			if      (+jrr.star_matches   ) os <<','  << jrr.star_matches                  ;
			if      (+jrr.deps           ) os <<'<'  << jrr.deps                          ;
			if      (+jrr.static_matches ) os <<'>'  << jrr.static_matches                ;
			if      (+jrr.stdin          ) os <<'<'  << jrr.stdin                         ;
			if      (+jrr.stdout         ) os <<'>'  << jrr.stdout                        ;
			if      (+jrr.timeout        ) os <<','  << jrr.timeout                       ;
			/**/                           os <<','  << jrr.cmd                           ; // last as it is most probably multi-line
			;
		break ;
		default : ;
	}
	return                           os << ')' ;
}

//
// JobMngtRpcReq
//

::ostream& operator<<( ::ostream& os , JobMngtRpcReq const& jmrr ) {
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

::ostream& operator<<( ::ostream& os , JobMngtRpcReply const& jmrr ) {
	/**/                               os << "JobMngtRpcReply(" << jmrr.proc ;
	switch (jmrr.proc) {
		case JobMngtProc::ChkDeps    : os <<','<< jmrr.fd <<','<<                                   jmrr.ok ; break ;
		case JobMngtProc::DepVerbose : os <<','<< jmrr.fd <<','<< jmrr.dep_infos                            ; break ;
		case JobMngtProc::Decode     : os <<','<< jmrr.fd <<','<< jmrr.txt <<','<< jmrr.crc <<','<< jmrr.ok ; break ;
		case JobMngtProc::Encode     : os <<','<< jmrr.fd <<','<< jmrr.txt <<','<< jmrr.crc <<','<< jmrr.ok ; break ;
		default : ;
	}
	return                             os << ')' ;
}

//
// SubmitAttrs
//

::ostream& operator<<( ::ostream& os , SubmitAttrs const& sa ) {
	const char* sep = "" ;
	/**/                 os << "SubmitAttrs("          ;
	if (+sa.tag      ) { os <<      sa.tag       <<',' ; sep = "," ; }
	if ( sa.live_out ) { os <<sep<< "live_out,"        ; sep = "," ; }
	if ( sa.n_retries) { os <<sep<< sa.n_retries <<',' ; sep = "," ; }
	if (+sa.pressure ) { os <<sep<< sa.pressure  <<',' ; sep = "," ; }
	if (+sa.deps     ) { os <<sep<< sa.deps      <<',' ; sep = "," ; }
	if (+sa.reason   )   os <<sep<< sa.reason    <<',' ;
	return               os <<')'                      ;
}

//
// JobInfo
//

::ostream& operator<<( ::ostream& os , JobInfoStart const& jis ) {
	return os << "JobInfoStart(" << jis.submit_attrs <<','<< jis.rsrcs <<','<< jis.pre_start <<','<< jis.start <<')' ;
}

::ostream& operator<<( ::ostream& os , JobInfoEnd const& jie ) {
	return os << "JobInfoEnd(" << jie.end <<')' ;
}

JobInfo::JobInfo(::string const& filename) {
	try {
		IFStream job_stream { filename } ;
		deserialize(job_stream,start) ;
		deserialize(job_stream,end  ) ;
	} catch (...) {}                    // we get what we get
}

void JobInfo::write(::string const& filename) const {
	OFStream os{dir_guard(filename)} ;
	serialize(os,start) ;
	serialize(os,end  ) ;
}
//
// codec
//


namespace Codec {

	::string mk_decode_node( ::string const& file , ::string const& ctx , ::string const& code ) {
		return CodecPfx+mk_printable<'.'>(file)+".cdir/"+mk_printable<'.'>(ctx)+".ddir/"+mk_printable(code) ;
	}

	::string mk_encode_node( ::string const& file , ::string const& ctx , ::string const& val ) {
		return CodecPfx+mk_printable<'.'>(file)+".cdir/"+mk_printable<'.'>(ctx)+".edir/"+::string(Xxh(val).digest()) ;
	}

	::string mk_file(::string const& node) {
		return parse_printable<'.'>(node,::ref(size_t(0))).substr(sizeof(CodecPfx)-1) ; // account for terminating null in CodecPfx
	}

}
