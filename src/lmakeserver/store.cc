// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // must be first to include Python.h first

#include <tuple>

#include "py.hh"
#include "rpc_job.hh"

using Backends::Backend ;

using namespace Disk ;
using namespace Py   ;

namespace Engine {

	SeqId*                    g_seq_id     = nullptr ;
	StaticUniqPtr<Config    > g_config     ;
	StaticUniqPtr<::vector_s> g_src_dirs_s ;

}

namespace Engine::Persistent {

	//
	// RuleBase
	//

	MatchGen                            RuleBase::s_match_gen = 1 ; // 0 is forbidden as it is reserved to mean !match
	StaticUniqPtr<Rules,MutexLvl::None> RuleBase::s_rules     ;     // almost a ::unique_ptr except we do not want it to be destroyed at the end to avoid problems

	void RuleBase::_s_save() {
		SWEAR(+Rule::s_rules) ;
		AcFd(_g_rules_filename,Fd::Write).write(serialize(*Rule::s_rules)) ;
	}

	void RuleBase::_s_update_crcs() {
		Trace trace("_s_update_crcs") ;
		::umap<Crc,Rule> rule_map ; for( Rule r : rule_lst(true/*with_special*/) ) rule_map[r->crc->match] = r ;
		for( RuleCrc rc : rule_crc_lst() ) {
			RuleCrcData& rcd = rc.data()                ;
			auto         it  = rule_map.find(rc->match) ;
			if (it==rule_map.end()) {
				rcd.rule  = {}                   ;
				rcd.state = RuleCrcState::CmdOld ;
			} else {
				Rule            r  = rule_map.at(rc->match) ;
				RuleData const& rd = *r                     ;
				/**/                                              rcd.rule  = r                      ;
				if      (rc->rsrcs==rd.crc->rsrcs               ) rcd.state = RuleCrcState::Ok       ;
				else if (rc->cmd  !=rd.crc->cmd                 ) rcd.state = RuleCrcState::CmdOld   ;
				else if (rc->state!=RuleCrcState::RsrcsForgotten) rcd.state = RuleCrcState::RsrcsOld ;
				//
				if (+r<+Special::NShared) SWEAR( rcd.state==RuleCrcState::Ok , r,rcd.state ) ;
			}
			trace(rc,*rc) ;
		}
		#ifndef NDEBUG
			for( Rule r : rule_lst(true/*with_special*/) ) SWEAR( r->crc->state==RuleCrcState::Ok && r->crc->rule==r , r,r->crc->rule ) ;
		#endif
	}

	void RuleBase::_s_set_rules() {
		Gil gil ;
		if (+Rule::s_rules) { py_set_sys("path",*Rule::s_rules->py_sys_path) ; s_rules->compile() ; }
		else                { py_reset_sys_path()                            ;                      } // use default sys.path
	}

	void RuleBase::s_from_disk() {
		Trace trace("s_from_disk") ;
		//
		try        { s_rules = new Rules{deserialize<Rules>(AcFd(_g_rules_filename).read())} ; }
		catch(...) { s_rules = nullptr ;                                                       }
		_s_set_rules() ;
		//
		trace("done") ;
	}

	void RuleBase::s_from_vec_dyn(Rules&& new_rules) {
		static StaticUniqPtr<Rules> s_prev_rules ;                                                // keep prev rules in case some on-going activity refers rules while being updated
		Trace trace("s_from_vec_dyn",new_rules.size()) ;
		SWEAR(s_rules->sys_path_crc==new_rules.sys_path_crc) ;                                    // may not change dynamically as this would potentially change rule cmd's
		SWEAR(s_rules->size()      ==new_rules.size()      ) ;                                    // may not change dynamically
		//
		::umap<Crc,RuleData*> rule_map ; for( RuleData& rd : new_rules ) rule_map.try_emplace( rd.crc->match , &rd ) ;
		//
		s_prev_rules = new Rules{New} ; s_prev_rules->reserve(s_rules->size()) ;                  // s_prev_rules is temporarily used to store new rules
		for( Rule r : rule_lst() ) s_prev_rules->push_back(::move(*rule_map.at(r->crc->match))) ; // crc->match's must be identical between old and new or we should be here
		s_prev_rules->dyn_vec     = ::move(new_rules.dyn_vec    ) ;
		s_prev_rules->py_sys_path = ::move(new_rules.py_sys_path) ;
		s_prev_rules->compile() ;
		//
		Rules* sav_rules    = &*s_rules            ;                                              // exchange s_rules and s_prev_rules, making s_prev_rules the previous rules
		/**/   s_rules      = ::move(s_prev_rules) ;                                              // .
		/**/   s_prev_rules = sav_rules            ;                                              // .
		//
		_s_save() ;
		trace("done") ;
	}

	void RuleBase::s_from_vec_not_dyn(Rules&& new_rules) {
		Trace trace("s_from_vec_not_dyn",new_rules.size()) ;
		s_rules = new Rules{::move(new_rules)} ;
		_s_set_rules  () ;
		_s_save       () ;
		_s_update_crcs() ;
		trace("done") ;
	}

	//
	// RuleCrcBase
	//

	::umap<Crc,RuleCrc> RuleCrcBase::s_by_rsrcs ;

	//
	// NodeBase
	//

	Mutex<MutexLvl::Node> NodeBase::_s_mutex ;

	NodeBase::NodeBase( ::string const& name_ , bool no_dir ) {
		SWEAR( +name_ && is_canon(name_) , name_ ) ;
		if (no_dir) {
			Name nn = _g_name_file.insert(name_) ;
			self = _g_name_file.c_at(nn).node() ;
			if (!self) {
				Lock lock { _s_mutex } ;
				JobNode& jn = _g_name_file.at     (nn) ;
				if (!jn) jn = _g_node_file.emplace(nn) ;                                       // test jn in case it has been created by another thread
				self = jn.node() ;
			}
			SWEAR( +self , name_ ) ;
		} else {
			::pair<Name/*top*/,::vector<Name>/*created*/> top_created = _g_name_file.insert_chain(name_,'/') ;
			SWEAR(+top_created) ;
			Node n ; if (+top_created.first) n = _g_name_file.c_at(top_created.first).node() ;
			if (+top_created.second) {                                                         // else fast path : avoid taking the lock
				Lock lock { _s_mutex } ;
				for( Name nn : top_created.second ) {                                          // create dir chain from top to bottom
					JobNode& jn = _g_name_file.at     (nn  ) ;
					if (!jn) jn = _g_node_file.emplace(nn,n) ;                                 // test jn in case it has been created by another thread since insert_chain
					n = jn.node() ;
				}
			}
			self = n ;
			SWEAR( +self , name_,top_created ) ;
		}
	}

	RuleTgts NodeBase::s_rule_tgts(::string const& target_name) {
		// first match on suffix
		PsfxIdx sfx_idx = _g_sfxs_file.longest(target_name,::string{Persistent::StartMrkr}).first ; // StartMrkr is to match rules w/ no stems
		if (!sfx_idx) return RuleTgts{} ;
		PsfxIdx pfx_root = _g_sfxs_file.c_at(sfx_idx) ;
		// then match on prefix
		PsfxIdx pfx_idx = _g_pfxs_file.longest(pfx_root,target_name).first ;
		if (!pfx_idx) return RuleTgts{} ;
		return _g_pfxs_file.c_at(pfx_idx) ;
	}

	//
	// Persistent
	//

	// on disk
	::string                      _g_rules_filename ;
	//
	JobFile                       _g_job_file       ; // jobs
	DepsFile                      _g_deps_file      ; // .
	TargetsFile                   _g_targets_file   ; // .
	NodeFile                      _g_node_file      ; // nodes
	JobTgtsFile                   _g_job_tgts_file  ; // .
	RuleCrcFile                   _g_rule_crc_file  ; // rules
	RuleTgtsFile                  _g_rule_tgts_file ; // .
	SfxFile                       _g_sfxs_file      ; // .
	PfxFile                       _g_pfxs_file      ; // .
	NameFile                      _g_name_file      ; // commons
	// in memory
	::uset<Job >       _frozen_jobs  ;
	::uset<Node>       _frozen_nodes ;
	::uset<Node>       _no_triggers  ;

	static void _compile_srcs() {
		Trace trace("_compile_srcs") ;
		g_src_dirs_s = New ;
		for( Node const n : Node::s_srcs(true/*dirs*/) ) g_src_dirs_s->push_back(n->name()+'/') ;
		trace("done") ;
	}

	static void _init_config() {
		try         { g_config = new Config{deserialize<Config>(AcFd(PrivateAdminDirS+"config_store"s).read())} ; }
		catch (...) { g_config = new Config                                                                     ; }
	}

	static void _init_srcs_rules(bool rescue) {
		Trace trace("_init_srcs_rules",STR(rescue)) ;
		//
		// START_OF_VERSIONING
		::string dir_s = g_config->local_admin_dir_s+"store/" ;
		//
		_g_rules_filename = dir_s+"rule" ;
		//
		mk_dir_s(dir_s) ;
		// jobs
		_g_job_file      .init( dir_s+"job"       , g_writable ) ;
		_g_deps_file     .init( dir_s+"deps"      , g_writable ) ;
		_g_targets_file  .init( dir_s+"targets"   , g_writable ) ;
		// nodes
		_g_node_file     .init( dir_s+"node"      , g_writable ) ;
		_g_job_tgts_file .init( dir_s+"job_tgts"  , g_writable ) ;
		// rules
		_g_rule_crc_file .init( dir_s+"rule_crc"  , g_writable ) ; if ( g_writable && !_g_rule_crc_file.c_hdr() ) _g_rule_crc_file.hdr() = 1 ; // hdr is match_gen, 0 is reserved to mean no match
		_g_rule_tgts_file.init( dir_s+"rule_tgts" , g_writable ) ;
		_g_sfxs_file     .init( dir_s+"sfxs"      , g_writable ) ;
		_g_pfxs_file     .init( dir_s+"pfxs"      , g_writable ) ;
		// commons
		_g_name_file     .init( dir_s+"name"      , g_writable ) ;
		// misc
		if (g_writable) {
			g_seq_id = &_g_job_file.hdr().seq_id ;
			if (!*g_seq_id) *g_seq_id = 1 ;                // avoid 0 (when store is brand new) to decrease possible confusion
		}
		// Rule
		RuleBase::s_match_gen = _g_rule_crc_file.c_hdr() ;
		// END_OF_VERSIONING
		//
		SWEAR(RuleBase::s_match_gen>0) ;
		_g_job_file      .keep_open = true ;               // files may be needed post destruction as there may be alive threads as we do not masterize destruction order
		_g_deps_file     .keep_open = true ;               // .
		_g_targets_file  .keep_open = true ;               // .
		_g_node_file     .keep_open = true ;               // .
		_g_job_tgts_file .keep_open = true ;               // .
		_g_rule_crc_file .keep_open = true ;               // .
		_g_rule_tgts_file.keep_open = true ;               // .
		_g_sfxs_file     .keep_open = true ;               // .
		_g_pfxs_file     .keep_open = true ;               // .
		_g_name_file     .keep_open = true ;               // .
		_compile_srcs() ;
		Rule::s_from_disk() ;
		for( Job  j : _g_job_file .c_hdr().frozens    ) _frozen_jobs .insert(j) ;
		for( Node n : _g_node_file.c_hdr().frozens    ) _frozen_nodes.insert(n) ;
		for( Node n : _g_node_file.c_hdr().no_triggers) _no_triggers .insert(n) ;
		//
		if (rescue) {
			trace("rescue") ;
			Fd::Stderr.write("previous crash detected, checking & rescuing\n") ;
			try {
				chk()                                    ; // first verify we have a coherent store
				invalidate_match(true/*force_physical*/) ; // then rely only on essential data that should be crash-safe
				Fd::Stderr.write("seems ok\n") ;
			} catch (::string const&) {
				exit(Rc::Format,"failed to rescue, consider running lrepair") ;
			}
		}
		//
		trace("done") ;
	}

	void chk() {
		// files
		/**/                                    _g_job_file      .chk(                      ) ; // jobs
		/**/                                    _g_deps_file     .chk(                      ) ; // .
		/**/                                    _g_targets_file  .chk(                      ) ; // .
		/**/                                    _g_node_file     .chk(                      ) ; // nodes
		/**/                                    _g_job_tgts_file .chk(                      ) ; // .
		/**/                                    _g_rule_crc_file .chk(                      ) ; // .
		/**/                                    _g_rule_tgts_file.chk(                      ) ; // .
		/**/                                    _g_sfxs_file     .chk(                      ) ; // .
		for( PsfxIdx idx : _g_sfxs_file.lst() ) _g_pfxs_file     .chk(_g_sfxs_file.c_at(idx)) ; // .
		/**/                                    _g_name_file     .chk(                      ) ; // commons
	}

	static void _save_config() {
		AcFd( PrivateAdminDirS+"config_store"s , Fd::Write ).write(serialize(*g_config)  ) ;
		AcFd( AdminDirS+"config"s              , Fd::Write ).write(g_config->pretty_str()) ;
	}

	static void _diff_config( Config const& old_config , bool dyn ) {
		Trace trace("_diff_config",old_config) ;
		for( BackendTag t : iota(All<BackendTag>) ) {
			if (g_config->backends[+t].ifce==old_config.backends[+t].ifce) continue ;
			throw_if( dyn , "cannot change server address while running" ) ;
			break ;
		}
		//
		if (g_config->path_max!=old_config.path_max) invalidate_match() ; // we may discover new buildable nodes or vice versa
	}

	void new_config( Config&& config , bool dyn , bool rescue , ::function<void(Config const& old,Config const& new_)> diff ) {
		Trace trace("new_config",Pdate(New),STR(dyn),STR(rescue) ) ;
		if ( !dyn                                         ) mk_dir_s( AdminDirS+"outputs/"s , true/*unlnk_ok*/ ) ;
		if ( !dyn                                         ) _init_config() ;
		else                                                SWEAR(+*g_config,*g_config) ; // we must update something
		if (                                   +*g_config ) config.key = g_config->key ;
		//
		/**/                                                diff(*g_config,config) ;
		//
		/**/                                                ConfigDiff d = +config ? g_config->diff(config) : ConfigDiff::None ;
		if (          d>ConfigDiff::Static  && +*g_config ) throw "repo must be clean"s  ;
		if (  dyn &&  d>ConfigDiff::Dyn                   ) throw "repo must be steady"s ;
		//
		if (  dyn && !d                                   ) return ;                      // fast path, nothing to update
		//
		/**/                                                Config old_config = *g_config ;
		if (         +d                                   ) *g_config = ::move(config) ;
		if (                                   !*g_config ) throw "no config available"s ;
		/**/                                                g_config->open( dyn , +config ) ;
		if (         +d                                   ) _save_config()                  ;
		if ( !dyn                                         ) _init_srcs_rules(rescue)        ;
		if (         +d                                   ) _diff_config(old_config,dyn)    ;
		trace("done",Pdate(New)) ;
	}

	// str has target syntax
	// return suffix after last stem (StartMrkr+str if no stem)
	static ::string _parse_sfx(::string const& str) {
		size_t pos = 0 ;
		for(;;) {                                           // cannot use rfind as anything can follow a StemMrkr, including a StemMrkr, so iterate with find
			size_t nxt_pos = str.find(Rule::StemMrkr,pos) ;
			if (nxt_pos==Npos) break ;
			pos = nxt_pos+1+sizeof(VarIdx) ;
		}
		if (pos==0) return StartMrkr+str   ;                // signal that there is no stem by prefixing with StartMrkr
		else        return str.substr(pos) ;                // suppress stem marker & stem idx
	}
	// return prefix before first stem (empty if no stem)
	static ::string _parse_pfx(::string const& str) {
		size_t pos = str.find(Rule::StemMrkr) ;
		if (pos==Npos) return {}                ;           // absence of stem is already signal in _parse_sfx, we just need to pretend there is no prefix
		else           return str.substr(0,pos) ;
	}
	struct Rt : RuleTgt {
		// cxtors & casts
		Rt() = default  ;
		Rt( RuleCrc rc , VarIdx ti ) : RuleTgt{rc,ti} , pfx{_parse_pfx(target())} , sfx{_parse_sfx(target())} {}
		// data (cache)
		::string pfx ;
		::string sfx ;
	} ;

}

namespace std {
	template<> struct hash<Engine::Persistent::Rt> {
		size_t operator()(Engine::Persistent::Rt const& rt) const { return hash<Engine::RuleTgt>()(rt) ; } // there is no more info in a Rt than in a RuleTgt
	} ;
}

namespace Engine::Persistent {

	template<bool IsSfx> static void _propag_to_longer( ::map_s<uset<Rt>>& psfx_map , ::uset_s const& sub_repos_s={} ) {
		for( auto& [long_psfx,long_entry] : psfx_map ) {                // entries order guarantees that if an entry is a prefix/suffix of another, it is processed first
			if ( !IsSfx && sub_repos_s.contains(long_psfx) ) continue ; // dont propagate through sub_repos boundaries
			for( size_t shorten_by : iota(1,long_psfx.size()+1) ) {
				::string short_psfx = long_psfx.substr( IsSfx?shorten_by:0 , long_psfx.size()-shorten_by ) ;
				if ( !IsSfx && sub_repos_s.contains(short_psfx) ) break ;                                    // dont propagate through sub_repos boundaries
				auto short_it = psfx_map.find(short_psfx) ;
				if (short_it==psfx_map.end()) continue ;
				long_entry.merge(::copy(short_it->second)) ; // copy arg as merge clobbers it
				break ;                                      // psfx's are sorted shortest first, so as soon as a short one is found, it is already merged with previous ones
			}
		}
	}

	// make a prefix/suffix map that records which rule has which prefix/suffix
	static void _compile_psfxs() {
		_g_sfxs_file.clear() ;
		_g_pfxs_file.clear() ;
		//
		// first compute a suffix map
		::map_s<uset<Rt>> sfx_map ;
		for( Rule r : rule_lst() )
			for( VarIdx ti : iota<VarIdx>(r->matches.size()) ) {
				if ( r->matches[ti].second.flags.is_target!=Yes         ) continue ;
				if (!r->matches[ti].second.flags.tflags()[Tflag::Target]) continue ;
				Rt rt { r->crc , ti } ;
				sfx_map[rt.sfx].insert(rt) ;
			}
		_propag_to_longer<true/*IsSfx*/>(sfx_map) ;                              // propagate to longer suffixes as a rule that matches a suffix also matches any longer suffix
		//
		// now, for each suffix, compute a prefix map
		// create empty entries for all sub-repos so as markers to ensure prefixes are not propagated through sub-repo boundaries
		::map_s<uset<Rt>> empty_pfx_map ;                                  for( ::string const& sr_s : g_config->sub_repos_s ) empty_pfx_map.try_emplace(sr_s) ;
		::uset_s          sub_repos_s   = mk_uset(g_config->sub_repos_s) ;
		for( auto const& [sfx,sfx_rule_tgts] : sfx_map ) {
			::map_s<uset<Rt>> pfx_map = empty_pfx_map ;
			if ( sfx.starts_with(StartMrkr) ) {                                  // manage targets with no stems as a suffix made of the entire target and no prefix
				::string_view sfx1 = substr_view(sfx,1) ;
				for( Rt const& rt : sfx_rule_tgts ) if (sfx1.starts_with(rt.pfx)) pfx_map[""].insert(rt) ;
			} else {
				for( Rt const& rt : sfx_rule_tgts ) pfx_map[rt.pfx].insert(rt) ;
				_propag_to_longer<false/*IsSfx*/>(pfx_map,sub_repos_s) ;         // propagate to longer prefixes as a rule that matches a prefix also matches any longer prefix
			}
			//
			// store proper rule_tgts (ordered by decreasing prio, giving priority to AntiRule within each prio) for each prefix/suffix
			PsfxIdx pfx_root = _g_pfxs_file.emplace_root() ;
			_g_sfxs_file.insert_at(sfx) = pfx_root ;
			for( auto const& [pfx,pfx_rule_tgts] : pfx_map ) {
				if (!pfx_rule_tgts) continue ;                                   // this is a sub-repo marker, not a real entry
				vector<Rt> pfx_rule_tgt_vec = mk_vector(pfx_rule_tgts) ;
				::sort(
					pfx_rule_tgt_vec
				,	[]( Rt const& a , Rt const& b )->bool {
						// compulsery : order by priority, with special Rule's before plain Rule's, with Anti's before GenericSrc's within each priority level
						// optim      : put more specific rules before more generic ones to favor sharing RuleTgts in reversed PrefixFile
						// finally    : any stable sort is fine, just to avoid random order
						return
							::tuple( a->rule->is_special() , a->rule->prio , a->rule->special , a.pfx.size()+a.sfx.size() , a->rule->name , a->rule->sub_repo_s )
						>	::tuple( b->rule->is_special() , b->rule->prio , b->rule->special , b.pfx.size()+b.sfx.size() , b->rule->name , a->rule->sub_repo_s )
						;
					}
				) ;
				_g_pfxs_file.insert_at(pfx_root,pfx) = RuleTgts(mk_vector<RuleTgt>(pfx_rule_tgt_vec)) ;
			}
		}
	}

	//                                      vv must fit in rule file vv   vvvvvvvv idx must fit within type vvvvvvvv
	static constexpr size_t NRules = ::min( (size_t(1)<<NRuleIdxBits)-1 , (size_t(1)<<NBits<Rule>)-+Special::NShared ) ; // reserv 0 and full 1 to manage prio

	static void _compute_prios(Rules& rules) {
		::map<RuleData::Prio,RuleIdx> prio_map ;                                             // mapping from user_prio to prio
		for( RuleData const& rd    : rules    ) prio_map[rd.user_prio] ;                     // create entries
		RuleIdx p = 1 ;                                                                      // reserv 0 for "after all user rules"
		for( auto&           [k,v] : prio_map ) { SWEAR( 0<p && p<NRules , p ) ; v = p++ ; } // and full 1 for "before all user rules"
		for( RuleData&       rd    : rules    ) rd.prio = prio_map.at(rd.user_prio) ;
	}

	bool/*invalidate*/ new_rules( Rules&& new_rules_ , bool dyn ) {
		Trace trace("new_rules",new_rules_.size()) ;
		//
		throw_unless( new_rules_.size()<NRules , "too many rules (",new_rules_.size(),"), max is ",NRules-1 ) ; // ensure we can use RuleIdx as index
		//
		_compute_prios(new_rules_) ;
		//
		::umap<Crc,RuleData const*> old_rds   ;
		::umap<Crc,RuleData*>       new_rds   ;
		::uset_s                    new_names ;
		for( Rule r : rule_lst() ) old_rds.try_emplace(r->crc->match,&*r) ;
		for( RuleData& rd : new_rules_ ) {
			if (rd.special<Special::NShared) continue ;
			auto [it,new_crc ] = new_rds.try_emplace(rd.crc->match,&rd)  ;
			bool     new_name  = new_names.insert(rd.full_name()).second ;
			if ( !new_crc && !new_name ) throw "rule "+rd.full_name()+" appears twice"                                                        ;
			if ( !new_crc              ) throw "rules "+rd.full_name()+" and "+it->second->full_name()+" match identically and are redundant" ;
			if (             !new_name ) throw "2 rules have the same name "+rd.full_name()                                                   ;
		}
		//
		RuleIdx n_old_rules         = old_rds.size() ;
		RuleIdx n_new_rules         = 0              ;
		RuleIdx n_modified_prio     = 0              ;
		RuleIdx n_modified_cmd      = 0              ;
		RuleIdx n_modified_rsrcs    = 0              ;
		bool    modified_rule_order = false          ;                                                          // only checked on common rules (old & new)
		// evaluate diff
		for( auto& [match_crc,new_rd] : new_rds ) {
			auto it = old_rds.find(match_crc) ;
			if (it==old_rds.end()) {
				n_new_rules++ ;
			} else {
				n_old_rules-- ;
				RuleData const& old_rd = *it->second ;
				n_modified_prio     += new_rd->user_prio !=old_rd.user_prio  ;
				n_modified_cmd      += new_rd->crc->cmd  !=old_rd.crc->cmd   ;
				n_modified_rsrcs    += new_rd->crc->rsrcs!=old_rd.crc->rsrcs ;
				modified_rule_order |= new_rd->prio      !=old_rd.prio       ;
				//
				new_rd->cost_per_token = old_rd.cost_per_token ;
				new_rd->exec_time      = old_rd.exec_time      ;
				new_rd->stats_weight   = old_rd.stats_weight   ;
			}
		}
		bool res = n_new_rules || n_old_rules || modified_rule_order ;
		if (dyn) {                                                                                              // check if compatible with dynamic update
			throw_if( n_new_rules         , "new rules appeared"           ) ;
			throw_if( n_old_rules         , "old rules disappeared"        ) ;
			throw_if( n_modified_cmd      , "rule cmd's were modified"     ) ;
			throw_if( n_modified_rsrcs    , "rule resources were modified" ) ;
			throw_if( modified_rule_order , "rule prio's were modified"    ) ;
			RuleBase::s_from_vec_dyn(::move(new_rules_)) ;
		} else {
			RuleBase::s_from_vec_not_dyn(::move(new_rules_)) ;
			if (res) _compile_psfxs() ;                                                                         // recompute matching
		}
		trace(STR(n_new_rules),STR(n_old_rules),STR(n_modified_prio),STR(n_modified_cmd),STR(n_modified_rsrcs),STR(modified_rule_order)) ;
		// trace
		Trace trace2 ;
		for( PsfxIdx sfx_idx : _g_sfxs_file.lst() ) {
			::string sfx      = _g_sfxs_file.str_key(sfx_idx) ;
			PsfxIdx  pfx_root = _g_sfxs_file.at     (sfx_idx) ;
			bool     single   = +sfx && sfx[0]==StartMrkr  ;
			for( PsfxIdx pfx_idx : _g_pfxs_file.lst(pfx_root) ) {
				RuleTgts rts = _g_pfxs_file.at     (pfx_idx) ;
				::string pfx = _g_pfxs_file.str_key(pfx_idx) ;
				if (single) { SWEAR(!pfx,pfx) ; trace2(         sfx.substr(1) , ':' ) ; }
				else        {                   trace2( pfx+'*'+sfx           , ':' ) ; }
				Trace trace3 ;
				for( RuleTgt rt : rts.view() )
					trace3( rt->rule , ':' , rt->rule->user_prio , rt->rule->prio , rt->rule->full_name() , rt.key() ) ;
			}
		}
		// user report
		{	::vector<Rule> rules ; for( Rule r : rule_lst() ) rules.push_back(r) ;
			::sort( rules , [](Rule a,Rule b){
				if (a->sub_repo_s!=b->sub_repo_s) return a->sub_repo_s < b->sub_repo_s ;
				if (a->user_prio !=b->user_prio ) return a->user_prio  > b->user_prio  ;
				else                              return a->name       < b->name       ;
			} ) ;
			trace("user_report") ;
			First    first   ;
			::string content ;
			for( Rule rule : rules ) if (rule->user_defined())
				content <<first("","\n")<< rule->pretty_str() ;
			AcFd( AdminDirS+"rules"s , Fd::Write ).write(content) ;
		}
		trace("done") ;
		return res ;
	}

	bool/*invalidate*/ new_srcs( Sources&& src_names , bool dyn ) {
		NfsGuard             nfs_guard    { g_config->reliable_dirs } ;
		::vmap<Node,FileTag> srcs         ;
		::umap<Node,FileTag> old_srcs     ;
		::umap<Node,FileTag> new_srcs_    ;
		::uset<Node        > src_dirs     ;
		::uset<Node        > old_src_dirs ;
		::uset<Node        > new_src_dirs ;
		Trace trace("new_srcs") ;
		// check and format new srcs
		size_t      repo_root_depth = 0                                                                 ; { for( char c : *g_repo_root_s ) repo_root_depth += c=='/' ; } repo_root_depth-- ;
		RealPathEnv rpe            { .lnk_support=g_config->lnk_support , .repo_root_s=*g_repo_root_s } ;
		RealPath    real_path      { rpe                                                              } ;
		for( ::string& src : src_names ) {
			throw_unless( +src , "found an empty source" ) ;
			bool is_dir_ = is_dir_name(src) ;
			if (!is_canon(src)          ) throw cat("source ",is_dir_?"dir ":"",src," canonical form is ",mk_canon(src)                 ) ;
			if (Record::s_is_simple(src)) throw cat("source ",is_dir_?"dir ":"",src," cannot lie within or encompass system directories") ;
			//
			if (is_dir_) {
				if ( !is_abs_s(src) && uphill_lvl_s(src)>=repo_root_depth ) throw "cannot access relative source dir "+no_slash(src)+" from repository "+no_slash(*g_repo_root_s) ;
				src.pop_back() ;
			}
			if (dyn) nfs_guard.access(src) ;
			RealPath::SolveReport sr = real_path.solve(src,true/*no_follow*/) ;
			FileInfo              fi { src }                                  ;
			if (+sr.lnks) {
				throw                                                                             "source "+src+(is_dir_?"/":"")+" has symbolic link "+sr.lnks[0]+" in its path"    ;
			} else if (is_dir_) {
				throw_unless( fi.tag()==FileTag::Dir                                            , "source ",src," is not a directory"                                             ) ;
			} else {
				throw_unless( sr.file_loc==FileLoc::Repo                                        , "source ",src," is not in repo"                                                 ) ;
				throw_unless( fi.exists()                                                       , "source ",src," is not a regular file nor a symbolic link"                      ) ;
				throw_if    ( g_config->lnk_support==LnkSupport::None && fi.tag()==FileTag::Lnk , "source ",src," is a symbolic link and they are not supported"                  ) ;
				SWEAR(src==sr.real,src,sr.real) ;                              // src is local, canonic and there are no links, what may justify real from being different ?
			}
			srcs.emplace_back( Node(src,!is_lcl(src)/*no_dir*/) , fi.tag() ) ; // external src dirs need no uphill dir
		}
		// format old srcs
		for( bool dirs : {false,true} ) for( Node s : Node::s_srcs(dirs) ) old_srcs.emplace(s,dirs?FileTag::Dir:FileTag::None) ; // dont care whether we delete a regular file or a link
		//
		for( auto [n,_] : srcs     ) for( Node d=n->dir() ; +d ; d = d->dir() ) if (!src_dirs    .insert(d).second) break ;      // non-local nodes have no dir
		for( auto [n,_] : old_srcs ) for( Node d=n->dir() ; +d ; d = d->dir() ) if (!old_src_dirs.insert(d).second) break ;      // .
		// further checks
		for( auto [n,t] : srcs ) {
			if (!src_dirs.contains(n)) continue ;
			::string nn   = n->name() ;
			::string nn_s = nn+'/'    ;
			for( ::string const& sn : src_names ) throw_if( sn.starts_with(nn_s) , "source ",t==FileTag::Dir?"dir ":"",nn," is a dir of ",sn ) ;
			FAIL(nn,"is a source dir of no source") ;
		}
		// compute diff
		bool fresh = !old_srcs ;
		for( auto nt : srcs ) {
			auto it = old_srcs.find(nt.first) ;
			if (it==old_srcs.end()) new_srcs_.insert(nt) ;
			else                    old_srcs .erase (it) ;
		}
		if (!fresh) {
			for( auto [n,t] : new_srcs_ ) if (t==FileTag::Dir) throw "new source dir "+n->name()+' '+git_clean_msg() ; // we may not have recorded some deps to these, and this is unpredictable
			for( auto [n,t] : old_srcs  ) if (t==FileTag::Dir) throw "old source dir "+n->name()+' '+git_clean_msg() ; // XXX! : this could be managed if necessary
		}
		//
		for( Node d : src_dirs ) { if ( auto it=old_src_dirs.find(d) ; it!=old_src_dirs.end() ) old_src_dirs.erase(it) ; else new_src_dirs.insert(d) ; }
		//
		if ( !old_srcs && !new_srcs_ ) return false ;
		if (dyn) {
			if (+new_srcs_) throw "new source "    +new_srcs_.begin()->first->name() ;
			if (+old_srcs ) throw "removed source "+old_srcs .begin()->first->name() ;
			FAIL() ;
		}
		//
		trace("srcs",'-',old_srcs.size(),'+',new_srcs_.size()) ;
		// commit
		for( bool add : {false,true} ) {
			::umap<Node,FileTag> const& srcs = add ? new_srcs_ : old_srcs ;
			::vector<Node>              ss   ;                              ss.reserve(srcs.size()) ;                  // typically, there are very few src dirs
			::vector<Node>              sds  ;                                                                         // .
			for( auto [n,t] : srcs ) if (t==FileTag::Dir) sds.push_back(n) ; else ss.push_back(n) ;
			//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Node::s_srcs(false/*dirs*/,add,ss ) ;
			Node::s_srcs(true /*dirs*/,add,sds) ;
			//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		{	Trace trace2 ;
			for( auto [n,t] : old_srcs     ) { Node(n)->mk_no_src()           ; trace2('-',t==FileTag::Dir?"dir":"",n) ; }
			for( Node  d    : old_src_dirs )        d ->mk_no_src()           ;
			for( auto [n,t] : new_srcs_    ) { Node(n)->mk_src(t            ) ; trace2('+',t==FileTag::Dir?"dir":"",n) ; }
			for( Node  d    : new_src_dirs )        d ->mk_src(FileTag::None) ;
		}
		_compile_srcs() ;
		// user report
		{	::string content ;
			for( auto [n,t] : srcs ) content << n->name() << (t==FileTag::Dir?"/":"") <<'\n' ;
			AcFd( AdminDirS+"manifest"s , Fd::Write ).write(content) ;
		}
		trace("done",srcs.size(),"srcs") ;
		return true ;
	}

	void invalidate_match(bool force_physical) {
		MatchGen& match_gen = _g_rule_crc_file.hdr() ;
		Trace trace("invalidate_match","old gen",match_gen) ;
		match_gen++ ;                                                                                                         // increase generation, which automatically makes all nodes !match_ok()
		if ( force_physical || match_gen==0 ) {                                                                               // unless we wrapped around
			trace("reset") ;
			Fd::Stderr.write("collecting nodes ...") ; for( Node n : node_lst() ) n->mk_old() ; Fd::Stderr.write(" done\n") ; // physically reset node match_gen's
			match_gen = 1 ;
		}
		Rule::s_match_gen = match_gen ;
	}

}
