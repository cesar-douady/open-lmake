// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <cstdint>

#include "ebpf_client.hh"

#include "ebpf_utils.hh"

int g_log_fd = 2 ;

void vlog( bool with_errno , bool with_nl , const char* fmt , va_list ap ) {
	static constexpr size_t N = 1024 ; // buf size
	int     en        = errno ;
	timeval now_tv    ;         ::gettimeofday(&now_tv,nullptr/*timezone*/)        ;
	tm      now_tm    ;         ::localtime_r(&now_tv.tv_sec,&now_tm)              ;
	char    now_s[32] ;         ::strftime(now_s,sizeof(now_s),"%H:%M:%S",&now_tm) ;
	char    buf  [N ] ;
	size_t  i         = 0     ;
	//
	if (g_log_fd<0) {
		g_log_fd = ::open( EbpfDaemon::get_file_name("log").c_str() , O_WRONLY|O_APPEND|O_CREAT , 0020/*mode*/ ) ;
		if (g_log_fd<0) {
			fprintf(stderr,"cannot open log file : %s\n",::strerror(errno)) ;
			exit(2) ;
		}
	}
	//
	/**/              i += ::snprintf ( buf+i , N-i , "lmake_ebpfd %s.%03d : ",now_s,int(now_tv.tv_usec/1000) ) ; if (i>=N) return ;
	/**/              i += ::vsnprintf( buf+i , N-i , fmt                     ,ap                             ) ; if (i>=N) return ;
	if (with_errno) { i += ::snprintf ( buf+i , N-i , " : %s"                 ,::strerror(en)                 ) ; if (i>=N) return ; }
	if (with_nl   ) { i += ::snprintf ( buf+i , N-i , "\n"                                                    ) ; if (i>=N) return ; }
	::write( g_log_fd , buf , i ) ;
}

void reset_log() {
	::ftruncate( g_log_fd , 0/*length*/ ) ;
}
