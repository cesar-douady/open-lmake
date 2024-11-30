// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <fstream>
#include <iostream>
#include <sstream>

#include "utils.hh"

template<class Stream> struct FakeStream : Stream {
	struct Buf : ::streambuf {
		int underflow(   ) { return EOF ; }
		int overflow (int) { return EOF ; }
		int sync     (   ) { return 0   ; }
	} ;
	// cxtors & casts
	FakeStream() : Stream{&_buf} {}
	// data
protected :
	Buf _buf ;
} ;
using OFakeStream = FakeStream<::ostream> ;
using IFakeStream = FakeStream<::istream> ;

inline void _set_cloexec(::filebuf* fb) {
	int fd = np_get_fd(*fb) ;
	if (fd>=0) ::fcntl(fd,F_SETFD,FD_CLOEXEC) ;
}
inline void sanitize(::ostream& os) {
	os.exceptions(~os.goodbit) ;
	os<<::left<<::boolalpha ;
}
struct OFStream : ::ofstream {
	using Base = ::ofstream ;
	// cxtors & casts
	OFStream (                                                                     ) : Base{           } { sanitize(self) ;                         }
	OFStream ( ::string const& f , ::ios_base::openmode om=::ios::out|::ios::trunc ) : Base{f,om       } { sanitize(self) ; _set_cloexec(rdbuf()) ; }
	OFStream ( OFStream&& ofs                                                      ) : Base{::move(ofs)} {                                          }
	~OFStream(                                                                     )                     {                                          }
	//
	OFStream& operator=(OFStream&& ofs) { Base::operator=(::move(ofs)) ; return self ; }
	// services
	template<class... A> void open(A&&... args) { Base::open(::forward<A>(args)...) ; _set_cloexec(rdbuf()) ; }
} ;
struct IFStream : ::ifstream {
	using Base = ::ifstream ;
	// cxtors & casts
	IFStream(                                 ) : Base{    } { exceptions(~goodbit) ; _set_cloexec(rdbuf()) ; }
	IFStream( ::string const& f               ) : Base{f   } { exceptions(~goodbit) ; _set_cloexec(rdbuf()) ; }
	IFStream( ::string const& f , openmode om ) : Base{f,om} { exceptions(~goodbit) ; _set_cloexec(rdbuf()) ; }
	//
	IFStream& operator=(IFStream&& ifs) { Base::operator=(::move(ifs)) ; return self ; }
	// services
	template<class... A> void open(A&&... args) { Base::open(::forward<A>(args)...) ; _set_cloexec(rdbuf()) ; }
} ;

struct OStringStream : ::ostringstream {
	OStringStream() : ::ostringstream{} { sanitize(self) ; }
} ;
struct IStringStream : ::istringstream {
	IStringStream(::string const& s) : ::istringstream{s} { exceptions(~goodbit) ; }
} ;

template<class... A> ::string fmt_string(A const&... args) {
	OStringStream res ;
	[[maybe_unused]] bool _[] = { false , (res<<args,false)... } ;
	return ::move(res).str() ;
}

