// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "pycxx.hh"

#include "hash.hh"
#include "serialize.hh"

#include "core.hh"

namespace Engine {

	ENUM( StarAction
	,	None
	,	Stop
	,	Err
	)

	static const ::string _g_specials = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f" ;
	static ::string escape(::string const& s) {
		::string res ; res.reserve(s.size()+(s.size()>>4)) ;                   // take a little margin for escapes
		for( char c : s ) {
			if (_g_specials.find(c)!=Npos) res.push_back('\\') ;               // escape specials
			res.push_back(c) ;
		}
		return res ;
	}

	// str has the same syntax as Python f-strings
	// cb is called on each stem found
	// return str with stems substituted with the return value of cb
	// stems are of the form {<identifier>\*?} or {<identifier>?\*?:.*} (where .* after : must have matching {})
	// cb is called with :
	// - the <identifier>
	// - true if <identifier> is followed by a *
	// - the regular expression that follows the : or nullptr for the first case
	// /!\ : this function is also implemented in read_makefiles.py:add_stems, both must stay in sync
	using ParsePyFuncFixed = ::function<void( string const& fixed                                             )> ;
	using ParsePyFuncStem  = ::function<void( string const& key , bool star , bool unnamed , string const* re )> ;
	static void _parse_py( ::string const& str , size_t* unnamed_star_idx , ParsePyFuncFixed const& cb_fixed , ParsePyFuncStem const& cb_stem ) {
		enum { Literal , SeenStart , Key , Re , SeenStop } state = Literal ;
		::string fixed ;
		::string key   ;
		::string re    ;
		size_t unnamed_idx = 1 ;
		size_t depth = 0 ;
		for( char c : str ) {
			bool with_re = false ;
			switch (state) {
				case Literal :
					if      (c=='{') state = SeenStart ;
					else if (c=='}') state = SeenStop  ;
					else             fixed.push_back(c) ;
				break ;
				case SeenStop :
					if (c!='}') goto End ;
					state = Literal ;
					fixed.push_back(c) ;                                       // }} are transformed into }
				break ;
				case SeenStart :
					if (c=='{') {
						state = Literal ;
						fixed.push_back(c) ;                                   // {{ are transformed into {
						break ;
					}
					cb_fixed(fixed) ;
					fixed.clear() ;
					state = Key ;
				/*fall through*/
				case Key :
					if (c!='}') {
						if (c==':') state = Re ;
						else        key.push_back(c) ;
						break ;
					}
					goto Call ;
				break ;
				case Re :
					if (!( c=='}' && depth==0 )) {
						if      (c=='{') depth++ ;
						else if (c=='}') depth-- ;
						re.push_back(c) ;
						break ;
					}
					with_re = true ;
				Call :
					// trim key
					size_t start = 0          ; while( start<key.size() && ::isspace(key[start]) ) start++ ;
					size_t end   = key.size() ; while( end>start        && ::isspace(key[end-1]) ) end  -- ;
					if (end<=start) key.clear() ;
					else            key = key.substr(start,end-start) ;
					bool star = !key.empty() && key.back()=='*' ;
					if (star) key.pop_back() ;
					bool unnamed = key.empty() ;
					if (unnamed) {
						if (!unnamed_star_idx) {               throw to_string("no auto-stem allowed in "              ,str) ;                                                           }
						if (star             ) { if (!with_re) throw to_string("unnamed star stems must be defined in ",str) ; key = to_string("<star_stem",(*unnamed_star_idx)++,'>') ; }
						else                   {                                                                               key = to_string("<stem"     ,  unnamed_idx      ++,'>') ; }
					} else {
						if (!is_identifier(key)) throw to_string("bad key ",key," must be empty or an identifier") ;
					}
					if (with_re) { cb_stem(key,star,unnamed,&re    ) ; re .clear() ; }
					else         { cb_stem(key,star,unnamed,nullptr) ;               }
					key.clear() ;
					state = Literal ;
				break ;
			}
		}
	End :
		switch (state) {
			case Literal   : cb_fixed(fixed) ; break ;                         // trailing fixed
			case SeenStop  : throw to_string("spurious } in ",str) ;
			case SeenStart :
			case Key       :
			case Re        : throw to_string("spurious { in ",str) ;
			default : FAIL(state) ;
		}
	}
	static inline void _parse_py( ::string const& str , size_t* unnamed_star_idx , ParsePyFuncStem const& cb_stem ) {
		_parse_py( str , unnamed_star_idx , [](::string const&)->void{} , cb_stem ) ;
	}
	static inline void _parse_py( ::string const& str , size_t* unnamed_star_idx , ParsePyFuncFixed const& cb_fixed ) {
		_parse_py( str , unnamed_star_idx , cb_fixed , [](::string const&,bool,bool,::string const*)->void{} ) ;
	}

	// star stems are represented by a StemMrkr followed by the stem idx
	// cb is called on each stem found
	// return str with stems substituted with the return value of cb and special characters outside stems escaped if escape==true
	using SubstTargetFunc = ::function<string( FileNameIdx pos , VarIdx stem )> ;
	static ::string _subst_target( ::string const& str , SubstTargetFunc const& cb , bool escape=false , VarIdx stop_above=-1 ) {
		::string res   ;
		uint8_t  state = 0 ;                                                   // may never be above 72
		VarIdx   stem  = 0 ;
		for( char c : str ) {                                                  // XXX : change loop and use to_int to determine stems
			if (state>0) {
				stem |= VarIdx(uint8_t(c))<<((state-1)*8) ;
				if (state<sizeof(VarIdx)) { state++ ; continue ; }
				if (stem>=stop_above) return res ;
				res += cb(res.size(),stem) ;
				state = 0 ;
				stem  = 0 ;
			} else {
				if (c==Rule::StemMrkr) { state = 1 ; continue ; }
				if ( escape && _g_specials.find(c)!=Npos ) res += '\\' ;
				res += c ;
			}
		}
		SWEAR(state==0) ;
		return res ;
	}
	// provide shortcut when pos is unused
	static inline ::string _subst_target( ::string const& str , ::function<string(VarIdx)> const& cb , bool escape=false , VarIdx stop_above=-1 ) {
		return _subst_target( str , [&](FileNameIdx,VarIdx s)->::string { return cb(s) ; } , escape , stop_above ) ;
	}

	// same as above, except pos is position in input and no result
	using ParseTargetFunc = ::function<void( FileNameIdx pos , VarIdx stem )> ;
	static void _parse_target( ::string const& str , ParseTargetFunc const& cb , VarIdx stop_above=-1 ) {
		uint8_t state = 0 ;                                                    // the number of stem char seen, may never be >8
		VarIdx  stem  = 0 ;
		for( FileNameIdx i=0 ; i<str.size() ; i++ ) {
			char c = str[i] ;
			if (state>0) {
				stem |= VarIdx(uint8_t(c))<<((state-1)*8) ;
				if (state<sizeof(VarIdx)) { state++ ; continue ; }
				if (stem>=stop_above) return ;
				cb(i-state,stem) ;                                             // i points to the last char of stem and we want the first one
				state = 0 ;
				stem  = 0 ;
			} else {
				if (c==Rule::StemMrkr) { state = 1 ; continue ; }
			}
		}
		SWEAR(state==0) ;
	}
	// provide shortcut when pos is unused
	static inline void _parse_target( ::string const& str , ::function<void(VarIdx)> const& cb , VarIdx stop_above=-1 ) {
		_parse_target( str , [&](FileNameIdx,VarIdx s)->void { cb(s) ; } , stop_above ) ;
	}

	//
	// Rule
	//

	::ostream& operator<<( ::ostream& os , Rule const r ) {
		os << "R(" ;
		if (+r) os << +r ;
		return os << ')' ;
	}

	void Rule::new_job_exec_time( Delay exec_time , Tokens1 tokens1 ) {
		if((*this)->stats_weight<RuleWeight) (*this)->stats_weight++ ;
		Delay delta = (exec_time-(*this)->exec_time) / (*this)->stats_weight ;
		(*this)->exec_time += delta ;
		for( Req req : Req::s_reqs_by_start ) req.inc_rule_exec_time(*this,delta,tokens1) ;
	}

	//
	// RuleTgt
	//

	::ostream& operator<<( ::ostream& os , RuleTgt const rt ) {
		return os << "RT(" << Rule(rt) <<':'<< int(rt.tgt_idx) << ')' ;
	}

	//
	// Attrs
	//

	void Attrs::s_acquire( bool& dst , PyObject* py_src ) {
		if (s_easy(dst,py_src,false)) return ;
		//
		int v = PyObject_IsTrue(py_src) ;
		if (v==-1) throw "cannot determine truth value"s ;
		dst = v ;
	}

	void Attrs::s_acquire( Time::Delay& dst , PyObject* py_src ) {
		if (s_easy(dst,py_src,{})) return ;
		//
		if (PyFloat_Check(py_src)) {
			dst = Time::Delay(PyFloat_AsDouble(py_src)) ;
		} else if (PyLong_Check(py_src)) {
			long sd = PyLong_AsLong(py_src) ;
			if ( sd==-1 && PyErr_Occurred() ) { PyErr_Clear() ; throw "overflow"s  ; }
			if ( sd<0                       )                   throw "underflow"s ;
			dst = Time::Delay(double(sd)) ;
		} else if (PyUnicode_Check(py_src)) {
			PyObject* f = PyFloat_FromString(py_src) ;
			if (!f) throw "cannot convert to float"s ;
			dst = Time::Delay(PyFloat_AsDouble(f)) ;
			Py_DECREF(f) ;
		} else {
			throw "cannot convert to float"s ;
		}
	}

	void Attrs::s_acquire( ::string& dst , PyObject* py_src ) {
		if (s_easy(dst,py_src,{})) return ;
		//
		bool is_str = PyUnicode_Check(py_src) ;
		if (!is_str) py_src = PyObject_Str(py_src) ;
		if (!py_src) throw "cannot convert to str"s ;
		dst = PyUnicode_AsUTF8(py_src) ;
		if (!is_str) Py_DECREF(py_src) ;
	}

	//
	// CreateMatchAttrs
	//

	::pair<string,DFlags> CreateMatchAttrs::s_split_dflags( ::string const& key , PyObject* py_dep ) {
		DFlags flags = StaticDFlags ;
		if ( PyUnicode_Check (py_dep)) return { PyUnicode_AsUTF8(py_dep) , flags } ;
		if (!PySequence_Check(py_dep)) throw to_string("dep ",key," is neither a str nor a sequence") ;
		PyObject* py_fast_dep = PySequence_Fast(py_dep,"") ;
		SWEAR(py_fast_dep) ;
		ssize_t    n = PySequence_Fast_GET_SIZE(py_fast_dep) ;
		PyObject** p = PySequence_Fast_ITEMS   (py_fast_dep) ;
		if (n<1                   ) throw to_string("dep ",key,"is empty"    ) ;
		if (!PyUnicode_Check(p[0])) throw to_string("dep ",key,"is not a str") ;
		for( ssize_t i=1 ; i<n ; i++ ) {
			if (!PyUnicode_Check(p[i])) throw to_string("dep ",key,"has a flag which is not a str") ;
			const char* flag_str = PyUnicode_AsUTF8(p[i]) ;
			bool        neg      = flag_str[0]=='-'       ;
			flag_str += neg ;
			if (!can_mk_enum<DFlag>(flag_str)) throw to_string("unexpected flag ",flag_str," for dep ",key) ;
			DFlag flag = mk_enum<DFlag>(flag_str) ;
			if (flag>=DFlag::Private) throw to_string("unexpected flag ",flag_str," for dep ",key) ;
			if (neg) flags &= ~flag ;
			else     flags |=  flag ;
		}
		return { PyUnicode_AsUTF8(p[0]) , flags } ;
	}

	void CreateMatchAttrs::update( PyObject* py_src , RuleData const& rd , ::umap_s<VarIdx> const& static_stem_idxs , VarIdx n_static_unnamed_stems ) {
		if (py_src==Py_None) {
			full_dynamic = true ;
			return ;
		}
		full_dynamic = false ;
		SWEAR(PyDict_Check(py_src)) ;
		PyObject*        py_key = nullptr/*garbage*/ ;
		PyObject*        py_val = nullptr/*garbage*/ ;
		Py_ssize_t       pos    = 0                  ;
		::map_s<DepSpec> map    ;                                              // use a sorted map so deps are sorted, which makes them more stable
		while (PyDict_Next( py_src , &pos , &py_key , &py_val )) {
			SWEAR(PyUnicode_Check(py_key)) ;
			::string key = PyUnicode_AsUTF8(py_key) ;
			if (py_val==Py_None) {
				map[key] ;                                                     // create empty entry to allow dynamic deps to use key
			} else {
				auto     [dep,flags]  = s_split_dflags(key,py_val) ;
				::string parsed_dep   ;
				VarIdx   seen_unnamed = 0                      ;
				_parse_py( dep , nullptr/*unnamed_star_idx*/ ,
					[&]( ::string const& fixed )->void {
						parsed_dep.append(fixed) ;
					}
				,	[&]( ::string const& k , bool star , bool unnamed , ::string const* def )->void {
						SWEAR( static_stem_idxs.contains(k) && !star && !def ) ;
						size_t res_sz = parsed_dep.size() ;
						parsed_dep.resize(parsed_dep.size()+1+sizeof(VarIdx)) ;
						char* p = parsed_dep.data()+res_sz ;
						p[0] = Rule::StemMrkr ;
						from_int( p+1 , static_stem_idxs.at(k) ) ;
						seen_unnamed += unnamed ;
					}
				) ;
				if ( seen_unnamed && seen_unnamed!=n_static_unnamed_stems ) throw to_string("dep ",key," contains some but not all unnamed static stems") ;
				map[key] = { rd.add_cwd(::move(parsed_dep)) , flags } ;
			}
		}
		if (map.size()>Rule::NoVar) throw "too many static deps"s ;
		deps = mk_vmap(map) ;
	}

	::vmap_s<pair<Node,DFlags>> CreateMatchAttrs::mk( Rule rule , ::vector_s const& stems , PyObject* py_src ) const {
		auto subst = [&](::string const& d)->::string {
			return _subst_target( d , [&](VarIdx s)->::string { return stems[s] ; } ) ;
		} ;
		::vmap_s<pair<Node,DFlags>> res ;
		for( auto const& [k,ds] : deps ) res.emplace_back( k , ::pair(Node(subst(ds.pattern)),ds.flags) ) ;
		if (!s_easy(res,py_src,{})) {
			::map_s<pair<Node,DFlags>> map ;
			for( size_t d=0 ; d<deps.size() ; d++ ) map[deps[d].first] = res[d].second ;
			PyObject*  py_key = nullptr/*garbage*/ ;
			PyObject*  py_val = nullptr/*garbage*/ ;
			Py_ssize_t pos    = 0                  ;
			while (PyDict_Next( py_src , &pos , &py_key , &py_val )) {
				if (!PyUnicode_Check(py_key)) {
					PyObject* py_key_str = PyObject_Str(py_key) ;
					if (!py_key_str) throw "a dep has a non printable key"s ;
					::string key_str = PyUnicode_AsUTF8(py_key_str) ;
					Py_DECREF(py_key_str) ;
					throw to_string("a dep has a non str key : ",key_str) ;
				}
				::string key = PyUnicode_AsUTF8(py_key) ;
				if (py_val==Py_None) { map.erase(key) ; continue ; }
				//
				auto [dep,flags] = s_split_dflags(key,py_val) ;
				if (!full_dynamic) SWEAR(map.contains(key)) ;
				map[key] = { rule->add_cwd(::move(dep)) , flags } ;
			}
			if (full_dynamic) res = mk_vmap(map) ;
			else              for( size_t d=0 ; d<deps.size() ; d++ ) res[d] = { deps[d].first , map[deps[d].first] } ;
		}
		return res ;
	}

	::vmap_s<pair<Node,DFlags>> DynamicCreateMatchAttrs::eval( Rule r , ::vector_s const& stems ) const {
		if (!is_dynamic) return spec.mk(r,stems) ;
		SWEAR( !need_deps && !need_rsrcs ) ;
		Py::Gil   gil ;
		PyObject* d   ;
		if (need_targets) d = _mk_dict( r , stems , Rule::SimpleMatch(r,stems).targets() ) ;
		else              d = _mk_dict( r , stems                                        ) ;
		::vmap_s<pair<Node,DFlags>> res = spec.mk(r,stems,d) ;
		Py_DECREF(d) ;
		return res  ;
	}

	//
	// Cmd
	//

	::string DynamicCmd::eval( Job j , Rule::SimpleMatch& match_ , ::vmap_ss const& rsrcs ) const {
		if (!spec.is_python) return Base::eval(j,match_,rsrcs).cmd ;
		::pair<vmap_ss,vmap_s<vmap_ss>> ctx_ ;
		if ( need_stems || need_targets ) {
			if (!match_) match_ = Rule::SimpleMatch{j} ;
			if (need_targets) ctx_ = j->rule.eval_ctx( ctx , match_.stems , match_.targets() , j->deps , rsrcs ) ;
			else              ctx_ = j->rule.eval_ctx( ctx , match_.stems , {}/*targets*/    , j->deps , rsrcs ) ; // fast path : no need to compute targets
		} else {
			/**/              ctx_ = j->rule.eval_ctx( ctx , {}/*stems*/  , {}/*targets*/    , j->deps , rsrcs ) ; // fast path : no need to compute match_
		}
		::string res ;
		for( auto const& [k ,v ] : ctx_.first  ) res += to_string(k," = ",mk_py_str(v),'\n') ;
		for( auto const& [k1,v1] : ctx_.second ) {
			res += to_string(k1," = {\n") ;
			bool first = true ;
			for( auto const& [k2,v2] : v1 ) {
				if (!first) res += ',' ;
				res += to_string('\t', mk_py_str(k2) ," : ", mk_py_str(v2) ,'\n') ;
				first = false ;
			}
			res += "}\n" ;
		}
		/**/                       res += spec.cmd ;
		if (spec.cmd.back()!='\n') res += '\n'                                                                  ;
		/**/                       res += "rc = cmd()\nif rc : raise RuntimeError(f'cmd() returned rc={rc}')\n" ;
		return res ;
	}

	//
	// RuleData
	//

	size_t RuleData::s_name_sz = 0 ;

	RuleData::RuleData(Special s) {
		prio = Infinity    ;                                                   // by default, rule is alone and this value has no impact
		name = mk_snake(s) ;
		switch (s) {
			case Special::Src      :                    force = true ; break ; // Force so that source files are systematically inspected
			case Special::Req      :                    force = true ; break ;
			case Special::Uphill   : prio = +Infinity ; anti  = true ; break ; // +inf : there may be other rules after , AllDepsStatic : dir must exist to apply rule
			case Special::Infinite : prio = -Infinity ;                break ; // -inf : it can appear after other rules, NoDep         : deps contains the chain
			default : FAIL(s) ;
		}
	}

	::ostream& operator<<( ::ostream& os , RuleData const& rd ) {
		return os << "RD(" << rd.name << ')' ;
	}

	template<class Flag> static BitMap<Flag> _get_flags( size_t n_ignore , Py::Sequence const& py_flags , BitMap<Flag> dflt ) {
		SWEAR(size_t(py_flags.size())>=n_ignore) ;
		BitMap<Flag> flags[2/*inv*/] ;
		for( auto const& k2 : py_flags ) {
			if (n_ignore) { n_ignore-- ; continue ; }
			::string k2s = Py::String(k2) ;
			Flag f   ;
			bool inv = k2s[0]=='-' ;
			if (inv) k2s = k2s.substr(1) ;
			try                          { f = mk_enum<Flag>(k2s) ;                 }
			catch(::out_of_range const&) { throw to_string("unknown flag : ",k2s) ; }
			if (f>=Flag::Private)        { throw to_string("unknown flag : ",k2s) ; }
			//
			if (flags[ inv][f]) throw to_string("flag ",f," is repeated"          ) ;
			if (flags[!inv][f]) throw to_string("flag ",f," is both set and reset") ;
			flags[inv] |= f ;
		}
		return ( dflt & ~flags[true/*inv*/] ) | flags[false/*inv*/] ;
	}
	// 2 targets may conflict if it is possible to find a file name that matches both
	// to do that, we analyze both the prefix and the suffix, knowing that the static stems are identical for both
	static bool _may_conflict( VarIdx n_static_stems , ::string const& a , ::string const& b ) {
		// prefix
		for( bool is_prefix : {true, false} ) {
			// first identify the first star stem
			FileNameIdx sz_a = a.size() ;
			FileNameIdx sz_b = b.size() ;
			_parse_target( a , [&]( FileNameIdx pos , VarIdx s ) -> void { if ( s>=n_static_stems && sz_a==a.size() ) sz_a = is_prefix?pos:a.size()-1-pos ; } ) ;
			_parse_target( b , [&]( FileNameIdx pos , VarIdx s ) -> void { if ( s>=n_static_stems && sz_b==b.size() ) sz_b = is_prefix?pos:b.size()-1-pos ; } ) ;
			FileNameIdx     sz = sz_a>sz_b ? sz_b : sz_a ;
			// analyse divergence
			for( FileNameIdx i=0 ; i<sz ; i++ ) {
				FileNameIdx ia        = i  ; if (!is_prefix) ia   = a.size()-1-ia                    ;  // current index
				FileNameIdx ib        = i  ; if (!is_prefix) ib   = b.size()-1-ib                    ;  // .
				FileNameIdx iae       = ia ; if ( is_prefix) iae += sizeof(VarIdx)                   ;  // last char of stem if it is one (cannot subtract sizeof(VarIdx) as FileNameIdx is unsigned)
				FileNameIdx ibe       = ib ; if ( is_prefix) ibe += sizeof(VarIdx)                   ;  // .
				bool        a_is_stem = iae>=sizeof(VarIdx) && a[iae-sizeof(VarIdx)]==Rule::StemMrkr ;
				bool        b_is_stem = ibe>=sizeof(VarIdx) && b[ibe-sizeof(VarIdx)]==Rule::StemMrkr ;
				if ( !a_is_stem && !b_is_stem ) {
					if (a[ia]==b[ib]) continue ;                               // same      chars, continue analysis
					else              return false ;                           // different chars, no conflict possible
				}
				if ( a_is_stem && b_is_stem ) {
					::string_view sa = ::string_view(a).substr(iae+1-sizeof(VarIdx),sizeof(VarIdx)) ;
					::string_view sb = ::string_view(b).substr(ibe+1-sizeof(VarIdx),sizeof(VarIdx)) ;
					if (sa==sb) { i+=sizeof(VarIdx) ; continue ; }                                    // same      stems, continue analysis
					else        { goto NxtSide ;                 }                                    // different stems, could have identical values
				}
				goto NxtSide ;                                                 // one is a stem, not the other, the stem value can match the fixed part of the other, may conflict
			}
			if ( sz == (sz_a>sz_b?b.size():a.size()) ) {                       // if shortest is a prefix of longest, analyse remaining of longest to see if we are certain it is non-empty
				::string const& l = sz_a>sz_b ? a : b ;                        // longest
				for( FileNameIdx i=sz ; i<l.size() ; i++ ) {
					FileNameIdx j       = i ; if (!is_prefix) j  = l.size()-1-j                      ; // current index
					FileNameIdx je      = j ; if ( is_prefix) j += sizeof(VarIdx)                    ; // last char of stem if it is one (cannot subtract sizeof(VarIdx) as FileNameIdx is unsigned)
					bool        is_stem = je>=sizeof(VarIdx) && l[je-sizeof(VarIdx)]==Rule::StemMrkr ;
					if (is_stem) i += sizeof(VarIdx) ;                                                 // stem value can be empty, may still conflict, continue
					else         return false ;                                                        // one is a strict suffix of the other, no conflict possible
				}
			}
		NxtSide : ;
		}
		return true ;
	}
	void RuleData::_acquire_py(Py::Dict const& dct) {
		::string field ;
		try {
			//
			// acquire essential (necessary for Anti)
			//
			//
			field = "__anti__" ; if (dct.hasKey(field)) anti  = Py::Object(dct[field]).as_bool() ;
			field = "name"     ; if (dct.hasKey(field)) name  = Py::String(dct[field])           ; else throw "not found"s ;
			field = "prio"     ; if (dct.hasKey(field)) prio  = Py::Float (dct[field])           ;
			field = "cwd"      ; if (dct.hasKey(field)) cwd_s = Py::String(dct[field])           ;
			if (!cwd_s.empty()) {
				if (cwd_s.back ()!='/') cwd_s += '/' ;
				if (cwd_s.front()=='/') {
					if (cwd_s.starts_with(*g_root_dir+'/')) cwd_s.erase(0,g_root_dir->size()+1) ;
					else                                    throw "cwd must be relative to root dir"s ;
				}
				prio += g_config.sub_prio_boost * ::count(cwd_s.begin(),cwd_s.end(),'/') ;
			}
			//
			Trace trace("_acquire_py",name,prio) ;
			//
			::map_s<::pair<string,Bool3/*star*/>> stem_map ;                   // ordered so that stems is ordered
			field = "stems" ;
			if (dct.hasKey(field))
				for( auto const& [k,v] : Py::Dict(dct[field]) ) stem_map[Py::String(k)] = {Py::String(v),Maybe} ; // the real stems are restricted to what is necessary for job_name & targets
			//
			// augment stems with definitions found in job_name and targets
			size_t unnamed_star_idx = 1 ;                                      // free running while walking over job_name + targets
			auto augment_stems = [&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> void {
				Bool3 star3 = No|star ;
				if (stem_map.contains(k)) {
					::pair<string,Bool3/*star*/>& e = stem_map.at(k) ;
					if ( re && e.first!=*re       ) throw to_string("2 different definitions for stem ",k," : ",e.first," and ",*re) ;
					if ( e.second==Maybe || !star ) e.second = star3 ;         // record stem is used, and is static as soone as one use is static
				} else {
					if (re) stem_map[k] = {*re,star3} ;
				}
			} ;
			field = "job_name" ;
			if (!dct.hasKey(field)) throw "not found"s ;
			job_name = add_cwd(Py::String(dct[field])) ;
			_parse_py( job_name , &unnamed_star_idx , augment_stems ) ;
			field = "targets" ;
			if (!dct.hasKey(field)) throw "not found"s ;
			Py::Dict py_targets{dct[field]} ;
			::string job_name_or_key = "job_name" ;
			for( auto const& [py_k,py_tfs] : py_targets ) {
				field = Py::String(py_k) ;
				Py::Sequence pyseq_tfs{ py_tfs } ;                             // targets are a tuple (target_pattern,flags...)
				::string target = add_cwd(Py::String(pyseq_tfs[0])) ;
				// avoid processing target if it is identical to job_name
				// this is not an optimization, it is to ensure unnamed_star_idx's match
				if (target==job_name) job_name_or_key = field ;
				else                  _parse_py( target , &unnamed_star_idx , augment_stems ) ;
			}
			//
			// gather job_name and targets
			field            = "job_name" ;
			unnamed_star_idx = 1          ;                                    // reset free running at each pass over job_name+targets
			VarIdx n_static_unnamed_stems = 0     ;
			bool   job_name_is_star       = false ;
			auto   stem_words             = [&]( ::string const& k , bool star , bool unnamed ) -> ::string {
				const char* stem = star ? "star stem" : "stem" ;
				return unnamed ? to_string("unnamed ",stem) : to_string(stem,' ',k) ;
			} ;
			_parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) -> void {
					if      (!stem_map.contains(k)) throw to_string("found undefined ",stem_words(k,star,unnamed)," in ",job_name_or_key) ;
					if      (star                 ) job_name_is_star = true ;
					else if (unnamed              ) n_static_unnamed_stems++ ;
				}
			) ;
			//
			field = "targets" ;
			bool found_matching = false ;
			{	::vmap_s<TargetSpec> star_targets ;                            // defer star targets so that static targets are put first
				for( auto const& [py_k,py_tfs] : Py::Dict(dct[field]) ) {      // targets are a tuple (target_pattern,flags...)
					field = Py::String(py_k) ;
					Py::Sequence pyseq_tfs      { py_tfs }                          ;
					bool         is_native_star = false                             ;
					::string     target         = add_cwd(Py::String(pyseq_tfs[0])) ;
					::set_s      missing_stems  ;
					// avoid processing target if it is identical to job_name
					// this is not an optimization, it is to ensure unnamed_star_idx's match
					if (target==job_name) {
						if (job_name_is_star) is_native_star = true ;
					} else {
						for( auto const& [k,s] : stem_map ) if (s.second==No) missing_stems.insert(k) ;
						_parse_py( target , &unnamed_star_idx ,
							[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) -> void {
								if      (!stem_map.contains(k)    ) throw to_string("found undefined ",stem_words(k,star,unnamed)," in target") ;
								if      (star                     ) is_native_star = true ;
								else if (stem_map.at(k).second!=No) throw to_string(stem_words(k,star,unnamed)," appears in target but not in ",job_name_or_key) ;
								else                                missing_stems.erase(k) ;
							}
						) ;
					}
					TFlags min_flags  = _get_flags<TFlag>( 1 , pyseq_tfs , TFlags::None ) ;
					TFlags max_flags  = _get_flags<TFlag>( 1 , pyseq_tfs , TFlags::All  ) ;
					TFlags dflt_flags = DfltTFlags                                        ;
					TFlags flags      = (dflt_flags&max_flags)|min_flags                  ; // tentative value
					if (is_native_star     ) dflt_flags |= TFlag::Star ;
					if (flags[TFlag::Match]) dflt_flags &= ~TFlag::Dep ;
					flags = (dflt_flags&max_flags)|min_flags ;                              // definitive value
					// check
					if (is_native_star) {
						if ( !flags[TFlag::Star]     ) throw to_string("flag ",mk_snake(TFlag::Star)," cannot be reset because target contains star stems") ;
					}
					if (flags[TFlag::Match]) {
						if (!missing_stems.empty()   ) throw to_string("missing stems ",missing_stems," in target")                                         ;
						found_matching = true ;
					} else {
						if (anti                     ) throw           "non-matching targets are meaningless for anti-rules"s                               ;
					}
					if (field=="<stdout>") {
						if (flags[TFlag::Star       ]) throw           "stdout cannot be directed to a star target"s                                        ;
						if (flags[TFlag::Phony      ]) throw           "stdout cannot be directed to a phony target"s                                       ;
						if (flags[TFlag::Incremental]) throw           "stdout cannot be directed to a incremental target"s                                 ;
					}
					chk(flags) ;
					// record
					(flags[TFlag::Star]?star_targets:targets).emplace_back( field , TargetSpec(target,is_native_star,flags) ) ;
				}
				n_static_targets = targets.size()        ;
				has_stars        = !star_targets.empty() ;
				if (!anti) for( auto& ts : star_targets ) targets.push_back(::move(ts)) ; // star-targets are meaningless for an anti-rule
			}
			SWEAR(found_matching) ;                                            // we should not have come until here without a clean target
			field = "" ;
			if (targets.size()>NoVar) throw to_string("too many targets : ",targets.size()," > ",NoVar) ;
			::umap_s<VarIdx>              stem_idxs ;
			::umap_s<pair<CmdVar,VarIdx>> var_idxs  ;
			for( Bool3 star : {No,Yes} ) { // keep only useful stems and order them : static first, then star
				for( auto const& [k,v] : stem_map )
					if (v.second==star) {
						/**/          stem_idxs[k] =               VarIdx(stems.size())  ;
						stems.emplace_back(k,v.first) ;
					}
				if (star==No) {
					n_static_stems = stems.size() ;
					/**/                                     var_idxs["stems"       ] = {CmdVar::Stems,0} ;
					for( VarIdx s=0 ; s<stems.size() ; s++ ) var_idxs[stems[s].first] = {CmdVar::Stem ,s} ;
				}
			}
			if (stems.size()>NoVar) throw to_string("too many stems : ",stems.size()," > ",NoVar) ;
			//
			// reformat job_name & targets to improve matching efficiency
			// {Stem} is replaced by "StemMrkr<stem_idx>"
			// StemMrkr is there to unambiguously announce a stem idx
			//
			::string mk_tgt ;
			auto mk_stem = [&]( ::string const& key , bool /*star*/ , bool /*unnamed*/ , ::string const* /*re*/ )->void {
				VarIdx   s   = stem_idxs.at(key)                   ;
				::string res ( 1+sizeof(VarIdx) , Rule::StemMrkr ) ;
				from_int( &res[1] , s ) ;
				mk_tgt += res ;
			} ;
			auto mk_fixed = [&](::string const& fixed)->void { mk_tgt += fixed ; } ;
			unnamed_star_idx = 1 ;                                                    // reset free running at each pass over job_name+targets
			mk_tgt.clear() ;
			_parse_py( job_name , &unnamed_star_idx , mk_fixed , mk_stem ) ;
			::string new_job_name = ::move(mk_tgt) ;
			// compile potential conflicts as there are rare and rather expensive to detect, we can avoid most of the verifications by statically analyzing targets
			for( VarIdx t=0 ; t<targets.size() ; t++ ) {
				TargetSpec& tf = targets[t].second ;
				// avoid processing target if it is identical to job_name
				// this is not an optimization, it is to ensure unnamed_star_idx's match
				if (tf.pattern==job_name) tf.pattern = new_job_name ;
				else {
					mk_tgt.clear() ;
					_parse_py( tf.pattern , &unnamed_star_idx , mk_fixed , mk_stem ) ;
					tf.pattern = ::move(mk_tgt) ;
				}
				for( VarIdx t2=0 ; t2<t ; t2++ )
					if ( _may_conflict( n_static_stems , tf.pattern , targets[t2].second.pattern ) ) { trace("conflict",t,t2) ; tf.conflicts.push_back(t2) ; }
			}
			job_name = ::move(new_job_name) ;
			//
			//vvvvvvvvvvvvvv
			if (anti) return ;                                                 // if Anti, we only need essential info
			//^^^^^^^^^^^^^^
			//
			field = "n_tokens" ;
			if (dct.hasKey(field)) {
				n_tokens = static_cast<unsigned long>(Py::Long(dct[field])) ;
				if (n_tokens==0) throw "value must be positive"s ;
			}

			/**/                                       var_idxs["targets"       ] = {CmdVar::Targets,0} ;
			for( VarIdx t=0 ; t<targets.size() ; t++ ) var_idxs[targets[t].first] = {CmdVar::Target ,t} ;
			// new
			{	::umap_s<VarIdx> static_stem_idxs ;
				for( auto const& [k,i] : var_idxs ) if (i.first==CmdVar::Stem) static_stem_idxs[k] = i.second ;
				if (dct.hasKey("create_match_attrs")) create_match_attrs = { Py::Object(dct["create_match_attrs"]).ptr() , var_idxs , *this , static_stem_idxs , n_static_unnamed_stems } ;
			}
			//
			/**/                                                            var_idxs["deps"                               ] = { CmdVar::Deps , 0 } ;
			for( VarIdx d=0 ; d<create_match_attrs.spec.deps.size() ; d++ ) var_idxs[create_match_attrs.spec.deps[d].first] = { CmdVar::Dep  , d } ;
			//
			field = "create_none_attrs"  ; if (dct.hasKey(field)) create_none_attrs  = { Py::Object(dct[field]).ptr() , var_idxs } ;
			field = "cache_none_attrs"   ; if (dct.hasKey(field)) cache_none_attrs   = { Py::Object(dct[field]).ptr() , var_idxs } ;
			field = "submit_rsrcs_attrs" ; if (dct.hasKey(field)) submit_rsrcs_attrs = { Py::Object(dct[field]).ptr() , var_idxs } ;
			//
			/**/                                                             var_idxs["resources"                           ] = { CmdVar::Rsrcs , 0 } ;
			for( VarIdx r=0 ; r<submit_rsrcs_attrs.spec.rsrcs.size() ; r++ ) var_idxs[submit_rsrcs_attrs.spec.rsrcs[r].first] = { CmdVar::Rsrc  , r } ;
			//
			field = "start_cmd_attrs"   ; if (dct.hasKey(field)) start_cmd_attrs   = { Py::Object(dct[field]).ptr() , var_idxs } ;
			field = "cmd"               ; if (dct.hasKey(field)) cmd               = { Py::Object(dct[field]).ptr() , var_idxs } ; else throw "not found"s ;
			field = "start_rsrcs_attrs" ; if (dct.hasKey(field)) start_rsrcs_attrs = { Py::Object(dct[field]).ptr() , var_idxs } ;
			field = "start_none_attrs"  ; if (dct.hasKey(field)) start_none_attrs  = { Py::Object(dct[field]).ptr() , var_idxs } ;
			field = "end_cmd_attrs"     ; if (dct.hasKey(field)) end_cmd_attrs     = { Py::Object(dct[field]).ptr() , var_idxs } ;
			field = "end_none_attrs"    ; if (dct.hasKey(field)) end_none_attrs    = { Py::Object(dct[field]).ptr() , var_idxs } ;

			//
			// now process fields linked to execution
			//
			field = "ete"   ; if (dct.hasKey(field)) exec_time = Delay(Py::Float (dct[field]))          ;
			field = "force" ; if (dct.hasKey(field)) force     =       Py::Object(dct[field]).as_bool() ;
			for( VarIdx t=0 ; t<targets.size() ; t++ ) {
				if (targets[t].first!="<stdout>") continue ;
				if (targets[t].second.flags[TFlag::Star]) throw "<stdout> must be a static target"s ;
				stdout_idx = t ;
				break ;
			}
			for( VarIdx d=0 ; d<create_match_attrs.spec.deps.size() ; d++ ) {
				if (create_match_attrs.spec.deps[d].first!="<stdin>") continue ;
				stdin_idx = d ;
				break ;
			}
		}
		catch(::string const& e) { throw to_string("while processing ",user_name(),'.',field," :\n"  ,indent(e)     ) ; }
		catch(Py::Exception & e) { throw to_string("while processing ",user_name(),'.',field," :\n\t",e.errorValue()) ; }
	}

	void RuleData::_compile() {
		Py::Gil gil ;
		try {
			// targets
			// Generate and compile Python pattern
			// target has the same syntax as Python f-strings except expressions must be named found in stems
			// we transform that into a pattern by :
			// - escape specials outside keys
			// - transform f-string syntax into Python regexpr syntax
			// for example "a{b}c.d" with stems["b"]==".*" becomes "a(?P<b>.*)c\.d"
			// remember that what is stored in targets is actually a stem idx, not a stem key
			for( auto const& [k,tf] : targets ) {
				::uset<VarIdx> seen       ;
				::uset<VarIdx> seen_twice ;
				_parse_target( tf.pattern ,
					[&](VarIdx s)->void {
						if (seen.contains(s)) seen_twice.insert(s) ;
						else                  seen      .insert(s) ;
					}
				) ;
				seen.clear() ;
				::string pattern = _subst_target(
					tf.pattern
				,	[&](VarIdx s)->::string {
						if      ( seen.contains(s)                           ) {                  return to_string("(?P=",to_string('_',s),                    ')') ; }
						else if ( s<n_static_stems || seen_twice.contains(s) ) { seen.insert(s) ; return to_string("(?P<",to_string('_',s),'>',stems[s].second,')') ; }
						else                                                   {                  return to_string('('   ,                     stems[s].second,')') ; }
					}
				,	true/*escape*/
				) ;
				target_patterns.emplace_back(pattern) ;
				Py::boost(target_patterns.back()) ;                            // prevent deallocation at end of execution that generates crashes
			}
			_set_crcs() ;
			create_match_attrs.compile() ;
			create_none_attrs .compile() ;
			cache_none_attrs  .compile() ;
			submit_rsrcs_attrs.compile() ;
			start_cmd_attrs   .compile() ;
			cmd               .compile() ;
			start_rsrcs_attrs .compile() ;
			start_none_attrs  .compile() ;
			end_cmd_attrs     .compile() ;
			end_none_attrs    .compile() ;
		}
		catch(::string const& e) { throw to_string("while processing ",user_name()," :\n"  ,indent(e)     ) ; }
		catch(Py::Exception & e) { throw to_string("while processing ",user_name()," :\n\t",e.errorValue()) ; }
	}

	static ::string _pretty_vmap( size_t i , ::vmap_ss const& m ) {
		OStringStream res ;
		size_t        wk  = 0 ;
		//
		for( auto const& [k,v] : m ) wk = ::max(wk,k.size()) ;
		for( auto const& [k,v] : m ) res << ::string(i,'\t') << ::setw(wk)<<k <<" : "<< v <<'\n' ;
		//
		return res.str() ;
	}
	static ::string _pretty_pattern( ::string const& target , ::vmap_ss const& stems , VarIdx n_static_stems ) {
		return _subst_target( target , [&](VarIdx t)->::string {
			return to_string( '{' , stems[t].first , t<n_static_stems?"":"*" , '}' ) ;
		} ) ;
	}
	static ::string _pretty_targets( RuleData const& rd , size_t i , ::vmap_s<TargetSpec> const& targets ) {
		OStringStream res      ;
		size_t        wk       = 0 ;
		size_t        wt       = 0 ;
		::umap_ss     patterns ;
		//
		for( auto const& [k,tf] : targets ) {
			::string p = _pretty_pattern( tf.pattern , rd.stems , rd.n_static_stems ) ;
			wk          = ::max(wk,k.size()) ;
			wt          = ::max(wt,p.size()) ;
			patterns[k] = ::move(p)          ;
		}
		for( auto const& [k,tf] : targets ) {
			TFlags        dflt_flags = DfltTFlags ;                            // flags in effect if no special user info
			OStringStream flags      ;
			if (tf.flags[TFlag::Match]) dflt_flags &= ~TFlag::Dep  ;
			if (tf.is_native_star     ) dflt_flags |=  TFlag::Star ;
			//
			bool first = true ;
			for( TFlag f : TFlag::N ) {
				if (f>=TFlag::Private         ) continue ;
				if (tf.flags[f]==dflt_flags[f]) continue ;
				//
				if (first) { flags <<" : " ; first = false ; }
				else       { flags <<" , " ;                 }
				//
				if (!tf.flags[f]) flags << '-'         ;
				/**/              flags << mk_snake(f) ;
			}
			bool first_conflict = true ;
			for( VarIdx c : tf.conflicts ) {
				if (first_conflict) {
					if (first) { flags <<" : " ; first = false ; }
					else       { flags <<" , " ;                 }
					flags << "except[" ;
					first_conflict = false ;
				} else {
					flags << ',' ;
				}
				flags << targets[c].first ;
			}
			if (!first_conflict) flags << ']'         ;
			res << ::string(i,'\t') << ::setw(wk)<<k <<" : " ;
			::string flags_str = ::move(flags).str() ;
			if (flags_str.empty()) res <<             patterns[k]              ;
			else                   res << ::setw(wt)<<patterns[k] << flags_str ;
			res <<'\n' ;
		}
		return res.str() ;
	}
	static ::string _pretty_sigs( ::vector<uint8_t> const& sigs ) {
		::string        res  ;
		::uset<uint8_t> seen ;
		const char*     sep  = "" ;
		for( uint8_t sig : sigs ) {
			if (sig) {
				res += to_string(sep,int(sig)) ;
				if (!seen.contains(sig)) {
					seen.insert(sig) ;
					res += to_string('(',::strsignal(sig),')') ;
				}
			}
			sep = " , " ;
		}
		return res ;
	}
	static ::string _pretty_job_name(RuleData const& rd) {
		for( auto const& [k,tf] : rd.targets )
			if (rd.job_name==tf.pattern) return to_string("<targets.",k,'>') ;
		return rd.job_name ;
	}

	static ::string _pretty( size_t i , CreateMatchAttrs const& ms , ::vmap_ss const& stems ) {
		OStringStream res      ;
		size_t        wk       = 0 ;
		size_t        wd       = 0 ;
		::umap_ss     patterns ;
		//
		for( auto const& [k,ds] : ms.deps ) {
			if (ds.pattern.empty()) continue ;
			::string p = _subst_target( ds.pattern , [&](VarIdx s)->::string { return to_string('{',stems[s].first,'}') ; } ) ;
			wk          = ::max(wk,k.size()) ;
			wd          = ::max(wd,p.size()) ;
			patterns[k] = ::move(p)          ;
		}
		for( auto const& [k,ds] : ms.deps ) {
			if (ds.pattern.empty()) continue ;
			::string flags ;
			bool     first = true ;
			for( DFlag f : DFlag::N ) {
				if (f>=DFlag::Private           ) continue ;
				if (ds.flags[f]==StaticDFlags[f]) continue ;
				//
				if (first) { flags += " : " ; first = false ; }
				else       { flags += " , " ;                 }
				//
				if (!ds.flags[f]) flags += '-'         ;
				/**/              flags += mk_snake(f) ;
			}
			res << ::string(i,'\t') << ::setw(wk)<<k <<" : " ;
			if (flags.empty()) res <<             patterns[k]          ;
			else               res << ::setw(wd)<<patterns[k] << flags ;
			res <<'\n' ;
		}
		return res.str() ;
	}
	static ::string _pretty( size_t i , CreateNoneAttrs const& sna ) {
		::vmap_ss entries ;
		if  (sna.tokens1!=0) entries.emplace_back( "job_tokens" , to_string(sna.tokens1+1) ) ;
		return _pretty_vmap(i,entries) ;
	}
	static ::string _pretty( size_t i , CacheNoneAttrs const& cna ) {
		if (!cna.key.empty()) return to_string(::string(i,'\t'),"key : ",cna.key,'\n') ;
		else                  return {}                                               ;
	}
	static ::string _pretty( size_t i , SubmitRsrcsAttrs const& sra ) {
		::vmap_ss entries ;
		/**/                                 if (sra.backend!=BackendTag::Local) entries.emplace_back( "<backend>" , mk_snake (sra.backend) ) ;
		for (auto const& [k,v] : sra.rsrcs ) if (!v.empty()                    ) entries.emplace_back( k           , v                    ) ;
		return _pretty_vmap(i,entries) ;
	}
	static ::string _pretty( size_t i , StartCmdAttrs const& sca ) {
		size_t        key_sz = 0 ;
		OStringStream res    ;
		int           pass   ;
		//
		auto do_field = [&](::string const& key , ::string const& val )->void {
			if (pass==1) key_sz = ::max(key_sz,key.size()) ;                                 // during 1st pass, compute max key size ;
			else         res << indent( to_string(::setw(key_sz),key," : ",val,'\n') , i ) ;
		} ;
		for( pass=1 ; pass<=2 ; pass++ ) {                                          // on 1st pass we compute key size, on 2nd pass we do the job
			if ( sca.auto_mkdir         ) do_field( "auto_mkdir"   , to_string(sca.auto_mkdir ) ) ;
			if ( sca.ignore_stat        ) do_field( "ignore_stat"  , to_string(sca.ignore_stat) ) ;
			/**/                          do_field( "autodep"      , mk_snake (sca.method     ) ) ;
			if (!sca.chroot     .empty()) do_field( "chroot"       ,           sca.chroot       ) ;
			if (!sca.local_mrkr .empty()) do_field( "local_marker" ,           sca.local_mrkr   ) ;
			if (!sca.interpreter.empty()) {
				OStringStream i ;
				for( ::string const& c : sca.interpreter ) i <<' '<<c ;
				do_field( "interpreter" , i.str().substr(1) ) ;
			}
		}
		if (!sca.env.empty()) {
			res << indent("environ :\n",i) << _pretty_vmap( i+1 , sca.env ) ;
		}
		return res.str() ;
	}
	static ::string _pretty( size_t i , Cmd const& c ) {
		::string cmd = c.cmd ;
		if ( !cmd.empty() && cmd.back()!='\n' ) cmd += '\n' ;
		return indent(cmd,i) ;
	}
	static ::string _pretty( size_t i , StartRsrcsAttrs const& sra ) {
		OStringStream res     ;
		::vmap_ss     entries ;
		if (+sra.timeout) entries.emplace_back( "timeout" , sra.timeout.short_str() ) ;
		/**/                  res << _pretty_vmap(i,entries) ;
		if (!sra.env.empty()) res << indent("environ :\n",i) << _pretty_vmap( i+1 , sra.env ) ;
		return res.str() ;
	}
	static ::string _pretty( size_t i , StartNoneAttrs const& sna ) {
		OStringStream res     ;
		::vmap_ss     entries ;
		if ( sna.keep_tmp         ) entries.emplace_back( "keep_tmp"    , to_string   (sna.keep_tmp   )            ) ;
		if (+sna.start_delay      ) entries.emplace_back( "start_delay" ,              sna.start_delay.short_str() ) ;
		if (!sna.kill_sigs.empty()) entries.emplace_back( "kill_sigs"   , _pretty_sigs(sna.kill_sigs  )            ) ;
		/**/                  res << _pretty_vmap(i,entries) ;
		if (!sna.env.empty()) res << indent("environ :\n",i) << _pretty_vmap( i+1 , sna.env ) ;
		return res.str() ;
	}
	static ::string _pretty( size_t i , EndCmdAttrs const& eca ) {
		::vmap_ss entries ;
		if  (eca.allow_stderr) entries.emplace_back( "allow_stderr" , to_string(eca.allow_stderr) ) ;
		return _pretty_vmap(i,entries) ;
	}
	static ::string _pretty( size_t i , EndNoneAttrs const& ena ) {
		::vmap_ss entries ;
		if  (ena.stderr_len!=size_t(-1)) entries.emplace_back( "stderr_len" , to_string(ena.stderr_len) ) ;
		return _pretty_vmap(i,entries) ;
	}

	template<class T,class... A> ::string RuleData::_pretty_str( size_t i , Dynamic<T> const& d , A&&... args ) const {
		OStringStream res ;
		::string s = _pretty( i+1 , d.spec , ::forward<A>(args)... ) ;
		if ( !s.empty() || d.is_dynamic ) res << indent(to_string(T::Msg," :\n"),i) << s ;
		if (d.is_dynamic) {
			if (!d.ctx     .empty()) { res << indent("<context> :"          ,i+1) ; for( ::string const& k : list_ctx(d.ctx) ) res <<' '<< k ; res << '\n' ; }
			if (!d.glbs_str.empty()) { res << indent("<dynamic globals> :\n",i+1) << indent(d.glbs_str,i+2) ; if (d.glbs_str.back()!='\n') res << '\n' ;     }
			if (!d.code_str.empty()) { res << indent("<dynamic code> :\n"   ,i+1) << indent(d.code_str,i+2) ; if (d.code_str.back()!='\n') res << '\n' ;     }
		}
		return res.str() ;
	}

	::string RuleData::pretty_str() const {
		OStringStream res     ;
		::vmap_ss     entries ;
		//
		/**/      res << name << " :" ;
		if (anti) res << " AntiRule"  ;
		/**/      res << '\n'         ;
		if (prio          ) entries.emplace_back( "prio"     , to_string(prio)                ) ;
		/**/                entries.emplace_back( "job_name" , _pretty_job_name(*this)        ) ;
		if (!cwd_s.empty()) entries.emplace_back( "cwd"      , cwd_s.substr(0,cwd_s.size()-1) ) ;
		if (!anti) {
			if (force) entries.emplace_back( "force"    , to_string(force   ) ) ;
			/**/       entries.emplace_back( "n_tokens" , to_string(n_tokens) ) ;
		}
		res << _pretty_vmap(1,entries) ;
		if (!stems.empty()) res << indent("stems :\n"  ,1) << _pretty_vmap   (      2,stems  ) ;
		/**/                res << indent("targets :\n",1) << _pretty_targets(*this,2,targets) ;
		if (!anti) {
			res << _pretty_str(1,create_match_attrs,stems) ;
			res << _pretty_str(1,create_none_attrs       ) ;
			res << _pretty_str(1,cache_none_attrs        ) ;
			res << _pretty_str(1,submit_rsrcs_attrs      ) ;
			res << _pretty_str(1,start_none_attrs        ) ;
			res << _pretty_str(1,start_cmd_attrs         ) ;
			res << _pretty_str(1,cmd                     ) ;
			res << _pretty_str(1,start_rsrcs_attrs       ) ;
			res << _pretty_str(1,end_cmd_attrs           ) ;
			res << _pretty_str(1,end_none_attrs          ) ;
		}
		//
		return res.str() ;
	}

	::vector_s RuleData::list_ctx(::vmap<CmdVar,VarIdx> const& ctx) const {
		::vector_s res ;
		for( auto const& [k,i] : ctx )
			switch (k) {
				case CmdVar::Stem    : res.push_back(stems                        [i].first) ; break ;
				case CmdVar::Target  : res.push_back(targets                      [i].first) ; break ;
				case CmdVar::Dep     : res.push_back(create_match_attrs.spec.deps [i].first) ; break ;
				case CmdVar::Rsrc    : res.push_back(submit_rsrcs_attrs.spec.rsrcs[i].first) ; break ;
				case CmdVar::Stems   : res.push_back("stems"                               ) ; break ;
				case CmdVar::Targets : res.push_back("targets"                             ) ; break ;
				case CmdVar::Deps    : res.push_back("deps"                                ) ; break ;
				case CmdVar::Rsrcs   : res.push_back("resources"                           ) ; break ;
				default : FAIL(k) ;
			}
		return res ;
	}

	::pair<vmap_ss,vmap_s<vmap_ss>> Rule::eval_ctx(
		::vmap<CmdVar,VarIdx> const& ctx
	,	::vector_s            const& stems_
	,	::vector_s            const& targets_
	,	::vector_view_c<Dep>  const& deps
	,	::vmap_ss             const& rsrcs
	) const {
		auto const&                     stems_spec   = (*this)->stems                         ;
		auto const&                     targets_spec = (*this)->targets                       ;
		auto const&                     deps_spec    = (*this)->create_match_attrs.spec.deps  ;
		auto const&                     rsrcs_spec   = (*this)->submit_rsrcs_attrs.spec.rsrcs ;
		::umap_ss                       rsrcs_map    = mk_umap(rsrcs)                         ;
		::pair<vmap_ss,vmap_s<vmap_ss>> res          ;
		//
		for( auto const& [k,i] : ctx ) {
			::string  var ;
			::string  str ;
			::vmap_ss dct ;
			switch (k) {
				case CmdVar::Stem   : var = stems_spec  [i].first ; str =            stems_  [i]              ; goto Str ;
				case CmdVar::Target : var = targets_spec[i].first ; str =            targets_[i]              ; goto Str ;
				case CmdVar::Dep    : var = deps_spec   [i].first ; str = +deps[i] ? deps    [i].name() : ""s ; goto Str ;
				case CmdVar::Rsrc   : {
					var = rsrcs_spec[i].first ;
					auto it = rsrcs_map.find(var) ;
					if (it==rsrcs_map.end()) continue ;                        // if resource is not found, do not set corresponding variable
					str = it->second ;
					goto Str ;
				}
				//
				case CmdVar::Stems :
					var = "stems" ;
					for( VarIdx j=0 ; j<(*this)->n_static_stems ; j++ ) dct.emplace_back( stems_spec[j].first , stems_[j] ) ;
					goto Dct ;
				case CmdVar::Targets :
				var = "targets" ;
				for( VarIdx j=0 ; j<targets_spec.size() ; j++ ) if (!targets_[j].empty()) dct.emplace_back( targets_spec[j].first , targets_[j] ) ;
				goto Dct ;
				case CmdVar::Deps :
					var = "deps" ;
					if ((*this)->create_match_attrs.spec.full_dynamic) {
						for( auto const& [k,df] : (*this)->create_match_attrs.eval(*this,stems_) ) dct.emplace_back( k , df.first.name() ) ;
					} else {
						for( VarIdx j=0 ; j<deps_spec.size() ; j++ ) if (+deps[j]) dct.emplace_back( deps_spec[j].first , deps[j].name() ) ;
					}
					goto Dct ;
				case CmdVar::Rsrcs :
					var = "resources" ;
					dct = rsrcs       ;
					goto Dct ;
				default : FAIL(k) ;
			}
		Str :
			res.first.emplace_back( ::move(var) , ::move(str) ) ;
			continue ;
		Dct :
			res.second.emplace_back( ::move(var) , ::move(dct) ) ;
			continue ;
		}
		return res ;
	}

	// match_crc is an id of the rule : a new rule is a replacement of an old rule if it has the same match_crc
	// also, 2 rules matching identically is forbidden : the idea is that one is useless
	// this is not strictly true, though : you could imagine a rule generating a* from b, another generating a* from b but with disjoint sets of a
	// although awkward & useless (as both rules could be merged), this can be meaningful
	// if the need arises, we will add an "id" artificial field entering in match_crc to distinguish them
	void RuleData::_set_crcs() {
		{	::vector<TargetSpec> targets_ ;
			static constexpr TFlags MatchFlags{ TFlag::Star , TFlag::Match , TFlag::Dep } ;
			for( auto const& [k,t] : targets ) {
				if (!t.flags[TFlag::Match]) continue ;                         // no influence on matching if not matching, only on execution
				TargetSpec t_ = t ;
				t_.flags &= MatchFlags ;                                       // only these flags are important for matching, others are for execution only
				targets_.push_back(t_) ;                                       // keys have no influence on matching, only on execution
			}
			Hash::Xxh h ;
			/**/       h.update(anti              ) ;
			/**/       h.update(stems             ) ;
			if (!anti) h.update(job_name          ) ;                          // job_name has no effect for anti as it is only used to store jobs and there is no anti-jobs
			/**/       h.update(cwd_s             ) ;
			/**/       h.update(targets_          ) ;
			if (!anti) h.update(create_match_attrs) ;
			match_crc = h.digest() ;
		}
		if (anti) return ;                                                     // anti-rules are only capable of matching
		{	Hash::Xxh h ;                                                      // cmd_crc is stand-alone : it guarantee rule uniqueness (i.e. contains match_crc)
			h.update(stems             ) ;
			h.update(job_name          ) ;
			h.update(targets           ) ;
			h.update(force             ) ;
			h.update(create_match_attrs) ;
			h.update(start_cmd_attrs   ) ;
			h.update(cmd               ) ;
			h.update(end_cmd_attrs     ) ;
			cmd_crc = h.digest() ;
		}
		{	Hash::Xxh h ;
			h.update(submit_rsrcs_attrs) ;
			h.update(start_rsrcs_attrs ) ;
			h.update(targets           ) ;                                     // all is not necessary, but simpler to code
			rsrcs_crc = h.digest() ;
		}
	}

	//
	// Rule::SimpleMatch
	//

	Rule::SimpleMatch::SimpleMatch(Job job) : rule{job->rule} {
		::string name_ = job.full_name() ;
		//
		SWEAR(Rule(name_)==rule) ;                                             // only name suffix is considered to make Rule
		//
		char* p = &name_[name_.size()-( rule->n_static_stems*(sizeof(FileNameIdx)*2) + sizeof(Idx) )] ; // start of suffix
		for( VarIdx s=0 ; s<rule->n_static_stems ; s++ ) {
			FileNameIdx pos = to_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			FileNameIdx sz  = to_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			stems.push_back(name_.substr(pos,sz)) ;
		}
	}

	::ostream& operator<<( ::ostream& os , Rule::SimpleMatch const& m ) {
		os << "RSM(" << m.rule << ',' << m.stems << ')' ;
		return os ;
	}

	void Rule::SimpleMatch::_compute_targets() const {
		for( VarIdx t=0 ; t<rule->targets.size() ; t++ ) {
			bool is_star = rule->flags(t)[TFlag::Star] ;
			_targets.push_back(_subst_target(
				rule->targets[t].second.pattern
			,	[&](VarIdx s)->::string {
					if (s<rule->n_static_stems) {                  return is_star ? escape(stems[s]) : stems[s] ; }
					else                        { SWEAR(is_star) ; return '('+rule->stems[s].second+')'         ; }
				}
			,	is_star/*escape*/
			)) ;
		}
	}

	::vector_s Rule::SimpleMatch::target_dirs() const {
		::set_s dirs ;
		for( auto const& [k,t] : rule->targets ) {
			::string target = _subst_target(
				t.pattern
			,	[&](VarIdx s)->::string { return stems[s] ; }
			,	false/*escape*/
			,	rule->n_static_stems/*stop_above*/
			) ;
			size_t sep = target.rfind('/') ;
			if (sep!=Npos) dirs.insert(target.substr(0,sep)) ;
		}
		return mk_vector(dirs) ;
	}

	::pair_ss Rule::SimpleMatch::name() const {
		::vector<FileNameIdx> poss ; poss.resize(rule->n_static_stems) ;
		::string name = _subst_target( rule->job_name ,
			[&]( FileNameIdx p , VarIdx s ) -> ::string {
				if (s<rule->n_static_stems) { poss[s] = p ; return stems[s]   ; }
				else                        {               return {StarMrkr} ; }
			}
		) ;
		::string sfx = rule.job_sfx() ;                                        // provides room for stems, but we have to fill it
		size_t i = 1 ;                                                         // skip initial JobMrkr
		for( VarIdx s=0 ; s<rule->n_static_stems ; s++ ) {
			FileNameIdx p  = poss [s]        ;
			FileNameIdx sz = stems[s].size() ;
			from_int( &sfx[i] , p  ) ; i+= sizeof(FileNameIdx) ;
			from_int( &sfx[i] , sz ) ; i+= sizeof(FileNameIdx) ;
		}
		return {name,sfx} ;
	}

	::string Rule::SimpleMatch::user_name() const {
		return _subst_target( rule->job_name ,
			[&](VarIdx s)->::string {
				if (s<rule->n_static_stems) return stems[s] ;
				else                        return {'*'}    ;
			}
		) ;
	}

	//
	// Rule::Match
	//

	Rule::Match::Match( RuleTgt rt , ::string const& target ) {
		Trace trace("Match",rt,target) ;
		Py::Gil   gil ;
		Py::Match m   = rt.pattern().match(target) ;
		if (!m) { trace("no_match") ; return ; }
		rule = rt ;
		for( VarIdx s=0 ; s<rt->n_static_stems ; s++ ) stems.push_back(m[to_string('_',s)]) ;
		::vector<VarIdx> const& conflicts = rt->targets[rt.tgt_idx].second.conflicts ;
		if (conflicts.empty()) { trace("stems",stems) ; return ; }                     // fast path : avoid computing targets()
		targets() ;                                                                    // _match needs targets but do not compute them as targets computing needs _match
		for( VarIdx t : rt->targets[rt.tgt_idx].second.conflicts )
			if (_match(t,target)) {                                            // if target matches an earlier target, it is not a match for this one
				rule .clear() ;
				stems.clear() ;
				trace("conflict") ;
				return ;
			}
		trace("stems",stems) ;
	}

	::ostream& operator<<( ::ostream& os , Rule::Match const& m ) {
		os << "RM(" << m.rule << ',' << m.stems << ')' ;
		return os ;
	}

	Py::Pattern const& Rule::Match::_target_pattern(VarIdx t) const {
		if (_target_patterns.empty()) _target_patterns.resize(rule->targets.size()) ;
		SWEAR(_targets.size()>t) ;                                                    // _targets must have been computed up to t at least
		if (!*_target_patterns[t]) _target_patterns[t] = _targets[t] ;
		return _target_patterns[t] ;
	}

	bool Rule::Match::_match( VarIdx t , ::string const& target ) const {
		Py::Gil gil ;
		SWEAR(_targets.size()>t) ;                                                  // _targets must have been computed up to t at least
		if (rule->flags(t)[TFlag::Star]) return +_target_pattern(t).match(target) ;
		else                             return target==_targets[t]               ;
	}

}
