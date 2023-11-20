// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "time.hh"

#include "core.hh"

namespace Engine {
	using namespace Disk ;
	using namespace Time ;

	//
	// NodeReqInfo
	//

	::ostream& operator<<( ::ostream& os , NodeReqInfo const& ri ) {
		/**/                          os << "NRI(" << ri.req <<','<< ri.action <<',' ;
		if (ri.prio_idx==Node::NoIdx) os << "None"                                   ;
		else                          os << ri.prio_idx                              ;
		if (ri.done                 ) os << ",done"                                  ;
		if (ri.n_wait               ) os << ".wait:"<<ri.n_wait                      ;
		if (ri.err>NodeErr::None    ) os << ',' << ri.err                            ;
		return                        os << ')'                                      ;
	}

	//
	// Node
	//

	::ostream& operator<<( ::ostream& os , Node const n ) {
		/**/    os << "N(" ;
		if (+n) os << +n   ;
		return  os << ')'  ;
	}

	//
	// NodeData
	//

	::ostream& operator<<( ::ostream& os , NodeData const& nd ) {
		/**/                             os << '(' << nd.crc           ;
		/**/                             os << ',' << nd.date          ;
		/**/                             os << ','                     ;
		if (!nd.match_ok()             ) os << '~'                     ;
		/**/                             os << "job:"                  ;
		/**/                             os << +Job(nd.actual_job_tgt) ;
		if (nd.actual_job_tgt.is_sure()) os << '+'                     ;
		return                           os << ")"                     ;
	}

	void NodeData::_set_pressure_raw( ReqInfo& ri ) const {
		for( Job job : conform_job_tgts(ri) ) job->set_pressure(job->req_info(ri.req),ri.pressure) ; // go through current analysis level as this is where we may have deps we are waiting for
	}

	void NodeData::set_special( Special special , ::vector<Node> const& deps , Accesses a , Dflags df , bool p ) {
		Trace trace("set_special",*this,special,deps) ;
		JobTgts jts       = job_tgts ;
		Bool3   buildable = Yes      ;
		if ( !jts.empty() && jts.back()->rule->is_special() ) SWEAR( jts.back()->rule->special==special , jts.back()->rule->special , special ) ;
		else                                                  job_tgts.append(::vector<JobTgt>({{ Job(special,idx(),Deps(deps,a,df,p)) , true/*is_sure*/ }})) ;
		for( Dep const& d : job_tgts.back()->deps ) {
			if (d->buildable==Bool3::Unknown) buildable &= Maybe        ; // if not computed yet, well note we do not know
			else                              buildable &= d->buildable ; // could break as soon as !Yes is seen, but this way, we can have a more agressive swear
		}
		SWEAR(buildable!=No) ;
		if (buildable==Yes) rule_tgts.clear() ;
		_set_buildable(buildable) ;
	}

	::vector_view_c<JobTgt> NodeData::prio_job_tgts(RuleIdx prio_idx) const {
		JobTgts const& jts = job_tgts ;                                       // /!\ jts is a CrunchVector, so if single element, a subvec would point to it, so it *must* be a ref
		if (prio_idx<jts.size()) {
			RuleIdx                 sz   = 0                    ;
			::vector_view_c<JobTgt> sjts = jts.subvec(prio_idx) ;
			Prio                    prio = -Infinity            ;
			for( JobTgt jt : sjts ) {
				Prio new_prio = jt->rule->prio ;
				if (new_prio<prio) break ;
				prio = new_prio ;
				sz++ ;
			}
			return sjts.subvec(0,sz) ;
		} else {
			SWEAR( prio_idx==jts.size() || prio_idx==NoIdx , prio_idx , jts.size() ) ;
		}
		return {} ;
	}

	struct JobTgtIter {
		// cxtors
		JobTgtIter( NodeData& n , RuleIdx i=0 ) : node{n} , idx{i} {}
		// services
		JobTgtIter&   operator++(int)       { _prev_prio = _cur_prio() ; idx++ ; return *this ;            }
		JobTgt        operator* (   ) const { return node.job_tgts[idx] ;                                  }
		JobTgt const* operator->(   ) const { return node.job_tgts.begin()+idx ;                           }
		JobTgt      * operator->(   )       { return node.job_tgts.begin()+idx ;                           }
		operator bool           (   ) const { return idx<node.job_tgts.size() && _cur_prio()>=_prev_prio ; }
		void reset(RuleIdx i=0) {
			idx        = i         ;
			_prev_prio = -Infinity ;
		}
	private :
		Prio _cur_prio() const { return (**this)->rule->prio ; }
		// data
	public :
		NodeData& node ;
		RuleIdx   idx  ;
	private :
		Prio _prev_prio = -Infinity ;
	} ;

	// instantiate rule_tgts into job_tgts by taking the first iso-prio chunk and return how many rule_tgts were consumed
	// - anti-rules always precede regular rules at given prio and are deemed to be of higher prio (and thus in different iso-prio chunks)
	// - if a sure job is found, then all rule_tgts are consumed as there will be no further match
	::pair<Bool3/*buildable*/,RuleIdx/*shorten_by*/> NodeData::_gather_prio_job_tgts( ::vector<RuleTgt> const& rule_tgts , Req req , DepDepth lvl ) {
		//
		if (rule_tgts.empty()) return {No,NoIdx} ;                             // fast path : avoid computing name()
		//
		::string name_     = name()        ;
		Prio     prio      = -Infinity     ;                                   // initially, we are ready to accept any rule
		RuleIdx  n         = 0             ;
		bool     clear     = false         ;
		Bool3    buildable = No            ;                                   // return if we found a job candidate
		bool     is_lcl_   = is_lcl(name_) ;
		//
		::vector<JobTgt> jts ; jts.reserve(rule_tgts.size()) ;                 // typically, there is a single priority
		for( RuleTgt const& rt : rule_tgts ) {
			if (rt->prio<prio) goto Done ;
			n++ ;
			if ( !is_lcl_ && !rt->allow_ext ) continue ;
			if (rt->is_anti()) {
				if (+Rule::FullMatch(rt,name_)) { SWEAR(jts.empty(),jts) ; clear = true ; goto Return ; }
				else                            {                                         continue    ; }
			}
			//          vvvvvvvvvvvvvvvvvvvvvvvvvv
			JobTgt jt = JobTgt(rt,name_,req,lvl+1) ;
			//          ^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (!jt) continue ;
			if (jt.sure()) { buildable |= Yes   ; clear = true ; }
			else           { buildable |= Maybe ;                }
			jts.push_back(jt) ;
			prio = rt->prio ;
		}
		clear = true ;
	Done :
		//                vvvvvvvvvvvvvvvvvvvv
		if (!jts.empty()) job_tgts.append(jts) ;
		//                ^^^^^^^^^^^^^^^^^^^^
	Return :
		if (clear) return {buildable,NoIdx} ;
		else       return {buildable,n    } ;
	}

	void NodeData::_set_buildable_raw( Req req , DepDepth lvl ) {
		Trace trace("set_buildable",*this,lvl) ;
		if (lvl>=g_config.max_dep_depth) throw ::vector<Node>({idx()}) ; // infinite dep path
		::vector<RuleTgt> rule_tgts_ = raw_rule_tgts() ;
		rule_tgts.clear() ;
		job_tgts .clear() ;
		conform_idx = NoIdx ;
		uphill      = false ;
		//                                                       vvvvvvvvvvvvvvvvvv
		if ( g_config.path_max && name_sz()>g_config.path_max) { _set_buildable(No) ; goto Return ; } // path is ridiculously long, make it unbuildable
		//                                                       ^^^^^^^^^^^^^^^^^^
		// during analysis, temporarily set buildable to break loops that will be caught at exec time
		// in case of crash, rescue mode is used and ensures all matches are recomputed
		_set_buildable(Yes) ;
		try {
			if (!external) {
				Node dir_ = dir() ;
				if (+dir_) {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvv
					dir_->set_buildable(req,lvl+1) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (dir_->buildable!=No) {
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						set_special( Special::Uphill , ::vector<Node>({dir_}) , Access::Lnk ) ;
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						if (dir_->buildable==Yes) goto Return   ;
						else                      goto AllRules ;
					}
				}
			}
			//                            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			auto [buildable,shorten_by] = _gather_prio_job_tgts(rule_tgts_,req,lvl) ;
			//vvvvvvvvvvvvvvvvvvvvvvv     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			_set_buildable(buildable) ;
			//^^^^^^^^^^^^^^^^^^^^^^^
			//
			if (shorten_by) {
				//                     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (shorten_by!=NoIdx) rule_tgts = ::vector_view_c<RuleTgt>(rule_tgts_,shorten_by) ;
				//                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				goto Return ;
			}
		} catch (::vector<Node>& e) {
			//vvvvvvvvvvvvvv
			_set_buildable() ;                                                 // restore Unknown as we do not want to appear as having been analyzed
			//^^^^^^^^^^^^^^
			e.push_back(idx()) ;
			throw ;
		}
	AllRules :
		//              vvvvvvvvvvvvvvvvvvvvvv
		if (!rule_tgts) rule_tgts = rule_tgts_ ;
		//              ^^^^^^^^^^^^^^^^^^^^^^
	Return :
		SWEAR(match_ok()) ;
		trace("summary",buildable) ;
		return ;
	}

	NodeData::ReqInfo const& NodeData::_make_raw( ReqInfo const& cri , RunAction run_action , MakeAction make_action ) {
		bool    multi_         = false   ;
		RuleIdx prod_idx       = NoIdx   ;
		Req     req            = cri.req ;
		Bool3   clean          = Maybe   ;                                     // lazy evaluate manual()==No
		Job     regenerate_job ;
		Trace trace("Nmake",idx(),cri,run_action,make_action) ;
		SWEAR(run_action<=RunAction::Dsk) ;
		//                                vvvvvvvvvvvvvvvvvv
		try                             { set_buildable(req) ;                                           }
		//                                ^^^^^^^^^^^^^^^^^^
		catch (::vector<Node> const& e) { set_special(Special::Infinite,e,{}/*accesses*/,{}/*dflags*/) ; }
		if (buildable==No) {                                                                               // avoid allocating ReqInfo for non-buildable Node's
			SWEAR( make_action<MakeAction::Dec , make_action ) ;
			SWEAR( !cri.has_watchers()                       ) ;
			trace("not_buildable",cri) ;
			// if file has been removed, everything is ok again : file is not buildable and does not exist
			if ( crc!=Crc::None && manual()==Maybe ) refresh( Crc::None , Ddate::s_now() ) ;
			return cri ;
		}
		ReqInfo& ri = req_info(cri) ;                                          // past this point, cri must not be used as it may be obsolete, use ri instead
		ri.update( run_action , make_action , *this ) ;
		if (ri.waiting()) goto Wait ;
		if (ri.done) {
			if ( run_action<=RunAction::Status || !unlinked ) goto Wakeup ;
			if ( !makable()                                 ) goto Wakeup ;    // no hope to regenerate, proceed as a done target
			ri.done        = false             ;
			regenerate_job = conform_job_tgt() ;                               // we must regenerate target, only run the conform job
		}
		//
		if (ri.prio_idx==NoIdx) {
			ri.prio_idx = 0 ;                                                  // initially skip the check of jobs we were waiting for
		} else {
			// check jobs we were waiting for
			JobTgtIter it{*this,ri.prio_idx} ;
			SWEAR(it) ;                                                        // how can it be that we were waiting for nothing ?
			for(; it ; it++ ) {
				JobTgt jt = *it ;
				trace("check",jt,jt->c_req_info(req)) ;
				if (!jt->c_req_info(req).done(run_action)) {                   // if it needed to be regenerated, it may not be done any more although we waited for it
					prod_idx = NoIdx ;                                         // safer to restart analysis at same level, although this may not be absolutely necessary
					multi_   = false ;                                         // this situation is exceptional enough not to bother trying to avoid this analysis restart
					goto Make ;
				}
				if (!jt.produces(idx())) continue ;                            // if Maybe, job is in error and is deemed to produce all its potential targets
				if (prod_idx==NoIdx) prod_idx = it.idx ;
				else                 multi_   = true   ;
			}
			if (prod_idx!=NoIdx) goto DoWakeup ;                               // we have done job, no need to investigate any further
			ri.prio_idx = it.idx ;
		}
	Make :
		SWEAR( prod_idx==NoIdx && !multi_ , prod_idx ) ;
		for (;;) {
			if (ri.prio_idx>=job_tgts.size()) {
				if (!rule_tgts) break ;                                        // fast path
				try {
					//                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					RuleIdx shorten_by = _gather_prio_job_tgts(rule_tgts.view(),req).second ;
					//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (shorten_by==NoIdx) { rule_tgts.clear()                ; }
					else                   { rule_tgts.shorten_by(shorten_by) ; }
					if (ri.prio_idx>=job_tgts.size()) break ;                     // fast path
				} catch (::vector<Node> const& e) {
					set_special(Special::Infinite,e,{}/*accesses*/,{}/*dflags*/) ;
					break ;
				}
			}
			JobTgtIter it{ *this , ri.prio_idx } ;
			for(; it ; it++ ) {                                                // check if we obviously have several jobs, in which case make nothing
				JobTgt jt = *it ;
				if      (jt.sure()                  ) _set_buildable(Yes) ;    // buildable is data independent & pessimistic (may be Maybe instead of Yes)
				else if (!jt->c_req_info(req).done()) continue ;
				else if (!jt.produces(idx())        ) continue ;
				if      (prod_idx==NoIdx            ) prod_idx = it.idx ;
				else                                  multi_   = true   ;
			}
			if (multi_) break ;
			prod_idx = NoIdx ;
			// make eligible jobs
			{	SaveInc save{ri.n_wait} ;                                      // ensure we appear waiting while making jobs so loops will block (caught because we are idle and req is not done)
				for( it.reset(ri.prio_idx) ; it ; it++ ) {
					JobTgt    jt     = *it               ;
					RunAction action = RunAction::Status ;
					JobReason reason ;
					if (+regenerate_job) {
						if (jt==regenerate_job) reason = {JobReasonTag::NoTarget,+idx()} ;
					} else {
						switch (ri.action) {
							case RunAction::Makable : if (jt.is_sure()) action = RunAction::Makable ; break ; // if star, job must be run to know if we are generated
							case RunAction::Status  :                                                 break ;
							case RunAction::Dsk     :
								if ( jt.is_sure() && !has_actual_job_tgt(jt) ) { // wash polution (if we have an actual job) or create target
									action = RunAction::Run ;
								} else {
									if (clean==Maybe) {                                                      // solve lazy evaluation
										if (jt->rule->is_special()) clean = No | (manual        (   )==No) ; // special rules handle manual targets specially
										else                        clean = No | (manual_refresh(req)==No) ;
									}
									if ( clean==No && jt.produces(idx()) ) reason = {JobReasonTag::NoTarget,+idx()} ;
								}
							break ;
							default : FAIL(ri.action) ;
						}
					}
					trace("make_job",ri,clean,action,jt) ;
					Job::ReqInfo& jri = jt->req_info(req) ;
					jri.live_out = ri.live_out ;                               // transmit user request to job for last level live output
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					jt->make( jri , action , reason ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (jri.waiting()      ) { jt->add_watcher(jri,idx(),ri,ri.pressure) ; continue ; }
					if (!jt.produces(idx())) {                                             continue ; }
					if (prod_idx==NoIdx    ) { prod_idx = it.idx ;                                    }
					else                     { multi_   = true   ;                                    }
				}
			}
			if (ri.waiting()   ) goto Wait ;
			if (prod_idx!=NoIdx) break     ;
			ri.prio_idx = it.idx ;
		}
	DoWakeup :
		multi       = multi_                                                                   ;
		conform_idx = multi_ ? NoIdx : prod_idx                                                ;
		uphill      = conform_idx!=NoIdx && job_tgts[prod_idx]->rule->special==Special::Uphill ;
		if (multi_) {
			::vector<JobTgt> jts ;
			for( JobTgt jt : conform_job_tgts(ri) ) if (jt.produces(idx())) jts.push_back(jt) ;
			trace("multi",ri,job_tgts.size(),conform_job_tgts(ri),jts) ;
			audit_multi(req,jts) ;
		}
		ri.done = true ;
	Wakeup :
		SWEAR(done(ri)) ;
		trace("wakeup",ri) ;
		ri.wakeup_watchers() ;
	Wait :
		return ri ;
	}

	void NodeData::audit_multi( Req req , ::vector<JobTgt> const& jts ) {
		/**/                   req->audit_node(Color::Err ,"multi",idx()            ) ;
		/**/                   req->audit_info(Color::Note,"several rules match :",1) ;
		for( JobTgt jt : jts ) req->audit_info(Color::Note,jt->rule->name         ,2) ;
	}

	bool/*ok*/ NodeData::forget( bool targets , bool deps ) {
		Trace trace("Nforget",idx(),STR(targets),STR(deps),STR(waiting()),conform_job_tgts()) ;
		if (is_src() ) throw ::pair("cannot forget source"s,idx().name()) ;
		if (waiting()) return false                                       ;
		//
		bool res = true ;
		RuleIdx k =0 ;
		for( Job j : job_tgts ) {
			/**/                                                    res &= j->forget(targets,deps) ;           // all jobs above conform jobs
			if (k==conform_idx) { for( Job j : conform_job_tgts() ) res &= j->forget(targets,deps) ; break ; } // conform jobs
			k++ ;
		}
		_set_buildable() ;                                                     // wash cache
		return res ;
	}

	void NodeData::mk_old() {
		Trace trace("mk_old",idx()) ;
		if (match_gen==NMatchGen) { trace("locked") ; return ; }                      // node is locked
		if ( +actual_job_tgt && actual_job_tgt->rule.old() ) actual_job_tgt.clear() ; // old jobs may be collected, do not refer to them anymore
		_set_buildable() ;
	}

	void NodeData::mk_no_src() {
		Trace trace("mk_no_src",idx()) ;
		_set_buildable() ;
		fence() ;
		rule_tgts     .clear() ;
		job_tgts      .clear() ;
		actual_job_tgt.clear() ;
		refresh() ;
	}

	void NodeData::mk_anti_src() {
		Trace trace("mk_anti_src",idx()) ;
		_set_buildable(No) ;
		fence() ;
		rule_tgts     .clear() ;
		job_tgts      .clear() ;
		actual_job_tgt.clear() ;
		match_gen = NMatchGen ;                                                // sources are locked match_ok
		refresh( Crc::None , Ddate::s_now() ) ;
	}

	void NodeData::mk_src() {
		Trace trace("mk_src",idx()) ;
		trace.hide() ;
		mk_anti_src() ;
		set_special(Special::Src) ;
		actual_job_tgt = { job_tgts[0] , true/*is_sure*/ } ;
		refresh() ;
	}

	bool/*modified*/ NodeData::refresh( Crc crc_ , Ddate date_ ) {
		bool steady = crc.match(crc_) ;
		Trace trace("refresh",idx(),STR(steady),crc,"->",crc_,date,"->",date_) ;
		if (steady) {                      date = date_ ;                        } // regulars and links cannot have the same crc
		else        { crc = {} ; fence() ; date = date_ ; fence() ; crc = crc_ ; } // ensure crc is never associated with a wrong date
		//
		if (unlinked) trace("!unlinked") ;
		unlinked = false ;                                            // dont care whether file exists, it have been generated according to its job
		return !steady ;
	}

	static inline ::pair<Bool3,bool/*refreshed*/> _manual_refresh( NodeData& nd , FileInfoDate const& fid ) {
		Bool3 m = nd.manual(fid) ;
		if (m!=Yes         ) return {m,false/*refreshed*/} ;                   // file was not modified
		if (nd.crc==Crc::None) return {m,false/*refreshed*/} ;                 // file appeared, it cannot be steady
		//
		::string nm  = nd.name()                 ;
		Crc      crc { nm , g_config.hash_algo } ;
		if (!nd.crc.match(Crc(nm,g_config.hash_algo))) return {Yes,false/*refreshed*/} ; // real modif
		//
		nd.date = file_date(nm) ;
		return {No,true/*refreshed*/} ;                                        // file is steady
	}
	Bool3 NodeData::manual_refresh( Req req , FileInfoDate const& fid ) {
		auto [m,refreshed] = _manual_refresh(*this,fid) ;
		if (refreshed) req->audit_node(Color::Note,"manual_steady",idx()) ;
		return m ;
	}
	Bool3 NodeData::manual_refresh( JobData const& j , FileInfoDate const& fid ) {
		auto [m,refreshed] = _manual_refresh(*this,fid) ;
		if (refreshed) for( Req r : j.reqs() ) r->audit_node(Color::Note,"manual_steady",idx()) ;
		return m ;
	}

	//
	// Target
	//

	::ostream& operator<<( ::ostream& os , Target const t ) {
		/**/                   os << "T("          ;
		if (+t               ) os << +t            ;
		if (t.is_unexpected()) os << ",unexpected" ;
		return                 os << ')'           ;
	}


	//
	// Deps
	//

	::ostream& operator<<( ::ostream& os , Deps const& ds ) {
		return os << vector_view_c<Dep>(ds) ;
	}

	//
	// Dep
	//

	::ostream& operator<<( ::ostream& os , Dep const& d ) {
		return os << static_cast<DepDigestBase<Node> const&>(d) ;
	}

	::string Dep::accesses_str() const {
		::string res ; res.reserve(+Access::N) ;
		for( Access a : Access::N ) res.push_back( accesses[a] ? AccessChars[+a] : '-' ) ;
		return res ;
	}

	::string Dep::dflags_str() const {
		::string res ; res.reserve(+Dflag::N) ;
		for( Dflag df : Dflag::N ) res.push_back( dflags[df] ? DflagChars[+df] : '-' ) ;
		return res ;
	}

}
