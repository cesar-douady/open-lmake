// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "pycxx.hh"

#include "serialize.hh"

#include "core.hh"

namespace Engine {

	using namespace Disk ;
	using namespace Re   ;

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
	// cb_fixed is called on each fixed part found
	// cb_stem  is called on each stem       found
	// stems are of the form {<identifier>\*?} or {<identifier>?\*?:.*} (where .* after : must have matching {})
	// cb_stem is called with :
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
				[[fallthrough]] ;
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
					size_t start = 0          ; while( start<key.size() && is_space(key[start]) ) start++ ;
					size_t end   = key.size() ; while( end>start        && is_space(key[end-1]) ) end  -- ;
					if (end<=start) key.clear() ;
					else            key = key.substr(start,end-start) ;
					bool star = +key && key.back()=='*' ;
					if (star) key.pop_back() ;
					bool unnamed = !key ;
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
	static ::string _subst_target( ::string const& str , SubstTargetFunc const& cb , bool escape=false , VarIdx stop_above=Rule::NoVar ) {
		::string res ;
		for( size_t i=0 ; i<str.size() ; i++ ) {
			char c = str[i] ;
			if (c==Rule::StemMrkr) {
				VarIdx stem = decode_int<VarIdx>(&str[i+1]) ; i += sizeof(VarIdx) ;
				if (stem>=stop_above) return res ;
				res += cb(res.size(),stem) ;
			} else {
				if ( escape && _g_specials.find(c)!=Npos ) res += '\\' ;
				/**/                                       res += c    ;
			}
		}
		return res ;
	}
	// provide shortcut when pos is unused
	static inline ::string _subst_target( ::string const& str , ::function<string(VarIdx)> const& cb , bool escape=false , VarIdx stop_above=-1 ) {
		return _subst_target( str , [&](FileNameIdx,VarIdx s)->::string { return cb(s) ; } , escape , stop_above ) ;
	}

	// same as above, except pos is position in input and no result
	using ParseTargetFunc = ::function<void( FileNameIdx pos , VarIdx stem )> ;
	static void _parse_target( ::string const& str , ParseTargetFunc const& cb ) {
		for( size_t i=0 ; i<str.size() ; i++ ) {
			char c = str[i] ;
			if (c==Rule::StemMrkr) {
				VarIdx stem = decode_int<VarIdx>(&str[i+1]) ; i += sizeof(VarIdx) ;
				cb(i,stem) ;
			}
		}
	}
	// provide shortcut when pos is unused
	static inline void _parse_target( ::string const& str , ::function<void(VarIdx)> const& cb ) {
		_parse_target( str , [&](FileNameIdx,VarIdx s)->void { cb(s) ; } ) ;
	}

	template<class Flag> static void _mk_flags( BitMap<Flag>& flags , ::string const& key , PyObject** p , ssize_t n ) {
		for( ssize_t i=0 ; i<n ; i++ ) {
			if (PyUnicode_Check(p[i])) {
				const char* flag_str = PyUnicode_AsUTF8(p[i]) ;
				bool        neg      = flag_str[0]=='-'       ;
				flag_str += neg ;
				if (!can_mk_enum<Flag>(flag_str)) throw to_string("unexpected flag ",flag_str," for ",key) ;
				Flag flag = mk_enum<Flag>(flag_str) ;
				if ( flag<Flag::RuleMin || flag>=Flag::RuleMax1 ) throw to_string("unexpected flag ",flag_str," for ",key) ;
				if (neg) flags &= ~flag ;
				else     flags |=  flag ;
			} else if (PySequence_Check(p[i])) {
				PyObject* py_fast_dep = PySequence_Fast(p[i],"") ;
				SWEAR(py_fast_dep) ;
				ssize_t    n2 = PySequence_Fast_GET_SIZE(py_fast_dep) ;
				PyObject** p2 = PySequence_Fast_ITEMS   (py_fast_dep) ;
				_mk_flags(flags,key,p2,n2) ;
			} else {
				throw to_string(key,"has a flag which is not a str") ;
			}
		}
	}
	template<class Flag> static ::string _split_flags( BitMap<Flag>& flags , ::string const& key , PyObject* py ) {
		if ( PyUnicode_Check (py)) return PyUnicode_AsUTF8(py) ;
		if (!PySequence_Check(py)) throw to_string(key," is neither a str nor a sequence") ;
		PyObject* py_fast = PySequence_Fast(py,"") ;
		SWEAR(py_fast) ;
		ssize_t    n = PySequence_Fast_GET_SIZE(py_fast) ;
		PyObject** p = PySequence_Fast_ITEMS   (py_fast) ;
		if (n<1                   ) throw to_string(key,"is empty"    ) ;
		if (!PyUnicode_Check(p[0])) throw to_string(key,"is not a str") ;
		_mk_flags(flags,"dep "+key,p+1,n-1) ;
		return PyUnicode_AsUTF8(p[0]) ;
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

	namespace Attrs {

		bool/*updated*/ acquire( bool& dst , PyObject* py_src ) {
			if (!py_src        ) {           return false ;                              }
			if (py_src==Py_None) { if (!dst) return false ; dst = false ; return true  ; }
			//
			int v = PyObject_IsTrue(py_src) ;
			if (v==-1) throw "cannot determine truth value"s ;
			dst = v ;
			return true ;
		}

		bool/*updated*/ acquire( Delay& dst , PyObject* py_src , Delay min , Delay max ) {
			if (!py_src        ) {           return false ;                           }
			if (py_src==Py_None) { if (!dst) return false ; dst = {} ; return true  ; }
			//
			if (PyFloat_Check(py_src)) {
				dst = Time::Delay(PyFloat_AsDouble(py_src)) ;
			} else if (PyLong_Check(py_src)) {
				long sd = PyLong_AsLong(py_src) ;
				if ( sd==-1 && PyErr_Occurred() ) { PyErr_Clear() ; throw "overflow"s  ; }
				dst = Delay(sd) ;
				if ( dst<min                    )                   throw "underflow"s ;
				if ( dst>max                    )                   throw "overflow"s  ;
			} else if (PyUnicode_Check(py_src)) {
				PyObject* f = PyFloat_FromString(py_src) ;
				if (!f) throw "cannot convert to float"s ;
				dst = Time::Delay(PyFloat_AsDouble(f)) ;
				Py_DECREF(f) ;
			} else {
				throw "cannot convert to float"s ;
			}
			return true ;
		}

		bool/*updated*/ acquire( DbgEntry& dst , PyObject* py_src ) {
			if (!py_src        ) {                          return false ;                           }
			if (py_src==Py_None) { if (!dst.first_line_no1) return false ; dst = {} ; return true  ; }
			//
			if (!PySequence_Check(py_src)) throw "not a sequence"s ;
			PyObject* fast_val = PySequence_Fast(py_src,"")                 ; SWEAR(fast_val  ) ;
			size_t     n       = size_t(PySequence_Fast_GET_SIZE(fast_val)) ; SWEAR(n==4    ,n) ;
			PyObject** p       =        PySequence_Fast_ITEMS   (fast_val)  ;
			acquire(dst.module        ,p[0]                 ) ;
			acquire(dst.qual_name     ,p[1]                 ) ;
			acquire(dst.filename      ,p[2]                 ) ;
			acquire(dst.first_line_no1,p[3],size_t(1)/*min*/) ;
			return true ;
		}

		::string subst_fstr( ::string const& fstr , ::umap_s<CmdIdx> const& var_idxs , VarIdx& n_unnamed , BitMap<VarCmd>& need ) {
			::string res ;
			_parse_py( fstr , nullptr/*unnamed_star_idx*/ ,
				[&]( ::string const& fixed )->void {
					res.append(fixed) ;
				}
			,	[&]( ::string const& k , bool star , bool unnamed , ::string const* def )->void {
					SWEAR(var_idxs.contains(k)) ;
					SWEAR(!star               ) ;
					SWEAR(!def                ) ;
					size_t sz = res.size() ;
					res.resize(sz+1+sizeof(VarCmd)+sizeof(VarIdx)) ;
					char* p = res.data()+sz ;
					auto it = var_idxs.find(k)    ;
					p[0] = Rule::StemMrkr ;
					need |= it->second.bucket ;
					encode_enum( p+1                , it->second.bucket ) ;
					encode_int ( p+1+sizeof(VarCmd) , it->second.idx    ) ;
					n_unnamed += unnamed ;
				}
			) ;
			return res ;
		}

	}

	//
	// DepsAttrs
	//

	ENUM( DepKind
	,	Dep
	,	Python
	,	Shell
	)

	static bool/*keep*/ _qualify_dep( ::string const& key , ::string const& dep , DepKind kind=DepKind::Unknown , ::string const& dep_for_msg={} ) {
		::string dir_s          = dep.substr(0,dep.find(Rule::StemMrkr))          ;
		size_t   dir_pos        = dir_s.rfind('/')                                ;
		/**/     dir_s          = dir_pos==Npos ? ""s : dir_s.substr(0,dir_pos+1) ;
		//
		auto bad = [&] [[noreturn]] (::string const& msg) {
			::string const& d = +dep_for_msg ? dep_for_msg : dep ;
			if (kind==DepKind::Dep) throw to_string("dep ",key    ," (",d,") ",msg) ;
			else                    throw to_string(mk_snake(kind)," (",d,") ",msg) ;
		} ;
		auto bad_canon = [&] [[noreturn]] (::string const& dir_s) {
			bad("must be canonical : "+dir_s+dep.substr(dir_pos+1)) ;
		} ;
		//
		if (is_lcl(dep)) return true/*keep*/ ;
		// dep is non-local, substitute relative/absolute if it lies within a source dirs
		::string rel_dir_s = mk_rel(dir_s,*g_root_dir+'/') ; if (is_lcl_s(rel_dir_s)) bad_canon(rel_dir_s) ;
		::string abs_dir_s = mk_abs(dir_s,*g_root_dir+'/') ;
		//
		for( ::string const& sd_s : g_src_dirs_s ) {
			if (is_lcl_s(sd_s)) continue ;                                                                          // nothing to recognize inside repo
			if (abs_dir_s.starts_with(sd_s)) { if (abs_dir_s==dir_s) return true/*keep*/ ; bad_canon(abs_dir_s) ; }
			if (rel_dir_s.starts_with(sd_s)) { if (rel_dir_s==dir_s) return true/*keep*/ ; bad_canon(rel_dir_s) ; }
		}
		if (kind!=DepKind::Dep) return false/*keep*/ ;                         // normal case : interpreter is outside repo typically system python or bash
		bad("outside repository and all source dirs must be suppressed") ;
	}
	BitMap<VarCmd> DepsAttrs::init( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& var_idxs , RuleData const& rd ) {
		full_dynamic = py_src==Py_None ;
		if (full_dynamic) return {} ;
		//
		SWEAR(PyDict_Check(py_src)) ;
		BitMap<VarCmd>   need   ;
		PyObject*        py_key = nullptr/*garbage*/ ;
		PyObject*        py_val = nullptr/*garbage*/ ;
		Py_ssize_t       pos    = 0                  ;
		while (PyDict_Next( py_src , &pos , &py_key , &py_val )) {
			SWEAR(PyUnicode_Check(py_key)) ;
			::string key = PyUnicode_AsUTF8(py_key) ;
			if (py_val==Py_None) {
				deps.emplace_back(key,DepSpec()) ;
				continue ;
			}
			VarIdx   n_unnamed  = 0                                              ;
			Dflags   df         = StaticDflags                                   ;
			::string dep        = _split_flags(df,"dep "+key,py_val)             ;
			::string parsed_dep = Attrs::subst_fstr(dep,var_idxs,n_unnamed,need) ;
			//
			rd.add_cwd( parsed_dep , df[Dflag::Top] ) ;
			_qualify_dep( key , parsed_dep , DepKind::Dep , dep ) ;
			//
			if (n_unnamed) {
				for( auto const& [k,ci] : var_idxs ) if (ci.bucket==VarCmd::Stem) n_unnamed-- ;
				if (n_unnamed) throw to_string("dep ",key," (",dep,") ","contains some but not all unnamed static stems") ;
			}
			deps.emplace_back( key , DepSpec( ::move(parsed_dep) , df ) ) ;
		}
		if (_qualify_dep( {} , rd.interpreter[0] , rd.is_python?DepKind::Python:DepKind::Shell )) deps.emplace_back( "<interpreter>" , DepSpec(::copy(rd.interpreter[0]),StaticDflags) ) ;
		if (deps.size()>=Rule::NoVar) throw to_string("too many static deps : ",deps.size()) ;
		return need ;
	}

	::vmap_s<pair_s<AccDflags>> DynamicDepsAttrs::eval( Rule::SimpleMatch const& match ) const {
		SWEAR( !(need&(NeedDeps|NeedRsrcs)) , need ) ;
		//
		Accesses a = match.rule->cmd_needs_deps ? Accesses::All : Accesses::None ;
		::vmap_s<pair_s<AccDflags>> res ;
		for( auto const& [k,ds] : spec.deps ) res.emplace_back( k , pair_s( parse_fstr(ds.pattern,match) , AccDflags(a,ds.dflags) ) ) ;
		//
		if (is_dynamic) {
			Py::Gil   gil    ;
			PyObject* py_dct = _eval_code(match) ;
			::map_s<VarIdx> dep_idxs ;
			for( VarIdx di=0 ; di<spec.deps.size() ; di++ ) dep_idxs[spec.deps[di].first] = di ;
			PyObject*  py_key = nullptr/*garbage*/ ;
			PyObject*  py_val = nullptr/*garbage*/ ;
			Py_ssize_t pos    = 0                  ;
			while (PyDict_Next( py_dct , &pos , &py_key , &py_val )) {
				if (py_val==Py_None) continue ;
				if (!PyUnicode_Check(py_key)) {
					PyObject* py_key_str = PyObject_Str(py_key) ;
					Py_DECREF(py_dct) ;
					if (!py_key_str) throw "a dep has a non printable key"s ;
					::string key_str = PyUnicode_AsUTF8(py_key_str) ;
					Py_DECREF(py_key_str) ;
					throw to_string("a dep has a non str key : ",key_str) ;
				}
				::string key = PyUnicode_AsUTF8(py_key)           ;
				Dflags   df  = StaticDflags                       ;            // initial value
				::string dep = _split_flags(df,"dep "+key,py_val) ;            // updates df
				match.rule->add_cwd( dep , df[Dflag::Top] ) ;
				_qualify_dep( key , dep , DepKind::Dep ) ;
				::pair_s<AccDflags> e { dep , {a,df} } ;
				if (spec.full_dynamic) { SWEAR(!dep_idxs.contains(key),key) ; res.emplace_back(key,e) ;          } // dep cannot be both static and dynamic
				else                   {                                      res[dep_idxs.at(key)].second = e ; } // if not full_dynamic, all deps must be listed in spec
			}
			Py_DECREF(py_dct) ;
		}
		//
		return res  ;
	}

	//
	// SubmitRsrcsAttrs
	//

	void SubmitRsrcsAttrs::s_canon(::vmap_ss& rsrcs) {
		for ( auto& [k,v] : rsrcs ) {
			/**/                                 if (!can_mk_enum<StdRsrc>(k)) continue ; // resource is not standard
			StdRsrc  r   = mk_enum<StdRsrc>(k) ; if (k!=mk_snake(r)          ) continue ; // .
			uint64_t val = 0 /*garbage*/       ;
			try                     { val = from_string_with_units<uint64_t>(v) ; }
			catch (::string const&) { continue ;                                  } // value is not recognized
			//
			if ( g_config.rsrc_digits[+r] && val ) {
				uint8_t sw = ::max(0,int(bit_width(val))-int(g_config.rsrc_digits[+r])) ; // compute necessary shift for rounding, /!\ beware of signness with unsigned arithmetic
				val = (((val-1)>>sw)+1)<<sw ;                                             // quantify by rounding up
			}
			//
			v = to_string_with_units(val) ;
		}
	}

	//
	// Cmd
	//

	BitMap<VarCmd> Cmd::init( bool /*is_dynamic*/ , PyObject* py_src , ::umap_s<CmdIdx> const& var_idxs , RuleData const& rd ) {
		BitMap<VarCmd> need    ;
		::string       raw_cmd ;
		Attrs::acquire_from_dct( raw_cmd   , py_src , "cmd"       ) ;
		Attrs::acquire_from_dct( decorator , py_src , "decorator" ) ;
		if (rd.is_python) {
			cmd = ::move(raw_cmd) ;
		} else {
			VarIdx n_unnamed = 0 ;
			cmd = Attrs::subst_fstr(raw_cmd,var_idxs,n_unnamed,need) ;
			SWEAR( !n_unnamed , n_unnamed ) ;
		}
		return need ;
	}

	::pair_ss/*script,call*/ DynamicCmd::eval( Job job , Rule::SimpleMatch& match , ::vmap_ss const& rsrcs ) const {
		Rule r = +job ? job->rule : match.rule ;                                                                     // if we have no job, we must have a match as job ...
		if (r->is_python) {                                                                                          // ... is there to lazy evaluate match if necessary
			::string res ;
			append_to_string( res , "lmake_runtime = {}\n"                                                                   ) ;
			append_to_string( res , "exec(open(",mk_py_str(*g_lmake_dir+"/lib/lmake_runtime.py"),").read(),lmake_runtime)\n" ) ;
			append_to_string( res , spec.decorator," = lmake_runtime['lmake_func']\n"                                        ) ;
			append_to_string( res , spec.decorator,".dbg = {\n"                                                              ) ;
			bool first = true ;
			SWEAR(+r) ;
			for( auto const& [k,v] : r->dbg_info ) {
				if (!first) res += ',' ;
				first = false ;
				append_to_string(res,'\t',mk_py_str(k)," : (",mk_py_str(v.module),',',mk_py_str(v.qual_name),',',mk_py_str(v.filename),',',v.first_line_no1,")\n") ;
			}
			res += "}\n" ;
			eval_ctx( job , match , rsrcs
			,	[&]( VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) -> void {
					Rule r = match.rule ;
					if ( vc!=VarCmd::Target || !r->tflags(i)[Tflag::Star] ) {
						append_to_string(res,key," = ",mk_py_str(val),'\n') ;
					} else {
						::vector_s args ;
						::string expr = _subst_target(
							r->targets[i].second.pattern
						,	[&](VarIdx s)->::string {
								::string k = r->stems[s].first ;
								if ( k.front()=='<' and k.back()=='>' ) k = k.substr(1,k.size()-2) ;
								if ( s>=r->n_static_stems             ) { args.push_back(k) ; return to_string('{',k,'}') ; }
								else                                                          return match.stems[s]       ;
							}
						) ;
						append_to_string(res,"def ",key,'(') ;
						bool f = true ;
						for( ::string const& a : args )
							if (f) { append_to_string(res,' ' ,a,' ') ; f = false ; }
							else     append_to_string(res,", ",a,' ') ;
						append_to_string(res,") : return f"   ,mk_py_str(expr),'\n') ;
						append_to_string(res,key,".regexpr = ",mk_py_str(val ),'\n') ;
					}
				}
			,	[&]( VarCmd , VarIdx , ::string const& key , ::vmap_ss const& val ) -> void {
					append_to_string(res,key," = {\n") ;
					bool f = true ;
					for( auto const& [k,v] : val ) {
						if (!f) res += ',' ;
						append_to_string(res,'\t',mk_py_str(k)," : ",mk_py_str(v),'\n') ;
						f = false ;
					}
					res += "}\n" ;
				}
			) ;
			res += ensure_nl(spec.cmd) ;
			return {res,"cmd()\n"} ;
		} else {
			if (!is_dynamic) return {parse_fstr(spec.cmd,job,match,rsrcs),{}} ;
			Py::Gil   gil    ;
			PyObject* cmd_py = _eval_code(job,match,rsrcs) ;
			::string  cmd    ;
			Attrs::acquire(cmd,cmd_py) ;
			Py_DECREF(cmd_py)  ;
			return {cmd,{}} ;
		}
	}

	::ostream& operator<<( ::ostream& os , DbgEntry const& de ) {
		if (+de) return os<<"( "<<de.module<<" , "<<de.qual_name<<" , "<<de.filename<<" , "<<de.first_line_no1<<" )" ;
		else     return os<<"()"                                                                                     ;
	}

	//
	// RuleData
	//

	size_t RuleData::s_name_sz = 0 ;

	static void _append_stem( ::string& target , VarIdx stem_idx ) {
		::string s ; s.resize(sizeof(VarIdx)) ;
		encode_int( s.data() , stem_idx ) ;
		target += Rule::StemMrkr ;
		target += s              ;
	}

	RuleData::RuleData( Special s , ::string const& src_dir_s ) {
		if (s<=Special::Shared) SWEAR( !src_dir_s                          , s , src_dir_s ) ; // shared rules cannot have parameters as, precisely, they are shared
		else                    SWEAR( +src_dir_s && src_dir_s.back()=='/' , s , src_dir_s ) ; // ensure source dir ends with a /
		special = s           ;
		name    = mk_snake(s) ;
		switch (s) {
			case Special::Req      : force = true      ; break ;
			case Special::Infinite : prio  = -Infinity ; break ;               // -inf : it can appear after other rules
			case Special::GenericSrc :
				name             = "source dir"  ;
				job_name         = src_dir_s     ; _append_stem(job_name,0) ;
				force            = true          ;
				n_static_stems   = 1             ;
				n_static_targets = 1             ;
				allow_ext        = true          ;                              // sources may lie outside repo
				stems           .emplace_back("",".*"                 ) ;
				stem_mark_counts.push_back   (0                       ) ;
				targets         .emplace_back("",TargetEntry(job_name)) ;
				_compile() ;
			break ;
			default : FAIL(s) ;
		}
	}

	::ostream& operator<<( ::ostream& os , RuleData const& rd ) {
		return os << "RD(" << rd.name << ')' ;
	}

	template<class Flag> static BitMap<Flag> _get_flags( size_t n_ignore , Py::Sequence const& py_flags , BitMap<Flag> dflt ) {
		SWEAR( size_t(py_flags.size())>=n_ignore , py_flags.size() , n_ignore ) ;
		BitMap<Flag> flags[2/*inv*/] ;
		for( auto const& k2 : py_flags ) {
			if (n_ignore) { n_ignore-- ; continue ; }
			::string k2s = Py::String(k2) ;
			Flag f   ;
			bool inv = k2s[0]=='-' ;
			if (inv) k2s = k2s.substr(1) ;
			try                                         { f = mk_enum<Flag>(k2s) ;                 }
			catch(::out_of_range const&)                { throw to_string("unknown flag : ",k2s) ; }
			if ( f<Flag::RuleMin || f>=Flag::RuleMax1 ) { throw to_string("unknown flag : ",k2s) ; }
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
			_parse_target( a , [&]( FileNameIdx pos , VarIdx s )->void { if ( s>=n_static_stems && sz_a==a.size() ) sz_a = is_prefix?pos:a.size()-1-pos ; } ) ;
			_parse_target( b , [&]( FileNameIdx pos , VarIdx s )->void { if ( s>=n_static_stems && sz_b==b.size() ) sz_b = is_prefix?pos:b.size()-1-pos ; } ) ;
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
			// acquire essential (necessary for Anti & GenericSrc)
			//
			//
			field = "__special__" ;
			if (dct.hasKey(field)) {
				special = mk_enum<Special>(Py::String(dct[field])) ;
				if (special<=Special::Shared) throw to_string("unexpected value for __special__ attribute : ",special) ;
			} else {
				special = Special::Plain ;
			}
			field = "name" ; if (dct.hasKey(field)) name  = Py::String(dct[field]) ; else throw "not found"s ;
			field = "prio" ; if (dct.hasKey(field)) prio  = Py::Float (dct[field]) ;
			field = "cwd"  ; if (dct.hasKey(field)) cwd_s = Py::String(dct[field]) ;
			if (+cwd_s) {
				if (cwd_s.back ()!='/') cwd_s += '/' ;
				if (cwd_s.front()=='/') {
					if (cwd_s.starts_with(*g_root_dir+'/')) cwd_s.erase(0,g_root_dir->size()+1) ;
					else                                    throw "cwd must be relative to root dir"s ;
				}
			}
			//
			Trace trace("_acquire_py",name,prio) ;
			//
			::umap_ss      stem_defs  ;
			::map_s<Bool3> stem_stars ;                                        // ordered so that stems are ordered, Maybe means stem is used both as static and star
			field = "stems" ;
			if (dct.hasKey(field))
				for( auto const& [k,v] : Py::Dict(dct[field]) ) stem_defs.emplace( ::string(Py::String(k)) , ::string(Py::String(v)) ) ;
			//
			// augment stems with definitions found in job_name and targets
			size_t unnamed_star_idx = 1 ;                                                                               // free running while walking over job_name + targets
			auto augment_stems = [&]( ::string const& k , bool star , ::string const* re , bool star_only ) -> void {
				if (re) {
					auto [it,inserted] = stem_defs.emplace(k,*re) ;
					if ( !inserted && *re!=it->second ) throw to_string("2 different definitions for stem ",k," : ",it->second," and ",*re) ;
				}
				if ( !star_only || star ) {
					auto [it,inserted] = stem_stars.emplace(k,No|star) ;
					if ( !inserted && (No|star)!=it->second ) it->second = Maybe ; // stem is used both as static and star
				}
			} ;
			field = "job_name" ;
			if (!dct.hasKey(field)) throw "not found"s ;
			job_name = Py::String(dct[field]) ;
			_parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> void { augment_stems(k,star,re,false/*star_only*/) ; }
			) ;
			field = "targets" ;
			if (!dct.hasKey(field)) throw "not found"s ;
			Py::Dict py_targets{dct[field]} ;
			::string job_name_key ;
			::string job_name_msg = "job_name" ;
			for( auto const& [py_k,py_tfs] : py_targets ) {
				field = Py::String(py_k) ;
				Py::Sequence pyseq_tfs{ py_tfs } ;                             // targets are a tuple (target_pattern,flags...)
				::string target = Py::String(pyseq_tfs[0]) ;
				// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
				if (target!=job_name) {
					_parse_py( target , &unnamed_star_idx ,
						// static stems are declared in job_name, but error will be caught later on, when we can generate a sound message
						[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> void { augment_stems(k,star,re,true/*star_only*/) ; }
					) ;
				} else if (!job_name_key) {
					job_name_key =           field ;
					job_name_msg = "target "+field ;
				}
			}
			//
			// gather job_name and targets
			field            = "job_name" ;
			unnamed_star_idx = 1          ;                                    // reset free running at each pass over job_name+targets
			VarIdx n_static_unnamed_stems = 0     ;
			bool   job_name_is_star       = false ;
			auto   stem_words             = []( ::string const& k , bool star , bool unnamed ) -> ::string {
				const char* stem = star ? "star stem" : "stem" ;
				return unnamed ? to_string("unnamed ",stem) : to_string(stem,' ',k) ;
			} ;
			_parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) -> void {
					if      (!stem_defs.contains(k)) throw to_string("found undefined ",stem_words(k,star,unnamed)," in ",job_name_msg) ;
					if      (star                  ) job_name_is_star = true ;
					else if (unnamed               ) n_static_unnamed_stems++ ;
				}
			) ;
			//
			field = "targets" ;
			bool found_matching = false ;
			{	::vmap_s<TargetEntry> star_targets ;                           // defer star targets so that static targets are put first
				bool                 seen_top     = false ;
				for( auto const& [py_k,py_tfs] : Py::Dict(dct[field]) ) {      // targets are a tuple (target_pattern,flags...)
					field = Py::String(py_k) ;
					Py::Sequence pyseq_tfs      { py_tfs }                 ;
					bool         is_native_star = false                    ;
					::string     target         = Py::String(pyseq_tfs[0]) ;
					::set_s      missing_stems  ;
					// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
					if (target==job_name) {
						if (job_name_is_star) is_native_star = true ;
					} else {
						for( auto const& [k,s] : stem_stars ) if (s!=Yes) missing_stems.insert(k) ;
						_parse_py( target , &unnamed_star_idx ,
							[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) -> void {
								if (!stem_defs.contains(k)) throw to_string("found undefined ",stem_words(k,star,unnamed)," in target") ;
								//
								if (star) {
									is_native_star = true ;
									return ;
								}
								auto it = stem_stars.find(k) ;
								if ( it==stem_stars.end() || it->second==Yes )
									throw to_string(stem_words(k,star,unnamed)," appears in target but not in ",job_name_msg,", consider using ",k,'*') ;
								missing_stems.erase(k) ;
							}
						) ;
					}
					Tflags min_flags  = Tflags::None ; _split_flags(min_flags,"target",pyseq_tfs.ptr()) ;
					Tflags max_flags  = Tflags::All  ; _split_flags(max_flags,"target",pyseq_tfs.ptr()) ;
					Tflags dflt_flags = DfltTflags                                                      ;
					Tflags flags      = (dflt_flags&max_flags)|min_flags                                ; // tentative value to get the Match & Incremental flags
					if (is_native_star            ) dflt_flags |= Tflag::Star     ;
					if (!flags[Tflag::Match      ]) dflt_flags |= Tflag::Dep      ;
					if ( flags[Tflag::Incremental]) dflt_flags |= Tflag::Uniquify ;
					flags = (dflt_flags&max_flags)|min_flags ;                      // definitive value
					//
					if (flags[Tflag::Match]) found_matching = true ;
					// check
					bool is_stdout = field=="<stdout>" ;
					if ( target.starts_with(*g_root_dir) && target[g_root_dir->size()]=='/' ) throw to_string("target must be relative to root dir : "                         ,target) ;
					if ( !is_lcl(target)                                                    ) throw to_string("target must be local : "                                        ,target) ;
					if ( !is_canon(target)                                                  ) throw to_string("target must be canonical : "                                    ,target) ;
					if ( !flags[Tflag::Star       ] && is_native_star                       ) throw to_string("flag star cannot be reset because target contains star stems : ",target) ;
					if (  flags[Tflag::Match      ] && +missing_stems                       ) throw to_string("missing stems ",missing_stems," in target : "                   ,target) ;
					if ( !flags[Tflag::Match      ] && is_special()                         ) throw           "non-matching targets are meaningless for source and anti-rules"s         ;
					if (  flags[Tflag::Star       ] && is_stdout                            ) throw           "stdout cannot be directed to a star target"s                             ;
					if (  flags[Tflag::Phony      ] && is_stdout                            ) throw           "stdout cannot be directed to a phony target"s                            ;
					if (  flags[Tflag::Incremental] && is_stdout                            ) throw           "stdout cannot be directed to an incremental target"s                     ;
					chk(flags) ;
					seen_top |= flags[Tflag::Top] ;
					// record
					add_cwd( target , flags[Tflag::Top] ) ;
					(flags[Tflag::Star]?star_targets:targets).emplace_back( field , TargetEntry(::move(target),flags) ) ;
					if (field==job_name_key) add_cwd( job_name , flags[Tflag::Top] ) ;
				}
				if (!job_name_key) add_cwd( job_name , seen_top ) ;
				n_static_targets = targets.size() ;
				if (!is_special()) for( auto& st : star_targets ) targets.push_back(::move(st)) ; // star-targets are meaningless for source & anti-rule
			}
			SWEAR(found_matching) ;                                            // we should not have come until here without a clean target
			field = "" ;
			if (targets.size()>NoVar) throw to_string("too many targets : ",targets.size()," > ",NoVar) ;
			::umap_s<VarIdx> stem_idxs ;
			for( bool star : {false,true} ) {                                  // keep only useful stems and order them : static first, then star
				for( auto const& [k,v] : stem_stars ) {
					if (v==(No|!star)) continue ;                                   // stems that are both static and start appear twice
					stem_idxs.emplace     ( k+" *"[star] , VarIdx(stems.size()) ) ;
					stems    .emplace_back( k            , stem_defs.at(k)      ) ;
				}
				if (!star) n_static_stems = stems.size() ;
			}
			::umap_s<CmdIdx> var_idxs ;
			/**/                                       var_idxs["stems"       ] = {VarCmd::Stems,0} ;
			for( VarIdx s=0 ; s<n_static_stems ; s++ ) var_idxs[stems[s].first] = {VarCmd::Stem ,s} ;
			if (stems.size()>NoVar) throw to_string("too many stems : ",stems.size()," > ",NoVar) ;
			//
			// reformat job_name & targets to improve matching efficiency
			// {Stem} is replaced by "StemMrkr<stem_idx>"
			// StemMrkr is there to unambiguously announce a stem idx
			//
			::string mk_tgt ;
			auto mk_fixed = [&]( ::string const& fixed                                                       )->void { mk_tgt += fixed ;                                   } ;
			auto mk_stem  = [&]( ::string const& key , bool star , bool /*unnamed*/ , ::string const* /*re*/ )->void { _append_stem(mk_tgt,stem_idxs.at(key+" *"[star])) ; } ;
			unnamed_star_idx = 1 ;                                                    // reset free running at each pass over job_name+targets
			mk_tgt.clear() ;
			_parse_py( job_name , &unnamed_star_idx , mk_fixed , mk_stem ) ;
			::string new_job_name = ::move(mk_tgt) ;
			// compile potential conflicts as there are rare and rather expensive to detect, we can avoid most of the verifications by statically analyzing targets
			for( VarIdx t=0 ; t<targets.size() ; t++ ) {
				TargetEntry& te = targets[t].second ;
				// avoid processing target if it is identical to job_name
				// this is not an optimization, it is to ensure unnamed_star_idx's match
				if (te.pattern==job_name) te.pattern = new_job_name ;
				else {
					mk_tgt.clear() ;
					_parse_py( te.pattern , &unnamed_star_idx , mk_fixed , mk_stem ) ;
					te.pattern = ::move(mk_tgt) ;
				}
				for( VarIdx t2=0 ; t2<t ; t2++ )
					if ( _may_conflict( n_static_stems , te.pattern , targets[t2].second.pattern ) ) { trace("conflict",t,t2) ; te.conflicts.push_back(t2) ; }
			}
			job_name = ::move(new_job_name) ;
			//
			//vvvvvvvvvvvvvvvvvvvvvvvv
			if (is_special()) return ;                                         // if special, we have no dep, no execution, we only need essential info
			//^^^^^^^^^^^^^^^^^^^^^^^^
			//
			// acquire fields linked to job execution
			//
			field = "interpreter" ; if (dct.hasKey(field)) Attrs::acquire( interpreter  ,            dct[field]       .ptr() ) ; if (!interpreter) throw "no interpreter found"s ;
			field = "is_python"   ; if (dct.hasKey(field)) Attrs::acquire( is_python    ,            dct[field]       .ptr() ) ; else              throw "not found"s            ;
			field = "n_tokens"    ; if (dct.hasKey(field)) Attrs::acquire( n_tokens_key , Py::Object(dct[field]).str().ptr() ) ;
			//
			/**/                                       var_idxs["targets"       ] = {VarCmd::Targets,0} ;
			for( VarIdx t=0 ; t<targets.size() ; t++ ) var_idxs[targets[t].first] = {VarCmd::Target ,t} ;
			//
			field = "deps" ;
			if (dct.hasKey("deps_attrs")) deps_attrs = { Py::Object(dct["deps_attrs"]).ptr() , var_idxs , *this } ;
			//
			/**/                                                    var_idxs["deps"                       ] = { VarCmd::Deps , 0 } ;
			for( VarIdx d=0 ; d<deps_attrs.spec.deps.size() ; d++ ) var_idxs[deps_attrs.spec.deps[d].first] = { VarCmd::Dep  , d } ;
			//
			field = "create_none_attrs"  ; if (dct.hasKey(field)) create_none_attrs  = { Py::Object(dct[field]).ptr() , var_idxs } ;
			field = "cache_none_attrs"   ; if (dct.hasKey(field)) cache_none_attrs   = { Py::Object(dct[field]).ptr() , var_idxs } ;
			field = "submit_rsrcs_attrs" ; if (dct.hasKey(field)) submit_rsrcs_attrs = { Py::Object(dct[field]).ptr() , var_idxs } ;
			field = "submit_none_attrs"  ; if (dct.hasKey(field)) submit_none_attrs  = { Py::Object(dct[field]).ptr() , var_idxs } ;
			//
			/**/                                                             var_idxs["resources"                           ] = { VarCmd::Rsrcs , 0 } ;
			for( VarIdx r=0 ; r<submit_rsrcs_attrs.spec.rsrcs.size() ; r++ ) var_idxs[submit_rsrcs_attrs.spec.rsrcs[r].first] = { VarCmd::Rsrc  , r } ;
			//
			field = "start_cmd_attrs"   ; if (dct.hasKey(field)) start_cmd_attrs   = { Py::Object(dct[field]).ptr() , var_idxs         } ;
			field = "cmd"               ; if (dct.hasKey(field)) cmd               = { Py::Object(dct[field]).ptr() , var_idxs , *this } ; else throw "not found"s ;
			field = "start_rsrcs_attrs" ; if (dct.hasKey(field)) start_rsrcs_attrs = { Py::Object(dct[field]).ptr() , var_idxs         } ;
			field = "start_none_attrs"  ; if (dct.hasKey(field)) start_none_attrs  = { Py::Object(dct[field]).ptr() , var_idxs         } ;
			field = "end_cmd_attrs"     ; if (dct.hasKey(field)) end_cmd_attrs     = { Py::Object(dct[field]).ptr() , var_idxs         } ;
			field = "end_none_attrs"    ; if (dct.hasKey(field)) end_none_attrs    = { Py::Object(dct[field]).ptr() , var_idxs         } ;
			//
			cmd_needs_deps = _get_cmd_needs_deps() ;
			max_tflags     = Tflags::None          ;
			min_tflags     = Tflags::All           ;
			for( auto const& [k,te] : targets ) {
				max_tflags |= te.tflags ;
				min_tflags &= te.tflags ;
			}
			//
			field = "dbg_info" ;
			if (dct.hasKey(field)) Attrs::acquire( dbg_info , dct[field].ptr() ) ;
			//
			field = "ete"   ; if (dct.hasKey(field)) exec_time = Delay(Py::Float (dct[field]))          ;
			field = "force" ; if (dct.hasKey(field)) force     =       Py::Object(dct[field]).as_bool() ;
			for( VarIdx t=0 ; t<targets.size() ; t++ ) {
				if (targets[t].first!="<stdout>") continue ;
				if (targets[t].second.tflags[Tflag::Star]) throw "<stdout> must be a static target"s ;
				stdout_idx = t ;
				break ;
			}
			for( VarIdx d=0 ; d<deps_attrs.spec.deps.size() ; d++ ) {
				if (deps_attrs.spec.deps[d].first!="<stdin>") continue ;
				stdin_idx = d ;
				break ;
			}
		}
		catch(::string const& e) { throw to_string("while processing ",name,'.',field," :\n"  ,indent(e)     ) ; }
		catch(Py::Exception & e) { throw to_string("while processing ",name,'.',field," :\n\t",e.errorValue()) ; }
	}

	TargetPattern RuleData::_mk_pattern( ::string const& target , bool for_name ) const {
		// Generate and compile Python pattern
		// target has the same syntax as Python f-strings except expressions must be named as found in stems
		// we transform that into a pattern by :
		// - escape specials outside keys
		// - transform f-string syntax into Python regexpr syntax
		// for example "a{b}c.d" with stems["b"]==".*" becomes "a(?P<_0>.*)c\.d"
		// remember that what is stored in targets is actually a stem idx, not a stem key
		//
		TargetPattern res       ;
		VarIdx        cur_group = 1 ;
		//
		res.txt = _subst_target(
			target
		,	[&](VarIdx s)->::string {
				if ( s>=n_static_stems && for_name ) {
					::string const& k = stems[s].first ;
					if (k.front()=='<'&&k.back()=='>' ) return escape(to_string('{',""s,"*}")) ; // when matching on job name, star stems are matched as they are reported to user
					else                                return escape(to_string('{',k  ,"*}")) ; // .
				}
				if ( grow(res.groups,s) )
					return to_string("(?:\\",res.groups[s],')') ; // already seen, we must protect against following text potentially containing numbers
				grow(res.groups,s)  = cur_group             ;
				cur_group          += 1+stem_mark_counts[s] ;
				return to_string('(',stems[s].second,')') ;
			}
		,	true/*escape*/
		) ;
		res.re = RegExpr( res.txt , true/*fast*/ ) ; // stem regexprs have been validated, normally there is no error here
		return res ;
	}

	void RuleData::_compile() {
		try {
			for( auto const& [k,s] : stems )
				try         { stem_mark_counts.push_back(RegExpr(s).mark_count()) ; }
				catch (...) { throw to_string("bad regexpr for stem ",k," : ",s) ;  }
			// job_name & targets
			/**/                                job_name_pattern =        _mk_pattern(job_name  ,true /*for_name*/)  ;
			for( auto const& [k,te] : targets ) target_patterns.push_back(_mk_pattern(te.pattern,false/*for_name*/)) ;
			_set_crcs() ;
			deps_attrs        .compile() ;
			create_none_attrs .compile() ;
			cache_none_attrs  .compile() ;
			submit_rsrcs_attrs.compile() ;
			submit_none_attrs .compile() ;
			start_cmd_attrs   .compile() ;
			cmd               .compile() ;
			start_rsrcs_attrs .compile() ;
			start_none_attrs  .compile() ;
			end_cmd_attrs     .compile() ;
			end_none_attrs    .compile() ;
		}
		catch (::string const& e) {
			throw to_string("while processing ",name," :\n",indent(e)) ;
		}
	}

	template<class T> static ::string _pretty_vmap( size_t i , ::vmap_s<T> const& m , bool uniq=false ) {
		OStringStream res  ;
		size_t        wk   = 0 ;
		::uset_s      keys ;
		//
		for( auto const& [k,_] : m ) wk = ::max(wk,k.size()) ;
		for( auto const& [k,v] : m ) if ( !uniq || keys.insert(k).second ) res << ::string(i,'\t') << ::setw(wk)<<k <<" : "<< v <<'\n' ;
		//
		return res.str() ;
	}
	static ::string _pretty_fstr( ::string const& fstr , RuleData const& rd ) {
			::string res ;
			for( size_t ci=0 ; ci<fstr.size() ; ci++ ) {
				char c = fstr[ci] ;
				switch (c) {
					case Rule::StemMrkr : {
						VarCmd k = decode_enum<VarCmd>(&fstr[ci+1]) ; ci += sizeof(VarCmd) ;
						VarIdx i = decode_int <VarIdx>(&fstr[ci+1]) ; ci += sizeof(VarIdx) ;
						res += '{' ;
						switch (k) {
							case VarCmd::Stem   : res += rd.stems                        [i].first ; break ;
							case VarCmd::Target : res += rd.targets                      [i].first ; break ;
							case VarCmd::Dep    : res += rd.deps_attrs.spec.deps         [i].first ; break ;
							case VarCmd::Rsrc   : res += rd.submit_rsrcs_attrs.spec.rsrcs[i].first ; break ;
							default : FAIL(k) ;
						}
						res += '}' ;
					} break ;
					case '{' : res += "{{" ; break ;
					case '}' : res += "}}" ; break ;
					default  : res += c    ; break ;
				}
			}
			return res ;
	}
	static ::string _pretty_targets( RuleData const& rd , size_t i , ::vmap_s<RuleData::TargetEntry> const& targets ) {
		OStringStream res      ;
		size_t        wk       = 0 ;
		size_t        wt       = 0 ;
		::umap_ss     patterns ;
		//
		for( auto const& [k,te] : targets ) {
			::string p = _subst_target( te.pattern ,
				[&](VarIdx t)->::string { return to_string( '{' , rd.stems[t].first , t<rd.n_static_stems?"":"*" , '}' ) ; }
			) ;
			wk          = ::max(wk,k.size()) ;
			wt          = ::max(wt,p.size()) ;
			patterns[k] = ::move(p)          ;
		}
		//
		res << indent("targets :\n",i) ;
		//
		for( auto const& [k,te] : targets ) {
			Tflags        dflt_flags = DfltTflags ;                            // flags in effect if no special user info
			OStringStream flags      ;
			if (!te.tflags[Tflag::Match]) dflt_flags |= Tflag::Dep ;
			//
			bool first = true ;
			for( Tflag f=Tflag::RuleMin ; f<Tflag::RuleMax1 ; f++ ) {
				if (te.tflags[f]==dflt_flags[f]) continue ;
				//
				if (first) { flags <<" : " ; first = false ; }
				else       { flags <<" , " ;                 }
				//
				if (!te.tflags[f]) flags << '-'         ;
				/**/               flags << mk_snake(f) ;
			}
			bool first_conflict = true ;
			for( VarIdx c : te.conflicts ) {
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
			if (!first_conflict) flags << ']' ;
			res << indent(to_string(::setw(wk),k," : "),i+1) ;
			::string flags_str = ::move(flags).str() ;
			if (+flags_str) res << ::setw(wt)<<patterns[k] << flags_str ;
			else            res <<             patterns[k]              ;
			res <<'\n' ;
		}
		//
		res << indent("target_patterns :\n",i) ;
		for( size_t t=0 ; t<targets.size() ; t++ ) res << indent(to_string(::setw(wk),rd.targets[t].first," : ",rd.target_patterns[t].txt,'\n'),i+1) ;
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
		for( auto const& [k,te] : rd.targets )
			if (rd.job_name==te.pattern) return to_string("<targets.",k,'>') ;
		return rd.job_name ;
	}

	static ::string _pretty( size_t i , DepsAttrs const& ms , RuleData const& rd ) {
		OStringStream res      ;
		size_t        wk       = 0 ;
		size_t        wd       = 0 ;
		::umap_ss     patterns ;
		//
		for( auto const& [k,ds] : ms.deps ) {
			if (!ds.pattern) continue ;
			::string p = _pretty_fstr(ds.pattern,rd) ;
			wk          = ::max(wk,k.size()) ;
			wd          = ::max(wd,p.size()) ;
			patterns[k] = ::move(p)          ;
		}
		for( auto const& [k,ds] : ms.deps ) {
			if (!ds.pattern) continue ;
			::string flags ;
			bool     first = true ;
			for( Dflag f=Dflag::RuleMin ; f<Dflag::RuleMax1 ; f++ ) {
				if (ds.dflags[f]==StaticDflags[f]) continue ;
				//
				if (first) { flags += " : " ; first = false ; }
				else       { flags += " , " ;                 }
				//
				if (!ds.dflags[f]) flags += '-'         ;
				/**/               flags += mk_snake(f) ;
			}
			/**/        res << ::string(i,'\t') << ::setw(wk)<<k <<" : " ;
			if (+flags) res << ::setw(wd)<<patterns[k] << flags ;
			else        res <<             patterns[k]          ;
			/**/        res <<'\n' ;
		}
		return res.str() ;
	}
	static ::string _pretty( size_t i , CreateNoneAttrs const& sna ) {
		if (sna.tokens1) return to_string(::string(i,'\t'),"job_tokens : ",sna.tokens1+1,'\n') ;
		else             return {}                                                             ;
	}
	static ::string _pretty( size_t i , CacheNoneAttrs const& cna ) {
		if (+cna.key) return to_string(::string(i,'\t'),"key : ",cna.key,'\n') ;
		else          return {}                                                ;
	}
	static ::string _pretty( size_t i , SubmitRsrcsAttrs const& sra ) {
		::vmap_ss entries ;
		/**/                                 if (sra.backend!=BackendTag::Local) entries.emplace_back( "<backend>" , mk_snake (sra.backend) ) ;
		for (auto const& [k,v] : sra.rsrcs ) if (+v                            ) entries.emplace_back( k           , v                    ) ;
		return _pretty_vmap(i,entries) ;
	}
	static ::string _pretty( size_t i , SubmitNoneAttrs const& sna ) {
		::vmap_ss entries ;
		if (sna.n_retries!=0) entries.emplace_back( "n_retries" , mk_snake (sna.n_retries) ) ;
		return _pretty_vmap(i,entries) ;
	}
	static ::string _pretty_env( size_t i , ::vmap_ss const& m ) {
		OStringStream res ;
		size_t        wk  = 0 ;
		//
		for( auto const& [k,v] : m ) wk = ::max(wk,k.size()) ;
		for( auto const& [k,v] : m ) {
			res << ::setw(wk)<<k ;
			if      (v==EnvPassMrkr) res<<"   ..."                     ;
			else if (v==EnvDynMrkr ) res<<"   <dynamic>"               ;
			else if (+v            ) res<<" : "<<env_decode(::copy(v)) ;
			else                     res<<" :"                         ;
			res << '\n' ;
		}
		//
		return indent(res.str(),i) ;
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
		for( pass=1 ; pass<=2 ; pass++ ) {                                                         // on 1st pass we compute key size, on 2nd pass we do the job
			if ( sca.auto_mkdir ) do_field( "auto_mkdir"  , to_string(sca.auto_mkdir ) ) ;
			if ( sca.ignore_stat) do_field( "ignore_stat" , to_string(sca.ignore_stat) ) ;
			/**/                  do_field( "autodep"     , mk_snake (sca.method     ) ) ;
			if (+sca.chroot     ) do_field( "chroot"      ,           sca.chroot       ) ;
			if (+sca.tmp        ) do_field( "tmp"         ,           sca.tmp          ) ;
			if ( sca.use_script ) do_field( "use_script"  , to_string(sca.use_script ) ) ;
		}
		if (+sca.env) {
			res << indent("environ :\n",i) << _pretty_env( i+1 , sca.env ) ;
		}
		return res.str() ;
	}
	static ::string _pretty( size_t i , Cmd const& c , RuleData const& rd ) {
		if (!c.cmd      ) return {}                                          ;
		if (rd.is_python) return indent(ensure_nl(             c.cmd    ),i) ;
		else              return indent(ensure_nl(_pretty_fstr(c.cmd,rd)),i) ;
	}
	static ::string _pretty( size_t i , StartRsrcsAttrs const& sra ) {
		OStringStream res     ;
		::vmap_ss     entries ;
		if (+sra.timeout) entries.emplace_back( "timeout" , sra.timeout.short_str() ) ;
		/**/              res << _pretty_vmap(i,entries)                                 ;
		if (+sra.env    ) res << indent("environ :\n",i) << _pretty_env( i+1 , sra.env ) ;
		return res.str() ;
	}
	static ::string _pretty( size_t i , StartNoneAttrs const& sna , RuleData const& rd ) {
		OStringStream res     ;
		::vmap_ss     entries ;
		if ( sna.keep_tmp   ) entries.emplace_back( "keep_tmp"    , to_string   (sna.keep_tmp   )            ) ;
		if (+sna.start_delay) entries.emplace_back( "start_delay" ,              sna.start_delay.short_str() ) ;
		if (+sna.kill_sigs  ) entries.emplace_back( "kill_sigs"   , _pretty_sigs(sna.kill_sigs  )            ) ;
		/**/              res << _pretty_vmap(i,entries)                                         ;
		if (+sna.env    ) res << indent("environ :\n"   ,i) << _pretty_env ( i+1 , sna.env     ) ;
		if (+rd.dbg_info) res << indent("debug info :\n",i) << _pretty_vmap( i+1 , rd.dbg_info ) ;
		return res.str() ;
	}
	static ::string _pretty( size_t i , EndCmdAttrs const& eca ) {
		::vmap_ss entries ;
		if  (eca.allow_stderr) entries.emplace_back( "allow_stderr" , to_string(eca.allow_stderr) ) ;
		return _pretty_vmap(i,entries) ;
	}
	static ::string _pretty( size_t i , EndNoneAttrs const& ena ) {
		::vmap_ss entries ;
		if  (ena.max_stderr_len!=size_t(-1)) entries.emplace_back( "max_stderr_len" , to_string(ena.max_stderr_len) ) ;
		return _pretty_vmap(i,entries) ;
	}

	template<class T,class... A> ::string RuleData::_pretty_str( size_t i , Dynamic<T> const& d , A&&... args ) const {
		::string s = _pretty( i+1 , d.spec , ::forward<A>(args)... ) ;
		if ( !s && !d.code_str ) return {} ;
		//
		::string res ;
		/**/                                        append_to_string( res , indent(to_string(T::Msg," :\n"),i  )                                     ) ;
		if (+d.glbs_str)                            append_to_string( res , indent("<dynamic globals> :\n" ,i+1) , ensure_nl(indent(d.glbs_str,i+2)) ) ;
		if (+d.ctx     )                            append_to_string( res , indent("<context> :"           ,i+1)                                     ) ;
		for( ::string const& k : _list_ctx(d.ctx) ) append_to_string( res , ' ',k                                                                    ) ;
		if (+d.ctx     )                            append_to_string( res , '\n'                                                                     ) ;
		if (+s         )                            append_to_string( res , s                                                                        ) ;
		if (+d.code_str)                            append_to_string( res , indent("<dynamic code> :\n",i+1)     , ensure_nl(indent(d.code_str,i+2)) ) ;
		return res ;
	}

	::string RuleData::pretty_str() const {
		OStringStream res     ;
		::vmap_ss     entries ;
		//
		res << name << " :" ;
		switch (special) {
			case Special::Anti       : res <<" AntiRule"   ; break ;
			case Special::GenericSrc : res <<" SourceRule" ; break ;
			default : ;
		}
		res << '\n' ;
		if (prio  ) entries.emplace_back( "prio"     , to_string(prio)                ) ;
		/**/        entries.emplace_back( "job_name" , _pretty_job_name(*this)        ) ;
		if (+cwd_s) entries.emplace_back( "cwd"      , cwd_s.substr(0,cwd_s.size()-1) ) ;
		if (!is_special()) {
			::string i ; for( ::string const& c : interpreter ) append_to_string( i , +i?" ":"" , c ) ;
			//
			if (force        ) entries.emplace_back( "force"       , to_string(force                         ) ) ;
			if (+n_tokens_key) entries.emplace_back( "n_tokens"    , to_string(n_tokens_key," (",n_tokens,')') ) ;
			/**/               entries.emplace_back( "interpreter" , i                                         ) ;
		}
		res << _pretty_vmap(1,entries) ;
		if (+stems) res << indent("stems :\n",1) << _pretty_vmap   (      2,stems,true/*uniq*/) ;
		/**/        res <<                          _pretty_targets(*this,1,targets           ) ;
		if (!is_special()) {
			res << _pretty_str(1,deps_attrs        ,*this) ;
			res << _pretty_str(1,create_none_attrs       ) ;
			res << _pretty_str(1,cache_none_attrs        ) ;
			res << _pretty_str(1,submit_rsrcs_attrs      ) ;
			res << _pretty_str(1,submit_none_attrs       ) ;
			res << _pretty_str(1,start_none_attrs  ,*this) ;
			res << _pretty_str(1,start_cmd_attrs         ) ;
			res << _pretty_str(1,cmd               ,*this) ;
			res << _pretty_str(1,start_rsrcs_attrs       ) ;
			res << _pretty_str(1,end_cmd_attrs           ) ;
			res << _pretty_str(1,end_none_attrs          ) ;
		}
		//
		return res.str() ;
	}

	::vector_s RuleData::_list_ctx(::vector<CmdIdx> const& ctx) const {
		::vector_s res ;
		for( auto [k,i] : ctx ) switch (k) {
			case VarCmd::Stem    : res.push_back(stems                        [i].first) ; break ;
			case VarCmd::Target  : res.push_back(targets                      [i].first) ; break ;
			case VarCmd::Dep     : res.push_back(deps_attrs.spec.deps         [i].first) ; break ;
			case VarCmd::Rsrc    : res.push_back(submit_rsrcs_attrs.spec.rsrcs[i].first) ; break ;
			case VarCmd::Stems   : res.push_back("stems"                               ) ; break ;
			case VarCmd::Targets : res.push_back("targets"                             ) ; break ;
			case VarCmd::Deps    : res.push_back("deps"                                ) ; break ;
			case VarCmd::Rsrcs   : res.push_back("resources"                           ) ; break ;
			default : FAIL(k) ;
		}
		return res ;
	}

	// match_crc is an id of the rule : a new rule is a replacement of an old rule if it has the same match_crc
	// also, 2 rules matching identically is forbidden : the idea is that one is useless
	// this is not strictly true, though : you could imagine a rule generating a* from b, another generating a* from b but with disjoint sets of a
	// although awkward & useless (as both rules could be merged), this can be meaningful
	// if the need arises, we will add an "id" artificial field entering in match_crc to distinguish them
	void RuleData::_set_crcs() {
		bool special = is_special() ;
		{	::vmap_s<Tflags> targets_ ;
			static constexpr Tflags MatchFlags{ Tflag::Star , Tflag::Match , Tflag::Dep } ; // only these flags are important for matching, others are for execution only
			for( auto const& [k,te] : targets )
				if (te.tflags[Tflag::Match])
					targets_.emplace_back(te.pattern,te.tflags&MatchFlags) ;   // keys and other flags have no influence on matching
			Hash::Xxh h ;
			/**/          h.update(special   ) ;
			/**/          h.update(stems     ) ;
			/**/          h.update(cwd_s     ) ;
			if (!special) h.update(job_name  ) ;                               // job_name has no effect for source & anti as it is only used to store jobs and there are none
			/**/          h.update(targets_  ) ;
			/**/          h.update(allow_ext ) ;
			if (!special) h.update(deps_attrs) ;                               // no deps for source & anti
			//
			if ( !special && _qualify_dep({},interpreter[0]) ) h.update(interpreter[0]) ; // no interpreter for source & anti
			//
			match_crc = ::move(h).digest() ;
		}
		if (special) return ;                                                  // source & anti are only capable of matching
		{	Hash::Xxh h ;                                                      // cmd_crc is stand-alone : it guarantee rule uniqueness (i.e. contains match_crc)
			h.update(stems          ) ;
			h.update(job_name       ) ;
			h.update(targets        ) ;
			h.update(force          ) ;
			h.update(is_python      ) ;
			h.update(interpreter    ) ;
			h.update(deps_attrs     ) ;
			h.update(start_cmd_attrs) ;
			h.update(cmd            ) ;
			h.update(end_cmd_attrs  ) ;
			cmd_crc = ::move(h).digest() ;
		}
		{	Hash::Xxh h ;
			h.update(submit_rsrcs_attrs) ;
			h.update(start_rsrcs_attrs ) ;
			h.update(targets           ) ;                                     // all is not necessary, but simpler to code
			rsrcs_crc = ::move(h).digest() ;
		}
	}

	//
	// Rule::SimpleMatch
	//

	Rule::SimpleMatch::SimpleMatch(Job job) : rule{job->rule} {
		::string name_ = job->full_name() ;
		//
		SWEAR( Rule(name_)==rule , mk_printable(name_) , rule->name ) ;        // only name suffix is considered to make Rule
		//
		char* p = &name_[name_.size()-( rule->n_static_stems*(sizeof(FileNameIdx)*2) + sizeof(Idx) )] ; // start of suffix
		for( VarIdx s=0 ; s<rule->n_static_stems ; s++ ) {
			FileNameIdx pos = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			FileNameIdx sz  = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			stems.push_back(name_.substr(pos,sz)) ;
		}
	}

	Rule::SimpleMatch::SimpleMatch( Rule r , TargetPattern const& pattern , ::string const& name ) {
		Trace trace("SimpleMatch",r,name) ;
		Match m = pattern.match(name) ;
		if (!m) { trace("no_match") ; return ; }
		rule = r ;
		for( VarIdx s=0 ; s<r->n_static_stems ; s++ ) stems.push_back(::string(m[pattern.groups[s]])) ;
		trace("stems",stems) ;
	}

	Rule::SimpleMatch::SimpleMatch( RuleTgt rt , ::string const& target ) : SimpleMatch{rt,rt.pattern(),target} {
		if (!*this) return ;
		for( VarIdx t : rt->targets[rt.tgt_idx].second.conflicts ) {
			if (!rt->target_patterns[t].match(target)) continue ;
			rule .clear() ;
			stems.clear() ;
			Trace("SimpleMatch","conflict",rt.tgt_idx,t) ;
			return ;
		}
	}

	::ostream& operator<<( ::ostream& os , Rule::SimpleMatch const& m ) {
		os << "RSM(" << m.rule << ',' << m.stems << ')' ;
		return os ;
	}

	::uset<Node> Rule::SimpleMatch::target_dirs() const {
		::uset<Node> dirs ;
		for( auto const& [k,te] : rule->targets ) {
			::string target = _subst_target(
				te.pattern
			,	[&](VarIdx s)->::string { return stems[s] ; }
			,	false/*escape*/
			,	rule->n_static_stems/*stop_above*/
			) ;
			size_t sep = target.rfind('/') ;
			if (sep!=Npos) dirs.insert(Node(target.substr(0,sep))) ;
		}
		return dirs ;
	}

	::vector_s Rule::SimpleMatch::star_patterns() const {
		::vector_s res ;
		for( VarIdx t=rule->n_static_targets ; t<rule->targets.size() ; t++ ) {
			SWEAR(rule->tflags(t)[Tflag::Star]) ;
			::uset<VarIdx> seen ;
			res.push_back(_subst_target(
				rule->targets[t].second.pattern
			,	[&](VarIdx s)->::string {
					if (s<rule->n_static_stems) return escape(stems[s]) ;
					if (seen.insert(s).second ) return to_string('('    ,rule->stems[s].second             ,')') ;
					else                        return to_string("(?:\\",rule->target_patterns[t].groups[s],')') ; // we must protect against following text potentially containing numbers
				}
			,	true/*escape*/
			)) ;
		}
		return res ;
	}

	::vector_s Rule::SimpleMatch::py_targets() const {
		::vector_s res = static_targets() ;
		for( VarIdx t=rule->n_static_targets ; t<rule->targets.size() ; t++ ) {
			SWEAR(rule->tflags(t)[Tflag::Star]) ;
			::uset<VarIdx> seen ;
			res.push_back(_subst_target(
				rule->targets[t].second.pattern
			,	[&](VarIdx s)->::string {
					if (s<rule->n_static_stems) return escape(stems[s]) ;
					if (seen.insert(s).second ) return to_string("(?P<",rule->stems[s].first,'>',rule->stems[s].second,')') ;
					else                        return to_string("(?P=",rule->stems[s].first,                          ')') ;
				}
			,	true/*escape*/
			)) ;
		}
		return res ;
	}

	::vector_s Rule::SimpleMatch::static_targets() const {
		::vector_s res ;
		for( VarIdx t=0 ; t<rule->n_static_targets ; t++ ) {
			SWEAR(!rule->tflags(t)[Tflag::Star]) ;
			res.push_back(_subst_target(
				rule->targets[t].second.pattern
			,	[&](VarIdx s)->::string {
					SWEAR(s<rule->n_static_stems) ;
					return stems[s] ;
				}
			)) ;
		}
		return res ;
	}

	::pair_ss Rule::SimpleMatch::full_name() const {
		::vector<FileNameIdx> poss(rule->n_static_stems) ;
		::string name = _subst_target( rule->job_name ,
			[&]( FileNameIdx p , VarIdx s ) -> ::string {
				if (s<rule->n_static_stems) {
					poss[s] = p ;
					return stems[s] ;
				}
				::string const& key = rule->stems[s].first ;
				if ( key.front()=='<' && key.back()=='>' ) return "{*}"                   ;
				else                                       return to_string('{',key,"*}") ;
			}
		) ;
		::string sfx = rule.job_sfx() ;                                        // provides room for stems, but we have to fill it
		size_t   i   = 1              ;                                        // skip initial JobMrkr
		for( VarIdx s=0 ; s<rule->n_static_stems ; s++ ) {
			encode_int<FileNameIdx>( &sfx[i] , poss [s]        ) ; i+= sizeof(FileNameIdx) ;
			encode_int<FileNameIdx>( &sfx[i] , stems[s].size() ) ; i+= sizeof(FileNameIdx) ; // /!\ beware of selecting encode_int of the right size
		}
		return {name,sfx} ;
	}

	VarIdx Rule::SimpleMatch::idx(::string const& target) const {
		if (!_has_static_targets) {
			::vector_s sts = static_targets() ;
			for( VarIdx t=0 ; t<rule->n_static_targets ; t++ ) _static_targets.emplace(sts[t],t) ;
			_has_static_targets = true ;
		}
		auto it = _static_targets.find(target) ;
		if (it==_static_targets.end()) return star_idx(target) ;
		else                           return it->second       ;
	}

	VarIdx Rule::SimpleMatch::star_idx(::string const& target) const {
Trace trace("star_idx",*this,target) ;
		for( VarIdx t=rule->n_static_targets ; t<rule->targets.size() ; t++ ) {
trace(t);
			Match m = rule->target_patterns[t].match(target) ;
			if (!m) continue ;
trace(rule->n_static_stems,rule->target_patterns[t].groups,stems) ;
			for( VarIdx i=0 ; i<rule->n_static_stems ; i++ ) if (m[rule->target_patterns[t].groups[i]]!=stems[i]) goto Continue ;
			return t ;
		Continue : ;
		}
		return NoVar ;
	}

}
