// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "fd.hh"

#include "fuse.hh"

using namespace Disk ;

namespace Fuse {

	#if HAS_FUSE

		::string pfx = "/home/cdy/open-lmake/b" ;
		static int xmp_getattr( const char* path , struct stat* stbuf , struct fuse_file_info* ffi ) {
			::cerr<<t_thread_key<<" "<<"getattr "<<path<<" "<<stbuf<<" "<<ffi<<endl;
			int res = ::lstat((pfx+path).c_str(),stbuf) ;
			if (res<0) return -errno ;
			else       return 0      ;
		}

		static int xmp_readlink( const char* path , char* buf , size_t size ) {
			::cerr<<t_thread_key<<" "<<"readlink "<<path<<endl;
			int res = ::readlink( (pfx+path).c_str() , buf , size-1 ) ;
			if (res<0)                     return -errno ;
			else       { buf[res] = '\0' ; return 0      ; }
		}

		static int xmp_open(const char* path , struct fuse_file_info *ffi ) {
			int res = ::open( (pfx+path).c_str() , ffi->flags ) ;
			::cerr<<t_thread_key<<" "<<"open "<<path<<" "<<ffi<<" "<<res<<endl;
			if (res<0)                   return -errno ;
			else       { ffi->fh = res ; return 0      ; }
		}

		static int xmp_read( const char* path , char* buf , size_t size , off_t offset , struct fuse_file_info* ffi ) {
			SWEAR(ffi) ;
			::cerr<<t_thread_key<<" "<<"read1 "<<ffi->fh<<" "<<(path?path:"<null>")<<" "<<size<<" "<<offset<<endl ;
			Fd  fd     = ffi->fh ; SWEAR(+fd)        ;
			int res    = ::pread(fd,buf,size,offset) ;
			int errno_ = errno                       ;       // save before potential overwrite
			::cerr<<t_thread_key<<" "<<"read2 "<<res<<endl ;
			//
			if (res<0) return -errno_ ;
			else       return res     ;
		}

		static int xmp_write( const char* path , const char* buf , size_t size , off_t offset , struct fuse_file_info* ffi ) {
			SWEAR(ffi) ;
			::cerr<<t_thread_key<<" "<<"write1 "<<ffi->fh<<" "<<(path?path:"<null>")<<" "<<size<<" "<<offset<<endl ;
			Fd  fd     = ffi->fh ; SWEAR(+fd)         ;
			int res    = ::pwrite(fd,buf,size,offset) ;
			int errno_ = errno                        ;       // save before potential overwrite
			::cerr<<t_thread_key<<" "<<"write2 "<<res<<endl ;
			//
			if (res<0) return -errno_ ;
			else       return res     ;
		}

		static void* xmp_init( struct fuse_conn_info* , struct fuse_config* cfg ) {
			::cerr<<t_thread_key<<" "<<"init"<<endl;
			cfg->use_ino          = 1 ;
			cfg->direct_io        = 1 ;
			cfg->entry_timeout    = 0 ;
			cfg->attr_timeout     = 0 ;
			cfg->negative_timeout = 0 ;
			return nullptr ;
		}

		static constexpr struct fuse_operations FuseOps = {
			.getattr         = xmp_getattr
		,	.readlink        = xmp_readlink
		,	.mknod           = nullptr      // xmp_mknod
		,	.mkdir           = nullptr      // xmp_mkdir
		,	.unlink          = nullptr      // xmp_unlink
		,	.rmdir           = nullptr      // xmp_rmdir
		,	.symlink         = nullptr      // xmp_symlink
		,	.rename          = nullptr      // xmp_rename
		,	.link            = nullptr      // xmp_link
		,	.chmod           = nullptr      // xmp_chmod
		,	.chown           = nullptr      // xmp_chown
		,	.truncate        = nullptr      // xmp_truncate
		,	.open            = xmp_open
		,	.read            = xmp_read
		,	.write           = xmp_write
		,	.statfs          = nullptr      // xmp_statfs
		,	.flush           = nullptr
		,	.release         = nullptr      // xmp_release
		,	.fsync           = nullptr      // xmp_fsync
		,	.setxattr        = nullptr      // xmp_setxattr
		,	.getxattr        = nullptr      // xmp_getxattr
		,	.listxattr       = nullptr      // xmp_listxattr
		,	.removexattr     = nullptr      // xmp_removexattr
		,	.opendir         = nullptr
		,	.readdir         = nullptr      // xmp_readdir
		,	.releasedir      = nullptr
		,	.fsyncdir        = nullptr
		,	.init            = xmp_init
		,	.destroy         = nullptr
		,	.access          = nullptr      // xmp_access
		,	.create          = nullptr      // xmp_create
		,	.lock            = nullptr
		,	.utimens         = nullptr      // xmp_utimens
		,	.bmap            = nullptr
		,	.ioctl           = nullptr
		,	.poll            = nullptr
		,	.write_buf       = nullptr
		,	.read_buf        = nullptr
		,	.flock           = nullptr
		,	.fallocate       = nullptr      // xmp_fallocate
		,	.copy_file_range = nullptr      // xmp_copy_file_range
		,	.lseek           = nullptr      // xmp_lseek
		} ;

		void Mount::open() {
			const char* argv[3] { "fuse_test" , "-osubtype=passthrough" , nullptr } ;

			struct fuse_args fuse_args = {
				.argc      = 2
			,	.argv      = const_cast<char**>(argv)
			,	.allocated = 0
			} ;
			//
			::cerr<<t_thread_key<<" "<<"Mount::open1 "<<src_s<<" "<<dst_s<<endl ;
			/**/ _fuse = fuse_new( &fuse_args , &FuseOps , sizeof(FuseOps) , nullptr/*user_data*/ ) ; SWEAR(_fuse) ;
			int  rc    = fuse_mount( _fuse , no_slash(dst_s).c_str() )                              ; SWEAR(rc==0) ;
			::cerr<<t_thread_key<<" "<<"Mount::open2 "<<rc<<endl ;
		}
		void Mount::close() {
			::cerr<<t_thread_key<<"Mount::close "<<no_slash(dst_s)<<" "<<errno<<endl ;
			int rc = ::umount(no_slash(dst_s).c_str()) ;
			if (rc<0) ::cerr<<t_thread_key<<" "<<"Mount::close error "<<rc<<" "<<errno<<" "<<strerror(errno)<<endl ;
		}

		void Mount::_loop(::stop_token) {
			::cerr<<t_thread_key<<" "<<"start loop"<<endl;
			::fuse_loop(_fuse) ;
			::cerr<<t_thread_key<<" "<<"end loop"<<endl;
		}

	#endif

}
