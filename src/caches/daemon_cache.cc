// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"
#include "process.hh"

#include "app.hh"

// cache format :
#include "daemon_cache.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

namespace Caches {

	::string& operator+=( ::string& os , DaemonCache::RpcReq const& dcrr ) {
		/**/                   os << "DaemonCache::RpcReq("<<dcrr.proc ;
		if (+dcrr.job        ) os << ','  <<dcrr.job                 ;
		if (+dcrr.repo_deps  ) os << ",D:"<<dcrr.repo_deps.size()    ;
		if (+dcrr.reserved_sz) os << ",S:"<<dcrr.reserved_sz         ;
		if (+dcrr.upload_key ) os << ",K:"<<dcrr.upload_key          ;
		return                 os <<')'                              ;
	}

	::string& operator+=( ::string& os , DaemonCache::RpcReply const& dcrr ) {
		/**/                   os << "DaemonCache::RpcReply("<<dcrr.proc ;
		if (+dcrr.hit_info   ) os << ','<<dcrr.hit_info                ;
		if (+dcrr.dir_s      ) os << ','<<dcrr.dir_s                   ;
		if (+dcrr.upload_key ) os << ','<<dcrr.upload_key              ;
		return                 os <<')'                                ;
	}

	::vmap_ss DaemonCache::descr() const {
		return {
			{ "dir_s"     ,                     dir_s                   }
		,	{ "file_sync" , snake_str          (config_.file_sync)      }
		,	{ "max_rate"  , to_string_with_unit(config_.max_rate )      }
		,	{ "perm_ext"  , snake_str          (config_.perm_ext )      }
		,	{ "repo_key"  ,                     repo_key         .hex() }
		,	{ "service"   ,                     service          .str() }
		} ;
	}

	void DaemonCache::chk(ssize_t /*delta_sz*/) const {}

	#if CACHE_LIGHT
		void DaemonCache::config( ::vmap_ss const& /*dct*/ , bool /*may_init*/ ) { FAIL() ; }
	#else
		void DaemonCache::config( ::vmap_ss const& dct , bool may_init ) {
			Trace trace(CacheChnl,"DaemonCache::config",dct.size(),STR(may_init)) ;
			for( auto const& [key,val] : dct ) {
				try {
					switch (key[0]) {
						case 'd' : if (key=="dir"     ) { dir_s    = with_slash(val) ; continue ; } break ; // dir is necessary to access cache
						case 'r' : if (key=="repo_key") { repo_key = Crc(New   ,val) ; continue ; } break ; // cannot be shared as it identifies repo
					DN}
				} catch (::string const& e) { trace("bad_val",key,val) ; throw cat("wrong value for entry ",key,": ",val) ; }
				trace("bad_repo_key",key) ;
				throw cat("wrong key (",key,") in lmake.config") ;
			}
			throw_unless(       +dir_s  , "dir must be specified for daemon_cache") ;
			throw_unless( is_abs(dir_s) , "dir must be absolute for daemon_cache" ) ;
			::vector_s daemon_cmd_line = { *g_lmake_root_s+"bin/ldaemon_cache_server" , "-d"/*no_daemon*/ } ;
			try                           { _fd = connect_to_server( true/*try_old*/ , Magic , ::move(daemon_cmd_line) , ServerMrkr , dir_s ).first ; }
			catch (::pair_s<Rc> const& e) { throw e.first ;                                                                                           }
			service = _fd.service()                                 ;
			_dir_fd = AcFd( dir_s , {.flags=O_RDONLY|O_DIRECTORY} ) ;
			//
			OMsgBuf( RpcReq{.proc=Proc::Config} ).send(_fd) ;
			auto reply = _imsg.receive<RpcReply>( _fd , Maybe/*once*/ , {}/*key*/ ) ; SWEAR( reply.proc==Proc::Config , reply ) ;
			config_ = reply.config ;
		}
	#endif

	void DaemonCache::repair(bool /*dry_run*/) {
		FAIL() ;
	}

	::pair<Cache::DownloadDigest,AcFd> DaemonCache::sub_download( ::string const& job , MDD const& repo_deps ) {
		OMsgBuf( RpcReq{ .proc=Proc::Download , .job=job , .repo_deps=repo_deps } ).send(_fd) ;
		auto     reply     = _imsg.receive<RpcReply>( _fd , Maybe/*once*/ ) ;
		NfsGuard nfs_guard { config_.file_sync }                            ;
		if (reply.hit_info>=CacheHitInfo::Miss) return { DownloadDigest{.hit_info=reply.hit_info} , AcFd() } ;                                             // XXX/ : gcc 11&12 require explicit types
		return {
			DownloadDigest{ .hit_info=reply.hit_info , .job_info=deserialize<JobInfo>(AcFd({_dir_fd,reply.dir_s+"info"},{.nfs_guard=&nfs_guard}).read()) } // .
		,	                                                                          AcFd({_dir_fd,reply.dir_s+"data"},{.nfs_guard=&nfs_guard})
		} ;
	}

	Cache::SubUploadDigest DaemonCache::sub_upload( Delay exe_time , Sz max_sz ) {
		float        rate      = max_sz/float(exe_time)              ; if (rate>config_.max_rate) return {} ; // job is too easy to reproduce, no interest to cache
		ClientSockFd fd        { service }                           ;
		::string     magic_str = fd.read(sizeof(Magic))              ; throw_unless( magic_str.size()==sizeof(Magic) , "bad_answer_sz" ) ;
		uint64_t     magic_    = decode_int<uint64_t>(&magic_str[0]) ; throw_unless( magic_          ==Magic         , "bad_answer"    ) ;
		//
		OMsgBuf( RpcReq{ .proc=Proc::Upload , .reserved_sz=max_sz } ).send(fd) ;
		auto reply = _imsg.receive<RpcReply>( fd , Maybe/*once*/ ) ;
		//
		return {
			.file       = dir_s + s_reserved_file(reply.upload_key)
		,	.upload_key = reply.upload_key
		,	.perm_ext   = config_.perm_ext
		} ;
	}

	void DaemonCache::sub_commit( uint64_t upload_key , ::string const& job , JobInfo&& job_info ) {
		OMsgBuf( RpcReq{ .proc=Proc::Commit , .repo_key=repo_key , .job=job , .job_info=::move(job_info) , .upload_key=upload_key } ).send(_fd) ;
	}

	void DaemonCache::sub_dismiss(uint64_t upload_key) {
		OMsgBuf( RpcReq{ .proc=Proc::Dismiss ,.upload_key=upload_key } ).send(_fd) ;
	}

}
