// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// do not include "utils.hh" nor any other lmake .hh so as to keep ebpf daemon very simple to analyze

#pragma once

#include <bpf/libbpf.h>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <stdio.h>

#include <cstdint>

#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ebpf_event.h"

namespace EbpfDaemon {

	static constexpr int  NTrials   = 3                 ; // number of times to try to launch daemon before giving up
	static constexpr char SockDir[] = "/run/open-lmake" ;

	extern uint64_t const g_mrkr ;

	/**/   std::string get_file_name(std::string const& sfx) ;
	inline std::string get_sock_name(                      ) { return get_file_name("sock") ; }

	struct Path {
		bool operator+() const { return root_id || !path.empty() ; }
		bool operator!() const { return !+*this                  ; }
		EbpfRootId  root_id = 0  ;
		std::string path    = {} ;
	} ;
	struct RingBufBase {
		static void s_start_job(                                 ) ;                            // called by top level job thread
		static void s_add_root ( EbpfRootId , std::string const& ) ;                            // called from tracer
	protected :
		static Path _s_mk_path( EbpfRootId , std::string_view ) ;                               // components are stored leaf first, separated by null
	public :
		// cxtors & co
		RingBufBase() = default ;
		RingBufBase(RingBufBase const& ) = delete ;                                             // not copyable
		RingBufBase( EbpfJobId jid , int rbfd ) : job_id{jid} , _ring_buf_fd{rbfd} { init() ; }
		~RingBufBase() ;
		bool operator+() const { return _ring_buf_fd>=0 ; }
		bool operator!() const { return !+*this         ; }
		// services
		bool/*ok*/ connect_to_daemon() ;
		void       init             () ;
		// data
		EbpfJobId job_id = 0 ;
	protected :
		int _ring_buf_fd = -1 ;
	private :
		int  _daemon_fd  = -1    ;
		bool _registered = false ;
	} ;

	template<class Consumer> struct RingBuf : RingBufBase {
		struct ThreadCtx {
			pid_t                     pid_in_ns  ;
			pid_t                     tid_in_ns  ;
			Path                      dst        = {} ;                                                                           // dest of 2-files events
			std::unordered_set<pid_t> to_wake_up = {} ;                                                                           // pids to wake up when ring_buffer has enough room
		} ;
	private :
		static int _s_handle_event( void* ctx , void* data , size_t sz ) {
			RingBuf& ring_buf = *static_cast<RingBuf*>(ctx) ;
			ring_buf._handle_event(data,sz) ;
			return 0 ;
		}
		// cxtors & co
	public :
		RingBuf() = default ;
		RingBuf( EbpfJobId jid , int rbfd ) : RingBufBase{jid,rbfd} { init() ; }
		~RingBuf() {
			::ring_buffer__free(_ring_buf) ;
		}
		void init() {
			using namespace std::string_literals ;
			RingBufBase::init() ;
			_ring_buf = ::ring_buffer__new( _ring_buf_fd , _s_handle_event , this , nullptr/*opts*/ ) ; if (!_ring_buf) throw "cannot open ebpf ring buffer"s ;
			epoll_fd  = ::ring_buffer__epoll_fd(_ring_buf)                                            ;
		}
		// services
		bool/*done*/ consume() {                                                                                                  // consume available events
			::ring_buffer__consume(_ring_buf) ;
			return _done ;
		}
	private :
		void _handle_event( void* data , size_t sz ) {
			using namespace std::string_literals ;
			if (sz<sizeof(EbpfEventHdr)) throw "bad data event"s ;
			//
			EbpfEventHdr const& hdr = *static_cast<EbpfEventHdr const*>(data) ;
			switch (hdr.kind) {
				case EBPF_NONE      : throw "bad empty event"s ;                                                                  // EBPF_NONE messages should have been filtered out
				case EBPF_JOB_START : if ( _started) throw "double job start"s        ; if (_done) throw "event while done"s ; _started = true ; break ;
				case EBPF_JOB_END   : if (!_started) throw "event while not started"s ; if (_done) throw "double job end"s   ; _done    = true ; break ;
				default             : if (!_started) throw "event while not started"s ; if (_done) throw "event while done"s ;
			}
			switch (hdr.kind) {
				case EBPF_JOB_START    :
				case EBPF_PROC_START   :
				case EBPF_THREAD_START : if ( !_ctxs.insert({ hdr.tid , {hdr.pid_in_ns,hdr.tid_in_ns} }).second ) throw "double thread start"s ;
				default                : ;
			}
			ThreadCtx & ctx = _ctxs.at(hdr.tid) ;
			if (hdr.kind>=EBPF_HAS_DATA) {
				std::string_view view = std::string_view( static_cast<const char*>(data)+sizeof(EbpfEventHdr) , sz-sizeof(EbpfEventHdr) ) ;
				Path             path = _s_mk_path( hdr.root_id , view )                                                                  ;
				switch (hdr.kind) {
					case EBPF_PATH_RES :                                             consumer.handle_path_res(path) ; break ;
					case EBPF_DST      : if (+ctx.dst) throw "double destination"s ; ctx.dst = path ;                 break ;
					default :
						consumer.handle_event( hdr , path , ctx.dst ) ;
						ctx.dst = {} ;
				}
			}
			if ( hdr.flags & EBPF_F_STOPPED )
				ctx.to_wake_up.insert(ctx.pid_in_ns) ;                                                                            // restart
			if ( !ctx.to_wake_up.empty() && ring__avail_data_size(ring_buffer__ring(_ring_buf,0/*idx*/))<EBPF_RING_BUF_MARGIN ) { // but only once we have enough room in ring buf
				for( pid_t pid : ctx.to_wake_up ) kill( pid , SIGCONT ) ;
				ctx.to_wake_up = {} ;
			}
			switch (hdr.kind) {
				case EBPF_JOB_END    :
				case EBPF_PROC_END   :
				case EBPF_THREAD_END : if (!_ctxs.erase(hdr.tid)) throw "double thread end"s ; break ;
				default              : ;
			}
		}
		// data
	public :
		int      epoll_fd = -1 ;
		Consumer consumer ;
	private :
		::ring_buffer*                      _ring_buf = nullptr ;
		std::unordered_map<pid_t,ThreadCtx> _ctxs     ;                                                                           // context of currently handled syscall
		bool                                _started  = false   ;
		bool                                _done     = false   ;
	} ;

}
