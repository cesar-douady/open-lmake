// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_client.hh"

::ostream& operator<<( ::ostream& os , ReqOptions const& ro ) {
	os << "ReqOptions(" ;
	const char* sep = "" ;
	if (!ro.startup_dir_s.empty()) { os <<sep<< ro.startup_dir_s ; sep = "," ; }
	if ( ro.reverse_video==Yes   ) { os <<sep<< "reverse_video"  ; sep = "," ; }
	if ( ro.reverse_video==No    ) { os <<sep<< "normal_video"   ; sep = "," ; }
	if (+ro.key                  ) { os <<sep<< ro.key           ; sep = "," ; }
	if (+ro.flags                ) { os <<sep<< ro.flags         ; sep = "," ; }
	return os << ')' ;
}

::ostream& operator<<( ::ostream& os , ReqRpcReq const& rrr ) {
	/**/                            os << "ReqRpcReq(" << rrr.proc           ;
	if (rrr.proc>=ReqProc::HasArgs) os <<','<< rrr.files <<','<< rrr.options ;
	return                          os <<')'                                 ;
}

::ostream& operator<<( ::ostream& os , ReqRpcReply const& rrr ) {
	os << "ReqRpcReply("<<rrr.kind ;
	switch (rrr.kind) {
		case ReqKind::None      :                               break ;
		case ReqKind::Status    : os << (rrr.ok?",ok":",err") ; break ;
		case ReqKind::Txt       : os <<','<< rrr.txt          ; break ;
		default : FAIL(rrr.kind) ;
	}
	return os << ')' ;
}
