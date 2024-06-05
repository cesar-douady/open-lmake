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
	for( auto const& [f,a] : pre_actions ) {                                                                                        // pre_actions are adequately sorted
		SWEAR(+f) ;                                                                                                                 // acting on root dir is non-sense
		switch (a.tag) {
			case FileActionTag::None  :
			case FileActionTag::Unlnk : {
				FileSig sig { nfs_guard.access(f) } ;
				if (!sig) break ;                                                                                                   // file does not exist, nothing to do
				bool done = true/*garbage*/ ;
				bool quarantine = sig!=a.sig && (a.crc==Crc::None||!a.crc.valid()||!a.crc.match(Crc(f,ha))) ;
				if (quarantine) {
					done = ::rename( f.c_str() , dir_guard(QuarantineDirS+f).c_str() )==0 ;
					if (done) append_to_string(msg,"quarantined "         ,mk_file(f),'\n') ;
					else      append_to_string(msg,"failed to quarantine ",mk_file(f),'\n') ;
				} else {
					SWEAR(is_lcl(f)) ;
					done = unlnk(nfs_guard.change(f)) ;
					if (!done) append_to_string(msg,"failed to unlink ",mk_file(f),'\n') ;
				}
				trace(STR(quarantine),STR(done),f) ;
				if ( done && unlnks ) unlnks->push_back(f) ;
				ok &= done ;
			} break ;
			case FileActionTag::Uniquify : if (uniquify(nfs_guard.change(f))) append_to_string(msg,"uniquified ",mk_file(f),'\n') ; break ;
			case FileActionTag::Mkdir    : mk_dir(f,nfs_guard) ;                                                                    break ;
			case FileActionTag::Rmdir    :
				if (!keep_dirs.contains(f))
					try                     { rmdir(nfs_guard.change(f)) ;                                                        }
					catch (::string const&) { for( ::string d=f ; +d ; d = dir_name(d) ) if (!keep_dirs.insert(d).second) break ; } // if a dir cannot rmdir'ed, no need to try those uphill
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
	/**/                  os <<'('                       ;
	if (+js.chroot_dir) { os <<     "C:"<< js.chroot_dir ; sep = "," ; }
	if (+js.root_view ) { os <<sep<<"R:"<< js.root_view  ; sep = "," ; }
	if (+js.tmp_view  )   os <<sep<<"T:"<< js.tmp_view   ;
	return                os <<')'                       ;
}

static void _chroot(::string const& dir) { Trace trace("_chroot",dir) ; if (::chroot(dir.c_str())!=0) throw to_string("cannot chroot to ",dir," : ",strerror(errno)) ; }
static void _chdir (::string const& dir) { Trace trace("_chdir" ,dir) ; if (::chdir (dir.c_str())!=0) throw to_string("cannot chdir to " ,dir," : ",strerror(errno)) ; }

static void _mount( ::string const& dst , ::string const& src ) {                           // bind mount
	Trace trace("_mount","bind",dst,src) ;
	if (::mount( src.c_str() ,  dst.c_str() , nullptr/*type*/ , MS_BIND|MS_REC , nullptr/*data*/ )!=0)
		throw to_string("cannot bind mount ",src," onto ",dst," : ",strerror(errno)) ;
}
static void _mount( ::string const& dst , size_t sz_mb ) {                                  // tmpfs mount
	SWEAR(sz_mb) ;
	Trace trace("_mount","tmp",dst,sz_mb) ;
	if (::mount( "" ,  dst.c_str() , "tmpfs" , 0/*flags*/ , to_string(sz_mb,"m").c_str() )!=0)
		throw to_string("cannot mount tmpfs of size",sz_mb," MB onto ",dst," : ",strerror(errno)) ;
}
static void _mount( ::string const& dst , ::vector_s const& srcs , ::string const& work ) { // overlay mount
	SWEAR(+srcs) ;
	SWEAR(srcs.size()>1,dst,srcs,work) ;                                                    // use bind mount in that case
	//
	Trace trace("_mount","overlay",dst,srcs,work) ;
	for( size_t i=1 ; i<srcs.size() ; i++ )
		if (srcs[i].find(':')!=Npos)
			throw to_string("cannot overlay mount ",dst," to ",srcs,"with embedded columns (:)") ;
	mk_dir(work) ;
	//
	::string                                data  = "userxattr"                                     ;
	/**/                                    data += ",upperdir="+srcs[0].substr(0,srcs[0].size()-1) ;
	/**/                                    data += ",lowerdir="+srcs[1].substr(0,srcs[1].size()-1) ;
	for( size_t i=2 ; i<srcs.size() ; i++ ) data += ':'         +srcs[i].substr(0,srcs[i].size()-1) ;
	/**/                                    data += ",workdir=" +work                               ;
	SWEAR(dst.back()=='/') ;
	::string dst_no_s = dst ; dst_no_s.pop_back() ;
	if (::mount( nullptr ,  dst_no_s.c_str() , "overlay" , 0 , data.c_str() )!=0)
		throw to_string("cannot overlay mount ",dst_no_s," to ",data," : ",strerror(errno)) ;
}

static void _atomic_write( ::string const& file , ::string const& data ) {
	Trace trace("_atomic_write",file,data) ;
	AutoCloseFd fd = ::open(file.c_str(),O_WRONLY|O_TRUNC) ;
	if (!fd) throw to_string("cannot open ",file," for writing") ;
	ssize_t cnt = ::write( fd , data.c_str() , data.size() ) ;
	if (cnt<0                  ) throw to_string("cannot write atomically ",data.size(), " bytes to ",file," : ",strerror(errno)          ) ;
	if (size_t(cnt)<data.size()) throw to_string("cannot write atomically ",data.size(), " bytes to ",file," : only ",cnt," bytes written") ;
}

static bool _is_lcl_tmp( ::string const& f , ::string const& tmp_view ) {
	if (is_lcl(f)               ) return true  ;
	if (!tmp_view               ) return false ;
	if (!f.starts_with(tmp_view)) return false ;
	if (tmp_view.back()=='/'    ) return true  ;
	/**/                          return f[tmp_view.size()]=='/' || f.size()==tmp_view.size() ;
} ;

bool/*entered*/ JobSpace::enter( ::string const& phy_root_dir , ::string const& phy_tmp_dir , size_t tmp_sz_mb , ::string const& work_dir , ::vector_s const& src_dirs_s ) const {
	Trace trace("enter",*this,phy_root_dir,phy_tmp_dir,tmp_sz_mb,work_dir) ;
	//
	if (!*this) return false/*entered*/ ;
	//
	int uid = ::getuid() ;                                                                           // must be done before unshare that invents a new user
	int gid = ::getgid() ;                                                                           // .
	//
	if (::unshare(CLONE_NEWUSER|CLONE_NEWNS)!=0) throw to_string("cannot create namespace : ",strerror(errno)) ;
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
		if ( +tmp_view && view.starts_with(tmp_view) ) continue     ;
		if ( !is_lcl(view)                           ) goto BadView ;
		if ( (view+'/').starts_with(AdminDirS)       ) goto BadView ;
		continue ;
	BadView :
		throw "cannot map "+view+" that must either be local in the repository or lie in tmp_view" ; // else we must guarantee canon after mapping, extend src_dirs to include their views, etc.
	}
	//
	::string phy_super_root_dir ;                                                                    // dir englobing all relative source dirs
	::string super_root_view    ;                                                                    // .
	if (+root_view) {
		phy_super_root_dir = phy_root_dir ; for( size_t i=0 ; i<src_dirs_uphill_lvl ; i++ ) phy_super_root_dir = dir_name(phy_super_root_dir) ;
		super_root_view    = root_view    ; for( size_t i=0 ; i<src_dirs_uphill_lvl ; i++ ) super_root_view    = dir_name(super_root_view   ) ;
		SWEAR(+phy_super_root_dir,phy_root_dir,src_dirs_uphill_lvl) ;                                                                           // this should have been checked earlier
		if (!super_root_view) {
			highest.pop_back() ;
			throw to_string(
				"cannot map repository dir to ",root_view," with relative source dir ",highest
			,	", "
			,	"consider setting <rule>.root_view=",mk_py_str("/repo"+phy_root_dir.substr(phy_super_root_dir.size()))
			) ;
		}
		if (root_view.substr(super_root_view.size())!=phy_root_dir.substr(phy_super_root_dir.size()))
			throw to_string(
				"last ",src_dirs_uphill_lvl," components do not match between physical root dir and root view"
			,	", "
			,	"consider setting <rule>.root_view=",mk_py_str("/repo"+phy_root_dir.substr(phy_super_root_dir.size()))
			) ;
	}
	if ( +super_root_view && super_root_view.rfind('/')!=0 ) throw "non top-level root_view not yet implemented"s ; // XXX : handle cases where dir is not top level
	if ( +tmp_view        && tmp_view       .rfind('/')!=0 ) throw "non top-level tmp_view not yet implemented"s  ; // .
	//
	::string        work_root_dir    ;                                                                              // must be outside the if as we may keep the address of it in chrd
	::string const* chrd             = &chroot_dir                                             ;
	bool            must_create_root = +super_root_view && !is_dir(chroot_dir+super_root_view) ;
	bool            must_create_tmp  = +tmp_view        && !is_dir(chroot_dir+tmp_view )       ;
	trace("create",STR(must_create_root),STR(must_create_tmp)) ;
	if ( must_create_root || must_create_tmp ) {                                                                    // we may not mount directly in chroot_dir
		if (!work_dir)
			throw to_string(
				"need a work dir to create"
			,	must_create_root                    ? " root view" : ""
			,	must_create_root && must_create_tmp ? " and"       : ""
			,	                    must_create_tmp ? " tmp view"  : ""
			) ;
		::vector_s top_lvls = lst_dir(+chroot_dir?chroot_dir:"/","/") ;
		work_root_dir = work_dir+"/root" ;
		mk_dir      (work_root_dir) ;
		unlnk_inside(work_root_dir) ;
		trace("top_lvls",work_root_dir,top_lvls) ;
		for( ::string const& f : top_lvls ) {
			::string src_f     = chroot_dir   +f ;
			::string private_f = work_root_dir+f ;
			switch (FileInfo(src_f).tag()) {
				case FileTag::Reg   :
				case FileTag::Empty :
				case FileTag::Exe   : OFStream{private_f                } ; _mount(private_f,src_f) ; break ;       // create file
				case FileTag::Dir   : mk_dir  (private_f                ) ; _mount(private_f,src_f) ; break ;       // create dir
				case FileTag::Lnk   : lnk     (private_f,read_lnk(src_f)) ;                           break ;       // copy symlink
				default             : ;                                                                             // exclude weird files
			}
		}
		if (must_create_root) mk_dir(work_root_dir+super_root_view) ;
		if (must_create_tmp ) mk_dir(work_root_dir+tmp_view       ) ;
		chrd = &work_root_dir ;
	}
	// mapping uid/gid is necessary to manage overlayfs
	_atomic_write( "/proc/self/setgroups" , "deny"                            ) ;                                   // necessary to be allowed to write the gid_map (if desirable)
	_atomic_write( "/proc/self/uid_map"   , to_string(uid,' ',uid,' ',1,'\n') ) ;
	_atomic_write( "/proc/self/gid_map"   , to_string(gid,' ',gid,' ',1,'\n') ) ;
	//
	::string root_dir ;
	if (!root_view)   root_dir = phy_root_dir    ;
	else            { root_dir = *chrd+root_view ; _mount( *chrd+super_root_view , phy_super_root_dir ) ; }
	if (+tmp_view) {
		if      (+phy_tmp_dir) _mount( *chrd+tmp_view , phy_tmp_dir ) ;
		else if (tmp_sz_mb   ) _mount( *chrd+tmp_view , tmp_sz_mb   ) ;
	}
	//
	if      ( +*chrd && *chrd!="/" ) { _chroot(*chrd) ; _chdir(root_dir) ; }
	else if ( +root_view           )                    _chdir(root_dir) ;
	if (+views) {
		::string root_dir_s = root_dir+'/' ;
		size_t   i          = 0            ;
		for( auto const& [view,phys] : views ) {
			::string   abs_view = mk_abs(view,root_dir_s) ;
			::vector_s abs_phys ; for( ::string const& phy : phys ) abs_phys.push_back(mk_abs(phy,root_dir_s)) ;
			//
			/**/                              if ( view.back()=='/' && _is_lcl_tmp(view,tmp_view) ) mk_dir(view) ;
			for( ::string const& phy : phys ) if ( phy .back()=='/' && _is_lcl_tmp(phy ,tmp_view) ) mk_dir(phy ) ;
			//
			if (phys.size()==1) {
				_mount( abs_view , abs_phys[0] ) ;
			} else {
				::string work = is_lcl(phys[0]) ? to_string(work_dir,"/view_work/",i++) : phys[0].substr(0,phys[0].size()-1)+".work" ; // if not in the repo, it must be in tmp
				mk_dir(work) ;
				_mount( abs_view , abs_phys , mk_abs(work,root_dir_s) ) ;
			}
		}
	}
	return true/*entered*/ ;
}

void JobSpace::chk() const {
	if ( +chroot_dir && !(is_abs(chroot_dir)&&is_canon(chroot_dir)) ) throw "chroot_dir must be a canonic absolute path : "+chroot_dir ;
	if ( +root_view  && !(is_abs(root_view )&&is_canon(root_view )) ) throw "root_view must be a canonic absolute path : " +root_view  ;
	if ( +tmp_view   && !(is_abs(tmp_view  )&&is_canon(tmp_view  )) ) throw "tmp_view must be a canonic absolute path : "  +tmp_view   ;
	for( auto const& [view,phys] : views ) {
		bool lcl_view = _is_lcl_tmp(view,tmp_view) ;
		bool dir_view = view.back()=='/'           ;
		/**/                             if ( !view                                                                    ) throw "cannot map empty view"s                          ;
		/**/                             if ( !is_canon(view)                                                          ) throw "cannot map non-canonic view "+view               ;
		/**/                             if ( !dir_view && phys.size()!=1                                              ) throw "cannot overlay map non-dir " +view               ;
		for( auto const& [v,_] : views ) if ( &v!=&view && view.starts_with(v) && (v.back()=='/'||view[v.size()]=='/') ) throw "cannot map "                 +view+" within "+v  ;
		for( ::string const& phy : phys ) {
			bool lcl_phy = _is_lcl_tmp(phy,tmp_view) ;
			if ( !phy                         ) throw "cannot map "              +view+" to empty location"        ;
			if ( !is_canon(phy)               ) throw "cannot map "              +view+" to non-canonic view "+phy ;
			if ( !lcl_view && lcl_phy         ) throw "cannot map external view "+view+" to local or tmp "    +phy ;
			if (  dir_view && phy.back()!='/' ) throw "cannot map dir "          +view+" to file "            +phy ;
			if ( !dir_view && phy.back()=='/' ) throw "cannot map file "         +view+" to dir "             +phy ;
			for( auto const& [v,_] : views ) {
				if ( phy.starts_with(v  ) && (v  .back()=='/'||phy[v  .size()]=='/') ) throw "cannot map "+view+" to "+phy+" within "   +v ;
				if ( v  .starts_with(phy) && (phy.back()=='/'||v  [phy.size()]=='/') ) throw "cannot map "+view+" to "+phy+" englobing "+v ;
			}
		}
	}
}

//
// JobRpcReq
//

::ostream& operator<<( ::ostream& os , TargetDigest const& td ) {
	const char* sep = "" ;
	/**/                    os << "TargetDigest("      ;
	if ( td.pre_exist   ) { os <<sep<< "pre_exist"     ; sep = "," ; }
	if (+td.tflags      ) { os <<sep<< td.tflags       ; sep = "," ; }
	if (+td.extra_tflags) { os <<sep<< td.extra_tflags ; sep = "," ; }
	if (+td.crc         ) { os <<sep<< td.crc          ; sep = "," ; }
	if (+td.sig         ) { os <<sep<< td.sig          ; sep = "," ; }
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
			/**/                           os <<',' << hex<<jrr.addr<<dec               ;
			/**/                           os <<',' << jrr.autodep_env                  ;
			if      (+jrr.job_space      ) os <<',' << jrr.job_space                    ;
			if      ( jrr.keep_tmp_dir   ) os <<',' << "keep"                           ;
			else if ( jrr.tmp_sz_mb==Npos) os <<',' << "..."                            ;
			else                           os <<',' << jrr.tmp_sz_mb                    ;
			if (+jrr.cwd_s               ) os <<',' << jrr.cwd_s                        ;
			if (+jrr.date_prec           ) os <<',' << jrr.date_prec                    ;
			/**/                           os <<',' << mk_printable(to_string(jrr.env)) ; // env may contain the non-printable EnvPassMrkr value
			/**/                           os <<',' << jrr.interpreter                  ;
			/**/                           os <<',' << jrr.kill_sigs                    ;
			if (jrr.live_out             ) os <<',' << "live_out"                       ;
			/**/                           os <<',' << jrr.method                       ;
			if (+jrr.network_delay       ) os <<',' << jrr.network_delay                ;
			if (+jrr.pre_actions         ) os <<',' << jrr.pre_actions                  ;
			/**/                           os <<',' << jrr.small_id                     ;
			if (+jrr.star_matches        ) os <<',' << jrr.star_matches                 ;
			if (+jrr.deps                ) os <<'<' << jrr.deps                         ;
			if (+jrr.static_matches      ) os <<'>' << jrr.static_matches               ;
			if (+jrr.stdin               ) os <<'<' << jrr.stdin                        ;
			if (+jrr.stdout              ) os <<'>' << jrr.stdout                       ;
			if (+jrr.timeout             ) os <<',' << jrr.timeout                      ;
			/**/                           os <<',' << jrr.cmd                          ; // last as it is most probably multi-line
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
		return to_string(CodecPfx,mk_printable<'.'>(file),".cdir/",mk_printable<'.'>(ctx),".ddir/",mk_printable(code)) ;
	}

	::string mk_encode_node( ::string const& file , ::string const& ctx , ::string const& val ) {
		return to_string(CodecPfx,mk_printable<'.'>(file),".cdir/",mk_printable<'.'>(ctx),".edir/",::string(Xxh(val).digest())) ;
	}

	::string mk_file(::string const& node) {
		return parse_printable<'.'>(node).first.substr(sizeof(CodecPfx)-1) ; // account for terminating nul which is included in CodecPfx
	}

}
