// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "idxed.hh"

#ifdef STRUCT_DECL

ENUM_1( JobReport
,	Useful = Failed // <=Useful means job was usefully run
,	Steady
,	Done
,	Failed
,	Completed
,	Killed
,	Lost
,	LostErr
,	Resubmit
,	Rerun
,	Hit
)

namespace Engine {
	struct Req     ;
	struct ReqData ;
	struct ReqInfo ;
}

#endif
#ifdef STRUCT_DEF

namespace Engine {

	template<class JN> concept IsWatcher = IsOneOf<JN,Job,Node> ;

	struct Req
	:	             Idxed<ReqIdx>
	{	using Base = Idxed<ReqIdx> ;
		friend ::ostream& operator<<( ::ostream& , Req const ) ;
		using ErrReport = ::vmap<Node,DepDepth/*lvl*/> ;
		// init
		static void s_init() {}
		// statics
		template<class T> requires(IsOneOf<T,JobData,NodeData>) static ::vector<Req> s_reqs(T const& jn) {                   // sorted by start
			::vector<Req> res ; res.reserve(s_reqs_by_start.size()) ;                                                        // pessimistic
			for( Req r : s_reqs_by_start ) if (jn.has_req(r)) res.push_back(r) ;
			return res ;
		}
		//
		static Idx               s_n_reqs     () {                           return s_reqs_by_start.size() ; }
		static ::vector<Req>     s_reqs_by_eta() { Lock lock{s_reqs_mutex} ; return _s_reqs_by_eta         ; }
		static ::vmap<Req,Pdate> s_etas       () ;
		// static data
		static SmallIds<ReqIdx,true/*ThreadSafe*/> s_small_ids     ;
		static ::vector<Req>                       s_reqs_by_start ;                                                         // INVARIANT : ordered by item->start
		//
		static Mutex<MutexLvl::Req>                s_reqs_mutex ;                                                            // protects s_store, _s_reqs_by_eta as a whole
		static ::vector<ReqData>                   s_store      ;
	private :
		static ::vector<Req> _s_reqs_by_eta ;                                                                                // INVARIANT : ordered by item->stats.eta
		static_assert(sizeof(ReqIdx)==1) ;                                                                                   // else an array to hold zombie state is not ideal
		static ::array<atomic<uint8_t>,1<<(sizeof(ReqIdx)*8-3)> _s_zombie_tab ;
		// cxtors & casts
	public :
		using Base::Base ;
		Req(NewType) : Base{s_small_ids.acquire()} {}
		// accesses
		ReqData const& operator* () const ;
		ReqData      & operator* ()       ;
		ReqData const* operator->() const { return &**this ; }
		ReqData      * operator->()       { return &**this ; }
		//
		bool zombie(      ) const { return                     bit( uint8_t(_s_zombie_tab[+*this>>3]) , +*this&0x7     ) ; } // req has been killed, waiting to be closed when all jobs are done
		void zombie(bool z)       { _s_zombie_tab[+*this>>3] = bit( uint8_t(_s_zombie_tab[+*this>>3]) , +*this&0x7 , z ) ; } // dont care about atomicity as this is called by a single thread
		// services
		void make   (EngineClosureReq const&) ;
		void kill   (                       ) ;
		void close  (                       ) ;
		void chk_end(                       ) ;
		void dealloc(                       ) { s_small_ids.release(+*this) ; }
		//
		void inc_rule_exec_time( Rule ,                                            Delay delta     , Tokens1 ) ;
		void new_exec_time     ( JobData const& , bool remove_old , bool add_new , Delay old_exec_time       ) ;
	private :
		void _adjust_eta(bool push_self=false) ;
		//
		template<class... A> ::string _title    (A&&...) const ;
		/**/                 ::string _color_pfx(Color ) const ;
		/**/                 ::string _color_sfx(Color ) const ;
		//
		bool/*overflow*/ _report_err    ( Dep const& , size_t& n_err , bool& seen_stderr , ::uset<Job>& seen_jobs , ::uset<Node>& seen_nodes , DepDepth lvl=0 ) ;
		bool/*overflow*/ _report_err    ( Job , Node , size_t& n_err , bool& seen_stderr , ::uset<Job>& seen_jobs , ::uset<Node>& seen_nodes , DepDepth lvl=0 ) ;
		void             _report_cycle  ( Node                                                                                                                ) ;
	} ;

	struct ReqStats {
	private :
		static bool   s_valid_cur(JobStep i) {                         return i>=JobStep::MinCurStats && i<=JobStep::MaxCurStats1 ; }
		static size_t s_mk_idx   (JobStep i) { SWEAR(s_valid_cur(i)) ; return +i-+JobStep::MinCurStats                            ; }
		//services
	public :
		JobIdx const& cur  (JobStep   i) const { SWEAR( s_valid_cur(i) ) ; return _cur  [s_mk_idx(i)] ; }
		JobIdx      & cur  (JobStep   i)       { SWEAR( s_valid_cur(i) ) ; return _cur  [s_mk_idx(i)] ; }
		JobIdx const& ended(JobReport i) const {                           return _ended[+i         ] ; }
		JobIdx      & ended(JobReport i)       {                           return _ended[+i         ] ; }
		//
		JobIdx cur   () const { JobIdx res = 0 ; for( JobStep   i : All<JobStep  > ) if (s_valid_cur(i)      ) res+=cur  (i) ; return res ; }
		JobIdx useful() const { JobIdx res = 0 ; for( JobReport i : All<JobReport> ) if (i<=JobReport::Useful) res+=ended(i) ; return res ; }
		// data
		Time::Delay jobs_time[2/*useful*/] ;
	private :
		JobIdx _cur  [+JobStep::MaxCurStats1-+JobStep::MinCurStats] = {} ;
		JobIdx _ended[N<JobReport>                                ] = {} ;
	} ;

	struct JobAudit {
		friend ::ostream& operator<<( ::ostream& os , JobAudit const& ) ;
		// data
		JobReport report      = {}   /*garbage*/ ; // if not Hit, it is a rerun and this is the report to do if finally not a rerun
		bool      modified    = false/*garbage*/ ;
		::string  backend_msg ;
	} ;

}

#endif
#ifdef INFO_DEF

namespace Engine {

	using Watcher = Idxed2<Job,Node> ;

	struct ReqInfo {
		friend Req ;
		friend ::ostream& operator<<( ::ostream& , ReqInfo const& ) ;
		using Idx    = ReqIdx    ;
		static constexpr uint8_t NWatchers  = sizeof(::vector<Watcher>*)/sizeof(Watcher) ; // size of array that fits within the layout of a pointer
		static constexpr uint8_t VectorMrkr = NWatchers+1                                ; // special value to mean that watchers are in vector

		struct WaitInc {
			WaitInc (ReqInfo& ri) : _ri{ri} { SWEAR(_ri.n_wait<::numeric_limits<WatcherIdx>::max()) ; _ri.inc_wait() ; }
			~WaitInc(           )           { SWEAR(_ri.n_wait>0                                  ) ; _ri.dec_wait() ; }
		private :
			ReqInfo& _ri ;
		} ;

		// cxtors & casts
		 ReqInfo(Req r={}) : req{r} , _n_watchers{0} , _watchers_a{} {}
		 ~ReqInfo() {
		 	if (_n_watchers==VectorMrkr) delete _watchers_v ;
			else                         _watchers_a.~array () ;
		 }
		ReqInfo(ReqInfo&& ri) { *this = ::move(ri) ; }
		ReqInfo& operator=(ReqInfo&& ri) {
			n_wait   = ri.n_wait   ;
			live_out = ri.live_out ;
			pressure = ri.pressure ;
			req      = ri.req      ;
			if (_n_watchers==VectorMrkr) delete _watchers_v   ;
			else                         _watchers_a.~array() ;
			_n_watchers = ri._n_watchers ;
			if (_n_watchers==VectorMrkr) _watchers_v = new ::vector<Watcher          >{::move(*ri._watchers_v)} ;
			else                         new(&_watchers_a) ::array <Watcher,NWatchers>{::move( ri._watchers_a)} ;
			return *this ;
		}
		// acesses
		void       inc_wait    ()       {                 n_wait++ ; SWEAR(n_wait) ;                       }
		void       dec_wait    ()       { SWEAR(n_wait) ; n_wait-- ;                                       }
		bool       waiting     () const { return n_wait                                                  ; }
		bool       has_watchers() const { return _n_watchers                                             ; }
		WatcherIdx n_watchers  () const { return _n_watchers==VectorMrkr?_watchers_v->size():_n_watchers ; }
		// services
	private :
		void _add_watcher(Watcher watcher) ;
	public :
		template<class RI> void add_watcher( Watcher watcher , RI& watcher_req_info ) {
			_add_watcher(watcher) ;
			watcher_req_info.inc_wait() ;
		}
		void wakeup_watchers() ;
		Job  asking         () const ;
		//
		bool/*propag*/ set_pressure(CoarseDelay pressure_) {
			if (pressure_<=pressure) return false ;
			// pressure precision is about 10%, a reasonable compromise with perf (to avoid too many pressure updates)
			// because as soon as a new pressure is discovered for a Node/Job, it is propagated down the path while it may not be the definitive value
			// imposing an exponential progression guarantees a logarithmic number of updates
			// also, in case of loop, this limits the number of loops to 10
			bool propag = pressure_>pressure.scale_up(10/*percent*/) ;
			pressure = pressure_ ;
			return propag ;
		}
		// data
		WatcherIdx  n_wait  :NBits<WatcherIdx>-1 = 0     ;        // ~20<=31 bits, INVARIANT : number of watchers pointing to us + 1 if job is submitted
		bool        live_out:1                   = false ;        //       1 bit , if true <=> generate live output
		CoarseDelay pressure                     ;                //      16 bits, critical delay from end of job to end of req
		Req         req                          ;                //       8 bits
	private :
		uint8_t _n_watchers:2 = 0 ; static_assert(VectorMrkr<4) ; //       2 bits, number of watchers, if NWatcher <=> watchers is a vector
		union {
			::vector<Watcher          >* _watchers_v ;            //      64 bits, if _n_watchers==VectorMrkr
			::array <Watcher,NWatchers>  _watchers_a ;            //      64 bits, if _n_watchers< VectorMrkr
		} ;
	} ;
	static_assert(sizeof(ReqInfo)==16) ;                          // check expected size

}

#endif
#ifdef DATA_DEF

namespace Engine {

	struct ReqData {
		friend Req ;
		using Idx = ReqIdx ;
		template<IsWatcher T> struct InfoMap : ::umap<T,typename T::ReqInfo> {
			typename T::ReqInfo dflt ;
			using ::umap<T,typename T::ReqInfo>::umap ;
		} ;
		static constexpr size_t StepSz = 14 ;           // size of the field representing step in output
		// static data
	private :
		static Mutex<MutexLvl::Audit> _s_audit_mutex ;  // should not be static, but if per ReqData, this would prevent ReqData from being movable
		// cxtors & casts
	public :
		void clear() ;
		// accesses
		bool   operator+() const { return +job                                                ; }
		bool   operator!() const { return !+*this                                             ; }
		bool   is_open  () const { return idx_by_start!=Idx(-1)                               ; }
		JobIdx n_running() const { return stats.cur(JobStep::Queued)+stats.cur(JobStep::Exec) ; }
		// services
		void audit_summary(bool err) const ;
		//
		#define SC ::string const
		//                                                                                                                                                                     as_is
		void audit_info      ( Color c , SC& t , ::string const& lt , DepDepth l=0 ) const { audit( audit_fd , log_stream , options , c , to_string(t,' ',Disk::mk_file(lt)) , false , l ) ; }
		void audit_info      ( Color c , SC& t ,                      DepDepth l=0 ) const { audit( audit_fd , log_stream , options , c ,           t                        , false , l ) ; }
		void audit_info_as_is( Color c , SC& t ,                      DepDepth l=0 ) const { audit( audit_fd , log_stream , options , c ,           t                        , true  , l ) ; }
		void audit_node      ( Color c , SC& p , Node n             , DepDepth l=0 ) const ;
		//
		void audit_job( Color , Pdate , SC& step , SC& rule_name , SC& job_name , in_addr_t host=NoSockAddr , Delay exec_time={} ) const ;
		void audit_job( Color , Pdate , SC& step , Job                          , in_addr_t host=NoSockAddr , Delay exec_time={} ) const ;
		void audit_job( Color , Pdate , SC& step , JobExec const&                                           , Delay exec_time={} ) const ;
		//
		void audit_job( Color c , SC& s , SC& rn , SC& jn , in_addr_t h=NoSockAddr , Delay et={} ) const { audit_job(c,Pdate(New)                                ,s,rn,jn,h,et) ; }
		void audit_job( Color c , SC& s , Job j           , in_addr_t h=NoSockAddr , Delay et={} ) const { audit_job(c,Pdate(New)                                ,s,j    ,h,et) ; }
		void audit_job( Color c , SC& s , JobExec const& je , bool at_end=false    , Delay et={} ) const { audit_job(c,at_end?je.end_date.date:je.start_date.date,s,je     ,et) ; }
		#undef SC
		//
		void         audit_status( bool ok                                                                                    ) const ;
		void         audit_stats (                                                                                            ) const ;
		bool/*seen*/ audit_stderr( ::string const& msg , ::string const& stderr , size_t max_stderr_lines=-1 , DepDepth lvl=0 ) const ;
	private :
		bool/*overflow*/ _send_err      ( bool intermediate , ::string const& pfx , ::string const& name , size_t& n_err , DepDepth lvl=0 ) ;
		void             _report_no_rule( Node , Disk::NfsGuard&                                                         , DepDepth lvl=0 ) ;
		// data
	public :
		Idx                  idx_by_start   = Idx(-1) ;
		Idx                  idx_by_eta     = Idx(-1) ;
		Job                  job            ;           // owned if job->rule->special==Special::Req
		InfoMap<Job >        jobs           ;
		InfoMap<Node>        nodes          ;
		::umap<Job,JobAudit> missing_audits ;
		ReqStats             stats          ;
		Fd                   audit_fd       ;           // to report to user
		OFStream mutable     log_stream     ;           // saved output
		Job      mutable     last_info      ;           // used to identify last message to generate an info line in case of ambiguity
		ReqOptions           options        ;
		Pdate                start_pdate    ;
		Ddate                start_ddate    ;
		Delay                ete            ;           // Estimated Time Enroute
		Pdate                eta            ;           // Estimated Time of Arrival
		::umap<Rule,JobIdx > ete_n_rules    ;           // number of jobs participating to stats.ete with exec_time from rule
		bool                 has_backend    = false   ;
		// summary
		::vector<Node>        up_to_dates  ;            // asked nodes already done when starting
		::umap<Job ,JobIdx  > frozen_jobs  ;            // frozen     jobs                                   (value is just for summary ordering purpose)
		::umap<Node,NodeIdx > frozen_nodes ;            // frozen     nodes                                  (value is just for summary ordering purpose)
		::umap<Node,NodeIdx > long_names   ;            // nodes with name too long                          (value is just for summary ordering purpose)
		::umap<Node,NodeIdx > no_triggers  ;            // no-trigger nodes                                  (value is just for summary ordering purpose)
		::umap<Node,NodeIdx > clash_nodes  ;            // nodes that have been written by simultaneous jobs (value is just for summary ordering purpose)
	} ;

}

#endif
#ifdef IMPL

namespace Engine {

	//
	// Req
	//

	inline ReqData const& Req::operator*() const { return s_store[+*this] ; }
	inline ReqData      & Req::operator*()       { return s_store[+*this] ; }
	inline ::vmap<Req,Pdate> Req::s_etas() {
		Lock              lock { s_reqs_mutex } ;
		::vmap<Req,Pdate> res  ;
		for ( Req r : _s_reqs_by_eta ) res.emplace_back(r,r->eta) ;
		return res ;
	}

	//
	// ReqData
	//

	inline void ReqData::audit_job( Color c , Pdate d , ::string const& s , Job j , in_addr_t h , Delay et ) const { audit_job( c , d , s , j->rule->name , j->name() , h       , et ) ; }
	inline void ReqData::audit_job( Color c , Pdate d , ::string const& s , JobExec const& je   , Delay et ) const { audit_job( c , d , s , je                        , je.host , et ) ; }

	inline void ReqData::audit_node( Color c , ::string const& p , Node n , DepDepth l ) const { audit_info( c , p , +n?n->name():""s , l )  ; }

}

#endif
