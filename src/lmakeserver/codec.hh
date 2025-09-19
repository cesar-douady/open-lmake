// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "fd.hh"
#include "thread.hh"
#include "rpc_job.hh"
#include "time.hh"

#include "idxed.hh"

namespace Codec {

	bool/*ok*/ refresh( NodeIdx , ReqIdx ) ;

	Bool3 _mk_codec_entries( CodecMap const& , ReqIdx , bool create ) ; // No means cannot create, Maybe means must create, Yes means already created
	//
	inline bool/*ok*/ can_mk_codec_entries( CodecMap const& map , ReqIdx r ) {
		return _mk_codec_entries( map , r , false/*create*/ )!=No ;
	}
	inline bool/*ok*/ mk_codec_entries( CodecMap const& map , ReqIdx r ) {
		switch ( _mk_codec_entries( map , r , false/*create*/ ) ) { //!         ok
			case No    :                                                 return false ;
			case Maybe : _mk_codec_entries( map , r , true/*create*/ ) ; return true  ;
			case Yes   :                                                 return true  ;
		DF}
	}

	struct Closure {
		friend ::string& operator+=( ::string& , Closure const& ) ;
		using Proc = JobMngtProc ;
		struct Entry {
			// log_date is the semantic date, i.e. :
			// - all decode & encode nodes for this file have this common date
			// - when file physical date was this date, it was canonic
			Time::Pdate sample_date ; // date at which file has been sampled on disk
			Time::Ddate log_date    ;
			Time::Ddate phy_date    ; // actual file date on disk
		} ;
		// statics
		static void s_init    () ;
		static void s_finalize() ;
		//
		static bool/*ok*/ s_refresh( ::string const& file , NodeIdx=0 , ::vector<ReqIdx> const& ={} ) ;
	private :
		static void _s_canonicalize( ::string const& file , ::vector<ReqIdx> const& ) ;
		// static data
	public :
		static ::umap_s<Entry> s_tab ;
		// cxtors & casts
		Closure() = default ;
		Closure(
			Proc       p
		,	JobIdx     j
		,	Fd         fd_
		,	SeqId      id
		,	::string&& code
		,	::string&& f
		,	::string&& c
		) :
			proc   {        p     }
		,	job    {        j     }
		,	fd     {        fd_   }
		,	seq_id {        id    }
		,	file   { ::move(f   ) }
		,	ctx    { ::move(c   ) }
		,	txt    { ::move(code) }
		{ SWEAR(p==Proc::Decode) ; }
		Closure(
			Proc       p
		,	JobIdx     j
		,	Fd         fd_
		,	SeqId      id
		,	::string&& val
		,	::string&& f
		,	::string&& c
		,	uint8_t    ml
		) :
			proc     {        p    }
		,	_min_len {        ml   }
		,	job      {        j    }
		,	fd       {        fd_  }
		,	seq_id   {        id   }
		,	file     { ::move(f  ) }
		,	ctx      { ::move(c  ) }
		,	txt      { ::move(val) }
		{ SWEAR(p==Proc::Encode) ; }
		// accesses
		uint8_t min_len() const { SWEAR(proc==Proc::Encode) ; return _min_len ; }
		// services
		JobMngtRpcReply decode() const ;
		JobMngtRpcReply encode() const ;
		// data
		Proc proc = {} ;
	private :
		uint8_t _min_len = 0 ;
	public :
		JobIdx   job    = 0 ;
		Fd       fd     ;
		SeqId    seq_id = 0 ;
		::string file   ;
		::string ctx    ;
		::string txt    ;
	} ;

	struct ValMrkr  {} ;
	struct CodeMrkr {} ;

	using Val  = Vector::Simple<CodecIdx,char,ValMrkr > ;
	using Code = Vector::Simple<CodecIdx,char,CodeMrkr> ;

	extern StaticUniqPtr<QueueThread<Closure>> g_codec_queue ;

}

namespace Codec::Persistent {

	using ValFile  = Store::VectorFile< false/*autolock*/ , void/*Hdr*/ , CodecIdx , NCodecIdxBits , char , uint32_t , 64/*MinSz*/ > ;
	using CodeFile = Store::VectorFile< false/*autolock*/ , void/*Hdr*/ , CodecIdx , NCodecIdxBits , char , uint32_t ,  4/*MinSz*/ > ;

	extern ValFile  val_file  ;
	extern CodeFile code_file ;

}

namespace Vector {
	template<> struct File<Codec::Val > { static constexpr Codec::Persistent::ValFile & file = Codec::Persistent::val_file  ; } ;
	template<> struct File<Codec::Code> { static constexpr Codec::Persistent::CodeFile& file = Codec::Persistent::code_file ; } ;
}
