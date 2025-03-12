// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "fd.hh"
#include "serialize.hh"

//
// MsgBuf
//

struct MsgBuf {
	friend ::string& operator+=( ::string& , MsgBuf const& ) ;
	using Len = uint32_t ;                                     // /!\ dont use size_t in serialized stream to make serialization interoperable between 32 bits and 64 bits
	// statics
	static Len s_sz(const char* str) {
		Len len = 0 ; ::memcpy( &len , str , sizeof(Len) ) ;
		return len ;
	}
	// data
	::string buf ;                                             // reading : sized after expected size, but actually filled up only with len char's   // writing : contains len+data to be sent
protected :
	Len  _len       = 0     ;                                  // data sent/received so far, reading : may also apply to len accumulated in buf
	bool _data_pass = false ;                                  // reading : if true <=> buf contains partial data, else it contains partial data len // writing : if true <=> buf contains data
} ;
inline ::string& operator+=( ::string& os , MsgBuf const& mb ) { return os<<"MsgBuf("<<mb._len<<','<<mb._data_pass<<')' ; }

struct IMsgBuf : MsgBuf {
	// cxtors & casts
	IMsgBuf() { buf.resize(sizeof(Len)) ; }             // prepare to receive len
	// services
	template<class T> T receive(Fd fd) {
		T res ;
		while (!receive_step(fd,res)) ;
		return res ;
	}
	template<class T> bool/*complete*/ receive_step( Fd fd , T& res ) {
	DataPass :
		ssize_t cnt = ::read( fd , &buf[_len] , buf.size()-_len ) ;
		if      (cnt<0 ) throw "cannot receive over "      +cat(fd)+" : "+::strerror(errno) ;
		else if (cnt==0) throw "peer closed connection on "+cat(fd)                         ;
		_len += cnt ;
		if (_len<buf.size()) return false/*complete*/ ; // buf is still partial
		if (_data_pass) {
			//    vvvvvvvvvvvvvvvvvvv
			res = deserialize<T>(buf) ;
			//    ^^^^^^^^^^^^^^^^^^^
			self = {} ;
			return true/*complete*/ ;
		} else {
			SWEAR( buf.size()==sizeof(Len) , buf.size() ) ;
			Len len = s_sz(buf.data()) ;
			// we now expect the data
			try         { buf.resize(len) ;                               }
			catch (...) { throw "cannot resize message to lenght "s+len ; }
			_data_pass = true ;
			_len       = 0    ;
			goto DataPass ;
		}
	}
} ;

struct OMsgBuf : MsgBuf {
	// cxtors & casts
	OMsgBuf() = default ;
	template<class T> OMsgBuf(T const& x) {
		init(x) ;
	}
	// accesses
	size_t size() const { return buf.size() ; }
	// services
	template<class T> void init(T const& x) {
		SWEAR(!_data_pass) ;
		buf = serialize(::pair<Len,T const&>(0,x)) ; SWEAR(buf.size()>=sizeof(Len)) ; // directly serialize in buf to avoid copy : serialize a pair with length+data
		//
		size_t len_ = buf.size()-sizeof(Len) ;
		Len    len  = len_                   ; SWEAR(len==len_,len_) ;                // ensure truncation is harmless
		::memcpy( buf.data() , &len , sizeof(Len) ) ;                                 // overwrite len
		_data_pass = true ;
	}
	template<class T> void send( Fd fd , T const& x ) {
		init(x) ;
		send(fd) ;
	}
	void send(Fd fd) {
		while (!send_step(fd)) ;
	}
	bool/*complete*/ send_step(Fd fd) {
		SWEAR(_data_pass) ;
		ssize_t cnt = ::write( fd , &buf[_len] , buf.size()-_len ) ;
		if (cnt<=0) { int en=errno ; throw "cannot send over "+cat(fd)+" : "+::strerror(en) ; }
		_len += cnt ;
		return _len==buf.size()/*complete*/ ;
	}
} ;
