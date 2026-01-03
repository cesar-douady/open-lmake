// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
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
	using Len = uint32_t    ;                                    // /!\ dont use size_t in serialized stream to make serialization interoperable between 32 bits and 64 bits
	using Key = SockFd::Key ;
	// statics
	static Len s_sz(const char* str) {
		Len len ; ::memcpy( &len , str , sizeof(Len) ) ;
		return len ;
	}
	// cxtors & casts
	MsgBuf() = default ;                                         // suppress aggregate cxtors
	// accesses
	bool   operator+() const { return +_buf       ; }
	size_t size     () const { return _buf.size() ; }
	// data
protected :
	::string _buf = {} ;
} ;
inline ::string& operator+=( ::string& os , MsgBuf const& mb ) { // START_OF_NO_COV
	return os<<"MsgBuf("<<mb._buf.size()<<')' ;
}                                                                // END_OF_NO_COV

struct IMsgBuf : MsgBuf {
	// services
	template<class T> T receive( SockFd&&         fd , Bool3 once                   ) { return receive<T>( fd , once , fd.key ) ; } // Maybe means read a single entry but ok to read more
	template<class T> T receive( SockFd&/*inout*/ fd , Bool3 once                   ) { return receive<T>( fd , once , fd.key ) ; } // .
	template<class T> T receive( Fd               fd , Bool3 once , Key&&         k ) { return receive<T>( fd , once , k      ) ; } // .
	template<class T> T receive( Fd               fd , Bool3 once , Key&/*inout*/ k ) {                                             // .
		switch (once) {
			case No    : for(;;) if ( ::optional<T> x=receive_step<T>(fd,Yes            ,k) ; +x ) return ::move(*x) ;
			case Maybe : for(;;) if ( ::optional<T> x=receive_step<T>(fd,Maybe|!_msg_len,k) ; +x ) return ::move(*x) ;              // no need to read past message
			case Yes   : for(;;) if ( ::optional<T> x=receive_step<T>(fd,Maybe          ,k) ; +x ) return ::move(*x) ;
		DF} // NO_COV
	}
	template<class T> ::optional<T> receive_step( SockFd&&         fd , Bool3 fetch                   ) { return _receive_step<T>( fd , fetch , fd.key ) ; } // Maybe means read only necessary bytes
	template<class T> ::optional<T> receive_step( SockFd&/*inout*/ fd , Bool3 fetch                   ) { return _receive_step<T>( fd , fetch , fd.key ) ; } // .
	template<class T> ::optional<T> receive_step( Fd               fd , Bool3 fetch , Key&&         k ) { return _receive_step<T>( fd , fetch , k      ) ; } // .
	template<class T> ::optional<T> receive_step( Fd               fd , Bool3 fetch , Key&/*inout*/ k ) { return _receive_step<T>( fd , fetch , k      ) ; } // .
	//
private :
	template<class T> ::optional<T> _receive_step( Fd fd , Bool3 fetch , Key&/*inout*/ key ) {  // Maybe means read only necessary bytes
		static constexpr Len ChunkSz = 4096 ;
		//
		::optional<T> res      ;
		bool          can_read = fetch!=No ;
		//
		auto advance = [&](Len sz) {
			Len end = _msg_start+sz ;
			if      (_buf_sz>=end) return true  ;
			else if (!can_read   ) return false ;
			//
			Len new_sz = end + (fetch==Yes?ChunkSz:0) ;                                         // if fetch==Maybe, read only necessary bytes
			if (_buf.size()<new_sz) _buf.resize(new_sz) ;                                       // ensure logical size fits within physical one
			ssize_t cnt = ::read( fd , &_buf[_buf_sz] , new_sz-_buf_sz ) ;
			if (cnt<=0) {
				if (cnt<0)
					switch (errno) {
						#if EWOULDBLOCK!=EAGAIN
							case EWOULDBLOCK :
						#endif
						case EAGAIN     :
						case EINTR      : return false                                        ;
						case ECONNRESET : break                                               ; // if peer dies abruptly, we get ECONNRESET, but this is equivalent to eof
						default         : throw cat("cannot receive over ",fd," : ",StrErr()) ;
					}
				res = T() ;                                                                     // return empty on eof, even if not at a message boundary
				return false ;
			}
			_buf_sz  += cnt   ;
			can_read  = false ;                                                                 // when used with epoll, we are only sure of a single non-blocking read
			return _buf_sz>=end ;
		} ;
		if (_msg_len==0) {
			if ( !advance( (key?sizeof(Key):0) + sizeof(Len) ) ) return res ;                   // waiting header
			if (key) {                                                                          // check key
				Key rk = decode_int<Key>(&_buf[_msg_start]) ; if (rk!=key) return T() ;         // this connection is not for us, pretend it was closed immediately
				key = {} ;                                                                      // key has been checked, dont process it again
				_msg_start += sizeof(Key) ;
			}
			_msg_len    = decode_int<Len>(&_buf[_msg_start]) ; SWEAR( _msg_len , fetch,can_read,_msg_start,_buf_sz ) ;
			_msg_start += sizeof(Len)                        ;
		} else {
			SWEAR( !key , key,_msg_len ) ;                                                      // cannot receive key while expecting a message
		}
		if (!advance(_msg_len)) return res ;                                                    // waiting data
		::string_view bv = substr_view(_buf,_msg_start,_msg_len) ;
		//    vvvvvvvvvvvvvvvvvv
		res = deserialize<T>(bv) ;                                                              // make res optional to avoid moving when return
		//    ^^^^^^^^^^^^^^^^^^
		SWEAR( !bv , _msg_start,_msg_len,bv.size() ) ;                                          // check lengths consistency
		_msg_start += _msg_len ;
		_msg_len    = 0        ;
		if (_buf_sz-_msg_start <= _msg_start ) {                                                // suppress old messages, but only move data once in a while and ensure no overlap for memcpy
			_buf_sz -= _msg_start ;
			::memcpy( &_buf[0] , &_buf[_msg_start] , _buf_sz ) ;
			_msg_start = 0 ;
		}
		return res ;
	}
	// data
	Len _msg_start = 0 ;                                                                        // start of next message to return
	Len _msg_len   = 0 ;                                                                        // if 0, message len is not yet processed
	Len _buf_sz    = 0 ;                                                                        // logical buf size
} ;

struct OMsgBuf : MsgBuf {
	// cxtors & casts
	/**/              OMsgBuf(          )             { _buf.resize(sizeof(Key)) ; }
	template<class T> OMsgBuf(T const& x) : OMsgBuf{} { add(x)                   ; }
	// accesses
	bool operator+() const { return _buf.size()>sizeof(Key) ; }
	// services                                                                          Serialize
	template<class T> void add           ( T        const& x                     ) { _add<true   >(x)         ; }
	/**/              void add_serialized( ::string const& s                     ) { _add<false  >(s)         ; }
	/**/              void send          ( SockFd&&         fd                   ) { send( fd , fd.key )      ; }
	/**/              void send          ( SockFd&/*inout*/ fd                   ) { send( fd , fd.key )      ; }
	/**/              void send          ( Fd               fd , Key&&         k ) { send( fd , k      )      ; }
	/**/              void send          ( Fd               fd , Key&/*inout*/ k ) { while (!send_step(fd,k)) ; }
	//
	bool/*complete*/ send_step( SockFd&&         fd                     ) { return send_step( fd , fd.key ) ; }
	bool/*complete*/ send_step( SockFd&/*inout*/ fd                     ) { return send_step( fd , fd.key ) ; }
	bool/*complete*/ send_step( Fd               fd , Key&&         k   ) { return send_step( fd , k      ) ; }
	bool/*complete*/ send_step( Fd               fd , Key&/*inout*/ key ) {                                     // key is only used on first call
		if (key) {
			SWEAR( !_pos , fd,_pos,key ) ;
			encode_int( &_buf[0] , key ) ;                                                                      // overwrite key
			key = {} ;
		} else {
			_pos = sizeof(Key) ;
		}
		ssize_t cnt = ::write( fd , &_buf[_pos] , _buf.size()-_pos ) ;
		throw_unless( cnt>=0 , "cannot send over ",fd," : ", StrErr()             ) ;
		throw_unless( cnt> 0 , "cannot send over ",fd," : peer closed connection" ) ;
		_pos += cnt ;
		return _pos==_buf.size()/*complete*/ ;
	}
private :
	template<bool Serialize,class T> void _add(T const& x) {
		size_t offset = _buf.size() ;
		//
		/**/                     serialize( _buf , Len(0) ) ;
		if constexpr (Serialize) serialize( _buf , x      ) ;
		else                     _buf += x                  ;
		//
		SWEAR( _buf.size()>=offset+sizeof(Len) ) ;
		size_t l = _buf.size()-(offset+sizeof(Len)) ; SWEAR( Len(l)==l , l ) ;                                  // ensure truncation is harmless
		encode_int( &_buf[offset] , Len(l) ) ;                                                                  // overwrite _len
	}
	// data
	Len _pos = 0 ;
} ;
