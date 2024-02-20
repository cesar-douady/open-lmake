// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "store/store_utils.hh"
#include "store/struct.hh"
#include "store/alloc.hh"
#include "store/vector.hh"
#include "store/prefix.hh"
#include "store/side_car.hh"

#include "idxed.hh"

//
// There are 9 files :
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
// - 3 files for rules :
//   - A rule string file containing strings describing the rule.
//   - A rule index file containing indexes in the rule string file.
//     The reason for this indirection is to have a short (16 bits) index for rules while the index in the rule string file is 32 bits.
//   - A rule-targets file containing vectors of rule-target's. A rule-target is a rule index and a target index within the rule.
//     This file is for use by nodes to represent candidates to generate them.
//     During the analysis process, rule-targets are transformed into job-target when possible (else they are dropped), so that the yet to analyse part which
//     the node keeps is a suffix of the original list.
//     For this reason, the file is stored as a suffix-tree (like a prefix-tree, but reversed).
//

#ifdef STRUCT_DECL

namespace Engine {

	struct StoreMrkr {} ; // just a marker to disambiguate file association

	struct Dep    ;
	struct Target ;

	extern SeqId* g_seq_id ; // used to identify launched jobs. Persistent so that we keep as many old traces as possible

}

namespace Engine {
	namespace Persistent { using RuleStr     = Vector::Simple<RuleStrIdx,char  ,StoreMrkr> ; }
	/**/                   using DepsBase    = Vector::Simple<NodeIdx   ,Dep   ,StoreMrkr> ;
	/**/                   using TargetsBase = Vector::Simple<NodeIdx   ,Target,StoreMrkr> ;
}

#endif

#ifdef STRUCT_DEF

namespace Engine::Persistent {

	struct RuleTgts
	:	             Idxed<RuleTgtsIdx>
	{	using Base = Idxed<RuleTgtsIdx> ;
		// cxtors & casts
		using Base::Base ;
		RuleTgts           (::c_vector_view<RuleTgt> const&  ) ;
		RuleTgts& operator=(::c_vector_view<RuleTgt> const& v) ;
		void pop() ;
		// accesses
		::vector<RuleTgt> view() const ;
		size_t            size() const ;
		// services
		void shorten_by    (RuleIdx by) ;
		void invalidate_old(          ) ;
	} ;

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

	struct DataBase {
		friend struct JobBase  ;
		friend struct NodeBase ;
		// cxtors & casts
		DataBase(      ) = default ;
		DataBase(Name n) : _full_name{n} { fence() ; } // ensure when you reach item by name, its name is the expected one
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
		// statics
		static ::vector<Rule> s_lst() ;
		// static data
		static MatchGen         s_match_gen ;
		static umap_s<RuleBase> s_by_name   ;
		// cxtors & casts
		using Base::Base ;
		constexpr RuleBase(Special s ) : Base{RuleIdx(+s)} { SWEAR( +s && s!=Special::Unknown ) ; } // Special::0 is a marker that says not special
		void invalidate_old() ;
		// accesses
		RuleData      & data      ()       ;
		RuleData const& operator* () const ;
		RuleData const* operator->() const { return &**this ; }
		//
		constexpr bool    is_shared() const { return +*this<=+Special::Shared ; }
		/**/      bool    old      () const ;
		//
		::string_view str() const ;
		// services
		void save() const ;
	private :
		Persistent::RuleStr _str() const ;
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
	using Name        = Persistent::Name                        ;
	using JobBase     = Persistent::JobBase                     ;
	using JobTgtsBase = Vector::Crunch<JobIdx,JobTgt,StoreMrkr> ;
	using NodeBase    = Persistent::NodeBase                    ;
	using RuleBase    = Persistent::RuleBase                    ;
	using RuleTgts    = Persistent::RuleTgts                    ;
	using DataBase    = Persistent::DataBase                    ;
}

#endif
#ifdef INFO_DEF

namespace Engine {
	extern Config     g_config     ;
	extern ::vector_s g_src_dirs_s ;
}

#endif
#ifdef IMPL

namespace Engine::Persistent {

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

	//                                           autolock header     index             key       data       misc
	// jobs
	using JobFile      = Store::AllocFile       < false  , JobHdr   , Job             ,           JobData                     > ;
	using DepsFile     = Store::VectorFile      < false  , void     , Deps            ,           Dep      , NodeIdx , 4      > ;
	using TargetsFile  = Store::VectorFile      < false  , void     , Targets         ,           Target                      > ;
	// nodes
	using NodeFile     = Store::AllocFile       < false  , NodeHdr  , Node            ,           NodeData                    > ;
	using JobTgtsFile  = Store::VectorFile      < false  , void     , JobTgts::Vector ,           JobTgt   , RuleIdx          > ;
	// rules
	using RuleStrFile  = Store::VectorFile      < false  , void     , RuleStr         ,           char     , uint32_t         > ;
	using RuleFile     = Store::AllocFile       < false  , MatchGen , Rule            ,           RuleStr                     > ;
	using RuleTgtsFile = Store::SinglePrefixFile< false  , void     , RuleTgts        , RuleTgt , void     , true /*Reverse*/ > ;
	using SfxFile      = Store::SinglePrefixFile< false  , void     , PsfxIdx         , char    , PsfxIdx  , true /*Reverse*/ > ; // map sfxes to root of pfxes, no lock : static
	using PfxFile      = Store::MultiPrefixFile < false  , void     , PsfxIdx         , char    , RuleTgts , false/*Reverse*/ > ;
	// commons
	using NameFile     = Store::SinglePrefixFile< true   , void     , Name            , char    , JobNode                     > ; // for Job's & Node's

	static constexpr char StartMrkr = 0x0 ; // used to indicate a single match suffix (i.e. a suffix which actually is an entire file name)

	// visible data
	extern bool writable ;

	// on disk
	extern JobFile      _job_file       ; // jobs
	extern DepsFile     _deps_file      ; // .
	extern TargetsFile  _targets_file   ; // .
	extern NodeFile     _node_file      ; // nodes
	extern JobTgtsFile  _job_tgts_file  ; // .
	extern RuleStrFile  _rule_str_file  ; // rules
	extern RuleFile     _rule_file      ; // .
	extern RuleTgtsFile _rule_tgts_file ; // .
	extern SfxFile      _sfxs_file      ; // .
	extern PfxFile      _pfxs_file      ; // .
	extern NameFile     _name_file      ; // commons
	// in memory
	extern ::uset<Job >       _frozen_jobs  ;
	extern ::uset<Node>       _frozen_nodes ;
	extern ::uset<Node>       _no_triggers  ;
	extern ::vector<RuleData> _rule_datas   ;

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
	bool/*invalidate*/ new_srcs        ( ::vmap_s<Disk::FileTag>&& srcs , ::vector_s&& src_dirs_s ) ;
	bool/*invalidate*/ new_rules       ( ::umap<Crc,RuleData>&&                                   ) ;
	void               invalidate_match(                                                          ) ;
	void               invalidate_exec ( bool cmd_ok                                              ) ;
	void               repair          ( ::string const& from_dir                                 ) ;
	//
	NodeFile::Lst  node_lst() ;
	JobFile ::Lst  job_lst () ;
	::vector<Rule> rule_lst() ;
	//
	void chk() ;
	//
	void _init_config       (                                                                              ) ;
	void _diff_config       ( Config const& old_config , bool dynamic                                      ) ;
	void _save_config       (                                                                              ) ;
	void _init_srcs_rules   ( bool rescue=false                                                            ) ;
	void _new_max_dep_depth ( DepDepth                                                                     ) ;
	void _save_rules        (                                                                              ) ;
	void _compile_rule_datas(                                                                              ) ;
	void _compile_psfxs     (                                                                              ) ;
	void _compile_srcs      (                                                                              ) ;
	void _compile_rules     (                                                                              ) ;
	void _compile_n_tokenss (                                                                              ) ;
	void _invalidate_exec   ( ::vector<pair<bool,ExecGen>> const& keep_cmd_gens                            ) ;
	void _collect_old_rules (                                                                              ) ;
	void _set_exec_gen      ( RuleData& , ::pair<bool,ExecGen>& keep_cmd_gen , bool cmd_ok , bool rsrcs_ok ) ;

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
	// RuleTgts
	//
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
	inline void RuleTgts::invalidate_old() {
		for( RuleTgt rt : view() ) if (rt.old()) { pop() ; break ; }
	}

	template<class Disk,class Item> static inline void _s_update( Disk& disk , ::uset<Item>& mem , bool add , ::vector<Item> const& items ) {
		bool modified = false ;
		if      (add ) for( Item i : items ) modified |= mem.insert(i).second ;
		else if (+mem) for( Item i : items ) modified |= mem.erase (i)        ; // fast path : no need to update mem if it is already empty
		if (modified) disk.assign(mk_vector<typename Disk::Item>(mem)) ;
	}
	template<class Disk,class Item> inline void _s_update( Disk& disk , bool add , ::vector<Item> const& items ) {
		::uset<Item> mem = mk_uset<Item>(disk) ;
		_s_update(disk,mem,add,items) ;
	}

	//
	// JobBase
	//
	// statics
	inline bool          JobBase::s_has_frozens  (                                       ) { return               +_job_file.c_hdr().frozens     ;                                  }
	inline ::vector<Job> JobBase::s_frozens      (                                       ) { return mk_vector<Job>(_job_file.c_hdr().frozens   ) ;                                  }
	inline void          JobBase::s_frozens      ( bool add , ::vector<Job> const& items ) { _s_update(            _job_file.hdr  ().frozens   ,_frozen_jobs   ,add,items) ;        }
	inline void          JobBase::s_clear_frozens(                                       ) {                       _job_file.hdr  ().frozens   .clear() ; _frozen_jobs   .clear() ; }
	//
	inline Job JobBase::s_idx(JobData const& jd) { return _job_file.idx(jd) ; }
	// cxtors & casts
	template<class... A> inline JobBase::JobBase( NewType , A&&... args ) {                               // 1st arg is only used to disambiguate
		*this = _job_file.emplace( Name() , ::forward<A>(args)... ) ;
	}
	template<class... A> inline JobBase::JobBase( ::pair_ss const& name_sfx , bool new_ , A&&... args ) { // jobs are only created in main thread, so no locking is necessary
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
	// statics
	inline Node NodeBase::s_idx     (NodeData  const& nd  ) { return  _node_file.idx   (nd  ) ; }
	inline bool NodeBase::s_is_known( ::string const& name) { return +_name_file.search(name) ; }

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
		static ::mutex s_m ;                                                                                // nodes can be created from several threads, ensure coherence between names and nodes
		::unique_lock lock = locked? (SWEAR(!s_m.try_lock()),::unique_lock<mutex>()) : ::unique_lock(s_m) ;
		*this = _name_file.c_at(name_).node() ;
		if (+*this) {
			SWEAR( name_==(*this)->_full_name , *this , name_ , (*this)->_full_name , _name_file.c_at((*this)->_full_name).node() ) ;
		} else {
			_name_file.at(name_) = *this = _node_file.emplace(name_,no_dir,true/*locked*/) ;                // if dir must be created, we already hold the lock
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
	//statics
	inline ::vector<Rule> RuleBase::s_lst() { return Persistent::rule_lst() ; }
	// cxtors & casts
	inline void RuleBase::invalidate_old() { if (old()) _rule_file.pop(Rule(*this)) ; }
	// accesses
	inline RuleData      & RuleBase::data     ()       {                       return _rule_datas[+*this]              ; }
	inline RuleData const& RuleBase::operator*() const {                       return _rule_datas[+*this]              ; }
	inline bool            RuleBase::old      () const {                       return !is_shared() && !_str()          ; }
	inline ::string_view   RuleBase::str      () const {                       return _rule_str_file.str_view(+_str()) ; }
	inline RuleStr         RuleBase::_str     () const { SWEAR(!is_shared()) ; return _rule_file.c_at(Rule(*this))     ; }
	// services
	inline void RuleBase::save() const {
		_rule_file.at(*this) = _rule_str_file.assign(_str(),::string(**this)) ;
	}

	//
	// Persistent
	//

	inline NodeFile::Lst  node_lst() { return _node_file.lst() ; }
	inline JobFile ::Lst  job_lst () { return _job_file .lst() ; }
	inline ::vector<Rule> rule_lst() {
		::vector<Rule> res ; res.reserve(_rule_file.size()) ;
		for( Rule r : _rule_file.lst() ) if ( !r.is_shared() && !r.old() ) res.push_back(r) ;
		return res ;
	}

}

#endif
