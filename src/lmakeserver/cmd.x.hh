// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#ifdef DATA_DEF
namespace Engine {

	bool forget( Fd fd , ReqOptions const& , ::vector<Node> const& targets ) ;
	bool freeze( Fd fd , ReqOptions const& , ::vector<Node> const& targets ) ;
	bool show  ( Fd fd , ReqOptions const& , ::vector<Node> const& targets ) ;

	using CmdFunc = bool (*)(Fd,ReqOptions const&,::vector<Node> const&) ;
	extern CmdFunc g_cmd_tab[+ReqProc::N] ;

}
#endif
