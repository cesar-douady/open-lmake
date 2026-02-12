// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include <tuple>

#include "py.hh"
#include "rpc_job.hh"

using Backends::Backend ;

using namespace Disk ;
using namespace Py   ;

namespace Engine {

	SeqId*                    g_seq_id         = nullptr ;
	StaticUniqPtr<Config    > g_config         ;
	StaticUniqPtr<::vector_s> g_src_dirs_s     ;
	StaticUniqPtr<::vector_s> g_ext_codec_dirs ;

}

namespace Engine::Persistent {

	//
	// RuleBase
	//

	MatchGen                            RuleBase::s_match_gen = 1 ; // 0 is forbidden as it is reserved to mean !match
	StaticUniqPtr<Rules,MutexLvl::None> RuleBase::s_rules     ;     // almost a ::unique_ptr except we do not want it to be destroyed at the end to avoid problems

	void RuleBase::_s_save() {
		SWEAR(+Rule::s_rules) ;
		AcFd( _g_rules_filename , {O_WRONLY|O_TRUNC|O_CREAT} ).write( serialize(*Rule::s_rules) ) ;
	}

	void RuleBase::_s_update_crcs() {
		Trace trace("_s_update_crcs") ;
		::umap<Crc,Rule> rule_map ; if (+s_rules) rule_map.reserve(s_rules->size()) ; for( Rule r : rule_lst(true/*with_special*/) ) rule_map[r->crc->match] = r ;
		for( RuleCrc rc : rule_crc_lst() ) {
			RuleCrcData& rcd = rc.data()                ;
			auto         it  = rule_map.find(rcd.match) ;
			if (it==rule_map.end()) {
				rcd.rule  = {}                   ;
				rcd.state = RuleCrcState::CmdOld ;
			} else {
				Rule            r  = rule_map.at(rcd.match) ;
				RuleData const& rd = *r                     ;
				/**/                                              rcd.rule  = r                      ;
				if      (rcd.rsrcs==rd.crc->rsrcs               ) rcd.state = RuleCrcState::Ok       ;
				else if (rcd.cmd  !=rd.crc->cmd                 ) rcd.state = RuleCrcState::CmdOld   ;
				else if (rcd.state!=RuleCrcState::RsrcsForgotten) rcd.state = RuleCrcState::RsrcsOld ;
				//
				if (+r<+Special::NUniq) SWEAR( rcd.state==RuleCrcState::Ok , r,rcd.state ) ;
			}
			trace(rc,rcd) ;
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
		catch(...) { s_rules = nullptr                                                       ; }
		_s_set_rules() ;
		//
		trace("done") ;
	}

	void RuleBase::s_from_vec_dyn(Rules&& new_rules) {
		static StaticUniqPtr<Rules> s_prev_rules ;                                                              // keep prev rules in case some on-going activity refers rules while being updated
		Trace trace("s_from_vec_dyn",new_rules.size()) ;
		SWEAR( s_rules->sys_path_crc==new_rules.sys_path_crc , s_rules->sys_path_crc,new_rules.sys_path_crc ) ; // may not change dynamically as this would potentially change rule cmd's
		SWEAR( s_rules->size()      ==new_rules.size()       , s_rules->size()      ,new_rules.size()       ) ; // may not change dynamically
		//
		::umap<Crc,RuleData*> rule_map ; rule_map.reserve(new_rules.size()) ; for( RuleData& rd : new_rules ) rule_map.try_emplace( rd.crc->match , &rd ) ;
		//
		Rules* next_rules = new Rules{New} ; if (+s_rules) next_rules->reserve(s_rules->size()) ;
		for( Rule r : rule_lst() ) next_rules->push_back(::move(*rule_map.at(r->crc->match))) ;                 // crc->match's must be identical between old and new or we should be here
		next_rules->dyn_vec      = ::move(new_rules.dyn_vec     ) ;
		next_rules->py_sys_path  = ::move(new_rules.py_sys_path ) ;
		next_rules->sys_path_crc =        new_rules.sys_path_crc  ;
		next_rules->compile() ;
		//
		/**/   s_prev_rules = ::move(s_rules) ;
		/**/   s_rules      = next_rules      ;
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
	// JobBase
	//

	Mutex<MutexLvl::Job,true/*Shared*/> JobDataBase::_s_mutex ;

	//
	// NodeBase
	//

	Mutex<MutexLvl::Node,true/*Shared*/> NodeDataBase::_s_mutex ;

	NodeBase::NodeBase(::string const& name_) {
		SWEAR( +name_ && is_canon(name_) , name_ ) ;
		SharedLock lock { NodeDataBase::_s_mutex }        ;
		NodeName   nn   = _g_node_name_file.search(name_) ; if (!nn) return ;
		self = _g_node_name_file.at(nn) ;
		SWEAR(+self) ;
	}

	NodeBase::NodeBase( NewType , ::string const& name_ , bool no_dir ) {
		SWEAR( +name_ && is_canon(name_) , name_ ) ;
		Lock lock { NodeDataBase::_s_mutex } ;
		if (no_dir) {
			NodeName nn = _g_node_name_file.insert(name_) ;
			Node&    n  = _g_node_name_file.at(nn)        ;
			if (!n) n = _g_node_file.emplace_back(nn) ;
			self = n ;
		} else {
			::pair<NodeName/*top*/,::vector<NodeName>/*created*/> top_created = _g_node_name_file.insert_chain(name_,'/') ;
			SWEAR(+top_created) ;
			Node last_n ;
			if (+top_created.first)
				last_n = _g_node_name_file.c_at(top_created.first) ;
			for( NodeName nn : top_created.second ) {
				Node& n = _g_node_name_file.at(nn) ; SWEAR( !n , n ) ;
				last_n = n = _g_node_file.emplace_back(nn,last_n/*dir*/) ; // create dir chain from top to bottom
			}
			self = last_n ;
		}
		SWEAR( +self , name_,no_dir ) ;
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
	::string _g_rules_filename ;
	//
	JobFile      _g_job_file       ; // jobs
	JobNameFile  _g_job_name_file  ; // .
	DepsFile     _g_deps_file      ; // .
	TargetsFile  _g_targets_file   ; // .
	NodeFile     _g_node_file      ; // nodes
	NodeNameFile _g_node_name_file ; // .
	JobTgtsFile  _g_job_tgts_file  ; // .
	RuleCrcFile  _g_rule_crc_file  ; // rules
	RuleTgtsFile _g_rule_tgts_file ; // .
	SfxFile      _g_sfxs_file      ; // .
	PfxFile      _g_pfxs_file      ; // .
	// in memory
	::uset<Job > _frozen_jobs  ;
	::uset<Node> _frozen_nodes ;
	::uset<Node> _no_triggers  ;

	static void _compile_srcs() {
		Trace trace("_compile_srcs") ;
		g_src_dirs_s = New ;
		for( Node const n : Node::s_srcs(true/*dirs*/) ) g_src_dirs_s->push_back(n->name()+'/') ;
		trace("done") ;
	}

	static void _init_config() {
		try         { g_config = new Config{deserialize<Config>(AcFd(cat(PrivateAdminDirS,"config_store")).read())} ; g_config->booted = true ; } // buggy clang-tidy sees booted as garbage
		catch (...) { g_config = new Config                                                                         ;                           }
	}

	static void _init_srcs_rules(bool rescue) {
		Trace trace("_init_srcs_rules",STR(rescue)) ;
		//
		// START_OF_VERSIONING REPO
		::string dir_s = g_config->local_admin_dir_s+"store/" ;
		//
		_g_rules_filename = dir_s+"rule" ;
		// jobs
		_g_job_file      .init( dir_s+"job"       , g_writable ) ;
		_g_job_name_file .init( dir_s+"job_name"  , g_writable ) ;
		_g_deps_file     .init( dir_s+"deps"      , g_writable ) ;
		_g_targets_file  .init( dir_s+"targets"   , g_writable ) ;
		// nodes
		_g_node_file     .init( dir_s+"node"      , g_writable ) ;
		_g_node_name_file.init( dir_s+"node_name" , g_writable ) ;
		_g_job_tgts_file .init( dir_s+"job_tgts"  , g_writable ) ;
		// rules
		_g_rule_crc_file .init( dir_s+"rule_crc"  , g_writable ) ; if ( g_writable && !_g_rule_crc_file.c_hdr() ) _g_rule_crc_file.hdr() = 1 ; // hdr is match_gen, 0 is reserved to mean no match
		_g_rule_tgts_file.init( dir_s+"rule_tgts" , g_writable ) ;
		_g_sfxs_file     .init( dir_s+"sfxs"      , g_writable ) ;
		_g_pfxs_file     .init( dir_s+"pfxs"      , g_writable ) ;
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
				Fd::Stderr.write("seems ok\n")           ;
			} catch (::string const&) {
				throw "failed to rescue, consider running lmake_repair"s ;
			}
		}
		//
		trace("done") ;
	}

	void chk() {
		/**/                                    _g_job_file      .chk(                      ) ; // jobs
		/**/                                    _g_job_name_file .chk(                      ) ; // .
		/**/                                    _g_deps_file     .chk(                      ) ; // .
		/**/                                    _g_targets_file  .chk(                      ) ; // .
		/**/                                    _g_node_file     .chk(                      ) ; // nodes
		/**/                                    _g_node_name_file.chk(                      ) ; // .
		/**/                                    _g_job_tgts_file .chk(                      ) ; // .
		/**/                                    _g_rule_crc_file .chk(                      ) ; // .
		/**/                                    _g_rule_tgts_file.chk(                      ) ; // .
		/**/                                    _g_sfxs_file     .chk(                      ) ; // .
		for( PsfxIdx idx : _g_sfxs_file.lst() ) _g_pfxs_file     .chk(_g_sfxs_file.c_at(idx)) ; // .
	}

	static void _save_config() {
		AcFd( cat(PrivateAdminDirS,"config_store") , {O_WRONLY|O_TRUNC|O_CREAT} ).write( serialize(*g_config)   ) ;
		AcFd( cat(AdminDirS       ,"config"      ) , {O_WRONLY|O_TRUNC|O_CREAT} ).write( g_config->pretty_str() ) ;
	}

	static void _diff_config(Config const& old_config) {
		Trace trace("_diff_config",old_config) ;
		//
		if (
			g_config->path_max     !=old_config.path_max
		||	g_config->max_dep_depth!=old_config.max_dep_depth
		) invalidate_match() ;                                // we may discover new buildable nodes or vice versa
	}

	void new_config( Config&& config , bool rescue , ::function<void(Config const& old,Config const& new_)> diff ) {
		Trace trace("new_config",Pdate(New),STR(rescue) ) ;
		static bool s_first_time = true ; bool first_time = s_first_time ; s_first_time = false ;
		//
		if (  first_time                                        ) _init_config()                  ;
		else                                                      SWEAR( +*g_config , *g_config ) ;                                    // we must update something
		if (                                         +*g_config ) config.key = g_config->key ;
		//
		/**/                                                      diff(*g_config,config) ;
		//
		/**/                                                      ConfigDiff d = +config ? g_config->diff(config) : ConfigDiff::None ; // if no config passed, assume no update
		if (                 d>ConfigDiff::Static && +*g_config ) throw ::pair( "repo must be clean"s  , Rc::CleanRepo  ) ;
		if ( !first_time &&  d>ConfigDiff::Dyn                  ) throw ::pair( "repo must be steady"s , Rc::SteadyRepo ) ;
		//
		if ( !first_time && !d                                  ) return ;                                                             // fast path, nothing to update
		//
		/**/                                                      Config old_config = *g_config ;
		if (                +d                                  ) *g_config = ::move(config) ;
		if (                                         !*g_config ) throw "no config available"s ;
		/**/                                                      g_config->open()         ;
		if (                +d                                  ) _save_config()           ;
		if (  first_time                                        ) _init_srcs_rules(rescue) ;
		if (                +d                                  ) _diff_config(old_config) ;
		trace("done",Pdate(New)) ;
	}

	// str has target syntax
	// return suffix after last stem (StartMrkr+str if no stem)
	static ::string _parse_sfx(::string const& str) {
		size_t pos = 0 ;
		for(;;) {                                                        // cannot use rfind as anything can follow a StemMrkr, including a StemMrkr, so iterate with find
			size_t nxt_pos = str.find(Rule::StemMrkr,pos) ;
			if (nxt_pos==Npos) break ;
			pos = nxt_pos+1+sizeof(VarIdx) ;
		}
		if (pos==0) return StartMrkr+str   ;                             // signal that there is no stem by prefixing with StartMrkr
		else        return str.substr(pos) ;                             // suppress stem marker & stem idx
	}
	// return prefix before first stem (empty if no stem)
	static ::string _parse_pfx(::string const& str) {
		size_t pos = str.find(Rule::StemMrkr) ;
		if (pos==Npos) return {}                ;                        // absence of stem is already signal in _parse_sfx, we just need to pretend there is no prefix
		else           return str.substr(0,pos) ;
	}
	struct Rt : RuleTgt {
		// cxtors & casts
		Rt() = default  ;
		Rt( RuleCrc rc , VarIdx ti ) : RuleTgt{rc,ti} , pfx{_parse_pfx(target())} , sfx{_parse_sfx(target())} {}
		// services
		size_t hash() const { return ::hash<Engine::RuleTgt>()(self) ; } // there is no more info in a Rt than in a RuleTgt
		// data (cache)
		::string pfx ;
		::string sfx ;
	} ;

}

namespace Engine::Persistent {

	template<bool IsSfx> static void _propag_to_longer( ::map_s<::uset<Rt>>& psfx_map , ::uset_s const& sub_repos_s={} ) {
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
		::map_s<::uset<Rt>> sfx_map ;
		for( Rule r : rule_lst(true/*with_shared*/) )                                        // Codec is shared and matches, hence we must list shared rules here
			for ( bool star : {false,true} )
				for( VarIdx ti : r->matches_iotas[star][+MatchKind::Target] ) {
					Rt rt { r->crc , ti } ;
					sfx_map[rt.sfx].insert(rt) ;
				}
		_propag_to_longer<true/*IsSfx*/>(sfx_map) ;                                          // propagate to longer suffixes as a rule that matches a suffix also matches any longer suffix
		//
		// now, for each suffix, compute a prefix map
		// create empty entries for private admin dir and all sub-repos as markers to ensure prefixes are not propagated through sub-repo boundaries
		::map_s<::uset<Rt>> empty_pfx_map { {PrivateAdminDirS,{}} }        ; for( ::string const& sr_s : g_config->sub_repos_s ) empty_pfx_map.try_emplace(sr_s) ;
		::uset_s            sub_repos_s   = mk_uset(g_config->sub_repos_s) ;
		for( auto const& [sfx,sfx_rule_tgts] : sfx_map ) {
			::map_s<::uset<Rt>> pfx_map = empty_pfx_map ;
			if ( sfx.starts_with(StartMrkr) ) {                                              // manage targets with no stems as a suffix made of the entire target and no prefix
				::string_view sfx1 = substr_view(sfx,1) ;
				for( Rt const& rt : sfx_rule_tgts ) if (sfx1.starts_with(rt.pfx)) pfx_map[""].insert(rt) ;
			} else {
				for( Rt const& rt : sfx_rule_tgts ) pfx_map[rt.pfx].insert(rt) ;
				_propag_to_longer<false/*IsSfx*/>(pfx_map,sub_repos_s) ;                     // propagate to longer prefixes as a rule that matches a prefix also matches any longer prefix
			}
			//
			// store proper rule_tgts (ordered by decreasing prio, giving priority to AntiRule within each prio) for each prefix/suffix
			PsfxIdx pfx_root = _g_pfxs_file.emplace_root() ;
			_g_sfxs_file.insert_at(sfx) = pfx_root ;
			for( auto const& [pfx,pfx_rule_tgts] : pfx_map ) {
				if (!pfx_rule_tgts) continue ;                                               // this is a sub-repo marker, not a real entry
				vector<Rt>                     pfx_rule_tgt_vec = mk_vector(pfx_rule_tgts) ;
				::umap<Rule,size_t/*psfx_sz*/> psfx_szs         ;                            // used to optimize rule order
				for( Rt const& rt : pfx_rule_tgt_vec ) {
					size_t& psfx_sz = psfx_szs[rt->rule] ;
					psfx_sz = ::max( psfx_sz , rt.pfx.size() + rt.sfx.size() ) ;
				}
				::sort(
					pfx_rule_tgt_vec
				,	[&]( Rt const& a , Rt const& b ) {
						// order :
						// - rule order :
						//   - special Rule's before plain Rule's
						//   - by decreasing prio
						//   - Anti's before GenericSrc's within given priority
						//   - max size of pfx+sfx (among targets appearing here) to favor sharing of last section (as it is stored in a reversed prefix tree)
						//   - any stable sort
						// - within rule :
						//   - by tgt_idx so as to correspond to candidate order
						Rule            ar  = a->rule ;
						Rule            br  = b->rule ;
						RuleData const& ard = *ar     ;
						RuleData const& brd = *br     ;
						return //!   <------------semantic_sort------------->   optim_sort    stable_sort within_rule
							::tuple( !ard.is_plain() , ard.prio , ard.special , psfx_szs[ar] , +a->match , a.tgt_idx )
						>	::tuple( !brd.is_plain() , brd.prio , brd.special , psfx_szs[br] , +b->match , b.tgt_idx )
						;
					}
				) ;
				_g_pfxs_file.insert_at(pfx_root,pfx) = RuleTgts(mk_vector<RuleTgt>(pfx_rule_tgt_vec)) ;
			}
		}
	}

	//                                      <--must_fit_in_rule_file-->   <--------idx_must_fit_within_type------>
	static constexpr size_t NRules = ::min( (size_t(1)<<NRuleIdxBits)-1 , (size_t(1)<<NBits<Rule>)-+Special::NUniq ) ; // reserv 0 and full 1 to manage prio

	static void _compute_prios(Rules& rules) {
		::map<RuleData::Prio,RuleIdx> prio_map ; for( RuleData const& rd : rules ) prio_map[rd.user_prio] ; // mapping from user_prio (double) to prio (RuleIdx) in same order
		RuleIdx p = 1 ;
		for( auto& [k,v] : prio_map ) {
			SWEAR( 0<p && p<NRules , p ) ;                                                                  // reserv 0 for "after all user rules" and full 1 for "before all user rules"
			v = p++ ;
		}
		for( RuleData& rd : rules ) rd.prio = prio_map.at(rd.user_prio) ;
	}

	bool/*invalidate*/ new_rules(Rules&& new_rules_) {
		Trace trace("new_rules",new_rules_.size()) ;
		static bool s_first_time = true ; bool first_time = s_first_time ; s_first_time = false ;
		//
		throw_unless( new_rules_.size()<NRules , "too many rules (",new_rules_.size(),"), max is ",NRules-1 ) ; // ensure we can use RuleIdx as index
		//
		_compute_prios(new_rules_) ;
		//
		::umap<Crc,RuleData const*> old_rds   ; if (+Rule::s_rules) old_rds.reserve(Rule::s_rules->size()) ;
		::umap<Crc,RuleData      *> new_rds   ;                     new_rds.reserve(new_rules_   . size()) ;
		::uset_s                    new_names ;
		for( Rule r : rule_lst() ) old_rds.try_emplace(r->crc->match,&*r) ;
		for( RuleData& rd : new_rules_ ) {
			if (rd.special<Special::NUniq) continue ;
			auto [it,new_crc ] = new_rds.try_emplace(rd.crc->match,&rd)  ;
			bool     new_name  = new_names.insert(rd.user_name()).second ;
			if ( !new_crc && !new_name ) throw cat("rule ",rd.user_name()," appears twice"                                                       ) ;
			if ( !new_crc              ) throw cat("rules ",rd.user_name()," and ",it->second->user_name()," match identically and are redundant") ;
			if (             !new_name ) throw cat("2 rules have the same name ",rd.user_name()                                                  ) ;
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
				new_rd->exe_time       = old_rd.exe_time       ;
				new_rd->stats_weight   = old_rd.stats_weight   ;
			}
		}
		bool invalidate = n_new_rules || n_old_rules || modified_rule_order ;
		if (!first_time) {                                                                                      // check if compatible with dynamic update
			throw_if( n_new_rules         , "new rules appeared"           ) ;
			throw_if( n_old_rules         , "old rules disappeared"        ) ;
			throw_if( n_modified_cmd      , "rule cmd's were modified"     ) ;
			throw_if( n_modified_rsrcs    , "rule resources were modified" ) ;
			throw_if( modified_rule_order , "rule prio's were modified"    ) ;
			RuleBase::s_from_vec_dyn(::move(new_rules_)) ;
		} else {
			RuleBase::s_from_vec_not_dyn(::move(new_rules_)) ;
			if (invalidate) _compile_psfxs() ;                                                                  // recompute matching
		}
		trace(STR(n_new_rules),STR(n_old_rules),STR(n_modified_prio),STR(n_modified_cmd),STR(n_modified_rsrcs),STR(modified_rule_order)) ;
		// matching report
		{	::map_s<::vector<RuleTgt>> match_report ;
			size_t                     w_prio       = 4 ;                                                       // 4 to account for header : prio
			size_t                     w_name       = 4 ;                                                       // 4 to account for header : name
			for( PsfxIdx sfx_idx : _g_sfxs_file.lst() ) {
				::string sfx      = _g_sfxs_file.str_key(sfx_idx) ;
				PsfxIdx  pfx_root = _g_sfxs_file.at     (sfx_idx) ;
				bool     single   = +sfx && sfx[0]==StartMrkr     ;
				for( PsfxIdx pfx_idx : _g_pfxs_file.lst(pfx_root) ) {
					RuleTgts           rts   = _g_pfxs_file.at     (pfx_idx)        ;
					::string           pfx   = _g_pfxs_file.str_key(pfx_idx)        ;
					::string           key   = single ? sfx.substr(1) : pfx+'*'+sfx ;
					::vector<RuleTgt>& entry = match_report[key]                    ;
					for( RuleTgt rt : rts.view() ) {
						entry.push_back(rt) ;
						w_prio = ::max(w_prio,cat(rt->rule->user_prio).size()) ;
						w_name = ::max(w_name,rt->rule->user_name()   .size()) ;
					}
				}
			}
			::string match_report_str ;
			match_report_str << "#\t" << widen("prio",w_prio) <<' '<<widen("rule",w_name) <<' '<< "target" <<'\n' ;;
			for( auto const& [key,rts] : match_report ) {
				match_report_str << key <<" :\n" ;
				for( RuleTgt rt : rts ) match_report_str <<'\t'<< widen(cat(rt->rule->user_prio),w_prio) <<' '<< widen(rt->rule->user_name(),w_name) <<' '<< rt.key() <<'\n' ;
			}
			AcFd( cat(AdminDirS,"matching") , {O_WRONLY|O_TRUNC|O_CREAT} ).write( match_report_str ) ;
		}
		// rule report
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
			AcFd( cat(AdminDirS,"rules") , {O_WRONLY|O_TRUNC|O_CREAT} ).write( content ) ;
		}
		trace("done") ;
		return invalidate ;
	}

	bool/*invalidate*/ new_srcs( Sources&& src_names , ::string const& manifest ) {
		static bool s_first_time = true ; bool first_time = s_first_time ; s_first_time = false ;
		//
		size_t               n_codecs       = g_config->codecs.size() ;
		size_t               n_old_srcs     = Node::s_srcs(false/*dirs*/).size() + Node::s_srcs(true/*dirs*/).size() ;
		NfsGuard             nfs_guard      { first_time ? FileSync::None : g_config->file_sync }                    ; // when dynamic, sources may be modified from jobs
		::vmap<Node,FileTag> srcs           ; srcs    .reserve(src_names.size()+n_codecs)                            ; // worst case
		::umap<Node,FileTag> old_srcs       ; old_srcs.reserve(n_old_srcs               )                            ;
		::umap<Node,FileTag> new_srcs       ; new_srcs.reserve(src_names.size()+n_codecs)                            ; // worst case
		::uset<Node        > src_dirs       ;
		::uset<Node        > old_src_dirs   ;
		::uset<Node        > new_src_dirs   ;
		::uset_s             ext_src_dirs_s ;
		::uset_s             lcl_src_regs   ;
		bool                 has_codecs     = +g_config->codecs                                                      ;
		Trace trace("new_srcs",src_names.size(),manifest) ;
		// check and format new srcs
		size_t      repo_root_depth = ::count(*g_repo_root_s,'/') - 1                                                                                    ; // account for terminating /
		RealPathEnv rpe             { .lnk_support=g_config->lnk_support , .repo_root_s=*g_repo_root_s , .tmp_dir_s=*g_repo_root_s+PRIVATE_ADMIN_DIR_S } ;
		RealPath    real_path       { rpe                                                                                                              } ;
		// user report done before analysis so manifest is available for investigation in case of error
		{	::string content ;
			for( ::string const& src : src_names ) content << src <<'\n' ;
			AcFd( manifest , {O_WRONLY|O_TRUNC|O_CREAT} ).write( content ) ;
		}
		for( ::string& src : src_names ) {
			throw_unless( +src , "found an empty source" ) ;
			bool is_dir_ = is_dir_name(src) ;
			if (!is_canon(src)) {
				if ( ::string c=mk_canon(src) ; c!=src ) throw cat("source ",is_dir_?"dir ":"","is not cannonical : ",src," is not canonical (consider ",c,')') ;
				else                                     throw cat("source ",is_dir_?"dir ":"","is not cannonical : ",src                                     ) ;
			}
			if (Record::s_is_simple(src)) throw cat("source ",is_dir_?"dir ":"",src," cannot lie within system directories") ;
			//
			if (is_dir_) {
				if ( size_t lvl=uphill_lvl(src) ; lvl>=repo_root_depth ) {
					if (lvl==repo_root_depth) throw cat("use absolute name to access source dir "   ,src," from repo ",*g_repo_root_s,rm_slash) ;
					else                      throw cat("too many .. to access relative source dir ",src," from repo ",*g_repo_root_s,rm_slash) ;
				}
				src.pop_back() ;
			}
			FileTag               tag ;
			RealPath::SolveReport sr  = real_path.solve(src,true/*no_follow*/) ;
			if (+sr.lnks) throw cat("source ",is_dir_?"dir ":"",src,"/ has symbolic link ",sr.lnks[0]," in its path") ; // cannot use throw_if as sr.lnks[0] is illegal if !sr.lnks
			if (is_dir_) {
				tag = FileTag::Dir ;
				if (sr.file_loc>FileLoc::Repo) ext_src_dirs_s.insert(with_slash(src)) ;
			} else {
				throw_unless( sr.file_loc==FileLoc::Repo , "source ",src," is not in repo" ) ;
				SWEAR( src==sr.real , src,sr.real ) ;                                          // src is local, canonic and there are no links, what may justify real from being different ?
				tag = FileInfo(src,{.nfs_guard=&nfs_guard}).tag() ;
				switch (tag) {
					case FileTag::Dir   : tag = FileTag::None ; break ;                        // dirs do not officially exist as source
					case FileTag::Empty : tag = FileTag::Reg  ; break ;                        // do not remember file is empty, so it is marked new instead of steady/changed when first seen
				DN}
				if ( has_codecs && tag==FileTag::Reg ) lcl_src_regs.insert(src) ;
			}
			srcs.emplace_back( Node(New,src,sr.file_loc>FileLoc::Repo) , tag ) ;               // external src dirs need no uphill dir
		}
		// format old srcs
		for( bool is_dir : {false,true} ) for( Node s : Node::s_srcs(is_dir) ) old_srcs.emplace(s,is_dir?FileTag::Dir:FileTag::None) ;           // dont care whether we delete a regular file or a link
		//
		for( auto [n,_] : srcs     ) for( Node d=n->dir ; +d ; d = d->dir ) if (!src_dirs    .insert(d).second) break ;                          // non-local nodes have no dir
		for( auto [n,_] : old_srcs ) for( Node d=n->dir ; +d ; d = d->dir ) if (!old_src_dirs.insert(d).second) break ;                          // .
		// further checks
		for( auto [n,t] : srcs ) {
			if (!src_dirs.contains(n)) continue ;
			::string nn   = n->name() ;
			::string nn_s = nn+'/'    ;
			for( ::string const& sn : src_names ) throw_if( sn.starts_with(nn_s) , "source ",t==FileTag::Dir?"dir ":"",nn," is a dir of ",sn ) ;
			FAIL(nn,"is a source dir of no source") ;                                                                                            // NO_COV
		}
		for( auto const& [key,val] : g_config->codecs ) {
			if (!is_canon(val.tab)) {
				if ( ::string c=mk_canon(val.tab) ; c!=val.tab ) throw cat("codec table is not cannonical : ",val.tab," (consider ",c,')') ;
				else                                             throw cat("codec table is not cannonical : ",val.tab                    ) ;
			}
			//
			RealPath::SolveReport sr = real_path.solve(no_slash(val.tab),false/*no_follow*/) ;                                         // cannot use throw_if as sr.lnks[0] is illegal if !sr.lnks
			if (+sr.lnks) throw cat("codec table ",val.tab," has symbolic link ",sr.lnks[0]," in its path") ;
			//
			if (val.is_dir()) {
				if (is_lcl(val.tab)) throw cat("codec table ",key," must not end with /, consider : lmake.config.codecs.",key," = ",mk_py_str(no_slash(val.tab))) ;
				try {
					for( ::string d_s=val.tab ;; d_s=dir_name_s(d_s) )                                                                 // try all accessible uphill dirs
						if (ext_src_dirs_s.contains(d_s)) goto CodecFound ;
				} catch (::string const&) {
					if (+g_config->extra_manifest) throw cat("codec table ",key," must lie within a source dir, consider : lmake.extra_manifest.append(",mk_py_str(val.tab),')') ;
					else                           throw cat("codec table ",key," must lie within a source dir, consider : lmake.extra_manifest = ["    ,mk_py_str(val.tab),']') ;
				}
			} else {
				if (!is_lcl(val.tab)) throw cat("codec table ",key," must end with /, consider : lmake.config.codecs.",key," = ",mk_py_str(with_slash(val.tab))) ;
				if (lcl_src_regs.contains(val.tab)) goto CodecFound ;
				throw cat("codec table ",key," must be a source, consider : git add ",mk_file(val.tab,FileDisplay::Shell)) ;
			}                                                                                                                          // solve lazy
		CodecFound : ;
		}
		// compute diff
		bool fresh = !old_srcs ;
		for( auto nt : srcs ) {
			auto it = old_srcs.find(nt.first) ;
			if (it==old_srcs.end()) new_srcs.insert(nt) ;
			else                    old_srcs.erase (it) ;
		}
		if (!fresh) {
			for( auto [n,t] : new_srcs ) if (t==FileTag::Dir) throw cat("new source dir ",n->name(),", consider : ",git_clean_msg()) ; // we may have missed some deps, and this is unpredictable
			for( auto [n,t] : old_srcs ) if (t==FileTag::Dir) throw cat("old source dir ",n->name(),", consider : ",git_clean_msg()) ; // XXX? : this could be managed if necessary (is it worth?)
		}
		//
		for( Node d : src_dirs ) { if ( auto it=old_src_dirs.find(d) ; it!=old_src_dirs.end() ) old_src_dirs.erase(it) ; else new_src_dirs.insert(d) ; }
		//
		if ( !old_srcs && !new_srcs ) return false/*invalidate*/ ;
		if (!first_time) {
			if (+new_srcs) throw "new source "    +new_srcs.begin()->first->name() ;
			if (+old_srcs) throw "removed source "+old_srcs.begin()->first->name() ;
			FAIL() ;                                                                                           // NO_COV
		}
		//
		trace("srcs",'-',old_srcs.size(),'+',new_srcs.size()) ;
		// commit
		for( bool add : {false,true} ) {
			::umap<Node,FileTag> const& srcs = add ? new_srcs : old_srcs ;
			::vector<Node>              ss   ;                             ss.reserve(srcs.size()) ;           // typically, there are very few src dirs
			::vector<Node>              sds  ;                                                                 // .
			for( auto [n,t] : srcs ) if (t==FileTag::Dir) sds.push_back(n) ; else ss.push_back(n) ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Node::s_srcs(false/*dirs*/,add,ss ) ;
			Node::s_srcs(true /*dirs*/,add,sds) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		bool invalidate = +old_srcs ;
		{	Trace trace2 ;
			for( auto [n,t] : old_srcs ) {
				Node(n)->mk_no_src() ;
				trace2('-',t==FileTag::Dir?"dir":"",n) ;
			}
			for( Node d : old_src_dirs ) d->mk_no_src() ;
			for( auto [n,t] : new_srcs ) {
				if (!( n->buildable==Buildable::Unknown || n->buildable>=Buildable::Yes )) invalidate = true ; // if node was unknown or known buildable, making it a source cannot change matching
				Node(n)->mk_src( t==FileTag::Dir?Buildable::SrcDir:Buildable::Src , t ) ;
				trace2('+',t==FileTag::Dir?"dir":"",n) ;
			}
			for( Node d : new_src_dirs ) d->mk_src( Buildable::Anti , Crc::None ) ;
		}
		_compile_srcs() ;
		// user report
		{	::string content ;
			for( auto [n,t] : srcs ) content << n->name() << (t==FileTag::Dir?"/":"") <<'\n' ;
			AcFd( manifest , {O_WRONLY|O_TRUNC|O_CREAT} ).write( content ) ;
		}
		trace("done",srcs.size(),"srcs") ;
		return invalidate ;
	}

	void invalidate_match(bool force_physical) {
		MatchGen& match_gen = _g_rule_crc_file.hdr() ;
		Trace trace("invalidate_match","old gen",match_gen) ;
		match_gen++ ;                                         // increase generation, which automatically makes all nodes !match_ok()
		if ( force_physical || match_gen==0 ) {               // unless we wrapped around
			trace("reset") ;
			Fd::Stderr.write("collecting nodes ...") ;
			for( Node n : node_lst() ) n->mk_old() ;
			Fd::Stderr.write(" done\n") ;                     // physically reset node match_gen's
			match_gen = 1 ;
		}
		Rule::s_match_gen = match_gen ;
	}

}
