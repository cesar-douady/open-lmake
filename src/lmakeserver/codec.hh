// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "fd.hh"
#include "thread.hh"
#include "rpc_job.hh"
#include "time.hh"

#include "idxed.hh"

namespace Codec {

	extern bool writable ;

	bool/*ok*/ refresh( NodeIdx , ReqIdx ) ;

	struct Closure {
		friend ::ostream& operator<<( ::ostream& , Closure const& ) ;
		struct Entry {
			// log_date is the semantic date, i.e. :
			// - all decode & encode nodes for this file have this common date
			// - when file physical date was this date, it was canonic
			Time::Pdate sample_date ; // date at which file has been sampled on disk
			Time::Ddate log_date    ;
			Time::Ddate phys_date   ; // actual file date on disk
		} ;
		// statics
		static void s_init    () ;
		static void s_finalize() ;
		//
		static bool/*ok*/ s_refresh( ::string const& file , NodeIdx , ::vector<ReqIdx> const& ) ;
	private :
		static void _s_canonicalize( ::string const& file ,           ::vector<ReqIdx> const& ) ;
		// static data
	public :
		static ::umap_s<Entry> s_tab ;
		// cxtors & casts
		Closure() = default ;
		Closure(
			JobProc                 p
		,	::string&&              code
		,	::string&&              f
		,	::string&&              c
		,	::vector<ReqIdx> const& rs
		,	Fd                      rfd
		) :
			proc    {        p     }
		,	reply_fd{        rfd   }
		,	file    { ::move(f   ) }
		,	ctx     { ::move(c   ) }
		,	txt     { ::move(code) }
		,	reqs    {        rs    }
		{ SWEAR(p==JobProc::Decode) ; }
		Closure(
			JobProc                 p
		,	::string&&              val
		,	::string&&              f
		,	::string&&              c
		,	uint8_t                 ml
		,	::vector<ReqIdx> const& rs
		,	Fd                      rfd
		) :
			proc     {        p    }
		,	_min_len {        ml   }
		,	reply_fd {        rfd  }
		,	file     { ::move(f  ) }
		,	ctx      { ::move(c  ) }
		,	txt      { ::move(val) }
		,	reqs     {        rs   }
		{ SWEAR(p==JobProc::Encode) ; }
		// accesses
		uint8_t min_len() const { SWEAR(proc==JobProc::Encode) ; return _min_len ; }
		// services
	private :
	public :
		JobRpcReply decode() const ;
		JobRpcReply encode() const ;
		// data
		JobProc proc = {}/*garbage*/ ;
	private :
		uint8_t _min_len = 0/*garbage*/ ;
	public :
		Fd               reply_fd ;
		::string         file     ;
		::string         ctx      ;
		::string         txt      ;
		::vector<ReqIdx> reqs     ;
	} ;

	struct ValMrkr  {} ;
	struct CodeMrkr {} ;

	using Val  = Vector::Simple<CodecIdx,char,ValMrkr > ;
	using Code = Vector::Simple<CodecIdx,char,CodeMrkr> ;

	extern QueueThread<Codec::Closure>* g_codec_queue ;

}

namespace Codec::Persistent {

	using ValFile  = Store::VectorFile< false/*autolock*/ , void/*Hdr*/ , CodecIdx , char , uint32_t , 64/*MinSz*/ > ;
	using CodeFile = Store::VectorFile< false/*autolock*/ , void/*Hdr*/ , CodecIdx , char , uint32_t ,  4/*MinSz*/ > ;

	extern ValFile  val_file  ;
	extern CodeFile code_file ;

}

namespace Vector {
	template<> struct File<Codec::Val > { static constexpr Codec::Persistent::ValFile & file = Codec::Persistent::val_file  ; } ;
	template<> struct File<Codec::Code> { static constexpr Codec::Persistent::CodeFile& file = Codec::Persistent::code_file ; } ;
}
