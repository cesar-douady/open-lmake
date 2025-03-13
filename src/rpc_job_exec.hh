// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "serialize.hh"
#include "time.hh"

#include "rpc_job_common.hh"

ENUM_2( JobExecProc
,	HasFile     = CodecCtx // >=HasFile     means file      field is significative
,	HasFileInfo = Access   // >=HasFileInfo means file_info field is significative
,	None
,	ChkDeps
,	Confirm
,	DepVerbose
,	Tmp                    // write activity in tmp has been detected (hence clean up is required)
,	CodecCtx
,	CodecFile
,	Decode
,	Guard
,	Panic                  // ensure job is in error
,	Trace                  // no algorithmic info, just for tracing purpose
,	Access
,	DepVerbosePush
,	Encode                 // file_info is used to transport min_len
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
	// accesses
	uint8_t const& min_len() const { SWEAR(proc==Proc::Encode) ; return *reinterpret_cast<uint8_t const*>(&file_info) ; }
	uint8_t      & min_len()       { SWEAR(proc==Proc::Encode) ; return *reinterpret_cast<uint8_t      *>(&file_info) ; }
	// services
	void chk() const {
		SWEAR( (proc>=Proc::HasFile    ) == +file      , proc,file           ) ;
		SWEAR( (proc< Proc::HasFileInfo) <= !file_info , proc,file,file_info ) ; // Encode uses file_info to store min_len
		switch (proc) {
			case Proc::ChkDeps        :
			case Proc::Tmp            : SWEAR( +date                &&  !digest , proc,date,     digest ) ; break ;
			case Proc::DepVerbose     : SWEAR( +date && sync==Yes               , proc,date,sync        ) ; break ;
			case Proc::Trace          :
			case Proc::Panic          : SWEAR( !date && sync==No    &&  !digest , proc,date,sync,digest ) ; break ;
			case Proc::CodecFile      :
			case Proc::CodecCtx       :
			case Proc::DepVerbosePush : SWEAR( !date && sync==Maybe &&  !digest , proc,date,sync,digest ) ; break ;
			case Proc::Confirm        : SWEAR( !date                            , proc,date             ) ; break ;
			case Proc::Guard          : SWEAR( !date                &&  !digest , proc,date,     digest ) ; break ;
			case Proc::Decode         :
			case Proc::Encode         : SWEAR( !date && sync==Yes   &&  !digest , proc,date,sync,digest ) ; break ;
			case Proc::Access         : SWEAR( +date                            , proc,date             ) ; break ;
		DF}
	}
	template<IsStream T> void serdes(T& s) {
		/**/                         ::serdes(s,proc        ) ;
		/**/                         ::serdes(s,sync        ) ;
		/**/                         ::serdes(s,comment     ) ;
		/**/                         ::serdes(s,comment_exts) ;
		/**/                         ::serdes(s,id          ) ;
		if (proc>=Proc::HasFile    ) ::serdes(s,file        ) ;
		if (proc>=Proc::HasFileInfo) ::serdes(s,file_info   ) ;
		switch (proc) {
			case Proc::ChkDeps    :
			case Proc::Tmp        :                            ::serdes(s,date) ; break ;
			case Proc::Confirm    : ::serdes(s,digest.write) ;                    break ;
			case Proc::DepVerbose : ::serdes(s,digest      ) ; ::serdes(s,date) ; break ;
			case Proc::Access     : ::serdes(s,digest      ) ; ::serdes(s,date) ; break ;
		DN}
	}
	// data
	Proc        proc         = {}            ;
	Bool3       sync         = No            ;                                   // Maybe means transport as sync (not using fast_report), but not actually sync
	Comment     comment      = Comment::None ;
	CommentExts comment_exts = {}            ;
	AD          digest       = {}            ;
	pid_t       id           = 0             ;                                   // used to distinguish flows from different processes when muxed on fast report fd
	Pdate       date         = {}            ;                                   // access date to reorder accesses during analysis
	::string    file         = {}            ;                                   // contains all text info for CodecCtx, Encode, Decode, Trace and Panic
	FI          file_info    = {}            ;
} ;

struct JobExecRpcReply {
	friend ::string& operator+=( ::string& , JobExecRpcReply const& ) ;
	using Proc = JobExecProc ;
	using Crc  = Hash::Crc   ;
	// cxtors & casts
	JobExecRpcReply(                                               ) = default ;
	JobExecRpcReply( Proc p                                        ) : proc{p}                                 { SWEAR( proc!=Proc::ChkDeps && proc!=Proc::DepVerbose ) ; }
	JobExecRpcReply( Proc p , Bool3 o                              ) : proc{p} , ok{o}                         { SWEAR( proc==Proc::ChkDeps                           ) ; }
	JobExecRpcReply( Proc p , ::vector<pair<Bool3/*ok*/,Crc>>&& is ) : proc{p} ,         dep_infos{::move(is)} { SWEAR( proc==Proc::DepVerbose                        ) ; }
	JobExecRpcReply( Proc p , Bool3 o , ::string const& t          ) : proc{p} , ok{o} , txt{t}                { SWEAR( proc==Proc::Decode || proc==Proc::Encode      ) ; }
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
