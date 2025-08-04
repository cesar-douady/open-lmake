// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "serialize.hh"
#include "time.hh"

#include "rpc_job_common.hh"

enum class JobExecProc : uint8_t {
	None
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
,	AccessPattern          // pass flags on a regexpr basis
,	DepVerbosePush
,	Encode                 // file_info is used to transport min_len
//
// aliases
,	HasFile     = CodecCtx // >=HasFile     means file      field is significative
,	HasFileInfo = Access   // >=HasFileInfo means file_info field is significative
} ;

struct AccessDigest {                                                                    // semantic access order is first read, first write, last write, unlink
	friend ::string& operator+=( ::string& , AccessDigest const& ) ;
	// accesses
	bool has_read () const { return +accesses || read_dir   ; }                          // true if some access of some sort is done
	bool operator+() const { return has_read() || write!=No ; }                          // true if some access of some sort is done
	// services
	bool          operator==(AccessDigest const&   ) const = default ;
	AccessDigest& operator|=(AccessDigest const&   ) ;
	AccessDigest  operator| (AccessDigest const& ad) const {                 return ::copy(self)|=ad ; }
	AccessDigest& operator|=(Accesses     const& a )       { accesses |= a ; return        self      ; }
	AccessDigest  operator| (Accesses     const& a ) const {                 return ::copy(self)|=a  ; }
	// data
	Bool3      write    = No                                                           ; // if Maybe, write is not confirmed
	bool       read_dir = false                                                        ;
	Accesses   accesses = {}                                                           ;
	MatchFlags flags    = { .dflags=DflagsDfltDyn , .extra_dflags=ExtraDflagsDfltDyn } ; // kind is unused
} ;

struct JobExecRpcReq {
	friend ::string& operator+=( ::string& , JobExecRpcReq const& ) ;
	// make short lines
	using Proc = JobExecProc ;
	using Id   = uint64_t    ;
	//
	static const size_t MaxSz ;
	// accesses
	uint8_t const& min_len() const { SWEAR(proc==Proc::Encode) ; return *::launder(reinterpret_cast<uint8_t const*>(&file_info)) ; }
	uint8_t      & min_len()       { SWEAR(proc==Proc::Encode) ; return *::launder(reinterpret_cast<uint8_t      *>(&file_info)) ; }
	// services
	void chk() const {
		SWEAR( (proc>=Proc::HasFile    ) == +file      , proc,file           ) ;
		SWEAR( (proc< Proc::HasFileInfo) <= !file_info , proc,file,file_info ) ;                                                           // Encode uses file_info to store min_len
		switch (proc) {
			case Proc::ChkDeps        :
			case Proc::Tmp            : SWEAR(                !digest            &&  !id                       && +date , self ) ; break ;
			case Proc::DepVerbose     : SWEAR( sync==Yes   &&                        !id                       && +date , self ) ; break ;
			case Proc::Trace          :
			case Proc::Panic          : SWEAR( sync==No    && !digest            &&  !id                       && !date , self ) ; break ;
			case Proc::CodecFile      :
			case Proc::CodecCtx       :
			case Proc::DepVerbosePush : SWEAR( sync==Maybe && !digest            &&  !id                       && !date , self ) ; break ;
			case Proc::Confirm        : SWEAR(                !digest.has_read() && ( id&&digest.write!=Maybe) && !date , self ) ; break ;
			case Proc::Guard          : SWEAR(                !digest            &&  !id                       && !date , self ) ; break ;
			case Proc::Decode         :
			case Proc::Encode         : SWEAR( sync==Yes   && !digest            &&  !id                       && !date , self ) ; break ;
			case Proc::Access         : SWEAR(                                      ( id||digest.write!=Maybe) && +date , self ) ; break ;
			case Proc::AccessPattern  : SWEAR( sync!=Yes   && !digest.has_read() && (!id&&digest.write!=Maybe) && +date , self ) ; break ;
		DF}                                                                                                                                // NO_COV
	}
	template<IsStream T> void serdes(T& s) {
		/**/                         ::serdes(s,proc        ) ;
		/**/                         ::serdes(s,sync        ) ;
		/**/                         ::serdes(s,comment     ) ;
		/**/                         ::serdes(s,comment_exts) ;
		if (proc>=Proc::HasFile    ) ::serdes(s,file        ) ;
		if (proc>=Proc::HasFileInfo) ::serdes(s,file_info   ) ;
		switch (proc) {
			case Proc::ChkDeps       :
			case Proc::Tmp           :                                             ::serdes(s,date) ; break ;
			case Proc::Confirm       : ::serdes(s,digest.write) ; ::serdes(s,id) ;                    break ;
			case Proc::DepVerbose    : ::serdes(s,digest      ) ;                  ::serdes(s,date) ; break ;
			case Proc::Access        : ::serdes(s,digest      ) ; ::serdes(s,id) ; ::serdes(s,date) ; break ;
			case Proc::AccessPattern : ::serdes(s,digest      ) ;                  ::serdes(s,date) ; break ;
		DN}
	}
	// data
	Proc           proc         = {}            ;
	Bool3          sync         = No            ;                        // Maybe means transport as sync (not using fast_report), but not actually sync
	Comment        comment      = Comment::None ;
	CommentExts    comment_exts = {}            ;
	AccessDigest   digest       = {}            ;
	Id             id           = 0             ;                        // used to distinguish flows from different processes when muxed on fast report fd
	Time::Pdate    date         = {}            ;                        // access date to reorder accesses during analysis
	::string       file         = {}            ;                        // contains all text info for CodecCtx, Encode, Decode, Trace and Panic
	Disk::FileInfo file_info    = {}            ;
} ;
constexpr size_t JobExecRpcReq::MaxSz = PATH_MAX+sizeof(JobExecRpcReq) ; // maximum size of a message : a file + overhead

struct JobExecRpcReply {
	friend ::string& operator+=( ::string& , JobExecRpcReply const& ) ;
	using Proc = JobExecProc ;
	using Crc  = Hash::Crc   ;
	// accesses
	bool operator+() const { return proc!=Proc::None ; }
	// services
	void chk() const {
		switch (proc) {
			case Proc::None       : SWEAR( ok==Maybe && !dep_infos && !txt ) ; break ;
			case Proc::ChkDeps    : SWEAR(              !dep_infos && !txt ) ; break ;
			case Proc::DepVerbose : SWEAR( ok==Maybe               && !txt ) ; break ;
			case Proc::Decode     :
			case Proc::Encode     : SWEAR(              !dep_infos         ) ; break ;
		DF}                                                                            // NO_COV
	}
	template<IsStream S> void serdes(S& s) {
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::ChkDeps    : ::serdes(s,ok       ) ;            break ;
			case Proc::DepVerbose : ::serdes(s,dep_infos) ;            break ;
			case Proc::Decode     :
			case Proc::Encode     : ::serdes(s,ok) ; ::serdes(s,txt) ; break ;
		DN}
	}
	// data
	Proc                     proc      = Proc::None ;
	Bool3                    ok        = Maybe      ;                                  // if proc==ChkDeps|Decode|Encode
	::vector<DepVerboseInfo> dep_infos = {}         ;                                  // if proc==DepVerbose            , same order as deps
	::string                 txt       = {}         ;                                  // if proc==        Decode|Encode , value for Decode, code for Encode
} ;
