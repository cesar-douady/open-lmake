// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "msg.hh"

#include "rpc_job.hh"

enum class DaemonCacheRpcProc : uint8_t {
	None
,	Download
,	Upload
,	Commit
,	Dismiss
} ;

namespace Caches {

	struct DaemonCacheRpcReq {
		friend ::string& operator+=( ::string& , DaemonCacheRpcReq const& ) ;
		using Proc = DaemonCacheRpcProc ;
		// service
		bool operator+() const { return +proc ; }
		template<IsStream S> void serdes(S& s) {
			::serdes( s , proc ) ;
			switch (proc) {
				case Proc::Download : ::serdes( s ,          job,repo_deps                             ) ; break ;
				case Proc::Upload   : ::serdes( s ,                        reserved_sz                 ) ; break ;
				case Proc::Commit   : ::serdes( s , repo_key,job,                      info,upload_key ) ; break ;
				case Proc::Dismiss  : ::serdes( s ,                                         upload_key ) ; break ;
			DN}
		}
		// data
		Proc                proc        = Proc::None ;
		Hash::Crc           repo_key    = {}         ;
		::string            job         = {}         ; // if proc = Download |          Commit
		::vmap_s<DepDigest> repo_deps   = {}         ; // if proc = Download |          Commit
		Disk::DiskSz        reserved_sz = 0          ; // if proc =            Upload
		JobInfo             info        = {}         ; // if proc =                     Commit
		uint64_t            upload_key  = 0          ; // if proc =                     Commit | Dismiss
	} ;

	struct DaemonCacheRpcReply {
		friend ::string& operator+=( ::string& , DaemonCacheRpcReply const& ) ;
		using Proc = DaemonCacheRpcProc ;
		// service
		bool operator+() const { return +proc ; }
		template<IsStream S> void serdes(S& s) {
			::serdes( s , proc ) ;
			switch (proc) {
				case Proc::Download : ::serdes( s , file_sync,hit_info,dir_s ) ; break ;
				case Proc::Upload   : ::serdes( s , perm_ext,upload_key      ) ; break ;
			DF}
		}
		// data
		Proc         proc       = Proc::None ;
		PermExt      perm_ext   = {}         ; // if proc=Upload
		FileSync     file_sync  = {}         ; // if proc=Download
		CacheHitInfo hit_info   = {}         ; // if proc=Download
		::string     dir_s      = {}         ; // if proc=Download, dir in which data and info files lie
		uint64_t     upload_key = 0          ; // if proc=Upload
	} ;

	struct DaemonCache : Cache {                               // PER_CACHE : inherit from Cache and provide implementation
		using Proc     = DaemonCacheRpcProc  ;
		using RpcReq   = DaemonCacheRpcReq   ;
		using RpcReply = DaemonCacheRpcReply ;
		static constexpr uint64_t Magic = 0x604178e6d1838dce ; // any random improbable value!=0 used as a sanity check when client connect to server
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
		SubUploadDigest             sub_upload  ( Sz max_sz                                                 ) override ;
		void                        sub_commit  ( uint64_t upload_key , ::string const& /*job*/ , JobInfo&& ) override ;
		void                        sub_dismiss ( uint64_t upload_key                                       ) override ;
		//
		void chk(ssize_t delta_sz=0) const ;
	private :
		// START_OF_VERSIONING REPO
		template<IsStream S> void _serdes(S& s) {
			::serdes( s , dir_s    ) ;
			::serdes( s , repo_key ) ;
			::serdes( s , service  ) ;
		}
		// END_OF_VERSIONING
		// data
	public :
		::string     dir_s    ;
		Hash::Crc    repo_key = Hash::Crc::None ;
		KeyedService service  ;
	private :
		ClientSockFd _fd     ;
		IMsgBuf      _imsg   ;
		AcFd         _dir_fd ;
	} ;

}
