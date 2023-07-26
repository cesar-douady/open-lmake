// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#ifdef STRUCT_DECL
namespace Engine {

	struct Node        ;
	struct UNode       ;
	struct NodeData    ;
	struct NodeReqInfo ;

	struct Target  ;
	struct Targets ;

	struct Deps ;
	struct Dep  ;

	static constexpr uint8_t NodeNGuardBits = 1 ;                              // to be able to make Target

}
#endif
#ifdef STRUCT_DEF
namespace Engine {

	//
	// Node
	//

	ENUM_1( NodeMakeAction
	,	Dec = Wakeup                                                           // >=Dec means n_wait must be decremented
	,	None
	,	Wakeup                                                                 // a job has completed
	)

	struct Node : NodeBase {
		friend ::ostream& operator<<( ::ostream& , Node const ) ;
		friend UNode ;
		using Date       = DiskDate       ;
		using ReqInfo    = NodeReqInfo    ;
		using MakeAction = NodeMakeAction ;
		using LvlIdx     = RuleIdx        ;                                    // lvl may indicate the number of rules tried
		//
		static constexpr RuleIdx NoIdx         = -1                                                   ;
		static constexpr DFlags  SpecialDFlags { DFlag::Essential , DFlag::Required , DFlag::Static } ;
		// cxtors & casts
	public :
		using NodeBase::NodeBase ;
		// accesses
		Bool3 manual_ok(FileInfoDate const&) const ;
		Bool3 manual_ok(                   ) const { return manual_ok(FileInfoDate(name())) ; } // ensure we gather the correct date with NFS
		//
		bool           has_req   (Req           ) const ;
		ReqInfo const& c_req_info(Req           ) const ;
		ReqInfo      & req_info  (Req           ) const ;
		ReqInfo      & req_info  (ReqInfo const&) const ;                      // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs      (              ) const ;
		bool           waiting   (              ) const ;
		bool           err       (ReqInfo const&) const ;
		bool           done      (ReqInfo const&) const ;
		bool           done      (Req           ) const ;
		// services
		::vector<RuleTgt> raw_rule_tgts() const ;
		void              mk_old       () ;
		void              mk_anti_src  () ;
		void              mk_src       () ;
		void              mk_no_src    () ;
		//
		::vector_view_c<JobTgt> prio_job_tgts   ( RuleIdx prio_idx ) const ;
		::vector_view_c<JobTgt> conform_job_tgts( ReqInfo const&   ) const ;
		::vector_view_c<JobTgt> conform_job_tgts(                  ) const ;
		//
		void set_buildable( DepDepth lvl=0                     ) ;             // data independent, may be pessimistic (Maybe instead of Yes)
		void set_pressure ( ReqInfo& ri , CoarseDelay pressure ) const ;
		//
		void set_special( Special , ::vmap<Node,DFlags> const& deps={} ) ;
		//
		ReqInfo const& make( ReqInfo const&     , RunAction=RunAction::Status , MakeAction   =MakeAction::None ) ;
		ReqInfo const& make( ReqInfo const& cri ,                               MakeAction ma                  ) { return make( cri , RunAction::Status , ma ) ; }
		//
		void audit_multi( Req , ::vector<JobTgt> const& ) ;
		//
		bool/*ok*/ forget() ;
		//
		void add_watcher( ReqInfo& ri , Job watcher , Job::ReqInfo& wri , CoarseDelay pressure ) ;
	private :
		void           _set_buildable_raw( DepDepth                                ) ;
		ReqInfo const& _make_raw         ( ReqInfo const& , RunAction , MakeAction ) ;
		void           _set_pressure_raw ( ReqInfo&                                ) const ;
		//
		::pair<Bool3/*buildable*/,RuleIdx/*shorten_by*/> _gather_prio_job_tgts( ::vector<RuleTgt> const& rule_tgts , DepDepth lvl=0 ) ;
		//
		void _set_buildable(Bool3=Bool3::Unknown) ;
	} ;

	//
	// UNode
	//

	struct UNode : Node {                                                      // UNode is a Node with unique data
		friend ::ostream& operator<<( ::ostream& , UNode const ) ;
		// cxtors & casts
		UNode(                 ) = default ;
		UNode(Idx             i) : Node(i) { unique() ; }
		UNode(Node            n) : Node(n) { unique() ; }
		UNode(::string const& n) : Node(n) { unique() ; }
		// accesses
		NodeData const& operator* () const { return _data() ; }
		NodeData      & operator* ()       { return _data() ; }
		NodeData const* operator->() const { return &**this ; }
		NodeData      * operator->()       { return &**this ; }
		// services
		bool/*modified*/ refresh( bool is_lnk , Crc , Date ) ;
		void             refresh(                          ) ;
		//
	} ;

	struct Target : Node {
		static_assert(Node::NGuardBits>=1) ;
		static constexpr uint8_t NGuardBits = Node::NGuardBits-1      ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::ostream& operator<<( ::ostream& , Target const ) ;
		// cxtors & casts
		Target(                        ) = default ;
		Target( Node n , bool iu=false ) : Node(n ) { is_update( +n && iu     ) ; } // if no node, ensure Target appears as false
		Target( Target const& tu       ) : Node(tu) { is_update(tu.is_update()) ; }
		//
		Target& operator=(Target const& tu) { Node::operator=(tu) ; is_update(tu.is_update()) ; return *this ; }
		// accesses
		Idx operator+() const { return Node::operator+() | is_update()<<(NValBits-1) ; }
		//
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) Idx  side(       ) const = delete ; // { return Node::side<W,LSB+1>(   ) ; }
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) void side(Idx val)       = delete ; // {        Node::side<W,LSB+1>(val) ; }
		// services
		bool is_update(        ) const { return Node::side<1>(   ) ; }
		void is_update(bool val)       {        Node::side<1>(val) ; }
	} ;

	struct Targets : TargetsBase {
		friend ::ostream& operator<<( ::ostream& , Targets const ) ;
		// cxtors & casts
		using TargetsBase::TargetsBase ;
	} ;

	//
	// Deps
	//

	struct Deps : DepsBase {
		friend ::ostream& operator<<( ::ostream& , Deps const& ) ;
		// cxtors & casts
		using DepsBase::DepsBase ;
		Deps(::vmap<Node,DFlags> const&) ;
	} ;

	//
	// Dep
	//

	struct Dep : DepDigestBase<Node> {
		friend ::ostream& operator<<( ::ostream& , Dep const& ) ;
		using Base = DepDigestBase<Node> ;
		// cxtors & casts
		using Base::Base ;
		// services
		bool crc_ok     () const ;
		void acquire_crc() ;
	} ;

}
#endif
#ifdef DATA_DEF
namespace Engine {

	struct NodeData {
		using Idx  = Node::Idx  ;
		using Date = Node::Date ;
		static constexpr uint8_t     NBuildable = +Bool3::N+1              ;   // Bool3::Unknown (=Bool3::N) is a possible value
		static constexpr NodeDataIdx NShared    = (NMatchGen+1)*NBuildable ;   // match_gen range is 0-NMachGen inclusive
		// statics
		static NodeData s_mk_shared(NodeDataIdx shared_idx) { return NodeData(shared_idx) ; }
		static constexpr NodeDataIdx s_shared_idx( MatchGen match_gen=0 , Bool3 buildable=Maybe ) {
			return 1 + ( match_gen*NBuildable + +buildable ) ;
		}
		// cxtors & casts
		NodeData(NodeDataIdx shared_idx) {
			NodeDataIdx i = shared_idx-1 ;
			match_gen =       i/NBuildable  ;
			buildable = Bool3(i%NBuildable) ;                                  // avoid narrowing warning
		}
		~NodeData() {
			job_tgts.pop() ;
		}
		// accesses
		bool match_ok          (         ) const {                          return match_gen>=Rule::s_match_gen                                   ; }
		Idx  shared_idx        (         ) const {                          return s_shared_idx( match_gen , buildable )                          ; }
		bool is_src            (         ) const {                          return !job_tgts.empty() && job_tgts[0]->rule.special()==Special::Src ; }
		bool has_actual_job    (         ) const {                          return +actual_job_tgt && !actual_job_tgt->rule.old()                 ; }
		bool has_actual_job    (Job    j ) const { SWEAR(!j ->rule.old()) ; return actual_job_tgt==j                                              ; }
		bool has_actual_job_tgt(JobTgt jt) const { SWEAR(!jt->rule.old()) ; return actual_job_tgt==jt                                             ; }
		bool sharable          (         ) const {
			return
				!date
			&&	crc==Crc::None
			&&	!rule_tgts
			&&	job_tgts.empty()
			&&	!has_actual_job()
			&&	(SWEAR(conform_idx==Node::NoIdx),true)                         // if no job_tgts, conform_idx must be NoIdx
			//	match_gen                                                      // handled by shared nodes
			//	buildable                                                      // handled by shared nodes
			&&	(SWEAR(!uphill                 ),true)                         // if no job _tgts, it cannot be uphill
			&&	(SWEAR(!is_lnk                 ),true)                         // if crc is None, it cannot be a link
			&&	!multi                                                         // exceptional, does not justify to double the number of shared nodes
			;
		}
		bool makable(bool uphill_ok=false) const {
			if (conform_idx==Node::NoIdx) return multi   ;                     // multi is an error case, but is makable
			if (uphill_ok               ) return true    ;
			/**/                          return !uphill ;
		}
		JobTgt conform_job_tgt() const {
			SWEAR(makable()) ;
			return job_tgts[conform_idx] ;
		}
		bool conform() const {
			SWEAR(makable()) ;
			JobTgt cjt = conform_job_tgt() ;
			return cjt->is_special() || has_actual_job_tgt(cjt) ;
		}
		bool err() const {
			if (multi     ) return true                     ;
			if (!makable()) return false                    ;
			if (!conform()) return true                     ;
			else            return conform_job_tgt()->err() ;
		}
		bool is_special() const { return makable(true/*uphill_ok*/) && conform_job_tgt()->is_special() ; }
		// services
		Date db_date() const { return has_actual_job() ? actual_job_tgt->db_date : Date() ; }
		//
		bool read(DFlags dfs) const {                                          // return true <= file was perceived different from non-existent, assuming access provided in dfs
			if (crc==Crc::None  ) return false           ;                     // file does not exist, cannot perceive difference
			if (dfs[DFlag::Stat]) return true            ;                     // if file exists, stat is different
			if (is_lnk          ) return dfs[DFlag::Lnk] ;
			if (!crc            ) return +dfs            ;                     // dont know if file is a link, any access may have perceived a difference
			/**/                  return dfs[DFlag::Reg] ;
			return +dfs ;
		}
		// data
		Date     date                         ;                      // ~40<=64 bits,         deemed ctime (in ns) or when it was known non-existent. 40 bits : lifetime=30 years @ 1ms resolution
		Crc      crc                          = Crc::None      ;     // ~47<=64 bits,         disk file CRC when file's ctime was date. 45 bits : MTBF=1000 years @ 1000 files generated per second.
		RuleTgts rule_tgts                    ;                      // ~20<=32 bits, shared, matching rule_tgts issued from suffix on top of job_tgts, valid if match_ok
		JobTgts  job_tgts                     ;                      //      32 bits, owned , ordered by prio, valid if match_ok
		JobTgt   actual_job_tgt               ;                      //  31<=32 bits, shared, job that generated node
		RuleIdx  conform_idx                  = Node::NoIdx    ;     //      16 bits,         index to job_tgts to first job with execut.ing.ed prio level, if NoIdx <=> uphill or no job found
		MatchGen match_gen     :NMatchGenBits = 0              ;     //       7 bits,         if <Rule::s_match_gen => deem !job_tgts.size() && !rule_tgts && !sure
		bool     uphill        :1             = false          ;     //       1 bit ,         if true <=> node is produced by uphill
		Bool3    buildable     :2             = Bool3::Unknown ;     //       2 bits,         data independent, if Maybe => buildability is data dependent, if Unknown => not yet computed
		bool     is_lnk        :1             = false          ;     //       1 bit ,         if true <=> node is a link (in particular, false if crc==None or Unknown)
		bool     multi         :1             = false          ;     //       1 bit ,         if true <=> several jobs generate this node
		bool     unlinked      :1             = false          ;     //       1 bit ,         if true <=> node as been unlinked by another rule
	} ;
	static_assert(sizeof(NodeData)==32) ;                                      // check expected size

	ENUM( NodeLvl
	,	None                           // reserve value 0 as they are not counted in n_total
	,	Zombie                         // req is zombie but node not marked done yet
	,	Uphill                         // first level set at init, uphill directory
	,	NoJob                          // job candidates are exhausted
	,	Plain                          // >=PlainLvl means plain jobs starting at lvl-Lvl::Plain (all at same priority)
	)

	struct NodeReqInfo : ReqInfo<Job> {                                         // watchers of Node's are Job's
		friend ::ostream& operator<<( ::ostream& os , NodeReqInfo const& ri ) ;
		//
		using Base       = ReqInfo<Job>   ;
		using Lvl        = NodeLvl        ;
		using MakeAction = NodeMakeAction ;
		//
		static constexpr RuleIdx NoIdx = Node::NoIdx ;
		static const     ReqInfo Src   ;
		// cxtors & casts
		using Base::Base ;
		// services
		void update( RunAction , MakeAction , Node ) ;
		// data
	public :
		RuleIdx prio_idx = NoIdx ;     //     16 bits
		bool    done     = false ;     //  1<= 8 bit , if true => analysis is over (non-buildable Node's are automatically done)
		Bool3   err      = No    ;     //  2<= 8 bit , if true => node is in error (typically dangling), if Maybe => node is inadequate (typically overwritten)
	} ;
	static_assert(sizeof(NodeReqInfo)==40) ;                                   // check expected size

}
#endif
#ifdef IMPL
namespace Engine {

	//
	// Node
	//

	inline bool Node::err (ReqInfo const& cri) const { return cri.err!=No || (*this)->err() ;      }
	inline bool Node::done(ReqInfo const& cri) const { return cri.done || (*this)->buildable==No ; }
	inline bool Node::done(Req            r  ) const { return done(c_req_info(r)) ;                }

	inline ::vector_view_c<JobTgt> Node::conform_job_tgts(ReqInfo const& cri) const { return prio_job_tgts(cri.prio_idx) ; }
	inline ::vector_view_c<JobTgt> Node::conform_job_tgts() const {
		// conform_idx is (one of) the producing job, not necessarily the first of the job_tgt's at same prio level
		RuleIdx prio_idx = (*this)->conform_idx ;
		if (prio_idx!=NoIdx) while ( prio_idx && (*this)->job_tgts[prio_idx-1]->rule->prio==(*this)->job_tgts[prio_idx]->rule->prio ) prio_idx-- ; // rewind to start of first job within prio level
		return prio_job_tgts(prio_idx) ;
	}

	inline ::vector<RuleTgt> Node::raw_rule_tgts() const {
		::vector<RuleTgt> rts = s_rule_tgts(name()).view() ;
		::vector<RuleTgt> res ; res.reserve(rts.size())    ;                   // pessimistic reserve ensures no realloc
		Py::Gil           gil ;
		for( RuleTgt const& rt : rts )
			if (+rt.pattern().match(name())) res.push_back(rt) ;
		return res ;
	}

	inline Bool3 Node::manual_ok(FileInfoDate const& fid) const {
		const char* res_str ;
		Bool3       res     ;
		if      ((*this)->crc==Crc::None) { res_str = +fid?"created":"not_exist" ; res = No | !fid ; }
		else if (!fid                   ) { res_str = "disappeared"              ; res = Maybe     ; }
		else if (fid.date<=(*this)->date) { res_str = "steady"                   ; res = Yes       ; }
		else                              { res_str = "newer"                    ; res = No        ; }
		//
		if (res!=Yes) Trace("manual_ok",*this,fid.tag,fid.date,(*this)->crc,(*this)->date,res_str) ;
		return res ;
	}

	inline bool Node::has_req(Req r) const {
		return Req::s_store[+r].nodes.contains(*this) ;
	}
	inline Node::ReqInfo const& Node::c_req_info(Req r) const {
		::umap<Node,ReqInfo> const& req_infos = Req::s_store[+r].nodes ;
		auto                        it        = req_infos.find(*this)  ;       // avoid double look up
		if (it==req_infos.end()) return Req::s_store[+r].nodes.dflt ;
		else                     return it->second                  ;
	}
	inline Node::ReqInfo& Node::req_info(Req r) const {
		return Req::s_store[+r].nodes.try_emplace(*this,NodeReqInfo(r)).first->second ;
	}
	inline Node::ReqInfo& Node::req_info(ReqInfo const& cri) const {
		if (&cri==&Req::s_store[+cri.req].nodes.dflt) return req_info(cri.req)         ; // allocate
		else                                          return const_cast<ReqInfo&>(cri) ; // already allocated, no look up
	}
	inline ::vector<Req> Node::reqs() const { return Req::reqs(*this) ; }

	inline bool Node::waiting() const {
		for( Req r : reqs() ) if (c_req_info(r).waiting()) return true ;
		return false ;
	}

	inline void Node::add_watcher( ReqInfo& ri , Job watcher , Job::ReqInfo& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void Node::_set_buildable(Bool3 b) {
		MatchGen match_gen = b==Bool3::Unknown ? 0 : ::max((*this)->match_gen,Rule::s_match_gen) ;
		if (shared()) {
			mk_shared( match_gen , b ) ;
		} else {
			UNode un{*this} ;
			un->match_gen = match_gen ;
			un->buildable = b         ;
		}
	}

	inline void Node::set_buildable(DepDepth lvl) {
		if ((*this)->match_ok()) return ;                                      // already set
		_set_buildable_raw(lvl) ;
	}

	inline void Node::set_pressure( ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                 // pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_set_pressure_raw(ri) ;
	}

	inline Node::ReqInfo const& Node::make( ReqInfo const& cri , RunAction run_action , MakeAction make_action ) {
		// /!\ do not recognize buildable==No : we must execute set_buildable before in case a non-buildable becomes buildable
		if ( cri.done && ( run_action<=RunAction::Status || !(*this)->unlinked ) ) return cri ;
		return _make_raw(cri,run_action,make_action) ;
	}

	//
	// UNode
	//

	inline void UNode::refresh() {
		FileInfoDate fid{name()} ;
		switch (manual_ok(fid)) {
			case No    : refresh( false/*is_lnk*/ , Crc::Unknown , fid.date          ) ; break ;
			case Maybe : refresh( false/*is_lnk*/ , Crc::None    , DiskDate::s_now() ) ; break ;
			case Yes   :                                                                 break ;
			default : FAIL(fid) ;
		}
	}

	//
	// NodeReqInfo
	//

	inline void NodeReqInfo::update( RunAction run_action , MakeAction make_action , Node node ) {
		if (make_action>=MakeAction::Dec) {
			SWEAR(n_wait) ;
			n_wait-- ;
		}
		if (run_action>action) {                                               // normally, increasing action requires to reset checks
			action = run_action ;
			if (action!=RunAction::Dsk) prio_idx = NoIdx ;                     // except transition Dsk->Run which is no-op for Node
		}
		if (n_wait) return ;
		if      ( req->zombie                                        ) done = true ;
		else if ( node->buildable==Yes && action==RunAction::Makable ) done = true ;
	}

	//
	// Deps
	//

	inline Deps::Deps(::vmap<Node,DFlags> const& static_deps) {
		::vector<Dep> ds ; ds.reserve(static_deps.size()) ;
		for( auto const& [d,f] : static_deps ) { SWEAR(f[DFlag::Static]) ; ds.emplace_back( d , f , DepOrder::Parallel ) ; }
		*this = Deps(ds) ;
	}

	//
	// Dep
	//

	inline bool Dep::crc_ok() const {
		switch (is_date) {
			case No    : return crc().match((*this)->crc) ;
			case Yes   : return false                     ;
			case Maybe : return true                      ;                    // dep was not accessed, always ok
			default : FAIL(is_date) ;
		}
	}

	inline void Dep::acquire_crc() {
		if (is_date!=Yes        ) {                  return ; }                // no need
		if (!date()             ) { crc(Crc::None) ; return ; }                // no date means access did not find a file, crc is None, easy
		if (date()>(*this)->date) {                  return ; }                // file is manual, maybe too early and crc is not updated yet (also works if !(*this)->date)
		if (date()<(*this)->date) { crc({}       ) ; return ; }                // too late, file has changed
		if (!(*this)->crc       ) {                  return ; }                // too early, no crc available yet
		crc((*this)->crc) ;                                                    // got it !
	}

}
#endif
