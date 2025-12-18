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

namespace Caches {

	::string& operator+=( ::string& os , DaemonCacheRpcReq const& dcrr ) {
		/**/                   os << "DaemonCacheRpcReq("<<dcrr.proc ;
		if (+dcrr.job        ) os << ','  <<dcrr.job                 ;
		if (+dcrr.repo_deps  ) os << ",D:"<<dcrr.repo_deps.size()    ;
		if (+dcrr.reserved_sz) os << ",S:"<<dcrr.reserved_sz         ;
		if (+dcrr.upload_key ) os << ",K:"<<dcrr.upload_key          ;
		return                 os <<')'                              ;
	}

	::string& operator+=( ::string& os , DaemonCacheRpcReply const& dcrr ) {
		/**/                   os << "DaemonCacheRpcReply("<<dcrr.proc ;
		if (+dcrr.file       ) os << ','<<dcrr.file                    ;
		if (+dcrr.upload_key ) os << ','<<dcrr.upload_key              ;
		return                 os <<')'                                ;
	}

	::vmap_ss DaemonCache::descr() const {
		return {
			{ "dir_s"    , dir_s          }
		,	{ "repo_key" , repo_key.hex() }
		,	{ "service"  , service.str()  }
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
						case 'd' : if (key=="dir") { dir_s    = with_slash(val) ; continue ; } break ; // dir is necessary to access cache
						case 'k' : if (key=="key") { repo_key = Crc(New   ,val) ; continue ; } break ; // cannot be shared as it identifies repo
					DN}
				} catch (::string const& e) { trace("bad_val",key,val) ; throw cat("wrong value for entry "    ,key,": ",val) ; }
				trace("bad_repo_key",key) ;
				throw cat("wrong key (",key,") in lmake.config") ;
			}
			throw_unless(       +dir_s  , "dir must be specified for daemon_cache") ;
			throw_unless( is_abs(dir_s) , "dir must be absolute for daemon_cache" ) ;
			::vector_s daemon_cmd_line = { *g_lmake_root_s+"bin/ldaemon_cache_server" , "-d"/*no_daemon*/ } ;
			try                           { _fd = connect_to_server( true/*try_old*/ , Magic , ::move(daemon_cmd_line) , ServerMrkr , dir_s ).first ; }
			catch (::pair_s<Rc> const& e) { throw e.first ;                                                                                           }
			service = _fd.service() ;
		}
	#endif

	void DaemonCache::repair(bool /*dry_run*/) {
		FAIL() ;
	}

	::pair<Cache::DownloadDigest,AcFd> DaemonCache::sub_download( ::string const& job , MDD const& repo_deps ) {
		OMsgBuf( RpcReq{ .proc=Proc::Download , .job=job , .repo_deps=repo_deps } ).send(_fd) ;
		auto reply = _imsg.receive<RpcReply>( _fd , Maybe/*once*/ ) ;
		return { ::move(reply.digest) , AcFd(reply.file) } ;
	}

	::pair<uint64_t/*upload_key*/,AcFd> DaemonCache::sub_upload(Sz reserved_sz) {
		ClientSockFd fd        { service }                           ;
		::string     magic_str = fd.read(sizeof(Magic))              ; throw_unless( magic_str.size()==sizeof(Magic) , "bad_answer_sz" ) ; // ... it is probably not working properly
		uint64_t     magic_    = decode_int<uint64_t>(&magic_str[0]) ; throw_unless( magic_          ==Magic         , "bad_answer"    ) ;
		//
		OMsgBuf( RpcReq{ .proc=Proc::Upload , .reserved_sz=reserved_sz } ).send(fd) ;
		auto reply = _imsg.receive<RpcReply>( fd , Maybe/*once*/ ) ;
		//
		return { reply.upload_key , AcFd(reply.file,{.flags=O_WRONLY|O_CREAT|O_TRUNC,.mod=0444,.perm_ext=reply.perm_ext}) } ;
	}

	void DaemonCache::sub_commit( uint64_t upload_key , ::string const& job , JobInfo&& info ) {
		OMsgBuf( RpcReq{ .proc=Proc::Commit , .repo_key=repo_key , .job=job , .info=::move(info) , .upload_key=upload_key } ).send(_fd) ;
	}

	void DaemonCache::sub_dismiss(uint64_t upload_key) {
		OMsgBuf( RpcReq{ .proc=Proc::Dismiss ,.upload_key=upload_key } ).send(_fd) ;
	}

}
