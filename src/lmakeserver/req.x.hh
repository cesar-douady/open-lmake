// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "rpc_client.hh"

#ifdef STRUCT_DECL
namespace Engine {

	struct Req     ;
	struct ReqData ;
	template<class W> struct ReqInfo ;

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
		static Idx           s_n_reqs     () {                                    return s_reqs_by_start.size() ; }
		static ::vector<Req> s_reqs_by_eta() { ::unique_lock lock{s_reqs_mutex} ; return _s_reqs_by_eta         ; }
		// static data
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
		Req( Fd , ::vector<Node> const& targets , ReqOptions const& ) ;
		// accesses
		ReqData const& operator* () const ;
		ReqData      & operator* ()       ;
		ReqData const* operator->() const { return &**this ; }
		ReqData      * operator->()       { return &**this ; }
		// services
		void make   () ;
		void kill   () ;
		void close  () ;
		void chk_end() ;
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
		Time::Pdate eta                    ;
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

	template<class W> ::ostream& operator<<( ::ostream& , ReqInfo<W> const& ) ;
	template<class W> struct ReqInfo {
		static_assert(IsWatcher<W>) ;
		friend Req ;
		friend ::ostream& operator<< <>( ::ostream& , ReqInfo const& ) ;
		using Idx = ReqIdx ;
		static constexpr uint8_t NWatchers  = sizeof(::vector<W>)/sizeof(W) ;  // size of array that first within the layout of a vector
		static constexpr uint8_t VectorMrkr = NWatchers+1                   ;  // special value to mean that watchers are in vector
		// cxtors & casts
		 ReqInfo(Req r={}) : req{r} , _n_watchers{0} , _watchers_a{} {}
		 ~ReqInfo() {
		 	if (_n_watchers==VectorMrkr) _watchers_v.~vector() ;
			else                         _watchers_a.~array () ;
		 }
		ReqInfo(ReqInfo&& ri) { *this = ::move(ri) ; }
		ReqInfo& operator=(ReqInfo&& ri) {
			req         = ri.req    ;
			action      = ri.action ;
			n_wait      = ri.n_wait ;
			if (_n_watchers==VectorMrkr) _watchers_v.~vector() ;
			else                         _watchers_a.~array () ;
			_n_watchers = ri._n_watchers ;
			if (_n_watchers==VectorMrkr) new(&_watchers_v) ::vector<W          >{::move(ri._watchers_v)} ;
			else                         new(&_watchers_a) ::array <W,NWatchers>{::move(ri._watchers_a)} ;
			return *this ;
		}
		// acesses
		bool waiting     () const { return n_wait      ; }
		bool has_watchers() const { return _n_watchers ; }
		// services
	private :
		void _add_watcher(W watcher) {
			//                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			if (_n_watchers==VectorMrkr) { _watchers_v.emplace_back(watcher)    ; return ; } // vector stays vector, simple
			if (_n_watchers< NWatchers ) { _watchers_a[_n_watchers++] = watcher ; return ; } // array  stays array , simple
			//                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			// array becomes vector, complex
			::array<W,NWatchers> tmp = _watchers_a ;
			_watchers_a.~array() ;
			new(&_watchers_v) ::vector<W>(NWatchers+1) ;                       // cannot put {} here or it is taken as an initializer list
			for( uint8_t i=0 ; i<NWatchers ; i++ ) _watchers_v[i] = tmp[i] ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			_watchers_v[NWatchers] = watcher ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			_n_watchers = VectorMrkr ;
		}
	public :
		void add_watcher( W watcher , typename W::ReqInfo& watcher_req_info ) {
			_add_watcher(watcher) ;
			watcher_req_info.n_wait++ ;
		}
		void wakeup_watchers() {
			SWEAR(!waiting()) ;                                                // dont wake up watchers if we are not ready
			::vector<W> watchers ;                                             //  we need to copy watchers aside before calling them as during a call, we could become not done and be waited for again
			if (_n_watchers==VectorMrkr) {
				watchers = ::move(_watchers_v) ;
				_watchers_v.~vector() ;                                        // transform vector into array as there is no watchers any more
				new(&_watchers_a) ::array<W,NWatchers> ;
			} else {
				watchers = mk_vector(::vector_view(_watchers_a.data(),_n_watchers)) ;
			}
			_n_watchers = 0 ;
			// we are done for a given RunAction, but calling make on a dependent may raise the RunAciton and we can become waiting() again
			for( auto it = watchers.begin() ; it!=watchers.end() ; it++ )
				if (waiting()) _add_watcher(*it) ;                                           // if waiting again, add back watchers we have got and that we no more want to call
				else           (*it)->make( (*it)->req_info(req) , W::MakeAction::Wakeup ) ; // ok, we are still done, we can call watcher
		}
		::vector<W> watchers() const {
			if (_n_watchers==VectorMrkr) return _watchers_v                                              ;
			else                         return mk_vector(::vector_view(_watchers_a.data(),_n_watchers)) ;
		}
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
		W::Idx      n_wait     = 0                 ;                                  // ~20<=31 bits, INVARIANT : number of watchers pointing to us + 1 if job is submitted
		CoarseDelay pressure   ;                                                      //      16 bits, critical delay from end of job to end of req
		Req         req        ;                                                      //       8 bits
		RunAction   action  :3 = RunAction::Status ; static_assert(+RunAction::N<8) ; //       3 bits
		bool        live_out:1 = false             ;                                  //       1 bit  , if true <=> generate live output
	private :
		uint8_t _n_watchers:3 = 0 ; static_assert(VectorMrkr<8) ;              //  3   bits, number of watchers, if NWatcher <=> watchers is a vector
		union { /* use array as long as possible, and vector when overflow*/   // 64*3 bits, notify watchers when done
			::vector<W          > _watchers_v ;
			::array <W,NWatchers> _watchers_a ;
		} ;
	} ;
	static_assert(sizeof(ReqInfo<Node>)==32) ;                                 // check expected size
	static_assert(sizeof(ReqInfo<Job >)==32) ;                                 // check expected size

}
#endif
#ifdef DATA_DEF
namespace Engine {

	struct ReqData {
		friend struct Req ;
		using Idx = Req::Idx ;
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
		void audit_info( Color c , ::string const& t ,          DepDepth l=0 ) const { audit(audit_fd,log_stream,options,c,l,t                  ) ; }
		void audit_node( Color c , ::string const& p , Node n , DepDepth l=0 ) const { audit(audit_fd,log_stream,options,c,l,p, +n?n.name():""s ) ; }
		//
		void audit_job( Color , Pdate , ::string const& step , Rule , ::string const& job_name , ::string const& host={} , Delay exec_time={} ) const ;
		void audit_job( Color , Pdate , ::string const& step , Job                             , ::string const& host={} , Delay exec_time={} ) const ;
		void audit_job( Color , Pdate , ::string const& step , JobExec const&                                            , Delay exec_time={} ) const ;
		//
		void audit_job( Color c , ::string const& s , Rule r , ::string const& jn , ::string const& h={} , Delay et={} ) const { audit_job(c,Pdate::s_now()                  ,s,r,jn,h,et) ; }
		void audit_job( Color c , ::string const& s , Job j                       , ::string const& h={} , Delay et={} ) const { audit_job(c,Pdate::s_now()                  ,s,j   ,h,et) ; }
		void audit_job( Color c , ::string const& s , JobExec const& je , bool at_end=false              , Delay et={} ) const { audit_job(c,at_end?je.end_date:je.start_date,s,je    ,et) ; }
		//
		void         audit_status( bool ok                                                                                                ) const ;
		void         audit_stats (                                                                                                        ) const ;
		bool/*seen*/ audit_stderr( AnalysisErr const& analysis_err , ::string const& stderr , size_t max_stderr_lines=-1 , DepDepth lvl=0 ) const ;
	private :
		bool/*overflow*/ _send_err( bool intermediate , ::string const& pfx , Node , size_t& n_err , DepDepth lvl ) ;
		// data
	public :
		Idx                  idx_by_start   = Idx(-1) ;
		Idx                  idx_by_eta     = Idx(-1) ;
		Owned<Job>           job            ;
		InfoMap<Job >        jobs           ;
		InfoMap<Node>        nodes          ;
		::umap<Job,JobAudit> missing_audits ;
		bool                 zombie         = false   ;    // req has been killed, waiting to be closed when all jobs are actually killed
		ReqStats             stats          ;
		Fd                   audit_fd       ;              // to report to user
		OFStream  mutable    log_stream     ;              // saved output
		ReqOptions           options        ;
		Ddate                start          ;
		Delay                ete            ;              // Estimated Time Enroute
		::umap<Rule,JobIdx > ete_n_rules    ;              // number of jobs participating to stats.ete with exec_time from rule
		::umap<Job ,JobIdx > frozens        ;              // frozen     jobs  to report in summary             (value is just for summary ordering purpose)
		::umap<Node,NodeIdx> no_triggers    ;              // no-trigger nodes to report in summary             (value is just for summary ordering purpose)
		::umap<Node,NodeIdx> clash_nodes    ;              // nodes that have been written by simultaneous jobs (value is just for summary ordering purpose)
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

	//
	// ReqInfo
	//

	template<class W> ::ostream& operator<<( ::ostream& os , ReqInfo<W> const& ri ) {
		return os<<"ReqInfo("<<ri.req<<','<<ri.action<<','<<ri.n_wait<<')' ;
	}

	//
	// ReqData
	//

	inline void ReqData::audit_job( Color c , Pdate d , ::string const& s , Job j , ::string const& h , Delay et ) const { audit_job( c , d , s , j->rule , j.name() , h       , et ) ; }
	inline void ReqData::audit_job( Color c , Pdate d , ::string const& s , JobExec const& je         , Delay et ) const { audit_job( c , d , s , je                 , je.host , et ) ; }

}
#endif
