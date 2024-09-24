// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included in 5 passes

#include "re.hh"

#include "rpc_job.hh"

#include "autodep/ld_server.hh"
#include "autodep/record.hh"

#include "store/prefix.hh"
#include "store/struct.hh"
#include "store/vector.hh"

#ifdef STRUCT_DECL

// START_OF_VERSIONING
ENUM( VarCmd
,	Stems   , Stem
,	Targets , Match , StarMatch
,	Deps    , Dep
,	Rsrcs   , Rsrc
)
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM_1( EnvFlag
,	Dflt = Rsrc
//
,	None // ignore variable
,	Rsrc // consider variable as a resource : upon modification, rebuild job if it was in error
,	Cmd  // consider variable as a cmd      : upon modification, rebuild job
)
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM_2( Special
,	Shared  = Infinite // <=Shared means there is a single such rule
,	HasJobs = Plain    // <=HasJobs means jobs can refer to this rule
//
,	None               // value 0 reserved to mean not initialized, so shared rules have an idx equal to special
,	Req
,	Infinite
,	Plain
// ordered by decreasing matching priority within each prio
,	Anti
,	GenericSrc
)
// END_OF_VERSIONING

namespace Engine {

	struct Rule     ;
	struct RuleData ;

	struct RuleTgt ;

}

#endif
#ifdef STRUCT_DEF

namespace Engine {

	struct CmdIdx {
		// services
		::strong_ordering operator<=>(CmdIdx const&) const = default ;
		// data
		VarCmd bucket ;
		VarIdx idx    ;
	} ;

	struct Rule : RuleBase {
		friend ::ostream& operator<<( ::ostream& os , Rule const r ) ;
		static constexpr char   JobMrkr  =  0 ;                        // ensure no ambiguity between job names and node names
		static constexpr char   StarMrkr =  0 ;                        // signal a star stem in job_name
		static constexpr char   StemMrkr =  0 ;                        // signal a stem in job_name & targets & deps & cmd
		static constexpr VarIdx NoVar    = -1 ;
		//
		struct SimpleMatch ;
		// cxtors & casts
		using RuleBase::RuleBase ;
		Rule(RuleBase const& rb     ) : RuleBase{ rb                                                      } {                                                     }
		Rule(::string const& job_sfx) : Rule    { decode_int<Idx>( &job_sfx[job_sfx.size()-sizeof(Idx)] ) } { SWEAR(job_sfx.size()>=sizeof(Idx),job_sfx.size()) ; }
		// acesses
		::string job_sfx() const ;
	} ;

}

#endif
#ifdef DATA_DEF

namespace Engine {

	namespace Attrs {
		// statics
		/**/                   bool/*updated*/ acquire( bool               & dst , Py::Object const* py_src                                                                               ) ;
		/**/                   bool/*updated*/ acquire( Delay              & dst , Py::Object const* py_src , Delay min=Delay::Lowest              , Delay max=Delay::Highest             ) ;
		/**/                   bool/*updated*/ acquire( JobSpace::ViewDescr& dst , Py::Object const* py_src                                                                               ) ;
		template<::integral I> bool/*updated*/ acquire( I                  & dst , Py::Object const* py_src , I     min=::numeric_limits<I>::min() , I     max=::numeric_limits<I>::max() ) ;
		template<StdEnum    E> bool/*updated*/ acquire( E                  & dst , Py::Object const* py_src                                                                               ) ;
		//
		template<        bool Env=false>                                       bool/*updated*/ acquire( ::string   & dst , Py::Object const* py_src ) ;
		template<class T,bool Env=false> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vector<T>& dst , Py::Object const* py_src ) ;
		template<class T,bool Env=false> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vmap_s<T>& dst , Py::Object const* py_src ) ;
		//
		template<class T,bool Env=false> requires(!Env||IsOneOf<T,::string,::vector_s,::vmap_ss>) bool/*update*/ acquire_from_dct( T& dst , Py::Dict const& py_dct , ::string const& key ) {
			try {
				if      constexpr (IsOneOf<T,::string            >) {                       if (py_dct.contains(key)) return acquire<         Env>( dst , &py_dct[key] ) ; else return false ; }
				else if constexpr (IsOneOf<T,::vector_s,::vmap_ss>) {                       if (py_dct.contains(key)) return acquire<::string,Env>( dst , &py_dct[key] ) ; else return false ; }
				else                                                { static_assert(!Env) ; if (py_dct.contains(key)) return acquire              ( dst , &py_dct[key] ) ; else return false ; }
			} catch (::string const& e) {
				throw "while processing "+key+" : "+e ;
			}
		}
		template<class T> bool/*update*/ acquire_from_dct( T& dst , Py::Dict const& py_dct , ::string const& key , T min ) {
				if (py_dct.contains(key)) return acquire( dst , &py_dct[key] , min ) ;
				else                      return false                               ;
		}
		template<class T> bool/*update*/ acquire_from_dct( T& dst , Py::Dict const& py_dct , ::string const& key , T min , T max ) {
				if (py_dct.contains(key)) return acquire( dst , &py_dct[key] , min , max ) ;
				else                      return false                                    ;
		}
		inline void acquire_env( ::vmap_ss& dst , Py::Dict const& py_dct , ::string const& key ) { acquire_from_dct<::vmap_ss,true/*Env*/>(dst,py_dct,key) ; }
		//
		::string subst_fstr( ::string const& fstr , ::umap_s<CmdIdx> const& var_idxs , VarIdx& n_unnamed ) ;
	} ;

	struct DepSpec {
		::string    txt          ;
		Dflags      dflags       ;
		ExtraDflags extra_dflags ;
	} ;

	// used at match time
	struct DepsAttrs {
		static constexpr const char* Msg = "deps" ;
		// services
		void init           ( bool is_dynamic , Py::Dict const* , ::umap_s<CmdIdx> const& , RuleData const& ) ;
		void add_interpreter(                                                               RuleData const& ) ;
		// data
		// START_OF_VERSIONING
		bool              full_dynamic = true ; // if true <=> deps is empty and new keys can be added, else dynamic deps must be within dep keys ...
		::vmap_s<DepSpec> deps         ;        // ... if full_dynamic, we are not initialized, so be ready by default
		// END_OF_VERSIONING
	} ;

	// used at match time, but participate in nothing
	struct CreateNoneAttrs {
		static constexpr const char* Msg = "create ancillary attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , Py::Dict const* py_src , ::umap_s<CmdIdx> const& ) { update(*py_src) ; }
		void update(                       Py::Dict const& py_dct                           ) {
			size_t tokens = 0/*garbage*/ ;
			Attrs::acquire_from_dct( tokens , py_dct , "job_tokens" ) ;
			if      (tokens==0                              ) tokens1 = 0                                ;
			else if (tokens>::numeric_limits<Tokens1>::max()) tokens1 = ::numeric_limits<Tokens1>::max() ;
			else                                              tokens1 = tokens-1                         ;
		}
		// data
		// START_OF_VERSIONING
		Tokens1 tokens1 = 0 ;
		// END_OF_VERSIONING
	} ;

	// used at submit time, participate in resources
	struct SubmitRsrcsAttrs {
		static constexpr const char* Msg = "submit resources attributes" ;
		static void s_canon(::vmap_ss& rsrcs) ;                               // round and cannonicalize standard resources
		// services
		void init  ( bool /*is_dynamic*/ , Py::Dict const* py_src , ::umap_s<CmdIdx> const& ) { update(*py_src) ; }
		void update(                       Py::Dict const& py_dct                           ) {
			Attrs::acquire_from_dct( backend , py_dct , "backend" ) ;
			if ( Attrs::acquire_from_dct( rsrcs , py_dct , "rsrcs" ) ) {
				::sort(rsrcs) ;                                               // stabilize rsrcs crc
				s_canon(rsrcs) ;
			}
		}
		uint32_t tokens() const {
			for(auto const& [k,v] : rsrcs) if (k=="cpu")
				try                     { return from_string<uint32_t>(v) ; }
				catch (::string const&) { break ;                           } // no valid cpu count, do as if no cpu found
			return 1 ;                                                        // not found : default to 1 cpu
		}
		// data
		// START_OF_VERSIONING
		BackendTag backend = BackendTag::Local ;                              // backend to use to launch jobs
		::vmap_ss  rsrcs   ;
		// END_OF_VERSIONING
	} ;

	// used at submit time, participate in resources
	struct SubmitNoneAttrs {
		static constexpr const char* Msg = "submit ancillary attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , Py::Dict const* py_src , ::umap_s<CmdIdx> const& ) { update(*py_src) ; }
		void update(                       Py::Dict const& py_dct                           ) {
			Attrs::acquire_from_dct( n_retries , py_dct , "n_retries" ) ;
		}
		// data
		// START_OF_VERSIONING
		uint8_t n_retries = 0 ;
		// END_OF_VERSIONING
	} ;

	// used both at submit time (for cache look up) and at end of execution (for cache upload)
	struct CacheNoneAttrs {
		static constexpr const char* Msg = "cache key" ;
		// services
		void init  ( bool /*is_dynamic*/ , Py::Dict const* py_src , ::umap_s<CmdIdx> const& ) { update(*py_src) ; }
		void update(                       Py::Dict const& py_dct                           ) {
			Attrs::acquire_from_dct( key , py_dct , "key" ) ;
			if ( +key && !Cache::s_tab.contains(key) ) throw "unexpected cache key "+key+" not found in config" ;
		}
		// data
		// START_OF_VERSIONING
		::string key ;
		// END_OF_VERSIONING
	} ;

	// used at start time, participate in cmd
	struct StartCmdAttrs {
		static constexpr const char* Msg = "execution command attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , Py::Dict const* py_src , ::umap_s<CmdIdx> const& ) { update(*py_src) ; }
		void update(                       Py::Dict const& py_dct                           ) {
			using namespace Attrs ;
			Attrs::acquire_from_dct( interpreter            , py_dct , "interpreter" ) ;
			Attrs::acquire_from_dct( auto_mkdir             , py_dct , "auto_mkdir"  ) ;
			Attrs::acquire_from_dct( job_space.chroot_dir_s , py_dct , "chroot_dir"  ) ; if (+job_space.chroot_dir_s) job_space.chroot_dir_s = Disk::with_slash(job_space.chroot_dir_s) ;
			Attrs::acquire_env     ( env                    , py_dct , "env"         ) ;
			Attrs::acquire_from_dct( ignore_stat            , py_dct , "ignore_stat" ) ;
			Attrs::acquire_from_dct( job_space.root_view_s  , py_dct , "root_view"   ) ; if (+job_space.root_view_s ) job_space.root_view_s  = Disk::with_slash(job_space.root_view_s ) ;
			Attrs::acquire_from_dct( job_space.tmp_view_s   , py_dct , "tmp_view"    ) ; if (+job_space.tmp_view_s  ) job_space.tmp_view_s   = Disk::with_slash(job_space.tmp_view_s  ) ;
			Attrs::acquire_from_dct( use_script             , py_dct , "use_script"  ) ;
			Attrs::acquire_from_dct( job_space.views        , py_dct , "views"       ) ;
			::sort( env                                                                                                                                   ) ; // stabilize cmd crc
			::sort( job_space.views , [](::pair_s<JobSpace::ViewDescr> const& a,::pair_s<JobSpace::ViewDescr> const&b)->bool { return a.first<b.first ; } ) ; // .
		}
		// data
		// START_OF_VERSIONING
		::vector_s interpreter ;
		bool       auto_mkdir  = false ;
		bool       ignore_stat = false ;
		::vmap_ss  env         ;
		JobSpace   job_space   ;
		bool       use_script  = false ;
		// END_OF_VERSIONING
	} ;

	struct DbgEntry {
		friend ::ostream& operator<<( ::ostream& , DbgEntry const& ) ;
		bool operator +() const { return first_line_no1 ; }
		bool operator !() const { return !+*this ;        }
		// START_OF_VERSIONING
		::string module         ;
		::string qual_name      ;
		::string filename       ;
		size_t   first_line_no1 = 0 ; // illegal value as lines start at 1
		// END_OF_VERSIONING
	} ;

	struct Cmd {
		static constexpr const char* Msg = "execution command" ;
		// services
		void init  ( bool /*is_dynamic*/ , Py::Dict const* , ::umap_s<CmdIdx> const& , RuleData const& ) ;
		void update(                       Py::Dict const& py_dct                                      ) {
			Attrs::acquire_from_dct( cmd , py_dct , "cmd" ) ;
		}
		// data
		// START_OF_VERSIONING
		::string cmd ;
		// END_OF_VERSIONING
	} ;
	namespace Attrs {
		bool/*updated*/ acquire( DbgEntry& dst , Py::Object const* py_src ) ;
	}

	// used at start time, participate in resources
	struct StartRsrcsAttrs {
		static constexpr const char* Msg = "execution resources attributes" ;
		void init  ( bool /*is_dynamic*/ , Py::Dict const* py_src , ::umap_s<CmdIdx> const& ) { update(*py_src) ; }
		void update(                       Py::Dict const& py_dct                           ) {
			Attrs::acquire_env     ( env     , py_dct , "env"                            ) ;
			Attrs::acquire_from_dct( method  , py_dct , "autodep"                        ) ;
			Attrs::acquire_from_dct( timeout , py_dct , "timeout" , Time::Delay()/*min*/ ) ;
			::sort(env) ;                                                                                                    // stabilize rsrcs crc
			// check
			if ( method==AutodepMethod::Fuse    && !HAS_FUSE     ) throw snake(method)+" is not supported on this system"s ; // PER_AUTODEP_METHOD
			if ( method==AutodepMethod::LdAudit && !HAS_LD_AUDIT ) throw snake(method)+" is not supported on this system"s ; // .
		}
		// data
		// START_OF_VERSIONING
		::vmap_ss     env     ;
		AutodepMethod method  = {} ;
		Time::Delay   timeout ;                                                                                              // if 0 <=> no timeout, maximum time allocated to job execution in s
		// END_OF_VERSIONING
	} ;

	// used at start time, participate to nothing
	struct StartNoneAttrs {
		static constexpr const char* Msg = "execution ancillary attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , Py::Dict const* py_src , ::umap_s<CmdIdx> const& ) { update(*py_src) ; }
		void update(                       Py::Dict const& py_dct                           ) {
			using namespace Attrs ;
			Attrs::acquire_from_dct( keep_tmp    , py_dct , "keep_tmp"                           ) ;
			Attrs::acquire_from_dct( start_delay , py_dct , "start_delay" , Time::Delay()/*min*/ ) ;
			Attrs::acquire_from_dct( kill_sigs   , py_dct , "kill_sigs"                          ) ;
			Attrs::acquire_from_dct( n_retries   , py_dct , "n_retries"                          ) ;
			Attrs::acquire_env     ( env         , py_dct , "env"                                ) ;
			::sort(env) ;                                                                            // by symmetry with env entries in StartCmdAttrs and StartRsrcsAttrs
		}
		// data
		// START_OF_VERSIONING
		bool              keep_tmp    = false ;
		Time::Delay       start_delay ;                                                              // job duration above which a start message is generated
		::vector<uint8_t> kill_sigs   ;                                                              // signals to use to kill job (tried in sequence, 1s apart from each other)
		uint8_t           n_retries   = 0     ;                                                      // max number of retry if job is lost
		::vmap_ss         env         ;
		// END_OF_VERSIONING
	} ;

	// used at end of job execution, participate in cmd
	struct EndCmdAttrs {
		static constexpr const char* Msg = "end command attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , Py::Dict const* py_src , ::umap_s<CmdIdx> const& ) { update(*py_src) ; }
		void update(                       Py::Dict const& py_dct                           ) {
			Attrs::acquire_from_dct( allow_stderr , py_dct , "allow_stderr" ) ;
		}
		// data
		// START_OF_VERSIONING
		bool allow_stderr = false ; // if true <=> non empty stderr does not imply job error
		// END_OF_VERSIONING
	} ;

	// used at end of job execution, participate in nothing
	struct EndNoneAttrs {
		static constexpr const char* Msg = "end ancillary attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , Py::Dict const* py_src , ::umap_s<CmdIdx> const& ) { update(*py_src) ; }
		void update(                       Py::Dict const& py_dct                           ) {
			Attrs::acquire_from_dct( max_stderr_len , py_dct , "max_stderr_len" , size_t(1) ) ;
		}
		// data
		// START_OF_VERSIONING
		size_t max_stderr_len = -1 ; // max lines when displaying stderr (full content is shown with lshow -e)
		// END_OF_VERSIONING
	} ;

	using EvalCtxFuncStr = ::function<void( VarCmd , VarIdx idx , string const& key , string  const& val )> ;
	using EvalCtxFuncDct = ::function<void( VarCmd , VarIdx idx , string const& key , vmap_ss const& val )> ;

	// the part of the Dynamic struct which is stored on disk
	struct DynamicDskBase {
		// statics
		static bool s_is_dynamic(Py::Tuple const& ) ;
	protected :
		static void _s_eval( Job , Rule::SimpleMatch&/*lazy*/ , ::vmap_ss const& rsrcs_ , ::vector<CmdIdx> const& ctx , EvalCtxFuncStr const& , EvalCtxFuncDct const& ) ;
		// cxtors & casts
		DynamicDskBase() = default ;
		DynamicDskBase( Py::Tuple const& , ::umap_s<CmdIdx> const& var_idxs ) ;
		// services
		// START_OF_VERSIONING
		template<IsStream S> void serdes(S& s) {
			::serdes(s,is_dynamic        ) ;
			::serdes(s,glbs_str          ) ;
			::serdes(s,code_str          ) ;
			::serdes(s,ctx               ) ;
			::serdes(s,lmake_dir_var_name) ;
			::serdes(s,dbg_info          ) ;
		}
		void update_hash(Hash::Xxh& h) const { // ignore debug info as these does not participate to the semantic
			h.update(is_dynamic) ;
			h.update(glbs_str  ) ;
			h.update(code_str  ) ;
			h.update(ctx       ) ;
		}
		// END_OF_VERSIONING
		::string append_dbg_info(::string const& code) const {
			::string res = code ;
			if (+dbg_info) {
				res
				<<	set_nl
				<<	lmake_dir_var_name<<" = "<<mk_py_str(Disk::no_slash(*g_lmake_dir_s))<<'\n'
				<<	dbg_info
				;
			}
			return res ;
		}
		// data
	public :
		// START_OF_VERSIONING
		bool             is_dynamic = false ;
		::string         glbs_str   ;          // if is_dynamic <=> contains string to run to get the glbs below
		::string         code_str   ;          // if is_dynamic <=> contains string to compile to code object below
		::vector<CmdIdx> ctx        ;          // a list of stems, targets & deps, accessed by code
		// END_OF_VERSIONING
		// START_OF_VERSIONING
		::string lmake_dir_var_name = {} ;     // name of variable holding lmake_dir
		::string dbg_info           = {} ;
		// END_OF_VERSIONING
	} ;

	template<class T> struct DynamicDsk : DynamicDskBase {
		// statics
		static ::string s_exc_msg (bool using_static) { return "cannot compute dynamic "s + T::Msg + (using_static?", using static info":"") ; }
		// cxtors & casts
		DynamicDsk() = default ;
		template<class... A> DynamicDsk( Py::Tuple const& , ::umap_s<CmdIdx> const& var_idxs , A&&... ) ;
		// services
		template<IsStream S> void serdes(S& s) {
			DynamicDskBase::serdes(s) ;
			::serdes(s,spec) ;
		}
		void update_hash(Hash::Xxh& h) const {
			DynamicDskBase::update_hash(h) ;
			h.update(spec) ;
		}
		// data
		// START_OF_VERSIONING
		T spec ; // contains default values when code does not provide the necessary entries
		// END_OF_VERSIONING
	} ;

	template<class T> struct Dynamic : DynamicDsk<T> {
		using Base = DynamicDsk<T> ;
		using Base::is_dynamic      ;
		using Base::glbs_str        ;
		using Base::code_str        ;
		using Base::ctx             ;
		using Base::spec            ;
		using Base::_s_eval         ;
		using Base::append_dbg_info ;
		// statics
		static bool s_is_dynamic(Py::Tuple const&) ;
		// cxtors & casts
		using Base::Base ;
		Dynamic           (Dynamic const& src) : Base{       src } , glbs{       src.glbs } , code{       src.code } {}                                  // mutex is not copiable
		Dynamic           (Dynamic     && src) : Base{::move(src)} , glbs{::move(src.glbs)} , code{::move(src.code)} {}                                  // .
		Dynamic& operator=(Dynamic const& src) {                                                                                                         // .
			Base::operator=(src) ;
			glbs = src.glbs ;
			code = src.code ;
			return *this ;
		}
		Dynamic& operator=(Dynamic&& src) {                                                                                                              // .
			Base::operator=(::move(src)) ;
			glbs = ::move(src.glbs) ;
			code = ::move(src.code) ;
			return *this ;
		}
		// services
		void compile() ;
		//
		// SimpleMatch is lazy evaluated from Job (when there is one)
		T eval( Job   , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs={} , ::vmap_s<DepDigest>* deps=nullptr ) const ;
		T eval(         Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} , ::vmap_s<DepDigest>* deps=nullptr ) const { return eval( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs , deps ) ; }
		T eval( Job j , Rule::SimpleMatch      & m ,                             ::vmap_s<DepDigest>* deps         ) const { return eval( j  ,                                m  , {}    , deps ) ; }
		T eval(         Rule::SimpleMatch const& m ,                             ::vmap_s<DepDigest>* deps         ) const { return eval( {} , const_cast<Rule::SimpleMatch&>(m) , {}    , deps ) ; }
		//
		void eval_ctx( Job , Rule::SimpleMatch      &/*lazy*/ , ::vmap_ss const& rsrcs , EvalCtxFuncStr const&     , EvalCtxFuncDct const&     ) const ;
		void eval_ctx(       Rule::SimpleMatch const& m       , ::vmap_ss const& rsrcs , EvalCtxFuncStr const& cbs , EvalCtxFuncDct const& cbd ) const {
			return eval_ctx( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs , cbs , cbd ) ;                                                              // cannot lazy evaluate w/o a job
		}
		::string parse_fstr( ::string const& fstr , Job , Rule::SimpleMatch      &/*lazy*/ , ::vmap_ss const& rsrcs={} ) const ;
		::string parse_fstr( ::string const& fstr ,       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} ) const {
			return parse_fstr( fstr , {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs ) ;                                                                 // cannot lazy evaluate w/o a job
		}
	protected :
		Py::Ptr<Py::Object> _eval_code( Job , Rule::SimpleMatch      &/*lazy*/ , ::vmap_ss const& rsrcs={} , ::vmap_s<DepDigest>* deps=nullptr ) const ;
		Py::Ptr<Py::Object> _eval_code(       Rule::SimpleMatch const& m       , ::vmap_ss const& rsrcs={} , ::vmap_s<DepDigest>* deps=nullptr ) const {
			return _eval_code( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs , deps ) ;                                                                 // cannot lazy evaluate w/o a job
		}
		// data
	private :
		mutable Mutex<MutexLvl::Rule> _glbs_mutex ; // ensure glbs is not used for several jobs simultaneously
	public :
		Py::Ptr<Py::Dict> mutable glbs ;            // if is_dynamic <=> dict to use as globals when executing code, modified then restored during evaluation
		Py::Ptr<Py::Code>         code ;            // if is_dynamic <=> python code object to execute with stems as locals and glbs as globals leading to a dict that can be used to build data
	} ;

	struct DynamicDepsAttrs : Dynamic<DepsAttrs> {
		using Base = Dynamic<DepsAttrs> ;
		// cxtors & casts
		using Base::Base ;
		DynamicDepsAttrs           (DynamicDepsAttrs const& src) : Base           {       src } {}                 // only copy disk backed-up part, in particular mutex is not copied
		DynamicDepsAttrs           (DynamicDepsAttrs     && src) : Base           {::move(src)} {}                 // .
		DynamicDepsAttrs& operator=(DynamicDepsAttrs const& src) { Base::operator=(       src ) ; return *this ; } // .
		DynamicDepsAttrs& operator=(DynamicDepsAttrs     && src) { Base::operator=(::move(src)) ; return *this ; } // .
		// services
		::vmap_s<DepSpec> eval(Rule::SimpleMatch const&) const ;
	} ;

	struct DynamicStartCmdAttrs : Dynamic<StartCmdAttrs> {
		using Base = Dynamic<StartCmdAttrs> ;
		// cxtors & casts
		using Base::Base ;
		DynamicStartCmdAttrs           (DynamicStartCmdAttrs const& src) : Base           {       src } {}                 // only copy disk backed-up part, in particular mutex is not copied
		DynamicStartCmdAttrs           (DynamicStartCmdAttrs     && src) : Base           {::move(src)} {}                 // .
		DynamicStartCmdAttrs& operator=(DynamicStartCmdAttrs const& src) { Base::operator=(       src ) ; return *this ; } // .
		DynamicStartCmdAttrs& operator=(DynamicStartCmdAttrs     && src) { Base::operator=(::move(src)) ; return *this ; } // .
	} ;

	struct DynamicCmd : Dynamic<Cmd> {
		using Base = Dynamic<Cmd> ;
		// cxtors & casts
		using Base::Base ;
		DynamicCmd           (DynamicCmd const& src) : Base           {       src } {}                 // only copy disk backed-up part, in particular mutex is not copied
		DynamicCmd           (DynamicCmd     && src) : Base           {::move(src)} {}                 // .
		DynamicCmd& operator=(DynamicCmd const& src) { Base::operator=(       src ) ; return *this ; } // .
		DynamicCmd& operator=(DynamicCmd     && src) { Base::operator=(::move(src)) ; return *this ; } // .
		// services
		::pair_ss/*script,call*/ eval( Rule::SimpleMatch const& , ::vmap_ss const& rsrcs={} , ::vmap_s<DepDigest>* deps=nullptr ) const ;
	} ;

	struct TargetPattern {
		Re::Match match(::string const& t) const { return re.match(t) ; }
		// data
		Re::RegExpr      re     ;
		::vector<VarIdx> groups ; // indexed by stem index, provide the corresponding group number in pattern
		::string         txt    ; // human readable pattern
	} ;

	struct RuleData {
		friend ::ostream& operator<<( ::ostream& , RuleData const& ) ;
		friend Rule ;
		static constexpr VarIdx NoVar = Rule::NoVar ;
		struct MatchEntry {
			// data
			::string         pattern   = {} ;
			MatchFlags       flags     = {} ;
			::vector<VarIdx> conflicts = {} ;                          // for target only, the idx of the previous targets that may conflict with this one
		} ;
		// static data
		static size_t s_name_sz ;
		// cxtors & casts
		RuleData(                                        ) = default ;
		RuleData( Special , ::string const& src_dir_s={} ) ;           // src_dir in case Special is SrcDir
		RuleData(::string_view const& str) {
			IStringStream is{::string(str)} ;
			serdes(static_cast<::istream&>(is)) ;
		}
		RuleData(Py::Dict const& dct) {
			_acquire_py(dct) ;
			_compile   (   ) ;
		}
		operator ::string() const {
			return serialize(*this) ;
		}
		template<IsStream S> void serdes(S&) ;
	private :
		void _acquire_py(Py::Dict const&) ;
		void _compile   (               ) ;
		//
		template<class T,class... A> ::string _pretty_str( size_t i , Dynamic<T> const& d , A&&... args ) const ;
	public :
		::string pretty_str() const ;
		// accesses
		bool   is_special  (         ) const { return special!=Special::Plain                              ; }
		bool   user_defined(         ) const { return !allow_ext                                           ; }                                    // used to decide to print in LMAKE/rules
		Tflags tflags      (VarIdx ti) const { SWEAR(ti!=NoVar) ; return matches[ti].second.flags.tflags() ; }
		//
		vmap_view_c_ss static_stems() const { return vmap_view_c_ss(stems).subvec(0,n_static_stems) ; }
		//
		FileNameIdx job_sfx_len() const {
			return
				1                                                                                                                                 // null to disambiguate w/ Node names
			+	n_static_stems * sizeof(FileNameIdx)*2                                                                                            // pos+len for each stem
			+	sizeof(RuleIdx)                                                                                                                   // Rule index
			;
		}
		// services
		::string add_cwd( ::string&& file , bool top=false ) const {
			if ( !top && +cwd_s ) return Disk::mk_glb(file,cwd_s) ;
			else                  return ::move(file)             ;
		}
		//
		::string gen_py_line( Job , Rule::SimpleMatch      &/*lazy*/ , VarCmd    , VarIdx   , ::string const& key , ::string const& val ) const ;
		::string gen_py_line(       Rule::SimpleMatch const& m       , VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) const {
			return gen_py_line( {} , const_cast<Rule::SimpleMatch&>(m) , vc , i , key , val ) ;                                                   // cannot lazy evaluate w/o a job
		}
		void new_job_report( Delay exec_time , CoarseDelay cost , uint32_t tokens ) const ;
	private :
		::vector_s    _list_ctx  ( ::vector<CmdIdx> const& ctx     ) const ;
		void          _set_crcs  (                                 ) ;
		TargetPattern _mk_pattern( ::string const& , bool for_name ) const ;
		// START_OF_VERSIONING
		// user data
	public :
		Special              special    = Special::None ;
		Prio                 prio       = 0             ;      // the priority of the rule
		::string             name       ;                      // the short message associated with the rule
		::vmap_ss            stems      ;                      // stems are ordered : statics then stars, stems used as both static and star appear twice
		::string             cwd_s      ;                      // cwd in which to interpret targets & deps and execute cmd (with ending /)
		::string             job_name   ;                      // used to show in user messages (not all fields are actually used)
		::vmap_s<MatchEntry> matches    ;                      // keep star user order, static entries first in MatchKind order
		VarIdx               stdout_idx = NoVar         ;      // index of target used as stdout
		VarIdx               stdin_idx  = NoVar         ;      // index of dep used as stdin
		bool                 allow_ext  = false         ;      // if true <=> rule may match outside repo
		// following is only if plain rules
		DynamicDepsAttrs          deps_attrs         ;         // in match crc, evaluated at job creation time
		Dynamic<CreateNoneAttrs > create_none_attrs  ;         // in no    crc, evaluated at job creation time
		Dynamic<CacheNoneAttrs  > cache_none_attrs   ;         // in no    crc, evaluated twice : at submit time to look for a hit and after execution to upload result
		Dynamic<SubmitRsrcsAttrs> submit_rsrcs_attrs ;         // in rsrcs crc, evaluated at submit time
		Dynamic<SubmitNoneAttrs > submit_none_attrs  ;         // in no    crc, evaluated at submit time
		DynamicStartCmdAttrs      start_cmd_attrs    ;         // in cmd   crc, evaluated before execution
		DynamicCmd                cmd                ;         // in cmd   crc, evaluated before execution
		Dynamic<StartRsrcsAttrs > start_rsrcs_attrs  ;         // in rsrcs crc, evaluated before execution
		Dynamic<StartNoneAttrs  > start_none_attrs   ;         // in no    crc, evaluated before execution
		Dynamic<EndCmdAttrs     > end_cmd_attrs      ;         // in cmd   crc, evaluated after  execution
		Dynamic<EndNoneAttrs    > end_none_attrs     ;         // in no    crc, evaluated after  execution
		bool                      is_python          = false ;
		bool                      force              = false ;
		uint8_t                   n_submits          = 0     ; // max number of submission for a given job for a given req (disabled if 0)
		// derived data
		VarIdx n_static_stems   = 0 ;
		VarIdx n_static_targets = 0 ;                          // number of official static targets
		VarIdx n_statics        = 0 ;
		// management data
		ExecGen cmd_gen   = 1 ;                                // cmd generation, must be >0 as 0 means !cmd_ok
		ExecGen rsrcs_gen = 1 ;                                // for a given cmd, resources generation, must be >=cmd_gen
		// stats
		mutable Delay  cost_per_token = {} ;                   // average cost per token
		mutable Delay  exec_time      = {} ;                   // average exec_time
		mutable JobIdx stats_weight   = 0  ;                   // number of jobs used to compute average cost_per_token and exec_time

		// END_OF_VERSIONING

		// not stored on disk
		::vector<size_t>        stem_mark_counts ;
		/**/     TargetPattern  job_name_pattern ;
		::vector<TargetPattern> patterns         ;
		Crc                     match_crc        = Crc::None ;
		Crc                     cmd_crc          = Crc::None ;
		Crc                     rsrcs_crc        = Crc::None ;
	} ;

	// SimpleMatch does not call Python and only provides services that can be served with this constraint
	struct Rule::SimpleMatch {
		friend ::ostream& operator<<( ::ostream& , SimpleMatch const& ) ;
		// cxtors & casts
	public :
		SimpleMatch(                                        ) = default ;
		SimpleMatch( Rule    r , ::vector_s const& ss       ) : rule{r} , stems{ss} {}
		SimpleMatch( Job                                    ) ;
		SimpleMatch( Rule    r , ::string   const& job_name ) : SimpleMatch{r,r->job_name_pattern,job_name} {}
		SimpleMatch( RuleTgt   , ::string   const& target   ) ;
	private :
		SimpleMatch( Rule , TargetPattern const& , ::string const& ) ;
	public :
		bool operator==(SimpleMatch const&) const = default ;
		bool operator+ (                  ) const { return +rule ; }
		bool operator! (                  ) const { return !rule ; }
		// accesses
		::vector_s star_patterns () const ;
		::vector_s py_matches    () const ;
		::vector_s static_matches() const ;
		//
		::vmap_s<DepSpec> const& deps() const {
			if (!_has_deps) {
				_deps = rule->deps_attrs.eval(*this) ;
				_has_deps = true ;
			}
			return _deps ;
		}
		// services
		::pair_ss    full_name  () const ;
		::string     name       () const { return full_name().first ; }
		::uset<Node> target_dirs() const ;
		// data
		Rule       rule  ;
		::vector_s stems ; // static stems only of course
		// cache
	private :
		mutable bool                  _has_static_targets = false ;
		mutable bool                  _has_star_targets   = false ;
		mutable bool                  _has_deps           = false ;
		mutable ::umap_s<VarIdx>      _static_targets     ;
		mutable ::vector<Re::RegExpr> _star_targets       ;
		mutable ::vmap_s<DepSpec>     _deps               ;
	} ;

	struct RuleTgt : Rule {
		friend ::ostream& operator<<( ::ostream& , RuleTgt const ) ;
		using Rep = Uint< NBits<Rule> + NBits<VarIdx> > ;
		//cxtors & casts
		RuleTgt(                    ) = default ;
		RuleTgt( Rule r , VarIdx ti ) : Rule{r} , tgt_idx{ti} {}
		// accesses
		Rep                operator+   (              ) const { return (+Rule(*this)<<NBits<VarIdx>) | tgt_idx  ; }
		bool               operator==  (RuleTgt const&) const = default ;
		::partial_ordering operator<=> (RuleTgt const&) const = default ;
		//
		::string const& key         () const {                                                          return _matches().first                       ; }
		::string const& target      () const { SWEAR(_matches().second.flags.tflags()[Tflag::Target]) ; return _matches().second.pattern              ; }
		Tflags          tflags      () const {                                                          return _matches().second.flags.tflags      () ; }
		ExtraTflags     extra_tflags() const {                                                          return _matches().second.flags.extra_tflags() ; }
		//
		bool sure() const { return ( tgt_idx<(*this)->n_static_targets && !extra_tflags()[ExtraTflag::Optional] ) || tflags()[Tflag::Phony] ; }
	private :
		::pair_s<RuleData::MatchEntry> const& _matches() const { return (*this)->matches[tgt_idx] ; }
		// services
	public :
		TargetPattern const& pattern() const { return (*this)->patterns[tgt_idx] ; }
		// data
		VarIdx tgt_idx = 0 ;
	} ;

}

#endif
#ifdef IMPL

namespace Engine {

	//
	// Rule
	//

	inline ::string Rule::job_sfx() const {
		::string res(
			size_t(
					1                                             // JobMrkr to disambiguate with Node names
				+	(*this)->n_static_stems*sizeof(FileNameIdx)*2 // room for stems spec
				+	sizeof(Idx)                                   // Rule index set below
			)
		,	JobMrkr
		) ;
		encode_int( &res[res.size()-sizeof(Idx)] , +*this ) ;
		return res ;
	}

	//
	// Attrs
	//

	namespace Attrs {

		template<::integral I> bool/*updated*/ acquire( I& dst , Py::Object const* py_src , I min , I max ) {
			if (!py_src          ) {           return false ; }
			if (*py_src==Py::None) { dst = 0 ; return true  ; }
			//
			long v = py_src->as_a<Py::Int>() ;
			if (::cmp_less   (v,min)) throw "underflow"s ;
			if (::cmp_greater(v,max)) throw "overflow"s  ;
			dst = I(v) ;
			return true ;
		}

		template<StdEnum E> bool/*updated*/ acquire( E& dst , Py::Object const* py_src ) {
			if (!py_src          ) {                 return false ; }
			if (*py_src==Py::None) { dst = E::Dflt ; return true  ; }
			//
			dst = mk_enum<E>(py_src->as_a<Py::Str>()) ;
			return true ;
		}

		template<bool Env> bool/*updated*/ acquire( ::string& dst , Py::Object const* py_src ) {
			if      ( !py_src                       )             return false ;
			else if (  Env && *py_src==Py::None     )                            dst = EnvDynMrkr     ;   // special case environment variable to mark dynamic values
			else if ( !Env && *py_src==Py::None     ) { if (!dst) return false ; dst = {}             ; }
			else if ( !Env && *py_src==Py::Ellipsis )                            dst = "..."          ;
			else                                                                 dst = *py_src->str() ;
			return true ;
		}

		template<class T,bool Env> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vector<T>& dst , Py::Object const* py_src ) {
			if (!py_src          )             return false ;
			if (*py_src==Py::None) { if (!dst) return false ; dst = {} ; return true  ; }
			//
			bool   updated = false      ;
			size_t n       = dst.size() ;
			size_t i       = 0          ;
			auto handle_entry = [&](Py::Object const& py_item)->void {
				if (py_item==Py::None) return ;
				try {
					if constexpr (Env) updated |= acquire<Env>(grow(dst,i++),&py_item) ; // special case for environment where we replace occurrences of lmake & root dirs by markers ...
					else               updated |= acquire     (grow(dst,i++),&py_item) ; // ... to make repo robust to moves of lmake or itself
				} catch (::string const& e) {
					throw "for item "s+i+" : "+e ;
				}
			} ;
			if (py_src->is_a<Py::Sequence>()) for( Py::Object const& py_item : py_src->as_a<Py::Sequence>() ) handle_entry(py_item) ;
			else                                                                                              handle_entry(*py_src) ;
			if (i!=n) {
				updated = true ;
				dst.resize(i) ;
			}
			return updated ;
		}

		template<class T,bool Env> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vmap_s<T>& dst , Py::Object const* py_src ) {
			if (!py_src          )             return false ;
			if (*py_src==Py::None) { if (!dst) return false ; dst = {} ; return true  ; }
			//
			bool            updated = false                    ;
			::map_s<T>      dst_map = mk_map(dst)              ;
			Py::Dict const& py_map  = py_src->as_a<Py::Dict>() ;
			for( auto const& [py_key,py_val] : py_map ) {
				::string key = py_key.template as_a<Py::Str>() ;
				if constexpr (Env)
					if (py_val==Py::Ellipsis) {
						updated      = true        ;
						dst_map[key] = EnvPassMrkr ;                                 // special case for environment where we put an otherwise illegal marker to ask to pass value from job_exec env
						continue ;
					}
				try {
					auto [it,inserted] = dst_map.emplace(key,T()) ;
					/**/               updated |= inserted                         ;
					if constexpr (Env) updated |= acquire<Env>(it->second,&py_val) ; // special case for environment where we replace occurrences of lmake & root dirs by markers ...
					else               updated |= acquire     (it->second,&py_val) ; // ... to make repo robust to moves of lmake or itself
				} catch (::string const& e) {
					throw "for item "+key+" : "+e ;
				}
			}
			dst = mk_vmap(dst_map) ;
			return updated ;
		}

	}

	//
	// Dynamic
	//

	template<class T> template<class... A> DynamicDsk<T>::DynamicDsk( Py::Tuple const& py_src , ::umap_s<CmdIdx> const& var_idxs , A&&... args ) : DynamicDskBase(py_src,var_idxs) {
		if (py_src[0]!=Py::None) spec.init( is_dynamic , &py_src[0].as_a<Py::Dict>() , var_idxs , ::forward<A>(args)... ) ;
	}

	template<class T> void Dynamic<T>::compile() {
		if (!is_dynamic) return ;
		Py::Gil::s_swear_locked() ;
		try { code = code_str                              ; code->boost() ; } catch (::string const& e) { throw "cannot compile code :\n"   +indent(e,1) ; }
		try { glbs = Py::py_run(append_dbg_info(glbs_str)) ; glbs->boost() ; } catch (::string const& e) { throw "cannot compile context :\n"+indent(e,1) ; }
	}

	template<class T> void Dynamic<T>::eval_ctx( Job job , Rule::SimpleMatch& match_ , ::vmap_ss const& rsrcs_ , EvalCtxFuncStr const& cb_str , EvalCtxFuncDct const& cb_dct ) const {
		_s_eval(job,match_,rsrcs_,ctx,cb_str,cb_dct) ;
	}

	template<class T> ::string Dynamic<T>::parse_fstr( ::string const& fstr , Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs ) const {
		::vector<CmdIdx> ctx_  ;
		::vector_s       fixed { 1 } ;
		size_t           fi    = 0   ;
		for( size_t ci=0 ; ci<fstr.size() ; ci++ ) {
			if (fstr[ci]==Rule::StemMrkr) {
				VarCmd vc = decode_enum<VarCmd>(&fstr[ci+1]) ; ci += sizeof(VarCmd) ;
				VarIdx i  = decode_int <VarIdx>(&fstr[ci+1]) ; ci += sizeof(VarIdx) ;
				ctx_ .push_back   (CmdIdx{vc,i}) ;
				fixed.emplace_back(            ) ;
				fi++ ;
			} else {
				fixed[fi] += fstr[ci] ;
			}
		}
		fi = 0 ;
		::string res = ::move(fixed[fi++]) ;
		auto cb_str = [&]( VarCmd , VarIdx , string const& /*key*/ , string  const&   val   )->void { res<<val<<fixed[fi++] ; } ;
		auto cb_dct = [&]( VarCmd , VarIdx , string const& /*key*/ , vmap_ss const& /*val*/ )->void { FAIL()                ; } ;
		_s_eval(job,match,rsrcs,ctx_,cb_str,cb_dct) ;
		return res ;
	}

	template<class T> Py::Ptr<Py::Object> Dynamic<T>::_eval_code( Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps ) const {
		// functions defined in glbs use glbs as their global dict (which is stored in the code object of the functions), so glbs must be modified in place or the job-related values will not
		// be seen by these functions, which is the whole purpose of such dynamic values
		Rule       r       = +match ? match.rule : job->rule ;
		::vector_s to_del  ;
		::string   to_eval ;
		eval_ctx( job , match , rsrcs
		,	[&]( VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) -> void {
				to_del.push_back(key) ;
				if (vc!=VarCmd::StarMatch) glbs->set_item(key,*Py::Ptr<Py::Str>(val)) ;
				else                       to_eval += r->gen_py_line( job , match , vc , i , key , val ) ;
			}
		,	[&]( VarCmd , VarIdx , ::string const& key , ::vmap_ss const& val ) -> void {
				to_del.push_back(key) ;
				Py::Ptr<Py::Dict> py_dct { New } ;
				for( auto const& [k,v] : val ) py_dct->set_item(k,*Py::Ptr<Py::Str>(v)) ;
				glbs->set_item(key,*py_dct) ;
			}
		) ;
		Py::py_run(to_eval,*glbs) ;
		Py::Ptr<Py::Object> res      ;
		::string            err      ;
		bool                seen_err = false  ;
		AutodepLock         lock     { deps } ;
		//                                vvvvvvvvvvvvvvvvv
		try                       { res = code->eval(*glbs) ;   }
		//                                ^^^^^^^^^^^^^^^^^
		catch (::string const& e) { err = e ; seen_err = true ; }
		for( ::string const& key : to_del ) glbs->del_item(key) ;        // delete job-related info, just to avoid percolation to other jobs, even in case of error
		if ( +lock.err || seen_err ) throw ::pair(lock.err/*msg*/,err) ;
		return res ;
	}

	template<class T> T Dynamic<T>::eval( Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps ) const {
		T res = spec ;
		if (is_dynamic) {
			Py::Gil             gil    ;
			Py::Ptr<Py::Object> py_obj = _eval_code( job , match , rsrcs , deps ) ;
			if (*py_obj!=Py::None) {
				if (!py_obj->is_a<Py::Dict>()) throw "type error : "s+py_obj->ob_type->tp_name+" is not a dict" ;
				try                       { res.update(py_obj->template as_a<Py::Dict>()) ; }
				catch (::string const& e) { throw ::pair_ss({}/*msg*/,e/*err*/) ;           }
			}
		}
		return res ;
	}

	//
	// RuleData
	//

	// START_OF_VERSIONING
	template<IsStream S> void RuleData::serdes(S& s) {
		if (::is_base_of_v<::istream,S>) *this = {} ;
		::serdes(s,special         ) ;
		::serdes(s,prio            ) ;
		::serdes(s,name            ) ;
		::serdes(s,stems           ) ;
		::serdes(s,cwd_s           ) ;
		::serdes(s,job_name        ) ;
		::serdes(s,matches         ) ;
		::serdes(s,stdout_idx      ) ;
		::serdes(s,stdin_idx       ) ;
		::serdes(s,allow_ext       ) ;
		::serdes(s,n_static_stems  ) ;
		::serdes(s,n_static_targets) ;
		::serdes(s,n_statics       ) ;
		if (special==Special::Plain) {
			::serdes(s,deps_attrs        ) ;
			::serdes(s,create_none_attrs ) ;
			::serdes(s,cache_none_attrs  ) ;
			::serdes(s,submit_rsrcs_attrs) ;
			::serdes(s,submit_none_attrs ) ;
			::serdes(s,start_cmd_attrs   ) ;
			::serdes(s,cmd               ) ;
			::serdes(s,start_rsrcs_attrs ) ;
			::serdes(s,start_none_attrs  ) ;
			::serdes(s,end_cmd_attrs     ) ;
			::serdes(s,end_none_attrs    ) ;
			::serdes(s,is_python         ) ;
			::serdes(s,force             ) ;
			::serdes(s,n_submits         ) ;
			::serdes(s,cmd_gen           ) ;
			::serdes(s,rsrcs_gen         ) ;
			::serdes(s,cost_per_token    ) ;
			::serdes(s,exec_time         ) ;
			::serdes(s,stats_weight      ) ;
		}
		if (is_base_of_v<::istream,S>) {
			Py::Gil gil ;
			_compile() ;
		}
	}
	// END_OF_VERSIONING

}

namespace std {
	template<> struct hash<Engine::RuleTgt> {
		size_t operator()(Engine::RuleTgt const& rt) const { // use FNV-32, easy, fast and good enough, use 32 bits as we are mostly interested by lsb's
			size_t res = 0x811c9dc5 ;
			res = (res^+Engine::Rule(rt)) * 0x01000193 ;
			res = (res^ rt.tgt_idx      ) * 0x01000193 ;
			return res ;
		}
	} ;
}

#endif
