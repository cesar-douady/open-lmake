// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "serialize.hh"
#include "time.hh"

#include "rpc_job_common.hh"

ENUM_2( JobExecProc
,	HasFile     = Guard  // >=HasFile     means file      field is significative
,	HasFileInfo = Access // >=HasFileInfo means file_info field is significative
,	None
,	ChkDeps
,	DepVerbose
,	Tmp                  // write activity in tmp has been detected (hence clean up is required)
,	Trace                // no algorithmic info, just for tracing purpose
,	Panic                // ensure job is in error
,	Confirm
,	Guard
,	Decode
,	Encode
,	Access
,	DepVerbosePush
)

struct AccessDigest {                                                // order is first read, first write, last write, unlink
	friend ::string& operator+=( ::string& , AccessDigest const& ) ;
	// accesses
	bool operator+() const { return +accesses || write!=No ; }       // true if some access of some sort is done
	// services
	bool          operator==(AccessDigest const&   ) const = default ;
	AccessDigest& operator|=(AccessDigest const&   ) ;
	AccessDigest  operator| (AccessDigest const& ad) const { return ::copy(self) |= ad ;   }
	AccessDigest& operator|=(Accesses     const& a )       { accesses |= a ; return self ; }
	AccessDigest  operator| (Accesses     const& a ) const { return ::copy(self) |= a  ;   }
	// data
	Bool3       write        = No ;                                  // if Maybe, write is not confirmed
	Accesses    accesses     = {} ;
	Tflags      tflags       = {} ;                                  // dflags are inherited from DepDigest
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
	using Proc  = JobExecProc    ;
	// services
	void chk() const {
		SWEAR( (proc>=Proc::HasFile    ) == +file      , proc,file           ) ;
		SWEAR( (proc< Proc::HasFileInfo) <= !file_info , proc,file,file_info ) ;
		switch (proc) {
			case Proc::ChkDeps        :
			case Proc::Tmp            : SWEAR( +date              &&  !digest                       , proc,date,     digest ) ; break ;
			case Proc::DepVerbose     : SWEAR( +date && sync==Yes                                   , proc,date,sync        ) ; break ;
			case Proc::Trace          :
			case Proc::Panic          :
			case Proc::DepVerbosePush : SWEAR( !date && sync!=Yes &&  !digest                       , proc,date,sync,digest ) ; break ;
			case Proc::Confirm        : SWEAR( !date                                                , proc,date             ) ; break ;
			case Proc::Guard          : SWEAR( !date              &&  !digest                       , proc,date,     digest ) ; break ;
			case Proc::Decode         :
			case Proc::Encode         : SWEAR( !date && sync==Yes &&  !digest                       , proc,date,sync,digest ) ; break ;
			case Proc::Access         : SWEAR( +date              && (!digest.accesses)<=!file_info , proc,date,     digest ) ; break ;
		DF}
	}
	template<IsStream T> void serdes(T& s) {
		/**/                         ::serdes(s,proc     ) ;
		/**/                         ::serdes(s,sync     ) ;
		if (proc>=Proc::HasFile    ) ::serdes(s,file     ) ;
		if (proc>=Proc::HasFileInfo) ::serdes(s,file_info) ;
		switch (proc) {
			case Proc::ChkDeps    :
			case Proc::Tmp        : ::serdes(s,date) ;                                                  break ;
			case Proc::Confirm    :                    ::serdes(s,digest.write) ;                       break ;
			case Proc::DepVerbose : ::serdes(s,date) ; ::serdes(s,digest      ) ;                       break ;
			case Proc::Decode     :                    ::serdes(s,ctx         ) ;                       break ;
			case Proc::Encode     :                    ::serdes(s,ctx         ) ; ::serdes(s,min_len) ; break ;
			case Proc::Access     : ::serdes(s,date) ; ::serdes(s,digest      ) ;                       break ;
		DN}
		::serdes(s,txt) ;
	}
	// data
	Proc     proc      = {} ;
	Bool3    sync      = No ; // Maybe means transport as sync (not using fast_report), but not actually sync
	uint8_t  min_len   = 0  ; // if proc==Encode
	AD       digest    = {} ;
	Pdate    date      = {} ; // access date to reorder accesses during analysis
	::string file      = {} ;
	FI       file_info = {} ;
	::string txt       = {} ; // if proc==Access|Decode|Encode|Trace comment for Access, code for Decode, value for Encode
	::string ctx       = {} ; // if proc==       Decode|Encode
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
	// accesses
	bool operator+() const { return proc!=Proc::None ; }
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::ChkDeps    : ::serdes(s,ok       ) ;             break ;
			case Proc::DepVerbose : ::serdes(s,dep_infos) ;             break ;
			case Proc::Decode     :
			case Proc::Encode     : ::serdes(s,ok ) ; ::serdes(s,txt) ; break ;
		DN}
	}
	// data
	Proc                            proc      = Proc::None ;
	Bool3                           ok        = Maybe      ; // if proc==ChkDeps|Decode|Encode
	::vector<pair<Bool3/*ok*/,Crc>> dep_infos ;              // if proc==DepVerbose            , same order as deps
	::string                        txt       ;              // if proc==        Decode|Encode, value for Decode, code for Encode
} ;
