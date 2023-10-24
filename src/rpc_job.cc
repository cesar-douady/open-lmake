// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_job.hh"

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
	/**/                             os << "SubmitAttrs("  ;
	if (sa.tag!=BackendTag::Unknown) os << sa.tag    <<',' ;
	if (sa.live_out                ) os << "live_out,"     ;
	return                           os << sa.reason <<')' ;
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
	return              os <<')'               ;
}

::ostream& operator<<( ::ostream& os , JobDigest const& jd ) {
	return os << "JobDigest(" << jd.wstatus<<':'<<jd.status <<','<< jd.targets <<','<< jd.deps << ')' ;
}

::ostream& operator<<( ::ostream& os , JobRpcReq const& jrr ) {
	os << "JobRpcReq(" << jrr.proc <<','<< jrr.seq_id <<','<< jrr.job ;
	switch (jrr.proc) {
		case JobProc::Start    : os <<','<< jrr.host                    ; break ;
		case JobProc::LiveOut  : os <<','<< jrr.txt                     ; break ;
		case JobProc::DepInfos : os <<','<< jrr.digest.deps             ; break ;
		case JobProc::End      : os <<','<< jrr.host <<','<< jrr.digest ; break ;
		default                :                                          break ;
	}
	return os << ')' ;
}

//
// JobRpcReply
//

::ostream& operator<<( ::ostream& os , TargetSpec const& tf ) {
	return os << "TargetSpec(" << tf.pattern <<','<< tf.tflags <<','<< tf.conflicts <<')' ;
}

::ostream& operator<<( ::ostream& os , JobRpcReply const& jrr ) {
	os << "JobRpcReply(" << jrr.proc ;
	switch (jrr.proc) {
		case JobProc::ChkDeps  : os <<','<< jrr.ok    ; break ;
		case JobProc::DepInfos : os <<','<< jrr.infos ; break ;
		case JobProc::Start :
			/**/                          os <<',' << hex<<jrr.addr<<dec               ;
			/**/                          os <<',' << jrr.autodep_env                  ;
			if (!jrr.chroot.empty()     ) os <<',' << jrr.chroot                       ;
			if (!jrr.cwd_s .empty()     ) os <<',' << jrr.cwd_s                        ;
			/**/                          os <<',' << mk_printable(to_string(jrr.env)) ; // env may contain the non-printable EnvPassMrkr value
			if (!jrr.static_deps.empty()) os <<',' << jrr.static_deps                  ;
			/**/                          os <<',' << jrr.interpreter                  ;
			if (jrr.keep_tmp            ) os <<',' << "keep_tmp"                       ;
			/**/                          os <<',' << jrr.kill_sigs                    ;
			if (jrr.live_out            ) os <<',' << "live_out"                       ;
			/**/                          os <<',' << jrr.method                       ;
			/**/                          os <<',' << jrr.remote_admin_dir             ;
			/**/                          os <<',' << jrr.small_id                     ;
			if (!jrr.stdin   .empty()   ) os <<'<' << jrr.stdin                        ;
			if (!jrr.stdout  .empty()   ) os <<'>' << jrr.stdout                       ;
			/**/                          os <<"*>"<< jrr.targets                      ;
			if (+jrr.timeout            ) os <<',' << jrr.timeout                      ;
			/**/                          os <<',' << jrr.cmd                          ; // last as it is most probably multi-line
			;
		break ;
		default : ;
	}
	return os << ')' ;
}

//
// JobExecRpcReq
//

::ostream& operator<<( ::ostream& os , JobExecRpcReq::AccessDigest const& ad ) {
	const char* sep = "" ;
	/**/                  os << "AccessDigest("           ;
	if (+ad.accesses  ) { os <<sep     << ad.accesses   ; sep = "," ; }
	if (+ad.dflags    ) { os <<sep     << ad.dflags     ; sep = "," ; }
	if ( ad.write     ) { os <<sep     << "write"       ; sep = "," ; }
	if (+ad.neg_tflags) { os <<sep<<'-'<< ad.neg_tflags ; sep = "," ; }
	if (+ad.pos_tflags) { os <<sep<<'+'<< ad.pos_tflags ; sep = "," ; }
	if ( ad.unlink    ) { os <<sep     << "unlink"      ; sep = "," ; }
	return                os <<')'                      ;
}

void JobExecRpcReq::AccessDigest::update( AccessDigest const& ad , AccessOrder order ) {
	dflags |= ad.dflags ;                                                      // in all cases, dflags are always accumulated
	if ( order<AccessOrder::Write || idle() ) {
		if ( order==AccessOrder::Before && !ad.idle() ) accesses  = Accesses::None ;
		/**/                                            accesses |= ad.accesses    ;
	}
	if (order>=AccessOrder::Write) {
		neg_tflags &= ~ad.pos_tflags ; neg_tflags |= ad.neg_tflags ;           // ad flags have priority over this flags
		pos_tflags &= ~ad.neg_tflags ; pos_tflags |= ad.pos_tflags ;           // .
	} else {
		neg_tflags |= ad.neg_tflags & ~pos_tflags ;                            // this flags have priority over ad flags
		pos_tflags |= ad.pos_tflags & ~neg_tflags ;                            // .
	}
	if (!ad.idle()) {
		if      (idle()                   ) unlink = ad.unlink ;
		else if (order==AccessOrder::After) unlink = ad.unlink ;
		write |= ad.write ;
	}
}

::ostream& operator<<( ::ostream& os , JobExecRpcReq const& jerr ) {
	os << "JobExecRpcReq(" << jerr.proc <<','<< jerr.date ;
	if (jerr.sync            ) os << ",sync"            ;
	if (jerr.auto_date       ) os << ",auto_date"       ;
	if (jerr.no_follow       ) os << ",no_follow"       ;
	/**/                       os <<',' << jerr.digest  ;
	if (!jerr.comment.empty()) os <<',' << jerr.comment ;
	if (jerr.has_files()) {
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

//
// JobExecRpcReply
//

::ostream& operator<<( ::ostream& os , JobExecRpcReply const& jerr ) {
	os << "JobExecRpcReply(" << jerr.proc ;
	switch (jerr.proc) {
		case JobExecRpcProc::ChkDeps  : os <<','<< jerr.ok    ; break ;
		case JobExecRpcProc::DepInfos : os <<','<< jerr.infos ; break ;
		default : ;
	}
	return os << ')' ;
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
