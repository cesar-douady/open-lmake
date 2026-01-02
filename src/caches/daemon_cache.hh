// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "msg.hh"
#include "time.hh"

#include "rpc_job.hh"

enum class DaemonCacheRpcProc : uint8_t {
	None
,	Config
,	Download
,	Upload
,	Commit
,	Dismiss
} ;

namespace Caches {

	// START_OF_VERSIONING DAEMON_CACHE

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

	template<class I> struct StrId {
		StrId() = default ;
		StrId(::string const& n) : name{n} {}
		StrId(I               i) : id  {i} {}
		// accesses
		bool operator+() const { return +name || +id ; }
		bool is_name  () const { return +name        ; }
		bool is_id    () const { return          +id ; }
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
		else if (si.is_id  ()) return os << si.id   ;
		else                   return os << "()"    ;
	}

	struct DaemonCache : Cache {          // PER_CACHE : inherit from Cache and provide implementation
		using Proc = DaemonCacheRpcProc ;

		struct Config {
			friend ::string& operator+=( ::string& , Config const& ) ;
			// statics
			static ::string s_store_dir_s(bool for_bck=false) ;
			// cxtors & casts
			Config() = default ;
			Config(NewType) ;
			// services
			template<IsStream S> void serdes(S& s) {
				::serdes( s , file_sync,perm_ext,max_rate,max_sz ) ;
			}
			// data
			FileSync     file_sync        = {}    ;
			PermExt      perm_ext         = {}    ;
			Disk::DiskSz max_rate         = 1<<30 ; // in B/s, maximum rate (total_sz/exe_time) above which run is not cached
			Disk::DiskSz max_sz           = 0     ;
			uint16_t     max_runs_per_job = 100   ;
		} ;

		struct RpcReq {
			friend ::string& operator+=( ::string& , RpcReq const& ) ;
			// accesses
			bool operator+() const { return +proc ; }
			// service
			template<IsStream S> void serdes(S& s) {
				::serdes( s , proc ) ;
				switch (proc) {
					case Proc::None     :
					case Proc::Config   : ::serdes( s , repo_key                         ) ; break ;
					case Proc::Download : ::serdes( s , job        ,repo_deps            ) ; break ;
					case Proc::Upload   : ::serdes( s , reserved_sz                      ) ; break ;
					case Proc::Commit   : ::serdes( s , job        ,job_info ,upload_key ) ; break ;
					case Proc::Dismiss  : ::serdes( s ,                       upload_key ) ; break ;
				DF}                                                                                  // NO_COV
			}
			// data
			Proc                              proc        = Proc::None ;
			::string                          repo_key    = {}         ;                             // if proc = Config
			StrId<CjobIdx>                    job         = {}         ;                             // if proc = Download | Commit
			::vmap<StrId<CnodeIdx>,DepDigest> repo_deps   = {}         ;                             // if proc = Download | Commit
			Disk::DiskSz                      reserved_sz = 0          ;                             // if proc =            Upload
			JobInfo                           job_info    = {}         ;                             // if proc =            Commit
			uint64_t                          upload_key  = 0          ;                             // if proc =            Commit | Dismiss
		} ;

		struct RpcReply {
			friend ::string& operator+=( ::string& , RpcReply const& ) ;
			// accesses
			bool operator+() const { return +proc ; }
			// service
			template<IsStream S> void serdes(S& s) {
				::serdes( s , proc ) ;
				switch (proc) {
					case Proc::None     :                                                      break ;
					case Proc::Config   : ::serdes( s , config    ,gen                     ) ; break ;
					case Proc::Download : ::serdes( s , hit_info  ,key,key_is_last,dep_ids ) ; break ;
					case Proc::Upload   : ::serdes( s , upload_key,msg                     ) ; break ;
				DF}                                                                                    // NO_COV
			}
			// data
			Proc               proc        = Proc::None ;
			Config             config      = {}         ;                                              // if proc = Config
			uint64_t           gen         = {}         ;                                              // if proc = Config
			CacheHitInfo       hit_info    = {}         ;                                              // if proc = Download
			CkeyIdx            key         = 0          ;                                              // if proc = Download
			bool               key_is_last = false      ;                                              // if proc = Download
			::vector<CnodeIdx> dep_ids     = {}         ;                                              // if proc = Download, idx of correspding deps in Req that were passed by name
			uint64_t           upload_key  = 0          ;                                              // if proc = Upload
			::string           msg         = {}         ;                                              // if proc = Upload and upload_key=0
		} ;

		static constexpr uint64_t Magic = 0x604178e6d1838dce ;                                             // any random improbable value!=0 used as a sanity check when client connect to server
		// statics
		static ::string s_reserved_file(uint64_t upload_key) {
			return cat(AdminDirS,"reserved/",upload_key) ;
		}
		static ::string s_run_dir( ::string const& job , CkeyIdx key , bool key_is_last ) {
			::string         res =  job            ;
			/**/             res << '/'<<+key<<'-' ;
			if (key_is_last) res << "last"         ;
			else             res << "first"        ;
			return res ;
		}
		// services
		void      config( ::vmap_ss const& , bool may_init=false )       override ;
		::vmap_ss descr (                                        ) const override ;
		void      repair( bool dry_run                           )       override ;
		Tag       tag   (                                        )       override { return Tag::Daemon ; }
		void      serdes( ::string     & os                      )       override { _serdes(os) ;        } // serialize  , cannot be a template as it is a virtual method
		void      serdes( ::string_view& is                      )       override { _serdes(is) ;        } // deserialize, .
		//
		::pair<DownloadDigest,AcFd> sub_download( ::string const& job , MDD const&                          ) override ;
		SubUploadDigest             sub_upload  ( Time::Delay exe_time , Sz max_sz                          ) override ;
		void                        sub_commit  ( uint64_t upload_key , ::string const& /*job*/ , JobInfo&& ) override ;
		void                        sub_dismiss ( uint64_t upload_key                                       ) override ;
		//
		void chk(ssize_t delta_sz=0) const ;
	private :
		// START_OF_VERSIONING REPO
		template<IsStream S> void _serdes(S& s) {
			::serdes( s , dir_s,repo_key,service ) ;
			::serdes( s , config_                ) ;
		}
		// END_OF_VERSIONING
		// data
	public :
		::string     dir_s    ;
		::string     repo_key ;
		KeyedService service  ;
		Config       config_  ;
	private :
		ClientSockFd _fd     ;
		IMsgBuf      _imsg   ;
		AcFd         _dir_fd ;
	} ;

	inline ::string& operator+=( ::string& os , DaemonCache::Config const& dcc ) {
		/**/                os <<"DaemonCache::Config("              ;
		if (+dcc.file_sync) os <<dcc.file_sync<<','                  ;
		if (+dcc.perm_ext ) os <<dcc.perm_ext <<','                  ;
		return              os <<dcc.max_rate <<','<<dcc.max_sz<<')' ;
	}

}
