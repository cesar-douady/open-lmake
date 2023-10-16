// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "pycxx.hh"

#include "store/prefix.hh"
#include "store/vector.hh"
#include "store/struct.hh"

#include "rpc_job.hh"

#ifdef STRUCT_DECL

namespace Engine {

	struct Rule     ;
	struct RuleData ;

	struct RuleTgt ;

	ENUM( VarCmd
	,	Stems   , Stem
	,	Targets , Target
	,	Deps    , Dep
	,	Rsrcs   , Rsrc
	)
	constexpr BitMap<VarCmd> NeedStems   { VarCmd::Stems   , VarCmd::Stem   } ;
	constexpr BitMap<VarCmd> NeedTargets { VarCmd::Targets , VarCmd::Target } ;
	constexpr BitMap<VarCmd> NeedDeps    { VarCmd::Deps    , VarCmd::Dep    } ;
	constexpr BitMap<VarCmd> NeedRsrcs   { VarCmd::Rsrcs   , VarCmd::Rsrc   } ;

	ENUM_1( EnvFlag
	,	Dflt = Rsrc
	//
	,	None                           // ignore variable
	,	Rsrc                           // consider variable as a resource : upon modification, rebuild job if it was in error
	,	Cmd                            // consider variable as a cmd      : upon modification, rebuild job
	)

	ENUM_1( Special
	,	Shared = Infinite              // <=Shared means there is a single such rule
	//
	,	None                           // value 0 reserved to mean not initialized, so shared rules have an idx equal to special
	,	Src
	,	Req
	,	Uphill
	,	Infinite
	// ordered by decreasing matching priority within each prio
	,	GenericSrc
	,	Anti
	,	Plain
	)

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
		static constexpr char   JobMrkr  =  0 ;                                // ensure no ambiguity between job names and node names
		static constexpr char   StarMrkr =  0 ;                                // signal a star stem in job_name
		static constexpr char   StemMrkr =  0 ;                                // signal a stem in job_name & targets & deps & cmd
		static constexpr VarIdx NoVar    = -1 ;
		//
		struct SimpleMatch ;
		struct FullMatch   ;
		// cxtors & casts
		using RuleBase::RuleBase ;
		Rule(RuleBase const& rb     ) : RuleBase{ rb                                                      } {                                                     }
		Rule(::string const& job_sfx) : Rule    { decode_int<Idx>( &job_sfx[job_sfx.size()-sizeof(Idx)] ) } { SWEAR(job_sfx.size()>=sizeof(Idx),job_sfx.size()) ; }
		// acesses
		::string job_sfx() const ;
		// services
		void new_job_exec_time( Delay , Tokens1 ) ;
	} ;

}
#endif
#ifdef DATA_DEF
namespace Engine {

	namespace Attrs {
		// statics
		/**/                                  bool/*updated*/ acquire( bool       & dst , PyObject* py_src ) ;
		/**/                                  bool/*updated*/ acquire( Time::Delay& dst , PyObject* py_src ) ;
		/**/                                  bool/*updated*/ acquire( ::string   & dst , PyObject* py_src ) ;
		template<class      T               > bool/*updated*/ acquire( ::vector<T>& dst , PyObject* py_src ) ;
		template<class      T,bool Env=false> bool/*updated*/ acquire( ::vmap_s<T>& dst , PyObject* py_src ) ;
		template<::integral I               > bool/*updated*/ acquire( I          & dst , PyObject* py_src ) ;
		template<StdEnum    E               > bool/*updated*/ acquire( E          & dst , PyObject* py_src ) ;
		//
		template<class T,bool Env=false> bool/*update*/ acquire_from_dct( T& dst , PyObject* py_dct , ::string const& key ) {
			try {
				static_assert( !Env || is_same_v<T,::vmap_ss> ) ;
				if constexpr (Env) return acquire<::string,Env>( dst , PyDict_GetItemString(py_dct,key.c_str()) ) ;
				else               return acquire              ( dst , PyDict_GetItemString(py_dct,key.c_str()) ) ;
			} catch (::string const& e) {
				throw to_string("while processing ",key," : ",e) ;
			}
		}
		static inline void acquire_env( ::vmap_ss& dst , PyObject* py_dct , ::string const& key ) { acquire_from_dct<::vmap_ss,true/*Env*/>(dst,py_dct,key) ; }
		//
		::string subst_fstr( ::string const& fstr , ::umap_s<CmdIdx> const& var_idxs , VarIdx& n_unnamed , BitMap<VarCmd>& need ) ;
	} ;

	// used at match time
	struct DepsAttrs {
		static constexpr const char* Msg = "deps" ;
		struct DepSpec {
			::string pattern ;
			Dflags   dflags  ;
		} ;
		// services
		BitMap<VarCmd> init( bool is_dynamic , PyObject* , ::umap_s<CmdIdx> const& , RuleData const& ) ;
		// data
		bool              full_dynamic = false ;           // if true <=> deps is empty and new keys can be added, else dynamic deps must be within dep keys
		::vmap_s<DepSpec> deps         ;
	} ;

	// used at match time, but participate in nothing
	struct CreateNoneAttrs {
		static constexpr const char* Msg = "tokens" ;
		BitMap<VarCmd> init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; return {} ; }
		void           update(                       PyObject* py_dct                           ) {
			size_t tokens ;
			Attrs::acquire_from_dct(tokens,py_dct,"job_tokens") ;
			if (tokens==0)                                    tokens1 = 0                                ;
			else if (tokens>::numeric_limits<Tokens1>::max()) tokens1 = ::numeric_limits<Tokens1>::max() ;
			else                                              tokens1 = tokens-1                         ;
		}
		// data
		Tokens1 tokens1 = 0 ;
	} ;

	// used at submit time, participate in resources
	struct SubmitRsrcsAttrs {
		static constexpr const char* Msg = "submit resources attributes" ;
		static void s_canon(::vmap_ss& rsrcs) ;                                // round and cannonicalize standard resources
		// services
		BitMap<VarCmd> init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; return {} ; }
		void           update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct(backend,py_dct,"backend") ;
			if ( Attrs::acquire_from_dct(rsrcs  ,py_dct,"rsrcs"  ) ) {
				::sort(rsrcs) ;                                                // stabilize rsrcs crc
				canon() ;
			}
		}
		void canon() { s_canon(rsrcs) ; }
		// data
		BackendTag backend = BackendTag::Local ;           // backend to use to launch jobs
		::vmap_ss  rsrcs   ;
	} ;

	// used at submit time, participate in resources
	struct SubmitNoneAttrs {
		static constexpr const char* Msg = "n_retries" ;
		// services
		BitMap<VarCmd> init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; return {} ; }
		void           update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct(n_retries,py_dct,"n_retries") ;
		}
		// data
		uint8_t n_retries = 0 ;
	} ;

	// used both at submit time (for cache look up) and at end of execution (for cache upload)
	struct CacheNoneAttrs {
		static constexpr const char* Msg = "cache key" ;
		// services
		BitMap<VarCmd> init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; return {} ; }
		void           update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct(key,py_dct,"key") ;
			if ( !key.empty() && !Cache::s_tab.contains(key) ) throw to_string("unexpected cache key ",key," not found in config") ;
		}
		// data
		::string key ;
	} ;

	// used at start time, participate in cmd
	struct StartCmdAttrs {
		static constexpr const char* Msg = "execution command attributes" ;
		// services
		BitMap<VarCmd> init  ( bool is_dynamic , PyObject* py_src , ::umap_s<CmdIdx> const&                 ) { update(py_src,!is_dynamic) ; return {} ; }
		void           update(                   PyObject* py_dct                           , bool chk=true ) {
			using namespace Attrs ;
			acquire_from_dct(auto_mkdir ,py_dct,"auto_mkdir" ) ;
			acquire_from_dct(chroot     ,py_dct,"chroot"     ) ;
			acquire_env     (env        ,py_dct,"env"        ) ;
			acquire_from_dct(ignore_stat,py_dct,"ignore_stat") ;
			acquire_from_dct(interpreter,py_dct,"interpreter") ;
			acquire_from_dct(local_mrkr ,py_dct,"local_mrkr" ) ;
			acquire_from_dct(method     ,py_dct,"autodep"    ) ;
			acquire_from_dct(tmp        ,py_dct,"tmp"        ) ;
			//
			if (chk) {
				if (!tmp.empty()) {
					switch (method) {
						case AutodepMethod::None   :                                                                                 // cannot map if not spying
						case AutodepMethod::Ptrace : throw to_string("cannot map tmp directory from ",tmp," with autodep=",method) ; // cannot allocate memory in traced child to hold mapped path
						default : ;
					}
				}
				switch (method) {
					case AutodepMethod::None      :                                                                                 break ;
					case AutodepMethod::Ptrace    :                                                                                 break ;
					case AutodepMethod::LdAudit   : if (!HAS_LD_AUDIT) throw to_string(method," is not supported on this system") ; break ;
					case AutodepMethod::LdPreload :                                                                                 break ;
					default : FAIL(method) ;
				}
			}
			::sort(env) ;                                                      // stabilize cmd crc
		}
		// data
		bool          auto_mkdir  = false               ;
		bool          ignore_stat = false               ;
		::string      chroot      ;
		::vmap_ss     env         ;
		::vector_s    interpreter ;
		::string      local_mrkr  ;
		AutodepMethod method      = AutodepMethod::Dflt ;
		::string      tmp         ;
	} ;

	struct Cmd {
		static constexpr const char* Msg = "execution command" ;
		// services
		BitMap<VarCmd> init  ( bool /*is_dynamic*/ , PyObject* , ::umap_s<CmdIdx> const& ) ;
		void           update(                       PyObject* py_dct                    ) {
			Attrs::acquire_from_dct(cmd  ,py_dct,"cmd") ;
		}
		// data
		bool     is_python = false/*garbage*/ ;
		::string cmd       ;
	} ;

	// used at start time, participate in resources
	struct StartRsrcsAttrs {
		static constexpr const char* Msg = "execution resources attributes" ;
		BitMap<VarCmd> init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; return {} ; }
		void           update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct(timeout,py_dct,"timeout") ;
			Attrs::acquire_env     (env    ,py_dct,"env"    ) ;
			if (timeout<Delay()) throw "timeout must be positive or null (no timeout if null)"s ;
			::sort(env) ;                                                                         // stabilize rsrcs crc
		}
		// data
		Time::Delay timeout ;          // if 0 <=> no timeout, maximum time allocated to job execution in s
		::vmap_ss   env     ;
	} ;

	// used at start time, participate to nothing
	struct StartNoneAttrs {
		static constexpr const char* Msg = "execution ancillary attributes" ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,keep_tmp   ) ;
			::serdes(s,start_delay) ;
			::serdes(s,kill_sigs  ) ;
			::serdes(s,n_retries  ) ;
			::serdes(s,env        ) ;
		}
		BitMap<VarCmd> init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; return {} ; }
		void           update(                       PyObject* py_dct                           ) {
			using namespace Attrs ;
			acquire_from_dct(keep_tmp   ,py_dct,"keep_tmp"   ) ;
			acquire_from_dct(start_delay,py_dct,"start_delay") ;
			acquire_from_dct(kill_sigs  ,py_dct,"kill_sigs"  ) ;
			acquire_from_dct(n_retries  ,py_dct,"n_retries"  ) ;
			acquire_env     (env        ,py_dct,"env"        ) ;
			::sort(env) ;                                                      // by symmetry with env entries in StartCmdAttrs and StartRsrcsAttrs
		}
		// data
		bool              keep_tmp    = false ;
		Time::Delay       start_delay ;                    // job duration above which a start message is generated
		::vector<uint8_t> kill_sigs   ;                    // signals to use to kill job (tried in sequence, 1s apart from each other)
		uint8_t           n_retries   = 0     ;            // max number of retry if job is lost
		::vmap_ss         env         ;
	} ;

	// used at end of job execution, participate in cmd
	struct EndCmdAttrs {
		static constexpr const char* Msg = "allow stderr" ;
		// services
		BitMap<VarCmd> init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; return {} ; }
		void           update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct(allow_stderr,py_dct,"allow_stderr") ;
		}
		// data
		bool allow_stderr = false ;    // if true <=> non empty stderr does not imply job error
	} ;

	// used at end of job execution, participate in nothing
	struct EndNoneAttrs {
		static constexpr const char* Msg = "max stderr length" ;
		// services
		BitMap<VarCmd> init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; return {} ; }
		void           update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct(stderr_len,py_dct,"stderr_len") ;
		}
		// data
		size_t stderr_len = -1 ;       // max lines when displaying stderr (full content is shown with lshow -e)
	} ;

	// the part of the Dynamic struct which is stored on disk
	template<class T> struct DynamicDsk {
		// statics
		static bool     s_is_dynamic(PyObject*        ) ;
		static ::string s_exc_msg   (bool using_static) { return to_string( "cannot compute dynamic " , T::Msg , using_static?", using static info":"" ) ; }
		// cxtors & casts
		DynamicDsk() = default ;
		template<class... A> DynamicDsk( PyObject* , ::umap_s<CmdIdx> const& var_idxs , A&&... ) ;
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,is_dynamic) ;
			::serdes(s,spec      ) ;
			::serdes(s,need      ) ;
			::serdes(s,glbs_str  ) ;
			::serdes(s,code_str  ) ;
			::serdes(s,ctx       ) ;
		}
		// data
		bool             is_dynamic = false ;
		T                spec       ;                      // contains default values when code does not provide the necessary entries
		BitMap<VarCmd>   need       ;
		::string         glbs_str   ;                      // if is_dynamic <=> contains string to run to get the glbs below
		::string         code_str   ;                      // if is_dynamic <=> contains string to compile to code object below
		::vector<CmdIdx> ctx        ;                      // a list of stems, targets & deps, accessed by code
	} ;
	template<class T> struct Dynamic : DynamicDsk<T> {
		using Base = DynamicDsk<T> ;
		using Base::is_dynamic ;
		using Base::need       ;
		using Base::spec       ;
		using Base::glbs_str   ;
		using Base::code_str   ;
		using Base::ctx        ;
		// statics
		static bool s_is_dynamic(PyObject*) ;
		// cxtors & casts
		using Base::Base ;
		Dynamic           (Dynamic const& src) : Base{       src } , glbs{Py::boost(src.glbs)} , code{Py::boost(src.code)} {                                           } // mutex is not copiable
		Dynamic           (Dynamic     && src) : Base{::move(src)} , glbs{          src.glbs } , code{          src.code } { src.glbs = nullptr ; src.code = nullptr ; } // .
		Dynamic& operator=(Dynamic const& src) {                                                                                                                         // .
			Base::operator=(src) ;
			Py_XDECREF(glbs) ; glbs = src.glbs ; Py_XINCREF(glbs) ;
			Py_XDECREF(code) ; code = src.code ; Py_XINCREF(code) ;
			return *this ;
		}
		Dynamic& operator=(Dynamic&& src) {                                                                                                                              // .
			Base::operator=(::move(src)) ;
			Py_XDECREF(glbs) ; glbs = src.glbs ; src.glbs = nullptr ;
			Py_XDECREF(code) ; code = src.code ; src.code = nullptr ;
			return *this ;
		}
		// services
		void compile   (                              ) ;
		Rule solve_lazy( Job j , Rule::SimpleMatch& m ) const ;
		//
		T eval( Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs={} ) const ;                                                                   // SimpleMatch is lazy evaluated from Job
		T eval(       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} ) const { return eval( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs ) ; } // cannot lazy evaluate w/o a job
		//
		using EvalCtxFuncStr = ::function<void( string const& key , string val  )> ;
		using EvalCtxFuncDct = ::function<void( string const& key , vmap_ss val )> ;
		void eval_ctx( Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs , EvalCtxFuncStr const&     , EvalCtxFuncDct const&     ) const ; // SimpleMatch is lazy evaluated from Job
		void eval_ctx(       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs , EvalCtxFuncStr const& cbs , EvalCtxFuncDct const& cbd ) const {
			return eval_ctx( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs , cbs , cbd ) ;                                                        // cannot lazy evaluate w/o a job
		}
		::string parse_fstr( ::string const& fstr , Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs={} ) const ; // SimpleMatch is lazy evaluated from Job
		::string parse_fstr( ::string const& fstr ,       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} ) const {
			return parse_fstr( fstr , {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs ) ;                                   // cannot lazy evaluate w/o a job
		}
	protected :
		PyObject* _mk_dct( Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs={} ) const ;
		PyObject* _mk_dct(       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} ) const { return _mk_dct( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs ) ; } // cannot lazy evaluate w/o a job
		// data
	private :
		mutable ::mutex _glbs_mutex ;  // ensure glbs is not used for several jobs simultaneously
	public :
		// not stored on disk
		PyObject* glbs = nullptr ;     // if is_dynamic <=> dict to use as globals when executing code
		PyObject* code = nullptr ;     // if is_dynamic <=> python code object to execute with stems as locals and glbs as globals leading to a dict that can be used to build data
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
		::vmap_s<pair_s<AccDflags>> eval( Rule::SimpleMatch const& ) const ;
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
		::string eval( Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs={} ) const ; // SimpleMatch is lazy evaluated from Job
		::string eval(       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} ) const {
			return eval( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs ) ; // m cannot be lazy evaluated w/o a job
		}
	} ;

	struct RuleData {
		friend ::ostream& operator<<( ::ostream& , RuleData const& ) ;
		friend Rule ;
		static constexpr VarIdx NoVar = Rule::NoVar ;

		// statics
		static bool s_sure(Tflags tf) { return !tf[Tflag::Star] || tf[Tflag::Phony] ; } // if phony, a target is deemed generated, even if it does not exist, hence it is sure
		// static data
		static size_t s_name_sz ;

		// cxtors & casts
		RuleData(                                        ) = default ;
		RuleData( Special , ::string const& src_dir_s={} ) ;                   // src_dir in case Special is SrcDir
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
		bool   is_anti     (        ) const { return special==Special::Anti || special==Special::Uphill     ; }
		bool   is_special  (        ) const { return special!=Special::Plain                                ; }
		bool   is_src      (        ) const { return special==Special::Src || special==Special::GenericSrc  ; }
		bool   is_sure     (        ) const { return special!=Special::GenericSrc                           ; } // GenericSrc targets are only buildable if file actually exists
		bool   user_defined(        ) const { return !allow_ext                                             ; } // used to decide to print in LMAKE/rules
		Tflags tflags      (VarIdx t) const { return t==NoVar ? UnexpectedTflags : targets[t].second.tflags ; }
		bool   sure        (VarIdx t) const { return s_sure(tflags(t))                                      ; }
		//
		Bool3 common_tflags( Tflag  f, bool unexpected ) const {
			if (unexpected) return No | UnexpectedTflags[f]                  ;
			else            return ( Maybe | min_tflags[f] ) & max_tflags[f] ;
		}
		//
		vmap_view_c_ss static_stems() const { return vmap_view_c_ss(stems).subvec(0,n_static_stems) ; }
		//
		::string    user_name  () const { return is_identifier(name) ? name : to_string('"',name,'"') ; }
		FileNameIdx job_sfx_len() const {
			return
				1                                                              // null to disambiguate w/ Node names
			+	n_static_stems * sizeof(FileNameIdx)*2                         // pos+len for each stem
			+	sizeof(RuleIdx)                                                // Rule index
			;
		}

		// services
		void add_cwd( ::string& file , bool top ) const {
			if (!( top || file.empty() || cwd_s.empty() )) file.insert(0,cwd_s) ;
		}
	private :
		bool _get_cmd_needs_deps() const {                                     // if deps are needed for cmd computation, then static deps are deemed to be read...
			return                                                             // as accesses are not recorded and we need to be pessimistic
				submit_rsrcs_attrs.is_dynamic
			||	start_cmd_attrs   .is_dynamic
			||	cmd               .is_dynamic
			||	start_rsrcs_attrs .is_dynamic
			||	end_cmd_attrs     .is_dynamic
			;
		}
		::vector_s _list_ctx(::vector<CmdIdx> const& ctx) const ;
		void _set_crcs() ;

		// user data
	public :
		Special              special    = Special::None ;
		Prio                 prio       = 0             ;                      // the priority of the rule
		::string             name       ;                                      // the short message associated with the rule
		::vmap_ss            stems      ;                                      // stems   are ordered : statics then stars
		::string             cwd_s      ;                                      // cwd in which to interpret targets & deps and execute cmd (with ending /)
		::string             job_name   ;                                      // used to show in user messages, same format as a target
		::vmap_s<TargetSpec> targets    ;                                      // keep user order, except static targets before star targets
		VarIdx               stdout_idx = NoVar         ;                      // index of target used as stdout
		VarIdx               stdin_idx  = NoVar         ;                      // index of dep used as stdin
		bool                 allow_ext   = false        ;                      // if true <=> rule may match outside repo
		// following is only if plain rules
		DynamicDepsAttrs          deps_attrs         ;                         // in match crc, evaluated at job creation time
		Dynamic<CreateNoneAttrs > create_none_attrs  ;                         // in no    crc, evaluated at job creation time
		Dynamic<CacheNoneAttrs  > cache_none_attrs   ;                         // in no    crc, evaluated twice : at submit time to look for a hit and after execution to upload result
		Dynamic<SubmitRsrcsAttrs> submit_rsrcs_attrs ;                         // in rsrcs crc, evaluated at submit time
		Dynamic<SubmitNoneAttrs > submit_none_attrs  ;                         // in no    crc, evaluated at submit time
		Dynamic<StartCmdAttrs   > start_cmd_attrs    ;                         // in cmd   crc, evaluated before execution
		DynamicCmd                cmd                ;
		Dynamic<StartRsrcsAttrs > start_rsrcs_attrs  ;                         // in rsrcs crc, evaluated before execution
		Dynamic<StartNoneAttrs  > start_none_attrs   ;                         // in no    crc, evaluated before execution
		Dynamic<EndCmdAttrs     > end_cmd_attrs      ;                         // in cmd   crc, evaluated after  execution
		Dynamic<EndNoneAttrs    > end_none_attrs     ;                         // in no    crc, evaluated after  execution
		bool                      force              = false ;
		size_t                    n_tokens           = 1     ;                 // available tokens for this rule, used to estimate req ETE (cannot be dynamic)
		// derived data
		bool   cmd_needs_deps   = false        ;
		Tflags max_tflags       = Tflags::All  ;
		Tflags min_tflags       = Tflags::None ;
		VarIdx n_static_stems   = 0            ;
		VarIdx n_static_targets = 0            ;
		// management data
		ExecGen cmd_gen   = 1 ;                                                // cmd generation, must be >0 as 0 means !cmd_ok
		ExecGen rsrcs_gen = 1 ;                                                // for a given cmd, resources generation, must be >=cmd_gen
		// stats
		mutable Delay  exec_time    = {} ;                                     // average exec_time
		mutable JobIdx stats_weight = 0  ;                                     // number of jobs used to compute average
		// not stored on disk
		::vector<Py::Pattern> target_patterns ;
		Crc                   match_crc       = Crc::None ;
		Crc                   cmd_crc         = Crc::None ;
		Crc                   rsrcs_crc       = Crc::None ;
	} ;

	// SimpleMatch does not call Python and only provides services that can be served with this constraint
	struct Rule::SimpleMatch {
		friend ::ostream& operator<<( ::ostream& , SimpleMatch const& ) ;
		// cxtors & casts
	public :
		SimpleMatch(                               ) = default ;
		SimpleMatch( Rule r , ::vector_s const& ss ) : rule{r} , stems{ss} {}
		SimpleMatch( Job                           ) ;
		bool operator==(SimpleMatch const&) const = default ;
		bool operator+ (                  ) const { return +rule ; }
		bool operator! (                  ) const { return !rule ; }
		// accesses
		::vector_s                  const& targets() const { if (!_has_targets) { _compute_targets()                   ; _has_targets = true ; } return _targets ; }
		::vmap_s<pair_s<AccDflags>> const& deps   () const { if (!_has_deps   ) { _deps = rule->deps_attrs.eval(*this) ; _has_deps    = true ; } return _deps    ; }
		::vector_view_c_s           static_targets() const { return {targets(),0,rule->n_static_targets} ;                                                         }
	protected :
		void _compute_targets() const ;
		// services
	public :
		::pair_ss  name       () const ;
		::string   user_name  () const ;
		::vector_s target_dirs() const ;
		// data
	public :
		Rule       rule  ;
		::vector_s stems ;             // static stems only of course
		// cache
	protected :
		mutable bool _has_targets = false ; mutable ::vector_s                  _targets ;
		mutable bool _has_deps    = false ; mutable ::vmap_s<pair_s<AccDflags>> _deps    ;
	} ;

	struct Rule::FullMatch : SimpleMatch {
		friend ::ostream& operator<<( ::ostream& , FullMatch const& ) ;
		// helper functions
	private :
		static ::string _group( Py::Object match_object , ::string const& key ) {
			return Py::String(_s_py_group.apply(Py::TupleN(match_object,Py::String(key)))) ;
		}
		// statics
		// Python callables that actually do the job
		static Py::Callable _s_py_group ;                                      // access named groups in a match object
		// cxtors & casts
	public :
		using SimpleMatch::SimpleMatch ;
		FullMatch( SimpleMatch const& sm            ) : SimpleMatch(sm) {}
		FullMatch( RuleTgt , ::string const& target ) ;
		// accesses
	private :
		Py::Pattern const& _target_pattern(VarIdx) const ;                     // solve lazy evaluation
		// services
	public :
		VarIdx idx(::string const&) const ;
	private :
		bool _match( VarIdx , ::string const& ) const ;
		// cache
		mutable ::vector<Py::Pattern> _target_patterns ;                       // lazy evaluated
	} ;

	struct RuleTgt : Rule {
		friend ::ostream& operator<<( ::ostream& , RuleTgt const ) ;
		using Rep = Uint< NBits<Rule> + NBits<VarIdx> > ;
		//cxtors & casts
		RuleTgt(                    ) = default ;
		RuleTgt( Rule r , VarIdx ti ) : Rule{r} , tgt_idx{ti} {}
		// accesses
		Rep                operator+  (              ) const { return (+Rule(*this)<<NBits<VarIdx>) | tgt_idx  ; }
		bool               operator== (RuleTgt const&) const = default ;
		::partial_ordering operator<=>(RuleTgt const&) const = default ;
		::string const&    key        (              ) const { return (*this)->targets[tgt_idx].first          ; }
		::string const&    target     (              ) const { return (*this)->targets[tgt_idx].second.pattern ; }
		//
		Tflags tflags() const { return (*this)->tflags(tgt_idx) ; }
		bool   sure  () const { return (*this)->sure  (tgt_idx) ; }
		// services
		Py::Pattern pattern() const { return (*this)->target_patterns[tgt_idx] ; }
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
					1                                                      // JobMrkr to disambiguate with Node names
				+	(*this)->n_static_stems*sizeof(FileNameIdx)*2          // room for stems spec
				+	sizeof(Idx)                                            // Rule index set below
			)
		,	JobMrkr
		) ;
		encode_int( &res[res.size()-sizeof(Idx)] , +*this ) ;
		return res ;
	}

	//
	// Attrs
	//

	template<::integral I> bool/*updated*/ Attrs::acquire( I& dst , PyObject* py_src ) {
		if (!py_src        ) {           return false ; }
		if (py_src==Py_None) { dst = 0 ; return true  ; }
		//
		PyObject* py_src_long = PyNumber_Long(py_src) ;
		if (!py_src_long) throw "cannot convert to an int"s ;
		int  ovrflw = 0                                             ;
		long v      = PyLong_AsLongAndOverflow(py_src_long,&ovrflw) ;
		Py_DECREF(py_src_long) ;
		if (ovrflw                                     ) throw "overflow when converting to an int"s ;
		if (::cmp_less   (v,::numeric_limits<I>::min())) throw "underflow"s                          ;
		if (::cmp_greater(v,::numeric_limits<I>::max())) throw "overflow"s                           ;
		dst = I(v) ;
		return true ;
	}

	template<StdEnum E> bool/*updated*/ Attrs::acquire( E& dst , PyObject* py_src ) {
		if (!py_src        ) {                 return false ; }
		if (py_src==Py_None) { dst = E::Dflt ; return true  ; }
		//
		if (!PyUnicode_Check(py_src)) throw "not a str"s ;
		dst = mk_enum<E>(PyUnicode_AsUTF8(py_src)) ;
		return true ;
	}

	template<class T> bool/*updated*/ Attrs::acquire( ::vector<T>& dst , PyObject* py_src ) {
		if (!py_src        ) {                  return false ;                           }
		if (py_src==Py_None) { if (dst.empty()) return false ; dst = {} ; return true  ; }
		//
		bool updated = false ;
		if (!PySequence_Check(py_src)) throw "not a sequence"s ;
		PyObject* fast_val = PySequence_Fast(py_src,"") ;
		SWEAR(fast_val) ;
		size_t     n = size_t(PySequence_Fast_GET_SIZE(fast_val)) ;
		PyObject** p =        PySequence_Fast_ITEMS   (fast_val)  ;
		for( size_t i=0 ; i<n ; i++ ) {
			if (i>=dst.size()) { updated = true ; dst.push_back(T()) ; }       // create empty entry
			if (p[i]==Py_None) continue ;
			try                       { updated |= acquire(dst.back(),p[i]) ;    }
			catch (::string const& e) { throw to_string("for item ",i," : ",e) ; }
		}
		if (n!=dst.size()) { updated = true ; dst.resize(n) ; }
		return updated ;
	}

	template<class T,bool Env> bool/*updated*/ Attrs::acquire( ::vmap_s<T>& dst , PyObject* py_src ) {
		if (!py_src        ) {                  return false ;                           }
		if (py_src==Py_None) { if (dst.empty()) return false ; dst = {} ; return true  ; }
		//
		bool updated = false ;
		::map_s<T> map = mk_map(dst) ;
		if (!PyDict_Check(py_src)) throw "not a dict"s ;
		PyObject*  py_key = nullptr/*garbage*/ ;
		PyObject*  py_val = nullptr/*garbage*/ ;
		ssize_t    pos    = 0                  ;
		while (PyDict_Next( py_src , &pos , &py_key , &py_val )) {
			if (!PyUnicode_Check(py_key)) throw "key is not a str"s ;
			const char* key = PyUnicode_AsUTF8(py_key) ;
			if (py_val==Py_None) {
				updated |= map.emplace(key,T()).second ;
				continue ;
			}
			if constexpr (Env)
				if (py_val==Py::g_ellipsis) {
					updated  = true        ;
					map[key] = EnvPassMrkr ;                                   // special case for environment where we put an otherwise illegal marker to ask to pass value from job_exec env
					continue ;
				}
			try {
				auto [it,inserted] = map.emplace(key,T()) ;
				updated |= inserted                   ;
				updated |= acquire(it->second,py_val) ;
			} catch (::string const& e) {
				throw to_string("for item ",key," : ",e) ;
			}
		}
		dst = mk_vmap(map) ;
		return updated ;
	}

	//
	// Dynamic
	//

	template<class T> bool DynamicDsk<T>::s_is_dynamic(PyObject* py_src) {
		SWEAR(PyTuple_Check(py_src)) ;
		ssize_t sz = PyTuple_GET_SIZE(py_src) ;
		switch (sz) {
			case 1  :
				return false ;
			case 2  :
				SWEAR(PySequence_Check(PyTuple_GET_ITEM(py_src,1))) ;
				return false ;
			case 4  :
				SWEAR(PySequence_Check(PyTuple_GET_ITEM(py_src,1))) ;
				SWEAR(PyUnicode_Check (PyTuple_GET_ITEM(py_src,2))) ;
				SWEAR(PyUnicode_Check (PyTuple_GET_ITEM(py_src,3))) ;
				return true ;
			default :
				FAIL(sz) ;
		}
	}

	template<class T> template<class... A> DynamicDsk<T>::DynamicDsk( PyObject* py_src , ::umap_s<CmdIdx> const& var_idxs , A&&... args ) :
		is_dynamic{ s_is_dynamic(py_src)                                           }
	,	glbs_str  { is_dynamic ? PyUnicode_AsUTF8(PyTuple_GET_ITEM(py_src,2)) : "" }
	,	code_str  { is_dynamic ? PyUnicode_AsUTF8(PyTuple_GET_ITEM(py_src,3)) : "" }
	{
		need = spec.init( is_dynamic , PyTuple_GET_ITEM(py_src,0) , var_idxs , ::forward<A>(args)... ) ;
		if (PyTuple_GET_SIZE(py_src)<=1) return ;
		PyObject* fast_val = PySequence_Fast(PyTuple_GET_ITEM(py_src,1),"") ;
		SWEAR(fast_val) ;
		size_t     n = size_t(PySequence_Fast_GET_SIZE(fast_val)) ;
		PyObject** p =        PySequence_Fast_ITEMS   (fast_val)  ;
		for( size_t i=0 ; i<n ; i++ ) {
			CmdIdx ci = var_idxs.at(PyUnicode_AsUTF8(p[i])) ;
			ctx.push_back(ci) ;
			need |= ci.bucket ;
		}
		::sort(ctx) ;                                                          // stabilize crc's
	}

	template<class T> void Dynamic<T>::compile() {
		if (!is_dynamic) return ;
		Py::Gil gil ;
		code = Py::boost(Py_CompileString( code_str.c_str() , "<code>" , Py_eval_input )) ; // avoid problems at finalization
		if (!code) throw to_string("cannot compile code :\n",indent(Py::err_str(),1)) ;
		glbs = Py::boost(PyDict_New())                                       ; // avoid problems at finalization
		if (!glbs_str.empty()) {
			PyDict_SetItemString( glbs , "inf"          , *Py::Float(Infinity) ) ;
			PyDict_SetItemString( glbs , "nan"          , *Py::Float(nan("") ) ) ;
			PyDict_SetItemString( glbs , "__builtins__" , PyEval_GetBuiltins() ) ; // Python3.6 does not provide it for us
			//
			PyObject* val = PyRun_String(glbs_str.c_str(),Py_file_input,glbs,glbs) ;
			if (!val) throw to_string("cannot compile context :\n",indent(Py::err_str(),1)) ;
			Py_DECREF(val) ;
		}
	}

	template<class T> inline Rule Dynamic<T>::solve_lazy( Job j , Rule::SimpleMatch& m ) const {
		SWEAR( +j || +m ) ;
		if (+m                ) return m.rule ;
		if (+(need&~NeedRsrcs)) m = Rule::SimpleMatch(j) ;                     // fast path : when no need to compute match (resources do not come from match)
		/**/                    return j->rule ;
	}

	template<class T> void Dynamic<T>::eval_ctx( Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs , EvalCtxFuncStr const& cb_str , EvalCtxFuncDct const& cb_dct ) const {
		::string                           res        ;
		::vector_s                         empty1     ;
		::vmap_s<pair_s<AccDflags>>        empty2     ;
		Rule                               r          = solve_lazy(job,match)                          ;
		auto                        const& rsrcs_spec = r->submit_rsrcs_attrs.spec.rsrcs               ;
		::vector_s                  const& stems      = +(need&NeedStems  ) ? match.stems     : empty1 ;    // fast path : when no need to compute match
		::vector_s                  const& tgts       = +(need&NeedTargets) ? match.targets() : empty1 ;    // fast path : when no need to compute targets
		::vmap_s<pair_s<AccDflags>> const& deps       = +(need&NeedDeps   ) ? match.deps   () : empty2 ;    // fast path : when no need to compute deps
		::umap_ss                          rsrcs_map  ; if (+(need&NeedRsrcs)) rsrcs_map = mk_umap(rsrcs) ;
		for( auto [k,i] : ctx ) {
			::vmap_ss dct ;
			switch (k) {
				case VarCmd::Stem   :                                                                                      cb_str(r->stems  [i].first,stems[i]             ) ;   break ;
				case VarCmd::Target :                                                   if (!tgts[i].empty()             ) cb_str(r->targets[i].first,tgts [i]             ) ;   break ;
				case VarCmd::Dep    :                                                   if (!deps[i].second.first.empty()) cb_str(deps      [i].first,deps [i].second.first) ;   break ;
				case VarCmd::Rsrc   : { auto it = rsrcs_map.find(rsrcs_spec[i].first) ; if (it!=rsrcs_map.end()          ) cb_str(it->first          ,it->second           ) ; } break ;
				//
				case VarCmd::Stems   : for( VarIdx j=0 ; j<r->n_static_stems ; j++ )                         dct.emplace_back(r->stems  [j].first,stems[j] ) ; cb_dct("stems"    ,dct  ) ; break ;
				case VarCmd::Targets : for( VarIdx j=0 ; j<r->targets.size() ; j++ ) if (!tgts[j]  .empty()) dct.emplace_back(r->targets[j].first,tgts [j] ) ; cb_dct("targets"  ,dct  ) ; break ;
				case VarCmd::Deps    : for( auto const& [k,daf] : deps             ) if (!daf.first.empty()) dct.emplace_back(k                  ,daf.first) ; cb_dct("deps"     ,dct  ) ; break ;
				case VarCmd::Rsrcs   :                                                                                                                         cb_dct("resources",rsrcs) ; break ;
				default : FAIL(k) ;
			}
		}
	}

	template<class T> ::string Dynamic<T>::parse_fstr( ::string const& fstr , Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs ) const {
		::string                           res        ;
		::vector_s                         empty1     ;
		::vmap_s<pair_s<AccDflags>>        empty2     ;
		Rule                               r          = solve_lazy(job,match)                          ;
		auto                        const& rsrcs_spec = r->submit_rsrcs_attrs.spec.rsrcs               ;
		::vector_s                  const& stems      = +(need&NeedStems  ) ? match.stems     : empty1 ;    // fast path : when no need to compute match
		::vector_s                  const& tgts       = +(need&NeedTargets) ? match.targets() : empty1 ;    // fast path : when no need to compute targets
		::vmap_s<pair_s<AccDflags>> const& deps       = +(need&NeedDeps   ) ? match.deps   () : empty2 ;    // fast path : when no need to compute deps
		::umap_ss                          rsrcs_map  ; if (+(need&NeedRsrcs)) rsrcs_map = mk_umap(rsrcs) ;
		for( size_t ci=0 ; ci<fstr.size() ; ci++ ) {
			char c = fstr[ci] ;
			if (c==Rule::StemMrkr) {
				VarCmd k = decode_enum<VarCmd>(&fstr[ci+1]) ; ci += sizeof(VarCmd) ;
				VarIdx i = decode_int <VarIdx>(&fstr[ci+1]) ; ci += sizeof(VarIdx) ;
				switch (k) {
					case VarCmd::Stem   :                                                                                      res += stems[i]              ;   break ;
					case VarCmd::Target :                                                                                      res += tgts [i]              ;   break ;
					case VarCmd::Dep    :                                                   if (!deps[i].second.first.empty()) res += deps [i].second.first ;   break ;
					case VarCmd::Rsrc   : { auto it = rsrcs_map.find(rsrcs_spec[i].first) ; if (it!=rsrcs_map.end()          ) res += it->second            ; } break ;
					default : FAIL(k) ;
				}
			} else {
				res += c ;
			}
		}
		return res ;
	}

	template<class T> PyObject* Dynamic<T>::_mk_dct( Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs ) const {
		// functions defined in glbs use glbs as their global dict (which is stored in the code object of the functions), so glbs must be modified in place or the job-related values will not...
		// be seen by these functions, which is the whole purpose of such dynamic values
		::vector_s to_del ;
		eval_ctx( job , match , rsrcs
		,	[&]( ::string const& key , ::string const& val ) -> void {
				PyObject* py_str = PyUnicode_FromString(val.c_str()) ;
				SWEAR(py_str) ;                                                 // else, we are in trouble
				swear(PyDict_SetItemString( glbs , key.c_str() , py_str )==0) ; // else, we are in trouble
				Py_DECREF(py_str) ;                                             // py_v is not stolen by PyDict_SetItemString
				to_del.push_back(key) ;
			}
		,	[&]( ::string const& key , ::vmap_ss const& val ) -> void {
				PyObject* py_dct = PyDict_New() ; SWEAR(py_dct) ;
				for( auto const& [k,v] : val ) {
					PyObject* py_v = PyUnicode_FromString(v.c_str()) ;
					SWEAR(py_v) ;                                                 // else, we are in trouble
					swear(PyDict_SetItemString( py_dct , k.c_str() , py_v )==0) ; // else, we are in trouble
					Py_DECREF(py_v) ;                                             // py_v is not stolen by PyDict_SetItemString
				}
				swear(PyDict_SetItemString( glbs , key.c_str() , py_dct )==0) ; // else, we are in trouble
				Py_DECREF(py_dct) ;                                             // py_v is not stolen by PyDict_SetItemString
				to_del.push_back(key) ;
			}
		) ;
		//            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		PyObject* d = PyEval_EvalCode( code , glbs , nullptr ) ;
		//            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		for( ::string const& key : to_del ) swear(PyDict_DelItemString( glbs , key.c_str() )==0) ; // else, we are in trouble, delete job-related info, just to avoid percolation to other jobs...
		//
		if (!d) throw Py::err_str() ;
		SWEAR(PyDict_Check(d)) ;
		return d ;
	}

	template<class T> T Dynamic<T>::eval( Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs ) const {
		if (!is_dynamic) return spec ;
		T         res = spec                           ;
		Py::Gil   gil ;
		PyObject* d   = _mk_dct( job , match , rsrcs ) ;
		res.update(d) ;
		Py_DECREF(d)  ;
		return res  ;
	}

	//
	// RuleData
	//

	template<IsStream S> void RuleData::serdes(S& s) {
		if (::is_base_of_v<::istream,S>) *this = {} ;
		::serdes(s,special         ) ;
		::serdes(s,prio            ) ;
		::serdes(s,name            ) ;
		::serdes(s,stems           ) ;
		::serdes(s,cwd_s           ) ;
		::serdes(s,job_name        ) ;
		::serdes(s,targets         ) ;
		::serdes(s,stdout_idx      ) ;
		::serdes(s,stdin_idx       ) ;
		::serdes(s,allow_ext       ) ;
		::serdes(s,cmd_needs_deps  ) ;
		::serdes(s,max_tflags      ) ;
		::serdes(s,min_tflags      ) ;
		::serdes(s,n_static_stems  ) ;
		::serdes(s,n_static_targets) ;
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
			::serdes(s,force             ) ;
			::serdes(s,n_tokens          ) ;
			::serdes(s,cmd_gen           ) ;
			::serdes(s,rsrcs_gen         ) ;
			::serdes(s,exec_time         ) ;
			::serdes(s,stats_weight      ) ;
		}
		if (is_base_of_v<::istream,S>) _compile() ;
	}

	//
	// Rule::FullMatch
	//

	inline VarIdx Rule::FullMatch::idx(::string const& target) const {
		targets() ;                                                                       // ensure _targets is populated
		for( VarIdx t=0 ; t<rule->targets.size() ; t++ ) if (_match(t,target)) return t ;
		return NoVar ;
	}

}

namespace std {
	template<> struct hash<Engine::RuleTgt> {
		size_t operator()(Engine::RuleTgt const& rt) const {                   // use FNV-32, easy, fast and good enough, use 32 bits as we are mostly interested by lsb's
			size_t res = 0x811c9dc5 ;
			res = (res^+Engine::Rule(rt)) * 0x01000193 ;
			res = (res^ rt.tgt_idx      ) * 0x01000193 ;
			return res ;
		}
	} ;
}

#endif
