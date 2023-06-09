// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

#define STR(x) Trace::str((x),#x)

extern ::string* g_trace_file ;     // pointer to avoid init/fini order hazards, relative to admin dir

struct Trace {
	// init
	static void s_init() ;
	// statics
	static void s_start         (               ) ;
	static void s_new_trace_file(::string const&) ;
private :
	static void _s_open() ;
	//
public :
	template<class T> static ::string str( T const& v , ::string const& s ) { return s+"="+to_string(v) ; }
	/**/              static ::string str( bool     v , ::string const& s ) { return v ? s : '!'+s      ; }
	/**/              static ::string str( uint8_t  v , ::string const& s ) { return str(int(v),s)      ; } // avoid confusion with char
	/**/              static ::string str( int8_t   v , ::string const& s ) { return str(int(v),s)      ; } // avoid confusion with char
	// static data
	static thread_local char t_key  ;
	static thread_local int  t_lvl  ;
	static thread_local bool t_hide ;                      // if true <=> do not generate trace
private :
	static thread_local OStringStream* _t_buf ;            // pointer to avoid init/fini order hazards
	//
public :
	static size_t           s_pos          ;               // current line number
	static ::atomic<size_t> s_sz           ;               // max number of lines
	static bool             s_ping         ;               // ping-pong to distinguish where trace stops in the middle of a trace
	static bool             s_backup_trace ;
private :
	static Fd      _s_fd    ;
	static ::mutex _s_mutex ;
	// cxtors & casts
public :
	Trace() : _hidden{t_hide} {
		t_lvl++ ;
	}
	template<class T,class... Ts> Trace( T const& first_arg , Ts const&... args ) : Trace{} {
		_first_arg = to_string(first_arg) ;
		//
		_first = true ;
		(*this)(args...) ;
		_first = false ;
	}
	~Trace() {
		t_lvl-- ;
		t_hide = _hidden ;
	}
	// services
	/**/                  void hide      (bool h=true      ) { t_hide = h ;                               }
	template<class... Ts> void operator()(Ts const&... args) { if ( s_sz && !_hidden ) _record(args...) ; }
private :
	template<class... Ts> void _record(Ts const&...  ) ;
	template<class    T > void _output(T const&     x) { *_t_buf <<     x  ; }
	/**/                  void _output(uint8_t      x) { *_t_buf << int(x) ; } // avoid confusion with char
	/**/                  void _output(int8_t       x) { *_t_buf << int(x) ; } // avoid confusion with char
	/**/                  void _output(bool         x) = delete ;              // bool is not explicit enough, use strings
	// data
	bool            _hidden    = false ;
	bool            _first     = false ;
	::string        _first_arg ;
} ;

template<class... Ts> void Trace::_record(Ts const&... args) {
	static constexpr char Seps[] = ".,'\"`~-+^" ;
	if (!_t_buf) _t_buf = new OStringStream ;
	//
	*_t_buf << (s_ping?'"':'\'') << t_key << '\t' ;
	for( int i=0 ; i<t_lvl ; i++ ) {
		if ( _first && i==t_lvl-1 ) *_t_buf << '*'                          ;
		else                        *_t_buf << Seps[ i % (sizeof(Seps)-1) ] ;
		*_t_buf << '\t' ;
	}
	*_t_buf << _first_arg ;
	int _[1+sizeof...(Ts)] = { 0 , (*_t_buf<<' ',_output(args),0)... } ; (void)_ ;
	*_t_buf << '\n' ;
	//
	#if HAS_OSTRINGSTREAM_VIEW
		::string_view buf_view = _t_buf->view() ;
	#else
		::string      buf_view = _t_buf->str () ;
	#endif
	//
	{	::unique_lock lock     { _s_mutex }            ;
		size_t        new_pos  = s_pos+buf_view.size() ;
		if (new_pos>s_sz) {
			if (s_pos<s_sz) _s_fd.write(::string(s_sz-s_pos,0)) ;
			::lseek(_s_fd,0,SEEK_SET) ;
			s_ping  = !s_ping         ;
			s_pos   = 0               ;
			new_pos = buf_view.size() ;
		}
		_s_fd.write(buf_view) ;
		s_pos = new_pos ;
	}
	_t_buf->str({}) ;
}
