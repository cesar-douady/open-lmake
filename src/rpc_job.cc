// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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
	os << "TargetDigest(" ;
	const char* sep = "" ;
	if (+td.dfs  ) { os <<sep<< td.dfs  ; sep = "," ; }
	if ( td.write) { os <<sep<< "write" ; sep = "," ; }
	if (+td.tfs  ) { os <<sep<< td.tfs  ; sep = "," ; }
	if (+td.crc  ) { os <<sep<< td.crc  ; sep = "," ; }
	return os <<')' ;
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
	return os << "TargetSpec(" << tf.pattern <<','<< tf.flags <<','<< tf.conflicts <<')' ;
}

::ostream& operator<<( ::ostream& os , JobRpcReply const& jrr ) {
	os << "JobRpcReply(" << jrr.proc ;
	switch (jrr.proc) {
		case JobProc::ChkDeps  : os <<','<< jrr.ok    ; break ;
		case JobProc::DepInfos : os <<','<< jrr.infos ; break ;
		case JobProc::Start :
			/**/                          os       << hex<<jrr.addr<<dec   ;
			/**/                          os <<',' << jrr.autodep_env      ;
			if (!jrr.chroot.empty()     ) os <<',' << jrr.chroot           ;
			if (!jrr.cwd_s .empty()     ) os <<',' << jrr.cwd_s            ;
			/**/                          os <<',' << jrr.env              ;
			if (!jrr.static_deps.empty()) os <<',' << jrr.static_deps      ;
			/**/                          os <<',' << jrr.interpreter      ;
			/**/                          os <<',' << jrr.job_tmp_dir      ;
			if (jrr.keep_tmp            ) os <<',' << "keep_tmp"           ;
			/**/                          os <<',' << jrr.kill_sigs        ;
			if (jrr.live_out            ) os <<',' << "live_out"           ;
			/**/                          os <<',' << jrr.method           ;
			/**/                          os <<',' << jrr.remote_admin_dir ;
			/**/                          os <<',' << jrr.small_id         ;
			if (!jrr.stdin   .empty()   ) os <<'<' << jrr.stdin            ;
			if (!jrr.stdout  .empty()   ) os <<'>' << jrr.stdout           ;
			/**/                          os <<"*>"<< jrr.targets          ;
			if (+jrr.timeout            ) os <<',' << jrr.timeout          ;
			/**/                          os <<',' << jrr.cmd              ; // last as it is most probably multi-line
			;
		break ;
		default : ;
	}
	return os << ')' ;
}

//
// JobExecRpcReq
//

::ostream& operator<<( ::ostream& os , JobExecRpcReq::AccessInfo const& ai ) {
	os << "AccessInfo(" ;
	const char* sep = "" ;
	if (+ai.dfs    ) { os <<sep     << ai.dfs     ; sep = "," ; }
	if ( ai.write  ) { os <<sep     << "write"    ; sep = "," ; }
	if (+ai.neg_tfs) { os <<sep<<'-'<< ai.neg_tfs ; sep = "," ; }
	if (+ai.pos_tfs) { os <<sep<<'+'<< ai.pos_tfs ; sep = "," ; }
	if ( ai.unlink ) { os <<sep     << "unlink"   ; sep = "," ; }
	return os <<')' ;
}

void JobExecRpcReq::AccessInfo::update( AccessInfo const& ai , Bool3 after ) {
	switch (after) {
		case Yes :                                                     // order is : this.read - this.write - ai.read - ai.write
			if (idle()) dfs |= ai.dfs ;                                // if this.idle(), ai.read is a real read
			unlink  &= !ai.write   ; unlink  |= ai.unlink  ;           // if ai writes, it cancels previous this.unlink
			neg_tfs &= ~ai.pos_tfs ; neg_tfs |= ai.neg_tfs ;           // ai flags have priority over this flags
			pos_tfs &= ~ai.neg_tfs ; pos_tfs |= ai.pos_tfs ;           // .
		break ;
		case Maybe :                                                   // order is : this.read - ai.read - ai.write - this.write
			dfs     |= ai.dfs                 ;                        // ai.read is always a real read
			unlink  |= ai.unlink  && !write   ;                        // if this writes, it cancels previous ai.unlink
			neg_tfs |= ai.neg_tfs &  ~pos_tfs ;                        // this flags have priority over ai flags
			pos_tfs |= ai.pos_tfs &  ~neg_tfs ;                        // .
		break ;
		case No :                                                      // order is : ai.read - ai.write - this.read - this.write
			if (ai.idle()) dfs |= ai.dfs ;                             // if ai.idle(), this.read is a real read
			else           dfs  = ai.dfs ;                             // else, this.read is canceled
			unlink  |= ai.unlink  && !write   ;                        // if this writes, it cancels previous ai.unlink
			neg_tfs |= ai.neg_tfs &  ~pos_tfs ;                        // this flags have priority over ai flags
			pos_tfs |= ai.pos_tfs &  ~neg_tfs ;                        // .
		break ;
		default : FAIL(after) ;
	}
	write |= ai.write ;                                                // in all cases, there is a write if either write
}

::ostream& operator<<( ::ostream& os , JobExecRpcReq const& jerr ) {
	os << "JobExecRpcReq(" << jerr.proc <<','<< jerr.date ;
	if (jerr.sync            ) os << ",sync"            ;
	if (jerr.auto_date       ) os << ",auto_date"       ;
	if (jerr.no_follow       ) os << ",no_follow"       ;
	/**/                       os <<',' << jerr.info    ;
	if (!jerr.comment.empty()) os <<',' << jerr.comment ;
	if (jerr.has_files()) {
		if ( +jerr.info.dfs && !jerr.auto_date ) {
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
