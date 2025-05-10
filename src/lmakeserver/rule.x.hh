// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
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

ENUM( DynKind
,	None
,	ShellCmd     // static shell cmd
,	PythonCmd    // python cmd (necessarily static)
,	Dyn          // dynamic  code, not compiled
,	Compiled     // compiled code, glbs not computed
,	CompiledGlbs // compiled code, glbs     computed
)

ENUM_1( EnvFlag
,	Dflt = Rsrc
//
,	None // ignore variable
,	Rsrc // consider variable as a resource : upon modification, rebuild job if it was in error
,	Cmd  // consider variable as a cmd      : upon modification, rebuild job
)

ENUM( DynImport
,	Static      // may import when computing glbs
,	Dyn         // may import when executing code
)

ENUM_1( RuleCrcState
,	CmdOk = RsrcsOld // <=CmdOk means no need to run job because of cmd
,	Ok
,	RsrcsOld
,	RsrcsForgotten   // when rsrcs are forgotten (and rsrcs were old), process as if cmd changed (i.e. always rerun)
,	CmdOld
)

ENUM_2( Special
,	NShared = Plain // <NShared means there is a single such rule
,	HasJobs = Plain // <=HasJobs means jobs can refer to this rule
//
,	None            // value 0 reserved to mean not initialized
,	Req
,	InfiniteDep
,	InfinitePath
,	Plain
// ordered by decreasing matching priority within each prio
,	Anti
,	GenericSrc
)
inline bool is_infinite(Special s) { return s==Special::InfiniteDep || s==Special::InfinitePath ; }

ENUM( VarCmd
,	Stems   , Stem
,	Targets , Match , StarMatch
,	Deps    , Dep
,	Rsrcs   , Rsrc
)

// END_OF_VERSIONING

namespace Engine {

	struct Rule        ;
	struct RuleCrc     ;
	struct RuleData    ;
	struct RuleCrcData ;

	struct RuleTgt ;

	struct DynStr   ;
	struct DynEntry ;

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
		friend ::string& operator+=( ::string& , Rule const ) ;
		struct RuleMatch ;
		//
		static constexpr char   StarMrkr =  0 ; // signal a star stem in job_name
		static constexpr char   StemMrkr =  0 ; // signal a stem in job_name & targets & deps & cmd
		static constexpr VarIdx NoVar    = -1 ;
		// statics
		static bool/*keep*/ s_qualify_dep( ::string const& key , ::string const& dep ) ;
		// static data
		static Atomic<Pdate> s_last_dyn_date ;  // used to check dynamic attribute computation does not take too long
		static Job           s_last_dyn_job  ;
		static const char*   s_last_dyn_msg  ;
		// cxtors & casts
		using RuleBase::RuleBase ;
		Rule(RuleBase const& rb) : RuleBase{rb} {}
	} ;

	struct RuleCrc : RuleCrcBase {
		friend ::string& operator+=( ::string& , RuleCrc const ) ;
		// cxtors & casts
		using RuleCrcBase::RuleCrcBase ;
	} ;

}

#endif
#ifdef DATA_DEF

namespace Engine {

	namespace Attrs {
		// statics
		/**/                   bool/*updated*/ acquire( bool               & dst , Py::Object const* py_src                                                      ) ;
		/**/                   bool/*updated*/ acquire( Delay              & dst , Py::Object const* py_src , Delay min=Delay::Lowest , Delay max=Delay::Highest ) ;
		/**/                   bool/*updated*/ acquire( JobSpace::ViewDescr& dst , Py::Object const* py_src                                                      ) ;
		template<::integral I> bool/*updated*/ acquire( I                  & dst , Py::Object const* py_src , I     min=Min<I>        , I     max=Max<I>         ) ;
		template<StdEnum    E> bool/*updated*/ acquire( E                  & dst , Py::Object const* py_src                                                      ) ;
		//
		template<        bool Env=false>                                         bool/*updated*/ acquire( ::string     & dst , Py::Object const* py_src ) ;
		template<class T,bool Env=false> requires(!Env||::is_same_v<T,::string>) bool/*updated*/ acquire( ::vector<T  >& dst , Py::Object const* py_src ) ;
		template<class T,bool Env=false> requires(!Env||::is_same_v<T,::string>) bool/*updated*/ acquire( ::vmap_s<T  >& dst , Py::Object const* py_src ) ;
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
	} ;

	struct DepSpec {
		friend ::string& operator+=( ::string& , DepSpec const& ) ;
		// data
		::string    txt          ;
		Dflags      dflags       ;
		ExtraDflags extra_dflags ;
	} ;

	// used at match time
	struct DepsAttrs {
		static constexpr const char* Msg = "deps" ;
		// services
		void init( Py::Dict const* , ::umap_s<CmdIdx> const& , RuleData const& ) ;
		// data
		// START_OF_VERSIONING
		bool              full_dyn = true ; // if true <=> deps is empty and new keys can be added, else dynamic deps must be within dep keys ...
		::vmap_s<DepSpec> deps     ;        // ... if full_dyn, we are not initialized, so be ready by default
		// END_OF_VERSIONING
	} ;

	// used at submit time, participate in resources
	struct SubmitRsrcsAttrs {
		static constexpr const char* Msg = "submit resources attributes" ;
		// services
		void init  ( Py::Dict const* py_src , ::umap_s<CmdIdx> const& , RuleData const& ) { update(*py_src) ; }
		void update( Py::Dict const& py_dct ) {
			/**/ Attrs::acquire_from_dct( backend , py_dct , "backend" ) ;
			if ( Attrs::acquire_from_dct( rsrcs   , py_dct , "rsrcs"   ) ) ::sort(rsrcs) ;                                          // stabilize rsrcs crc
		}
		Tokens1 tokens1() const {
			for(auto const& [k,v] : rsrcs) if (k=="cpu")
				try                     { return ::min( ::max(from_string<uint32_t>(v),uint32_t(1))-1 , uint32_t(Max<Tokens1>) ); }
				catch (::string const&) { break ;                                                                                 } // no valid cpu count, do as if no cpu found
			return 0 ;                                                                                                              // not found : default to 1 cpu
		}
		// data
		// START_OF_VERSIONING
		BackendTag backend = BackendTag::Local ;                                                                                    // backend to use to launch jobs
		::vmap_ss  rsrcs   ;
		// END_OF_VERSIONING
	} ;

	// used both at submit time (for cache look up) and at end of execution (for cache upload)
	struct SubmitAncillaryAttrs {
		static constexpr const char* Msg = "cache key" ;
		// services
		void init  ( Py::Dict const* py_src , ::umap_s<CmdIdx> const& , RuleData const& ) { update(*py_src) ; }
		void update( Py::Dict const& py_dct ) {
			Attrs::acquire_from_dct( cache , py_dct , "cache" ) ;
			throw_unless( !cache || g_config->cache_idxs.contains(cache) , "unexpected cache ",cache," not found in config" ) ;
		}
		// data
		// START_OF_VERSIONING
		::string cache ;
		// END_OF_VERSIONING
	} ;

	// used at start time, participate in cmd
	struct StartCmdAttrs {
		static constexpr const char* Msg = "execution command attributes" ;
		// services
		void init  ( Py::Dict const* py_src , ::umap_s<CmdIdx> const& , RuleData const& ) { update(*py_src) ; }
		void update( Py::Dict const& py_dct ) {
			using namespace Attrs ;
			Attrs::acquire_from_dct( allow_stderr           , py_dct , "allow_stderr"  ) ;
			Attrs::acquire_from_dct( auto_mkdir             , py_dct , "auto_mkdir"    ) ;
			Attrs::acquire_from_dct( job_space.chroot_dir_s , py_dct , "chroot_dir"    ) ; if (+job_space.chroot_dir_s) job_space.chroot_dir_s = with_slash(job_space.chroot_dir_s) ;
			Attrs::acquire_env     ( env                    , py_dct , "env"           ) ;
			Attrs::acquire_from_dct( ignore_stat            , py_dct , "ignore_stat"   ) ;
			Attrs::acquire_from_dct( interpreter            , py_dct , "interpreter"   ) ;
			Attrs::acquire_from_dct( job_space.lmake_view_s , py_dct , "lmake_view"    ) ; if (+job_space.lmake_view_s) job_space.lmake_view_s = with_slash(job_space.lmake_view_s) ;
			Attrs::acquire_from_dct( job_space.repo_view_s  , py_dct , "repo_view"     ) ; if (+job_space.repo_view_s ) job_space.repo_view_s  = with_slash(job_space.repo_view_s ) ;
			Attrs::acquire_from_dct( job_space.tmp_view_s   , py_dct , "tmp_view"      ) ; if (+job_space.tmp_view_s  ) job_space.tmp_view_s   = with_slash(job_space.tmp_view_s  ) ;
			Attrs::acquire_from_dct( job_space.views        , py_dct , "views"         ) ;
			::sort( env                                                                                                                                   ) ; // stabilize cmd crc
			::sort( job_space.views , [](::pair_s<JobSpace::ViewDescr> const& a,::pair_s<JobSpace::ViewDescr> const&b)->bool { return a.first<b.first ; } ) ; // .
		}
		// data
		// START_OF_VERSIONING
		::vector_s interpreter  ;
		bool       allow_stderr = false ;
		bool       auto_mkdir   = false ;
		bool       ignore_stat  = false ;
		::vmap_ss  env          ;
		JobSpace   job_space    ;
		// END_OF_VERSIONING
	} ;

	// used at start time, participate in resources
	struct StartRsrcsAttrs {
		static constexpr const char* Msg = "execution resources attributes" ;
		void init  ( Py::Dict const* py_src , ::umap_s<CmdIdx> const& , RuleData const& ) { update(*py_src) ; }
		void update( Py::Dict const& py_dct ) {
			Attrs::acquire_env     ( env        , py_dct , "env"                               ) ;
			Attrs::acquire_from_dct( method     , py_dct , "autodep"                           ) ;
			Attrs::acquire_from_dct( timeout    , py_dct , "timeout"    , Time::Delay()/*min*/ ) ;
			Attrs::acquire_from_dct( use_script , py_dct , "use_script"                        ) ;
			::sort(env) ;                                                                          // stabilize rsrcs crc
		}
		// data
		// START_OF_VERSIONING
		::vmap_ss     env        ;
		AutodepMethod method     = {}    ;
		Time::Delay   timeout    ;                                                                 // if 0 <=> no timeout, maximum time allocated to job execution in s
		bool          use_script = false ;
		// END_OF_VERSIONING
	} ;

	// used at start time, participate to nothing
	struct StartAncillaryAttrs {
		static constexpr const char* Msg = "execution ancillary attributes" ;
		// services
		void init  ( Py::Dict const* py_src , ::umap_s<CmdIdx> const& , RuleData const& ) { update(*py_src) ; }
		void update( Py::Dict const& py_dct ) {
			using namespace Attrs ;
			Attrs::acquire_from_dct( keep_tmp       , py_dct , "keep_tmp"                              ) ;
			Attrs::acquire_from_dct( z_lvl          , py_dct , "compression"                           ) ;
			Attrs::acquire_from_dct( start_delay    , py_dct , "start_delay"    , Time::Delay()/*min*/ ) ;
			Attrs::acquire_from_dct( kill_sigs      , py_dct , "kill_sigs"                             ) ;
			Attrs::acquire_from_dct( max_stderr_len , py_dct , "max_stderr_len"                        ) ;
			Attrs::acquire_env     ( env            , py_dct , "env"                                   ) ;
			::sort(env) ;                                                                                  // by symmetry with env entries in StartCmdAttrs and StartRsrcsAttrs
		}
		// data
		// START_OF_VERSIONING
		Time::Delay       start_delay    ;                                                                 // job duration above which a start message is generated
		::vector<uint8_t> kill_sigs      ;                                                                 // signals to use to kill job (tried in sequence, 1s apart from each other)
		::vmap_ss         env            ;
		uint16_t          max_stderr_len = 0     ;                                                         // max lines when displaying stderr, 0 means no limit (full content is shown with lshow -e)
		bool              keep_tmp       = false ;
		uint8_t           z_lvl          = 0     ;
		// END_OF_VERSIONING
	} ;

	struct Cmd {
		static constexpr const char* Msg = "execution command" ;
		// services
		void init( Py::Dict const* , ::umap_s<CmdIdx> const& , RuleData const& ) {}
	} ;

	//
	// Dyn*
	//

	struct DynEntry {
		using Kind = DynKind ;
		// cxtros & casts
		DynEntry() = default ;
		DynEntry( RulesBase const& , Bool3 is_python , Py::Dict const& py_src , ::umap_s<CmdIdx> const& var_idxs , bool compile ) ; // is_python is Yes/No for cmd, Maybe for dynamic attrs
		// accesses
		bool operator+() const { return +kind ; }
		// services
		template<IsStream S> void serdes   ( S& , RulesBase const* =nullptr ) ;
		/**/                 void compile  (      RulesBase const&          ) ;
		/**/                 void decompile(                                ) ;
		bool operator==(DynEntry const& de) const {
			if (kind!=de.kind) return false ;
			if (ctx !=de.ctx ) return false ;
			// intentionally ignore dbg_info
			// may_import results from code analysis, no need to compare
			switch (kind) {
				case Kind::None         : return true                                           ;
				case Kind::ShellCmd     : return code_str==de.code_str                          ;
				case Kind::PythonCmd    : return code_str==de.code_str && glbs_str==de.glbs_str ;
				case Kind::Dyn          : return code_str==de.code_str && glbs_str==de.glbs_str ;
				case Kind::Compiled     :                                                         // NO_COV
				case Kind::CompiledGlbs : FAIL() ;                                                // NO_COV too heavy to marshal code to compare, we'll see if necessary
			DF}                                                                                   // NO_COV
		}
		// data
		::vector<CmdIdx>  ctx        ;                 // a list of stems, targets & deps, accessed by code
		::string          code_str   ;                 // contains string to compile to code object below or cmd for static shell cmd
		::string          glbs_str   ;                 // contains string to run to get the glbs below
		::string          dbg_info   ;
		Py::Ptr<Py::Code> code       ;                 // python code object to execute with stems as locals and glbs as globals leading to dynamic value
		Py::Ptr<Py::Dict> glbs       ;                 // dict to use as globals when executing code, modified then restored during evaluation
		Py::Ptr<Py::Code> glbs_code  ;                 // python code object to execute with stems as locals and glbs as globals leading to a dict that can be used to build data
		DynKind           kind       = DynKind::None ;
		BitMap<DynImport> may_import ;                 // if true <= glbs_str or code_str is sensitive to sys.path
	} ;

	using EvalCtxFuncStr = ::function<void( VarCmd , VarIdx idx , string const& key , string  const& val )> ;
	using EvalCtxFuncDct = ::function<void( VarCmd , VarIdx idx , string const& key , vmap_ss const& val )> ;

	struct DynBase {
		static void s_eval( Job , Rule::RuleMatch&/*lazy*/ , ::vmap_ss const& rsrcs_ , ::vector<CmdIdx> const& ctx , EvalCtxFuncStr const& , EvalCtxFuncDct const& ) ;
		static ::string s_parse_fstr( ::string const& fstr , Job , Rule::RuleMatch      &/*lazy*/ , ::vmap_ss const& rsrcs={} ) ;
		static ::string s_parse_fstr( ::string const& fstr ,       Rule::RuleMatch const& m       , ::vmap_ss const& rsrcs={} ) {
			return s_parse_fstr( fstr , {} , const_cast<Rule::RuleMatch&>(m) , rsrcs ) ;                                              // cannot lazy evaluate w/o a job
		}
		// cxtors & casts
		DynBase() = default ;
		DynBase( Py::Ptr<>* /*out*/ py_update , RulesBase& , Py::Dict const& , ::umap_s<CmdIdx> const& var_idxs , Bool3 is_python ) ; // is_python is Yes/No for cmd, Maybe for dynamic attrs
		// accesses
		bool            has_entry(                   ) const { return dyn_idx ; }
		DynEntry const& entry    (RulesBase const& rs) const ;
		DynEntry      & entry    (RulesBase      & rs) const ;
		DynEntry const& entry    (                   ) const ;
		bool            is_dyn   (RulesBase const& rs) const ;
		bool            is_dyn   (                   ) const ;
		// data
		RuleIdx dyn_idx = 0 ;
	} ;

	template<class T> struct Dyn : DynBase {
		// statics
		static ::string s_exc_msg(bool using_static) { return "cannot compute dynamic "s + T::Msg + (using_static?", using static info":"") ; }
		// cxtors & casts
		Dyn() = default ;
		Dyn( RulesBase& , Py::Dict const& , ::umap_s<CmdIdx> const& var_idxs , RuleData const& ) ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,static_cast<DynBase&>(self)) ;
			::serdes(s,spec) ;
		}
		void update_hash( Hash::Xxh&/*inout*/ h , RulesBase const& rs ) const {
			// START_OF_VERSIONING
			/**/             h += spec        ;
			/**/             h += has_entry() ;
			if (has_entry()) const_cast<DynEntry&>(entry(rs)).serdes( /*inout*/h , &rs ) ; // serdes is declared non-const because it is also used for deserializing
			// END_OF_VERSIONING
		}
		// RuleMatch is lazy evaluated from Job (when there is one)
		T eval( Job   , Rule::RuleMatch      &   , ::vmap_ss const& rsrcs={} , ::vmap_s<DepDigest>* deps=nullptr ) const ;
		T eval(         Rule::RuleMatch const& m , ::vmap_ss const& rsrcs={} , ::vmap_s<DepDigest>* deps=nullptr ) const { return eval( {} , const_cast<Rule::RuleMatch&>(m) , rsrcs , deps ) ; }
		T eval( Job j , Rule::RuleMatch      & m ,                             ::vmap_s<DepDigest>* deps         ) const { return eval( j  ,                              m  , {}    , deps ) ; }
		T eval(         Rule::RuleMatch const& m ,                             ::vmap_s<DepDigest>* deps         ) const { return eval( {} , const_cast<Rule::RuleMatch&>(m) , {}    , deps ) ; }
		//
		void eval_ctx( Job , Rule::RuleMatch      &/*lazy*/ , ::vmap_ss const& rsrcs , EvalCtxFuncStr const&     , EvalCtxFuncDct const&     ) const ;
		void eval_ctx(       Rule::RuleMatch const& m       , ::vmap_ss const& rsrcs , EvalCtxFuncStr const& cbs , EvalCtxFuncDct const& cbd ) const {
			return eval_ctx( {} , const_cast<Rule::RuleMatch&>(m) , rsrcs , cbs , cbd ) ;                                                              // cannot lazy evaluate w/o a job
		}
	protected :
		Py::Ptr<> _eval_code( Job , Rule::RuleMatch      &/*lazy*/ , ::vmap_ss const& rsrcs={} , ::vmap_s<DepDigest>* deps=nullptr ) const ;
		Py::Ptr<> _eval_code(       Rule::RuleMatch const& m       , ::vmap_ss const& rsrcs={} , ::vmap_s<DepDigest>* deps=nullptr ) const {
			return _eval_code( {} , const_cast<Rule::RuleMatch&>(m) , rsrcs , deps ) ;                                                                 // cannot lazy evaluate w/o a job
		}
	public :
		T spec ; // contains default values when code does not provide the necessary entries
	} ;

	struct DynDepsAttrs : Dyn<DepsAttrs> {
		using Base = Dyn<DepsAttrs> ;
		// cxtors & casts
		using Base::Base ;
		// services
		::pair_s</*msg*/::vmap_s<DepSpec>> eval     (Rule::RuleMatch const&  ) const ;
		/**/            ::vmap_s<DepSpec>  dep_specs(Rule::RuleMatch const& m) const ;
	} ;

	struct DynCmd : Dyn<Cmd> {
		using Base = Dyn<Cmd> ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		bool has_entry() const = delete ; // always true for Cmd's, should not ask
		// services
		// use_script is set to true if result is too large and used to generate cmd
		::string eval( bool&/*inout*/ use_script , Rule::RuleMatch const& , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps , StartCmdAttrs const& ) const ;
	} ;

	struct TargetPattern {
		// services
		Re::Match match(::string const& t,Bool3 chk_psfx=Yes) const { return re.match(t,chk_psfx) ; } //chk_psfx=Maybe means check size only
		// data
		Re::RegExpr        re     ;
		::vector<uint32_t> groups ;                                                                   // indexed by stem index, provide the corresponding group number in pattern
		::string           txt    ;                                                                   // human readable pattern
	} ;

	struct RuleData {
		friend ::string& operator+=( ::string& , RuleData const& ) ;
		friend Rule ;
		static constexpr char   JobMrkr =  0          ;                // ensure no ambiguity between job names and node names
		static constexpr VarIdx NoVar   = Rule::NoVar ;
		// START_OF_VERSIONING
		using Prio = double ;
		// END_OF_VERSIONING
		struct MatchEntry {
			// services
			void set_pattern( ::string&&        , VarIdx n_stems ) ;
			void set_pattern( ::string const& p , VarIdx n_stems ) { set_pattern(::copy(p),n_stems) ; }
			// data
			// START_OF_VERSIONING
			::string         pattern   = {} ;
			MatchFlags       flags     = {} ;
			::vector<VarIdx> conflicts = {} ;                          // for target only, the idx of the previous targets that may conflict with this one
			::vector<VarIdx> ref_cnts  = {} ;                          // indexed by stem, number of times stem is referenced
			// END_OF_VERSIONING
		} ;
		// cxtors & casts
		RuleData(                                        ) = default ;
		RuleData( Special , ::string const& src_dir_s={} ) ;           // src_dir in case Special is SrcDir
		RuleData(::string_view str) {
			serdes(str) ;
		}
		RuleData( RulesBase& rules , Py::Dict const& dct ) {
			_acquire_py( rules , dct ) ;
			_set_crcs  ( rules       ) ;
		}
		template<IsStream S> void serdes (S&) ;
		/**/                 void compile(  ) ;
	private :
		void _acquire_py( RulesBase& , Py::Dict const& ) ;
	public :
		::string pretty_str() const ;
		// accesses
		bool   operator+   (         ) const {                    return !special                          ; }
		bool   is_special  (         ) const {                    return special!=Special::Plain           ; }
		bool   user_defined(         ) const {                    return !allow_ext                        ; }                                  // used to decide to print in LMAKE/rules
		Tflags tflags      (VarIdx ti) const { SWEAR(ti!=NoVar) ; return matches[ti].second.flags.tflags() ; }
		//
		::span<::pair_ss const> static_stems() const { return ::span<::pair_ss const>(stems).subspan(0,n_static_stems) ; }
		//
		::string user_name() const {
			::string res = name ; if (+sub_repo_s) res <<':'<< no_slash(sub_repo_s) ;
			return mk_printable(res) ;
		}
		Disk::FileNameIdx job_sfx_len(                ) const ;
		::string          job_sfx    (                ) const ;
		void              validate   (::string job_sfx) const ;
		// services
		::string add_cwd( ::string&& file , bool top=false ) const {
			if ( !top && +sub_repo_s ) return Disk::mk_glb(file,sub_repo_s) ;
			else                       return ::move(file)                  ;
		}
		//
		::string gen_py_line( Job , Rule::RuleMatch      &/*lazy*/ , VarCmd    , VarIdx   , ::string const& key , ::string const& val ) const ;
		::string gen_py_line(       Rule::RuleMatch const& m       , VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) const { // cannot lazy evaluate w/o a job
			return gen_py_line( {} , const_cast<Rule::RuleMatch&>(m) , vc , i , key , val ) ;
		}
		void        new_job_report( Delay exec_time , CoarseDelay cost , Tokens1 tokens1 ) const ;
		CoarseDelay cost          (                                                      ) const ;
	private :
		::vector_s    _list_ctx  ( ::vector<CmdIdx> const& ctx       ) const ;
		void          _set_crcs  ( RulesBase        const&           ) ;
		TargetPattern _mk_pattern( MatchEntry const& , bool for_name ) const ;
		//
		/**/              ::string _pretty_fstr   (::string const& fstr) const ;
		template<class T> ::string _pretty_dyn    (Dyn<T>   const&     ) const ;
		/**/              ::string _pretty_matches(                    ) const ;
		/**/              ::string _pretty_deps   (                    ) const ;
		/**/              ::string _pretty_env    (                    ) const ;
		// START_OF_VERSIONING
		// user data
	public :
		Special              special    = Special::None ;
		Prio                 user_prio  = 0             ;                          // the priority of the rule as specified by user
		RuleIdx              prio       = 0             ;                          // the relative priority of the rule
		::string             name       ;                                          // the short message associated with the rule
		::vmap_ss            stems      ;                                          // stems are ordered : statics then stars, stems used as both static and star appear twice
		::string             sub_repo_s ;                                          // sub_repo which this rule belongs to
		::string             job_name   ;                                          // used to show in user messages (not all fields are actually used)
		::vmap_s<MatchEntry> matches    ;                                          // keep star user order, static entries first in MatchKind order
		VarIdx               stdout_idx = NoVar         ;                          // index of target used as stdout
		VarIdx               stdin_idx  = NoVar         ;                          // index of dep used as stdin
		bool                 allow_ext  = false         ;                          // if true <=> rule may match outside repo
		// following is only if plain rules
		DynDepsAttrs              deps_attrs             ;                         // in match crc, evaluated at job creation time
		Dyn<SubmitRsrcsAttrs    > submit_rsrcs_attrs     ;                         // in rsrcs crc, evaluated at submit time
		Dyn<SubmitAncillaryAttrs> submit_ancillary_attrs ;                         // in no    crc, evaluated at submit time
		Dyn<StartCmdAttrs       > start_cmd_attrs        ;                         // in cmd   crc, evaluated before execution
		Dyn<StartRsrcsAttrs     > start_rsrcs_attrs      ;                         // in rsrcs crc, evaluated before execution
		Dyn<StartAncillaryAttrs > start_ancillary_attrs  ;                         // in no    crc, evaluated before execution
		DynCmd                    cmd                    ;                         // in cmd   crc, evaluated before execution
		bool                      is_python              = false ;
		bool                      force                  = false ;
		uint8_t                   n_losts                = 0     ;                 // max number of times a job can be lost
		uint8_t                   n_submits              = 0     ;                 // max number of times a job can be submitted (except losts & retries), 0 = infinity
		// derived data
		::vector<uint32_t> stem_mark_cnts   ;                                      // number of capturing groups within each stem
		RuleCrc            crc              ;
		VarIdx             n_static_stems   = 0 ;
		VarIdx             n_static_targets = 0 ;                                  // number of official static targets
		VarIdx             n_statics        = 0 ;
		// stats
		mutable Delay    cost_per_token = {} ;                                     // average cost per token
		mutable Delay    exec_time      = {} ;                                     // average exec_time
		mutable uint64_t tokens1_32     = 0  ; static_assert(sizeof(Tokens1)<=4) ; // average number of tokens1 <<32
		mutable JobIdx   stats_weight   = 0  ;                                     // number of jobs used to compute average cost_per_token and exec_time
		// END_OF_VERSIONING
		//
		// not stored on disk
		/**/     TargetPattern  job_name_pattern ;
		::vector<TargetPattern> patterns         ;
	} ;

	struct RuleCrcData {
		friend ::string& operator+=( ::string& , RuleCrcData const& ) ;
		using State = RuleCrcState ;
		// data
		// START_OF_VERSIONING
		Crc   match ;
		Crc   cmd   ;
		Crc   rsrcs ;
		Rule  rule  = {}            ; // rule associated with match
		State state = State::CmdOld ;
		// END_OF_VERSIONING
	} ;

	// RuleMatch does not call python and only provides services that can be served with this constraint
	struct Rule::RuleMatch {
		friend ::string& operator+=( ::string& , RuleMatch const& ) ;
		// cxtors & casts
	public :
		RuleMatch(                                        ) = default ;
		RuleMatch( Rule    r , ::vector_s const& ss       ) : rule{r} , stems{ss} {}
		RuleMatch( Job                                    ) ;
		RuleMatch( Rule    r , ::string   const& job_name , Bool3 chk_psfx=Yes ) : RuleMatch{r,r->job_name_pattern,job_name,chk_psfx} {} // chk_psfx=Maybe means check size only
		RuleMatch( RuleTgt   , ::string   const& target   , Bool3 chk_psfx=Yes ) ;                                                       // .
	private :
		RuleMatch( Rule , TargetPattern const& , ::string const& , Bool3 chk_psfx=Yes ) ;                                                // .
	public :
		bool operator==(RuleMatch const&) const = default ;
		bool operator+ (                ) const { return +rule ; }
		// accesses
		::vector_s star_patterns () const ;
		::vector_s py_matches    () const ;
		::vector_s static_targets() const ;
		::vector_s star_targets  () const ;
		//
		::vmap_s<DepSpec> const& deps_holes() const {
			if (!_has_deps) {
				_deps     = rule->deps_attrs.eval(self).second ;                                                                         // this includes empty slots
				_has_deps = true                               ;
			}
			return _deps ;
		}
		// services
		::pair_ss    full_name  () const ;
		::string     name       () const { return full_name().first ; }
		::uset<Node> target_dirs() const ;
		// data
		Rule       rule  ;
		::vector_s stems ;                                                                                                               // static stems only of course
		// cache
	private :
		mutable bool              _has_deps = false ;
		mutable ::vmap_s<DepSpec> _deps     ;
	} ;

	struct RuleTgt : RuleCrc {
		friend ::string& operator+=( ::string& , RuleTgt const ) ;
		using Rep = Uint< NBits<RuleCrc> + NBits<VarIdx> > ;
		//cxtors & casts
		RuleTgt(                        ) = default ;
		RuleTgt( RuleCrc rc , VarIdx ti ) : RuleCrc{rc} , tgt_idx{ti} {}
		// accesses
		Rep                operator+   (              ) const { return (+RuleCrc(self)<<NBits<VarIdx>) | tgt_idx  ; }
		bool               operator==  (RuleTgt const&) const = default ;
		::partial_ordering operator<=> (RuleTgt const&) const = default ;
		//
		::pair_s<RuleData::MatchEntry> const& key_matches () const { SWEAR(+self->rule)             ; return self->rule->matches [tgt_idx]  ; }
		TargetPattern                  const& pattern     () const { SWEAR(+self->rule)             ; return self->rule->patterns[tgt_idx]  ; }
		::string                       const& key         () const {                                  return key_matches().first            ; }
		RuleData::MatchEntry           const& matches     () const {                                  return key_matches().second           ; }
		::string                       const& target      () const { SWEAR(tflags()[Tflag::Target]) ; return matches().pattern              ; }
		Tflags                                tflags      () const {                                  return matches().flags.tflags      () ; }
		ExtraTflags                           extra_tflags() const {                                  return matches().flags.extra_tflags() ; }
		// services
		bool sure() const {
			Rule       r  = self->rule      ; if (!r) return false ;
			MatchFlags mf = matches().flags ;
			return ( tgt_idx<r->n_static_targets && !mf.extra_tflags()[ExtraTflag::Optional] ) || mf.tflags()[Tflag::Phony] ;
		}
		size_t hash() const {         // use FNV-32, easy, fast and good enough, use 32 bits as we are mostly interested by lsb's
			size_t res = 0x811c9dc5 ;
			res = (res^+RuleCrc(self)) * 0x01000193 ;
			res = (res^ tgt_idx      ) * 0x01000193 ;
			return res ;
		}
		// data
		VarIdx tgt_idx = 0 ;
	} ;

}

#endif
#ifdef IMPL

namespace Engine {

	//
	// Attrs
	//

	namespace Attrs {

		template<::integral I> bool/*updated*/ acquire( I& dst , Py::Object const* py_src , I min , I max ) {
			if (!py_src          ) {           return false/*updated*/ ; }
			if (*py_src==Py::None) { dst = 0 ; return true /*updated*/ ; }
			//
			long v = py_src->as_a<Py::Int>() ;
			throw_if( ::cmp_less   (v,min) , "underflow" ) ;
			throw_if( ::cmp_greater(v,max) , "overflow"  ) ;
			dst = I(v) ;
			return true ;
		}

		template<StdEnum E> bool/*updated*/ acquire( E& dst , Py::Object const* py_src ) {
			if (!py_src          ) {                 return false/*updated*/ ; }
			if (*py_src==Py::None) { dst = E::Dflt ; return true /*updated*/ ; }
			//
			dst = mk_enum<E>(py_src->as_a<Py::Str>()) ;
			return true ;
		}

		template<bool Env> bool/*updated*/ acquire( ::string& dst , Py::Object const* py_src ) {
			if      ( !py_src                       )             return false/*updated*/ ;
			else if (  Env && *py_src==Py::None     )                                       dst = EnvDynMrkr     ;   // special case environment variable to mark dynamic values
			else if ( !Env && *py_src==Py::None     ) { if (!dst) return false/*updated*/ ; dst = {}             ; }
			else if ( !Env && *py_src==Py::Ellipsis )                                       dst = "..."          ;
			else                                                                            dst = *py_src->str() ;
			return true ;
		}

		template<class T,bool Env> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vector<T>& dst , Py::Object const* py_src ) {
			if (!py_src          )             return false/*updated*/ ;
			if (*py_src==Py::None) { if (!dst) return false/*updated*/ ; dst = {} ; return true/*update*/ ; }
			//
			bool   updated = false      ;
			size_t n       = dst.size() ;
			size_t i       = 0          ;
			auto handle_entry = [&](Py::Object const& py_item)->void {
				if (py_item==Py::None) return ;
				try {
					if constexpr (Env) updated |= acquire<Env>(grow(dst,i++),&py_item) ; // special case for environment where we replace occurrences of lmake & repo roots by markers ...
					else               updated |= acquire     (grow(dst,i++),&py_item) ; // ... to make repo robust to moves of lmake or itself
				} catch (::string const& e) {
					throw "for item "s+i+" : "+e ;
				}
			} ;
			if (py_src->is_a<Py::Sequence>()) {
				Py::Sequence const& py_seq = py_src->as_a<Py::Sequence>() ;
				dst.reserve(py_seq.size()) ;
				for( Py::Object const& py_item : py_seq ) handle_entry(py_item) ;
			} else {
				handle_entry(*py_src) ;
			}
			if (i!=n) {
				updated = true ;
				dst.resize(i) ;
			}
			return updated ;
		}

		template<class T,bool Env> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vmap_s<T>& dst , Py::Object const* py_src ) {
			if (!py_src          )             return false/*updated*/ ;
			if (*py_src==Py::None) { if (!dst) return false/*updated*/ ; dst = {} ; return true/*updated*/ ; }
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
	// Dyn
	//

	inline DynEntry const& DynBase::entry (RulesBase const& rs) const { SWEAR(has_entry()) ; return rs.dyn_vec[dyn_idx-1]                       ; } // dyn_idx 0 is reserved to mean non-dynamic
	inline DynEntry      & DynBase::entry (RulesBase      & rs) const { SWEAR(has_entry()) ; return rs.dyn_vec[dyn_idx-1]                       ; } // .
	inline DynEntry const& DynBase::entry (                   ) const {                      return entry (*Rule::s_rules)                      ; }
	inline bool            DynBase::is_dyn(RulesBase const& rs) const {                      return has_entry() && entry(rs).kind>=DynKind::Dyn ; } // for Cmd, static code is stored in DynEntry
	inline bool            DynBase::is_dyn(                   ) const {                      return is_dyn(*Rule::s_rules)                      ; }

	template<IsStream S> void DynEntry::serdes( S& s , RulesBase const* rs ) {
		static constexpr bool IsHash = Hash::IsHash<S> ;
		Trace trace("DynEntry::serdes",STR(IsIStream<S>)) ;
		// START_OF_VERSIONING
		Kind     kind_ ;
		::string buf   ;
		if (!IsIStream<S>) {
			kind_ = kind ;
			if (kind_==Kind::CompiledGlbs)
				try                     { buf   = serialize(glbs) ; }
				catch (::string const&) { kind_ = Kind::Compiled  ; }
		}
		::serdes(s,kind_) ;
		if (IsIStream<S>) {
			kind = kind_ ;
		}
		::serdes(s,ctx ) ;
		switch (kind_) {
			case Kind::None         :                                                                                                                 break ;
			case Kind::ShellCmd     : ::serdes(s,code_str) ;                                                                                          break ;
			case Kind::PythonCmd    : ::serdes(s,code_str) ; ::serdes(s,glbs_str ) ; { if (!IsHash) ::serdes(s,dbg_info) ; }                          break ; // dbg_info is not hashed as it no ...
			case Kind::Dyn          : ::serdes(s,code_str) ; ::serdes(s,glbs_str ) ; { if (!IsHash) ::serdes(s,dbg_info) ; } ::serdes(s,may_import) ; break ; // ... semantic value
			case Kind::Compiled     : ::serdes(s,code    ) ; ::serdes(s,glbs_code) ;                                       ; ::serdes(s,may_import) ; break ;
			case Kind::CompiledGlbs : ::serdes(s,code    ) ; ::serdes(s,buf      ) ;                                       ; ::serdes(s,may_import) ; break ; // buf is marshaled info
		}
		if constexpr (IsHash) {
			if ( kind_>=Kind::Dyn && +may_import ) { SWEAR(rs) ; ::serdes(s,rs->sys_path_crc) ; }
		} else if (IsIStream<S>) {
			if      (kind_==Kind::Compiled    ) { SWEAR(rs) ; glbs = glbs_code->run( nullptr/*glbs*/ , rs->py_sys_path ) ; kind = Kind::CompiledGlbs ; }
			else if (kind_==Kind::CompiledGlbs) { deserialize(buf,glbs) ;                                                                              }
		}
		// END_OF_VERSIONING
		trace("done",kind,kind_) ;
	}

	template<class Spec> concept SpecHasUpdate = requires( Spec spec , Py::Dict dct ) { spec.update(dct) ; } ;
	template<class T> Dyn<T>::Dyn( RulesBase& rules , Py::Dict const& py_src , ::umap_s<CmdIdx> const& var_idxs , RuleData const& rd ) {
		Py::Ptr<>  py_update ;
		Py::Ptr<>* pu        = SpecHasUpdate<T> ? &py_update : nullptr ;
		//
		static_cast<DynBase&>(self) = DynBase( /*out*/pu , rules , py_src , var_idxs , ::is_same_v<T,Cmd>?No|rd.is_python:Maybe ) ;
		if (py_src.contains("static") )                     spec.init  ( &py_src["static"].as_a<Py::Dict>() , var_idxs , rd ) ;
		if constexpr (SpecHasUpdate<T>) { if   (+py_update) spec.update( py_update->as_a<Py::Dict>()                        ) ; }
		else                              SWEAR(!py_update) ;                                                                     // if we have no update, we'd better have nothing to update
	}

	template<class T> void Dyn<T>::eval_ctx( Job job , Rule::RuleMatch& match_ , ::vmap_ss const& rsrcs_ , EvalCtxFuncStr const& cb_str , EvalCtxFuncDct const& cb_dct ) const {
		DynBase::s_eval( job , match_ , rsrcs_ , entry().ctx , cb_str , cb_dct ) ;
	}

	template<class T> Py::Ptr<> Dyn<T>::_eval_code( Job job , Rule::RuleMatch& match , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps ) const {
		// functions defined in glbs use glbs as their global dict (which is stored in the code object of the functions), so glbs must be modified in place or the job-related values will not
		// be seen by these functions, which is the whole purpose of such dynamic values
		Rule       r       = +match ? match.rule : job->rule() ;
		::vector_s to_del  ;
		::string   to_eval ;
		//
		SWEAR(!Rule::s_last_dyn_date,Rule::s_last_dyn_date) ;
		Rule::s_last_dyn_job  = job    ;
		Rule::s_last_dyn_msg  = T::Msg ;
		Save<Atomic<Pdate>> sav_last_dyn_date { Rule::s_last_dyn_date , New } ;
		//
		Py::Ptr<Py::Dict> tmp_glbs = entry().glbs ;                        // use a modifyable copy as we restore after use
		eval_ctx( job , match , rsrcs
		,	[&]( VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) -> void {
				to_del.push_back(key) ;
				if (vc!=VarCmd::StarMatch) tmp_glbs->set_item(key,*Py::Ptr<Py::Str>(val)) ;
				else                       to_eval += r->gen_py_line( job , match , vc , i , key , val ) ;
			}
		,	[&]( VarCmd , VarIdx , ::string const& key , ::vmap_ss const& val ) -> void {
				to_del.push_back(key) ;
				Py::Ptr<Py::Dict> py_dct { New } ;
				for( auto const& [k,v] : val ) py_dct->set_item(k,*Py::Ptr<Py::Str>(v)) ;
				tmp_glbs->set_item(key,*py_dct) ;
			}
		) ;
		Py::py_run(to_eval,tmp_glbs) ;
		g_kpi.py_exec_time += Pdate(New) - Rule::s_last_dyn_date ;
		Py::Ptr<>   res      ;
		::string    err      ;
		bool        seen_err = false  ;
		AutodepLock lock     { deps } ;                                    // ensure waiting for lock is not accounted as python exec time
		Rule::s_last_dyn_date = Pdate(New) ;
		//                                vvvvvvvvvvvvvvvvvvvvvvvvvvvv
		try                       { res = entry().code->eval(tmp_glbs) ; }
		//                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		catch (::string const& e) { err = e ; seen_err = true ;          }
		for( ::string const& key : to_del ) tmp_glbs->del_item(key) ;      // delete job-related info, just to avoid percolation to other jobs, even in case of error
		g_kpi.py_exec_time += Pdate(New) - Rule::s_last_dyn_date ;
		if ( +lock.err || seen_err ) throw MsgStderr{.msg=lock.err,.stderr=err} ;
		return res ;
	}

	template<class T> T Dyn<T>::eval( Job job , Rule::RuleMatch& match , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps ) const {
		T res = spec ;
		if (is_dyn()) {
			Py::Gil   gil    ;
			Py::Ptr<> py_obj = _eval_code( job , match , rsrcs , deps ) ;
			if (*py_obj!=Py::None) res.update(py_obj->as_a<Py::Dict>()) ;
		}
		return res ;
	}

	//
	// RuleData
	//

	// START_OF_VERSIONING
	template<IsStream S> void RuleData::serdes(S& s) {
		::serdes(s,special         ) ;
		::serdes(s,user_prio       ) ;
		::serdes(s,prio            ) ;
		::serdes(s,name            ) ;
		::serdes(s,stems           ) ;
		::serdes(s,sub_repo_s      ) ;
		::serdes(s,job_name        ) ;
		::serdes(s,matches         ) ;
		::serdes(s,stdout_idx      ) ;
		::serdes(s,stdin_idx       ) ;
		::serdes(s,allow_ext       ) ;
		::serdes(s,stem_mark_cnts  ) ;
		::serdes(s,crc             ) ;
		::serdes(s,n_static_stems  ) ;
		::serdes(s,n_static_targets) ;
		::serdes(s,n_statics       ) ;
		if (special==Special::Plain) {
			::serdes(s,deps_attrs            ) ;
			::serdes(s,submit_rsrcs_attrs    ) ;
			::serdes(s,submit_ancillary_attrs) ;
			::serdes(s,start_cmd_attrs       ) ;
			::serdes(s,start_rsrcs_attrs     ) ;
			::serdes(s,start_ancillary_attrs ) ;
			::serdes(s,cmd                   ) ;
			::serdes(s,is_python             ) ;
			::serdes(s,force                 ) ;
			::serdes(s,n_losts               ) ;
			::serdes(s,n_submits             ) ;
			::serdes(s,cost_per_token        ) ;
			::serdes(s,exec_time             ) ;
			::serdes(s,stats_weight          ) ;
		}
	}
	inline Disk::FileNameIdx RuleData::job_sfx_len() const {
		return
			1                                            // JobMrkr to disambiguate w/ Node names
		+	n_static_stems * sizeof(Disk::FileNameIdx)*2 // pos+len for each stem
		+	sizeof(Crc::Val)                             // Rule
		;
	}
	inline ::string RuleData::job_sfx() const {
		::string res( job_sfx_len() , JobMrkr ) ;
		encode_int( &res[res.size()-sizeof(Crc::Val)] , +crc->match ) ;
		return res ;
	}
	// END_OF_VERSIONING
	inline void RuleData::validate(::string job_sfx) const {
		Crc crc_ { decode_int<Crc::Val>(&job_sfx[job_sfx.size()-sizeof(Crc::Val)]) } ;
		SWEAR( crc_==crc->match , name , sub_repo_s , crc_ , crc->match ) ;
	}

}

#endif
