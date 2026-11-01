// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// do not include "utils.hh" nor any other lmake .hh so as to keep ebpf daemon very simple to analyze

#ifndef SYSCALL_TAB_H
#define SYSCALL_TAB_H

// -1 means arg not present (for nofollow, it means info is in arg flags)
//  X( name              , kind      ,dfd1,path1,dfd2,path2, fd ,mode,flags,mask,nofollow)
#define EBPF_TRACKED_SYSCALLS(X) \
	X( access            , STAT      , -1 , 0   , -1 ,-1   , -1 ,  1 , -1  , -1 ,  0     ) \
	X( faccessat         , STAT      ,  0 , 1   , -1 ,-1   , -1 ,  2 , -1  , -1 ,  0     ) /*flags is not implemented for faccessat (cf man 2 faccessat)*/ \
	X( faccessat2        , STAT      ,  0 , 1   , -1 ,-1   , -1 ,  2 ,  3  , -1 , -1     ) \
	X( chdir             , PATH      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( chmod             , UPDATE    , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( fchmodat          , UPDATE    ,  0 , 1   , -1 ,-1   , -1 , -1 , -1  , -1 , -1     ) /*flags is not implemented for fchmodat (cf man 2 fchmod)*/     \
	X( chroot            , FORBIDDEN , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 , -1     ) \
	X( creat             , OPEN      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( execve            , OPEN      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( execveat          , OPEN      ,  0 , 1   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( getdents          , READDIR   , -1 ,-1   , -1 ,-1   ,  0 , -1 , -1  , -1 , -1     ) \
	X( getdents64        , READDIR   , -1 ,-1   , -1 ,-1   ,  0 , -1 , -1  , -1 , -1     ) \
	X( link              , LINK      , -1 , 0   , -1 , 1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( linkat            , LINK      ,  0 , 1   ,  2 , 3   , -1 , -1 ,  4  , -1 , -1     ) \
	X( mkdir             , CREATE    , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( mkdirat           , CREATE    ,  0 , 1   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( mknod             , CREATE    , -1 , 0   , -1 ,-1   , -1 ,  1 , -1  , -1 ,  1     ) \
	X( mknodat           , CREATE    ,  0 , 1   , -1 ,-1   , -1 ,  2 , -1  , -1 ,  1     ) \
	X( mount             , FORBIDDEN , -1 , 1   , -1 ,-1   , -1 , -1 , -1  , -1 , -1     ) \
	X( move_mount        , FORBIDDEN ,  0 , 1   ,  2 , 3   , -1 , -1 , -1  , -1 , -1     ) \
	X( name_to_handle_at , PATH      ,  0 , 1   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( open              , OPEN      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 , -1     ) \
	X( openat            , OPEN      ,  0 , 1   , -1 ,-1   , -1 , -1 , -1  , -1 , -1     ) \
	X( openat2           , OPEN      ,  0 , 1   , -1 ,-1   , -1 , -1 , -1  , -1 , -1     ) \
	X( open_tree         , FORBIDDEN ,  0 , 1   , -1 ,-1   , -1 , -1 ,  2  , -1 , -1     ) \
	X( open_tree_attr    , FORBIDDEN ,  0 , 1   , -1 ,-1   , -1 , -1 ,  2  , -1 , -1     ) \
	X( readdir           , READDIR   , -1 ,-1   , -1 ,-1   ,  0 , -1 , -1  , -1 , -1     ) \
	X( readlink          , READLINK  , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( readlinkat        , READLINK  ,  0 , 1   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( rename            , RENAME    , -1 , 0   , -1 , 1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( renameat          , RENAME    ,  0 , 1   ,  2 , 3   , -1 , -1 , -1  , -1 ,  1     ) \
	X( renameat2         , RENAME    ,  0 , 1   ,  2 , 3   , -1 , -1 ,  3  , -1 ,  1     ) \
	X( rmdir             , PATH      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( stat              , STAT      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( stat64            , STAT      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( fstatat64         , STAT      ,  0 , 1   , -1 ,-1   , -1 , -1 ,  3  , -1 , -1     ) \
	X( lstat             , STAT      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( lstat64           , STAT      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( statx             , STAT      ,  0 , 1   , -1 ,-1   , -1 , -1 ,  2  ,  3 , -1     ) \
	X( newfstatat        , STAT      ,  0 , 1   , -1 ,-1   , -1 , -1 ,  3  , -1 , -1     ) \
	X( oldstat           , STAT      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( oldlstat          , STAT      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( statfs            , PATH      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( statfs64          , PATH      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( symlink           , CREATE    , -1 , 1   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( symlinkat         , CREATE    ,  1 , 2   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( truncate          , UPDATE    , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( umount            , FORBIDDEN , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( umount2           , FORBIDDEN , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( unlink            , UNLINK    , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  1     ) \
	X( unlinkat          , UNLINK    ,  0 , 1   , -1 ,-1   , -1 , -1 ,  2  , -1 ,  1     ) \
	X( utime             , PATH      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( utimes            , PATH      , -1 , 0   , -1 ,-1   , -1 , -1 , -1  , -1 ,  0     ) \
	X( utimensat         , PATH      ,  0 , 1   , -1 ,-1   , -1 , -1 ,  3  , -1 , -1     )
// syscalls of SyscallDescr::s_tab that need no tracking with ebpf : cwd is captured when needed and io_uring are not forbidden (its operations are tracked)
#define EBPF_UNTRACKED_SYSCALLS(X) \
	X(fchdir           ) \
	X(io_uring_enter   ) \
	X(io_uring_register) \
	X(io_uring_setup   )

#endif
