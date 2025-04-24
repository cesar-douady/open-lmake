// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_client.hh"

::string& operator+=( ::string& os , ReqOptions const& ro ) {   // START_OF_NO_COV
	const char* sep = "" ;
	/**/                          os << "ReqOptions("         ;
	if (+ro.startup_dir_s     ) { os <<      ro.startup_dir_s ; sep = "," ; }
	if ( ro.reverse_video==Yes) { os <<sep<< "reverse_video"  ; sep = "," ; }
	if ( ro.reverse_video==No ) { os <<sep<< "normal_video"   ; sep = "," ; }
	if (+ro.key               ) { os <<sep<< ro.key           ; sep = "," ; }
	if (+ro.flags             )   os <<sep<< ro.flags         ;
	return                        os <<')'                    ;
}                                                               // END_OF_NO_COV

::string& operator+=( ::string& os , ReqRpcReq const& rrr ) {                  // START_OF_NO_COV
	/**/                            os << "ReqRpcReq(" << rrr.proc           ;
	if (rrr.proc>=ReqProc::HasArgs) os <<','<< rrr.files <<','<< rrr.options ;
	return                          os <<')'                                 ;
}                                                                              // END_OF_NO_COV

::string& operator+=( ::string& os , ReqRpcReply const& rrr ) {   // START_OF_NO_COV
	using Proc = ReqRpcReplyProc ;
	os << "ReqRpcReply("<<rrr.proc ;
	switch (rrr.proc) {
		case Proc::None   :                               break ;
		case Proc::Status : os << (rrr.ok?",ok":",err") ; break ;
		case Proc::File   :
		case Proc::Stderr :
		case Proc::Stdout : os <<",T:"<< rrr.txt        ; break ;
	DF}                                                           // NO_COV
	return os << ')' ;
}                                                                 // END_OF_NO_COV
