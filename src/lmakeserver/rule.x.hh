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

	struct RuleData {
		friend ::ostream& operator<<( ::ostream& , RuleData const& ) ;
		friend Rule ;
		static constexpr VarIdx NoVar = VarIdx(-1) ;

		struct DepSpec {
			friend ::ostream& operator<<( ::ostream& , DepSpec const& ) ;
			// cxtors & casts
			DepSpec(                                             ) = default ;
			DepSpec( ::string const& p , bool ic , DFlags dfs={} ) : pattern{p} , flags{dfs} , is_code(ic) {}
			template<IsStream S> void serdes(S& s) {
				if (is_base_of_v<::istream,S>) code = nullptr ;
				::serdes(s,pattern) ;
				::serdes(s,flags  ) ;
				::serdes(s,is_code) ;
			}
			// services
			bool              operator== (DepSpec const&      ) const = default ;
			::strong_ordering operator<=>(DepSpec const& other) const {
				if (pattern!=other.pattern) return pattern<=>other.pattern ;
				/**/                        return +flags <=>+other.flags  ;
			}
			// data
			::string pattern ;
			DFlags   flags   ;
			bool     is_code = false ;
			// not stored on disk
			PyObject* code = nullptr ; // code object when dep is a f-string
		} ;

		struct DepsSpec {
			friend ::ostream& operator<<( ::ostream& , DepsSpec const& ) ;
			// cxtors & casts
			DepsSpec(                                                                                         ) = default ;
			DepsSpec( ::string const& p , ::vector<pair<CmdVar,VarIdx>> const& c , ::vmap_s<DepSpec> const& d ) : prelude{p} , ctx(c) , dct{d} {}
			template<IsStream S> void serdes(S& s) {
				if (is_base_of_v<::istream,S>) env = nullptr ;
				::serdes(s,prelude) ;
				::serdes(s,ctx    ) ;
				::serdes(s,dct    ) ;
			}
			// services
			bool              operator== (DepsSpec const&) const = default ;
			::strong_ordering operator<=>(DepsSpec const&) const = default ;
			// data
			::string                      prelude ;                            // code to execute to provide the necessary context to evaluate deps when f-strings
			::vector<pair<CmdVar,VarIdx>> ctx     ;                            // a list of stems & targets, accessed by dep f-strings
			::vmap_s<DepSpec>             dct     ;                            // maps Dep name to pattern using {Key} notation found in targets
			// not stored on disk
			PyObject* env = nullptr ;                                          // dict resulting from prelude execution
		} ;

		struct EnvSpec {
			friend ::ostream& operator<<( ::ostream& , EnvSpec const& ) ;
			// cxtors & casts
			EnvSpec() = default ;
			EnvSpec( ::string const& v , EnvFlag f ) : val{v} , flag{f} {}
			// services
			template<IsStream S> void serdes(S& s) {
				::serdes(s,val ) ;
				::serdes(s,flag) ;
			}
			// data
			::string val  ;
			EnvFlag  flag ;
		} ;

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
			_update_sz() ;
		}
		RuleData(Py::Dict const& dct) {
			_acquire_py          (dct) ;
			_compile_derived_info(   ) ;
			_update_sz           (   ) ;
		}
		operator ::string() const {
			return serialize(*this) ;
		}
		template<IsStream S> void serdes(S&) ;
	private :
		void _acquire_py          (Py::Dict const&) ;
		void _compile_derived_info(               ) ;
		void _update_sz           (               ) { s_name_sz = ::max(s_name_sz,name.size()) ; }
		//
	public :
		::string pretty_str() const ;

		// accesses
		TFlags flags(VarIdx t) const { return targets[t].second.flags ; }
		bool   sure (VarIdx t) const { return s_sure(flags(t))        ; }
		//
		vmap_view_c_ss static_stems() const { return vmap_view_c_ss(stems).subvec(0,n_static_stems) ; }
		//
		::string user_name() const { return is_identifier(name) ? name : to_string('"',name,'"') ; }
		Delay    margin   () const ;
		VarIdx   n_deps   () const { return no_deps ? 0 : deps.dct.size()                        ; }
		VarIdx   n_rsrcs  () const { return rsrcs.dct.size()                                     ; }

		// services
		Crc match_crc() const ;
		Crc cmd_crc  () const ;
		Crc rsrcs_crc() const ;
		//
	private :
		void _compile_dep_code( ::string const& key , RuleData::DepSpec &                  ) ;
		void _mk_deps         ( ::string const& key , RuleData::DepsSpec& , bool need_code ) ;

		// user data
	public :
		bool                          anti           = false                  ; // this is an anti-rule
		::string                      job_name       ;                          // used to show in user messages, same format as a target
		::string                      name           ;                          // the short message associated with the rule
		::vmap_ss                     stems          ;                          // stems   are ordered : statics then stars
		::vmap_s<TargetSpec>          targets        ;                          // keep user order, except static targets before star targets
		// following is only if !anti
		bool                          allow_stderr   = false                  ; // writing to stderr is allowed
		AutodepMethod                 autodep_method = AutodepMethod::Unknown ;
		bool                          auto_mkdir     = false                  ; // auto make dir in case of chdir
		Backends::Tag                 backend        = Backends::Tag::Local   ; // backend to use to launch jobs
		::string                      chroot         ;                          // chroot in which to execute cmd
		::vector<pair<CmdVar,VarIdx>> cmd_ctx        ;                          // a list of stems, targets, deps, rsrcs & tokens accessed by cmd
		::string                      cwd            ;                          // cwd in which to execute cmd
		DepsSpec                      deps           ;
		::vmap_s<EnvSpec>             env            ;                          // a list of environment variables with an EnvFlag
		bool                          force          = false                  ; // jobs are never up-to-date
		bool                          ignore_stat    = false                  ; // stat-like syscalls do not trigger dependencies
		::vector_s                    interpreter    ;                          // actual interpreter to interpret script
		bool                          is_python      = false                  ; // if true <=> script is a Python script
		DepSpec                       job_tokens     ;                          // tokens for a job
		bool                          keep_tmp       = false                  ; // keep tmp dir after job execution
		::vector<int>                 kill_sigs      ;                          // signals to use to kill job (tried in sequence, 1s apart from each other)
		size_t                        n_tokens       = 1                      ; // available tokens for this rule, used to estimate req ETE
		Prio                          prio           = 0                      ; // the priority of the rule
		DepsSpec                      rsrcs          ;                          // same format as deps, resources passed to backend that must be meaningful to it
		::string                      script         ;
		Time::Delay                   start_delay    ;                          // job duration above which a start message is generated
		size_t                        stderr_len     = -1                     ; // stderr output is truncated to that many lines (unlimited if -1)
		Time::Delay                   timeout        ;                          // if 0 <=> no timeout, maximum time allocated to job execution in s
		// derived data
		bool    has_stars        = false ;
		VarIdx  n_static_stems   = 0     ;
		VarIdx  n_static_targets = 0     ;
		// management data
		ExecGen cmd_gen   = 1 ;                                                // cmd generation, must be >0 as 0 means !cmd_ok
		ExecGen rsrcs_gen = 1 ;                                                // for a given cmd, resources generation, must be >=cmd_gen
		// stats
		mutable Delay  exec_time    = {} ;                                     // average exec_time
		mutable JobIdx stats_weight = 0  ;                                     // number of jobs used to compute average
		// not stored on disk
		bool                  all_deps_static = false ;                        // all deps are deemed static, for special rules only
		bool                  no_deps         = false ;                        // deps are not analyszed    , for special rules only
		::vector<Py::Pattern> target_patterns ;
	} ;

	struct Rule : RuleBase {
		friend ::ostream& operator<<( ::ostream& os , Rule const r ) ;
		static constexpr char JobMrkr  = 0               ;                     // ensure no ambiguity between job names and node names
		static constexpr char StarMrkr = 0               ;                     // signal a star stem in job_name
		static constexpr char StemMrkr = 0               ;                     // signal a stem in job_name & targets & deps
		static constexpr VarIdx NoVar  = RuleData::NoVar ;
		//
		struct SimpleMatch ;
		struct Match       ;
		// cxtors & casts
		using RuleBase::RuleBase ;
		Rule(RuleBase const& rb     ) : RuleBase{ rb                                                  } {                                      }
		Rule(::string const& job_sfx) : Rule    { to_int<Idx>( &job_sfx[job_sfx.size()-sizeof(Idx)] ) } { SWEAR(job_sfx.size()>=sizeof(Idx)) ; }
		// acesses
		::string job_sfx() const {
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
		// services
		void new_job_exec_time( Delay , Tokens ) ;
	} ;

}
#endif
#ifdef DATA_DEF
namespace Engine {

	// SimpleMatch does not call Python and only provides services that can be served with this constraint
	struct Rule::SimpleMatch {
		friend ::ostream& operator<<( ::ostream& , SimpleMatch const& ) ;
		// cxtors & casts
	public :
		SimpleMatch(   ) = default ;
		SimpleMatch(Job) ;
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
		::vector_s const& deps   () const { if (!_has_deps   ) { _compute_deps   () ; _has_deps    = true ; } return _deps    ; }
		::vector_s const& rsrcs  () const { if (!_has_rsrcs  ) { _compute_rsrcs  () ; _has_rsrcs   = true ; } return _rsrcs   ; }
		Tokens            tokens () const { if (!_has_rsrcs  ) { _compute_rsrcs  () ; _has_rsrcs   = true ; } return _tokens  ; } // tokens & rsrcs are computed together
	private :
		void               _compute_deps   (      ) const ;
		void               _compute_rsrcs  (      ) const ;
		Py::Pattern const& _target_pattern (VarIdx) const ;                    // solve lazy evaluation
		//
		::string _gather_dep( PyObject*& ctx , RuleData::DepSpec const& , RuleData::DepsSpec const& spec , bool for_deps ) const ;
		// services
	public :
		VarIdx idx(::string const&) const ;
	private :
		bool _match( VarIdx , ::string const& ) const ;
		// cache
		/**/                                mutable ::vector<Py::Pattern> _target_patterns ; // lazy evaluated
		mutable bool _has_deps    = false ; mutable ::vector_s            _deps            ;
		mutable bool _has_rsrcs   = false ; mutable ::vector_s            _rsrcs           ;
		/**/                                mutable Tokens                _tokens          = 0 ;
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
	// RuleData
	//

	template<IsStream S> void RuleData::serdes(S& s) {
		if (::is_base_of_v<::istream,S>) *this = {} ;
		::serdes(s,anti) ;
		//
		::serdes(s,job_name        ) ;
		::serdes(s,name            ) ;
		::serdes(s,n_static_stems  ) ;
		::serdes(s,n_static_targets) ;
		::serdes(s,prio            ) ;
		::serdes(s,stems           ) ;
		::serdes(s,targets         ) ;
		if (!anti) {
			::serdes(s,allow_stderr  ) ;
			::serdes(s,autodep_method) ;
			::serdes(s,auto_mkdir    ) ;
			::serdes(s,backend       ) ;
			::serdes(s,chroot        ) ;
			::serdes(s,cmd_ctx       ) ;
			::serdes(s,cmd_gen       ) ;
			::serdes(s,cwd           ) ;
			::serdes(s,deps          ) ;
			::serdes(s,env           ) ;
			::serdes(s,exec_time     ) ;
			::serdes(s,force         ) ;
			::serdes(s,has_stars     ) ;
			::serdes(s,ignore_stat   ) ;
			::serdes(s,interpreter   ) ;
			::serdes(s,is_python     ) ;
			::serdes(s,job_tokens    ) ;
			::serdes(s,keep_tmp      ) ;
			::serdes(s,kill_sigs     ) ;
			::serdes(s,n_tokens      ) ;
			::serdes(s,rsrcs         ) ;
			::serdes(s,rsrcs_gen     ) ;
			::serdes(s,script        ) ;
			::serdes(s,start_delay   ) ;
			::serdes(s,stats_weight  ) ;
			::serdes(s,stderr_len    ) ;
			::serdes(s,timeout       ) ;
		}
		if (is_base_of_v<::istream,S>) _compile_derived_info() ;
	}
	inline Delay RuleData::margin() const {
		SWEAR(backend!=Backends::Tag::Unknown) ;
		return g_config.backends[+backend].margin ;
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
