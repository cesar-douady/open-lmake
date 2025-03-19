// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"      // must be first to include Python.h first
#include "serialize.hh"

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

ENUM( Escape
,	None
,	Re
,	Fstr
)

namespace Engine {

	using namespace Disk ;
	using namespace Py   ;
	using namespace Time ;

	static const ::string _g_fstr_specials = "{}" ;
	static ::string _fstr_escape(::string const& s) {
		::string res ; res.reserve(s.size()+(s.size()>>4)) ;       // take a little margin for escapes
		for( char c : s ) {
			if (_g_fstr_specials.find(c)!=Npos) res.push_back(c) ; // double specials
			/**/                                res.push_back(c) ;
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
					fixed.push_back(c) ;                  // }} are transformed into }
				break ;
				case SeenStart :
					if (c=='{') {
						state = Literal ;
						fixed.push_back(c) ;              // {{ are transformed into {
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
						throw_unless( unnamed_star_idx , "no auto-stem allowed in ",str ) ;
						if (star) { throw_unless( with_re , "unnamed star stems must be defined in ",str ) ; key = "<star_stem"s+((*unnamed_star_idx)++)+'>' ; }
						else                                                                                 key = "<stem"s     +(  unnamed_idx      ++)+'>' ;
					} else {
						throw_unless( is_identifier(key) , "bad key ",key," must be empty or an identifier" ) ;
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
			case Literal   : cb_fixed(fixed) ; break ;    // trailing fixed
			case SeenStop  : throw "spurious } in "+str ;
			case SeenStart :
			case Key       :
			case Re        : throw "spurious { in "+str ;
		DF}
	}
	static void _parse_py( ::string const& str , size_t* unnamed_star_idx , ParsePyFuncStem const& cb_stem ) {
		_parse_py( str , unnamed_star_idx , [](::string const&)->void{} , cb_stem ) ;
	}

	// star stems are represented by a StemMrkr followed by the stem idx
	// cb is called on each stem found
	// return str with stems substituted with the return value of cb and special characters outside stems escaped as asked
	static ::string _subst_target( ::string const& str , SubstTargetFunc const& cb , Escape escape=Escape::None , VarIdx stop_above=Rule::NoVar ) {
		::string res ;
		for( size_t i=0 ; i<str.size() ; i++ ) {                                                  // /!\ not a iota
			char c = str[i] ;
			if (c==Rule::StemMrkr) {
				VarIdx stem = decode_int<VarIdx>(&str[i+1]) ; i += sizeof(VarIdx) ;
				if (stem>=stop_above) return res ;
				res += cb(res.size(),stem) ;
			} else {
				switch (escape) {
					case Escape::None :                                                   break ;
					case Escape::Re   : if (Re::SpecialChars.find(c)!=Npos) res += '\\' ; break ; // escape specials
					case Escape::Fstr : if (_g_fstr_specials.find(c)!=Npos) res += c    ; break ; // double specials
				DF}
				res += c ;
			}
		}
		return res ;
	}
	// provide shortcut when pos is unused
	static ::string _subst_target( ::string const& str , ::function<string(VarIdx)> const& cb , Escape escape=Escape::None , VarIdx stop_above=-1 ) {
		return _subst_target( str , [&](FileNameIdx,VarIdx s)->::string { return cb(s) ; } , escape , stop_above ) ;
	}

	// same as above, except pos is position in input and no result
	static void _parse_target( ::string const& str , ParseTargetFunc const& cb ) {
		for( size_t i=0 ; i<str.size() ; i++ ) {                                   // /!\ not a iota
			char c = str[i] ;
			if (c==Rule::StemMrkr) {
				VarIdx stem = decode_int<VarIdx>(&str[i+1]) ; i += sizeof(VarIdx) ;
				cb(i,stem) ;
			}
		}
	}
	// provide shortcut when pos is unused
	static void _parse_target( ::string const& str , ::function<string(VarIdx)> const& cb ) {
		_parse_target( str , [&](FileNameIdx,VarIdx s)->::string { return cb(s) ; } ) ;
	}

	template<class F,class EF> static void _mk_flags( ::string const& key , Sequence const& py_seq , uint8_t n_skip , BitMap<F>& flags , BitMap<EF>& extra_flags ) {
		for( Object const& item : py_seq ) {
			if (n_skip>0) { n_skip-- ; continue ; }
			if (item.is_a<Str>()) {
				::string flag_str = item.as_a<Str>() ;
				bool     neg      = flag_str[0]=='-' ;
				if (neg) flag_str.erase(0,1) ;         // suppress initial - sign
				//
				if      ( F  f  ; can_mk_enum<F >(flag_str) && (f =mk_enum<F >(flag_str),f <F ::NRule) ) flags      .set(f ,!neg) ;
				else if ( EF ef ; can_mk_enum<EF>(flag_str) && (ef=mk_enum<EF>(flag_str),ef<EF::NRule) ) extra_flags.set(ef,!neg) ;
				else                                                                                     throw "unexpected flag "+flag_str+" for "+key ;
			} else if (item.is_a<Sequence>()) {
				_mk_flags( key , item.as_a<Sequence>() , 0 , flags , extra_flags ) ;
			} else {
				throw key+"has a flag that is not a str" ;
			}
		}
	}
	template<class F,class EF> static ::string _split_flags( ::string const& key , Object const& py , uint8_t n_skip , BitMap<F>& flags , BitMap<EF>& extra_flags ) {
		if (py.is_a<Str>()) return py.as_a<Str>() ;
		Sequence const& py_seq = py.as_a<Sequence>() ;
		SWEAR(py_seq.size()>=n_skip          ,key) ;
		SWEAR(py_seq[0].is_a<Str>(),key) ;
		_mk_flags( key , py_seq , n_skip , flags , extra_flags ) ;
		return py_seq[0].as_a<Str>() ;
	}

	//
	// Dynamic
	//

	::string& operator+=( ::string& os , DepSpec const& ds ) {
		return os <<"DepSpec("<< ds.txt <<','<< ds.dflags <<','<< ds.extra_dflags <<')' ;
	}

	bool DynamicDskBase::s_is_dynamic(Tuple const& py_src) {
		ssize_t sz = py_src.size() ;
		if (sz>1) SWEAR(py_src[1].is_a<Sequence>()) ; // names
		switch (sz) {
			case 1 :
			case 2 : return false ;
			case 5 :
				SWEAR(py_src[2].is_a<Str>()) ;        // glbs
				SWEAR(py_src[3].is_a<Str>()) ;        // code
				SWEAR(py_src[4].is_a<Str>()) ;        // dbg_info
				return +py_src[3] ;
		DF}
	}

	DynamicDskBase::DynamicDskBase( Tuple const& py_src , ::umap_s<CmdIdx> const& var_idxs ) :
		is_dynamic { s_is_dynamic(py_src)                                        }
	,	glbs_str   { is_dynamic      ? ::string(py_src[2].as_a<Str>()) : ""s }
	,	code_str   { is_dynamic      ? ::string(py_src[3].as_a<Str>()) : ""s }
	,	dbg_info   { py_src.size()>4 ? ::string(py_src[4].as_a<Str>()) : ""s }
	{
		Gil::s_swear_locked() ;
		if (py_src.size()<=1) return ;
		ctx.reserve(py_src[1].as_a<Sequence>().size()) ;
		for( Object const& py_item : py_src[1].as_a<Sequence>() ) {
			CmdIdx ci = var_idxs.at(py_item.as_a<Str>()) ;
			ctx.push_back(ci) ;
		}
		::sort(ctx) ; // stabilize crc's
	}

	void DynamicDskBase::_s_eval( Job j , Rule::RuleMatch& m/*lazy*/ , ::vmap_ss const& rsrcs_ , ::vector<CmdIdx> const& ctx , EvalCtxFuncStr const& cb_str , EvalCtxFuncDct const& cb_dct ) {
		::string         res        ;
		Rule             r          = +j ? j->rule() : m.rule          ;
		::vmap_ss const& rsrcs_spec = r->submit_rsrcs_attrs.spec.rsrcs ;
		::vector_s       mtab       ;
		::vmap_ss        dtab       ;
		::umap_ss        rtab       ;
		//
		auto match = [&]()->Rule::RuleMatch const& { { if (!m) m = Rule::RuleMatch(j) ; } return m             ; } ; // solve lazy evaluation
		auto stems = [&]()->::vector_s      const& {                                      return match().stems ; } ;
		//
		auto matches = [&]()->::vector_s const& { { if (!mtab) for( ::string const& t      : match().py_matches() ) mtab.push_back   (  mk_lcl(t     ,r->sub_repo_s)) ; } return mtab ; } ;
		auto deps    = [&]()->::vmap_ss  const& { { if (!dtab) for( auto     const& [k,dn] : match().deps      () ) dtab.emplace_back(k,mk_lcl(dn.txt,r->sub_repo_s)) ; } return dtab ; } ;
		auto rsrcs   = [&]()->::umap_ss  const& { { if (!rtab) rtab = mk_umap(rsrcs_) ;                                                                                 } return rtab ; } ;
		for( auto [vc,i] : ctx ) {
			::vmap_ss dct ;
			switch (vc) {
				case VarCmd::Stem      :                                                                        cb_str(vc,i,r->stems  [i].first,stems  ()[i]       ) ;   break ;
				case VarCmd::StarMatch :
				case VarCmd::Match     :                                                                        cb_str(vc,i,r->matches[i].first,matches()[i]       ) ;   break ;
				case VarCmd::Dep       :                                                                        cb_str(vc,i,deps()    [i].first,deps   ()[i].second) ;   break ;
				case VarCmd::Rsrc      : { auto it = rsrcs().find(rsrcs_spec[i].first) ; if (it!=rsrcs().end()) cb_str(vc,i,it->first          ,it->second         ) ; } break ;
				//
				case VarCmd::Stems   : for( VarIdx j : iota(r->n_static_stems  ) ) dct.emplace_back(r->stems  [j].first,stems  ()[j]) ; cb_dct(vc,i,"stems"    ,dct   ) ; break ;
				case VarCmd::Targets : for( VarIdx j : iota(r->n_static_targets) ) dct.emplace_back(r->matches[j].first,matches()[j]) ; cb_dct(vc,i,"targets"  ,dct   ) ; break ;
				case VarCmd::Deps    : for( auto const& [k,d] : deps()           ) dct.emplace_back(k                  ,d           ) ; cb_dct(vc,i,"deps"     ,dct   ) ; break ;
				case VarCmd::Rsrcs   :                                                                                                  cb_dct(vc,i,"resources",rsrcs_) ; break ;
			DF}
		}
	}

	//
	// Rule
	//

	Atomic<Pdate> Rule::s_last_dyn_date ;
	Job           Rule::s_last_dyn_job  ;
	const char*   Rule::s_last_dyn_msg  = nullptr ;

	::string& operator+=( ::string& os , Rule const r ) {
		/**/    os << "R(" ;
		if (+r) os << +r   ;
		return  os << ')'  ;
	}

	//
	// RuleCrc
	//

	::string& operator+=( ::string& os , RuleCrc const r ) {
		/**/    os << "RC(" ;
		if (+r) os << +r   ;
		return  os << ')'  ;
	}

	//
	// RuleTgt
	//

	::string& operator+=( ::string& os , RuleTgt const rt ) {
		return os << "RT(" << RuleCrc(rt) <<':'<< int(rt.tgt_idx) << ')' ;
	}

	//
	// Attrs
	//

	namespace Attrs {

		bool/*updated*/ acquire( bool& dst , Object const* py_src ) {
			if (!py_src      ) {           return false/*updated*/ ;                                        }
			if (*py_src==None) { if (!dst) return false/*updated*/ ; dst = false ; return true/*updated*/ ; }
			//
			dst = +*py_src ;
			return true/*updated*/ ;
		}

		bool/*updated*/ acquire( Delay& dst , Object const* py_src , Delay min , Delay max ) {
			if (!py_src      )             return false/*updated*/ ;
			if (*py_src==None) { if (!dst) return false/*updated*/ ; dst = {} ; return true/*updated*/ ; }
			//
			double d = 0 ;
			if      (py_src->is_a<Float>()) d =             py_src->as_a<Float>()  ;
			else if (py_src->is_a<Str  >()) d = *Ptr<Float>(py_src->as_a<Str  >()) ;
			else                            throw "cannot convert to float"s ;
			dst = Delay(d) ;
			throw_unless( dst>=min , "underflow" ) ;
			throw_unless( dst<=max , "overflow"  ) ;
			return true/*updated*/ ;
		}

		bool/*updated*/ acquire( JobSpace::ViewDescr& dst , Object const* py_src ) {
			if (!py_src      )             return false/*updated*/ ;
			if (*py_src==None) { if (!dst) return false/*updated*/ ; dst = {} ; return true/*updated*/ ; }
			::string   upper   ;
			::vector_s lower   ;
			::vector_s copy_up ;
			if (py_src->is_a<Str>()) {
				if (!acquire(upper,py_src)) throw "nothing to bind to"s ;
			} else if (py_src->is_a<Dict>()) {
				Dict const& py_dct = py_src->as_a<Dict>() ;
				acquire_from_dct(upper  ,py_dct,"upper"  ) ;
				acquire_from_dct(lower  ,py_dct,"lower"  ) ;
				acquire_from_dct(copy_up,py_dct,"copy_up") ;
				/**/                                throw_unless( +upper             , "no upper"                            ) ;
				/**/                                throw_unless( !copy_up || +lower , "cannot copy up from nowhere"         ) ;
				for( ::string const& cu : copy_up ) throw_unless( !is_abs(cu)        , "copy up item must be relative : ",cu ) ;
			} else throw "unexpected view description which is not a str nor a dict"s ;
			/**/                       dst.phys.push_back(::move(upper)) ;
			for( ::string& l : lower ) dst.phys.push_back(::move(l    )) ;
			/**/                       dst.copy_up = ::move(copy_up) ;
			return true/*updated*/ ;
		}

		bool/*updated*/ acquire( DbgEntry& dst , Object const* py_src ) {
			if (!py_src      ) {                          return false/*updated*/ ;                                     }
			if (*py_src==None) { if (!dst.first_line_no1) return false/*updated*/ ; dst = {} ; return true/*updated*/ ; }
			//
			Sequence const& py_seq = py_src->as_a<Sequence>() ;
			acquire(dst.module        ,&py_seq[0].as_a<Str>()                 ) ;
			acquire(dst.qual_name     ,&py_seq[1].as_a<Str>()                 ) ;
			acquire(dst.filename      ,&py_seq[2].as_a<Str>()                 ) ;
			acquire(dst.first_line_no1,&py_seq[3].as_a<Int>(),size_t(1)/*min*/) ;
			return true/*updated*/ ;
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

	static bool/*keep*/ _qualify_dep( ::string const& key , bool is_python , ::string const& dep , ::string const& full_dep , ::string const& dep_for_msg ) {
		::string dir_s = dep.substr(0,dep.find(Rule::StemMrkr)) ; if ( size_t p=dir_s.rfind('/') ; p!=Npos ) dir_s.resize(p+1) ; else dir_s.clear() ;
		//
		auto bad = [&] ( ::string const& msg, bool interpreter_ok )->void {
			if (key!="<interpreter>") throw "dep "+key+" ("+dep_for_msg+") "+msg ;
			if (interpreter_ok      ) return                                     ;
			if (is_python           ) throw "python ("     +dep_for_msg+") "+msg ;
			/**/                      throw "shell ("      +dep_for_msg+") "+msg ;
		} ;
		//
		if (!is_canon(dir_s)) bad("canonical form is : "+mk_canon(full_dep),false/*interpreter_ok*/) ;
		if (is_lcl(dep)     ) return true/*keep*/ ;
		// dep is non-local, substitute relative/absolute if it lies within a source dirs
		::string rel_dir_s = mk_rel(dir_s,*g_repo_root_s) ;
		::string abs_dir_s = mk_abs(dir_s,*g_repo_root_s) ;
		if (is_lcl_s(rel_dir_s)) bad("must be provided as local file : "+rel_dir_s+substr_view(full_dep,dir_s.size()),false/*interpreter_ok*/) ;
		//
		for( ::string const& sd_s : *g_src_dirs_s ) {
			if ( is_lcl_s(sd_s)                                                ) continue ;                                                                  // nothing to recognize inside repo
			::string const& d_s = is_abs_s(sd_s) ? abs_dir_s : rel_dir_s ;
			if (!( d_s.starts_with(sd_s) && is_lcl_s(d_s.substr(sd_s.size())) )) continue            ;                                                       // not in this source dir
			if ( is_abs_s(dir_s)==is_abs_s(sd_s)                               ) return true/*keep*/ ;
			bad("must be "s+(is_abs_s(sd_s)?"absolute":"relative")+" inside source dir "+sd_s,false/*interpreter_ok*/) ;
		}
		bad("is outside repository and all source dirs, consider : lmake.manifest.append("+mk_py_str(dir_s)+")",true/*interpreter_ok*/) ;                    // interpreter is typically outside repo
		return false/*keep*/ ;
	}
	static bool/*keep*/ _qualify_dep( ::string const& key , bool is_python , ::string const& dep ) {
		if ( is_canon(dep) && is_lcl(dep) ) return true/*keep*/                       ;                                                                      // fast path when evaluating job deps
		else                                return _qualify_dep(key,is_python,dep,dep,dep) ;
	}
	void DepsAttrs::init( bool /*is_dynamic*/ , Dict const* py_src , ::umap_s<CmdIdx> const& var_idxs , RuleData const& rd ) {
		full_dynamic = false ;                                                                                                                               // if full dynamic, we are not initialized
		//
		for( auto const& [py_key,py_val] : py_src->as_a<Dict>() ) {
			::string key = py_key.template as_a<Str>() ;
			if (py_val==None) {
				deps.emplace_back(key,DepSpec()) ;
				continue ;
			}
			VarIdx      n_unnamed  = 0                                                                                    ;
			Dflags      df         { Dflag::Essential , Dflag::Static }                                                   ;
			ExtraDflags edf        ;
			::string    dep        = _split_flags( "dep "+key , py_val , 1/*n_skip*/ , df , edf )                         ; SWEAR(!(edf&~ExtraDflag::Top)) ; // or we must review side_deps in DepSpec
			::string    parsed_dep = rd.add_cwd( Attrs::subst_fstr( dep , var_idxs , n_unnamed ) , edf[ExtraDflag::Top] ) ;
			::string    full_dep   = rd.add_cwd( ::copy(dep)                                     , edf[ExtraDflag::Top] ) ;
			//
			if (!_qualify_dep( key , rd.is_python , parsed_dep , full_dep , dep )) continue ;
			//
			if (n_unnamed) {
				for( auto const& [k,ci] : var_idxs ) if (ci.bucket==VarCmd::Stem) n_unnamed-- ;
				throw_unless( !n_unnamed , "dep ",key," (",dep,") contains some but not all unnamed static stems" ) ;
			}
			deps.emplace_back( key , DepSpec{ ::move(parsed_dep) , df , edf } ) ;
		}
		throw_unless( deps.size()<Rule::NoVar-1 , "too many static deps : ",deps.size() ) ; // -1 to leave some room to the interpreter, if any
	}

	::vmap_s<DepSpec> DynamicDepsAttrs::eval(Rule::RuleMatch const& match) const {
		::vmap_s<DepSpec> res ;
		for( auto const& [k,ds] : spec.deps ) res.emplace_back( k , DepSpec{parse_fstr(ds.txt,match),ds.dflags,ds.extra_dflags} ) ;
		//
		if (is_dynamic) {
			Gil         gil    ;
			Ptr<Object> py_obj = _eval_code(match) ;
			//
			::map_s<VarIdx> dep_idxs ;
			for( VarIdx di : iota<VarIdx>(spec.deps.size()) ) dep_idxs[spec.deps[di].first] = di ;
			if (*py_obj!=None) {
				throw_unless( +py_obj->is_a<Dict>() , "type error : ",py_obj->ob_type->tp_name," is not a dict" ) ;
				for( auto const& [py_key,py_val] : py_obj->as_a<Dict>() ) {
					if (py_val==None) continue ;
					::string key = py_key.as_a<Str>() ;
					Dflags      df  { Dflag::Essential , Dflag::Static } ;
					ExtraDflags edf ; SWEAR(!(edf&~ExtraDflag::Top)) ;                                                                             // or we must review side_deps
					::string    dep = match.rule->add_cwd( _split_flags( "dep "+key , py_val , 1/*n_skip*/ , df , edf ) , edf[ExtraDflag::Top] ) ;
					if ( !_qualify_dep( key , match.rule->is_python , dep ) ) continue ;
					DepSpec ds { dep , df , edf } ;
					if (spec.full_dynamic) { SWEAR(!dep_idxs.contains(key),key) ; res.emplace_back(key,ds) ;          } // dep cannot be both static and dynamic
					else                                                          res[dep_idxs.at(key)].second = ds ;   // if not full_dynamic, all deps must be listed in spec
				}
			}
			if (!spec.full_dynamic) {                                                                                   // suppress dep placeholders that are not set by dynamic code
				NodeIdx i = 0 ;
				for( NodeIdx j : iota(res.size()) ) {
					if (!res[j].second.txt) continue ;
					if (i!=j              ) res[i] = ::move(res[j]) ;
					i++ ;
				}
				res.resize(i) ;
			}
		}
		//
		return res  ;
	}

	//
	// Cmd
	//

	::string RuleData::gen_py_line( Job j , Rule::RuleMatch& m/*lazy*/ , VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) const {
		if (vc!=VarCmd::StarMatch) return key+" = "+mk_py_str(val)+'\n' ;
		//
		Rule           r    = +m ? m.rule : j->rule() ;
		::vector_s     args ;
		::uset<VarIdx> seen ;
		::string   expr = _subst_target(
			matches[i].second.pattern
		,	[&](VarIdx s)->::string {
				bool first = seen.insert(s).second ;
				::string k = stems[s].first        ;
				if ( k.front()=='<' and k.back()=='>' )   k = k.substr(1,k.size()-2) ;
				if ( s>=r->n_static_stems             ) { if (first) args.push_back(k)      ; return '{'+k+'}'                ; }
				else                                    { if (!m   ) m = Rule::RuleMatch(j) ; return _fstr_escape(m.stems[s]) ; } // solve lazy m
			}
		,	Escape::Fstr
		) ;
		::string res   = "def "+key+'(' ;
		First    first ;
		for( ::string const& a : args ) res <<first("",",")<<' '<<a<<' ' ;
		res<<") : return f"<<mk_py_str(expr)<<'\n'    ;
		res<<key<<".regexpr = "<<mk_py_str(val)<<'\n' ;
		return res ;
	}

	void Cmd::init( bool /*is_dynamic*/ , Dict const* py_src , ::umap_s<CmdIdx> const& var_idxs , RuleData const& rd ) {
		::string raw_cmd ;
		Attrs::acquire_from_dct( raw_cmd , *py_src , "cmd" ) ;
		if (rd.is_python) {
			cmd = ::move(raw_cmd) ;
		} else {
			VarIdx n_unnamed = 0 ;
			cmd = Attrs::subst_fstr(raw_cmd,var_idxs,n_unnamed) ;
			SWEAR( !n_unnamed , n_unnamed ) ;
		}
	}

	pair_ss/*script,call*/ DynamicCmd::eval( Rule::RuleMatch const& match , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps ) const {
		Rule r = match.rule ; // if we have no job, we must have a match as job is there to lazy evaluate match if necessary
		if (!r->is_python) {
			::string cmd ;
			if (!is_dynamic) {
				cmd = parse_fstr(spec.cmd,match,rsrcs) ;
			} else {
				Gil         gil    ;
				Ptr<Object> py_obj = _eval_code( match , rsrcs , deps ) ;
				throw_unless( +py_obj->is_a<Str>() , "type error : ",py_obj->type_name()," is not a str" ) ;
				Attrs::acquire( cmd , &py_obj->as_a<Str>() ) ;
			}
			return {{}/*preamble*/,::move(cmd)} ;
		} else {
			::string res ;
			eval_ctx( match , rsrcs
			,	[&]( VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) -> void {
					res += r->gen_py_line( match , vc , i , key , val ) ;
				}
			,	[&]( VarCmd , VarIdx , ::string const& key , ::vmap_ss const& val ) -> void {
					res<<key<<" = {\n" ;
					bool f = true ;
					for( auto const& [k,v] : val ) {
						if (!f) res += ',' ;
						res<<'\t'<<mk_py_str(k)<<" : "<<mk_py_str(v)<<'\n' ;
						f = false ;
					}
					res += "}\n" ;
				}
			) ;
			res <<set_nl<< spec.cmd ;
			return {append_dbg_info(res)/*preamble*/,"cmd()"} ;
		}
	}

	::string& operator+=( ::string& os , DbgEntry const& de ) {
		if (+de) return os<<"( "<<de.module<<" , "<<de.qual_name<<" , "<<de.filename<<" , "<<de.first_line_no1<<" )" ;
		else     return os<<"()"                                                                                     ;
	}

	//
	// RuleData
	//

	void RuleData::MatchEntry::set_pattern( ::string&& p , VarIdx n_stems ) {
		pattern  = ::move(p)                 ;
		ref_cnts = ::vector<VarIdx>(n_stems) ;
		_parse_target( pattern , [&](VarIdx s)->::string { ref_cnts[s]++ ; return {} ; } ) ;
	}

	static void _append_stem( ::string& target , VarIdx stem_idx ) {
		::string s ; s.resize(sizeof(VarIdx)) ;
		encode_int( s.data() , stem_idx ) ;
		target += Rule::StemMrkr ;
		target += s              ;
	}

	RuleData::RuleData( Special s , ::string const& src_dir_s ) : special{s} , name{snake(s)} {
		if (!s) {
			name.clear() ;
			return ;
		}
		if (s<Special::NShared) SWEAR( !src_dir_s                          , s , src_dir_s ) ; // shared rules cannot have parameters as, precisely, they are shared
		else                    SWEAR( +src_dir_s && is_dirname(src_dir_s) , s , src_dir_s ) ; // ensure source dir ends with a /
		switch (s) {
			case Special::Req      : force = true ; break ;
			case Special::Infinite :                break ;
			case Special::GenericSrc :
				name             = "source dir" ;
				job_name         = src_dir_s    ; _append_stem(job_name,0) ;
				force            = true         ;
				n_static_stems   = 1            ;
				n_static_targets = 1            ;
				n_statics        = 1            ;
				allow_ext        = true         ;                                              // sources may lie outside repo
				stems         .emplace_back("",".*"                ) ;
				stem_mark_cnts.push_back   (0                      ) ;
				matches       .emplace_back("",MatchEntry{job_name}) ;
				_compile () ;
			break ;
		DF}
		_set_crcs() ;
	}

	::string& operator+=( ::string& os , RuleData const& rd ) {
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
			for( FileNameIdx i=0 ; i<sz ; i++ ) {                                                      // /!\ not a iota
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
					::string_view sa = substr_view( a , iae+1-sizeof(VarIdx) , sizeof(VarIdx) ) ;
					::string_view sb = substr_view( b , ibe+1-sizeof(VarIdx) , sizeof(VarIdx) ) ;
					if (sa==sb) { i+=sizeof(VarIdx) ; continue ; }                                     // same      stems, continue analysis
					else        { goto Continue ;                }                                     // different stems, could have identical values
				}
				goto Continue ;                              // one is a stem, not the other, the stem value can match the fixed part of the other, may conflict
			}
			if ( sz == (sz_a>sz_b?b.size():a.size()) ) {     // if shortest is a prefix of longest, analyse remaining of longest to see if we are certain it is non-empty
				::string const& l = sz_a>sz_b ? a : b ;      // longest
				for( FileNameIdx i=sz ; i<l.size() ; i++ ) {                                           // /!\ not a iota
					FileNameIdx j       = i ; if (!is_prefix) j   = l.size()-1-j                     ; // current index
					FileNameIdx je      = j ; if ( is_prefix) je += sizeof(VarIdx)                   ; // last char of stem if it is one (cannot subtract sizeof(VarIdx) as FileNameIdx is unsigned)
					bool        is_stem = je>=sizeof(VarIdx) && l[je-sizeof(VarIdx)]==Rule::StemMrkr ;
					if (is_stem) i += sizeof(VarIdx) ;                                                 // stem value can be empty, may still conflict, continue
					else         return false ;                                                        // one is a strict suffix of the other, no conflict possible
				}
			}
		Continue : ;
		}
		return true ;
	}
	void RuleData::_acquire_py(Dict const& dct) {
		::string field ;
		Gil::s_swear_locked() ;
		try {
			//
			// acquire essential (necessary for Anti & GenericSrc)
			//
			//
			field = "__special__" ;
			if (dct.contains(field)) {
				special = mk_enum<Special>(dct[field].as_a<Str>()) ;
				throw_unless( special>=Special::NShared , "unexpected value for __special__ attribute : ",special ) ;
			} else {
				special = Special::Plain ;
			}
			field = "name"       ; if (dct.contains(field)) name       = dct[field].as_a<Str  >() ; else throw "not found"s ;
			field = "sub_repo_s" ; if (dct.contains(field)) sub_repo_s = dct[field].as_a<Str  >() ;
			field = "prio"       ; if (dct.contains(field)) user_prio  = dct[field].as_a<Float>() ;
			if (+sub_repo_s) {
				sub_repo_s = with_slash(sub_repo_s) ;
				if (sub_repo_s.front()=='/') {
					if (sub_repo_s.starts_with(*g_repo_root_s)) sub_repo_s.erase(0,g_repo_root_s->size()) ;
					else                                        throw "cwd must be relative to repo root dir"s ;
				}
			}
			//
			Trace trace("_acquire_py",name,sub_repo_s,user_prio) ;
			//
			::umap_ss      stem_defs  ;
			::map_s<Bool3> stem_stars ;                                                                // ordered so that stems are ordered, Maybe means stem is used both as static and star
			field = "stems" ;
			if (dct.contains(field))
				for( auto const& [py_k,py_v] : dct[field].as_a<Dict>() )
					stem_defs.emplace( ::string(py_k.as_a<Str>()) , ::string(py_v.as_a<Str>()) ) ;
			//
			// augment stems with definitions found in job_name and targets
			size_t unnamed_star_idx = 1 ;                                                                                      // free running while walking over job_name + targets
			auto augment_stems = [&]( ::string const& k , bool star , ::string const* re , bool star_only ) -> void {
				if (re) {
					auto [it,inserted] = stem_defs.emplace(k,*re) ;
					throw_unless( +inserted || *re==it->second , "2 different definitions for stem ",k," : ",it->second," and ",*re ) ;
				}
				if ( !star_only || star ) {
					auto [it,inserted] = stem_stars.emplace(k,No|star) ;
					if ( !inserted && (No|star)!=it->second ) it->second = Maybe ;                                             // stem is used both as static and star
				}
			} ;
			field = "job_name" ;
			throw_unless( dct.contains(field) , "not found" ) ;
			job_name = dct[field].as_a<Str>() ;
			_parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> void { augment_stems(k,star,re,false/*star_only*/) ; }
			) ;
			field = "matches" ;
			throw_unless( dct.contains(field) , "not found" ) ;
			::string job_name_key ;
			::string job_name_msg = "job_name" ;
			for( auto const& [py_k,py_tkfs] : dct[field].as_a<Dict>() ) {
				field = py_k.as_a<Str>() ;
				::string  target =                    py_tkfs.as_a<Sequence>()[0].as_a<Str>()  ;                               // .
				MatchKind kind   = mk_enum<MatchKind>(py_tkfs.as_a<Sequence>()[1].as_a<Str>()) ;                               // targets are a tuple (target_pattern,kind,flags...)
				// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
				if (target!=job_name) {
					_parse_py( target , &unnamed_star_idx ,
						// static stems are declared in job_name, but error will be caught later on, when we can generate a sound message
						[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> void {
							augment_stems(k,star,re,true/*star_only*/) ;
						}
					) ;
				} else if (!job_name_key) {
					job_name_key =                     field ;
					job_name_msg = snake_str(kind)+' '+field ;
				}
			}
			//
			// gather job_name and targets
			field            = "job_name" ;
			unnamed_star_idx = 1          ;                                                                                    // reset free running at each pass over job_name+targets
			VarIdx n_static_unnamed_stems = 0     ;
			bool   job_name_is_star       = false ;
			auto   stem_words             = []( ::string const& k , bool star , bool unnamed ) -> ::string {
				const char* stem = star ? "star stem" : "stem" ;
				return unnamed ? "unnamed "s+stem : ""s+stem+' '+k ;
			} ;
			_parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) -> void {
					if      (!stem_defs.contains(k)) throw "found undefined "+stem_words(k,star,unnamed)+" in "+job_name_msg ;
					if      (star                  ) job_name_is_star = true ;
					else if (unnamed               ) n_static_unnamed_stems++ ;
				}
			) ;
			//
			field = "matches" ;
			{	::vmap_s<MatchEntry> star_matches                 ;                                                            // defer star matches so that static targets are put first
				::vmap_s<MatchEntry> static_matches[N<MatchKind>] ;                                                            // defer star matches so that static targets are put first
				bool                 seen_top                     = false ;
				bool                 seen_target                  = false ;
				for( auto const& [py_k,py_tkfs] : dct[field].as_a<Dict>() ) {                                                  // targets are a tuple (target_pattern,flags...)
					field = py_k.as_a<Str>() ;
					Sequence const& pyseq_tkfs         = py_tkfs.as_a<Sequence>()                      ;
					::string        target             =                    pyseq_tkfs[0].as_a<Str>()  ;                       // .
					MatchKind       kind               = mk_enum<MatchKind>(pyseq_tkfs[1].as_a<Str>()) ;                       // targets are a tuple (target_pattern,kind,flags...)
					bool            is_star            = false                                         ;
					::set_s         missing_stems      ;
					bool            is_target          = kind!=MatchKind::SideDep                      ;
					bool            is_official_target = kind==MatchKind::Target                       ;
					bool            is_stdout          = field=="<stdout>"                             ;
					MatchFlags      flags              ;
					Tflags          tflags             ;
					Dflags          dflags             ;
					ExtraTflags     extra_tflags       ;
					ExtraDflags     extra_dflags       ;
					// ignore side_targets and side_deps for source and anti-rules
					// this is meaningless, but may be inherited for stems, typically as a PyRule
					if ( !is_official_target && is_special() ) continue ;
					// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
					if (target==job_name) {
						if (job_name_is_star) is_star = true ;
					} else {
						if (is_official_target) for( auto const& [k,s] : stem_stars ) if (s!=Yes) missing_stems.insert(k) ;
						_parse_py( target , &unnamed_star_idx ,
							[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) -> void {
								if (!stem_defs.contains(k)) throw "found undefined "+stem_words(k,star,unnamed)+" in "+kind ;
								//
								if (star) {
									is_star = true ;
									return ;
								}
								auto it = stem_stars.find(k) ;
								throw_unless( it!=stem_stars.end() && it->second!=Yes , stem_words(k,star,unnamed)," appears in ",kind," but not in ",job_name_msg,", consider using ",k,'*' ) ;
								if (is_official_target) missing_stems.erase(k) ;
							}
						) ;
					}
					if (             is_official_target )   tflags |= Tflag::Target    ;
					if ( !is_star && is_official_target )   tflags |= Tflag::Essential ;                                       // static targets are essential by default
					if ( !is_star                       )   tflags |= Tflag::Static    ;
					if ( is_target                      ) { _split_flags( snake_str(kind) , pyseq_tkfs , 2/*n_skip*/ , tflags , extra_tflags ) ; flags = {tflags,extra_tflags} ; }
					else                                  { _split_flags( snake_str(kind) , pyseq_tkfs , 2/*n_skip*/ , dflags , extra_dflags ) ; flags = {dflags,extra_dflags} ; }
					// check
					if ( target.starts_with(*g_repo_root_s)                            ) throw snake_str(kind)+" must be relative to root dir : "          +target  ;
					if ( !is_lcl(target)                                               ) throw snake_str(kind)+" must be local : "                         +target  ;
					if ( !is_canon(target)                                             ) throw snake_str(kind)+" must be canonical : "                     +target  ;
					if ( +missing_stems                                                ) throw cat("missing stems ",missing_stems," in ",kind," : "        ,target) ;
					if (  is_star                              && is_special()         ) throw "star "s+kind+"s are meaningless for source and anti-rules"          ;
					if (  is_star                              && is_stdout            ) throw "stdout cannot be directed to a star target"s                        ;
					if ( tflags      [Tflag     ::Incremental] && is_stdout            ) throw "stdout cannot be directed to an incremental target"s                ;
					if ( extra_tflags[ExtraTflag::Optional   ] && is_star              ) throw "star targets are natively optional : "                     +target  ;
					if ( extra_tflags[ExtraTflag::Optional   ] && tflags[Tflag::Phony] ) throw "cannot be simultaneously optional and phony : "            +target  ;
					bool is_top = is_target ? extra_tflags[ExtraTflag::Top] : extra_dflags[ExtraDflag::Top] ;
					seen_top    |= is_top             ;
					seen_target |= is_official_target ;
					// record
					/**/                     target   = add_cwd( ::move(target  ) , is_top ) ;
					if (field==job_name_key) job_name = add_cwd( ::move(job_name) , is_top ) ;
					(is_star?star_matches:static_matches[+kind]).emplace_back( field , MatchEntry{::move(target),flags} ) ;
				}
				SWEAR(+seen_target) ;                                                                                          // we should not have come up to here without a target
				if (!job_name_key) job_name = add_cwd( ::move(job_name) , seen_top ) ;
				n_static_targets = static_matches[+MatchKind::Target].size() ; static_assert(+MatchKind::Target==0) ;          // ensure offical static targets are first in matches
				for( MatchKind k : iota(All<MatchKind>) ) for( auto& st : static_matches[+k] ) matches.push_back(::move(st)) ; // put static first
				n_statics  = matches.size() ;
				/**/                                      for( auto& st : star_matches       ) matches.push_back(::move(st)) ; // then star
			}
			field = "" ;
			throw_unless( matches.size()<NoVar , "too many targets, side_targets and side_deps ",matches.size()," >= ",int(NoVar) ) ;
			::umap_s<VarIdx> stem_idxs ;
			for( bool star : {false,true} ) {                                                                                  // keep only useful stems and order them : static first, then star
				for( auto const& [k,v] : stem_stars ) {
					if (v==(No|!star)) continue ;                                                                              // stems that are both static and start appear twice
					::string const& s = stem_defs.at(k) ;
					stem_idxs.emplace     ( k+" *"[star] , VarIdx(stems.size()) ) ;
					stems    .emplace_back( k            , s                    ) ;
					try         { stem_mark_cnts.push_back(Re::RegExpr(s).mark_count()) ; }
					catch (...) { throw "bad regexpr for stem "+k+" : "+s ;               }
				}
				if (!star) n_static_stems = stems.size() ;
			}
			::umap_s<CmdIdx> var_idxs ;
			/**/                                   var_idxs["stems"       ] = {VarCmd::Stems,0} ;
			for( VarIdx s : iota(n_static_stems) ) var_idxs[stems[s].first] = {VarCmd::Stem ,s} ;
			throw_unless( stems.size()<=NoVar , "too many stems : ",stems.size()," > ",int(NoVar) ) ;
			//
			// reformat job_name & targets to improve matching efficiency
			// {Stem} is replaced by "StemMrkr<stem_idx>"
			// StemMrkr is there to unambiguously announce a stem idx
			//
			::string mk_tgt ;
			auto mk_fixed = [&]( ::string const& fixed                                                       )->void { mk_tgt += fixed ;                                   } ;
			auto mk_stem  = [&]( ::string const& key , bool star , bool /*unnamed*/ , ::string const* /*re*/ )->void { _append_stem(mk_tgt,stem_idxs.at(key+" *"[star])) ; } ;
			unnamed_star_idx = 1 ;                                                                                             // reset free running at each pass over job_name+targets
			mk_tgt.clear() ;
			_parse_py( job_name , &unnamed_star_idx , mk_fixed , mk_stem ) ;
			::string new_job_name = ::move(mk_tgt) ;
			// compile potential conflicts as there are rare and rather expensive to detect, we can avoid most of the verifications by statically analyzing targets
			for( VarIdx mi : iota<VarIdx>(matches.size()) ) {
				MatchEntry& me = matches[mi].second ;
				// avoid processing target if it is identical to job_name
				// this is not an optimization, it is to ensure unnamed_star_idx's match
				if (me.pattern==job_name) me.set_pattern(new_job_name,stems.size()) ;
				else {
					mk_tgt.clear() ;
					_parse_py( me.pattern , &unnamed_star_idx , mk_fixed , mk_stem ) ;
					me.set_pattern(::move(mk_tgt),stems.size()) ;
				}
				for( VarIdx mi2 : iota(mi) )
					if ( _may_conflict( n_static_stems , me.pattern , matches[mi2].second.pattern ) ) { trace("conflict",mi,mi2) ; me.conflicts.push_back(mi2) ; }
			}
			job_name = ::move(new_job_name) ;
			//
			//vvvvvvvvvvvvvvvvvvvvvvvv
			if (is_special()) return ;                                                                                         // if special, we have no dep, no execution, we only need essential info
			//^^^^^^^^^^^^^^^^^^^^^^^^
			//
			// acquire fields linked to job execution
			//
			field = "ete"                 ; if (dct.contains(field)) Attrs::acquire( exec_time , &dct[field] ) ;
			field = "force"               ; if (dct.contains(field)) Attrs::acquire( force     , &dct[field] ) ;
			field = "is_python"           ; if (dct.contains(field)) Attrs::acquire( is_python , &dct[field] ) ; else throw "not found"s ;
			field = "max_retries_on_lost" ; if (dct.contains(field)) Attrs::acquire( n_losts   , &dct[field] ) ;
			field = "max_submits"         ; if (dct.contains(field)) Attrs::acquire( n_submits , &dct[field] ) ;
			//
			/**/                                            var_idxs["targets"        ] = { VarCmd::Targets                              , 0  } ;
			for( VarIdx mi : iota<VarIdx>(matches.size()) ) var_idxs[matches[mi].first] = { mi<n_statics?VarCmd::Match:VarCmd::StarMatch , mi } ;
			//
			field = "deps" ;
			if (dct.contains("deps_attrs")) deps_attrs = { dct["deps_attrs"].as_a<Tuple>() , var_idxs , self } ;
			//
			/**/                                                        var_idxs["deps"                       ] = { VarCmd::Deps , 0 } ;
			for( VarIdx d : iota<VarIdx>(deps_attrs.spec.deps.size()) ) var_idxs[deps_attrs.spec.deps[d].first] = { VarCmd::Dep  , d } ;
			//
			field = "submit_rsrcs_attrs" ; if (dct.contains(field)) submit_rsrcs_attrs = { dct[field].as_a<Tuple>() , var_idxs } ;
			field = "submit_none_attrs"  ; if (dct.contains(field)) submit_none_attrs  = { dct[field].as_a<Tuple>() , var_idxs } ;
			//
			/**/                                                                 var_idxs["resources"                           ] = { VarCmd::Rsrcs , 0 } ;
			for( VarIdx r : iota<VarIdx>(submit_rsrcs_attrs.spec.rsrcs.size()) ) var_idxs[submit_rsrcs_attrs.spec.rsrcs[r].first] = { VarCmd::Rsrc  , r } ;
			//
			field = "start_cmd_attrs"   ; if (dct.contains(field)) start_cmd_attrs   = { dct[field].as_a<Tuple>() , var_idxs        } ;
			field = "start_rsrcs_attrs" ; if (dct.contains(field)) start_rsrcs_attrs = { dct[field].as_a<Tuple>() , var_idxs        } ;
			field = "start_none_attrs"  ; if (dct.contains(field)) start_none_attrs  = { dct[field].as_a<Tuple>() , var_idxs        } ;
			field = "cmd"               ; if (dct.contains(field)) cmd               = { dct[field].as_a<Tuple>() , var_idxs , self } ; else throw "not found"s ;
			//
			for( VarIdx mi : iota(n_static_targets) ) {
				if (matches[mi].first!="<stdout>") continue ;
				stdout_idx = mi ;
				break ;
			}
			for( VarIdx di : iota<VarIdx>(deps_attrs.spec.deps.size()) ) {
				if (deps_attrs.spec.deps[di].first!="<stdin>") continue ;
				stdin_idx = di ;
				break ;
			}
		}
		catch(::string const& e) { throw "while processing "+full_name()+'.'+field+" :\n"+indent(e) ; }
	}

	TargetPattern RuleData::_mk_pattern( MatchEntry const& me , bool for_name ) const {
		// Generate and compile Python pattern
		// target has the same syntax as Python f-strings except expressions must be named as found in stems
		// we transform that into a pattern by :
		// - escape specials outside keys
		// - transform f-string syntax into Python regexpr syntax
		// for example "a{b}c.d" with stems["b"]==".*" becomes "a(.*)c\.d"
		TargetPattern res       ;
		VarIdx        cur_group = 1 ;
		res.groups = ::vector<uint32_t>(stems.size()) ;
		res.txt    = _subst_target(
			me.pattern
		,	[&](VarIdx s)->::string {
				if ( s>=n_static_stems && for_name ) {
					::string const& k = stems[s].first ;
					if (k.front()=='<'&&k.back()=='>' ) return Re::escape("{*}"     ) ; // when matching on job name, star stems are matched as they are reported to user
					else                                return Re::escape('{'+k+"*}") ; // .
				}
				if (res.groups[s]) return "(?:\\"s+res.groups[s]+')' ;                  // already seen, we must protect against following text potentially containing numbers
				bool capture = s<n_static_stems || me.ref_cnts[s]>1 ;                   // star stems are only captured if back referenced
				if (capture) res.groups[s] = cur_group ;
				cur_group += capture+stem_mark_cnts[s] ;
				return (capture?"(":"(?:")+stems[s].second+')' ;
			}
		,	Escape::Re
		) ;
		res.re = res.txt ;                                                              // stem regexprs have been validated, normally there is no error here
		return res ;
	}

	void RuleData::new_job_report( Delay exec_time , CoarseDelay cost , Tokens1 tokens1 ) const {
		if (stats_weight<RuleWeight) stats_weight++ ;
		//
		Delay::Tick cost_per_token_delta = Delay(cost).val()/(tokens1+1) - cost_per_token.val() ;
		Delay::Tick exec_time_delta      = exec_time  .val()             - exec_time     .val() ;
		int64_t     tokens1_32_delta     = (uint64_t(tokens1)<<32)       - tokens1_32           ;
		//
		cost_per_token += Delay(New,cost_per_token_delta/stats_weight) ;
		exec_time      += Delay(New,exec_time_delta     /stats_weight) ;
		tokens1_32     +=           tokens1_32_delta    /stats_weight  ;
	}

	CoarseDelay RuleData::cost() const {                          // compute cost_per_tokens * tokens, but takes care of the details
		uint64_t    t_16   = (tokens1_32>>16)+(uint64_t(1)<<16) ;
		Delay::Tick cpt_16 = cost_per_token.val()>>16           ;
		return Delay(New,t_16*cpt_16) ;
	}

	void RuleData::_compile() {
		try {
			// job_name & targets
			MatchEntry job_name_match_entry ; job_name_match_entry.set_pattern(job_name,stems.size()) ;
			job_name_pattern = _mk_pattern(job_name_match_entry,true /*for_name*/)  ;
			for( auto const& [k,me] : matches ) patterns.push_back(_mk_pattern(me,false/*for_name*/ )) ;
			//
			deps_attrs        .compile() ;
			submit_rsrcs_attrs.compile() ;
			submit_none_attrs .compile() ;
			start_cmd_attrs   .compile() ;
			start_rsrcs_attrs .compile() ;
			start_none_attrs  .compile() ;
			cmd               .compile() ;
		} catch (::string const& e) {
			throw "while processing "+full_name()+" :\n"+indent(e) ;
		}
	}

	//
	// pretty print RuleData
	//

	template<class T> static ::string _pretty_vmap( ::string const& title , ::vmap_s<T> const& m , bool uniq=false ) {
		if (!m) return {} ;
		::string res  ;
		size_t   wk   = 0 ; for( auto const& [k,_] : m ) wk = ::max(wk,k.size()) ;
		::uset_s keys ;
		//
		res << title <<'\n' ;
		for( auto const& [k,v] : m ) if ( !uniq || keys.insert(k).second ) {
			if (+v) res <<'\t'<< widen(k,wk) <<" : "<< v <<'\n' ;
			else    res <<'\t'<< widen(k,wk) <<" :"      <<'\n' ;
		}
		return res ;
	}

	::string RuleData::_pretty_env() const {
		::string res ;
		for( auto const& [h,m] : ::vmap_s<::vmap_ss>({ {"environ",start_cmd_attrs.spec.env} , {"environ_resources",start_rsrcs_attrs.spec.env} , {"environ_ancillary",start_none_attrs.spec.env} }) ) {
			if (!m) continue ;
			size_t wk = 0 ; for( auto const& [k,_] : m ) wk = ::max(wk,k.size()) ;
			res << h <<" :\n" ;
			for( auto const& [k,v] : m ) {
				/**/                     res <<'\t'<< widen(k,wk) ;
				if      (v==EnvPassMrkr) res << "   ..."          ;
				else if (v==EnvDynMrkr ) res << "   <dynamic>"    ;
				else if (+v            ) res << " : "<< v         ;
				else                     res << " :"              ;
				/**/                     res <<'\n'               ;
			}
		}
		return res ;
	}

	static ::string _pretty_views( ::string const& title , ::vmap_s<JobSpace::ViewDescr> const& m ) {
		if (!m) return {} ;
		::string res  ;
		res << title <<'\n' ;
		for( auto const& [k,v] : m ) {
			res <<'\t'<< k <<" :" ;
			SWEAR(+v.phys) ;
			if (v.phys.size()==1) {
				SWEAR(!v.copy_up) ;
				res <<' '<< v.phys[0] ;
			} else {
				size_t w = +v.copy_up ? 7 : 5 ;
				/**/            res <<"\n\t\t" << widen("upper"  ,w) <<" : "<< v.phys[0]                          ;
				/**/            res <<"\n\t\t" << widen("lower"  ,w) <<" : "<< ::span(&v.phys[1],v.phys.size()-1) ;
				if (+v.copy_up) res <<"\n\t\t" << widen("copy_up",w) <<" : "<< v.copy_up                          ;
			}
			res <<'\n' ;
		}
		return res ;
	}

	::string RuleData::_pretty_fstr(::string const& fstr) const {
			::string res ;
			for( size_t ci=0 ; ci<fstr.size() ; ci++ ) { // /!\ not a iota
				switch (fstr[ci]) {
					case Rule::StemMrkr : {
						VarCmd vc = decode_enum<VarCmd>(&fstr[ci+1]) ; ci += sizeof(VarCmd) ;
						VarIdx i  = decode_int <VarIdx>(&fstr[ci+1]) ; ci += sizeof(VarIdx) ;
						res += '{' ;
						switch (vc) {
							case VarCmd::Stem      : res += stems                        [i].first ; break ;
							case VarCmd::StarMatch :
							case VarCmd::Match     : res += matches                      [i].first ; break ;
							case VarCmd::Dep       : res += deps_attrs.spec.deps         [i].first ; break ;
							case VarCmd::Rsrc      : res += submit_rsrcs_attrs.spec.rsrcs[i].first ; break ;
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

	::string RuleData::_pretty_matches() const {
		auto kind = [&](RuleData::MatchEntry const& me)->::string_view {
			return snake(
				me.flags.is_target==No           ? MatchKind::SideDep
			:	me.flags.tflags()[Tflag::Target] ? MatchKind::Target
			:	                                   MatchKind::SideTarget
			) ;
		} ;
		size_t    w1        = 0 ;
		size_t    w2        = 0 ;
		size_t    w3        = 0 ;
		::umap_ss patterns_ ;
		//
		for( auto const& [k,me] : matches ) {
			::string p = _subst_target(
				me.pattern
			,	[&](VarIdx s)->::string { return '{' + stems[s].first + (s<n_static_stems?"":"*") + '}' ; }
			,	Escape::Fstr
			) ;
			w1           = ::max(w1,kind(me).size()) ;
			w2           = ::max(w2,k       .size()) ;
			w3           = ::max(w3,p       .size()) ;
			patterns_[k] = ::move(p)                 ;
		}
		//
		::string res = "matches :\n" ;
		//
		for( VarIdx mi : iota<VarIdx>(matches.size()) ) {
			::string             const& k     = matches[mi].first  ;
			RuleData::MatchEntry const& me    = matches[mi].second ;
			::string                    flags ;
			//
			bool first = true ;
			if (me.flags.is_target==No) {
				for( Dflag df : iota(Dflag::NRule) ) {
					if (!me.flags.dflags()[df]) continue ;
					flags << (first?" : ":" , ") << df ;
					first = false ;
				}
				for( ExtraDflag edf : iota(ExtraDflag::NRule) ) {
					if (!me.flags.extra_dflags()[edf]) continue ;
					flags << (first?" : ":" , ") << edf ;
					first = false ;
				}
			} else {
				for( Tflag tf : iota(Tflag::NRule) ) {
					if (!me.flags.tflags()[tf]) continue ;
					flags << (first?" : ":" , ") << tf ;
					first = false ;
				}
				for( ExtraTflag etf : iota(ExtraTflag::NRule) ) {
					if (!me.flags.extra_tflags()[etf]) continue ;
					flags << (first?" : ":" , ") << etf ;
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
			/**/        res <<'\t'<< widen(cat(kind(me)),w1)<<' '<<widen(k,w2)<<" : " ;
			if (+flags) res << widen(patterns_[k],w3) << flags                        ;
			else        res <<       patterns_[k]                                     ;
			/**/        res <<'\n'                                                    ;
		}
		// report exceptions (i.e. sub-repos in which rule does not apply) unless it can be proved we cannot match in such sub-repos
		::vector_s excepts_s ;
		::uset_s   seens_s   ;                                                                        // we are only interested in first level sub-repos under our sub-repo
		for( ::string const& sr_s : g_config->sub_repos_s ) {
			if (!( sr_s.size()>sub_repo_s.size() && sr_s.starts_with(sub_repo_s) )) continue ;        // if considered sub-repo is not within our sub-repo, it cannot match
			for( ::string const& e_s : seens_s )
				if (sr_s.starts_with(e_s)) goto Skip ;                                                // g_config->sub_repos_s are sorted so that higher level occurs first
			seens_s.insert(sr_s) ;
			for( auto const& [k,me] : matches ) {                                                     // if all targets have a prefix that excludes considered sub-repo, it cannot match
				if (!( me.flags.is_target==Yes && me.flags.tflags()[Tflag::Target] )) continue ;      // not a target
				::string_view pfx = substr_view( me.pattern , 0 , me.pattern.find(Rule::StemMrkr) ) ; // find target prefix
				if (sr_s.starts_with(pfx )) goto Report ;                                             // found a target that may      match in sub-repo, include it
				if (pfx .starts_with(sr_s)) goto Report ;                                             // found a target that may only match in sub-repo, include it
			}
		Skip :
			continue ;
		Report :
			excepts_s.push_back(sr_s) ;
		}
		if (+excepts_s) {
			/**/                                  res << "except in sub-repos :\n"  ;
			for( ::string const& e_s : excepts_s) res <<'\t'<< no_slash(e_s) <<'\n' ;
		}
		// report actual reg-exprs to ease debugging
		res << "patterns :\n" ;
		for( size_t mi : iota(matches.size()) )
			res <<'\t'<<
				/**/     widen(cat(kind(matches[mi].second)),w1)
			<<	' '   << widen(         matches[mi].first   ,w2)
			<<	" : " <<       patterns[mi].txt
			<<'\n' ;
		return res ;
	}

	::string RuleData::_pretty_deps() const {
		size_t    wk       = 0 ;
		size_t    wd       = 0 ;
		::umap_ss patterns ;
		//
		for( auto const& [k,ds] : deps_attrs.spec.deps ) {
			if (!ds.txt) continue ;
			::string p = _pretty_fstr(ds.txt) ;
			wk          = ::max(wk,k.size()) ;
			wd          = ::max(wd,p.size()) ;
			patterns[k] = ::move(p)          ;
		}
		if (!patterns) return {} ;
		//
		::string res = "deps :\n" ;
		for( auto const& [k,ds] : deps_attrs.spec.deps ) {
			if (!ds.txt) continue ;
			::string flags ;
			bool     first = true ;
			for( Dflag      df  : iota(Dflag     ::NRule) ) if (ds.dflags      [df ]) { flags += first?" : ":" , " ; first = false ; flags += df  ; }
			for( ExtraDflag edf : iota(ExtraDflag::NRule) ) if (ds.extra_dflags[edf]) { flags += first?" : ":" , " ; first = false ; flags += edf ; }
			/**/        res <<'\t'<< widen(k,wk) <<" : "      ;
			if (+flags) res << widen(patterns[k],wd) << flags ;
			else        res <<       patterns[k]              ;
			/**/        res <<'\n' ;
		}
		return res ;
	}

	template<class T> ::string RuleData::_pretty_dyn(Dynamic<T> const& d) const {
		if (!d.code_str) return {} ;
		::string res ;
		/**/                                        res << T::Msg <<" :\n"                                                                 ;
		if (+d.glbs_str)                            res << "\t<dynamic globals> :\n" << ensure_nl(indent(d.append_dbg_info(d.glbs_str),2)) ;
		if (+d.ctx     )                            res << "\t<context> :"                                                                 ;
		for( ::string const& k : _list_ctx(d.ctx) ) res << ' '<<k                                                                          ;
		if (+d.ctx     )                            res << '\n'                                                                            ;
		if (+d.code_str)                            res << "\t<dynamic code> :\n" << ensure_nl(indent(d.code_str,2))                       ;
		return res ;
	}

	::string RuleData::pretty_str() const {
		::string  title       ;
		::vmap_ss entries     ;
		::string  job_name_   = job_name ;
		::string  interpreter ;
		::string  kill_sigs   ;
		::string  cmd_        ;
		//
		{	title = full_name() + " :" + (special==Special::Anti?" AntiRule":special==Special::GenericSrc?" SourceRule":"") ;
			for( auto const& [k,me] : matches ) if (job_name_==me.pattern) { job_name_ = "<targets."+k+'>' ; break ; }
		}
		if (!is_special()) {
			{	First first ;
				for( ::string const& c : start_cmd_attrs.spec.interpreter ) interpreter<<first(""," ")<<c ;
			}
			{	First           first ;
				::uset<uint8_t> seen  ;
				for( uint8_t sig : start_none_attrs.spec.kill_sigs ) {
					kill_sigs << first(""," , ") ;
					if (!sig) continue ;
					kill_sigs << int(sig) ;
					if (seen.insert(sig).second) kill_sigs << '('<<::strsignal(sig)<<')' ;
				}
			}
			if (+cmd.spec.cmd) cmd_ = is_python ? cmd.append_dbg_info(cmd.spec.cmd) : _pretty_fstr(cmd.spec.cmd) ;
		}
		//
		// first simple static attrs
		{	if ( user_prio!=0                                      ) entries.emplace_back( "prio"                , cat        (user_prio                                      ) ) ;
			/**/                                                     entries.emplace_back( "job_name"            ,             job_name_                                        ) ;
			if (+sub_repo_s                                        ) entries.emplace_back( "sub_repo"            , no_slash   (sub_repo_s                                     ) ) ;
		}
		if (!is_special()) {
			if ( force                                             ) entries.emplace_back( "force"               , cat        (force                                          ) ) ;
			if ( n_losts                                           ) entries.emplace_back( "max_retries_on_lost" , ::to_string(n_losts                                        ) ) ;
			if ( n_submits                                         ) entries.emplace_back( "max_submits"         , ::to_string(n_submits                                      ) ) ;
			if ( submit_rsrcs_attrs.spec.backend!=BackendTag::Local) entries.emplace_back( "backend"             , snake      (submit_rsrcs_attrs.spec.backend                ) ) ;
			if (+submit_none_attrs .spec.cache                     ) entries.emplace_back( "cache"               ,             submit_none_attrs .spec.cache                  ) ;
			if (+interpreter                                       ) entries.emplace_back( "interpreter"         ,             interpreter                                      ) ;
			if ( start_cmd_attrs   .spec.allow_stderr              ) entries.emplace_back( "allow_stderr"        , cat        (start_cmd_attrs   .spec.allow_stderr           ) ) ;
			if ( start_cmd_attrs   .spec.auto_mkdir                ) entries.emplace_back( "auto_mkdir"          , cat        (start_cmd_attrs   .spec.auto_mkdir             ) ) ;
			if (+start_cmd_attrs   .spec.job_space.chroot_dir_s    ) entries.emplace_back( "chroot_dir"          , no_slash   (start_cmd_attrs   .spec.job_space.chroot_dir_s ) ) ;
			if (+start_cmd_attrs   .spec.job_space.lmake_view_s    ) entries.emplace_back( "lmake_view"          , no_slash   (start_cmd_attrs   .spec.job_space.lmake_view_s ) ) ;
			if (+start_cmd_attrs   .spec.job_space.repo_view_s     ) entries.emplace_back( "repo_view"           , no_slash   (start_cmd_attrs   .spec.job_space.repo_view_s  ) ) ;
			if (+start_cmd_attrs   .spec.job_space.tmp_view_s      ) entries.emplace_back( "tmp_view"            , no_slash   (start_cmd_attrs   .spec.job_space.tmp_view_s   ) ) ;
			/**/                                                     entries.emplace_back( "autodep"             , snake      (start_rsrcs_attrs .spec.method                 ) ) ;
			if (+start_rsrcs_attrs .spec.timeout                   ) entries.emplace_back( "timeout"             ,             start_rsrcs_attrs .spec.timeout.short_str()      ) ;
			if ( start_rsrcs_attrs .spec.use_script                ) entries.emplace_back( "use_script"          , cat        (start_rsrcs_attrs .spec.use_script             ) ) ;
			if ( start_none_attrs  .spec.keep_tmp                  ) entries.emplace_back( "keep_tmp"            , cat        (start_none_attrs  .spec.keep_tmp               ) ) ;
			if (+start_none_attrs  .spec.start_delay               ) entries.emplace_back( "start_delay"         ,             start_none_attrs  .spec.start_delay.short_str()  ) ;
			if (+start_none_attrs  .spec.kill_sigs                 ) entries.emplace_back( "kill_sigs"           ,             kill_sigs                                        ) ;
			if ( start_none_attrs  .spec.max_stderr_len            ) entries.emplace_back( "max_stderr_len"      , ::to_string(start_none_attrs  .spec.max_stderr_len         ) ) ;
			if ( start_none_attrs  .spec.z_lvl                     ) entries.emplace_back( "compression"         , ::to_string(start_none_attrs  .spec.z_lvl                  ) ) ;
		}
		::string res = _pretty_vmap( title , entries ) ;
		//
		// then composite static attrs
		{	res << indent( _pretty_vmap   ("stems :"  ,stems,true/*uniq*/                       ) , 1 ) ;
			res << indent( _pretty_matches(                                                     ) , 1 ) ;
		}
		if (!is_special()) {
			res << indent( _pretty_deps   (                                                     ) , 1 ) ;
			res << indent( _pretty_vmap   ("resources :",submit_rsrcs_attrs.spec.rsrcs          ) , 1 ) ;
			res << indent( _pretty_views  ("views :"    ,start_cmd_attrs   .spec.job_space.views) , 1 ) ;
			res << indent( _pretty_env    (                                                     ) , 1 ) ;
		}
		// then dynamic part
		if (!is_special()) {
			res << indent( _pretty_dyn(deps_attrs        ) , 1 ) ;
			res << indent( _pretty_dyn(submit_rsrcs_attrs) , 1 ) ;
			res << indent( _pretty_dyn(submit_none_attrs ) , 1 ) ;
			res << indent( _pretty_dyn(start_cmd_attrs   ) , 1 ) ;
			res << indent( _pretty_dyn(start_rsrcs_attrs ) , 1 ) ;
			res << indent( _pretty_dyn(start_none_attrs  ) , 1 ) ;
			res << indent( _pretty_dyn(cmd               ) , 1 ) ;
		}
		// and finally the cmd
		if (!is_special()) {
			if (+cmd.spec.cmd) res << indent("cmd :\n",1) << indent(ensure_nl(cmd_),2) ;
		}
		return res ;
	}

	::vector_s RuleData::_list_ctx(::vector<CmdIdx> const& ctx) const {
		::vector_s res ; res.reserve(ctx.size()) ;
		for( auto [vc,i] : ctx ) switch (vc) {
			case VarCmd::Stem      : res.push_back(stems                        [i].first) ; break ;
			case VarCmd::StarMatch :
			case VarCmd::Match     : res.push_back(matches                      [i].first) ; break ;
			case VarCmd::Dep       : res.push_back(deps_attrs.spec.deps         [i].first) ; break ;
			case VarCmd::Rsrc      : res.push_back(submit_rsrcs_attrs.spec.rsrcs[i].first) ; break ;
			case VarCmd::Stems     : res.push_back("stems"                               ) ; break ;
			case VarCmd::Targets   : res.push_back("targets"                             ) ; break ;
			case VarCmd::Deps      : res.push_back("deps"                                ) ; break ;
			case VarCmd::Rsrcs     : res.push_back("resources"                           ) ; break ;
		DF}
		return res ;
	}

	// START_OF_VERSIONING
	// crc->match is an id of the rule : a new rule is a replacement of an old rule if it has the same crc->match
	// also, 2 rules matching identically is forbidden : the idea is that one is useless
	// this is not strictly true, though : you could imagine a rule generating a* from b, another generating a* from b but with disjoint sets of a*
	// although awkward & useless (as both rules could be merged), this can be meaningful
	// if the need arises, we will add an "id" artificial field entering in crc->match to distinguish them
	void RuleData::_set_crcs() {
		Hash::Xxh h ;                                                                            // each crc continues after the previous one, so they are standalone
		//
		::vmap_s<bool> targets ;
		for( auto const& [k,me] : matches )
			if ( me.flags.is_target==Yes && me.flags.tflags()[Tflag::Target] )
				targets.emplace_back(me.pattern,me.flags.extra_tflags()[ExtraTflag::Optional]) ; // keys and flags have no influence on matching, except Optional
		h.update(special) ;
		h.update(stems  ) ;
		h.update(targets) ;
		if (is_special()) {
			h.update(allow_ext) ;                                                                // only exists for special rules
		} else {
			h.update(job_name)        ;                                                          // job_name has no effect for source & anti as it is only used to store jobs and there are none
			deps_attrs.update_hash(h) ;                                                          // no deps for source & anti
		}
		Crc match = h.digest() ;
		//
		if (is_special()) {
			crc = {match} ;
		} else {
			h.update(sub_repo_s            ) ;
			h.update(Node::s_src_dirs_crc()) ;                                                   // src_dirs influences deps recording
			h.update(matches               ) ;                                                   // these define names and influence cmd execution, all is not necessary but simpler to code
			h.update(force                 ) ;
			h.update(is_python             ) ;
			start_cmd_attrs.update_hash(h)   ;
			cmd            .update_hash(h)   ;
			Crc cmd = h.digest() ;
			//
			submit_rsrcs_attrs.update_hash(h) ;
			start_rsrcs_attrs .update_hash(h) ;
			Crc rsrcs = h.digest() ;
			//
			crc = { match , cmd , rsrcs } ;
		}
	}
	// END_OF_VERSIONING

	//
	// RuleCrcData
	//

	::string& operator+=( ::string& os , RuleCrcData const& rcd ) {
		return os << "RCD(" << rcd.rule <<','<< rcd.state <<','<< rcd.match <<','<< rcd.cmd <<','<< rcd.rsrcs << ')' ;
	}

	//
	// Rule::RuleMatch
	//

	Rule::RuleMatch::RuleMatch(Job job) : rule{job->rule()} {
		::string name_ = job->full_name() ;
		//
		rule->validate(name_) ;
		//
		char* p = &name_[name_.size()-rule->job_sfx_len()+1] ;          // start of suffix, after JobMrkr
		for( [[maybe_unused]] VarIdx _ : iota(rule->n_static_stems) ) {
			FileNameIdx pos = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			FileNameIdx sz  = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			stems.push_back(name_.substr(pos,sz)) ;
		}
	}

	Rule::RuleMatch::RuleMatch( Rule r , TargetPattern const& pattern , ::string const& name , bool chk_psfx ) {
		Trace trace("RuleMatch",r,name,STR(chk_psfx)) ;
		/**/                                         if (!r) { trace("no_rule" ) ; return ; }
		Re::Match m = pattern.match(name,chk_psfx) ; if (!m) { trace("no_match") ; return ; }
		rule = r ;
		for( VarIdx s : iota(r->n_static_stems) ) stems.push_back(::string(m[pattern.groups[s]])) ;
		trace("stems",stems) ;
	}

	Rule::RuleMatch::RuleMatch( RuleTgt rt , ::string const& target , bool chk_psfx ) : RuleMatch{rt->rule,rt.pattern(),target,chk_psfx} {
		if (!self) return ;
		for( VarIdx t : rt.matches().conflicts ) {
			if (!rule->patterns[t].match(target,true/*chk_psfx*/)) continue ;
			rule .clear() ;
			stems.clear() ;
			Trace("RuleMatch","conflict",rt.tgt_idx,t) ;
			return ;
		}
	}

	::string& operator+=( ::string& os , Rule::RuleMatch const& m ) {
		os << "RSM(" << m.rule << ',' << m.stems << ')' ;
		return os ;
	}

	::uset<Node> Rule::RuleMatch::target_dirs() const {
		::uset<Node> dirs ;
		for( auto const& [k,me] : rule->matches ) {
			if (me.flags.is_target!=Yes) continue ;
			::string target = _subst_target(
				me.pattern
			,	[&](VarIdx s)->::string { return stems[s] ; }
			,	Escape::None
			,	rule->n_static_stems/*stop_above*/
			) ;
			if ( size_t sep=target.rfind('/') ; sep!=Npos ) {
				target.resize(sep)        ;
				dirs.insert(Node(target)) ;
			}
		}
		return dirs ;
	}

	::vector_s Rule::RuleMatch::star_patterns() const {
		::vector_s res ; res.reserve(rule->matches.size()-rule->n_statics) ;
		for( VarIdx t : iota<VarIdx>( rule->n_statics , rule->matches.size() ) ) {
			size_t                      cur_group = 1                       ;
			::vector<uint32_t>          groups    ( rule->stems.size() )    ;   // used to set back references
			RuleData::MatchEntry const& me        = rule->matches[t].second ;
			res.push_back(_subst_target(
				me.pattern
			,	[&](VarIdx s)->::string {
					if (s<rule->n_static_stems) return Re::escape(stems[s])   ;
					if (groups[s]             ) return "(?:\\"s+groups[s]+')' ; // enclose in () to protect against following text potentially containing numbers
					bool capture = me.ref_cnts[s]>1 ;
					if (capture) groups[s] = cur_group ;
					cur_group += capture+rule->stem_mark_cnts[s] ;
					return (capture?"(":"(?:")+rule->stems[s].second+')' ;
				}
			,	Escape::Re
			)) ;
		}
		return res ;
	}

	::vector_s Rule::RuleMatch::py_matches() const {
		::vector_s res = static_targets() ;
		for( VarIdx mi : iota<VarIdx>( rule->n_statics , rule->matches.size() ) ) {
			::uset<VarIdx> seen ;
			res.push_back(_subst_target(
				rule->matches[mi].second.pattern
			,	[&](VarIdx s)->::string {
					if (s<rule->n_static_stems) return Re::escape(stems[s]) ;
					::pair_ss const& stem = rule->stems[s] ;
					if      ( !seen.insert(s).second                            ) return "(?P="+stem.first                +')' ;
					else if ( stem.first.front()=='<' && stem.first.back()=='>' ) return '('   +               stem.second+')' ; // stem is unnamed
					else                                                          return "(?P<"+stem.first+'>'+stem.second+')' ;
				}
			,	Escape::Re
			)) ;
		}
		return res ;
	}

	::vector_s Rule::RuleMatch::static_targets() const {
		::vector_s res ; res.reserve(rule->n_statics) ;
		for( VarIdx mi : iota(rule->n_statics) ) {
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

	::vector_s Rule::RuleMatch::star_targets() const {
		::vector_s res ; res.reserve(rule->matches.size()-rule->n_statics) ;
		for( VarIdx mi : iota( rule->n_statics , rule->matches.size() ) ) {
			res.push_back(_subst_target(
				rule->matches[mi].second.pattern
			,	[&](VarIdx s)->::string {
					if (s<rule->n_static_stems) return stems[s]                      ;
					else                        return "{"+rule->stems[s].first+"*}" ;
				}
			)) ;
		}
		return res ;
	}

	::pair_ss Rule::RuleMatch::full_name() const {
		::vector<FileNameIdx> poss(rule->n_static_stems) ;
		::string name = _subst_target( rule->job_name ,
			[&]( FileNameIdx p , VarIdx s ) -> ::string {
				if (s<rule->n_static_stems) {
					poss[s] = p ;
					return stems[s] ;
				}
				::string const& key = rule->stems[s].first ;
				if ( key.front()=='<' && key.back()=='>' ) return "{*}"        ;
				else                                       return '{'+key+"*}" ;
			}
		) ;
		::string sfx = rule->job_sfx() ;                                                     // provides room for stems, but we have to fill it
		size_t   i   = 1               ;                                                     // skip initial JobMrkr
		for( VarIdx s : iota(rule->n_static_stems) ) {
			encode_int<FileNameIdx>( &sfx[i] , poss [s]        ) ; i+= sizeof(FileNameIdx) ;
			encode_int<FileNameIdx>( &sfx[i] , stems[s].size() ) ; i+= sizeof(FileNameIdx) ; // /!\ beware of selecting encode_int of the right size
		}
		return {name,sfx} ;
	}

}
