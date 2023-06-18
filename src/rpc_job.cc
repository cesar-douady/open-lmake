// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_job.hh"

::ostream& operator<<( ::ostream& os , JobReason const& jr ) {
	os << "JobReason(" << jr.tag ;
	if (jr.tag>=JobReasonTag::HasNode) os << ',' << jr.node ;
	return os << ')' ;
}

::ostream& operator<<( ::ostream& os , DepDigest const& dd ) {
	os << "DepDigest(" ;
	if (!dd.garbage) os << dd.date <<',' ;
	return os << dd.order <<')' ;
}

::ostream& operator<<( ::ostream& os , TargetDigest const& td ) {
	os << "TargetDigest(" ;
	if (+td.dfs ) os      <<td.dfs <<',' ;
	/**/          os      <<td.tfs       ;
	if (td.write) os << ",write"         ;
	if (+td.crc ) os <<','<< td.crc      ;
	return os <<')' ;
}

::ostream& operator<<( ::ostream& os , JobDigest const& jd ) {
	return os << "JobDigest(" << jd.status <<','<< jd.targets <<','<< jd.deps << ')' ;
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

::ostream& operator<<( ::ostream& os , TargetSpec const& tf ) {
	return os << "TargetSpec(" << tf.pattern <<','<< tf.flags <<','<< tf.conflicts <<')' ;
}

::ostream& operator<<( ::ostream& os , JobRpcReply const& jrr ) {
	os << "JobRpcReply(" << jrr.proc ;
	switch (jrr.proc) {
		case JobProc::ChkDeps  : os <<','<< jrr.ok    ; break ;
		case JobProc::DepInfos : os <<','<< jrr.infos ; break ;
		case JobProc::Start :
			/**/                         os       << hex<<jrr.addr<<dec   ;
			/**/                         os <<',' << jrr.ancillary_file   ;
			if (jrr.auto_mkdir         ) os <<',' << "auto_mkdir"         ;
			if (!jrr.chroot.empty()    ) os <<',' << jrr.chroot           ;
			if (!jrr.cwd   .empty()    ) os <<',' << jrr.cwd              ;
			/**/                         os <<',' << jrr.env              ;
			if (!jrr.force_deps.empty()) os <<',' << jrr.force_deps       ;
			/**/                         os <<',' << jrr.interpreter      ;
			/**/                         os <<',' << jrr.job_tmp_dir      ;
			if (jrr.keep_tmp           ) os <<',' << "keep_tmp"           ;
			/**/                         os <<',' << jrr.kill_sigs        ;
			if (jrr.live_out           ) os <<',' << "live_out"           ;
			/**/                         os <<',' << jrr.reason           ;
			/**/                         os <<',' << jrr.remote_admin_dir ;
			/**/                         os <<',' << jrr.rsrcs            ;
			/**/                         os <<',' << jrr.small_id         ;
			if (!jrr.stdin .empty()    ) os <<'<' << jrr.stdin            ;
			if (!jrr.stdout.empty()    ) os <<'>' << jrr.stdout           ;
			/**/                         os <<"*>"<< jrr.targets          ;
			if (+jrr.timeout           ) os <<',' << jrr.timeout          ;
			/**/                         os <<',' << jrr.script           ; // last as it is most probably multi-line
			;
		break ;
		default : ;
	}
	return os << ')' ;
}

::ostream& operator<<( ::ostream& os , JobInfo const& ji ) {
	return os << "JobInfo(" << ji.end_date <<','<< ji.stdout.size() <<','<< ji.wstatus <<')' ;
}

::ostream& operator<<( ::ostream& os , JobExecRpcReq const& jerr ) {
	os << "JobExecRpcReq(" << jerr.proc <<','<< jerr.date ;
	switch (jerr.proc) {
		case JobExecRpcProc::Targets :
		case JobExecRpcProc::Unlinks : { ::vector_s fs ; for( auto [f,d] : jerr.files ) fs.push_back(f) ; os <<','<< fs         ; } break ;
		case JobExecRpcProc::Trace   :
		case JobExecRpcProc::Deps    : {                                                                  os <<','<< jerr.files ; } break ;
		default : ;
	}
	if (jerr.sync) os << ",sync" ;
	return os <<','<< jerr.comment <<')' ;
}

::ostream& operator<<( ::ostream& os , JobExecRpcReply const& jerr ) {
	os << "JobExecRpcReply(" << jerr.proc ;
	if (jerr.proc==JobExecRpcProc::DepInfos) os <<','<< jerr.infos ;
	return os << ')' ;
}
