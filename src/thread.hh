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

template<class Q,bool Flush=true> struct ThreadQueue : Q { // if Flush, process remaining items when asked to stop
	using ThreadMutex = Mutex<MutexLvl::Thread> ;
	using Val         = typename Q::value_type  ;
	using Q::size ;
	// cxtors & casts
	ThreadQueue() = default ;
	// accesses
	bool operator+() const {
		Lock<ThreadMutex> lock{_mutex} ;
		return !Q::empty() ;
	}
	bool operator!() const { return !+*this ;  }
	//
	void lock  (MutexLvl lvl) const { _mutex.lock  (lvl) ; }
	void unlock(MutexLvl lvl) const { _mutex.unlock(lvl) ; }
	// services
	template<class    T> void push_urgent   (T&&    x) { Lock<ThreadMutex> lock{_mutex} ; Q::push_front   (::forward<T>(x)   ) ; _cond.notify_one() ; }
	template<class    T> void push          (T&&    x) { Lock<ThreadMutex> lock{_mutex} ; Q::push_back    (::forward<T>(x)   ) ; _cond.notify_one() ; }
	template<class... A> void emplace_urgent(A&&... a) { Lock<ThreadMutex> lock{_mutex} ; Q::emplace_front(::forward<A>(a)...) ; _cond.notify_one() ; }
	template<class... A> void emplace       (A&&... a) { Lock<ThreadMutex> lock{_mutex} ; Q::emplace_back (::forward<A>(a)...) ; _cond.notify_one() ; }
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
		if ( Flush && !Q::empty()                                       ) return {true /*popped*/,_pop()} ;
		if ( !_cond.wait( lock , stop , [&](){ return !Q::empty() ; } ) ) return {false/*popped*/,{}    } ;
		/**/                                                              return {true /*popped*/,_pop()} ;
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
template<class T,bool Flush=true> using ThreadDeque = ThreadQueue<::deque<T>,Flush> ;

template<class Q,bool Flush=true> struct QueueThread : ThreadQueue<Q,Flush> { // if Flush, process remaining items when asked to stop
	using Base = ThreadQueue<Q,Flush> ;
	using Val  = typename Base::Val  ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , QueueThread* self , ::function<void(Val&&)> func ) {
		t_thread_key = key ;
		Trace trace("DequeThread::_s_thread_func") ;
		for(;;) {
			auto [popped,info] = self->pop(stop) ;
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
	// data
private :
	::jthread _thread ;                                // ensure _thread is last so other fields are constructed when it starts
} ;
template<class T,bool Flush=true> using DequeThread = QueueThread<::deque<T>,Flush> ;

template<class T,bool Flush=true> struct TimedDequeThread : ThreadDeque<::pair<Time::Pdate,T>,Flush> { // if Flush, process immediately executable remaining items when asked to stop
	using Base  = ThreadDeque<::pair<Time::Pdate,T>,Flush> ;
	using Pdate = Time::Pdate                              ;
	using Delay = Time::Delay                              ;
	using Val   = T                                        ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , TimedDequeThread* self , ::function<void(Val&&)> func ) {
		t_thread_key = key ;
		Trace trace("TimedDequeThread::_s_thread_func") ;
		for(;;) {
			auto [popped,info] = self->pop(stop) ;
			if ( !popped                             ) break ;
			if ( !info.first.sleep_until(stop,Flush) ) break ;
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
	template<class U> void push_urgent(           U&& x ) { Base::emplace_urgent(Pdate()     , ::forward<U>(x) ) ; }
	template<class U> void push       (           U&& x ) { Base::emplace       (Pdate()     , ::forward<U>(x) ) ; }
	template<class U> void push_at    ( Pdate d , U&& x ) { Base::emplace       (d           , ::forward<U>(x) ) ; }
	template<class U> void push_after ( Delay d , U&& x ) { Base::emplace       (Pdate(New)+d, ::forward<U>(x) ) ; }
	//
	template<class... A> void emplace_urgent(           A&&... a ) { push_urgent(  Val(::forward<A>(a)...)) ; }
	template<class... A> void emplace       (           A&&... a ) { push       (  Val(::forward<A>(a)...)) ; }
	template<class... A> void emplace_at    ( Pdate d , A&&... a ) { push_at    (d,Val(::forward<A>(a)...)) ; }
	template<class... A> void emplace_after ( Delay d , A&&... a ) { push_after (d,Val(::forward<A>(a)...)) ; }
	// data
private :
	::jthread _thread ;                                     // ensure _thread is last so other fields are constructed when it starts
} ;

template<bool Flush=true> struct WakeupThread {
	using ThreadMutex = Mutex<MutexLvl::Thread> ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , WakeupThread* self , ::function<void()> func ) {
		t_thread_key = key ;
		Trace trace("WakeupThread::_s_thread_func") ;
		for(;;) {
			if ( Flush && self->_active ) {
				self->_active = false ;
			} else {
				Lock<ThreadMutex> lock { self->_mutex } ;
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
template<class Req,bool Flush=true> struct ServerThread {                                 // if Flush, finish on going connections
	using Delay = Time::Delay ;
	using EventKind = ServerThreadEventKind ;
private :
	static void _s_thread_func( ::stop_token stop , char key , ServerThread* self , ::function<bool/*keep_fd*/(Req&&,SlaveSockFd const&)> func ) {
		static constexpr uint64_t One = 1 ;
		t_thread_key = key ;
		AutoCloseFd        stop_fd = ::eventfd(0,O_CLOEXEC) ; stop_fd.no_std() ;
		Epoll              epoll   { New }                  ;
		::umap<Fd,IMsgBuf> slaves  ;
		::stop_callback    stop_cb {                                                       // transform request_stop into an event Epoll can wait for
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
			::vector<Epoll::Event> events = epoll.wait(epoll.cnt?Delay::Forever:Delay()) ; // wait for 1 event, no timeout unless stopped
			if (!events) { SWEAR(Flush) ; return ; }                                       // if !Flush, we should have returned immediately
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
						} catch (::string const& e) { trace("cannot_accept",e) ; }         // ignore error as this may be fd starvation and client will retry
					} break ;
					case EventKind::Stop : {
						uint64_t one ;
						ssize_t  cnt = ::read(efd,&one,sizeof(one)) ;
						SWEAR( cnt==sizeof(one) , cnt ) ;
						trace("stop",mk_key_vector(slaves)) ;
						for( auto const& [sfd,_] : slaves ) epoll.close(sfd) ;
						trace("done") ;
						if (Flush) epoll.cnt-- ;                                           // dont wait for new incoming connections, but finish on going connections and process what comes
						else       return ;                                                // stop immediately
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
