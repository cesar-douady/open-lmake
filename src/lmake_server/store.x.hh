// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 5 times, successively with following macros defined : STRUCT_DECL, STRUCT_DEF, INFO_DEF, DATA_DEF, IMPL

#include "store/store_utils.hh"
#include "store/struct.hh"
#include "store/alloc.hh"
#include "store/vector.hh"
#include "store/prefix.hh"
#include "store/side_car.hh"

#include "store/idxed.hh"

//
// There are 13 files :
// - 2 name files associate a name with a node and a job :
//   - These are prefix-trees to share as much prefixes as possible since names tend to share a lot of prefixes
//   - For jobs, a suffix containing the rule and the positions of the stems is added.
// - 2 files for nodes :
//   - A node data file provides its name (a pointer to the name file) and all pertinent info about a node.
//   - A job-star file containing vectors of job-star, a job-star is a job index and a marker saying if we refer to a static or a star target
// - 3 files for jobs :
//   - A job data file containing its name (a pointer to the name file) and all the pertinent info for a job
//   - A targets file containing vectors of star targets (static targets can be identified from the rule).
//     A target is a node index and a marker saying if target has been updated, i.e. it was not unlinked before job execution.
//     This file is sorted so that searching a node inside a vector can be done efficiently.
//   - A deps file containing vectors of deps, ordered with static deps first, then critical deps then non-critical deps, in order in which they were opened.
// - 6 files for rules :
//   - A rule string file containing strings describing the rule.
//   - A rule index file containing indexes in the rule string file.
//     The reason for this indirection is to have a short (16 bits) index for rules while the index in the rule string file is 32 bits.
//   - A rule crc file containing an history of rule crc's (match, cmd and rsrcs).
//     Jobs store an index in this file rather than directly rule crc's as this index is 32 bits instead of 3x64 bit.
//   - A rule-targets file containing vectors of rule-target's. A rule-target is a rule index and a target index within the rule.
//     This file is for use by nodes to represent candidates to generate them.
//     During the analysis process, rule-targets are transformed into job-target when possible (else they are dropped), so that the yet to analyse part which
//     the node keeps is a suffix of the original list.
//     For this reason, the file is stored as a suffix-tree (like a prefix-tree, but reversed).
//   - A rule suffix file storing rule target candidates indexed by suffix (actually entry to the rule prefix file).
//     This file is used with a longest match to find candidates for a given target by looking at its suffix.
//   - A rule prefix file storing rule target candidates indexd by prefix for eahc possible suffix.
//     This file is used with a longest match to find candidates for a given target by looking at its prefix/suffix.
//

#ifdef STRUCT_DECL

namespace Engine {

	struct StoreMrkr {} ; // just a marker to disambiguate file association

	union  GenericDep ;
	struct Target     ;

	extern SeqId* g_seq_id ; // used to identify launched jobs. Persistent so that we keep as many old traces as possible

}

namespace Engine {

	namespace Persistent { using RuleStr     = Vector::Simple<RuleStrIdx,char      ,StoreMrkr> ; }
	/**/                   using DepsBase    = Vector::Simple<DepsIdx   ,GenericDep,StoreMrkr> ;
	/**/                   using TargetsBase = Vector::Simple<TargetsIdx,Target    ,StoreMrkr> ;

}

#endif

#ifdef STRUCT_DEF

namespace Engine::Persistent {

	struct RuleTgts ;

	struct JobName
	:	             Idxed<JobNameIdx>
	{	using Base = Idxed<JobNameIdx> ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		::string str(size_t sfx_sz=0) const ;
		// services
		void pop() ;
	} ;

	struct NodeName
	:	             Idxed<NodeNameIdx>
	{	using Base = Idxed<NodeNameIdx> ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		::string str() const ;
	} ;

	struct JobDataBase {
		friend struct JobBase ;
		friend struct JobName ;
		// static data
	private :
		static Mutex<MutexLvl::Job,true/*Shared*/> _s_mutex ; // jobs are created in main thread but its name may be accessed in other threads
		// cxtors & casts
	public :
		JobDataBase() = default ;
		JobDataBase(JobName n) : _full_name{n} {}
		// accesses
		::string full_name(size_t sfx_sz=0) const { return _full_name.str(sfx_sz) ; }
		// data
	private :
		JobName _full_name ;
	} ;

	struct NodeDataBase {
		friend struct NodeBase ;
		friend struct NodeName ;
		// static data
	private :
		static Mutex<MutexLvl::Node,true/*Shared*/> _s_mutex ; // nodes can be created from several threads, ensure coherence between names and nodes
		// cxtors & casts
	public :
		NodeDataBase() = default ;
		NodeDataBase(NodeName n) : _name{n} {}
		// accesses
		::string name() const { return _name.str() ; }
		// data
	private :
		NodeName _name ;
	} ;

	struct JobBase
	:	             Idxed<JobIdx,NJobGuardBits>
	{	using Base = Idxed<JobIdx,NJobGuardBits> ;
		// statics
		static Job           s_idx          ( JobData const&                        ) ;
		static bool          s_has_frozens  (                                       ) ;
		static ::vector<Job> s_frozens      (                                       ) ;
		static void          s_frozens      ( bool add , ::vector<Job> const& items ) ;
		static void          s_clear_frozens(                                       ) ;
		// cxtors & casts
		using Base::Base ;
		template<class... A> JobBase(                             NewType  , A&&...      ) ;
		template<class... A> JobBase( ::pair_ss const& name_sfx , NewType  , A&&... args ) : JobBase(name_sfx,true /*new*/,::forward<A>(args)...) {}
		template<class... A> JobBase( ::pair_ss const& name_sfx , DfltType , A&&... args ) : JobBase(name_sfx,false/*new*/,::forward<A>(args)...) {}
		/**/                 JobBase( ::pair_ss const& name_sfx                          ) : JobBase(name_sfx,Dflt                              ) {}
	private :
		template<class... A> JobBase( ::pair_ss const& name_sfx , bool new_ , A&&... ) ;
	public :
		void pop() ;
		// accesses
		JobData const& operator* () const ;
		JobData      & operator* ()       ;
		JobData const* operator->() const { return &*self ; }
		JobData      * operator->()       { return &*self ; }
		//
		RuleIdx rule_idx () const ;
		bool    frozen   () const ;
		// services
		void chk() const ;
	} ;

	struct NodeBase
	:	             Idxed<NodeIdx,NNodeGuardBits>
	{	using Base = Idxed<NodeIdx,NNodeGuardBits> ;
		// statics
		static Node           s_idx              ( NodeData const&                  ) ;
		static bool           s_is_known         ( ::string const&                  ) ;
		static bool           s_has_frozens      (                                  ) ;
		static bool           s_has_no_triggers  (                                  ) ;
		static bool           s_has_srcs         (                                  ) ;
		static ::vector<Node> s_frozens          (                                  ) ;
		static ::vector<Node> s_no_triggers      (                                  ) ;
		static void           s_frozens          ( bool add , ::vector<Node> const& ) ; // erase (!add) or insert (add)
		static void           s_no_triggers      ( bool add , ::vector<Node> const& ) ; // .
		static void           s_clear_frozens    (                                  ) ;
		static void           s_clear_no_triggers(                                  ) ;
		static void           s_clear_srcs       (                                  ) ;
		//
		static Targets const s_srcs( bool dirs                                    ) ;
		static void          s_srcs( bool dirs , bool add , ::vector<Node> const& ) ;   // erase (!add) or insert (add)
		//
		static RuleTgts s_rule_tgts(::string const& target_name) ;
		// cxtors & casts
		using Base::Base ;
		NodeBase(           ::string const& name                     ) ;                // dont create node if does not already exist
		NodeBase( NewType , ::string const& name , bool no_dir=false ) ;
		// accesses
		NodeData const& operator* () const ;
		NodeData      & operator* ()       ;
		NodeData const* operator->() const { return &*self ; }
		NodeData      * operator->()       { return &*self ; }
		bool            frozen    () const ;
		bool            no_trigger() const ;
		// services
		void chk() const ;
	} ;

	struct RuleBase
	:	             Idxed<RuleIdx>
	{	using Base = Idxed<RuleIdx> ;
		friend Iota2<Rule> rule_lst(bool with_shared) ;
		static constexpr char   NoRuleName[] = "no_rule"            ;
		static constexpr size_t NoRuleNameSz = sizeof(NoRuleName)-1 ;     // -1 to account for teminating null, cannot use ::strlen which not constexpr with clang
		// statics
		static void s_from_disk       (       ) ;
		static void s_from_vec_dyn    (Rules&&) ;
		static void s_from_vec_not_dyn(Rules&&) ;
	private :
		static void _s_save       () ;
		static void _s_update_crcs() ;
		static void _s_set_rules  () ;
		// static data
	public :
		static MatchGen                            s_match_gen ;
		static StaticUniqPtr<Rules,MutexLvl::None> s_rules     ;          // almost a ::unique_ptr except we do not want it to be destroyed at the end to avoid problems
		// cxtors & casts
		using Base::Base ;
		constexpr RuleBase(Special s) : Base{RuleIdx(+s)} { SWEAR(+s) ; } // Special::0 is a marker that says not special
		// accesses
		RuleData      & data      ()       ;
		RuleData const& operator* () const ;
		RuleData const* operator->() const { return &*self ; }
	} ;

	struct RuleCrcBase
	:	             Idxed<RuleCrcIdx>
	{	using Base = Idxed<RuleCrcIdx> ;
		// statics
		// static data
		static ::umap<Crc,RuleCrc> s_by_rsrcs ;
		// cxtors & casts
		using Base::Base ;
		RuleCrcBase( Crc match , Crc cmd=Crc::Unknown , Crc rsrcs=Crc::Unknown ) ;
		// accesses
		RuleCrcData      & data      ()       ;
		RuleCrcData const& operator* () const ;
		RuleCrcData const* operator->() const { return &*self ; }
	} ;

	struct RuleTgts
	:	             Idxed<RuleTgtsIdx>
	{	using Base = Idxed<RuleTgtsIdx> ;
		// statics
		// cxtors & casts
		using Base::Base ;
		RuleTgts           (::span<RuleTgt const> const&  ) ;
		RuleTgts& operator=(::span<RuleTgt const> const& v) ;
		void pop() ;
		// accesses
		::vector<RuleTgt> view() const ;
		// services
		void shorten_by(RuleIdx by) ;
	} ;

	struct SfxBase
	:	             Idxed<RuleIdx>
	{	using Base = Idxed<RuleIdx> ;
		// cxtors & casts
		using Base::Base ;
	} ;

}

namespace Engine {
	using JobBase      = Persistent::JobBase                         ;
	using JobDataBase  = Persistent::JobDataBase                     ;
	using JobName      = Persistent::JobName                         ;
	using JobTgtsBase  = Vector::Crunch<JobTgtsIdx,JobTgt,StoreMrkr> ;
	using NodeBase     = Persistent::NodeBase                        ;
	using NodeDataBase = Persistent::NodeDataBase                    ;
	using NodeName     = Persistent::NodeName                        ;
	using RuleBase     = Persistent::RuleBase                        ;
	using RuleCrcBase  = Persistent::RuleCrcBase                     ;
	using RuleTgts     = Persistent::RuleTgts                        ;
}

#endif
#ifdef INFO_DEF

namespace Engine {

	extern StaticUniqPtr<Config    > g_config     ;
	extern StaticUniqPtr<::vector_s> g_src_dirs_s ;

}

#endif
#ifdef IMPL

namespace Engine::Persistent {

	// START_OF_VERSIONING REPO

	struct JobHdr {
		SeqId   seq_id  ;
		JobTgts frozens ; // these jobs are not rebuilt
	} ;

	struct NodeHdr {
		Targets srcs        ;
		Targets src_dirs    ;
		Targets frozens     ; // these nodes are not updated, like sources
		Targets no_triggers ; // these nodes do not trigger rebuild
	} ;

	//                                          ThreadKey header     index             n_index_bits       key       data          misc
	// jobs
	using JobFile      = Store::AllocFile       < 0     , JobHdr   , Job             , NJobIdxBits      ,           JobData                        > ;
	using JobNameFile  = Store::SinglePrefixFile< 0     , void     , JobName         , NJobNameIdxBits  , char    , Job                            > ;
	using DepsFile     = Store::VectorFile      < '='   , void     , Deps            , NDepsIdxBits     ,           GenericDep  , NodeIdx , 4      > ; // Deps are compressed when Crc==None
	using TargetsFile  = Store::VectorFile      < '='   , void     , Targets         , NTargetsIdxBits  ,           Target                         > ;
	// nodes
	using NodeFile     = Store::StructFile      < 0     , NodeHdr  , Node            , NNodeIdxBits     ,           NodeData                       > ;
	using NodeNameFile = Store::SinglePrefixFile< 0     , void     , NodeName        , NNodeNameIdxBits , char    , Node                           > ;
	using JobTgtsFile  = Store::VectorFile      < '='   , void     , JobTgts::Vector , NJobTgtsIdxBits  ,           JobTgt      , RuleIdx          > ;
	// rules
	using RuleCrcFile  = Store::AllocFile       < '='   , MatchGen , RuleCrc         , NRuleCrcIdxBits  ,           RuleCrcData                    > ;
	using RuleTgtsFile = Store::SinglePrefixFile< '='   , void     , RuleTgts        , NRuleTgtsIdxBits , RuleTgt , void        , true /*Reverse*/ > ;
	using SfxFile      = Store::SinglePrefixFile< '='   , void     , PsfxIdx         , NPsfxIdxBits     , char    , PsfxIdx     , true /*Reverse*/ > ; // map sfxes to root of pfxes
	using PfxFile      = Store::MultiPrefixFile < '='   , void     , PsfxIdx         , NPsfxIdxBits     , char    , RuleTgts    , false/*Reverse*/ > ;

	static constexpr char StartMrkr = 0x0 ; // used to indicate a single match suffix (i.e. a suffix which actually is an entire file name)

	// END_OF_VERSIONING

	// on disk
	extern ::string _g_rules_file_name ;
	//
	extern JobFile      _g_job_file       ; // jobs
	extern JobNameFile  _g_job_name_file  ; // .
	extern DepsFile     _g_deps_file      ; // .
	extern TargetsFile  _g_targets_file   ; // .
	extern NodeFile     _g_node_file      ; // nodes
	extern NodeNameFile _g_node_name_file ; // .
	extern JobTgtsFile  _g_job_tgts_file  ; // .
	extern RuleCrcFile  _g_rule_crc_file  ; // rules
	extern RuleTgtsFile _g_rule_tgts_file ; // .
	extern SfxFile      _g_sfxs_file      ; // .
	extern PfxFile      _g_pfxs_file      ; // .
	// in memory
	extern ::uset<Job > _frozen_jobs  ;
	extern ::uset<Node> _frozen_nodes ;
	extern ::uset<Node> _no_triggers  ;

}

namespace Vector {
	template<> struct Descr<Engine::DepsBase   > { static constexpr Engine::Persistent::DepsFile   & file = Engine::Persistent::_g_deps_file     ; } ;
	template<> struct Descr<Engine::TargetsBase> { static constexpr Engine::Persistent::TargetsFile& file = Engine::Persistent::_g_targets_file  ; } ;
	template<> struct Descr<Engine::JobTgtsBase> { static constexpr Engine::Persistent::JobTgtsFile& file = Engine::Persistent::_g_job_tgts_file ; } ;
}

namespace Engine::Persistent {

	void new_config( Config&& , bool rescue=false , ::function<void(Config const& old,Config const& new_)> diff=[](Config const&,Config const&)->void{} ) ;
	//
	bool/*invalidate*/ new_srcs        ( Sources&& , ::string const& manifest ) ;
	bool/*invalidate*/ new_rules       ( Rules  &&                            ) ;
	void               invalidate_match( bool force_physical=false            ) ;
	//
	void chk() ;

	template<class Disk,class Item> void _s_update( Disk& disk , ::uset<Item>& mem , bool add , ::vector<Item> const& items ) {
		bool modified = false ;
		if      (add ) for( Item i : items ) modified |= mem.insert(i).second ;
		else if (+mem) for( Item i : items ) modified |= mem.erase (i)        ; // fast path : no need to update mem if it is already empty
		if (modified) disk.assign(mk_vector<typename Disk::Item>(mem)) ;
	}
	template<class Disk,class Item> void _s_update( Disk& disk , bool add , ::vector<Item> const& items ) {
		::uset<Item> mem = mk_uset<Item>(disk) ;
		_s_update(disk,mem,add,items) ;
	}

	//
	// JobName
	//
	// cxtors & casts
	inline void JobName::pop() {
		Lock lock { JobDataBase::_s_mutex } ;
		Persistent::_g_job_name_file.pop(+self) ;
	}
	// accesses
	inline ::string JobName::str(size_t sfx_sz) const {
		SharedLock lock { JobDataBase::_s_mutex } ;
		return Persistent::_g_job_name_file.str_key(+self,sfx_sz) ;
	}

	//
	// NodeName
	//
	inline ::string NodeName::str() const {
		SharedLock lock { NodeDataBase::_s_mutex } ;
		return Persistent::_g_node_name_file.str_key(+self) ;
	}

	//
	// JobBase
	//
	inline JobFile::Lst job_lst() { SWEAR(t_thread_key=='=') ; return _g_job_file.lst() ; }
	// statics
	inline Job JobBase::s_idx(JobData const& jd) { return _g_job_file.idx(jd) ; }
	//
	inline bool          JobBase::s_has_frozens  (                                       ) { SWEAR(t_thread_key=='=') ; return               +_g_job_file.c_hdr().frozens  ;                 }
	inline ::vector<Job> JobBase::s_frozens      (                                       ) { SWEAR(t_thread_key=='=') ; return mk_vector<Job>(_g_job_file.c_hdr().frozens) ;                 }
	inline void          JobBase::s_frozens      ( bool add , ::vector<Job> const& items ) { SWEAR(t_thread_key=='=') ; _s_update(_g_job_file.hdr().frozens,_frozen_jobs,add,items) ;        }
	inline void          JobBase::s_clear_frozens(                                       ) { SWEAR(t_thread_key=='=') ;           _g_job_file.hdr().frozens.clear() ; _frozen_jobs.clear() ; }
	// cxtors & casts
	template<class... A> JobBase::JobBase( NewType , A&&... args ) {                               // 1st arg is only used to disambiguate
		SWEAR(t_thread_key=='=') ;
		self = _g_job_file.emplace( JobName() , ::forward<A>(args)... ) ;
	}
	template<class... A> JobBase::JobBase( ::pair_ss const& name_sfx , bool new_ , A&&... args ) { // jobs are only created in main thread, so no locking is necessary
		SWEAR(t_thread_key=='=') ;
		Lock    lock  { JobDataBase::_s_mutex }                                 ;
		JobName name_ = _g_job_name_file.insert(name_sfx.first,name_sfx.second) ;
		self = _g_job_name_file.c_at(+name_) ;
		if (+self) {
			SWEAR( name_==self->_full_name , name_,self->_full_name ) ;
			if (new_) *self = JobData( name_ , ::forward<A>(args)...) ;
		} else {
			self             = _g_job_name_file.at(+name_) = _g_job_file.emplace( name_ , ::forward<A>(args)... ) ;
			self->_full_name =                               name_                                                ;
		}
	}
	inline void JobBase::pop() {
		SWEAR(t_thread_key=='=') ;
		if (!self            ) return ;
		if (+self->_full_name) self->_full_name.pop() ;
		_g_job_file.pop(Job(+self)) ;
		clear() ;
	}
	// accesses
	inline bool JobBase::frozen() const { return _frozen_jobs.contains(Job(+self)) ; }
	//
	inline JobData const& JobBase::operator*() const { return _g_job_file.c_at(+self) ; }
	inline JobData      & JobBase::operator*()       { return _g_job_file.at  (+self) ; }
	// services
	inline void JobBase::chk() const {
		JobName fn = self->_full_name          ; if (!fn) return ;
		Job     j  = _g_job_name_file.c_at(fn) ;
		SWEAR( self==j , self , fn , j ) ;
	}

	//
	// NodeBase
	//
	inline NodeFile::Lst node_lst() { SWEAR(t_thread_key=='=') ; return _g_node_file.lst() ; }
	// statics
	inline Node NodeBase::s_idx     (NodeData  const& nd  ) {                                              return  _g_node_file.idx        (nd  ) ; }
	inline bool NodeBase::s_is_known( ::string const& name) { SharedLock lock { NodeDataBase::_s_mutex } ; return +_g_node_name_file.search(name) ; }
	//
	inline bool           NodeBase::s_has_frozens      (                                        ) { SWEAR(t_thread_key=='=') ; return                +_g_node_file.c_hdr().frozens       ;         }
	inline bool           NodeBase::s_has_no_triggers  (                                        ) { SWEAR(t_thread_key=='=') ; return                +_g_node_file.c_hdr().no_triggers   ;         }
	inline bool           NodeBase::s_has_srcs         (                                        ) { SWEAR(t_thread_key=='=') ; return                +_g_node_file.c_hdr().srcs          ;         }
	inline ::vector<Node> NodeBase::s_frozens          (                                        ) { SWEAR(t_thread_key=='=') ; return mk_vector<Node>(_g_node_file.c_hdr().frozens     ) ;         }
	inline ::vector<Node> NodeBase::s_no_triggers      (                                        ) { SWEAR(t_thread_key=='=') ; return mk_vector<Node>(_g_node_file.c_hdr().no_triggers ) ;         }
	inline void           NodeBase::s_frozens          ( bool add , ::vector<Node> const& items ) { SWEAR(t_thread_key=='=') ; _s_update(_g_node_file.hdr().frozens    ,_frozen_nodes,add,items) ; }
	inline void           NodeBase::s_no_triggers      ( bool add , ::vector<Node> const& items ) { SWEAR(t_thread_key=='=') ; _s_update(_g_node_file.hdr().no_triggers,_no_triggers ,add,items) ; }
	inline void           NodeBase::s_clear_frozens    (                                        ) { SWEAR(t_thread_key=='=') ; _g_node_file.hdr().frozens    .clear() ; _frozen_nodes.clear() ;    }
	inline void           NodeBase::s_clear_no_triggers(                                        ) { SWEAR(t_thread_key=='=') ; _g_node_file.hdr().no_triggers.clear() ; _no_triggers .clear() ;    }
	inline void           NodeBase::s_clear_srcs       (                                        ) { SWEAR(t_thread_key=='=') ; _g_node_file.hdr().srcs       .clear() ;                       ;    }
	//
	inline Targets const NodeBase::s_srcs(bool dirs) {
		SWEAR(t_thread_key=='=') ;
		NodeHdr const& nh = _g_node_file.c_hdr() ;
		return dirs?nh.src_dirs:nh.srcs ;
	}
	inline void NodeBase::s_srcs( bool dirs , bool add , ::vector<Node> const& items ) {
		SWEAR(t_thread_key=='=') ;
		NodeHdr& nh = _g_node_file.hdr() ;
		_s_update( dirs?nh.src_dirs:nh.srcs , add , items ) ;
	}

	// accesses
	inline bool NodeBase::frozen    () const { return _frozen_nodes.contains(Node(+self)) ; }
	inline bool NodeBase::no_trigger() const { return _no_triggers .contains(Node(+self)) ; }
	//
	inline NodeData const& NodeBase::operator*() const { return _g_node_file.c_at(+self) ; }
	inline NodeData      & NodeBase::operator*()       { return _g_node_file.at  (+self) ; }
	// services
	inline void NodeBase::chk() const {
		NodeName fn = self->_name                ;
		Node     n  = _g_node_name_file.c_at(fn) ;
		SWEAR( self==n , self , fn , n ) ;
	}

	//
	// RuleBase
	//
	inline Iota2<Rule> rule_lst(bool with_shared=false) {
		if (+Rule::s_rules) return { with_shared?1:+Special::NUniq , Rule(Rule::s_rules->size()+1) } ; // rules are numbered from 1 to _s_n_rules
		else                return { 0                             , 0                             } ;
	}
	// accesses
	inline RuleData      & RuleBase::data     ()       { SWEAR(+self) ; return (*s_rules)[+self-1] ; } // 0 is reserved to mean no rule
	inline RuleData const& RuleBase::operator*() const { SWEAR(+self) ; return (*s_rules)[+self-1] ; } // .

	//
	// RuleCrcBase
	//
	inline RuleCrcFile::Lst rule_crc_lst() {
		SWEAR(t_thread_key=='=') ;
		return _g_rule_crc_file.lst() ;
	}
	// cxtors & casts
	inline RuleCrcBase::RuleCrcBase( Crc match , Crc cmd , Crc rsrcs ) {
		SWEAR(t_thread_key=='=') ;
		if (!cmd       ) cmd   = match ;                                              // cmd must include match, so if not given, use match
		if (!rsrcs     ) rsrcs = cmd   ;                                              // rsrcs must include cmd, so if not given, use cmd
		if (!s_by_rsrcs)                                                              // auto-init s_by_rsrcs
			for( RuleCrc rc : rule_crc_lst() ) s_by_rsrcs.try_emplace(rc->rsrcs,rc) ;
		auto it_inserted = s_by_rsrcs.try_emplace(rsrcs) ;
		if (it_inserted.second) {
			self = it_inserted.first->second = _g_rule_crc_file.emplace( match , cmd , rsrcs ) ;
		} else {
			self = it_inserted.first->second ;
			RuleCrcData const& d = data() ;
			SWEAR( match==d.match , match , d.match ) ;
			SWEAR( cmd  ==d.cmd   , cmd   , d.cmd   ) ;
			SWEAR( rsrcs==d.rsrcs , rsrcs , d.rsrcs ) ;
		}
	}
	// accesses
	inline RuleCrcData const& RuleCrcBase::operator*() const { return _g_rule_crc_file.c_at(+self) ; }
	inline RuleCrcData      & RuleCrcBase::data     ()       { return _g_rule_crc_file.at  (+self) ; }

	//
	// RuleTgts
	//
	inline RuleTgtsFile::Lst rule_tgts_lst() {
		SWEAR(t_thread_key=='=') ;
		return _g_rule_tgts_file.lst() ;
	}
	// cxtors & casts
	inline RuleTgts::RuleTgts(::span<RuleTgt const> const& gs) : Base{+gs?_g_rule_tgts_file.insert(gs):RuleTgts()} {
		SWEAR(t_thread_key=='=') ;
	}
	inline void RuleTgts::pop() {
		SWEAR(t_thread_key=='=') ;
		_g_rule_tgts_file.pop(+self) ;
		self = RuleTgts() ;
	}
	//
	inline RuleTgts& RuleTgts::operator=(::span<RuleTgt const> const& v) { self = RuleTgts(v) ; return self ; }
	// accesses
	inline ::vector<RuleTgt> RuleTgts::view() const { return _g_rule_tgts_file.key(self) ; }
	// services
	inline void RuleTgts::shorten_by(RuleIdx by) {
		if (by==RuleIdx(-1)) { clear() ; return ; }
		self = _g_rule_tgts_file.insert_shorten_by( self , by ) ;
		if (_g_rule_tgts_file.empty(self)) self = RuleTgts() ;
	}

}

#endif
