// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "serialize.hh"

#include "core.hh"

ENUM( DepKind
,	Dep
,	Python
,	Shell
)

ENUM( StarAction
,	None
,	Stop
,	Err
)

namespace Engine {

	using namespace Disk ;
	using namespace Py   ;
	using namespace Re   ;
	using namespace Time ;

	static const ::string _g_specials = "()[]{}?*+-|^$\\.&~# \t\n\r\v\f" ;
	static ::string escape(::string const& s) {
		::string res ; res.reserve(s.size()+(s.size()>>4)) ;     // take a little margin for escapes
		for( char c : s ) {
			if (_g_specials.find(c)!=Npos) res.push_back('\\') ; // escape specials
			res.push_back(c) ;
		}
		return res ;
	}

	using ParsePyFuncFixed = ::function<void  ( string const& fixed                                             )> ;
	using ParsePyFuncStem  = ::function<void  ( string const& key , bool star , bool unnamed , string const* re )> ;
	using SubstTargetFunc  = ::function<string( FileNameIdx pos , VarIdx stem                                   )> ;
	using ParseTargetFunc  = ::function<void  ( FileNameIdx pos , VarIdx stem                                   )> ;

	// str has the same syntax as Python f-strings
	// cb_fixed is called on each fixed part found
	// cb_stem  is called on each stem       found
	// stems are of the form {<identifier>\*?} or {<identifier>?\*?:.*} (where .* after : must have matching {})
	// cb_stem is called with :
	// - the <identifier>
	// - true if <identifier> is followed by a *
	// - the regular expression that follows the : or nullptr for the first case
	// /!\ : this function is also implemented in read_makefiles.py:add_stems, both must stay in sync
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
					fixed.push_back(c) ;                             // }} are transformed into }
				break ;
				case SeenStart :
					if (c=='{') {
						state = Literal ;
						fixed.push_back(c) ;                         // {{ are transformed into {
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
			case Literal   : cb_fixed(fixed) ; break ;               // trailing fixed
			case SeenStop  : throw to_string("spurious } in ",str) ;
			case SeenStart :
			case Key       :
			case Re        : throw to_string("spurious { in ",str) ;
		DF}
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

	template<class F,class EF> static void _mk_flags( ::string const& key , Sequence const& py_seq , uint8_t n_skip , BitMap<F>& flags , BitMap<EF>& extra_flags , EF n_extra_flags ) {
		for( auto item : py_seq ) {
			if (n_skip>0) { n_skip-- ; continue ; }
			if (item->is_a<Str>()) {
				::string flag_str = item->as_a<Str>() ;
				bool     neg      = flag_str[0]=='-'  ;
				if (neg) flag_str = flag_str.substr(1) ; // suppress initial - sign
				//
				if      ( F  f  ; can_mk_enum<F >(flag_str) && (f =mk_enum<F >(flag_str),f <F::NRule     ) ) flags      .set(f ,!neg) ;
				else if ( EF ef ; can_mk_enum<EF>(flag_str) && (ef=mk_enum<EF>(flag_str),ef<n_extra_flags) ) extra_flags.set(ef,!neg) ;
				else                                                                                         throw to_string("unexpected flag ",flag_str," for ",key) ;
			} else if (item->is_a<Sequence>()) {
				_mk_flags( key , item->as_a<Sequence>() , 0 , flags , extra_flags , n_extra_flags ) ;
			} else {
				throw to_string(key,"has a flag that is not a str") ;
			}
		}
	}
	template<class F,class EF> static ::string _split_flags( ::string const& key , Object const& py , uint8_t n_skip , BitMap<F>& flags , BitMap<EF>& extra_flags , EF n_extra_flags=All<EF> ) {
		if (py.is_a<Str>()) return py.as_a<Str>() ;
		Sequence const& py_seq = py.as_a<Sequence>() ;
		SWEAR(py_seq.size()>=n_skip          ,key) ;
		SWEAR(py_seq[0].is_a<Str>(),key) ;
		_mk_flags( key , py_seq , n_skip , flags , extra_flags , n_extra_flags ) ;
		return py_seq[0].as_a<Str>() ;
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

		bool/*updated*/ acquire( bool& dst , Object const* py_src ) {
			if (!py_src      ) {           return false ;                              }
			if (*py_src==None) { if (!dst) return false ; dst = false ; return true  ; }
			//
			dst = +*py_src ;
			return true ;
		}

		bool/*updated*/ acquire( Delay& dst , Object const* py_src , Delay min , Delay max ) {
			if (!py_src      ) {           return false ;                           }
			if (*py_src==None) { if (!dst) return false ; dst = {} ; return true  ; }
			//
			double d = 0 ;
			if      (py_src->is_a<Float>()) d =             py_src->as_a<Float>()  ;
			else if (py_src->is_a<Str  >()) d = *Ptr<Float>(py_src->as_a<Str  >()) ;
			else                            throw "cannot convert to float"s ;
			dst = Delay(d) ;
			if (dst<min) throw "underflow"s ;
			if (dst>max) throw "overflow"s  ;
			return true ;
		}

		bool/*updated*/ acquire( DbgEntry& dst , Object const* py_src ) {
			if (!py_src      ) {                          return false ;                           }
			if (*py_src==None) { if (!dst.first_line_no1) return false ; dst = {} ; return true  ; }
			//
			Sequence const& py_seq = py_src->as_a<Sequence>() ;
			acquire(dst.module        ,&py_seq[0].as_a<Str>()                 ) ;
			acquire(dst.qual_name     ,&py_seq[1].as_a<Str>()                 ) ;
			acquire(dst.filename      ,&py_seq[2].as_a<Str>()                 ) ;
			acquire(dst.first_line_no1,&py_seq[3].as_a<Int>(),size_t(1)/*min*/) ;
			return true ;
		}

		::string subst_fstr( ::string const& fstr , ::umap_s<CmdIdx> const& var_idxs , VarIdx& n_unnamed ) {
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

	static bool/*keep*/ _qualify_dep( ::string const& key , ::string const& dep , DepKind kind , ::string const& dep_for_msg={} ) {
		::string dir_s          = dep.substr(0,dep.find(Rule::StemMrkr))          ;
		size_t   dir_pos        = dir_s.rfind('/')                                ;
		/**/     dir_s          = dir_pos==Npos ? ""s : dir_s.substr(0,dir_pos+1) ;
		//
		auto bad = [&] [[noreturn]] (::string const& msg) {
			::string const& d = +dep_for_msg ? dep_for_msg : dep ;
			if (kind==DepKind::Dep) throw to_string("dep ",key ," (",d,") ",msg) ;
			else                    throw to_string(snake(kind)," (",d,") ",msg) ;
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
		if (kind!=DepKind::Dep) return false/*keep*/ ;                                                              // normal case : interpreter is outside repo typically system python or bash
		bad("outside repository and all source dirs must be suppressed") ;
	}
	void DepsAttrs::init( bool /*is_dynamic*/ , Dict const* py_src , ::umap_s<CmdIdx> const& var_idxs , RuleData const& rd ) {
		full_dynamic = false ;                                                                                                                           // if full dynamic, we are not initialized
		//
		for( auto [py_key,py_val] : py_src->as_a<Dict>() ) {
			::string key = py_key->template as_a<Str>() ;
			if (*py_val==None) {
				deps.emplace_back(key,DepSpec()) ;
				continue ;
			}
			VarIdx      n_unnamed  = 0                                                                                ;
			Dflags      df         { Dflag::Essential , Dflag::Static }                                               ;
			ExtraDflags edf        ;
			::string    dep        = _split_flags( "dep "+key , *py_val , 1/*n_skip*/ , df , edf , ExtraDflag::NDep ) ; SWEAR(!(edf&~ExtraDflag::Top)) ; // or we must review dep_flags in DepSpec
			::string    parsed_dep = Attrs::subst_fstr( dep , var_idxs , n_unnamed )                                  ;
			//
			rd.add_cwd( parsed_dep , edf[ExtraDflag::Top] ) ;
			_qualify_dep( key , parsed_dep , DepKind::Dep , dep ) ;
			//
			if (n_unnamed) {
				for( auto const& [k,ci] : var_idxs ) if (ci.bucket==VarCmd::Stem) n_unnamed-- ;
				if (n_unnamed) throw to_string("dep ",key," (",dep,") ","contains some but not all unnamed static stems") ;
			}
			deps.emplace_back( key , DepSpec( ::move(parsed_dep) , df ) ) ;
		}
		if (_qualify_dep( {} , rd.interpreter[0] , rd.is_python?DepKind::Python:DepKind::Shell ))
			deps.emplace_back( "<interpreter>" , DepSpec(::copy(rd.interpreter[0]),Dflag::Static) ) ;
		if (deps.size()>=Rule::NoVar) throw to_string("too many static deps : ",deps.size()) ;
	}

	::vmap_s<pair_s<AccDflags>> DynamicDepsAttrs::eval( Rule::SimpleMatch const& match ) const {
		::vmap_s<pair_s<AccDflags>> res ;
		for( auto const& [k,ds] : spec.deps ) res.emplace_back( k , pair_s( parse_fstr(ds.pattern,match) , AccDflags({},ds.dflags) ) ) ;
		//
		if (is_dynamic) {
			try {
				Gil         gil    ;
				Ptr<Object> py_obj = _eval_code(match) ;
				//
				::map_s<VarIdx> dep_idxs ;
				for( VarIdx di=0 ; di<spec.deps.size() ; di++ ) dep_idxs[spec.deps[di].first] = di ;
				for( auto [py_key,py_val] : py_obj->as_a<Dict>() ) {
					if (*py_val==None) continue ;
					::string key = py_key->as_a<Str>() ;
					Dflags      df  { Dflag::Essential , Dflag::Static }                                               ;
					ExtraDflags edf ;
					::string    dep = _split_flags( "dep "+key , *py_val , 1/*n_skip*/ , df , edf , ExtraDflag::NDep ) ; SWEAR(!(edf&~ExtraDflag::Top)) ; // or we must review dep_flags in AccDflags
					match.rule->add_cwd( dep , edf[ExtraDflag::Top] ) ;
					_qualify_dep( key , dep , DepKind::Dep ) ;
					::pair_s<AccDflags> e { dep , {{},df} } ;
					if (spec.full_dynamic) { SWEAR(!dep_idxs.contains(key),key) ; res.emplace_back(key,e) ;          } // dep cannot be both static and dynamic
					else                                                          res[dep_idxs.at(key)].second = e ;   // if not full_dynamic, all deps must be listed in spec
				}
			} catch (::string const& e) { throw ::pair_ss(e/*msg*/,{}/*err*/) ; }
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
			StdRsrc  r   = mk_enum<StdRsrc>(k) ; if (k!=snake(r)             ) continue ; // .
			uint64_t val = 0 /*garbage*/       ;
			try                     { val = from_string_with_units<uint64_t>(v) ; }
			catch (::string const&) { continue ;                                  }       // value is not recognized
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

	void Cmd::init( bool /*is_dynamic*/ , Dict const* py_src , ::umap_s<CmdIdx> const& var_idxs , RuleData const& rd ) {
		::string raw_cmd ;
		Attrs::acquire_from_dct( raw_cmd   , *py_src , "cmd"       ) ;
		Attrs::acquire_from_dct( decorator , *py_src , "decorator" ) ;
		if (rd.is_python) {
			cmd = ::move(raw_cmd) ;
		} else {
			VarIdx n_unnamed = 0 ;
			cmd = Attrs::subst_fstr(raw_cmd,var_idxs,n_unnamed) ;
			SWEAR( !n_unnamed , n_unnamed ) ;
		}
	}

	pair_ss/*script,call*/ DynamicCmd::eval( Rule::SimpleMatch const& match , ::vmap_ss const& rsrcs , ::vmap_s<Accesses>* deps ) const {
		Rule r = match.rule ; // if we have no job, we must have a match as job is there to lazy evaluate match if necessary
		if (!r->is_python) {
			if (!is_dynamic) return {parse_fstr(spec.cmd,match,rsrcs),{}} ;
			Gil         gil    ;
			Ptr<Object> py_obj = _eval_code( match , rsrcs , deps ) ;
			::string    cmd    ;
			Attrs::acquire( cmd , &py_obj->as_a<Str>() ) ;
			return {cmd,{}} ;
		}
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
		eval_ctx( match , rsrcs
		,	[&]( VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) -> void {
				if ( vc!=VarCmd::Match || i<r->n_statics ) {
					append_to_string(res,key," = ",mk_py_str(val),'\n') ;
					return ;
				}
				::vector_s args ;
				::string expr = _subst_target(
					r->matches[i].second.pattern
				,	[&](VarIdx s)->::string {
						::string k = r->stems[s].first ;
						if ( k.front()=='<' and k.back()=='>' ) k = k.substr(1,k.size()-2) ;
						if ( s>=r->n_static_stems             ) { args.push_back(k) ; return to_string('{',k,'}') ; }
						else                                                          return match.stems[s]       ;
					}
				) ;
				append_to_string(res,"def ",key,'(') ;
				const char* sep = "" ;
				for( ::string const& a : args ) {
					append_to_string(res,sep,' ' ,a,' ') ;
					sep = "," ;
				}
				append_to_string(res,") : return f"   ,mk_py_str(expr),'\n') ;
				append_to_string(res,key,".regexpr = ",mk_py_str(val ),'\n') ;
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
		special = s        ;
		name    = snake(s) ;
		switch (s) {
			case Special::Req      : force = true      ; break ;
			case Special::Infinite : prio  = -Infinity ; break ;                               // -inf : it can appear after other rules
			case Special::GenericSrc :
				name                          = "source dir" ;
				job_name                      = src_dir_s    ; _append_stem(job_name,0) ;
				force                         = true         ;
				n_static_stems                = 1            ;
				n_static_targets              = 1            ;
				n_statics                     = 1            ;
				allow_ext                     = true         ;                                 // sources may lie outside repo
				stems           .emplace_back("",".*"                ) ;
				stem_mark_counts.push_back   (0                      ) ;
				matches         .emplace_back("",MatchEntry(job_name)) ;
				_compile() ;
			break ;
		DF}
	}

	::ostream& operator<<( ::ostream& os , RuleData const& rd ) {
		return os << "RD(" << rd.name << ')' ;
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
				FileNameIdx ia        = i  ; if (!is_prefix) ia   = a.size()-1-ia                    ; // current index
				FileNameIdx ib        = i  ; if (!is_prefix) ib   = b.size()-1-ib                    ; // .
				FileNameIdx iae       = ia ; if ( is_prefix) iae += sizeof(VarIdx)                   ; // last char of stem if it is one (cannot subtract sizeof(VarIdx) as FileNameIdx is unsigned)
				FileNameIdx ibe       = ib ; if ( is_prefix) ibe += sizeof(VarIdx)                   ; // .
				bool        a_is_stem = iae>=sizeof(VarIdx) && a[iae-sizeof(VarIdx)]==Rule::StemMrkr ;
				bool        b_is_stem = ibe>=sizeof(VarIdx) && b[ibe-sizeof(VarIdx)]==Rule::StemMrkr ;
				if ( !a_is_stem && !b_is_stem ) {
					if (a[ia]==b[ib]) continue ;                                                       // same      chars, continue analysis
					else              return false ;                                                   // different chars, no conflict possible
				}
				if ( a_is_stem && b_is_stem ) {
					::string_view sa = ::string_view(a).substr(iae+1-sizeof(VarIdx),sizeof(VarIdx)) ;
					::string_view sb = ::string_view(b).substr(ibe+1-sizeof(VarIdx),sizeof(VarIdx)) ;
					if (sa==sb) { i+=sizeof(VarIdx) ; continue ; }                                     // same      stems, continue analysis
					else        { goto Continue ;                }                                     // different stems, could have identical values
				}
				goto Continue ;                              // one is a stem, not the other, the stem value can match the fixed part of the other, may conflict
			}
			if ( sz == (sz_a>sz_b?b.size():a.size()) ) {     // if shortest is a prefix of longest, analyse remaining of longest to see if we are certain it is non-empty
				::string const& l = sz_a>sz_b ? a : b ;      // longest
				for( FileNameIdx i=sz ; i<l.size() ; i++ ) {
					FileNameIdx j       = i ; if (!is_prefix) j  = l.size()-1-j                      ; // current index
					FileNameIdx je      = j ; if ( is_prefix) j += sizeof(VarIdx)                    ; // last char of stem if it is one (cannot subtract sizeof(VarIdx) as FileNameIdx is unsigned)
					bool        is_stem = je>=sizeof(VarIdx) && l[je-sizeof(VarIdx)]==Rule::StemMrkr ;
					if (is_stem) i += sizeof(VarIdx) ;                                                 // stem value can be empty, may still conflict, continue
					else         return false ;                                                        // one is a strict suffix of the other, no conflict possible
				}
			}
		Continue : ;
		}
		return true ;
	}
	void RuleData::_acquire_py(Py::Dict const& dct) {
		static ::string root_dir_s = *g_root_dir+'/' ;
		Gil      gil   ;
		::string field ;
		try {
			//
			// acquire essential (necessary for Anti & GenericSrc)
			//
			//
			field = "__special__" ;
			if (dct.contains(field)) {
				special = mk_enum<Special>(dct[field].as_a<Str>()) ;
				if (special<=Special::Shared) throw to_string("unexpected value for __special__ attribute : ",special) ;
			} else {
				special = Special::Plain ;
			}
			field = "name" ; if (dct.contains(field)) name  = dct[field].as_a<Str  >() ; else throw "not found"s ;
			field = "prio" ; if (dct.contains(field)) prio  = dct[field].as_a<Float>() ;
			field = "cwd"  ; if (dct.contains(field)) cwd_s = dct[field].as_a<Str  >() ;
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
			::map_s<Bool3> stem_stars ;                                                                // ordered so that stems are ordered, Maybe means stem is used both as static and star
			field = "stems" ;
			if (dct.contains(field))
				for( auto [k,v] : dct[field].as_a<Dict>() )
					stem_defs.emplace( ::string(k->as_a<Str>()) , ::string(v->as_a<Str>()) ) ;
			//
			// augment stems with definitions found in job_name and targets
			size_t unnamed_star_idx = 1 ;                                                                                // free running while walking over job_name + targets
			auto augment_stems = [&]( ::string const& k , bool star , ::string const* re , bool star_only ) -> void {
				if (re) {
					auto [it,inserted] = stem_defs.emplace(k,*re) ;
					if ( !inserted && *re!=it->second ) throw to_string("2 different definitions for stem ",k," : ",it->second," and ",*re) ;
				}
				if ( !star_only || star ) {
					auto [it,inserted] = stem_stars.emplace(k,No|star) ;
					if ( !inserted && (No|star)!=it->second ) it->second = Maybe ;                                       // stem is used both as static and star
				}
			} ;
			field = "job_name" ;
			if (!dct.contains(field)) throw "not found"s ;
			job_name = dct[field].as_a<Str>() ;
			_parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> void { augment_stems(k,star,re,false/*star_only*/) ; }
			) ;
			field = "matches" ;
			if (!dct.contains(field)) throw "not found"s ;
			::string job_name_key ;
			::string job_name_msg = "job_name" ;
			for( auto [py_k,py_tkfs] : dct[field].as_a<Dict>() ) {
				field = py_k->as_a<Str>() ;
				::string  target =                    py_tkfs->as_a<Sequence>()[0].as_a<Str>()  ;                        // .
				MatchKind kind   = mk_enum<MatchKind>(py_tkfs->as_a<Sequence>()[1].as_a<Str>()) ;                        // targets are a tuple (target_pattern,kind,flags...)
				// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
				if (target!=job_name) {
					_parse_py( target , &unnamed_star_idx ,
						// static stems are declared in job_name, but error will be caught later on, when we can generate a sound message
						[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> void {
							augment_stems(k,star,re,true/*star_only*/) ;
						}
					) ;
				} else if (!job_name_key) {
					job_name_key =                           field  ;
					job_name_msg = to_string(snake(kind),' ',field) ;
				}
			}
			//
			// gather job_name and targets
			field            = "job_name" ;
			unnamed_star_idx = 1          ;                                                                              // reset free running at each pass over job_name+targets
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
			field = "matches" ;
			{	::vmap_s<MatchEntry> star_matches                 ;                                                      // defer star matches so that static targets are put first
				::vmap_s<MatchEntry> static_matches[N<MatchKind>] ;                                                      // defer star matches so that static targets are put first
				bool                 seen_top                     = false ;
				bool                 seen_target                  = false ;
				for( auto [py_k,py_tkfs] : dct[field].as_a<Dict>() ) {                                                   // targets are a tuple (target_pattern,flags...)
					field = py_k->as_a<Str>() ;
					Sequence const& pyseq_tkfs         = py_tkfs->as_a<Sequence>()                     ;
					::string        target             =                    pyseq_tkfs[0].as_a<Str>()  ;                 // .
					MatchKind       kind               = mk_enum<MatchKind>(pyseq_tkfs[1].as_a<Str>()) ;                 // targets are a tuple (target_pattern,kind,flags...)
					bool            is_star            = false                                         ;
					::set_s         missing_stems      ;
					bool            is_target          = kind!=MatchKind::DepFlags                     ;
					bool            is_official_target = kind==MatchKind::Target                       ;
					bool            is_stdout          = field=="<stdout>"                             ;
					Tflags          tflags             ;
					Dflags          dflags             ;
					ExtraTflags     extra_tflags       ;
					ExtraDflags     extra_dflags       ;
					MatchFlags      flags              ;
					//
					// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
					if (target==job_name) {
						if (job_name_is_star) is_star = true ;
					} else {
						if (is_official_target) for( auto const& [k,s] : stem_stars ) if (s!=Yes) missing_stems.insert(k) ;
						_parse_py( target , &unnamed_star_idx ,
							[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) -> void {
								if (!stem_defs.contains(k)) throw to_string("found undefined ",stem_words(k,star,unnamed)," in ",snake(kind)) ;
								//
								if (star) {
									is_star = true ;
									return ;
								}
								auto it = stem_stars.find(k) ;
								if ( it==stem_stars.end() || it->second==Yes )
									throw to_string(stem_words(k,star,unnamed)," appears in ",snake(kind)," but not in ",job_name_msg,", consider using ",k,'*') ;
								if (is_official_target)
									missing_stems.erase(k) ;
							}
						) ;
					}
					if ( !is_star                       ) tflags |= Tflag::Static    ;
					if (             is_official_target ) tflags |= Tflag::Target    ;
					if ( !is_star && is_official_target ) tflags |= Tflag::Essential ;                                   // static targets are essential by default
					if ( is_target                      ) { _split_flags( snake_str(kind) , pyseq_tkfs , 2/*n_skip*/ , tflags , extra_tflags ) ; flags = {tflags,extra_tflags} ; }
					else                                  { _split_flags( snake_str(kind) , pyseq_tkfs , 2/*n_skip*/ , dflags , extra_dflags ) ; flags = {dflags,extra_dflags} ; }
					// check
					if ( target.starts_with(root_dir_s)             ) throw to_string(snake(kind)," must be relative to root dir : "         ,target) ;
					if ( !is_lcl(target)                            ) throw to_string(snake(kind)," must be local : "                        ,target) ;
					if ( !is_canon(target)                          ) throw to_string(snake(kind)," must be canonical : "                    ,target) ;
					if ( +missing_stems                             ) throw to_string("missing stems ",missing_stems," in ",snake(kind)," : ",target) ;
					if ( !is_official_target        && is_special() ) throw           "flags are meaningless for source and anti-rules"s              ;
					if ( is_star                    && is_special() ) throw to_string("star ",kind,"s are meaningless for source and anti-rules")     ;
					if ( is_star                    && is_stdout    ) throw           "stdout cannot be directed to a star target"s                   ;
					if ( tflags[Tflag::Incremental] && is_stdout    ) throw           "stdout cannot be directed to an incremental target"s           ;
					bool is_top = is_target ? extra_tflags[ExtraTflag::Top] : extra_dflags[ExtraDflag::Top] ;
					seen_top    |= is_top             ;
					seen_target |= is_official_target ;
					// record
					/**/                     add_cwd( target   , is_top ) ;
					if (field==job_name_key) add_cwd( job_name , is_top ) ;
					(is_star?star_matches:static_matches[+kind]).emplace_back( field , MatchEntry(::move(target),flags) ) ;
				}
				SWEAR(+seen_target) ;                                                                                    // we should not have come up to here without a target
				if (!job_name_key) add_cwd( job_name , seen_top ) ;
				n_static_targets = static_matches[+MatchKind::Target].size() ; static_assert(+MatchKind::Target==0) ;    // ensure offical static targets are first in matches
				for( MatchKind k : All<MatchKind> ) for( auto& st : static_matches[+k] ) matches.push_back(::move(st)) ; // put static first
				n_statics  = matches.size() ;
				/**/                                for( auto& st : star_matches       ) matches.push_back(::move(st)) ; // then star
			}
			field = "" ;
			if (matches.size()>=NoVar) throw to_string("too many targets, targets_flags and dep_flags ",matches.size()," >= ",NoVar) ;
			::umap_s<VarIdx> stem_idxs ;
			for( bool star : {false,true} ) {                                                                            // keep only useful stems and order them : static first, then star
				for( auto const& [k,v] : stem_stars ) {
					if (v==(No|!star)) continue ;                                                                        // stems that are both static and start appear twice
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
			unnamed_star_idx = 1 ;                                                                                       // reset free running at each pass over job_name+targets
			mk_tgt.clear() ;
			_parse_py( job_name , &unnamed_star_idx , mk_fixed , mk_stem ) ;
			::string new_job_name = ::move(mk_tgt) ;
			// compile potential conflicts as there are rare and rather expensive to detect, we can avoid most of the verifications by statically analyzing targets
			for( VarIdx mi=0 ; mi<matches.size() ; mi++ ) {
				MatchEntry& me = matches[mi].second ;
				// avoid processing target if it is identical to job_name
				// this is not an optimization, it is to ensure unnamed_star_idx's match
				if (me.pattern==job_name) me.pattern = new_job_name ;
				else {
					mk_tgt.clear() ;
					_parse_py( me.pattern , &unnamed_star_idx , mk_fixed , mk_stem ) ;
					me.pattern = ::move(mk_tgt) ;
				}
				for( VarIdx mi2=0 ; mi2<mi ; mi2++ )
					if ( _may_conflict( n_static_stems , me.pattern , matches[mi2].second.pattern ) ) { trace("conflict",mi,mi2) ; me.conflicts.push_back(mi2) ; }
			}
			job_name = ::move(new_job_name) ;
			//
			//vvvvvvvvvvvvvvvvvvvvvvvv
			if (is_special()) return ;                                                                                   // if special, we have no dep, no execution, we only need essential info
			//^^^^^^^^^^^^^^^^^^^^^^^^
			//
			// acquire fields linked to job execution
			//
			field = "interpreter" ; if (dct.contains(field)) Attrs::acquire( interpreter  , &dct[field]       ) ; if (!interpreter) throw "no interpreter found"s ;
			field = "is_python"   ; if (dct.contains(field)) Attrs::acquire( is_python    , &dct[field]       ) ; else              throw "not found"s            ;
			field = "n_tokens"    ; if (dct.contains(field)) Attrs::acquire( n_tokens_key ,  dct[field].str() ) ;
			//
			/**/                                          var_idxs["targets"        ] = {VarCmd::Targets,0 } ;
			for( VarIdx mi=0 ; mi<matches.size() ; mi++ ) var_idxs[matches[mi].first] = {VarCmd::Match  ,mi} ;
			//
			field = "deps" ;
			if (dct.contains("deps_attrs")) deps_attrs = { dct["deps_attrs"].as_a<Tuple>() , var_idxs , *this } ;
			//
			/**/                                                    var_idxs["deps"                       ] = { VarCmd::Deps , 0 } ;
			for( VarIdx d=0 ; d<deps_attrs.spec.deps.size() ; d++ ) var_idxs[deps_attrs.spec.deps[d].first] = { VarCmd::Dep  , d } ;
			//
			field = "create_none_attrs"  ; if (dct.contains(field)) create_none_attrs  = { dct[field].as_a<Tuple>() , var_idxs } ;
			field = "cache_none_attrs"   ; if (dct.contains(field)) cache_none_attrs   = { dct[field].as_a<Tuple>() , var_idxs } ;
			field = "submit_rsrcs_attrs" ; if (dct.contains(field)) submit_rsrcs_attrs = { dct[field].as_a<Tuple>() , var_idxs } ;
			field = "submit_none_attrs"  ; if (dct.contains(field)) submit_none_attrs  = { dct[field].as_a<Tuple>() , var_idxs } ;
			//
			/**/                                                             var_idxs["resources"                           ] = { VarCmd::Rsrcs , 0 } ;
			for( VarIdx r=0 ; r<submit_rsrcs_attrs.spec.rsrcs.size() ; r++ ) var_idxs[submit_rsrcs_attrs.spec.rsrcs[r].first] = { VarCmd::Rsrc  , r } ;
			//
			field = "start_cmd_attrs"   ; if (dct.contains(field)) start_cmd_attrs   = { dct[field].as_a<Tuple>() , var_idxs         } ;
			field = "cmd"               ; if (dct.contains(field)) cmd               = { dct[field].as_a<Tuple>() , var_idxs , *this } ; else throw "not found"s ;
			field = "start_rsrcs_attrs" ; if (dct.contains(field)) start_rsrcs_attrs = { dct[field].as_a<Tuple>() , var_idxs         } ;
			field = "start_none_attrs"  ; if (dct.contains(field)) start_none_attrs  = { dct[field].as_a<Tuple>() , var_idxs         } ;
			field = "end_cmd_attrs"     ; if (dct.contains(field)) end_cmd_attrs     = { dct[field].as_a<Tuple>() , var_idxs         } ;
			field = "end_none_attrs"    ; if (dct.contains(field)) end_none_attrs    = { dct[field].as_a<Tuple>() , var_idxs         } ;
			//
			field = "dbg_info" ;
			if (dct.contains(field)) Attrs::acquire( dbg_info , &dct[field] ) ;
			//
			field = "ete"   ; if (dct.contains(field)) exec_time = Delay(dct[field].as_a<Float>()) ;
			field = "force" ; if (dct.contains(field)) force     =      +dct[field]                ;
			for( VarIdx mi=0 ; mi<n_static_targets ; mi++ ) {
				if (matches[mi].first!="<stdout>") continue ;
				stdout_idx = mi ;
				break ;
			}
			for( VarIdx di=0 ; di<deps_attrs.spec.deps.size() ; di++ ) {
				if (deps_attrs.spec.deps[di].first!="<stdin>") continue ;
				stdin_idx = di ;
				break ;
			}
		}
		catch(::string const& e) { throw to_string("while processing ",name,'.',field," :\n"  ,indent(e)     ) ; }
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
		res.groups.resize(stems.size()) ;
		res.txt = _subst_target(
			target
		,	[&](VarIdx s)->::string {
				if ( s>=n_static_stems && for_name ) {
					::string const& k = stems[s].first ;
					if (k.front()=='<'&&k.back()=='>' ) return escape(to_string('{',""s,"*}")) ; // when matching on job name, star stems are matched as they are reported to user
					else                                return escape(to_string('{',k  ,"*}")) ; // .
				}
				if (res.groups[s]) return to_string("(?:\\",res.groups[s],')') ;                 // already seen, we must protect against following text potentially containing numbers
				res.groups[s]  = cur_group             ;
				cur_group     += 1+stem_mark_counts[s] ;
				return to_string('(',stems[s].second,')') ;
			}
		,	true/*escape*/
		) ;
		res.re = RegExpr( res.txt , true/*fast*/ ) ;                                             // stem regexprs have been validated, normally there is no error here
		return res ;
	}

	void RuleData::_compile() {
		try {
			for( auto const& [k,s] : stems )
				try         { stem_mark_counts.push_back(RegExpr(s).mark_count()) ; }
				catch (...) { throw to_string("bad regexpr for stem ",k," : ",s) ;  }
			// job_name & targets
			/**/                                job_name_pattern = _mk_pattern(job_name  ,true /*for_name*/)  ;
			for( auto const& [k,me] : matches ) patterns.push_back(_mk_pattern(me.pattern,false/*for_name*/)) ;
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
		} catch (::string const& e) {
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
				switch (fstr[ci]) {
					case Rule::StemMrkr : {
						VarCmd vc = decode_enum<VarCmd>(&fstr[ci+1]) ; ci += sizeof(VarCmd) ;
						VarIdx i  = decode_int <VarIdx>(&fstr[ci+1]) ; ci += sizeof(VarIdx) ;
						res += '{' ;
						switch (vc) {
							case VarCmd::Stem  : res += rd.stems                        [i].first ; break ;
							case VarCmd::Match : res += rd.matches                      [i].first ; break ;
							case VarCmd::Dep   : res += rd.deps_attrs.spec.deps         [i].first ; break ;
							case VarCmd::Rsrc  : res += rd.submit_rsrcs_attrs.spec.rsrcs[i].first ; break ;
						DF}
						res += '}' ;
					} break ;
					case '{' : res += "{{"     ; break ;
					case '}' : res += "}}"     ; break ;
					default  : res += fstr[ci] ; break ;
				}
			}
			return res ;
	}
	static ::string _pretty_matches( RuleData const& rd , size_t i , ::vmap_s<RuleData::MatchEntry> const& matches ) {
		auto kind = [&](RuleData::MatchEntry const& me)->::string_view {
			return snake(
				me.flags.is_target==No           ? MatchKind::DepFlags
			:	me.flags.tflags()[Tflag::Target] ? MatchKind::Target
			:	                                   MatchKind::TargetFlags
			) ;
		} ;
		size_t    w1       = 0 ;
		size_t    w2       = 0 ;
		size_t    w3       = 0 ;
		::umap_ss patterns ;
		//
		for( auto const& [k,me] : matches ) {
			::string p = _subst_target( me.pattern ,
				[&](VarIdx s)->::string { return to_string( '{' , rd.stems[s].first , s<rd.n_static_stems?"":"*" , '}' ) ; }
			) ;
			w1          = ::max(w1,kind(me).size()) ;
			w2          = ::max(w2,k       .size()) ;
			w3          = ::max(w3,p       .size()) ;
			patterns[k] = ::move(p)                 ;
		}
		//
		OStringStream res ;
		res << indent("matches :\n",i) ;
		//
		for( VarIdx mi=0 ; mi<matches.size() ; mi++ ) {
			::string             const& k     = matches[mi].first  ;
			RuleData::MatchEntry const& me    = matches[mi].second ;
			OStringStream               flags ;
			//
			bool first = true ;
			if (me.flags.is_target==No) {
				for( Dflag df : Dflag::NRule ) {
					if (!me.flags.dflags()[df]) continue ;
					flags << (first?" : ":" , ") << snake(df) ;
					first = false ;
				}
			} else {
				for( Tflag tf : Tflag::NRule ) {
					if (!me.flags.tflags()[tf]) continue ;
					flags << (first?" : ":" , ") << snake(tf) ;
					first = false ;
				}
				if (me.flags.tflags()[Tflag::Target]) {
					bool first_conflict = true ;
					for( VarIdx c : me.conflicts ) {
						if (first_conflict) {
							flags << (first?" : ":" , ") << "except[" ;
							first_conflict = false ;
							first          = false ;
						} else {
							flags << ',' ;
						}
						flags << matches[c].first ;
					}
					if (!first_conflict) flags << ']' ;
				}
			}
			res << indent(to_string(::setw(w1),kind(me),' ',::setw(w2),k," : "),i+1) ;
			::string flags_str = ::move(flags).str() ;
			if (+flags_str) res << ::setw(w3)<<patterns[k] << flags_str ;
			else            res <<             patterns[k]              ;
			res <<'\n' ;
		}
		res << indent("patterns :\n",i) ;
		for( size_t mi=0 ; mi<matches.size() ; mi++ )
			res << indent(
				to_string(
					/**/    ::setw(w1) , kind(matches[mi].second)
				,	' '   , ::setw(w2) , matches[mi].first
				,	" : " ,              rd.patterns[mi].txt
				,'\n')
			,	i+1
			) ;
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
		for( auto const& [k,me] : rd.matches ) if (rd.job_name==me.pattern) return to_string("<targets.",k,'>') ;
		/**/                                                                return rd.job_name                  ;
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
			for( Dflag f : Dflag::NRule ) {
				if (!ds.dflags[f]) continue ;
				//
				if (first) { flags += " : " ; first = false ; }
				else       { flags += " , " ;                 }
				//
				flags += snake(f) ;
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
		/**/                                 if (sra.backend!=BackendTag::Local) entries.emplace_back( "<backend>" , snake(sra.backend) ) ;
		for (auto const& [k,v] : sra.rsrcs ) if (+v                            ) entries.emplace_back( k           , v                  ) ;
		return _pretty_vmap(i,entries) ;
	}
	static ::string _pretty( size_t i , SubmitNoneAttrs const& sna ) {
		::vmap_ss entries ;
		if (sna.n_retries!=0) entries.emplace_back( "n_retries" , to_string(sna.n_retries) ) ;
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
		for( pass=1 ; pass<=2 ; pass++ ) {                                                   // on 1st pass we compute key size, on 2nd pass we do the job
			if ( sca.auto_mkdir ) do_field( "auto_mkdir"  , to_string(sca.auto_mkdir ) ) ;
			if ( sca.ignore_stat) do_field( "ignore_stat" , to_string(sca.ignore_stat) ) ;
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
		/**/              entries.emplace_back( "autodep" , snake(sra.method)       ) ;
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Warray-bounds"                                      // gcc -O3 complains about array bounds with a completely incoherent message (looks like a bug)
		if (+sra.timeout) entries.emplace_back( "timeout" , sra.timeout.short_str() ) ;
		#pragma GCC diagnostic pop
		/**/              res << _pretty_vmap(i,entries)                                  ;
		if (+sra.env    ) res << indent("environ :\n",i) << _pretty_env( i+1 , sra.env )  ;
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
		/**/        res <<                          _pretty_matches(*this,1,matches           ) ;
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
		for( auto [vc,i] : ctx ) switch (vc) {
			case VarCmd::Stem    : res.push_back(stems                        [i].first) ; break ;
			case VarCmd::Match   : res.push_back(matches                      [i].first) ; break ;
			case VarCmd::Dep     : res.push_back(deps_attrs.spec.deps         [i].first) ; break ;
			case VarCmd::Rsrc    : res.push_back(submit_rsrcs_attrs.spec.rsrcs[i].first) ; break ;
			case VarCmd::Stems   : res.push_back("stems"                               ) ; break ;
			case VarCmd::Targets : res.push_back("targets"                             ) ; break ;
			case VarCmd::Deps    : res.push_back("deps"                                ) ; break ;
			case VarCmd::Rsrcs   : res.push_back("resources"                           ) ; break ;
		DF}
		return res ;
	}

	// match_crc is an id of the rule : a new rule is a replacement of an old rule if it has the same match_crc
	// also, 2 rules matching identically is forbidden : the idea is that one is useless
	// this is not strictly true, though : you could imagine a rule generating a* from b, another generating a* from b but with disjoint sets of a
	// although awkward & useless (as both rules could be merged), this can be meaningful
	// if the need arises, we will add an "id" artificial field entering in match_crc to distinguish them
	void RuleData::_set_crcs() {
		bool special = is_special() ;
		{	::vector_s targets ;
			for( auto const& [k,me] : matches )
				if ( me.flags.is_target==Yes && me.flags.tflags()[Tflag::Target] )
					targets.push_back(me.pattern) ;                                // keys and flags have no influence on matching
			Hash::Xxh h ;
			/**/          h.update(special   ) ;
			/**/          h.update(stems     ) ;
			/**/          h.update(cwd_s     ) ;
			if (!special) h.update(job_name  ) ;                                   // job_name has no effect for source & anti as it is only used to store jobs and there are none
			/**/          h.update(targets   ) ;
			/**/          h.update(allow_ext ) ;
			if (!special) h.update(deps_attrs) ;                                   // no deps for source & anti
			//
			if ( !special && _qualify_dep({},interpreter[0],is_python?DepKind::Python:DepKind::Shell) ) h.update(interpreter[0]) ; // no interpreter for source & anti
			//
			match_crc = ::move(h).digest() ;
		}
		if (special) return ;                // source & anti are only capable of matching
		{	Hash::Xxh h ;
			h.update(match_crc      ) ;
			h.update(matches        ) ;      // these define names and influence cmd execution, all is not necessary but simpler to code
			h.update(force          ) ;
			h.update(is_python      ) ;
			h.update(interpreter    ) ;
			h.update(start_cmd_attrs) ;
			h.update(cmd            ) ;
			h.update(end_cmd_attrs  ) ;
			cmd_crc = ::move(h).digest() ;   // stand-alone : it guarantees rule uniqueness (contains match_crc)
		}
		{	Hash::Xxh h ;
			h.update(cmd_crc           ) ;
			h.update(submit_rsrcs_attrs) ;
			h.update(start_rsrcs_attrs ) ;
			rsrcs_crc = ::move(h).digest() ; // stand-alone : it guarantees rule uniqueness (contains cmd_crc)
		}
	}

	//
	// Rule::SimpleMatch
	//

	Rule::SimpleMatch::SimpleMatch(Job job) : rule{job->rule} {
		::string name_ = job->full_name() ;
		//
		SWEAR( Rule(name_)==rule , mk_printable(name_) , rule->name ) ;                                 // only name suffix is considered to make Rule
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
		for( VarIdx t : rt->matches[rt.tgt_idx].second.conflicts ) {
			if (!rt->patterns[t].match(target)) continue ;
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
		for( auto const& [k,me] : rule->matches ) {
			if (me.flags.is_target!=Yes) continue ;
			::string target = _subst_target(
				me.pattern
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
		for( VarIdx t=rule->n_statics ; t<rule->matches.size() ; t++ ) {
			::uset<VarIdx> seen ;
			res.push_back(_subst_target(
				rule->matches[t].second.pattern
			,	[&](VarIdx s)->::string {
					if (s<rule->n_static_stems) return escape(stems[s]) ;
					if (seen.insert(s).second ) return to_string('('    ,rule->stems[s].second      ,')') ;
					else                        return to_string("(?:\\",rule->patterns[t].groups[s],')') ; // we must protect against following text potentially containing numbers
				}
			,	true/*escape*/
			)) ;
		}
		return res ;
	}

	::vector_s Rule::SimpleMatch::py_matches() const {
		::vector_s res = static_matches() ;
		for( VarIdx mi=rule->n_statics ; mi<rule->matches.size() ; mi++ ) {
			::uset<VarIdx> seen ;
			res.push_back(_subst_target(
				rule->matches[mi].second.pattern
			,	[&](VarIdx s)->::string {
					if (s<rule->n_static_stems) return escape(stems[s]) ;
					::pair_ss const& stem = rule->stems[s] ;
					if      ( !seen.insert(s).second                            ) return to_string("(?P=",stem.first,                ')') ;
					else if ( stem.first.front()=='<' && stem.first.back()=='>' ) return to_string('('   ,               stem.second,')') ; // stem is unnamed
					else                                                          return to_string("(?P<",stem.first,'>',stem.second,')') ;
				}
			,	true/*escape*/
			)) ;
		}
		return res ;
	}

	::vector_s Rule::SimpleMatch::static_matches() const {
		::vector_s res ;
		for( VarIdx mi=0 ; mi<rule->n_statics ; mi++ ) {
			res.push_back(_subst_target(
				rule->matches[mi].second.pattern
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
		::string sfx = rule.job_sfx() ;                                                      // provides room for stems, but we have to fill it
		size_t   i   = 1              ;                                                      // skip initial JobMrkr
		for( VarIdx s=0 ; s<rule->n_static_stems ; s++ ) {
			encode_int<FileNameIdx>( &sfx[i] , poss [s]        ) ; i+= sizeof(FileNameIdx) ;
			encode_int<FileNameIdx>( &sfx[i] , stems[s].size() ) ; i+= sizeof(FileNameIdx) ; // /!\ beware of selecting encode_int of the right size
		}
		return {name,sfx} ;
	}

}
