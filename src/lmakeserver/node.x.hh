// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "rpc_job.hh"

#ifdef STRUCT_DECL
namespace Engine {

	struct Node        ;
	struct NodeData    ;
	struct NodeReqInfo ;

	struct Target  ;
	using Targets = TargetsBase ;


	struct Dep  ;
	struct Deps ;

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
		using MakeAction = NodeMakeAction ;
		using ReqInfo    = NodeReqInfo    ;
		//
		static constexpr RuleIdx NoIdx     = -1 ;
		static constexpr RuleIdx MultiIdx  = -2 ;
		static constexpr RuleIdx UphillIdx = -3 ;
		//
		using NodeBase::NodeBase ;
	} ;

	//
	// Target
	//

	struct Target : Node {
		static_assert(Node::NGuardBits>=1) ;
		static constexpr uint8_t NGuardBits = Node::NGuardBits-1      ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::ostream& operator<<( ::ostream& , Target const ) ;
		// cxtors & casts
		Target(                        ) = default ;
		Target( Node n , bool iu=false ) : Node(n) { is_unexpected( +n && iu        ) ; } // if no node, ensure Target appears as false
		Target( Target const& t        ) : Node(t) { is_unexpected(t.is_unexpected()) ; }
		//
		Target& operator=(Target const& tu) { Node::operator=(tu) ; is_unexpected(tu.is_unexpected()) ; return *this ; }
		// accesses
		Idx operator+() const { return Node::operator+() | is_unexpected()<<(NValBits-1) ; }
		//
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) Idx  side(       ) const = delete ; // { return Node::side<W,LSB+1>(   ) ; }
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) void side(Idx val)       = delete ; // {        Node::side<W,LSB+1>(val) ; }
		//
		bool is_unexpected(        ) const { return Node::side<1>(   ) ; }
		void is_unexpected(bool val)       {        Node::side<1>(val) ; }
		// services
		bool lazy_tflag( Tflag tf , Rule::SimpleMatch const& sm , Rule::FullMatch& fm , ::string& tn ) ; // fm & tn are lazy evaluated
	} ;

	//
	// Dep
	//

	struct Dep : DepDigestBase<Node> {
		friend ::ostream& operator<<( ::ostream& , Dep const& ) ;
		using Base = DepDigestBase<Node> ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		::string accesses_str() const ;
		::string dflags_str  () const ;
		// services
		bool up_to_date () const ;
		void acquire_crc() ;
	} ;
	static_assert(sizeof(Dep)==16) ;

	//
	// Deps
	//

	struct Deps : DepsBase {
		friend ::ostream& operator<<( ::ostream& , Deps const& ) ;
		// cxtors & casts
		using DepsBase::DepsBase ;
		Deps( ::vmap  <Node,AccDflags> const& ,                                  bool parallel=true ) ;
		Deps( ::vmap  <Node,Dflags   > const& , Accesses ,                       bool parallel=true ) ;
		Deps( ::vector<Node          > const& , Accesses , Dflags=StaticDflags , bool parallel=true ) ;
	} ;

}
#endif
#ifdef INFO_DEF
namespace Engine {

	ENUM( NodeLvl
	,	None       // reserve value 0 as they are not counted in n_total
	,	Zombie     // req is zombie but node not marked done yet
	,	Uphill     // first level set at init, uphill directory
	,	NoJob      // job candidates are exhausted
	,	Plain      // >=PlainLvl means plain jobs starting at lvl-Lvl::Plain (all at same priority)
	)

	ENUM( NodeErr
	,	None
	,	Dangling
	,	Overwritten
	)

	struct NodeReqInfo : ReqInfo {                                              // watchers of Node's are Job's
		friend ::ostream& operator<<( ::ostream& os , NodeReqInfo const& ri ) ;
		//
		using Lvl        = NodeLvl        ;
		using MakeAction = NodeMakeAction ;
		//
		static constexpr RuleIdx NoIdx = Node::NoIdx ;
		static const     ReqInfo Src   ;
		// cxtors & casts
		using ReqInfo::ReqInfo ;
		// services
		void update( RunAction , MakeAction , NodeData const& ) ;
		// data
	public :
		RuleIdx prio_idx = NoIdx         ;                 //    16 bits
		bool    done     = false         ;                 // 1<= 8 bit , if true => analysis is over (non-buildable Node's are automatically done)
		NodeErr err      = NodeErr::None ;                 // 2<= 8 bit , if Yes  => node is in error (typically dangling), if Maybe => node is inadequate (typically overwritten)
	} ;
	static_assert(sizeof(NodeReqInfo)==24) ;                                   // check expected size

}
#endif
#ifdef DATA_DEF
namespace Engine {

	struct NodeData : DataBase {
		using Idx        = NodeIdx        ;
		using ReqInfo    = NodeReqInfo    ;
		using MakeAction = NodeMakeAction ;
		using LvlIdx     = RuleIdx        ;                                    // lvl may indicate the number of rules tried
		//
		static constexpr RuleIdx NoIdx     = Node::NoIdx     ;
		static constexpr RuleIdx MultiIdx  = Node::MultiIdx  ;
		static constexpr RuleIdx UphillIdx = Node::UphillIdx ;
		// cxtors & casts
		NodeData() = default ;
		NodeData( Name n , size_t sz , bool external ) : DataBase{n} , long_name{sz>g_config.path_max} {
			if ( long_name || external ) return ;                                                        // no need for a dir as we will not try to build node
			dir = dir_name() ;                                                                           // if !long_name, dir is safe as it is shorter
		}
		~NodeData() {
			job_tgts.pop() ;
		}
		// accesses
		Node     idx    () const { return Node::s_idx(*this) ; }
		::string name   () const { return full_name()        ; }
		size_t   name_sz() const { return full_name_sz()     ; }
		//
		bool           has_req   (Req           ) const ;
		ReqInfo const& c_req_info(Req           ) const ;
		ReqInfo      & req_info  (Req           ) const ;
		ReqInfo      & req_info  (ReqInfo const&) const ;                      // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs      (              ) const ;
		bool           waiting   (              ) const ;
		bool           done      (ReqInfo const&) const ;
		bool           done      (Req           ) const ;
		//
		bool     match_ok          (         ) const {                          return match_gen>=Rule::s_match_gen                   ; }
		bool     has_actual_job    (         ) const {                          return +actual_job_tgt && !actual_job_tgt->rule.old() ; }
		bool     has_actual_job    (Job    j ) const { SWEAR(!j ->rule.old()) ; return actual_job_tgt==j                              ; }
		bool     has_actual_job_tgt(JobTgt jt) const { SWEAR(!jt->rule.old()) ; return actual_job_tgt==jt                             ; }
		//
		Bool3 manual        (                  FileInfoDate const& ) const ;
		Bool3 manual_refresh( Req            , FileInfoDate const& ) ;                                                         // refresh date if file was updated but steady
		Bool3 manual_refresh( JobData const& , FileInfoDate const& ) ;                                                         // .
		Bool3 manual        (                                      ) const { return manual        (  FileInfoDate(name())) ; }
		Bool3 manual_refresh( Req            r                     )       { return manual_refresh(r,FileInfoDate(name())) ; }
		Bool3 manual_refresh( JobData const& j                     )       { return manual_refresh(j,FileInfoDate(name())) ; }
		//
		bool multi  (                    ) const { return conform_idx==MultiIdx  ; }
		bool uphill (                    ) const { return conform_idx==UphillIdx ; }
		bool makable(bool uphill_ok=false) const {
			switch (conform_idx) {
				case NoIdx     : return false     ;
				case MultiIdx  : return true      ;                            // multi is an error case, but is makable
				case UphillIdx : return uphill_ok ;
				default        : return true      ;
			}
		}
		bool is_src() const {
			return !job_tgts.empty() && job_tgts[0]->is_src() ;
		}
		JobTgt conform_job_tgt() const {
			switch (conform_idx) {
				case NoIdx     :
				case MultiIdx  : return {}                    ;
				case UphillIdx : return {}                    ;
				default        : return job_tgts[conform_idx] ;
			}
		}
		bool conform() const {
			JobTgt cjt = conform_job_tgt() ;
			return +cjt && ( cjt->is_special() || has_actual_job_tgt(cjt) ) ;
		}
		bool err(bool uphill_ok=false) const {
			if (multi()            ) return true                     ;
			if (!makable(uphill_ok)) return false                    ;
			if (!conform()         ) return true                     ;
			else                     return conform_job_tgt()->err() ;
		}
		bool err       ( ReqInfo const& cri , bool uphill_ok=false ) const { return cri.err>NodeErr::None || err(uphill_ok)                       ; }
		bool is_special(                                           ) const { return makable(true/*uphill_ok*/) && conform_job_tgt()->is_special() ; }
		// services
		ReqChrono db_chrono() const { return has_actual_job() ? actual_job_tgt->db_chrono() : 0 ; }
		void new_path_max(bool new_is_longer) {
			/**/                                         if (long_name!=new_is_longer) return ;
			bool is_long = name_sz()>g_config.path_max ; if (long_name==is_long      ) return ;
			//
			long_name = is_long ;
			if      (is_long             ) dir.clear() ;
			else if (Disk::is_lcl(name())) dir = dir_name() ;
		}
		//
		bool read(Accesses a) const {                                          // return true <= file was perceived different from non-existent, assuming access provided in a
			if (crc==Crc::None ) return false          ;                       // file does not exist, cannot perceive difference
			if (a[Access::Stat]) return true           ;                       // if file exists, stat is different
			if (crc.is_lnk()   ) return a[Access::Lnk] ;
			if (+crc           ) return a[Access::Reg] ;
			else                 return +a             ;                       // dont know if file is a link, any access may have perceived a difference
		}
		bool up_to_date(DepDigest const& dd) const { return crc.match(dd.crc(),dd.accesses) ; } // only manage crc, not dates
		//
		::vector<RuleTgt> raw_rule_tgts() const ;
		void              mk_old       () ;
		void              mk_anti_src  () ;
		void              mk_src       () ;
		void              mk_no_src    () ;
		//
		::vector_view_c<JobTgt> prio_job_tgts   (RuleIdx prio_idx) const ;
		::vector_view_c<JobTgt> conform_job_tgts(ReqInfo const&  ) const ;
		::vector_view_c<JobTgt> conform_job_tgts(                ) const ;
		//
		void set_buildable( Req , DepDepth lvl=0               ) ;             // data independent, may be pessimistic (Maybe instead of Yes), req is for error reporing only
		void set_pressure ( ReqInfo& ri , CoarseDelay pressure ) const ;
		//
		void set_special( Special , ::vector<Node> const& deps={} , Accesses={} , Dflags=StaticDflags , bool parallel=true ) ;
		//
		ReqInfo const&      make       ( ReqInfo const&     , RunAction=RunAction::Status , Job asking={} , MakeAction=MakeAction::None ) ;
		RuleIdx/*prod_idx*/ make_uphill( ReqInfo      & ri  ,                               Job asking={}                               ) ;
		//
		ReqInfo const& make( ReqInfo const& cri , MakeAction ma ) { return make(cri,RunAction::Status,{}/*asking*/,ma) ; } // for wakeup
		//
		void audit_multi( Req , ::vector<JobTgt> const& ) ;
		//
		bool/*ok*/ forget( bool targets , bool deps ) ;
		//
		template<class RI> void add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) ;
		//
		bool/*modified*/ refresh( Crc , Ddate ) ;
		void             refresh(             ) ;
	private :
		void           _set_buildable_raw( Req            , DepDepth                                                ) ; // req is for error reporting only
		ReqInfo const& _make_raw         ( ReqInfo const& , RunAction , Job asking={} , MakeAction=MakeAction::None ) ;
		void           _set_pressure_raw ( ReqInfo      &                                                           ) const ;
		//
		::pair<Bool3/*buildable*/,RuleIdx/*shorten_by*/> _gather_prio_job_tgts( ::vector<RuleTgt> const& rule_tgts , Req , DepDepth lvl=0 ) ;
		//
		void _set_match_gen(bool ok) ;
		void _set_buildable(Bool3  ) ;
		// data
	public :
		Node     dir                     ;                  //      31 bits, shared
		Ddate    date                    ;                  // ~40<=64 bits,         deemed mtime (in ns) or when it was known non-existent. 40 bits : lifetime=30 years @ 1ms resolution
		Crc      crc                     = Crc::None      ; // ~45<=64 bits,         disk file CRC when file's mtime was date. 45 bits : MTBF=1000 years @ 1000 files generated per second.
		RuleTgts rule_tgts               ;                  // ~20<=32 bits, shared, matching rule_tgts issued from suffix on top of job_tgts, valid if match_ok
		JobTgts  job_tgts                ;                  //      32 bits, owned , ordered by prio, valid if match_ok
		JobTgt   actual_job_tgt          ;                  //  31<=32 bits, shared, job that generated node
		RuleIdx  conform_idx             = Node::NoIdx    ; //      16 bits,         index to job_tgts to first job with execut.ing.ed prio level, if NoIdx <=> uphill or no job found
		MatchGen match_gen:NMatchGenBits = 0              ; //       8 bits,         if <Rule::s_match_gen => deem !job_tgts.size() && !rule_tgts && !sure
		Bool3    buildable:2             = Bool3::Unknown ; //       2 bits,         data independent, if Maybe => buildability is data dependent, if Unknown => not yet computed
		bool     unlinked :1             = false          ; //       1 bit ,         if true <=> node as been unlinked by another rule
		bool     long_name:1             = false          ; //       1 bit ,         name length is larger than allowed by config
	} ;
	static_assert(sizeof(NodeData)==40) ;                                      // check expected size

}
#endif
#ifdef IMPL
namespace Engine {

	//
	// NodeReqInfo
	//

	inline void NodeReqInfo::update( RunAction run_action , MakeAction make_action , NodeData const& node ) {
		if (make_action>=MakeAction::Dec) {
			SWEAR(n_wait) ;
			n_wait-- ;
		}
		if (run_action>action) {                                               // normally, increasing action requires to reset checks
			action = run_action ;
			if (action!=RunAction::Dsk) prio_idx = NoIdx ;                     // except transition Dsk->Run which is no-op for Node
		}
		if (n_wait) return ;
		if      ( req->zombie                                       ) done = true ;
		else if ( node.buildable==Yes && action==RunAction::Makable ) done = true ;
	}

	//
	// NodeData
	//

	inline bool NodeData::has_req(Req r) const {
		return Req::s_store[+r].nodes.contains(idx()) ;
	}
	inline Node::ReqInfo const& NodeData::c_req_info(Req r) const {
		::umap<Node,ReqInfo> const& req_infos = Req::s_store[+r].nodes ;
		auto                        it        = req_infos.find(idx())  ;       // avoid double look up
		if (it==req_infos.end()) return Req::s_store[+r].nodes.dflt ;
		else                     return it->second                  ;
	}
	inline Node::ReqInfo& NodeData::req_info(Req r) const {
		return Req::s_store[+r].nodes.try_emplace(idx(),NodeReqInfo(r)).first->second ;
	}
	inline Node::ReqInfo& NodeData::req_info(ReqInfo const& cri) const {
		if (&cri==&Req::s_store[+cri.req].nodes.dflt) return req_info(cri.req)         ; // allocate
		else                                          return const_cast<ReqInfo&>(cri) ; // already allocated, no look up
	}
	inline ::vector<Req> NodeData::reqs() const { return Req::reqs(*this) ; }

	inline bool NodeData::waiting() const {
		for( Req r : reqs() ) if (c_req_info(r).waiting()) return true ;
		return false ;
	}

	inline bool NodeData::done( ReqInfo const& cri ) const { return cri.done || buildable==No ; }
	inline bool NodeData::done( Req            r   ) const { return done(c_req_info(r))       ; }

	inline Bool3 NodeData::manual(FileInfoDate const& fid) const {
		const char* res_str ;
		Bool3       res     ;
		if      (crc==Crc::None) { res_str = +fid?"created":"not_exist" ; res = No | +fid ; }
		else if (!fid          ) { res_str = "disappeared"              ; res = Maybe     ; }
		else if (fid.date==date) { res_str = "steady"                   ; res = No        ; }
		else                     { res_str = "newer"                    ; res = Yes       ; }
		//
		if (res!=No) Trace("manual",idx(),fid.tag,fid.date,crc,date,res_str) ;
		return res ;
	}

	inline ::vector_view_c<JobTgt> NodeData::conform_job_tgts(ReqInfo const& cri) const { return prio_job_tgts(cri.prio_idx) ; }
	inline ::vector_view_c<JobTgt> NodeData::conform_job_tgts(                  ) const {
		// conform_idx is (one of) the producing job, not necessarily the first of the job_tgt's at same prio level
		RuleIdx prio_idx = conform_idx ;
		switch (prio_idx) {
			case NoIdx     :
			case MultiIdx  :
			case UphillIdx : break ;
			default :
				Prio prio = job_tgts[prio_idx]->rule->prio ;
				while ( prio_idx && job_tgts[prio_idx-1]->rule->prio==prio ) prio_idx-- ; // rewind to first job within prio level
		}
		return prio_job_tgts(prio_idx) ;
	}

	inline ::vector<RuleTgt> NodeData::raw_rule_tgts() const {
		::vector<RuleTgt> rts = Node::s_rule_tgts(name()).view() ;
		::vector<RuleTgt> res ; res.reserve(rts.size())    ;                   // pessimistic reserve ensures no realloc
		Py::Gil           gil ;
		for( RuleTgt const& rt : rts )
			if (+rt.pattern().match(name())) res.push_back(rt) ;
		return res ;
	}

	template<class RI> inline void NodeData::add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void NodeData::_set_match_gen(bool ok) {
		if      (!ok                        ) { match_gen = 0                 ; buildable = Bool3::Unknown ; }
		else if (match_gen<Rule::s_match_gen)   match_gen = Rule::s_match_gen ;
	}

	inline void NodeData::_set_buildable(Bool3 b) {
		SWEAR(match_gen>=Rule::s_match_gen,match_gen,Rule::s_match_gen) ;
		SWEAR(b!=Bool3::Unknown                                       ) ;
		buildable = b ;
	}

	inline void NodeData::set_buildable( Req req , DepDepth lvl ) {            // req is for error reporting only
		if (match_ok()) return ;                                               // already set
		_set_buildable_raw(req,lvl) ;
	}

	inline void NodeData::set_pressure( ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                     // pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_set_pressure_raw(ri) ;
	}

	inline NodeData::ReqInfo const& NodeData::make( ReqInfo const& cri , RunAction run_action , Job asking , MakeAction make_action ) {
		// /!\ do not recognize buildable==No : we must execute set_buildable before in case a non-buildable becomes buildable
		if ( cri.done && ( run_action<=RunAction::Status || !unlinked ) ) return cri ;
		return _make_raw(cri,run_action,asking,make_action) ;
	}

	inline void NodeData::refresh() {
		FileInfoDate fid{name()} ;
		switch (manual(fid)) {
			case Yes   : refresh( {}        , fid.date       ) ; break ;
			case Maybe : refresh( Crc::None , Ddate::s_now() ) ; break ;
			case No    :                                         break ;
			default : FAIL(fid) ;
		}
	}

	//
	// Target
	//

	inline bool Target::lazy_tflag( Tflag tf , Rule::SimpleMatch const& sm , Rule::FullMatch& fm , ::string& tn ) { // fm & tn are lazy evaluated
		Bool3 res = sm.rule->common_tflags(tf,is_unexpected()) ;
		if (res!=Maybe) return res==Yes ;                                      // fast path : flag is common, no need to solve lazy evaluation
		if (!fm       ) fm = sm              ;                                 // solve lazy evaluation
		if (tn.empty()) tn = (*this)->name() ;                                 // .
		/**/            return sm.rule->tflags(fm.idx(tn))[tf] ;
	}

	//
	// Deps
	//

	inline Deps::Deps( ::vmap<Node,AccDflags> const& static_deps , bool p ) {
		::vector<Dep> ds ; ds.reserve(static_deps.size()) ;
		for( auto const& [d,af] : static_deps ) ds.emplace_back( d , af.accesses , af.dflags , p ) ;
		*this = Deps(ds) ;
	}

	inline Deps::Deps( ::vmap<Node,Dflags> const& static_deps , Accesses a , bool p ) {
		::vector<Dep> ds ; ds.reserve(static_deps.size()) ;
		for( auto const& [d,df] : static_deps ) { ds.emplace_back( d , a , df , p ) ; }
		*this = Deps(ds) ;
	}

	inline Deps::Deps( ::vector<Node> const& deps , Accesses a , Dflags df , bool p ) {
		::vector<Dep> ds ; ds.reserve(deps.size()) ;
		for( auto const& d : deps ) ds.emplace_back( d , a , df , p ) ;
		*this = Deps(ds) ;
	}

	//
	// Dep
	//

	inline bool Dep::up_to_date() const {
		return !is_date && crc().match((*this)->crc,accesses) ;
	}

	inline void Dep::acquire_crc() {
		if (!is_date             ) {                  return ; }               // no need
		if (!date()              ) { crc(Crc::None) ; return ; }               // no date means access did not find a file, crc is None, easy
		if (date()> (*this)->date) {                  return ; }               // file is manual, maybe too early and crc is not updated yet (also works if !(*this)->date)
		if (date()!=(*this)->date) { crc({}       ) ; return ; }               // too late, file has changed
		if (!(*this)->crc        ) {                  return ; }               // too early, no crc available yet
		crc((*this)->crc) ;                                                    // got it !
	}

}
#endif
