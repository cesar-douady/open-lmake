// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"      // /!\ must be first to include Python.h first
#include "serialize.hh"

enum class StarAction : uint8_t {
	None
,	Stop
,	Err
} ;

namespace Engine {

	using namespace Disk ;
	using namespace Py   ;
	using namespace Time ;

	static ::string/*msg*/ _reject_msg( MatchKind mk , ::string const& file , bool has_pfx=false , bool has_sfx=false ) {
		if ( !has_pfx && !has_sfx && file=="." ) {
			if (mk!=MatchKind::SideDep) return "is top-level" ;
		} else if ( !has_pfx && !has_sfx && !file ) {
			return "is empty" ;
		} else if ( !is_canon( file , mk==MatchKind::SideDep/*ext_ok*/ , true/*empty_ok*/ , has_pfx , has_sfx ) ) {
			if ( ::string c=mk_canon(file) ; c!=file ) return cat(file," is not canonical, consider using : ",c) ;
			else                                       return cat(file," is not canonical"                     ) ;
		} else if ( !has_sfx && file.back()=='/' ) {
			if ( ::string ns=no_slash(file) ; ns!=file ) return cat(file," ends with /, consider using : ",ns) ;
			else                                         return cat(file," ends with /"                      ) ;
		}
		//
		return {} ; // ok
	}

	using ParseTargetFunc = ::function<void( FileNameIdx pos , VarIdx stem )> ;

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

	static void _mk_flags( ::string const& key , Sequence const& py_seq , uint8_t n_skip , MatchFlags& flags , bool dep_only ) {
		for( Object const& item : py_seq ) {
			if (n_skip>0) { n_skip-- ; continue ; }
			if (item.is_a<Str>()) {
				::string flag_str = item.as_a<Str>() ;
				bool     neg      = flag_str[0]=='-' ;
				if (neg) flag_str.erase(0,1) ;                        // suppress initial - sign
				//
				if      ( Tflag      f ; !dep_only && can_mk_enum<Tflag     >(flag_str) && (f=mk_enum<Tflag     >(flag_str))<Tflag     ::NRule ) flags.tflags      .set(f,!neg) ;
				else if ( ExtraTflag f ; !dep_only && can_mk_enum<ExtraTflag>(flag_str) && (f=mk_enum<ExtraTflag>(flag_str))<ExtraTflag::NRule ) flags.extra_tflags.set(f,!neg) ;
				else if ( Dflag      f ;              can_mk_enum<Dflag     >(flag_str) && (f=mk_enum<Dflag     >(flag_str))<Dflag     ::NRule ) flags.dflags      .set(f,!neg) ;
				else if ( ExtraDflag f ;              can_mk_enum<ExtraDflag>(flag_str) && (f=mk_enum<ExtraDflag>(flag_str))<ExtraDflag::NRule ) flags.extra_dflags.set(f,!neg) ;
				else                                                                                                                             throw "unexpected flag "+flag_str+" for "+key ;
			} else if (item.is_a<Sequence>()) {
				_mk_flags( key , item.as_a<Sequence>() , 0 , flags , dep_only ) ;
			} else {
				throw key+"has a flag that is not a str" ;
			}
		}
	}
	static ::string _split_flags( ::string const& key , Object const& py , uint8_t n_skip , MatchFlags& flags , bool dep_only ) {
		if (py.is_a<Str>()) {
			SWEAR(n_skip==1) ;                                        // cannot skip 2 values with a single Str
			return py.as_a<Str>() ;
		}
		Sequence const* py_seq ;
		try                       { py_seq = &py.as_a<Sequence>() ; }
		catch (::string const& e) { throw e+" nor a str" ;          } // e is a type error
		SWEAR(py_seq->size()>=n_skip,key) ;
		SWEAR((*py_seq)[0].is_a<Str>(),key) ;
		_mk_flags( key , *py_seq , n_skip , flags , dep_only ) ;
		return (*py_seq)[0].as_a<Str>() ;
	}

	::string _subst_fstr( ::string const& fstr , ::umap_s<CmdIdx> const& var_idxs , VarIdx& n_unnamed , bool* /*out*/ keep_for_deps=nullptr ) {
		::string res ;
		//
		if (keep_for_deps) *keep_for_deps = true ;                                                  // unless found to be external
		parse_py( fstr , nullptr/*unnamed_star_idx*/ ,
			[&]( ::string const& k , bool star , bool unnamed , ::string const* def )->void {
				SWEAR(var_idxs.contains(k)) ;
				SWEAR(!star               ) ;
				SWEAR(!def                ) ;
				size_t sz = res.size() ;
				res.resize(sz+1+sizeof(VarCmd)+sizeof(VarIdx)) ;
				char* p = res.data()+sz    ;
				auto it = var_idxs.find(k) ;
				p[0] = Rule::StemMrkr ;
				encode_enum( p+1                , it->second.bucket ) ;
				encode_int ( p+1+sizeof(VarCmd) , it->second.idx    ) ;
				n_unnamed += unnamed ;
			}
		,	[&]( ::string const& fixed , bool has_pfx , bool has_sfx )->void {
				SWEAR(+fixed) ;
				res.append(fixed) ;
				if (!keep_for_deps) return ;                                                        // not a dep, no check
				if ( !is_canon( fixed , true/*ext_ok*/ , true/*empty_ok*/ , has_pfx , has_sfx ) ) {
					if ( ::string c=mk_canon(fstr) ; c!=fstr ) throw cat("is not canonical, consider using : ",c) ;
					else                                       throw cat("is not canonical"                     ) ;
				}
				if ( !has_sfx && fixed.back()=='/' ) {
					if ( ::string ns=no_slash(fstr) ; ns!=fstr ) throw cat("is ends with /, consider using : ",ns) ;
					else                                         throw cat("is ends with /"                      ) ;
				}
				if (has_pfx      ) return ;                                   // further check only for prefix
				if (is_lcl(fixed)) return ;
				//
				// dep is non-local, check if it lies within a source dirs
				*keep_for_deps = false ;                                      // unless found in a source dir
				if (!*g_src_dirs_s) return ;                                  // fast path : no need to compute rel/abs versions
				//
				::string dir_s     = fixed.substr(0,fixed.rfind('/')+1) ;
				::string rel_fixed = mk_rel(fixed,*g_repo_root_s)       ;
				::string abs_fixed = mk_abs(fixed,*g_repo_root_s)       ;
				::string rel_dir_s = mk_rel(dir_s,*g_repo_root_s)       ;
				::string abs_dir_s = mk_abs(dir_s,*g_repo_root_s)       ;
				if (is_lcl_s(rel_dir_s)) throw "must be provided as local file, consider : "+rel_dir_s+substr_view(fstr,dir_s.size()) ;
				//
				for( ::string const& sd_s : *g_src_dirs_s ) {
					if (is_lcl_s(sd_s)) continue ;                            // nothing to recognize inside repo
					bool            abs_sd = is_abs_s(sd_s)                 ;
					::string const& f_s    = abs_sd ? abs_fixed : rel_fixed ;
					::string const& d_s    = abs_sd ? abs_dir_s : rel_dir_s ;
					if (!( has_sfx && sd_s.starts_with(f_s) )) {              // we only have a prefix, could lie in this source dir when complemented if we have the right initial part
						bool inside = d_s.starts_with(sd_s) ;
						if ( !abs_sd && sd_s.ends_with("../") && (sd_s.size()==3||sd_s[sd_s.size()-4]=='/') ) { // sd_s is entirely composed of .. components (as it is canonical)
							inside = !inside ;                                                                  // criteria is reversed when dealing with ..'s
							if (!inside) {
								const char* p = &d_s[sd_s.size()] ;
								inside = !( p[0]=='.' && p[1]=='.' && (p[2]=='/'||p[2]==0) ) ;
							}
						}
						if ( !inside                  ) continue ;                                              // dont keep dep because of this source dir if not inside it
						if (  is_abs(fstr) && !abs_sd ) throw "must be relative inside source dir "+no_slash(sd_s)+", consider : "+mk_rel(fstr,*g_repo_root_s) ;
						if ( !is_abs(fstr) &&  abs_sd ) throw "must be absolute inside source dir "+no_slash(sd_s)+", consider : "+mk_abs(fstr,*g_repo_root_s) ;
					}
					*keep_for_deps = true ;
					break ;
				}
			}
		) ;
		return res ;
	}

	//
	// Dyn
	//

	::string& operator+=( ::string& os , DepSpec const& ds ) {                            // START_OF_NO_COV
		return os <<"DepSpec("<< ds.txt <<','<< ds.dflags <<','<< ds.extra_dflags <<')' ;
	}                                                                                     // END_OF_NO_COV

	DynEntry::DynEntry( RulesBase const& rules , Bool3 is_python , Dict const& py_src , ::umap_s<CmdIdx> const& var_idxs , bool compile_ ) {
		switch (is_python) {
			case Yes :
				kind = Kind::PythonCmd ;
				if (py_src.contains("glbs"    )) glbs_str = ::string(py_src["glbs"    ].as_a<Str>()) ;
				/**/                             code_str = ::string(py_src["expr"    ].as_a<Str>()) ;
				if (py_src.contains("dbg_info")) dbg_info = ::string(py_src["dbg_info"].as_a<Str>()) ;
			break ;
			case No :
				if (py_src.contains("static")) {
					kind = Kind::ShellCmd ;
					VarIdx n_unnamed = 0 ;
					code_str = _subst_fstr( ::string(py_src["static"].as_a<Dict>()["cmd"].as_a<Str>()) , var_idxs , n_unnamed ) ;
					SWEAR( !n_unnamed , n_unnamed ) ;
					break ;
				}
			[[fallthrough]] ;           // dynamic shell cmd, process as dynamic attributes
			case Maybe :
				if (!py_src.contains("expr")) {
					kind = Kind::None ;
					break ;
				}
				/**/                               code_str = ::string(py_src["expr"    ].as_a<Str>()) ;
				if (py_src.contains("glbs"      )) glbs_str = ::string(py_src["glbs"    ].as_a<Str>()) ;
				if (py_src.contains("dbg_info"  )) dbg_info = ::string(py_src["dbg_info"].as_a<Str>()) ;
				if (py_src.contains("may_import"))
					for( Object const& py_k : py_src["may_import"].as_a<Sequence>() )
						may_import |= mk_enum<DynImport>(py_k.as_a<Str>()) ;
				kind = Kind::Dyn ;
				if (compile_) compile(rules) ;
			break ;
		DF}                             // NO_COV
		if (py_src.contains("names")) {
			ctx.reserve(py_src["names"].as_a<Sequence>().size()) ;
			for( Object const& py_item : py_src["names"].as_a<Sequence>() ) {
				CmdIdx ci = var_idxs.at(py_item.as_a<Str>()) ;
				ctx.push_back(ci) ;
			}
			::sort(ctx) ;               // stabilize crc's
		}
	}

	void DynEntry::compile( RulesBase const& rules ) {
		if (kind!=Kind::Dyn) return ;
		// keep str info to allow decompilation    for_eval
		try { code      = { code_str               , true  }                    ; } catch (::string const& e) { throw "cannot compile code :\n"   +indent(e) ; }
		try { glbs_code = { glbs_str+'\n'+dbg_info , false }                    ; } catch (::string const& e) { throw "cannot compile context :\n"+indent(e) ; }
		try { glbs      = glbs_code->run( nullptr/*glbs*/ , rules.py_sys_path ) ; } catch (::string const& e) { throw "cannot execute context :\n"+indent(e) ; }
		kind = Kind::CompiledGlbs ;
	}
	void DynEntry::decompile() {
		SWEAR(kind==Kind::CompiledGlbs) ;
		kind      = Kind::Dyn ;
		code      = {} ;
		glbs_code = {} ;
		glbs      = {} ;
	}

	DynBase::DynBase( Ptr<>* py_update , RulesBase& rules , Dict const& py_src , ::umap_s<CmdIdx> const& var_idxs , Bool3 is_python ) {
		Ptr<> cmd_update ;
		if (is_python!=Maybe) {
			SWEAR( !py_update , is_python ) ;                                                             // no report for Cmd as static info is stored in DynEntry
			py_update = &cmd_update ;
		}
		DynEntry de { rules , is_python , py_src , var_idxs , false/*compile*/ } ;
		if ( de.kind==DynKind::Dyn && py_update && !de.ctx ) {                                            // dynamic value depends on nothing, we can make it static
			SWEAR(is_python!=Yes) ;                                                                       // python cmd cannot be dynamic
			de.compile(rules) ;
			::vmap_s<DepDigest> deps ;
			{	AutodepLock lock { &deps } ;
				try                       { *py_update = de.code->eval( de.glbs , rules.py_sys_path ) ; } // tell caller what to do in py_update
				catch (::string const& e) { if (!deps) throw ;                                          } // if we do disk accesses, dont care about errors, we are going to decompile anyway
			}
			if (+deps) {
				de.decompile() ;                                                                          // if doing disk accesses, we must record deps and we cannot precompile
			} else {
				de = {} ;                                                                                 // synthetize a static entry
				if (is_python!=Maybe) {                                                                   // unless for ShellCmd, there is no dynamic entry at all
					de.kind     = DynKind::ShellCmd                   ;
					de.code_str = ::string((*py_update)->as_a<Str>()) ;                                   // note that f-string is not interpolated as this is already done during dynamic eval
				}
			}
		}
		if (!de) {                                                                                        // value is static
			return ;
		}
		auto it_inserted = rules.dyn_idx_tab.try_emplace(de,0) ;
		if (it_inserted.second) {
			de.compile(rules) ;
			rules.dyn_vec.emplace_back(::move(de)) ;
			it_inserted.first->second = rules.dyn_vec.size() ;                                            // dyn_idx 0 is used to mean non-dynamic
		}
		dyn_idx = it_inserted.first->second ;
	}

	void DynBase::s_eval( Job j , Rule::RuleMatch& m/*lazy*/ , ::vmap_ss const& rsrcs_ , ::vector<CmdIdx> const& ctx , EvalCtxFuncStr const& cb_str , EvalCtxFuncDct const& cb_dct ) {
		RuleData const&  rd                  = *( +j ? j->rule() : m.rule )                        ;
		::vmap_ss const& rsrcs_spec          = rd.submit_rsrcs_attrs.spec.rsrcs                    ;
		::vector_s       mtab                ;
		::vmap_ss        dtab                ;
		::umap_ss        rtab                ;
		Iota2<VarIdx>    static_targets_iota = rd.matches_iotas[false/*star*/][+MatchKind::Target] ;
		//
		auto match = [&]()->Rule::RuleMatch const& { { if (!m) m = Rule::RuleMatch(j) ; } return m             ; } ;                                                            // solve lazy evaluation
		auto stems = [&]()->::vector_s      const& {                                      return match().stems ; } ;
		//
		auto matches = [&]()->::vector_s const& { { if (!mtab) for( ::string const& t      : match().py_matches() ) mtab.push_back   (  mk_lcl(t     ,rd.sub_repo_s)) ; } return mtab ; } ;
		auto deps    = [&]()->::vmap_ss  const& { { if (!dtab) for( auto     const& [k,dn] : match().deps_holes() ) dtab.emplace_back(k,mk_lcl(dn.txt,rd.sub_repo_s)) ; } return dtab ; } ;
		auto rsrcs   = [&]()->::umap_ss  const& { { if (!rtab) rtab = mk_umap(rsrcs_) ;                                                                                 } return rtab ; } ;
		for( auto [vc,i] : ctx ) {
			::vmap_ss dct ;
			switch (vc) {
				case VarCmd::Stem      :                                                                        cb_str(vc,i,rd.stems  [i].first,stems  ()[i]       ) ;   break ;
				case VarCmd::StarMatch :
				case VarCmd::Match     :                                                                        cb_str(vc,i,rd.matches[i].first,matches()[i]       ) ;   break ;
				case VarCmd::Dep       :                                                                        cb_str(vc,i,deps()    [i].first,deps   ()[i].second) ;   break ;
				case VarCmd::Rsrc      : { auto it = rsrcs().find(rsrcs_spec[i].first) ; if (it!=rsrcs().end()) cb_str(vc,i,it->first          ,it->second         ) ; } break ;
				//
				case VarCmd::Stems   : for( VarIdx j : iota(rd.n_static_stems) ) dct.emplace_back(rd.stems  [j].first,stems  ()[j]) ; cb_dct(vc,i,"stems"    ,dct   ) ; break ;
				case VarCmd::Targets : for( VarIdx j : static_targets_iota     ) dct.emplace_back(rd.matches[j].first,matches()[j]) ; cb_dct(vc,i,"targets"  ,dct   ) ; break ;
				case VarCmd::Deps    : for( auto const& [k,d] : deps()         ) dct.emplace_back(k                  ,d           ) ; cb_dct(vc,i,"deps"     ,dct   ) ; break ;
				case VarCmd::Rsrcs   :                                                                                                cb_dct(vc,i,"resources",rsrcs_) ; break ;
			DF}                                                                                                                                                                 // NO_COV
		}
	}

	::string DynBase::s_parse_fstr( ::string const& fstr , Job job , Rule::RuleMatch& match , ::vmap_ss const& rsrcs ) {
		::vector<CmdIdx> ctx_  ;
		::vector_s       fixed { 1 } ;
		size_t           fi    = 0   ;
		for( size_t ci=0 ; ci<fstr.size() ; ci++ ) {                                                                              // /!\ not a iota
			if (fstr[ci]==Rule::StemMrkr) {
				VarCmd vc = decode_enum<VarCmd>(&fstr[ci+1]) ; ci += sizeof(VarCmd) ;
				VarIdx i  = decode_int <VarIdx>(&fstr[ci+1]) ; ci += sizeof(VarIdx) ;
				ctx_ .push_back   (CmdIdx{vc,i}) ;
				fixed.emplace_back(            ) ;
				fi++ ;
			} else {
				fixed[fi] += fstr[ci] ;
			}
		}
		fi = 0 ;
		::string res = ::move(fixed[fi++]) ;
		auto cb_str = [&]( VarCmd , VarIdx , string const& /*key*/ , string  const&   val   )->void { res<<val<<fixed[fi++] ; } ;
		auto cb_dct = [&]( VarCmd , VarIdx , string const& /*key*/ , vmap_ss const& /*val*/ )->void { FAIL()                ; } ; // NO_COV
		DynBase::s_eval(job,match,rsrcs,ctx_,cb_str,cb_dct) ;
		return res ;
	}

	//
	// Rule
	//

	Atomic<Pdate> Rule::s_last_dyn_date ;
	Job           Rule::s_last_dyn_job  ;
	const char*   Rule::s_last_dyn_msg  = nullptr ;

	::string& operator+=( ::string& os , Rule const r ) { // START_OF_NO_COV
		/**/    os << "R(" ;
		if (+r) os << +r   ;
		return  os << ')'  ;
	}                                                     // END_OF_NO_COV

	bool/*keep*/ Rule::s_qualify_dep( ::string const& key , ::string const& dep ) {
		auto bad = [&] ( ::string const& msg , ::string const& consider={} )->void {
			::string e = cat("dep ",key,' ',msg,+dep?" : ":"",dep) ;
			if ( +consider && consider!=dep ) e <<", consider : "<< consider ;
			throw e ;
		} ;
		if (!dep                         ) bad( "is empty"                         ) ;
		if (!is_canon(dep,true/*ext_ok*/)) bad( "is not canonical" , mk_canon(dep) ) ;
		if (dep.back()=='/'              ) bad( "ends with /"      , no_slash(dep) ) ;
		if (is_lcl(dep)                  ) return true/*keep*/ ;
		// dep is non-local, substitute relative/absolute if it lies within a source dirs
		::string rel_dep = mk_rel( dep , *g_repo_root_s ) ;
		::string abs_dep = mk_abs( dep , *g_repo_root_s ) ;
		if (is_lcl_s(rel_dep)) bad( "must be provided as local file" , rel_dep ) ;
		//
		for( ::string const& sd_s : *g_src_dirs_s ) {
			if (is_lcl_s(sd_s)) continue ;                                                                    // nothing to recognize inside repo
			bool            abs_sd = is_abs_s(sd_s)             ;
			::string const& d      = abs_sd ? abs_dep : rel_dep ;
			bool            inside = d.starts_with(sd_s)        ;
			if ( !abs_sd && sd_s.ends_with("../") && (sd_s.size()==3||sd_s[sd_s.size()-4]=='/') ) {           // sd_s is entirely composed of .. components (as it is canonical)
				inside = !inside ;                                                                            // criteria is reversed when dealing with ..'s
				if (!inside) {
					const char* p = &d[sd_s.size()] ;
					inside = !( p[0]=='.' && p[1]=='.' && (p[2]=='/'||p[2]==0) ) ;
				}
			} else {
				inside = d.starts_with(sd_s) ;
			}
			if ( !inside                 ) continue ;                                                         // dont keep dep because of this source dir if not inside it
			if (  is_abs(dep) && !abs_sd ) bad( cat("must be relative inside source dir ",sd_s) , rel_dep ) ;
			if ( !is_abs(dep) &&  abs_sd ) bad( cat("must be absolute inside source dir ",sd_s) , abs_dep ) ;
			return true/*keep*/ ;
		}
		if ( key=="python" || key=="shell" ) return false/*keep*/ ;                                           // accept external dep for interpreter (but ignore it)
		bad( "is outside repo and all source dirs" , "suppress dep" ) ;
		return false/*garbage*/ ;                                                                             // NO_COV, to please compiler
	}

	//
	// RuleCrc
	//

	::string& operator+=( ::string& os , RuleCrc const r ) { // START_OF_NO_COV
		/**/    os << "RC(" ;
		if (+r) os << +r   ;
		return  os << ')'  ;
	}                                                        // END_OF_NO_COV

	//
	// RuleTgt
	//

	::string& operator+=( ::string& os , RuleTgt const rt ) {              // START_OF_NO_COV
		return os << "RT(" << RuleCrc(rt) <<':'<< int(rt.tgt_idx) << ')' ;
	}                                                                      // END_OF_NO_COV

	//
	// Attrs
	//

	namespace Attrs {

		bool/*updated*/ acquire( bool& dst , Object const* py_src ) {
			//                                    updated
			if (!py_src       )             return false ;
			if ( py_src==&None) { if (!dst) return false ; dst = false ; return true/*updated*/ ; }
			//
			dst = +*py_src ;
			return true/*updated*/ ;
		}

		bool/*updated*/ acquire( Delay& dst , Object const* py_src , Delay min , Delay max ) {
			//                                    updated
			if (!py_src       )             return false ;
			if ( py_src==&None) { if (!dst) return false ; dst = {} ; return true/*updated*/ ; }
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
			//                                                 updated
			if (!py_src       )                          return false ;
			if ( py_src==&None) { dst = {.is_dyn=true} ; return true  ; }
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

	}

	//
	// DepsAttrs
	//

	void DepsAttrs::init( Dict const& py_src , ::umap_s<CmdIdx> const& var_idxs , RuleData const& rd ) {
		SWEAR(!dyn_deps) ;                                                                               // init should not be called in that case
		//
		for( auto const& [py_key,py_val] : py_src.as_a<Dict>() ) {
			::string key = py_key.template as_a<Str>() ;
			if (py_val==None) {
				deps.emplace_back(key,DepSpec()) ;
				continue ;
			}
			VarIdx     n_unnamed = 0                                                                          ;
			MatchFlags mfs       = { .dflags=DflagsDfltStatic , .extra_dflags=ExtraDflagsDfltStatic }         ;
			::string   dep       = _split_flags( "dep "+key , py_val , 1/*n_skip*/ , mfs , true/*dep_only*/ ) ;
			dep  = rd.add_cwd( ::move(dep) , mfs.extra_dflags[ExtraDflag::Top] ) ;
			try {
				bool     keep       = false/*garbage*/                                                          ;
				::string parsed_dep = _subst_fstr( dep , var_idxs , n_unnamed , /*out*/&keep/*keep_for_deps*/ ) ;
				if (!keep) {
					if ( key=="python" || key=="shell" ) continue             ;                          // accept external dep for interpreter (but ignore it)
					else                                 throw "is external"s ;
				}
				//
				if (n_unnamed) {
					for( auto const& [k,ci] : var_idxs ) if (ci.bucket==VarCmd::Stem) n_unnamed-- ;
					throw_unless( !n_unnamed , "contains some but not all unnamed static stems" ) ;
				}
				deps.emplace_back( key , DepSpec{ ::move(parsed_dep) , mfs.dflags , mfs.extra_dflags } ) ;
			} catch (::string const& e) {
				throw cat("dep ",key," (",dep,") ",e) ;
			}
		}
		throw_unless( deps.size()<Rule::NoVar-1 , "too many static deps : ",deps.size() ) ;              // -1 to leave some room to the interpreter, if any
	}

	::pair_s</*msg*/::vmap_s<DepSpec>> DynDepsAttrs::eval(Rule::RuleMatch const& match) const {
		try {
			::pair_s<::vmap_s<DepSpec>> msg_ds    ;
			::string         &          msg       = msg_ds.first  ;
			::vmap_s<DepSpec>&          dep_specs = msg_ds.second ;
			for( auto const& [k,ds] : spec.deps ) {
				dep_specs.emplace_back( k , DepSpec{ {} , ds.dflags , ds.extra_dflags } ) ; // create an entry for each dep so that indexes stored in other attributes (e.g. cmd) are correct
				if (!ds.txt) continue ;                                                     // entry is dynamic
				::string  d   = s_parse_fstr(ds.txt,match)  ;
				::string& txt = dep_specs.back().second.txt ;
				try                      { if (Rule::s_qualify_dep(k,d)) txt = ::move(d) ; }
				catch(::string const& e) { if (!msg                    ) msg = e         ; }
			}
			//
			if (is_dyn()) {
				Gil   gil    ;
				Ptr<> py_obj = _eval_code(match) ;
				//
				::map_s<VarIdx> dep_idxs ;
				if (!spec.dyn_deps)
					for( VarIdx di : iota<VarIdx>(spec.deps.size()) ) dep_idxs[spec.deps[di].first] = di ;
				if (*py_obj!=None)
					for( auto const& [py_key,py_val] : py_obj->as_a<Dict>() ) {
						if (py_val==None) continue ;
						::string key = py_key.as_a<Str>() ;
						try {
							MatchFlags mfs = { .dflags=DflagsDfltStatic , .extra_dflags=ExtraDflagsDfltStatic }         ;
							::string   dep = _split_flags( "dep "+key , py_val , 1/*n_skip*/ , mfs , true/*dep_only*/ ) ;
							dep = match.rule->add_cwd( ::move(dep) , mfs.extra_dflags[ExtraDflag::Top] ) ;
							DepSpec ds { dep , mfs.dflags , mfs.extra_dflags } ;
							try {
								if (!Rule::s_qualify_dep(key,ds.txt)) continue ;
								if (spec.dyn_deps) {
									dep_specs.emplace_back( key , ::move(ds) ) ;
								} else {
									DepSpec& ds2 = dep_specs[dep_idxs.at(key)].second ;
									SWEAR(!ds2.txt) ;                                       // dep cannot be both static and dynamic
									ds2 = ::move(ds) ;                                      // if not full dyn, all deps must be listed in spec
								}
							} catch(::string const& e) {
								if (!msg) msg = e ;
							}
						} catch (::string const& e) {
							throw cat("while processing dep ",key," : ",e) ;
						}
					}
			}
			//
			return msg_ds ;
		} catch (::string const& e) {                                                       // convention here is to report (msg,sterr), if we have a single string, it is from us and it is a msg
			throw ::pair(e,""s) ;
		}
	}

	::vmap_s<DepSpec>  DynDepsAttrs::dep_specs(Rule::RuleMatch const& m) const {
		::pair_s</*msg*/::vmap_s<DepSpec>> digest = eval(m) ;
		if (+digest.first) throw digest.first ;
		::vmap_s<DepSpec>& dep_specs = digest.second ;
		NodeIdx            i         = 0             ;
		for( NodeIdx j : iota(dep_specs.size()) ) {
			if (!dep_specs[j].second.txt) continue ;                            // filter holes out
			if (i!=j                    ) dep_specs[i] = ::move(dep_specs[j]) ;
			i++ ;
		}
		dep_specs.resize(i) ;
		return ::move(dep_specs) ;
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
		::string   expr = subst_target(
			matches[i].second.pattern
		,	[&](VarIdx s)->::string {
				bool first = seen.insert(s).second ;
				::string k = stems[s].first        ;
				if ( k.front()=='<' and k.back()=='>' )   k = k.substr(1,k.size()-2) ;
				if ( s>=r->n_static_stems             ) { if (first) args.push_back(k)      ; return '{'+k+'}'                  ; }
				else                                    { if (!m   ) m = Rule::RuleMatch(j) ; return py_fstr_escape(m.stems[s]) ; } // solve lazy m
			}
		,	py_fstr_escape
		) ;
		::string res   = "def "+key+'(' ;
		First    first ;
		for( ::string const& a : args ) res <<first("",",")<<' '<<a<<' ' ;
		res << ") : return f"<<mk_py_str(expr)      <<'\n' ;
		res << key<<".reg_expr = "<<mk_py_str(val ) <<'\n' ;
		return res ;
	}

	::string DynCmd::eval( bool&/*inout*/ use_script , Rule::RuleMatch const& match , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps , StartCmdAttrs const& start_cmd_attrs ) const {
		Rule     r   = match.rule ; // if we have no job, we must have a match as job is there to lazy evaluate match if necessary
		::string res ;
		// if script is large (roughly >64k), force use_script to ensure reasonable debug experience and no Linux resources overrun (max 2M for script+env if not use_script)
		if (!r->is_python) {
			if (!is_dyn()) {
				res = s_parse_fstr( entry().code_str , match , rsrcs ) ;
			} else {
				Gil   gil    ;
				Ptr<> py_obj = _eval_code( match , rsrcs , deps ) ;
				throw_unless( +py_obj->is_a<Str>() , "type error : ",py_obj->type_name()," is not a str" ) ;
				Attrs::acquire( res , &py_obj->as_a<Str>() ) ;
			}
			if (res.size()>1<<16) use_script = true ;
		} else {
			if ( entry().glbs_str.size() + entry().code_str.size() > 1<<16 ) use_script = true ;
			if ( use_script                                                ) res << "import sys ; sys.path[0] = '' ; del sys\n#\n" ; // ensure sys.path is the same as if run with -c, ...
			res << "lmake_root = " << mk_py_str(no_slash(start_cmd_attrs.job_space.lmake_view_s|*g_lmake_root_s)) <<'\n' ;           // ... del sys to ensure total transparency
			res << "repo_root  = " << mk_py_str(no_slash(start_cmd_attrs.job_space.repo_view_s |*g_repo_root_s )) <<'\n' ;
			res << '#'                                                                                            <<'\n' ;
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
			res <<"#\n"<< entry().glbs_str <<"#\n"<< entry().dbg_info <<"#\n"<< entry().code_str ;
		}
		return res ;
	}

	//
	// RuleData
	//

	void RuleData::MatchEntry::set_pattern( ::string&& p , VarIdx n_stems ) {
		::uset<VarIdx> seen ;
		pattern = ::move(p) ;
		captures.resize(n_stems) ; for( bool c : captures ) SWEAR(!c) ; // captures is being initialized
		_parse_target( pattern , [&](VarIdx s)->::string {
			if (!seen.insert(s).second) captures[s] = true ;            // stem must always be captured for back-reference if seen several times
			return {} ;
		} ) ;
	}

	static void _append_stem( ::string& target , VarIdx stem_idx ) {
		::string s ; s.resize(sizeof(VarIdx)) ;
		encode_int( s.data() , stem_idx ) ;
		target += Rule::StemMrkr ;
		target += s              ;
	}

	RuleData::RuleData( Special s , ::string const& src_dir_s ) : special{s} , name{snake(s)} {
		SWEAR(+s) ;
		//
		if (s<Special::NShared) SWEAR( !src_dir_s                           , s , src_dir_s ) ; // shared rules cannot have parameters as, precisely, they are shared
		else                    SWEAR( +src_dir_s && is_dir_name(src_dir_s) , s , src_dir_s ) ; // ensure source dir ends with a /
		switch (s) {
			case Special::Req          : force = true ; break ;
			case Special::InfiniteDep  :
			case Special::InfinitePath :                break ;
			case Special::GenericSrc :
				name           = "source dir" ;
				job_name       = src_dir_s    ; _append_stem(job_name,0) ;
				force          = true         ;
				allow_ext      = true         ;                                                 // sources may lie outside repo
				n_static_stems = 1            ;
				stems       .emplace_back("",".*"                ) ;
				stem_n_marks.push_back   (0                      ) ;
				matches     .emplace_back("",MatchEntry{job_name}) ;
				matches_iotas[false/*star*/][+MatchKind::Target] = {0,1} ;
			break ;
		DF}                                                                                     // NO_COV
		_set_crcs({}) ;                                                                         // rules is not necessary for special rules
	}

	::string& operator+=( ::string& os , RuleData const& rd ) { // START_OF_NO_COV
		return os << "RD(" << rd.name << ')' ;
	}                                                           // END_OF_NO_COV

	void RuleData::_acquire_py( RulesBase& rules , Dict const& dct ) {
		::string field ;
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
			::map_s<Bool3> stem_stars ; // ordered so that stems are ordered, Maybe means stem is used both as static and star
			field = "stems" ;
			if (dct.contains(field))
				for( auto const& [py_k,py_v] : dct[field].as_a<Dict>() )
					stem_defs.emplace( ::string(py_k.as_a<Str>()) , ::string(py_v.as_a<Str>()) ) ;
			//
			// augment stems with definitions found in job_name and targets
			size_t unnamed_star_idx = 1 ;                                                                             // free running while walking over job_name + targets
			auto augment_stems = [&]( ::string const& k , bool star , ::string const* re , bool star_only ) -> void {
				if (re) {
					auto [it,inserted] = stem_defs.emplace(k,*re) ;
					throw_unless( +inserted || *re==it->second , "2 different definitions for stem ",k," : ",it->second," and ",*re ) ;
				}
				if ( !star_only || star ) {
					auto [it,inserted] = stem_stars.emplace(k,No|star) ;
					if ( !inserted && (No|star)!=it->second ) it->second = Maybe ;                                    // stem is used both as static and star
				}
			} ;
			field = "job_name" ;
			throw_unless( dct.contains(field) , "not found" ) ;
			job_name = dct[field].as_a<Str>() ;
			parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> void { augment_stems(k,star,re,false/*star_only*/) ; }
			) ;
			field = "matches" ;
			throw_unless( dct.contains(field) , "not found" ) ;
			::string  job_name_key  ;
			MatchKind job_name_kind ;
			for( auto const& [py_k,py_tkfs] : dct[field].as_a<Dict>() ) {
				field = py_k.as_a<Str>() ;
				::string  target =                    py_tkfs.as_a<Sequence>()[0].as_a<Str>()  ;                      // .
				MatchKind kind   = mk_enum<MatchKind>(py_tkfs.as_a<Sequence>()[1].as_a<Str>()) ;                      // targets are a tuple (target_pattern,kind,flags...)
				// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
				if (target!=job_name) {
					parse_py( target , &unnamed_star_idx ,
						// static stems are declared in job_name, but error will be caught later on, when we can generate a sound message
						[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> void {
							augment_stems(k,star,re,true/*star_only*/) ;
						}
					) ;
				} else if (!job_name_key) {
					job_name_key  = field ;
					job_name_kind = kind  ;
				}
			}
			//
			// gather job_name and targets
			field            = "job_name" ;
			unnamed_star_idx = 1          ;                                                                           // reset free running at each pass over job_name+targets
			VarIdx n_static_unnamed_stems = 0     ;
			bool   job_name_is_star       = false ;
			auto   stem_words             = []( ::string const& k , bool star , bool unnamed ) -> ::string {
				const char* stem = star ? "star stem" : "stem" ;
				return unnamed ? cat("unnamed ",stem) : cat(stem,' ',k) ;
			} ;
			parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) -> void {
					if      (!stem_defs.contains(k)) throw cat("found undefined ",stem_words(k,star,unnamed)," in ",job_name_kind,' ',job_name_key) ;
					if      (star                  ) job_name_is_star = true ;
					else if (unnamed               ) n_static_unnamed_stems++ ;
				}
			) ;
			//
			field = "matches" ;
			{	::vmap_s<MatchEntry> star_matches  [N<MatchKind>] ;                                                   // defer star matches so that static targets are put first
				::vmap_s<MatchEntry> static_matches[N<MatchKind>] ;                                                   // .
				bool                 seen_top                     = false ;
				bool                 seen_target                  = false ;
				for( auto const& [py_k,py_tkfs] : dct[field].as_a<Dict>() ) {                                         // targets are a tuple (target_pattern,flags...)
					field = py_k.as_a<Str>() ;
					Sequence const& pyseq_tkfs    = py_tkfs.as_a<Sequence>()                      ;
					::string        target        =                    pyseq_tkfs[0].as_a<Str>()  ;                   // .
					MatchKind       kind          = mk_enum<MatchKind>(pyseq_tkfs[1].as_a<Str>()) ;                   // targets are a tuple (target_pattern,kind,flags...)
					bool            is_star       = false                                         ;
					::set_s         missing_stems ;
					bool            is_stdout     = field=="target"                               ;
					MatchFlags      flags         ;
					// ignore side_targets and side_deps for source and anti-rules
					// this is meaningless, but may be inherited for stems, typically as a PyRule
					if ( kind!=MatchKind::Target && is_special() ) continue ;
					// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
					if (target==job_name) {
						if (job_name_is_star) is_star = true ;
					} else {
						if (kind==MatchKind::Target) for( auto const& [k,s] : stem_stars ) if (s!=Yes) missing_stems.insert(k) ;
						parse_py( target , &unnamed_star_idx ,
							[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) -> void {
								if (!stem_defs.contains(k)) throw "found undefined "+stem_words(k,star,unnamed)+" in "+kind ;
								//
								if (star) {
									is_star = true ;
									return ;
								}
								auto it = stem_stars.find(k) ;
								throw_unless(
									it!=stem_stars.end() && it->second!=Yes
								,	stem_words(k,star,unnamed)," appears in ",kind," but not in ",job_name_kind,' ',job_name_key,", consider using ",k,'*'
								) ;
								if (kind==MatchKind::Target) missing_stems.erase(k) ;
							}
						) ;
					}
					if (             kind==MatchKind::Target  ) flags.tflags       |= Tflag::Target      ;
					if ( !is_star && kind==MatchKind::Target  ) flags.tflags       |= Tflag::Essential   ;            // static targets are essential by default
					if ( !is_star                             ) flags.tflags       |= Tflag::Static      ;
					if (             kind!=MatchKind::SideDep ) flags.extra_tflags |= ExtraTflag::Allow  ;
					if ( !is_star                             ) flags.extra_dflags |= ExtraDflag::NoStar ;
					_split_flags( snake_str(kind) , pyseq_tkfs , 2/*n_skip*/ , flags , kind==MatchKind::SideDep ) ;
					// check
					if ( target.starts_with(*g_repo_root_s)                                        ) throw cat(snake_str(kind)," must be relative to root dir : "        ,target) ;
					if ( !is_lcl(target)                                                           ) throw cat(snake_str(kind)," must be local : "                       ,target) ;
					if ( +missing_stems                                                            ) throw cat("missing stems ",missing_stems," in ",kind," : "          ,target) ;
					if (  is_star                                    && is_special()               ) throw cat("star ",kind,"s are meaningless for source and anti-rules"       ) ;
					if (  is_star                                    && is_stdout                  ) throw     "stdout cannot be directed to a star target"s                      ;
					if ( flags.tflags      [Tflag     ::Incremental] && is_stdout                  ) throw     "stdout cannot be directed to an incremental target"s              ;
					if ( flags.extra_tflags[ExtraTflag::Optional   ] && is_star                    ) throw cat("star targets are natively optional : "                   ,target) ;
					if ( flags.extra_tflags[ExtraTflag::Optional   ] && flags.tflags[Tflag::Phony] ) throw cat("cannot be simultaneously optional and phony : "          ,target) ;
					bool is_top = flags.extra_tflags[ExtraTflag::Top] || flags.extra_dflags[ExtraDflag::Top] ;
					seen_top    |= is_top                  ;
					seen_target |= kind==MatchKind::Target ;
					// record
					/**/                     target   = add_cwd( ::move(target  ) , is_top ) ;
					if (field==job_name_key) job_name = add_cwd( ::move(job_name) , is_top ) ;
					(is_star?star_matches:static_matches)[+kind].emplace_back( field , MatchEntry{::move(target),flags} ) ;
				}
				SWEAR(+seen_target) ;                                                                                 // we should not have come up to here without a target
				if (!job_name_key) job_name = add_cwd( ::move(job_name) , seen_top ) ;
				SWEAR(+MatchKind::Target==0) ;                                                                        // targets (both static and star) must be first to ensure ...
				for( MatchKind k : iota(All<MatchKind>) ) {                                                           // ... RuleTgt stability when Rule's change without crc.match modif
					//            star
					matches_iotas[false][+k] = { VarIdx(matches.size()) , VarIdx(matches.size()+static_matches[+k].size()) } ; for( auto& st : static_matches[+k] ) matches.push_back(::move(st)) ;
					matches_iotas[true ][+k] = { VarIdx(matches.size()) , VarIdx(matches.size()+star_matches  [+k].size()) } ; for( auto& st : star_matches  [+k] ) matches.push_back(::move(st)) ;
				}
			}
			field.clear() ;
			throw_unless( matches.size()<NoVar , "too many targets, side_targets and side_deps ",matches.size()," >= ",int(NoVar) ) ;
			::umap_s<VarIdx> stem_idxs ;
			for( bool star : {false,true} ) {                                                                         // keep only useful stems and order them : static first, then star
				for( auto const& [k,v] : stem_stars ) {
					if (v==(No|!star)) continue ;                                                                     // stems that are both static and start appear twice
					::string const& s = stem_defs.at(k) ;
					stem_idxs.emplace     ( k+" *"[star] , VarIdx(stems.size()) ) ;
					stems    .emplace_back( k            , s                    ) ;
					try         { stem_n_marks.push_back(Re::RegExpr(s,true/*cache*/).n_marks()) ; }
					catch (...) { throw "bad regexpr for stem "+k+" : "+s ;                        }
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
			::string        mk_tgt       ;
			::string const* ensure_canon = nullptr ;
			MatchKind       kind         ;
			auto mk_fixed = [&]( ::string const& fixed , bool has_pfx , bool has_sfx )->void {
				SWEAR(+fixed) ;
				mk_tgt += fixed ;
				if (!ensure_canon) return ;
				::string msg = _reject_msg( kind , fixed , has_pfx , has_sfx ) ;
				throw_if( +msg , *ensure_canon , +*ensure_canon?" ":"" , msg ) ;
			} ;
			auto mk_stem = [&]( ::string const& key , bool star , bool /*unnamed*/ , ::string const* /*re*/ )->void {
				_append_stem(mk_tgt,stem_idxs.at(key+" *"[star])) ;
			} ;
			if (!job_name_key) {
				field        = "job_name"    ;
			} else {
				field        = job_name_key  ;
				ensure_canon = &job_name     ;
				kind         = job_name_kind ;
			}                                                                                                         // if job_name is a target, canon must be checked
			unnamed_star_idx = 1 ;                                                                                    // reset free running at each pass over job_name+targets
			mk_tgt.clear() ;
			parse_py( job_name , &unnamed_star_idx , mk_stem , mk_fixed ) ;
			::string new_job_name = ::move(mk_tgt) ;
			for( VarIdx mi : iota<VarIdx>(matches.size()) ) {
				/**/        field = matches[mi].first  ;
				MatchEntry& me    = matches[mi].second ;
				// avoid processing target if it is identical to job_name
				// this is not an optimization, it is to ensure unnamed_star_idx's match
				if (me.pattern==job_name) {
					me.set_pattern(new_job_name,stems.size()) ;
				} else {
					ensure_canon = &me.pattern     ;
					kind         = me.flags.kind() ;                                                                  // providing . as side_deps may be useful to pass readdir_ok flag
					mk_tgt.clear() ;
					parse_py( me.pattern , &unnamed_star_idx , mk_stem , mk_fixed ) ;
					me.set_pattern(::move(mk_tgt),stems.size()) ;
				}
			}
			field.clear() ;
			job_name = ::move(new_job_name) ;
			//
			//vvvvvvvvvvvvvvvvvvvvvvvv
			if (is_special()) return ;                                                                                // if special, we have no dep, no execution, we only need essential info
			//^^^^^^^^^^^^^^^^^^^^^^^^
			//
			// acquire fields linked to job execution
			//
			field = "ete"                 ; if (dct.contains(field)) Attrs::acquire( exec_time    , &dct[field] ) ;
			field = "force"               ; if (dct.contains(field)) Attrs::acquire( force        , &dct[field] ) ;
			field = "is_python"           ; if (dct.contains(field)) Attrs::acquire( is_python    , &dct[field] ) ; else throw "not found"s ;
			field = "max_retries_on_lost" ; if (dct.contains(field)) Attrs::acquire( n_losts      , &dct[field] ) ;
			field = "max_submits"         ; if (dct.contains(field)) Attrs::acquire( n_submits    , &dct[field] ) ;
			//
			var_idxs["targets"] = { VarCmd::Targets , 0 } ;
			for( bool star : {false,true} )
				for( MatchKind k : iota(All<MatchKind>) )
					for( VarIdx mi : matches_iotas[star][+k] )
						var_idxs[matches[mi].first] = { star?VarCmd::StarMatch:VarCmd::Match , mi } ;
			//
			field = "deps" ;
			if (dct.contains("deps_attrs")) deps_attrs = { rules , dct["deps_attrs"].as_a<Dict>() , var_idxs , self } ;
			//
			/**/                                                                                       var_idxs["deps"                       ] = { VarCmd::Deps , 0 } ;
			if (!deps_attrs.spec.dyn_deps) for( VarIdx d : iota<VarIdx>(deps_attrs.spec.deps.size()) ) var_idxs[deps_attrs.spec.deps[d].first] = { VarCmd::Dep  , d } ;
			//
			field = "submit_rsrcs_attrs"     ; if (dct.contains(field)) submit_rsrcs_attrs     = { rules , dct[field].as_a<Dict>() , var_idxs , self } ;
			field = "submit_ancillary_attrs" ; if (dct.contains(field)) submit_ancillary_attrs = { rules , dct[field].as_a<Dict>() , var_idxs , self } ;
			//
			/**/                                                                 var_idxs["resources"                           ] = { VarCmd::Rsrcs , 0 } ;
			for( VarIdx r : iota<VarIdx>(submit_rsrcs_attrs.spec.rsrcs.size()) ) var_idxs[submit_rsrcs_attrs.spec.rsrcs[r].first] = { VarCmd::Rsrc  , r } ;
			//
			field = "start_cmd_attrs"       ; if (dct.contains(field)) start_cmd_attrs       = { rules , dct[field].as_a<Dict>() , var_idxs , self } ;
			field = "start_rsrcs_attrs"     ; if (dct.contains(field)) start_rsrcs_attrs     = { rules , dct[field].as_a<Dict>() , var_idxs , self } ;
			field = "start_ancillary_attrs" ; if (dct.contains(field)) start_ancillary_attrs = { rules , dct[field].as_a<Dict>() , var_idxs , self } ;
			field = "cmd"                   ; if (dct.contains(field)) cmd                   = { rules , dct[field].as_a<Dict>() , var_idxs , self } ; else throw "not found"s ;
			//
			field.clear() ;
			//
			for( VarIdx mi : matches_iotas[false/*star*/][+MatchKind::Target] ) {
				if (matches[mi].first=="target") {
					stdout_idx = mi ;
					break ;
				}
			}
			if (!deps_attrs.spec.dyn_deps)
				for( VarIdx di : iota<VarIdx>(deps_attrs.spec.deps.size()) ) {
					if (deps_attrs.spec.deps[di].first!="dep") continue ;                                             // dep is a reserved key that means stdin
					stdin_idx = di ;
					break ;
				}
		}
		catch(::string const& e) {
			if (+field) throw "while processing "+user_name()+'.'+field+" :\n"+indent(e) ;
			else        throw "while processing "+user_name()+          " :\n"+indent(e) ;
		}
	}

	TargetPattern RuleData::_mk_pattern( MatchEntry const& me , bool for_name ) const {
		// Generate and compile python pattern
		// target has the same syntax as python f-strings except expressions must be named as found in stems
		// we transform that into a pattern by :
		// - escape specials outside keys
		// - transform f-string syntax into python regexpr syntax
		// for example "a{b}c.d" with stems["b"]==".*" becomes "a(.*)c\.d"
		TargetPattern res       ;
		VarIdx        cur_group = 1 ;
		Re::Pattern   pattern   ;
		res.groups = ::vector<uint32_t>(stems.size()) ;
		res.txt    = subst_target(
			me.pattern
		,	[&](VarIdx s)->::string {
				if ( s>=n_static_stems && for_name ) {
					::string const& k = stems[s].first ;
					// when matching on job name, star stems are matched as they are reported to user
					::string r = k.front()=='<'&&k.back()=='>' ? "{*}"s : cat('{',k,"*}") ;
					pattern.emplace_back(r,Maybe/*capture*/) ;
					return Re::escape(r) ;
				}
				if (res.groups[s]) {                                      // already seen, we must protect against following text potentially containing numbers
					::string r = cat('\\',res.groups[s]) ;
					pattern.emplace_back( r , No/*capture*/ ) ;
					return cat("(?:",r,')') ;
				}
				bool capture = s<n_static_stems || me.captures[s] ;       // star stems are only captured if back referenced
				if (capture) res.groups[s] = cur_group ;
				cur_group += capture+stem_n_marks[s] ;
				pattern.emplace_back( stems[s].second , No|capture ) ;
				return (capture?"(":"(?:")+stems[s].second+')' ;
			}
		,	[&](::string const& s)->::string {
				pattern.emplace_back( s , Maybe ) ;
				return Re::escape(s) ;
			}
		) ;
		res.re = {pattern,true/*cache*/} ;                                // stem regexprs have been validated, normally there is no error here
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

	void RuleData::compile() {
		Trace trace("compile",name) ;
		try {
			// job_name & targets
			MatchEntry job_name_match_entry ; job_name_match_entry.set_pattern(job_name,stems.size()) ;
			job_name_pattern = _mk_pattern(job_name_match_entry,true /*for_name*/)  ;
			for( auto const& [k,me] : matches ) patterns.push_back(_mk_pattern(me,false/*for_name*/ )) ;
			//
		} catch (::string const& e) {
			throw "while processing "+user_name()+" :\n"+indent(e) ;
		}
		trace("done",patterns.size()) ;
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
			res <<'\t'<< widen(k,wk) ;
			if (v==DynMrkr) res <<" <dynamic>" ;
			if (+v        ) res <<" : "<< v    ;
			else            res <<" :"         ;
			res <<'\n' ;
		}
		return res ;
	}

	::string RuleData::_pretty_env() const {
		::string res ;
		for( auto const& [h,m_d] : ::vmap_s<::pair<::vmap_ss,bool>>({
			{ "environ"           , {start_cmd_attrs      .spec.env,start_cmd_attrs      .spec.dyn_env} }
		,	{ "environ_resources" , {start_rsrcs_attrs    .spec.env,start_rsrcs_attrs    .spec.dyn_env} }
		,	{ "environ_ancillary" , {start_ancillary_attrs.spec.env,start_ancillary_attrs.spec.dyn_env} }
		}) ) {
			if ( m_d.second) { res <<" <dynamic>\n" ; continue ; }
			if (!m_d.first )                          continue ;
			size_t wk = 0 ; for( auto const& [k,_] : m_d.first ) wk = ::max(wk,k.size()) ;
			res << h <<" :\n" ;
			for( auto const& [k,v] : m_d.first ) {
				/**/                  res <<'\t'<< widen(k,wk) ;
				if      (v==PassMrkr) res << "   ..."          ;
				else if (v==DynMrkr ) res << "   <dynamic>"    ;
				else if (+v         ) res << " : "<< v         ;
				else                  res << " :"              ;
				/**/                  res <<'\n'               ;
			}
		}
		return res ;
	}

	::string RuleData::_pretty_views() const {
		::vmap_s<JobSpace::ViewDescr> const& m = start_cmd_attrs.spec.job_space.views ;
		if (start_cmd_attrs.spec.dyn_views) return "views <dynamic>\n" ;
		if (!m                            ) return {}                  ;
		::string res = "views :\n" ;
		for( auto const& [k,v] : m ) {
			res <<'\t'<< k ;
			if (v.is_dyn) { res << " <dynamic>\n" ; continue ; }
			res << " :" ;
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
			for( size_t ci=0 ; ci<fstr.size() ; ci++ ) {                                                     // /!\ not a iota
				switch (fstr[ci]) {
					case Rule::StemMrkr : {
						VarCmd vc = decode_enum<VarCmd>(&fstr[ci+1]) ; ci += sizeof(VarCmd) ;
						VarIdx i  = decode_int <VarIdx>(&fstr[ci+1]) ; ci += sizeof(VarIdx) ;
						res += '{' ;
						switch (vc) {
							case VarCmd::Stem      : res += stems                        [i].first ; break ;
							case VarCmd::StarMatch :
							case VarCmd::Match     : res += matches                      [i].first ; break ;
							case VarCmd::Dep       : res += deps_attrs        .spec.deps [i].first ; break ;
							case VarCmd::Rsrc      : res += submit_rsrcs_attrs.spec.rsrcs[i].first ; break ;
						DF}                                                                                  // NO_COV
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
		::string res ;
		size_t   wk  = 0 ;
		//
		for( MatchKind mk : iota(All<MatchKind>) ) {
			size_t     wk2       = 0 ;
			size_t     wp        = 0 ;
			::vector_s patterns_ ;
			//
			for( bool star : {false,true} )
				for( VarIdx mi : matches_iotas[star][+mk] ) {
					::string p = subst_target(
						matches[mi].second.pattern
					,	[&](VarIdx s)->::string { return '{' + stems[s].first + (s<n_static_stems?"":"*") + '}' ; }
					,	py_fstr_escape
					) ;
					wk2 = ::max(wk2,matches[mi].first.size()) ;
					wp  = ::max(wp ,p                .size()) ;
					patterns_.push_back(::move(p)) ;
				}
			wk = ::max(wk,wk2) ;
			::string matches_str ;
			VarIdx   i           = 0 ;
			for( bool star : {false,true} )
				for( VarIdx mi : matches_iotas[star][+mk] ) {
					MatchFlags mf        = matches[mi].second.flags ;
					::string   flags_str ;
					First      first     ;
					if (mk!=MatchKind::SideDep) for( Tflag      tf  : iota(Tflag     ::NRule) ) if (mf.tflags      [tf ]) flags_str << first(" : "," , ") << tf  ;
					if (mk!=MatchKind::SideDep) for( ExtraTflag etf : iota(ExtraTflag::NRule) ) if (mf.extra_tflags[etf]) flags_str << first(" : "," , ") << etf ;
					/**/                        for( Dflag      df  : iota(Dflag     ::NRule) ) if (mf.dflags      [df ]) flags_str << first(" : "," , ") << df  ;
					/**/                        for( ExtraDflag edf : iota(ExtraDflag::NRule) ) if (mf.extra_dflags[edf]) flags_str << first(" : "," , ") << edf ;
					/**/            matches_str <<'\t'<< widen(matches[mi].first,wk2)<<" : " ;
					if (+flags_str) matches_str << widen(patterns_[i],wp) << flags_str       ;
					else            matches_str <<       patterns_[i]                        ;
					/**/            matches_str <<'\n'                                       ;
					i++ ;
				}
			if (+matches_str) res << mk<<'s' <<" :\n"<< matches_str ;
		}
		// report exceptions (i.e. sub-repos in which rule does not apply) unless it can be proved we cannot match in such sub-repos
		::vector_s excepts_s ;                                                                 // sub-repos exceptions
		::uset_s   seens_s   ;                                                                 // we are only interested in first level sub-repos under our sub-repo
		for( ::string const& sr_s : g_config->sub_repos_s ) {
			if (!( sr_s.size()>sub_repo_s.size() && sr_s.starts_with(sub_repo_s) )) continue ; // if considered sub-repo is not within our sub-repo, it cannot match
			for( ::string const& e_s : seens_s )
				if (sr_s.starts_with(e_s)) goto Skip ;                                         // g_config->sub_repos_s are sorted so that higher level occurs first
			seens_s.insert(sr_s) ;
			for( bool star : {false,true} )
				for( VarIdx mi : matches_iotas[star][+MatchKind::Target] ) {
					::string const& p   = matches[mi].second.pattern                    ;
					::string_view   pfx = substr_view( p , 0 , p.find(Rule::StemMrkr) ) ;      // find target prefix
					if (sr_s.starts_with(pfx )) goto Report ;                                  // found a target that may      match in sub-repo, include it
					if (pfx .starts_with(sr_s)) goto Report ;                                  // found a target that may only match in sub-repo, include it
				}
		Skip :
			continue ;
		Report :
			excepts_s.push_back(sr_s) ;
		}
		if (+excepts_s) {
			/**/                                  res << "except in sub-repos :\n"  ;
			for( ::string const& e_s : excepts_s) res << indent(no_slash(e_s)) <<'\n' ;
		}
		// report actual reg-exprs to ease debugging
		res << "patterns :\n" ;
		for( size_t mi : iota(matches.size()) ) res <<'\t'<< widen(matches[mi].first,wk) <<" : "<< patterns[mi].txt <<'\n' ;
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

	template<class T> ::string RuleData::_pretty_dyn(Dyn<T> const& d) const {
		if (!d.is_dyn()) return {} ;
		::string res ;
		/**/                         res <<"dynamic "<< T::Msg <<" :\n" ;
		if (+d.entry().ctx       ) { res << "\t<context>  :" ; for( ::string const& k : _list_ctx(d.entry().ctx)    ) res <<' '<<k ; res <<'\n' ; }
		if (+d.entry().may_import) { res << "\t<sys.path> :" ; for( Object   const& d : *Rule::s_rules->py_sys_path ) res <<' '<< ::string(d.as_a<Str>()) ; res <<'\n' ; }
		if (+d.entry().glbs_str  )   res << "\t<globals> :\n" << ensure_nl(indent(ensure_nl(d.entry().glbs_str)+d.entry().dbg_info,2)) ;
		if (+d.entry().code_str  )   res << "\t<code> :\n"    << ensure_nl(indent(          d.entry().code_str                    ,2)) ;
		return res ;
	}

	::string RuleData::pretty_str() const {
		::string  title       ;
		::vmap_ss entries     ;
		::string  job_name_   = job_name ;
		::string  interpreter ;
		::string  kill_sigs   ;
		//
		{	title = user_name() + " :" + (special==Special::Anti?" AntiRule":special==Special::GenericSrc?" SourceRule":"") ;
			for( bool star : {false,true} )
				for( VarIdx mi : matches_iotas[star][+MatchKind::Target] )
					if (job_name_==matches[mi].second.pattern) { job_name_ = "<targets."+matches[mi].first+'>' ; break ; }
		}
		if (!is_special()) {
			{	First first ;
				for( ::string const& c : start_cmd_attrs.spec.interpreter ) interpreter<<first(""," ")<<c ;
			}
			{	First           first ;
				::uset<uint8_t> seen  ;
				for( uint8_t sig : start_ancillary_attrs.spec.kill_sigs ) {
					kill_sigs << first(""," , ") ;
					if (!sig) continue ;
					kill_sigs << int(sig) ;
					if (seen.insert(sig).second) kill_sigs << '('<<::strsignal(sig)<<')' ;
				}
			}
		}
		//
		// first simple static attrs
		{	if ( user_prio!=0                                      ) entries.emplace_back( "prio"                , cat        (user_prio                                         ) ) ;
			/**/                                                     entries.emplace_back( "job_name"            ,             job_name_                                           ) ;
			if (+sub_repo_s                                        ) entries.emplace_back( "sub_repo"            , no_slash   (sub_repo_s                                        ) ) ;
		}
		if (!is_special()) {
			if ( start_cmd_attrs      .spec.auto_mkdir             ) entries.emplace_back( "auto_mkdir"          , cat        (start_cmd_attrs       .spec.auto_mkdir             ) ) ;
			/**/                                                     entries.emplace_back( "autodep"             , snake      (start_rsrcs_attrs     .spec.method                 ) ) ;
			if ( submit_rsrcs_attrs.spec.backend!=BackendTag::Local) entries.emplace_back( "backend"             , snake      (submit_rsrcs_attrs    .spec.backend                ) ) ;
			if (+submit_ancillary_attrs .spec.cache                ) entries.emplace_back( "cache"               ,             submit_ancillary_attrs.spec.cache                  ) ;
			if (+start_cmd_attrs      .spec.job_space.chroot_dir_s ) entries.emplace_back( "chroot_dir"          , no_slash   (start_cmd_attrs       .spec.job_space.chroot_dir_s ) ) ;
			if ( start_ancillary_attrs.spec.z_lvl                  ) entries.emplace_back( "compression"         , ::to_string(start_ancillary_attrs .spec.z_lvl                  ) ) ;
			if ( force                                             ) entries.emplace_back( "force"               , cat        (force                                              ) ) ;
			if (+interpreter                                       ) entries.emplace_back( "interpreter"         ,             interpreter                                          ) ;
			if ( start_ancillary_attrs.spec.keep_tmp               ) entries.emplace_back( "keep_tmp"            , cat        (start_ancillary_attrs .spec.keep_tmp               ) ) ;
			if (+start_ancillary_attrs.spec.kill_sigs              ) entries.emplace_back( "kill_sigs"           ,             kill_sigs                                            ) ;
			if (+start_cmd_attrs      .spec.job_space.lmake_view_s ) entries.emplace_back( "lmake_view"          , no_slash   (start_cmd_attrs       .spec.job_space.lmake_view_s ) ) ;
			if ( n_losts                                           ) entries.emplace_back( "max_retries_on_lost" , ::to_string(n_losts                                            ) ) ;
			if ( start_ancillary_attrs.spec.max_stderr_len         ) entries.emplace_back( "max_stderr_len"      , ::to_string(start_ancillary_attrs .spec.max_stderr_len         ) ) ;
			if ( n_submits                                         ) entries.emplace_back( "max_submits"         , ::to_string(n_submits                                          ) ) ;
			if ( start_cmd_attrs      .spec.readdir_ok             ) entries.emplace_back( "readdir_ok"          , cat        (start_cmd_attrs       .spec.readdir_ok             ) ) ;
			if (+start_cmd_attrs      .spec.job_space.repo_view_s  ) entries.emplace_back( "repo_view"           , no_slash   (start_cmd_attrs       .spec.job_space.repo_view_s  ) ) ;
			if (+start_ancillary_attrs.spec.start_delay            ) entries.emplace_back( "start_delay"         ,             start_ancillary_attrs .spec.start_delay.short_str()  ) ;
			if ( start_cmd_attrs      .spec.stderr_ok              ) entries.emplace_back( "stderr_ok"           , cat        (start_cmd_attrs       .spec.stderr_ok              ) ) ;
			if (+start_rsrcs_attrs    .spec.timeout                ) entries.emplace_back( "timeout"             ,             start_rsrcs_attrs     .spec.timeout.short_str()      ) ;
			if (+start_cmd_attrs      .spec.job_space.tmp_view_s   ) entries.emplace_back( "tmp_view"            , no_slash   (start_cmd_attrs       .spec.job_space.tmp_view_s   ) ) ;
			if ( start_rsrcs_attrs    .spec.use_script             ) entries.emplace_back( "use_script"          , cat        (start_rsrcs_attrs     .spec.use_script             ) ) ;
		}
		::string res = _pretty_vmap( title , entries ) ;
		// checksums
		SWEAR( crc->state==RuleCrcState::Ok , name , crc ) ;
		SWEAR( &*crc->rule==this            , name , crc ) ;
		res << indent(_pretty_vmap( "checksums :" , crc->descr()) ) ;
		// then composite static attrs
		{	res << indent(_pretty_vmap   ("stems :",stems,true/*uniq*/)) ;
			res << indent(_pretty_matches(                            )) ;
		}
		if (!is_special()) {
			res << indent(_pretty_deps (                                           )) ;
			res << indent(_pretty_vmap ("resources :",submit_rsrcs_attrs.spec.rsrcs)) ;
			res << indent(_pretty_views(                                           )) ;
			res << indent(_pretty_env  (                                           )) ;
		}
		// then dynamic part
		if (!is_special()) {
			res << indent(_pretty_dyn(deps_attrs            )) ;
			res << indent(_pretty_dyn(submit_rsrcs_attrs    )) ;
			res << indent(_pretty_dyn(submit_ancillary_attrs)) ;
			res << indent(_pretty_dyn(start_cmd_attrs       )) ;
			res << indent(_pretty_dyn(start_rsrcs_attrs     )) ;
			res << indent(_pretty_dyn(start_ancillary_attrs )) ;
			res << indent(_pretty_dyn(cmd                   )) ;
		}
		// and finally the cmd
		if ( !is_special() && cmd.entry().kind<DynKind::Dyn ) {
			if (is_python) res << indent("cmd :\n") << indent(ensure_nl(cmd.entry().glbs_str+cmd.entry().dbg_info+cmd.entry().code_str) ,2) ;
			else           res << indent("cmd :\n") << indent(ensure_nl(_pretty_fstr(                             cmd.entry().code_str)),2) ;
		}
		return res ;
	}

	::vector_s RuleData::_list_ctx(::vector<CmdIdx> const& ctx) const {
		::vector_s res ; res.reserve(ctx.size()) ;
		for( auto [vc,i] : ctx ) switch (vc) {
			case VarCmd::Stem      : res.push_back(stems                        [i].first) ; break ;
			case VarCmd::StarMatch :
			case VarCmd::Match     : res.push_back(matches                      [i].first) ; break ;
			case VarCmd::Dep       : res.push_back(deps_attrs        .spec .deps[i].first) ; break ;
			case VarCmd::Rsrc      : res.push_back(submit_rsrcs_attrs.spec.rsrcs[i].first) ; break ;
			case VarCmd::Stems     : res.push_back("stems"                               ) ; break ;
			case VarCmd::Targets   : res.push_back("targets"                             ) ; break ;
			case VarCmd::Deps      : res.push_back("deps"                                ) ; break ;
			case VarCmd::Rsrcs     : res.push_back("resources"                           ) ; break ;
		DF}                                                                                          // NO_COV
		return res ;
	}

	// crc->match is an id of the rule : a new rule is a replacement of an old rule if it has the same crc->match
	// also, 2 rules matching identically is forbidden : the idea is that one is useless
	// this is not strictly true, though : you could imagine a rule generating a* from b, another generating a* from b but with disjoint sets of a*
	// although awkward & useless (as both rules could be merged), this can be meaningful
	// if the need arises, we will add an "id" artificial field entering in crc->match to distinguish them
	void RuleData::_set_crcs(RulesBase const& rules) {
		if (!is_special()) SWEAR(+rules) ;
		Hash::Xxh h ;                                                  // each crc continues after the previous one, so they are standalone
		//
		// START_OF_VERSIONING
		::vmap_s<bool> targets ;
		for( bool star : {false,true} )
			for( VarIdx mi : matches_iotas[star][+MatchKind::Target] ) // targets (static and star) must be kept first in matches so RuleTgt is stable when match_crc is stable
				targets.emplace_back( matches[mi].second.pattern , matches[mi].second.flags.extra_tflags[ExtraTflag::Optional] ) ; // keys and flags have no influence on matching, except Optional
		h += special ;                                                                                                             // in addition to distinguishing special from other, ...
		h += stems   ;                                                                                                             // ... this guarantees that shared rules have different crc's
		h += targets ;
		if (is_special()) {
			h += allow_ext ;                               // only exists for special rules
		} else {
			h += job_name ;
			deps_attrs.update_hash( /*inout*/h , rules ) ; // no deps for source & anti
		}
		Crc match_crc = h.digest() ;
		//
		if (is_special()) {
			crc = {match_crc} ;
		} else {
			h += sub_repo_s             ;
			h += Node::s_src_dirs_crc() ;                  // src_dirs influences deps recording
			h += matches                ;                  // these define names and influence cmd execution, all is not necessary but simpler to code
			h += force                  ;
			h += is_python              ;
			start_cmd_attrs.update_hash( /*inout*/h , rules ) ;
			cmd            .update_hash( /*inout*/h , rules ) ;
			Crc cmd_crc = h.digest() ;
			//
			submit_rsrcs_attrs.update_hash( /*inout*/h , rules ) ;
			start_rsrcs_attrs .update_hash( /*inout*/h , rules ) ;
			Crc rsrcs_crc = h.digest() ;
			//
			crc = { match_crc , cmd_crc , rsrcs_crc } ;
		}
		// END_OF_VERSIONING
	}

	//
	// RuleCrcData
	//

	::string& operator+=( ::string& os , RuleCrcData const& rcd ) {                                                    // START_OF_NO_COV
		return os << "RCD(" << rcd.rule <<','<< rcd.state <<','<< rcd.match <<','<< rcd.cmd <<','<< rcd.rsrcs << ')' ;
	}                                                                                                                  // END_OF_NO_COV

	::vmap_ss RuleCrcData::descr() const {
		return {
			{ "match"     , match.hex() }
		,	{ "cmd"       , cmd  .hex() }
		,	{ "resources" , rsrcs.hex() }
		} ;
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

	Rule::RuleMatch::RuleMatch( Rule r , TargetPattern const& pattern , ::string const& name , Bool3 chk_psfx ) {
		Trace trace("RuleMatch",r,name,chk_psfx) ;
		/**/                                         if (!r) { trace("no_rule" ) ; return ; }
		Re::Match m = pattern.match(name,chk_psfx) ; if (!m) { trace("no_match") ; return ; }
		rule = r ;
		for( VarIdx s : iota(r->n_static_stems) ) stems.push_back( ::string(m.group( name , pattern.groups[s] )) ) ;
		trace("stems",stems) ;
	}

	::string& operator+=( ::string& os , Rule::RuleMatch const& m ) { // START_OF_NO_COV
		os << "RSM(" << m.rule << ',' << m.stems << ')' ;
		return os ;
	}                                                                 // END_OF_NO_COV

	::uset<Node> Rule::RuleMatch::target_dirs() const {
		::uset<Node> dirs ;
		for( auto const& [k,me] : rule->matches ) {
			if (!me.flags.extra_tflags[ExtraTflag::Allow]) continue ;
			::string target = subst_target(
				me.pattern
			,	[&](VarIdx s)->::string { return stems[s] ; }
			,	rule->n_static_stems/*stop_above*/
			) ;
			if ( size_t sep=target.rfind('/') ; sep!=Npos ) {
				target.resize(sep)       ;
				dirs.emplace(New,target) ;
			}
		}
		return dirs ;
	}

	::vector<Re::Pattern> Rule::RuleMatch::star_patterns() const {
		RuleData const&       rd  = *rule ;
		VarIdx                n   = 0     ; for( MatchKind mk : iota(All<MatchKind>) ) n += rd.matches_iotas[true/*star*/][+mk].size() ;
		::vector<Re::Pattern> res ;         res.reserve(n) ;
		for( MatchKind mk : iota(All<MatchKind>) )
			for( VarIdx mi : rd.matches_iotas[true/*star*/][+mk] ) {
				RuleData::MatchEntry const& me        = rd.matches[mi].second ;
				uint32_t                    cur_group = 1                     ;
				VarIdx                      nss       = rd.n_static_stems     ;
				::vector<uint32_t>          groups    ( rd.stems.size()-nss ) ; // used to set back references
				Re::Pattern                 pattern   ;
				subst_target(
					me.pattern
				,	[&](VarIdx s)->::string {
						if (s<nss        ) { pattern.emplace_back( stems[s]                , Maybe/*capture*/ ) ; return {} ; }
						if (groups[s-nss]) { pattern.emplace_back( cat('\\',groups[s-nss]) , No   /*capture*/ ) ; return {} ; }
						bool capture = me.captures[s] ;
						if (capture) groups[s-nss] = cur_group ;
						cur_group += capture+rd.stem_n_marks[s] ;
						pattern.emplace_back( rd.stems[s].second , No|capture ) ;
						return {} ;
					}
				,	[&](::string const& s)->::string {
						pattern.emplace_back( s , Maybe ) ;
						return {} ;
					}
				) ;
				res.push_back(::move(pattern)) ;
			}
		return res ;
	}

	::vector_s Rule::RuleMatch::py_matches() const {
		RuleData const& rd  = *rule                  ;
		::vector_s      res ;                          res.reserve(rd.matches.size()) ;
		::vector_s      ms  = matches(false/*star*/) ;
		VarIdx          i   = 0                      ;
		for( MatchKind mk : iota(All<MatchKind>) ) {
			for( [[maybe_unused]] VarIdx _  : rd.matches_iotas[false/*star*/][+mk] ) res.push_back(::move(ms[i++])) ;
			for(                  VarIdx mi : rd.matches_iotas[true /*star*/][+mk] ) {
				::uset<VarIdx> seen_stems ;
				res.push_back(subst_target(
					rd.matches[mi].second.pattern
				,	[&](VarIdx s)->::string {
						if (s<rd.n_static_stems) return Re::escape(stems[s]) ;
						::pair_ss const& stem = rd.stems[s] ;
						if      ( !seen_stems.insert(s).second                      ) return "(?P="+stem.first                +')' ;
						else if ( stem.first.front()=='<' && stem.first.back()=='>' ) return '('   +               stem.second+')' ; // stem is unnamed
						else                                                          return "(?P<"+stem.first+'>'+stem.second+')' ;
					}
				,	Re::escape
				)) ;
			}
		}
		SWEAR( i==ms.size() , i,ms.size() ) ;
		return res ;
	}

	::vector_s Rule::RuleMatch::_matches( bool star , bool targets_only ) const {
		Iota1<MatchKind> iota_mk = iota( targets_only ? MatchKind::Target+1 : All<MatchKind> ) ;
		RuleData const&  rd      = *rule                                                       ;
		VarIdx           n       = 0                                                           ; for( MatchKind mk : iota_mk ) n += rd.matches_iotas[star][+mk].size() ;
		::vector_s       res     ;                                                               res.reserve(n) ;
		for( MatchKind mk : iota_mk )
			for( VarIdx mi : rd.matches_iotas[star][+mk] )
				res.push_back(subst_target(
					rd.matches[mi].second.pattern
				,	[&](VarIdx s)->::string {
						if (!star                 ) SWEAR(s<rd.n_static_stems) ;
						if (s<rule->n_static_stems) return stems[s]                      ;
						else                        return "{"+rule->stems[s].first+"*}" ;
					}
				)) ;
		return res ;
	}

	::pair_ss Rule::RuleMatch::full_name() const {
		::vector<FileNameIdx> poss(rule->n_static_stems) ;
		::string name = subst_target( rule->job_name ,
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

	::pair_s<VarIdx> Rule::RuleMatch::reject_msg() const {
		RuleData const& rd = *rule ;
		SWEAR( rd.special<=Special::HasJobs , rd.special ) ;
		for( bool star : {false,true} ) {
			Iota2<VarIdx> matches_iota = rd.matches_iotas[star][+MatchKind::Target] ;
			auto it = matches_iota.begin() ;
			for( ::string const& t : targets(star) ) {
				SWEAR(it!=matches_iota.end()) ;
				MatchKind k = rd.matches[*it].second.flags.kind() ;
				if ( ::string msg=_reject_msg(k,t) ; +msg ) return {msg,*it} ;
				it++ ;
			}
		}
		return {} ;
	}

}
