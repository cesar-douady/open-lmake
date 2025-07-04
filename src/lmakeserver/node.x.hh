// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "codec.hh"

#ifdef STRUCT_DECL

// START_OF_VERSIONING
enum class Buildable : uint8_t {
	Anti                         //                                   match independent, include uphill dirs of Src/SrcDir listed in manifest
,	SrcDir                       //                                   match independent SrcDir listed in manifest (much like star targets, i.e. only existing files are deemed buildable)
,	SubSrc                       //                                   match independent sub-file of a Src listed in manifest
,	PathTooLong                  //                                   match dependent (as limit may change with config)
,	DynAnti                      //                                   match dependent
,	No                           // <=No means node is not buildable
,	Maybe                        //                                   buildability is data dependent (maybe converted to Yes by further analysis)
,	SubSrcDir                    //                                   sub-file of a SrcDir
,	Unknown
,	Yes                          // >=Yes means node is buildable
,	DynSrc                       //                                   match dependent
,	Src                          //                                   file listed in manifest, match independent
,	Decode                       //                                   file name representing a code->val association
,	Encode                       //                                   file name representing a val->code association
,	Loop                         //                                   node is being analyzed, deemed buildable so as to block further analysis
} ;
// END_OF_VERSIONING
static constexpr ::amap<Buildable,::pair<Bool3,bool>,N<Buildable>> BuildableAttrs {{
	//                         has_file src_anti
	{ Buildable::Anti        , { No    , true  } }
,	{ Buildable::SrcDir      , { No    , true  } }
,	{ Buildable::SubSrc      , { No    , true  } }
,	{ Buildable::PathTooLong , { No    , true  } }
,	{ Buildable::DynAnti     , { No    , true  } }
,	{ Buildable::No          , { No    , false } }
,	{ Buildable::Maybe       , { Maybe , false } }
,	{ Buildable::SubSrcDir   , { Maybe , true  } }
,	{ Buildable::Unknown     , { Maybe , false } }
,	{ Buildable::Yes         , { Maybe , false } }
,	{ Buildable::DynSrc      , { Maybe , true  } }
,	{ Buildable::Src         , { Yes   , true  } }
,	{ Buildable::Decode      , { No    , false } }
,	{ Buildable::Encode      , { No    , false } }
,	{ Buildable::Loop        , { No    , false } }
}} ;
static_assert(chk_enum_tab(BuildableAttrs)) ;


enum class Manual : uint8_t {
	Ok                        // file is as recorded
,	Unlnked                   // file has been unlinked
,	Empty                     // file is modified but is empty
,	Modif                     // file is modified and may contain user sensitive info
,	Unknown
//
// aliases
,	Changed = Empty           // >=Changed means that job is sensitive to new content
} ;

enum class NodeGoal : uint8_t { // each action is included in the following one
	None
,	Status                      // check book-keeping, no disk access
,	Dsk                         // ensure up-to-date on disk
} ;

enum class NodeMakeAction : uint8_t {
	Wakeup                            // a job has completed
,	Status
,	Dsk
,	Query                             // query only, no job is executed, besides that, looks like Dsk
} ;

enum class NodeStatus : uint8_t {
	Plain                         // must be first (as 0 is deemed to be a job_tgt index), node is generated by a job
,	Multi         // several jobs
,	Src           // node is a src     or a file within a src dir
,	SrcDir        // node is a src dir or a dir  within a src dir
,	None          // no job
,	Uphill        // >=Uphill means node has a buildable uphill dir, node has a regular file as uphill dir
,	Transient     //                                                 node has a link         as uphill dir (and such a dep will certainly disappear when job is remade unless it is a static dep)
,	Unknown
//
// aliases
,	Makable = Src // <=Makable means node can be used as dep
} ;

// START_OF_VERSIONING
enum class Polluted : uint8_t {
	Clean                       // must be first
,	Old
,	PreExist
,	Job
} ;
// END_OF_VERSIONING

namespace Engine {

	struct Node        ;
	struct NodeData    ;
	struct NodeReqInfo ;

	struct Target  ;
	using Targets = TargetsBase ;

	struct Dep  ;
	struct Deps ;

}

#endif
#ifdef STRUCT_DEF

inline NodeGoal mk_goal(NodeMakeAction ma) {
	static constexpr ::amap<NodeMakeAction,NodeGoal,N<NodeMakeAction>> Goals = {{
		{ NodeMakeAction::Wakeup  , NodeGoal::None   }
	,	{ NodeMakeAction::Status  , NodeGoal::Status }
	,	{ NodeMakeAction::Dsk     , NodeGoal::Dsk    }
	,	{ NodeMakeAction::Query   , NodeGoal::Dsk    }
	}} ;
	static_assert(chk_enum_tab(Goals)) ;
	return Goals[+ma].second ;
}

inline NodeMakeAction mk_action( NodeGoal g , bool query ) {
	static constexpr ::amap<NodeGoal,NodeMakeAction,N<NodeGoal>> Actions = {{
		{ NodeGoal::None    , {}/*garbage*/          }
	,	{ NodeGoal::Status  , NodeMakeAction::Status }
	,	{ NodeGoal::Dsk     , NodeMakeAction::Dsk    }
	}} ;
	static_assert(chk_enum_tab(Actions)) ;
	SWEAR(g!=NodeGoal::None) ;
	return query ? NodeMakeAction::Query : Actions[+g].second ;
}

namespace Engine {

	//
	// Node
	//

	struct Node : NodeBase {
		friend ::string& operator+=( ::string& , Node const ) ;
		using MakeAction = NodeMakeAction ;
		using ReqInfo    = NodeReqInfo    ;
		//
		static constexpr RuleIdx NoIdx      = -1                        ;
		static constexpr RuleIdx MaxRuleIdx = RuleIdx(-N<NodeStatus>-1) ;
		// statics
		static Hash::Crc s_src_dirs_crc() ;
		// static data
	private :
		static Hash::Crc _s_src_dirs_crc ;
		// cxtors & casts
		using NodeBase::NodeBase ;
		explicit operator ::string() const ;
	} ;

	//
	// Target
	//

	struct Target : Node {
		static_assert(Node::NGuardBits>=1) ;                            // need 1 bit to store static_phony state
		static constexpr uint8_t NGuardBits = Node::NGuardBits-1      ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		friend ::string& operator+=( ::string& , Target const ) ;
		// cxtors & casts
		Target(                       ) = default ;
		Target( Node n , Tflags tf={} ) : Node(n) , tflags{tf} { SWEAR(+self) ; }
		// accesses
		bool static_phony() const { return ::static_phony(tflags) ; }
		// services
		constexpr ::strong_ordering operator<=>(Node const& other) const { return Node::operator<=>(other) ; }
		// data
		Tflags tflags ;
	} ;
	static_assert(sizeof(Target)==8) ;

	//
	// Dep
	//

	struct Dep : DepDigestBase<Node> {
		friend ::string& operator+=( ::string& , Dep const& ) ;
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

	union GenericDep {
		static constexpr uint8_t NodesPerDep = sizeof(Dep)/sizeof(Node) ;
		friend ::string& operator+=( ::string& , GenericDep const& ) ;
		// cxtors & casts
		GenericDep(Dep const& d={}) : hdr{d} {}
		// services
		GenericDep const* next() const { return this+1+div_up<GenericDep::NodesPerDep>(hdr.sz) ; }
		GenericDep      * next()       { return this+1+div_up<GenericDep::NodesPerDep>(hdr.sz) ; }
		// data
		Dep hdr                 = {} ;
		Node chunk[1/*hdr.sz*/] ;
	} ;

	//
	// Deps
	//

	struct DepsIter {
		struct Digest {
			friend ::string& operator+=( ::string& , Digest const& ) ;
			bool operator==(Digest const&) const = default ;
			DepsIdx hdr     = 0 ;
			uint8_t i_chunk = 0 ;
		} ;
		// cxtors & casts
		DepsIter(                     ) = default ;
		DepsIter( DepsIter const& dit ) : hdr{dit.hdr} , i_chunk{dit.i_chunk} {}
		DepsIter( GenericDep const* d ) : hdr{d      }                        {}
		DepsIter( Deps , Digest       ) ;
		//
		DepsIter& operator=(DepsIter const& dit) {
			hdr     = dit.hdr     ;
			i_chunk = dit.i_chunk ;
			return self ;
		}
		// accesses
		bool operator==(DepsIter const& dit) const { return hdr==dit.hdr && i_chunk==dit.i_chunk ; }
		Digest digest  (Deps               ) const ;
		// services
		Dep const* operator->() const { return &*self ; }
		Dep const& operator* () const {
			// Node's in chunk are semantically located before header so :
			// - if i_chunk< hdr->sz : refer to dep with no crc, flags nor parallel
			// - if i_chunk==hdr->sz : refer to header
			SWEAR(hdr) ;
			if (i_chunk==hdr->hdr.sz) return hdr->hdr ;
			static_cast<Node&>(tmpl) = hdr[1].chunk[i_chunk]   ;
			tmpl.accesses            = hdr->hdr.chunk_accesses ;
			return tmpl ;
		}
		DepsIter& operator++(int) { return ++self ; }
		DepsIter& operator++(   ) {
			if (i_chunk<hdr->hdr.sz)   i_chunk++ ;                         // go to next item in chunk
			else                     { i_chunk = 0 ; hdr = hdr->next() ; } // go to next chunk
			return self ;
		}
		DepsIter& next_existing(DepsIter const& end) {
			SWEAR(end.i_chunk==0) ;                                        // at end, an iterator always have a null i_chunk
			if (hdr==end.hdr) return self ;
			i_chunk = hdr->hdr.sz ;                                        // go to last item in chunk, i.e. skip over non-existing deps in the chunk
			while ( hdr->hdr.is_crc && hdr->hdr.crc()==Crc::None ) {
				hdr = hdr->next() ;                                        // go to next chunk if already at end of chunk
				if (hdr==end.hdr) { i_chunk = 0           ; break ; }
				else                i_chunk = hdr->hdr.sz ;                // go to last item in chunk, the only one that may be existing
			}
			return self ;
		}
		// data
		GenericDep const* hdr     = nullptr                    ;           // pointer to current chunk header
		mutable Dep       tmpl    = {{}/*accesses*/,Crc::None} ;           // template to store uncompressed Dep's
		uint8_t           i_chunk = 0                          ;           // current index in chunk
	} ;

	struct Deps : DepsBase {
		// cxtors & casts
		using DepsBase::DepsBase ;
		Deps( ::vector<Node> const& , Accesses , Dflags , bool parallel ) ;
		// accesses
		NodeIdx size() const = delete ; // deps are compressed
		// services
		DepsIter begin() const {
			GenericDep const* first = items() ;
			return {first} ;
		}
		DepsIter end() const {
			GenericDep const* last1 = items()+DepsBase::size() ;
			return {last1} ;
		}
		void assign      (            ::vector<Dep> const& ) ;
		void replace_tail( DepsIter , ::vector<Dep> const& ) ;
	private :
		void _chk( ::vector<Node> const& deps , size_t is_tail=false ) ;
	} ;

}

#endif
#ifdef INFO_DEF

namespace Engine {

	struct NodeReqInfo : ReqInfo {                                            // watchers of Node's are Job's
		friend ::string& operator+=( ::string& os , NodeReqInfo const& ri ) ;
		//
		using MakeAction = NodeMakeAction ;
		//
		static constexpr RuleIdx NoIdx = Node::NoIdx ;
		static const     ReqInfo Src   ;
		// cxtors & casts
		using ReqInfo::ReqInfo ;
		// accesses
		bool done(NodeGoal ng) const { return done_>=ng   ; }
		bool done(           ) const { return done_>=goal ; }
		// data
	public :
//		ReqInfo                                                               //    128 bits, inherits
		RuleIdx  prio_idx    = NoIdx           ;                              //     16 bits, index to the first job of the current prio being or having been analyzed
		bool     single      = false           ;                              // 1<=  8 bits, if true <=> consider only job indexed by prio_idx, not all jobs at this priority
		Accesses overwritten ;                                                // 3<=  8 bits, accesses for which overwritten file can be perceived (None if file has not been overwritten)
		Manual   manual      = Manual::Unknown ;                              // 3<=  8 bits, info is available as soon as done_=Dsk
		Bool3    speculate   = Yes             ;                              // 2<=  8 bits, Yes : prev dep not ready, Maybe : prev dep in error
		NodeGoal goal        = NodeGoal::None  ;                              // 2<=  8 bits, asked level
		NodeGoal done_       = NodeGoal::None  ;                              // 2<=  8 bits, done level
	} ;

}

#endif
#ifdef DATA_DEF

namespace Engine {

	struct RejectSet {
		// cxtors & casts
		RejectSet (NodeData& nd) : _node_data{nd} {}
		~RejectSet(            )                  { _save() ; }
		//
		RejectSet& operator=(RejectSet const&) = delete ;
		// services
		bool contains(RuleTgt rt) {
			_load() ;
			return _rule_tgts.contains(rt) ;
		}
		void insert(RuleTgt rt) {
			_load() ;
			bool inserted = _rule_tgts.push(rt) ;
			SWEAR(inserted) ;
		}
	private :
		void _save() ;
		void _load() ;
		// data
		NodeData&                   _node_data ;
		OrderedSet<RuleTgt>/*lazy*/ _rule_tgts ;         // keep insertion order to generate stable vectors
		Bool3                       _dirty     = Maybe ; // if Maybe <=> lazy must be solved, if Yes <=> must be written back to rejected_rule_tgts
	} ;

	struct NodeData : NodeDataBase {
		using Idx        = NodeIdx        ;
		using ReqInfo    = NodeReqInfo    ;
		using MakeAction = NodeMakeAction ;
		using LvlIdx     = RuleIdx        ;                                                                           // lvl may indicate the number of rules tried
		//
		static constexpr RuleIdx MaxRuleIdx = Node::MaxRuleIdx ;
		static constexpr RuleIdx NoIdx      = Node::NoIdx      ;
		// static data
		static Mutex<MutexLvl::NodeCrcDate> s_crc_date_mutex ;
		// cxtors & casts
		NodeData() = delete ;                                                                                         // if necessary, we must take care of the union
		NodeData( NodeName n             ) : NodeDataBase{n} {                }
		NodeData( NodeName n , Node dir_ ) : NodeDataBase{n} { dir() = dir_ ; }
		~NodeData() {
			job_tgts().pop() ;
		}
		// accesses
		Node idx () const { return Node::s_idx(self) ; }
		//
		bool is_decode() const { return buildable==Buildable::Decode ; }
		bool is_encode() const { return buildable==Buildable::Encode ; }
		bool is_plain () const { return !is_decode() && !is_encode() ; }
		//
		Node             & dir               ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .dir                                      ; }
		Node        const& dir               () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .dir                                      ; }
		JobTgts          & job_tgts          ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .job_tgts                                 ; }
		JobTgts     const& job_tgts          () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .job_tgts                                 ; }
		RuleTgts         & rule_tgts         ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .rule_tgts                                ; }
		RuleTgts    const& rejected_rule_tgts() const { SWEAR(  is_plain () , buildable ) ; return _if_plain .rejected_rule_tgts                       ; }
		RuleTgts         & rejected_rule_tgts()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .rejected_rule_tgts                       ; }
		RuleTgts    const& rule_tgts         () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .rule_tgts                                ; }
		Job              & actual_job        ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .actual_job                               ; }
		Job         const& actual_job        () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .actual_job                               ; }
		Job              & polluting_job     ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .polluting_job                            ; }
		Job         const& polluting_job     () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .polluting_job                            ; }
		Tflags           & actual_tflags     ()       { SWEAR(  is_plain () , buildable ) ; return _actual_tflags                                      ; }
		Tflags      const& actual_tflags     () const { SWEAR(  is_plain () , buildable ) ; return _actual_tflags                                      ; }
		SigDate          & date              ()       { SWEAR(  is_plain () , buildable ) ; return _if_plain .date                                     ; }
		SigDate     const& date              () const { SWEAR(  is_plain () , buildable ) ; return _if_plain .date                                     ; }
		Codec::Val       & codec_val         ()       { SWEAR(  is_decode() , buildable ) ; return _if_decode.val                                      ; }
		Codec::Val  const& codec_val         () const { SWEAR(  is_decode() , buildable ) ; return _if_decode.val                                      ; }
		Codec::Code      & codec_code        ()       { SWEAR(  is_encode() , buildable ) ; return _if_encode.code                                     ; }
		Codec::Code const& codec_code        () const { SWEAR(  is_encode() , buildable ) ; return _if_encode.code                                     ; }
		Ddate            & log_date          ()       { SWEAR( !is_plain () , buildable ) ; return is_decode()?_if_decode.log_date:_if_encode.log_date ; }
		Ddate       const& log_date          () const { SWEAR( !is_plain () , buildable ) ; return is_decode()?_if_decode.log_date:_if_encode.log_date ; }
		//
		void crc_date( Crc crc_ , SigDate const& sd ) {
			date() = sd   ;
			crc    = crc_ ;
		}
		//
		bool           has_req   ( Req                       ) const ;
		ReqInfo const& c_req_info( Req                       ) const ;
		ReqInfo      & req_info  ( Req                       ) const ;
		ReqInfo      & req_info  ( ReqInfo const&            ) const ;                                                // make R/W while avoiding look up (unless allocation)
		::vector<Req>  reqs      (                           ) const ;
		bool           waiting   (                           ) const ;
		bool           done      ( ReqInfo const& , NodeGoal ) const ;
		bool           done      ( ReqInfo const&            ) const ;
		bool           done      ( Req            , NodeGoal ) const ;
		bool           done      ( Req                       ) const ;
		//
		bool match_ok      (     ) const {                     return match_gen>=Rule::s_match_gen                         ; }
		bool has_actual_job(     ) const {                     return is_plain() && +actual_job() && +actual_job()->rule() ; }
		bool has_actual_job(Job j) const { SWEAR(+j->rule()) ; return is_plain() && actual_job()==j                        ; }
		//
		Manual manual        (         FileSig const& ) const ;
		Manual manual        (                        ) const { return manual(FileSig(name())) ; }
		Manual manual_refresh( Req   , FileSig const& ) ;                                                             // refresh date if file was updated but steady
		Manual manual_refresh( Req r                  )       { return manual_refresh(r,FileSig(name())) ; }
		//
		bool/*modified*/ refresh_src_anti( bool report_no_file , ::vector<Req> const&      , ::string const& name ) ; // Req's are for reporting only
		bool/*modified*/ refresh_src_anti( bool report_no_file , ::vector<Req> const& reqs                        ) { return refresh_src_anti(report_no_file,reqs,name()) ; }
		//
		void full_refresh( bool report_no_file , ::vector<Req> const& reqs ) {
			if (+reqs) set_buildable(reqs[0]) ;
			else       set_buildable(       ) ;
			if (is_src_anti()) refresh_src_anti(report_no_file,reqs) ;
			else               manual_refresh  (Req()              ) ;                                  // no manual_steady diagnostic as this may be because of another job
		}
		//
		RuleIdx    conform_idx(              ) const { if   (_conform_idx<=MaxRuleIdx)   return _conform_idx              ; else return NoIdx             ; }
		void       conform_idx(RuleIdx    idx)       { SWEAR(idx         <=MaxRuleIdx) ; _conform_idx = idx               ;                                 }
		NodeStatus status     (              ) const { if   (_conform_idx> MaxRuleIdx)   return NodeStatus(-_conform_idx) ; else return NodeStatus::Plain ; }
		void       status     (NodeStatus s  )       { SWEAR(+s                      ) ; _conform_idx = -+s               ;                                 }
		//
		JobTgt conform_job_tgt() const {
			if (status()==NodeStatus::Plain) { SWEAR( conform_idx()<n_job_tgts , status(),conform_idx(),n_job_tgts ) ; return job_tgts()[conform_idx()] ; }
			else                                                                                                       return {}                        ;
		}
		bool conform() const {
			Job cj = conform_job_tgt() ;
			return +cj && ( cj->is_special() || has_actual_job(cj) ) ;
		}
		Bool3 ok(bool force_err=false) const {                                                          // if Maybe <=> not built
			switch (status()) {
				case NodeStatus::Plain : return No | !( force_err || conform_job_tgt()->err() ) ;
				case NodeStatus::Multi : return No                                              ;
				case NodeStatus::Src   : return No | !( force_err || crc==Crc::None           ) ;
				default                : return Maybe                                           ;
			}
		}
		Bool3 ok( ReqInfo const& cri , Accesses a=~Accesses() ) const {
			SWEAR(cri.done(NodeGoal::Status)) ;
			return ok(+(cri.overwritten&a)) ;
		}
		bool running(ReqInfo const& cri) const {
			for( Job j : conform_job_tgts(cri) )
				for( Req r : j->running_reqs() )
					if (j->c_req_info(r).step()==JobStep::Exec) return true ;
			return false ;
		}
		// for is_src_anti, if !match_ok(), recorded buildable is best effort to distinguish src/anti
		Bool3 has_file   (bool permissive=false) const { { if (!permissive) SWEAR(match_ok()) ; } return BuildableAttrs[+(match_ok()?buildable:Buildable::Unknown)].second.first  ; }
		bool  is_src_anti(bool permissive=false) const { { if (!permissive) SWEAR(match_ok()) ; } return BuildableAttrs[+            buildable                    ].second.second ; }
		//
		// services
		bool read(Accesses a) const {                                                                   // return true <= file was perceived different from non-existent, assuming access provided in a
			if (crc==Crc::None ) return false          ;                                                // file does not exist, cannot perceive difference
			if (a[Access::Stat]) return true           ;                                                // if file exists, stat is different
			if (crc.is_lnk()   ) return a[Access::Lnk] ;
			if (crc.is_reg()   ) return a[Access::Reg] ;
			else                 return +a             ;                                                // dont know if file is a link, any access may have perceived a difference
		}
		bool up_to_date(DepDigest const& dd) const {                                                    // only manage crc, not dates
			return crc.match( dd.crc() , dd.accesses ) ;
		}
		//
		Manual manual_wash( ReqInfo& ri , bool query , bool dangling ) ;
		//
		void mk_old   (                        ) ;
		void mk_src   (Buildable=Buildable::Src) ;
		void mk_src   (FileTag                 ) ;
		void mk_no_src(                        ) ;
		//
		::span<JobTgt const> prio_job_tgts     (RuleIdx prio_idx) const ;
		::span<JobTgt const> conform_job_tgts  (ReqInfo const&  ) const ;
		::span<JobTgt const> conform_job_tgts  (                ) const ;
		::span<JobTgt const> candidate_job_tgts(                ) const ;                               // all jobs above prio provided in conform_idx
		//
		// data independent, may be pessimistic (Maybe versus Yes), req is for error reporing only, set infinite if !throw_if_infinite && looping
		void set_buildable( Req   , RejectSet* /*lazy*/ known_rejected , DepDepth lvl=0 , bool throw_if_infinite=false ) ;
		void set_buildable( Req r , RejectSet& /*lazy*/ kr             , DepDepth lvl=0 , bool tii              =false ) { set_buildable(r ,&kr    ,lvl,tii) ; }
		void set_buildable( Req r ,                                      DepDepth lvl=0 , bool tii              =false ) { set_buildable(r ,nullptr,lvl,tii) ; }
		void set_buildable(         RejectSet& /*lazy*/ kr             , DepDepth lvl=0 , bool tii              =false ) { set_buildable({},&kr    ,lvl,tii) ; }
		void set_buildable(                                              DepDepth lvl=0 , bool tii              =false ) { set_buildable({},nullptr,lvl,tii) ; }
		//
		void set_pressure( ReqInfo& , CoarseDelay pressure ) const ;
		//
		void propag_speculate( Req req , Bool3 speculate ) const {
			/**/                          if (speculate==Yes         ) return ;                         // fast path : nothing to propagate
			ReqInfo& ri = req_info(req) ; if (speculate>=ri.speculate) return ;
			ri.speculate = speculate ;
			_propag_speculate(ri) ;
		}
		//
		void set_infinite( Special , ::vector<Node> const& deps ) ;
		//
		void make  ( ReqInfo& , MakeAction , Bool3 speculate=Yes ) ;
		void wakeup( ReqInfo& ri                                 ) { return make(ri,MakeAction::Wakeup) ; }
		//
		bool/*ok*/ forget( bool targets , bool deps ) ;
		//
		template<class RI> void add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) ;
		//
		bool/*modified*/ refresh( Crc , SigDate const& ={} ) ;
		void             refresh(                          ) ;
	private :
		void           _do_set_buildable( Req      , RejectSet&/*lazy*/ known_rejected , DepDepth=0 ) ; // req is for error reporting only
		bool/*solved*/ _make_pre        ( ReqInfo& , bool query                                     ) ;
		void           _do_make         ( ReqInfo& , MakeAction , Bool3 speculate=Yes               ) ;
		void           _do_set_pressure ( ReqInfo&                                                  ) const ;
		void           _propag_speculate( ReqInfo const&                                            ) const ;
		//
		Buildable _gather_special_rule_tgts( ::string const&   name ,       RejectSet&/*lazy*/ known_rejected                  ) ;
		Buildable _gather_prio_job_tgts    ( ::string&/*lazy*/ name , Req , RejectSet&/*lazy*/ known_rejected , DepDepth lvl=0 ) ;
		//
		void _set_match_ok() ;
		// data
		// START_OF_VERSIONING
	public :
		struct IfPlain {
			SigDate  date               ;                // ~40+40<128 bits,           date : production date, sig : if file sig is sig, crc is valid, 40 bits : 30 years @ms resolution
			Node     dir                ;                //  31   < 32 bits, shared
			JobTgts  job_tgts           ;                //         32 bits, owned ,   ordered by prio, valid if match_ok, may contain extra JobTgt's (used as a reservoir to avoid matching)
			RuleTgts rule_tgts          ;                // ~20   < 32 bits, shared,   matching rule_tgts issued from suffix on top of job_tgts, valid if match_ok
			RuleTgts rejected_rule_tgts ;                // ~20   < 32 bits, shared,   rule_tgts known not to match, independent of match_ok
			Job      actual_job         ;                //  31   < 32 bits, shared,   job that generated node
			Job      polluting_job      ;                //         32 bits,           polluting job when polluted was last set to Polluted::Job
		} ;
		struct IfDecode {
			Ddate      log_date ;                        // ~40   < 64 bits,           logical date to detect overwritten
			Codec::Val val      ;                        //         32 bits,           offset in association file where the association line can be found
		} ;
		struct IfEncode {
			Ddate       log_date ;                       // ~40   < 64 bits,           logical date to detect overwritten
			Codec::Code code     ;                       //         32 bits,           offset in association file where the association line can be found
		} ;
		//NodeName name   ;                              //         32 bits, inherited
		Watcher    asking ;                              //         32 bits,           last watcher needing this node
		Crc        crc    = Crc::None ;                  // ~45   < 64 bits,           disk file CRC when file's mtime was date.p. 45 bits : MTBF=1000 years @ 1000 files generated per second.
	private :
		union {
			IfPlain  _if_plain  = {} ;                   //        320 bits
			IfDecode _if_decode ;                        //         96 bits
			IfEncode _if_encode ;                        //         96 bits
		} ;
	public :
		RuleIdx   n_job_tgts  = 0                  ;     //         16 bits,           number of actual meaningful JobTgt's in job_tgts
		MatchGen  match_gen   = 0                  ;     //          8 bits,           if <Rule::s_match_gen => deem n_job_tgts==0 && !rule_tgts && !sure
		Buildable buildable:4 = Buildable::Unknown ;     //          4 bits,           data independent, if Maybe => buildability is data dependent, if Plain => not yet computed
		Polluted  polluted :2 = Polluted::Clean    ;     //          2 bits,           reason for pollution
		bool      busy     :1 = false              ;     //          1 bit ,           a job is running with this node as target
	private :
		Tflags  _actual_tflags ;                         //   6   <  8 bits,           tflags associated with actual_job
		RuleIdx _conform_idx   = -+NodeStatus::Unknown ; //         16 bits,           index to job_tgts to first job with execut.ing.ed prio level, if NoIdx <=> uphill or no job found
		// END_OF_VERSIONING
	} ;

}

#endif
#ifdef IMPL

namespace Engine {

	//
	// Node
	//

	inline Node::operator ::string() const { return self->name() ; }

	//
	// NodeData
	//

	inline bool               NodeData::has_req   (Req r             ) const { return Req::s_store[+r      ].nodes.contains  (    idx()) ; }
	inline NodeReqInfo const& NodeData::c_req_info(Req r             ) const { return Req::s_store[+r      ].nodes.c_req_info(    idx()) ; }
	inline NodeReqInfo      & NodeData::req_info  (Req r             ) const { return Req::s_store[+r      ].nodes.req_info  (r  ,idx()) ; }
	inline NodeReqInfo      & NodeData::req_info  (ReqInfo const& cri) const { return Req::s_store[+cri.req].nodes.req_info  (cri,idx()) ; }
	//
	inline ::vector<Req> NodeData::reqs() const { return Req::s_reqs(self) ; }

	inline bool NodeData::waiting() const {
		for( Req r : reqs() ) if (c_req_info(r).waiting()) return true ;
		return false ;
	}

	inline bool NodeData::done( ReqInfo const& cri , NodeGoal na ) const {
		if (cri.done(na)) return true ;
		switch (na) {                                                               // if not actually done, report obvious cases
			case NodeGoal::None   : return true                                   ;
			case NodeGoal::Status : return match_ok() && buildable<=Buildable::No ;
			case NodeGoal::Dsk    : return false                                  ;
		DF}                                                                         // NO_COV
	}
	inline bool NodeData::done( ReqInfo const& cri               ) const { return done(cri          ,cri.goal) ; }
	inline bool NodeData::done( Req            r   , NodeGoal ng ) const { return done(c_req_info(r),ng      ) ; }
	inline bool NodeData::done( Req            r                 ) const { return done(c_req_info(r)         ) ; }

	inline Manual NodeData::manual(FileSig const& sig) const {
		if (sig==date().sig) return Manual::Ok ;               // None and Dir are deemed identical
		Manual res = Manual::Modif ;
		if      (!sig                     ) res = Manual::Unlnked ;
		else if (sig.tag()==FileTag::Empty) res = Manual::Empty   ;
		Trace("manual",res,idx(),sig,crc,date()) ;
		return res ;
	}

	inline ::span<JobTgt const> NodeData::conform_job_tgts(ReqInfo const& cri) const { return prio_job_tgts(cri.prio_idx) ; }
	inline ::span<JobTgt const> NodeData::conform_job_tgts(                  ) const {
		// conform_idx is (one of) the producing job, not necessarily the first of the job_tgt's at same prio level
		if (status()!=NodeStatus::Plain) return {} ;
		RuleIdx prio_idx = conform_idx()                      ; SWEAR(prio_idx<n_job_tgts) ;
		RuleIdx prio     = job_tgts()[prio_idx]->rule()->prio ;
		while ( prio_idx && job_tgts()[prio_idx-1]->rule()->prio==prio ) prio_idx-- ; // rewind to first job within prio level
		return prio_job_tgts(prio_idx) ;
	}

	template<class RI> void NodeData::add_watcher( ReqInfo& ri , Watcher watcher , RI& wri , CoarseDelay pressure ) {
		ri.add_watcher(watcher,wri) ;
		set_pressure(ri,pressure) ;
	}

	inline void NodeData::_set_match_ok() {
		if (match_gen>=Rule::s_match_gen) return ; // already ok
		SWEAR(buildable!=Buildable::Unknown) ;
		match_gen = Rule::s_match_gen ;
	}

	inline void NodeData::set_buildable( Req req , RejectSet* /*lazy*/ known_rejected , DepDepth lvl , bool throw_if_infinite ) { // req is for error reporting only
		try {
			if      (match_ok()                       ) {}                                                                        // buildable is already known
			else if (known_rejected                   ) _do_set_buildable( req , *known_rejected        , lvl ) ;
			else                                        _do_set_buildable( req , ::ref(RejectSet(self)) , lvl ) ;
			if      (buildable==Buildable::PathTooLong) throw ::pair( Special::InfinitePath , ::vector<Node>{idx()} ) ;           // mimic _do_set_buildable
		} catch (::pair<Special,::vector<Node>> const& e) {
			if (throw_if_infinite) throw ;
			else                   set_infinite(e.first,e.second) ;
		}
		SWEAR(buildable!=Buildable::Unknown) ;
	}

	inline void NodeData::set_pressure( ReqInfo& ri , CoarseDelay pressure ) const {
		if (!ri.set_pressure(pressure)) return ;                                     // pressure is not significantly higher than already existing, nothing to propagate
		if (!ri.waiting()             ) return ;
		_do_set_pressure(ri) ;
	}

	inline void NodeData::make( ReqInfo& ri , MakeAction ma , Bool3 s ) {
		if ( ma!=MakeAction::Wakeup && s>=ri.speculate && ri.done(mk_goal(ma)) && !polluted && !busy ) return ; // fast path
		_do_make(ri,ma,s) ;
	}

	inline void NodeData::refresh() {
		FileSig sig { name() } ;
		switch (manual(sig)) {
			case Manual::Ok      :                                      break ;
			case Manual::Unlnked : refresh( Crc::None  , Pdate(New) ) ; break ;
			case Manual::Empty   : refresh( Crc::Empty , sig        ) ; break ;
			case Manual::Modif   : refresh( {}         , sig        ) ; break ;
		DF}                                                                     // NO_COV
	}

	//
	// Dep
	//

	inline bool Dep::up_to_date() const {
		return is_crc && crc().match( self->crc , accesses ) ;
	}

	inline void Dep::acquire_crc() {
		if (is_crc              ) return ;                                              // already a crc ==> nothing to do
		if (!self->crc.valid()  ) return ;                                              // nothing to acquire
		if (self->crc==Crc::None) { if (sig().tag()<FileTag::Target) crc(self->crc) ; } // acquire no file (cannot test sig equality as sig contains date at which file was known non-existent)
		else                      { if (sig()==self->date().sig    ) crc(self->crc) ; } // acquire existing file
	}

	//
	// Deps
	//

	inline DepsIter::DepsIter( Deps ds , Digest d ) : hdr{+ds?ds.items()+d.hdr:nullptr} , i_chunk{d.i_chunk} {}

	inline DepsIter::Digest DepsIter::digest(Deps ds) const {
		return { hdr?DepsIdx(hdr-ds.items()):0 , i_chunk } ;
	}

	//
	// RejectSet
	//

	inline void RejectSet::_save() {
		if (_dirty!=Yes) return ;
		_node_data.rejected_rule_tgts() = ::vector<RuleTgt>(_rule_tgts) ; // sorted when converted to ::vector<RuleTgt>
		_dirty                          = No                            ;
	}

	inline void RejectSet::_load() {
		if (_dirty!=Maybe) return ;
		_dirty = No ;
		for( RuleTgt rt : _node_data.rejected_rule_tgts().view() ) {
			Rule r = rt->rule ;
			if      (!r        )   _dirty = Yes ;                                                 // if a rule is obsolete, forget it and remind to save cleaned up vector
			else if (rt!=r->crc) { _dirty = Yes ; _rule_tgts.push(RuleTgt(r->crc,rt.tgt_idx)) ; } // take last version of rule and retain insertion order
			else                                  _rule_tgts.push(rt                        ) ;   // .
		}
	}

}

#endif
