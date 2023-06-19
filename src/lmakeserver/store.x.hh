// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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

//
// There are 10 files :
// - 1 name file associates a name with either a node or a job :
//   - This is a prefix-tree to share as much prefixes as possible since names tend to share a lot of prefixes
//   - For jobs, a suffix containing the rule and the positions of the stems is added.
//   - Before this suffix, a non printable char is inserted to distinguish nodes and jobs.
//   - A single file is used to store both nodes and jobs as they tend to share the same prefixes.
// - 3 files for nodes :
//   - A node idx file provides the name and the index in the node data file. The reason for this indirection is to avoid
//     allocating data for nodes that have not much to store, mostly nodes that are not makable.
//   - A node data file containing all pertinent info about a node.
//   - A job-star file containing vectors of job-star, a job-star is a job index and a marker saying if we refer to a static or a star target
// - 3 files for jobs :
//   - A job data file containing all the pertinent info for a job
//   - A targets file containing vectors of star targets (static targets can be identified from the rule).
//     A target is a node index and a marker saying if target has been updated, i.e. it was not unlinked before job execution.
//     This file is sorted so that searching a node inside a vector can be done efficiently.
//   - A deps file containing vectors of deps, ordered with static deps first, then critical deps then non-critical deps, in order in which they were opened.
// - 3 files for rules :
//   - A rule string file containing strings describing the rule.
//   - A rule index file containing indexes in the rule string file.
//     The reason for this indirection is to have a short (16 bits) index for rules while the index in the rule string file is 32 bits.
//   - A rule-targets file containing vectors of rule-target's. A rule-target is a rule index and a target index within the rule.
//   - This file is for use by nodes to represent candidates to generate them.
//     During the analysis process, rule-targets are transformed into job-target when possible (else they are dropped), so that the yet to analyse part which
//     the node keeps is a suffix of the original list.
//     For this reason, the file is stored as a suffix-tree (like a prefix-tree, but reversed).
//

#ifdef STRUCT_DECL
namespace Engine {
	extern SeqId* g_seq_id ;           // used to identify launched jobs. Persistent so that we keep as many old traces as possible
}
#endif

#ifdef STRUCT_DEF
namespace Engine {

	//
	// Idxed
	//

	template<class T> static constexpr uint8_t NGuardBits_ = Store::NGuardBits<T> ;
	template<class T> static constexpr uint8_t NValBits_   = Store::NValBits  <T> ;
	//
	template<class I,uint8_t NGuardBits_=0> struct Idxed {
		static constexpr bool IsIdxed = true ;
		using Idx = I ;
		static constexpr uint8_t NGuardBits = NGuardBits_             ;
		static constexpr uint8_t NValBits   = NBits<Idx> - NGuardBits ;
		// statics
	private :
		static constexpr void _s_chk(Idx idx) { swear_prod( !(idx&~lsb_msk(NValBits)) , "index overflow" ) ; }
		// cxtors & casts
	public :
		constexpr Idxed(       ) = default ;
		constexpr Idxed(Idx idx) : _idx(idx ) { _s_chk(idx) ; }                // ensure no index overflow
		//
		constexpr Idx  operator+() const { return  _idx&lsb_msk(NValBits) ; }
		constexpr bool operator!() const { return !+*this                 ; }
		//
		void clear() { *this = Idxed{} ; }
		// accesses
		constexpr bool              operator== (Idxed other) const { return +*this== +other ; }
		constexpr ::strong_ordering operator<=>(Idxed other) const { return +*this<=>+other ; }
		//
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) Idx  side(       ) const { return bits(_idx,W,NValBits+LSB)     ; }
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) void side(Idx val)       { _idx = bits(_idx,W,NValBits+LSB,val) ; }
		// data
	private :
		Idx _idx = 0 ;
	} ;
	template<class T> concept IsIdxed = T::IsIdxed && sizeof(T)==sizeof(typename T::Idx) ;
	template<Engine::IsIdxed I> ::ostream& operator<<( ::ostream& os , I const i ) { return os<<+i ; }

	template<IsIdxed T> struct Owned : T {
		using T::T ;
		//
		Owned           (Owned const&) = delete ;                                  // cannot duplicate ownership
		Owned& operator=(Owned const&) = delete ;                                  // .
		//
		Owned           (Owned && t) : T{::move(t)} {                                                 t.clear() ;                }
		Owned& operator=(T     && t)                { T::pop() ; static_cast<T&>(*this) = ::move(t) ;             return *this ; }
		Owned& operator=(Owned && t)                { T::pop() ; static_cast<T&>(*this) = ::move(t) ; t.clear() ; return *this ; }
		~Owned          (          )                { T::pop() ;                                                                 }
	} ;

}

namespace std {
	template<Engine::IsIdxed I> struct hash<I> { size_t operator()(I i) const { return +i ; } } ;
}

namespace Engine {

	//
	// TaggedUnion
	//

	template<IsIdxed A,IsIdxed B> requires(!::is_same_v<A,B>) struct TaggedUnion {
		static constexpr bool IsTaggedUnion = true ;

		using Idx  = Largest< typename A::Idx , typename B::Idx > ;
		using SIdx = ::make_signed_t<Idx> ;
		static constexpr uint8_t NValBits   = ::max(NValBits_<A>,NValBits_<B>)+1 ;
		static constexpr uint8_t NGuardBits = NValBits_<Idx>-NValBits            ;
		static_assert( NValBits_<Idx> >= NValBits ) ;

		template<class T> static constexpr bool IsA    = ::is_base_of_v<A,T> && ( ::is_base_of_v<B,A> || !::is_base_of_v<B,T> ) ; // ensure T does not derive independently from both A & B
		template<class T> static constexpr bool IsB    = ::is_base_of_v<B,T> && ( ::is_base_of_v<A,B> || !::is_base_of_v<A,T> ) ; // .
		template<class T> static constexpr bool IsAOrB = IsA<T> || IsB<T> ;

		// cxtors & casts
		constexpr TaggedUnion(   ) = default ;
		constexpr TaggedUnion(A a) : _val{SIdx( +a)} {}
		constexpr TaggedUnion(B b) : _val{SIdx(-+b)} {}
		//
		template<class T> requires(IsAOrB<T>) operator T() const {
			SWEAR(is_a<T>()) ;
			if (IsA<T>) return T(  _val  & lsb_msk(NValBits)) ;
			else        return T((-_val) & lsb_msk(NValBits)) ;
		}
		template<class T> requires( IsA<T> && sizeof(T)==sizeof(Idx) ) explicit operator T const&() const { SWEAR(is_a<T>()) ; return reinterpret_cast<T const&>(*this) ; }
		template<class T> requires( IsA<T> && sizeof(T)==sizeof(Idx) ) explicit operator T      &()       { SWEAR(is_a<T>()) ; return reinterpret_cast<T      &>(*this) ; }
		//
		void clear() { *this = TaggedUnion() ; }
		// accesses
		template<class T> requires(IsAOrB<T>) bool is_a() const {
			if (IsA<T>) return !bit( _val,NValBits-1) ;
			else        return !bit(-_val,NValBits-1) ;
		}
		//
		SIdx     operator+    () const { return _val<<NGuardBits>>NGuardBits ; }
		explicit operator bool() const { return _val<<NGuardBits != 0        ; }
		bool     operator!    () const { return !bool(*this)                 ; }
		//
		bool              operator== (TaggedUnion other) const { return +*this== +other ; }
		::strong_ordering operator<=>(TaggedUnion other) const { return +*this<=>+other ; }
		//
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) Idx  side(       ) const { return bits(_val,W,NValBits+LSB)     ; }
		template<uint8_t W,uint8_t LSB=0> requires( W>0 && W+LSB<=NGuardBits ) void side(Idx val)       { _val = bits(_val,W,NValBits+LSB,val) ; }
	private :
		// data
		SIdx _val = 0 ;
	} ;
	template<class T> concept IsTaggedUnion = T::IsTaggedUnion && sizeof(T)==sizeof(typename T::Idx) ;
	template<class A,class B> ::ostream& operator<<( ::ostream& os , TaggedUnion<A,B> const tu ) {
		if      (!tu                  ) os << '0'   ;
		else if (tu.template is_a<A>()) os << A(tu) ;
		else                            os << B(tu) ;
		return os ;
	}

}

// must be outside Engine namespace as it specializes std::hash
namespace std {
	template<Engine::IsTaggedUnion TU> struct hash<TU> { size_t operator()(TU tu) const { return +tu ; } } ;
}

namespace Engine {

	//
	// Vector's
	//

	template<class Idx_,class Item_,uint8_t NGuardBits=0> struct SimpleVectorBase
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
	template<class Idx_,class Item_,uint8_t NGuardBits=1> struct CrunchVectorBase
	:	               TaggedUnion< Item_ , Idxed<Idx_,NGuardBits> >
	{	using Base   = TaggedUnion< Item_ , Idxed<Idx_,NGuardBits> > ;
		using Vector =                      Idxed<Idx_,NGuardBits>   ;
		using Idx    =                            Idx_               ;
		using Item   =              Item_                            ;
		using Sz = Idx   ;
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
		bool _empty () const { return !*this                         ; }
		// services
	public :
		void shorten_by(Sz by) ;
		//
		template<IsA<Item> I> void append(::vector_view<I> const&) ;
	} ;

	template<class V> struct GenericVector : V {
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
		template<IsA<Item> I> requires( ::is_const_v<I>) GenericVector(::vector           <::remove_const_t<I>> const& v) : Base(        vector_view_c<I>(v)) {}
		template<IsA<Item> I> requires(!::is_const_v<I>) GenericVector(::vector           <                 I > const& v) : Base(        vector_view_c<I>(v)) {}
		template<IsA<Item> I> requires(IsStr           ) GenericVector(::basic_string_view<                 I > const& s) : Base(        vector_view_c<I>(s)) {}
		template<IsA<Item> I> requires(IsStr           ) GenericVector(::basic_string     <                 I > const& s) : Base(        vector_view_c<I>(s)) {}
		template<IsA<Item> I>                            void assign  (::vector_view      <                 I > const& v) { Base::assign(                 v ) ; }
		template<IsA<Item> I> requires( ::is_const_v<I>) void assign  (::vector           <::remove_const_t<I>> const& v) {       assign(vector_view_c<I>(v)) ; }
		template<IsA<Item> I> requires(!::is_const_v<I>) void assign  (::vector           <                 I > const& v) {       assign(vector_view_c<I>(v)) ; }
		template<IsA<Item> I> requires(IsStr           ) void assign  (::basic_string_view<                 I > const& s) {       assign(vector_view_c<I>(s)) ; }
		template<IsA<Item> I> requires(IsStr           ) void assign  (::basic_string     <                 I > const& s) {       assign(vector_view_c<I>(s)) ; }
		//
		operator ::vector_view_c    <Item>() const                 { return view    () ; }
		operator ::vector_view      <Item>()                       { return view    () ; }
		operator ::basic_string_view<Item>() const requires(IsStr) { return str_view() ; }
		// accesses
		bool                      empty   () const                 { return !size() ; }
		::vector_view_c    <Item> view    () const                 { return { items() , size() } ; }
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
		::vector_view_c    <Item> const subvec( size_t start , size_t sz=NPos ) const { return ::vector_view_c    ( begin()+start , ::min(sz,size()-start) ) ; }
		::vector_view      <Item>       subvec( size_t start , size_t sz=NPos )       { return ::vector_view      ( begin()+start , ::min(sz,size()-start) ) ; }
		::basic_string_view<Item> const substr( size_t start , size_t sz=NPos ) const { return ::basic_string_view( begin()+start , ::min(sz,size()-start) ) ; }
		::basic_string_view<Item>       substr( size_t start , size_t sz=NPos )       { return ::basic_string_view( begin()+start , ::min(sz,size()-start) ) ; }
		// services
		template<IsA<Item> I> void append(::vector_view      <I> const& v) { return Base::append(                v ) ; }
		template<IsA<Item> I> void append(::vector           <I> const& v) { return       append(::vector_view_c(v)) ; }
		template<IsA<Item> I> void append(::basic_string_view<I> const& s) { return       append(::vector_view_c(s)) ; }
		template<IsA<Item> I> void append(::basic_string     <I> const& s) { return       append(::vector_view_c(s)) ; }
	} ;

	template<class Idx_,class Item_> using SimpleVector = GenericVector<SimpleVectorBase<Idx_,Item_>> ;
	template<class Idx_,class Item_> using CrunchVector = GenericVector<CrunchVectorBase<Idx_,Item_>> ;

	struct RuleTgts
	:	             Idxed<RuleTgtsIdx>
	{	using Base = Idxed<RuleTgtsIdx> ;
		// cxtors & casts
		using Base::Base ;
		RuleTgts           (::vector_view_c<RuleTgt> const&  ) ;
		RuleTgts& operator=(::vector_view_c<RuleTgt> const& v) ;
		void pop() ;
		// accesses
		::vector<RuleTgt> view () const ;
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
	} ;

	using DepsBase = SimpleVector<NodeIdx,Dep> ;

	struct JobBase
	:	             Idxed<JobIdx,JobNGuardBits>
	{	using Base = Idxed<JobIdx,JobNGuardBits> ;
		// statics
		static ::vector<Job> s_frozens(                    ) ;
		static void          s_frozens(::vector<Job> const&) ;
		// cxtors & casts
		using Base::Base ;
		template<class... A> JobBase(                         NewType  , A&&...      ) ;
		template<class... A> JobBase( ::pair_ss const& name , NewType  , A&&... args ) : JobBase(name,true /*new*/,::forward<A>(args)...) {}
		template<class... A> JobBase( ::pair_ss const& name , DfltType , A&&... args ) : JobBase(name,false/*new*/,::forward<A>(args)...) {}
		/**/                 JobBase( ::pair_ss const& name                          ) : JobBase(name,Dflt                              ) {}
	private :
		template<class... A> JobBase( ::pair_ss const& name , bool new_  , A&&... ) ;
	public :
		void pop() ;
		// accesses
		JobData const& operator* () const ;
		JobData      & operator* () ;
		JobData const* operator->() const { return &**this ; }
		JobData      * operator->()       { return &**this ; }
		//
		RuleIdx  rule_idx    (                    ) const ;
		bool     has_name    (                    ) const ;
		::string full_name   (FileNameIdx sfx_sz=0) const ;
		size_t   full_name_sz(FileNameIdx sfx_sz=0) const ;
		//
		Name const& _name() const ;
		Name      & _name()       ;
	} ;

	using JobTgtsBase = CrunchVector<JobIdx,JobTgt> ;

	struct NodePtr
	:	             Idxed<NodeDataIdx>
	{	using Base = Idxed<NodeDataIdx> ;
		// cxtors & casts
		using Base::Base ;
		void pop() ;
		// accesses
		NodeData const& operator*() const ;
		NodeData      & operator*()       ;
		//
		bool shared() const ;
	} ;

	struct NodeBase
	:	             Idxed<NodeIdx,NodeNGuardBits>
	{	using Base = Idxed<NodeIdx,NodeNGuardBits> ;
		// statics
		static ::vector<Node> s_srcs   (                     ) ;
		static ::vector<Node> s_frozens(                     ) ;
		static void           s_srcs   (::vector<Node> const&) ;
		static void           s_frozens(::vector<Node> const&) ;
		//
		static RuleTgts s_rule_tgts(::string const& target_name) ;
		// cxtors & casts
		using Base::Base ;
		NodeBase(::string const& name) ;
	private :
		NodeBase(Name) ;                                                       // no lock as it is managed in public cxtor & dir method
		// accesses
	protected :
		NodeData const& _data() const ;
		NodeData      & _data() ;                                              // provide mutable access so UNode can use it to define operator*
	public :
		NodeData const& operator* () const { return _data() ; }                // provide only const access as data may be shared
		NodeData const* operator->() const { return &**this ; }
		bool            shared    () const ;
		::string        name      () const ;
		size_t          name_sz   () const ;
	private :
		NodePtr const& _ptr () const ;
		NodePtr      & _ptr () ;
		Name    const& _name() const ;
		Name         & _name() ;
		// services
	public :
		template<class... A> void mk_shared(A&&... args) ;
		void share () ;
		void unique() ;
		Node dir   () const ;
	} ;

	using RuleStr = SimpleVector<RuleStrIdx,char> ;

	struct RuleBase
	:	             Idxed<RuleIdx>
	{	using Base = Idxed<RuleIdx> ;
		// statics
		static ::vector<Rule> s_lst() ;
		// static data
		static MatchGen s_match_gen ;
		// cxtors & casts
		using Base::Base ;
		constexpr RuleBase(Special         s ) : Base{RuleIdx(+s)} { SWEAR( +s && s!=Special::Unknown ) ; } // Special::0 is a marker that says not special
		/**/      RuleBase(RuleData const& rd) ;
		void invalidate_old() ;
		// accesses
		RuleData      & rule_data ()       ;
		RuleData const& operator* () const ;
		RuleData const* operator->() const { return &**this ; }
		//
		constexpr bool    is_special() const { return +*this<=+Special::N                                             ; }
		/**/      Special special   () const { SWEAR(+*this) ; return is_special() ? Special(+*this) : Special::Plain ; }
		/**/      bool    old       () const ;
		//
		::string_view str() const ;
		// services
		void stamp() const ;
	private :
		RuleStr _str() const ;
	} ;

	struct SfxBase
	:	             Idxed<RuleIdx>
	{	using Base = Idxed<RuleIdx> ;
		// cxtors & casts
		using Base::Base ;
	} ;

	using TargetsBase = SimpleVector<NodeIdx,Target> ;

	struct JobNode
	:	             Idxed<JobNodeIdx>                                         // can index Node or Job (no need to distinguish as Job names are suffixed with rule)
	{	using Base = Idxed<JobNodeIdx> ;
		// cxtors & casts
		using Base::Base ;
		JobNode(JobBase ) ;
		JobNode(NodeBase) ;
		Job  job () const ;
		Node node() const ;
	} ;

}
#endif

#ifdef DATA_DEF
namespace Engine {
	extern Config& g_config                 ;
	extern ::string    * g_local_admin_dir  ; extern bool g_has_local_admin_dir ;
	extern ::string    * g_remote_admin_dir ; extern bool g_has_local_admin_dir ;
}
#endif

#ifdef IMPL
namespace Engine {

	struct JobHdr {
		SeqId   seq_id  ;
		JobTgts frozens ;
	} ;

	struct NodeHdr {
		Targets srcs    ;
		Targets frozens ;
	} ;

	//                                            autolock header     index             key       data       misc
	// jobs
	using JobFile      = Store::SideCarFile     < false ,  JobHdr   , Job             ,           JobData  , Name             > ;
	using DepsFile     = Store::VectorFile      < false ,  void     , Deps            ,           Dep                         > ;
	using TargetsFile  = Store::VectorFile      < false ,  void     , Targets         ,           Target                      > ;
	// nodes
	using NodeIdxFile  = Store::SideCarFile     < false ,  NodeHdr  , Node            ,           NodePtr  , Name             > ;
	using NodeDataFile = Store::AllocFile       < false ,  void     , NodePtr         ,           NodeData                    > ;
	using JobTgtsFile  = Store::VectorFile      < false ,  void     , JobTgts::Vector ,           JobTgt   , RuleIdx          > ;
	// rules
	using RuleStrFile  = Store::VectorFile      < false ,  void     , RuleStr         ,           char     , uint32_t         > ;
	using RuleFile     = Store::AllocFile       < false ,  MatchGen , Rule            ,           RuleStr                     > ;
	using RuleTgtsFile = Store::SinglePrefixFile< false ,  void     , RuleTgts        , RuleTgt , void     , true /*Reverse*/ > ;
	using SfxFile      = Store::SinglePrefixFile< false ,  void     , PsfxIdx         , char    , PsfxIdx  , true /*Reverse*/ > ; // map sfxes to root of pfxes, no lock : static
	using PfxFile      = Store::MultiPrefixFile < false ,  void     , PsfxIdx         , char    , RuleTgts , false/*Reverse*/ > ;
	// commons
	using NameFile     = Store::SinglePrefixFile< true  ,  void     , Name            , char    , JobNode                     > ; // for Job's & Node's

	struct EngineStore {
		static constexpr char StartMrkr = 0x0 ;                                // used to indicate a single match suffix (i.e. a suffix which actually is an entire file name)

		struct NewRulesSrcs ;

		// statics
	public :
		static void s_new_config   ( ::string const& local_admin_dir , ::string const& remote_admin_dir , Config&& , bool rescue=false ) ;
		static void s_new_makefiles( ::umap<Crc,RuleData>&& , ::vector_s&& srcs                                                        ) ;
		//
		static void s_keep_config     (bool rescue=false) ;
		static void s_keep_makefiles  (                 ) ;
		static void s_invalidate_match(                 ) ;
	private :
		static void                 _s_init_config      (                                                                              ) ;
		static Config/*old_config*/ _s_set_config       ( Config     && new_config                                                     ) ;
		static void                 _s_diff_config      ( Config const& old_config                                                     ) ;
		static void                 _s_init_srcs_rules  ( bool rescue=false                                                            ) ;
		static void                 _s_set_exec_gen     ( RuleData& , ::pair<bool,ExecGen>& keep_cmd_gen , bool cmd_ok , bool rsrcs_ok ) ;
		static void                 _s_collect_old_rules(                                                                              ) ;
		static void                 _s_invalidate_exec  ( ::vector<pair<bool,ExecGen>> const& keep_cmd_gens                            ) ;
		static bool/*invalidate*/   _s_new_srcs         ( ::vector<Node>&& srcs                                                        ) ;
		static void                 _s_new_rules        ( ::umap<Crc,RuleData>&& , bool force_invalidate                               ) ;
		// cxtors
	public :
		EngineStore(bool w) : writable{w} {}
		// accesses
		NodeIdxFile::Lst node_lst() const { return node_idx_file.lst() ; }
		JobFile    ::Lst job_lst () const { return job_file     .lst() ; }
		::vector<Rule>   rule_lst() const {
			::vector<Rule> res ; res.reserve(rule_file.size()) ;
			for( Rule r : rule_file.lst() ) if ( !r.is_special() && !r.old() ) res.push_back(r) ;
			return res ;
		}
		// services
	private :
		void _new_max_dep_depth (DepDepth) ;
		void _save_rules        (        ) ;
		void _compile_rule_datas(        ) ;
		void _compile_psfxs     (        ) ;
		void _compile_rules     (        ) ;
		//
		//
	public :
		void invalidate_exec(bool cmd_ok) ;
		void chk() const {
			// files
			job_file         .chk() ;  // jobs
			deps_file        .chk() ;  // .
			star_targets_file.chk() ;  // .
			node_idx_file    .chk() ;  // nodes
			node_data_file   .chk() ;  // .
			job_tgts_file    .chk() ;  // .
			rule_str_file    .chk() ;  // rules
			rule_file        .chk() ;  // .
			rule_tgts_file   .chk() ;  // .
			name_file        .chk() ;  // commons
			// memory
			sfxs.chk() ;
			for( PsfxIdx idx : sfxs.lst() ) pfxs.chk(sfxs.c_at(idx)) ;
		}
		// data
		bool writable = false ;
		// files
		JobFile      job_file          ;                   // jobs
		DepsFile     deps_file         ;                   // .
		TargetsFile  star_targets_file ;                   // .
		NodeIdxFile  node_idx_file     ;                   // nodes
		NodeDataFile node_data_file    ;                   // .
		JobTgtsFile  job_tgts_file     ;                   // .
		RuleStrFile  rule_str_file     ;                   // rules
		RuleFile     rule_file         ;                   // .
		RuleTgtsFile rule_tgts_file    ;                   // .
		NameFile     name_file         ;                   // commons
		// memory
		::vector<RuleData> rule_datas  ;
		SfxFile            sfxs        ;
		PfxFile            pfxs        ;
		Config             config      ;
	} ;

	extern EngineStore   g_store  ;

	template<class Idx_,class Item_> struct VectorHelper ;
	//
	template<> struct VectorHelper<NodeIdx   ,Dep   > { static constexpr DepsFile   & g_file() { return g_store.deps_file         ; } } ;
	template<> struct VectorHelper<NodeIdx   ,Target> { static constexpr TargetsFile& g_file() { return g_store.star_targets_file ; } } ;
	template<> struct VectorHelper<JobIdx    ,JobTgt> { static constexpr JobTgtsFile& g_file() { return g_store.job_tgts_file     ; } } ;
	template<> struct VectorHelper<RuleStrIdx,char  > { static constexpr RuleStrFile& g_file() { return g_store.rule_str_file     ; } } ;

}
#endif
#ifdef IMPL
namespace Engine {

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
	inline void Name::pop() { g_store.name_file.pop(+*this) ; }
	// accesses
	inline ::string Name::str   (size_t sfx_sz) const { return g_store.name_file.str_key(+*this,sfx_sz) ; }
	inline size_t   Name::str_sz(size_t sfx_sz) const { return g_store.name_file.key_sz (+*this,sfx_sz) ; }

	//
	// SimpleVectorBase
	//
	template<class Idx,class Item,uint8_t NGuardBits> constexpr Idx SimpleVectorBase<Idx,Item,NGuardBits>::EmptyIdx = VectorHelper<Idx,Item>::g_file().EmptyIdx ;
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
	template<class Idx,class Item,uint8_t NGuardBits> inline Item const* SimpleVectorBase<Idx,Item,NGuardBits>::items() const      { return VectorHelper<Idx,Item>::g_file().items(+*this) ; }
	template<class Idx,class Item,uint8_t NGuardBits> inline Item      * SimpleVectorBase<Idx,Item,NGuardBits>::items()            { return VectorHelper<Idx,Item>::g_file().items(+*this) ; }
	template<class Idx,class Item,uint8_t NGuardBits> inline auto        SimpleVectorBase<Idx,Item,NGuardBits>::size () const ->Sz { return VectorHelper<Idx,Item>::g_file().size (+*this) ; }
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
			/**/           return VectorHelper<Idx,Item>::g_file().items(*this) ;
		}
	template<class Idx,class Item,uint8_t NGuardBits>
		inline Item* CrunchVectorBase<Idx,Item,NGuardBits>::items() {
			if (_single()) return &static_cast<Item&>(*this)                    ;
			/**/           return VectorHelper<Idx,Item>::g_file().items(*this) ;
		}
	template<class Idx,class Item,uint8_t NGuardBits>
		inline auto CrunchVectorBase<Idx,Item,NGuardBits>::size() const -> Sz {
			if (_single()) return 1 ;
			/**/           return VectorHelper<Idx,Item>::g_file().size(*this) ;
		}
	// services
	template<class Idx,class Item,uint8_t NGuardBits>
		inline void CrunchVectorBase<Idx,Item,NGuardBits>::shorten_by(Sz by) {
			Sz sz = size() ;
			SWEAR(by<=sz) ;
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
			if      (_empty()  ) { assign(v) ;                                                     ; }
			else if (_multi()  ) { *this = VectorHelper<Idx,Item>::g_file().append (     *this ,v) ; }
			else if (!v.empty()) { *this = VectorHelper<Idx,Item>::g_file().emplace(Item(*this),v) ; }
		}

	//
	// RuleTgts
	//
	// cxtors & casts
	inline RuleTgts::RuleTgts(::vector_view_c<RuleTgt> const& gs) : Base{g_store.rule_tgts_file.insert(gs)} {}
	inline void RuleTgts::pop() { g_store.rule_tgts_file.pop(+*this) ; }
	//
	inline RuleTgts& RuleTgts::operator=(::vector_view_c<RuleTgt> const& v) { *this = RuleTgts(v) ; return *this ; }
	// accesses
	inline ::vector<RuleTgt> RuleTgts::view() const { return g_store.rule_tgts_file.key(+*this) ; }
	// services
	inline void RuleTgts::shorten_by(RuleIdx by) {
		if (by==RuleIdx(-1)) clear() ;
		else                 *this = g_store.rule_tgts_file.insert_shorten_by( +*this , by ) ;
	}
	inline void RuleTgts::invalidate_old() {
		for( RuleTgt rt : view() ) {
			if (rt.old()) { pop() ; return ; }
		}
	}

	//
	// JobBase
	//
	// statics
	inline ::vector<Job> JobBase::s_frozens(                            ) { return mk_vector<Job>(g_store.job_file.c_hdr().frozens) ;           }
	inline void          JobBase::s_frozens(::vector<Job> const& frozens) { g_store.job_file.hdr().frozens.assign(mk_vector<JobTgt>(frozens)) ; }
	// cxtors & casts
	template<class... A> inline JobBase::JobBase( NewType , A&&... args ) {    // 1st arg is only used to disambiguate
		*this = g_store.job_file.emplace(::forward<A>(args)...) ;
	}
	template<class... A> inline JobBase::JobBase( ::pair_ss const& name , bool new_ , A&&... args ) {
		Name name_ = g_store.name_file.insert(name.first,name.second) ;
		*this      = g_store.name_file.c_at(+name_).job() ;
		if (+*this) {
			SWEAR(name_==_name()) ;
			if (new_) **this = JobData(::forward<A>(args)...) ;
		} else {
			*this = g_store.job_file.emplace(::forward<A>(args)...) ;
			g_store.name_file.at(+name_) = *this ;
			_name() = name_ ;
			::string fn = full_name() ;
			size_t pos = fn.find(char(0)) ;
			OStringStream sfx ;
			for( size_t i=pos+1 ; i<fn.size() ; i++ ) sfx << '.' << int(fn[i]) ;
			Trace("new_job",+*this,string_view(fn).substr(0,pos),sfx.str()) ;
		}
	}
	inline void JobBase::pop() {
		if (!*this) return ;
		if (+_name()) _name().pop() ;
		g_store.job_file.pop(+*this) ;
		clear() ;
	}
	// accesses
	inline JobData const& JobBase::operator*() const { return g_store.job_file.c_at(+*this) ; }
	inline JobData      & JobBase::operator*()       { return g_store.job_file.at  (+*this) ; }
	//
	inline bool     JobBase::has_name    (                  ) const { return +_name()                ; }
	inline ::string JobBase::full_name   (FileNameIdx sfx_sz) const { return  _name().str   (sfx_sz) ; }
	inline size_t   JobBase::full_name_sz(FileNameIdx sfx_sz) const { return  _name().str_sz(sfx_sz) ; }
	//
	inline Name const& JobBase::_name() const { return g_store.job_file.c_side_car(+*this) ; }
	inline Name      & JobBase::_name()       { return g_store.job_file.side_car  (+*this) ; }

	//
	// NodePtr
	//
	// cxtors & casts
	inline void NodePtr::pop() { g_store.node_data_file.pop(+*this) ; }
	// accesses
	inline NodeData const& NodePtr::operator*() const {                    return g_store.node_data_file.c_at(+*this) ; }
	inline NodeData      & NodePtr::operator*()       { SWEAR(!shared()) ; return g_store.node_data_file.at  (+*this) ; }
	//
	inline bool NodePtr::shared() const { return +*this<=NodeData::NShared ; }

	//
	// NodeBase
	//
	// statics
	inline ::vector<Node> NodeBase::s_srcs   (                             ) { return mk_vector<Node>(g_store.node_idx_file.c_hdr().srcs   ) ;          }
	inline ::vector<Node> NodeBase::s_frozens(                             ) { return mk_vector<Node>(g_store.node_idx_file.c_hdr().frozens) ;          }
	inline void           NodeBase::s_srcs   (::vector<Node> const& srcs   ) { g_store.node_idx_file.hdr().srcs   .assign(mk_vector<Target>(srcs   )) ; }
	inline void           NodeBase::s_frozens(::vector<Node> const& frozens) { g_store.node_idx_file.hdr().frozens.assign(mk_vector<Target>(frozens)) ; }
	//
	inline RuleTgts NodeBase::s_rule_tgts(::string const& target_name) {
		// first match on suffix
		PsfxIdx sfx_idx = g_store.sfxs.longest(target_name,::string{EngineStore::StartMrkr}).first ; // StartMrkr is to match rules w/ no stems
		if (!sfx_idx) return RuleTgts{} ;
		PsfxIdx pfx_root = g_store.sfxs.c_at(sfx_idx) ;
		// then match on prefix
		PsfxIdx pfx_idx = g_store.pfxs.longest(pfx_root,target_name).first ;
		if (!pfx_idx) return RuleTgts{} ;
		return g_store.pfxs.c_at(pfx_idx) ;

	}
	// cxtors & casts
	inline NodeBase::NodeBase(Name name_) {
		*this = g_store.name_file.c_at(name_).node() ;
		if (+*this) {
			SWEAR(name_==_name()) ;
		} else {
			*this = g_store.node_idx_file.emplace(NodeData::s_shared_idx()) ;  // points to default NodeData
			g_store.name_file.at(name_) = *this ;
			_name() = name_ ;
			Trace("new_node",+*this,name()) ;
		}
	}
	inline NodeBase::NodeBase(::string const& n) {
		*this = Node(g_store.name_file.insert(n)) ;
	}
	// accesses
	inline NodeData const& NodeBase::_data () const { return *_ptr() ; }
	inline NodeData      & NodeBase::_data ()       { return *_ptr() ; }
	//
	inline bool     NodeBase::shared () const { return _ptr().shared()  ; }
	inline ::string NodeBase::name   () const { return _name().str   () ; }
	inline size_t   NodeBase::name_sz() const { return _name().str_sz() ; }
	//
	inline NodePtr const& NodeBase::_ptr () const { return g_store.node_idx_file.c_at      (+*this) ; }
	inline NodePtr      & NodeBase::_ptr ()       { return g_store.node_idx_file.at        (+*this) ; }
	inline Name    const& NodeBase::_name() const { return g_store.node_idx_file.c_side_car(+*this) ; }
	inline Name         & NodeBase::_name()       { return g_store.node_idx_file.side_car  (+*this) ; }
	// services
	template<class... A> inline void NodeBase::mk_shared(A&&... args) {
		if (!shared()) _ptr().pop() ;
		_ptr() = NodeData::s_shared_idx(::forward<A>(args)...) ;
		SWEAR(shared()) ;
	}
	inline void NodeBase::share() {
		if ( shared() || !(*this)->sharable() ) return ;
		NodePtr p = _ptr() ;
		_ptr() = (*this)->shared_idx() ;
		fence() ;                                                              // ensure NodeData is destroyed only once there is no pointer to it in case of crash
		p.pop() ;
		Trace("share_node",+*this) ;
	}
	inline void NodeBase::unique() {
		if (!shared()) return ;
		_ptr() = g_store.node_data_file.emplace(+_ptr()) ;
		Trace("unique_node",+*this,_ptr()) ;
	}
	inline Node NodeBase::dir() const {
		Name name_ = g_store.name_file.insert_dir(_name(),'/') ;
		if (+name_) return Node(name_) ;
		else        return Node(     ) ;
	}

	//
	// RuleBase
	//
	//statics
	inline ::vector<Rule>  RuleBase::s_lst         ()       {                        return g_store.rule_lst()                      ;               }
	// cxtors & casts
	inline void            RuleBase::invalidate_old()       { if (old()) g_store.rule_file.pop(Rule(*this))                         ;               }
	// accesses
	inline RuleData      & RuleBase::rule_data     ()       {                        return g_store.rule_datas[+*this]              ;               }
	inline RuleData const& RuleBase::operator*     () const {                        return g_store.rule_datas[+*this]              ;               }
	inline bool            RuleBase::old           () const {                        return !is_special() && !_str()                ;               }
	inline ::string_view   RuleBase::str           () const {                        return g_store.rule_str_file.str_view(+_str()) ;               }
	inline RuleStr         RuleBase::_str          () const { SWEAR(!is_special()) ; return g_store.rule_file.c_at(Rule(*this))     ;               }
	// services
	inline void            RuleBase::stamp         () const { g_store.rule_file.at(*this) = g_store.rule_str_file.assign(_str(),::string(**this)) ; }

}
#endif
