// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <condition_variable>
#include <deque>
#include <latch>
#include <thread>

#include "msg.hh"
#include "time.hh"
#include "trace.hh"

template<class Q> struct ThreadQueue : private Q {
	using ThreadMutex = Mutex<MutexLvl::Thread> ;
	using Val         = typename Q::value_type  ;
	using Q::size ;
	// cxtors & casts
	ThreadQueue() = default ;
	bool operator+() const {
		Lock<ThreadMutex> lock{_mutex} ;
		return !Q::empty() ;
	}
	bool operator!() const { return !+*this ; }
	// services
	/**/                 void push          (Val const& x) { Lock<ThreadMutex> lock{_mutex} ; Q::push_back    (       x          ) ; _cond.notify_one() ; }
	/**/                 void push          (Val     && x) { Lock<ThreadMutex> lock{_mutex} ; Q::push_back    (::move(x)         ) ; _cond.notify_one() ; }
	/**/                 void push_urgent   (Val const& x) { Lock<ThreadMutex> lock{_mutex} ; Q::push_front   (       x          ) ; _cond.notify_one() ; }
	/**/                 void push_urgent   (Val     && x) { Lock<ThreadMutex> lock{_mutex} ; Q::push_front   (::move(x)         ) ; _cond.notify_one() ; }
	template<class... A> void emplace       (A&&...     a) { Lock<ThreadMutex> lock{_mutex} ; Q::emplace_back (::forward<A>(a)...) ; _cond.notify_one() ; }
	template<class... A> void emplace_urgent(A&&...     a) { Lock<ThreadMutex> lock{_mutex} ; Q::emplace_front(::forward<A>(a)...) ; _cond.notify_one() ; }
	Val pop() {
		Lock<ThreadMutex> lock { _mutex } ;
		_cond.wait( lock , [&](){ return !Q::empty() ; } ) ;
		return _pop() ;
	}
	::pair<bool/*popped*/,Val> try_pop() {
		Lock<ThreadMutex> lock { _mutex } ;
		if (Q::empty()) return {false/*popped*/,{}    } ;
		else            return {true /*popped*/,_pop()} ;
	}
	::pair<bool/*popped*/,Val> pop(::stop_token stop) {
		Lock<ThreadMutex> lock { _mutex } ;
		if (!_cond.wait( lock , stop , [&](){ return !Q::empty() ; } )) return {false/*popped*/,{}} ;
		return {true/*popped*/,_pop()} ;
	}
private :
	Val _pop() {
		Val res = ::move(Q::front()) ;
		Q::pop_front() ;
		return res ;
	}
	// data
	ThreadMutex mutable      _mutex ;
	::condition_variable_any _cond  ;
} ;
template<class T> using ThreadDeque = ThreadQueue<::deque<T>> ;

template<class Q> struct QueueThread {
	using Queue = ThreadQueue<Q>      ;
	using Val   = typename Queue::Val ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , QueueThread* self , ::function<void(Val&&)> func ) {
		t_thread_key = key ;
		Trace trace("DequeThread::_s_thread_func") ;
		for(;;) {
			auto [popped,info] = self->_queue.pop(stop) ;
			if (!popped) break ;
			func(::move(info)) ;
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	QueueThread() = default ;
	QueueThread( char key , ::function<void(Val&&)> func ) { open(key,func) ; }
	void open( char key , ::function<void(Val&&)> func ) {
		_thread = ::jthread( _s_thread_func , key , this , func ) ;
	}
	// services
	template<class    U> void push         (U&&    x) { _queue.push   (::forward<U>(x)   ) ; }
	template<class    U> void push_at      (U&&    x) { _queue.push   (::forward<U>(x)   ) ; }
	template<class    U> void push_after   (U&&    x) { _queue.push   (::forward<U>(x)   ) ; }
	template<class... A> void emplace      (A&&... a) { _queue.emplace(::forward<A>(a)...) ; }
	template<class... A> void emplace_at   (A&&... a) { _queue.emplace(::forward<A>(a)...) ; }
	template<class... A> void emplace_after(A&&... a) { _queue.emplace(::forward<A>(a)...) ; }
	// data
private :
	Queue     _queue  ;
	::jthread _thread ; // ensure _thread is last so other fields are constructed when it starts
} ;
template<class T> using DequeThread = QueueThread<::deque<T>> ;

template<class T> struct TimedDequeThread {
	using Pdate = Time::Pdate                  ;
	using Delay = Time::Delay                  ;
	using Queue = ThreadDeque<::pair<Pdate,T>> ;
	using Val   = T                            ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , TimedDequeThread* self , ::function<void(Val&&)> func ) {
		t_thread_key = key ;
		Trace trace("TimedDequeThread::_s_thread_func") ;
		for(;;) {
			auto [popped,info] = self->_queue.pop(stop) ;
			if ( !popped                       ) break ;
			if ( !info.first.sleep_until(stop) ) break ;
			func(::move(info.second)) ;
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	TimedDequeThread() = default ;
	TimedDequeThread( char key , ::function<void(Val&&)> func ) { open(key,func) ; }
	void open       ( char key , ::function<void(Val&&)> func ) {
		_thread = ::jthread( _s_thread_func , key , this , func ) ;
	}
	// services
	template<class    U> void push         (           U&&    x ) { _queue.push   ({Pdate()     ,    ::forward<U>(x)    }) ; }
	template<class    U> void push_at      ( Pdate d , U&&    x ) { _queue.push   ({d           ,    ::forward<U>(x)    }) ; }
	template<class    U> void push_after   ( Delay d , U&&    x ) { _queue.push   ({Pdate(New)+d,    ::forward<U>(x)    }) ; }
	template<class... A> void emplace      (           A&&... a ) { _queue.emplace( Pdate()     ,Val(::forward<A>(a)...) ) ; }
	template<class... A> void emplace_at   ( Pdate d , A&&... a ) { _queue.emplace( d           ,Val(::forward<A>(a)...) ) ; }
	template<class... A> void emplace_after( Delay d , A&&... a ) { _queue.emplace( Pdate(New)+d,Val(::forward<A>(a)...) ) ; }
	// data
private :
	Queue     _queue  ;
	::jthread _thread ; // ensure _thread is last so other fields are constructed when it starts
} ;

struct WakeupThread {
	using ThreadMutex = Mutex<MutexLvl::Thread> ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , WakeupThread* self , ::function<void()> func ) {
		t_thread_key = key ;
		Trace trace("WakeupThread::_s_thread_func") ;
		for(;;) {
			{	Lock<ThreadMutex> lock { self->_mutex } ;
				if ( !self->_cond.wait( lock , stop , [&]()->bool { return self->_active ; } ) ) break ;
				self->_active = false ;
			}
			func() ;
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	WakeupThread() = default ;
	WakeupThread( char key , ::function<void()> func ) { open(key,func) ; }
	void open   ( char key , ::function<void()> func ) {
		_thread = ::jthread( _s_thread_func , key , this , func ) ;
	}
	// services
	void wakeup() {
		if (_active) return ;
		Lock<ThreadMutex> lock{_mutex} ;
		_active = true ;
		_cond.notify_one() ;
	}
private :
	// data
	ThreadMutex mutable      _mutex  ;
	::condition_variable_any _cond   ;
	::atomic<bool>           _active = false ;
	::jthread                _thread ;         // ensure _thread is last so other fields are constructed when it starts
} ;

ENUM(ServerThreadEventKind
,	Master
,	Slave
,	Stop
)
template<class Req> struct ServerThread {
	using Ddate = Time::Ddate ;
	using EventKind = ServerThreadEventKind ;
private :
	static void _s_thread_func( ::stop_token stop , char key , ServerThread* self , ::function<bool/*keep_fd*/(Req&&,SlaveSockFd const&)> func ) {
		static constexpr uint64_t One = 1 ;
		t_thread_key = key ;
		AutoCloseFd        stop_fd = ::eventfd(0,O_CLOEXEC) ; stop_fd.no_std() ;
		Epoll              epoll   { New }                  ;
		::umap<Fd,IMsgBuf> slaves  ;
		::stop_callback    stop_cb {                                                                               // transform request_stop into an event Epoll can wait for
			stop
		,	[&](){
				Trace trace("ServerThread::_s_thread_func::stop_cb",stop_fd) ;
				ssize_t cnt = ::write(stop_fd,&One,sizeof(One)) ;
				SWEAR( cnt==sizeof(One) , cnt , stop_fd ) ;
			}
		} ;
		//
		Trace trace("ServerThread::_s_thread_func",self->fd,self->fd.port(),stop_fd) ;
		self->_ready.count_down() ;
		//
		epoll.add_read(self->fd,EventKind::Master) ;
		epoll.add_read(stop_fd ,EventKind::Stop  ) ;
		for(;;) {
			trace("wait") ;
			::vector<Epoll::Event> events = epoll.wait() ;                                                         // wait for 1 event, no timeout
			for( Epoll::Event event : events ) {
				EventKind kind = event.data<EventKind>() ;
				Fd        efd  = event.fd()              ;
				trace("waited",efd,kind) ;
				switch (kind) {
					case EventKind::Master : {
						SWEAR(efd==self->fd) ;
						try {
							SlaveSockFd slave_fd { self->fd.accept() } ;
							trace("new_req",slave_fd) ;
							epoll.add_read(slave_fd,EventKind::Slave) ;
							slaves.try_emplace(::move(slave_fd)) ;
						} catch (::string const& e) { trace("cannot_accept",e) ; }                                 // ignore error as this may be fd starvation and client will retry
					} break ;
					case EventKind::Stop : {
						uint64_t one ;
						ssize_t  cnt = ::read(efd,&one,sizeof(one)) ;
						SWEAR( cnt==sizeof(one) , cnt ) ;
						trace("stop",mk_key_vector(slaves)) ;
						for( auto const& [sfd,_] : slaves ) epoll.close(sfd) ;
						trace("done") ;
						return ;
					} break ;
					case EventKind::Slave : {
						Req r ;
						try         { if (!slaves.at(efd).receive_step(efd,r)) { trace("partial") ; continue ; } }
						catch (...) {                                            trace("bad_msg") ; continue ;   } // ignore malformed messages
						//
						epoll.del(efd) ;            // Func may trigger efd being closed by another thread, hence epoll.del must be done before
						slaves.erase(efd) ;
						SlaveSockFd ssfd { efd }            ;
						bool        keep = false/*garbage*/ ;
						keep=func(::move(r),ssfd) ;
						if (keep) ssfd.detach() ;   // dont close ssfd if requested to keep it
						trace("called",STR(keep)) ;
					} break ;
				DF}
			}
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	ServerThread() = default ;
	ServerThread( char key , ::function<bool/*keep_fd*/(Req&&,SlaveSockFd const&)> func , int backlog=0 ) { open(key,func,backlog) ; }
	void open   ( char key , ::function<bool/*keep_fd*/(Req&&,SlaveSockFd const&)> func , int backlog=0 ) {
		fd      = {New,backlog}                                   ;
		_thread = ::jthread( _s_thread_func , key , this , func ) ;
	}
	// services
	void wait_started() {
		_ready.wait() ;
	}
	// data
	ServerSockFd fd ;
private :
	::latch   _ready  {1} ;
	::jthread _thread ;                             // ensure _thread is last so other fields are constructed when it starts
} ;
