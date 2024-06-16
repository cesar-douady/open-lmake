// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "fuse.hh"

namespace Fuse {

	#if HAS_FUSE

		static constexpr struct fuse_operations FuseOps = {
			.getattr         = nullptr // xmp_getattr
		,	.readlink        = nullptr // xmp_readlink
		,	.mknod           = nullptr // xmp_mknod
		,	.mkdir           = nullptr // xmp_mkdir
		,	.unlink          = nullptr // xmp_unlink
		,	.rmdir           = nullptr // xmp_rmdir
		,	.symlink         = nullptr // xmp_symlink
		,	.rename          = nullptr // xmp_rename
		,	.link            = nullptr // xmp_link
		,	.chmod           = nullptr // xmp_chmod
		,	.chown           = nullptr // xmp_chown
		,	.truncate        = nullptr // xmp_truncate
		,	.open            = nullptr // xmp_open
		,	.read            = nullptr // xmp_read
		,	.write           = nullptr // xmp_write
		,	.statfs          = nullptr // xmp_statfs
		,	.flush           = nullptr
		,	.release         = nullptr // xmp_release
		,	.fsync           = nullptr // xmp_fsync
		,	.setxattr        = nullptr // xmp_setxattr
		,	.getxattr        = nullptr // xmp_getxattr
		,	.listxattr       = nullptr // xmp_listxattr
		,	.removexattr     = nullptr // xmp_removexattr
		,	.opendir         = nullptr
		,	.readdir         = nullptr // xmp_readdir
		,	.releasedir      = nullptr
		,	.fsyncdir        = nullptr
		,	.init            = nullptr // xmp_init
		,	.destroy         = nullptr
		,	.access          = nullptr // xmp_access
		,	.create          = nullptr // xmp_create
		,	.lock            = nullptr
		,	.utimens         = nullptr // xmp_utimens
		,	.bmap            = nullptr
		,	.ioctl           = nullptr
		,	.poll            = nullptr
		,	.write_buf       = nullptr
		,	.read_buf        = nullptr
		,	.flock           = nullptr
		,	.fallocate       = nullptr // xmp_fallocate
		,	.copy_file_range = nullptr // xmp_copy_file_range
		,	.lseek           = nullptr // xmp_lseek
		} ;

		void Mount::open() {
			throw "fuse not yet implemented"s ;
			//
			char* empty = nullptr ;
			struct fuse_args fuse_args = {
				.argc      = 0
			,	.argv      = &empty
			,	.allocated = 0
			} ;

			/**/ _fuse = fuse_new( &fuse_args , &FuseOps , sizeof(FuseOps) , nullptr/*user_data*/ ) ; SWEAR(_fuse) ;
			int  rc    = fuse_mount( _fuse , dst.c_str() )                                          ; SWEAR(rc==0) ;
		}

		void Mount::_loop(::stop_token /*stop*/) {
			fuse_loop(_fuse) ;
		}

	#endif

}
