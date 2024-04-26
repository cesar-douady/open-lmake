// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"

#include "rpc_job_exec.hh"

using namespace Disk ;
using namespace Hash ;

//
// AccessDigest
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
// JobExecRpcReq
//

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
