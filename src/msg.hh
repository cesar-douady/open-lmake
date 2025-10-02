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
	using Len = uint32_t ;                                       // /!\ dont use size_t in serialized stream to make serialization interoperable between 32 bits and 64 bits
	// statics
	static Len s_sz(const char* str) {
		Len len ; ::memcpy( &len , str , sizeof(Len) ) ;
		return len ;
	}
	// accesses
	bool operator+() const { return +_buf ; }
	// data
protected :
	::string _buf ;                                              // reading : sized after expected size, but actually filled up only with len char's   // writing : contains len+data to be sent
	Len      _len = 0 ;                                          // data sent/received so far, reading : may also apply to len accumulated in _buf
} ;
inline ::string& operator+=( ::string& os , MsgBuf const& mb ) { // START_OF_NO_COV
	return os<<"MsgBuf("<<mb._len<<','<<mb._buf.size()<<')' ;
}                                                                // END_OF_NO_COV

struct IMsgBuf : MsgBuf {
	// cxtors & casts
	IMsgBuf() { _buf.resize(sizeof(Len)) ; }             // prepare to receive len
	// services
	template<class T> T receive(Fd fd) {
		T res ;
		while (!receive_step(fd,res)) ;
		return res ;
	}
	template<class T> bool/*complete*/ receive_step( Fd fd , T& res ) {
	DataPass :
		ssize_t cnt = ::read( fd , &_buf[_len] , _buf.size()-_len ) ;
		if (cnt<=0) throw cat("cannot receive over ",fd," : ", cnt<0?::strerror(errno):"peer closed connection" ) ;
		_len += cnt ;
		if (_len<_buf.size()) return false/*complete*/ ; // _buf is still partial
		if (!_data_pass) {
			SWEAR( _buf.size()==sizeof(Len) , _buf.size() ) ;
			Len len = s_sz(_buf.data()) ;
			// we now expect the data
			try         { _buf.resize(len) ;                                  }
			catch (...) { throw cat("cannot resize message to length ",len) ; }
			_data_pass = true ;
			_len       = 0    ;
			goto DataPass/*BACKWARD*/ ;                  // length is acquired, process data
		} else {
			//    vvvvvvvvvvvvvvvvvvvv
			res = deserialize<T>(_buf) ;
			//    ^^^^^^^^^^^^^^^^^^^^
			self = {} ;
			return true/*complete*/ ;
		}
	}
	// data
	bool _data_pass = false ;                            // if true <=> _buf contains partial data, else it contains partial data len
} ;

struct OMsgBuf : MsgBuf {
	// cxtors & casts
	OMsgBuf() = default ;
	template<class T> OMsgBuf(T const& x) { add(x) ; }
	// accesses
	size_t size() const { return _buf.size() ; }
	// services
	template<class T> void add(T const& x) {
		size_t offset = _buf.size() ;
		serialize( _buf , ::pair<Len,T const&>(0,x) ) ;
		SWEAR(_buf.size()>=offset+sizeof(Len)) ;
		//
		size_t len = _buf.size()-(offset+sizeof(Len)) ; SWEAR(Len(len)==len,len) ; // ensure truncation is harmless
		encode_int( &_buf[offset] , Len(len) ) ;                                   // overwrite len
	}
	template<class T> void send( Fd fd , T const& x ) {
		add(x) ;
		send(fd) ;
	}
	void send(Fd fd) {
		while (!send_step(fd)) ;
	}
	bool/*complete*/ send_step(Fd fd) {
		ssize_t cnt = ::write( fd , &_buf[_len] , _buf.size()-_len ) ;
		if (cnt<=0) throw cat("cannot send over ",fd," : ", cnt<0?::strerror(errno):"peer closed connection" ) ;
		_len += cnt ;
		return _len==_buf.size()/*complete*/ ;
	}
} ;
