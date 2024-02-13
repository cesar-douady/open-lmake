// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "pycxx.hh"
#include "re.hh"

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
	,	Targets , Match
	,	Deps    , Dep
	,	Rsrcs   , Rsrc
	)

	ENUM_1( EnvFlag
	,	Dflt = Rsrc
	//
	,	None // ignore variable
	,	Rsrc // consider variable as a resource : upon modification, rebuild job if it was in error
	,	Cmd  // consider variable as a cmd      : upon modification, rebuild job
	)

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
		// services
		void new_job_exec_time( Delay , Tokens1 ) ;
	} ;

}
#endif
#ifdef DATA_DEF
namespace Engine {

	namespace Attrs {
		// statics
		/**/                   bool/*updated*/ acquire( bool       & dst , PyObject* py_src                                                                                           ) ;
		/**/                   bool/*updated*/ acquire( Time::Delay& dst , PyObject* py_src , Time::Delay min=Time::Delay::Lowest        , Time::Delay max=Time::Delay::Highest       ) ;
		template<::integral I> bool/*updated*/ acquire( I          & dst , PyObject* py_src , I           min=::numeric_limits<I>::min() , I           max=::numeric_limits<I>::max() ) ;
		template<StdEnum    E> bool/*updated*/ acquire( E          & dst , PyObject* py_src                                                                                           ) ;
		//
		template<        bool Env=false>                                       bool/*updated*/ acquire( ::string   & dst , PyObject* py_src ) ;
		template<class T,bool Env=false> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vector<T>& dst , PyObject* py_src ) ;
		template<class T,bool Env=false> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vmap_s<T>& dst , PyObject* py_src ) ;
		//
		template<class T,bool Env=false> requires(!Env||IsOneOf<T,::string,::vector_s,::vmap_ss>) bool/*update*/ acquire_from_dct( T& dst , PyObject* py_dct , ::string const& key ) {
			try {
				if      constexpr (IsOneOf<T,::string            >) {                       return acquire<         Env>( dst , PyDict_GetItemString(py_dct,key.c_str()) ) ; }
				else if constexpr (IsOneOf<T,::vector_s,::vmap_ss>) {                       return acquire<::string,Env>( dst , PyDict_GetItemString(py_dct,key.c_str()) ) ; }
				else                                                { static_assert(!Env) ; return acquire              ( dst , PyDict_GetItemString(py_dct,key.c_str()) ) ; }
			} catch (::string const& e) {
				throw to_string("while processing ",key," : ",e) ;
			}
		}
		template<class T> bool/*update*/ acquire_from_dct( T& dst , PyObject* py_dct , ::string const& key , T min ) {
				return acquire( dst , PyDict_GetItemString(py_dct,key.c_str()) , min ) ;
		}
		template<class T> bool/*update*/ acquire_from_dct( T& dst , PyObject* py_dct , ::string const& key , T min , T max ) {
				return acquire( dst , PyDict_GetItemString(py_dct,key.c_str()) , min , max ) ;
		}
		static inline void acquire_env( ::vmap_ss& dst , PyObject* py_dct , ::string const& key ) { acquire_from_dct<::vmap_ss,true/*Env*/>(dst,py_dct,key) ; }
		//
		::string subst_fstr( ::string const& fstr , ::umap_s<CmdIdx> const& var_idxs , VarIdx& n_unnamed ) ;
	} ;

	// used at match time
	struct DepsAttrs {
		static constexpr const char* Msg = "deps" ;
		struct DepSpec {
			::string pattern ;
			Dflags   dflags  ;
		} ;
		// services
		void init( bool is_dynamic , PyObject* , ::umap_s<CmdIdx> const& , RuleData const& ) ;
		// data
		bool              full_dynamic = false ; // if true <=> deps is empty and new keys can be added, else dynamic deps must be within dep keys
		::vmap_s<DepSpec> deps         ;
	} ;

	// used at match time, but participate in nothing
	struct CreateNoneAttrs {
		static constexpr const char* Msg = "create ancillary attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; }
		void update(                       PyObject* py_dct                           ) {
			size_t tokens = 0/*garbage*/ ;
			Attrs::acquire_from_dct( tokens , py_dct , "job_tokens" ) ;
			if      (tokens==0                              ) tokens1 = 0                                ;
			else if (tokens>::numeric_limits<Tokens1>::max()) tokens1 = ::numeric_limits<Tokens1>::max() ;
			else                                              tokens1 = tokens-1                         ;
		}
		// data
		Tokens1 tokens1 = 0 ;
	} ;

	// used at submit time, participate in resources
	struct SubmitRsrcsAttrs {
		static constexpr const char* Msg = "submit resources attributes" ;
		static void s_canon(::vmap_ss& rsrcs) ;                            // round and cannonicalize standard resources
		// services
		void init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; }
		void update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct( backend , py_dct , "backend" ) ;
			if ( Attrs::acquire_from_dct( rsrcs , py_dct , "rsrcs" ) ) {
				::sort(rsrcs) ;                                            // stabilize rsrcs crc
				canon() ;
			}
		}
		void canon() { s_canon(rsrcs) ; }
		// data
		BackendTag backend = BackendTag::Local ;                           // backend to use to launch jobs
		::vmap_ss  rsrcs   ;
	} ;

	// used at submit time, participate in resources
	struct SubmitNoneAttrs {
		static constexpr const char* Msg = "submit ancillary attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; }
		void update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct( n_retries , py_dct , "n_retries" ) ;
		}
		// data
		uint8_t n_retries = 0 ;
	} ;

	// used both at submit time (for cache look up) and at end of execution (for cache upload)
	struct CacheNoneAttrs {
		static constexpr const char* Msg = "cache key" ;
		// services
		void init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; }
		void update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct( key , py_dct , "key" ) ;
			if ( +key && !Cache::s_tab.contains(key) ) throw to_string("unexpected cache key ",key," not found in config") ;
		}
		// data
		::string key ;
	} ;

	// used at start time, participate in cmd
	struct StartCmdAttrs {
		static constexpr const char* Msg = "execution command attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; }
		void update(                       PyObject* py_dct                           ) {
			using namespace Attrs ;
			acquire_from_dct( auto_mkdir  , py_dct , "auto_mkdir"  ) ;
			acquire_from_dct( chroot      , py_dct , "chroot"      ) ;
			acquire_env     ( env         , py_dct , "env"         ) ;
			acquire_from_dct( ignore_stat , py_dct , "ignore_stat" ) ;
			acquire_from_dct( tmp         , py_dct , "tmp"         ) ;
			acquire_from_dct( use_script  , py_dct , "use_script"  ) ;
			::sort(env) ;                                                                                                                                // stabilize cmd crc
		}
		void chk(AutodepMethod method) {
			switch (method) {
				case AutodepMethod::None    :                                                                                                            // cannot map if not spying
				case AutodepMethod::Ptrace  : if (+tmp         ) throw to_string("cannot map tmp directory from ",tmp," with autodep=",method) ; break ; // cannot alloc mem in traced child ...
				case AutodepMethod::LdAudit : if (!HAS_LD_AUDIT) throw to_string(method," is not supported on this system")                    ; break ; // ... to hold mapped path
				default : ;
			}
		}
		// data
		bool          auto_mkdir  = false ;
		bool          ignore_stat = false ;
		::string      chroot      ;
		::vmap_ss     env         ;
		::string      tmp         ;
		bool          use_script  = false ;
	} ;

	struct DbgEntry {
		friend ::ostream& operator<<( ::ostream& , DbgEntry const& ) ;
		bool operator +() const { return first_line_no1 ; }
		bool operator !() const { return !+*this ;        }
		::string module         ;
		::string qual_name      ;
		::string filename       ;
		size_t   first_line_no1 = 0 ; // illegal value as lines start at 1
	} ;

	struct Cmd {
		static constexpr const char* Msg = "execution command" ;
		// services
		void init  ( bool /*is_dynamic*/ , PyObject* , ::umap_s<CmdIdx> const& , RuleData const& ) ;
		void update(                       PyObject* py_dct                                      ) {
			Attrs::acquire_from_dct( cmd , py_dct , "cmd" ) ;
		}
		// data
		::string cmd       ;
		::string decorator ;
	} ;
	namespace Attrs {
		bool/*updated*/ acquire( DbgEntry& dst , PyObject* py_src ) ;
	}

	// used at start time, participate in resources
	struct StartRsrcsAttrs {
		static constexpr const char* Msg = "execution resources attributes" ;
		void init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; }
		void update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct( timeout , py_dct , "timeout" , Time::Delay()/*min*/ ) ;
			Attrs::acquire_env     ( env     , py_dct , "env"                ) ;
			if (timeout<Delay()) throw "timeout must be positive or null (no timeout if null)"s ;
			::sort(env) ;                                                                         // stabilize rsrcs crc
		}
		// data
		Time::Delay timeout ;                                                                     // if 0 <=> no timeout, maximum time allocated to job execution in s
		::vmap_ss   env     ;
	} ;

	// used at start time, participate to nothing
	struct StartNoneAttrs {
		static constexpr const char* Msg = "execution ancillary attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; }
		void update(                       PyObject* py_dct                           ) {
			using namespace Attrs ;
			acquire_from_dct( keep_tmp    , py_dct , "keep_tmp"                           ) ;
			acquire_from_dct( start_delay , py_dct , "start_delay" , Time::Delay()/*min*/ ) ;
			acquire_from_dct( kill_sigs   , py_dct , "kill_sigs"                          ) ;
			acquire_from_dct( method      , py_dct , "autodep"                            ) ;
			acquire_from_dct( n_retries   , py_dct , "n_retries"                          ) ;
			acquire_env     ( env         , py_dct , "env"                                ) ;
			::sort(env) ;                                                                     // by symmetry with env entries in StartCmdAttrs and StartRsrcsAttrs
		}
		// data
		bool              keep_tmp    = false               ;
		Time::Delay       start_delay ;                                                       // job duration above which a start message is generated
		::vector<uint8_t> kill_sigs   ;                                                       // signals to use to kill job (tried in sequence, 1s apart from each other)
		AutodepMethod     method      = AutodepMethod::Dflt ;
		uint8_t           n_retries   = 0                   ;                                 // max number of retry if job is lost
		::vmap_ss         env         ;
	} ;

	// used at end of job execution, participate in cmd
	struct EndCmdAttrs {
		static constexpr const char* Msg = "end command attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; }
		void update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct( allow_stderr , py_dct , "allow_stderr" ) ;
		}
		// data
		bool allow_stderr = false ; // if true <=> non empty stderr does not imply job error
	} ;

	// used at end of job execution, participate in nothing
	struct EndNoneAttrs {
		static constexpr const char* Msg = "end ancillary attributes" ;
		// services
		void init  ( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& ) { update(py_src) ; }
		void update(                       PyObject* py_dct                           ) {
			Attrs::acquire_from_dct( max_stderr_len , py_dct , "max_stderr_len" , size_t(1) ) ;
		}
		// data
		size_t max_stderr_len = -1 ; // max lines when displaying stderr (full content is shown with lshow -e)
	} ;

	using EvalCtxFuncStr = ::function<void( VarCmd , VarIdx idx , string const& key , string  const& val )> ;
	using EvalCtxFuncDct = ::function<void( VarCmd , VarIdx idx , string const& key , vmap_ss const& val )> ;

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
			::serdes(s,glbs_str  ) ;
			::serdes(s,code_str  ) ;
			::serdes(s,ctx       ) ;
		}
		// data
		bool             is_dynamic = false ;
		T                spec       ;         // contains default values when code does not provide the necessary entries
		::string         glbs_str   ;         // if is_dynamic <=> contains string to run to get the glbs below
		::string         code_str   ;         // if is_dynamic <=> contains string to compile to code object below
		::vector<CmdIdx> ctx        ;         // a list of stems, targets & deps, accessed by code
	} ;
	template<class T> struct Dynamic : DynamicDsk<T> {
		using Base = DynamicDsk<T> ;
		using Base::is_dynamic ;
		using Base::spec       ;
		using Base::glbs_str   ;
		using Base::code_str   ;
		using Base::ctx        ;
		// statics
		static bool s_is_dynamic(PyObject*) ;
		// cxtors & casts
		using Base::Base ;
		Dynamic           (Dynamic const& src) : Base{       src } , glbs{src.glbs} , code{src.code} { Py_XINCREF(glbs)   ; Py_XINCREF(code)   ; }       // mutex is not copiable
		Dynamic           (Dynamic     && src) : Base{::move(src)} , glbs{src.glbs} , code{src.code} { src.glbs = nullptr ; src.code = nullptr ; }       // .
		Dynamic& operator=(Dynamic const& src) {                                                                                                         // .
			Base::operator=(src) ;
			Py_XDECREF(glbs) ; glbs = src.glbs ; Py_XINCREF(glbs) ;
			Py_XDECREF(code) ; code = src.code ; Py_XINCREF(code) ;
			return *this ;
		}
		Dynamic& operator=(Dynamic&& src) {                                                                                                              // .
			Base::operator=(::move(src)) ;
			Py_XDECREF(glbs) ; glbs = src.glbs ; src.glbs = nullptr ;
			Py_XDECREF(code) ; code = src.code ; src.code = nullptr ;
			return *this ;
		}
		// services
		void compile() ;
		//
		T eval( Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs={} ) const ;                                                                   // SimpleMatch is lazy evaluated from Job
		T eval(       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} ) const { return eval( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs ) ; } // cannot lazy evaluate w/o a job
		//
		void eval_ctx( Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs , EvalCtxFuncStr const&     , EvalCtxFuncDct const&     ) const ;       // SimpleMatch is lazy evaluated from Job
		void eval_ctx(       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs , EvalCtxFuncStr const& cbs , EvalCtxFuncDct const& cbd ) const {
			return eval_ctx( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs , cbs , cbd ) ;                                                              // cannot lazy evaluate w/o a job
		}
		::string parse_fstr( ::string const& fstr , Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs={} ) const ;                               // SimpleMatch is lazy evaluated from Job
		::string parse_fstr( ::string const& fstr ,       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} ) const {
			return parse_fstr( fstr , {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs ) ;                                                                 // cannot lazy evaluate w/o a job
		}
	protected :
		PyObject* _eval_code( Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs={} ) const ;
		PyObject* _eval_code(       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} ) const {                                                     // cannot lazy evaluate w/o a job
			return _eval_code( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs ) ;
		}
		// data
	private :
		mutable ::mutex _glbs_mutex ; // ensure glbs is not used for several jobs simultaneously
	public :
		// not stored on disk
		PyObject* glbs = nullptr ;    // if is_dynamic <=> dict to use as globals when executing code
		PyObject* code = nullptr ;    // if is_dynamic <=> python code object to execute with stems as locals and glbs as globals leading to a dict that can be used to build data
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
		DynamicCmd           (DynamicCmd const& src) : Base           {       src } {}                        // only copy disk backed-up part, in particular mutex is not copied
		DynamicCmd           (DynamicCmd     && src) : Base           {::move(src)} {}                        // .
		DynamicCmd& operator=(DynamicCmd const& src) { Base::operator=(       src ) ; return *this ; }        // .
		DynamicCmd& operator=(DynamicCmd     && src) { Base::operator=(::move(src)) ; return *this ; }        // .
		// services
		::pair_ss/*script,call*/ eval( Job , Rule::SimpleMatch      &   , ::vmap_ss const& rsrcs={} ) const ; // SimpleMatch is lazy evaluated from Job
		::pair_ss/*script,call*/ eval(       Rule::SimpleMatch const& m , ::vmap_ss const& rsrcs={} ) const {
			return eval( {} , const_cast<Rule::SimpleMatch&>(m) , rsrcs ) ;                                   // m cannot be lazy evaluated w/o a job
		}
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
			::vector<VarIdx> conflicts = {} ;                                                                  // for target only, the idx of the previous targets that may conflict with this one
		} ;
		// static data
		static size_t s_name_sz ;
		// cxtors & casts
		RuleData(                                        ) = default ;
		RuleData( Special , ::string const& src_dir_s={} ) ;                                                   // src_dir in case Special is SrcDir
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
		bool   user_defined(         ) const { return !allow_ext                                           ; } // used to decide to print in LMAKE/rules
		Tflags tflags      (VarIdx ti) const { SWEAR(ti!=NoVar) ; return matches[ti].second.flags.tflags() ; }
		//
		vmap_view_c_ss static_stems() const { return vmap_view_c_ss(stems).subvec(0,n_static_stems) ; }
		//
		FileNameIdx job_sfx_len() const {
			return
				1                                                                                              // null to disambiguate w/ Node names
			+	n_static_stems * sizeof(FileNameIdx)*2                                                         // pos+len for each stem
			+	sizeof(RuleIdx)                                                                                // Rule index
			;
		}
		// services
		void add_cwd( ::string& file , bool top ) const {
			if (!( top || !file || !cwd_s )) file.insert(0,cwd_s) ;
		}
	private :
		bool _get_cmd_needs_deps() const {                // if deps are needed for cmd computation, then static deps are deemed to be read as accesses are not recorded and we need to be pessimistic
			return
				submit_rsrcs_attrs.is_dynamic
			||	start_cmd_attrs   .is_dynamic
			||	cmd               .is_dynamic
			||	start_rsrcs_attrs .is_dynamic
			||	end_cmd_attrs     .is_dynamic
			;
		}
		::vector_s    _list_ctx  ( ::vector<CmdIdx> const& ctx     ) const ;
		void          _set_crcs  (                                 ) ;
		TargetPattern _mk_pattern( ::string const& , bool for_name ) const ;
		// user data
	public :
		Special              special    = Special::None ;
		Prio                 prio       = 0             ; // the priority of the rule
		::string             name       ;                 // the short message associated with the rule
		::vmap_ss            stems      ;                 // stems are ordered : statics then stars, stems used as both static and star appear twice
		::string             cwd_s      ;                 // cwd in which to interpret targets & deps and execute cmd (with ending /)
		::string             job_name   ;                 // used to show in user messages (not all fields are actually used)
		::vmap_s<MatchEntry> matches    ;                 // keep star user order, static entries first in MatchKind order
		VarIdx               stdout_idx = NoVar         ; // index of target used as stdout
		VarIdx               stdin_idx  = NoVar         ; // index of dep used as stdin
		bool                 allow_ext  = false         ; // if true <=> rule may match outside repo
		// following is only if plain rules
		DynamicDepsAttrs          deps_attrs         ;    // in match crc, evaluated at job creation time
		Dynamic<CreateNoneAttrs > create_none_attrs  ;    // in no    crc, evaluated at job creation time
		Dynamic<CacheNoneAttrs  > cache_none_attrs   ;    // in no    crc, evaluated twice : at submit time to look for a hit and after execution to upload result
		Dynamic<SubmitRsrcsAttrs> submit_rsrcs_attrs ;    // in rsrcs crc, evaluated at submit time
		Dynamic<SubmitNoneAttrs > submit_none_attrs  ;    // in no    crc, evaluated at submit time
		Dynamic<StartCmdAttrs   > start_cmd_attrs    ;    // in cmd   crc, evaluated before execution
		DynamicCmd                cmd                ;    // in cmd   crc, evaluated before execution
		Dynamic<StartRsrcsAttrs > start_rsrcs_attrs  ;    // in rsrcs crc, evaluated before execution
		Dynamic<StartNoneAttrs  > start_none_attrs   ;    // in no    crc, evaluated before execution
		Dynamic<EndCmdAttrs     > end_cmd_attrs      ;    // in cmd   crc, evaluated after  execution
		Dynamic<EndNoneAttrs    > end_none_attrs     ;    // in no    crc, evaluated after  execution
		::vmap_s<DbgEntry>        dbg_info           ;    // in no    crc, contains info to debug cmd that must not appear in cmd crc
		::string                  n_tokens_key       ;    // in no    crc, contains the key in config.n_tokenss to determine n_tokens
		::vector_s                interpreter        ;
		bool                      is_python          = false ;
		bool                      force              = false ;
		// derived data
		bool             cmd_needs_deps   = false ;
		VarIdx           n_static_stems   = 0     ;
		VarIdx           n_static_targets = 0     ;       // number of official static targets
		VarIdx           n_statics        = 0     ;
		size_t           n_tokens         = 1     ;
		// management data
		ExecGen cmd_gen   = 1 ;                           // cmd generation, must be >0 as 0 means !cmd_ok
		ExecGen rsrcs_gen = 1 ;                           // for a given cmd, resources generation, must be >=cmd_gen
		// stats
		mutable Delay  exec_time    = {} ;                // average exec_time
		mutable JobIdx stats_weight = 0  ;                // number of jobs used to compute average
		// not stored on disk
		::vector<VarIdx>        stem_mark_counts ;
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
		::vmap_s<pair_s<AccDflags>> const& deps() const {
			if (!_has_deps) {
				_deps = rule->deps_attrs.eval(*this) ;
				_has_deps = true ;
			}
			return _deps ;
		}
		// services
		::pair_ss       full_name  () const ;
		::string        name       () const { return full_name().first ; }
		::uset<Node>    target_dirs() const ;
		// data
		Rule       rule  ;
		::vector_s stems ; // static stems only of course
		// cache
	private :
		mutable bool                        _has_static_targets = false ;
		mutable bool                        _has_star_targets   = false ;
		mutable bool                        _has_deps           = false ;
		mutable ::umap_s<VarIdx>            _static_targets     ;
		mutable ::vector<Re::RegExpr>       _star_targets       ;
		mutable ::vmap_s<pair_s<AccDflags>> _deps               ;
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
		::string const&    key        (              ) const {                                                          return _matches().first          ; }
		::string const&    target     (              ) const { SWEAR(_matches().second.flags.tflags()[Tflag::Target]) ; return _matches().second.pattern ; }
		//
		Tflags tflags() const { return _matches().second.flags.tflags()                            ; }
		bool   sure  () const { return tgt_idx<(*this)->n_static_targets || tflags()[Tflag::Phony] ; }
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

		template<::integral I> bool/*updated*/ acquire( I& dst , PyObject* py_src , I min , I max ) {
			if (!py_src        ) {           return false ; }
			if (py_src==Py_None) { dst = 0 ; return true  ; }
			//
			PyObject* py_src_long = PyNumber_Long(py_src) ;
			if (!py_src_long) throw "cannot convert to an int"s ;
			int  ovrflw = 0                                             ;
			long v      = PyLong_AsLongAndOverflow(py_src_long,&ovrflw) ;
			Py_DECREF(py_src_long) ;
			if (ovrflw              ) throw "overflow when converting to an int"s ;
			if (::cmp_less   (v,min)) throw "underflow"s                          ;
			if (::cmp_greater(v,max)) throw "overflow"s                           ;
			dst = I(v) ;
			return true ;
		}

		template<StdEnum E> bool/*updated*/ acquire( E& dst , PyObject* py_src ) {
			if (!py_src        ) {                 return false ; }
			if (py_src==Py_None) { dst = E::Dflt ; return true  ; }
			//
			if (!PyUnicode_Check(py_src)) throw "not a str"s ;
			dst = mk_enum<E>(PyUnicode_AsUTF8(py_src)) ;
			return true ;
		}

		template<bool Env> bool/*updated*/ acquire( ::string& dst , PyObject* py_src ) {
			if ( !py_src                 )                                               return false ;
			if (  Env && py_src==Py_None ) {                          dst = EnvDynMrkr ; return true  ; } // special case environment variable to mark dynamic values
			if ( !Env && py_src==Py_None ) { if (!dst) return false ; dst = {}         ; return true  ; }
			//
			bool is_str = PyUnicode_Check(py_src) ;
			if (!is_str) py_src = PyObject_Str(py_src) ;
			if (!py_src) throw "cannot convert to str"s ;
			if (Env) dst = env_encode(PyUnicode_AsUTF8(py_src)) ;                                         // for environment, replace occurrences of lmake & root absolute paths par markers ...
			else     dst =            PyUnicode_AsUTF8(py_src)  ;                                         // ... so as to make repo rebust to moves of lmake or itself
			if (!is_str) Py_DECREF(py_src) ;
			return true ;
		}

		template<class T,bool Env> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vector<T>& dst , PyObject* py_src ) {
			if (!py_src        )             return false ;
			if (py_src==Py_None) { if (!dst) return false ; dst = {} ; return true  ; }
			//
			bool updated = false ;
			if (!PySequence_Check(py_src)) throw "not a sequence"s ;
			PyObject* fast_val = PySequence_Fast(py_src,"") ;
			SWEAR(fast_val) ;
			size_t     n = size_t(PySequence_Fast_GET_SIZE(fast_val)) ;
			PyObject** p =        PySequence_Fast_ITEMS   (fast_val)  ;
			if (n!=dst.size()) {
				updated = true ;
				dst.resize(n) ;
			}
			for( size_t i=0 ; i<n ; i++ ) {
				if (p[i]==Py_None) continue ;
				try {
					if constexpr (Env) updated |= acquire<Env>(dst[i],p[i]) ; // special case for environment where we replace occurrences of lmake & root dirs by markers ...
					else               updated |= acquire     (dst[i],p[i]) ; // ... to make repo robust to moves of lmake or itself
				} catch (::string const& e) {
					throw to_string("for item ",i," : ",e) ;
				}
			}
			return updated ;
		}

		template<class T,bool Env> requires(!Env||::is_same_v<T,string>) bool/*updated*/ acquire( ::vmap_s<T>& dst , PyObject* py_src ) {
			if (!py_src        )             return false ;
			if (py_src==Py_None) { if (!dst) return false ; dst = {} ; return true  ; }
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
				if constexpr (Env)
					if (py_val==Py::g_ellipsis) {
						updated  = true        ;
						map[key] = EnvPassMrkr ;                                    // special case for environment where we put an otherwise illegal marker to ask to pass value from job_exec env
						continue ;
					}
				try {
					auto [it,inserted] = map.emplace(key,T()) ;
					/**/               updated |= inserted                        ;
					if constexpr (Env) updated |= acquire<Env>(it->second,py_val) ; // special case for environment where we replace occurrences of lmake & root dirs by markers ...
					else               updated |= acquire     (it->second,py_val) ; // ... to make repo robust to moves of lmake or itself
				} catch (::string const& e) {
					throw to_string("for item ",key," : ",e) ;
				}
			}
			dst = mk_vmap(map) ;
			return updated ;
		}

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
		spec.init( is_dynamic , PyTuple_GET_ITEM(py_src,0) , var_idxs , ::forward<A>(args)... ) ;
		if (PyTuple_GET_SIZE(py_src)<=1) return ;
		PyObject* fast_val = PySequence_Fast(PyTuple_GET_ITEM(py_src,1),"") ;
		SWEAR(fast_val) ;
		size_t     n = size_t(PySequence_Fast_GET_SIZE(fast_val)) ;
		PyObject** p =        PySequence_Fast_ITEMS   (fast_val)  ;
		for( size_t i=0 ; i<n ; i++ ) {
			CmdIdx ci = var_idxs.at(PyUnicode_AsUTF8(p[i])) ;
			ctx.push_back(ci) ;
		}
		::sort(ctx) ; // stabilize crc's
	}

	template<class T> void Dynamic<T>::compile() {
		if (!is_dynamic) return ;
		Py::Gil gil ;
		code = Py_CompileString( code_str.c_str() , "<code>" , Py_eval_input ) ;
		if (!code) throw to_string("cannot compile code :\n",indent(Py::err_str(),1)) ;
		Py_INCREF(code) ;                                                               // avoid problems at finalization
		glbs = Py::eval_dict(true/*printed_expr*/) ;
		Py_INCREF(glbs) ;                                                               // .
		if (+glbs_str) {
			//
			PyObject* val = PyRun_String(glbs_str.c_str(),Py_file_input,glbs,glbs) ;
			if (!val) throw to_string("cannot compile context :\n",indent(Py::err_str(),1)) ;
			Py_DECREF(val) ;
		}
	}

	static inline void _eval( Job j , Rule::SimpleMatch& m/*lazy*/ , ::vmap_ss const& rsrcs_ , ::vector<CmdIdx> const& ctx , EvalCtxFuncStr const& cb_str , EvalCtxFuncDct const& cb_dct ) {
		::string                    res          ;
		Rule                        r            = +j ? j->rule : m.rule            ;
		::vmap_ss const&            rsrcs_spec   = r->submit_rsrcs_attrs.spec.rsrcs ;
		::vector_s                  matches_bits ;
		::vmap_s<pair_s<AccDflags>> deps_bits    ;
		::umap_ss                   rsrcs_bits   ;
		auto match   = [&]()->Rule::SimpleMatch           const& { { if (!m           ) m            = Rule::SimpleMatch(j) ; } return m             ; } ; // solve lazy evaluation
		auto stems   = [&]()->::vector_s                  const& {                                                              return match().stems ; } ;
		auto matches = [&]()->::vector_s                  const& { { if (!matches_bits) matches_bits = match().py_matches() ; } return matches_bits  ; } ;
		auto deps    = [&]()->::vmap_s<pair_s<AccDflags>> const& { { if (!deps_bits   ) deps_bits    = match().deps()       ; } return deps_bits     ; } ;
		auto rsrcs   = [&]()->::umap_ss                   const& { { if (!rsrcs_bits  ) rsrcs_bits   = mk_umap(rsrcs_)      ; } return rsrcs_bits    ; } ;
		for( auto [vc,i] : ctx ) {
			::vmap_ss dct ;
			switch (vc) {
				case VarCmd::Stem  :                                                                        cb_str(vc,i,r->stems  [i].first,stems  ()[i]             ) ;   break ;
				case VarCmd::Match :                                                                        cb_str(vc,i,r->matches[i].first,matches()[i]             ) ;   break ;
				case VarCmd::Dep   :                                                                        cb_str(vc,i,deps()    [i].first,deps   ()[i].second.first) ;   break ;
				case VarCmd::Rsrc  : { auto it = rsrcs().find(rsrcs_spec[i].first) ; if (it!=rsrcs().end()) cb_str(vc,i,it->first          ,it->second               ) ; } break ;
				//
				case VarCmd::Stems   : for( VarIdx j=0 ; j<r->n_static_stems  ; j++ ) dct.emplace_back(r->stems  [j].first,stems  ()[j]) ; cb_dct(vc,i,"stems"    ,dct   ) ; break ;
				case VarCmd::Targets : for( VarIdx j=0 ; j<r->n_static_targets; j++ ) dct.emplace_back(r->matches[j].first,matches()[j]) ; cb_dct(vc,i,"targets"  ,dct   ) ; break ;
				case VarCmd::Deps    : for( auto const& [k,daf] : deps()            ) dct.emplace_back(k                  ,daf.first   ) ; cb_dct(vc,i,"deps"     ,dct   ) ; break ;
				case VarCmd::Rsrcs   :                                                                                                     cb_dct(vc,i,"resources",rsrcs_) ; break ;
				default : FAIL(vc) ;
			}
		}
	}

	template<class T> void Dynamic<T>::eval_ctx( Job job , Rule::SimpleMatch& match_ , ::vmap_ss const& rsrcs_ , EvalCtxFuncStr const& cb_str , EvalCtxFuncDct const& cb_dct ) const {
		_eval(job,match_,rsrcs_,ctx,cb_str,cb_dct) ;
	}

	template<class T> ::string Dynamic<T>::parse_fstr( ::string const& fstr , Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs ) const {
		::vector<CmdIdx> ctx_  ;
		::vector_s       fixed { 1 } ;
		size_t           fi    = 0   ;
		for( size_t ci=0 ; ci<fstr.size() ; ci++ ) {
			if (fstr[ci]==Rule::StemMrkr) {
				VarCmd vc = decode_enum<VarCmd>(&fstr[ci+1]) ; ci += sizeof(VarCmd) ;
				VarIdx i  = decode_int <VarIdx>(&fstr[ci+1]) ; ci += sizeof(VarIdx) ;
				ctx_ .emplace_back(vc,i) ;
				fixed.emplace_back(    ) ;
				fi++ ;
			} else {
				fixed[fi] += fstr[ci] ;
			}
		}
		fi = 0 ;
		::string res = ::move(fixed[fi++]) ;
		auto cb_str = [&]( VarCmd , VarIdx , string const& /*key*/ , string  const&   val   )->void { append_to_string(res,val,fixed[fi++]) ; } ;
		auto cb_dct = [&]( VarCmd , VarIdx , string const& /*key*/ , vmap_ss const& /*val*/ )->void { FAIL()                                ; } ;
		_eval(job,match,rsrcs,ctx_,cb_str,cb_dct) ;
		return res ;
	}

	template<class T> PyObject* Dynamic<T>::_eval_code( Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs ) const {
		// functions defined in glbs use glbs as their global dict (which is stored in the code object of the functions), so glbs must be modified in place or the job-related values will not
		// be seen by these functions, which is the whole purpose of such dynamic values
		::vector_s to_del ;
		eval_ctx( job , match , rsrcs
		,	[&]( VarCmd , VarIdx , ::string const& key , ::string const& val ) -> void {
				PyObject* py_str = PyUnicode_FromString(val.c_str()) ;
				SWEAR(py_str) ;                                                                    // else, we are in trouble
				swear(PyDict_SetItemString( glbs , key.c_str() , py_str )==0) ;                    // else, we are in trouble
				Py_DECREF(py_str) ;                                                                // py_v is not stolen by PyDict_SetItemString
				to_del.push_back(key) ;
			}
		,	[&]( VarCmd , VarIdx , ::string const& key , ::vmap_ss const& val ) -> void {
				PyObject* py_dct = PyDict_New() ; SWEAR(py_dct) ;
				for( auto const& [k,v] : val ) {
					PyObject* py_v = PyUnicode_FromString(v.c_str()) ;
					SWEAR(py_v) ;                                                                  // else, we are in trouble
					swear(PyDict_SetItemString( py_dct , k.c_str() , py_v )==0) ;                  // else, we are in trouble
					Py_DECREF(py_v) ;                                                              // py_v is not stolen by PyDict_SetItemString
				}
				swear(PyDict_SetItemString( glbs , key.c_str() , py_dct )==0) ;                    // else, we are in trouble
				Py_DECREF(py_dct) ;                                                                // py_v is not stolen by PyDict_SetItemString
				to_del.push_back(key) ;
			}
		) ;
		//            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		PyObject* d = PyEval_EvalCode( code , glbs , nullptr ) ;
		//            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		for( ::string const& key : to_del ) swear(PyDict_DelItemString( glbs , key.c_str() )==0) ; // else, we are in trouble, delete job-related info, just to avoid percolation to other jobs
		//
		if (!d) throw Py::err_str() ;
		return d ;
	}

	template<class T> T Dynamic<T>::eval( Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs ) const {
		if (!is_dynamic) return spec ;
		T         res = spec                        ;
		Py::Gil   gil ;
		PyObject* d   = _eval_code(job,match,rsrcs) ;
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
		::serdes(s,matches         ) ;
		::serdes(s,stdout_idx      ) ;
		::serdes(s,stdin_idx       ) ;
		::serdes(s,allow_ext       ) ;
		::serdes(s,cmd_needs_deps  ) ;
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
			::serdes(s,dbg_info          ) ;
			::serdes(s,n_tokens_key      ) ;
			::serdes(s,interpreter       ) ;
			::serdes(s,is_python         ) ;
			::serdes(s,force             ) ;
			::serdes(s,cmd_gen           ) ;
			::serdes(s,rsrcs_gen         ) ;
			::serdes(s,exec_time         ) ;
			::serdes(s,stats_weight      ) ;
		}
		if (is_base_of_v<::istream,S>) _compile() ;
	}

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
