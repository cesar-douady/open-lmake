// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <deque>
using std::deque ;

#include "msg.hh"
#include "time.hh"
#include "trace.hh"

template<class T,bool Flush=true,bool Urgent=false> struct ThreadQueue { // if Flush, process remaining items when asked to stop
	using ThreadMutex = Mutex<MutexLvl::Thread> ;
	using L           = Lock<ThreadMutex>       ;
	using Delay       = Time::Delay             ;
	// accesses
	#define LCK Lock lock{_mutex}
	bool operator+() const { LCK ; return !_empty() ; }
	//
	void lock        (MutexLvl& lvl) const { _mutex.lock        (lvl) ; }
	void unlock      (MutexLvl& lvl) const { _mutex.unlock      (lvl) ; }
	void swear_locked(             ) const { _mutex.swear_locked(   ) ; }
	//
	size_t size () const { LCK ; size_t res = _queue[0].size () ; if (Urgent) res += _queue[1].size () ; return res ; }
	//
	auto begin()       requires(!Urgent) { return _queue[0].begin() ; }  // if needed for Urgent, we must iterate over both queues
	auto begin() const requires(!Urgent) { return _queue[0].begin() ; }  // .
	auto end  ()       requires(!Urgent) { return _queue[0].end  () ; }  // .
	auto end  () const requires(!Urgent) { return _queue[0].end  () ; }  // .
private :
	bool _empty() const {
		bool res = _queue[0].empty() ; if (Urgent) res &= _queue[1].empty() ;
		return res ;
	}
	// services
public :
	template<class    U> void push_urgent   (U&&    x) requires(Urgent) { LCK ; _queue[1].push_back   (::forward<U>(x)   ) ; _cond.notify_one() ; }
	template<class    U> void push          (U&&    x)                  { LCK ; _queue[0].push_back   (::forward<U>(x)   ) ; _cond.notify_one() ; }
	template<class... A> void emplace_urgent(A&&... a) requires(Urgent) { LCK ; _queue[1].emplace_back(::forward<A>(a)...) ; _cond.notify_one() ; }
	template<class... A> void emplace       (A&&... a)                  { LCK ; _queue[0].emplace_back(::forward<A>(a)...) ; _cond.notify_one() ; }
	// clear res while we are waiting
	void           pop    (                               T&/*out*/ res ) { LCK ; bool e = _empty() ; if (e) res = {} ;                         _wait(       lock) ;        _pop(res) ;            }
	bool/*popped*/ pop    ( ::stop_token stop ,           T&/*out*/ res ) { LCK ; bool e = _empty() ; if (e) res = {} ; bool p = (Flush&&!e) || _wait(stop,  lock) ; if (p) _pop(res) ; return p ; }
	bool/*popped*/ pop_for(                     Delay d , T&/*out*/ res ) { LCK ; bool e = _empty() ; if (e) res = {} ; bool p = (Flush&&!e) || _wait(     d,lock) ; if (p) _pop(res) ; return p ; }
	bool/*popped*/ pop_for( ::stop_token stop , Delay d,  T&/*out*/ res ) { LCK ; bool e = _empty() ; if (e) res = {} ; bool p = (Flush&&!e) || _wait(stop,d,lock) ; if (p) _pop(res) ; return p ; }
	//
	/**/       T  pop    (                             ) { LCK ;                                     _wait(       lock) ;             return _pop() ;                  }
	::optional<T> pop    ( ::stop_token stop           ) { LCK ; bool popped = (Flush&&!_empty()) || _wait(stop,  lock) ; if (popped) return _pop() ; else return {} ; }
	::optional<T> pop_for(                     Delay d ) { LCK ; bool popped = (Flush&&!_empty()) || _wait(     d,lock) ; if (popped) return _pop() ; else return {} ; }
	::optional<T> pop_for( ::stop_token stop , Delay d ) { LCK ; bool popped = (Flush&&!_empty()) || _wait(stop,d,lock) ; if (popped) return _pop() ; else return {} ; }
	#undef LCK
private :
	void _pop(T& res) { swear_locked() ; bool has_urgent = Urgent && !_queue[1].empty() ;   res = ::move(_queue[has_urgent].front()) ; _queue[has_urgent].pop_front() ;              }
	T    _pop(      ) { swear_locked() ; bool has_urgent = Urgent && !_queue[1].empty() ; T res = ::move(_queue[has_urgent].front()) ; _queue[has_urgent].pop_front() ; return res ; }
	//
	void _wait(                               Lock<ThreadMutex>& lock ) {        _cond.wait    ( lock ,                                   [&](){ return !_empty() ; } ) ; }
	bool _wait( ::stop_token stop           , Lock<ThreadMutex>& lock ) { return _cond.wait    ( lock , stop ,                            [&](){ return !_empty() ; } ) ; }
	bool _wait(                     Delay d , Lock<ThreadMutex>& lock ) { return _cond.wait_for( lock ,        ::chrono::nanoseconds(d) , [&](){ return !_empty() ; } ) ; }
	bool _wait( ::stop_token stop , Delay d , Lock<ThreadMutex>& lock ) { return _cond.wait_for( lock , stop , ::chrono::nanoseconds(d) , [&](){ return !_empty() ; } ) ; }
	// data
	ThreadMutex mutable      _mutex           ;
	::condition_variable_any _cond            ;
	::deque<T>               _queue[1+Urgent] ;                          // need 2 queues if we manage urgent messages
} ;

template<class T,bool Flush=true,bool QueueAccess=false,bool Urgent=false> struct QueueThread : private ThreadQueue<T,Flush,Urgent> { // if Flush, process remaining items when stopped
	using Base = ThreadQueue<T,Flush,Urgent> ;
	using Base::push_urgent    ;
	using Base::push           ;
	using Base::emplace_urgent ;
	using Base::emplace        ;
	using Base::lock           ;
	using Base::unlock         ;
	using Base::swear_locked   ;
	using Delay = Time::Delay ;
	#define RQA  requires( QueueAccess)
	#define RNQA requires(!QueueAccess)
	// statics
private :
	// XXX! : why gcc refuses to call both functions _s_thread_func ?
	static void _s_thread_func1( ::stop_token stop , char key , QueueThread* this_ , ::function<void(::stop_token,T const&)> func ) RQA {
		t_thread_key = key ;
		Trace trace("QueueThread::_s_thread_func1") ;
		while (this_->pop(stop,this_->_cur)) func(stop,this_->_cur) ;
		trace("done") ;
	}
	static void _s_thread_func2( ::stop_token stop , char key , QueueThread* this_ , ::function<void(::stop_token,T&&)> func ) RNQA {
		t_thread_key = key ;
		Trace trace("QueueThread::_s_thread_func2") ;
		for(;;) {
			::optional<T> info = this_->pop(stop) ;
			if (!info) break ;
			func(stop,::move(*info)) ;
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	QueueThread() = default ;
	QueueThread( char k , ::function<void(               T const&)> f ) { open(k,f) ; }
	QueueThread( char k , ::function<void(::stop_token , T const&)> f ) { open(k,f) ; }
	//
	void open( char k , ::function<void(               T const&)> f ) RQA  { thread = ::jthread( _s_thread_func1 , k , this , [=](::stop_token,T const& v)->void {f(       v );} ) ; }
	void open( char k , ::function<void(::stop_token , T const&)> f ) RQA  { thread = ::jthread( _s_thread_func1 , k , this , f                                                  ) ; }
	void open( char k , ::function<void(               T     &&)> f ) RNQA { thread = ::jthread( _s_thread_func2 , k , this , [=](::stop_token,T     && v)->void {f(::move(v));} ) ; }
	void open( char k , ::function<void(::stop_token , T     &&)> f ) RNQA { thread = ::jthread( _s_thread_func2 , k , this , f                                                  ) ; }
	// accesses
	T const& cur() const RQA { return _cur ; }
	// services
	auto begin()       RQA { swear_locked() ; return Base::begin() ; }
	auto begin() const RQA { swear_locked() ; return Base::begin() ; }
	auto end  ()       RQA { swear_locked() ; return Base::end  () ; }
	auto end  () const RQA { swear_locked() ; return Base::end  () ; }
	#undef RQA
	#undef RNQA
	// data
private :
	bool                                _has_cur = false ;
	::conditional_t<QueueAccess,T,Void> _cur     ;
public :
	::jthread thread ; // ensure thread is last so other fields are constructed before it starts and destructed after it ends
} ;

template<class T,bool Flush=true> struct TimedQueueThread : ThreadQueue<::pair<Time::Pdate,T>,Flush> { // if Flush, process immediately available remaining items when stopped
	using Base  = ThreadQueue<::pair<Time::Pdate,T>,Flush> ;
	using Pdate = Time::Pdate                              ;
	using Delay = Time::Delay                              ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , TimedQueueThread* this_ , ::function<void(::stop_token,T&&)> func ) {
		t_thread_key = key ;
		Trace trace("TimedQueueThread::_s_thread_func") ;
		for(;;) {
			::optional<::pair<Time::Pdate,T>> info = this_->pop(stop) ;
			if (!info                               ) break ;
			if (!info->first.sleep_until(stop,Flush)) break ;
			func(stop,::move(info->second)) ;
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	TimedQueueThread() = default ;
	TimedQueueThread( char k , ::function<void(             T&&)> f ) { open(k,f) ; }
	TimedQueueThread( char k , ::function<void(::stop_token,T&&)> f ) { open(k,f) ; }
	//
	void open( char k , ::function<void(             T&&)> f ) { thread = ::jthread( _s_thread_func , k , this , [=](::stop_token,T&& v)->void {f(::move(v));} ) ; }
	void open( char k , ::function<void(::stop_token,T&&)> f ) { thread = ::jthread( _s_thread_func , k , this , f                                             ) ; }
	// services
	template<class U> void push_urgent(           U&& x ) { Base::emplace_urgent(Pdate()     , ::forward<U>(x) ) ; }
	template<class U> void push       (           U&& x ) { Base::emplace       (Pdate()     , ::forward<U>(x) ) ; }
	template<class U> void push_at    ( Pdate d , U&& x ) { Base::emplace       (d           , ::forward<U>(x) ) ; }
	template<class U> void push_after ( Delay d , U&& x ) { Base::emplace       (Pdate(New)+d, ::forward<U>(x) ) ; }
	//
	template<class... A> void emplace_urgent(           A&&... a ) { push_urgent(  T(::forward<A>(a)...)) ; }
	template<class... A> void emplace       (           A&&... a ) { push       (  T(::forward<A>(a)...)) ; }
	template<class... A> void emplace_at    ( Pdate d , A&&... a ) { push_at    (d,T(::forward<A>(a)...)) ; }
	template<class... A> void emplace_after ( Delay d , A&&... a ) { push_after (d,T(::forward<A>(a)...)) ; }
	// data
	::jthread thread ;                                                                                 // ensure thread is last so other fields are constructed when it starts
} ;

enum class WakeupState : uint8_t {
	Wait
,	Proceed
,	Last
,	Stop
} ;

template<bool Flush=true> struct WakeupThread {
	using ThreadMutex = Mutex<MutexLvl::Thread> ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , WakeupThread* this_ , ::function<void(::stop_token)> func ) {
		t_thread_key = key ;
		Trace trace("WakeupThread::_s_thread_func") ;
		::stop_callback stop_cb {
			stop
		,	[&]() {
				Trace trace("WakeupThread::_s_thread_func::stop_cb") ;
				this_->_request_stop() ;
			}
		} ;
		for(;;) {
			this_->_state.wait(WakeupState::Wait) ;
			switch (this_->_state) {
				case WakeupState::Proceed : this_->_state = WakeupState::Wait ; func(stop) ; break     ;
				case WakeupState::Last    : { if (Flush) func(stop) ; }                      goto Done ;
				case WakeupState::Stop    :                                                  goto Done ;
			DF}                                                                                          // NO_COV
		}
	Done :
		trace("done") ;
	}
	// cxtors & casts
public :
	WakeupThread() = default ;
	//
	WakeupThread ( char key , ::function<void(            )> f ) { open(key,f)     ; }
	WakeupThread ( char key , ::function<void(::stop_token)> f ) { open(key,f)     ; }
	~WakeupThread(                                             ) { _request_stop() ; }
	//
	void open( char key , ::function<void(            )> f ) { thread = ::jthread( _s_thread_func , key , this , [=](::stop_token)->void {f();}) ; }
	void open( char key , ::function<void(::stop_token)> f ) { thread = ::jthread( _s_thread_func , key , this , f                             ) ; }
	// services
	void wakeup() {
		switch (_state) {
			case WakeupState::Wait : _state = WakeupState::Proceed ; _state.notify_one() ; break ;
		DN}                                                                                              // NO_COV
	}
private :
	void _request_stop() {
		switch (_state) {
			case WakeupState::Proceed : _state = WakeupState::Last ; _state.notify_one() ; break ;
			case WakeupState::Wait    : _state = WakeupState::Stop ; _state.notify_one() ; break ;
		DN}                                                                                              // NO_COV
	}
	// data
	Atomic<WakeupState,MutexLvl::Thread> _state = WakeupState::Wait ;
public :
	::jthread thread ;                                                                                   // ensure thread is last so other fields are constructed when it starts
} ;

enum class ServerThreadEventKind : uint8_t {
	Master
,	Slave
,	Stop
} ;
template<class T,bool Flush=true> struct ServerThread {                            // if Flush, finish on going connections
	using Delay     = Time::Delay           ;
	using EventKind = ServerThreadEventKind ;
private :
	static void _s_thread_func( ::stop_token stop , char key , ServerThread* this_ , ::function<bool/*keep_fd*/(::stop_token,T&&,SlaveSockFd const&)> func ) {
		using Event = Epoll<EventKind>::Event ;
		t_thread_key = key ;
		EventFd            stop_fd { New } ;
		Epoll<EventKind>   epoll   { New } ;
		::umap<Fd,IMsgBuf> slaves  ;
		::stop_callback    stop_cb {                                               // transform request_stop into an event Epoll can wait for
			stop
		,	[&]() {
				Trace trace("ServerThread::_s_thread_func::stop_cb",stop_fd) ;
				stop_fd.wakeup() ;
			}
		} ;
		//
		Trace trace("ServerThread::_s_thread_func",this_->fd,this_->fd.port(),stop_fd) ;
		this_->_ready.count_down() ;
		//
		epoll.add_read( this_->fd , EventKind::Master ) ;
		epoll.add_read( stop_fd   , EventKind::Stop   ) ;
		for(;;) {
			trace("wait") ;
			::vector<Event> events = epoll.wait(+epoll?Delay::Forever:Delay()) ;   // wait for 1 event, no timeout unless stopped
			if (!events) { SWEAR(Flush) ; return ; }                               // if !Flush, we should have returned immediately
			for( Event event : events ) {
				EventKind kind = event.data() ;
				Fd        efd  = event.fd  () ;
				trace("waited",efd,kind) ;
				switch (kind) {
					case EventKind::Master : {
						SWEAR(efd==this_->fd) ;
						try {
							Fd slave_fd = this_->fd.accept().detach() ;
							trace("new_req",slave_fd) ;
							epoll.add_read( slave_fd , EventKind::Slave ) ;
							slaves.try_emplace(slave_fd) ;
						} catch (::string const& e) { trace("cannot_accept",e) ; } // ignore error as this may be fd starvation and client will retry
					} break ;
					case EventKind::Stop : {
						stop_fd.flush() ;
						trace("stop",mk_key_vector(slaves)) ;
						for( auto const& [sfd,_] : slaves ) epoll.close(false/*write*/,sfd) ;
						trace("done") ;
						if (Flush) epoll.dec() ;                                   // dont wait for new incoming connections, but finish on going connections and process what comes
						else       return ;                                        // stop immediately
					} break ;
					case EventKind::Slave : {
						auto          it = slaves.find(efd) ;
						::optional<T> r  ;
						try {
							r = it->second.receive_step<T>(efd) ;
							if (!r) { trace("partial") ; continue ; }
						} catch (::string const& e) {
							if (+e) trace("malformed",e) ;                         // close upon malformed messages
							else    trace("eof"        ) ;
							epoll.close( false/*write*/ , efd ) ;
							slaves.erase(it) ;
							continue ;
						}
						SlaveSockFd ssfd { efd }                            ;
						bool        keep = func( stop , ::move(*r) , ssfd ) ;
						if (keep) {
							ssfd.detach() ;                                        // dont close ssfd if requested to keep it
						} else {
							slaves.erase(it) ;
							epoll.del( false/*write*/ , efd ) ;
						}
						trace("called",STR(keep)) ;
					} break ;
				DF}                                                                // NO_COV
			}
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	ServerThread() = default ;
	ServerThread( char key , ::function<bool/*keep_fd*/(             T&&,SlaveSockFd const&)> f , int backlog=0 ) { open(key,f,backlog) ; }
	ServerThread( char key , ::function<bool/*keep_fd*/(::stop_token,T&&,SlaveSockFd const&)> f , int backlog=0 ) { open(key,f,backlog) ; }
	//
	void open( char key , ::function<bool/*keep_fd*/(::stop_token,T&&,SlaveSockFd const&)> f , int backlog=0 ) {
		fd     = { New , backlog , true/*reuse_addr*/ }       ;
		thread = ::jthread( _s_thread_func , key , this , f ) ;
	}
	void open( char key , ::function<bool/*keep_fd*/(T&&,SlaveSockFd const&)> f , int backlog=0 ) {
		open( key , [=](::stop_token,T&& r,SlaveSockFd const& fd)->bool/*keep_fd*/ { return f(::move(r),fd) ; } , backlog ) ;
	}
	// services
	void wait_started() {
		_ready.wait() ;
	}
	// data
	ServerSockFd fd ;
private :
	::latch   _ready  { 1 } ;
public :
	::jthread thread ;                                                             // ensure thread is last so other fields are constructed when it starts
} ;
