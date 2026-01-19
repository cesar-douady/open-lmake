// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include "serialize.hh"

#include "cache/rpc_cache.hh"

using namespace Disk   ;
using namespace Engine ;

namespace Cache {

	::vector<CacheServerSide> CacheServerSide::s_tab ;

	void CacheServerSide::s_config( ::vmap_s<::vmap_ss> const& caches ) {
		Trace trace(CacheChnl,"Cache::s_config",caches.size()) ;
		for( auto const& [k,cache] : caches ) {
			try {
				trace(k,cache) ;
				s_tab.emplace_back(CacheServerSide(cache)) ;
			} catch (::string const& e) {
				trace("no_config",e) ;
				Fd::Stderr.write(cat("ignore cache ",k," (cannot configure) : ",e,add_nl)) ;
				s_tab.emplace_back() ;
			}
		}
	}

	CacheServerSide::CacheServerSide(::vmap_ss const& dct) {
		Trace trace(CacheChnl,"Cache::Cache",dct.size()) ;
		for( auto const& [key,val] : dct ) {
			try {
				switch (key[0]) {
					case 'd' : if (key=="dir"     ) { dir_s    = with_slash(val) ; continue ; } break ; // dir is necessary to access cache
					case 'r' : if (key=="repo_key") { repo_key =            val  ; continue ; } break ; // cannot be shared as it identifies repo
				DN}
			} catch (::string const& e) { trace("bad_val",key,val) ; throw cat("wrong value for entry ",key,": ",val) ; }
			trace("bad_repo_key",key) ;
			throw cat("wrong key (",key,") in lmake.config") ;
		}
		throw_unless(       +dir_s  , "dir must be specified for cache" ) ;
		throw_unless( is_abs(dir_s) , "dir must be absolute for cache"  ) ;
		::vector_s cmd_line = { *g_lmake_root_s+"bin/lcache_server" , "-d"/*no_daemon*/ } ;
		try                           { _fd = connect_to_server( true/*try_old*/ , CacheMagic , ::move(cmd_line) , ServerMrkr , dir_s , CacheChnl ).first ; }
		catch (::pair_s<Rc> const& e) { throw e.first ;                                                                                                     }
		service = _fd.service()                                 ;
		_dir_fd = AcFd( dir_s , {.flags=O_RDONLY|O_DIRECTORY} ) ;
		//
		OMsgBuf( CacheRpcReq{ .proc=CacheRpcProc::Config , .repo_key=repo_key } ).send(_fd) ;
		auto reply = _imsg.receive<CacheRpcReply>( _fd , Maybe/*once*/ , {}/*key*/ ) ; throw_unless( reply.proc==CacheRpcProc::Config , "cache did not start" ) ;
		max_rate  = reply.config.max_rate  ;
		conn_id   = reply.conn_id          ;
		file_sync = reply.config.file_sync ;
		perm_ext  = reply.config.perm_ext  ;
		trace("done",max_rate,conn_id,file_sync,perm_ext) ;
	}

	::vmap_ss CacheServerSide::descr() const {
		return {
			{ "dir_s"     ,                     dir_s           }
		,	{ "file_sync" , snake_str          (file_sync)      }
		,	{ "max_rate"  , to_string_with_unit(max_rate )      }
		,	{ "perm_ext"  , snake_str          (perm_ext )      }
		,	{ "repo_key"  ,                     repo_key        }
		,	{ "service"   ,                     service  .str() }
		} ;
	}

	CacheServerSide::DownloadDigest CacheServerSide::download( Job job , Rule::RuleMatch const& match , bool incremental ) {
		Trace trace(CacheChnl,"download",job,STR(incremental)) ;
		// provide node actual crc as this is the hit criteria
		::vmap<StrId<CnodeIdx>,DepDigest> deps       ;
		StrId<CjobIdx>                    job_str_id ;
		if ( +job<_cjobs.size() && _cjobs[+job] ) job_str_id = {_cjobs[+job]      } ;
		else                                      job_str_id = {job->unique_name()} ;
		//
		for( Dep const& d : job->deps ) {
			DepDigest dd = d ;
			dd.set_crc(d->crc,d->ok()==No) ;
			if ( +d<_cnodes.size() && _cnodes[+d] ) deps.emplace_back( _cnodes[+d] , dd ) ;
			else                                    deps.emplace_back( d->name()   , dd ) ;
		}
		//
		OMsgBuf( CacheRpcReq{ .proc=CacheRpcProc::Download , .job=job_str_id , .repo_deps=deps } ).send(_fd) ;
		auto    reply   = _imsg.receive<CacheRpcReply>( _fd , Maybe/*once*/ ) ;
		NodeIdx repo_i  = 0                                                   ;
		NodeIdx cache_i = 0                                                   ;
		if ( reply.job_id && job_str_id.is_name()) grow(_cjobs,+job) = reply.job_id ;
		if (+reply.dep_ids)
			for( Dep const& d : job->deps ) {
				SWEAR( repo_i <deps.size() , repo_i ,deps.size() ) ;
				if (deps[repo_i++].first.is_name()) {
					SWEAR( cache_i<reply.dep_ids.size() , cache_i,reply.dep_ids.size() ) ;
					grow(_cnodes,+d) = reply.dep_ids[cache_i++] ;
				}
			}
		//
		trace("hit_info",reply.hit_info) ;
		if (reply.hit_info>=CacheHitInfo::Miss) return {.hit_info=reply.hit_info} ;
		//
		::string       rd              = run_dir( job_str_id.is_name()?job_str_id.name:job->unique_name() , reply.key , reply.key_is_last ) ;
		NfsGuard       cache_nfs_guard { file_sync                                            }                                             ;
		NfsGuard       repo_nfs_guard  { g_config->file_sync                                  }                                             ;
		AcFd           download_fd     { {_dir_fd,rd+"-data"} , {.nfs_guard=&cache_nfs_guard} }                                             ; // open as soon as possible as entry could disappear
		AcFd           info_fd         { {_dir_fd,rd+"-info"} , {.nfs_guard=&cache_nfs_guard} }                                             ; // .
		DownloadDigest res             { .hit_info=reply.hit_info , .job_info=deserialize<JobInfo>(info_fd.read()) }                        ;
		//
		if (res.hit_info==CacheHitInfo::Hit) {                                                                // actually download targets
			Zlvl zlvl = res.job_info.start.start.zlvl ;
			#if !HAS_ZLIB
				throw_if( zlvl.tag==ZlvlTag::Zlib , "cannot uncompress without zlib" ) ;
			#endif
			#if !HAS_ZSTD
				throw_if( zlvl.tag==ZlvlTag::Zstd , "cannot uncompress without zstd" ) ;
			#endif
			//
			JobEndRpcReq          & end      = res.job_info.end ;
			JobDigest<>           & digest   = end.digest       ; throw_if( digest.incremental && incremental , "cached job was incremental" ) ;
			::vmap_s<TargetDigest>& targets  = digest.targets   ;
			NodeIdx                 n_copied = 0                ;
			::vmap_s<FileAction>    actions  ;                    for( auto [t,a] : job->pre_actions(match,true/*no_incremental*/) ) actions.emplace_back( t->name() , a ) ;
			//
			trace("download",targets.size(),zlvl) ;
			res.file_actions_msg = do_file_actions( /*out*/::ref(::vector_s())/*unlnks*/ , /*out*/::ref(false)/*incremental*/ , ::move(actions) , &repo_nfs_guard ) ;
			try {
				InflateFd data_fd    { ::move(download_fd) , zlvl }                                             ;
				auto      target_szs = IMsgBuf().receive<::vector<DiskSz>>( data_fd , Yes/*once*/ , {}/*key*/ ) ; SWEAR( target_szs.size()==targets.size() , target_szs.size(),targets.size() ) ;
				//
				for( NodeIdx ti : iota(targets.size()) ) {
					auto&           entry = targets[ti]            ;
					::string const& tn    = entry.first            ;
					FileTag         tag   = entry.second.sig.tag() ;
					DiskSz          sz    = target_szs[ti]         ;
					n_copied = ti+1 ;                                                                         // this is a protection, so record n_copied *before* action occurs
					repo_nfs_guard.change(tn) ;
					if (tag==FileTag::None) try { unlnk( tn                  ) ; } catch (::string const&) {} // if we do not want the target, avoid unlinking potentially existing sub-files
					else                          unlnk( tn , {.dir_ok=true} ) ;
					switch (tag) {
						case FileTag::None  :                                                                                               break ;
						case FileTag::Lnk   : trace("lnk_to"  ,tn,sz) ; sym_lnk( tn , data_fd.read(target_szs[ti]) )                      ; break ;
						case FileTag::Empty : trace("empty_to",tn   ) ; AcFd   ( tn , {O_WRONLY|O_TRUNC|O_CREAT|O_NOFOLLOW,0666/*mod*/} ) ; break ;
						case FileTag::Exe   :
						case FileTag::Reg   : {
							AcFd fd { tn , { O_WRONLY|O_TRUNC|O_CREAT|O_NOFOLLOW , mode_t(tag==FileTag::Exe?0777:0666) } } ;
							if (sz) { trace("write_to"  ,tn,sz) ; data_fd.receive_to( fd , sz ) ; }
							else      trace("no_data_to",tn   ) ;                                             // empty exe are Exe, not Empty
						} break ;
					DN}
					entry.second.sig = FileSig(tn) ;                                                          // target digest is not stored in cache
				}
				end.end_date = New ;                                                                          // date must be after files are copied
				// ensure we take a single lock at a time to avoid deadlocks
				trace("done") ;
			} catch(::string const& e) {
				trace("failed",e,n_copied) ;
				for( NodeIdx ti : iota(n_copied) ) unlnk(targets[ti].first) ;                                 // clean up partial job
				trace("throw") ;
				throw ;
			}
		}
		return res ;
	}

	void CacheServerSide::commit( Job job , CacheUploadKey upload_key ) {
		Trace trace(CacheChnl,"Cache::commit",upload_key,job) ;
		//
		JobInfo job_info = job.job_info() ;
		if (!( +job_info.start && +job_info.end )) {  // we need a full report to cache job
			trace("no_ancillary_file") ;
			dismiss(upload_key) ;
			throw "no ancillary file"s ;
		}
		//
		job_info.update_digest() ;                    // ensure cache has latest crc available
		// check deps
		::vmap<StrId<CnodeIdx>,DepDigest> repo_deps ;
		for( auto const& [dn,dd] : job_info.end.digest.deps ) {
			if ( !dd.is_crc || dd.never_match() ) {
				trace("not_a_crc_dep",dn,dd) ;
				dismiss(upload_key) ;
				throw cat("not a valid crc dep : ",dn) ;
			}
			repo_deps.emplace_back(dn,dd) ;
		}
		job_info.cache_cleanup() ;                    // defensive programming : remove useless/meaningless info
		::string job_info_str = serialize(job_info) ;
		{	NfsGuard nfs_guard    { file_sync                                                                                                                          } ;
			AcFd     ifd          { {_dir_fd,reserved_file(upload_key)+"-info"} , {.flags=O_WRONLY|O_CREAT|O_TRUNC,.mod=0444,.perm_ext=perm_ext,.nfs_guard=&nfs_guard} } ;
			ifd.write(job_info_str) ;
		}
		//
		StrId<CjobIdx> job_str_id ;
		if ( +job<_cjobs.size() && _cjobs[+job] ) job_str_id = {_cjobs[+job]      } ;
		else                                      job_str_id = {job->unique_name()} ;
		CacheRpcReq crr {
			.proc        = CacheRpcProc::Commit
		,	.job         = job_str_id
		,	.repo_deps   = ::move(repo_deps)
		,	.total_z_sz  = job_info.end.total_z_sz
		,	.job_info_sz = job_info_str.size()
		,	.exe_time    = job_info.end.digest.exe_time
		,	.upload_key  = upload_key
		} ;
		trace("req",crr) ;
		OMsgBuf(crr).send(_fd) ;
		trace("done") ;
	}

}
