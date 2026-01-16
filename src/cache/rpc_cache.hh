// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "msg.hh"
#include "time.hh"

#include "rpc_job.hh"

enum class CacheRpcProc : uint8_t {
	None
,	Config
,	Download
,	Upload
,	Commit
,	Dismiss
} ;

namespace Cache {

	static constexpr Channel  CacheChnl  = Channel::Cache     ;
	static constexpr uint64_t CacheMagic = 0x604178e6d1838dce ; // any random improbable value!=0 used as a sanity check when client connect to server
		// statics

	// START_OF_VERSIONING CACHE

	// used for cache efficiency
	// rate=0 means max_rate as per config
	// +1 means job took 13.3% more time per byte of generated data
	using Rate = uint8_t ;

	// can be tailored to fit needs
	static constexpr uint8_t NCkeyIdxBits      = 32 ;
	static constexpr uint8_t NCjobNameIdxBits  = 32 ;
	static constexpr uint8_t NCnodeNameIdxBits = 32 ;
	static constexpr uint8_t NCjobIdxBits      = 32 ;
	static constexpr uint8_t NCrunIdxBits      = 32 ;
	static constexpr uint8_t NCnodeIdxBits     = 32 ;
	static constexpr uint8_t NCnodesIdxBits    = 32 ;
	static constexpr uint8_t NCcrcsIdxBits     = 32 ;

	// END_OF_VERSIONING

	// rest cannot be tailored

	static constexpr Rate NRates = Max<Rate> ; // missing highest value, but easier to code

	using CkeyIdx      = Uint<NCkeyIdxBits     > ;
	using CjobNameIdx  = Uint<NCjobNameIdxBits > ;
	using CnodeNameIdx = Uint<NCnodeNameIdxBits> ;
	using CjobIdx      = Uint<NCjobIdxBits     > ;
	using CrunIdx      = Uint<NCrunIdxBits     > ;
	using CnodeIdx     = Uint<NCnodeIdxBits    > ;
	using CnodesIdx    = Uint<NCnodesIdxBits   > ;
	using CcrcsIdx     = Uint<NCcrcsIdxBits    > ;

	inline ::string reserved_file(CacheUploadKey upload_key) {
		return cat(AdminDirS,"reserved/",upload_key) ;
	}

	inline ::string run_dir( ::string const& job , CkeyIdx key , bool key_is_last ) {
		::string         res =  job            ;
		/**/             res << '/'<<+key<<'-' ;
		if (key_is_last) res << "last"         ;
		else             res << "first"        ;
		return res ;
	}

	template<class I> struct StrId {
		StrId() = default ;
		StrId(::string const& n) : name{n} {}
		StrId(I               i) : id  {i} {}
		// accesses
		bool operator+() const { return +name || +id ; }
		bool is_name  () const { return +name        ; }
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , name,id ) ;
		}
		// data
		::string name ;
		I        id   = {} ;
	} ;
	template<class I> ::string& operator+=( ::string& os , StrId<I> const& si ) {
		if      (si.is_name()) return os << si.name ;
		else if (si.id       ) return os << si.id   ;
		else                   return os << "()"    ;
	}

	struct CacheConfig {
		friend ::string& operator+=( ::string& , CacheConfig const& ) ;
		// data
		// START_OF_VERSIONING CACHE
		Disk::DiskSz max_sz           = 0     ;
		Disk::DiskSz max_rate         = 1<<30 ; // in B/s, maximum rate (total_sz/exe_time) above which run is not cached
		uint16_t     max_runs_per_job = 100   ;
		FileSync     file_sync        = {}    ;
		PermExt      perm_ext         = {}    ;
		// END_OF_VERSIONING
	} ;

	struct CacheRpcReq {
		friend ::string& operator+=( ::string& , CacheRpcReq const& ) ;
		// accesses
		bool operator+() const { return +proc ; }
		// service
		template<IsStream S> void serdes(S& s) {
			::serdes( s , proc ) ;
			switch (proc) {
				case CacheRpcProc::None     :                                                                                                         break ;
				case CacheRpcProc::Config   : ::serdes( s , repo_key                                                                              ) ; break ;
				case CacheRpcProc::Download : ::serdes( s ,          job,repo_deps                                                                ) ; break ;
				case CacheRpcProc::Upload   : ::serdes( s ,                        conn_id,reserved_sz                                            ) ; break ;
				case CacheRpcProc::Commit   : ::serdes( s ,          job,repo_deps,                    total_z_sz,job_info_sz,exe_time,upload_key ) ; break ;
				case CacheRpcProc::Dismiss  : ::serdes( s ,                        conn_id,                                            upload_key ) ; break ;
			DF}                                                                                                                                               // NO_COV
		}
		// data
		// START_OF_VERSIONING CACHE
		CacheRpcProc                      proc        = {} ;
		::string                          repo_key    = {} ; // if proc = Config
		StrId<CjobIdx>                    job         = {} ; // if proc = Download | Commit
		::vmap<StrId<CnodeIdx>,DepDigest> repo_deps   = {} ; // if proc = Download | Commit
		uint32_t                          conn_id     = {} ; // if proc =            Upload | Dismiss (when from job_exec)
		Disk::DiskSz                      reserved_sz = 0  ; // if proc =            Upload
		Disk::DiskSz                      total_z_sz  = 0  ; // if proc =            Commit
		Disk::DiskSz                      job_info_sz = 0  ; // if proc =            Commit
		Time::CoarseDelay                 exe_time    = {} ; // if proc =            Commit
		CacheUploadKey                    upload_key  = 0  ; // if proc =            Commit | Dismiss
		// END_OF_VERSIONING
	} ;

	struct CacheRpcReply {
		friend ::string& operator+=( ::string& , CacheRpcReply const& ) ;
		// accesses
		bool operator+() const { return +proc ; }
		// service
		template<IsStream S> void serdes(S& s) {
			::serdes( s , proc ) ;
			switch (proc) {
				case CacheRpcProc::None     :                                                           break ;
				case CacheRpcProc::Config   : ::serdes( s , config,conn_id                          ) ; break ;
				case CacheRpcProc::Download : ::serdes( s , hit_info,key,key_is_last,job_id,dep_ids ) ; break ;
				case CacheRpcProc::Upload   : ::serdes( s , upload_key,msg                          ) ; break ;
			DF}                                                                                                 // NO_COV
		}
		// data
		// START_OF_VERSIONING CACHE
		CacheRpcProc       proc        = {}    ;
		CacheConfig        config      = {}    ;                                                                // if proc = Config
		uint32_t           conn_id     = 0     ;                                                                // if proc = Config  , id to be repeated by upload requests
		CacheHitInfo       hit_info    = {}    ;                                                                // if proc = Download
		CkeyIdx            key         = 0     ;                                                                // if proc = Download
		bool               key_is_last = false ;                                                                // if proc = Download
		::vector<CnodeIdx> dep_ids     = {}    ;                                                                // if proc = Download, idx of corresponding deps in Req that were passed by name
		CjobIdx            job_id      = 0     ;                                                                // if proc = Download, idx of corresponding job  in Req if passed by name
		CacheUploadKey     upload_key  = 0     ;                                                                // if proc = Upload
		::string           msg         = {}    ;                                                                // if proc = Upload and upload_key=0
		// END_OF_VERSIONING
	} ;

}
