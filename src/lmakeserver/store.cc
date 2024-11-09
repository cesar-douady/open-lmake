// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <tuple>

#include "core.hh"    // must be first to include Python.h first
#include "rpc_job.hh"

using Backends::Backend ;

using namespace Disk ;

namespace Engine {

	SeqId     * g_seq_id     = nullptr ;
	Config    * g_config     = nullptr ;
	::vector_s* g_src_dirs_s = nullptr ;

}

namespace Engine::Persistent {

	//
	// RuleBase
	//

	MatchGen            RuleBase::s_match_gen    = 1            ; // 0 is forbidden as it is reserved to mean !match
	::umap_s<Rule>      RuleBase::s_by_name      ;
	size_t              RuleBase::s_name_sz      = Rule::NameSz ;
	bool                RuleBase::s_ping         = false        ; // use ping-pong to update _s_rule_datas atomically
	RuleIdx             RuleBase::s_n_rule_datas = 0            ;
	//
	::vector<RuleData>  RuleBase::_s_rule_data_vecs[2] ;
	::atomic<RuleData*> RuleBase::_s_rule_datas        = nullptr ;

	void RuleBase::_s_init_vec(bool ping) {
		::vector<RuleData>& vec = _s_rule_data_vecs[ping] ;
		SWEAR(!vec) ;
		for( Special s : iota(1,Special::NShared) ) { // Special::0 is not a special rule
			RuleData     rd  { s }           ;
			RuleCrcData& rcd = rd.crc.data() ;
			if (!rcd.rule) {
				rcd.rule  = +s               ;        // special is the id of shared rules
				rcd.state = RuleCrcState::Ok ;
			}
			vec.emplace_back(::move(rd)) ;
		}
	}

	void RuleBase::_s_save() {
		_rule_str_file.clear() ;
		_rule_file    .clear() ;
		for( Rule r : rule_lst() ) _rule_file.emplace_back(_rule_str_file.emplace(serialize(*r))) ;
	}

	void RuleBase::_s_update_crcs() {
		Trace trace("_s_update_crcs") ;
		::umap<Crc,Rule> rule_map ; for( Rule r : rule_lst() ) rule_map[r->crc->match] = r ;
		for( RuleCrc rc : rule_crc_lst() ) {
			RuleCrcData& rcd = rc.data()                ; if ( +rcd.rule && rcd.rule.is_shared() ) continue ; // shared rules are static and mapped in rule_map
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
			}
			trace(rc,*rc) ;
		}
	}

	void RuleBase::s_from_disk() {
		// handle Rule's
		s_n_rule_datas = +Special::NShared+_rule_file.size()-1 ;
		s_name_sz      = _rule_str_file.hdr()                  ; // hdr is only composed of name_sz
		s_by_name.clear() ;
		_s_rule_data_vecs[s_ping].reserve(s_n_rule_datas) ;
		//
		_s_init_vec(s_ping) ;
		for( Rule r=Special::NShared ; r<s_n_rule_datas ; r=+r+1 ) {
			RuleData rd = r.str() ;
			s_by_name[rd.name] = r ;
			_s_rule_data_vecs[s_ping].emplace_back(::move(rd)) ;
		}
		//
		_s_set_rule_datas(s_ping) ;
	}

	void RuleBase::s_from_vec_dynamic(::vector<RuleData>&& new_rules) {
		SWEAR(s_n_rule_datas==+Special::NShared+new_rules.size()) ;
		::umap<Crc,RuleData*> rule_map ;           for( RuleData& rd : new_rules ) rule_map.try_emplace(rd.crc->match,&rd) ;
		bool                  pong     = !s_ping ;
		//
		s_by_name.clear() ;
		s_name_sz = Rule::NameSz ;
		//
		_s_init_vec(pong) ;
		for( Rule r : rule_lst() ) {
			RuleData& rd = *rule_map.at(r->crc->match) ;
			SWEAR(rd.crc==r->crc) ;                                // check match, cmd and rsrcs are all ok as we should not be here if it is not the case
			s_by_name[rd.name] = r                               ;
			s_name_sz          = ::max(s_name_sz,rd.name.size()) ;
			_s_rule_data_vecs[pong].emplace_back(::move(rd)) ;
		}
		_rule_str_file.hdr() = s_name_sz ;
		fence() ;
		_s_set_rule_datas(pong) ;                                  // because update is dynamic, take care of atomicity
		fence() ;
		_s_rule_data_vecs[s_ping].clear() ;
		s_ping = pong ;
		//
		_s_save() ;
	}

	void RuleBase::s_from_vec_not_dynamic(::vector<RuleData>&& new_rules) {
		s_by_name.clear() ;
		s_name_sz = Rule::NameSz ;
		//
		_s_rule_data_vecs[s_ping].clear() ;
		_s_init_vec(s_ping) ;
		for( RuleData& rd : new_rules ) {
			s_by_name[rd.name] = _s_rule_data_vecs[s_ping].size() ;
			s_name_sz          = ::max(s_name_sz,rd.name.size())  ;
			_s_rule_data_vecs[s_ping].emplace_back(::move(rd)) ;
		}
		_rule_str_file.hdr() = s_name_sz ;
		//
		_s_set_rule_datas(s_ping) ;
		_s_save          (      ) ;
		_s_update_crcs   (      ) ;
	}

	//
	// RuleCrcBase
	//

	::umap<Crc,RuleCrc> RuleCrcBase::s_by_rsrcs ;

	//
	// NodeBase
	//

	RuleTgts NodeBase::s_rule_tgts(::string const& target_name) {
		// first match on suffix
		PsfxIdx sfx_idx = _sfxs_file.longest(target_name,::string{Persistent::StartMrkr}).first ; // StartMrkr is to match rules w/ no stems
		if (!sfx_idx) return RuleTgts{} ;
		PsfxIdx pfx_root = _sfxs_file.c_at(sfx_idx) ;
		// then match on prefix
		PsfxIdx pfx_idx = _pfxs_file.longest(pfx_root,target_name).first ;
		if (!pfx_idx) return RuleTgts{} ;
		return _pfxs_file.c_at(pfx_idx) ;
	}

	//
	// Persistent
	//

	// on disk
	JobFile      _job_file       ; // jobs
	DepsFile     _deps_file      ; // .
	TargetsFile  _targets_file   ; // .
	NodeFile     _node_file      ; // nodes
	JobTgtsFile  _job_tgts_file  ; // .
	RuleFile     _rule_file      ; // rules
	RuleCrcFile  _rule_crc_file  ; // .
	RuleStrFile  _rule_str_file  ; // .
	RuleTgtsFile _rule_tgts_file ; // .
	SfxFile      _sfxs_file      ; // .
	PfxFile      _pfxs_file      ; // .
	NameFile     _name_file      ; // commons
	// in memory
	::uset<Job >       _frozen_jobs  ;
	::uset<Node>       _frozen_nodes ;
	::uset<Node>       _no_triggers  ;

	static void _compile_srcs() {
		if (g_src_dirs_s) g_src_dirs_s->clear() ;
		else              g_src_dirs_s = new ::vector_s ;
		for( Node const n : Node::s_srcs(true/*dirs*/) ) g_src_dirs_s->push_back(n->name()+'/') ;
	}

	static void _init_config() {
		try         { g_config = new Config{deserialize<Config>(AcFd(PrivateAdminDirS+"config_store"s).read())} ; }
		catch (...) { g_config = new Config                                                                     ; }
	}

	static void _init_srcs_rules(bool rescue) {
		Trace trace("_init_srcs_rules",Pdate(New)) ;
		//
		// START_OF_VERSIONING
		::string dir_s = g_config->local_admin_dir_s+"store/" ;
		//
		mk_dir_s(dir_s) ;
		// jobs
		_job_file      .init( dir_s+"job"       , g_writable ) ;
		_deps_file     .init( dir_s+"deps"      , g_writable ) ;
		_targets_file  .init( dir_s+"_targets"  , g_writable ) ;
		// nodes
		_node_file     .init( dir_s+"node"      , g_writable ) ;
		_job_tgts_file .init( dir_s+"job_tgts"  , g_writable ) ;
		// rules
		_rule_file     .init( dir_s+"rule"      , g_writable ) ; if ( g_writable && !_rule_file.c_hdr() ) _rule_file.hdr() = 1 ; // 0 is reserved to mean no match
		_rule_crc_file .init( dir_s+"rule_crc"  , g_writable ) ;
		_rule_str_file .init( dir_s+"rule_str"  , g_writable ) ;
		_rule_tgts_file.init( dir_s+"rule_tgts" , g_writable ) ;
		_sfxs_file     .init( dir_s+"sfxs"      , g_writable ) ;
		_pfxs_file     .init( dir_s+"pfxs"      , g_writable ) ;
		// commons
		_name_file     .init( dir_s+"name"      , g_writable ) ;
		// misc
		if (g_writable) {
			g_seq_id = &_job_file.hdr().seq_id ;
			if (!*g_seq_id) *g_seq_id = 1 ;      // avoid 0 (when store is brand new) to decrease possible confusion
		}
		// Rule
		RuleBase::s_match_gen = _rule_file.c_hdr() ;
		// END_OF_VERSIONING
		//
		SWEAR(RuleBase::s_match_gen>0) ;
		_job_file      .keep_open = true ;       // files may be needed post destruction as there may be alive threads as we do not masterize destruction order
		_deps_file     .keep_open = true ;       // .
		_targets_file  .keep_open = true ;       // .
		_node_file     .keep_open = true ;       // .
		_job_tgts_file .keep_open = true ;       // .
		_rule_file     .keep_open = true ;       // .
		_rule_crc_file .keep_open = true ;       // .
		_rule_str_file .keep_open = true ;       // .
		_rule_tgts_file.keep_open = true ;       // .
		_sfxs_file     .keep_open = true ;       // .
		_pfxs_file     .keep_open = true ;       // .
		_name_file     .keep_open = true ;       // .
		_compile_srcs () ;
		Rule::s_from_disk() ;
		for( Job  j : _job_file .c_hdr().frozens    ) _frozen_jobs .insert(j) ;
		for( Node n : _node_file.c_hdr().frozens    ) _frozen_nodes.insert(n) ;
		for( Node n : _node_file.c_hdr().no_triggers) _no_triggers .insert(n) ;
		//
		trace("done",Pdate(New)) ;
		//
		if (rescue) {
			Fd::Stderr.write("previous crash detected, checking & rescueing\n") ;
			try {
				chk()              ;             // first verify we have a coherent store
				invalidate_match() ;             // then rely only on essential data that should be crash-safe
				Fd::Stderr.write("seems ok\n") ;
			} catch (::string const&) {
				exit(Rc::Format,"failed to rescue, consider running lrepair") ;
			}
		}
	}

	void chk() {
		// files
		/**/                                  _job_file      .chk(                    ) ; // jobs
		/**/                                  _deps_file     .chk(                    ) ; // .
		/**/                                  _targets_file  .chk(                    ) ; // .
		/**/                                  _node_file     .chk(                    ) ; // nodes
		/**/                                  _job_tgts_file .chk(                    ) ; // .
		/**/                                  _rule_file     .chk(                    ) ; // rules
		/**/                                  _rule_crc_file .chk(                    ) ; // .
		/**/                                  _rule_str_file .chk(                    ) ; // .
		/**/                                  _rule_tgts_file.chk(                    ) ; // .
		/**/                                  _sfxs_file     .chk(                    ) ; // .
		for( PsfxIdx idx : _sfxs_file.lst() ) _pfxs_file     .chk(_sfxs_file.c_at(idx)) ; // .
		/**/                                  _name_file     .chk(                    ) ; // commons
	}

	static void _save_config() {
		AcFd( PrivateAdminDirS+"config_store"s , Fd::Write ).write(serialize(*g_config)  ) ;
		AcFd( AdminDirS+"config"s              , Fd::Write ).write(g_config->pretty_str()) ;
	}

	static void _diff_config( Config const& old_config , bool dynamic ) {
		Trace trace("_diff_config",old_config) ;
		for( BackendTag t : iota(All<BackendTag>) ) {
			if (g_config->backends[+t].ifce==old_config.backends[+t].ifce) continue ;
			throw_if( dynamic , "cannot change server address while running" ) ;
			break ;
		}
		//
		if (g_config->path_max!=old_config.path_max) invalidate_match() ; // we may discover new buildable nodes or vice versa
	}

	void new_config( Config&& config , bool dynamic , bool rescue , ::function<void(Config const& old,Config const& new_)> diff ) {
		Trace trace("new_config",Pdate(New),STR(dynamic),STR(rescue) ) ;
		if ( !dynamic                                                ) mk_dir_s( AdminDirS+"outputs/"s , true/*unlnk_ok*/ ) ;
		if ( !dynamic                                                ) _init_config() ;
		else                                                           SWEAR(g_config->booted,*g_config) ; // we must update something
		if (                                        g_config->booted ) config.key = g_config->key ;
		//
		/**/                                                           diff(*g_config,config) ;
		//
		/**/                                                          ConfigDiff d = config.booted ? g_config->diff(config) : ConfigDiff::None ;
		if (              d>ConfigDiff::Static  &&  g_config->booted ) throw "repo must be clean"s  ;
		if (  dynamic &&  d>ConfigDiff::Dynamic                      ) throw "repo must be steady"s ;
		//
		if (  dynamic && !d                                          ) return ;                            // fast path, nothing to update
		//
		/**/                                                           Config old_config = *g_config ;
		if (             +d                                          ) *g_config = ::move(config) ;
		if (                                       !g_config->booted ) throw "no config available"s ;
		/**/                                                           g_config->open(dynamic)          ;
		if (             +d                                          ) _save_config()                   ;
		if ( !dynamic                                                ) _init_srcs_rules(rescue)         ;
		if (             +d                                          ) _diff_config(old_config,dynamic) ;
		trace("done",Pdate(New)) ;
	}

	RepairDigest repair(::string const& from_dir_s) {
		Trace trace("repair",from_dir_s) ;
		RepairDigest     res      ;
		::umap<Crc,Rule> rule_tab ; for( Rule r : rule_lst() ) rule_tab[r->crc->cmd] = r ; SWEAR(rule_tab.size()==rule_lst().size()) ;
		for( ::string const& jd : walk(no_slash(from_dir_s),no_slash(from_dir_s)) ) {
			{	JobInfo job_info { jd } ;
				// qualify report
				if (job_info.start.pre_start.proc!=JobRpcProc::Start) { trace("no_pre_start",jd) ; goto NextJob ; }
				if (job_info.start.start    .proc!=JobRpcProc::Start) { trace("no_start"    ,jd) ; goto NextJob ; }
				if (job_info.end  .end      .proc!=JobRpcProc::End  ) { trace("no_end"      ,jd) ; goto NextJob ; }
				if (job_info.end  .end.digest.status!=Status::Ok    ) { trace("not_ok"      ,jd) ; goto NextJob ; }         // repairing jobs in error is useless
				// find rule
				auto it = rule_tab.find(job_info.start.rule_cmd_crc) ;
				if (it==rule_tab.end()) { trace("no_rule",jd) ; goto NextJob ; }                                            // no rule
				Rule rule = it->second ;
				// find targets
				::vector<Target> targets ; targets.reserve(job_info.end.end.digest.targets.size()) ;
				for( auto const& [tn,td] : job_info.end.end.digest.targets ) {
					if ( td.crc==Crc::None && !static_phony(td.tflags) )                                   continue     ;   // this is not a target
					if ( !td.crc.valid()                               ) { trace("invalid_target",jd,tn) ; goto NextJob ; } // XXX : handle this case
					if ( td.sig!=FileSig(tn)                           ) { trace("disk_mismatch" ,jd,tn) ; goto NextJob ; } // if dates do not match, we will rerun the job anyway
					//
					Node t{tn} ;
					t->refresh( td.crc , {td.sig,{}} ) ;                                                                    // if file does not exist, the Epoch as a date is fine
					targets.emplace_back( t , td.tflags ) ;
				}
				::sort(targets) ;                                                                              // ease search in targets
				// find deps
				::vector_s    src_dirs ; for( Node s : Node::s_srcs(true/*dirs*/) ) src_dirs.push_back(s->name()) ;
				::vector<Dep> deps     ; deps.reserve(job_info.end.end.digest.deps.size()) ;
				for( auto const& [dn,dd] : job_info.end.end.digest.deps ) {
					if ( !is_canon(dn)) goto NextJob ;                                                         // this should never happen, there is a problem with this job
					if (!is_lcl(dn)) {
						for( ::string const& sd : src_dirs ) if (dn.starts_with(sd)) goto KeepDep ;            // this could be optimized by searching the longest match in the name prefix tree
						goto NextJob ;                                                                         // this should never happen as src_dirs are part of cmd definition
					KeepDep : ;
					}
					Dep dep { Node(dn) , dd } ;
					if ( !dep.is_crc                         ) { trace("no_dep_crc" ,jd,dn) ; goto NextJob ; } // dep could not be identified when job ran, hum, better not to repair that
					if ( +dep.accesses && !dep.crc().valid() ) { trace("invalid_dep",jd,dn) ; goto NextJob ; } // no valid crc, no interest to repair as job will rerun anyway
					deps.emplace_back(dep) ;
				}
				// set job
				Job job { {rule,::move(job_info.start.stems)} } ;
				if (!job) goto NextJob ;
				job->targets.assign(targets) ;
				job->deps   .assign(deps   ) ;
				job->status = job_info.end.end.digest.status ;
				job->set_exec_ok() ;                                                                           // pretend job just ran
				// set target actual_job's
				for( Target t : targets ) {
					t->actual_job   () = job      ;
					t->actual_tflags() = t.tflags ;
				}
				// restore job_data
				job.record(job_info) ;
				trace("restored",jd,job->name()) ;
			}
			res.n_repaired++ ;
		NextJob : ;
			res.n_processed++ ;
		}
		return res ;
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
		if (pos==0) return StartMrkr+str   ; // signal that there is no stem by prefixing with StartMrkr
		else        return str.substr(pos) ; // suppress stem marker & stem idx
	}

	// return prefix before first stem (empty if no stem)
	static ::string _parse_pfx(::string const& str) {
		size_t pos = str.find(Rule::StemMrkr) ;
		if (pos==Npos) return {}                ; // absence of stem is already signal in _parse_sfx, we just need to pretend there is no prefix
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

	template<bool IsSfx> static void _propag_to_longer(::umap_s<uset<Rt>>& psfx_map) {
		::vector_s psfxs = ::mk_key_vector(psfx_map) ;
		::sort( psfxs , [](::string const& a,::string const& b){ return a.size()<b.size() ; } ) ;
		for( ::string const& long_psfx : psfxs ) {
			for( size_t l=1 ; l<=long_psfx.size() ; l++ ) {
				::string short_psfx = long_psfx.substr( IsSfx?l:0 , long_psfx.size()-l ) ;
				if (psfx_map.contains(short_psfx)) {
					psfx_map.at(long_psfx).merge(::copy(psfx_map.at(short_psfx))) ; // copy arg as merge clobbers it
					break ;                                                         // psfx's are sorted shortest first, so as soon as a short one is found, it is already merged with previous ones
				}
			}
		}
	}

	// make a prefix/suffix map that records which rule has which prefix/suffix
	static void _compile_psfxs() {
		_sfxs_file.clear() ;
		_pfxs_file.clear() ;
		// first compute a suffix map
		::umap_s<uset<Rt>> sfx_map ;
		for( Rule r : rule_lst() )
			for( VarIdx ti : iota<VarIdx>(r->matches.size()) ) {
				if ( r->matches[ti].second.flags.is_target!=Yes         ) continue ;
				if (!r->matches[ti].second.flags.tflags()[Tflag::Target]) continue ;
				Rt rt { r->crc , ti } ;
				sfx_map[rt.sfx].insert(rt) ;
			}
		_propag_to_longer<true/*IsSfx*/>(sfx_map) ;          // propagate to longer suffixes as a rule that matches a suffix also matches any longer suffix
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
				_propag_to_longer<false/*IsSfx*/>(pfx_map) ; // propagate to longer prefixes as a rule that matches a prefix also matches any longer prefix
			}
			//
			// store proper rule_tgts (ordered by decreasing prio, giving priority to AntiRule within each prio) for each prefix/suffix
			PsfxIdx pfx_root = _pfxs_file.emplace_root() ;
			_sfxs_file.insert_at(sfx) = pfx_root ;
			for( auto const& [pfx,pfx_rule_tgts] : pfx_map ) {
				vector<Rt> pfx_rule_tgt_vec = mk_vector(pfx_rule_tgts) ;
				::sort(
					pfx_rule_tgt_vec
				,	[]( Rt const& a , Rt const& b )->bool {
						// compulsery : order by priority, with special Rule's before plain Rule's, with Anti's before GenericSrc's within each priority level
						// optim      : put more specific rules before more generic ones to favor sharing RuleTgts in reversed PrefixFile
						// finally    : any stable sort is fine, just to avoid random order
						return
							::tuple( a->rule->is_special() , a->rule->prio , a->rule->special , a.pfx.size()+a.sfx.size() , a->rule->name )
						>	::tuple( b->rule->is_special() , b->rule->prio , b->rule->special , b.pfx.size()+b.sfx.size() , b->rule->name )
						;
					}
				) ;
				_pfxs_file.insert_at(pfx_root,pfx) = RuleTgts(mk_vector<RuleTgt>(pfx_rule_tgt_vec)) ;
			}
		}
	}

	bool/*invalidate*/ new_rules( ::vector<RuleData>&& new_rules_ , bool dynamic ) {
		Trace trace("new_rules",new_rules_.size()) ;
		// check number of rules before doing any action
		//                                      vv must fit in rule file vv   vvvvvvvv idx must fit within type vvvvvvvv
		static constexpr size_t NRules = ::min( (size_t(1)<<NRuleIdxBits)-1 , (size_t(1)<<NBits<Rule>)-+Special::NShared ) ;
		throw_unless( new_rules_.size()<=NRules , "too many rules (",new_rules_.size(),"), max is ",NRules ) ;
		//
		::umap<Crc,RuleData const*> old_rds ; for( Rule      r  : rule_lst() ) old_rds.try_emplace(r->crc->match,&*r) ;
		::umap<Crc,RuleData      *> new_rds ; for( RuleData& rd : new_rules_ ) new_rds.try_emplace(rd.crc->match,&rd) ;
		//
		RuleIdx n_old_rules      = old_rds.size() ;
		RuleIdx n_new_rules      = 0              ;
		RuleIdx n_modified_prio  = 0              ;
		RuleIdx n_modified_cmd   = 0              ;
		RuleIdx n_modified_rsrcs = 0              ;
		// evaluate diff
		for( auto& [match_crc,new_rd] : new_rds ) {
			auto it = old_rds.find(match_crc) ;
			if (it==old_rds.end()) {
				n_new_rules++ ;
			} else {
				n_old_rules-- ;
				RuleData const& old_rd = *it->second ;
				n_modified_prio  += new_rd->prio      !=old_rd.prio       ;
				n_modified_cmd   += new_rd->crc->cmd  !=old_rd.crc->cmd   ;
				n_modified_rsrcs += new_rd->crc->rsrcs!=old_rd.crc->rsrcs ;
				//
				new_rd->cost_per_token = old_rd.cost_per_token ;
				new_rd->exec_time      = old_rd.exec_time      ;
				new_rd->stats_weight   = old_rd.stats_weight   ;
			}
		}
		bool res = n_modified_prio || n_new_rules || n_old_rules ;
		if (dynamic) {                                                      // check if compatible with dynamic update
			throw_if( n_new_rules      , "new rules appeared"           ) ;
			throw_if( n_old_rules      , "old rules disappeared"        ) ;
			throw_if( n_modified_prio  , "rule prio's were modified"    ) ;
			throw_if( n_modified_cmd   , "rule cmd's were modified"     ) ;
			throw_if( n_modified_rsrcs , "rule resources were modified" ) ;
			RuleBase::s_from_vec_dynamic(::move(new_rules_)) ;
		} else {
			RuleBase::s_from_vec_not_dynamic(::move(new_rules_)) ;
			if (res) _compile_psfxs() ;                                     // recompute matching
		}
		trace(STR(n_new_rules),STR(n_old_rules),STR(n_modified_prio),STR(n_modified_cmd),STR(n_modified_rsrcs)) ;
		// trace
		Trace trace2 ;
		for( PsfxIdx sfx_idx : _sfxs_file.lst() ) {
			::string sfx      = _sfxs_file.str_key(sfx_idx) ;
			PsfxIdx  pfx_root = _sfxs_file.at     (sfx_idx) ;
			bool     single   = +sfx && sfx[0]==StartMrkr  ;
			for( PsfxIdx pfx_idx : _pfxs_file.lst(pfx_root) ) {
				RuleTgts rts = _pfxs_file.at(pfx_idx) ;
				::string pfx = _pfxs_file.str_key(pfx_idx) ;
				if (single) { SWEAR(!pfx,pfx) ; trace2(         sfx.substr(1) , ':' ) ; }
				else        {                   trace2( pfx+'*'+sfx           , ':' ) ; }
				Trace trace3 ;
				for( RuleTgt rt : rts.view() ) trace3( rt->rule , ':' , rt->rule->prio , rt->rule->name , rt.key() ) ;
			}
		}
		// user report
		{	::vector<Rule> rules ; for( Rule r : rule_lst() ) rules.push_back(r) ;
			::sort( rules , [](Rule a,Rule b){
				if (a->prio!=b->prio) return a->prio > b->prio ;
				else                  return a->name < b->name ;
			} ) ;
			First    first   ;
			::string content ;
			for( Rule rule : rules ) if (rule->user_defined())
				content <<first("","\n")<< rule->pretty_str() ;
			AcFd( AdminDirS+"rules"s , Fd::Write ).write(content) ;
		}
		return res ;
	}

	bool/*invalidate*/ new_srcs( ::pair<::vmap_s<FileTag>/*files*/,::vector_s/*dirs_s*/>&& src_names , bool dynamic ) {
		::map<Node,FileTag    > srcs         ;                                                                                                  // use ordered map/set to ensure stable execution
		::map<Node,FileTag    > old_srcs     ;                                                                                                  // .
		::map<Node,FileTag    > new_srcs_    ;                                                                                                  // .
		::set<Node            > src_dirs     ;                                                                                                  // .
		::set<Node            > old_src_dirs ;                                                                                                  // .
		::set<Node            > new_src_dirs ;                                                                                                  // .
		Trace trace("new_srcs") ;
		//
		size_t          root_dir_depth      = 0       ; { for( char c : *g_root_dir_s ) root_dir_depth += c=='/' ; } root_dir_depth-- ;         // there is one more / than the actual depth
		size_t          src_dirs_uphill_lvl = 0       ;
		::string const* highest             = nullptr ;
		for( ::string const& d_s : src_names.second ) {
			if (!is_abs_s(d_s))
				if ( size_t ul=uphill_lvl_s(d_s) ; ul>src_dirs_uphill_lvl ) {
					src_dirs_uphill_lvl = ul   ;
					highest             = &d_s ;
				}
		}
		if (root_dir_depth<=src_dirs_uphill_lvl) {
			SWEAR(highest) ;
			throw "cannot access relative source dir "+no_slash(*highest)+" from repository "+no_slash(*g_root_dir_s) ;
		}
		// format inputs
		for( bool dirs : {false,true} ) for( Node s : Node::s_srcs(dirs) ) old_srcs.emplace(s,dirs?FileTag::Dir:FileTag::None) ;                // dont care whether we delete a regular file or a link
		//
		for( auto const& [sn,t] : src_names.first  )                   srcs.emplace( Node(sn                      ) , t                   ) ;
		for( ::string&    sn    : src_names.second ) { sn.pop_back() ; srcs.emplace( Node(sn,!is_lcl(sn)/*no_dir*/) , FileTag::Dir/*dir*/ ) ; } // external src dirs need no uphill dir
		//
		for( auto [n,_] : srcs     ) for( Node d=n->dir() ; +d ; d = d->dir() ) if (!src_dirs    .insert(d).second) break ;                     // non-local nodes have no dir
		for( auto [n,_] : old_srcs ) for( Node d=n->dir() ; +d ; d = d->dir() ) if (!old_src_dirs.insert(d).second) break ;                     // .
		// check
		for( auto [n,t] : srcs ) {
			if (!src_dirs.contains(n)) continue ;
			::string nn   = n->name() ;
			::string nn_s = nn+'/'    ;
			for( auto const& [sn,_] : src_names.first )
				throw_if( sn.starts_with(nn_s) , "source ",t==FileTag::Dir?"dir ":"",nn," is a dir of ",sn ) ;
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
			for( auto [n,t] : new_srcs_ ) if (t==FileTag::Dir) throw "new source dir "+n->name()+' '+git_clean_msg() ;
			for( auto [n,t] : old_srcs  ) if (t==FileTag::Dir) throw "old source dir "+n->name()+' '+git_clean_msg() ;
		}
		//
		for( Node d : src_dirs ) { if (old_src_dirs.contains(d)) old_src_dirs.erase(d) ; else new_src_dirs.insert(d) ; }
		//
		if ( !old_srcs && !new_srcs_ ) return false ;
		if (dynamic) {
			if (+new_srcs_) throw "new source "    +new_srcs_.begin()->first->name() ;                                                          // XXX : accept new sources if unknown
			if (+old_srcs ) throw "removed source "+old_srcs .begin()->first->name() ;                                                          // XXX : accept old sources if unknown
			FAIL() ;
		}
		//
		trace("srcs",'-',old_srcs.size(),'+',new_srcs_.size()) ;
		// commit
		for( bool add : {false,true} ) {
			::map<Node,FileTag> const& srcs = add ? new_srcs_ : old_srcs ;
			::vector<Node>             ss   ; ss.reserve(srcs.size()) ;                                                                         // typically, there are very few src dirs
			::vector<Node>             sds  ;                                                                                                   // .
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

	void invalidate_match() {
		MatchGen& match_gen = _rule_file.hdr() ;
		Trace trace("invalidate_match","old gen",match_gen) ;
		match_gen++ ;                                                                                                         // increase generation, which automatically makes all nodes !match_ok()
		if (match_gen==0) {                                                                                                   // unless we wrapped around
			trace("reset") ;
			Fd::Stderr.write("collecting nodes ...") ; for( Node n : node_lst() ) n->mk_old() ; Fd::Stderr.write(" done\n") ; // physically reset node match_gen's
			match_gen = 1 ;
		}
		Rule::s_match_gen = match_gen ;
	}

}
