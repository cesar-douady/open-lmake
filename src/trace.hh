// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "fd.hh"
#include "time.hh"

// STR must only be called for tracing as return type is undefined
#define STR(x) Trace::s_str((x),#x)

extern ::string* g_trace_file ; // pointer to avoid init/fini order hazards, relative to admin dir

ENUM( Channel
,	Default
,	Backend
)
using Channels = BitMap<Channel> ;
static constexpr Channels DfltChannels = ~Channels() ;

#ifdef NO_TRACE

	struct Trace {
		// statics
		static void s_start         (                   ) {}                             // called from main thread
		static void s_new_trace_file(::string const& ={}) {}
		template<class T> static bool s_str( T const& , const char* ) { return false ; } // return anything (least expensive)
		// static data
		static ::atomic<bool    > s_backup_trace ;
		static ::atomic<size_t  > s_sz           ;
		static ::atomic<Channels> s_channels     ;
		// cxtors & casts
		/**/                  Trace( Channel                              ) {}
		template<class... Ts> Trace( Channel , const char* , Ts const&... ) {}
		/**/                  Trace(                                      ) {}
		template<class... Ts> Trace(           const char* , Ts const&... ) {}
		// services
		/**/                  void hide      (bool =true  ) {}
		template<class... Ts> void operator()(Ts const&...) {}
		template<class... Ts> void protect   (Ts const&...) {}
	} ;

#else

	struct Trace {
		// statics
		static void s_start         (                   ) ;
		static void s_new_trace_file(::string const& ={}) ;
	private :
		static void _s_open  () ;
		static void _t_commit() ;
		//
	public :
		template<class T> static ::string s_str( T const& v , const char* s ) { return cat(s,"=",v)            ; }
		/**/              static ::string s_str( bool     v , const char* s ) { return v ? cat(s) : cat('!',s) ; }
		/**/              static ::string s_str( uint8_t  v , const char* s ) { return s_str(int(v),s)         ; } // avoid confusion with char
		/**/              static ::string s_str( int8_t   v , const char* s ) { return s_str(int(v),s)         ; } // avoid confusion with char
		// static data
		static ::atomic<bool    > s_backup_trace ;
		static ::atomic<size_t  > s_sz           ;                                                                 // max overall size of trace, beyond, trace wraps
		static ::atomic<Channels> s_channels     ;
	private :
		static size_t                 _s_pos       ;                                                               // current line number
		static bool                   _s_ping      ;                                                               // ping-pong to distinguish where trace stops in the middle of a trace
		static Fd                     _s_fd        ;
		static ::atomic<bool>         _s_has_trace ;
		static uint8_t*               _s_data      ;                                                               // pointer to mmap'ped trace file
		static size_t                 _s_cur_sz    ;                                                               // current size of trace file
		static Mutex<MutexLvl::Trace> _s_mutex     ;
		//
		static thread_local int       _t_lvl  ;
		static thread_local bool      _t_hide ;                                                                    // if true <=> do not generate trace
		static thread_local ::string* _t_buf  ;                                                                    // pointer to avoid init/fini order hazards
		//
		// cxtors & casts
	public :
		/**/                  Trace( Channel channel                                       ) : _sav_lvl{_t_lvl} , _sav_hide{_t_hide} , _active{s_channels.load()[channel]}                            {}
		template<class... Ts> Trace( Channel channel , const char* tag , Ts const&... args ) : _sav_lvl{_t_lvl} , _sav_hide{_t_hide} , _active{s_channels.load()[channel]} , _first{true} , _tag{tag} {
			self(args...) ;
			_first = false ;
		}
		/**/                  Trace(                                     ) : Trace{Channel::Default            } {}
		template<class... Ts> Trace( const char* tag , Ts const&... args ) : Trace{Channel::Default,tag,args...} {}
		// services
		/**/                  void hide      (bool h=true      ) { _t_hide = h ;                                                                           }
		template<class... Ts> void operator()(Ts const&... args) { if ( _s_has_trace && _active && !_sav_hide.saved ) _record<false/*protect*/>(args...) ; }
		template<class... Ts> void protect   (Ts const&... args) { if ( _s_has_trace && _active && !_sav_hide.saved ) _record<true /*protect*/>(args...) ; }
	private :
		template<bool P,class... Ts> void _record(Ts const&...     ) ;
		template<bool P,class    T > void _output(T const&        x) { *_t_buf <<                    x  ; }
		template<bool P            > void _output(::string const& x) { *_t_buf << (P?mk_printable(x):x) ; }        // make printable if asked to do so
		template<bool P            > void _output(uint8_t         x) { *_t_buf << int(x)                ; }        // avoid confusion with char
		template<bool P            > void _output(int8_t          x) { *_t_buf << int(x)                ; }        // avoid confusion with char
		template<bool P            > void _output(bool            x) = delete ;                                    // bool is not explicit enough, use strings
		// data
		SaveInc<int > _sav_lvl  ;
		Save   <bool> _sav_hide ;
		bool          _active   = true  ;
		bool          _first    = false ;
		::string      _tag      ;
	} ;

	template<bool P,class... Ts> void Trace::_record(Ts const&... args) {
		static constexpr char Seps[] = ".,'\"`~-+^" ;
		if (!_t_buf) _t_buf = new ::string ;
		//
		*_t_buf << (_s_ping?'"':'\'') << t_thread_key << Time::Pdate(New).str(6/*prec*/,true/*in_day*/) << '\t' ;
		for( int i : iota(_t_lvl) ) {
			if ( _first && i==_t_lvl-1 ) *_t_buf << '*'                          ;
			else                         *_t_buf << Seps[ i % (sizeof(Seps)-1) ] ;
			*_t_buf << '\t' ;
		}
		*_t_buf << _tag ;
		int _[1+sizeof...(Ts)] = { 0 , (*_t_buf<<' ',_output<P>(args),0)... } ; (void)_ ;
		*_t_buf << '\n' ;
		_t_commit() ;
	}

#endif

template<class M,bool S=false> struct TraceLock {
	using Base = Lock<M,S> ;
	TraceLock ( M& m ,                       Channel channel , const char* tag ) : _trace{channel,tag,"lock",M::Lvl,STR(S)} , lock{m        } { _trace("locked") ; }
	TraceLock ( M& m ,                                         const char* tag ) : _trace{        tag,"lock",M::Lvl,STR(S)} , lock{m        } { _trace("locked") ; }
	TraceLock ( M& m , Time::Delay timeout , Channel channel , const char* tag ) : _trace{channel,tag,"lock",M::Lvl,STR(S)} , lock{m,timeout} { _trace("locked") ; }
	TraceLock ( M& m , Time::Delay timeout ,                   const char* tag ) : _trace{        tag,"lock",M::Lvl,STR(S)} , lock{m,timeout} { _trace("locked") ; }
	~TraceLock() { _trace("unlock") ; }
	// data
private :
	Trace _trace ;
public :
	Lock<M,S> lock ;
} ;
