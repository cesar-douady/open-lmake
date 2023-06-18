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
			if (_g_specials.find(c)!=NPos) res.push_back('\\') ;               // escape specials
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
	using ParsePyFunc = ::function<string( string const& key , bool star , bool unnamed , string const* re )> ;
	static ::string _parse_py( ::string const& str , bool allow_re , size_t* unnamed_star_idx , ParsePyFunc const& cb ) {
		enum { Literal , SeenStart , Key , Re , SeenStop } state = Literal ;
		::string res ;
		::string key ;
		::string re  ;
		size_t unnamed_idx = 1 ;
		size_t depth = 0 ;
		for( char c : str ) {
			bool with_re = false ;
			switch (state) {
				case Literal :
					if      (c=='{') state = SeenStart ;
					else if (c=='}') state = SeenStop  ;
					else             res.push_back(c) ;
				break ;
				case SeenStop :
					if (c!='}') goto End ;
					state = Literal ;
					res.push_back(c) ;                                         // }} are transformed into }
				break ;
				case SeenStart :
					if (c=='{') {
						state = Literal ;
						res.push_back(c) ;                                     // {{ are transformed into {
						break ;
					}
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
					if (!allow_re) throw to_string("no stem definition allowed in ",str) ;
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
					if (with_re) res += cb(key,star,unnamed,&re    ) ;
					else         res += cb(key,star,unnamed,nullptr) ;
					key.clear() ;
					re .clear() ;
					state = Literal ;
				break ;
			}
		}
	End :
		switch (state) {
			case Literal   : return res ;
			case SeenStop  : throw to_string("spurious } in ",str) ;
			case SeenStart :
			case Key       :
			case Re        : throw to_string("spurious { in ",str) ;
			default : FAIL(state) ;
		}
	}

	// star stems are represented by a StemMrkr followed by the stem idx
	// cb is called on each stem found
	// return str with stems substituted with the return value of cb and special characters outside stems escaped if escape==true
	using SubstTargetFunc = ::function<string( FileNameIdx pos , VarIdx stem )> ;
	static ::string _subst_target( ::string const& str , SubstTargetFunc const& cb , bool escape=false , VarIdx stop_above=-1 ) {
		::string res   ;
		uint8_t  state = 0 ;                                                   // may never be above 72
		VarIdx   stem  = 0 ;
		for( char c : str ) {
			if (state>0) {
				stem |= VarIdx(uint8_t(c))<<((state-1)*8) ;
				if (state<sizeof(VarIdx)) { state++ ; continue ; }
				if (stem>=stop_above) return res ;
				res += cb(res.size(),stem) ;
				state = 0 ;
				stem  = 0 ;
			} else {
				if (c==Rule::StemMrkr) { state = 1 ; continue ; }
				if ( escape && _g_specials.find(c)!=NPos ) res += '\\' ;
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
	// DepSpec
	//

	::ostream& operator<<( ::ostream& os , RuleData::DepSpec const& ds ) {
		return os << "DS(" << ds.pattern <<','<< ds.is_code << ')' ;
	}

	//
	// DepsSpec
	//

	::ostream& operator<<( ::ostream& os , RuleData::DepsSpec const& ds ) {
		return os << "DsS(" << ds.dct << ')' ;
	}

	//
	// EnvSpec
	//

	::ostream& operator<<( ::ostream& os , RuleData::EnvSpec const& es ) {
		return os << "ES(" << es.val <<','<< es.flag << ')' ;
	}

	//
	// Rule
	//

	::ostream& operator<<( ::ostream& os , Rule const r ) {
		os << "R(" ;
		if (+r) os << +r ;
		return os << ')' ;
	}

	void Rule::new_job_exec_time( Delay exec_time , Tokens tokens ) {
		if((*this)->stats_weight<RuleWeight) (*this)->stats_weight++ ;
		Delay delta = (exec_time-(*this)->exec_time) / (*this)->stats_weight ;
		(*this)->exec_time += delta ;
		for( Req req : Req::s_reqs_by_start ) req.inc_rule_exec_time(*this,delta,tokens) ;
	}

	//
	// RuleTgt
	//

	::ostream& operator<<( ::ostream& os , RuleTgt const rt ) {
		return os << "RT(" << Rule(rt) <<':'<< int(rt.tgt_idx) << ')' ;
	}

	//
	// RuleData
	//

	size_t RuleData::s_name_sz = 0 ;

	RuleData::RuleData(Special s) {
		prio            = Infinity    ;                                        // by default, rule is alone and this value has no impact
		name            = mk_snake(s) ;
		all_deps_static = true        ;                                        // for those which have deps, they certainly need them
		switch (s) {
			case Special::Src      :                    force   = true ;               break ; // Force so that source files are systematically inspected
			case Special::Req      :                    force   = true ;               break ;
			case Special::Uphill   : prio = +Infinity ;                  anti = true ; break ; // +inf : there may be other rules after , AllDepsStatic : dir must exist to apply rule
			case Special::Infinite : prio = -Infinity ; no_deps = true ;               break ; // -inf : it can appear after other rules, NoDep         : deps contains the chain
			default : FAIL(s) ;
		}
		_update_sz() ;
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
			auto add_cwd = [&](Py::Object const& py_txt) -> ::string {
				::string txt = Py::String(py_txt) ;
				if      (txt[0]=='/' ) return txt.substr(1)          ;
				else if (!cwd.empty()) return to_string(cwd,'/',txt) ;
				else                   return txt                    ;
			} ;
			//
			field = "__anti__" ; if (dct.hasKey(field)) anti = Py::Object(dct[field]).as_bool() ;
			field = "name"     ; if (dct.hasKey(field)) name = Py::String(dct[field])           ; else throw "not found"s ;
			field = "prio"     ; if (dct.hasKey(field)) prio = Py::Float (dct[field])           ;
			field = "cwd"      ; if (dct.hasKey(field)) cwd  = Py::String(dct[field])           ;
			if (!cwd.empty()) {
				if (cwd.back ()!='/') cwd += '/' ;
				if (cwd.front()=='/') {
					if (cwd.starts_with(*g_root_dir+'/')) cwd.erase(0,g_root_dir->size()+1) ;
					else                                  throw "cwd must be relative to root dir"s ;
				}
				if (!cwd.empty()) {
					prio += g_config.sub_prio_boost * ::count(cwd.begin(),cwd.end(),'/') ;
					cwd.pop_back() ;                                                            // cwd could have been emptied by previous line
				}
			}
			//
			Trace trace("_acquire_py",name,prio) ;
			//
			::umap_ss stem_map     ;
			::set_s   static_stems ;                                           // ordered so that stems is ordered
			::set_s   star_stems   ;                                           // .
			field = "stems" ;
			if (dct.hasKey(field))
				for( auto const& [k,v] : Py::Dict(dct[field]) ) stem_map[Py::String(k)] = Py::String(v) ; // the real stems are restricted to what is necessary for job_name & targets
			//
			// augment stems with definitions found in job_name and targets
			size_t unnamed_star_idx = 1 ;                                      // free running while walking over job_name + targets
			auto augment_stems = [&]( ::string const& k , bool star , bool /*unnamed*/ , ::string const* re ) -> ::string {
				if ( star && k.empty() ) {
					if (!re) throw "unnamed star stems must be defined"s ;
					return {} ;
				}
				if (re) {
					if (stem_map.contains(k)) { if (stem_map.at(k)!=*re) throw to_string("2 different definitions for stem ",k," : ",stem_map.at(k)," and ",*re) ; }
					else                      { stem_map[k] = *re ;                                                                                                }
				}
				return {} ;
			} ;
			field = "job_name" ;
			if (!dct.hasKey(field)) throw "not found"s ;
			job_name = add_cwd(dct[field]) ;
			_parse_py( job_name , true/*allow_re*/ , &unnamed_star_idx , augment_stems ) ;
			field = "targets" ;
			if (!dct.hasKey(field)) throw "not found"s ;
			Py::Dict py_targets{dct[field]} ;
			::string job_name_or_key = "job_name" ;
			for( auto const& [py_k,py_tfs] : py_targets ) {
				field = Py::String(py_k) ;
				Py::Sequence pyseq_tfs{ py_tfs } ;                             // targets are a tuple (target_pattern,flags...)
				::string target = add_cwd(pyseq_tfs[0]) ;
				// avoid processing target if it is identical to job_name
				// this is not an optimization, it is to ensure unnamed_star_idx's match
				if (target==job_name) job_name_or_key = field ;
				else                  _parse_py( target , true/*allow_re*/ , &unnamed_star_idx , augment_stems ) ;
			}
			//
			// gather job_name and targets
			field            = "job_name" ;
			unnamed_star_idx = 1          ;                                    // reset free running at each pass over job_name+targets
			bool job_name_is_star = false ;
			_parse_py( job_name , true/*allow_re*/ , &unnamed_star_idx ,
				[&]( ::string const& k , bool star , bool unnamed , ::string const* re ) -> ::string {
					if      ( star && unnamed     ) stem_map[k] = *re ;
					else if (!stem_map.contains(k)) throw to_string("found undefined stem ",k," in ",job_name_or_key) ;
					if      (star                 ) { star_stems  .insert(k) ; job_name_is_star = true ; }
					else                            { static_stems.insert(k) ;                           }
					return {} ;
				}
			) ;
			//
			field = "targets" ;
			bool found_matching = false ;
			{	::vmap_s<TargetSpec> star_targets ;                            // defer star targets so that static targets are put first
				for( auto const& [py_k,py_tfs] : Py::Dict(dct[field]) ) {      // targets are a tuple (target_pattern,flags...)
					field = Py::String(py_k) ;
					Py::Sequence pyseq_tfs      { py_tfs }              ;
					bool         is_native_star = false                 ;
					::string     target         = add_cwd(pyseq_tfs[0]) ;
					::set_s      missing_stems  ;
					// avoid processing target if it is identical to job_name
					// this is not an optimization, it is to ensure unnamed_star_idx's match
					if (target==job_name) {
						if (job_name_is_star) is_native_star = true ;
					} else {
						missing_stems = static_stems ;
						_parse_py( target , true/*allow_re*/ , &unnamed_star_idx ,
							[&]( ::string const& k , bool star , bool unnamed , ::string const* re ) -> ::string {
								if      ( star && unnamed           ) { stem_map[k] = *re ;                                                           }
								else if ( !stem_map.contains(k)     ) { throw to_string("found undefined stem ",k," in target") ;                     }
								if      ( star                      ) { star_stems.insert(k) ; is_native_star = true ;                                }
								else if ( !static_stems.contains(k) ) { throw to_string("stem ",k," appears in target but not in ",job_name_or_key) ; }
								else                                  { missing_stems.erase(k) ;                                                      }
								return {} ;
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
						if (!missing_stems.empty()   ) throw to_string("missing stems ",missing_stems," in target") ;
						found_matching = true ;
					} else {
						if (anti                     ) throw "non-matching targets are meaningless for anti-rules"s ;
					}
					if (field=="<stdout>") {
						if (flags[TFlag::Star       ]) throw "stdout cannot be directed to a star target"s        ;
						if (flags[TFlag::Phony      ]) throw "stdout cannot be directed to a phony target"s       ;
						if (flags[TFlag::Incremental]) throw "stdout cannot be directed to a incremental target"s ;
					}
					chk(flags) ;
					// record
					(flags[TFlag::Star]?star_targets:targets).emplace_back( field , TargetSpec(target,is_native_star,flags) ) ;
				}
				n_static_targets = targets.size()        ;
				has_stars        = !star_targets.empty() ;
				if (!anti) for( auto& ts : star_targets ) targets.push_back(::move(ts)) ; // star-targets are meaningless for an anti-rule
			}
			SWEAR(found_matching) ;                                                       // we should not have come until here without a clean target
			field = "" ;
			if (targets.size()>NoVar) throw to_string("too many targets : ",targets.size()," > ",NoVar) ;
			// keep only useful stems and order them : static first, then star
			::umap_s<VarIdx> stem_idxs ;
			for( ::string const& k : static_stems ) { stem_idxs[k] = VarIdx(stems.size()) ; stems.emplace_back(k,stem_map.at(k)) ; }
			for( ::string const& k : star_stems   ) { stem_idxs[k] = VarIdx(stems.size()) ; stems.emplace_back(k,stem_map.at(k)) ; }
			n_static_stems = static_stems.size() ;
			if (stems.size()>NoVar) throw to_string("too many stems : ",stems.size()," > ",NoVar) ;
			//
			// reformat job_name & targets to improve matching efficiency
			// {Stem} is replaced by "StemMrkr<stem_idx>"
			// StemMrkr is there to unambiguously announce a stem idx
			//
			auto mk_stem_ = [&]( ::string const& k , bool /*star*/=false , bool /*unnamed*/=false , ::string const* /*re*/=nullptr )->::string {
				VarIdx   s   = stem_idxs.at(k)                     ;
				::string res ( 1+sizeof(VarIdx) , Rule::StemMrkr ) ;
				from_int( &res[1] , s ) ;
				return res ;
			} ;
			unnamed_star_idx = 1 ;                                                               // reset free running at each pass over job_name+targets
			::string orig_job_name = ::move(job_name) ;
			job_name = _parse_py( orig_job_name , true/*allow_re*/ , &unnamed_star_idx , mk_stem_ ) ;
			// compile potential conflicts as there are rare and rather expensive to detect, we can avoid most of the verifications by statically analyzing targets
			for( VarIdx t=0 ; t<targets.size() ; t++ ) {
				TargetSpec& tf = targets[t].second ;
				// avoid processing target if it is identical to job_name
				// this is not an optimization, it is to ensure unnamed_star_idx's match
				if (tf.pattern==orig_job_name) tf.pattern = job_name                                                                  ;
				else                           tf.pattern = _parse_py( tf.pattern , true/*allow_re*/ , &unnamed_star_idx , mk_stem_ ) ;
				for( VarIdx t2=0 ; t2<t ; t2++ )
					if ( _may_conflict( n_static_stems , tf.pattern , targets[t2].second.pattern ) ) { trace("conflict",t,t2) ; tf.conflicts.push_back(t2) ; }
			}
			//
			//vvvvvvvvvvvvvv
			if (anti) return ;                                            // if Anti, we only need essential info
			//^^^^^^^^^^^^^^
			//
			// now process fields linked to execution
			//
			field = "allow_stderr" ; if (dct.hasKey(field)) allow_stderr   =                            Py::Object(dct[field]).as_bool() ;
			field = "auto_mkdir"   ; if (dct.hasKey(field)) auto_mkdir     =                            Py::Object(dct[field]).as_bool() ;
			field = "backend"      ; if (dct.hasKey(field)) backend        = mk_enum<Backends::Tag>    (Py::String(dct[field]))          ;
			field = "chroot"       ; if (dct.hasKey(field)) chroot         =                            Py::String(dct[field])           ;
			field = "ete"          ; if (dct.hasKey(field)) exec_time      = Delay                     (Py::Float (dct[field]))          ;
			field = "force"        ; if (dct.hasKey(field)) force          =                            Py::Object(dct[field]).as_bool() ;
			field = "ignore_stat"  ; if (dct.hasKey(field)) ignore_stat    =                            Py::Object(dct[field]).as_bool() ;
			field = "is_python"    ; if (dct.hasKey(field)) is_python      =                            Py::Object(dct[field]).as_bool() ; else throw "not found"s ;
			field = "keep_tmp"     ; if (dct.hasKey(field)) keep_tmp       =                            Py::Object(dct[field]).as_bool() ;
			field = "script"       ; if (dct.hasKey(field)) script         =                            Py::String(dct[field])           ; else throw "not found"s ;
			field = "stderr_len"   ; if (dct.hasKey(field)) stderr_len     = static_cast<unsigned long>(Py::Long  (dct[field]))          ;
			field = "start_delay"  ; if (dct.hasKey(field)) start_delay    = Delay                     (Py::Float (dct[field]))          ;
			//
			field = "autodep" ;
			if (!dct.hasKey(field)) throw "not found"s ;
			autodep_method = mk_enum<AutodepMethod>(Py::String(dct[field])) ;
			switch (autodep_method) {
				case AutodepMethod::None      :                                                                                         break ;
				case AutodepMethod::Ptrace    : if (!HAS_PTRACE  ) throw to_string(autodep_method," is not supported on this system") ; break ;
				case AutodepMethod::LdAudit   : if (!HAS_LD_AUDIT) throw to_string(autodep_method," is not supported on this system") ; break ;
				case AutodepMethod::LdPreload :                                                                                         break ;
				default : throw to_string("unexpected value : ",autodep_method) ;
			}
			//
			field = "timeout" ;
			if (dct.hasKey(field)) {
				timeout = Delay(Py::Float (dct[field])) ;
				if (timeout<Delay()) throw "value must be positive or null (no timeout)"s ;
			}
			//
			field = "n_tokens" ;
			if (dct.hasKey(field)) {
				n_tokens = static_cast<unsigned long>(Py::Long(dct[field])) ;
				if (n_tokens==0) throw "value must be positive"s ;
			}
			//
			field = "env" ;
			if (!dct.hasKey(field)) throw "not found"s ;
			for( auto const& [py_k,py_ef] : Py::Dict(dct[field]) ) {
				field = Py::String(py_k) ;
				Py::Sequence pyseq_ef{py_ef} ;
				if (pyseq_ef.size()!=2) throw to_string(pyseq_ef," is not a pair") ;
				env.emplace_back( field , EnvSpec( pyseq_ef[0].str() , mk_enum<EnvFlag>(pyseq_ef[1].str()) ) ) ;
			}
			::sort( env , []( ::pair_s<EnvSpec> const& a , ::pair_s<EnvSpec> const& b ) { return a.first<b.first ; } ) ;
			//
			field = "interpreter" ;
			if (dct.hasKey(field)) for( auto const& v : Py::Sequence(Py::Object(dct[field])) ) interpreter.emplace_back(Py::String(v)) ;
			else                   throw "not found"s ;
			field = "kill_sigs" ;
			if (dct.hasKey(field)) for( auto const& v : Py::Sequence(Py::Object(dct[field])) ) kill_sigs.emplace_back(Py::Long(v)) ;
			else                   throw "not found"s              ;
			if (kill_sigs.empty()) throw "no signal to kill jobs"s ;
			// set var_idxs w/o info about deps to compute deps
			::map_s<pair<CmdVar,VarIdx>> var_idxs = {
				{ "stems"   , {CmdVar::Stems  ,0} }
			,	{ "targets" , {CmdVar::Targets,0} }
			} ;
			for( VarIdx s=0 ; s<stems  .size() ; s++ ) var_idxs[stems  [s].first] = {CmdVar::Stem  ,s} ;
			for( VarIdx t=0 ; t<targets.size() ; t++ ) var_idxs[targets[t].first] = {CmdVar::Target,t} ;
			//
			auto mk_dep = [&](Py::Sequence const& py_df)->DepSpec {
				DFlags flags = _get_flags<DFlag>(2,py_df,StaticDFlags)                      ;
				DepSpec df   { add_cwd(py_df[0]) , Py::Object(py_df[1]).as_bool() , flags } ;
				if (!df.is_code) {
					try {
						df.pattern = _parse_py( df.pattern , false/*allow_re*/ , nullptr/*unnamed_star_idx*/ ,
							[&]( ::string const& k , bool star , bool unnamed , ::string const* )->::string {
								if ( star || unnamed || !static_stems.contains(k) ) throw 0 ;
								return mk_stem_(k) ;
							}
						) ;
					}
					catch(int) { df.is_code = true ; }                         // if dep is too fancy to process ourselves, call Python
				}
				return df ;
			} ;
			auto mk_deps = [&](::string const& f)->DepsSpec {
				DepsSpec res ;
				field = f ;
				if (!dct.hasKey(field)) throw "not found"s ;
				Py::Dict deps{dct[field]} ;
				//
				if (deps.hasKey("prelude")) res.prelude = Py::String(deps["prelude"])  ;
				//
				for( auto const& [k,v] : Py::Dict(deps["dct"]) ) {
					field = Py::String(k) ;
					res.dct.emplace_back(field,mk_dep(Py::Sequence(v))) ;
				}
				field = f ;
				::sort( res.dct.begin() , res.dct.end() ) ;                    // stabilize match crc
				//
				if (res.dct.size()>NoVar) throw to_string("too many ",f," : ",res.dct.size()," > ",NoVar) ;
				if (deps.hasKey("ctx"))
					for( Py::Object v : Py::Sequence(deps["ctx"]) )
						res.ctx.push_back(var_idxs.at(Py::String(v))) ;
				::sort( res.ctx.begin() , res.ctx.end() ) ;                    // stabilize match & rsrcs crc's
				//
				return res ;
			} ;
			// deps
			deps = mk_deps("deps") ;
			// complete var_idxs with info relative to deps to compute rsrcs & tokens env
			var_idxs["deps"] = {CmdVar::Deps,0} ;
			for( VarIdx d=0 ; d<n_deps() ; d++ ) var_idxs[deps.dct[d].first] = {CmdVar::Dep,d} ;
			// rsrcs & tokens
			rsrcs = mk_deps("resources") ;
			field = "job_tokens"         ;
			if (dct.hasKey(field)) job_tokens = mk_dep(dct[field])         ;
			else                   job_tokens = { "1" , false/*is_code*/ } ;
			// complete var_idxs with info relative to rsrcs & tokens to compute cmd context
			var_idxs["job_tokens"] = {CmdVar::Tokens,0} ;
			var_idxs["resources" ] = {CmdVar::Rsrcs ,0} ;
			for( VarIdx r=0 ; r<n_rsrcs() ; r++ ) var_idxs[rsrcs.dct[r].first] = {CmdVar::Rsrc,r} ;
			// cmd_ctx
			field = "cmd_ctx" ;
			for( VarIdx t=0 ; t<targets.size() ; t++ )
				if (targets[t].first=="<stdout>") {
					if (targets[t].second.flags[TFlag::Star]) throw "<stdout> must be a static target"s ;
					cmd_ctx.emplace_back( CmdVar::Stdout , t ) ;                                                    // must be present although not visibly referenced in cmd
				}
			for( VarIdx d=0 ; d<n_deps() ; d++ )
				if (deps.dct[d].first=="<stdin>")
					cmd_ctx.emplace_back( CmdVar::Stdin , d ) ;                // must be present although not visibly referenced in cmd
			if (dct.hasKey(field))
				for( Py::Object v : Py::Sequence(dct[field]) )
					cmd_ctx.push_back(var_idxs.at(Py::String(v))) ;
			::sort( cmd_ctx.begin() , cmd_ctx.end() ) ;                        // stabilize cmd crc
		}
		catch(::string const& e) { throw to_string("while processing ",user_name(),'.',field," :\n"  ,indent(e)     ) ; }
		catch(Py::Exception & e) { throw to_string("while processing ",user_name(),'.',field," :\n\t",e.errorValue()) ; }
	}

	void RuleData::_compile_dep_code( ::string const& key , RuleData::DepSpec& df ) {
		if (!df.is_code) return ;
		df.code = Py_CompileString(                                            // never decref'ed to prevent deallocation at end of execution that generates crashes
			df.pattern.c_str()                                                 // df.pattern is actually a python expression such as fr'toto'
		,	(user_name()+'.'+key).c_str()
		,	Py_eval_input
		) ;
		if (!df.code) {
			PyErr_Print() ;
			PyErr_Clear() ;
			throw to_string("cannot compile f-string for ",key) ;
		}
	}
	void RuleData::_mk_deps( ::string const& key , DepsSpec& ds , bool need_code ) {
		for( auto& [k,df] : ds.dct ) {
			if (df.is_code) need_code = true ;
			_compile_dep_code( k.empty()?"<stdin>"s:k , df ) ;
		}
		if (!need_code) return ;                                               // if a code is seen, we must prepare evaluation environment
		ds.env = PyDict_New() ;                                                // never decref'ed to prevent deallocation at end of execution that generates crashes
		PyDict_SetItemString(ds.env,"__builtins__",PyEval_GetBuiltins()) ;     // provide  builtins as Python 3.6 does not do it for us
		PyObject* res = PyRun_String(
			ds.prelude.c_str()
		,	Py_file_input
		,	ds.env , ds.env
		) ;
		if (!res) {
			PyErr_Print() ;
			PyErr_Clear() ;
			throw to_string("cannot make env to compute ",key," f-strings") ;
		}
		Py_DECREF(res) ;
	} ;
	void RuleData::_compile_derived_info() {
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
						if      ( seen.contains(s)                           ) {                  return to_string("(?P=",stems[s].first,                    ')') ; }
						else if ( s<n_static_stems || seen_twice.contains(s) ) { seen.insert(s) ; return to_string("(?P<",stems[s].first,'>',stems[s].second,')') ; }
						else                                                   {                  return to_string('('   ,                   stems[s].second,')') ; }
					}
				,	true/*escape*/
				) ;
				target_patterns.emplace_back(pattern) ;
				mk_static(target_patterns.back()) ;                            // prevent deallocation at end of execution that generates crashes
			}
			// deps & rsrcs
			_mk_deps( "deps"      , deps  , false/*need_code*/ ) ;
			_mk_deps( "resources" , rsrcs , job_tokens.is_code ) ;             // rsrcs context is used by tokens
			_compile_dep_code("job_tokens",job_tokens) ;
		}
		catch(::string const& e) { throw to_string("while processing ",user_name()," :\n"  ,indent(e)     ) ; }
		catch(Py::Exception & e) { throw to_string("while processing ",user_name()," :\n\t",e.errorValue()) ; }
	}

	static ::string _pretty_stems( size_t i , ::vmap_ss const& m ) {
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
					flags << "conflicts[" ;
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
	static ::string _pretty_deps( RuleData const& rd , size_t i , ::vmap_s<RuleData::DepSpec> const& deps ) {
		OStringStream res      ;
		size_t        wk       = 0 ;
		size_t        wd       = 0 ;
		::umap_ss     patterns ;
		//
		for( auto const& [k,df] : deps ) {
			::string p = df.is_code ? df.pattern : _pretty_pattern( df.pattern , rd.stems , rd.n_static_stems ) ;
			wk          = ::max(wk,k.size()) ;
			wd          = ::max(wd,p.size()) ;
			patterns[k] = ::move(p)          ;
		}
		for( auto const& [k,df] : deps ) {
			OStringStream flags ;
			bool          first = true ;
			for( DFlag f : DFlag::N ) {
				if (f>=DFlag::Private           ) continue ;
				if (df.flags[f]==StaticDFlags[f]) continue ;
				//
				if (first) { flags <<" : " ; first = false ; }
				else       { flags <<" , " ;                 }
				//
				if (!df.flags[f]) flags << '-'         ;
				/**/              flags << mk_snake(f) ;
			}
			res << ::string(i,'\t') << ::setw(wk)<<k <<" : " ;
			::string flags_str = ::move(flags).str() ;
			if (flags_str.empty()) res <<             patterns[k]              ;
			else                   res << ::setw(wd)<<patterns[k] << flags_str ;
			res <<'\n' ;
		}
		return res.str() ;
	}
	static ::string _pretty_env( size_t i , ::vmap_s<RuleData::EnvSpec> const& env ) {
		OStringStream res ;
		size_t        wk  = 0 ;
		size_t        wv  = 0 ;
		for( auto const& [k,ef] : env ) {
			wk = ::max(wk,k     .size()) ;
			wv = ::max(wv,ef.val.size()) ;
		}
		for( auto const& [k,ef] : env ) {
			res << ::string(i,'\t') << ::setw(wk)<<k <<" : " ;
			if   (ef.flag==EnvFlag::Dflt) res <<             ef.val                   ;
			else                          res << ::setw(wv)<<ef.val <<" : "<< ef.flag ;
			res <<'\n' ;
		}
		return res.str() ;
	}
	static ::string _pretty_cmd( size_t i , ::vector_s const& interpreter , ::string const& cmd ) {
		::string res ;
		for( size_t j=0 ; j<i ; j++ ) res+='\t' ;
		res += "#!" ;
		bool first = true ;
		for( ::string const& c : interpreter ) {
			if (first) first  = false ;
			else       res   += ' '   ;
			res += c ;
		}
		res += '\n' ;
		if (!cmd.empty()) {
			res += indent(cmd,i) ;
			if (cmd.back()!='\n') res += '\n' ;
		}
		return res ;
	}
	static ::string _pretty_txt( size_t i , ::string const& s ) {
		if (s.back()=='\n') return '\n'+ indent(s,i) ;
		else                return ' ' + s+'\n'      ;
	}
	static ::string _pretty_ctx( RuleData const& rd , ::vector<pair<CmdVar,VarIdx>> const& ctx ) {
		::string    res ;
		const char* sep = "" ;
		for( auto [cmd_var,idx] : ctx ) {
			res += sep ;
			sep = " , " ;
			switch (cmd_var) {
				case CmdVar::Stem    : res += rd.stems    .at(idx).first ; break ;
				case CmdVar::Target  : res += rd.targets  .at(idx).first ; break ;
				case CmdVar::Dep     : res += rd.deps .dct.at(idx).first ; break ;
				case CmdVar::Rsrc    : res += rd.rsrcs.dct.at(idx).first ; break ;
				case CmdVar::Stdout  : res += "<stdout>"                 ; break ;
				case CmdVar::Stdin   : res += "<stdin>"                  ; break ;
				case CmdVar::Stems   : res += "stems"                    ; break ;
				case CmdVar::Targets : res += "targets"                  ; break ;
				case CmdVar::Deps    : res += "deps"                     ; break ;
				case CmdVar::Rsrcs   : res += "resources"                ; break ;
				case CmdVar::Tokens  : res += "tokens"                   ; break ;
				default : FAIL(cmd_var) ;
			}
		}
		return res ;
	}
	static ::string _pretty_sigs( ::vector<int> const& sigs ) {
		::string    res  ;
		::uset<int> seen ;
		const char* sep  = "" ;
		for( int sig : sigs ) {
			if (sig) {
				res += to_string(sep,sig) ;
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
	::string RuleData::pretty_str() const {
		size_t        key_sz = 0 ;
		OStringStream res    ;
		int           pass   ;
		//
		//
		auto do_field = [&](::string const& key , char sep , ::string const& val )->void {
			if (pass==1) { key_sz = ::max(key_sz,key.size()) ; return ; }       // during 1st pass, compute max key size ;
			//
			/**/                                    res <<'\t'<< ::setw(key_sz)<<key <<" :"<<sep ;
			/**/                                    res << val                                   ;
			if (  val.empty() || val.back()!='\n' ) res << '\n'                                  ;
		} ;
		res << name << " :\n" ;
		if (anti) res << "\tAntiRule\n" ;
		for( pass=1 ; pass<=2 ; pass++ ) {                                     // on 1st pass we compute key size, on 2nd pass we do the job
			//
			/**/                        do_field( "prio"              , ' '  ,  to_string       (        prio              )              ) ;
			if (!stems.empty()        ) do_field( "stems"             , '\n' ,  _pretty_stems   (      2,stems             )              ) ;
			/**/                        do_field( "job_name"          , ' '  ,  _pretty_job_name(*this                     )              ) ;
			/**/                        do_field( "targets"           , '\n' ,  _pretty_targets (*this,2,targets           )              ) ;
			if (anti) continue ;
			if (!deps.prelude.empty() ) do_field( "deps_prelude"      , '\n' ,  _pretty_txt     (      2,deps.prelude      )              ) ;
			if (!deps.ctx    .empty() ) do_field( "deps_context"      , ' '  ,  _pretty_ctx     (*this,  deps.ctx          )              ) ;
			if (!deps.dct    .empty() ) do_field( "deps"              , '\n' ,  _pretty_deps    (*this,2,deps.dct          )              ) ;
			if (force                 ) do_field( "force"             , ' '  ,                           "True"                           ) ;
			/**/                        do_field( "backend"           , ' '  ,  mk_snake        (        backend           )              ) ;
			if (!chroot      .empty() ) do_field( "chroot"            , ' '  ,                           chroot                           ) ;
			if (!cwd         .empty() ) do_field( "cwd"               , ' '  ,                           cwd                              ) ;
			if (!rsrcs.prelude.empty()) do_field( "resources_prelude" , '\n' ,  _pretty_txt     (      2,rsrcs.prelude     )              ) ;
			if (!rsrcs.ctx    .empty()) do_field( "resources_context" , ' '  ,  _pretty_ctx     (*this,  rsrcs.ctx         )              ) ;
			if (!rsrcs.dct    .empty()) do_field( "resources"         , '\n' ,  _pretty_deps    (*this,2,rsrcs.dct         )              ) ;
			if (!env         .empty() ) do_field( "environ"           , '\n' ,  _pretty_env     (      2,env               )              ) ;
			if (auto_mkdir            ) do_field( "auto_mkdir"        , ' '  ,                           "True"                           ) ;
			/**/                        do_field( "autodep"           , ' '  ,  mk_snake        (        autodep_method    )              ) ;
			if (keep_tmp              ) do_field( "keep_tmp"          , ' '  ,                           "True"                           ) ;
			if (ignore_stat           ) do_field( "ignore_stat"       , ' '  ,                           "True"                           ) ;
			if (+start_delay          ) do_field( "start_delay"       , ' '  ,                           start_delay.short_str()          ) ;
			if (!cmd_ctx     .empty() ) do_field( "cmd_context"       , ' '  ,  _pretty_ctx     (*this,  cmd_ctx           )              ) ;
			/**/                        do_field( "cmd"               , '\n' ,  _pretty_cmd     (      2,interpreter,script)              ) ;
			/**/                        do_field( "kill_sigs"         , ' '  ,  _pretty_sigs    (        kill_sigs         )              ) ;
			if (allow_stderr          ) do_field( "allow_stderr"      , ' '  ,                           "True"                           ) ;
			/**/                        do_field( "stderr_len"        , ' '  ,  stderr_len==size_t(-1)?"unlimited"s:to_string(stderr_len) ) ;
			if (+timeout              ) do_field( "timeout"           , ' '  ,                           timeout.short_str()              ) ;
			/**/                        do_field( "job_tokens"        , ' '  ,                           job_tokens.pattern               ) ;
			/**/                        do_field( "n_tokens"          , ' '  ,  to_string       (        n_tokens          )              ) ;
		}
		//
		return res.str() ;
	}

	// this is an id of the rule : a new rule is a replacement of an old rule if it has the same match_crc
	// also, 2 rules matching identically is forbidden : the idea is that one is useless
	// this is not strictly true, though : you could imagine a rule generating a* from b, another generating a* from b but with disjoint sets of a
	// although awkward & useless (as both rules could be merged), this can be meaningful
	// if the need arises, we will add an "id" artificial field entering in match_crc to distinguish them
	Crc RuleData::match_crc() const {
		::vector<TargetSpec> targets_ ;
		static constexpr TFlags MatchFlags{ TFlag::Star , TFlag::Match , TFlag::Dep } ;
		for( auto const& [k,t] : targets ) {
			if (!t.flags[TFlag::Match]) continue ;                             // no influence on matching if not matching, only on execution
			TargetSpec t_ = t ;
			t_.flags &= MatchFlags ;                                           // only these flags are important for matching, others are for execution only
			targets_.push_back(t_) ;                                           // keys have no influence on matching, only on execution
		}
		::vector<DepSpec> deps_ ;
		for( auto const& [k,d] : deps.dct ) {
			DepSpec d_ = d ;
			d_.code = nullptr ;
			deps_.push_back(d_) ;                                              // keys have no influence on matching, only on execution
		}
		Hash::Xxh h ;
		/**/       h.update(anti   ) ;
		if (!anti) h.update(deps_  ) ;
		/**/       h.update(stems  ) ;
		/**/       h.update(targets) ;
		return h.digest() ;
	}
	Crc RuleData::cmd_crc() const {                                            // distinguish execution result within a given match_crc
		::vmap_ss env_ ;
		for( auto const& [k,ef] : env ) if (ef.flag==EnvFlag::Cmd) env_.emplace_back(k,ef.val) ; // env variables marked as Cmd have an influence on cmd
		Hash::Xxh h ;
		h.update(auto_mkdir  ) ;
		h.update(chroot      ) ;
		h.update(cmd_ctx     ) ;
		h.update(cwd         ) ;
		h.update(deps        ) ;                                               // info was only partially captured by match_crc
		h.update(env_        ) ;
		h.update(ignore_stat ) ;
		h.update(interpreter ) ;
		h.update(is_python   ) ;
		h.update(script      ) ;
		h.update(targets     ) ;                                               // .
		return h.digest() ;
	}
	Crc RuleData::rsrcs_crc() const {                                          // distinguish if errors are recoverable within a given match_crc & cmd_crc
		::vmap_ss env_ ;
		for( auto const& [k,ef] : env ) if (ef.flag==EnvFlag::Rsrc) env_.emplace_back(k,ef.val) ; // env variables marked as Rsrc have an influence on resources
		Hash::Xxh h ;
		h.update(allow_stderr) ;                                               // this only changes errors, not result, so it behaves like a resource
		h.update(backend     ) ;
		h.update(env_        ) ;
		h.update(rsrcs       ) ;
		h.update(targets     ) ;                                               // all is not necessary, but simpler to code
		return h.digest() ;
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
			FileNameIdx pos = 0 ;
			FileNameIdx sz  = 0 ;
			pos = to_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			sz  = to_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
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
			if (sep!=NPos) dirs.insert(target.substr(0,sep)) ;
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
		Trace trace("Match::Match",rt,target) ;
		Py::Match m = rt.pattern().match(target) ;
		if (!m) { trace("no_match") ; return ; }
		rule = rt ;
		for( auto const& [k,v] : rt->static_stems() ) stems.push_back(m[k]) ;
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
		SWEAR(_targets.size()>t) ;                                                  // _targets must have been computed up to t at least
		if (rule->flags(t)[TFlag::Star]) return +_target_pattern(t).match(target) ;
		else                             return target==_targets[t]               ;
	}

	void Rule::Match::_compute_deps() const {
		PyObject* ctx  = nullptr ;                                             // lazy evaluate if f-strings are seen
		try {
			for( auto const& [k,d] : rule->deps.dct ) _deps.push_back(_gather_dep( ctx , d , rule->deps , true/*for_deps*/ )) ;
		} catch (...) { Py_XDECREF(ctx) ; throw ; }
		Py_XDECREF(ctx) ;
	}

	void Rule::Match::_compute_rsrcs() const {
		PyObject* ctx = nullptr ;                                              // lazy evaluate if f-strings are seen
		try {
			for( auto const& [k,r] : rule->rsrcs.dct ) _rsrcs.push_back(_gather_dep( ctx , r , rule->rsrcs , false/*for_deps*/ )) ;
			long t = stol(_gather_dep( ctx , rule->job_tokens , rule->rsrcs , false/*for_deps*/ )) ;
			_tokens = t<0 ? 0 : t>Tokens(-1) ? Tokens(-1) : t ;
		} catch (...) { Py_XDECREF(ctx) ; throw ; }
		Py_XDECREF(ctx) ;
	}

	::string Rule::Match::_gather_dep( PyObject*&ctx , RuleData::DepSpec const& dep , RuleData::DepsSpec const& spec , bool for_deps ) const {
		if (!dep.is_code) {
			return _subst_target( dep.pattern , [&](VarIdx s)->::string { SWEAR(s<rule->n_static_stems) ; return stems[s] ; } ) ;
		}
		if (!ctx) {                                                            // resolve lazy evaluation
			ctx = PyDict_New() ;
			for( auto const& [k,i] : spec.ctx ) {
				if (for_deps) SWEAR( k!=CmdVar::Dep && k!=CmdVar::Deps ) ;     // deps are not available when expanding deps
				const char* var ;
				::string    str ;
				::vmap_ss   dct ;
				Rule        r   = rule ;                                       // to shorten lines
				switch (k) {
					case CmdVar::Stem    : var = r->stems   [i].first.c_str() ; str = stems    [i] ;                                                                                      goto Str ;
					case CmdVar::Target  : var = r->targets [i].first.c_str() ; str = targets()[i] ;                                                                                      goto Str ;
					case CmdVar::Dep     : var = r->deps.dct[i].first.c_str() ; str = deps   ()[i] ;                                                                                      goto Str ;
					case CmdVar::Stems   : var="stems"   ; dct.reserve(r->n_static_stems) ; for(VarIdx s=0;s<r->n_static_stems;s++) dct.emplace_back(r->stems   [s].first,stems    [s]) ; goto Dct ;
					case CmdVar::Targets : var="targets" ; dct.reserve(r->targets.size()) ; for(VarIdx t=0;t<r->targets.size();t++) dct.emplace_back(r->targets [t].first,targets()[t]) ; goto Dct ;
					case CmdVar::Deps    : var="deps"    ; dct.reserve(deps().size()    ) ; for(VarIdx d=0;d<deps().size()    ;d++) dct.emplace_back(r->deps.dct[d].first,deps   ()[d]) ; goto Dct ;
					default : FAIL(k) ;
				}
			Str :
				{	PyObject* py_str = PyUnicode_FromString(str.c_str()) ;
					PyDict_SetItemString( ctx , var , py_str ) ;
					Py_DECREF(py_str) ;
					continue ;
				}
			Dct :
				{	PyObject* py_dct = PyDict_New() ;
					for( auto const& [k,v] : dct ) {
						PyObject* py_str = PyUnicode_FromString(v.c_str()) ;
						PyDict_SetItemString( py_dct , k.c_str() , py_str ) ;
						Py_DECREF(py_str) ;
					}
					PyDict_SetItemString( ctx , var , py_dct ) ;
					Py_DECREF(py_dct) ;
				}
			}
		}
		PyObject* py_dep = PyEval_EvalCode(dep.code,spec.env,ctx) ;
		if (!py_dep) {
			PyErr_Print() ;
			PyErr_Clear() ;
			Py_DECREF(ctx) ;
			throw to_string("cannot compute ",for_deps?"deps":"resources") ;
		}
		ssize_t      dep_sz    ;
		const char*  dep_c_str = PyUnicode_AsUTF8AndSize( py_dep , &dep_sz ) ;
		::string     res       { dep_c_str , size_t(dep_sz) }                ; // capture result before Py_DECREF'ing it
		Py_DECREF(py_dep) ;
		return res ;
	}

}
