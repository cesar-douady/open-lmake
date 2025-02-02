// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sched.h>        // unshare
#include <sys/mount.h>    // mount
#include <sys/sendfile.h>

#include "disk.hh"
#include "hash.hh"
#include "trace.hh"
#include "caches/dir_cache.hh" // PER_CACHE : add include line for each cache method

#include "rpc_job.hh"

#if HAS_ZLIB
	#define ZLIB_CONST
	#include <zlib.h>
#endif

using namespace Disk ;
using namespace Hash ;

//
// FileAction
//

::string& operator+=( ::string& os , FileAction const& fa ) {
	/**/                                os << "FileAction(" << fa.tag ;
	if (fa.tag<=FileActionTag::HasFile) os <<','<< fa.sig             ;
	return                              os <<')'                      ;
}

::pair_s<bool/*ok*/> do_file_actions( ::vector_s* /*out*/ unlnks , ::vmap_s<FileAction>&& pre_actions , NfsGuard& nfs_guard ) {
	::uset_s keep_dirs ;
	::string msg       ;
	bool     ok        = true ;
	//
	Trace trace("do_file_actions",pre_actions) ;
	if (unlnks) unlnks->reserve(unlnks->size()+pre_actions.size()) ;                                       // most actions are unlinks
	for( auto const& [f,a] : pre_actions ) {                                                               // pre_actions are adequately sorted
		SWEAR(+f) ;                                                                                        // acting on root dir is non-sense
		switch (a.tag) {
			case FileActionTag::Unlink         :
			case FileActionTag::UnlinkWarning  :
			case FileActionTag::UnlinkPolluted :
			case FileActionTag::None           : {
				FileSig sig { nfs_guard.access(f) } ;
				if (!sig) break ;                                                                          // file does not exist, nothing to do
				bool done       = true/*garbage*/                                                        ;
				bool quarantine = sig!=a.sig && (a.crc==Crc::None||!a.crc.valid()||!a.crc.match(Crc(f))) ;
				if (quarantine) {
					done = ::rename( f.c_str() , dir_guard(QuarantineDirS+f).c_str() )==0 ;
					if (done) msg<<"quarantined "         <<mk_file(f)<<'\n' ;
					else      msg<<"failed to quarantine "<<mk_file(f)<<'\n' ;
				} else {
					SWEAR(is_lcl(f)) ;
					try {
						done = unlnk(nfs_guard.change(f)) ;
						if (a.tag==FileActionTag::None) { if ( done) msg << "unlinked "           << mk_file(f) << '\n' ; }
						else                            { if (!done) msg << "file disappeared : " << mk_file(f) << '\n' ; }
						done = true ;
					} catch (::string const& e) {
						msg <<  e << '\n' ;
						done = false ;
					}
				}
				trace(STR(quarantine),STR(done),f) ;
				if ( done && unlnks ) unlnks->push_back(f) ;
				ok &= done ;
			} break ;
			case FileActionTag::NoUniquify : if (can_uniquify(nfs_guard.change(f))) msg<<"did not uniquify "<<mk_file(f)<<'\n' ; break ;
			case FileActionTag::Uniquify   : if (uniquify    (nfs_guard.change(f))) msg<<"uniquified "      <<mk_file(f)<<'\n' ; break ;
			case FileActionTag::Mkdir      : mk_dir_s(with_slash(f),nfs_guard) ;                                                 break ;
			case FileActionTag::Rmdir      :
				if (!keep_dirs.contains(f))
					try {
						rmdir_s(with_slash(nfs_guard.change(f))) ;
					} catch (::string const&) {                                                            // if a dir cannot rmdir'ed, no need to try those uphill
						keep_dirs.insert(f) ;
						for( ::string d_s=dir_name_s(f) ; +d_s ; d_s=dir_name_s(d_s) )
							if (!keep_dirs.insert(no_slash(d_s)).second) break ;
					}
			break ;
		DF}
	}
	trace("done",STR(ok),localize(msg)) ;
	return {msg,ok} ;
}

//
// JobReason
//

::string& operator+=( ::string& os , JobReason const& jr ) {
	os << "JobReason(" << jr.tag ;
	if (jr.tag>=JobReasonTag::HasNode) os << ',' << jr.node ;
	return os << ')' ;
}

//
// EndAttrs
//

::string& operator+=( ::string& os , EndAttrs const& ea ) {
	First first ;
	/**/                    os << "EndAttrs("                            ;
	if (+ea.cache         ) os <<first("",",")<<       ea.cache          ;
	if (+ea.max_stderr_len) os <<first("",",")<<"L:"<< ea.max_stderr_len ;
	return                  os << ')'                                    ;
}

//
// DepInfo
//

::string& operator+=( ::string& os , DepInfo const& di ) {
	switch (di.kind) {
		case DepInfoKind::Crc  : return os <<'('<< di.crc () <<')' ;
		case DepInfoKind::Sig  : return os <<'('<< di.sig () <<')' ;
		case DepInfoKind::Info : return os <<'('<< di.info() <<')' ;
	DF}
}

//
// Cache
//

struct DeflateFd : AcFd {
	// cxtors & casts
	DeflateFd() = default ;
	#if HAS_ZLIB
		DeflateFd( AcFd&& fd , uint8_t lvl_=0 ) : AcFd{::move(fd)} , lvl{lvl_} {
			SWEAR(lvl<=Z_BEST_COMPRESSION) ;
			if (lvl) {
				int rc = deflateInit(&_zs,lvl) ; SWEAR(rc==Z_OK) ;
				_reset_buf() ;
			}
		}
		~DeflateFd() { flush() ; }
	#else
		DeflateFd( AcFd&& fd , uint8_t lvl=0 ) : AcFd{::move(fd)} { SWEAR(!lvl,lvl) ; }
		~DeflateFd() { _flush() ; }
	#endif
	// services
	void write(::string const& s) {
		#if HAS_ZLIB
			SWEAR(!_flushed) ;
			if (lvl) {
				_zs.next_in  = reinterpret_cast<uint8_t const*>(s.data()) ;
				_zs.avail_in = s.size()                                   ;
				while (_zs.avail_in) {
					if (!_zs.avail_out) {
						AcFd::write({_buf,DiskBufSz}) ;
						total_sz += DiskBufSz ;
						_reset_buf() ;
					}
					deflate(&_zs,Z_NO_FLUSH) ;
				}
				return ;
			}
		#endif
		_flush(s.size()) ;
		if      (s.size()>=DiskBufSz) { AcFd::write(s)                              ; total_sz += s.size() ; }                                                           // large data : send directly
		else if (s.size()           ) { ::memcpy( _buf+_pos , s.data() , s.size() ) ; _pos     += s.size() ; }                                                           // small data : put in _buf
	}
	void send_from( Fd fd_ , size_t sz ) {
		#if HAS_ZLIB
			SWEAR(!_flushed) ;
			if (lvl) {
				while (sz) {
					size_t cnt = ::min(sz,DiskBufSz) ;
					::string s = fd_.read(cnt) ; throw_unless(s.size()==cnt,"missing ",cnt-s.size()," bytes from ",fd) ;
					write(s) ;
					sz -= cnt ;
				}
				return ;
			}
		#endif
		_flush(sz) ;
		if      (sz>=DiskBufSz) { SWEAR(!_pos) ; size_t c = ::sendfile(self,fd_,nullptr,sz) ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; total_sz += sz ; } // large data : send directly
		else if (sz           ) {                size_t c = fd_.read_to({_buf+_pos,sz})     ; throw_unless(c==sz,"missing ",sz-c," bytes from ",fd) ; _pos     += c  ; } // small data : put in _buf
	}
	void flush() {
		#if HAS_ZLIB
			if (_flushed) return ;
			_flushed = true ;
			if (lvl) {
				_zs.next_in  = nullptr ;
				_zs.avail_in = 0       ;
				for (;;) {
					int    rc = deflate(&_zs,Z_FINISH)                                ;
					size_t sz = size_t(_zs.next_out-reinterpret_cast<uint8_t*>(_buf)) ;
					AcFd::write({_buf,sz}) ;
					total_sz += sz ;
					switch (rc) {
						case Z_OK         :
						case Z_BUF_ERROR  :                    break  ;
						case Z_STREAM_END : deflateEnd(&_zs) ; return ;
					DF}
					_reset_buf() ;
				}
				return ;
			}
		#endif
		_flush() ;
	}
private :
	#if HAS_ZLIB
		void _reset_buf() {
			_zs.next_out  = reinterpret_cast<uint8_t*>(_buf) ;
			_zs.avail_out = DiskBufSz                        ;
		}
	#endif
	void _flush(size_t room=DiskBufSz) {                                                                                                                                 // flush if not enough room
		#if HAS_ZLIB
			SWEAR(!lvl) ;
		#endif
		if (_pos+room<=DiskBufSz) return ;                                                                                                                               // enough room
		if (!_pos               ) return ;                                                                                                                               // _buf is already empty
		AcFd::write({_buf,_pos}) ;
		total_sz += _pos ;
		_pos      = 0    ;
	}
	// data
public :
	Disk::DiskSz total_sz = 0 ;
	#if HAS_ZLIB
		uint8_t lvl = 0 ;
	#endif
private :
	char   _buf[DiskBufSz] ;
	size_t _pos            = 0 ;
	#if HAS_ZLIB
		z_stream _zs      = {}    ;
		bool     _flushed = false ;
	#endif
} ;

struct InflateFd : AcFd {
	// cxtors & casts
	InflateFd() = default ;
	#if HAS_ZLIB
		InflateFd( AcFd&& fd , bool lvl_=false ) : AcFd{::move(fd)} , lvl{lvl_} {
			if (lvl) {
				int rc = inflateInit(&_zs) ; SWEAR(rc==Z_OK) ;
				_reset_buf() ;
			}
		}
	#else
		InflateFd( AcFd&& fd , bool lvl=false ) : AcFd{::move(fd)} { SWEAR(!lvl,lvl) ; }
	#endif
	// services
	::string read(size_t sz) {
		::string res ( sz , 0 ) ;
		#if HAS_ZLIB
			if (lvl) {
				_zs.next_out  = reinterpret_cast<uint8_t*>(res.data()) ;
				_zs.avail_out = res.size()                             ;
				while (_zs.avail_out) {
					if (!_zs.avail_in) {
						size_t cnt = AcFd::read_to({_buf,DiskBufSz}) ; throw_unless(cnt,"missing ",_zs.avail_out," bytes from ",self) ;
						_reset_buf(cnt) ;
					}
					inflate(&_zs,Z_NO_FLUSH) ;
				}
				return res ;
			}
		#endif
		size_t   cnt = ::min( sz , _len ) ;
		if (cnt) {                                                                                                           // gather available data from _buf
			::memcpy( res.data() , _buf+_pos , cnt ) ;
			_pos += cnt ;
			_len -= cnt ;
			sz   -= cnt ;
		}
		if (sz) {
			SWEAR(!_len,_len) ;
			if (sz>=DiskBufSz) {                                                                                             // large data : read directly
				size_t c = AcFd::read_to({&res[cnt],sz}) ; throw_unless(c   ==sz,"missing ",sz-c   ," bytes from ",self) ;
			} else {                                                                                                         // small data : bufferize
				_len = AcFd::read_to({_buf,DiskBufSz}) ;   throw_unless(_len>=sz,"missing ",sz-_len," bytes from ",self) ;
				::memcpy( &res[cnt] , _buf , sz ) ;
				_pos  = sz ;
				_len -= sz ;
			}
		}
		return res ;
	}
	void send_to( Fd fd_ , size_t sz ) {
		#if HAS_ZLIB
			if (lvl) {
				while (sz) {
					size_t   cnt = ::min(sz,DiskBufSz)                           ;
					::string s   = read(cnt) ; SWEAR(s.size()==cnt,s.size(),cnt) ;
					fd_.write(s) ;
					sz -= cnt ;
				}
				return ;
			}
		#endif
		size_t cnt = ::min(sz,_len) ;
		if (cnt) {                                                                                                           // gather available data from _buf
			fd_.write({_buf+_pos,cnt}) ;
			_pos += cnt ;
			_len -= cnt ;
			sz   -= cnt ;
		}
		if (sz) {
			SWEAR(!_len,_len) ;
			if (sz>=DiskBufSz) {                                                                                             // large data : transfer directly fd to fd
				size_t c = ::sendfile(fd_,self,nullptr,sz) ; throw_unless(c   ==sz,"missing ",sz-c   ," bytes from ",self) ;
			} else {                                                                                                         // small data : bufferize
				_len = AcFd::read_to({_buf,DiskBufSz}) ;     throw_unless(_len>=sz,"missing ",sz-_len," bytes from ",self) ;
				fd_.write({_buf,sz}) ;
				_pos  = sz ;
				_len -= sz ;
			}
		}
	}
private :
	#if HAS_ZLIB
		void _reset_buf(size_t cnt=0) {
			_zs.next_in  = reinterpret_cast<uint8_t const*>(_buf) ;
			_zs.avail_in = cnt                                    ;
		}
	#endif
	// data
public :
	#if HAS_ZLIB
		bool lvl = false ;
	#endif
private :
	char   _buf[DiskBufSz] ;
	size_t _pos        = 0 ;
	size_t _len        = 0 ;
	#if HAS_ZLIB
		z_stream _zs = {} ;
	#endif
} ;

namespace Caches {

	::map_s<Cache*> Cache::s_tab ;

	Cache* Cache::s_new(Tag tag) {
		switch (tag) {
			case Tag::None : return nullptr      ; // base class Cache actually caches nothing
			case Tag::Dir  : return new DirCache ; // PER_CACHE : add a case for each cache method
		DF}
	}

	void Cache::s_config( ::string const& key , Tag tag , ::vmap_ss const& dct ) {
		Cache* cache = s_new(tag) ;
		cache->config(dct) ;
		s_tab.emplace(key,cache) ;
	}

	JobInfo Cache::download( ::string const& match_key , NfsGuard& repo_nfs_guard ) {
		Trace trace("Cache::download",match_key) ;
		//
		::pair<JobInfo,AcFd>    info_fd  = sub_download(match_key) ;
		JobInfo&                job_info = info_fd.first           ;
		JobDigest&              digest   = job_info.end.digest     ;
		::vmap_s<TargetDigest>& targets  = digest.targets          ;
		NodeIdx                 n_copied = 0                       ;
		try {
			#if !HAS_ZLIB
				throw_if( job_info.start.start.z_lvl , "cannot uncompress without zlib" ) ;
			#endif
			//
			NodeIdx   n_targets = targets.size()                                              ;
			auto      mode      = [](FileTag t)->int { return t==FileTag::Exe?0777:0666 ; }   ;
			InflateFd data_fd   { ::move(info_fd.second) , bool(job_info.start.start.z_lvl) } ;
			//
			::string     target_szs_str = data_fd.read(n_targets*sizeof(Sz)) ;
			::vector<Sz> target_szs     ( n_targets )                        ; for( NodeIdx ti : iota(n_targets) ) ::memcpy( &target_szs[ti] , &target_szs_str[ti*sizeof(Sz)] , sizeof(Sz) ) ;
			for( NodeIdx ti : iota(n_targets) ) {
				auto&           entry = targets[ti]            ;
				::string const& tn    = entry.first            ;
				FileTag         tag   = entry.second.sig.tag() ;
				Sz              sz    = target_szs[ti]         ;
				n_copied = ti+1 ;
				repo_nfs_guard.change(tn) ;
				unlnk(dir_guard(tn),true/*dir_ok*/) ;
				switch (tag) {
					case FileTag::Lnk   : trace("lnk_to"  ,tn,sz) ; lnk( tn , data_fd.read(target_szs[ti]) )                                       ; break ;
					case FileTag::Empty : trace("empty_to",tn   ) ; AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC , mode(tag) )) ; break ;
					case FileTag::Exe   :
					case FileTag::Reg   :
						if (sz) { trace("write_to"  ,tn,sz) ; data_fd.send_to( AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,mode(tag) )) , sz ) ; }
						else    { trace("no_data_to",tn   ) ;                  AcFd(::open( tn.c_str() , O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,mode(tag) ))        ; } // may be an empty exe
					break ;
				DN}
				entry.second.sig = FileSig(tn) ;                          // target digest is not stored in cache
			}
			digest.end_date = New ;                                       // date must be after files are copied
			// ensure we take a single lock at a time to avoid deadlocks
			trace("done") ;
			return job_info ;
		} catch(::string const& e) {
			for( NodeIdx ti : iota(n_copied) ) unlnk(targets[ti].first) ; // clean up partial job
			trace("failed") ;
			throw e ;
		}
	}

	::string/*upload_key*/ Cache::upload( ::vmap_s<TargetDigest> const& targets , ::vector<FileInfo> const& target_fis , uint8_t z_lvl ) {
		Trace trace("DirCache::upload",targets.size(),z_lvl) ;
		//
		Sz max_sz = 0 ; for( FileInfo fi : target_fis ) max_sz += fi.sz ;
		#if HAS_ZLIB
			static_assert(sizeof(ulong)==sizeof(Sz)) ;                               // compressBound manages ulong and we need a Sz
			if (z_lvl) max_sz = ::compressBound(max_sz) ;
		#else
			SWEAR(!z_lvl,z_lvl) ;
		#endif
		::pair_s<AcFd> key_fd = sub_upload(max_sz) ;
		//
		try {
			NodeIdx n_targets = targets.size()                  ;
			DeflateFd data_fd { ::move(key_fd.second) , z_lvl } ;
			//
			SWEAR( target_fis.size()==n_targets , target_fis.size() , n_targets ) ;
			::string target_szs_str ( n_targets*sizeof(Sz) , 0 ) ; for( NodeIdx ti : iota(n_targets) ) ::memcpy( &target_szs_str[ti*sizeof(Sz)] , &target_fis[ti].sz , sizeof(Sz) ) ;
			data_fd.write(target_szs_str) ;
			//
			for( NodeIdx ti : iota(n_targets) ) {
				::pair_s<TargetDigest> const& entry = targets[ti]            ;
				::string               const& tn    = entry.first            ;
				FileTag                       tag   = entry.second.sig.tag() ;
				Sz                            sz    = target_fis[ti].sz      ;
				switch (tag) {
					case FileTag::Lnk   : { trace("lnk_from"  ,tn,sz) ; ::string l = read_lnk(tn) ; SWEAR(l.size()==sz) ; data_fd.write(l) ; goto ChkSig ; }
					case FileTag::Empty :   trace("empty_from",tn   ) ;                                                                      break       ;
					case FileTag::Reg   :
					case FileTag::Exe   :
						if (sz) {
							trace("read_from",tn,sz) ;
							data_fd.send_from( {::open(tn.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NOATIME)} , sz ) ;
							goto ChkSig ;
						} else {
							trace("empty_from",tn) ;
							break ;
						}
					break ;
				DN}
				continue ;
			ChkSig :                                                                 // ensure cache entry is reliable by checking file *after* copy
				throw_unless( FileSig(tn)==target_fis[ti].sig() , "unstable ",tn ) ;
			}
			data_fd.flush() ;                                                        // update data_fd.sz
		} catch (::string const&) {
			dismiss(key_fd.first) ;
			trace("failed") ;
			return {} ;
		}
		trace("done") ;
		return ::move(key_fd.first) ;
	}

	bool/*ok*/ Cache::commit( ::string const& upload_key , ::string const& job , JobInfo&& job_info ) {
		Trace trace("Cache::commit",upload_key,job) ;
		//
		JobInfoStart& start = job_info.start ;
		JobEndRpcReq& end   = job_info.end   ;
		//
		if (!( +start && +end )) {                 // we need a full report to cache job
			trace("no_ancillary_file") ;
			dismiss(upload_key) ;
			return false/*ok*/ ;
		}
		// check deps
		for( auto const& [dn,dd] : end.digest.deps ) if (!dd.is_crc) {
			trace("not_a_crc_dep",dn,dd) ;
			dismiss(upload_key) ;
			return false/*ok*/ ;
		}
		// defensive programming : remove useless/meaningless info
		start.eta                   = {}        ;  // cache does not care
		start.submit_attrs.cache    = {}        ;  // no recursive info
		start.submit_attrs.live_out = false     ;  // cache does not care
		start.submit_attrs.reason   = {}        ;  // .
		start.pre_start.seq_id      = SeqId(-1) ;  // 0 is reserved to mean no info
		start.pre_start.job         = 0         ;  // cache does not care
		start.pre_start.port        = 0         ;  // .
		start.start.addr            = 0         ;  // .
		start.start.cache           = nullptr   ;  // no recursive info
		start.start.live_out        = false     ;  // cache does not care
		start.start.small_id        = 0         ;  // no small_id since no execution
		start.start.pre_actions     = {}        ;  // pre_actions depend on execution context
		start.rsrcs                 = {}        ;  // caching resources is meaningless as they have no impact on content
		end.seq_id                  = SeqId(-1) ;  // 0 is reserved to mean no info
		end.job                     = 0         ;  // cache does not care
		end.digest.upload_key       = {}        ;  // no recursive info
		end.digest.end_attrs.cache  = {}        ;  // .
		for( auto& [_,td] : end.digest.targets ) {
			SWEAR(!td.pre_exist) ;                 // else cannot be a candidate for upload as this must have failed
			td.sig = td.sig.tag() ;                // forget date, just keep tag
		}
		//
		return sub_commit( upload_key , job , ::move(job_info) ) ;
	}

}

//
// JobSpace
//

::string& operator+=( ::string& os , JobSpace::ViewDescr const& vd ) {
	/**/             os <<"ViewDescr("<< vd.phys ;
	if (+vd.copy_up) os <<"CU:"<< vd.copy_up     ;
	return           os <<')'                    ;
}

::string& operator+=( ::string& os , JobSpace const& js ) {
	First first ;
	/**/                  os <<"JobSpace("                           ;
	if (+js.chroot_dir_s) os <<first("",",")<<"C:"<< js.chroot_dir_s ;
	if (+js.lmake_view_s) os <<first("",",")<<"R:"<< js.lmake_view_s ;
	if (+js.repo_view_s ) os <<first("",",")<<"R:"<< js.repo_view_s  ;
	if (+js.tmp_view_s  ) os <<first("",",")<<"T:"<< js.tmp_view_s   ;
	if (+js.views       ) os <<first("",",")<<"V:"<< js.views        ;
	return                os <<')'                                   ;
}

	static void _chroot(::string const& dir_s) { Trace trace("_chroot",dir_s) ; if (::chroot(no_slash(dir_s).c_str())!=0) throw "cannot chroot to "+no_slash(dir_s)+" : "+::strerror(errno) ; }
	static void _chdir (::string const& dir_s) { Trace trace("_chdir" ,dir_s) ; if (::chdir (no_slash(dir_s).c_str())!=0) throw "cannot chdir to " +no_slash(dir_s)+" : "+::strerror(errno) ; }

	static void _mount_bind( ::string const& dst , ::string const& src ) { // src and dst may be files or dirs
		Trace trace("_mount_bind",dst,src) ;
		if (::mount( no_slash(src).c_str() , no_slash(dst).c_str() , nullptr/*type*/ , MS_BIND|MS_REC , nullptr/*data*/ )!=0)
			throw "cannot bind mount "+src+" onto "+dst+" : "+::strerror(errno) ;
	}

static void _mount_overlay( ::string const& dst_s , ::vector_s const& srcs_s , ::string const& work_s ) {
	SWEAR(+srcs_s) ;
	SWEAR(srcs_s.size()>1,dst_s,srcs_s,work_s) ; // use bind mount in that case
	//
	Trace trace("_mount_overlay",dst_s,srcs_s,work_s) ;
	for( size_t i : iota(1,srcs_s.size()) )
		if (srcs_s[i].find(':')!=Npos)
			throw cat("cannot overlay mount ",dst_s," to ",srcs_s,"with embedded columns (:)") ;
	mk_dir_s(work_s) ;
	//
	::string                                data  = "userxattr"                      ;
	/**/                                    data += ",upperdir="+no_slash(srcs_s[0]) ;
	/**/                                    data += ",lowerdir="+no_slash(srcs_s[1]) ;
	for( size_t i : iota(2,srcs_s.size()) ) data += ':'         +no_slash(srcs_s[i]) ;
	/**/                                    data += ",workdir=" +no_slash(work_s   ) ;
	if (::mount( nullptr ,  no_slash(dst_s).c_str() , "overlay" , 0 , data.c_str() )!=0)
		throw "cannot overlay mount "+dst_s+" to "+data+" : "+::strerror(errno) ;
}

static void _atomic_write( ::string const& file , ::string const& data ) {
	Trace trace("_atomic_write",file,data) ;
	AcFd fd { file , Fd::Write } ;
	throw_unless( +fd , "cannot open ",file," for writing" ) ;
	ssize_t cnt = ::write( fd , data.c_str() , data.size() ) ;
	if (cnt<0                  ) throw "cannot write atomically "s+data.size()+" bytes to "+file+" : "+::strerror(errno)         ;
	if (size_t(cnt)<data.size()) throw "cannot write atomically "s+data.size()+" bytes to "+file+" : only "+cnt+" bytes written" ;
}

bool JobSpace::_is_lcl_tmp(::string const& f) const {
	if (is_lcl(f)  ) return true                      ;
	if (+tmp_view_s) return f.starts_with(tmp_view_s) ;
	/**/             return false                     ;
} ;

bool/*dst_ok*/ JobSpace::_create( ::vmap_s<MountAction>& deps , ::string const& dst , ::string const& src ) const {
	if (!_is_lcl_tmp(dst)) return false/*dst_ok*/ ;
	bool dst_ok = true ;
	if (is_dirname(dst)) {
		mk_dir_s(dst) ;
		deps.emplace_back(no_slash(dst),MountAction::Access) ;
	} else if (+FileInfo(dst).tag()) {
		deps.emplace_back(dst,MountAction::Access) ;
	} else if (+src) {
		/**/                        deps.emplace_back(src,MountAction::Read ) ;
		if ((dst_ok=+cpy(dst,src))) deps.emplace_back(dst,MountAction::Write) ;
		else                        dst_ok = false ;
	} else {
		AcFd fd { dir_guard(dst) , Fd::Write } ;
		if ((dst_ok=+fd)) deps.emplace_back(dst,MountAction::Write) ;
	}
	return dst_ok ;
}

bool/*entered*/ JobSpace::enter(
	::vmap_s<MountAction>&/*out*/ report
,	::string   const&             phy_lmake_root_s
,	::string   const&             phy_repo_root_s
,	::string   const&             phy_tmp_dir_s
,	::string   const&             cwd_s
,	::string   const&             work_dir_s
,	::vector_s const&             src_dirs_s
) {
	Trace trace("JobSpace::enter",self,phy_repo_root_s,phy_tmp_dir_s,cwd_s,work_dir_s,src_dirs_s) ;
	//
	if (!self) return false/*entered*/ ;
	//
	int uid = ::getuid() ;                                                          // must be done before unshare that invents a new user
	int gid = ::getgid() ;                                                          // .
	//
	if (::unshare(CLONE_NEWUSER|CLONE_NEWNS)!=0) throw "cannot create namespace : "s+::strerror(errno) ;
	//
	size_t   uphill_lvl = 0 ;
	::string highest_s  ;
	for( ::string const& d_s : src_dirs_s ) if (!is_abs_s(d_s))
		if ( size_t ul=uphill_lvl_s(d_s) ; ul>uphill_lvl ) {
			uphill_lvl = ul  ;
			highest_s  = d_s ;
		}
	//
	::string phy_super_repo_root_s ;                                                // dir englobing all relative source dirs
	::string super_repo_view_s     ;                                                // .
	::string top_repo_view_s       ;
	if (+repo_view_s) {
		if (!( repo_view_s.ends_with(cwd_s) && repo_view_s.size()>cwd_s.size()+1 )) // ensure repo_view_s has at least one more level than cwd_s
			throw
				"cannot map local repository dir to "+no_slash(repo_view_s)+" appearing as "+no_slash(cwd_s)+" in top-level repository, "
				"consider setting <rule>.repo_view="+mk_py_str("/repo/"+no_slash(cwd_s))
			;
		phy_super_repo_root_s = phy_repo_root_s ; for( [[maybe_unused]] size_t _ : iota(uphill_lvl) ) phy_super_repo_root_s = dir_name_s(phy_super_repo_root_s) ;
		super_repo_view_s     = repo_view_s     ; for( [[maybe_unused]] size_t _ : iota(uphill_lvl) ) super_repo_view_s     = dir_name_s(super_repo_view_s    ) ;
		SWEAR(phy_super_repo_root_s!="/",phy_repo_root_s,uphill_lvl) ;              // this should have been checked earlier
		if (!super_repo_view_s)
			throw
				"cannot map repository dir to "+no_slash(repo_view_s)+" with relative source dir "+no_slash(highest_s)
			+	", "
			+	"consider setting <rule>.repo_view="+mk_py_str("/repo/"+no_slash(phy_repo_root_s.substr(phy_super_repo_root_s.size())+cwd_s))
			;
		if (substr_view(repo_view_s,super_repo_view_s.size())!=substr_view(phy_repo_root_s,phy_super_repo_root_s.size()))
			throw
				"last "s+uphill_lvl+" components do not match between physical root dir and root view"
			+	", "
			+	"consider setting <rule>.repo_view="+mk_py_str("/repo/"+no_slash(phy_repo_root_s.substr(phy_super_repo_root_s.size())+cwd_s))
			;
		top_repo_view_s = repo_view_s.substr(0,repo_view_s.size()-cwd_s.size()) ;
	}
	if ( +lmake_view_s      && lmake_view_s     .rfind('/',lmake_view_s     .size()-2)!=0 ) throw "non top-level lmake_view not yet implemented"s ; // XXX! : handle cases where dir is not top level
	if ( +super_repo_view_s && super_repo_view_s.rfind('/',super_repo_view_s.size()-2)!=0 ) throw "non top-level repo_view not yet implemented"s  ; // .
	if ( +tmp_view_s        && tmp_view_s       .rfind('/',tmp_view_s       .size()-2)!=0 ) throw "non top-level tmp_view not yet implemented"s   ; // .
	//
	::string chroot_dir        = chroot_dir_s                                                          ; if (+chroot_dir) chroot_dir.pop_back() ; // cannot use no_slash to properly manage the '/' case
	bool     must_create_lmake = +lmake_view_s      && !is_dir(chroot_dir+no_slash(lmake_view_s     )) ;
	bool     must_create_repo  = +super_repo_view_s && !is_dir(chroot_dir+no_slash(super_repo_view_s)) ;
	bool     must_create_tmp   = +tmp_view_s        && !is_dir(chroot_dir+no_slash(tmp_view_s       )) ;
	trace("create",STR(must_create_repo),STR(must_create_tmp)) ;
	if ( must_create_repo || must_create_tmp || +views )
		try { unlnk_inside_s(work_dir_s) ; } catch (::string const& e) {} // if we need a work dir, we must clean it first as it is not cleaned upon exit (ignore errors as dir may not exist)
	if ( must_create_lmake || must_create_repo || must_create_tmp ) {     // we cannot mount directly in chroot_dir
		if (!work_dir_s)
			throw
				"need a work dir to"s
			+	(	must_create_lmake ? " create lmake view"
				:	must_create_repo  ? " create repo view"
				:	must_create_tmp   ? " create tmp view"
				:	                    " ???"
				)
			;
		::vector_s top_lvls    = lst_dir_s(+chroot_dir_s?chroot_dir_s:"/") ;
		::string   work_root   = work_dir_s+"root"                         ;
		::string   work_root_s = work_root+'/'                             ;
		mk_dir_s      (work_root_s) ;
		unlnk_inside_s(work_root_s) ;
		trace("top_lvls",work_root_s,top_lvls) ;
		for( ::string const& f : top_lvls ) {
			::string src_f     = (+chroot_dir_s?chroot_dir_s:"/"s) + f ;
			::string private_f = work_root_s                       + f ;
			switch (FileInfo(src_f).tag()) {                                                                                   // exclude weird files
				case FileTag::Reg   :
				case FileTag::Empty :
				case FileTag::Exe   : AcFd    (        private_f    ,Fd::Write      ) ; _mount_bind(private_f,src_f) ; break ; // create file
				case FileTag::Dir   : mk_dir_s(with_slash(private_f)                ) ; _mount_bind(private_f,src_f) ; break ; // create dir
				case FileTag::Lnk   : lnk     (           private_f ,read_lnk(src_f)) ;                                break ; // copy symlink
			DN}
		}
		if (must_create_lmake) mk_dir_s(work_root+lmake_view_s     ) ;
		if (must_create_repo ) mk_dir_s(work_root+super_repo_view_s) ;
		if (must_create_tmp  ) mk_dir_s(work_root+tmp_view_s       ) ;
		chroot_dir = ::move(work_root) ;
	}
	// mapping uid/gid is necessary to manage overlayfs
	_atomic_write( "/proc/self/setgroups" , "deny"                 ) ;                                                         // necessary to be allowed to write the gid_map (if desirable)
	_atomic_write( "/proc/self/uid_map"   , ""s+uid+' '+uid+" 1\n" ) ;
	_atomic_write( "/proc/self/gid_map"   , ""s+gid+' '+gid+" 1\n" ) ;
	//
	::string repo_root_s = +repo_view_s ? top_repo_view_s : phy_repo_root_s ;
	if (+lmake_view_s) _mount_bind( chroot_dir+lmake_view_s      , phy_lmake_root_s      ) ;
	if (+repo_view_s ) _mount_bind( chroot_dir+super_repo_view_s , phy_super_repo_root_s ) ;
	if (+tmp_view_s  ) _mount_bind( chroot_dir+tmp_view_s        , phy_tmp_dir_s         ) ;
	//
	if      (+chroot_dir ) _chroot(chroot_dir)     ;
	if      (+repo_view_s) _chdir(repo_view_s    ) ;
	else if (+chroot_dir ) _chdir(phy_repo_root_s) ;
	//
	size_t work_idx = 0 ;
	for( auto const& [view,descr] : views ) if (+descr) {                                                                      // empty descr does not represent a view
		::string   abs_view = mk_abs(view,repo_root_s) ;
		::vector_s abs_phys ;                            abs_phys.reserve(descr.phys.size()) ; for( ::string const& phy : descr.phys ) abs_phys.push_back(mk_abs(phy,repo_root_s)) ;
		/**/                                    _create(report,view) ;
		for( ::string const& phy : descr.phys ) _create(report,phy ) ;
		if (is_dirname(view)) {
			for( ::string const& cu : descr.copy_up ) {
				::string dst = descr.phys[0]+cu ;
				if (is_dirname(cu))
					_create(report,dst) ;
				else
					for( size_t i : iota(1,descr.phys.size()) )
						if (_create(report,dst,descr.phys[i]+cu)) break ;
			}
		}
		size_t          sz    = descr.phys.size() ;
		::string const& upper = descr.phys[0]     ;
		if (sz==1) {
			_mount_bind( abs_view , abs_phys[0] ) ;
		} else {
			::string work_s = is_lcl(upper) ? work_dir_s+"work_"+(work_idx++)+'/' : upper.substr(0,upper.size()-1)+".work/" ;  // if not in the repo, it must be in tmp
			mk_dir_s(work_s) ;
			_mount_overlay( abs_view , abs_phys , mk_abs(work_s,repo_root_s) ) ;
		}
	}
	trace("done") ;
	return true/*entered*/ ;
}

// XXX! : implement recursive views
// for now, phys cannot englobe or lie within a view, but when it is to be implemented, it is here
::vmap_s<::vector_s> JobSpace::flat_phys() const {
	::vmap_s<::vector_s> res ; res.reserve(views.size()) ;
	for( auto const& [view,descr] : views ) res.emplace_back(view,descr.phys) ;
	return res ;
}

void JobSpace::mk_canon(::string const& phy_repo_root_s) {
	auto do_top = [&]( ::string& dir_s , bool slash_ok , ::string const& key )->void {
		if ( !dir_s                                       ) return ;
		if ( !is_canon(dir_s)                             ) dir_s = ::mk_canon(dir_s) ;
		if ( slash_ok && dir_s=="/"                       ) return ;
		if (             dir_s=="/"                       ) throw key+" cannot be /"                                           ;
		if ( !is_abs(dir_s)                               ) throw key+" must be absolute : "+no_slash(dir_s)                   ;
		if ( phy_repo_root_s.starts_with(dir_s          ) ) throw "repository cannot lie within "+key+' '+no_slash(dir_s)      ;
		if ( dir_s          .starts_with(phy_repo_root_s) ) throw key+' '+no_slash(dir_s)+" cannot be local to the repository" ;
	} ;
	//                   slash_ok
	do_top( chroot_dir_s , true  , "chroot dir" ) ;
	do_top( lmake_view_s , false , "lmake view" ) ;
	do_top( repo_view_s  , false , "repo view"  ) ;
	do_top( tmp_view_s   , false , "tmp view"   ) ;
	if ( +lmake_view_s && +repo_view_s ) {
		if (lmake_view_s.starts_with(repo_view_s )) throw "lmake view "+no_slash(lmake_view_s)+" cannot lie within repo view " +no_slash(repo_view_s ) ;
 		if (repo_view_s .starts_with(lmake_view_s)) throw "repo view " +no_slash(repo_view_s )+" cannot lie within lmake view "+no_slash(lmake_view_s) ;
	}
	if ( +lmake_view_s && +tmp_view_s ) {
		if (lmake_view_s.starts_with(tmp_view_s  )) throw "lmake view "+no_slash(lmake_view_s)+" cannot lie within tmp view "  +no_slash(tmp_view_s  ) ;
		if (tmp_view_s  .starts_with(lmake_view_s)) throw "tmp view "  +no_slash(tmp_view_s  )+" cannot lie within lmake view "+no_slash(lmake_view_s) ;
	}
	if ( +repo_view_s && +tmp_view_s ) {
 		if (repo_view_s .starts_with(tmp_view_s  )) throw "repo view " +no_slash(repo_view_s )+" cannot lie within tmp view "  +no_slash(tmp_view_s  ) ;
		if (tmp_view_s  .starts_with(repo_view_s )) throw "tmp view "  +no_slash(tmp_view_s  )+" cannot lie within repo view " +no_slash(repo_view_s ) ;
	}
	//
	::string const& job_repo_root_s = +repo_view_s ? repo_view_s : phy_repo_root_s ;
	auto do_path = [&](::string& path)->void {
		if      (!is_canon(path)                  ) path = ::mk_canon(path)             ;
		if      (path.starts_with("../")          ) path = mk_abs(path,job_repo_root_s) ;
		else if (path.starts_with(job_repo_root_s)) path.erase(0,job_repo_root_s.size()) ;
	} ;
	for( auto& [view,_] : views ) {
		do_path(view) ;
		if (!view                            ) throw "cannot map the whole repository"s                  ;
		if (job_repo_root_s.starts_with(view)) throw "repository cannot lie within view "+no_slash(view) ;
	}
	//
	for( auto& [view,descr] : views ) {
		bool is_dir_view = is_dirname(view)  ;
		/**/                             if ( !is_dir_view && descr.phys.size()!=1                                     ) throw "cannot map non-dir " +no_slash(view)+" to an overlay" ;
		for( auto const& [v,_] : views ) if ( &v!=&view && view.starts_with(v) && (v.back()=='/'||view[v.size()]=='/') ) throw "cannot map "+no_slash(view)+" within "+v              ;
		bool lcl_view = _is_lcl_tmp(view) ;
		for( ::string& phy : descr.phys ) {
			do_path(phy) ;
			if ( !lcl_view && _is_lcl_tmp(phy)    ) throw "cannot map external view "+no_slash(view)+" to local or tmp "+no_slash(phy) ;
			if (  is_dir_view && !is_dirname(phy) ) throw "cannot map dir "          +no_slash(view)+" to file "        +no_slash(phy) ;
			if ( !is_dir_view &&  is_dirname(phy) ) throw "cannot map file "         +no_slash(view)+" to dir "         +no_slash(phy) ;
			if (+phy) {
				for( auto const& [v,_] : views ) {                                                                            // XXX! : suppress this check when recursive maps are implemented
					if ( phy.starts_with(v  ) && (v  .back()=='/'||phy[v  .size()]=='/') ) throw "cannot map "+no_slash(view)+" to "+no_slash(phy)+" within "    +no_slash(v) ;
					if ( v  .starts_with(phy) && (phy.back()=='/'||v  [phy.size()]=='/') ) throw "cannot map "+no_slash(view)+" to "+no_slash(phy)+" containing "+no_slash(v) ;
				}
			} else {
				for( auto const& [v,_] : views )                                                                              // XXX! : suppress this check when recursive maps are implemented
					if (!is_abs(v)) throw "cannot map "+no_slash(view)+" to full repository with "+no_slash(v)+" being map" ;
			}
		}
	}
}

//
// JobStartRpcReq
//

::string& operator+=( ::string& os , JobStartRpcReq const& jsrr ) {
	return os << "JobStartRpcReq(" << jsrr.seq_id <<','<< jsrr.job <<','<< jsrr.port <<','<< jsrr.msg <<')' ;
}

//
// JobEndRpcReq
//

::string& operator+=( ::string& os , TargetDigest const& td ) {
	const char* sep = "" ;
	/**/                    os << "TargetDigest("      ;
	if ( td.pre_exist   ) { os <<      "pre_exist"     ; sep = "," ; }
	if (+td.tflags      ) { os <<sep<< td.tflags       ; sep = "," ; }
	if (+td.extra_tflags) { os <<sep<< td.extra_tflags ; sep = "," ; }
	if (+td.crc         ) { os <<sep<< td.crc          ; sep = "," ; }
	if (+td.sig         )   os <<sep<< td.sig          ;
	return                  os <<')'                   ;
}

::string& operator+=( ::string& os , JobDigest const& jd ) {
	return os << "JobDigest(" << jd.end_attrs <<','<< jd.wstatus<<':'<<jd.status <<','<< jd.targets <<','<< jd.deps << ')' ;
}

::string& operator+=( ::string& os , JobEndRpcReq const& jerr ) {
	return os << "JobEndRpcReq(" << jerr.seq_id <<','<< jerr.job <<','<< jerr.digest <<','<< jerr.phy_tmp_dir_s <<','<< jerr.dynamic_env <<','<< jerr.msg <<')' ;
}

//
// JobStartRpcReply
//

::string& operator+=( ::string& os , MatchFlags const& mf ) {
	/**/             os << "MatchFlags(" ;
	switch (mf.is_target) {
		case Yes   : os << "target" ; if (+mf.tflags()) os<<','<<mf.tflags() ; if (+mf.extra_tflags()) os<<','<<mf.extra_tflags() ; break ;
		case No    : os << "dep,"   ; if (+mf.dflags()) os<<','<<mf.dflags() ; if (+mf.extra_dflags()) os<<','<<mf.extra_dflags() ; break ;
		case Maybe :                                                                                                                break ;
	DF}
	return           os << ')' ;
}

::string& operator+=( ::string& os , JobStartRpcReply const& jsrr ) {
	/**/                           os << "JobStartRpcReply("                ;
	/**/                           os <<','  << to_hex(jsrr.addr)           ;
	/**/                           os <<','  << jsrr.autodep_env            ;
	if (+jsrr.job_space          ) os <<','  << jsrr.job_space              ;
	if ( jsrr.keep_tmp           ) os <<','  << "keep"                      ;
	if (+jsrr.cwd_s              ) os <<','  << jsrr.cwd_s                  ;
	if (+jsrr.ddate_prec         ) os <<','  << jsrr.ddate_prec             ;
	/**/                           os <<','  << mk_printable(cat(jsrr.env)) ; // env may contain the non-printable EnvPassMrkr value
	/**/                           os <<','  << jsrr.interpreter            ;
	/**/                           os <<','  << jsrr.kill_sigs              ;
	if (jsrr.live_out            ) os <<','  << "live_out"                  ;
	if (jsrr.allow_stderr        ) os <<','  << "allow_stderr"              ;
	/**/                           os <<','  << jsrr.method                 ;
	if (+jsrr.network_delay      ) os <<','  << jsrr.network_delay          ;
	if (+jsrr.pre_actions        ) os <<','  << jsrr.pre_actions            ;
	/**/                           os <<','  << jsrr.small_id               ;
	if (+jsrr.star_matches       ) os <<','  << jsrr.star_matches           ;
	if (+jsrr.deps               ) os <<'<'  << jsrr.deps                   ;
	if (+jsrr.end_attrs          ) os <<','  << jsrr.end_attrs              ;
	if (+jsrr.static_matches     ) os <<'>'  << jsrr.static_matches         ;
	if (+jsrr.stdin              ) os <<'<'  << jsrr.stdin                  ;
	if (+jsrr.stdout             ) os <<'>'  << jsrr.stdout                 ;
	if (+jsrr.timeout            ) os <<','  << jsrr.timeout                ;
	/**/                           os <<','  << jsrr.cmd                    ; // last as it is most probably multi-line
	return                         os <<')'                                 ;
}

template<class... A> static bool/*match*/ _handle( ::string& v/*inout*/ , size_t& d /*inout*/, const char* key , A const&... args ) {
	size_t len   = ::strlen(key)    ;
	bool   brace = v[d+1/*$*/]=='{' ;
	size_t start = d+1/*$*/+brace   ;
	if ( ::string_view(&v[start],len)!=key    ) return false/*match*/ ;
	if (  brace && v[start+len]!='}'          ) return false/*match*/ ;
	if ( !brace && is_word_char(v[start+len]) ) return false/*match*/ ;
	::string pfx = v.substr(0,d) + no_slash(cat(args...)) ;
	d = pfx.size()                                             ;
	v = ::move(pfx) + ::string_view(v).substr(start+len+brace) ;
	return true/*match*/ ;
}
bool/*entered*/ JobStartRpcReply::enter(
		::vmap_s<MountAction>&/*out*/ actions
	,	::map_ss             &/*out*/ cmd_env
	,	::vmap_ss            &/*out*/ dynamic_env
	,	pid_t                &/*out*/ first_pid
	,	::string        const&/*in */ phy_lmake_root_s
	,	::string        const&/*in */ phy_repo_root_s
	,	::string        const&/*in */ phy_tmp_dir_s
	,	SeqId                 /*in */ seq_id
) {
	Trace trace("JobStartRpcReply::enter",phy_lmake_root_s,phy_repo_root_s,phy_tmp_dir_s,seq_id) ;
	//
	for( auto& [k,v] : env )
		if      (v!=EnvPassMrkr)                                                             cmd_env[k] = ::move(v ) ;
		else if (has_env(k)    ) { ::string ev=get_env(k) ; dynamic_env.emplace_back(k,ev) ; cmd_env[k] = ::move(ev) ; } // if special illegal value, use value from environment (typically from slurm)
	//
	::string const& lmake_root_s            = +job_space.lmake_view_s ? job_space.lmake_view_s : phy_lmake_root_s ;
	/**/            autodep_env.repo_root_s = +job_space.repo_view_s  ? job_space.repo_view_s  : phy_repo_root_s  ;
	/**/            autodep_env.tmp_dir_s   = +job_space.tmp_view_s   ? job_space.tmp_view_s   : phy_tmp_dir_s    ;
	_tmp_dir_s = autodep_env.tmp_dir_s ;                                                                                 // for use in exit (autodep.tmp_dir_s may be moved)
	//
	try {
		unlnk_inside_s(phy_tmp_dir_s,true/*abs_ok*/) ;                                                                   // ensure tmp dir is clean
	} catch (::string const&) {
		try                       { mk_dir_s(phy_tmp_dir_s) ;            }                                               // ensure tmp dir exists
		catch (::string const& e) { throw "cannot create tmp dir : "+e ; }
	}
	//
	cmd_env["TMPDIR"] = no_slash(autodep_env.tmp_dir_s) ;
	if (PY_LD_LIBRARY_PATH[0]!=0) {
		auto [it,inserted] = cmd_env.try_emplace("LD_LIBRARY_PATH",PY_LD_LIBRARY_PATH) ;
		if (!inserted) it->second <<':'<< PY_LD_LIBRARY_PATH ;
	}
	for( auto& [k,v] : cmd_env ) {
		for( size_t d=0 ;; d++ ) {
			d = v.find('$',d) ;
			if (d==Npos) break ;
			switch (v[d+1/*$*/]) {
				//                inout inout
				case 'L' : _handle( v  , d  , "LMAKE_ROOT"             , lmake_root_s                    ) ; break ;
				case 'P' : _handle( v  , d  , "PHYSICAL_LMAKE_ROOT"    , phy_lmake_root_s                )
				||         _handle( v  , d  , "PHYSICAL_REPO_ROOT"     , phy_repo_root_s         , cwd_s )
				||         _handle( v  , d  , "PHYSICAL_TMPDIR"        , phy_tmp_dir_s                   )
				||         _handle( v  , d  , "PHYSICAL_TOP_REPO_ROOT" , phy_repo_root_s                 ) ; break ;
				case 'R' : _handle( v  , d  , "REPO_ROOT"              , autodep_env.repo_root_s , cwd_s ) ; break ;
				case 'S' : _handle( v  , d  , "SEQUENCE_ID"            , seq_id                          )
				||         _handle( v  , d  , "SMALL_ID"               , small_id                        ) ; break ;
				case 'T' : _handle( v  , d  , "TMPDIR"                 , autodep_env.tmp_dir_s           )
				||         _handle( v  , d  , "TOP_REPO_ROOT"          , autodep_env.repo_root_s         ) ; break ;
			DN}
		}
	}
	//
	::string phy_work_dir_s = cat(PrivateAdminDirS,"work/",small_id,'/')                                                                                               ;
	bool     entered        = job_space.enter( /*out*/actions , phy_lmake_root_s , phy_repo_root_s , phy_tmp_dir_s , cwd_s , phy_work_dir_s , autodep_env.src_dirs_s ) ;
	if (entered) {
		// find a good starting pid
		// the goal is to minimize risks of pid conflicts between jobs in case pid is used to generate unique file names as temporary file instead of using TMPDIR, which is quite common
		// to do that we spread pid's among the availale range by setting the first pid used by jos as apart from each other as possible
		// call phi the golden number and NPids the number of available pids
		// spreading is maximized by using phi*NPids as an elementary spacing and id (small_id) as an index modulo NPids
		// this way there is a conflict between job 1 and job 2 when (id2-id1)*phi is near an integer
		// because phi is the irrational which is as far from rationals as possible, and id's are as small as possible, this probability is minimized
		// note that this is over-quality : any more or less random number would do the job : motivation is mathematical beauty rather than practical efficiency
		static constexpr uint32_t FirstPid = 300                                 ; // apparently, pid's wrap around back to 300
		static constexpr uint64_t NPids    = MAX_PID - FirstPid                  ; // number of available pid's
		static constexpr uint64_t DeltaPid = (1640531527*NPids) >> n_bits(NPids) ; // use golden number to ensure best spacing (see above), 1640531527 = (2-(1+sqrt(5))/2)<<32
		first_pid = FirstPid + ((small_id*DeltaPid)>>(32-n_bits(NPids)))%NPids ;   // DeltaPid on 64 bits to avoid rare overflow in multiplication
	}
	return entered ;
}

void JobStartRpcReply::exit() {
	// work dir cannot be cleaned up as we may have chroot'ed inside
	Trace trace("JobStartRpcReply::exit",STR(keep_tmp),_tmp_dir_s) ;
	if ( !keep_tmp && +_tmp_dir_s ) unlnk_inside_s(_tmp_dir_s,true/*abs_ok*/ ) ;
	job_space.exit() ;
}

//
// JobMngtRpcReq
//

::string& operator+=( ::string& os , JobMngtRpcReq const& jmrr ) {
	/**/                               os << "JobMngtRpcReq(" << jmrr.proc <<','<< jmrr.seq_id <<','<< jmrr.job <<','<< jmrr.fd ;
	switch (jmrr.proc) {
		case JobMngtProc::LiveOut    : os <<','<< jmrr.txt.size() ;                             break ;
		case JobMngtProc::ChkDeps    :
		case JobMngtProc::DepVerbose : os <<','<< jmrr.deps       ;                             break ;
		case JobMngtProc::Encode     : os <<','<< jmrr.min_len    ;                             [[fallthrough]] ;
		case JobMngtProc::Decode     : os <<','<< jmrr.ctx <<','<< jmrr.file <<','<< jmrr.txt ; break ;
		default                      :                                                          break ;
	}
	return                             os <<')' ;
}

//
// JobMngtRpcReply
//

::string& operator+=( ::string& os , JobMngtRpcReply const& jmrr ) {
	/**/                               os << "JobMngtRpcReply(" << jmrr.proc ;
	switch (jmrr.proc) {
		case JobMngtProc::ChkDeps    : os <<','<< jmrr.fd <<','<<                                   jmrr.ok ; break ;
		case JobMngtProc::DepVerbose : os <<','<< jmrr.fd <<','<< jmrr.dep_infos                            ; break ;
		case JobMngtProc::Decode     : os <<','<< jmrr.fd <<','<< jmrr.txt <<','<< jmrr.crc <<','<< jmrr.ok ; break ;
		case JobMngtProc::Encode     : os <<','<< jmrr.fd <<','<< jmrr.txt <<','<< jmrr.crc <<','<< jmrr.ok ; break ;
	DN}
	return                             os << ')' ;
}

//
// SubmitAttrs
//

::string& operator+=( ::string& os , SubmitAttrs const& sa ) {
	First first ;
	/**/               os << "SubmitAttrs("             ;
	if (+sa.tag      ) os <<first("",",")<< sa.tag      ;
	if ( sa.live_out ) os <<first("",",")<< "live_out"  ;
	if (+sa.pressure ) os <<first("",",")<< sa.pressure ;
	if (+sa.deps     ) os <<first("",",")<< sa.deps     ;
	if (+sa.reason   ) os <<first("",",")<< sa.reason   ;
	return             os <<')'                         ;
}

//
// JobInfoStart
//

::string& operator+=( ::string& os , JobInfoStart const& jis ) {
	return os << "JobInfoStart(" << jis.submit_attrs <<','<< jis.rsrcs <<','<< jis.pre_start <<','<< jis.start <<')' ;
}

//
// JobInfo
//

JobInfo::JobInfo(::string const& filename , Bool3 get_start , Bool3 get_end ) {
	Trace trace("JobInfo",filename,get_start,get_end) ;
	if ( get_start==No && get_end==No ) return ;                                                              // fast path : dont read filename
	::string      job_info ;            try { job_info = AcFd(filename).read() ; } catch (::string const&) {} // empty string in case of error, will be processed later
	::string_view jis      = job_info ;
	try {
		if (get_start==No) deserialize( jis , ::ref(JobInfoStart()) ) ;                                       // even if we do not need start, we need to skip it
		else               deserialize( jis , start                 ) ;
		trace("start") ;
	} catch (...) {
		if ( get_start!=No                  ) start = {} ;                                                    // ensure start is either empty or full
		if ( get_start==Yes || get_end==Yes ) throw ;                                                         // if we cannot skip start, we cannot get end
		return ;                                                                                              // .
	}
	try {
		if (get_end==No) return ;
		deserialize( jis , end ) ;
		trace("end") ;
	} catch (...) {
		end = {} ;                                                                                            // ensure end is either empty or full
		if (get_end==Yes) throw ;
	}
}

//
// codec
//

namespace Codec {

	::string mk_decode_node( ::string const& file , ::string const& ctx , ::string const& code ) {
		return CodecPfx+mk_printable<'/'>(file)+'/'+mk_printable<'/'>(ctx)+'/'+mk_printable(code) ;
	}

	::string mk_encode_node( ::string const& file , ::string const& ctx , ::string const& val ) {
		return CodecPfx+mk_printable<'/'>(file)+'/'+mk_printable<'/'>(ctx)+'/'+Xxh(val).digest().hex() ;
	}

	::string mk_file(::string const& node) {
		return parse_printable<'/'>(node,::ref(sizeof(CodecPfx)-1)) ; // account for terminating null in CodecPfx
	}

}
