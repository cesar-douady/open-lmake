// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 5 times, successively with following macros defined : STRUCT_DECL, STRUCT_DEF, INFO_DEF, DATA_DEF, IMPL

#include "re.hh"

#include "rpc_job.hh"

#include "autodep/ld_server.hh"

#include "store/prefix.hh"
#include "store/struct.hh"
#include "store/vector.hh"

#ifdef STRUCT_DECL

// START_OF_VERSIONING REPO

enum class DynImport : uint8_t {
	Static                       // may import when computing glbs
,	Dyn                          // may import when executing code
} ;

enum class DynKind : uint8_t {
	None
,	ShellCmd     // static shell cmd
,	PythonCmd    // python cmd (necessarily static)
,	Dyn          // dynamic  code, not compiled
,	Compiled     // compiled code, glbs not computed
,	CompiledGlbs // compiled code, glbs     computed
} ;

enum class EnvFlag : uint8_t {
	None                       // ignore variable
,	Rsrc                       // consider variable as a resource : upon modification, rebuild job if it was in error
,	Cmd                        // consider variable as a cmd      : upon modification, rebuild job
//
// aliases
,	Dflt = Rsrc
} ;

enum class RuleCrcState : uint8_t {
	Ok
,	RsrcsOld
,	RsrcsForgotten   // when rsrcs are forgotten (and rsrcs were old), process as if cmd changed (i.e. always rerun)
,	CmdOld
//
// aliases
,	CmdOk = RsrcsOld // <=CmdOk means no need to run job because of cmd
} ;

enum class Special : uint8_t {
	None                       // value 0 reserved to mean not initialized
,	Dep                        // used for synthetized jobs when asking for direct dep
,	Req                        // used for synthetized jobs representing a Req
,	InfiniteDep
,	InfinitePath
,	Codec
,	Plain
// ordered by decreasing matching priority within each prio
,	Anti
,	GenericSrc
//
// aliases
,	NUniq      = Plain         // < NUniq      means there is a single such rule
,	HasJobs    = Plain         // <=HasJobs    means jobs can refer to this rule
,	Fugitive   = InfinitePath  // <=Fugitive   means job is not kept permanently
,	HasMatches = Codec         // >=HasMatches means rules can get jobs by matching
,	HasTargets = InfiniteDep   // >=HasTargets means targets field exists
} ;
inline bool is_infinite(Special s) { return s==Special::InfiniteDep || s==Special::InfinitePath ; }

enum class VarCmd : uint8_t {
	Stems   , Stem
,	Targets , Match , StarMatch
,	Deps    , Dep
,	Rsrcs   , Rsrc
,	JobName
} ;

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
		struct RuleMatch ;
		//
		static constexpr char   StarMrkr =  0 ; // signal a star stem in job_name
		static constexpr char   StemMrkr =  0 ; // signal a stem in job_name & targets & deps & cmd
		static constexpr VarIdx NoVar    = -1 ;
		// statics
		static bool/*keep*/    s_qualify_dep( ::string const& key , ::string const& dep                                                              ) ;
		static ::string/*msg*/ s_reject_msg ( MatchKind mk , ::string const& file , bool has_pfx=false , bool has_sfx=false                          ) ;
		static ::string        s_split_flags( ::string const& key , Py::Object const& py , uint8_t n_skip , MatchFlags&/*out*/ flags , bool dep_only ) ;
		// static data
		static Atomic<Pdate> s_last_dyn_date ;  // used to check dynamic attribute computation does not take too long
		static Job           s_last_dyn_job  ;
		static const char*   s_last_dyn_msg  ;
		static Rule          s_last_dyn_rule ;
		// cxtors & casts
		using RuleBase::RuleBase ;
		Rule(RuleBase const& rb) : RuleBase{rb} {}
		// accesses
		void operator>>(::string&) const ;
	} ;

	struct RuleCrc : RuleCrcBase {
		// cxtors & casts
		using RuleCrcBase::RuleCrcBase ;
		// accesses
		void operator>>(::string&) const ;
	} ;

}

#endif
#ifdef DATA_DEF

namespace Engine {

	using IsDynVector = ::pair<bool,::vector<uint8_t>> ; // cannot use stupid std::vector<bool>
	using IsDynMap    = ::pair<bool,::uset_s         > ;
	namespace Attrs {
		/**/                   void acquire( bool               &/*out*/ dst , bool&/*out*/ is_dyn , Py::Object const* py_src                                                       ) ;
		/**/                   void acquire( Delay              &/*.  */ dst , bool&/*.  */ is_dyn , Py::Object const* py_src , Delay min=Delay::Lowest , Delay max=Delay::Highest  ) ;
		/**/                   void acquire( JobSpace::ViewDescr&/*.  */ dst , bool&/*.  */ is_dyn , Py::Object const* py_src                                                       ) ;
		/**/                   void acquire( Zlvl               &/*.  */ dst , bool&/*.  */ is_dyn , Py::Object const* py_src                                                       ) ;
		template<::integral I> void acquire( I                  &/*.  */ dst , bool&/*.  */ is_dyn , Py::Object const* py_src , I     min=Min<I>        , I     max=Max<I>          ) ;
		template<UEnum      E> void acquire( E                  &/*.  */ dst , bool&/*.  */ is_dyn , Py::Object const* py_src ,                     BitMap<E> accepted=~BitMap<E>() ) ;
		template<UEnum      E> void acquire( BitMap<E>          &/*.  */ dst , bool&/*.  */ is_dyn , Py::Object const* py_src , BitMap<E> dflt={} , BitMap<E> accepted=~BitMap<E>() ) ;
		//
		template<        bool Env=false> void acquire( ::string   &/*out*/ dst , bool       &/*out*/ is_dyn , Py::Object const* py_src ) ;
		template<class T               > void acquire( ::vector<T>&/*.  */ dst , IsDynVector&/*.  */ is_dyn , Py::Object const* py_src ) ;
		template<class T,bool Env=false> void acquire( ::vmap_s<T>&/*.  */ dst , IsDynMap   &/*.  */ is_dyn , Py::Object const* py_src ) ;
		//
		template<class T,class D,class... A> void acquire_from_dct( T&/*out*/ dst , D&/*out*/ is_dyn , Py::Dict const& py_dct , ::string const& key , A const&... args ) {
			if (py_dct.contains(key))
				try                       { acquire( /*out*/dst , /*out*/is_dyn , &py_dct[key] , args... ) ; }
				catch (::string const& e) { throw cat("while processing ",key," : ",e) ;                     }
		}
		template<class T,bool Env=false> void acquire_from_dct( ::vmap_s<T>&/*out*/ dst , IsDynMap&/*out*/ is_dyn , Py::Dict const& py_dct , ::string const& key ) {
			static_assert(!Env||::is_same_v<T,::string>) ;
			if (py_dct.contains(key))
				try                       { acquire<T,Env>( /*out*/dst , /*out*/is_dyn , &py_dct[key] ) ; }
				catch (::string const& e) { throw cat("while processing ",key," : ",e) ;                  }
		}
		inline void acquire_env( ::vmap_ss&/*out*/ dst , IsDynMap&/*out*/ is_dyn , Py::Dict const& py_dct , ::string const& key ) {
			acquire_from_dct<::string,true/*Env*/>( /*out*/dst , /*out*/is_dyn , py_dct , key ) ;
		}
	}

	struct DepSpec {
		// accesses
		void operator>>(::string&) const ;
		// data
		::string    txt          ;
		Dflags      dflags       ;
		ExtraDflags extra_dflags ;
	} ;

	// used at match time
	struct DepsAttrs {
		static constexpr const char* Msg = "deps" ;
		// cxtors & casts
		void init( Py::Object const& , ::umap_s<CmdIdx> const& , RuleData const& ) ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , deps,dyn_deps.first ) ; // dyn_deps.first is used algorithmically, not for pretty print only
		}
		// data
		// START_OF_VERSIONING REPO
		::vmap_s<DepSpec> deps ; IsDynMap dyn_deps = {} ;
		// END_OF_VERSIONING
	} ;

	// used at submit time, participate in resources
	struct SubmitRsrcsAttrs {
		static constexpr const char* Msg = "submit resources attributes" ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , backend ) ;
			::serdes( s , rsrcs   ) ;
		}
		void update(Py::Dict const& py_dct) {
			Attrs::acquire_from_dct( backend , dyn_backend , py_dct , "backend" ) ;
			Attrs::acquire_from_dct( rsrcs   , dyn_rsrcs   , py_dct , "rsrcs"   ) ;
		}
		Tokens1 tokens1() const {
			for( auto const& [k,v] : rsrcs ) if (k=="cpu")
				try                     { return ::min( ::max(from_string<uint32_t>(v),uint32_t(1))-1 , uint32_t(Max<Tokens1>) ); }
				catch (::string const&) { break ;                                                                                 } // no valid cpu count, do as if no cpu found
			return 0 ;                                                                                                              // not found : default to 1 cpu
		}
		// data
		// START_OF_VERSIONING REPO
		BackendTag backend = BackendTag::Local ; bool     dyn_backend = false ;                                                     // backend to use to launch jobs
		::vmap_ss  rsrcs   ;                     IsDynMap dyn_rsrcs   = {}    ;
		// END_OF_VERSIONING
	} ;

	// used both at submit time (for cache look up) and at end of execution (for cache upload)
	struct SubmitAncillaryAttrs {
		static constexpr const char* Msg = "cache key" ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , cache_name ) ;
		}
		void update( Py::Dict const& py_dct ) {
			Attrs::acquire_from_dct( cache_name , dyn_cache_name , py_dct , "cache" ) ;
			throw_unless( !cache_name || g_config->cache_idxes.contains(cache_name) , "unexpected cache ",cache_name," not found in config" ) ;
		}
		// data
		// START_OF_VERSIONING REPO
		::string cache_name ; bool dyn_cache_name = false ;
		// END_OF_VERSIONING
	} ;

	// used at start time, participate in cmd
	struct StartCmdAttrs {
		static constexpr const char* Msg = "execution command attributes" ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , chroot_dir_s    ) ;
			::serdes( s , auto_mkdir      ) ;
			::serdes( s , env             ) ;
			::serdes( s , ignore_stat     ) ;
			::serdes( s , interpreter     ) ;
			::serdes( s , mount_chroot_ok ) ;
			::serdes( s , job_space       ) ;
		}
		void update( Py::Dict const& py_dct ) {
			using namespace Attrs ;
			Attrs::acquire_from_dct( auto_mkdir             , dyn_auto_mkdir      , py_dct , "auto_mkdir"      ) ;
			Attrs::acquire_from_dct( chroot_dir_s           , dyn_chroot_dir_s    , py_dct , "chroot_dir"      ) ; if (+chroot_dir_s) add_slash(chroot_dir_s) ;
			Attrs::acquire_env     ( env                    , dyn_env             , py_dct , "env"             ) ;
			Attrs::acquire_from_dct( ignore_stat            , dyn_ignore_stat     , py_dct , "ignore_stat"     ) ;
			Attrs::acquire_from_dct( interpreter            , dyn_interpreter     , py_dct , "interpreter"     ) ;
			Attrs::acquire_from_dct( mount_chroot_ok        , dyn_mount_chroot_ok , py_dct , "mount_chroot_ok" ) ;
			Attrs::acquire_from_dct( job_space.lmake_view_s , dyn_lmake_view_s    , py_dct , "lmake_view"      ) ; if (+job_space.lmake_view_s) add_slash(job_space.lmake_view_s) ;
			Attrs::acquire_from_dct( job_space.repo_view_s  , dyn_repo_view_s     , py_dct , "repo_view"       ) ; if (+job_space.repo_view_s ) add_slash(job_space.repo_view_s ) ;
			Attrs::acquire_from_dct( job_space.tmp_view_s   , dyn_tmp_view_s      , py_dct , "tmp_view"        ) ; if (+job_space.tmp_view_s  ) add_slash(job_space.tmp_view_s  ) ;
			Attrs::acquire_from_dct( job_space.views        , dyn_views           , py_dct , "views"           ) ; for( auto& [v_s,_] : job_space.views ) add_slash(v_s) ;
		}
		// data
		// START_OF_VERSIONING REPO
		::string   chroot_dir_s    ;         bool        dyn_chroot_dir_s    = false ;
		bool       auto_mkdir      = false ; bool        dyn_auto_mkdir      = false ;
		::vmap_ss  env             ;         IsDynMap    dyn_env             = {}    ;
		bool       ignore_stat     = false ; bool        dyn_ignore_stat     = false ;
		::vector_s interpreter     ;         IsDynVector dyn_interpreter     = {}    ;
		bool       mount_chroot_ok = false ; bool        dyn_mount_chroot_ok = false ;
		JobSpace   job_space       ;         bool        dyn_lmake_view_s    = false ;
		/**/                                 bool        dyn_repo_view_s     = false ;
		/**/                                 bool        dyn_tmp_view_s      = false ;
		/**/                                 IsDynMap    dyn_views           = {}    ;
		// END_OF_VERSIONING
	} ;

	// used at start time, participate in resources
	struct StartRsrcsAttrs {
		static constexpr const char* Msg = "execution resources attributes" ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , chk_abs_paths  ) ;
			::serdes( s , chroot_actions ) ;
			::serdes( s , env            ) ;
			::serdes( s , lmake_root_s   ) ;
			::serdes( s , method         ) ;
			::serdes( s , readdir_ok     ) ;
			::serdes( s , stderr_ok      ) ;
			::serdes( s , timeout        ) ;
			::serdes( s , use_script     ) ;
		}
		void update( Py::Dict const& py_dct ) {
			Attrs::acquire_from_dct( chk_abs_paths  , dyn_chk_abs_paths  , py_dct , "check_abs_paths"                      ) ;
			Attrs::acquire_from_dct( chroot_actions , dyn_chroot_actions , py_dct , "chroot_actions"                       ) ;
			Attrs::acquire_env     ( env            , dyn_env            , py_dct , "env"                                  ) ;
			Attrs::acquire_from_dct( lmake_root_s   , dyn_lmake_root_s   , py_dct , "lmake_root"                           ) ; if (+lmake_root_s) add_slash(lmake_root_s) ;
			Attrs::acquire_from_dct( method         , dyn_method         , py_dct , "autodep"                              ) ;
			Attrs::acquire_from_dct( readdir_ok     , dyn_readdir_ok     , py_dct , "readdir_ok"                           ) ;
			Attrs::acquire_from_dct( stderr_ok      , dyn_stderr_ok      , py_dct , "stderr_ok"                            ) ;
			Attrs::acquire_from_dct( timeout        , dyn_timeout        , py_dct , "timeout"       , Time::Delay()/*min*/ ) ;
			Attrs::acquire_from_dct( use_script     , dyn_use_script     , py_dct , "use_script"                           ) ;
		}
		// data
		// START_OF_VERSIONING REPO
		bool          chk_abs_paths  = false               ; bool     dyn_chk_abs_paths  = false ;
		ChrootActions chroot_actions ;                       bool     dyn_chroot_actions = false ;
		::vmap_ss     env            ;                       IsDynMap dyn_env            = {}    ;
		::string      lmake_root_s   ;                       bool     dyn_lmake_root_s   = false ;
		AutodepMethod method         = AutodepMethod::Dflt ; bool     dyn_method         = false ;
		bool          readdir_ok     = false               ; bool     dyn_readdir_ok     = false ;
		bool          stderr_ok      = false               ; bool     dyn_stderr_ok      = false ;
		Time::Delay   timeout        ;                       bool     dyn_timeout        = false ; // if 0 <=> no timeout, maximum time allocated to job execution in s
		bool          use_script     = false               ; bool     dyn_use_script     = false ;
		// END_OF_VERSIONING
	} ;

	// used at start time, participate to nothing
	struct StartAncillaryAttrs {
		static constexpr const char* Msg = "execution ancillary attributes" ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , env            ) ;
			::serdes( s , keep_tmp       ) ;
			::serdes( s , kill_daemons   ) ;
			::serdes( s , kill_sigs      ) ;
			::serdes( s , max_stderr_len ) ;
			::serdes( s , start_delay    ) ;
			::serdes( s , zlvl           ) ;
		}
		void update( Py::Dict const& py_dct ) {
			using namespace Attrs ;
			Attrs::acquire_env     ( env            , dyn_env            , py_dct , "env"                                   ) ;
			Attrs::acquire_from_dct( keep_tmp       , dyn_keep_tmp       , py_dct , "keep_tmp"                              ) ;
			Attrs::acquire_from_dct( kill_daemons   , dyn_kill_daemons   , py_dct , "kill_daemons"                          ) ;
			Attrs::acquire_from_dct( kill_sigs      , dyn_kill_sigs      , py_dct , "kill_sigs"                             ) ;
			Attrs::acquire_from_dct( max_stderr_len , dyn_max_stderr_len , py_dct , "max_stderr_len"                        ) ;
			Attrs::acquire_from_dct( start_delay    , dyn_start_delay    , py_dct , "start_delay"    , Time::Delay()/*min*/ ) ;
			Attrs::acquire_from_dct( zlvl           , dyn_zlvl           , py_dct , "compression"                           ) ;
		}
		// data
		// START_OF_VERSIONING REPO
		::vmap_ss         env            ;         IsDynMap    dyn_env            = {}    ;
		bool              keep_tmp       = false ; bool        dyn_keep_tmp       = false ;
		bool              kill_daemons   = false ; bool        dyn_kill_daemons   = false ;
		::vector<uint8_t> kill_sigs      ;         IsDynVector dyn_kill_sigs      = {}    ; // signals to use to kill job (tried in sequence, 1s apart from each other)
		uint16_t          max_stderr_len = 0     ; bool        dyn_max_stderr_len = false ; // max lines when displaying stderr, 0 means no limit (full content is shown with lshow -e)
		Time::Delay       start_delay    ;         bool        dyn_start_delay    = false ; // job duration above which a start message is generated
		Zlvl              zlvl           = {}    ; bool        dyn_zlvl           = false ;
		// END_OF_VERSIONING
	} ;

	struct Cmd {
		static constexpr const char* Msg = "execution command" ;
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

	using EvalCtxFuncStr = ::function<void( VarCmd , VarIdx idx , ::string const& key , ::string  const& val )> ;
	using EvalCtxFuncDct = ::function<void( VarCmd ,              ::string const& key , ::vmap_ss const& val )> ;

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
		static ::string s_exc_msg(bool using_static) { return cat("cannot compute dynamic ",T::Msg,using_static?", using static info":"") ; }
		// cxtors & casts
		Dyn() = default ;
		Dyn( RulesBase& , Py::Dict const& , ::umap_s<CmdIdx> const& var_idxs , RuleData const& ) ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,static_cast<DynBase&>(self)) ;
			::serdes(s,spec                       ) ;
		}
		void update_hash( Hash::Xxh&/*inout*/ h , RulesBase const& rs ) const {
			// START_OF_VERSIONING REPO
			/**/             h += spec        ;
			/**/             h += has_entry() ;
			if (has_entry()) const_cast<DynEntry&>(entry(rs)).serdes( /*inout*/h , &rs ) ; // serdes is declared non-const because it is also used for deserializing
			// END_OF_VERSIONING
		}
		// RuleMatch is lazy evaluated from Job (when there is one)
		T eval( Job   , Rule::RuleMatch      &   , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>*     =nullptr ) const ;
		T eval(         Rule::RuleMatch const& m , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps=nullptr ) const { return eval( {} , const_cast<Rule::RuleMatch&>(m) , rsrcs , deps ) ; }
		T eval( Job j , Rule::RuleMatch      & m ,                          ::vmap_s<DepDigest>* deps=nullptr ) const { return eval( j  ,                              m  , {}    , deps ) ; }
		T eval(         Rule::RuleMatch const& m ,                          ::vmap_s<DepDigest>* deps=nullptr ) const { return eval( {} , const_cast<Rule::RuleMatch&>(m) , {}    , deps ) ; }
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

	struct DynStartCmdAttrs : Dyn<StartCmdAttrs> {
		using Base = Dyn<StartCmdAttrs> ;
		// cxtors & casts
		using Base::Base ;
		// services
		StartCmdAttrs eval( Rule::RuleMatch const& , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* =nullptr ) const ;
	} ;

	struct DynCmd : Dyn<Cmd> {
		using Base = Dyn<Cmd> ;
		// cxtors & casts
		using Base::Base ;
		// accesses
		bool has_entry() const = delete ; // always true for Cmd's, should not ask
		// services
		// use_script is set to true if result is too large and used to generate cmd
		::string eval( StartRsrcsAttrs&/*inout*/ , Rule::RuleMatch const& , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps , StartCmdAttrs const& ) const ;
	} ;

	struct TargetPattern : Re::RegExpr {
		using Re::RegExpr::RegExpr ;
		// data
		::vector<uint32_t> groups ; // indexed by stem index, provide the corresponding group number in pattern
		::string           txt    ; // human readable pattern
	} ;

	struct RuleData {                                   // NOLINT(clang-analyzer-optin.performance.Padding) prefer logical order
		friend Rule ;
		static constexpr char   JobMrkr =  0          ; // ensure no ambiguity between job names and node names
		static constexpr VarIdx NoVar   = Rule::NoVar ;
		// START_OF_VERSIONING REPO
		using Prio = double ;
		// END_OF_VERSIONING
		struct MatchEntry {
			// services
			void set_pattern( ::string&&        , VarIdx n_stems ) ;
			void set_pattern( ::string const& p , VarIdx n_stems ) { set_pattern(::copy(p),n_stems) ; }
			// data
			// START_OF_VERSIONING REPO
			::string       pattern  = {} ;
			MatchFlags     flags    = {} ;
			::vector<bool> captures = {} ;              // indexed by stem, true if stem is referenced
			// END_OF_VERSIONING
		} ;
		// cxtors & casts
		RuleData() = default ;
		RuleData(Special          ) ;
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
		void   operator>>  (::string&) const ;
		bool   is_plain    (         ) const {                    return special==Special::Plain         ; }
		bool   user_defined(         ) const {                    return !allow_ext                      ; }                                    // used to decide to print in LMAKE/rules
		Tflags tflags      (VarIdx ti) const { SWEAR(ti!=NoVar) ; return matches[ti].second.flags.tflags ; }
		//
		::span<::pair_ss const> static_stems() const { return ::span<::pair_ss const>(stems).subspan(0,n_static_stems) ; }
		//
		::string user_name() const {
			::string res = name ; if (+sub_repo_s) res <<':'<< sub_repo_s<<rm_slash ;
			return mk_printable(res) ;
		}
		Disk::FileNameIdx job_sfx_len(                       ) const ;
		::string          job_sfx    (                       ) const ;
		void              validate   (::string const& job_sfx) const ;
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
		void        new_job_report( Delay exe_time , CoarseDelay cost , Tokens1 ) const ;
		CoarseDelay cost          (                                             ) const ;
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
		/**/              ::string _pretty_rsrcs  (                    ) const ;
		/**/              ::string _pretty_views  (                    ) const ;
		// START_OF_VERSIONING REPO
		// user data
	public :
		Special              special    = Special::None ;
		Prio                 user_prio  = 0             ;                          // the priority of the rule as specified by user
		RuleIdx              prio       = 0             ;                          // the relative priority of the rule
		::string             name       ;                                          // the short message associated with the rule
		::vmap_ss            stems      ;                                          // stems are ordered : statics then stars, stems used as both static and star appear twice
		::string             sub_repo_s ;                                          // sub_repo which this rule belongs to
		::string             job_name   ;                                          // used to show in user messages (not all fields are actually used)
		::vmap_s<MatchEntry> matches    ;                                          // keep user within each star/MatchKind sequence, targets (static and star) are first to ensure RuleTgt stability
		VarIdx               stdout_idx = NoVar         ;                          // index of target used as stdout
		VarIdx               stdin_idx  = NoVar         ;                          // index of dep used as stdin
		bool                 allow_ext  = false         ;                          // if true <=> rule may match outside repo
		DynDepsAttrs         deps_attrs ;                                          // in match crc, evaluated at job creation time
		// following is only if plain rules
		Dyn<SubmitRsrcsAttrs    > submit_rsrcs_attrs     ;                         // in rsrcs crc, evaluated at submit time
		Dyn<SubmitAncillaryAttrs> submit_ancillary_attrs ;                         // in no    crc, evaluated at submit time
		DynStartCmdAttrs          start_cmd_attrs        ;                         // in cmd   crc, evaluated before execution
		Dyn<StartRsrcsAttrs     > start_rsrcs_attrs      ;                         // in rsrcs crc, evaluated before execution
		Dyn<StartAncillaryAttrs > start_ancillary_attrs  ;                         // in no    crc, evaluated before execution
		DynCmd                    cmd                    ;                         // in cmd   crc, evaluated before execution
		bool                      is_python              = false           ;
		bool                      force                  = false           ;
		uint8_t                   n_losts                = 0               ;       // max number of times a job can be lost
		uint16_t                  n_runs                 = 0               ;       // max number of times a job can be run                               , 0 = infinity
		uint16_t                  n_submits              = 0               ;       // max number of times a job can be submitted (except losts & retries), 0 = infinity
		BitMap<Status>            retried_errs           = DfltRetriedErrs ;       // retried errors when RetryOnError option is passed to lmake
		// derived data
		::vector<uint32_t> stem_n_marks                           ;                // number of capturing groups within each stem
		RuleCrc            crc                                    ;
		VarIdx             n_static_stems                         = 0  ;
		Iota2<VarIdx>      matches_iotas[2/*star*/][N<MatchKind>] = {} ;           // range in matches for each kind of match
		// stats
		mutable Delay    cost_per_token = {} ;                                     // average cost per token
		mutable Delay    exe_time       = {} ;                                     // average exe_time
		mutable uint64_t tokens1_32     = 0  ; static_assert(sizeof(Tokens1)<=4) ; // average number of tokens1 <<32
		mutable JobIdx   stats_weight   = 0  ;                                     // number of jobs used to compute average cost_per_token and exe_time
		// END_OF_VERSIONING
		//
		// not stored on disk
		/**/     TargetPattern  job_name_pattern ;
		::vector<TargetPattern> patterns         ;
	} ;

	struct RuleCrcData {
		using State = RuleCrcState ;
		// accesses
		void operator>>(::string&) const ;
		// services
		::vmap_ss descr() const;
		// data
		// START_OF_VERSIONING REPO
		Crc   match ;
		Crc   cmd   ;
		Crc   rsrcs ;
		Rule  rule  = {}            ; // rule associated with match
		State state = State::CmdOld ;
		// END_OF_VERSIONING
	} ;

	// RuleMatch does not call python and only provides services that can be served with this constraint
	struct Rule::RuleMatch {
		// cxtors & casts
	public :
		RuleMatch() = default ;
		RuleMatch( Rule    r , ::vector_s const& ss                          ) : rule{r} , stems{ss} {}
		RuleMatch( Job                                                       ) ;
		RuleMatch( Rule    r , ::string const& job_name , Bool3 chk_psfx=Yes ) : RuleMatch{r,r->job_name_pattern,job_name,chk_psfx} {} // chk_psfx=Maybe means check size only
		RuleMatch( RuleTgt   , ::string const& target   , Bool3 chk_psfx=Yes ) ;                                                       // .
	private :
		RuleMatch( Rule , TargetPattern const& , ::string const& , Bool3 chk_psfx=Yes ) ;                                              // .
		// accesses
	public :
		void operator>>(::string&          ) const ;
		bool operator==(RuleMatch const& rm) const { return rule==rm.rule && stems==rm.stems ; }
		bool operator+ (                   ) const { return +rule                            ; }
		::vector<Re::Pattern> star_patterns(         ) const ;
		::vector_s            py_matches   (         ) const ; //!                 targets_only
		::vector_s            matches      (bool star) const { return _matches(star,false     ) ; }
		::vector_s            targets      (bool star) const { return _matches(star,true      ) ; }
	private :
		::vector_s _matches( bool star , bool targets_only ) const ;
	public :
		::vmap_s<DepSpec> const& deps_holes() const {
			if (!_has_deps) {
				_deps     = rule->deps_attrs.eval(self).second ;                                                                       // this includes empty slots
				_has_deps = true                               ;
			}
			return _deps ;
		}
		// services
		::pair_s<VarIdx> reject_msg () const ;
		::pair_ss        full_name  () const ;
		::string         name       () const { return full_name().first ; }
		::uset<Node>     target_dirs() const ;
		// data
		Rule       rule  ;
		::vector_s stems ;                                                                                                             // static stems only of course
		// cache
	private :
		mutable bool              _has_deps = false ;
		mutable ::vmap_s<DepSpec> _deps     ;
	} ;

	struct RuleTgt : RuleCrc {
		using Rep = Uint< NBits<RuleCrc> + NBits<VarIdx> > ;
		//cxtors & casts
		RuleTgt() = default ;
		RuleTgt( RuleCrc rc , VarIdx ti ) : RuleCrc{rc} , tgt_idx{ti} {}
		// accesses
		void               operator>>  (::string&     ) const ;
		Rep                operator+   (              ) const { return (+RuleCrc(self)<<NBits<VarIdx>) | tgt_idx  ; }
		bool               operator==  (RuleTgt const&) const = default ;
		::partial_ordering operator<=> (RuleTgt const&) const = default ;
		//
		::pair_s<RuleData::MatchEntry> const& key_matches () const { SWEAR(+self->rule)             ; return self->rule->matches [tgt_idx] ; }
		TargetPattern                  const& pattern     () const { SWEAR(+self->rule)             ; return self->rule->patterns[tgt_idx] ; }
		::string                       const& key         () const {                                  return key_matches().first           ; }
		RuleData::MatchEntry           const& matches     () const {                                  return key_matches().second          ; }
		::string                       const& target      () const { SWEAR(tflags()[Tflag::Target]) ; return matches().pattern             ; }
		Tflags                                tflags      () const {                                  return matches().flags.tflags        ; }
		ExtraTflags                           extra_tflags() const {                                  return matches().flags.extra_tflags  ; }
		// services
		bool sure() const {
			Rule r = self->rule ;
			//                    star
			if (!r                                                            ) return false                                               ;
			if ( r->matches_iotas[false][+MatchKind::Target].contains(tgt_idx)) return !matches().flags.extra_tflags[ExtraTflag::Optional] ;
			if ( r->matches_iotas[true ][+MatchKind::Target].contains(tgt_idx)) return  matches().flags.tflags      [Tflag     ::Phony   ] ;
			/**/                                                                return false                                               ;
		}
		size_t hash() const {
			Hash::Fnv fnv ;         // good enough
			fnv += +RuleCrc(self) ;
			fnv += tgt_idx        ;
			return +fnv ;
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

		template<::integral I> void acquire( I&/*out*/ dst , bool&/*out*/ is_dyn , Py::Object const* py_src , I min , I max ) {
			if (!py_src           )                   return ;
			if ( py_src==&Py::None) { is_dyn = true ; return ; }
			//
			long v = py_src->as_a<Py::Int>() ;
			throw_if( ::cmp_less   (v,min) , "underflow" ) ;
			throw_if( ::cmp_greater(v,max) , "overflow"  ) ;
			dst = I(v) ;
		}


		template<UEnum E> ::pair<E,bool/*neg*/> _acquire_enum_val( ::string&& src , BitMap<E> accepted=~BitMap<E>() ) {
			bool neg = src[0]=='-'     ; if (neg) src = src.substr(1) ;
			/**/                         throw_unless( can_mk_enum<E>(src) , "unexpected value ",src," not in ",accepted ) ;
			E    val = mk_enum<E>(src) ; throw_unless( accepted[val]       , "unexpected value ",src," not in ",accepted ) ;
			/**/                         return { val , neg }                                                              ;
		}
		template<UEnum E> void acquire( E&/*out*/ dst , bool&/*out*/ is_dyn , Py::Object const* py_src , BitMap<E> accepted ) {
			if (!py_src           )                   return ;
			if ( py_src==&Py::None) { is_dyn = true ; return ; }
			bool neg ;
			tie(dst,neg) = _acquire_enum_val<E>( py_src->as_a<Py::Str>() , accepted ) ;
			throw_if( neg , "value ",dst,"cannot be negative" ) ;
		}
		template<UEnum E> void acquire( BitMap<E>&/*out*/ dst , bool&/*out*/ is_dyn , Py::Object const* py_src , BitMap<E> dflt , BitMap<E> accepted ) {
			if (!py_src           )                   return ;
			if ( py_src==&Py::None) { is_dyn = true ; return ; }
			//
			auto acquire1 = [&](Py::Object const& py_v) {
				::pair<E,bool/*neg*/> e_n = _acquire_enum_val<E>( py_v.as_a<Py::Str>() , accepted ) ;
				if (e_n.second) dst &= ~e_n.first ;
				else            dst |=  e_n.first ;
			} ;
			dst = dflt ;
			if      (py_src->is_a<Py::Str     >())                                                                  acquire1(*py_src) ;
			else if (py_src->is_a<Py::Sequence>()) { for( Py::Object const& py_val : py_src->as_a<Py::Sequence>() ) acquire1( py_val) ; }
			else                                     throw cat("type error : ",py_src->type_name()," is not a str nor a list/tuple") ;
		}

		template<bool Env> void acquire( ::string&/*out*/ dst , bool&/*out*/ is_dyn , Py::Object const* py_src ) {
			if (!py_src           )                   return ;
			if ( py_src==&Py::None) { is_dyn = true ; return ; }
			//
			if (*py_src==Py::Ellipsis) dst = Env ? PassMrkr : "..."s ;
			else                       dst = *py_src->str()          ;
		}

		template<class T> void acquire( ::vector<T>&/*out*/ dst , IsDynVector&/*out*/ is_dyn , Py::Object const* py_src ) {
			if (!py_src           )                         return ;
			if ( py_src==&Py::None) { is_dyn.first = true ; return ; }
			size_t n = dst.size() ;
			size_t i = 0          ;
			auto handle_entry = [&](Py::Object const& py_item) {
				try                       { bool id=false ; acquire( /*out*/grow(dst,i) , /*out*/id , &py_item ) ; grow(is_dyn.second,i)=id ; i++ ; }
				catch (::string const& e) { throw cat("for item ",i," : ",e) ;                                                                      }
			} ;
			if (py_src->is_a<Py::Sequence>()) {
				Py::Sequence const& py_seq = py_src->as_a<Py::Sequence>() ;
				dst.reserve(py_seq.size()) ;
				for( Py::Object const& py_item : py_seq ) handle_entry(py_item) ;
			} else {
				handle_entry(*py_src) ;
			}
			if (i!=n) dst.resize(i) ;
		}

		template<class T,bool Env> void acquire( ::vmap_s<T>&/*out*/ dst , IsDynMap&/*out*/ is_dyn , Py::Object const* py_src ) {
			static_assert(!Env||::is_same_v<T,::string>) ;
			if (!py_src           )                         return ;
			if ( py_src==&Py::None) { is_dyn.first = true ; return ; }
			//
			::map_s<T>      dst_map = mk_map(dst)              ;                                 // keep sorted to ensure crc stability
			Py::Dict const& py_map  = py_src->as_a<Py::Dict>() ;
			for( auto const& [py_key,py_val] : py_map ) {
				::string key = py_key.template as_a<Py::Str>() ;
				auto     it  = dst_map.emplace(key,T()).first  ;
				bool     id  = false                           ;
				try {
					if constexpr (Env) acquire<Env>( /*out*/it->second , /*out*/id , &py_val ) ; // special case for environment where we replace occurrences of lmake & root dirs by markers ...
					else               acquire     ( /*out*/it->second , /*out*/id , &py_val ) ; // ... to make repo robust to moves of lmake or itself
				} catch (::string const& e) {
					throw cat("for item ",key," : ",e) ;
				}
				if (id) is_dyn.second.insert(key) ;
			}
			dst = mk_vmap(dst_map) ;
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
		// START_OF_VERSIONING REPO
		Kind     kind_ ;
		::string buf   ;
		if constexpr (IsHash) {
			kind_ = ::min(Kind::Dyn,kind) ;                                                                                               // marshal is unstable and cannot be used for hash computation
		} else if (!IsIStream<S>) {
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
			case Kind::None         :                                                                                             break ;
			case Kind::ShellCmd     : ::serdes( s , code_str                          ) ;                                         break ;
			case Kind::PythonCmd    : ::serdes( s , code_str , glbs_str               ) ; { if (!IsHash) ::serdes(s,dbg_info) ; } break ; // dbg_info is not hashed as it no semantic value
			case Kind::Dyn          : ::serdes( s , code_str , glbs_str  , may_import ) ; { if (!IsHash) ::serdes(s,dbg_info) ; } break ;
			case Kind::Compiled     : ::serdes( s , code     , glbs_code , may_import ) ;                                       ; break ;
			case Kind::CompiledGlbs : ::serdes( s , code     , buf       , may_import ) ;                                       ; break ; // buf is marshaled info
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

	template<class Spec> concept SpecHasInit   = requires( Spec spec , Py::Object dct ) { spec.init  (dct,::umap_s<CmdIdx>(),RuleData()) ; } ;
	template<class Spec> concept SpecHasUpdate = requires( Spec spec , Py::Dict   dct ) { spec.update(dct                              ) ; } ;
	template<class T> Dyn<T>::Dyn( RulesBase& rules , Py::Dict const& py_src , ::umap_s<CmdIdx> const& var_idxs , RuleData const& rd ) {
		Py::Ptr<>  py_update ;
		Py::Ptr<>* pu        = SpecHasUpdate<T> ? &py_update : nullptr ;
		//
		static_cast<DynBase&>(self) = DynBase( /*out*/pu , rules , py_src , var_idxs , ::is_same_v<T,Cmd>?No|rd.is_python:Maybe ) ;
		if (py_src.contains("static") ) {
			if      constexpr (SpecHasInit  <T>) spec.init  ( py_src["static"] , var_idxs , rd  ) ;
			else if constexpr (SpecHasUpdate<T>) spec.update( py_src["static"].as_a<Py::Dict>() ) ;
		}
		if constexpr (SpecHasUpdate<T>) { if   (+py_update) spec.update(py_update->as_a<Py::Dict>()) ; }
		else                              SWEAR(!py_update) ;                                            // if we have no update, we'd better have nothing to update
	}

	template<class T> void Dyn<T>::eval_ctx( Job job , Rule::RuleMatch& match_ , ::vmap_ss const& rsrcs_ , EvalCtxFuncStr const& cb_str , EvalCtxFuncDct const& cb_dct ) const {
		DynBase::s_eval( job , match_ , rsrcs_ , entry().ctx , cb_str , cb_dct ) ;
	}

	template<class T> Py::Ptr<> Dyn<T>::_eval_code( Job job , Rule::RuleMatch& match , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps ) const {
		// functions defined in glbs use glbs as their global dict (which is stored in the code object of the functions), so glbs must be modified in place or the job-related values will not
		// be seen by these functions, which is the whole purpose of such dynamic values
		Rule r = +match ? match.rule : job->rule() ;
		//
		::vector_s        to_del   ;
		::string          to_eval  ;
		Py::Ptr<Py::Dict> tmp_glbs = entry().glbs ;                        // use a modifyable copy as we restore after use
		eval_ctx( job , match , rsrcs
		,	[&]( VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) {
				to_del.push_back(key) ;
				if (vc!=VarCmd::StarMatch) tmp_glbs->set_item(key,*Py::Ptr<Py::Str>(val)) ;
				else                       to_eval += r->gen_py_line( job , match , vc , i , key , val ) ;
			}
		,	[&]( VarCmd , ::string const& key , ::vmap_ss const& val ) {
				to_del.push_back(key) ;
				Py::Ptr<Py::Dict> py_dct { New } ;
				for( auto const& [k,v] : val ) py_dct->set_item(k,*Py::Ptr<Py::Str>(v)) ;
				tmp_glbs->set_item(key,*py_dct) ;
			}
		) ;
		//
		Rule::s_last_dyn_job  = job                             ;
		Rule::s_last_dyn_msg  = T::Msg                          ;
		Rule::s_last_dyn_rule = +job ? job->rule() : match.rule ;
		fence() ;
		Pdate               start             { New                           } ;
		Save<Atomic<Pdate>> sav_last_dyn_date { Rule::s_last_dyn_date , start } ; SWEAR( !sav_last_dyn_date.saved , sav_last_dyn_date.saved ) ;
		//
		Py::py_run(to_eval,tmp_glbs) ;
		g_kpi.py_exe_time += Pdate(New) - start ;
		Py::Ptr<>   res      ;
		::string    err      ;
		bool        seen_err = false  ;
		AutodepLock lock     { deps } ;
		start = New ;                                                      // ensure waiting for lock is not accounted as python exec time
		//                                vvvvvvvvvvvvvvvvvvvvvvvvvvvv
		try                       { res = entry().code->eval(tmp_glbs) ; }
		//                                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		catch (::string const& e) { err = e ; seen_err = true ;          }
		for( ::string const& key : to_del ) tmp_glbs->del_item(key) ;      // delete job-related info, just to avoid percolation to other jobs, even in case of error
		g_kpi.py_exe_time += Pdate(New) - start ;
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

	// START_OF_VERSIONING REPO
	template<IsStream S> void RuleData::serdes(S& s) {
		::serdes(s,special     ) ;
		::serdes(s,user_prio   ) ;
		::serdes(s,prio        ) ;
		::serdes(s,name        ) ;
		::serdes(s,stems       ) ;
		::serdes(s,sub_repo_s  ) ;
		::serdes(s,job_name    ) ;
		::serdes(s,matches     ) ;
		::serdes(s,stdout_idx  ) ;
		::serdes(s,stdin_idx   ) ;
		::serdes(s,allow_ext   ) ;
		::serdes(s,deps_attrs  ) ;
		::serdes(s,force       ) ;
		::serdes(s,n_losts     ) ;
		::serdes(s,n_runs      ) ;
		::serdes(s,n_submits   ) ;
		::serdes(s,retried_errs) ;
		if (special==Special::Plain) {
			::serdes(s,submit_rsrcs_attrs    ) ;
			::serdes(s,submit_ancillary_attrs) ;
			::serdes(s,start_cmd_attrs       ) ;
			::serdes(s,start_rsrcs_attrs     ) ;
			::serdes(s,start_ancillary_attrs ) ;
			::serdes(s,cmd                   ) ;
			::serdes(s,is_python             ) ;
			// stats
			::serdes(s,cost_per_token        ) ;
			::serdes(s,exe_time              ) ;
			::serdes(s,stats_weight          ) ;
		}
		// derived
		::serdes(s,stem_n_marks  ) ;
		::serdes(s,crc           ) ;
		::serdes(s,n_static_stems) ;
		::serdes(s,matches_iotas ) ;
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
	inline void RuleData::validate(::string const& job_sfx) const {
		if (job_sfx.size()<sizeof(Crc::Val)) FAIL( mk_printable(job_sfx) ) ;
		Crc crc_ { decode_int<Crc::Val>(&job_sfx[job_sfx.size()-sizeof(Crc::Val)]) } ;
		if (crc_!=crc->match) FAIL( name , sub_repo_s , crc_ , crc->match , mk_printable(job_sfx) ) ;
	}

	//
	// Rule::RuleMatch
	//

	inline Rule::RuleMatch::RuleMatch( RuleTgt rt , ::string const& target , Bool3 chk_psfx ) : RuleMatch{rt->rule,rt.pattern(),target,chk_psfx} {}

}

#endif
