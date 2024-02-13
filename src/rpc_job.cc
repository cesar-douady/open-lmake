// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"

#include "rpc_job.hh"

using namespace Disk ;
using namespace Hash ;

//
// FileAction
//

::ostream& operator<<( ::ostream& os , FileAction const& fa ) {
	/**/                                os << "FileAction(" << fa.tag ;
	if (fa.tag<=FileActionTag::HasFile) os <<','<< fa.date            ;
	return                              os <<')'                      ;
}

::pair<vector_s/*unlinks*/,pair_s<bool/*ok*/>/*msg*/> do_file_actions( ::vmap_s<FileAction>&& pre_actions , NfsGuard& nfs_guard , Hash::Algo ha ) {
	::uset_s   keep_dirs ;
	::vector_s unlinks   ;
	::string   msg       ;
	bool       ok        = true ;
	//
	for( auto const& [f,a] : pre_actions ) {                                                                                        // pre_actions are adequately sorted
		SWEAR(+f) ;                                                                                                                 // acting on root dir is non-sense
		if ( a.crc==Crc::None && a.tag<=FileActionTag::HasFile ) continue ;                                                         // no file to act on
		switch (a.tag) {
			case FileActionTag::None     :                                                                                     break ;
			case FileActionTag::Uniquify : if (uniquify(nfs_guard.change(f))) append_to_string(msg,"uniquified ",mk_file(f)) ; break ;
			case FileActionTag::Mkdir    : mkdir(f,nfs_guard) ;                                                                break ;
			case FileActionTag::Unlink   : {
				FileInfo fi { nfs_guard.access(f) } ;
				if (+fi) {
					bool done = true/*garbage*/ ;
					if ( fi.date!=a.date && a.crc.valid() && (a.crc==Crc::None||!a.crc.match(Crc(f,ha))) ) {
						done = ::rename( f.c_str() , dir_guard(QuarantineDirS+f).c_str() )==0 ;
						if (done) append_to_string(msg,"quarantined "         ,mk_file(f),'\n') ;
						else      append_to_string(msg,"failed to quarantine ",mk_file(f),'\n') ;
					} else {
						done = unlink(nfs_guard.change(f)) ;
						if (!done) append_to_string(msg,"failed to unlink ",mk_file(f),'\n') ;
					}
					if (done) unlinks.push_back(f) ;
					else      ok = false ;
				}
			} break ;
			case FileActionTag::Rmdir :
				if (!keep_dirs.contains(f))
					try                     { rmdir(nfs_guard.change(f)) ;                                                        }
					catch (::string const&) { for( ::string d=f ; +d ; d = dir_name(d) ) if (!keep_dirs.insert(d).second) break ; } // if a dir cannot rmdir'ed, no need to try those uphill
			break ;
			default : FAIL(a) ;
		}
	}
	return {unlinks,{msg,ok}} ;
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
// SubmitAttrs
//

::ostream& operator<<( ::ostream& os , SubmitAttrs const& sa ) {
	/**/                              os << "SubmitAttrs("     ;
	if ( sa.tag!=BackendTag::Unknown) os << sa.tag       <<',' ;
	if ( sa.live_out                ) os << "live_out,"        ;
	if ( sa.n_retries               ) os << sa.n_retries <<',' ;
	if (+sa.pressure                ) os << sa.pressure  <<',' ;
	return                            os << sa.reason    <<')' ;
}

//
// JobRpcReq
//

::ostream& operator<<( ::ostream& os , TargetDigest const& td ) {
	const char* sep = "" ;
	/**/                os << "TargetDigest("  ;
	if (+td.accesses) { os <<sep<< td.accesses ; sep = "," ; }
	if ( td.write   ) { os <<sep<< "write"     ; sep = "," ; }
	if (+td.tflags  ) { os <<sep<< td.tflags   ; sep = "," ; }
	if (+td.crc     ) { os <<sep<< td.crc      ; sep = "," ; }
	if (+td.date    ) { os <<sep<< td.date     ; sep = "," ; }
	return              os <<')'               ;
}

::ostream& operator<<( ::ostream& os , JobDigest const& jd ) {
	return os << "JobDigest(" << jd.wstatus<<':'<<jd.status <<','<< jd.targets <<','<< jd.deps << ')' ;
}

::ostream& operator<<( ::ostream& os , JobRpcReq const& jrr ) {
	os << "JobRpcReq(" << jrr.proc <<','<< jrr.seq_id <<','<< jrr.job ;
	switch (jrr.proc) {
		case JobProc::LiveOut  : os <<','<< jrr.msg         ; break ;
		case JobProc::DepInfos : os <<','<< jrr.digest.deps ; break ;
		case JobProc::End      : os <<','<< jrr.digest      ; break ;
		default                :                              break ;
	}
	return os << ')' ;
}

JobRpcReq::JobRpcReq( SI si , JI j , JobExecRpcReq&& jerr ) : seq_id{si} , job{j} {
	switch (jerr.proc) {
		case JobExecRpcProc::Decode : proc = P::Decode  ; msg = ::move(jerr.txt) ; file = ::move(jerr.files[0].first) ; ctx = ::move(jerr.ctx) ;                          break ;
		case JobExecRpcProc::Encode : proc = P::Encode  ; msg = ::move(jerr.txt) ; file = ::move(jerr.files[0].first) ; ctx = ::move(jerr.ctx) ; min_len = jerr.min_len ; break ;
		case JobExecRpcProc::DepInfos : {
			::vmap_s<DepDigest> ds ; ds.reserve(jerr.files.size()) ;
			for( auto&& [dep,date] : jerr.files ) ds.emplace_back( ::move(dep) , DepDigest(jerr.digest.accesses,date,{}/*dflags*/,true/*parallel*/) ) ; // no need for flags to ask info
			proc        = P::DepInfos ;
			digest.deps = ::move(ds) ;
		} break ;
		default : FAIL(jerr.proc) ;
	}
}

//
// JobRpcReply
//

::ostream& operator<<( ::ostream& os , MatchFlags const& mf ) {
	os << "MatchFlags(" ;
	switch (mf.is_target) {
		case Yes   : os << "target," << mf.tflags() ; break ;
		case No    : os << "dep,"    << mf.dflags() ; break ;
		case Maybe :                                  break ;
		default : FAIL(mf.is_target) ;
	}
	return os << ')' ;
}

::ostream& operator<<( ::ostream& os , JobRpcReply const& jrr ) {
	os << "JobRpcReply(" << jrr.proc ;
	switch (jrr.proc) {
		case JobProc::ChkDeps  : os <<','<<                                 jrr.ok ; break ;
		case JobProc::Decode   : os <<','<< jrr.txt <<','<< jrr.crc <<','<< jrr.ok ; break ;
		case JobProc::Encode   : os <<','<< jrr.txt <<','<< jrr.crc <<','<< jrr.ok ; break ;
		case JobProc::DepInfos : os <<','<< jrr.dep_infos                          ; break ;
		case JobProc::Start :
			/**/                     os <<',' << hex<<jrr.addr<<dec               ;
			/**/                     os <<',' << jrr.autodep_env                  ;
			if (+jrr.chroot        ) os <<',' << jrr.chroot                       ;
			if (+jrr.cwd_s         ) os <<',' << jrr.cwd_s                        ;
			/**/                     os <<',' << mk_printable(to_string(jrr.env)) ; // env may contain the non-printable EnvPassMrkr value
			/**/                     os <<',' << jrr.interpreter                  ;
			if (jrr.keep_tmp       ) os <<',' << "keep_tmp"                       ;
			/**/                     os <<',' << jrr.kill_sigs                    ;
			if (jrr.live_out       ) os <<',' << "live_out"                       ;
			/**/                     os <<',' << jrr.method                       ;
			if (+jrr.network_delay ) os <<',' << jrr.network_delay                ;
			/**/                     os <<',' << jrr.remote_admin_dir             ;
			/**/                     os <<',' << jrr.small_id                     ;
			if (+jrr.star_matches  ) os <<',' << jrr.star_matches                 ;
			if (+jrr.static_deps   ) os <<'<' << jrr.static_deps                  ;
			if (+jrr.static_matches) os <<'>' << jrr.static_matches               ;
			if (+jrr.stdin         ) os <<'<' << jrr.stdin                        ;
			if (+jrr.stdout        ) os <<'>' << jrr.stdout                       ;
			if (+jrr.timeout       ) os <<',' << jrr.timeout                      ;
			/**/                     os <<',' << jrr.cmd                          ; // last as it is most probably multi-line
			;
		break ;
		default : ;
	}
	return os << ')' ;
}

//
// JobExecRpcReq
//

::ostream& operator<<( ::ostream& os , AccessDigest const& ad ) {
	/**/             os << "AccessDigest(" << static_cast<DepDigest const&>(ad) ;
	if (+ad.tflags ) os <<","<< ad.tflags                                       ;
	if (+ad.dflags ) os <<","<< ad.dflags                                       ;
	if ( ad.write  ) os <<",write"                                              ;
	if ( ad.unlink ) os <<",unlink"                                             ;
	return           os <<')'                                                   ;
}

::ostream& operator<<( ::ostream& os , JobExecRpcReq const& jerr ) {
	/**/                os << "JobExecRpcReq(" << jerr.proc <<','<< jerr.date ;
	if (jerr.sync     ) os << ",sync"                                         ;
	if (jerr.auto_date) os << ",auto_date"                                    ;
	if (jerr.no_follow) os << ",no_follow"                                    ;
	/**/                os <<',' << jerr.digest                               ;
	if (+jerr.txt     ) os <<',' << jerr.txt                                  ;
	if (jerr.proc>=JobExecRpcProc::HasFiles) {
		if ( +jerr.digest.accesses && !jerr.auto_date ) {
			os <<','<< jerr.files ;
		} else {
			::vector_s fs ;
			for( auto [f,d] : jerr.files ) fs.push_back(f) ;
			os <<','<< fs ;
		}
	}
	return os <<')' ;
}

void AccessDigest::update( AccessDigest const& ad , AccessOrder order ) {
	switch (order) {
		case AccessOrder::Before :
			crc_date(ad) ;                                                 // read info is sampled at first read
			accesses = ad.accesses | (ad.idle()?accesses:Accesses::None) ;
		break ;
		case AccessOrder::BetweenReadAndWrite :
			if (!accesses) crc_date(ad) ;                                  // read info is sampled at first read
			/**/           accesses |= ad.accesses ;
		break ;
		default : SWEAR(order>=AccessOrder::Write) ;                       // ensure we have not forgotten a case
	}
	//
	tflags |= ad.tflags ;
	dflags |= ad.dflags ;
	//
	if (!ad.idle()) {
		if ( idle() || order==AccessOrder::After ) unlink = ( unlink && !ad.write ) || ad.unlink ;
		/**/                                       write  =   write                 || ad.write  ;
	}
}

//
// JobExecRpcReply
//

::ostream& operator<<( ::ostream& os , JobExecRpcReply const& jerr ) {
	os << "JobExecRpcReply(" << jerr.proc ;
	switch (jerr.proc) {
		case JobExecRpcProc::None     :                                     ; break ;
		case JobExecRpcProc::ChkDeps  : os <<','<< jerr.ok                  ; break ;
		case JobExecRpcProc::DepInfos : os <<','<< jerr.dep_infos           ; break ;
		case JobExecRpcProc::Decode   :
		case JobExecRpcProc::Encode   : os <<','<< jerr.txt <<','<< jerr.ok ; break ;
		default : FAIL(jerr.proc) ;
	}
	return os << ')' ;
}

JobExecRpcReply::JobExecRpcReply( JobRpcReply const& jrr ) {
	switch (jrr.proc) {
		case JobProc::None     :                        proc = Proc::None     ;                                             break ;
		case JobProc::ChkDeps  : SWEAR(jrr.ok!=Maybe) ; proc = Proc::ChkDeps  ; ok        = jrr.ok        ;                 break ;
		case JobProc::DepInfos :                        proc = Proc::DepInfos ; dep_infos = jrr.dep_infos ;                 break ;
		case JobProc::Decode   :                        proc = Proc::Decode   ; ok        = jrr.ok        ; txt = jrr.txt ; break ;
		case JobProc::Encode   :                        proc = Proc::Encode   ; ok        = jrr.ok        ; txt = jrr.txt ; break ;
		default : FAIL(jrr.proc) ;
	}
}

//
// JobSserverRpcReq
//

::ostream& operator<<( ::ostream& os , JobServerRpcReq const& jsrr ) {
	/**/                                        os << "JobServerRpcReq(" << jsrr.proc <<','<< jsrr.seq_id ;
	if (jsrr.proc==JobServerRpcProc::Heartbeat) os <<','<< jsrr.job                                       ;
	return                                      os <<')'                                                  ;
}

//
// JobInfoStart
//

::ostream& operator<<( ::ostream& os , JobInfoStart const& jis ) {
	return os << "JobInfoStart(" << jis.submit_attrs <<','<< jis.rsrcs <<','<< jis.pre_start <<','<< jis.start <<')' ;
}

//
// JobInfoEnd
//

::ostream& operator<<( ::ostream& os , JobInfoEnd const& jie ) {
	return os << "JobInfoEnd(" << jie.end <<')' ;
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
