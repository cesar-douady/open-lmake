// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// XXX : fuse autodep method is under construction

#include <sys/xattr.h>

#include "disk.hh"
#include "fd.hh"

#include "fuse.hh"

#if ! HAS_FUSE
	#error cannot compile fuse without fuse support
#endif

using namespace Disk ;

namespace Fuse {

	static const ::string ProcFd = "/proc/self/fd" ;

	static Mount& mk_self(fuse_req_t req      ) { return *reinterpret_cast<Mount*>(fuse_req_userdata(req)) ; }
	static Mount& mk_self(void*      user_data) { return *reinterpret_cast<Mount*>(user_data             ) ; }

	::fuse_entry_param Mount::mk_fuse_entry_param( fuse_ino_t parent , const char* name , int flags , mode_t mode , Fd* /*out*/ fd ) {
		if (!(flags&O_CREAT)) SWEAR(!mode,flags,mode) ;
		if (fd              ) *fd = {} ;                                                                               // by default, store an inoffencive value
		::fuse_entry_param res       = {}                                                                            ;
		Fd                 parent_fd = fds.at(parent)                                                                ; if (!parent_fd) throw EBADF ;
		Fd                 this_fd   ;
		int                rc        = ::fstatat( parent_fd , name , &res.attr , AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW ) ; if (rc<0      ) throw errno ;
		if (rc<0) {
			SWEAR(*name) ;
			/**/                                                                                             if (!(flags&O_CREAT)) throw errno ;
			this_fd = ::openat ( parent_fd , name             , flags                             , mode ) ; if (!this_fd        ) throw errno ;
			rc      = ::fstatat( this_fd   , ""   , &res.attr , AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW        ) ; if (rc<0            ) throw errno ;
		}
		//
		auto fd_inserted = fds.inc_ref(res.attr.st_ino) ;
		if      (!fd_inserted.second) SWEAR(!this_fd)                                                      ;
		else if (+this_fd           ) fd_inserted.first = this_fd                                          ;
		else                          fd_inserted.first = ::openat( parent_fd , name , O_PATH|O_NOFOLLOW ) ;
		//
		if (fd) *fd = fd_inserted.first ;
		//
		res.ino           = res.attr.st_ino ;
		res.generation    = 0               ;
		res.attr_timeout  = Infinity        ;
		res.entry_timeout = Infinity        ;
		return res ;
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

	static void lo_create( fuse_req_t req , fuse_ino_t parent , const char* name , mode_t mode , ::fuse_file_info* fi ) {
		try {
			Fd fd ;
			::fuse_entry_param res = mk_self(req).mk_fuse_entry_param( parent , name , (fi->flags|O_CREAT)&~O_NOFOLLOW , mode , &fd ) ;
			fi->fh = fd ;
			::cerr<<t_thread_key<<" create "<<parent<<" "<<name<<" "<<mode<<" "<<fd<<endl ;
			::fuse_reply_create( req , &res , fi ) ;
		} catch(int e) {
			::cerr<<t_thread_key<<" create "<<parent<<" "<<name<<" "<<mode<<" errno:"<<e<<endl ;
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_destroy(void* user_data) {
		::cerr<<t_thread_key<<" destroy"<<endl ;
		Mount& self = mk_self(user_data) ;
		self.fds.clear() ;
	}

	static void lo_flush( fuse_req_t req , fuse_ino_t , struct ::fuse_file_info* fi ) {
		::cerr<<t_thread_key<<" flush "<<Fd(fi->fh)<<endl ;
		int rc = ::close(::dup(fi->fh)) ;                   // mimic example code. this does flush ?!?
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_forget( fuse_req_t req , fuse_ino_t ino , uint64_t n ) {
		::cerr<<t_thread_key<<" forget "<<ino<<" "<<n<<endl ;
		mk_self(req).fds.dec_ref( ino , n );
		::fuse_reply_none(req) ;
	}

	static void lo_forget_multi( fuse_req_t req , size_t cnt , ::fuse_forget_data* forgets ) {
		::cerr<<t_thread_key<<" forget_multi "<<cnt<<endl ;
		Mount& self = mk_self(req) ;
		for ( ::fuse_forget_data const& e : ::vector_view(forgets,cnt) ) self.fds.dec_ref( e.ino , e.nlookup ) ;
		::fuse_reply_none(req) ;
	}

	static void lo_getattr( fuse_req_t req , fuse_ino_t ino , struct ::fuse_file_info* fi ) {
		::cerr<<t_thread_key<<" getattr "<<ino<<endl ;
		try {
			struct ::stat st ;
			int           rc = ::fstatat( fi?fi->fh:mk_self(req).fds.at(ino).fd , "" , &st , AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW ) ; if (rc<0) throw errno ;
			::fuse_reply_attr( req , &st , Infinity ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_getxattr( fuse_req_t req , fuse_ino_t ino , const char* attr , size_t sz ) {
		::cerr<<t_thread_key<<" getxattr "<<ino<<" "<<attr<<" "<<sz<<endl ;
		try {
			::vector<char> buf     ( sz )                                                                           ;
			int            attr_sz = ::fgetxattr( mk_self(req).fds.at(ino).fd , attr , sz?buf.data():nullptr , sz ) ; if (attr_sz< 0) throw errno ;
			if (attr_sz==0) throw 0 ;                                                                                                               // mimic example code from fuse repo
			::fuse_reply_buf( req , buf.data() , attr_sz ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_init( void* user_data , ::fuse_conn_info* /*conn*/ ) {
		Mount& self = mk_self(user_data) ;
		::cerr<<t_thread_key<<" init"<<self.dst_s<<" "<<self.src_s<<endl ;
		self.fds.root = ::open( no_slash(self.src_s).c_str() , O_PATH|O_NOFOLLOW|O_DIRECTORY|O_CLOEXEC ) ;
	}

	static void lo_link( fuse_req_t req , fuse_ino_t ino , fuse_ino_t parent , const char* name ) {
		::cerr<<t_thread_key<<" link "<<parent<<" "<<name<<" "<<ino<<endl ;
		try {
			Mount& self = mk_self(req)                                                                                               ;
			int    rc   = ::linkat( AT_FDCWD , (ProcFd+self.fds.at(ino).fd).c_str() , parent , name , AT_SYMLINK_FOLLOW ) ; if (rc<0) throw errno ;
			self.reply_entry( req , ino , "" ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_lookup( fuse_req_t req , fuse_ino_t parent , const char* name ) {
		::cerr<<t_thread_key<<" lookup "<<parent<<" "<<name<<endl ;
		try {
			mk_self(req).reply_entry( req , parent , name ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_mkdir( fuse_req_t req , fuse_ino_t parent , const char* name , mode_t mode ) {
		::cerr<<t_thread_key<<" mkdir "<<parent<<" "<<name<<" "<<mode<<endl ;
		try {
			Mount& self = mk_self(req)                                   ;
			int    rc   = ::mkdirat( self.fds.at(parent) , name , mode ) ; if (rc<0) throw errno ;
			self.reply_entry( req , parent , name ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_mknod( fuse_req_t req , fuse_ino_t parent , const char* name , mode_t mode , dev_t dev ) {
		::cerr<<t_thread_key<<" mknod "<<parent<<" "<<name<<" "<<mode<<" "<<dev<<endl ;
		try {
			Mount& self = mk_self(req)                                         ;
			int    rc   = ::mknodat( self.fds.at(parent) , name , mode , dev ) ; if (rc<0) throw errno ;
			self.reply_entry( req , parent , name ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_open( fuse_req_t req , fuse_ino_t ino , ::fuse_file_info* fi ) {
		::cerr<<t_thread_key<<" open "<<ino<<" "<<mk_self(req).fds.at(ino)<<endl ;
		try {
			Fd fd = ::open( (ProcFd+mk_self(req).fds.at(ino).fd).c_str() , fi->flags&~O_NOFOLLOW ) ; if (!fd) { int e=errno ; ::cerr<<"open1 "<<e<<endl; throw e ; }
			fi->fh = fd ;
			::fuse_reply_open(req,fi) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_read( fuse_req_t req , fuse_ino_t , size_t sz , off_t offset , ::fuse_file_info* fi ) {
		::cerr<<t_thread_key<<" read "<<Fd(fi->fh)<<offset<<" "<<sz<<endl ;
		::fuse_bufvec buf = mk_fuse_bufvec( fi->fh , offset , sz ) ;
		::fuse_reply_data( req , &buf , FUSE_BUF_SPLICE_MOVE ) ;
	}

	static void lo_release( fuse_req_t req , fuse_ino_t , ::fuse_file_info* fi ) {
		::cerr<<t_thread_key<<" release "<<Fd(fi->fh)<<endl ;
		::close(fi->fh) ;
		::fuse_reply_err(req,0) ;
	}

	static void lo_rename( fuse_req_t req , fuse_ino_t parent , const char* name , fuse_ino_t new_parent , const char* new_name , unsigned int flags ) {
		Mount& self = mk_self(req) ;
		int rc = ::renameat2( self.fds.at(parent) , name , self.fds.at(new_parent) , new_name , flags ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_rmdir( fuse_req_t req , fuse_ino_t parent , const char* name ) {
		::cerr<<t_thread_key<<" rmdir "<<parent<<" "<<name<<endl ;
		int rc = ::unlinkat( mk_self(req).fds.at(parent) , name , AT_REMOVEDIR ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_setattr( fuse_req_t req , fuse_ino_t ino , struct ::stat* attr , int valid , struct ::fuse_file_info* fi ) {
		::cerr<<t_thread_key<<" getattr "<<ino<<endl ;
		Fd fd = mk_self(req).fds.at(ino) ;
		try {
			if ( valid & FUSE_SET_ATTR_MODE ) {
				int rc = ::fchmod( fd , attr->st_mode ) ; if (rc<0) throw errno ;
			}
			if ( valid & (FUSE_SET_ATTR_UID|FUSE_SET_ATTR_GID) ) {
				uid_t uid = (valid&FUSE_SET_ATTR_UID) ?  attr->st_uid : -1                      ;
				gid_t gid = (valid&FUSE_SET_ATTR_GID) ?  attr->st_gid : -1                      ;
				int   rc  = ::fchownat( fd , "" , uid , gid , AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW ) ; if (rc<0) throw errno ;
			}
			if ( valid & FUSE_SET_ATTR_SIZE ) {
				if (fi) { int rc = ::ftruncate( fi->fh                 , attr->st_size ) ; if (rc<0) throw errno ; }
				else    { int rc = ::truncate ( (ProcFd+fd.fd).c_str() , attr->st_size ) ; if (rc<0) throw errno ; }
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
				if (fi) { int rc = ::futimens ( fi->fh   ,                          tv     ) ; if (rc<0) throw errno ; }
				else    { int rc = ::utimensat( AT_FDCWD , (ProcFd+fd.fd).c_str() , tv , 0 ) ; if (rc<0) throw errno ; }
			}
			lo_getattr( req , ino , fi ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_symlink( fuse_req_t req , const char* link , fuse_ino_t parent , const char* name ) {
		::cerr<<t_thread_key<<" symlink "<<parent<<" "<<name<<" "<<link<<endl ;
		try {
			Mount& self = mk_self(req)                                     ;
			int    rc   = ::symlinkat( link , self.fds.at(parent) , name ) ; if (rc<0) throw errno ;
			self.reply_entry( req , parent , name ) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	static void lo_unlink( fuse_req_t req , fuse_ino_t parent , const char* name ) {
		::cerr<<t_thread_key<<" unlink "<<parent<<" "<<name<<endl ;
		int rc = ::unlinkat( mk_self(req).fds.at(parent) , name , 0 ) ;
		::fuse_reply_err(req,rc<0?errno:0) ;
	}

	static void lo_write( fuse_req_t req , fuse_ino_t , const char* mem , size_t sz , off_t offset , ::fuse_file_info* fi ) {
		::cerr<<t_thread_key<<" write "<<Fd(fi->fh)<<(void*)mem<<" "<<offset<<" "<<sz<<endl ;
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
		::cerr<<t_thread_key<<" write_buf "<<Fd(fi->fh)<<" "<<offset<<" "<<::fuse_buf_size(in_buf)<<endl ;
		try {
			::fuse_bufvec out_buf = mk_fuse_bufvec( fi->fh , offset , ::fuse_buf_size(in_buf) ) ;
			ssize_t res = ::fuse_buf_copy( &out_buf , in_buf , ::fuse_buf_copy_flags(0) ) ; if (res<0) throw -res ;
			::fuse_reply_write(req, size_t(res)) ;
		} catch(int e) {
			::fuse_reply_err(req,e) ;
		}
	}

	// to be implemented

	static void lo_readlink( fuse_req_t req , fuse_ino_t ino ) {
		FAIL(req,ino) ;
	}

	static void lo_opendir( fuse_req_t req , fuse_ino_t ino , struct ::fuse_file_info* fi ) {
		FAIL(req,ino,fi) ;
	}

	static void lo_readdir( fuse_req_t req , fuse_ino_t ino , size_t sz , off_t offset , struct ::fuse_file_info* fi ) {
		FAIL(req,ino,sz,offset,fi) ;
	}

	static void lo_readdirplus( fuse_req_t req , fuse_ino_t ino , size_t sz , off_t offset , struct ::fuse_file_info* fi ) {
		FAIL(req,ino,sz,offset,fi) ;
	}

	static void lo_releasedir( fuse_req_t req , fuse_ino_t ino , struct ::fuse_file_info* fi ) {
		FAIL(req,ino,fi) ;
	}

	static void lo_fsyncdir( fuse_req_t req , fuse_ino_t ino , int datasync , struct ::fuse_file_info* fi ) {
		FAIL(req,ino,datasync,fi) ;
	}

	static void lo_fsync( fuse_req_t req , fuse_ino_t ino , int datasync , struct ::fuse_file_info* fi ) {
		FAIL(req,ino,datasync,fi) ;
	}

	static void lo_statfs( fuse_req_t req , fuse_ino_t ino ) {
		FAIL(req,ino) ;
	}

	static void lo_fallocate( fuse_req_t req , fuse_ino_t ino , int mode , off_t offset , off_t length , struct ::fuse_file_info* fi ) {
		FAIL(req,ino,mode,offset,length,fi) ;
	}

	static void lo_flock( fuse_req_t req , fuse_ino_t ino , struct ::fuse_file_info* fi , int op ) {
		FAIL(req,ino,fi,op) ;
	}

	static void lo_listxattr( fuse_req_t req , fuse_ino_t ino , size_t sz ) {
		FAIL(req,ino,sz) ;
	}

	static void lo_setxattr( fuse_req_t req , fuse_ino_t ino , const char* name , const char* val , size_t sz , int flags ) {
		FAIL(req,ino,name,val,sz,flags) ;
	}

	static void lo_removexattr( fuse_req_t req , fuse_ino_t ino , const char* name ) {
		FAIL(req,ino,name) ;
	}

	static void lo_access( fuse_req_t req , fuse_ino_t ino , int mask ) {
		FAIL(req,ino,mask) ;
	}

	static void lo_getlk( fuse_req_t req , fuse_ino_t ino , struct ::fuse_file_info* fi , struct ::flock* lock ) {
		FAIL(req,ino,fi,lock) ;
	}

	static void lo_setlk( fuse_req_t req , fuse_ino_t ino , struct ::fuse_file_info* fi , struct ::flock* lock , int sleep  ) {
		FAIL(req,ino,fi,lock,sleep) ;
	}

	static void lo_bmap( fuse_req_t req , fuse_ino_t ino , size_t blk_sz , uint64_t idx ) {
		FAIL(req,ino,blk_sz,idx) ;
	}

	static void lo_ioctl( fuse_req_t req , fuse_ino_t ino , unsigned int cmd , void *arg , struct ::fuse_file_info* fi , unsigned flags , const void* in_buf , size_t in_sz , size_t out_sz ) {
		FAIL(req,ino,cmd,arg,fi,flags,in_buf,in_sz,out_sz) ;
	}

	static void lo_poll( fuse_req_t req , fuse_ino_t ino , struct ::fuse_file_info* fi , struct ::fuse_pollhandle* ph ) {
		FAIL(req,ino,fi,ph) ;
	}

	static void lo_retrieve_reply( fuse_req_t req , void *cookie , fuse_ino_t ino , off_t offset , struct ::fuse_bufvec *bufv ) {
		FAIL(req,cookie,ino,offset,bufv) ;
	}

	static void lo_copy_file_range(
		fuse_req_t req
	,	fuse_ino_t ino_in  , off_t offset_in  , struct ::fuse_file_info* fi_in
	,	fuse_ino_t ino_out , off_t offset_out , struct ::fuse_file_info* fi_out
	,	size_t     len
	,	int        flags
	) {
		FAIL(req,ino_in,offset_in,fi_in,ino_out,offset_out,fi_out,len,flags) ;
		}

	static void lo_lseek( fuse_req_t req , fuse_ino_t ino , off_t offset , int whence , struct ::fuse_file_info* fi ) {
		FAIL(req,ino,offset,whence,fi) ;
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
	,	.readdir         = lo_readdir
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
	,	.readdirplus     = lo_readdirplus
	,	.copy_file_range = lo_copy_file_range
	,	.lseek           = lo_lseek
	} ;

	void Mount::open() {
		const char* argv[] = { "fuse_test" , "-osubtype=passthrough" , "-odefault_permissions" , nullptr } ;

		struct ::fuse_args fas = {
			.argc      = sizeof(argv)/sizeof(argv[0]) - 1 // -1 to account for the terminating null
		,	.argv      = const_cast<char**>(argv)
		,	.allocated = 0
		} ;
		//
		::cerr<<t_thread_key<<" Mount::open1 "<<src_s<<" "<<dst_s<<endl ;
		/**/ _fuse = ::fuse_session_new  ( &fas , &FuseOps , sizeof(FuseOps) , this ) ; SWEAR(_fuse) ;
		int  rc    = ::fuse_session_mount( _fuse , no_slash(dst_s).c_str()          ) ; SWEAR(rc==0) ;
		::cerr<<t_thread_key<<" Mount::open2 "<<rc<<endl ;
		_thread = ::jthread( _s_loop , this ) ;
		::cerr<<t_thread_key<<" Mount::open3"<<endl ;
	}
	void Mount::close() {
		::cerr<<t_thread_key<<" Mount::close1 "<<no_slash(dst_s)<<" "<<errno<<endl ;
		int rc        = ::umount(no_slash(dst_s).c_str()) ;
		int sav_errno = errno                             ;
		if (rc<0) ::cerr<<t_thread_key<<" Mount::close2 "<<rc<<" "<<sav_errno<<" "<<strerror(sav_errno)<<endl ;
		::cerr<<t_thread_key<<" Mount::close3 "<<no_slash(dst_s)<<" "<<errno<<endl ;
	}

	void Mount::_loop(::stop_token) {
		t_thread_key = 'F' ;
		::cerr<<t_thread_key<<" start loop"<<endl;
		::fuse_session_loop(_fuse) ;
		::cerr<<t_thread_key<<" end loop"<<endl;
	}

}
