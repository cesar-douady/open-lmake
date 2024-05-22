// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"

#include "record.hh"

using namespace Disk ;

namespace Backdoor {

	static constexpr char   Pfx[]  = PRIVATE_ADMIN_DIR "/backdoor/" ;

	static constexpr Fd     MagicFd     { +Fd::Cwd - 100 }               ; // any improbable negative value (to avoid conflict with real fd) will do
	static constexpr char   MagicPfx[]  = PRIVATE_ADMIN_DIR "/backdoor/" ; // any improbable prefix will do
	static constexpr size_t MagicPfxLen = sizeof(MagicPfx)-1             ; // -1 to account for terminating null

	using Func = ::function<ssize_t/*len*/(Record&,::string const& args,char* buf,size_t sz)> ;

	::umap_s<Func> const& get_func_tab() ;

	template<class T> typename T::Reply call(T&& args) {
		::string file = to_string(MagicPfx,T::Cmd,'/',mk_printable(serialize(args)))     ;
		::string buf  ( args.reply_len()+1 , 0 )                                         ; // +1 to distinguish truncation
		ssize_t  cnt  = ::readlinkat( MagicFd , file.c_str() , buf.data() , buf.size() ) ; // try to go through autodep to process args
		if (cnt<0) return args.process(::ref(Record(New,Yes/*enabled*/))) ;                // no autodep available, directly process args
		size_t ucnt = cnt ;
		SWEAR(ucnt<buf.size(),cnt,buf.size()) ;
		buf.resize(ucnt) ;
		return deserialize<typename T::Reply>(buf) ;
	}

	template<class T> ssize_t/*len*/ func( Record& r , ::string const& args_str , char* buf , size_t sz ) {
		::string reply_str ;
		try                    { reply_str = serialize(deserialize<T>(parse_printable(args_str).first).process(r)) ; }
		catch(::string const&) { errno = EIO ; return -1 ;                                                           }
		sz = ::min(reply_str.size(),sz) ;
		::memcpy( buf , reply_str.data() , sz ) ;
		return sz ;
	}

	struct Enable {
		static constexpr char Cmd[] = "enable" ;
		using Reply = bool/*enabled*/ ;
		size_t reply_len(         ) const ;
		Reply  process  (Record& r) const ;
		// data
		Bool3 enable = Maybe ; // Maybe means dont update record state
	} ;

	struct Solve {
		static constexpr char Cmd[] = "solve" ;
		struct Reply {
			::string real      ;                // if read : last file in case of overlay, if write : first file in case of overlay, if both : overlays not supported
			FileInfo file_info ;
			FileLoc  file_loc  = FileLoc::Ext ;
			Accesses accesses  ;                // accesses to report on real
		} ;
		size_t reply_len(         ) const ;
		Reply  process  (Record& r) const ;
		// data
		::string file      = {}      ;
		bool     no_follow = false   ;
		bool     read      = false   ;          // if both read & write, overlays are not supported
		bool     write     = false   ;          // .
		bool     create    = false   ;
		::string comment   = "solve" ;
	} ;

}
