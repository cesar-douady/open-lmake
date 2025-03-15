# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule,PyRule

	import gxx

	lmake.manifest = (
		'Lmakefile.py'
	,	'gxx.py'
	,	'exe.cc'
	)

	class Exe(Rule) :
		targets = { 'EXE' : 'exe'    }
		deps    = { 'SRC' : 'exe.cc' }
		cmd    = f'PATH={gxx.gxx_dir}:$PATH {gxx.gxx} -pthread -o {{EXE}} -std=c++20 {{SRC}}'

	class Dut(PyRule) :
		target = 'dut'
		deps   = { 'EXE' : 'exe' }
		cmd = './{EXE}'

else :

	import ut

	ut.mk_gxx_module('gxx')

	print(r'''
		#include <fcntl.h>
		#include <stdlib.h>
		#include <sys/wait.h>
		#include <time.h>
		#include <unistd.h>
		//
		#include <ctime>
		//
		#include <iomanip>
		#include <iostream>
		#include <sstream>
		#include <thread>
		//
		using namespace std ;

		int g_cnt = 0 ;

		void trace(::string const& msg) {
			static struct timespec s_start    ;
			static bool            s_started  = (::clock_gettime(CLOCK_REALTIME,&s_start),true)                 ;
			static uint64_t        s_start_ms = uint64_t(s_start.tv_sec*1000)+uint64_t(s_start.tv_nsec)/1000000 ;
			//
			struct timespec now ;
			::clock_gettime(CLOCK_REALTIME,&now) ;
			uint64_t now_ms   = uint64_t(now.tv_sec*1000)+uint64_t(now.tv_nsec)/1000000 ;
			uint64_t delta_ms = now_ms - s_start_ms                                     ;
			//
			::stringstream ss ;
			ss << (delta_ms/1000)<<'.'<<::setfill('0')<<::setw(3)<<(delta_ms%1000) <<' '<< ::setfill(' ')<<::setw(7)<<g_cnt <<" : "<< msg <<'\n' ;
			::cout << ss.str() << ::flush ;
		}

		void thread_func() {
			trace("thread1") ;
			for( ; g_cnt<1000000 ; g_cnt++ ) {
				::close(::open("Lmakefile.py",O_RDONLY)) ;
				if ((g_cnt%100000)==0) trace("thread2") ;
			}
			trace("thread3") ;
		}
		int main() {
			trace("main1") ;
			{	::jthread jt ;
				for( int i=0 ; i<300 ; i++ )
					if ( pid_t pid= i%2 ? ::vfork() : ::fork() ; pid==0 ) {
						::close(::open("Lmakefile.py",O_RDONLY)) ;
						if ((i%30)==0) trace("child1") ;
						_exit(0) ;
					} else {
						if (i==1     ) jt = ::jthread(thread_func) ;
						if ((i%30)==0) trace("parent1") ;
						::waitpid(pid,nullptr,0) ;
					}
			}
			trace("main2") ;
		}
	''',file=open('exe.cc','w'))

	ut.lmake( 'dut' , done=2 , new=2 ) # check target does not block when accessing file in vforked process while another thread does accesses
