// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
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
	using L           = Lock<ThreadMutex>       ;
	using Delay       = Time::Delay             ;
	//
	using Q::empty ;
	// accesses
	bool operator+() const {
		Lock<ThreadMutex> lock{_mutex} ;
		return !Q::empty() ;
	}
	//
	void lock        (MutexLvl lvl) const { _mutex.lock        (lvl) ; }
	void unlock      (MutexLvl lvl) const { _mutex.unlock      (lvl) ; }
	void swear_locked(            ) const { _mutex.swear_locked(   ) ; }
	// accesses
	#define LCK Lock lock{_mutex}
	size_t  size() const { LCK ; return Q::size() ; }
	// services
	template<class    T> void push_urgent   (T&&    x) { LCK ; Q::push_front   (::forward<T>(x)   ) ; _cond.notify_one() ; }
	template<class    T> void push          (T&&    x) { LCK ; Q::push_back    (::forward<T>(x)   ) ; _cond.notify_one() ; }
	template<class... A> void emplace_urgent(A&&... a) { LCK ; Q::emplace_front(::forward<A>(a)...) ; _cond.notify_one() ; }
	template<class... A> void emplace       (A&&... a) { LCK ; Q::emplace_back (::forward<A>(a)...) ; _cond.notify_one() ; }
	// clear res while we are waiting
	void           pop    (                               Val&/*out*/ res ) { LCK ; bool e = empty() ; if (e) res = {} ;                         _wait(       lock) ;        _pop(res) ;            }
	bool/*popped*/ pop    ( ::stop_token stop ,           Val&/*out*/ res ) { LCK ; bool e = empty() ; if (e) res = {} ; bool p = (Flush&&!e) || _wait(stop,  lock) ; if (p) _pop(res) ; return p ; }
	bool/*popped*/ pop_for(                     Delay d , Val&/*out*/ res ) { LCK ; bool e = empty() ; if (e) res = {} ; bool p = (Flush&&!e) || _wait(     d,lock) ; if (p) _pop(res) ; return p ; }
	bool/*popped*/ pop_for( ::stop_token stop , Delay d,  Val&/*out*/ res ) { LCK ; bool e = empty() ; if (e) res = {} ; bool p = (Flush&&!e) || _wait(stop,d,lock) ; if (p) _pop(res) ; return p ; }
	//
	/**/                  Val  pop    (                             ) { LCK ;                                    _wait(       lock) ; return                   _pop()         ; }
	::pair<bool/*popped*/,Val> pop    ( ::stop_token stop           ) { LCK ; bool popped = (Flush&&!empty()) || _wait(stop,  lock) ; return { popped , popped?_pop():Val() } ; }
	::pair<bool/*popped*/,Val> pop_for(                     Delay d ) { LCK ; bool popped = (Flush&&!empty()) || _wait(     d,lock) ; return { popped , popped?_pop():Val() } ; }
	::pair<bool/*popped*/,Val> pop_for( ::stop_token stop , Delay d ) { LCK ; bool popped = (Flush&&!empty()) || _wait(stop,d,lock) ; return { popped , popped?_pop():Val() } ; }
private :
	void _pop ( Val& res                                              ) { swear_locked() ;     res = ::move(Q::front()) ; Q::pop_front() ;                              }
	Val  _pop (                                                       ) { swear_locked() ; Val res = ::move(Q::front()) ; Q::pop_front() ; return res ;                 }
	void _wait(                               Lock<ThreadMutex>& lock ) {        _cond.wait    ( lock ,                                   [&](){ return !empty() ; } ) ; }
	bool _wait( ::stop_token stop           , Lock<ThreadMutex>& lock ) { return _cond.wait    ( lock , stop ,                            [&](){ return !empty() ; } ) ; }
	bool _wait(                     Delay d , Lock<ThreadMutex>& lock ) { return _cond.wait_for( lock ,        ::chrono::nanoseconds(d) , [&](){ return !empty() ; } ) ; }
	bool _wait( ::stop_token stop , Delay d , Lock<ThreadMutex>& lock ) { return _cond.wait_for( lock , stop , ::chrono::nanoseconds(d) , [&](){ return !empty() ; } ) ; }
	#undef LCK
	// data
private :
	ThreadMutex mutable      _mutex ;
	::condition_variable_any _cond  ;
} ;
template<class T,bool Flush=true> using ThreadDeque = ThreadQueue<::deque<T>,Flush> ;

template<class Q,bool Flush=true,bool QueueAccess=false> struct QueueThread : private ThreadQueue<Q,Flush> { // if Flush, process remaining items when asked to stop
	using Base = ThreadQueue<Q,Flush> ;
	using Val  = typename Base::Val  ;
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
	static void _s_thread_func1( ::stop_token stop , char key , QueueThread* self_ , ::function<void(::stop_token,Val const&)> func ) RQA {
		t_thread_key = key ;
		Trace trace("QueueThread::_s_thread_func1") ;
		while (self_->pop(stop,self_->_cur)) func(stop,self_->_cur) ;
		trace("done") ;
	}
	static void _s_thread_func2( ::stop_token stop , char key , QueueThread* self_ , ::function<void(::stop_token,Val&&)> func ) RNQA {
		t_thread_key = key ;
		Trace trace("QueueThread::_s_thread_func2") ;
		for(;;) {
			auto [popped,info] = self_->pop(stop) ;
			if (!popped) break ;
			func(stop,::move(info)) ;
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	QueueThread() = default ;
	QueueThread( char k , ::function<void(             Val const&)> f ) RQA  { open(k,f) ; }
	QueueThread( char k , ::function<void(::stop_token,Val const&)> f ) RQA  { open(k,f) ; }
	QueueThread( char k , ::function<void(             Val     &&)> f ) RNQA { open(k,f) ; }
	QueueThread( char k , ::function<void(::stop_token,Val     &&)> f ) RNQA { open(k,f) ; }
	void open  ( char k , ::function<void(             Val const&)> f ) RQA  { _thread = ::jthread( _s_thread_func1 , k , this , [=](::stop_token,Val const& v)->void {f(       v );} ) ; }
	void open  ( char k , ::function<void(::stop_token,Val const&)> f ) RQA  { _thread = ::jthread( _s_thread_func1 , k , this , f                                                    ) ; }
	void open  ( char k , ::function<void(             Val     &&)> f ) RNQA { _thread = ::jthread( _s_thread_func2 , k , this , [=](::stop_token,Val     && v)->void {f(::move(v));} ) ; }
	void open  ( char k , ::function<void(::stop_token,Val     &&)> f ) RNQA { _thread = ::jthread( _s_thread_func2 , k , this , f                                                    ) ; }
	// accesses
	Val const& cur() const RQA { return _cur ; }
	// services
	auto begin()       RQA { swear_locked() ; return Base::begin() ; }
	auto begin() const RQA { swear_locked() ; return Base::begin() ; }
	auto end  ()       RQA { swear_locked() ; return Base::end  () ; }
	auto end  () const RQA { swear_locked() ; return Base::end  () ; }
	#undef RQA
	#undef RNQA
	// data
private :
	bool                                  _has_cur = false ;
	::conditional_t<QueueAccess,Val,Void> _cur     ;
	::jthread                             _thread  ; // ensure _thread is last so other fields are constructed before it starts and destructed after it ends
} ;
template<class T,bool Flush=true,bool QueueAccess=false> using DequeThread = QueueThread<::deque<T>,Flush,QueueAccess> ;

template<class T,bool Flush=true> struct TimedDequeThread : ThreadDeque<::pair<Time::Pdate,T>,Flush> { // if Flush, process immediately executable remaining items when asked to stop
	using Base  = ThreadDeque<::pair<Time::Pdate,T>,Flush> ;
	using Pdate = Time::Pdate                              ;
	using Delay = Time::Delay                              ;
	using Val   = T                                        ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , TimedDequeThread* self_ , ::function<void(::stop_token,Val&&)> func ) {
		t_thread_key = key ;
		Trace trace("TimedDequeThread::_s_thread_func") ;
		for(;;) {
			auto [popped,info] = self_->pop(stop) ;
			if (!popped                            ) break ;
			if (!info.first.sleep_until(stop,Flush)) break ;
			func(stop,::move(info.second)) ;
		}
		trace("done") ;
	}
	// cxtors & casts
public :
	TimedDequeThread() = default ;
	TimedDequeThread( char k , ::function<void(             Val&&)> f ) { open(k,f) ; }
	TimedDequeThread( char k , ::function<void(::stop_token,Val&&)> f ) { open(k,f) ; }
	void open       ( char k , ::function<void(             Val&&)> f ) { _thread = ::jthread( _s_thread_func , k , this , [=](::stop_token,Val&& v)->void {f(::move(v));} ) ; }
	void open       ( char k , ::function<void(::stop_token,Val&&)> f ) { _thread = ::jthread( _s_thread_func , k , this , f                                               ) ; }
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
	::jthread _thread ;                                                                                // ensure _thread is last so other fields are constructed when it starts
} ;

ENUM( WakeupState
,	Wait
,	Proceed
,	Last
,	Stop
)

template<bool Flush=true> struct WakeupThread {
	using ThreadMutex = Mutex<MutexLvl::Thread> ;
	// statics
private :
	static void _s_thread_func( ::stop_token stop , char key , WakeupThread* self_ , ::function<void(::stop_token)> func ) {
		t_thread_key = key ;
		Trace trace("WakeupThread::_s_thread_func") ;
		for(;;) {
			self_->_state.wait(WakeupState::Wait) ;
			switch (self_->_state) {
				case WakeupState::Proceed : self_->_state = WakeupState::Wait ; func(stop) ; break     ;
				case WakeupState::Last    : { if (Flush) func(stop) ; }                      goto Done ;
				case WakeupState::Stop    :                                                  goto Done ;
			DF}
		}
	Done :
		trace("done") ;
	}
	// cxtors & casts
public :
	WakeupThread() = default ;
	WakeupThread( char key , ::function<void(            )> f ) { open(key,f) ; }
	WakeupThread( char key , ::function<void(::stop_token)> f ) { open(key,f) ; }
	~WakeupThread() {
		switch (_state) {
			case WakeupState::Proceed : _state = WakeupState::Last ; _state.notify_one() ; break ;
			case WakeupState::Wait    : _state = WakeupState::Stop ; _state.notify_one() ; break ;
		DF}
	}
	void open( char key , ::function<void(            )> f ) { _thread = ::jthread( _s_thread_func , key , this , [=](::stop_token)->void {f();}) ; }
	void open( char key , ::function<void(::stop_token)> f ) { _thread = ::jthread( _s_thread_func , key , this , f                             ) ; }
	// services
	void wakeup() {
		switch (_state) {
			case WakeupState::Proceed : return ;
			case WakeupState::Wait    : break  ;
		DF}
		_state = WakeupState::Proceed ;
		_state.notify_one() ;
	}
private :
	// data
	::atomic<WakeupState> _state = WakeupState::Wait ;
	::jthread             _thread ;                    // ensure _thread is last so other fields are constructed when it starts
} ;

ENUM(ServerThreadEventKind
,	Master
,	Slave
,	Stop
)
template<class Req,bool Flush=true> struct ServerThread {                          // if Flush, finish on going connections
	using Delay     = Time::Delay           ;
	using EventKind = ServerThreadEventKind ;
private :
	static void _s_thread_func( ::stop_token stop , char key , ServerThread* self_ , ::function<bool/*keep_fd*/(::stop_token,Req&&,SlaveSockFd const&)> func ) {
		using Event = Epoll<EventKind>::Event ;
		t_thread_key = key ;
		EventFd            stop_fd { New } ;
		Epoll<EventKind>   epoll   { New } ;
		::umap<Fd,IMsgBuf> slaves  ;
		::stop_callback    stop_cb {                                               // transform request_stop into an event Epoll can wait for
			stop
		,	[&](){
				Trace trace("ServerThread::_s_thread_func::stop_cb",stop_fd) ;
				stop_fd.wakeup() ;
			}
		} ;
		//
		Trace trace("ServerThread::_s_thread_func",self_->fd,self_->fd.port(),stop_fd) ;
		self_->_ready.count_down() ;
		//
		epoll.add_read( self_->fd , EventKind::Master ) ;
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
						SWEAR(efd==self_->fd) ;
						try {
							Fd slave_fd = self_->fd.accept().detach() ;
							trace("new_req",slave_fd) ;
							epoll.add_read(slave_fd,EventKind::Slave) ;
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
						Req r ;
						try         { if (!slaves.at(efd).receive_step(efd,r)) { trace("partial") ; continue ; } }
						catch (...) {                                            trace("bad_msg") ; continue ;   } // ignore malformed messages
						//
						epoll.del(false/*write*/,efd) ;                // Func may trigger efd being closed by another thread, hence epoll.del must be done before
						slaves.erase(efd) ;
						SlaveSockFd ssfd { efd }                     ;
						bool        keep = func(stop,::move(r),ssfd) ;
						if (keep) ssfd.detach() ;                      // dont close ssfd if requested to keep it
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
	ServerThread( char key , ::function<bool/*keep_fd*/(             Req&&,SlaveSockFd const&)> f , int backlog=0 ) { open(key,f,backlog) ; }
	ServerThread( char key , ::function<bool/*keep_fd*/(::stop_token,Req&&,SlaveSockFd const&)> f , int backlog=0 ) { open(key,f,backlog) ; }
	void open( char key , ::function<bool/*keep_fd*/(Req&&,SlaveSockFd const&)> f , int backlog=0 ) {
		fd      = {New,backlog}                                                                                                                            ;
		_thread = ::jthread( _s_thread_func , key , this , [=](::stop_token,Req&& r,SlaveSockFd const& fd)->bool/*keep_fd*/ { return f(::move(r),fd) ; } ) ;
	}
	void open( char key , ::function<bool/*keep_fd*/(::stop_token,Req&&,SlaveSockFd const&)> f , int backlog=0 ) {
		fd      = {New,backlog}                                ;
		_thread = ::jthread( _s_thread_func , key , this , f ) ;
	}
	// services
	void wait_started() {
		_ready.wait() ;
	}
	// data
	ServerSockFd fd ;
private :
	::latch   _ready  { 1 } ;
	::jthread _thread ;                                                // ensure _thread is last so other fields are constructed when it starts
} ;
