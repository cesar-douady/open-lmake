// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "serialize.hh"
#include "time.hh"

#include "rpc_job_common.hh"

ENUM_1( JobExecProc
,	HasFiles = Access // >=HasFiles means files field is significative
,	None
,	ChkDeps
,	Tmp               // write activity in tmp has been detected (hence clean up is required)
,	Trace             // no algorithmic info, just for tracing purpose
,	Panic             // ensure job is in error
,	Confirm
,	Access
,	Guard
,	DepVerbose
,	Decode
,	Encode
)

struct AccessDigest {                                                  // order is first read, first write, last write, unlink
	friend ::string& operator+=( ::string& , AccessDigest const& ) ;
	// accesses
	bool operator+() const { return +accesses || write!=No ; }         // true if some access of some sort is done
	// services
	bool          operator==(AccessDigest const&      ) const = default ;
	AccessDigest& operator|=(AccessDigest const&      ) ;
	AccessDigest  operator| (AccessDigest const& other) const { return ::copy(self)|= other ; }
	// data
	Bool3       write        = No ;                                    // if Maybe, write is not confirmed
	Accesses    accesses     = {} ;
	Tflags      tflags       = {} ;                                    // dflags are inherited from DepDigest
	ExtraTflags extra_tflags = {} ;
	Dflags      dflags       = {} ;
	ExtraDflags extra_dflags = {} ;
} ;

struct JobExecRpcReq {
	friend ::string& operator+=( ::string& , JobExecRpcReq const& ) ;
	// make short lines
	using Pdate = Time::Pdate    ;
	using FI    = Disk::FileInfo ;
	using AD    = AccessDigest   ;
	using P     = JobExecProc    ;
	// cxtors & casts
	#define JERR JobExecRpcReq
	#define S    ::string
	JERR() = default ;
	//
	JERR( P p ,                                S&& t={} ) : proc{p} ,                                         txt{::move(t)} { SWEAR( p!=P::Confirm && p<P::HasFiles ) ; }
	JERR( P p ,              bool s          , S&& t={} ) : proc{p} , sync{s} ,                               txt{::move(t)} { SWEAR( p!=P::Confirm && p<P::HasFiles ) ; }
	JERR( P p , uint64_t i ,          bool c , S&& t={} ) : proc{p} ,           id{i} , digest{.write=No|c} , txt{::move(t)} { SWEAR( p==P::Confirm && i             ) ; }
	JERR( P p , uint64_t i , bool s , bool c , S&& t={} ) : proc{p} , sync{s} , id{i} , digest{.write=No|c} , txt{::move(t)} { SWEAR( p==P::Confirm && i             ) ; }
	//
	JERR( P p , ::vmap_s<FI>&& fs , AD const& d , bool s , S&& c ) : proc{p} , sync{s} , files{::move(fs)} , digest{d} , txt{::move(c)} { SWEAR( p==P::Access || p==P::DepVerbose ) ; }
	JERR( P p , ::vmap_s<FI>&& fs , AD const& d ,          S&& c ) : proc{p} ,           files{::move(fs)} , digest{d} , txt{::move(c)} { SWEAR( p==P::Access || p==P::DepVerbose ) ; }
	//
	JERR( P p , S&& f , S&& c ) : proc{p} , files{{{::move(f),{}}}} , txt{::move(c)} { SWEAR(p==P::Guard) ; }
	//
	// no need for dates for codec
	JERR(P p,S&& f,S&& code,S&& c           ) : proc{p},sync{true},            date{},files{{{::move(f),{}}}},digest{.accesses=Access::Reg},txt{code},ctx{c} { SWEAR(p==P::Decode) ; }
	JERR(P p,S&& f,S&& val ,S&& c,uint8_t ml) : proc{p},sync{true},min_len{ml},date{},files{{{::move(f),{}}}},digest{.accesses=Access::Reg},txt{val },ctx{c} { SWEAR(p==P::Encode) ; }
	#undef S
	#undef JERR
	// accesses
	::string const& codec_file() const { SWEAR( proc==P::Decode || proc==P::Encode ) ; SWEAR(files.size()==1) ; return files[0].first ; }
	::string const& code      () const { SWEAR( proc==P::Decode                    ) ;                          return txt            ; }
	::string const& val       () const { SWEAR(                    proc==P::Encode ) ;                          return txt            ; }
	// services
	template<IsStream T> void serdes(T& s) {
		/**/                   ::serdes(s,proc ) ;
		/**/                   ::serdes(s,date ) ;
		/**/                   ::serdes(s,sync ) ;
		if (proc>=P::HasFiles) ::serdes(s,files) ;
		switch (proc) {
			case P::ChkDeps    :
			case P::Tmp        :
			case P::Trace      :
			case P::Panic      :
			case P::Guard      :                                                                      break ;
			case P::Confirm    : ::serdes(s,digest.write) ;                          ::serdes(s,id) ; break ;
			case P::Access     : ::serdes(s,digest      ) ; if (digest.write==Maybe) ::serdes(s,id) ; break ;
			case P::DepVerbose : ::serdes(s,digest      ) ;                                           break ;
			case P::Decode     : ::serdes(s,ctx         ) ;                                           break ;
			case P::Encode     : ::serdes(s,ctx         ) ; ::serdes(s,min_len) ;                     break ;
		DN}
		::serdes(s,txt) ;
	}
	// data
	P            proc    = {}    ;
	bool         sync    = false ;
	uint8_t      min_len = 0     ; // if proc==Encode
	uint64_t     id      = 0     ; // if proc==Access|Confirm used by Confirm to refer to confirmed Access
	Pdate        date    = New   ; //                         access date to reorder accesses during analysis
	::vmap_s<FI> files   ;
	AD           digest  ;
	::string     txt     ;         // if proc==Access|Decode|Encode|Trace comment for Access, code for Decode, value for Encode
	::string     ctx     ;         // if proc==       Decode|Encode
} ;

struct JobExecRpcReply {
	friend ::string& operator+=( ::string& , JobExecRpcReply const& ) ;
	using Proc = JobExecProc ;
	using Crc  = Hash::Crc   ;
	// cxtors & casts
	JobExecRpcReply(                                                              ) = default ;
	JobExecRpcReply( Proc p                                                       ) : proc{p}                         { SWEAR( proc!=Proc::ChkDeps && proc!=Proc::DepVerbose ) ; }
	JobExecRpcReply( Proc p , Bool3 o                                             ) : proc{p} , ok{o}                 { SWEAR( proc==Proc::ChkDeps                           ) ; }
	JobExecRpcReply( Proc p ,           ::vector<pair<Bool3/*ok*/,Crc>> const& is ) : proc{p} ,         dep_infos{is} { SWEAR( proc==Proc::DepVerbose                        ) ; }
	JobExecRpcReply( Proc p , Bool3 o , ::string const&                        t  ) : proc{p} , ok{o} , txt      {t } { SWEAR( proc==Proc::Decode || proc==Proc::Encode      ) ; }
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::Access     :                         break ;
			case Proc::ChkDeps    : ::serdes(s,ok       ) ; break ;
			case Proc::DepVerbose : ::serdes(s,dep_infos) ; break ;
			case Proc::Decode :
			case Proc::Encode :
				::serdes(s,ok ) ;
				::serdes(s,txt) ;
			break ;
		DF}
	}
	// data
	Proc                            proc      = Proc::None ;
	Bool3                           ok        = Maybe      ; // if proc==ChkDeps |Decode|Encode
	::vector<pair<Bool3/*ok*/,Crc>> dep_infos ;              // if proc==DepVerbose
	::string                        txt       ;              // if proc==         Decode|Encode (value for Decode, code for Encode)
} ;
