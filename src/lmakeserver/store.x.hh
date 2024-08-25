// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <ranges>

#include "store/store_utils.hh"
#include "store/struct.hh"
#include "store/alloc.hh"
#include "store/vector.hh"
#include "store/prefix.hh"
#include "store/side_car.hh"

#include "idxed.hh"

//
// There are 12 files :
// - 1 name file associates a name with either a node or a job :
//   - This is a prefix-tree to share as much prefixes as possible since names tend to share a lot of prefixes
//   - For jobs, a suffix containing the rule and the positions of the stems is added.
//   - Before this suffix, a non printable char is inserted to distinguish nodes and jobs.
//   - A single file is used to store both nodes and jobs as they tend to share the same prefixes.
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

namespace Engine {

	struct RepairDigest {
		JobIdx n_repaired  = 0 ;
		JobIdx n_processed = 0 ;
	} ;
}

namespace Engine::Persistent {

	struct RuleTgts ;

	struct Name
	:	             Idxed<NameIdx>
	{	using Base = Idxed<NameIdx> ;
		// cxtors & casts
		using Base::Base ;
		void pop() ;
		// accesses
		::string str   (size_t sfx_sz=0) const ;
		size_t   str_sz(size_t sfx_sz=0) const ;
		// services
		Name dir() const ;
	} ;

	struct JobNodeData {                                  // the common part for JobData and NodeData
		friend struct JobBase  ;
		friend struct NodeBase ;
		// cxtors & casts
		JobNodeData(      ) = default ;
		JobNodeData(Name n) : _full_name{n} { fence() ; } // ensure when you reach item by name, its name is the expected one
		// accesses
		::string full_name   (FileNameIdx sfx_sz=0) const { return _full_name.str   (sfx_sz) ; }
		size_t   full_name_sz(FileNameIdx sfx_sz=0) const { return _full_name.str_sz(sfx_sz) ; }
	protected :
		Name _dir_name() const { return _full_name.dir() ; }
		// data
	private :
		Name _full_name ;
	} ;

	struct JobBase
	:	             Idxed<JobIdx,JobNGuardBits>
	{	using Base = Idxed<JobIdx,JobNGuardBits> ;
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
		JobData      & operator* () ;
		JobData const* operator->() const { return &**this ; }
		JobData      * operator->()       { return &**this ; }
		//
		RuleIdx rule_idx () const ;
		bool    frozen   () const ;
		// services
		void chk() const ;
	} ;

	struct NodeBase
	:	             Idxed<NodeIdx,NodeNGuardBits>
	{	using Base = Idxed<NodeIdx,NodeNGuardBits> ;
		// statics
		static Node           s_idx              ( NodeData const&                  ) ;
		static bool           s_is_known         ( ::string const&                  ) ;
		static bool           s_has_frozens      (                                  ) ;
		static bool           s_has_no_triggers  (                                  ) ;
		static bool           s_has_srcs         (                                  ) ;
		static ::vector<Node> s_frozens          (                                  ) ;
		static ::vector<Node> s_no_triggers      (                                  ) ;
		static void           s_frozens          ( bool add , ::vector<Node> const& ) ;     // erase (!add) or insert (add)
		static void           s_no_triggers      ( bool add , ::vector<Node> const& ) ;     // .
		static void           s_clear_frozens    (                                  ) ;
		static void           s_clear_no_triggers(                                  ) ;
		static void           s_clear_srcs       (                                  ) ;
		//
		static Targets const s_srcs( bool dirs                                    ) ;
		static void          s_srcs( bool dirs , bool add , ::vector<Node> const& ) ;       // erase (!add) or insert (add)
		//
		static RuleTgts s_rule_tgts(::string const& target_name) ;
		// cxtors & casts
		using Base::Base ;
		/**/     NodeBase( ::string const& name , bool no_dir=false , bool locked=false ) ; // if locked, lock is already taken
		explicit NodeBase( Name                 , bool no_dir=false , bool locked=false ) ; // .
		// accesses
	public :
		NodeData const& operator* () const ;
		NodeData      & operator* () ;
		NodeData const* operator->() const { return &**this ; }
		NodeData      * operator->()       { return &**this ; }
		bool            frozen    () const ;
		bool            no_trigger() const ;
		// services
		void chk() const ;
	} ;

	struct RuleBase
	:	             Idxed<RuleIdx>
	{	using Base = Idxed<RuleIdx> ;
		static constexpr size_t NameSz = "no_rule"s.size() ;              // minimum size of the rule field : account for internally generated messages
		// statics
		static void s_from_disk           (                    ) ;
		static void s_from_vec_dynamic    (::vector<RuleData>&&) ;
		static void s_from_vec_not_dynamic(::vector<RuleData>&&) ;
	private :
		static void _s_init_vec      (bool ping) ;
		static void _s_set_rule_datas(bool ping) ;
		static void _s_save          (         ) ;
		static void _s_update_crcs   (         ) ;
		// static data
	public :
		static MatchGen       s_match_gen    ;
		static ::umap_s<Rule> s_by_name      ;
		static size_t         s_name_sz      ;
		static bool           s_ping         ;                            // use ping-pong to update _s_rule_datas atomically
		static RuleIdx        s_n_rule_datas ;
	private :
		static ::vector<RuleData>  _s_rule_data_vecs[2] ;
		static ::atomic<RuleData*> _s_rule_datas        ;
		// cxtors & casts
	public :
		using Base::Base ;
		constexpr RuleBase(Special s) : Base{RuleIdx(+s)} { SWEAR(+s) ; } // Special::0 is a marker that says not special
		// accesses
		RuleData      & data      ()       ;
		RuleData const& operator* () const ;
		RuleData const* operator->() const { return &**this ; }
		//
		constexpr bool is_shared() const { return +*this<+Special::NShared ; }
		::string_view  str      () const ;
		// services
		void save() const ;
	private :
		RuleStr _str() const ;
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
		RuleCrcData const* operator->() const { return &**this ; }
	} ;

	struct RuleTgts
	:	             Idxed<RuleTgtsIdx>
	{	using Base = Idxed<RuleTgtsIdx> ;
		// statics
		// cxtors & casts
		using Base::Base ;
		RuleTgts           (::c_vector_view<RuleTgt> const&  ) ;
		RuleTgts& operator=(::c_vector_view<RuleTgt> const& v) ;
		void pop() ;
		// accesses
		::vector<RuleTgt> view() const ;
		size_t            size() const ;
		// services
		void shorten_by(RuleIdx by) ;
	} ;

	struct SfxBase
	:	             Idxed<RuleIdx>
	{	using Base = Idxed<RuleIdx> ;
		// cxtors & casts
		using Base::Base ;
	} ;

	struct JobNode
	:	             Idxed<WatcherIdx>   // can index Node or Job (no need to distinguish as Job names are suffixed with rule)
	{	using Base = Idxed<WatcherIdx> ;
		// cxtors & casts
		using Base::Base ;
		JobNode(JobBase ) ;
		JobNode(NodeBase) ;
		Job  job () const ;
		Node node() const ;
	} ;

}

namespace Engine {
	using Name        = Persistent::Name                            ;
	using JobBase     = Persistent::JobBase                         ;
	using JobTgtsBase = Vector::Crunch<JobTgtsIdx,JobTgt,StoreMrkr> ;
	using NodeBase    = Persistent::NodeBase                        ;
	using RuleBase    = Persistent::RuleBase                        ;
	using RuleCrcBase = Persistent::RuleCrcBase                     ;
	using RuleTgts    = Persistent::RuleTgts                        ;
	using JobNodeData = Persistent::JobNodeData                     ;
}

#endif
#ifdef INFO_DEF

namespace Engine {

	extern Config    * g_config     ; // ensure g_config is not destroyed upon exit, while we may still need it
	extern ::vector_s* g_src_dirs_s ;

	struct RuleLst {
		struct Iterator {
			using iterator_categorie = ::input_iterator_tag ;
			using value_type         = Rule                 ;
			using difference_type    = ptrdiff_t            ;
			using pointer            = value_type*          ;
			using reference          = value_type&          ;
			// cxtors & casts
			Iterator(Rule r) : _cur(r) {}
			// services
			bool     operator==(Iterator const&) const = default ;
			Rule     operator* (               ) const {                                  return _cur  ; }
			Iterator operator++(               )       { _cur = +_cur+1 ;                 return *this ; }
			Iterator operator++(int            )       { Iterator res = *this ; ++*this ; return res   ; }
		private :
			// data
			Rule _cur ;
		} ;
		// accesses
		RuleIdx size() const { return Rule::s_n_rule_datas - (with_shared?1:+Special::NShared) ; }
		// services
		Iterator begin() const { return Iterator(with_shared?1:+Special::NShared) ; }
		Iterator end  () const { return Iterator(Rule::s_n_rule_datas           ) ; }
		// data
		bool with_shared = false ;
	} ;
}

#endif
#ifdef IMPL

namespace Engine::Persistent {

	// START_OF_VERSIONING

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

	//                                          autolock  header              index             key       data          misc
	// jobs
	using JobFile      = Store::AllocFile       < false , JobHdr            , Job             ,           JobData                        > ;
	using DepsFile     = Store::VectorFile      < false , void              , Deps            ,           GenericDep  , NodeIdx , 4      > ; // Deps are compressed when Crc==None
	using TargetsFile  = Store::VectorFile      < false , void              , Targets         ,           Target                         > ;
	// nodes
	using NodeFile     = Store::AllocFile       < false , NodeHdr           , Node            ,           NodeData                       > ;
	using JobTgtsFile  = Store::VectorFile      < false , void              , JobTgts::Vector ,           JobTgt      , RuleIdx          > ;
	// rules
	using RuleFile     = Store::StructFile      < false , MatchGen          , RuleIdx         ,           RuleStr                        > ;
	using RuleCrcFile  = Store::AllocFile       < false , void              , RuleCrc         ,           RuleCrcData                    > ;
	using RuleStrFile  = Store::VectorFile      < false , size_t/*name_sz*/ , RuleStr         ,           char        , uint32_t         > ;
	using RuleTgtsFile = Store::SinglePrefixFile< false , void              , RuleTgts        , RuleTgt , void        , true /*Reverse*/ > ;
	using SfxFile      = Store::SinglePrefixFile< false , void              , PsfxIdx         , char    , PsfxIdx     , true /*Reverse*/ > ; // map sfxes to root of pfxes, no lock : static
	using PfxFile      = Store::MultiPrefixFile < false , void              , PsfxIdx         , char    , RuleTgts    , false/*Reverse*/ > ;
	// commons
	using NameFile     = Store::SinglePrefixFile< true  , void              , Name            , char    , JobNode                        > ; // for Job's & Node's

	static constexpr char StartMrkr = 0x0 ; // used to indicate a single match suffix (i.e. a suffix which actually is an entire file name)

	// END_OF_VERSIONING

	// on disk
	extern JobFile      _job_file       ; // jobs
	extern DepsFile     _deps_file      ; // .
	extern TargetsFile  _targets_file   ; // .
	extern NodeFile     _node_file      ; // nodes
	extern JobTgtsFile  _job_tgts_file  ; // .
	extern RuleFile     _rule_file      ; // rules
	extern RuleCrcFile  _rule_crc_file  ; // .
	extern RuleStrFile  _rule_str_file  ; // .
	extern RuleTgtsFile _rule_tgts_file ; // .
	extern SfxFile      _sfxs_file      ; // .
	extern PfxFile      _pfxs_file      ; // .
	extern NameFile     _name_file      ; // commons
	// in memory
	extern ::uset<Job >       _frozen_jobs  ;
	extern ::uset<Node>       _frozen_nodes ;
	extern ::uset<Node>       _no_triggers  ;

}

namespace Vector {
	template<> struct File<Engine            ::DepsBase   > { static constexpr Engine::Persistent::DepsFile   & file = Engine::Persistent::_deps_file     ; } ;
	template<> struct File<Engine            ::TargetsBase> { static constexpr Engine::Persistent::TargetsFile& file = Engine::Persistent::_targets_file  ; } ;
	template<> struct File<Engine            ::JobTgtsBase> { static constexpr Engine::Persistent::JobTgtsFile& file = Engine::Persistent::_job_tgts_file ; } ;
	template<> struct File<Engine::Persistent::RuleStr    > { static constexpr Engine::Persistent::RuleStrFile& file = Engine::Persistent::_rule_str_file ; } ;
}

namespace Engine::Persistent {

	void new_config( Config&& , bool dynamic , bool rescue=false , ::function<void(Config const& old,Config const& new_)> diff=[](Config const&,Config const&)->void{} ) ;
	//
	bool/*invalidate*/ new_srcs        ( ::pair<::vmap_s<FileTag>/*files*/,::vector_s/*dirs_s*/>&& srcs , bool dynamic ) ;
	bool/*invalidate*/ new_rules       ( ::vector<RuleData>&&                                           , bool dynamic ) ;
	void               invalidate_match(                                                                               ) ;
	void               invalidate_exec ( bool cmd_ok                                                                   ) ;
	RepairDigest       repair          ( ::string const& from_dir_s                                                    ) ;
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
	// JobNode
	//
	// cxtors & casts
	inline JobNode::JobNode(JobBase  j) : Base(+j) {}
	inline JobNode::JobNode(NodeBase n) : Base(+n) {}
	inline Job  JobNode::job () const { return +*this ; }
	inline Node JobNode::node() const { return +*this ; }

	//
	// Name
	//
	// cxtors & casts
	inline void Name::pop() { Persistent::_name_file.pop(+*this) ; }
	// accesses
	inline ::string Name::str   (size_t sfx_sz) const { return Persistent::_name_file.str_key(+*this,sfx_sz) ; }
	inline size_t   Name::str_sz(size_t sfx_sz) const { return Persistent::_name_file.key_sz (+*this,sfx_sz) ; }
	// services
	inline Name     Name::dir   (             ) const { return Persistent::_name_file.insert_dir(*this,'/')  ; }

	//
	// JobBase
	//
	inline JobFile::Lst job_lst() { return _job_file .lst(  ); }
	// statics
	inline Job JobBase::s_idx(JobData const& jd) { return _job_file.idx(jd) ; }
	//
	inline bool          JobBase::s_has_frozens  (                                       ) { return               +_job_file.c_hdr().frozens     ;                                  }
	inline ::vector<Job> JobBase::s_frozens      (                                       ) { return mk_vector<Job>(_job_file.c_hdr().frozens   ) ;                                  }
	inline void          JobBase::s_frozens      ( bool add , ::vector<Job> const& items ) { _s_update(            _job_file.hdr  ().frozens   ,_frozen_jobs   ,add,items) ;        }
	inline void          JobBase::s_clear_frozens(                                       ) {                       _job_file.hdr  ().frozens   .clear() ; _frozen_jobs   .clear() ; }
	// cxtors & casts
	template<class... A> JobBase::JobBase( NewType , A&&... args ) {                               // 1st arg is only used to disambiguate
		*this = _job_file.emplace( Name() , ::forward<A>(args)... ) ;
	}
	template<class... A> JobBase::JobBase( ::pair_ss const& name_sfx , bool new_ , A&&... args ) { // jobs are only created in main thread, so no locking is necessary
		Name name_ = _name_file.insert(name_sfx.first,name_sfx.second) ;
		*this = _name_file.c_at(+name_).job() ;
		if (+*this) {
			SWEAR( name_==(*this)->_full_name , name_ , (*this)->_full_name ) ;
			if (!new_) return ;
			**this = JobData( name_ , ::forward<A>(args)...) ;
		} else {
			_name_file.at(+name_) = *this = _job_file.emplace( name_ , ::forward<A>(args)... ) ;
		}
		(*this)->_full_name = name_ ;
	}
	inline void JobBase::pop() {
		if (!*this) return ;
		if (+(*this)->_full_name) (*this)->_full_name.pop() ;
		_job_file.pop(+*this) ;
		clear() ;
	}
	// accesses
	inline bool JobBase::frozen() const { return _frozen_jobs.contains(Job(+*this)) ; }
	//
	inline JobData const& JobBase::operator*() const { return _job_file.c_at(+*this) ; }
	inline JobData      & JobBase::operator*()       { return _job_file.at  (+*this) ; }
	// services
	inline void JobBase::chk() const {
		Name fn = (*this)->_full_name ;
		if (!fn) return ;
		Job  j  = _name_file.c_at(fn).job() ;
		SWEAR( *this==j , *this , fn , j ) ;
	}

	//
	// NodeBase
	//
	inline NodeFile::Lst node_lst() { return _node_file.lst() ; }
	// statics
	inline Node NodeBase::s_idx     (NodeData  const& nd  ) { return  _node_file.idx   (nd  ) ; }
	inline bool NodeBase::s_is_known( ::string const& name) { return +_name_file.search(name) ; }
	//
	inline bool           NodeBase::s_has_frozens      (                                        ) { return                +_node_file.c_hdr().frozens       ;                                  }
	inline bool           NodeBase::s_has_no_triggers  (                                        ) { return                +_node_file.c_hdr().no_triggers   ;                                  }
	inline bool           NodeBase::s_has_srcs         (                                        ) { return                +_node_file.c_hdr().srcs          ;                                  }
	inline ::vector<Node> NodeBase::s_frozens          (                                        ) { return mk_vector<Node>(_node_file.c_hdr().frozens     ) ;                                  }
	inline ::vector<Node> NodeBase::s_no_triggers      (                                        ) { return mk_vector<Node>(_node_file.c_hdr().no_triggers ) ;                                  }
	inline void           NodeBase::s_frozens          ( bool add , ::vector<Node> const& items ) { _s_update(             _node_file.hdr  ().frozens    ,_frozen_nodes   ,add,items) ;        }
	inline void           NodeBase::s_no_triggers      ( bool add , ::vector<Node> const& items ) { _s_update(             _node_file.hdr  ().no_triggers,_no_triggers    ,add,items) ;        }
	inline void           NodeBase::s_clear_frozens    (                                        ) {                        _node_file.hdr  ().frozens    .clear() ; _frozen_nodes   .clear() ; }
	inline void           NodeBase::s_clear_no_triggers(                                        ) {                        _node_file.hdr  ().no_triggers.clear() ; _no_triggers    .clear() ; }
	inline void           NodeBase::s_clear_srcs       (                                        ) {                        _node_file.hdr  ().srcs       .clear() ;                          ; }
	//
	inline Targets const NodeBase::s_srcs( bool dirs                                          ) { NodeHdr const& nh = _node_file.c_hdr() ; return     dirs?nh.src_dirs:nh.srcs ;                 }
	inline void          NodeBase::s_srcs( bool dirs , bool add , ::vector<Node> const& items ) { NodeHdr      & nh = _node_file.hdr  () ; _s_update( dirs?nh.src_dirs:nh.srcs , add , items ) ; }

	// cxtors & casts
	inline NodeBase::NodeBase( Name name_ , bool no_dir , bool locked ) {
		if (!name_) return ;
		*this = _name_file.c_at(name_).node() ;
		if (+*this) {                                                                                                                 // fast path : avoid taking lock
			SWEAR( name_==(*this)->_full_name , *this , name_ , (*this)->_full_name , _name_file.c_at((*this)->_full_name).node() ) ;
			return ;
		}
		// restart with lock
		static Mutex<MutexLvl::Node> s_m ;                                                    // nodes can be created from several threads, ensure coherence between names and nodes
		Lock lock = locked ? (s_m.swear_locked(),Lock<Mutex<MutexLvl::Node>>()) : Lock(s_m) ;
		*this = _name_file.c_at(name_).node() ;
		if (+*this) {
			SWEAR( name_==(*this)->_full_name , *this , name_ , (*this)->_full_name , _name_file.c_at((*this)->_full_name).node() ) ;
		} else {
			_name_file.at(name_) = *this = _node_file.emplace(name_,no_dir,true/*locked*/) ;  // if dir must be created, we already hold the lock
		}
	}
	inline NodeBase::NodeBase( ::string const& n , bool no_dir , bool locked ) {
		*this = Node( _name_file.insert(n) , no_dir , locked ) ;
	}
	// accesses
	inline bool NodeBase::frozen    () const { return _frozen_nodes.contains(Node(+*this)) ; }
	inline bool NodeBase::no_trigger() const { return _no_triggers .contains(Node(+*this)) ; }
	//
	inline NodeData const& NodeBase::operator*() const { return _node_file.c_at(+*this) ; }
	inline NodeData      & NodeBase::operator*()       { return _node_file.at  (+*this) ; }
	// services
	inline void NodeBase::chk() const {
		Name fn = (*this)->_full_name ;
		Node n  = _name_file.c_at(fn).node() ;
		SWEAR( *this==n , *this , fn , n ) ;
	}

	//
	// RuleBase
	//
	inline void RuleBase::_s_set_rule_datas(bool ping) {
		_s_rule_datas  = _s_rule_data_vecs[ping].data()-1 ;                                                                            // entry 0 is not stored in _s_rule_data_vecs
		s_n_rule_datas = _s_rule_data_vecs[ping].size()+1 ;
	}
	inline RuleLst rule_lst(bool with_shared=false) { return RuleLst(with_shared) ; }
	// accesses
	inline RuleData      & RuleBase::data     ()       { SWEAR(+*this      ) ; return _s_rule_datas[+*this]                       ; }
	inline RuleData const& RuleBase::operator*() const { SWEAR(+*this      ) ; return _s_rule_datas[+*this]                       ; }
	inline ::string_view   RuleBase::str      () const {                       return _rule_str_file.str_view(+_str())            ; }
	inline RuleStr         RuleBase::_str     () const { SWEAR(!is_shared()) ; return _rule_file.c_at(+*this-+Special::NShared+1) ; } // first rule (NShared) is mapped to 1

	//
	// RuleCrcBase
	//
	inline RuleCrcFile::Lst rule_crc_lst() { return _rule_crc_file.lst() ; }
	// cxtors & casts
	inline RuleCrcBase::RuleCrcBase( Crc match , Crc cmd , Crc rsrcs ) {
		if (!cmd       ) cmd   = match ;                                              // cmd must include match, so if not given, use match
		if (!rsrcs     ) rsrcs = cmd   ;                                              // rsrcs must include cmd, so if not given, use cmd
		if (!s_by_rsrcs)                                                              // auto-init s_by_rsrcs
			for( RuleCrc rc : rule_crc_lst() ) s_by_rsrcs.try_emplace(rc->rsrcs,rc) ;
		auto it_inserted = s_by_rsrcs.try_emplace(rsrcs) ;
		if (it_inserted.second) {
			*this = it_inserted.first->second = _rule_crc_file.emplace( match , cmd , rsrcs ) ;
		} else {
			*this = it_inserted.first->second ;
			RuleCrcData const& d = data() ;
			SWEAR( match==d.match , match , d.match ) ;
			SWEAR( cmd  ==d.cmd   , cmd   , d.cmd   ) ;
			SWEAR( rsrcs==d.rsrcs , rsrcs , d.rsrcs ) ;
		}
	}
	// accesses
	inline RuleCrcData      & RuleCrcBase::data     ()       { return _rule_crc_file.at(+*this) ; }
	inline RuleCrcData const& RuleCrcBase::operator*() const { return _rule_crc_file.at(+*this) ; }

	//
	// RuleTgts
	//
	inline RuleTgtsFile::Lst rule_tgts_lst() { return _rule_tgts_file.lst() ; }
	// cxtors & casts
	inline RuleTgts::RuleTgts(::c_vector_view<RuleTgt> const& gs) : Base{+gs?_rule_tgts_file.insert(gs):RuleTgts()} {}
	inline void RuleTgts::pop() { _rule_tgts_file.pop(+*this) ; *this = RuleTgts() ; }
	//
	inline RuleTgts& RuleTgts::operator=(::c_vector_view<RuleTgt> const& v) { *this = RuleTgts(v) ; return *this ; }
	// accesses
	inline ::vector<RuleTgt> RuleTgts::view() const { return _rule_tgts_file.key(*this) ; }
	// services
	inline void RuleTgts::shorten_by(RuleIdx by) {
		if (by==RuleIdx(-1)) { clear() ; return ; }
		*this = _rule_tgts_file.insert_shorten_by( *this , by ) ;
		if (_rule_tgts_file.empty(*this)) *this = RuleTgts() ;
	}

}

#endif
