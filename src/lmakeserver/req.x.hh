// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "rpc_client.hh"

#include "idxed.hh"

#ifdef STRUCT_DECL
namespace Engine {

	struct Req     ;
	struct ReqData ;
	struct ReqInfo ;

	ENUM_1( RunAction                // each action is included in the following one
	,	Run = Dsk                      // for Job  : run job
	,	None                           // when used as done_action, means not done at all
	,	Makable                        // do whatever is necessary to assert if job can be run / node does exist (data dependent)
	,	Status                         // check deps (no disk access except sources), run if possible & necessary
	,	Dsk                            // for Node : ensure up-to-date on disk
	)

	ENUM( JobLvl                       // must be in chronological order
	,	None                           // no analysis done yet (not in stats)
	,	Dep                            // analyzing deps
	,	Hit                            // cache hit
	,	Queued                         // waiting for execution
	,	Exec                           // executing
	,	Done                           // done execution
	,	End                            // job execution just ended (not in stats)
	)

	ENUM_1( JobReport
	,	Useful = Failed                // <=Useful means job was usefully run
	,	Steady
	,	Done
	,	Failed
	,	Rerun
	,	Hit
	)

}
#endif
#ifdef STRUCT_DEF
namespace Engine {

	template<class JN> concept IsWatcher = IsOneOf<JN,Job,Node> ;

	using AnalysisErr = ::vector<pair_s<Node>> ;

	struct Req
	:	             Idxed<ReqIdx>
	{	using Base = Idxed<ReqIdx> ;
		friend ::ostream& operator<<( ::ostream& , Req const ) ;
		using Chrono    = ReqChrono                    ;
		using ErrReport = ::vmap<Node,DepDepth/*lvl*/> ;
		// init
		static void s_init() {}
		// statics
		template<class T> requires(IsOneOf<T,JobData,NodeData>) static ::vector<Req> reqs(T const& jn) { // sorted by start
			::vector<Req> res ; res.reserve(s_reqs_by_start.size()) ;                                    // pessimistic
			for( Req r : s_reqs_by_start ) if (jn.has_req(r)) res.push_back(r) ;
			return res ;
		}
		//
		static Idx               s_n_reqs     () {                                    return s_reqs_by_start.size() ; }
		static ::vector<Req>     s_reqs_by_eta() { ::unique_lock lock{s_reqs_mutex} ; return _s_reqs_by_eta         ; }
		static ::vmap<Req,Pdate> s_etas       () ;
		//
		static void s_tick() ;
		static bool s_before( Chrono c1 , Chrono c2 ) {
			static_assert(::is_unsigned_v<Chrono>) ;                           // unsigned arithmetic are guaranteed to work modulo 2^n
			SWEAR( c1 && c2 && s_chrono , c1 , c2 , s_chrono ) ;
			return Chrono(s_chrono-c1) > Chrono(s_chrono-c2) ;                 // following expr can be analyzed as :
			/**/                                                               // - roughly, c1 is before c2 if c1<c2, or if -c1>-c2
			/**/                                                               // - we must interpret values as relative to s_chrono so wrapping is supported
			/**/                                                               // - s_chrono is super late, i.e. later than latest Req, so if c1==s_chrono, it is before nothing
		}
		static Chrono s_next_chrono() { return s_chrono ; }
		// static data
		static Chrono            s_chrono        ;
		static SmallIds<ReqIdx > s_small_ids     ;
		static ::vector<Req>     s_reqs_by_start ;         // INVARIANT : ordered by item->start
		//
		static ::mutex           s_reqs_mutex ;            // protects s_store, _s_reqs_by_eta as a whole
		static ::vector<ReqData> s_store      ;
	private :
		static ::vector<Req> _s_reqs_by_eta ;              // INVARIANT : ordered by item->stats.eta
		// cxtors & casts
	public :
		using Base::Base ;
		Req(EngineClosureReq const&) ;
		// accesses
		ReqData const& operator* () const ;
		ReqData      & operator* ()       ;
		ReqData const* operator->() const { return &**this ; }
		ReqData      * operator->()       { return &**this ; }
		// services
		void make   (                       ) ;
		void kill   (                       ) ;
		void close  (bool close_backend=true) ;
		void chk_end(                       ) ;
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
		void             _report_no_rule( Node                                                                                               , DepDepth lvl=0 ) ;
	} ;

	struct ReqStats {
	private :
		static bool   s_valid_cur(JobLvl i) {                         return i>JobLvl::None && i<=JobLvl::Done ; }
		static size_t s_mk_idx   (JobLvl i) { SWEAR(s_valid_cur(i)) ; return +i-(+JobLvl::None+1) ;              }
		//services
	public :
		JobIdx const& cur  (JobLvl    i) const { SWEAR( s_valid_cur(i) ) ; return _cur  [s_mk_idx(i)] ; }
		JobIdx      & cur  (JobLvl    i)       { SWEAR( s_valid_cur(i) ) ; return _cur  [s_mk_idx(i)] ; }
		JobIdx const& ended(JobReport i) const {                           return _ended[+i         ] ; }
		JobIdx      & ended(JobReport i)       {                           return _ended[+i         ] ; }
		//
		JobIdx cur   () const { JobIdx res = 0 ; for( JobLvl    i : JobLvl::N    ) if (s_valid_cur(i)      ) res+=cur  (i) ; return res ; }
		JobIdx useful() const { JobIdx res = 0 ; for( JobReport i : JobReport::N ) if (i<=JobReport::Useful) res+=ended(i) ; return res ; }
		// data
		Time::Pdate start                  ;
		Time::Delay jobs_time[2/*useful*/] ;
	private :
		JobIdx _cur  [+JobLvl::Done+1-(+JobLvl::None+1)] = {} ;
		JobIdx _ended[+JobReport::N                    ] = {} ;
	} ;

	struct JobAudit {
		friend ::ostream& operator<<( ::ostream& os , JobAudit const& ) ;
		// data
		bool        hit          = false/*garbage*/ ;      // else it is a rerun
		bool        modified     = false/*garbage*/ ;
		AnalysisErr analysis_err ;
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
		using Chrono = ReqChrono ;
		static constexpr uint8_t NWatchers  = sizeof(::vector<Watcher>*)/sizeof(Watcher) ; // size of array that fits within the layout of a pointer
		static constexpr uint8_t VectorMrkr = NWatchers+1                                ; // special value to mean that watchers are in vector
		// cxtors & casts
		 ReqInfo(Req r={}) : req{r} , _n_watchers{0} , _watchers_a{} {}
		 ~ReqInfo() {
		 	if (_n_watchers==VectorMrkr) delete _watchers_v ;
			else                         _watchers_a.~array () ;
		 }
		ReqInfo(ReqInfo&& ri) { *this = ::move(ri) ; }
		ReqInfo& operator=(ReqInfo&& ri) {
			req         = ri.req    ;
			action      = ri.action ;
			n_wait      = ri.n_wait ;
			if (_n_watchers==VectorMrkr) delete _watchers_v   ;
			else                         _watchers_a.~array() ;
			_n_watchers = ri._n_watchers ;
			if (_n_watchers==VectorMrkr) _watchers_v = new ::vector<Watcher          >{::move(*ri._watchers_v)} ;
			else                         new(&_watchers_a) ::array <Watcher,NWatchers>{::move( ri._watchers_a)} ;
			return *this ;
		}
		// acesses
		bool waiting         () const { return n_wait                                                      ; }
		bool has_watchers    () const { return _n_watchers                                                 ; }
		WatcherIdx n_watchers() const { return _n_watchers==VectorMrkr ? _watchers_v->size() : _n_watchers ; }
		// services
	private :
		void _add_watcher(Watcher watcher) ;
	public :
		template<class RI> void add_watcher( Watcher watcher , RI& watcher_req_info ) {
			_add_watcher(watcher) ;
			watcher_req_info.n_wait++ ;
		}
		void wakeup_watchers() ;
		//
		bool/*propagate*/ set_pressure(CoarseDelay pressure_) {
			if (pressure_<=pressure) return false ;
			// pressure precision is about 10%, a reasonable compromise with perf (to avoid too many pressure updates)
			// because as soon as a new pressure is discovered for a Node/Job, it is propagated down the path while it may not be the definitive value
			// imposing an exponential progression guarantees a logarithmic number of updates
			// also, in case of loop, this limits the number of loops to 10
			bool propagate = pressure_>pressure.scale_up(10/*percent*/) ;
			pressure = pressure_ ;
			return propagate ;
		}
		// data
		WatcherIdx  n_wait     = 0                 ;                                  // ~20<=31 bits, INVARIANT : number of watchers pointing to us + 1 if job is submitted
		CoarseDelay pressure   ;                                                      //      16 bits, critical delay from end of job to end of req
		Req         req        ;                                                      //       8 bits
		RunAction   action  :3 = RunAction::Status ; static_assert(+RunAction::N<8) ; //       3 bits
		bool        live_out:1 = false             ;                                  //       1 bit  , if true <=> generate live output
	private :
		uint8_t _n_watchers:2 = 0 ; static_assert(VectorMrkr<4) ;              //  2   bits, number of watchers, if NWatcher <=> watchers is a vector
		union { /* use array as long as possible, and vector when overflow*/   // 64 bits, notify watchers when done
			::vector<Watcher          >* _watchers_v = nullptr ;
			::array <Watcher,NWatchers>  _watchers_a ;
		} ;
	} ;
	static_assert(sizeof(ReqInfo)==16) ;                                       // check expected size

}
#endif
#ifdef DATA_DEF
namespace Engine {

	struct ReqData {
		friend struct Req ;
		using Idx    = ReqIdx    ;
		using Chrono = ReqChrono ;
		template<IsWatcher T> struct InfoMap : ::umap<T,typename T::ReqInfo> { typename T::ReqInfo dflt ; } ;
		static constexpr size_t StepSz = 14 ;                                                                 // size of the field representing step in output
		// static data
	private :
		static ::mutex _s_audit_mutex ;                    // should not be static, but if per ReqData, this would prevent ReqData from being movable
		// cxtors & casts
	public :
		void clear() ;
		// accesses
		bool   is_open  () const { return idx_by_start!=Idx(-1)                             ; }
		JobIdx n_running() const { return stats.cur(JobLvl::Queued)+stats.cur(JobLvl::Exec) ; }
		// services
		void audit_summary(bool err) const ;
		//
		void audit_info( Color c , ::string const& t , ::string const& lt , DepDepth l=0 ) const { audit(audit_fd,log_stream,options,c,l,t,lt) ; }
		void audit_info( Color c , ::string const& t ,                      DepDepth l=0 ) const { audit_info( c , t , {}               , l )  ; }
		void audit_node( Color c , ::string const& p , Node n             , DepDepth l=0 ) const ;
		//
		void audit_job( Color , Pdate , ::string const& step , Rule , ::string const& job_name , in_addr_t host=NoSockAddr , Delay exec_time={} ) const ;
		void audit_job( Color , Pdate , ::string const& step , Job                             , in_addr_t host=NoSockAddr , Delay exec_time={} ) const ;
		void audit_job( Color , Pdate , ::string const& step , JobExec const&                                              , Delay exec_time={} ) const ;
		//
		void audit_job( Color c , ::string const& s , Rule r , ::string const& jn , in_addr_t h=NoSockAddr , Delay et={} ) const { audit_job(c,Pdate::s_now()                    ,s,r,jn,h,et) ; }
		void audit_job( Color c , ::string const& s , Job j                       , in_addr_t h=NoSockAddr , Delay et={} ) const { audit_job(c,Pdate::s_now()                    ,s,j   ,h,et) ; }
		void audit_job( Color c , ::string const& s , JobExec const& je , bool at_end=false                , Delay et={} ) const { audit_job(c,at_end?je.end_.date:je.start_.date,s,je    ,et) ; }
		//
		void         audit_status( bool ok                                                                                                                              ) const ;
		void         audit_stats (                                                                                                                                      ) const ;
		bool/*seen*/ audit_stderr( ::string const& backend_msg , AnalysisErr const& analysis_err , ::string const& stderr , size_t max_stderr_lines=-1 , DepDepth lvl=0 ) const ;
		bool/*seen*/ audit_stderr(                               AnalysisErr const& analysis_err , ::string const& stderr , size_t max_stderr_lines=-1 , DepDepth lvl=0 ) const {
			return audit_stderr( {}/*backend_msg*/ , analysis_err , stderr , max_stderr_lines , lvl ) ;
		}
	private :
		bool/*overflow*/ _send_err( bool intermediate , ::string const& pfx , ::string const& name , size_t& n_err , DepDepth lvl ) ;
		// data
	public :

		Chrono                                chrono         = 0/*garbage*/ ;
		Idx                                   idx_by_start   = Idx(-1)      ;
		Idx                                   idx_by_eta     = Idx(-1)      ;
		Job                                   job            ;                 // owned if job->rule->special==Special::Req
		InfoMap<Job >                         jobs           ;
		InfoMap<Node>                         nodes          ;
		::umap<Job,JobAudit>                  missing_audits ;
		bool                                  zombie         = false        ;  // req has been killed, waiting to be closed when all jobs are actually killed
		ReqStats                              stats          ;
		Fd                                    audit_fd       ;                 // to report to user
		OFStream mutable                      log_stream     ;                 // saved output
		Job      mutable                      last_info      ;                 // used to identify last message to generate an info line in case of ambiguity
		ReqOptions                            options        ;
		Ddate                                 start          ;
		Delay                                 ete            ;                 // Estimated Time Enroute
		Pdate                                 eta            ;                 // Estimated Time of Arrival
		::umap<Rule,JobIdx >                  ete_n_rules    ;                 // number of jobs participating to stats.ete with exec_time from rule
		// summary
		::vector<Node>                        up_to_dates    ;                 // asked nodes already done when starting
		::umap<Job ,                JobIdx  > losts          ;                 // lost       jobs                                   (value        is just for summary ordering purpose)
		::umap<Node,pair<bool/*ok*/,NodeIdx>> manuals        ;                 // manual files encountered                          (value.second is just for summary ordering purpose)
		::umap<Job ,                JobIdx  > frozens        ;                 // frozen     jobs                                   (value        is just for summary ordering purpose)
		::umap<Node,                NodeIdx > no_triggers    ;                 // no-trigger nodes                                  (value        is just for summary ordering purpose)
		::umap<Node,                NodeIdx > clash_nodes    ;                 // nodes that have been written by simultaneous jobs (value        is just for summary ordering purpose)
	} ;

}
#endif
#ifdef IMPL
namespace Engine {

	//
	// Req
	//

	inline void Req::s_tick() {
		SWEAR(s_chrono,s_chrono) ;
		/**/                                                                    s_chrono++ ;
		if ( !s_chrono                                                        ) s_chrono = 1 ;             // 0 is reserved to mean no info
		if ( !s_reqs_by_start.empty() && s_chrono==s_reqs_by_start[0]->chrono ) throw "chrono overflow"s ;
	}

	inline ReqData const& Req::operator*() const { return s_store[+*this] ; }
	inline ReqData      & Req::operator*()       { return s_store[+*this] ; }
	inline ::vmap<Req,Pdate> Req::s_etas() {
		::unique_lock lock{s_reqs_mutex} ;
		::vmap<Req,Pdate> res ;
		for ( Req r : _s_reqs_by_eta ) res.emplace_back(r,r->eta) ;
		return res ;
	}

	//
	// ReqData
	//

	inline void ReqData::audit_job( Color c , Pdate d , ::string const& s , Job j , in_addr_t h , Delay et ) const { audit_job( c , d , s , j->rule , j->name() , h       , et ) ; }
	inline void ReqData::audit_job( Color c , Pdate d , ::string const& s , JobExec const& je   , Delay et ) const { audit_job( c , d , s , je                  , je.host , et ) ; }

	inline void ReqData::audit_node( Color c , ::string const& p , Node n , DepDepth l ) const { audit_info( c , p , +n?n->name():""s , l )  ; }

}
#endif
