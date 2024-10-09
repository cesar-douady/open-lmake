// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// XXX : fuse autodep method is under construction

#include "disk.hh"

#include "fuse.hh"

using namespace Disk ;

int main( int argc , char* argv[] ) {
	t_thread_key = '=' ;
	::cerr<<t_thread_key<<" "<<"start "<<cwd_s()<<endl ;

	SWEAR(argc==2,argc) ;

	Fuse::Mount fm { "a" , "b" } ;

	::sleep(1) ;
	::cerr<<t_thread_key<<" "<<"main1 "<<cwd_s()<<" "<<FileInfo("b")<<endl ;
	struct stat stbuf ;
	int rc = ::lstat("a/x",&stbuf) ;
	::cerr<<t_thread_key<<" "<<"main2"<<" "<<rc<<endl ;
	try         { OFStream("a/x") << argv[1] << endl ;             }
	catch (...) { ::cerr<<t_thread_key<<" "<<"write error"<<endl ; }
	::cerr<<t_thread_key<<" "<<"main3\n";
	try                       { ::cout<<read_content("a/x") ;                    }
	catch (::string const& e) { ::cerr<<t_thread_key<<" "<<"error : "<<e<<endl ; }
	::cerr<<t_thread_key<<" "<<"main4\n";

	return 0 ;
}
