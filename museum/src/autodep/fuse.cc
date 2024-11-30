// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// XXX : fuse autodep does not work for the following reason :
// when foo/bar is open(RDONLY), fuse first does lookup(top-level,foo) before lookup(foo,bar) and finally open(foo/bar)
// the problem is that if foo does not exist, we still want to record a dep on foo/bar
// and we never get the info that foo/bar is accessed
// for now, there is no solution to this problem
// replying foo is a directory if it does not exist breaks immediately, for example if simply writing to foo
// and there is no way to tell if lookup(top-level,foo) is because of an access to foo/bar or to foo

#include <sys/statvfs.h>
#include <sys/xattr.h>

#include "disk.hh"
#include "fd.hh"
#include "trace.hh"

#include "record.hh"

#include "fuse.hh"

#if ! HAS_FUSE
	#error cannot compile fuse without fuse support
#endif

using namespace Disk ;

static constexpr bool T = false ;

namespace Fuse {

	//
	// auditing code
	//

	Record Mount::s_auditor ;

	//
	// Mount
	//

	static Mount& mk_self(fuse_req_t req      ) { return *reinterpret_cast<Mount*>(fuse_req_userdata(req)) ; }
	static Mount& mk_self(void*      user_data) { return *reinterpret_cast<Mount*>(user_data             ) ; }

	::fuse_entry_param Mount::mk_fuse_entry_param( fuse_ino_t parent , const char* name ) {
		::fuse_entry_param res = {}             ;
		FdEntry const&     pe  = fds.at(parent) ;
		//
		int rc = ::fstatat( pe.fd , name , &res.attr , AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW ) ; if (rc<0) throw errno ;
		//
		auto entry_inserted = fds.inc_ref(res.attr.st_ino) ;
		if (entry_inserted.second) {
			SWEAR( name && *name ) ;
			entry_inserted.first.fd   = ::openat( pe.fd , name , O_PATH|O_NOFOLLOW )         ;
			entry_inserted.first.name = parent==FUSE_ROOT_ID?::string(name):pe.name+'/'+name ;
		}
		res.ino           = res.attr.st_ino ;
		res.generation    = 0               ;
		res.attr_timeout  = Infinity        ;
		res.entry_timeout = Infinity        ;
		return res ;
	}

	void Mount::report_access( fuse_ino_t parent , const char* name , Accesses a , bool write , ::string&& comment ) const {
		if (!name) name = "" ;
		if ( parent==FUSE_ROOT_ID && !*name ) return ;
		::string n = report_name(parent,name) ;
		if ((n+'/').starts_with(ADMIN_DIR_S)) return ;
		//
		if      (!report_writes) write = false ;
		if      (+a            ) s_auditor.report_access( ::move(n) , FileInfo(fds.fd(parent),name) , a  , Yes&write , ::move(comment) ) ;
		else if (write         ) s_auditor.report_access( ::move(n) , FileInfo(                   ) , {} , Yes       , ::move(comment) ) ;
	}

	//
	// helpers
	//

	struct DirEntry {
		DIR     * dir    = nullptr ;
		::dirent* entry  = nullptr ;
		off_t     offset = 0       ;
	} ;
	static DirEntry*& dir_entry( ::fuse_file_info* fi ) {
		static_assert(sizeof(fi->fh)>=sizeof(DirEntry*)) ;
		return *reinterpret_cast<DirEntry**>(&fi->fh) ;
	}

	static ::fuse_bufvec mk_fuse_bufvec( ::fuse_buf const& buf ) {
		return {
			.count = 1
		,	.idx   = 0
		,	.off   = 0
		,	.buf   = {buf}
		} ;
	}

	static ::fuse_bufvec mk_fuse_bufvec( void* mem , size_t sz ) {
		return mk_fuse_bufvec({
			.size  = sz
		,	.flags = fuse_buf_flags(0)
		,	.mem   = mem
		,	.fd    = -1
		,	.pos   = 0
		}) ;
	}

	static ::fuse_bufvec mk_fuse_bufvec( Fd fd , off_t offset , size_t sz ) {
		return mk_fuse_bufvec({
			.size  = sz
		,	.flags = fuse_buf_flags(FUSE_BUF_IS_FD|FUSE_BUF_FD_SEEK)
		,	.mem   = nullptr
		,	.fd    = fd
		,	.pos   = offset
		}) ;
	}

	//
	// callbacks
	//

	static void lo_access( fuse_req_t req , fuse_ino_t ino , int mask ) {
		if (T) ::cerr<<t_thread_key<<" access "<<ino<<" "<<mask<<endl ;
		Mount& self = mk_self(req) ;
		int    rc   = ::faccessat( self.fds.fd(ino) , "" , mask , AT_EMPTY_PATH ) ;
		self.report_dep( ino , Access::Stat , "access" ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_bmap( fuse_req_t req , fuse_ino_t , size_t /*blk_sz*/ , uint64_t /*idx*/ ) {
		::fuse_reply_err(req,ENOSYS) ;
	}

	static void lo_copy_file_range(
		fuse_req_t req
	,	fuse_ino_t , off_t offset_in  , struct ::fuse_file_info* fi_in
	,	fuse_ino_t , off_t offset_out , struct ::fuse_file_info* fi_out
	,	size_t     len
	,	int        flags
	) {
		if (T) ::cerr<<t_thread_key<<" copy_file_range"<<endl ;
		try {
			int len_done = ::copy_file_range( fi_in->fh , &offset_in , fi_out->fh , &offset_out , len , flags ) ; if (len_done<0) throw errno ;
			::fuse_reply_write(req,len_done) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_create( fuse_req_t req , fuse_ino_t parent , const char* name , mode_t mode , ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" create "<<parent<<" "<<name<<" "<<mode<<endl ;
		Mount& self = mk_self(req) ;
		try {
			SWEAR(*name) ;
			fi->fh         = ::openat( self.fds.fd(parent) , name , (fi->flags|O_CREAT)&~O_NOFOLLOW , mode ) ; if (!Fd(fi->fh)) throw errno ;
			fi->keep_cache = true                                                                            ;
			//
			::fuse_entry_param res = self.mk_fuse_entry_param( parent , name )                                       ;
			//
			if (Fd(fi->fh)) self.report_target( parent , name , "create" ) ;
			::fuse_reply_create( req , &res , fi ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_destroy(void* user_data) {
		if (T) ::cerr<<t_thread_key<<" destroy"<<endl ;
		Mount& self = mk_self(user_data) ;
		self.fds.clear() ;
	}

	static void lo_fallocate( fuse_req_t req , fuse_ino_t , int mode , off_t offset , off_t len , struct ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" fallocate "<<Fd(fi->fh)<<mode<<" "<<offset<<" "<<len<<endl ;
		int rc = ::fallocate( fi->fh , mode , offset , len ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_flock( fuse_req_t req , fuse_ino_t , struct ::fuse_file_info* fi , int op ) {
		if (T) ::cerr<<t_thread_key<<" flock "<<Fd(fi->fh)<<op<<endl ;
		int rc = ::flock( fi->fh , op ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_flush( fuse_req_t req , fuse_ino_t , struct ::fuse_file_info* fi ) { // called whenever a file descriptor is closed
		if (T) ::cerr<<t_thread_key<<" flush "<<Fd(fi->fh)<<endl ;
		int rc = ::close(::dup(fi->fh)) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_forget( fuse_req_t req , fuse_ino_t ino , uint64_t n ) {
		if (T) ::cerr<<t_thread_key<<" forget "<<ino<<" "<<n<<endl ;
		mk_self(req).fds.dec_ref( ino , n ) ;
		::fuse_reply_none(req) ;
	}

	static void lo_forget_multi( fuse_req_t req , size_t cnt , ::fuse_forget_data* forgets ) {
		if (T) ::cerr<<t_thread_key<<" forget_multi "<<cnt<<endl ;
		Mount& self = mk_self(req) ;
		for ( ::fuse_forget_data const& e : ::span(forgets,cnt) ) self.fds.dec_ref( e.ino , e.nlookup ) ;
		::fuse_reply_none(req) ;
	}

	static void lo_fsync( fuse_req_t req , fuse_ino_t , int data_sync , struct ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" fsync "<<Fd(fi->fh)<<endl ;
		int rc = data_sync ? ::fdatasync(fi->fh) : ::fsync(fi->fh) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_fsyncdir( fuse_req_t req , fuse_ino_t , int data_sync , struct ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" fsyncdir"<<endl ;
		Fd  fd = ::dirfd(dir_entry(fi)->dir)               ;
		int rc = data_sync ? ::fdatasync(fd) : ::fsync(fd) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_getattr( fuse_req_t req , fuse_ino_t ino , struct ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" getattr "<<ino<<endl ;
		Mount& self = mk_self(req) ;
		try {
			self.report_dep( ino , Access::Stat , "getattr" ) ;
			struct ::stat st ;
			int           rc = ::fstatat( fi?fi->fh:self.fds.fd(ino).fd , "" , &st , AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW ) ; if (rc<0) throw errno ;
			::fuse_reply_attr( req , &st , Infinity ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_getlk( fuse_req_t req , fuse_ino_t , struct ::fuse_file_info* , struct ::flock* ) {
		::fuse_reply_err(req,ENOSYS) ;
	}

	static void lo_getxattr( fuse_req_t req , fuse_ino_t ino , const char* attr , size_t sz ) {
		if (T) ::cerr<<t_thread_key<<" getxattr "<<ino<<" "<<attr<<" "<<sz<<endl ;
		try {
			::vector<char> buf     ( sz )                                                                           ;
			int            attr_sz = ::fgetxattr( mk_self(req).fds.fd(ino) , attr , sz?buf.data():nullptr , sz ) ; if (attr_sz< 0) throw errno ;
			if (attr_sz==0) throw 0 ;                                                                                                            // mimic example code from fuse repo
			::fuse_reply_buf( req , buf.data() , attr_sz ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_init( void* user_data , ::fuse_conn_info* /*conn*/ ) {
		Mount& self = mk_self(user_data) ;
		if (T) ::cerr<<t_thread_key<<" init"<<self.dst_s<<" "<<self.src_s<<endl ;
		self.fds.root.fd = ::open( no_slash(self.src_s).c_str() , O_PATH|O_NOFOLLOW|O_DIRECTORY|O_CLOEXEC ) ;
	}

	static void lo_ioctl(
		fuse_req_t req , fuse_ino_t
	,	uint /*cmd*/ , void* /*arg*/
	,	struct ::fuse_file_info*
	,	uint /*flags*/ , const void* /*in_buf*/ , size_t /*in_sz*/ , size_t /*out_sz*/
	) {
		::fuse_reply_err(req,EINVAL) ;
	}

	static void lo_link( fuse_req_t req , fuse_ino_t ino , fuse_ino_t parent , const char* name ) {
		if (T) ::cerr<<t_thread_key<<" link "<<parent<<" "<<name<<" "<<ino<<endl ;
		Mount& self = mk_self(req) ;
		try {
			self.report_dep( ino , Access::Reg , "link.R" ) ;
			int rc = ::linkat( AT_FDCWD , self.fds.proc(ino).c_str() , parent , name , AT_SYMLINK_FOLLOW ) ; if (rc<0) throw errno ;
			self.report_target( parent , name , "link.W" ) ;
			self.reply_entry( req , ino , "" ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_listxattr( fuse_req_t req , fuse_ino_t ino , size_t sz ) {
		if (T) ::cerr<<t_thread_key<<" listxattr "<<ino<<" "<<sz<<endl ;
		try {
			::vector<char> buf ( sz )                                                                          ;
			ssize_t        len = ::listxattr( mk_self(req).fds.proc(ino).c_str() , buf.data() , sz ) ; if (len<0) throw errno ;
			::fuse_reply_buf( req , buf.data() , len ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_lookup( fuse_req_t req , fuse_ino_t parent , const char* name ) {
		if (T) ::cerr<<t_thread_key<<" lookup "<<parent<<" "<<name<<endl ;
		try {
			mk_self(req).reply_entry( req , parent , name ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_lseek( fuse_req_t req , fuse_ino_t , off_t offset , int whence , struct ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" lseek "<<Fd(fi->fh)<<offset<<" "<<whence<<endl ;
		try {
			int res = ::lseek( fi->fh , offset , whence ) ; if (res<0) throw errno ;
			::fuse_reply_lseek( req , res ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_mkdir( fuse_req_t req , fuse_ino_t parent , const char* name , mode_t mode ) {
		if (T) ::cerr<<t_thread_key<<" mkdir "<<parent<<" "<<name<<" "<<mode<<endl ;
		try {
			Mount& self = mk_self(req)                                   ;
			int    rc   = ::mkdirat( self.fds.fd(parent) , name , mode ) ; if (rc<0) throw errno ;
			Mount::s_auditor.report_guard( self.report_name(parent,name) , "mkdir" ) ;
			self.reply_entry( req , parent , name ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_mknod( fuse_req_t req , fuse_ino_t parent , const char* name , mode_t mode , dev_t dev ) {
		if (T) ::cerr<<t_thread_key<<" mknod "<<parent<<" "<<name<<" "<<mode<<" "<<dev<<endl ;
		try {
			Mount& self = mk_self(req)                                         ;
			int    rc   = ::mknodat( self.fds.fd(parent) , name , mode , dev ) ; if (rc<0) throw errno ;
			self.reply_entry( req , parent , name ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_open( fuse_req_t req , fuse_ino_t ino , ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" open "<<ino<<endl ;
		Mount& self  = mk_self(req) ;
		int    flags = fi->flags    ;
		//
		Accesses a ;
		bool     w = false ;
		if ( !(flags&O_CREAT) && (flags&O_ACCMODE)!=O_RDONLY ) a |= Access::Stat ;
		if ( (flags&O_ACCMODE)!=O_WRONLY && !(flags&O_TRUNC) ) a |= Access::Reg  ;
		if ( (flags&O_ACCMODE)!=O_RDONLY                     ) w  = true         ;
		self.report_access( ino , {} , a , w , "open" ) ;
		//
		try {
			fi->fh         = ::open( self.fds.proc(ino).c_str() , flags&~O_NOFOLLOW ) ; if (!Fd(fi->fh)) throw errno ;
			fi->keep_cache = true                                                     ;
			::fuse_reply_open(req,fi) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_opendir( fuse_req_t req , fuse_ino_t ino , struct ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" opendir "<<ino<<endl ;
		try {
			AcFd      fd = ::openat( mk_self(req).fds.fd(ino) , "." , O_RDONLY ) ; if (!fd) throw errno  ;
			DirEntry* de = new DirEntry                                          ;
			//
			de->dir           = ::fdopendir(fd) ; if (!de->dir) { delete de ; throw errno ; }
			dir_entry(fi)     = de              ;
			fi->cache_readdir = true            ;
			fd.detach() ;                         // keep fd open if everything is ok
			::fuse_reply_open(req,fi) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_poll( fuse_req_t req , fuse_ino_t , struct ::fuse_file_info* , struct ::fuse_pollhandle* ) {
		::fuse_reply_err(req,ENOSYS) ;
	}

	static void lo_read( fuse_req_t req , fuse_ino_t , size_t sz , off_t offset , ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" read "<<Fd(fi->fh)<<" "<<offset<<" "<<sz<<endl ;
		::fuse_bufvec buf = mk_fuse_bufvec( fi->fh , offset , sz ) ;
		::fuse_reply_data( req , &buf , FUSE_BUF_SPLICE_MOVE ) ;
	}

	template<bool Plus> static void lo_readdir( fuse_req_t req , fuse_ino_t ino , size_t sz , off_t offset , struct ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" readdir "<<ino<<" "<<offset<<" "<<sz<<(Plus?" plus":"")<<endl ;
		vector<char> buf ( sz ) ;
		size_t       pos = 0    ;
		try {
			Mount&    self = mk_self(req)  ;
			DirEntry* de   = dir_entry(fi) ;
			if (offset!=de->offset) {
				::seekdir( de->dir , offset ) ;
				de->offset = offset  ;
				de->entry  = nullptr ;                                                                    // entry is no more valid when offset is updated
			}
			for( size_t pos=0 ; pos<sz ;) {
				if (!de->entry) {
					errno = 0 ;                                                                           // if readdir return nullptr, this is the only way to distinguish error from eof
					de->entry = ::readdir(de->dir) ;
					if (!de->entry) {
						if (errno) throw errno ;
						else       break       ;
					}
				}
				const char*   name    = de->entry->d_name ;
				struct ::stat st                          ; st.st_ino=de->entry->d_ino ; st.st_mode=de->entry->d_type<<12 ;
				off_t         nxt_off = de->entry->d_off  ;
				size_t        old_pos = pos               ;
				if (!Plus) {
					pos += ::fuse_add_direntry     ( req , &buf[pos] , sz-pos , name , &st  , nxt_off ) ;
				} else if ( name[0]=='.' && (!name[1]||(name[1]=='.'&&!name[2])) ) {                      // name is . or ..
					::fuse_entry_param fep ; fep.attr=st ;
					pos += ::fuse_add_direntry_plus( req , &buf[pos] , sz-pos , name , &fep , nxt_off ) ;
				} else {
					::fuse_entry_param fep = self.mk_fuse_entry_param( ino , name ) ;
					pos += ::fuse_add_direntry_plus( req , &buf[pos] , sz-pos , name , &fep , nxt_off ) ;
					if (pos>sz) self.fds.dec_ref(fep.ino) ;                                               // revert mk_fuse_entry_param
				}
				if (pos>sz) {                                                                             // forget last read entry
					pos = old_pos ;
					break ;
				}
				de->offset = nxt_off ;                                                                    // record new state
				de->entry  = nullptr ;                                                                    // entry is no more valid when offset is updated
			}
		} catch(int e) {
			// if there is an error, we can only signal it if we haven't stored any entries yet - otherwise we'd end up with wrong lookup counts for the entries that are already in the buffer
			if (!pos) { ::fuse_reply_err(req,e) ; return ; }
		}
		::fuse_reply_buf( req , buf.data() , pos ) ;
	}

	static void lo_readlink( fuse_req_t req , fuse_ino_t ino ) {
		if (T) ::cerr<<t_thread_key<<" readlink "<<ino<<endl ;
		Mount& self = mk_self(req) ;
		try {
			char buf[PATH_MAX+1] ;
			self.report_dep( ino , Access::Lnk , "readlink" ) ;
			int  len             = ::readlinkat( self.fds.fd(ino) , "" , buf , sizeof(buf) ) ;
			if (       len <0          ) throw errno        ;
			if (size_t(len)>sizeof(buf)) throw ENAMETOOLONG ;
			buf[len] = 0 ;
			::fuse_reply_readlink(req,buf) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_release( fuse_req_t req , fuse_ino_t , ::fuse_file_info* fi ) { // called (after flush) whenever a file description is closed (i.e. when last file descriptor is closed)
		if (T) ::cerr<<t_thread_key<<" release "<<Fd(fi->fh)<<endl ;
		::close(fi->fh) ;
		::fuse_reply_err(req,0) ;
	}

	static void lo_releasedir( fuse_req_t req , fuse_ino_t , struct ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" releasedir "<<endl ;
		DirEntry* de = dir_entry(fi) ;
		::closedir(de->dir) ;
		delete de ;
		::fuse_reply_err(req,0) ;
	}

	static void lo_removexattr( fuse_req_t req , fuse_ino_t ino , const char* name ) {
		if (T) ::cerr<<t_thread_key<<" removexattr "<<ino<<" "<<name<<endl ;
		int rc = ::removexattr( mk_self(req).fds.proc(ino).c_str() , name ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_rename( fuse_req_t req , fuse_ino_t parent , const char* name , fuse_ino_t new_parent , const char* new_name , uint flags ) {
		if (T) ::cerr<<t_thread_key<<" rename "<<parent<<" "<<name<<new_parent<<" "<<new_name<<" "<<flags<<endl ;
		SWEAR(name    ) ;
		SWEAR(new_name) ;
		Mount& self = mk_self(req) ;
		// rename has not occurred yet so :
		// - files are read and unlinked in the source dir
		// - their coresponding files in the destination dir are written
		::vmap_s<FileInfo> reads  ;
		::vmap_s<FileInfo> stats  ;
		::umap_s<FileInfo> unlnks ;                                                                                   // files listed here are read and unlinked
		::vmap_s<FileInfo> writes ;                                                                                   // FileInfo is ignored here, but it is more practical to have it
		auto do1 = [&]( fuse_ino_t parent , const char* name , fuse_ino_t new_parent , const char* new_name )->void {
			Fd       parent_fd     = self.fds.fd(parent    )               ;
			Fd       new_parent_fd = self.fds.fd(new_parent)               ;
			::string pfx           = self.report_name(parent    ,name    ) ;
			::string new_pfx       = self.report_name(new_parent,new_name) ;
			for( ::string const& f : walk(self.fds.fd(parent),name) ) {
				if (self.report_writes    ) unlnks.try_emplace ( pfx    +f , FileInfo(parent_fd    ,name+f    ) ) ;
				else                        reads .emplace_back( pfx    +f , FileInfo(parent_fd    ,name+f    ) ) ;
				if (flags&RENAME_NOREPLACE) stats .emplace_back( new_pfx+f , FileInfo(new_parent_fd,new_name+f) ) ;   // probe existence of destination
				if (self.report_writes    ) writes.emplace_back( new_pfx+f , FileInfo(                        ) ) ;
			}
		} ;
		/**/                       do1( parent     , name     , new_parent , new_name ) ;
		if (flags&RENAME_EXCHANGE) do1( new_parent , new_name , parent     , name     ) ;
		for( ::pair_s<FileInfo> const& w : writes ) {
			auto it = unlnks.find(w.first) ;
			if (it==unlnks.end()) continue ;
			reads.push_back(*it) ;                                                                                    // if a file is read, unlinked and written, it is actually not unlinked
			unlnks.erase(it) ;                                                                                        // .
		}
		//
		int rc        = ::renameat2( self.fds.fd(parent) , name , self.fds.fd(new_parent) , new_name , flags ) ;
		int sav_errno = rc<0 ? errno : 0                                                                       ;
		// record read part in all cases                                                write
		if (+reads ) Mount::s_auditor.report_accesses( ::move(reads)   , DataAccesses , No         , "rename.src"   ) ;
		if (+stats ) Mount::s_auditor.report_accesses( ::move(stats)   , Access::Stat , No         , "rename.probe" ) ;
		if (+unlnks) Mount::s_auditor.report_accesses( mk_vmap(unlnks) , DataAccesses , No|(rc>=0) , "rename.unlnk" ) ;      // unlink if rename did occur
		if (rc>=0) {
			// rename occurred, record both read and write parts
			::uset_s guards ;
			for( ::pair_s<FileInfo> const& w : writes ) guards.insert(dir_name_s(w.first)) ;
			for( auto               const& u : unlnks ) guards.insert(dir_name_s(u.first)) ;
			guards.erase(""s) ;
			for( ::string const& g : guards ) Mount::s_auditor.report_guard( no_slash(g) , "rename.guard" ) ;
			//
			if (+writes) Mount::s_auditor.report_accesses( ::move(writes) , Accesses() , Yes/*write*/ , "rename.dst" ) ;
		}
		::fuse_reply_err(req,sav_errno) ;
	}

	static void lo_retrieve_reply( fuse_req_t req , void* cookie , fuse_ino_t ino , off_t offset , struct ::fuse_bufvec* bufv ) {
		FAIL(req,cookie,ino,offset,bufv) ; // if we do not issue notify_retrieve()'s, we should not receive retrieve_reply()'s.
	}

	static void lo_rmdir( fuse_req_t req , fuse_ino_t parent , const char* name ) {
		if (T) ::cerr<<t_thread_key<<" rmdir "<<parent<<" "<<name<<endl ;
		Mount& self = mk_self(req)                                            ;
		int    rc   = ::unlinkat( self.fds.fd(parent) , name , AT_REMOVEDIR ) ;
		if (rc>=0) Mount::s_auditor.report_guard( self.report_name(parent,name) , "mkdir" ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_setattr( fuse_req_t req , fuse_ino_t ino , struct ::stat* attr , int valid , struct ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" getattr "<<ino<<endl ;
		Mount& self = mk_self(req) ;
		try {
			if ( valid & FUSE_SET_ATTR_MODE ) {
				int rc = ::fchmod( self.fds.fd(ino) , attr->st_mode ) ; if (rc<0) throw errno ;
				self.report_target( ino , "chmod" ) ;
			}
			if ( valid & (FUSE_SET_ATTR_UID|FUSE_SET_ATTR_GID) ) {
				uid_t uid = (valid&FUSE_SET_ATTR_UID) ?  attr->st_uid : -1                      ;
				gid_t gid = (valid&FUSE_SET_ATTR_GID) ?  attr->st_gid : -1                      ;
				int   rc  = ::fchownat( self.fds.fd(ino) , "" , uid , gid , AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW ) ; if (rc<0) throw errno ;
			}
			if ( valid & FUSE_SET_ATTR_SIZE ) {
				if (fi) { int rc = ::ftruncate( fi->fh                     , attr->st_size ) ; if (rc<0) throw errno ; }
				else    { int rc = ::truncate ( self.fds.proc(ino).c_str() , attr->st_size ) ; if (rc<0) throw errno ; }
				self.report_target( ino , "truncate" ) ;
			}
			if ( valid & (FUSE_SET_ATTR_ATIME|FUSE_SET_ATTR_MTIME) ) {
				::timespec tv[2] = {
					{ .tv_sec=0 , .tv_nsec=UTIME_OMIT }
				,	{ .tv_sec=0 , .tv_nsec=UTIME_OMIT }
				} ;
				if      ( valid & FUSE_SET_ATTR_ATIME_NOW ) tv[0].tv_nsec = UTIME_NOW     ;
				else if ( valid & FUSE_SET_ATTR_ATIME     ) tv[0]         = attr->st_atim ;
				if      ( valid & FUSE_SET_ATTR_MTIME_NOW ) tv[1].tv_nsec = UTIME_NOW     ;
				else if ( valid & FUSE_SET_ATTR_MTIME     ) tv[1]         = attr->st_mtim ;
				if (fi) { int rc = ::futimens ( fi->fh   ,                              tv     ) ; if (rc<0) throw errno ; }
				else    { int rc = ::utimensat( AT_FDCWD , self.fds.proc(ino).c_str() , tv , 0 ) ; if (rc<0) throw errno ; }
			}
			lo_getattr( req , ino , fi ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_setlk( fuse_req_t req , fuse_ino_t , struct ::fuse_file_info* , struct ::flock* , int /*sleep*/ ) {
		::fuse_reply_err(req,ENOSYS) ;
	}

	static void lo_setxattr( fuse_req_t req , fuse_ino_t ino , const char* name , const char* val , size_t sz , int flags ) {
		if (T) ::cerr<<t_thread_key<<" setxattr "<<ino<<" "<<name<<" "<<sz<<" "<<flags<<endl ;
		int rc = ::setxattr( mk_self(req).fds.proc(ino).c_str() , name , val , sz , flags ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_statfs( fuse_req_t req , fuse_ino_t ino ) {
		if (T) ::cerr<<t_thread_key<<" statfs "<<ino<<endl ;
		try {
			struct ::statvfs buf ;
			int              rc  = fstatvfs(mk_self(req).fds.fd(ino),&buf) ; if (rc<0) throw errno ;
			::fuse_reply_statfs( req , &buf ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_symlink( fuse_req_t req , const char* link , fuse_ino_t parent , const char* name ) {
		if (T) ::cerr<<t_thread_key<<" symlink "<<parent<<" "<<name<<" "<<link<<endl ;
		try {
			Mount& self = mk_self(req)                                     ;
			int    rc   = ::symlinkat( link , self.fds.fd(parent) , name ) ; if (rc<0) throw errno ;
			self.report_target( parent , name , "symlink" ) ;
			self.reply_entry( req , parent , name ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_unlink( fuse_req_t req , fuse_ino_t parent , const char* name ) {
		if (T) ::cerr<<t_thread_key<<" unlink "<<parent<<" "<<name<<endl ;
		Mount& self = mk_self(req) ;
		int rc = ::unlinkat( self.fds.fd(parent) , name , 0 ) ;
		if (rc>=0) self.report_target( parent , name , "unlink" ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_write( fuse_req_t req , fuse_ino_t , const char* mem , size_t sz , off_t offset , ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" write "<<Fd(fi->fh)<<(void*)mem<<" "<<offset<<" "<<sz<<endl ;
		::fuse_bufvec in_buf = mk_fuse_bufvec( const_cast<char*>(mem) , sz ) ;
		try {
			::fuse_bufvec out_buf = mk_fuse_bufvec( fi->fh , offset , sz )                           ;
			ssize_t       res     = ::fuse_buf_copy( &out_buf , &in_buf , ::fuse_buf_copy_flags(0) ) ; if (res<0) throw -res ;
			::fuse_reply_write( req , size_t(res) ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_write_buf( fuse_req_t req , fuse_ino_t , ::fuse_bufvec* in_buf , off_t offset , ::fuse_file_info* fi ) {
		if (T) ::cerr<<t_thread_key<<" write_buf "<<Fd(fi->fh)<<" "<<offset<<" "<<::fuse_buf_size(in_buf)<<endl ;
		try {
			::fuse_bufvec out_buf = mk_fuse_bufvec( fi->fh , offset , ::fuse_buf_size(in_buf) ) ;
			ssize_t res = ::fuse_buf_copy( &out_buf , in_buf , ::fuse_buf_copy_flags(0) ) ; if (res<0) throw -res ;
			::fuse_reply_write(req, size_t(res)) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static constexpr struct ::fuse_lowlevel_ops FuseOps = {
		.init            = lo_init
	,	.destroy         = lo_destroy
	,	.lookup          = lo_lookup
	,	.forget          = lo_forget
	,	.getattr         = lo_getattr
	,	.setattr         = lo_setattr
	,	.readlink        = lo_readlink
	,	.mknod           = lo_mknod
	,	.mkdir           = lo_mkdir
	,	.unlink          = lo_unlink
	,	.rmdir           = lo_rmdir
	,	.symlink         = lo_symlink
	,	.rename          = lo_rename
	,	.link            = lo_link
	,	.open            = lo_open
	,	.read            = lo_read
	,	.write           = lo_write
	,	.flush           = lo_flush
	,	.release         = lo_release
	,	.fsync           = lo_fsync
	,	.opendir         = lo_opendir
	,	.readdir         = lo_readdir<false/*Plus*/>
	,	.releasedir      = lo_releasedir
	,	.fsyncdir        = lo_fsyncdir
	,	.statfs          = lo_statfs
	,	.setxattr        = lo_setxattr
	,	.getxattr        = lo_getxattr
	,	.listxattr       = lo_listxattr
	,	.removexattr     = lo_removexattr
	,	.access          = lo_access
	,	.create          = lo_create
	,	.getlk           = lo_getlk
	,	.setlk           = lo_setlk
	,	.bmap            = lo_bmap
	,	.ioctl           = lo_ioctl
	,	.poll            = lo_poll
	,	.write_buf       = lo_write_buf
	,	.retrieve_reply  = lo_retrieve_reply
	,	.forget_multi    = lo_forget_multi
	,	.flock           = lo_flock
	,	.fallocate       = lo_fallocate
	,	.readdirplus     = lo_readdir<true/*Plus*/>
	,	.copy_file_range = lo_copy_file_range
	,	.lseek           = lo_lseek
	} ;

	void Mount::open() {
		Trace trace("Mount::open") ;
		const char* argv[] = { "fuse_test" , "-osubtype=passthrough" , "-odefault_permissions" , nullptr } ;

		struct ::fuse_args fas = {
			.argc      = sizeof(argv)/sizeof(argv[0]) - 1 // -1 to account for the terminating null
		,	.argv      = const_cast<char**>(argv)
		,	.allocated = 0
		} ;
		//
		fuse_session* session = ::fuse_session_new  ( &fas , &FuseOps , sizeof(FuseOps) , this ) ; SWEAR(session) ;
		int           rc      = ::fuse_session_mount( session , no_slash(dst_s).c_str()          ) ; SWEAR(rc==0) ;
		_thread = ::jthread( _s_loop , this , session ) ;
		struct ::stat st ;
		::stat( no_slash(dst_s).c_str() , &st ) ;
		_dev = st.st_dev ;
		trace("done",_dev) ;
	}

	Mount::~Mount() {
		Trace trace("~Mount",_dev) ;
		OFStream("/sys/fs/fuse/connections/"s+_dev+"/abort") << 1 ; // there is no other methods to unmount : umount is priviledged, killing thread is not enough
		trace("done") ;
	}

	void Mount::_loop( ::stop_token , fuse_session* session ) {
		t_thread_key = 'F' ;
		Trace trace("Mount::_loop") ;
		::fuse_session_loop(session) ;
		trace("done") ;
	}

}
