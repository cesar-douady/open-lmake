// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "hash.hh"
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
,	Chroot               // forbidden chroot
,	DepDirect
,	DepVerbose
,	Guard
,	Panic                // ensure job is in error
,	Trace                // no algorithmic info, just for tracing purpose
// with file info
,	Access
,	AccessPattern        // pass flags on a regexpr basis
,	Mount                // forbidden mount
//
// aliases
,	HasFile     = Chroot // >=HasFile     means files[*].first  fields are significative
,	HasFileInfo = Access // >=HasFileInfo means files[*].second fields are significative
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
			case Proc::Chroot        :
			case Proc::Mount         : SWEAR(              !digest            &&  !id                       && !date && files.size()==1 , self ) ; break ;
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
			case Proc::Chroot        :
			case Proc::Mount         :                                            break ;
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

	// START_OF_VERSIONING CACHE JOB REPO CODEC
	using CodecCrc = Hash::Crc96 ;                                                                              // 64 bits is enough, but not easy to prove
	static constexpr char CodecSep    = '*'       ; //!                                                    null
	static constexpr char DecodeSfx[] = ".decode" ; static constexpr size_t DecodeSfxSz = sizeof(DecodeSfx)-1 ;
	static constexpr char EncodeSfx[] = ".encode" ; static constexpr size_t EncodeSfxSz = sizeof(EncodeSfx)-1 ;
	// END_OF_VERSIONING

	void creat_store( FileRef dir_s , ::string const& crc_str , ::string const& val , mode_t umask , NfsGuard* ) ; // ensure data exist in store

	struct CodecFile {
		friend ::string& operator+=( ::string& , CodecFile const& ) ;
		// statics
		static ::string s_pfx_s(bool tmp=false) {
			if (tmp) return cat(PrivateAdminDirS,"codec_tmp/") ;
			else     return cat(PrivateAdminDirS,"codec/"    ) ;
		}
		static ::string s_dir_s( ::string const& tab , bool tmp=false ) {
			if (Disk::is_dir_name(tab)) { SWEAR( !tmp , tab ) ; return                  tab            ; }
			else                                                return cat(s_pfx_s(tmp),tab,add_slash) ;
		}
		static ::string s_config_file(::string const& tab) {
			SWEAR(Disk::is_dir_name(tab)) ;
			return cat(tab,AdminDirS,"config.py") ;
		}
		// cxtors & casts
		CodecFile() = default ;
		CodecFile(               ::string const& f , ::string const& x , CodecCrc        val_crc  ) : file{       f } , ctx{       x } , _code_val_crc{val_crc} {}
		CodecFile(               ::string     && f , ::string     && x , CodecCrc        val_crc  ) : file{::move(f)} , ctx{::move(x)} , _code_val_crc{val_crc} {}
		CodecFile( bool encode , ::string const& f , ::string const& x , ::string const& code_val ) : file{       f } , ctx{       x } {
			if (encode) _code_val_crc = CodecCrc(New,code_val) ;
			else        _code_val_crc =              code_val  ;
		}
		CodecFile( bool encode , ::string&& f , ::string&& x , ::string&& code_val ) : file{::move(f)} , ctx{::move(x)} {
			if (encode) _code_val_crc = CodecCrc(New,code_val) ;
			else        _code_val_crc = ::move  (    code_val) ;
		}
		CodecFile( NewType , ::string const& node                                   ) ; // for local    file codec
		CodecFile( NewType , ::string const& node , ::string const& ext_codec_dir_s ) ; // for external dir  codec
		// acceses
		bool            is_encode() const { return        _code_val_crc.index()==1 ; }
		::string const& code     () const { return get<0>(_code_val_crc)           ; }
		::string      & code     ()       { return get<0>(_code_val_crc)           ; }
		CodecCrc const& val_crc  () const { return get<1>(_code_val_crc)           ; }
		CodecCrc      & val_crc  ()       { return get<1>(_code_val_crc)           ; }
		// services
		::string ctx_dir_s(bool tmp=false) const ;
		::string name     (bool tmp=false) const ;
		void chk() const ;
		// data
		::string file ;
		::string ctx  ;
	private :
		::variant<::string/*decode*/,CodecCrc/*encode*/> _code_val_crc ;
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

	struct CodecLock {
	private :
		static constexpr uint8_t     _NId           = 16   ;
		static constexpr uint8_t     _Excl          = -1   ;
		static constexpr Time::Delay _SharedTimeout { 10 } ; // shared locks older than that are deemed lost
		// statics
	public :
		static void s_init() ;
		// cxtors & casts
		CodecLock( Fd root_fd , ::string const& tab={} ) : _root_fd{root_fd} , _tab{New,tab} {}
		CodecLock(              ::string const& tab={} ) : CodecLock{Fd::Cwd,tab}            {}
		~CodecLock() ;
		// accesses
		CodecLock& operator=(CodecLock const&)       = default ;
		CodecLock& operator=(CodecLock     &&)       = default ;
		bool       operator+(                ) const { return +_root_fd ; }
		bool       locked   (                ) const { return _num      ; }
		// services
		void lock_shared(::string const& id={}) ;            // id is for debug purpose only
		void lock_excl  (                     ) ;            // .
		// data
	private :
		Fd          _root_fd ;
		Hash::Crc   _tab     ;
		uint8_t     _num     = 0 ;                           // 0 means unlocked, -1 means exclusive, 1 to _NId means shared
	} ;

}
