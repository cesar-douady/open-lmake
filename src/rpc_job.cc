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
	for( auto const& [f,a] : pre_actions ) {                                                                                        // pre_actions are adequately sorted
		SWEAR(+f) ;                                                                                                                 // acting on root dir is non-sense
		switch (a.tag) {
			case FileActionTag::None     :                                                                                          break ;
			case FileActionTag::Uniquify : if (uniquify(nfs_guard.change(f))) append_to_string(msg,"uniquified ",mk_file(f),'\n') ; break ;
			case FileActionTag::Mkdir    : mk_dir(f,nfs_guard) ;                                                                    break ;
			case FileActionTag::Unlnk    : {
				FileSig sig { nfs_guard.access(f) } ;
				if (!sig) break ;                                                                                                   // file does not exist, nothing to do
				bool done = true/*garbage*/ ;
				if ( sig!=a.sig && (a.crc==Crc::None||!a.crc.valid()||!a.crc.match(Crc(f,ha))) ) {
					done = ::rename( f.c_str() , dir_guard(QuarantineDirS+f).c_str() )==0 ;
					if (done) append_to_string(msg,"quarantined "         ,mk_file(f),'\n') ;
					else      append_to_string(msg,"failed to quarantine ",mk_file(f),'\n') ;
				} else {
					done = unlnk(nfs_guard.change(f)) ;
					if (!done ) append_to_string(msg,"failed to unlink ",mk_file(f),'\n') ;
				}
				if ( done && unlnks ) unlnks->push_back(f) ;
				ok &= done ;
			} break ;
			case FileActionTag::Rmdir :
				if (!keep_dirs.contains(f))
					try                     { rmdir(nfs_guard.change(f)) ;                                                        }
					catch (::string const&) { for( ::string d=f ; +d ; d = dir_name(d) ) if (!keep_dirs.insert(d).second) break ; } // if a dir cannot rmdir'ed, no need to try those uphill
			break ;
		DF}
	}
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
//
static void _mount( ::string const& dst , ::string const& src ) {
	Trace trace("_mount","bind",dst,src) ;
	if (::mount( src.c_str() ,  dst.c_str() , nullptr/*type*/ , MS_BIND|MS_REC , nullptr/*data*/ )!=0) throw to_string("cannot bind mount ",src," onto ",dst," : ",strerror(errno)) ;
}
static void _mount( ::string const& dst , size_t sz_mb ) {
	SWEAR(sz_mb) ;
	Trace trace("_mount","tmp",dst,sz_mb) ;
	if (::mount( "" ,  dst.c_str() , "tmpfs" , 0/*flags*/ , to_string(sz_mb,"m").c_str() )!=0) throw to_string("cannot mount tmpfs of size",sz_mb," MB onto ",dst," : ",strerror(errno)) ;
}
static void _atomic_write( ::string const& file , ::string const& data ) {
	Trace trace("_atomic_write",file,data) ;
	ssize_t cnt = ::write( AutoCloseFd(::open(file.c_str(),O_WRONLY)) , data.c_str() , data.size() ) ;
	if (cnt<0                  ) throw to_string("cannot write atomically ",data.size(), " bytes to ",file," : ",strerror(errno)          ) ;
	if (size_t(cnt)<data.size()) throw to_string("cannot write atomically ",data.size(), " bytes to ",file," : only ",cnt," bytes written") ;
}
void JobSpace::enter( ::string const& phy_root_dir , ::string const& phy_tmp_dir , size_t tmp_sz_mb , ::string const& work_dir ) const {
	Trace trace("enter",*this) ;
	//
	if (!*this) return ;
	//
	int uid = ::getuid() ;                                                                                    // must be done before unshare that invents a new user
	int gid = ::getgid() ;                                                                                    // .
	//
	if (::unshare(CLONE_NEWUSER|CLONE_NEWNS)!=0) throw to_string("cannot create namespace : ",strerror(errno)) ;
	//
	::string const* chrd               = &chroot_dir                                 ;
	bool            must_create_root   = +root_view && !is_dir(chroot_dir+root_view) ;
	bool            must_create_tmp    = +tmp_view  && !is_dir(chroot_dir+tmp_view ) ;
	trace("create",STR(must_create_root),STR(must_create_tmp)) ;
	if ( must_create_root || must_create_tmp ) {                                                              // we may not mount directly in chroot_dir
		if (!work_dir)
			throw to_string(
				"need a work dir to create"
			,	must_create_root                    ? " root view" : ""
			,	must_create_root && must_create_tmp ? " and"       : ""
			,	                    must_create_tmp ? " tmp view"  : ""
			) ;
		::vector_s top_lvls = lst_dir(+chroot_dir?chroot_dir:"/","/") ;
		mk_dir      (work_dir) ;
		unlnk_inside(work_dir) ;
		trace("top_lvls",work_dir,top_lvls) ;
		for( ::string const& f : top_lvls ) {
			::string src_f     = chroot_dir+f ;
			::string private_f = work_dir  +f ;
			switch (FileInfo(src_f).tag()) {
				case FileTag::Reg   :
				case FileTag::Empty :
				case FileTag::Exe   : OFStream{private_f                } ; _mount(private_f,src_f) ; break ; // create file
				case FileTag::Dir   : mk_dir  (private_f                ) ; _mount(private_f,src_f) ; break ; // create dir
				case FileTag::Lnk   : lnk     (private_f,read_lnk(src_f)) ;                           break ; // copy symlink
				default             : ;                                                                       // exclude weird files
			}
		}
		if (must_create_root) { SWEAR(root_view.rfind('/')==0,root_view) ; mk_dir(work_dir+root_view) ; }     // XXX : handle cases where dir is not top level
		if (must_create_tmp ) { SWEAR(tmp_view .rfind('/')==0,tmp_view ) ; mk_dir(work_dir+tmp_view ) ; }     // .
		chrd = &work_dir ;
	}
	if      (+root_view  ) _mount( *chrd+root_view , phy_root_dir ) ;
	if      (!tmp_view   ) {}
	else if (+phy_tmp_dir) _mount( *chrd+tmp_view  , phy_tmp_dir  ) ;
	else if (tmp_sz_mb   ) _mount( *chrd+tmp_view  , tmp_sz_mb    ) ;
	//
	if ( +*chrd && *chrd!="/" ) {
		_chroot( *chrd                                       ) ;
		_chdir ( +root_view ? *chrd+root_view : phy_root_dir ) ;
	}
	//
	_atomic_write( "/proc/self/setgroups" , "deny"                            ) ;                             // necessary to be allowed to write the gid_map (if desirable)
	_atomic_write( "/proc/self/uid_map"   , to_string(uid,' ',uid,' ',1,'\n') ) ;                             // XXX : unclear this is desirable as default is to map all id to self
	_atomic_write( "/proc/self/gid_map"   , to_string(gid,' ',gid,' ',1,'\n') ) ;                             // .
	//
	if (::setuid(uid)!=0) throw to_string("cannot set uid as ",uid,strerror(errno)) ;
	if (::setgid(gid)!=0) throw to_string("cannot set gid as ",uid,strerror(errno)) ;
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
		case JobProc::Start : os <<','<< jrr.port                           ; break ;
		case JobProc::End   : os <<','<< jrr.digest <<','<< jrr.dynamic_env ; break ;
		default             :                                                 break ;
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
		case JobProc::Start :
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
