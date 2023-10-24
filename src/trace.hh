// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

#define STR(x) Trace::str((x),#x)

extern ::string* g_trace_file ;     // pointer to avoid init/fini order hazards, relative to admin dir

#ifdef NO_TRACE

	struct Trace {
		// statics
		static void s_start         (               ) {}
		static void s_new_trace_file(::string const&) {}
		template<class T> static ::string str( T const& , ::string const& ) { return {} ; }
		// static data
		static              bool             s_backup_trace ;
		static              ::atomic<size_t> s_sz           ;
		static thread_local char             t_key          ;
		// cxtors & casts
		Trace() = default ;
		template<class T,class... Ts> Trace( T const& , Ts const&... ) {}
		// services
		/**/                  void hide      (bool =true  ) {}
		template<class... Ts> void operator()(Ts const&...) {}
		template<class... Ts> void protect   (Ts const&...) {}
	} ;

#else

	struct Trace {
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
		static              bool             s_backup_trace ;
		static              ::atomic<size_t> s_sz           ;                  // max number of lines
		static thread_local char             t_key          ;
	private :
		static size_t  _s_pos   ;      // current line number
		static bool    _s_ping  ;      // ping-pong to distinguish where trace stops in the middle of a trace
		static Fd      _s_fd    ;
		static ::mutex _s_mutex ;
		//
		static thread_local int            _t_lvl  ;
		static thread_local bool           _t_hide ;       // if true <=> do not generate trace
		static thread_local OStringStream* _t_buf  ;       // pointer to avoid init/fini order hazards
		//
		// cxtors & casts
	public :
		Trace() : _save_lvl{_t_lvl} , _save_hide{_t_hide} {}
		template<class T,class... Ts> Trace( T const& first_arg , Ts const&... args ) : Trace{} {
			_first_arg = to_string(first_arg) ;
			//
			_first = true ;
			(*this)(args...) ;
			_first = false ;
		}
		// services
		/**/                  void hide      (bool h=true      ) { _t_hide = h ;                                                         }
		template<class... Ts> void operator()(Ts const&... args) { if ( s_sz && !_save_hide.saved ) _record<false/*protect*/>(args...) ; }
		template<class... Ts> void protect   (Ts const&... args) { if ( s_sz && !_save_hide.saved ) _record<true /*protect*/>(args...) ; }
	private :
		template<bool P,class... Ts> void _record(Ts const&...     ) ;
		template<bool P,class    T > void _output(T const&        x) { *_t_buf <<                    x  ; }
		template<bool P            > void _output(::string const& x) { *_t_buf << (P?mk_printable(x):x) ; } // make printable if asked to do so
		template<bool P            > void _output(uint8_t         x) { *_t_buf << int(x)                ; } // avoid confusion with char
		template<bool P            > void _output(int8_t          x) { *_t_buf << int(x)                ; } // avoid confusion with char
		template<bool P            > void _output(bool            x) = delete ;                             // bool is not explicit enough, use strings
		// data
		SaveInc<int > _save_lvl  ;
		Save   <bool> _save_hide ;
		bool          _first     = false ;
		::string      _first_arg ;
	} ;

	template<bool P,class... Ts> void Trace::_record(Ts const&... args) {
		static constexpr char Seps[] = ".,'\"`~-+^" ;
		if (!_t_buf) _t_buf = new OStringStream ;
		//
		*_t_buf << (_s_ping?'"':'\'') << t_key << '\t' ;
		for( int i=0 ; i<_t_lvl ; i++ ) {
			if ( _first && i==_t_lvl-1 ) *_t_buf << '*'                          ;
			else                         *_t_buf << Seps[ i % (sizeof(Seps)-1) ] ;
			*_t_buf << '\t' ;
		}
		*_t_buf << _first_arg ;
		int _[1+sizeof...(Ts)] = { 0 , (*_t_buf<<' ',_output<P>(args),0)... } ; (void)_ ;
		*_t_buf << '\n' ;
		//
		#if HAS_OSTRINGSTREAM_VIEW
			::string_view buf_view = _t_buf->view() ;
		#else
			::string      buf_view = _t_buf->str () ;
		#endif
		//
		{	::unique_lock lock    { _s_mutex }             ;
			size_t        new_pos = _s_pos+buf_view.size() ;
			if (new_pos>s_sz) {
				if (_s_pos<s_sz) _s_fd.write(to_string(::right,::setw(s_sz-_s_pos),'\n')) ;
				::lseek(_s_fd,0,SEEK_SET) ;
				_s_ping = !_s_ping        ;
				_s_pos  = 0               ;
				new_pos = buf_view.size() ;
			}
			_s_fd.write(buf_view) ;
			_s_pos = new_pos ;
		}
		_t_buf->str({}) ;
	}

#endif
