// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

namespace Engine {

	using namespace Disk ;
	using namespace Py   ;

	::string _subst_fstr( ::string const& fstr , ::umap_s<CmdIdx> const& var_idxs , VarIdx&/*inout*/ n_unnamed , bool* /*out*/ keep_for_deps=nullptr ) {
		::string res ;
		//
		if (keep_for_deps) *keep_for_deps = true ;                                                  // unless found to be external
		parse_py( fstr , nullptr/*unnamed_star_idx*/ ,
			[&]( ::string const& k , bool star , bool unnamed , ::string const* def ) {
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
		,	[&]( ::string const& fixed , bool has_pfx , bool has_sfx ) {
				SWEAR(+fixed) ;
				res.append(fixed) ;
				if (!keep_for_deps) return ;                                                        // not a dep, no check
				if ( !is_canon( fixed , true/*ext_ok*/ , true/*empty_ok*/ , has_pfx , has_sfx ) ) {
					if ( ::string c=mk_canon(fstr) ; c!=fstr ) throw cat("is not canonical, consider using : ",c) ;
					else                                       throw cat("is not canonical"                     ) ;
				}
				if ( !has_sfx && fixed.back()=='/' ) {
					if ( ::string ns=no_slash(fstr) ; ns!=fstr ) throw cat("ends with /, consider using : ",ns) ;
					else                                         throw cat("ends with /"                      ) ;
				}
				if (has_pfx      ) return ;                                   // further check only for prefix
				if (is_lcl(fixed)) return ;
				//
				// dep is non-local, check if it lies within a source dirs
				*keep_for_deps = false ;                                      // unless found in a source dir
				if (!*g_src_dirs_s) return ;                                  // fast path : no need to compute rel/abs versions
				//
				::string dir_s     = fixed.substr(0,fixed.rfind('/')+1) ;
				::string rel_fixed = mk_rel  (fixed,*g_repo_root_s)     ;
				::string abs_fixed = mk_glb  (fixed,*g_repo_root_s)     ;
				::string rel_dir_s = mk_rel_s(dir_s,*g_repo_root_s)     ;
				::string abs_dir_s = mk_glb_s(dir_s,*g_repo_root_s)     ;
				if (is_lcl_s(rel_dir_s)) throw cat("must be provided as local file, consider : ",rel_dir_s+substr_view(fstr,dir_s.size())) ;
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
						if (  is_abs(fstr) && !abs_sd ) throw cat("must be relative inside source dir ",sd_s,rm_slash,", consider : ",mk_rel(fstr,*g_repo_root_s)) ;
						if ( !is_abs(fstr) &&  abs_sd ) throw cat("must be absolute inside source dir ",sd_s,rm_slash,", consider : ",mk_glb(fstr,*g_repo_root_s)) ;
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
					code_str = _subst_fstr( ::string(py_src["static"].as_a<Dict>()["cmd"].as_a<Str>()) , var_idxs , /*inout*/n_unnamed ) ;
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
		for( size_t ci=0 ; ci<fstr.size() ; ci++ ) {                                                                            // /!\ not a iota
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
		auto cb_str = [&]( VarCmd , VarIdx , ::string const& /*key*/ , ::string  const&   val   ) { res<<val<<fixed[fi++] ; } ;
		auto cb_dct = [&]( VarCmd , VarIdx , ::string const& /*key*/ , ::vmap_ss const& /*val*/ ) { FAIL()                ; } ; // NO_COV
		DynBase::s_eval(job,match,rsrcs,ctx_,cb_str,cb_dct) ;
		return res ;
	}

	//
	// Rule
	//

	Atomic<Pdate> Rule::s_last_dyn_date ;
	Job           Rule::s_last_dyn_job  ;
	const char*   Rule::s_last_dyn_msg  = nullptr ;
	Rule          Rule::s_last_dyn_rule ;

	::string& operator+=( ::string& os , Rule const r ) { // START_OF_NO_COV
		/**/    os << "R(" ;
		if (+r) os << +r   ;
		return  os << ')'  ;
	}                                                     // END_OF_NO_COV

	::string/*msg*/ Rule::s_reject_msg( MatchKind mk , ::string const& file , bool has_pfx , bool has_sfx ) {
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

	bool/*keep*/ Rule::s_qualify_dep( ::string const& key , ::string const& dep ) {
		auto bad = [&] ( ::string const& msg , ::string const& consider={} ) {
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
		::string abs_dep = mk_glb( dep , *g_repo_root_s ) ;
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

	static void _mk_flags( ::string const& key , Sequence const& py_seq , uint8_t n_skip , MatchFlags&/*inout*/ flags , bool dep_only ) {
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
				else                                                                                                                             throw cat("unexpected flag ",flag_str," for ",key) ;
			} else if (item.is_a<Sequence>()) {
				_mk_flags( key , item.as_a<Sequence>() , 0 , /*inout*/flags , dep_only ) ;
			} else {
				throw key+"has a flag that is not a str" ;
			}
		}
	}
	::string Rule::s_split_flags( ::string const& key , Object const& py , uint8_t n_skip , MatchFlags&/*out*/ flags , bool dep_only ) {
		if (py.is_a<Str>()) {
			SWEAR(n_skip==1) ;                                        // cannot skip 2 values with a single Str
			return py.as_a<Str>() ;
		}
		Sequence const* py_seq ;
		try                       { py_seq = &py.as_a<Sequence>() ; }
		catch (::string const& e) { throw e+" nor a str" ;          } // e is a type error
		SWEAR( py_seq->size()>=n_skip   , key ) ;
		SWEAR( (*py_seq)[0].is_a<Str>() , key ) ;
		_mk_flags( key , *py_seq , n_skip , /*inout*/flags , dep_only ) ;
		return (*py_seq)[0].as_a<Str>() ;
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
				throw_unless( +upper             , "no upper"                    ) ;
				throw_unless( !copy_up || +lower , "cannot copy up from nowhere" ) ;
				for( ::string const& cu : copy_up ) {
					throw_unless( +cu         , "copy up item must not be empty"      ) ;
					throw_unless( !is_abs(cu) , "copy up item must be relative : ",cu ) ;
				}
			} else throw "unexpected view description which is not a str nor a dict"s ;
			/**/                       dst.phys_s.push_back(with_slash(::move(upper))) ;
			for( ::string& l : lower ) dst.phys_s.push_back(with_slash(::move(l    ))) ;
			/**/                       dst.copy_up = ::move(copy_up) ;
			return true/*updated*/ ;
		}

		bool/*updated*/ acquire( Zlvl& dst , Object const* py_src ) {
			//                                     updated
			if (!py_src       )              return false ;
			if ( py_src==&None) { dst = {} ; return true  ; }
			if (py_src->is_a<Int>()) {
				dst.tag = ZlvlTag::Dflt ;
				acquire( dst.lvl , py_src ) ;
			} else if (py_src->is_a<Str>()) {
				acquire( dst.tag , py_src ) ;
				dst.lvl = 1 ;
			} else if (py_src->is_a<Sequence>()) {
				Sequence const& py_seq = py_src->as_a<Sequence>() ;
				throw_unless( py_seq.size()==2 , "cannot understand compression which is a sequence of len !=2" ) ;
				acquire( dst.tag , &py_seq[0] ) ;
				acquire( dst.lvl , &py_seq[1] ) ;
			} else {
				throw "cannot understand compression"s ;
			}
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
			VarIdx     n_unnamed = 0                                                                                        ;
			MatchFlags mfs       = { .dflags=DflagsDfltStatic , .extra_dflags=ExtraDflagsDfltStatic }                       ;
			::string   dep       = Rule::s_split_flags( "dep "+key , py_val , 1/*n_skip*/ , /*out*/mfs , true/*dep_only*/ ) ;
			dep  = rd.add_cwd( ::move(dep) , mfs.extra_dflags[ExtraDflag::Top] ) ;
			try {
				bool     keep       = false/*garbage*/                                                                   ;
				::string parsed_dep = _subst_fstr( dep , var_idxs , /*inout*/n_unnamed , /*out*/&keep/*keep_for_deps*/ ) ;
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
							MatchFlags mfs = { .dflags=DflagsDfltStatic , .extra_dflags=ExtraDflagsDfltStatic }                       ;
							::string   dep = Rule::s_split_flags( "dep "+key , py_val , 1/*n_skip*/ , /*out*/mfs , true/*dep_only*/ ) ;
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

	StartCmdAttrs DynStartCmdAttrs::eval( Rule::RuleMatch const& m , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps ) const {
		StartCmdAttrs res = Base::eval(m,rsrcs,deps) ;
		::erase_if( res.job_space.views , [](::pair_s<JobSpace::ViewDescr> const& v_d) { return !v_d.second ; } ) ; // empty views may appear dynamically and mean no view
		return res ;
	}

	::string DynCmd::eval( StartRsrcsAttrs&/*inout*/ sra , Rule::RuleMatch const& match , ::vmap_ss const& rsrcs , ::vmap_s<DepDigest>* deps , StartCmdAttrs const& sca ) const {
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
			if (res.size()>1<<16) sra.use_script = true ;
		} else {
			if ( entry().glbs_str.size() + entry().code_str.size() > 1<<16 ) sra.use_script = true ;
			//
			if (sra.use_script) res << "import sys ; sys.path[0] = '' ; del sys\n#"                                                        <<'\n' ; // ensure sys.path is as if run with -c, ...
			/**/                res << "lmake_root = " << mk_py_str(no_slash(sca.job_space.lmake_view_s|sra.lmake_root_s|*g_lmake_root_s)) <<'\n' ; // ... del sys to ensure total transparency
			/**/                res << "repo_root  = " << mk_py_str(no_slash(sca.job_space.repo_view_s |*g_repo_root_s                  )) <<'\n' ;
			/**/                res << '#'                                                                                                 <<'\n' ;
			eval_ctx( match , rsrcs
			,	[&]( VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) {
					res += r->gen_py_line( match , vc , i , key , val ) ;
				}
			,	[&]( VarCmd , VarIdx , ::string const& key , ::vmap_ss const& val ) {
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
		/**/                                         if (!r) { Trace("RuleMatch","no_rule"   ) ; return ; }
		Re::Match m = pattern.match(name,chk_psfx) ; if (!m) { Trace("RuleMatch",r,"no_match") ; return ; }
		rule = r ;
		for( VarIdx s : iota(r->n_static_stems) ) stems.push_back( ::string(m.group( name , pattern.groups[s] )) ) ;
		Trace("RuleMatch","stems",r,name,chk_psfx,stems) ;
	}

	::string& operator+=( ::string& os , Rule::RuleMatch const& m ) { // START_OF_NO_COV
		os << "RM(" << m.rule << ',' << m.stems << ')' ;
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
				,	[&](VarIdx s) {
						if (s<rd.n_static_stems) return Re::escape(stems[s]) ;
						::pair_ss const& stem = rd.stems[s] ;
						if      ( !seen_stems.insert(s).second                      ) return cat("(?P=",stem.first                ,')') ;
						else if ( stem.first.front()=='<' && stem.first.back()=='>' ) return cat('('   ,               stem.second,')') ; // stem is unnamed
						else                                                          return cat("(?P<",stem.first+'>',stem.second,')') ;
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
						if (s<rule->n_static_stems) return stems[s]                           ;
						else                        return cat("{",rule->stems[s].first,"*}") ;
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
				if ( key.front()=='<' && key.back()=='>' ) return "{*}"             ;
				else                                       return cat('{',key,"*}") ;
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
				if ( ::string msg=s_reject_msg(k,t) ; +msg ) return {msg,*it} ;
				it++ ;
			}
		}
		return {} ;
	}

}
