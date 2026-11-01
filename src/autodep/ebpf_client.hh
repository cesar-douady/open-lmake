// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// do not include "utils.hh" nor any other lmake .hh so as to keep ebpf daemon very simple to analyze

#pragma once

#include <bpf/libbpf.h>

#include <stdio.h>

#include <cstdint>

#include <format>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>

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
		EbpfRootId  root_id = 0 ;
		std::string path    ;
	} ;
	struct RingBufBase {
		static void s_start_job(                                 ) ;                                                                // called by top level job thread
		static void s_add_root ( EbpfRootId , std::string const& ) ;                                                                // called from tracer
	protected :
		static Path _s_mk_path(std::string_view) ;                                                                                  // components are stored leaf first, separated by null
	public :
		// cxtors & co
		RingBufBase() = default ;
		RingBufBase(RingBufBase const& ) = delete ;                                                                                 // not copyable
		RingBufBase( EbpfJobId jid , int rbfd , bool deps_in_system ) : job_id{jid} , _ring_buf_fd{rbfd} { init(deps_in_system) ; }
		RingBufBase(                            bool deps_in_system ) ;
		~RingBufBase() ;
		void init(bool deps_in_system) ;
		bool operator+() const { return _ring_buf_fd>=0 ; }
		bool operator!() const { return !+*this         ; }
		// data
		EbpfJobId job_id = 0 ;
	protected :
		int _ring_buf_fd = -1 ;
	private :
		int daemon_fd = -1 ;
	} ;

	template<class Consumer> struct RingBuf : RingBufBase {
	private :
		static int _s_handle_event( void* ctx , void* data , size_t sz ) {
			RingBuf& ring_buf = *static_cast<RingBuf*>(ctx) ;
			return ring_buf._handle_event(data,sz) ;
		}
		// cxtors & co
	public :
		template<class... A> RingBuf( EbpfJobId jid , int rbfd , bool deps_in_system , A&&... args ) : RingBufBase{jid,rbfd,deps_in_system} , consumer{std::forward<A>(args)...} { init() ; }
		template<class... A> RingBuf(                            bool deps_in_system , A&&... args ) : RingBufBase{         deps_in_system} , consumer{std::forward<A>(args)...} { init() ; }
		~RingBuf() {
			::ring_buffer__free(_ring_buf) ;
		}
		void init() {
			using namespace std::string_literals ;
			_ring_buf = ::ring_buffer__new( _ring_buf_fd , _s_handle_event , this , nullptr/*opts*/ ) ; if (!_ring_buf) throw "cannot open ebpf ring buffer"s ;
			epoll_fd  = ::ring_buffer__epoll_fd(_ring_buf)                                            ;
		}
		// services
		bool/*done*/ consume() {                // consume available events
			::ring_buffer__consume(_ring_buf) ;
			return _done ;
		}
	private :
		void _chk_plain(EbpfEventHdr const& e) {
			if (!_started) throw std::format("event {} while not started",e.kind) ;
			if ( _done   ) throw std::format("event {} while done"       ,e.kind) ;
		}
		int _handle_event( void* data , size_t sz ) {
			using namespace std::string_literals ;
			EbpfEventHdr const& event_hdr = *static_cast<EbpfEventHdr const*>(data) ;
			if (event_hdr.kind<EBPF_HAS_DATA) {
				if (sz!=sizeof(EbpfEventHdr)) throw "bad ebpf msg"s ;
				switch (event_hdr.kind) {
					case EBPF_NONE      :               throw "EBPF_NONE messages should have been filtered out"s ;
					case EBPF_JOB_START : if (_started) throw "double start"s                                     ; _started = true ; break ;
					case EBPF_JOB_END   : if (_done   ) throw "double end"s                                       ; _done    = true ; break ;
					default             :
						_chk_plain(event_hdr)            ;
						consumer.handle_event(event_hdr) ;
				}
			} else {
				if (sz<sizeof(EbpfEventBase)) throw "bad ebpf msg"s ;
				_chk_plain(event_hdr) ;
				EbpfEventBase const& event = *static_cast<EbpfEventBase const*>(data)                                                                        ;
				Path                 path  = _s_mk_path(std::string_view( static_cast<const char*>(data)+sizeof(EbpfEventBase) , sz-sizeof(EbpfEventBase) )) ;
				if (event.hdr.kind==EBPF_TARGET) {
					auto inserted = _targets.try_emplace( event.hdr.tid , std::move(path) ).second ; if (!inserted) throw "target clash"s ;
				} else {
					auto it = _targets.find(event.hdr.tid) ;
					if (it==_targets.end())   consumer.handle_event( event , path              ) ;
					else                    { consumer.handle_event( event , path , it->second ) ; _targets.erase(it) ; }
				}
			}
			return 0 ;
		}
		// data
	public :
		int      epoll_fd = -1 ;
		Consumer consumer ;
	private :
		::ring_buffer*                 _ring_buf = nullptr ;
		std::unordered_map<pid_t,Path> _targets  ;
		bool                           _started  = false   ;
		bool                           _done     = false   ;
	} ;

}
