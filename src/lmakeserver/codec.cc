// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "rpc_job.hh"

#include "core.hh"

#include "codec.hh"

using namespace Time ;

namespace Codec {

	void codec_thread_func(CodecClosure&& cc) ;

	::ostream& operator<<( ::ostream& os , CodecClosure const& cc ) {
		return os << "CodecClosure(" << (cc.encode?"encode":"decode") <<','<< cc.file <<','<< cc.ctx << ',' << cc.txt <<')' ;
	}

	static ::vmap_s<Ddate> file_dates ;

	void codec_thread_func(CodecClosure&& cc) {
		if (cc.encode) OMsgBuf().send( cc.reply_fd , JobRpcReply(JobProc::Encode,"") ) ;
		else           OMsgBuf().send( cc.reply_fd , JobRpcReply(JobProc::Decode,"") ) ;
		::close(cc.reply_fd) ;
	}

}

namespace Engine {
	QueueThread<Codec::CodecClosure> g_codec_queue{'D',Codec::codec_thread_func} ;
}
