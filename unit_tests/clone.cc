// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <fcntl.h>
#include <sched.h>
#include <sys/wait.h>
#include <time.h>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <stop_token>
#include <thread>

using namespace std ;

void trace( ::string const& msg , int i=0 ) {
	struct ::timespec now ;
	::clock_gettime(CLOCK_REALTIME,&now) ;
	::ostringstream line ;
	if (i) line << i <<' ' ;
	line << msg<<" : "<<(now.tv_sec%10)<<'.'<<(now.tv_nsec/1000000)<<'\n' ;
	::cout << ::move(line).str() << flush ;
}

void crazy_open( ::stop_token stop , int i ) {
	while (!stop.stop_requested()) {
		for( int i=0 ; i<10 ; i++ ) if (::open("dont_exist",O_RDONLY)!=-1) { ::cerr<<"file dont_exist exists"<<endl; return ; }
	}
	trace("in crazy",i) ;
}

int dut(void*) {
	trace("in child before") ;
	::open("dep",O_RDONLY) ;   // access a file : the goal is to ensure that this triggers a dep
	trace("in child after") ;
	return 0 ;
}

int main() {
	::jthread t1 { crazy_open , 1 } ;                        // ensure there is always an open on going
	::jthread t2 { crazy_open , 2 } ;                        // .
	::jthread t3 { crazy_open , 3 } ;                        // .
	trace("in parent step1") ;
	::this_thread::sleep_for(10ms) ;                         // ensure threads are running
	trace("in parent step2") ;
	static constexpr size_t StackSz = 1<<16 ;
	char* stack = new char[StackSz] ;
	pid_t pid = ::clone(dut,stack+StackSz,SIGCHLD,nullptr) ; // ensure autodep resists to clone in a multi-threaded context
	trace("in parent step3") ;
	::waitpid(pid,nullptr,0) ;
	trace("in parent step4") ;
}
