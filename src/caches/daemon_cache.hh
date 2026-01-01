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
			// service
			bool operator+() const { return +proc ; }
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
			Proc                proc        = Proc::None ;
			::string            repo_key    = {}         ;                                           // if proc =                     Config
			::string            job         = {}         ;                                           // if proc = Download |          Commit
			::vmap_s<DepDigest> repo_deps   = {}         ;                                           // if proc = Download |          Commit
			Disk::DiskSz        reserved_sz = 0          ;                                           // if proc =            Upload
			JobInfo             job_info    = {}         ;                                           // if proc =                     Commit
			uint64_t            upload_key  = 0          ;                                           // if proc =                     Commit | Dismiss
		} ;

		struct RpcReply {
			friend ::string& operator+=( ::string& , RpcReply const& ) ;
			// service
			bool operator+() const { return +proc ; }
			template<IsStream S> void serdes(S& s) {
				::serdes( s , proc ) ;
				switch (proc) {
					case Proc::None     :                                    break ;
					case Proc::Config   : ::serdes( s , config    ,gen   ) ; break ;
					case Proc::Download : ::serdes( s , hit_info  ,dir_s ) ; break ;
					case Proc::Upload   : ::serdes( s , upload_key,msg   ) ; break ;
				DF}                                                                  // NO_COV
			}
			// data
			Proc         proc       = Proc::None ;
			CacheHitInfo hit_info   = {}         ;                                   // if proc=Download
			::string     dir_s      = {}         ;                                   // if proc=Download, dir in which data and info files lie
			uint64_t     upload_key = 0          ;                                   // if proc=Upload
			::string     msg        = {}         ;                                   // if proc=Upload and upload_key=0
			Config       config     = {}         ;                                   // if proc==Config
			uint64_t     gen        = {}         ;                                   // if proc==Config
		} ;

		static constexpr uint64_t Magic = 0x604178e6d1838dce ;                                             // any random improbable value!=0 used as a sanity check when client connect to server
		// statics
		static ::string s_reserved_file(uint64_t upload_key) {
			return cat(AdminDirS,"reserved/",upload_key) ;
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
