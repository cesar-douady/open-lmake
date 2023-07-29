// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// included 3 times : with DEF_STRUCT defined, then with DATA_DEF defined, then with IMPL defined

#include "pycxx.hh"

#include "store/prefix.hh"
#include "store/vector.hh"
#include "store/struct.hh"

#ifdef STRUCT_DECL
namespace Engine {

	struct Rule     ;
	struct RuleData ;

	struct RuleTgt ;

	ENUM( Special
	,	Plain      //  must be 0, a marker to say that rule is not special
	,	Src
	,	Req
	,	Uphill
	,	Infinite
	)

	ENUM_1( EnvFlag
	,	Dflt = Rsrc
	,	None                           // ignore variable
	,	Rsrc                           // consider variable as a resource : upon modification, rebuild job if it was in error
	,	Cmd                            // consider variable as a cmd      : upon modification, rebuild job
	)

}
#endif
#ifdef STRUCT_DEF
namespace Engine {

	struct Rule : RuleBase {
		friend ::ostream& operator<<( ::ostream& os , Rule const r ) ;
		static constexpr char JobMrkr  = 0 ;                                   // ensure no ambiguity between job names and node names
		static constexpr char StarMrkr = 0 ;                                   // signal a star stem in job_name
		static constexpr char StemMrkr = 0 ;                                   // signal a stem in job_name & targets & deps
		static constexpr VarIdx NoVar = -1 ;
		//
		struct SimpleMatch ;
		struct Match       ;
		// cxtors & casts
		using RuleBase::RuleBase ;
		Rule(RuleBase const& rb     ) : RuleBase{ rb                                                  } {                                      }
		Rule(::string const& job_sfx) : Rule    { to_int<Idx>( &job_sfx[job_sfx.size()-sizeof(Idx)] ) } { SWEAR(job_sfx.size()>=sizeof(Idx)) ; }
		// acesses
		::string job_sfx() const ;
		// services
		void new_job_exec_time( Delay , Tokens ) ;
	} ;

}
#endif
#ifdef DATA_DEF
namespace Engine {

	struct Attrs {
		template<class T> static bool s_easy( T& dst , PyObject* py_src , T const& dflt ) {
			if (!py_src        ) {              return true ; }
			if (py_src==Py_None) { dst = dflt ; return true ; }
			return false ;
		}
		template<class T> static void s_acquire_from_dct( T& dst , PyObject* py_dct , ::string const& key ) {
			try                       { s_acquire( dst , PyDict_GetItemString(py_dct,key.c_str()) ) ; }
			catch (::string const& e) { throw to_string("while processing ",key," : ",e) ;            }
		}
		/**/                   static void s_acquire( bool       & dst , PyObject* py_src ) ;
		/**/                   static void s_acquire( Time::Delay& dst , PyObject* py_src ) ;
		/**/                   static void s_acquire( ::string   & dst , PyObject* py_src ) ;
		template<class      T> static void s_acquire( ::vector<T>& dst , PyObject* py_src ) ;
		template<class      T> static void s_acquire( ::vmap_s<T>& dst , PyObject* py_src ) ;
		template<::integral I> static void s_acquire( I          & dst , PyObject* py_src ) ;
		template<StdEnum    E> static void s_acquire( E          & dst , PyObject* py_src ) ;
	} ;

	// used at match time
	struct CreateMatchAttrs : Attrs {
		struct DepSpec {
			template<IsStream S> void serdes(S& s) {
				::serdes(s,pattern) ;
				::serdes(s,flags  ) ;
			}
			::string pattern ;
			DFlags   flags   ;
		} ;
		// statics
		static ::pair<string,DFlags> s_split_dflags( ::string const& key , PyObject* py_dep ) ;
		// services
		void update( PyObject* , RuleData const& , ::umap_s<VarIdx> const& static_stem_idxs , VarIdx n_static_unnamed_stems ) ;
		template<IsStream S> void serdes(S& s) {
			::serdes(s,deps) ;
		}
		::vmap<Node,DFlags> mk( Rule , ::vector_s const& stems , PyObject* py_src=nullptr ) const ;
		// data
		bool               full_dynamic = false ;          // if true <=> deps is empty and new keys can be added, else dynamic deps must be within dep keys
		::vmap_s<DepSpec> deps         ;
	} ;

	// used at match time, but participate in nothing
	struct CreateNoneAttrs : Attrs {
		void update(PyObject* py_src) {
			s_acquire_from_dct(tokens,py_src,"job_tokens") ;
		}
		// data
		Tokens tokens = 1 ;
	} ;

	// used at force time (i.e. when deciding whether to launch job), but participate in cmd
	struct ForceCmdAttrs : Attrs {
		void update(PyObject* py_src) {
			s_acquire_from_dct(force,py_src,"force") ;
		}
		// data
		bool force = false ;
	} ;

	// used at submit time, participate in resources
	struct SubmitRsrcsAttrs : Attrs {
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,backend) ;
			::serdes(s,rsrcs  ) ;
		}
		void update(PyObject* py_src) {
			s_acquire_from_dct(backend,py_src,"backend") ;
			s_acquire_from_dct(rsrcs  ,py_src,"rsrcs"  ) ;
			::sort(rsrcs) ;                                                    // stabilize rsrcs crc
		}
		// data
		BackendTag backend = BackendTag::Local ;           // backend to use to launch jobs
		::vmap_ss  rsrcs   ;
	} ;

	// used both at submit time (for cache look up) and at end of execution (for cache upload)
	struct CacheNoneAttrs : Attrs {
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,key) ;
		}
		void update(PyObject* py_src) {
			s_acquire_from_dct(key,py_src,"key") ;
			if ( !key.empty() && !Cache::s_tab.contains(key) ) throw to_string("unexpected cache key ",key," not found in config") ;
		}
		// data
		::string key ;
	} ;

	// used at start time, participate in cmd
	struct StartCmdAttrs : Attrs {
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,auto_mkdir ) ;
			::serdes(s,ignore_stat) ;
			::serdes(s,method     ) ;
			::serdes(s,chroot     ) ;
			::serdes(s,interpreter) ;
			::serdes(s,env        ) ;
		}
		void update(PyObject* py_src) {
			s_acquire_from_dct(auto_mkdir ,py_src,"auto_mkdir" ) ;
			s_acquire_from_dct(ignore_stat,py_src,"ignore_stat") ;
			s_acquire_from_dct(method     ,py_src,"autodep"    ) ;
			s_acquire_from_dct(chroot     ,py_src,"chroot"     ) ;
			s_acquire_from_dct(interpreter,py_src,"interpreter") ;
			s_acquire_from_dct(env        ,py_src,"env"        ) ;
			switch (method) {
				case AutodepMethod::None      :                                                                                 break ;
				case AutodepMethod::Ptrace    : if (!HAS_PTRACE  ) throw to_string(method," is not supported on this system") ; break ;
				case AutodepMethod::LdAudit   : if (!HAS_LD_AUDIT) throw to_string(method," is not supported on this system") ; break ;
				case AutodepMethod::LdPreload :                                                                                 break ;
				default : throw to_string("unexpected value : ",method) ;
			}
			::sort(env) ;                                                      // stabilize cmd crc
		}
		// data
		bool          auto_mkdir  = false               ;
		bool          ignore_stat = false               ;
		AutodepMethod method      = AutodepMethod::Dflt ;
		::string      chroot      ;
		::vector_s    interpreter ;
		::vmap_ss     env         ;
	} ;

	// used at start time, participate in resources
	struct StartRsrcsAttrs : Attrs {
		template<IsStream S> void serdes(S& s) {
			::serdes(s,timeout) ;
			::serdes(s,env    ) ;
		}
		void update(PyObject* py_src) {
			s_acquire_from_dct(timeout,py_src,"timeout") ;
			s_acquire_from_dct(env    ,py_src,"env"    ) ;
			if (timeout<Delay()) throw "timeout must be positive or null (no timeout if null)"s ;
			::sort(env) ;                                                                         // stabilize rsrcs crc
		}
		// data
		Time::Delay timeout ;          // if 0 <=> no timeout, maximum time allocated to job execution in s
		::vmap_ss   env     ;
	} ;

	// used at start time, participate to nothing
	struct StartNoneAttrs : Attrs {
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes(s,keep_tmp   ) ;
			::serdes(s,start_delay) ;
			::serdes(s,kill_sigs  ) ;
			::serdes(s,env        ) ;
		}
		void update(PyObject* py_src) {
			s_acquire_from_dct(keep_tmp   ,py_src,"keep_tmp"   ) ;
			s_acquire_from_dct(start_delay,py_src,"start_delay") ;
			s_acquire_from_dct(kill_sigs  ,py_src,"kill_sigs"  ) ;
			s_acquire_from_dct(env        ,py_src,"env"        ) ;
			::sort(env) ;                                                      // by symmetry with env entries in StartCmdAttrs and StartRsrcsAttrs
		}
		// data
		bool              keep_tmp    = false ;
		Time::Delay       start_delay ;                    // job duration above which a start message is generated
		::vector<uint8_t> kill_sigs   ;                    // signals to use to kill job (tried in sequence, 1s apart from each other)
		::vmap_ss         env         ;
	} ;

	// used at end of job execution, participate in cmd
	struct EndCmdAttrs : Attrs {
		// services
		void update(PyObject* py_src) {
			s_acquire_from_dct(allow_stderr,py_src,"allow_stderr") ;
		}
		// data
		bool allow_stderr = false ;    // if true <=> non empty stderr does not imply job error
	} ;

	// used at end of job execution, participate in nothing
	struct EndNoneAttrs : Attrs {
		// services
		void update(PyObject* py_src) {
			s_acquire_from_dct(stderr_len,py_src,"stderr_len") ;
		}
		// data
		size_t stderr_len = -1 ;       // max lines when displaying stderr (full content is shown with lshow -e)
	} ;

	template<class T> struct Dynamic {
		// statics
		static bool s_is_dynamic(PyObject*) ;
		// cxtors & casts
		Dynamic() = default ;
		template<class... A> Dynamic( PyObject* , ::umap_s<pair<CmdVar,VarIdx>> const& var_idxs , A&&... ) ;
		// services
		void compile(                                 ) ;
		T    eval   (Job , ::vector_s const& rsrcs={} ) const ;
		//
		template<IsStream S> void serdes(S& s) {
			::serdes(s,is_dynamic  ) ;
			::serdes(s,need_stems  ) ;
			::serdes(s,need_targets) ;
			::serdes(s,need_deps   ) ;
			::serdes(s,need_rsrcs  ) ;
			::serdes(s,spec        ) ;
			::serdes(s,glbs_str    ) ;
			::serdes(s,code_str    ) ;
			::serdes(s,ctx         ) ;
		}
	protected :
		PyObject* _mk_dict( Rule , ::vector_s const& stems , ::vector_s const& targets={} , ::vector_view_c<Dep> const& deps={} , ::vector_s const& rsrcs={} ) const ;
		// data
	public :
		bool                          is_dynamic   = false ;
		bool                          need_stems   = false ;
		bool                          need_targets = false ;
		bool                          need_deps    = false ;
		bool                          need_rsrcs   = false ;
		T                             spec         ;                           // contains default values when code does not provide the necessary entries
		::string                      glbs_str     ;                           // if is_dynamic <=> contains string to run to get the glbs below
		::string                      code_str     ;                           // if is_dynamic <=> contains string to compile to code object below
		::vector<pair<CmdVar,VarIdx>> ctx          ;                           // a list of stems, targets & deps, accessed by code
		// not stored on disk
		PyObject* glbs = nullptr ;     // if is_dynamic <=> dict to use as globals when executing code
		PyObject* code = nullptr ;     // if is_dynamic <=> python code object to execute with stems as locals and glbs as globals leading to a dict that can be used to build data
	} ;

	struct DynamicCreateMatchAttrs : Dynamic<CreateMatchAttrs> {
		// cxtors & casts
		using Dynamic<CreateMatchAttrs>::Dynamic ;
		// services
		::vmap<Node,DFlags> eval( Rule , ::vector_s const& stems ) const ;
	} ;

	struct RuleData {
		friend ::ostream& operator<<( ::ostream& , RuleData const& ) ;
		friend Rule ;
		static constexpr VarIdx NoVar = Rule::NoVar ;

		// statics
		static bool s_sure(TFlags flags) { return !flags[TFlag::Star] || flags[TFlag::Phony] ; } // if phony, a target is deemed generated, even if it does not exist, hence it is sure
		// static data
		static size_t s_name_sz ;

		// cxtors & casts
		RuleData(                        ) = default ;
		RuleData(Special                 ) ;
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
	public :
		::string pretty_str() const ;

		// accesses
		TFlags flags(VarIdx t) const { return t==NoVar ? UnexpectedTFlags : targets[t].second.flags ; }
		bool   sure (VarIdx t) const { return s_sure(flags(t))                                      ; }
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
		::pair<vmap_ss,vmap_s<vmap_ss>> eval_ctx(
			::vmap<CmdVar,VarIdx> const& ctx
		,	::vector_s            const& stems
		,	::vector_s            const& targets
		,	::vector_view_c<Dep>  const& deps
		,	::vector_s            const& rsrcs
		) const ;
		::string add_cwd(::string&& file) const {
			if      (file[0]=='/' ) return file.substr(1) ;
			else if (cwd_s.empty()) return ::move(file)   ;                    // fast path
			else                    return cwd_s+file     ;
		}
	private :
		void _set_crcs() ;

		// user data
	public :
		bool                 anti       = false ;                              // this is an anti-rule
		Prio                 prio       = 0     ;                              // the priority of the rule
		::string             name       ;                                      // the short message associated with the rule
		::vmap_ss            stems      ;                                      // stems   are ordered : statics then stars
		::string             cwd_s      ;                                      // cwd in which to interpret targets & deps and execute cmd (with ending /)
		::string             job_name   ;                                      // used to show in user messages, same format as a target
		::vmap_s<TargetSpec> targets    ;                                      // keep user order, except static targets before star targets
		VarIdx               stdout_idx = NoVar ;                              // index of target used as stdout
		VarIdx               stdin_idx  = NoVar ;                              // index of dep used as stdin
		// following is only if !anti
		DynamicCreateMatchAttrs       create_match_attrs ;                     // in match crc, evaluated at job creation time
		Dynamic<CreateNoneAttrs >     create_none_attrs  ;                     // in no    crc, evaluated at job creation time
		Dynamic<ForceCmdAttrs   >     force_cmd_attrs    ;                     // in cmd   crc, evaluated at job analysis time
		Dynamic<CacheNoneAttrs  >     cache_none_attrs   ;                     // in no    crc, evaluated twice : at submit time to look for a hit and after execution to upload result
		Dynamic<SubmitRsrcsAttrs>     submit_rsrcs_attrs ;                     // in rsrcs crc, evaluated at submit time
		Dynamic<StartCmdAttrs   >     start_cmd_attrs    ;                     // in cmd   crc, evaluated before execution
		Dynamic<StartRsrcsAttrs >     start_rsrcs_attrs  ;                     // in rsrcs crc, evaluated before execution
		Dynamic<StartNoneAttrs  >     start_none_attrs   ;                     // in no    crc, evaluated before execution
		Dynamic<EndCmdAttrs     >     end_cmd_attrs      ;                     // in cmd   crc, evaluated after  execution
		Dynamic<EndNoneAttrs    >     end_none_attrs     ;                     // in no    crc, evaluated after  execution
		::vector<pair<CmdVar,VarIdx>> cmd_ctx            ;                     // a list of stems, targets, deps, rsrcs & tokens accessed by cmd
		bool                          is_python          = false ;             // if true <=> cmd is a Python script
		::string                      cmd                ;
		size_t                        n_tokens           = 1     ;             // available tokens for this rule, used to estimate req ETE (cannot be dynamic)
		// derived data
		bool    has_stars        = false ;
		VarIdx  n_static_stems   = 0     ;
		VarIdx  n_static_targets = 0     ;
		// management data
		ExecGen cmd_gen   = ExecGenForce+1 ;                                   // cmd generation, must be >0 as 0 means !cmd_ok and >ExecGenForce as this means forced jobs
		ExecGen rsrcs_gen = ExecGenForce+1 ;                                   // for a given cmd, resources generation, must be >=cmd_gen
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
		::vector_s const& targets       () const { if (!_has_targets) { _compute_targets() ; _has_targets = true ; } return _targets ; }
		::vector_view_c_s static_targets() const { return {targets(),0,rule->n_static_targets} ;}
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
		mutable bool _has_targets = false ; mutable ::vector_s _targets ;
	} ;

	struct Rule::Match : SimpleMatch {
		friend ::ostream& operator<<( ::ostream& , Match const& ) ;
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
		Match( SimpleMatch const& sm            ) : SimpleMatch(sm) {}
		Match( RuleTgt , ::string const& target ) ;
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
		TFlags flags() const { return (*this)->flags(tgt_idx) ; }
		bool   sure () const { return (*this)->sure (tgt_idx) ; }
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
		from_int( &res[res.size()-sizeof(Idx)] , +*this ) ;
		return res ;
	}

	//
	// Attrs
	//

	template<::integral I> void Attrs::s_acquire( I& dst , PyObject* py_src ) {
		if (s_easy(dst,py_src,I(0))) return ;
		//
		PyObject* py_src_long = PyNumber_Long(py_src) ;
		if (!py_src_long) throw "cannot convert to an int" ;
		int  ovrflw = 0                                             ;
		long v      = PyLong_AsLongAndOverflow(py_src_long,&ovrflw) ;
		Py_DECREF(py_src_long) ;
		if (ovrflw                                     ) throw "overflow when converting to an int"s ;
		if (::cmp_less   (v,::numeric_limits<I>::min())) throw "underflow"s                          ;
		if (::cmp_greater(v,::numeric_limits<I>::max())) throw "overflow"s                           ;
		dst = I(v) ;
	}

	template<StdEnum E> void Attrs::s_acquire( E& dst , PyObject* py_src ) {
		if (s_easy(dst,py_src,E::Dflt)) return ;
		//
		if (!PyUnicode_Check(py_src)) throw "not a str"s ;
		dst = mk_enum<E>(PyUnicode_AsUTF8(py_src)) ;
	}

	template<class T> void Attrs::s_acquire( ::vector<T>& dst , PyObject* py_src ) {
		if (s_easy(dst,py_src,{})) return ;
		//
		if (!PySequence_Check(py_src)) throw "not a sequence"s ;
		PyObject* fast_val = PySequence_Fast(py_src,"") ;
		SWEAR(fast_val) ;
		size_t     n = size_t(PySequence_Fast_GET_SIZE(fast_val)) ;
		PyObject** p =        PySequence_Fast_ITEMS   (fast_val)  ;
		for( size_t i=0 ; i<n ; i++ ) {
			if (p[i]==Py_None) continue ;
			if (i>=dst.size()) dst.push_back(T()) ;
			try                       { s_acquire(dst.back(),p[i]) ;             }
			catch (::string const& e) { throw to_string("for item ",i," : ",e) ; }
		}
		dst.resize(n) ;
	}

	template<class T> void Attrs::s_acquire( ::vmap_s<T>& dst , PyObject* py_src ) {
		if (s_easy(dst,py_src,{})) return ;
		//
		::map_s<T> map = mk_map(dst) ;
		if (!PyDict_Check(py_src)) throw "not a dict";
		PyObject*  py_key = nullptr/*garbage*/ ;
		PyObject*  py_val = nullptr/*garbage*/ ;
		ssize_t    pos    = 0                  ;
		while (PyDict_Next( py_src , &pos , &py_key , &py_val )) {
			if (!PyUnicode_Check(py_key)) throw "key is not a str"s ;
			const char* key = PyUnicode_AsUTF8(py_key) ;
			if (py_val==Py_None) { map.erase(key) ; continue ; }
			try { s_acquire(map[key],py_val) ; }
			catch (::string const& e) { throw to_string("for item ",key," : ",e) ; }
		}
		dst = mk_vmap(map) ;
	}

	//
	// Dynamic
	//

	template<class T> bool Dynamic<T>::s_is_dynamic(PyObject* py_src) {
		SWEAR(PyTuple_Check(py_src)) ;
		ssize_t sz = PyTuple_GET_SIZE(py_src) ;
		switch (sz) {
			case 1  :
				return false ;
			case 4  :
				SWEAR(PyUnicode_Check (PyTuple_GET_ITEM(py_src,1))) ;
				SWEAR(PyUnicode_Check (PyTuple_GET_ITEM(py_src,2))) ;
				SWEAR(PySequence_Check(PyTuple_GET_ITEM(py_src,3))) ;
				return true ;
			default :
				FAIL(sz) ;
		}
	}

	template<class T> template<class... A> Dynamic<T>::Dynamic( PyObject* py_src , ::umap_s<pair<CmdVar,VarIdx>> const& var_idxs , A&&... args ) :
		is_dynamic{ s_is_dynamic(py_src)                                           }
	,	glbs_str  { is_dynamic ? PyUnicode_AsUTF8(PyTuple_GET_ITEM(py_src,1)) : "" }
	,	code_str  { is_dynamic ? PyUnicode_AsUTF8(PyTuple_GET_ITEM(py_src,2)) : "" }
	{
		spec.update( PyTuple_GET_ITEM(py_src,0) , ::forward<A>(args)... ) ;
		if (!is_dynamic) return ;
		PyObject* fast_val = PySequence_Fast(PyTuple_GET_ITEM(py_src,3),"") ;
		SWEAR(fast_val) ;
		size_t     n = size_t(PySequence_Fast_GET_SIZE(fast_val)) ;
		PyObject** p =        PySequence_Fast_ITEMS   (fast_val)  ;
		for( size_t i=0 ; i<n ; i++ ) {
			::pair<CmdVar,VarIdx> idx = var_idxs.at(PyUnicode_AsUTF8(p[i])) ;
			ctx.push_back(idx) ;
			switch (idx.first) {
				case CmdVar::Stems   : case CmdVar::Stem   : need_stems   = true ; break ;
				case CmdVar::Targets : case CmdVar::Target : need_targets = true ; break ;
				case CmdVar::Deps    : case CmdVar::Dep    : need_deps    = true ; break ;
				case CmdVar::Rsrcs   : case CmdVar::Rsrc   : need_rsrcs   = true ; break ;
				default : FAIL(idx.first) ;
			}
		}
	}

	template<class T> void Dynamic<T>::compile() {
		if (!is_dynamic) return ;
		Py::Gil gil ;
		code = Py_CompileString( code_str.c_str() , "<code>" , Py_eval_input ) ;
		if (!code) throw to_string("cannot compile code :\n",indent(Py::err_str(),1)) ;
		Py::mk_static(code) ;
		//
		glbs = Py::mk_static(PyDict_New())                                   ;
		PyDict_SetItemString( glbs , "inf"          , *Py::Float(Infinity) ) ;
		PyDict_SetItemString( glbs , "nan"          , *Py::Float(nan("") ) ) ;
		PyDict_SetItemString( glbs , "__builtins__" , PyEval_GetBuiltins() ) ;   // Python3.6 does not provide it for us
		PyObject* val = PyRun_String(glbs_str.c_str(),Py_file_input,glbs,glbs) ;
		if (!val) throw to_string("cannot compile context :\n",indent(Py::err_str(),1)) ;
		Py_DECREF(val) ;
	}

	template<class T> PyObject* Dynamic<T>::_mk_dict( Rule r , ::vector_s const& stems , ::vector_s const& targets , ::vector_view_c<Dep> const& deps , ::vector_s const& rsrcs ) const {
		::pair<vmap_ss,vmap_s<vmap_ss>> ctx_ = r->eval_ctx( ctx , stems , targets , deps , rsrcs ) ;
		//
		PyObject* lcls = PyDict_New() ; SWEAR(lcls) ;                          // else, we are in trouble
		//
		for( auto const& [key,str] : ctx_.first ) {
			PyObject* py_str = PyUnicode_FromString(str.c_str()) ;
			SWEAR(py_str) ;                                                    // else, we are in trouble
			swear(PyDict_SetItemString( lcls , key.c_str() , py_str )==0) ;    // else we are in trouble
			Py_DECREF(py_str) ;                                                // py_v is not stolen by PyDict_SetItemString
		}
		//
		for( auto const& [key,dct] : ctx_.second ) {
			PyObject* py_dct = PyDict_New() ; SWEAR(py_dct) ;
			for( auto const& [k,v] : dct ) {
				PyObject* py_v = PyUnicode_FromString(v.c_str()) ;
				SWEAR(py_v) ;                                                  // else, we are in trouble
				swear(PyDict_SetItemString( py_dct , k.c_str() , py_v )==0) ;  // else we are in trouble
				Py_DECREF(py_v) ;                                              // py_v is not stolen by PyDict_SetItemString
			}
			swear(PyDict_SetItemString( lcls , key.c_str() , py_dct )==0) ;    // else we are in trouble
			Py_DECREF(py_dct) ;                                                // py_v is not stolen by PyDict_SetItemString
		}
		//
		PyObject* d = PyEval_EvalCode( code , glbs , lcls ) ;
		Py_DECREF(lcls) ;
		if (!d) throw Py::err_str() ;
		SWEAR(PyDict_Check(d)) ;
		return d ;
	}

	template<class T> T Dynamic<T>::eval(Job j , ::vector_s const& rsrcs ) const {
		if (!is_dynamic) return spec ;
		//
		Py::Gil   gil ;
		PyObject* d   = nullptr/*garbage*/ ;
		//
		if ( need_stems || need_targets ) {
			Rule::SimpleMatch match_{j} ;
			if (need_targets) d = _mk_dict( j->rule , match_.stems , match_.targets() , j->deps , rsrcs ) ;
			else              d = _mk_dict( j->rule , match_.stems , {}/*targets*/    , j->deps , rsrcs ) ; // fast path : no need to compute targets
		} else {
			/**/              d = _mk_dict( j->rule , {}/*stems*/  , {}/*targets*/    , j->deps , rsrcs ) ; // fast path : no need to compute match_
		}
		T res = spec ;
		res.update(d) ;
		Py_DECREF(d) ;
		return res  ;
	}

	inline ::vmap<Node,DFlags> DynamicCreateMatchAttrs::eval( Rule r , ::vector_s const& stems ) const {
		if (!is_dynamic) return spec.mk(r,stems) ;
		SWEAR( !need_deps && !need_rsrcs ) ;
		Py::Gil   gil ;
		PyObject* d   ;
		if (need_targets) d = _mk_dict( r , stems , Rule::SimpleMatch(r,stems).targets() ) ;
		else              d = _mk_dict( r , stems                                        ) ;
		::vmap<Node,DFlags> res = spec.mk(r,stems,d) ;
		Py_DECREF(d) ;
		return res  ;
	}

	//
	// RuleData
	//

	template<IsStream S> void RuleData::serdes(S& s) {
		if (::is_base_of_v<::istream,S>) *this = {} ;
		::serdes(s,anti) ;
		//
		::serdes(s,prio            ) ;
		::serdes(s,name            ) ;
		::serdes(s,stems           ) ;
		::serdes(s,cwd_s           ) ;
		::serdes(s,job_name        ) ;
		::serdes(s,targets         ) ;
		::serdes(s,stdout_idx      ) ;
		::serdes(s,stdin_idx       ) ;
		::serdes(s,has_stars       ) ;
		::serdes(s,n_static_stems  ) ;
		::serdes(s,n_static_targets) ;
		if (!anti) {
			::serdes(s,create_match_attrs) ;
			::serdes(s,create_none_attrs ) ;
			::serdes(s,force_cmd_attrs   ) ;
			::serdes(s,cache_none_attrs  ) ;
			::serdes(s,submit_rsrcs_attrs) ;
			::serdes(s,start_cmd_attrs   ) ;
			::serdes(s,start_rsrcs_attrs ) ;
			::serdes(s,start_none_attrs  ) ;
			::serdes(s,end_cmd_attrs     ) ;
			::serdes(s,end_none_attrs    ) ;
			::serdes(s,cmd_ctx           ) ;
			::serdes(s,is_python         ) ;
			::serdes(s,cmd               ) ;
			::serdes(s,n_tokens          ) ;
			::serdes(s,cmd_gen           ) ;
			::serdes(s,rsrcs_gen         ) ;
			::serdes(s,exec_time         ) ;
			::serdes(s,stats_weight      ) ;
		}
		if (is_base_of_v<::istream,S>) _compile() ;
	}

	//
	// Rule::Match
	//

	inline VarIdx Rule::Match::idx(::string const& target) const {
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
