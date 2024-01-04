// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "pycxx.hh"

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

	struct Dep    ;
	struct Target ;

	extern SeqId* g_seq_id ;           // used to identify launched jobs. Persistent so that we keep as many old traces as possible

}

namespace Engine::Persistent {

	template<class V> struct GenericVector ;

	template<class Idx_,class Item_,uint8_t NGuardBits=0> struct SimpleVectorBase ;
	template<class Idx_,class Item_,uint8_t NGuardBits=1> struct CrunchVectorBase ;
	template<class Idx_,class Item_                     > using SimpleVector = GenericVector<SimpleVectorBase<Idx_,Item_>> ;
	template<class Idx_,class Item_                     > using CrunchVector = GenericVector<CrunchVectorBase<Idx_,Item_>> ;

	using DepsBase    = SimpleVector<NodeIdx,Dep   > ;
	using TargetsBase = SimpleVector<NodeIdx,Target> ;

}

namespace Engine {
	using DepsBase    = Persistent::DepsBase    ;
	using TargetsBase = Persistent::TargetsBase ;
}

#endif

#ifdef STRUCT_DEF

namespace Engine::Persistent {

	//
	// Vector's
	//

	template<class Idx_,class Item_,uint8_t NGuardBits> struct SimpleVectorBase
	:	             Idxed<Idx_,NGuardBits>
	{	using Base = Idxed<Idx_,NGuardBits> ;
		using Idx  = Idx_  ;
		using Item = Item_ ;
		using Sz   = Idx   ;
		static const Idx EmptyIdx ;
		// cxtors & casts
		using Base::Base ;
		//
		template<IsA<Item> I> SimpleVectorBase(I                const& x) : SimpleVectorBase(::vector_view<I>(&x,1)) {}
		template<IsA<Item> I> SimpleVectorBase(::vector_view<I> const&  ) ;
		template<IsA<Item> I> void assign     (::vector_view<I> const&  ) ;
		//
		void pop   () ;
		void clear () ;
		void forget() { Base::clear() ; }
		// accesses
		auto        size () const -> Sz ;
		Item const* items() const ;
		Item      * items()       ;
		// services
		void shorten_by(Sz by) ;
		//
		template<IsA<Item> I> void append(::vector_view<I> const&) ;
	} ;

	// CrunchVector's are like SimpleVector's except that a vector of 0 element is simply 0 and a vector of 1 element is stored in place
	// This is particular efficient for situations where the vector size is 1 most of the time
	template<class Idx_,class Item_,uint8_t NGuardBits> struct CrunchVectorBase
	:	               Idxed2< Item_ , Idxed<Idx_,NGuardBits> >
	{	using Base   = Idxed2< Item_ , Idxed<Idx_,NGuardBits> > ;
		using Vector =                 Idxed<Idx_,NGuardBits>   ;
		using Idx    =                       Idx_               ;
		using Item   =         Item_                            ;
		using Sz     =                       Idx                ;
		static_assert(sizeof(Item_)>=sizeof(Idx_)) ;                           // else it is difficult to implement the items() method with a cast in case of single element
		// cxtors & casts
		using Base::Base ;
		//
		template<IsA<Item> I> CrunchVectorBase(I                const& x) : Base(x) {}
		template<IsA<Item> I> CrunchVectorBase(::vector_view<I> const&  ) ;
		template<IsA<Item> I> void assign     (::vector_view<I> const&  ) ;
		//
		void pop   () ;
		void clear () ;
		void forget() { Base::clear() ; }
		// accesses
		auto        size () const -> Sz ;
		Item const* items() const ;
		Item      * items()       ;
	private :
		bool _multi () const { return !this->template is_a<Item  >() ; }       // 0 is both a Vector and an Item, so this way 0 is !_multi ()
		bool _single() const { return !this->template is_a<Vector>() ; }       // 0 is both a Vector and an Item, so this way 0 is !_single()
		// services
	public :
		void shorten_by(Sz by) ;
		//
		template<IsA<Item> I> void append(::vector_view<I> const&) ;
	} ;

	template<class V> ::ostream& operator<<( ::ostream& , GenericVector<V> const& ) ;
	template<class V> struct GenericVector : V {
		friend ::ostream& operator<< <>( ::ostream& , GenericVector const& ) ;
		using Base       = V                   ;
		using Idx        = typename Base::Idx  ;
		using Item       = typename Base::Item ;
		using value_type = Item                ;                               // mimic vector
		static constexpr bool   IsStr = IsChar<Item> ;
		//
		using Base::items ;
		using Base::size  ;
		// cxtors & casts
		using Base::Base  ;
		//
		template<IsA<Item> I> requires( ::is_const_v<I>) GenericVector(::vector           <::remove_const_t<I>> const& v) : Base{c_vector_view<I>(v)} {}
		template<IsA<Item> I> requires(!::is_const_v<I>) GenericVector(::vector           <                 I > const& v) : Base{c_vector_view<I>(v)} {}
		template<IsA<Item> I> requires(IsStr           ) GenericVector(::basic_string_view<                 I > const& s) : Base{c_vector_view<I>(s)} {}
		template<IsA<Item> I> requires(IsStr           ) GenericVector(::basic_string     <                 I > const& s) : Base{c_vector_view<I>(s)} {}
		//
		template<IsA<Item> I>                            void assign(::vector_view      <                 I > const& v) { Base::assign(                 v ) ; }
		template<IsA<Item> I> requires( ::is_const_v<I>) void assign(::vector           <::remove_const_t<I>> const& v) {       assign(c_vector_view<I>(v)) ; }
		template<IsA<Item> I> requires(!::is_const_v<I>) void assign(::vector           <                 I > const& v) {       assign(c_vector_view<I>(v)) ; }
		template<IsA<Item> I> requires(IsStr           ) void assign(::basic_string_view<                 I > const& s) {       assign(c_vector_view<I>(s)) ; }
		template<IsA<Item> I> requires(IsStr           ) void assign(::basic_string     <                 I > const& s) {       assign(c_vector_view<I>(s)) ; }
		//
		operator ::c_vector_view    <Item>() const                 { return view    () ; }
		operator ::vector_view      <Item>()                       { return view    () ; }
		operator ::basic_string_view<Item>() const requires(IsStr) { return str_view() ; }
		// accesses
		::c_vector_view    <Item> view    () const                 { return { items() , size() } ; }
		::vector_view      <Item> view    ()                       { return { items() , size() } ; }
		::basic_string_view<Item> str_view() const requires(IsStr) { return { items() , size() } ; }
		//
		Item const* begin     (        ) const { return items()           ; }  // mimic vector
		Item      * begin     (        )       { return items()           ; }  // .
		Item const* cbegin    (        ) const { return items()           ; }  // .
		Item const* end       (        ) const { return items()+size()    ; }  // .
		Item      * end       (        )       { return items()+size()    ; }  // .
		Item const* cend      (        ) const { return items()+size()    ; }  // .
		Item const& front     (        ) const { return items()[0       ] ; }  // .
		Item      & front     (        )       { return items()[0       ] ; }  // .
		Item const& back      (        ) const { return items()[size()-1] ; }  // .
		Item      & back      (        )       { return items()[size()-1] ; }  // .
		Item const& operator[](size_t i) const { return items()[i       ] ; }  // .
		Item      & operator[](size_t i)       { return items()[i       ] ; }  // .
		//
		::c_vector_view    <Item> const subvec( size_t start , size_t sz=Npos ) const { return ::c_vector_view    ( begin()+start , ::min(sz,size()-start) ) ; }
		::vector_view      <Item>       subvec( size_t start , size_t sz=Npos )       { return ::vector_view      ( begin()+start , ::min(sz,size()-start) ) ; }
		::basic_string_view<Item> const substr( size_t start , size_t sz=Npos ) const { return ::basic_string_view( begin()+start , ::min(sz,size()-start) ) ; }
		::basic_string_view<Item>       substr( size_t start , size_t sz=Npos )       { return ::basic_string_view( begin()+start , ::min(sz,size()-start) ) ; }
		// services
		template<IsA<Item> I> void append(::vector_view      <I> const& v) { return Base::append(                v ) ; }
		template<IsA<Item> I> void append(::vector           <I> const& v) { return       append(::c_vector_view(v)) ; }
		template<IsA<Item> I> void append(::basic_string_view<I> const& s) { return       append(::c_vector_view(s)) ; }
		template<IsA<Item> I> void append(::basic_string     <I> const& s) { return       append(::c_vector_view(s)) ; }
	} ;
	template<class V> ::ostream& operator<<( ::ostream& os , GenericVector<V> const& gv ) {
		bool first = true ;
		/**/                                                                  os <<'[' ;
		for( typename V::Item const& x : gv ) { if (first) first=false ; else os <<',' ; os << x ; }
		return                                                                os <<']' ;
	}

	using RuleStr = SimpleVector<RuleStrIdx,char> ;

	struct RuleTgts
	:	             Idxed<RuleTgtsIdx>
	{	using Base = Idxed<RuleTgtsIdx> ;
		// cxtors & casts
		using Base::Base ;
		RuleTgts           (::c_vector_view<RuleTgt> const&  ) ;
		RuleTgts& operator=(::c_vector_view<RuleTgt> const& v) ;
		void pop() ;
		// accesses
		::vector<RuleTgt> view () const ;
		size_t            size () const ;
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
		DataBase(Name n) : _full_name{n} {} ;
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
		template<class... A> JobBase( ::pair_ss const& name_sfx , bool new_  , A&&... ) ;
	public :
		void pop() ;
		// accesses
		JobData const& operator* () const ;
		JobData      & operator* () ;
		JobData const* operator->() const { return &**this ; }
		JobData      * operator->()       { return &**this ; }
		//
		RuleIdx rule_idx() const ;
		bool    frozen  () const ;
	} ;

	struct NodeBase
	:	             Idxed<NodeIdx,NodeNGuardBits>
	{	using Base = Idxed<NodeIdx,NodeNGuardBits> ;
		// statics
		static Node           s_idx              ( NodeData const&                  ) ;
		static bool           s_is_known         ( ::string const&                  ) ;
		static bool           s_has_frozens      (                                  ) ;
		static bool           s_has_manual_oks   (                                  ) ;
		static bool           s_has_no_triggers  (                                  ) ;
		static bool           s_has_srcs         (                                  ) ;
		static ::vector<Node> s_frozens          (                                  ) ;
		static ::vector<Node> s_manual_oks       (                                  ) ;
		static ::vector<Node> s_no_triggers      (                                  ) ;
		static void           s_frozens          ( bool add , ::vector<Node> const& ) ; // erase (!add) or insert (add)
		static void           s_manual_oks       ( bool add , ::vector<Node> const& ) ; // erase (!add) or insert (add)
		static void           s_no_triggers      ( bool add , ::vector<Node> const& ) ; // .
		static void           s_clear_frozens    (                                  ) ;
		static void           s_clear_manual_oks (                                  ) ;
		static void           s_clear_no_triggers(                                  ) ;
		static void           s_clear_srcs       (                                  ) ;
		//
		static Targets const s_srcs( bool dirs                                    ) ;
		static void          s_srcs( bool dirs , bool add , ::vector<Node> const& ) ; // erase (!add) or insert (add)
		//
		static RuleTgts s_rule_tgts(::string const& target_name) ;
		// cxtors & casts
		using Base::Base ;
		NodeBase( ::string const& name , bool no_dir=false ) ;
		NodeBase( Name                 , bool no_dir=false ) ;                 // no lock as it is managed in public cxtor & dir method
		// accesses
	public :
		NodeData const& operator* () const ;
		NodeData      & operator* () ;
		NodeData const* operator->() const { return &**this ; }
		NodeData      * operator->()       { return &**this ; }
		bool            frozen    () const ;
		bool            manual_ok () const ;
		bool            no_trigger() const ;
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
		void stamp() const ;
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
	:	             Idxed<WatcherIdx>                                         // can index Node or Job (no need to distinguish as Job names are suffixed with rule)
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
	using JobTgtsBase = Persistent::CrunchVector<JobIdx,JobTgt> ;
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
		SeqId   seq_id      ;
		JobTgts frozen_jobs ;          // these jobs are not rebuilt
	} ;

	struct NodeHdr {
		Targets srcs         ;
		Targets src_dirs     ;
		Targets frozen_nodes ;         // these nodes can be freely overwritten by jobs if they have been manually modified (typically through a debug sessions)
		Targets manual_oks   ;         // these nodes can be freely overwritten by jobs if they have been manually modified (typically through a debug sessions)
		Targets no_triggers  ;         // these nodes do not trigger rebuild
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

	static constexpr char StartMrkr = 0x0 ;                                // used to indicate a single match suffix (i.e. a suffix which actually is an entire file name)

	// statics
	void new_config( Config&& , bool dynamic , bool rescue=false , ::function<void(Config const& old,Config const& new_)> diff=[](Config const&,Config const&)->void{} ) ;
	//
	bool/*invalidate*/ new_srcs        ( ::vector_s          && srcs , ::vector_s&& src_dirs_s ) ;
	bool/*invalidate*/ new_rules       ( ::umap<Crc,RuleData>&&                                ) ;
	void               invalidate_match(                                                       ) ;
	void               invalidate_exec ( bool cmd_ok                                           ) ;
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

	// visible data
	extern bool writable ;

	// on disk
	extern JobFile      _job_file          ;               // jobs
	extern DepsFile     _deps_file         ;               // .
	extern TargetsFile  _star_targets_file ;               // .
	extern NodeFile     _node_file         ;               // nodes
	extern JobTgtsFile  _job_tgts_file     ;               // .
	extern RuleStrFile  _rule_str_file     ;               // rules
	extern RuleFile     _rule_file         ;               // .
	extern RuleTgtsFile _rule_tgts_file    ;               // .
	extern SfxFile      _sfxs_file         ;               // .
	extern PfxFile      _pfxs_file         ;               // .
	extern NameFile     _name_file         ;               // commons
	// in memory
	extern ::uset<Job >       _frozen_jobs  ;
	extern ::uset<Node>       _frozen_nodes ;
	extern ::uset<Node>       _manual_oks   ;
	extern ::uset<Node>       _no_triggers  ;
	extern ::vector<RuleData> _rule_datas   ;

	template<class Idx_,class Item_> struct VectorHelper ;
	//
	#define SC static constexpr
	template<> struct VectorHelper<NodeIdx   ,Dep   > { SC DepsFile   & g_file() { return _deps_file         ; } SC DepsFile    const& gc_file() { return _deps_file         ; } } ;
	template<> struct VectorHelper<NodeIdx   ,Target> { SC TargetsFile& g_file() { return _star_targets_file ; } SC TargetsFile const& gc_file() { return _star_targets_file ; } } ;
	template<> struct VectorHelper<JobIdx    ,JobTgt> { SC JobTgtsFile& g_file() { return _job_tgts_file     ; } SC JobTgtsFile const& gc_file() { return _job_tgts_file     ; } } ;
	template<> struct VectorHelper<RuleStrIdx,char  > { SC RuleStrFile& g_file() { return _rule_str_file     ; } SC RuleStrFile const& gc_file() { return _rule_str_file     ; } } ;
	#undef SC

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
		if      (add ) for( Item j : items ) modified |= mem.insert(j).second ;
		else if (+mem) for( Item j : items ) modified |= mem.erase (j)        ; // fast path : no need to update mem if it is already empty
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
	inline bool          JobBase::s_has_frozens  (                                       ) { return               +_job_file.c_hdr().frozen_jobs  ;                      }
	inline ::vector<Job> JobBase::s_frozens      (                                       ) { return mk_vector<Job>(_job_file.c_hdr().frozen_jobs) ;                      }
	inline void          JobBase::s_frozens      ( bool add , ::vector<Job> const& items ) { _s_update( _job_file.hdr().frozen_jobs         , _frozen_jobs,add,items ) ; }
	inline void          JobBase::s_clear_frozens(                                       ) {            _job_file.hdr().frozen_jobs.clear() ; _frozen_jobs.clear()     ; }
	//
	inline Job JobBase::s_idx(JobData const& jd) { return _job_file.idx(jd) ; }
	// cxtors & casts
	template<class... A> inline JobBase::JobBase( NewType , A&&... args ) {    // 1st arg is only used to disambiguate
		*this = _job_file.emplace(::forward<A>(args)...) ;
	}
	template<class... A> inline JobBase::JobBase( ::pair_ss const& name_sfx , bool new_ , A&&... args ) {
		Name name_ = _name_file.insert(name_sfx.first,name_sfx.second) ;
		*this = _name_file.c_at(+name_).job() ;
		if (+*this) {
			SWEAR( name_==(*this)->_full_name , name_ , (*this)->_full_name ) ;
			if (!new_) return ;
			**this = JobData(::forward<A>(args)...) ;
		} else {
			_name_file.at(+name_) = *this = _job_file.emplace(::forward<A>(args)...) ;
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
	//

	//
	// NodeBase
	//
	// statics
	inline Node NodeBase::s_idx     (NodeData  const& nd  ) { return  _node_file.idx   (nd  ) ; }
	inline bool NodeBase::s_is_known( ::string const& name) { return +_name_file.search(name) ; }

	inline bool           NodeBase::s_has_frozens      (                                        ) { return                +_node_file.c_hdr().frozen_nodes  ;          }
	inline bool           NodeBase::s_has_manual_oks   (                                        ) { return                +_node_file.c_hdr().manual_oks    ;          }
	inline bool           NodeBase::s_has_no_triggers  (                                        ) { return                +_node_file.c_hdr().no_triggers   ;          }
	inline bool           NodeBase::s_has_srcs         (                                        ) { return                +_node_file.c_hdr().srcs          ;          }
	inline ::vector<Node> NodeBase::s_frozens          (                                        ) { return mk_vector<Node>(_node_file.c_hdr().frozen_nodes) ;          }
	inline ::vector<Node> NodeBase::s_manual_oks       (                                        ) { return mk_vector<Node>(_node_file.c_hdr().manual_oks  ) ;          }
	inline ::vector<Node> NodeBase::s_no_triggers      (                                        ) { return mk_vector<Node>(_node_file.c_hdr().no_triggers ) ;          }
	inline void           NodeBase::s_frozens          ( bool add , ::vector<Node> const& items ) { _s_update(_node_file.hdr().frozen_nodes,_frozen_nodes,add,items) ; }
	inline void           NodeBase::s_manual_oks       ( bool add , ::vector<Node> const& items ) { _s_update(_node_file.hdr().manual_oks  ,_manual_oks  ,add,items) ; }
	inline void           NodeBase::s_no_triggers      ( bool add , ::vector<Node> const& items ) { _s_update(_node_file.hdr().no_triggers ,_no_triggers ,add,items) ; }
	inline void           NodeBase::s_clear_frozens    (                                        ) { _node_file.hdr().frozen_nodes.clear() ; _frozen_nodes.clear() ;    }
	inline void           NodeBase::s_clear_manual_oks (                                        ) { _node_file.hdr().manual_oks  .clear() ; _manual_oks  .clear() ;    }
	inline void           NodeBase::s_clear_no_triggers(                                        ) { _node_file.hdr().no_triggers .clear() ; _no_triggers .clear() ;    }
	inline void           NodeBase::s_clear_srcs       (                                        ) { _node_file.hdr().srcs        .clear() ;                       ;    }
	//
	inline Targets const NodeBase::s_srcs( bool dirs                                          ) { NodeHdr const& nh = _node_file.c_hdr() ; return dirs ? nh.src_dirs : nh.srcs ;            }
	inline void          NodeBase::s_srcs( bool dirs , bool add , ::vector<Node> const& items ) { NodeHdr      & nh = _node_file.hdr  () ; _s_update(dirs?nh.src_dirs:nh.srcs ,add,items) ; }

	// cxtors & casts
	inline NodeBase::NodeBase( Name name_ , bool no_dir ) {
		if (!name_) return ;
		*this = _name_file.c_at(name_).node() ;
		if (+*this) {
			SWEAR( name_==(*this)->_full_name , name_ , (*this)->_full_name ) ;
		} else {
			_name_file.at(name_) = *this = _node_file.emplace(name_,no_dir) ;
			(*this)->_full_name = name_                                    ;
		}
	}
	inline NodeBase::NodeBase( ::string const& n , bool no_dir ) {
		*this = Node( _name_file.insert(n) , no_dir ) ;
	}
	// accesses
	inline bool NodeBase::frozen    () const { return _frozen_nodes.contains(Node(+*this)) ; }
	inline bool NodeBase::manual_ok () const { return _manual_oks  .contains(Node(+*this)) ; }
	inline bool NodeBase::no_trigger() const { return _no_triggers .contains(Node(+*this)) ; }
	//
	inline NodeData const& NodeBase::operator*() const { return _node_file.c_at(+*this) ; }
	inline NodeData      & NodeBase::operator*()       { return _node_file.at  (+*this) ; }

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
	inline void RuleBase::stamp() const { _rule_file.at(*this) = _rule_str_file.assign(_str(),::string(**this)) ; }

	//
	// SimpleVectorBase
	//
	template<class Idx,class Item,uint8_t NGuardBits> constexpr Idx SimpleVectorBase<Idx,Item,NGuardBits>::EmptyIdx = VectorHelper<Idx,Item>::gc_file().EmptyIdx ;
	// cxtors & casts
	template<class Idx,class Item,uint8_t NGuardBits> template<IsA<Item> I>
		inline SimpleVectorBase<Idx,Item,NGuardBits>::SimpleVectorBase(::vector_view<I> const& v) : Base(VectorHelper<Idx,Item>::g_file().emplace(v)) {}
	//
	template<class Idx,class Item,uint8_t NGuardBits> template<IsA<Item> I>
		inline void SimpleVectorBase<Idx,Item,NGuardBits>::assign(::vector_view<I> const& v) {
			*this = VectorHelper<Idx,Item>::g_file().assign(+*this,v) ;
		}
	//
	template<class Idx,class Item,uint8_t NGuardBits> inline void SimpleVectorBase<Idx,Item,NGuardBits>::pop  () { VectorHelper<Idx,Item>::g_file().pop(+*this) ; forget() ; }
	template<class Idx,class Item,uint8_t NGuardBits> inline void SimpleVectorBase<Idx,Item,NGuardBits>::clear() { pop() ;                                                   }
	// accesses
	template<class Idx,class Item,uint8_t NGuardBits> inline Item const* SimpleVectorBase<Idx,Item,NGuardBits>::items() const      { return VectorHelper<Idx,Item>::gc_file().items(+*this) ; }
	template<class Idx,class Item,uint8_t NGuardBits> inline Item      * SimpleVectorBase<Idx,Item,NGuardBits>::items()            { return VectorHelper<Idx,Item>::g_file ().items(+*this) ; }
	template<class Idx,class Item,uint8_t NGuardBits> inline auto        SimpleVectorBase<Idx,Item,NGuardBits>::size () const ->Sz { return VectorHelper<Idx,Item>::gc_file().size (+*this) ; }
	// services
	template<class Idx,class Item,uint8_t NGuardBits>
		inline void SimpleVectorBase<Idx,Item,NGuardBits>::shorten_by(Sz by) {
			*this = VectorHelper<Idx,Item>::g_file().shorten_by(+*this,by) ;
		}
	//
	template<class Idx,class Item,uint8_t NGuardBits> template<IsA<Item> I>
		inline void SimpleVectorBase<Idx,Item,NGuardBits>::append(::vector_view<I> const& v ) {
			*this = VectorHelper<Idx,Item>::g_file().append(+*this,v) ;
		}

	//
	// CrunchVectorBase
	//
	// cxtors & casts
	template<class Idx,class Item,uint8_t NGuardBits> template<IsA<Item> I>
		inline CrunchVectorBase<Idx,Item,NGuardBits>::CrunchVectorBase(::vector_view<I> const& v) {
			if (v.size()!=1) { static_cast<Base&>(*this) = VectorHelper<Idx,Item>::g_file().emplace(v) ; }
			else             { static_cast<Base&>(*this) = v[0]                                        ; }
		}
	//
	template<class Idx,class Item,uint8_t NGuardBits> template<IsA<Item> I>
		inline void CrunchVectorBase<Idx,Item,NGuardBits>::assign(::vector_view<I> const& v) {
			if (_multi()) {
				if (v.size()!=1) { *this = VectorHelper<Idx,Item>::g_file().assign(*this,v) ;                                  }
				else             {         VectorHelper<Idx,Item>::g_file().pop   (*this  ) ; *this = CrunchVectorBase(v[0]) ; }
			} else {
				*this = CrunchVectorBase(v) ;
			}
		}
	//
	template<class Idx,class Item,uint8_t NGuardBits>
		inline void CrunchVectorBase<Idx,Item,NGuardBits>::pop() {
			if (_multi()) VectorHelper<Idx,Item>::g_file().pop(*this) ;
			forget() ;
		}
	template<class Idx,class Item,uint8_t NGuardBits>
		inline void CrunchVectorBase<Idx,Item,NGuardBits>::clear() { pop() ; }
	// accesses
	template<class Idx,class Item,uint8_t NGuardBits>
		inline Item const* CrunchVectorBase<Idx,Item,NGuardBits>::items() const {
			if (_single()) return &static_cast<Item const&>(*this)              ;
			/**/           return VectorHelper<Idx,Item>::gc_file().items(*this) ;
		}
	template<class Idx,class Item,uint8_t NGuardBits>
		inline Item* CrunchVectorBase<Idx,Item,NGuardBits>::items() {
			if (_single()) return &static_cast<Item&>(*this)                    ;
			/**/           return VectorHelper<Idx,Item>::g_file().items(*this) ;
		}
	template<class Idx,class Item,uint8_t NGuardBits>
		inline auto CrunchVectorBase<Idx,Item,NGuardBits>::size() const -> Sz {
			if (_single()) return 1 ;
			/**/           return VectorHelper<Idx,Item>::gc_file().size(*this) ;
		}
	// services
	template<class Idx,class Item,uint8_t NGuardBits>
		inline void CrunchVectorBase<Idx,Item,NGuardBits>::shorten_by(Sz by) {
			Sz sz = size() ;
			SWEAR( by<=sz , by , sz ) ;
			if (_multi()) {
				if (by!=sz-1) { *this = VectorHelper<Idx,Item>::g_file().shorten_by( *this , by ) ;                           }
				else          { Item save = (*this)[0] ; VectorHelper<Idx,Item>::g_file().pop(Vector(*this)) ; *this = save ; }
			} else {
				if (by==sz) forget() ;
			}
		}
	//
	template<class Idx,class Item,uint8_t NGuardBits> template<IsA<Item> I>
		inline void CrunchVectorBase<Idx,Item,NGuardBits>::append(::vector_view<I> const& v ) {
			if      (!*this  ) assign(v) ;
			else if (_multi()) *this = VectorHelper<Idx,Item>::g_file().append (     *this ,v) ;
			else if (+v      ) *this = VectorHelper<Idx,Item>::g_file().emplace(Item(*this),v) ;
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
