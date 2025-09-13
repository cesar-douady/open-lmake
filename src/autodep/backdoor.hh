// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"

#include "record.hh"

using namespace Disk ;

enum class BackdoorErr : uint8_t {
	Ok
,	OfficialReadlinkErr // value -1 is the normal readlink error and we need to distinguish backdoor errors
,	ParseErr
,	PokeErr
} ;

namespace Backdoor {

	static constexpr char   Pfx[]  = PRIVATE_ADMIN_DIR_S "backdoor/" ;

	static constexpr Fd     MagicFd     { +Fd::Cwd - 100 }                ; // any improbable negative value (to avoid conflict with real fd) will do
	static constexpr char   MagicPfx[]  = PRIVATE_ADMIN_DIR_S "backdoor/" ; // any improbable prefix will do
	static constexpr size_t MagicPfxLen = sizeof(MagicPfx)-1              ; // -1 to account for terminating null

	using Func = ::function<::pair_s<ssize_t/*len*/>(Record&,::string const& args,char* buf,size_t sz)> ;

	::umap_s<Func> const& get_func_tab() ;

	template<class T> typename T::Reply call(T const& args) {
		::string file = cat(MagicPfx,T::Cmd,'/',mk_printable(serialize(args)))           ;
		::string buf  ( T::MaxReplySz+1 , 0 )                                            ; // +1 to distinguish truncation
		ssize_t  cnt  = ::readlinkat( MagicFd , file.c_str() , buf.data() , buf.size() ) ; // try to go through autodep to process args
		if (cnt<0) {
			throw_if( cnt<-1 , "backdoor error ",BackdoorErr(-cnt) ) ;
			return args.process(::ref(Record(New,Yes/*enabled*/))) ;                       // no autodep available, directly process args
		}
		SWEAR( size_t(cnt)<buf.size() , cnt,buf.size() ) ;
		buf.resize(size_t(cnt)) ;
		return deserialize<typename T::Reply>(buf) ;
	}

	template<class T> ::pair_s<ssize_t/*len*/> func( Record& r , ::string const& args_str , char* buf , size_t sz ) {
		::string          parsed    ;
		T                 cmd       ;
		::string          descr     = "backdoor" ;
		typename T::Reply reply     ;
		::string          reply_str ;
		try {
			try { size_t pos = 0 ; parsed    = parse_printable(args_str,pos) ; throw_unless(pos==args_str.size()) ; } catch (::string const&) { throw "parse error"s           ; }
			try {                  cmd       = deserialize<T> (parsed      ) ;                                      } catch (::string const&) { throw "deserialization error"s ; }
			/**/                   descr     = cmd.descr      (            ) ;
			/**/                   reply     = cmd.process    (r           ) ;
			try {                  reply_str = serialize      (reply       ) ;                                      } catch (::string const&) { throw "serialization error"s   ; }
		} catch (::string const& e) {
			Fd::Stderr.write(cat(e," while procesing ",descr,'\n')) ;
			errno = EINVAL ;
			return { descr , -+BackdoorErr::ParseErr } ;
		}
		sz = ::min( reply_str.size() , sz ) ;
		::memcpy( buf , reply_str.data() , sz ) ;
		return { descr , sz } ;
	}

	struct Enable {
		friend ::string& operator+=( ::string& , Enable const& ) ;
		static constexpr char Cmd[] = "enable" ;
		using Reply = bool/*enabled*/ ;
		static constexpr size_t MaxReplySz = sizeof(Reply) ;
		// services
		Reply    process(Record& r) const ;
		::string descr  (         ) const ;
		// data
		Bool3 enable = Maybe ; // Maybe means dont update record state
	} ;

	struct Solve {
		friend ::string& operator+=( ::string& , Solve const& ) ;
		static constexpr char Cmd[] = "solve" ;
		struct Reply : Record::Solve {
			// cxtors & casts
			using Record::Solve::Solve ;
			// services
			template<IsStream S> void serdes(S& s) {
				Record::Solve::serdes(s) ;
				::serdes(s,file_info) ;
			}
			// data
			FileInfo file_info {} ;                                                                     // file info must be probed in process as we are protected against recording
		} ;
		static const size_t MaxReplySz ;
		// services
		Reply    process(Record& r) const ;
		::string descr  (         ) const ;
		// data
		::string file      = {}            ;
		bool     no_follow = false         ;
		bool     read      = false         ;                                                            // if both read & write, overlays are not supported
		bool     write     = false         ;                                                            // .
		bool     create    = false         ;
		Comment  comment   = Comment::None ;
	} ;
	constexpr size_t Solve::MaxReplySz = Record::Solve::MaxSz + (sizeof(Reply)-sizeof(Record::Solve)) ; // add local field sizes

}
