// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"

#include "trace.hh"

using namespace Disk ;
using namespace Time ;

Atomic<bool    > Trace::s_backup_trace ;
Atomic<size_t  > Trace::s_sz           = 100<<20      ; // limit to reasonable value until overridden
Atomic<Channels> Trace::s_channels     = DfltChannels ; // by default, trace default channel

#ifdef TRACE

	size_t                 Trace::_s_sz         =  0      ;
	::string               Trace::_s_trace_file ;
	Fd                     Trace::_s_fd         ;
	size_t                 Trace::_s_pos        =  0      ;
	bool                   Trace::_s_ping       = false   ;
	Atomic<bool>           Trace::_s_has_trace  ;
	uint8_t*               Trace::_s_data       = nullptr ;
	size_t                 Trace::_s_cur_sz     = 0       ;
	Mutex<MutexLvl::Trace> Trace::_s_mutex      ;

	thread_local int       Trace::_t_lvl  = 0       ;
	thread_local bool      Trace::_t_hide = false   ;
	thread_local ::string* Trace::_t_buf  = nullptr ;

	void Trace::s_start(::string const& trace_file) {
		SWEAR( !_s_trace_file , _s_trace_file ) ;
		if (!trace_file) return ;
		Lock lock{_s_mutex} ;
		_s_open(trace_file) ;
	}

	void Trace::s_new_trace_file(::string const& trace_file) {
		if (!_s_has_trace            ) return ;
		if (trace_file==_s_trace_file) return ;
		//
		Lock lock{_s_mutex} ;
		_s_close() ;
		_s_open(trace_file) ;
	}

	void Trace::_s_open(::string const& trace_file) {
		size_t asked_sz = round_up<PAGE_SZ>(s_sz.load()) ;
		_s_trace_file = trace_file ;
		if ( asked_sz!=_s_sz && _s_cur_sz ) {
			int rc = ::munmap( _s_data , _s_cur_sz ) ; SWEAR( rc==0 , rc ) ;
			_s_sz     = 0       ;
			_s_cur_sz = 0       ;
			_s_data   = nullptr ;
		}
		if (asked_sz<PAGE_SZ) return ;   // not enough room to trace
		_s_sz = asked_sz ;
		if (!s_channels   ) return ; // nothing to trace
		if (!_s_trace_file) return ; // nowhere to trace to
		if (s_backup_trace) {
			::string prev_old ; //!                                                                      src             dst
			for( char c : "54321"s ) { ::string old = _s_trace_file+'.'+c ; if (+prev_old) try { rename( old           , prev_old ) ; } catch (::string const&) {} prev_old = ::move(old) ; }
			/**/                                                            if (+prev_old) try { rename( _s_trace_file , prev_old ) ; } catch (::string const&) {}
		}
		::string trace_dir_s    = dir_name_s(_s_trace_file)                                         ;
		::string tmp_trace_file = cat(trace_dir_s,::to_string(Pdate(New).nsec_in_s()),'-',getpid()) ;
		//
		// initial size can be set with ftruncate because no mapping exists yet, so no race with page write-back
		// re-open trace file after setting its initial size as mmap would not work with BeeGFS with tuneCoherentBuffers=0
		try { _s_fd = {_s_trace_file,{.flags=O_RDWR|O_TRUNC|O_CREAT}} ; } catch (::string const& e) { exit(Rc::System,"cannot create trace file : ",e) ; }
		_s_map(PAGE_SZ) ;
		//
		_s_pos = 0 ;
		fence() ;
		_s_has_trace = true ;        // ensure _s_has_trace is updated once everything is ok as tracing may be called from other threads while being initialized
	}

	void Trace::_s_close() {
		_s_has_trace = false ;
		fence() ;
		::mmap( _s_data , _s_cur_sz , PROT_NONE , MAP_NORESERVE|MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED , -1/*fd*/ , 0/*offset*/ ) ;
		_s_fd.close() ;
		_s_cur_sz = 0 ;
		_s_pos    = 0 ; // avoid leaving stale useless info
	}

	void Trace::_t_commit() {
		static const ::string Giant = "<giant record>\n" ; // /!\ cannot be constexpr with gcc-11
		//
		::string const& buf_view = _t_buf->size()<=(s_sz>>4) ? *_t_buf : Giant ;
		//
		{	Lock   lock    { _s_mutex }             ;
			size_t new_pos = _s_pos+buf_view.size() ;
			if ( _s_cur_sz<s_sz && new_pos>_s_cur_sz )
				_s_map( ::min( round_up<PAGE_SZ>(::max(new_pos,_s_cur_sz+(_s_cur_sz>>2))) , size_t(s_sz) ) ) ;
			if (new_pos>s_sz) {
				if (_s_pos<s_sz) ::memset(_s_data+_s_pos,0,s_sz-_s_pos) ;
				_s_ping = !_s_ping        ;
				_s_pos  = 0               ;
				new_pos = buf_view.size() ;
			}
			::memcpy( _s_data+_s_pos , buf_view.data() , buf_view.size() ) ;
			_s_pos = new_pos ;
		}
		_t_buf->clear() ;
	}

	void Trace::_s_map(size_t sz) {
		SWEAR( sz>_s_cur_sz  , _s_cur_sz,sz ) ;
		SWEAR( sz%PAGE_SZ==0 , sz           ) ;
		// avoid touching file while maps exist
		if ( _s_cur_sz && ::munmap(_s_data,_s_cur_sz)!=0 ) exit( Rc::System , "cannot unmap trace file ",_s_trace_file," : ",StrErr() ) ;
		try {
			if (sz<=_s_cur_sz+(1<<20)) {            // fast path : only allocate necessary buf
				::string buf ( sz-_s_cur_sz , 0 ) ;
				_s_fd.write(buf) ;
			} else {
				::string buf ( 1<<20 , 0 ) ;        // write by chunks to avoid allocating giant buf
				size_t   s   = _s_cur_sz   ;
				for(; s+buf.size()<sz ; s+=buf.size() ) _s_fd.write(buf                    ) ;
				/**/                                    _s_fd.write(substr_view(buf,0,sz-s)) ;
			}
		} catch (::string const& e) {
			exit(Rc::System,"cannot extend trace file ",_s_trace_file," to size ",sz," : ",e) ;
		}
		_s_data = static_cast<uint8_t*>(::mmap( _s_data , sz , PROT_READ|PROT_WRITE , MAP_SHARED, _s_fd , 0 )) ;
		if (_s_data==MAP_FAILED) exit( Rc::System , "cannot map trace file "  ,_s_trace_file," : ",StrErr() ) ;
		_s_cur_sz = sz ;
	}

#endif
