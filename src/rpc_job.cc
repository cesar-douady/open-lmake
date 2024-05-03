// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"

#include "rpc_job.hh"

using namespace Disk ;
using namespace Hash ;

template<class E,class T> static constexpr bool _chk_flags_tab(::array<::pair<E,T>,N<E>> tab) {
	bool res = true ;
	for( E e=E(0) ; e!=All<E> ; e++ ) res &= tab[+e].first==e ;
	return res ;
}

static_assert(_chk_flags_tab(DflagChars     )) ;
static_assert(_chk_flags_tab(ExtraDflagChars)) ;
static_assert(_chk_flags_tab(TflagChars     )) ;
static_assert(_chk_flags_tab(ExtraTflagChars)) ;

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
			case FileActionTag::None  :
			case FileActionTag::Unlnk : {
				FileSig sig { nfs_guard.access(f) } ;
				if (!sig) break ;                                                                                                    // file does not exist, nothing to do
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
			case FileActionTag::Uniquify : if (uniquify(nfs_guard.change(f))) append_to_string(msg,"uniquified ",mk_file(f),'\n') ; break ;
			case FileActionTag::Mkdir    : mkdir(f,nfs_guard) ;                                                                     break ;
			case FileActionTag::Rmdir    :
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
// JobRpcReq
//

::ostream& operator<<( ::ostream& os , TargetDigest const& td ) {
	const char* sep = "" ;
	/**/                    os << "TargetDigest("      ;
	if ( td.polluted    ) { os <<sep<< "polluted"      ; sep = "," ; }
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
	/**/                             os << "JobRpcReply(" << jrr.proc ;
	switch (jrr.proc) {
		case JobProc::Start :
			/**/                     os <<',' << hex<<jrr.addr<<dec               ;
			/**/                     os <<',' << jrr.autodep_env                  ;
			if (+jrr.chroot        ) os <<',' << jrr.chroot                       ;
			if (+jrr.cwd_s         ) os <<',' << jrr.cwd_s                        ;
			if (+jrr.date_prec     ) os <<',' << jrr.date_prec                    ;
			/**/                     os <<',' << mk_printable(to_string(jrr.env)) ; // env may contain the non-printable EnvPassMrkr value
			/**/                     os <<',' << jrr.interpreter                  ;
			if (jrr.keep_tmp       ) os <<',' << "keep_tmp"                       ;
			/**/                     os <<',' << jrr.kill_sigs                    ;
			if (jrr.live_out       ) os <<',' << "live_out"                       ;
			/**/                     os <<',' << jrr.method                       ;
			if (+jrr.network_delay ) os <<',' << jrr.network_delay                ;
			if (+jrr.pre_actions   ) os <<',' << jrr.pre_actions                  ;
			/**/                     os <<',' << jrr.remote_admin_dir             ;
			/**/                     os <<',' << jrr.small_id                     ;
			if (+jrr.star_matches  ) os <<',' << jrr.star_matches                 ;
			if (+jrr.deps          ) os <<'<' << jrr.deps                         ;
			if (+jrr.static_matches) os <<'>' << jrr.static_matches               ;
			if (+jrr.stdin         ) os <<'<' << jrr.stdin                        ;
			if (+jrr.stdout        ) os <<'>' << jrr.stdout                       ;
			if (+jrr.timeout       ) os <<',' << jrr.timeout                      ;
			/**/                     os <<',' << jrr.cmd                          ; // last as it is most probably multi-line
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

JobMngtRpcReq::JobMngtRpcReq( SI si , JI j , Fd fd_ , JobExecRpcReq&& jerr ) : seq_id{si} , job{j} , fd{fd_} {
	SWEAR( jerr.sync || !fd , jerr,fd ) ;
	switch (jerr.proc) {
		case JobExecProc::Decode : proc = P::Decode ; ctx = ::move(jerr.ctx) ; file = ::move(jerr.files[0].first) ; txt = ::move(jerr.txt) ;                          break ;
		case JobExecProc::Encode : proc = P::Encode ; ctx = ::move(jerr.ctx) ; file = ::move(jerr.files[0].first) ; txt = ::move(jerr.txt) ; min_len = jerr.min_len ; break ;
		case JobExecProc::DepVerbose : {
			proc = P::DepVerbose ;
			deps.reserve(jerr.files.size()) ;
			for( auto&& [dep,date] : jerr.files ) deps.emplace_back( ::move(dep) , DepDigest(jerr.digest.accesses,date,{}/*dflags*/,true/*parallel*/) ) ; // no need for flags to ask info
		} break ;
	DF}
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
// JobExecRpcReq
//

::ostream& operator<<( ::ostream& os , AccessDigest const& ad ) {
	const char* sep = "" ;
	/**/                         os << "AccessDigest("                          ;
	if      (+ad.accesses    ) { os <<      ad.accesses                         ; sep = "," ; }
	if      (+ad.dflags      ) { os <<sep<< ad.dflags                           ; sep = "," ; }
	if      (+ad.extra_dflags) { os <<sep<< ad.extra_dflags                     ; sep = "," ; }
	if      (+ad.tflags      ) { os <<sep<< ad.tflags                           ; sep = "," ; }
	if      (+ad.extra_tflags) { os <<sep<< ad.extra_tflags                     ; sep = "," ; }
	if      ( ad.write!=No   )   os <<sep<< "written"<<(ad.write==Maybe?"?":"") ;
	return                       os <<')'                                       ;
}

::ostream& operator<<( ::ostream& os , JobExecRpcReq const& jerr ) {
	/**/                os << "JobExecRpcReq(" << jerr.proc <<','<< jerr.date ;
	if (jerr.sync     ) os << ",sync"                                         ;
	if (jerr.solve    ) os << ",solve"                                        ;
	if (jerr.no_follow) os << ",no_follow"                                    ;
	/**/                os <<',' << jerr.digest                               ;
	if (+jerr.txt     ) os <<',' << jerr.txt                                  ;
	if (jerr.proc>=JobExecProc::HasFiles) {
		if ( +jerr.digest.accesses && !jerr.solve ) os <<','<<               jerr.files  ;
		else                                        os <<','<< mk_key_vector(jerr.files) ;
	}
	return os <<')' ;
}

AccessDigest& AccessDigest::operator|=(AccessDigest const& other) {
	if (write!=Yes) accesses     |= other.accesses     ;
	/**/            write        |= other.write        ;
	/**/            tflags       |= other.tflags       ;
	/**/            extra_tflags |= other.extra_tflags ;
	/**/            dflags       |= other.dflags       ;
	/**/            extra_dflags |= other.extra_dflags ;
	return *this ;
}

//
// JobExecRpcReply
//

::ostream& operator<<( ::ostream& os , JobExecRpcReply const& jerr ) {
	os << "JobExecRpcReply(" << jerr.proc ;
	switch (jerr.proc) {
		case JobExecProc::None       :                                     ; break ;
		case JobExecProc::ChkDeps    : os <<','<< jerr.ok                  ; break ;
		case JobExecProc::DepVerbose : os <<','<< jerr.dep_infos           ; break ;
		case JobExecProc::Decode     :
		case JobExecProc::Encode     : os <<','<< jerr.txt <<','<< jerr.ok ; break ;
	DF}
	return os << ')' ;
}

JobExecRpcReply::JobExecRpcReply( JobMngtRpcReply&& jmrr ) {
	switch (jmrr.proc) {
		case JobMngtProc::None       :                         proc = Proc::None       ;                                                     break ;
		case JobMngtProc::ChkDeps    : SWEAR(jmrr.ok!=Maybe) ; proc = Proc::ChkDeps    ; ok = jmrr.ok ;                                      break ;
		case JobMngtProc::DepVerbose :                         proc = Proc::DepVerbose ;                dep_infos = ::move(jmrr.dep_infos) ; break ;
		case JobMngtProc::Decode     :                         proc = Proc::Decode     ; ok = jmrr.ok ; txt       = ::move(jmrr.txt      ) ; break ;
		case JobMngtProc::Encode     :                         proc = Proc::Encode     ; ok = jmrr.ok ; txt       = ::move(jmrr.txt      ) ; break ;
	DF}
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
