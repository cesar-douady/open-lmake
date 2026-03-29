// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_client.hh"

void ReqOptions::operator>>(::string& os) const {                    // START_OF_NO_COV
	First first ;
	/**/                       os << "ReqOptions("                 ;
	if      (+startup_dir_s  ) os << first("",",")<<startup_dir_s  ;
	if      ( dark_video==Yes) os << first("",",")<<"dark_video"   ;
	else if ( dark_video==No ) os << first("",",")<<"light_video"  ;
	if      (+key            ) os << first("",",")<<key            ;
	if      (+flags          ) os << first("",",")<<flags          ;
	/**/                       os << ')'                           ;
}                                                                    // END_OF_NO_COV

void ReqRpcReq::operator>>(::string& os) const {                 // START_OF_NO_COV
	/**/                        os << "ReqRpcReq("<<proc       ;
	if (proc>=ReqProc::HasArgs) os << ','<<files<<','<<options ;
	/**/                        os << ')'                      ;
}                                                                // END_OF_NO_COV

void ReqRpcReply::operator>>(::string& os) const {     // START_OF_NO_COV
	using Proc = ReqRpcReplyProc ;
	os << "ReqRpcReply("<<proc ;
	switch (proc) {
		case Proc::None   :                    break ;
		case Proc::Status : os << rc         ; break ;
		case Proc::File   :
		case Proc::Stderr :
		case Proc::Stdout : os << ",T:"<<txt ; break ;
	DF}                                                // NO_COV
	os << ')' ;
}                                                      // END_OF_NO_COV
