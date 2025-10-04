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
,	List                 // list deps/targets
,	Tmp                  // write activity in tmp has been detected (hence clean up is required)
// with file
,	Decode
,	DepDirect
,	DepVerbose
,	Encode
,	Guard
,	Panic                // ensure job is in error
,	Trace                // no algorithmic info, just for tracing purpose
// with file info
,	Access
,	AccessPattern        // pass flags on a regexpr basis
//
// aliases
,	HasFile     = Decode // >=HasFile     means files[*].first  fields are significative
,	HasFileInfo = Access // >=HasFileInfo means files[*].second fields are significative
} ;

struct JobExecRpcReq   ;
struct JobExecRpcReply ;

struct AccessDigest {                                                                    // semantic access order is first read, first write, last write, unlink
	friend ::string& operator+=( ::string& , AccessDigest const& ) ;
	// accesses
	bool has_read () const { return +accesses || read_dir   ; }                          // true if some read access of some sort is done
	bool operator+() const { return has_read() || write!=No ; }                          // true if some      access of some sort is done
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
	// accesses
	::string const& file    () const { SWEAR( proc==Proc::Decode || proc==Proc::Encode , proc ) ; return files[0].first ; } // reuse files to pass specific info
	::string      & file    ()       { SWEAR( proc==Proc::Decode || proc==Proc::Encode , proc ) ; return files[0].first ; } // .
	::string const& ctx     () const { SWEAR( proc==Proc::Decode || proc==Proc::Encode , proc ) ; return files[1].first ; } // .
	::string      & ctx     ()       { SWEAR( proc==Proc::Decode || proc==Proc::Encode , proc ) ; return files[1].first ; } // .
	::string const& code_val() const { SWEAR( proc==Proc::Decode || proc==Proc::Encode , proc ) ; return files[2].first ; } // .
	::string      & code_val()       { SWEAR( proc==Proc::Decode || proc==Proc::Encode , proc ) ; return files[2].first ; } // .
	::string const& code    () const { SWEAR( proc==Proc::Decode                       , proc ) ; return files[2].first ; } // .
	::string      & code    ()       { SWEAR( proc==Proc::Decode                       , proc ) ; return files[2].first ; } // .
	::string const& val     () const { SWEAR(                       proc==Proc::Encode , proc ) ; return files[2].first ; } // .
	::string      & val     ()       { SWEAR(                       proc==Proc::Encode , proc ) ; return files[2].first ; } // .
	::string const& txt     () const { SWEAR( proc==Proc::Panic  || proc==Proc::Trace  , proc ) ; return files[0].first ; } // .
	::string      & txt     ()       { SWEAR( proc==Proc::Panic  || proc==Proc::Trace  , proc ) ; return files[0].first ; } // .
	// services
	void chk() const {
		/**/                                                 SWEAR( (+files)==(proc>=Proc::HasFile) ) ;
		if ( proc>=Proc::HasFile && proc<Proc::HasFileInfo ) SWEAR( ::none_of(files,[](::pair_s<Disk::FileInfo> const& e) { return +e.second ; } ) , proc,files ) ;
		switch (proc) {
			case Proc::None          : SWEAR(              !min_len && !digest            &&  !id                       && !date                    , self ) ; break ;
			case Proc::ChkDeps       :
			case Proc::Tmp           : SWEAR(              !min_len && !digest            &&  !id                       && +date                    , self ) ; break ;
			case Proc::Confirm       : SWEAR(              !min_len && !digest.has_read() && ( id&&digest.write!=Maybe) && !date                    , self ) ; break ;
			case Proc::List          : SWEAR( sync==Yes && !min_len && !digest.has_read() &&  !id                       && +date                    , self ) ; break ;
			case Proc::Decode        : SWEAR( sync==Yes && !min_len && !digest            &&  !id                       && +date && files.size()==3 , self ) ; break ; // files = {file,ctx,code}
			case Proc::DepDirect     :
			case Proc::DepVerbose    : SWEAR( sync==Yes && !min_len &&                        !id                       && +date                    , self ) ; break ;
			case Proc::Encode        : SWEAR( sync==Yes &&             !digest            &&  !id                       && +date && files.size()==3 , self ) ; break ; // files = {file,ctx,val }
			case Proc::Guard         : SWEAR(              !min_len && !digest            &&  !id                       && +date                    , self ) ; break ;
			case Proc::Panic         :
			case Proc::Trace         : SWEAR(              !min_len && !digest            &&  !id                       && +date && files.size()==1 , self ) ; break ; // files = {txt          }
			case Proc::Access        : SWEAR(              !min_len &&                       ( id||digest.write!=Maybe) && +date                    , self ) ; break ;
			case Proc::AccessPattern : SWEAR(              !min_len && !digest.has_read() && (!id&&digest.write!=Maybe) && +date                    , self ) ; break ;
		DF}                                                                                                                                                             // NO_COV
	}
	template<IsStream S> void serdes(S& s) {
		/**/                     ::serdes(s,proc        ) ;
		/**/                     ::serdes(s,sync        ) ;
		/**/                     ::serdes(s,comment     ) ;
		/**/                     ::serdes(s,comment_exts) ;
		if (proc>=Proc::HasFile) ::serdes(s,files       ) ;
		switch (proc) {
			case Proc::ChkDeps       :
			case Proc::Tmp           : ::serdes( s ,                               date ) ; break ;
			case Proc::Confirm       : ::serdes( s ,           digest.write , id        ) ; break ;
			case Proc::List          : ::serdes( s ,           digest.write ,      date ) ; break ;
			case Proc::Decode        : ::serdes( s ,                               date ) ; break ;
			case Proc::DepDirect     :
			case Proc::DepVerbose    : ::serdes( s ,           digest       ,      date ) ; break ;
			case Proc::Encode        : ::serdes( s , min_len ,                     date ) ; break ;
			case Proc::Guard         : ::serdes( s ,                               date ) ; break ;
			case Proc::Panic         :
			case Proc::Trace         : ::serdes( s ,                               date ) ; break ;
			case Proc::Access        : ::serdes( s ,           digest       , id , date ) ; break ;
			case Proc::AccessPattern : ::serdes( s ,           digest       ,      date ) ; break ;
		DF}
	}
	JobExecRpcReply mimic_server() && ;
	// data
	Proc                     proc         = {}            ;
	Bool3                    sync         = No            ; // Maybe means transport as sync (not using fast_report), but not actually sync
	Comment                  comment      = Comment::None ;
	CommentExts              comment_exts = {}            ;
	uint8_t                  min_len      = 0             ;
	AccessDigest             digest       = {}            ;
	Id                       id           = 0             ; // used to distinguish flows from different processes when muxed on fast report fd
	Time::Pdate              date         = {}            ; // access date to reorder accesses during analysis
	::vmap_s<Disk::FileInfo> files        = {}            ;
} ;

struct JobExecRpcReply {
	friend ::string& operator+=( ::string& , JobExecRpcReply const& ) ;
	using Proc = JobExecProc ;
	using Crc  = Hash::Crc   ;
	// accesses
	bool operator+() const { return proc!=Proc::None ; }
	// services
	void chk() const {
		switch (proc) {
			case Proc::None       : SWEAR( ok==Maybe && !verbose_infos && !files && !txt ) ; break ;
			case Proc::ChkDeps    : SWEAR(              !verbose_infos && !files && !txt ) ; break ;
			case Proc::DepDirect  : SWEAR(              !verbose_infos && !files && !txt ) ; break ;
			case Proc::DepVerbose : SWEAR( ok==Maybe                   && !files && !txt ) ; break ;
			case Proc::List       : SWEAR( ok==Maybe && !verbose_infos &&           !txt ) ; break ;
			case Proc::Decode     :
			case Proc::Encode     : SWEAR(              !verbose_infos && !files         ) ; break ;
		DF}                                                                                          // NO_COV
	}
	template<IsStream S> void serdes(S& s) {
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::ChkDeps    : ::serdes(s , ok            ) ; break ;
			case Proc::DepDirect  : ::serdes(s , ok            ) ; break ;
			case Proc::DepVerbose : ::serdes(s , verbose_infos ) ; break ;
			case Proc::List       : ::serdes(s , files         ) ; break ;
			case Proc::Decode     :
			case Proc::Encode     : ::serdes(s , ok , txt      ) ; break ;
		DN}
	}
	// data
	Proc                  proc          = Proc::None ;
	Bool3                 ok            = Maybe      ;                                               // if proc==Decode|Encode|ChkDeps|DepDirect
	::vector<VerboseInfo> verbose_infos = {}         ;                                               // if proc==DepVerbose                      , same order as deps
	::vector_s            files         = {}         ;                                               // if proc==List
	::string              txt           = {}         ;                                               // if proc==Decode|Encode                   , value for Decode, code for Encode
} ;
