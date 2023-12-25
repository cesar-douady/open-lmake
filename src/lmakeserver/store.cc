// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <tuple>

#include "core.hh"

using Backends::Backend ;

using namespace Disk ;

namespace Engine {

	//
	// RuleBase
	//

	MatchGen         RuleBase::s_match_gen ;
	umap_s<RuleBase> RuleBase::s_by_name   ;

	//
	// NodeBase
	//

	RuleTgts NodeBase::s_rule_tgts(::string const& target_name) {
		// first match on suffix
		PsfxIdx sfx_idx = g_store.sfxs_file.longest(target_name,::string{EngineStore::StartMrkr}).first ; // StartMrkr is to match rules w/ no stems
		if (!sfx_idx) return RuleTgts{} ;
		PsfxIdx pfx_root = g_store.sfxs_file.c_at(sfx_idx) ;
		// then match on prefix
		PsfxIdx pfx_idx = g_store.pfxs_file.longest(pfx_root,target_name).first ;
		if (!pfx_idx) return RuleTgts{} ;
		return g_store.pfxs_file.c_at(pfx_idx) ;

	}

	//
	// Store
	//

	SeqId     * g_seq_id     = nullptr            ;
	EngineStore g_store      { true/*writable*/ } ;
	Config      g_config     ;
	::vector_s  g_src_dirs_s ;

	void EngineStore::_s_init_config() {
		try         { g_config = deserialize<Config>(IFStream(PrivateAdminDir+"/config_store"s)) ; }
		catch (...) { return ;                                                                     }
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
		g_store.node_file        .init( dir+"/node"         , writable ) ;
		g_store.job_tgts_file    .init( dir+"/job_tgts"     , writable ) ;
		// rules
		g_store.rule_str_file    .init( dir+"/rule_str"     , writable ) ;
		g_store.rule_file        .init( dir+"/rule"         , writable ) ;
		g_store.rule_tgts_file   .init( dir+"/rule_tgts"    , writable ) ;
		g_store.sfxs_file        .init( dir+"/sfxs"         , writable ) ;
		g_store.pfxs_file        .init( dir+"/pfxs"         , writable ) ;
		// commons
		g_store.name_file        .init( dir+"/name"         , writable ) ;
		// misc
		if (writable) {
			g_seq_id = &g_store.job_file.hdr().seq_id ;
			if (!*g_seq_id) *g_seq_id = 1 ;                                    // avoid 0 (when store is brand new) to decrease possible confusion
		}
		// memory
		// Rule
		if (g_store.rule_file.empty()) for( [[maybe_unused]] Special s : Special::Shared ) g_store.rule_file.emplace() ;
		RuleBase::s_match_gen = g_store.rule_file.c_hdr() ;
		g_store._compile_srcs () ;
		g_store._compile_rules() ;
		// jobs
		for( Job  j : g_store.job_file .c_hdr().frozen_jobs ) g_store.frozen_jobs.insert(j) ;
		// nodes
		for( Node n : g_store.node_file.c_hdr().frozen_nodes ) g_store.frozen_nodes.insert(n) ;
		for( Node n : g_store.node_file.c_hdr().manual_oks   ) g_store.manual_oks  .insert(n) ;
		for( Node n : g_store.node_file.c_hdr().no_triggers  ) g_store.no_triggers .insert(n) ;
		//
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
		g_config = ::move(new_config) ;
		serialize( OFStream(dir_guard(PrivateAdminDir+"/config_store"s)) , g_config ) ;
		{	OFStream config_stream{AdminDir+"/config"s} ;
			config_stream << g_config.pretty_str() ;
		}
	}

	void EngineStore::_s_diff_config( Config const& old_config , bool dynamic ) {
		Trace trace("_diff_config",old_config) ;
		if (_has_new_server_addr(old_config,g_config)) {
			if (dynamic) throw "cannot change server address while running"s ;
			g_store.invalidate_exec(true /*cmd_ok*/) ;                         // remote hosts may have been unreachable, do as if we have new resources
		}
		//
		if (g_config.path_max!=old_config.path_max) s_invalidate_match() ; // we may discover new buildable nodes or vice versa
	}

	void EngineStore::s_new_config( Config&& config , bool dynamic , bool rescue , ::function<void(Config const& old,Config const& new_)> diff ) {
		Trace trace("s_new_config",Pdate::s_now(),STR(dynamic),STR(rescue)) ;
		if ( !dynamic                                              ) make_dir( AdminDir+"/outputs"s , true/*unlink_ok*/ ) ;
		if ( !dynamic                                              ) _s_init_config() ;
		if (  dynamic                                              ) SWEAR(g_config.booted,g_config) ; // we must update something
		//
		diff(g_config,config) ;
		//
		/**/                                                         ConfigDiff d = config.booted ? g_config.diff(config) : ConfigDiff::None ;
		if (              d>ConfigDiff::Static  && g_config.booted ) throw "repo must be clean"s  ;
		if (  dynamic &&  d>ConfigDiff::Dynamic                    ) throw "repo must be steady"s ;
		//
		if (  dynamic && !d                                        ) return ;  // fast path, nothing to update
		//
		/**/                                                         Config old_config = g_config ;
		if (             +d                                        ) _s_set_config(::move(config))      ;
		/**/                                                         g_config.open(dynamic)             ;
		if ( !dynamic                                              ) _s_init_srcs_rules(rescue)         ;
		if (             +d                                        ) _s_diff_config(old_config,dynamic) ;
		trace("done",Pdate::s_now()) ;
		SWEAR(g_config.booted,g_config) ;                                      // we'd better have a config at the end
	}

	void EngineStore::_compile_rule_datas() {
		::vector<Rule> rules = rule_lst() ;
		RuleData::s_name_sz = "frozen"s.size() ;                               // account for internal names
		rule_datas.clear() ;                                                   // clearing before resize ensure all unused entries are clean
		for( Special s : Special::N ) if ( +s && s<=Special::Shared ) {
			grow(rule_datas,+s) = RuleData(s)                                               ;
			RuleData::s_name_sz = ::max( RuleData::s_name_sz , rule_datas[+s].name.size() ) ;
		}
		for( Rule r : rules ) {
			grow(rule_datas,+r) = r.str()                                                   ;
			RuleData::s_name_sz = ::max( RuleData::s_name_sz , rule_datas[+r].name.size() ) ;
		}
	}
	// str has target syntax
	// return suffix after last stem (StartMrkr+str if no stem)
	static ::string _parse_sfx(::string const& str) {
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
	static ::string _parse_pfx(::string const& str) {
		size_t pos = str.find(Rule::StemMrkr) ;
		if (pos==Npos) return {}                ;                              // absence of stem is already signal in _parse_sfx, we just need to pretend there is no prefix
		else           return str.substr(0,pos) ;
	}
	struct Rt : RuleTgt {
		// cxtors & casts
		Rt() = default  ;
		Rt( Rule r , VarIdx ti ) : RuleTgt{r,ti} , pfx{_parse_pfx(target())} , sfx{_parse_sfx(target())} {}
		// data (cache)
		::string pfx ;
		::string sfx ;
	} ;

}
namespace std {
	template<> struct hash<Engine::Rt> {
		size_t operator()(Engine::Rt const& rt) const { return hash<Engine::RuleTgt>()(rt) ; } // there is no more info in a Rt than in a RuleTgt
	} ;
}
namespace Engine {

	template<bool IsSfx> static void _propagate_to_longer(::umap_s<uset<Rt>>& psfx_map) {
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
		sfxs_file.clear() ;
		pfxs_file.clear() ;
		// first compute a suffix map
		::umap_s<uset<Rt>> sfx_map ;
		for( Rule r : rule_lst() )
			for( VarIdx ti=0 ; ti<r->targets.size() ; ti++ ) {
				Rt rt{r,ti} ;
				if (!rt.tflags()[Tflag::Match]) continue ;
				sfx_map[rt.sfx].insert(rt) ;
			}
		_propagate_to_longer<true/*IsSfx*/>(sfx_map) ;                         // propagate to longer suffixes as a rule that matches a suffix also matches any longer suffix
		//
		// now, for each suffix, compute a prefix map
		for( auto const& [sfx,sfx_rule_tgts] : sfx_map ) {
			::umap_s<uset<Rt>> pfx_map ;
			if ( sfx.starts_with(StartMrkr) ) {
				::string_view sfx1 = ::string_view(sfx).substr(1) ;
				for( Rt const& rt : sfx_rule_tgts )
					if (sfx1.starts_with(rt.pfx)) pfx_map[""].insert(rt) ;
			} else {
				for( Rt const& rt : sfx_rule_tgts )
					pfx_map[rt.pfx].insert(rt) ;
				_propagate_to_longer<false/*IsSfx*/>(pfx_map) ;                // propagate to longer prefixes as a rule that matches a prefix also matches any longer prefix
			}
			//
			// store proper rule_tgts (ordered by decreasing prio, giving priority to AntiRule within each prio) for each prefix/suffix
			PsfxIdx pfx_root = pfxs_file.emplace_root() ;
			sfxs_file.insert_at(sfx) = pfx_root ;
			for( auto const& [pfx,pfx_rule_tgts] : pfx_map ) {
				vector<Rt> pfx_rule_tgt_vec = mk_vector(pfx_rule_tgts) ;
				::sort(
					pfx_rule_tgt_vec
				,	[]( Rt const& a , Rt const& b )->bool {
						// compulsery : order by priority, with special Rule's before plain Rule's, with Anti's before GenericSrc's within each priority level
						// optim      : put more specific rules before more generic ones to favor sharing RuleTgts in reversed PrefixFile
						// finally    : any stable sort is fine, just to avoid random order
						return
							::tuple( a->is_special() , a->prio , a->special , a.pfx.size()+a.sfx.size() , a->name )
						>	::tuple( b->is_special() , b->prio , b->special , b.pfx.size()+b.sfx.size() , b->name )
						;
					}
				) ;
				pfxs_file.insert_at(pfx_root,pfx) = RuleTgts(mk_vector<RuleTgt>(pfx_rule_tgt_vec)) ;
			}
		}
	}

	void EngineStore::_compile_srcs() {
		g_src_dirs_s.clear() ;
		for( Node n : Node::s_srcs(true/*dirs*/) ) {
			::string nn_s = n->name() ; nn_s += '/' ;
			g_src_dirs_s.push_back(nn_s) ;
		}
	}

	void EngineStore::_compile_rules() {
		_compile_rule_datas() ;
		RuleBase::s_by_name.clear() ;
		for( Rule r : rule_lst() ) RuleBase::s_by_name[r->name] = r ;
	}

	void EngineStore::_save_rules() {
		rule_str_file.clear() ;
		for( Rule r : rule_lst() ) rule_file.at(r) = rule_str_file.emplace(::string(rule_datas[+r])) ;
	}

	void EngineStore::invalidate_exec(bool cmd_ok) {
		Trace trace("invalidate_exec",STR(cmd_ok)) ;
		::vector<pair<bool,ExecGen>> keep_cmd_gens{rule_datas.size()} ; // indexed by Rule, if entry.first => entry.second is 0 (always reset exec_gen) or exec_gen w/ cmd_ok but !rsrcs_ok
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

	bool/*invalidate*/ EngineStore::s_new_rules( ::umap<Crc,RuleData>&& new_rules ) {
		Trace trace("_new_rules",new_rules.size()) ;
		//
		Rule              max_old_rule = 0 ;
		::umap <Crc,Rule> old_rules    ;
		for( Rule r : g_store.rule_lst() ) {
			if (r>max_old_rule) max_old_rule = r ;
			old_rules[r->match_crc] = r ;
		}
		//
		RuleIdx n_new_rules      = 0 ; for( auto& [match_crc,new_rd] : new_rules ) n_new_rules += !old_rules.contains(match_crc) ;
		RuleIdx n_modified_cmd   = 0 ;
		RuleIdx n_modified_rsrcs = 0 ;
		RuleIdx n_modified_prio  = 0 ;
		for( auto const& [crc,r] : old_rules )                                 // make old rules obsolete but do not pop to prevent idx reuse as long as old rules are not collected
			if (!new_rules.contains(crc)) g_store.rule_file.clear(r) ;
		if ( old_rules.size()+n_new_rules >= RuleIdx(-1) )                     // in case of size overflow, physically collect obsolete rules as we cannot fit old & new rules
			_s_collect_old_rules() ;
		::vector<pair<bool,ExecGen>> keep_cmd_gens{+max_old_rule+1u} ;         // indexed by Rule, if entry.first => entry.second is 0 (always reset exec_gen) or exec_gen w/ cmd_ok but !rsrcs_ok
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
		bool invalidate_match = n_modified_prio || n_new_rules || old_rules.size() ;
		trace("rules",'-',old_rules.size(),'+',n_new_rules,"=cmd",n_modified_cmd,"=rsrcs",n_modified_rsrcs,"=prio",n_modified_prio) ;
		//
		g_store._compile_rules() ;                                             // recompute derived info in memory
		if (invalidate_match) g_store._compile_psfxs() ;                                             // recompute derived info on disk
		_s_invalidate_exec(keep_cmd_gens) ;
		// trace
		Trace trace2 ;
		for( PsfxIdx sfx_idx : g_store.sfxs_file.lst() ) {
			::string sfx      = g_store.sfxs_file.str_key(sfx_idx) ;
			PsfxIdx  pfx_root = g_store.sfxs_file.at     (sfx_idx) ;
			bool single = !sfx.empty() && sfx[0]==StartMrkr ;
			for( PsfxIdx pfx_idx : g_store.pfxs_file.lst(pfx_root) ) {
				RuleTgts rts = g_store.pfxs_file.at(pfx_idx) ;
				::string pfx = g_store.pfxs_file.str_key(pfx_idx) ;
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
		return invalidate_match ;
	}

	bool/*invalidate*/ EngineStore::s_new_srcs( ::vector_s&& src_names , ::vector_s&& src_dir_names_s ) {
		::map<Node,bool/*dir*/> srcs         ;                                                            // use ordered map/set to ensure stable execution
		::map<Node,bool/*dir*/> old_srcs     ;                                                            // .
		::map<Node,bool/*dir*/> new_srcs     ;                                                            // .
		::set<Node            > src_dirs     ;                                                            // .
		::set<Node            > old_src_dirs ;                                                            // .
		::set<Node            > new_src_dirs ;                                                            // .
		Trace trace("_s_new_srcs") ;
		// format inputs
		for( bool dirs : {false,true} ) for( Node s  : Node::s_srcs(dirs) ) old_srcs.emplace(s,dirs) ;
		//
		for( ::string const& sn : src_names       )                   srcs.emplace( Node(sn                      ) , false/*dir*/ ) ;
		for( ::string      & sn : src_dir_names_s ) { sn.pop_back() ; srcs.emplace( Node(sn,!is_lcl(sn)/*no_dir*/) , true /*dir*/ ) ; } // external src dirs need no uphill dir
		//
		for( auto [n,d] : srcs     ) for( Node d=n->dir ; +d ; d = d->dir ) if (!src_dirs    .insert(d).second) break ; // non-local nodes have no dir
		for( auto [n,d] : old_srcs ) for( Node d=n->dir ; +d ; d = d->dir ) if (!old_src_dirs.insert(d).second) break ; // .
		// check
		for( auto [n,d] : srcs ) {
			if (!src_dirs.contains(n)) continue ;
			::string nn_s = n->name()+'/' ;
			for( ::string const& sn : src_names )
				if ( sn.starts_with(nn_s) ) throw to_string("source ",(d?"dir ":""),n->name()," is a dir of ",sn) ;
			FAIL(n->name(),"is a source dir of no source") ;
		}
		// compute diff
		for( auto nd : srcs ) {
			auto it = old_srcs.find(nd.first) ;
			if (it==old_srcs.end()) new_srcs.insert(nd) ;
			else                    old_srcs.erase (it) ;
		}
		//
		for( Node d : src_dirs ) { if (old_src_dirs.contains(d)) old_src_dirs.erase(d) ; else new_src_dirs.insert(d) ; }
		//
		trace("srcs",'-',old_srcs.size(),'+',new_srcs.size()) ;
		// commit
		for( bool add : {false,true} ) {
			::map<Node,bool/*dir*/> const& srcs = add ? new_srcs : old_srcs ;
			::vector<Node>                 ss   ; ss.reserve(srcs.size()) ;           // typically, there are very few src dirs
			::vector<Node>                 sds  ;                                     // .
			for( auto [n,d] : srcs ) if (d) sds.push_back(n) ; else ss.push_back(n) ;
			//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Node::s_srcs(false/*dirs*/,add,ss ) ;
			Node::s_srcs(true /*dirs*/,add,sds) ;
			//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		{	Trace trace2 ;
			for( auto [n,d] : old_srcs     ) { Node(n)->mk_no_src()                                ; trace2('-',d?"dir":"",n) ; }
			for( Node  d    : old_src_dirs )        d ->mk_no_src()                                ;
			for( auto [n,d] : new_srcs     ) { Node(n)->mk_src(d?Buildable::SrcDir:Buildable::Src) ; trace2('+',d?"dir":"",n) ; }
			for( Node  d    : new_src_dirs )        d ->mk_src(Buildable::Anti                   ) ;
		}
		g_store._compile_srcs() ;
		// user report
		{	OFStream srcs_stream{AdminDir+"/manifest"s} ;
			for( auto [n,d] : srcs ) srcs_stream << n->name() << (d?"/":"") <<'\n' ;
		}
		trace("done",srcs.size(),"srcs") ;
		return !old_srcs.empty() || !new_srcs.empty() ;
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
		::cerr << "collecting" ;
		::cerr << " nodes ..." ; for( Node     n   : g_store.node_lst          () ) n  ->mk_old()         ; // handle nodes first as jobs are necessary at this step
		::cerr << " jobs ..."  ; for( Job      j   : g_store.job_lst           () ) j  ->invalidate_old() ;
		::cerr << " rules ..." ; for( RuleTgts rts : g_store.rule_tgts_file.lst() ) rts. invalidate_old() ;
		/**/                     for( Rule     r   : g_store.rule_lst          () ) r  . invalidate_old() ; // now that old rules are not referenced any more, they can be suppressed and reused
		::cerr << endl ;
		Rule::s_match_gen = match_gen = 1 ;
	}
	void EngineStore::s_invalidate_match() {
		MatchGen& match_gen = g_store.rule_file.hdr() ;
		if (match_gen<NMatchGen) {
			// increase generation, which automatically makes all nodes !match_ok()
			match_gen++ ;
			Trace("s_invalidate_match","new gen",match_gen) ;
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
		::cerr << "collecting job cmds ..." ;
		for( Job j : g_store.job_lst() ) {
			if (j->rule.is_shared()) continue ;
			auto [yes,cmd_gen] = keep_cmd_gens[+j->rule] ;
			if (!yes) continue ;
			ExecGen old_exec_gen = j->exec_gen ;
			j->exec_gen = cmd_gen && j->exec_gen >= cmd_gen ;                  // set to 0 if bad cmd, to 1 if cmd ok but bad rsrcs
			trace2(j,j->rule,old_exec_gen,"->",j->exec_gen) ;
		}
		::cerr << endl ;
	}

}
