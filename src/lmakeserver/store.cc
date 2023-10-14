// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

using Backends::Backend ;

using namespace Disk ;

namespace Engine {

	Version DbVersion = {1,0} ;

	// str has target syntax
	// return suffix after last stem (StartMrkr+str if no stem)
	static ::string parse_suffix(::string const& str) {
		size_t pos = 0 ;
		for(;;) {
			size_t nxt_pos = str.find(Rule::StemMrkr,pos) ;
			if (nxt_pos==Npos) break ;
			pos = nxt_pos+1+sizeof(VarIdx) ;
		}
		if (pos==0) return EngineStore::StartMrkr+str ;                        // signal that there is no stem by prefixing with StartMrkr
		else        return str.substr(pos)            ;                        // suppress stem marker & stem idx
	}
	// return prefix before first stem (empty if no stem)
	static ::string parse_prefix(::string const& str) {
		size_t pos = str.find(Rule::StemMrkr) ;
		if (pos==Npos) return {}                ;                              // absence of stem is already signal in parse_suffix, we just need to pretend there is no prefix
		else           return str.substr(0,pos) ;
	}

	//
	// RuleBase
	//

	MatchGen RuleBase::s_match_gen ;

	//
	// Store
	//

	SeqId     * g_seq_id = nullptr            ;
	EngineStore g_store  { true/*writable*/ } ;
	Config      g_config ;

	void EngineStore::_s_init_config() {
		try         { g_config = deserialize<Config>(IFStream(AdminDir+"/config_store"s)) ; }
		catch (...) { return ;                                                              }
		if (!( g_config.db_version.major==DbVersion.major && g_config.db_version.minor<=DbVersion.minor ))
			throw to_string( "data base version mismatch : expected : " , DbVersion.major,'.',DbVersion.minor , " found : " , g_config.db_version.major,'.',g_config.db_version.minor , '\n' ) ;
	}

	void EngineStore::_s_init_srcs_rules(bool rescue) {
		Trace trace("_init_srcs_rules",Pdate::s_now()) ;
		::string dir      = g_config.local_admin_dir+"/store" ;
		bool     writable = g_store.writable                  ;
		//
		make_dir(dir) ;
		// jobs
		g_store.job_file         .init( dir+"/job"          , writable ) ;
		g_store.deps_file        .init( dir+"/deps"         , writable ) ;
		g_store.star_targets_file.init( dir+"/star_targets" , writable ) ;
		// nodes
		g_store.node_idx_file    .init( dir+"/node_idx"     , writable ) ;
		g_store.node_data_file   .init( dir+"/node_data"    , writable ) ;
		g_store.job_tgts_file    .init( dir+"/job_tgts"     , writable ) ;
		// rules
		g_store.rule_str_file    .init( dir+"/rule_str"     , writable ) ;
		g_store.rule_file        .init( dir+"/rule"         , writable ) ;
		g_store.rule_tgts_file   .init( dir+"/rule_tgts"    , writable ) ;
		// commons
		g_store.name_file        .init( dir+"/name"         , writable ) ;
		// misc
		if (writable) {
			g_seq_id = &g_store.job_file.hdr().seq_id ;
			if (!*g_seq_id) *g_seq_id = 1 ;                                    // avoid 0 (when store is brand new) to decrease possible confusion
		}
		// memory
		g_store.sfxs.init(New) ;
		g_store.pfxs.init(New) ;
		// Rule
		if (g_store.rule_file.empty()) for( [[maybe_unused]] Special s : Special::N ) g_store.rule_file.emplace() ;
		RuleBase::s_match_gen = g_store.rule_file.c_hdr() ;
		g_store._compile_rules() ;
		// Node
		if (g_store.node_data_file.empty()) {
			SWEAR_PROD(g_store.node_data_file.writable) ;
			for( NodeIdx i=1 ; i<=NodeData::NShared ; i++ ) {
				NodePtr np = g_store.node_data_file.emplace(NodeData::s_mk_shared(i)) ;
				SWEAR( +np==i , np , i ) ;
			}
		}
		trace("done",Pdate::s_now()) ;
		//
		if (rescue) {
			::cerr<<"previous crash detected, checking & rescueing"<<endl ;
			g_store.chk()        ;                                             // first verify we have a coherent store
			s_invalidate_match() ;                                             // then rely only on essential data that should be crash-safe
			::cerr<<"seems ok"<<endl ;
		}
	}

	static bool _has_new_server_addr( Config const& old_config , Config const& new_config ) {
		for( BackendTag t : BackendTag::N ) if (new_config.backends[+t].addr!=old_config.backends[+t].addr) return true  ;
		/**/                                                                                                return false ;
	}
	void EngineStore::_s_set_config(Config&& new_config) {
		if ( !g_config.local_admin_dir.empty() && new_config.local_admin_dir!=g_config.local_admin_dir ) {
			if ( ::rename( g_config.local_admin_dir.c_str() , new_config.local_admin_dir.c_str() ) != 0 )
				// XXX : implement physical move of local admin dir
				throw to_string("physically moving local admin dir from ",g_config.local_admin_dir," to ",new_config.local_admin_dir," not yet implemented, please clean repository") ;
		}
		g_config            = ::move(new_config) ;
		g_config.db_version = DbVersion          ;
		serialize( OFStream(dir_guard(AdminDir+"/config_store"s)) , g_config ) ;
		{	OFStream config_stream{AdminDir+"/config"s} ;
			config_stream << g_config.pretty_str() ;
		}
		make_dir( to_string(g_config.local_admin_dir,"/outputs") , true/*unlink_ok*/ ) ;
		make_dir( AdminDir+"/job_keep_tmp"s                      , true/*unlink_ok*/ ) ;
	}

	void EngineStore::_s_diff_config(Config const& old_config) {
		Trace trace("_diff_config",old_config) ;
		if      ( g_config.lnk_support   >  old_config.lnk_support   ) g_store.invalidate_exec(false/*cmd_ok*/) ; // we could discover new deps            , do as if we have new commands
		else if ( _has_new_server_addr(old_config,g_config)          ) g_store.invalidate_exec(true /*cmd_ok*/) ; // remote hosts may have been unreachable, do as if we have new resources
		if      ( g_config.max_dep_depth >  old_config.max_dep_depth ) s_invalidate_match()                     ; // we may discover new buildable nodes
		else if ( g_config.path_max      >  old_config.path_max      ) s_invalidate_match()                     ; // .
	}

	void EngineStore::s_keep_config(bool rescue) {
		_s_init_config    (      ) ;
		g_config.open     (      ) ;
		_s_init_srcs_rules(rescue) ;
	}

	void EngineStore::s_new_config( Config&& config , bool rescue ) {
		Trace trace("s_new_config",Pdate::s_now(),STR(rescue)) ;
		_s_init_config() ;
		Config old_config = g_config ;
		_s_set_config(::move(config)) ;
		g_config.open() ;
		_s_init_srcs_rules(rescue) ;
		_s_diff_config(old_config) ;
		trace("done",Pdate::s_now()) ;
	}

	void EngineStore::s_new_makefiles( ::umap<Crc,RuleData>&& rules , ::vector_s&& srcs ) {
		Trace trace("s_new_makefiles",Pdate::s_now()) ;
		bool invalidate_src = _s_new_srcs ( mk_vector<Node>(srcs)           ) ;
		/**/                  _s_new_rules( ::move(rules ) , invalidate_src ) ;
		trace("done",Pdate::s_now()) ;
	}

	void EngineStore::_compile_rule_datas() {
		::vector<Rule> rules   = rule_lst()                                             ;
		RuleIdx        n_rules = ::max( +::max(rules) , RuleIdx(+Special::Shared) ) + 1 ;
		rule_datas.clear() ; rule_datas.resize(n_rules) ;                                            // clearing before resize ensure all unused entries are clean
		for( Special s : Special::N ) if ( +s && s<=Special::Shared ) rule_datas[+s] = RuleData(s) ;
		RuleData::s_name_sz = 0 ;
		for( Rule r : rules ) {
			rule_datas[+r] = r.str() ;
			RuleData::s_name_sz = ::max( RuleData::s_name_sz , rule_datas[+r].name.size() ) ;
		}
	}
	template<bool IsSfx> static void _propagate_to_longer(::umap_s<uset<RuleTgt>>& psfx_map) {
		::vector_s psfxs = ::mk_key_vector(psfx_map) ;
		::sort( psfxs , [](::string const& a,::string const& b){ return a.size()<b.size() ; } ) ;
		for( ::string const& long_psfx : psfxs ) {
			for( size_t l=1 ; l<=long_psfx.size() ; l++ ) {
				::string short_psfx = long_psfx.substr( IsSfx?l:0 , long_psfx.size()-l ) ;
				if (psfx_map.contains(short_psfx)) {
					psfx_map.at(long_psfx).merge(::clone(psfx_map.at(short_psfx))) ; // copy arg as merge clobbers it
					break ;                                                          // psfx's are sorted shortest first, so as soon as a short one is found, it is already merged with previous ones
				}
			}
		}
	}
	// make a prefix/suffix map that records which rule has which prefix/suffix
	void EngineStore::_compile_psfxs() {
		sfxs.clear() ;
		pfxs.clear() ;
		// first compute a suffix map
		::umap_s<uset<RuleTgt>> sfx_map ;
		for( Rule r : rule_lst() )
			for( VarIdx ti=0 ; ti<r->targets.size() ; ti++ ) {
				RuleTgt rt{r,ti} ;
				if (!rt.tflags()[Tflag::Match]) continue ;
				sfx_map[ parse_suffix(rt.target()) ].insert(rt) ;
			}
		_propagate_to_longer<true/*IsSfx*/>(sfx_map) ;                         // propagate to longer suffixes as a rule that matches a suffix also matches any longer suffix
		//
		// now, for each suffix, compute a prefix map
		for( auto const& [sfx,sfx_rule_tgts] : sfx_map ) {
			::umap_s<uset<RuleTgt>> pfx_map ;
			if ( sfx.starts_with(StartMrkr) ) {
				::string_view sfx1 = ::string_view(sfx).substr(1) ;
				for( RuleTgt rt : sfx_rule_tgts ) {
					::string pfx = parse_prefix(rt.target()) ;
					if (sfx1.starts_with(pfx)) pfx_map[""].insert(rt) ;
				}
			} else {
				for( RuleTgt rt : sfx_rule_tgts )
					pfx_map[ parse_prefix(rt.target()) ].insert(rt) ;
				_propagate_to_longer<false/*IsSfx*/>(pfx_map) ;                // propagate to longer prefixes as a rule that matches a prefix also matches any longer prefix
			}
			//
			// store proper rule_tgts (ordered by decreasing prio, giving priority to AntiRule within each prio) for each prefix/suffix
			PsfxIdx pfx_root = pfxs.emplace_root() ;
			sfxs.insert_at(sfx) = pfx_root ;
			for( auto const& [pfx,pfx_rule_tgts] : pfx_map ) {
				vector<RuleTgt> pfx_rule_tgt_vec = mk_vector(pfx_rule_tgts) ;
				::sort(
					pfx_rule_tgt_vec
				,	[]( RuleTgt const& a , RuleTgt const& b )->bool {
						// compulsery : order by priority, with Anti's first within each prio
						// optim      : put more specific rules before more generic ones to favor sharing RuleTgts in reversed PrefixFile
						// finally    : any stable sort is fine, just to avoid random order
						::string a_tgt = a.target() ; size_t a_psfx_sz = parse_prefix(a_tgt).size() + parse_suffix(a_tgt).size() ;
						::string b_tgt = b.target() ; size_t b_psfx_sz = parse_prefix(b_tgt).size() + parse_suffix(b_tgt).size() ;
						return ::tuple(a->prio,a->special,a_psfx_sz,a->name) > ::tuple(b->prio,b->special,b_psfx_sz,b->name) ;
					}
				) ;
				pfxs.insert_at(pfx_root,pfx) = RuleTgts(pfx_rule_tgt_vec) ;
			}
		}
	}
	void EngineStore::_compile_rules() {
		_compile_rule_datas() ;
		_compile_psfxs     () ;
	}

	void EngineStore::_save_rules() {
		rule_str_file.clear() ;
		for( Rule r : rule_lst() ) rule_file.at(r) = rule_str_file.emplace(::string(rule_datas[+r])) ;
	}

	void EngineStore::invalidate_exec(bool cmd_ok) {
		Trace trace("invalidate_exec",STR(cmd_ok)) ;
		::vector<pair<bool,ExecGen>> keep_cmd_gens ;
		keep_cmd_gens.resize(rule_datas.size()) ;
		for( Rule r : rule_lst() ) {
			_s_set_exec_gen( rule_datas[+r] , keep_cmd_gens[+r] , cmd_ok , false/*rsrcs_ok*/ ) ;
			trace(r,r->cmd_gen,r->rsrcs_gen) ;
		}
		_save_rules() ;
		_s_invalidate_exec(keep_cmd_gens) ;
	}

	//
	// NewRulesSrcs
	//

	void EngineStore::_s_new_rules( ::umap<Crc,RuleData>&& new_rules , bool force_invalidate ) {
		Trace trace("_new_rules",new_rules.size()) ;
		//
		::umap <Crc,Rule> old_rules ; for( Rule r : g_store.rule_lst() ) old_rules[r->match_crc] = r ;
		//
		RuleIdx n_new_rules      = 0 ; for( auto& [match_crc,new_rd] : new_rules ) n_new_rules += !old_rules.contains(match_crc) ;
		RuleIdx n_modified_cmd   = 0 ;
		RuleIdx n_modified_rsrcs = 0 ;
		RuleIdx n_modified_prio  = 0 ;
		// make old rules obsolete but do not pop to prevent idx reuse as long as old rules are not collected
		for( auto const& [crc,r] : old_rules ) if (!new_rules.contains(crc)) g_store.rule_file.clear(r) ;
		// in case of size overflow, physically collect obsolete rules as we cannot fit old & new rules
		if ( old_rules.size()+n_new_rules >= RuleIdx(-1) ) _s_collect_old_rules() ;
		// indexed by Rule, if entry.first => entry.second is 0 (always reset exec_gen) or exec_gen w/ cmd_ok but !rsrcs_ok
		::vector<pair<bool,ExecGen>> keep_cmd_gens{RuleIdx( +::max(mk_val_vector(old_rules)) + 1 )} ;
		//
		g_store.rule_str_file.clear() ;                                        // erase old rules before recording new ones
		for( auto& [match_crc,new_rd] : new_rules ) {
			Rule old_r ;
			if (old_rules.contains(match_crc)) {
				old_r = old_rules[match_crc] ;
				old_rules.erase(match_crc) ;
				bool cmd_ok   = new_rd.cmd_crc  ==old_r->cmd_crc   ;
				bool rsrcs_ok = new_rd.rsrcs_crc==old_r->rsrcs_crc ;
				new_rd.cmd_gen      = old_r->cmd_gen      ;
				new_rd.rsrcs_gen    = old_r->rsrcs_gen    ;
				new_rd.exec_time    = old_r->exec_time    ;
				new_rd.stats_weight = old_r->stats_weight ;
				SWEAR( +old_r<keep_cmd_gens.size() , old_r , keep_cmd_gens.size() ) ;
				_s_set_exec_gen( new_rd , keep_cmd_gens[+old_r] , cmd_ok , rsrcs_ok ) ;
				n_modified_cmd   += !cmd_ok              ;
				n_modified_rsrcs +=  cmd_ok && !rsrcs_ok ;
				if ( new_rd.prio!=old_r->prio ) n_modified_prio++ ;
				if ( !cmd_ok || !rsrcs_ok     ) trace("modified",new_rd,STR(cmd_ok),new_rd.cmd_gen,STR(rsrcs_ok),new_rd.rsrcs_gen) ;
			} else {
				trace("new",new_rd,new_rd.cmd_gen,new_rd.rsrcs_gen) ;
			}
			//           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			RuleStr rs = g_store.rule_str_file.emplace(::string(new_rd)) ;
			//           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (+old_r) g_store.rule_file.at(old_r) = rs ;
			else        g_store.rule_file.emplace(rs) ;
		}
		trace("rules",'-',old_rules.size(),'+',n_new_rules,"=cmd",n_modified_cmd,"=rsrcs",n_modified_rsrcs,"=prio",n_modified_prio) ;
		bool invalidate = n_modified_prio || n_new_rules || old_rules.size() || force_invalidate ;
		//
		g_store._compile_rules() ;                                             // recompute derived info
		if (invalidate) s_invalidate_match(             ) ;
		/**/            _s_invalidate_exec(keep_cmd_gens) ;
		// trace
		Trace trace2 ;
		for( PsfxIdx sfx_idx : g_store.sfxs.lst() ) {
			::string sfx      = g_store.sfxs.str_key(sfx_idx) ;
			PsfxIdx  pfx_root = g_store.sfxs.at     (sfx_idx) ;
			bool single = !sfx.empty() && sfx[0]==StartMrkr ;
			for( PsfxIdx pfx_idx : g_store.pfxs.lst(pfx_root) ) {
				RuleTgts rts = g_store.pfxs.at(pfx_idx) ;
				::string pfx = g_store.pfxs.str_key(pfx_idx) ;
				if (single) { SWEAR(pfx.empty(),pfx) ; trace2(         sfx.substr(1) , ':' ) ; }
				else        {                          trace2( pfx+'*'+sfx           , ':' ) ; }
				Trace trace3 ;
				for( RuleTgt rt : rts.view() ) trace3( +Rule(rt) , ':' , rt->prio , rt->name , rt.key() ) ;
			}
		}
		// user report
		{	OFStream rules_stream{AdminDir+"/rules"s} ;
			::vector<Rule> rules = g_store.rule_lst() ;
			::sort( rules , [](Rule a,Rule b){
				if (a->prio!=b->prio) return a->prio > b->prio ;
				else                  return a->name < b->name ;
			} ) ;
			bool first = true ;
			for( Rule rule : rules ) {
				if (!rule->user_defined()) continue ;
				if (first) first = false ;
				else       rules_stream << '\n' ;
				rules_stream<<rule->pretty_str() ;
			}
		}
	}

	bool/*invalidate*/ EngineStore::_s_new_srcs( ::vector<Node>&& src_vec ) {
		::set<Node> srcs         ;                                             // use ordered set/map to ensure stable execution (as we walk through them) (and we do not care about mem/perf)
		::set<Node> src_dirs     ;                                             // .
		::set<Node> old_src_dirs ;                                             // .
		::set<Node> new_src_dirs ;                                             // .
		::set<Node> old_srcs     ;                                             // .
		::set<Node> new_srcs     ;                                             // .
		Trace trace("_s_new_srcs") ;
		// format inputs
		for( Node n : Node::s_srcs() ) old_srcs.insert(n) ;
		for( Node n : src_vec        ) srcs    .insert(n) ;
		//
		for( Node n : srcs     ) { for( Node d=n.dir() ; +d ; d = d.dir() ) { if (src_dirs    .contains(d)) break ; src_dirs    .insert(d) ; } }
		for( Node n : old_srcs ) { for( Node d=n.dir() ; +d ; d = d.dir() ) { if (old_src_dirs.contains(d)) break ; old_src_dirs.insert(d) ; } }
		// check
		for( Node d : src_vec ) {
			if (!src_dirs.contains(d)) continue ;
			::string dn = d.name()+'/' ;
			for( Node n : src_vec )
				if ( n.name().starts_with(dn) ) throw to_string("source ",dn," is a dir of ",n.name()) ;
			FAIL(dn,"is a source dir of no source") ;
		}
		// compute diff
		for( Node n : srcs ) {
			auto it = old_srcs.find(n) ;
			if (it==old_srcs.end()) new_srcs.insert(n) ;
			else                    old_srcs.erase(it) ;
		}
		//
		for( Node d : src_dirs ) { if (old_src_dirs.contains(d)) old_src_dirs.erase(d) ; else new_src_dirs.insert(d) ; }
		//
		trace("srcs",'-',old_srcs.size(),'+',new_srcs.size()) ;
		// commit
		//    vvvvvvvvvvvvvvvvvvvvvvv
		Node::s_srcs(mk_vector(srcs)) ;
		//    ^^^^^^^^^^^^^^^^^^^^^^^
		{	Trace trace2 ;
			for( Node n : old_srcs     ) { Node (n). mk_no_src  () ; trace2('-',n) ; }
			for( Node d : old_src_dirs ) { d       . mk_no_src  () ;                 }
			for( Node n : new_srcs     ) { Node (n). mk_src     () ; trace2('+',n) ; }
			for( Node d : new_src_dirs ) { d       . mk_anti_src() ;                 }
		}
		// user report
		{	OFStream srcs_stream{AdminDir+"/sources"s} ;
			for( Node n : srcs ) srcs_stream << n.name() <<'\n' ;
		}
		trace("done",srcs.size(),"srcs") ;
		return old_srcs.size() || new_srcs.size() ;
	}

	void EngineStore::_s_set_exec_gen( RuleData& rd , ::pair<bool,ExecGen>& keep_cmd_gen , bool cmd_ok , bool rsrcs_ok ) {
		if ( cmd_ok && rsrcs_ok ) return ;
		Trace trace("_s_set_exec_gen") ;
		if (rd.rsrcs_gen<NExecGen-1) {
			if (!cmd_ok) rd.cmd_gen   = rd.rsrcs_gen+1 ;
			/**/         rd.rsrcs_gen = rd.rsrcs_gen+1 ;
			Trace trace("up gen",rd.cmd_gen,rd.rsrcs_gen) ;
		} else {
			rd.cmd_gen   = 1                 ;                                 // 0 is reserved to force !cmd_ok
			rd.rsrcs_gen = rd.cmd_gen+cmd_ok ;                                 // if cmd_ok, we must distinguish between bad cmd and good cmd with bad rsrcs
			if (!cmd_ok) keep_cmd_gen = {true,0         } ;                    // all cmd_gen must be set to 0 as we have a new cmd but no room left for a new cmd_gen
			else         keep_cmd_gen = {true,rd.cmd_gen} ;                    // all cmd_gen above this will be set to 1 to keep cmd, the others to 0
			trace("reset gen",rd.cmd_gen,rd.rsrcs_gen) ;
		}
	}

	void EngineStore::_s_collect_old_rules() {                                 // may be long, avoid as long as possible
		MatchGen& match_gen = g_store.rule_file.hdr() ;
		Trace("_s_collect_old_rules","reset",1) ;
		for( Node     n   : g_store.node_lst          () ) n  .mk_old()         ; // handle nodes first as jobs are necessary at this step
		for( Job      j   : g_store.job_lst           () ) j  .invalidate_old() ;
		for( RuleTgts rts : g_store.rule_tgts_file.lst() ) rts.invalidate_old() ;
		for( Rule     r   : g_store.rule_lst          () ) r  .invalidate_old() ; // now that old rules are not referenced any more, they can be suppressed
		Rule::s_match_gen = match_gen = 1 ;
	}
	void EngineStore::s_invalidate_match() {
		MatchGen& match_gen = g_store.rule_file.hdr() ;
		if (match_gen<NMatchGen-1) {
			// increase generation, which automatically makes all nodes !match_ok()
			Trace("s_invalidate_match","new gen",match_gen+1) ;
			match_gen++ ;
			Rule::s_match_gen = match_gen ;
		} else {
			_s_collect_old_rules() ;
		}
	}

	void EngineStore::_s_invalidate_exec(::vector<pair<bool,ExecGen>> const& keep_cmd_gens) {
		for( auto [yes,cmd_gen] : keep_cmd_gens ) if (yes) goto FullPass ;
		return ;
	FullPass :
		Trace trace("s_invalidate_exec") ;
		Trace trace2 ;
		for( Job j : g_store.job_lst() ) {
			if (j->rule.is_shared()) continue ;
			auto [yes,cmd_gen] = keep_cmd_gens[+j->rule] ;
			if (!yes) continue ;
			ExecGen old_exec_gen = j->exec_gen ;
			j->exec_gen = cmd_gen && j->exec_gen >= cmd_gen ;                  // set to 0 if bad cmd, to 1 if cmd ok but bad rsrcs
			trace2(j,j->rule,old_exec_gen,"->",j->exec_gen) ;
		}
	}

}
