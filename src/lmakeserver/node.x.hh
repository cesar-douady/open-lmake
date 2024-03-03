// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "codec.hh"

#ifdef STRUCT_DECL

ENUM( Buildable
,	LongName    //                                   name is longer than allowed in config
,	DynAnti     //                                   match dependent
,	Anti        //                                   match independent
,	SrcDir      //                                   match independent (much like star targets, i.e. only existing files are deemed buildable)
,	No          // <=No means node is not buildable
,	Maybe       //                                   buildability is data dependent (maybe converted to Yes by further analysis)
,	SubSrcDir   //                                   sub-file of a SrcDir
,	Unknown
,	Yes         // >=Yes means node is buildable
,	DynSrc      //                                   match dependent
,	Src         //                                   match independent
,	Decode      //                                   file name representing a code->val association
,	Encode      //                                   file name representing a val->code association
,	SubSrc      //                                   sub-file of a src listed in manifest
,	Loop        //                                   node is being analyzed, deemed buildable so as to block further analysis
)

ENUM_1( Manual
,	Changed = Empty // >=Changed means that job is sensitive to new content
,	Ok              // file is as recorded
,	Unlnked         // file has been unlinked
,	Empty           // file is modified but is empty
,	Modif           // file is modified and may contain user sensitive info
,	Unknown
)

ENUM_1( NodeMakeAction
,	None
,	Wakeup // a job has completed
)

ENUM_1( NodeStatus
,	Makable = Src  // <=Makable means node can be used as dep
,	Plain          // must be first (as 0 is deemed to be a job_tgt index), node is generated by a job
,	Multi          // several jobs
,	Src            // node is a src     or a file within a src dir
,	SrcDir         // node is a src dir or a dir  within a src dir
,	None           // no job
,	Uphill         // >=Uphill means node has a buildable uphill dir, node has a regular file as uphill dir
,	Transcient     //                                                 node has a link         as uphill dir (and such a dep will certainly disappear when job is remade unless it is a static dep)
,	Unknown
)

namespace Engine {

	struct Node        ;
	struct NodeData    ;
	struct NodeReqInfo ;

	struct Target  ;
	using Targets = TargetsBase ;


	struct Dep  ;
	struct Deps ;

	static constexpr uint8_t NodeNGuardBits = 1 ; // to be able to make Target

}

#endif
#ifdef STRUCT_DEF

namespace Engine {

	//
	// Node
	//

	struct Node : NodeBase {
		friend ::ostream& operator<<( ::ostream& , Node const ) ;
		using MakeAction = NodeMakeAction ;
		using ReqInfo    = NodeReqInfo    ;
		//
		static constexpr RuleIdx NoIdx      = -1                 ;
		static constexpr RuleIdx MaxRuleIdx = -(N<NodeStatus>+1) ;
		// cxtors & casts
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
		// statics
		static bool s_is_sure(Tflags tflags) {
			return tflags[Tflag::Target] && (tflags[Tflag::Static]||tflags[Tflag::Phony]) ;
		}
		// cxtors & casts
		Target(                       ) = default ;
		Target( Node n , Tflags tf={} ) : Node(n) , tflags{tf} {}
		// accesses
		bool is_sure() const { return s_is_sure(tflags) ; }
		// services
		constexpr ::strong_ordering operator<=>(Node const& other) const { return Node::operator<=>(other) ; }
		// data
		Tflags tflags ;
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
		Deps( ::vmap  <Node,Dflags> const& ,                     bool parallel=true ) ;
		Deps( ::vmap  <Node,Dflags> const& , Accesses ,          bool parallel=true ) ;
		Deps( ::vector<Node       > const& , Accesses , Dflags , bool parallel=true ) ;
	} ;

}

#endif
#ifdef INFO_DEF

namespace Engine {

	struct NodeReqInfo : ReqInfo {                                              // watchers of Node's are Job's
		friend ::ostream& operator<<( ::ostream& os , NodeReqInfo const& ri ) ;
		//
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
		RuleIdx  prio_idx    = NoIdx           ;                                //    16 bits
		bool     single      = false           ;                                // 1<= 8 bits, if true <=> consider only job indexed by prio_idx, not all jobs at this priority
		Accesses overwritten = Accesses::None  ;                                // 3<= 8 bits, accesses for which overwritten file can be perceived (None if file has not been overwritten)
		Manual   manual      = Manual::Unknown ;                                // 3<= 8 bits
		Bool3    speculate   = Yes             ;                                // 2<= 8 bits, Yes : prev dep not ready, Maybe : prev dep in error
	} ;
	static_assert(sizeof(NodeReqInfo)==24) ;                                    // check expected size

}

#endif
#ifdef DATA_DEF

namespace Engine {

	struct NodeData : DataBase {
		using Idx        = NodeIdx        ;
		using ReqInfo    = NodeReqInfo    ;
		using MakeAction = NodeMakeAction ;
		using LvlIdx     = RuleIdx        ;                                                                                                      // lvl may indicate the number of rules tried
		//
		static constexpr RuleIdx MaxRuleIdx = Node::MaxRuleIdx ;
		static constexpr RuleIdx NoIdx      = Node::NoIdx      ;
		// cxtors & casts
		NodeData(                                          ) = default ;
		NodeData( Name n , bool no_dir , bool locked=false ) : DataBase{n} {
			if (!no_dir) dir() = Node(_dir_name(),false/*no_dir*/,locked) ;
		}
		~NodeData() {
			job_tgts().pop() ;
		}
		// accesses
		Node         idx    () const {                         return Node::s_idx(*this) ; }
		::string     name   () const {                         return full_name()        ; }
		size_t       name_sz() const {                         return full_name_sz()     ; }
		Ddate const& date   () const { SWEAR(crc!=Crc::None) ; return _date              ; }
		Ddate      & date   ()       { SWEAR(crc!=Crc::None) ; return _date              ; }
		//
		bool is_decode() const { return buildable==Buildable::Decode ; }
		bool is_encode() const { return buildable==Buildable::Encode ; }
		bool is_plain () const { return !is_decode() && !is_encode() ; }
		//
		Node             & dir           ()       { SWEAR(is_plain (),buildable) ; return _if_plain .dir            ; }
		Node        const& dir           () const { SWEAR(is_plain (),buildable) ; return _if_plain .dir            ; }
		JobTgts          & job_tgts      ()       { SWEAR(is_plain (),buildable) ; return _if_plain .job_tgts       ; }
		JobTgts     const& job_tgts      () const { SWEAR(is_plain (),buildable) ; return _if_plain .job_tgts       ; }
		RuleTgts         & rule_tgts     ()       { SWEAR(is_plain (),buildable) ; return _if_plain .rule_tgts      ; }
		RuleTgts    const& rule_tgts     () const { SWEAR(is_plain (),buildable) ; return _if_plain .rule_tgts      ; }
		JobTgt           & actual_job_tgt()       { SWEAR(is_plain (),buildable) ; return _if_plain .actual_job_tgt ; }
		JobTgt      const& actual_job_tgt() const { SWEAR(is_plain (),buildable) ; return _if_plain .actual_job_tgt ; }
		Codec::Val       & codec_val     ()       { SWEAR(is_decode(),buildable) ; return _if_decode.val            ; }
		Codec::Val  const& codec_val     () const { SWEAR(is_decode(),buildable) ; return _if_decode.val            ; }
		Codec::Code      & codec_code    ()       { SWEAR(is_encode(),buildable) ; return _if_encode.code           ; }
		Codec::Code const& codec_code    () const { SWEAR(is_encode(),buildable) ; return _if_encode.code           ; }
		//
		bool           has_req   ( Req                        ) const ;
		ReqInfo const& c_req_info( Req                        ) const ;
		ReqInfo      & req_info  ( Req                        ) const ;
		ReqInfo      & req_info  ( ReqInfo const&             ) const ;                                                                          // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs      (                            ) const ;
		bool           waiting   (                            ) const ;
		bool           done      ( ReqInfo const& , RunAction ) const ;
		bool           done      ( ReqInfo const&             ) const ;
		bool           done      ( Req            , RunAction ) const ;
		bool           done      ( Req                        ) const ;
		//
		bool match_ok          (         ) const {                          return match_gen>=Rule::s_match_gen                                     ; }
		bool has_actual_job    (         ) const {                          return is_plain() && +actual_job_tgt() && !actual_job_tgt()->rule.old() ; }
		bool has_actual_job    (Job    j ) const { SWEAR(!j ->rule.old()) ; return is_plain() && actual_job_tgt()==j                                ; }
		bool has_actual_job_tgt(JobTgt jt) const { SWEAR(!jt->rule.old()) ; return is_plain() && actual_job_tgt()==jt                               ; }
		//
		Manual manual        ( Ddate , bool empty                        ) const ;
		Manual manual        (                  Disk::FileInfo const& fi ) const {                             return manual(fi.date,!fi.sz) ; }
		Manual manual        (                                           ) const { Disk::FileInfo fi{name()} ; return manual(fi            ) ; }
		Manual manual_refresh( Req            , Disk::FileInfo const& fi ) ;                                                                     // refresh date if file was updated but steady
		Manual manual_refresh( JobData const& , Disk::FileInfo const& fi ) ;                                                                     // .
		Manual manual_refresh( Req            r                          )       { Disk::FileInfo fi{name()} ; return manual_refresh(r,fi)   ; }
		Manual manual_refresh( JobData const& j                          )       { Disk::FileInfo fi{name()} ; return manual_refresh(j,fi)   ; }
		//
		bool/*modified*/ refresh_src_anti( bool report_no_file , ::vector<Req> const& , ::string const& name ) ;                                 // Req's are for reporting only
		//
		RuleIdx    conform_idx(              ) const { if   (_conform_idx<=MaxRuleIdx)   return _conform_idx              ; else return NoIdx             ; }
		void       conform_idx(RuleIdx    idx)       { SWEAR(idx         <=MaxRuleIdx) ; _conform_idx = idx               ;                                 }
		NodeStatus status     (              ) const { if   (_conform_idx> MaxRuleIdx)   return NodeStatus(-_conform_idx) ; else return NodeStatus::Plain ; }
		void       status     (NodeStatus s  )       { SWEAR(+s                      ) ; _conform_idx = -+s               ;                                 }
		//
		JobTgt conform_job_tgt() const {
			if (status()==NodeStatus::Plain) return job_tgts()[conform_idx()] ;
			else                             return {}                        ;
		}
		bool conform() const {
			JobTgt cjt = conform_job_tgt() ;
			return +cjt && ( cjt->is_special() || has_actual_job_tgt(cjt) ) ;
		}
		Bool3 ok() const {                                                                      // if Maybe <=> not built
			switch (status()) {
				case NodeStatus::Plain : return No    | !conform_job_tgt()->err() ;
				case NodeStatus::Multi : return No                                ;
				case NodeStatus::Src   : return Yes   & (crc!=Crc::None)          ;
				default                : return Maybe                             ;
			}
		}
		Bool3 ok     ( ReqInfo const& cri , Accesses a=Accesses::All ) const { SWEAR(cri.done()) ; return +(cri.overwritten&a) ? No : ok() ; }
		bool  running( ReqInfo const& cri ) const {
			for( Job j : conform_job_tgts(cri) )
				for( Req r : j->running_reqs() )
					if (j->c_req_info(r).step==JobStep::Exec) return true ;
			return false ;
		}
		//
		bool is_src_anti() const {
			SWEAR(match_ok()) ;
			switch (buildable) {
				case Buildable::LongName  :
				case Buildable::DynAnti   :
				case Buildable::Anti      :
				case Buildable::SrcDir    :
				case Buildable::SubSrcDir :
				case Buildable::DynSrc    :
				case Buildable::Src       :
				case Buildable::Decode    :
				case Buildable::Encode    :
				case Buildable::SubSrc    : return true  ;
				default                   : return false ;
			}
		}
		//
		// services
		bool read(Accesses a) const {                                                           // return true <= file was perceived different from non-existent, assuming access provided in a
			if (crc==Crc::None ) return false          ;                                        // file does not exist, cannot perceive difference
			if (unlnked        ) return false          ;                                        // file did not exist despite a non-None crc
			if (a[Access::Stat]) return true           ;                                        // if file exists, stat is different
			if (crc.is_lnk()   ) return a[Access::Lnk] ;
			if (crc.is_reg()   ) return a[Access::Reg] ;
			else                 return +a             ;                                        // dont know if file is a link, any access may have perceived a difference
		}
		bool up_to_date(DepDigest const& dd) const { return crc.match(dd.crc(),dd.accesses) ; } // only manage crc, not dates
		//
		Manual manual_wash( ReqInfo& ri , bool lazy=false ) ;
		//
		void mk_old   (                    ) ;
		void mk_src   (FileTag=FileTag::Err) ;                                                  // Err means no crc update
		void mk_no_src(                    ) ;
		//
		::c_vector_view<JobTgt> prio_job_tgts   (RuleIdx prio_idx) const ;
		::c_vector_view<JobTgt> conform_job_tgts(ReqInfo const&  ) const ;
		::c_vector_view<JobTgt> conform_job_tgts(                ) const ;
		//
		void set_buildable( Req={}   , DepDepth lvl=0       ) ;                                 // data independent, may be pessimistic (Maybe instead of Yes), req is for error reporing only
		void set_pressure ( ReqInfo& , CoarseDelay pressure ) const ;
		//
		void propag_speculate( Req req , Bool3 speculate ) const {
			/**/                          if (speculate==Yes         ) return ;                 // fast path : nothing to propagate
			ReqInfo& ri = req_info(req) ; if (speculate>=ri.speculate) return ;
			ri.speculate = speculate ;
			_propag_speculate(ri) ;
		}
		//
		void set_infinite(::vector<Node> const& deps) ;
		//
		void make( ReqInfo& , RunAction=RunAction::Status , Watcher asking={} , Bool3 speculate=Yes , MakeAction=MakeAction::None ) ;
		//
		void make( ReqInfo& ri , MakeAction ma ) { return make(ri,RunAction::Status,{}/*asking*/,Yes/*speculate*/,ma) ; }                  // for wakeup
		//
		bool/*ok*/ forget( bool targets , bool deps ) ;
		//
		template<class RI> void add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) ;
		//
		bool/*modified*/ refresh( Crc , Ddate ) ;
		void             refresh(             ) ;
	private :
		void         _set_buildable_raw( Req      , DepDepth                                                                           ) ; // req is for error reporting only
		bool/*done*/ _make_pre         ( ReqInfo&                                                                                      ) ;
		void         _make_raw         ( ReqInfo& , RunAction , Watcher asking_={} , Bool3 speculate=Yes , MakeAction=MakeAction::None ) ;
		void         _set_pressure_raw ( ReqInfo&                                                                                      ) const ;
		void         _propag_speculate ( ReqInfo const&                                                                                ) const ;
		//
		Buildable _gather_special_rule_tgts( ::string const& name                          ) ;
		Buildable _gather_prio_job_tgts    ( ::string const& name , Req   , DepDepth lvl=0 ) ;
		Buildable _gather_prio_job_tgts    (                        Req r , DepDepth lvl=0 ) {
			if (!rule_tgts()) return Buildable::No                             ;                                                           // fast path : avoid computing name()
			else              return _gather_prio_job_tgts( name() , r , lvl ) ;
		}
		//
		void _set_match_gen(bool ok) ;
		// data
	public :
		struct IfPlain {
			Node     dir            ;                            //  31<=32 bits, shared
			JobTgts  job_tgts       ;                            //      32 bits, owned , ordered by prio, valid if match_ok
			RuleTgts rule_tgts      ;                            // ~20<=32 bits, shared, matching rule_tgts issued from suffix on top of job_tgts, valid if match_ok
			JobTgt   actual_job_tgt ;                            //  31<=32 bits, shared, job that generated node
		} ;
		struct IfDecode {
			Codec::Val val ;                                     //      32 bits,         offset in association file where the association line can be found
		} ;
		struct IfEncode {
			Codec::Code code ;                                   //      32 bits,         offset in association file where the association line can be found
		} ;
		//Name  name   ;                                         //      32 bits, inherited
		Watcher asking ;                                         //      32 bits,         last watcher needing this node
		Crc     crc    = Crc::None ;                             // ~45<=64 bits,         disk file CRC when file's mtime was date. 45 bits : MTBF=1000 years @ 1000 files generated per second.
	private :
		Ddate _date ;                                            // ~40<=64 bits,         deemed mtime (in ns) or when it was known non-existent. 40 bits : lifetime=30 years @ 1ms resolution
		union {
			IfPlain  _if_plain  ;                                //     128 bits
			IfDecode _if_decode ;                                //      32 bits
			IfEncode _if_encode ;                                //      32 bits
		} ;
	public :
		MatchGen  match_gen:NMatchGenBits = 0                  ; //       8 bits,         if <Rule::s_match_gen => deem !job_tgts.size() && !rule_tgts && !sure
		Buildable buildable:4             = Buildable::Unknown ; //       4 bits,         data independent, if Maybe => buildability is data dependent, if Plain => not yet computed
		bool      unlnked  :1             = false              ; //       1 bit ,         if true <=> node as been unlinked by another rule
	private :
		RuleIdx _conform_idx = -+NodeStatus::Unknown ;           //      16 bits,         index to job_tgts to first job with execut.ing.ed prio level, if NoIdx <=> uphill or no job found
	} ;
	static_assert(sizeof(NodeData)==48) ;                        // check expected size

}

#endif
#ifdef IMPL

namespace Engine {

	//
	// NodeReqInfo
	//

	inline void NodeReqInfo::update( RunAction run_action , MakeAction make_action , NodeData const& node ) {
		SWEAR(run_action!=RunAction::Run) ;                                                                   // Run is only for Job's
		if (make_action==MakeAction::Wakeup) {
			SWEAR(n_wait) ;
			n_wait-- ;
		}
		if (run_action>action) action = run_action ;                                                          // increasing action requires to reset checks
		if (n_wait) return ;
		if      ( req->zombie                                                  ) done_ = RunAction::Dsk     ;
		else if ( node.buildable>=Buildable::Yes && action==RunAction::Makable ) done_ = RunAction::Makable ;
	}

	//
	// NodeData
	//

	inline bool NodeData::has_req(Req r) const {
		return Req::s_store[+r].nodes.contains(idx()) ;
	}
	inline Node::ReqInfo const& NodeData::c_req_info(Req r) const {
		::umap<Node,ReqInfo> const& req_infos = Req::s_store[+r].nodes ;
		auto                        it        = req_infos.find(idx())  ;                 // avoid double look up
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

	inline bool NodeData::done( ReqInfo const& cri , RunAction ra ) const {
		if (cri.done(ra)) return true ;
		switch (ra) {                                                   // if not actually done, report obvious cases
			case RunAction::None    : return true                     ;
			case RunAction::Makable : return is_src_anti()            ;
			case RunAction::Status  : return buildable<=Buildable::No ;
			case RunAction::Dsk     : return false                    ;
		DF}
	}
	inline bool NodeData::done( ReqInfo const& cri                ) const { return done(cri          ,cri.action)           ; }
	inline bool NodeData::done( Req            r   , RunAction ra ) const { return done(c_req_info(r),ra        )           ; }
	inline bool NodeData::done( Req            r                  ) const { return done(c_req_info(r)           )           ; }

	inline Manual NodeData::manual(Ddate d,bool empty) const {
		Manual res = {}/*garbage*/ ;
		if (crc==Crc::None) {
			if      (!d       ) return Manual::Ok      ;
			else if (empty    ) res =  Manual::Empty   ;
			else                res =  Manual::Modif   ;
		} else {
			if      (!d       ) res =  Manual::Unlnked ;
			else if (d==date()) return Manual::Ok      ;
			else if (empty    ) res =  Manual::Empty   ;
			else                res =  Manual::Modif   ;
		}
		//
		Trace("manual",idx(),d,crc,crc==Crc::None?Ddate():date(),res,STR(empty)) ;
		return res ;
	}

	inline ::c_vector_view<JobTgt> NodeData::conform_job_tgts(ReqInfo const& cri) const { return prio_job_tgts(cri.prio_idx) ; }
	inline ::c_vector_view<JobTgt> NodeData::conform_job_tgts(                  ) const {
		// conform_idx is (one of) the producing job, not necessarily the first of the job_tgt's at same prio level
		if (status()!=NodeStatus::Plain) return {} ;
		RuleIdx prio_idx = conform_idx() ;
		Prio prio = job_tgts()[prio_idx]->rule->prio ;
		while ( prio_idx && job_tgts()[prio_idx-1]->rule->prio==prio ) prio_idx-- ; // rewind to first job within prio level
		return prio_job_tgts(prio_idx) ;
	}

	template<class RI> inline void NodeData::add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void NodeData::_set_match_gen(bool ok) {
		if      (!ok                        ) { SWEAR(is_plain()                   ) ; match_gen = 0                 ; buildable = Buildable::Unknown ; }
		else if (match_gen<Rule::s_match_gen) { SWEAR(buildable!=Buildable::Unknown) ; match_gen = Rule::s_match_gen ;                                  }
	}

	inline void NodeData::set_buildable( Req req , DepDepth lvl ) { // req is for error reporting only
		if (!match_ok()) _set_buildable_raw(req,lvl) ;              // if not already set
		SWEAR(buildable!=Buildable::Unknown) ;
	}

	inline void NodeData::set_pressure( ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                     // pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_set_pressure_raw(ri) ;
	}

	inline void NodeData::make( ReqInfo& ri , RunAction run_action , Watcher asking , Bool3 speculate , MakeAction make_action ) {
		// /!\ do not recognize buildable==No : we must execute set_buildable before in case a non-buildable becomes buildable
		if ( !(run_action>=RunAction::Dsk&&unlnked) && make_action!=MakeAction::Wakeup && speculate>=ri.speculate && ri.done(run_action) ) return ; // fast path
		_make_raw(ri,run_action,asking,speculate,make_action) ;
	}

	inline void NodeData::refresh() {
		FileInfo fi = Disk::FileInfo{name()} ;
		switch (manual(fi)) {
			case Manual::Ok      :                                  break ;
			case Manual::Unlnked : refresh( Crc::None , Ddate() ) ; break ;
			case Manual::Empty   :
			case Manual::Modif   : refresh( {}        , fi.date ) ; break ;
		DF}
	}

	//
	// Deps
	//

	inline Deps::Deps( ::vmap<Node,Dflags> const& static_deps , bool p ) {
		::vector<Dep> ds ; ds.reserve(static_deps.size()) ;
		for( auto const& [d,f] : static_deps ) ds.emplace_back( d , Accesses() , f , p ) ;
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
		if ( is_date && (*this)->crc.valid() && (*this)->crc.exists() && date()==(*this)->date() ) crc((*this)->crc) ;
	}

}

#endif
