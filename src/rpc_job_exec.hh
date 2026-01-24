// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "hash.hh"
#include "serialize.hh"
#include "time.hh"

#include "rpc_job_common.hh"

enum class CodecDir : uint8_t {
	Plain
,	Tmp
,	Lock
} ;

enum class JobExecProc : uint8_t {
	None
,	ChkDeps
,	Confirm
,	List                    // list deps/targets
,	Tmp                     // write activity in tmp has been detected (hence clean up is required)
// with file
,	DepDirect
,	DepVerbose
,	Guard
,	Panic                   // ensure job is in error
,	Trace                   // no algorithmic info, just for tracing purpose
// with file info
,	Access
,	AccessPattern           // pass flags on a regexpr basis
//
// aliases
,	HasFile     = DepDirect // >=HasFile     means files[*].first  fields are significative
,	HasFileInfo = Access    // >=HasFileInfo means files[*].second fields are significative
} ;

struct JobExecRpcReq   ;
struct JobExecRpcReply ;

struct AccessDigest {                                                                        // semantic access order is first read, first write, last write, unlink
	friend ::string& operator+=( ::string& , AccessDigest const& ) ;
	// accesses
	bool has_read () const { return +accesses || read_dir   ; }                              // true if some read access of some sort is done
	bool operator+() const { return has_read() || write!=No ; }                              // true if some      access of some sort is done
	// services
	bool          operator==(AccessDigest const&   ) const = default ;
	AccessDigest& operator|=(AccessDigest const&   )       ;
	AccessDigest  operator| (AccessDigest const& ad) const {                 return ::copy(self)|=ad ; }
	AccessDigest& operator|=(Accesses     const& a )       { accesses |= a ; return        self      ; }
	AccessDigest  operator| (Accesses     const& a ) const {                 return ::copy(self)|=a  ; }
	// data
	Bool3      write        = No                                                           ; // if Maybe => write is not confirmed
	bool       read_dir     = false                                                        ;
	bool       force_is_dep = false                                                        ; // if true => access must be a dep even if written to beforehand
	Accesses   accesses     = {}                                                           ;
	MatchFlags flags        = { .dflags=DflagsDfltDyn , .extra_dflags=ExtraDflagsDfltDyn } ; // kind is unused
} ;

struct JobExecRpcReq {
	friend ::string& operator+=( ::string& , JobExecRpcReq const& ) ;
	// make short lines
	using Proc = JobExecProc ;
	using Id   = uint64_t    ;
	// accesses
	::string const& txt() const { SWEAR( proc==Proc::Panic  || proc==Proc::Trace  , proc ) ; return files[0].first ; }                                     // reuse files to pass specific info
	::string      & txt()       { SWEAR( proc==Proc::Panic  || proc==Proc::Trace  , proc ) ; return files[0].first ; }                                     // .
	// services
	void chk() const {
		/**/                                                 SWEAR( (+files)==(proc>=Proc::HasFile)                                                , proc,files ) ;
		if ( proc>=Proc::HasFile && proc<Proc::HasFileInfo ) SWEAR( ::none_of(files,[](::pair_s<Disk::FileInfo> const& e) { return +e.second ; } ) , proc,files ) ;
		switch (proc) {
			case Proc::None          : SWEAR(              !digest            &&  !id                       && !date                    , self ) ; break ;
			case Proc::ChkDeps       :
			case Proc::Tmp           : SWEAR(              !digest            &&  !id                       && +date                    , self ) ; break ;
			case Proc::Confirm       : SWEAR(              !digest.has_read() && ( id&&digest.write!=Maybe) && !date                    , self ) ; break ;
			case Proc::List          : SWEAR( sync==Yes && !digest.has_read() &&  !id                       && +date                    , self ) ; break ;
			case Proc::DepDirect     :
			case Proc::DepVerbose    : SWEAR( sync==Yes &&                        !id                       && +date                    , self ) ; break ;
			case Proc::Guard         : SWEAR(              !digest            &&  !id                       && +date                    , self ) ; break ;
			case Proc::Panic         :
			case Proc::Trace         : SWEAR(              !digest            &&  !id                       && +date && files.size()==1 , self ) ; break ; // files = {txt}
			case Proc::Access        : SWEAR(                                    ( id||digest.write!=Maybe) && +date                    , self ) ; break ;
			case Proc::AccessPattern : SWEAR(              !digest.has_read() && (!id&&digest.write!=Maybe) && +date                    , self ) ; break ;
		DF}                                                                                                                                                // NO_COV
	}
	template<IsStream S> void serdes(S& s) {
		/**/                     ::serdes(s,proc        ) ;
		/**/                     ::serdes(s,sync        ) ;
		/**/                     ::serdes(s,comment     ) ;
		/**/                     ::serdes(s,comment_exts) ;
		if (proc>=Proc::HasFile) ::serdes(s,files       ) ;
		switch (proc) {
			case Proc::ChkDeps       :
			case Proc::Tmp           : ::serdes( s ,                     date ) ; break ;
			case Proc::Confirm       : ::serdes( s , digest.write , id        ) ; break ;
			case Proc::List          : ::serdes( s , digest.write ,      date ) ; break ;
			case Proc::DepDirect     :
			case Proc::DepVerbose    : ::serdes( s , digest       ,      date ) ; break ;
			case Proc::Guard         : ::serdes( s ,                     date ) ; break ;
			case Proc::Panic         :
			case Proc::Trace         : ::serdes( s ,                     date ) ; break ;
			case Proc::Access        : ::serdes( s , digest       , id , date ) ; break ;
			case Proc::AccessPattern : ::serdes( s , digest       ,      date ) ; break ;
		DF}                                                                                                                                                // NO_COV
	}
	JobExecRpcReply mimic_server() && ;
	// data
	Proc                     proc         = {}            ;
	Bool3                    sync         = No            ; // Maybe means transport as sync (not using fast_report), but not actually sync
	Comment                  comment      = Comment::None ;
	CommentExts              comment_exts = {}            ;
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
			case Proc::None       : SWEAR( ok==Maybe && !verbose_infos && !files ) ; break ;
			case Proc::ChkDeps    : SWEAR(              !verbose_infos && !files ) ; break ;
			case Proc::DepDirect  : SWEAR(              !verbose_infos && !files ) ; break ;
			case Proc::DepVerbose : SWEAR( ok==Maybe &&                   !files ) ; break ;
			case Proc::List       : SWEAR( ok==Maybe && !verbose_infos           ) ; break ;
		DF}                                                                                  // NO_COV
	}
	template<IsStream S> void serdes(S& s) {
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::ChkDeps    : ::serdes(s , ok            ) ; break ;
			case Proc::DepDirect  : ::serdes(s , ok            ) ; break ;
			case Proc::DepVerbose : ::serdes(s , verbose_infos ) ; break ;
			case Proc::List       : ::serdes(s , files         ) ; break ;
		DN}
	}
	// data
	Proc                  proc          = Proc::None ;
	Bool3                 ok            = Maybe      ;                                       // if proc==ChkDeps|DepDirect
	::vector<VerboseInfo> verbose_infos = {}         ;                                       // if proc==DepVerbose       , same order as deps
	::vector_s            files         = {}         ;                                       // if proc==List
} ;

namespace Codec {

	// START_OF_VERSIONING
	static constexpr char Pfx      [] = PRIVATE_ADMIN_DIR_S "codec" ;
	static constexpr char CodecSep    = '*'                         ;
	static constexpr char DecodeSfx[] = ".decode"                   ;
	static constexpr char EncodeSfx[] = ".encode"                   ;
	// END_OF_VERSIONING

	void s_init() ;

	struct CodecFile {
		friend ::string& operator+=( ::string& , CodecFile const& ) ;
		// statics
		static ::string s_pfx_s         (                        CodecDir d=CodecDir::Plain ) { return cat(Pfx,'_',d,'/'              ) ; }
		static ::string s_dir_s         ( ::string const& file , CodecDir d=CodecDir::Plain ) { return cat(s_file (file,d),'/'        ) ; }
		static ::string s_new_codes_file( ::string const& file                              ) { return cat(s_dir_s(file  ),"new_codes") ; }
		static ::string s_file          ( ::string const& file , CodecDir d=CodecDir::Plain ) {
			if (Disk::is_dir_name(file)) { SWEAR(d==CodecDir::Plain) ; return no_slash(file)       ; }
			else                                                       return cat(s_pfx_s(d),file) ;
		}
		static ::string s_lock_file(::string const& file) {
			if (Disk::is_dir_name(file)) return file+"lock"                                     ;
			else                         return s_pfx_s(CodecDir::Lock)+mk_printable<'/'>(file) ;
		}
		static ::string s_config_file(::string const& file) {
			SWEAR(Disk::is_dir_name(file)) ;
			return cat(file,AdminDirS,"config.py") ;
		}
		//
		static bool s_is_codec(::string const& node) { return +node && ( !Disk::is_lcl(node) || node.starts_with(s_pfx_s()) ) ; }
		// cxtors & casts
		CodecFile(               ::string const& f , ::string const& x , Hash::Crc       val_crc  ) : file{       f } , ctx{       x } , _code_val_crc{val_crc} {}
		CodecFile(               ::string     && f , ::string     && x , Hash::Crc       val_crc  ) : file{::move(f)} , ctx{::move(x)} , _code_val_crc{val_crc} {}
		CodecFile( bool encode , ::string const& f , ::string const& x , ::string const& code_val ) : file{       f } , ctx{       x } {
			if (encode) _code_val_crc = Hash::Crc(New,code_val) ;
			else        _code_val_crc =               code_val  ;
		}
		CodecFile( bool encode , ::string&& f , ::string&& x , ::string&& code_val ) : file{::move(f)} , ctx{::move(x)} {
			if (encode) _code_val_crc = Hash::Crc(New,code_val) ;
			else        _code_val_crc = ::move   (    code_val) ;
		}
		CodecFile(::string const& node) ;
		// accesses
		bool             is_encode() const { return        _code_val_crc.index()==1 ; }
		::string  const& code     () const { return get<0>(_code_val_crc)           ; }
		Hash::Crc const& val_crc  () const { return get<1>(_code_val_crc)           ; }
		// services
		::string name(bool tmp=false) const ;
		// data
		::string file ;
		::string ctx  ;
	private :
		::variant<::string/*decode*/,Hash::Crc/*encode*/> _code_val_crc ;
	} ;

	struct Entry {
		friend ::string& operator+=( ::string& , Entry const& ) ;
		// cxtors & casts
		Entry() = default ;
		Entry( ::string const& x , ::string const& c , ::string const& v ) : ctx{x} , code{c} , val{v} {}
		Entry( ::string const& line                                      ) ;                              // format : "\t<code>\t<ctx>\t<val>" exactly
		// services
		::string line(bool with_nl=false) const ;                                                         // .
		// data
		::string ctx  ;
		::string code ;
		::string val  ;
	} ;

	// this lock ensures correct operation even in case of crash
	// principle is that new_codes_file is updated before creating actual files and last action is replayed if it was interupted
	struct _LockAction {
		bool     err_ok    = false          ;
		FileSync file_sync = FileSync::Dflt ;
	} ;
	struct CodecGuardLock : NfsGuardLock {
		using Action = _LockAction ;
		// ctxors & casts
		CodecGuardLock ( FileRef , Action={} ) ;
		~CodecGuardLock(                     ) ;
		// data
		File file ;
	} ;

}
