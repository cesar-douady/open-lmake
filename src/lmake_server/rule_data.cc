// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

namespace Engine {

	using namespace Disk ;
	using namespace Py   ;

	static void _parse_target( ::string const& str , ::function<void(FileNameIdx pos,VarIdx stem)> const& cb ) {
		for( size_t i=0 ; i<str.size() ; i++ ) {                                                                 // /!\ not a iota
			char c = str[i] ;
			if (c==Rule::StemMrkr) {
				VarIdx stem = decode_int<VarIdx>(&str[i+1]) ; i += sizeof(VarIdx) ;
				cb(i,stem) ;
			}
		}
	}
	// provide shortcut when pos is unused
	static void _parse_target( ::string const& str , ::function<string(VarIdx)> const& cb ) {
		_parse_target( str , [&](FileNameIdx,VarIdx s) { return cb(s) ; } ) ;
	}

	::string& operator+=( ::string& os , RuleData const& rd ) { // START_OF_NO_COV
		return os << "RD(" << rd.name << ')' ;
	}                                                           // END_OF_NO_COV

	::string RuleData::gen_py_line( Job j , Rule::RuleMatch& m/*lazy*/ , VarCmd vc , VarIdx i , ::string const& key , ::string const& val ) const {
		if (vc!=VarCmd::StarMatch) return key+" = "+mk_py_str(val)+'\n' ;
		//
		Rule           r    = +m ? m.rule : j->rule() ;
		::vector_s     args ;
		::uset<VarIdx> seen ;
		::string   expr = subst_target(
			matches[i].second.pattern
		,	[&](VarIdx s) {
				bool first = seen.insert(s).second ;
				::string k = stems[s].first        ;
				if ( k.front()=='<' and k.back()=='>' )   k = k.substr(1,k.size()-2) ;
				if ( s>=r->n_static_stems             ) { if (first) args.push_back(k)      ; return cat('{',k,'}')             ; }
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

	void RuleData::MatchEntry::set_pattern( ::string&& p , VarIdx n_stems ) {
		::uset<VarIdx> seen ;
		pattern = ::move(p) ;
		captures.resize(n_stems) ; for( bool c : captures ) SWEAR(!c) ; // captures is being initialized
		_parse_target( pattern , [&](VarIdx s)->::string {
			if (!seen.insert(s).second) captures[s] = true ;            // stem must always be captured for back-reference if seen several times
			return {} ;
		} ) ;
	}

	static ::string _stem_mrkr(VarIdx stem_idx) {     // for targets
		::string res ; res.resize(1+sizeof(VarIdx)) ;
		res[0] = Rule::StemMrkr ;
		encode_int( &res[1] , stem_idx ) ;
		return res ;
	}

	static ::string _stem_mrkr( VarCmd var_cmd , VarIdx stem_idx ) { // for deps
		::string res ( 1+sizeof(VarCmd)+sizeof(VarIdx) , 0 ) ;
		res[0] = Rule::StemMrkr ;
		encode_int( &res[1               ] , +var_cmd ) ;
		encode_int( &res[1+sizeof(VarCmd)] , stem_idx ) ;
		return res ;
	}

	RuleData::RuleData(Special s) : special{s} , name{snake(s)} {
		SWEAR(+s) ;
		//
		switch (s) {
			case Special::Dep          :
			case Special::InfiniteDep  :
			case Special::InfinitePath : break ;
			case Special::Req :
				force  = true ;
				n_runs = 2    ;
			break ;
			case Special::Codec : {
				using namespace Codec ;
				// START_OF_VERSIONING REPO CACHE CODEC
				static constexpr MatchFlags IncPhony { .tflags{Tflag::Incremental,Tflag::Phony,Tflag::Target} } ;
				stems = {
					{ "File" ,     ".+"                                            } // static
				,	{ "Ctx"  , cat("[^",CodecSep,"]*")                             } // star
				,	{ "Code" ,     "[^/]*"                                         } // .
				,	{ "Val"  , cat("[A-Za-z0-9_-]{",Codec::CodecCrc::Base64Sz,'}') } // .      /!\ - must be first or last char in []
				} ;
				n_static_stems = 1 ;
				//
				static ::string pfx = Codec::CodecFile::s_pfx_s() ;
				job_name = cat(pfx,_stem_mrkr(0/*File*/)) ;
				matches  = { //!                             File                        Ctx                                                                 File Ctx Code/Val
					{ "DECODE" , {.pattern=cat(pfx,_stem_mrkr(0 ),'/',CodecSep,_stem_mrkr(1),'/',_stem_mrkr(2/*Code*/),DecodeSfx),.flags=IncPhony,.captures={true,true,true  }} } // star target
				,	{ "ENCODE" , {.pattern=cat(pfx,_stem_mrkr(0 ),'/',CodecSep,_stem_mrkr(1),'/',_stem_mrkr(3/*Val */),EncodeSfx),.flags=IncPhony,.captures={true,true,true  }} } // .
				} ;
				matches_iotas[true/*star*/][+MatchKind::Target] = { 0/*start*/ , VarIdx(matches.size())/*end*/ } ;
				//
				deps_attrs.spec.deps = {
					{ "CODEC_FILE" , {.txt=_stem_mrkr(VarCmd::Stem,0/*File*/),.dflags=DflagsDfltStatic,.extra_dflags=ExtraDflagsDfltStatic} }
				} ;
				// END_OF_VERSIONING
			} break ;
		DF}                                                                                                                                                                       // NO_COV
		for( auto const& [_,v] : stems ) stem_n_marks.push_back(Re::RegExpr(v,true/*cache*/).n_marks()) ;
		_set_crcs({}) ;                                                                                   // rules is not necessary for special rules
	}

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
				throw_unless( special>=Special::NUniq , "unexpected value for __special__ attribute : ",special ) ;
			} else {
				special = Special::Plain ;
			}
			field = "name"       ; if (dct.contains(field)) name       = dct[field].as_a<Str  >() ; else throw "not found"s ;
			field = "sub_repo_s" ; if (dct.contains(field)) sub_repo_s = dct[field].as_a<Str  >() ;
			field = "prio"       ; if (dct.contains(field)) user_prio  = dct[field].as_a<Float>() ;
			if (+sub_repo_s) {
				add_slash(sub_repo_s) ;
				if (sub_repo_s[0]=='/') {
					if (sub_repo_s.starts_with(*g_repo_root_s)) sub_repo_s.erase(0,g_repo_root_s->size()) ;
					else                                        throw "cwd must be relative to repo root dir"s ;
				}
			}
			//
			Trace trace("_acquire_py",name,sub_repo_s,user_prio) ;
			//
			::umap_ss      stem_defs  ;
			::map_s<Bool3> stem_stars ;                                                                          // ordered so that stems are ordered, Maybe means stem is used both as static and star
			field = "stems" ;
			if (dct.contains(field))
				for( auto const& [py_k,py_v] : dct[field].as_a<Dict>() )
					stem_defs.emplace( ::string(py_k.as_a<Str>()) , ::string(py_v.as_a<Str>()) ) ;
			//
			// augment stems with definitions found in job_name and targets
			size_t unnamed_star_idx = 1 ;                                                                        // free running while walking over job_name + targets
			auto augment_stems = [&]( ::string const& k , bool star , ::string const* re , bool for_job_name ) {
				if (re) {
					auto [it,inserted] = stem_defs.emplace(k,*re) ;
					throw_unless( +inserted || *re==it->second , "2 different definitions for stem ",k," : ",it->second," and ",*re ) ;
				}
				if ( for_job_name || star ) {
					auto [it,inserted] = stem_stars.emplace(k,No|star) ;
					if ( !inserted && (No|star)!=it->second ) it->second = Maybe ;                               // stem is used both as static and star
				}
			} ;
			field = "job_name" ;
			throw_unless( dct.contains(field) , "not found" ) ;
			job_name = dct[field].as_a<Str>() ;
			parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) {
					augment_stems( k , star , re , true/*for_job_name*/ ) ;
				}
			) ;
			field = "matches" ;
			throw_unless( dct.contains(field) , "not found" ) ;
			::string  job_name_key  ;
			MatchKind job_name_kind ;
			for( auto const& [py_k,py_tkfs] : dct[field].as_a<Dict>() ) {
				field = py_k.as_a<Str>() ;
				::string  target =                    py_tkfs.as_a<Sequence>()[0].as_a<Str>()  ;                 // .
				MatchKind kind   = mk_enum<MatchKind>(py_tkfs.as_a<Sequence>()[1].as_a<Str>()) ;                 // targets are a tuple (target_pattern,kind,flags...)
				// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
				if (target!=job_name) {
					parse_py( target , &unnamed_star_idx ,
						// static stems are declared in job_name, but error will be caught later on, when we can generate a sound message
						[&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) {
							augment_stems( k , star , re , false/*for_job_name*/ ) ;
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
			unnamed_star_idx = 1          ;                                                                      // reset free running at each pass over job_name+targets
			VarIdx n_static_unnamed_stems = 0     ;
			bool   job_name_is_star       = false ;
			auto   stem_words             = []( ::string const& k , bool star , bool unnamed ) -> ::string {
				const char* stem = star ? "star stem" : "stem" ;
				return unnamed ? cat("unnamed ",stem) : cat(stem,' ',k) ;
			} ;
			parse_py( job_name , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) {
					if      (!stem_defs.contains(k)) throw cat("found undefined ",stem_words(k,star,unnamed)," in ",job_name_kind,' ',job_name_key) ;
					if      (star                  ) job_name_is_star = true ;
					else if (unnamed               ) n_static_unnamed_stems++ ;
				}
			) ;
			//
			field = "matches" ;
			{	::vmap_s<MatchEntry> star_matches  [N<MatchKind>] ;                                              // defer star matches so that static targets are put first
				::vmap_s<MatchEntry> static_matches[N<MatchKind>] ;                                              // .
				bool                 seen_top                     = false ;
				bool                 seen_target                  = false ;
				for( auto const& [py_k,py_tkfs] : dct[field].as_a<Dict>() ) {                                    // targets are a tuple (target_pattern,flags...)
					field = py_k.as_a<Str>() ;
					Sequence const& pyseq_tkfs    = py_tkfs.as_a<Sequence>()                      ;
					::string        target        =                    pyseq_tkfs[0].as_a<Str>()  ;              // .
					MatchKind       kind          = mk_enum<MatchKind>(pyseq_tkfs[1].as_a<Str>()) ;              // targets are a tuple (target_pattern,kind,flags...)
					bool            is_star       = false                                         ;
					::set_s         missing_stems ;
					bool            is_stdout     = field=="target"                               ;
					MatchFlags      flags         ;
					// ignore side_targets and side_deps for source and anti-rules
					// this is meaningless, but may be inherited for stems, typically as a PyRule
					if ( kind!=MatchKind::Target && !is_plain() ) continue ;
					// avoid processing target if it is identical to job_name : this is not an optimization, it is to ensure unnamed_star_idx's match
					if (target==job_name) {
						if (job_name_is_star) is_star = true ;
					} else {
						if (kind==MatchKind::Target) for( auto const& [k,s] : stem_stars ) if (s!=Yes) missing_stems.insert(k) ;
						parse_py( target , &unnamed_star_idx ,
							[&]( ::string const& k , bool star , bool unnamed , ::string const* /*re*/ ) {
								if (!stem_defs.contains(k)) throw cat("found undefined ",stem_words(k,star,unnamed)," in ",kind) ;
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
					if ( !is_star && kind==MatchKind::Target  ) flags.tflags       |= Tflag::Essential   ;       // static targets are essential by default
					if ( !is_star                             ) flags.tflags       |= Tflag::Static      ;
					if (             kind!=MatchKind::SideDep ) flags.extra_tflags |= ExtraTflag::Allow  ;
					if ( !is_star                             ) flags.extra_dflags |= ExtraDflag::NoStar ;
					Rule::s_split_flags( snake_str(kind) , pyseq_tkfs , 2/*n_skip*/ , /*out*/flags , kind==MatchKind::SideDep ) ;
					// check
					if ( target.starts_with(*g_repo_root_s)                                        ) throw cat(kind," must be relative to root dir : "                   ,target) ;
					if ( !target                                                                   ) throw cat(kind," must not be empty"                                        ) ;
					if ( !is_lcl(target)                                                           ) throw cat(kind," must be local : "                                  ,target) ;
					if ( +missing_stems                                                            ) throw cat("missing stems ",missing_stems," in ",kind," : "          ,target) ;
					if (  is_star                                    && !is_plain()                ) throw cat("star ",kind,"s are meaningless for source and anti-rules"       ) ;
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
				SWEAR(+seen_target) ;                                                                            // we should not have come up to here without a target
				if (!job_name_key) job_name = add_cwd( ::move(job_name) , seen_top ) ;
				static_assert(+MatchKind::Target==0) ;                                                           // targets (both static and star) must be first to ensure ...
				for( MatchKind k : iota(All<MatchKind>) ) {                                                      // ... RuleTgt stability when Rule's change without crc.match modif
					//            star
					matches_iotas[false][+k] = { VarIdx(matches.size()) , VarIdx(matches.size()+static_matches[+k].size()) } ; for( auto& st : static_matches[+k] ) matches.push_back(::move(st)) ;
					matches_iotas[true ][+k] = { VarIdx(matches.size()) , VarIdx(matches.size()+star_matches  [+k].size()) } ; for( auto& st : star_matches  [+k] ) matches.push_back(::move(st)) ;
				}
			}
			field.clear() ;
			throw_unless( matches.size()<NoVar , "too many targets, side_targets and side_deps ",matches.size()," >= ",int(NoVar) ) ;
			::umap_s<VarIdx> stem_idxs ;
			for( bool star : {false,true} ) {                                                                    // keep only useful stems and order them : static first, then star
				for( auto const& [k,v] : stem_stars ) {
					if (v==(No|!star)) continue ;                                                                // stems that are both static and start appear twice
					::string const& s = stem_defs.at(k) ;
					stem_idxs.emplace     ( k+" *"[star] , VarIdx(stems.size()) ) ;
					stems    .emplace_back( k            , s                    ) ;
					try         { stem_n_marks.push_back(Re::RegExpr(s,true/*cache*/).n_marks()) ; }
					catch (...) { throw cat("bad regexpr for stem ",k," : ",s) ;                   }
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
			auto mk_fixed = [&]( ::string const& fixed , bool has_pfx , bool has_sfx ) {
				SWEAR(+fixed) ;
				mk_tgt += fixed ;
				if (!ensure_canon) return ;
				::string msg = Rule::s_reject_msg( kind , fixed , has_pfx , has_sfx ) ;
				throw_if( +msg , *ensure_canon , +*ensure_canon?" ":"" , msg ) ;
			} ;
			auto mk_stem = [&]( ::string const& key , bool star , bool /*unnamed*/ , ::string const* /*re*/ ) {
				mk_tgt += _stem_mrkr(stem_idxs.at(key+" *"[star])) ;
			} ;
			if (!job_name_key) {
				field        = "job_name"    ;
			} else {
				field        = job_name_key  ;
				ensure_canon = &job_name     ;
				kind         = job_name_kind ;
			}                                                                                                    // if job_name is a target, canon must be checked
			unnamed_star_idx = 1 ;                                                                               // reset free running at each pass over job_name+targets
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
					kind         = me.flags.kind() ;                                                             // providing . as side_deps may be useful to pass readdir_ok flag
					mk_tgt.clear() ;
					parse_py( me.pattern , &unnamed_star_idx , mk_stem , mk_fixed ) ;
					me.set_pattern(::move(mk_tgt),stems.size()) ;
				}
			}
			field.clear() ;
			job_name = ::move(new_job_name) ;
			//
			//vvvvvvvvvvvvvvvvvvvvv
			if (!is_plain()) return ;                                                                            // if special, we have no dep, no execution, we only need essential info
			//^^^^^^^^^^^^^^^^^^^^^
			//
			// acquire fields linked to job execution
			//
			field = "ete"                 ; if (dct.contains(field)) Attrs::acquire( exe_time  , &dct[field] ) ;
			field = "force"               ; if (dct.contains(field)) Attrs::acquire( force     , &dct[field] ) ;
			field = "is_python"           ; if (dct.contains(field)) Attrs::acquire( is_python , &dct[field] ) ; else throw "not found"s ;
			field = "max_retries_on_lost" ; if (dct.contains(field)) Attrs::acquire( n_losts   , &dct[field] ) ;
			field = "max_runs"            ; if (dct.contains(field)) Attrs::acquire( n_runs    , &dct[field] ) ;
			field = "max_submits"         ; if (dct.contains(field)) Attrs::acquire( n_submits , &dct[field] ) ;
			if ( n_runs && n_submits ) n_submits = ::max( n_submits , n_runs ) ;                                 // n_submits<n_runs is meaningless
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
					if (deps_attrs.spec.deps[di].first!="dep") continue ;                                        // dep is a reserved key that means stdin
					stdin_idx = di ;
					break ;
				}
		}
		catch(::string const& e) {
			if (+field) throw cat("while processing ",user_name(),'.',field," :\n",indent(e)) ;
			else        throw cat("while processing ",user_name(),          " :\n",indent(e)) ;
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
		,	[&](VarIdx s) {
				if ( s>=n_static_stems && for_name ) {
					::string const& k = stems[s].first ;
					// when matching on job name, star stems are matched as they are reported to user
					::string r = k.front()=='<'&&k.back()=='>' ? "{*}"s : cat('{',k,"*}") ;
					pattern.emplace_back(r,Maybe/*capture*/) ;
					return Re::escape(r) ;
				}
				if (res.groups[s]) {                                // already seen, we must protect against following text potentially containing numbers
					::string r = cat('\\',res.groups[s]) ;
					pattern.emplace_back( r , No/*capture*/ ) ;
					return cat("(?:",r,')') ;
				}
				bool capture = s<n_static_stems || me.captures[s] ; // star stems are only captured if back referenced
				if (capture) res.groups[s] = cur_group ;
				cur_group += capture+stem_n_marks[s] ;
				pattern.emplace_back( stems[s].second , No|capture ) ;
				return cat(capture?"(":"(?:",stems[s].second,')') ;
			}
		,	[&](::string const& s) {
				pattern.emplace_back( s , Maybe ) ;
				return Re::escape(s) ;
			}
		) ;
		res.re = {pattern,true/*cache*/} ;                          // stem regexprs have been validated, normally there is no error here
		return res ;
	}

	void RuleData::new_job_report( Delay exe_time , CoarseDelay cost , Tokens1 tokens1 ) const {
		if (stats_weight<RuleWeight) stats_weight++ ;
		//
		Delay::Tick cost_per_token_delta = Delay(cost).val()/(tokens1+1) - cost_per_token.val() ;
		Delay::Tick exe_time_delta       = exe_time   .val()             - exe_time      .val() ;
		int64_t     tokens1_32_delta     = (uint64_t(tokens1)<<32)       - tokens1_32           ;
		//
		cost_per_token += Delay(New,cost_per_token_delta/stats_weight) ;
		exe_time       += Delay(New,exe_time_delta      /stats_weight) ;
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
			for( auto const& [k,me] : matches ) patterns.push_back(_mk_pattern(me,false/*for_name*/)) ;
			//
		} catch (::string const& e) {
			throw cat("while processing ",user_name()," :\n",indent(e)) ;
		}
		trace("done",patterns.size()) ;
	}

	//
	// pretty print RuleData
	//

	template<class T> static ::string _pretty_vmap( ::string const& title , ::vmap_s<T> const& m , bool uniq=false ) {
		if (!m) return {} ;
		::string res  ;
		size_t   wk   = ::max<size_t>( m , [](auto const& k_v) { return k_v.first.size() ; } ) ;
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
			size_t wk = ::max<size_t>( m_d.first , [](auto const& k_v) { return k_v.first.size() ; } ) ;
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
			SWEAR(+v.phys_s) ;
			if (v.phys_s.size()==1) {
				SWEAR(!v.copy_up) ;
				res <<' '<< no_slash(v.phys_s[0]) ;
			} else {
				::vector_s phys ; for( ::string const& p_s : v.phys_s ) phys.push_back(no_slash(p_s)) ;
				size_t w = +v.copy_up ? 7 : 5 ;
				/**/            res <<"\n\t\t" << widen("upper"  ,w) <<" : "<<         phys[0]                ;
				/**/            res <<"\n\t\t" << widen("lower"  ,w) <<" : "<< ::span(&phys[1],phys.size()-1) ;
				if (+v.copy_up) res <<"\n\t\t" << widen("copy_up",w) <<" : "<< v.copy_up                      ;
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
					,	[&](VarIdx s) { return cat('{',stems[s].first,s<n_static_stems?"":"*",'}') ; }
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
		if (+d.entry().glbs_str  )   res << "\t<globals> :\n" << indent(with_nl(d.entry().glbs_str)+d.entry().dbg_info,2) <<add_nl ;
		if (+d.entry().code_str  )   res << "\t<code> :\n"    << indent(        d.entry().code_str                    ,2) <<add_nl ;
		return res ;
	}

	::string RuleData::pretty_str() const {
		::string  title       ;
		::vmap_ss entries     ;
		::string  job_name_   = job_name ;
		::string  interpreter ;
		::string  kill_sigs   ;
		//
		{	title = user_name() + " :" ;
			switch (special) {
				case Special::Anti       : title << " AntiRule"   ; break ;
				case Special::GenericSrc : title << " SourceRule" ; break ;
				case Special::Plain      :                          break ;
			DF}
			for( bool star : {false,true} )
				for( VarIdx mi : matches_iotas[star][+MatchKind::Target] )
					if (job_name_==matches[mi].second.pattern) { job_name_ = "<targets."+matches[mi].first+'>' ; break ; }
		}
		if (is_plain()) {
			if (!(
				start_cmd_attrs.spec.interpreter.size() == 1
			&&	start_cmd_attrs.spec.interpreter[0]     == (is_python?"$PYTHON":"$SHELL")
			)) {
				First first ;
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
		// first simple static attrs
		{	if ( user_prio!=0                                          ) entries.emplace_back( "prio"                , cat        (user_prio                                          ) ) ;
			/**/                                                         entries.emplace_back( "job_name"            ,             job_name_                                            ) ;
			if (+sub_repo_s                                            ) entries.emplace_back( "sub_repo"            , no_slash   (sub_repo_s                                         ) ) ;
		}
		if (is_plain()) {
			if ( start_cmd_attrs       .spec.auto_mkdir                ) entries.emplace_back( "auto_mkdir"          , cat        (start_cmd_attrs       .spec.auto_mkdir             ) ) ;
			/**/                                                         entries.emplace_back( "autodep"             , snake      (start_rsrcs_attrs     .spec.method                 ) ) ;
			if ( submit_rsrcs_attrs    .spec.backend!=BackendTag::Local) entries.emplace_back( "backend"             , snake      (submit_rsrcs_attrs    .spec.backend                ) ) ;
			if (+submit_ancillary_attrs.spec.cache_name                ) entries.emplace_back( "cache"               ,             submit_ancillary_attrs.spec.cache_name               ) ;
			if ( start_rsrcs_attrs     .spec.chk_abs_paths             ) entries.emplace_back( "check_abs_paths"     , cat        (start_rsrcs_attrs     .spec.chk_abs_paths          ) ) ;
			if (+start_cmd_attrs       .spec.chroot_dir_s              ) entries.emplace_back( "chroot_dir"          , no_slash   (start_cmd_attrs       .spec.chroot_dir_s           ) ) ;
			if (+start_rsrcs_attrs     .spec.chroot_actions            ) entries.emplace_back( "chroot_actions"      , cat        (start_rsrcs_attrs     .spec.chroot_actions         ) ) ;
			if (+start_ancillary_attrs .spec.zlvl                      ) entries.emplace_back( "compression"         , cat        (start_ancillary_attrs .spec.zlvl                   ) ) ;
			if ( force                                                 ) entries.emplace_back( "force"               , cat        (force                                              ) ) ;
			if (+interpreter                                           ) entries.emplace_back( "interpreter"         ,             interpreter                                          ) ;
			if ( start_ancillary_attrs .spec.keep_tmp                  ) entries.emplace_back( "keep_tmp"            , cat        (start_ancillary_attrs .spec.keep_tmp               ) ) ;
			if (+start_ancillary_attrs .spec.kill_daemons              ) entries.emplace_back( "kill_daemons"        , cat        (start_ancillary_attrs .spec.kill_daemons           ) ) ;
			if (+start_ancillary_attrs .spec.kill_sigs                 ) entries.emplace_back( "kill_sigs"           ,             kill_sigs                                            ) ;
			if (+start_rsrcs_attrs     .spec.lmake_root_s              ) entries.emplace_back( "lmake_root"          , no_slash   (start_rsrcs_attrs     .spec.lmake_root_s           ) ) ;
			if (+start_cmd_attrs       .spec.job_space.lmake_view_s    ) entries.emplace_back( "lmake_view"          , no_slash   (start_cmd_attrs       .spec.job_space.lmake_view_s ) ) ;
			if ( n_losts                                               ) entries.emplace_back( "max_retries_on_lost" , ::to_string(n_losts                                            ) ) ;
			if ( start_ancillary_attrs .spec.max_stderr_len            ) entries.emplace_back( "max_stderr_len"      , ::to_string(start_ancillary_attrs .spec.max_stderr_len         ) ) ;
			if ( n_runs                                                ) entries.emplace_back( "max_runs"            , ::to_string(n_runs                                             ) ) ;
			if ( n_submits                                             ) entries.emplace_back( "max_submits"         , ::to_string(n_submits                                          ) ) ;
			if ( start_cmd_attrs       .spec.mount_chroot_ok           ) entries.emplace_back( "mount_chroot_ok"     , cat        (start_cmd_attrs       .spec.mount_chroot_ok        ) ) ;
			if ( start_rsrcs_attrs     .spec.readdir_ok                ) entries.emplace_back( "readdir_ok"          , cat        (start_rsrcs_attrs     .spec.readdir_ok             ) ) ;
			if (+start_cmd_attrs       .spec.job_space.repo_view_s     ) entries.emplace_back( "repo_view"           , no_slash   (start_cmd_attrs       .spec.job_space.repo_view_s  ) ) ;
			if (+start_ancillary_attrs .spec.start_delay               ) entries.emplace_back( "start_delay"         ,             start_ancillary_attrs .spec.start_delay.short_str()  ) ;
			if ( start_cmd_attrs       .spec.stderr_ok                 ) entries.emplace_back( "stderr_ok"           , cat        (start_cmd_attrs       .spec.stderr_ok              ) ) ;
			if (+start_rsrcs_attrs     .spec.timeout                   ) entries.emplace_back( "timeout"             ,             start_rsrcs_attrs     .spec.timeout.short_str()      ) ;
			if (+start_cmd_attrs       .spec.job_space.tmp_view_s      ) entries.emplace_back( "tmp_view"            , no_slash   (start_cmd_attrs       .spec.job_space.tmp_view_s   ) ) ;
			if ( start_rsrcs_attrs     .spec.use_script                ) entries.emplace_back( "use_script"          , cat        (start_rsrcs_attrs     .spec.use_script             ) ) ;
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
		if (is_plain()) {
			res << indent(_pretty_deps (                                           )) ;
			res << indent(_pretty_vmap ("resources :",submit_rsrcs_attrs.spec.rsrcs)) ;
			res << indent(_pretty_views(                                           )) ;
			res << indent(_pretty_env  (                                           )) ;
		}
		// then dynamic part
		if (is_plain()) {
			res << indent(_pretty_dyn(deps_attrs            )) ;
			res << indent(_pretty_dyn(submit_rsrcs_attrs    )) ;
			res << indent(_pretty_dyn(submit_ancillary_attrs)) ;
			res << indent(_pretty_dyn(start_cmd_attrs       )) ;
			res << indent(_pretty_dyn(start_rsrcs_attrs     )) ;
			res << indent(_pretty_dyn(start_ancillary_attrs )) ;
			res << indent(_pretty_dyn(cmd                   )) ;
		}
		// and finally the cmd
		if ( is_plain() && cmd.entry().kind<DynKind::Dyn ) {
			if (is_python) res << indent("cmd :\n") << indent(with_nl(cmd.entry().glbs_str+cmd.entry().dbg_info+cmd.entry().code_str) ,2) ;
			else           res << indent("cmd :\n") << indent(with_nl(_pretty_fstr(                             cmd.entry().code_str)),2) ;
		}
		return res ;
	}

	::vector_s RuleData::_list_ctx(::vector<CmdIdx> const& ctx) const {
		::vector_s res ; res.reserve(ctx.size()) ;
		for( auto [vc,i] : ctx ) switch (vc) {
			case VarCmd::Stem      : res.push_back   (stems                        [i].first) ; break ;
			case VarCmd::StarMatch :
			case VarCmd::Match     : res.push_back   (matches                      [i].first) ; break ;
			case VarCmd::Dep       : res.push_back   (deps_attrs        .spec .deps[i].first) ; break ;
			case VarCmd::Rsrc      : res.push_back   (submit_rsrcs_attrs.spec.rsrcs[i].first) ; break ;
			case VarCmd::Stems     : res.emplace_back("stems"                               ) ; break ;
			case VarCmd::Targets   : res.emplace_back("targets"                             ) ; break ;
			case VarCmd::Deps      : res.emplace_back("deps"                                ) ; break ;
			case VarCmd::Rsrcs     : res.emplace_back("resources"                           ) ; break ;
		DF}                                                                                             // NO_COV
		return res ;
	}

	// crc->match is an id of the rule : a new rule is a replacement of an old rule if it has the same crc->match
	// also, 2 rules matching identically is forbidden : the idea is that one is useless
	// this is not strictly true, though : you could imagine a rule generating a* from b, another generating a* from b but with disjoint sets of a*
	// although awkward & useless (as both rules could be merged), this can be meaningful
	// if the need arises, we will add an "id" artificial field entering in crc->match to distinguish them
	void RuleData::_set_crcs(RulesBase const& rules) {
		if (is_plain()) SWEAR(+rules) ;
		Hash::Xxh h ;                                                  // each crc continues after the previous one, so they are standalone
		//
		// START_OF_VERSIONING REPO
		::vmap_s<bool> targets ;
		for( bool star : {false,true} )
			for( VarIdx mi : matches_iotas[star][+MatchKind::Target] ) // targets (static and star) must be kept first in matches so RuleTgt is stable when match_crc is stable
				targets.emplace_back( matches[mi].second.pattern , matches[mi].second.flags.extra_tflags[ExtraTflag::Optional] ) ; // keys and flags have no influence on matching, except Optional
		h += special ;                                                                                                             // in addition to distinguishing special from other, ...
		h += stems   ;                                                                                                             // ... this guarantees that shared rules have different crc's
		h += targets ;
		deps_attrs.update_hash( /*inout*/h , rules ) ;                                                                             // no deps for source & anti
		if (is_plain()) h += job_name  ;
		else            h += allow_ext ; // only exists for special rules
		Crc match_crc = h.digest() ;
		//
		if (!is_plain()) {               // no cmd nor resources for special rules
			crc = {match_crc} ;
			return ;
		}
		h += g_config->lnk_support  ;    // this has an influence on generated deps, hence is part of cmd def
		h += g_config->os_info      ;    // this has an influence on job execution , hence is part of cmd def
		h += sub_repo_s             ;
		h += Node::s_src_dirs_crc() ;    // src_dirs influences deps recording
		h += matches                ;    // these define names and influence cmd execution, all is not necessary but simpler to code
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
		// END_OF_VERSIONING
	}

}
