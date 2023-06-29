// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
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
		os << "NRI(" << ri.req <<','<< ri.action <<',' ;
		if (ri.prio_idx==Node::NoIdx) os<<"None"      ;
		else                          os<<ri.prio_idx ;
		return os <<','<< STR(ri.done) <<','<< STR(ri.n_wait) <<','<< STR(ri.err) <<')' ;
	}

	//
	// Node
	//

	::ostream& operator<<( ::ostream& os , Node const n ) {
		os << "N(" ;
		if (+n) os << +n ;
		os << ')' ;
		return os ;
	}

	void Node::_set_pressure_raw( ReqInfo& ri ) const {
		Trace trace("set_pressure","propagate",*this,ri) ;
		for( Job job : conform_job_tgts(ri) ) job.set_pressure(job.req_info(ri.req),ri.pressure) ; // go through current analysis level as this is where we may have deps we are waiting for
	}

	void Node::set_special( Special special , ::vmap<Node,DFlags> const& deps ) {
		Trace trace("set_special",*this,special,deps) ;
		JobTgts jts       = (*this)->job_tgts ;
		UNode   un        { *this }           ;
		Bool3   buildable = Yes               ;
		if ( !jts.empty() && jts.back()->rule.is_special() ) SWEAR(jts.back()->rule.special()==special) ;
		else                                                 un->job_tgts.append(::vector<JobTgt>({{Job(special,*this,deps),true/*is_sure*/}})) ;
		for( Dep const& d : (*this)->job_tgts.back()->static_deps() )
			if (d->buildable==Bool3::Unknown) buildable &= Maybe        ; // if not computed yet, well note we do not know
			else                              buildable &= d->buildable ; // could break as soon as !Yes is seen, but this way, we can have a more agressive swear
		SWEAR(buildable!=No) ;
		if (buildable==Yes) un->rule_tgts.clear() ;
		_set_buildable(buildable) ;
	}

	::vector_view_c<JobTgt> Node::prio_job_tgts(RuleIdx prio_idx) const {
		JobTgts const& jts = (*this)->job_tgts ;                               // /!\ jts is a CrunchVector, so if single element, a subvec would point to it, so it *must* be a ref
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
			SWEAR( prio_idx==jts.size() || prio_idx==NoIdx ) ;
		}
		return {} ;
	}

	template<class N> struct JobTgtIter {
		// cxtors
		JobTgtIter( N n , RuleIdx i=0 ) : node{n} , idx{i} {}
		// services
		JobTgtIter&   operator++(int)                                      { _prev_prio = _cur_prio() ; idx++ ; return *this ;             }
		JobTgt        operator* (   ) const                                { return node->job_tgts[idx] ;                                  }
		JobTgt*       operator->(   )       requires(::is_same_v<N,UNode>) { return node->job_tgts.begin()+idx ;                           }
		JobTgt const* operator->(   ) const                                { return node->job_tgts.begin()+idx ;                           }
		operator bool           (   ) const                                { return idx<node->job_tgts.size() && _cur_prio()>=_prev_prio ; }
		void reset(RuleIdx i=0) {
			idx        = i         ;
			_prev_prio = -Infinity ;
		}
	private :
		Prio _cur_prio() const { return (**this)->rule->prio ; }
		// data
	public :
		N       node ;
		RuleIdx idx  ;
	private :
		Prio _prev_prio = -Infinity ;
	} ;

	// instantiate rule_tgts into job_tgts by taking the first iso-prio chunk and return how many rule_tgts were consumed
	// - anti-rules always precede regular rules at given prio and are deemed to be of higher prio (and thus in different iso-prio chunks)
	// - if a sure job is found, then all rule_tgts are consumed as there will be no further match
	::pair<Bool3/*buildable*/,RuleIdx/*shorten_by*/> Node::_gather_prio_job_tgts( ::vector<RuleTgt> const& rule_tgts , DepDepth lvl ) {
		//
		if (rule_tgts.empty()) return {No,NoIdx} ;                             // fast path : avoid computing name()
		//
		::string name_     = name()    ;
		Prio     prio      = -Infinity ;                                       // initially, we are ready to accept any rule
		RuleIdx  n         = 0         ;
		bool     clear     = false     ;
		Bool3    buildable = No        ;                                       // return if we found a job candidate
		//
		::vector<JobTgt> jts ; jts.reserve(rule_tgts.size()) ;                 // typically, there is a single priority
		for( RuleTgt const& rt : rule_tgts ) {
			if (rt->prio<prio) goto Done ;
			n++ ;
			if ( rt->anti && +Rule::Match(rt,name_) ) { SWEAR(jts.empty()) ; clear = true ; goto Return ; }
			//          vvvvvvvvvvvvvvvvvvvvvv
			JobTgt jt = JobTgt(rt,name_,lvl+1) ;
			//          ^^^^^^^^^^^^^^^^^^^^^^
			if (!jt) continue ;
			if (jt.sure()) { buildable |= Yes   ; clear = true ; }
			else           { buildable |= Maybe ;                }
			jts.push_back(jt) ;
			prio = rt->prio ;
		}
		clear = true ;
	Done :
		//                vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (!jts.empty()) UNode(*this)->job_tgts.append(jts) ;
		//                ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	Return :
		if (clear) return {buildable,NoIdx} ;
		else       return {buildable,n    } ;
	}

	void Node::_set_buildable_raw(DepDepth lvl) {
		Trace trace("set_buildable",*this,lvl) ;
		if (lvl>=g_config.max_dep_depth) throw ::vmap<Node,DFlags>({{*this,SpecialDFlags}}) ; // infinite dep path
		::vector<RuleTgt> rule_tgts = raw_rule_tgts() ;
		if (!shared()) {
			UNode un{*this} ;
			un->rule_tgts.clear() ;
			un->job_tgts .clear() ;
			un->conform_idx = NoIdx ;
			un->uphill      = false ;
			share() ;
		}
		//                                                       vvvvvvvvvvvvvvvvvv
		if ( g_config.path_max && name_sz()>g_config.path_max) { _set_buildable(No) ; goto Return ; } // path is ridiculously long, make it unbuildable
		//                                                       ^^^^^^^^^^^^^^^^^^
		// during analysis, temporarily set buildable to break loops that will be caught at exec time
		// in case of crash, rescue mode is used and ensures all matches are recomputed
		_set_buildable(Yes) ;
		try {
			if ( Node dir_=dir() ; +dir_ ) {
				//vvvvvvvvvvvvvvvvvvvvvvv
				dir_.set_buildable(lvl+1) ;
				//^^^^^^^^^^^^^^^^^^^^^^^
				if (dir_->buildable!=No) {
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					set_special(Special::Uphill,{{dir_,SpecialDFlags|DFlag::Lnk}}) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (dir_->buildable==Yes) goto Return   ;
					else                      goto AllRules ;
				}
			}
			//                            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			auto [buildable,shorten_by] = _gather_prio_job_tgts(rule_tgts,lvl) ;
			//vvvvvvvvvvvvvvvvvvvvvvv     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			_set_buildable(buildable) ;
			//^^^^^^^^^^^^^^^^^^^^^^^
			//
			if (shorten_by) {
				//                     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (shorten_by!=NoIdx) UNode(*this)->rule_tgts = ::vector_view_c<RuleTgt>(rule_tgts,shorten_by) ;
				//                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				goto Return ;
			}
		} catch (::vmap<Node,DFlags>& e) {
			//vvvvvvvvvvvvvv
			_set_buildable() ;                                                 // restore Unknown as we do not want to appear as having been analyzed
			//^^^^^^^^^^^^^^
			e.emplace_back(*this,SpecialDFlags) ;
			throw ;
		}
	AllRules :
		//                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (!(*this)->rule_tgts) UNode(*this)->rule_tgts = rule_tgts ;
		//                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	Return :
		SWEAR((*this)->match_ok()) ;
		trace("summary",(*this)->buildable) ;
		return ;
	}

	Node::ReqInfo const& Node::_make_raw( ReqInfo const& cri , RunAction run_action , MakeAction make_action ) {
		bool    multi          = false   ;
		RuleIdx prod_idx       = NoIdx   ;
		Req     req            = cri.req ;
		Bool3   clean          = Maybe   ;                                     // lazy evaluate manual_ok()==Yes
		Job     regenerate_job ;
		Trace trace("Nmake",*this,cri,run_action,make_action) ;
		SWEAR(run_action<=RunAction::Dsk) ;
		//                                     vvvvvvvvvvvvvvv
		try                                  { set_buildable() ;                  }
		//                                     ^^^^^^^^^^^^^^^
		catch (::vmap<Node,DFlags> const& e) { set_special(Special::Infinite,e) ; }
		if ((*this)->buildable==No) {                                              // avoid allocating ReqInfo for non-buildable Node's
			SWEAR(make_action<MakeAction::Dec) ;
			SWEAR(!cri.has_watchers()        ) ;
			trace("not_buildable",cri) ;
			return cri ;
		}
		ReqInfo& ri = req_info(cri) ;                                          // past this point, cri must not be used as it may be obsolete, use ri instead
		ri.update( run_action , make_action , *this ) ;
		if (ri.waiting()) goto Wait ;
		if (ri.done) {
			if ( run_action<=RunAction::Status || !(*this)->unlinked ) goto Wakeup ;
			if ( !(*this)->makable()                                 ) goto Wakeup ; // no hope to regenerate, proceed as a done target
			ri.done        = false                      ;
			regenerate_job = (*this)->conform_job_tgt() ;                      // we must regenerate target, only run the conform job
		}
		//
		if (ri.prio_idx==NoIdx) {
			ri.prio_idx = 0 ;                                                  // initially skip the check of jobs we were waiting for
		} else {
			// check jobs we were waiting for
			JobTgtIter it{*this,ri.prio_idx} ;
			SWEAR(it) ;                                                        // how can it be that we were waiting for nothing ?
			for(; it ; it++ ) {
				trace("check",*it,it->c_req_info(req)) ;
				if (!it->c_req_info(req).done(run_action)) {                   // if it needed to be regenerated, it may not be done any more although we waited for it
					prod_idx = NoIdx ;                                         // safer to restart analysis at same level, although this may not be absolutely necessary
					multi    = false ;                                         // this situation is exceptional enough not to bother trying to avoid this analysis restart
					goto Make ;
				}
				if (it->produces(*this)==No) continue ;
				if (prod_idx==NoIdx) prod_idx = it.idx ;
				else                 multi    = true   ;
			}
			if (prod_idx!=NoIdx) goto DoWakeup ;                               // we have done job, no need to investigate any further
			ri.prio_idx = it.idx ;
		}
	Make :
		SWEAR( prod_idx==NoIdx && !multi ) ;
		for (;;) {
			if (ri.prio_idx>=(*this)->job_tgts.size()) {
				if (!(*this)->rule_tgts) break ;                               // fast path : avoid creating UNode(*this)
				try {
					//                   vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					RuleIdx shorten_by = _gather_prio_job_tgts((*this)->rule_tgts.view()).second ;
					//                   ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (shorten_by==NoIdx) { if (!shared()) { UNode(*this)->rule_tgts.clear()                ; share() ; } }
					else                   {                  UNode(*this)->rule_tgts.shorten_by(shorten_by) ;             }
					if (ri.prio_idx>=(*this)->job_tgts.size()) break ;                                                       // fast path
				} catch (::vmap<Node,DFlags> const& e) {
					set_special(Special::Infinite,e) ;
					break ;
				}
			}
			JobTgtIter it{ UNode(*this) , ri.prio_idx } ;
			for(; it ; it++ ) {                                                // check if we obviously have several jobs, in which case make nothing
				if      (it->sure()                 ) _set_buildable(Yes) ;    // buildable is data independent & pessimistic (may be Maybe instead of Yes)
				else if (!it->c_req_info(req).done()) continue ;
				else if (it->produces(*this)==No    ) continue ;
				if      (prod_idx==NoIdx            ) prod_idx = it.idx ;
				else                                  multi    = true   ;
			}
			if (multi) break ;
			prod_idx = NoIdx ;
			// make eligible jobs
			ri.n_wait++ ;                                                      // ensure we appear waiting while making jobs so loops will block (caught because we are idle and req is not done)
			for( it.reset(ri.prio_idx) ; it ; it++ ) {
				RunAction action = RunAction::None ;
				if (+regenerate_job) {
					if (*it==regenerate_job) action = RunAction::Run ;
				} else {
					switch (ri.action) {
						case RunAction::Makable : action = it->is_sure() ? RunAction::Makable : RunAction::Status ; break ; // if star, job must be run to know if we are generated
						case RunAction::Status  : action =                 RunAction::Status                      ; break ;
						case RunAction::Dsk     :
							if ( it->is_sure() && !(*this)->has_actual_job_tgt(*it) ) action = RunAction::Run ; // wash polution
							else {
								if      ( clean==Maybe                                           ) clean  = No | (manual_ok()==Yes) ; // solve lazy evaluation
								if      ( clean==Yes                                             ) action = RunAction::Status       ;
								else if ( !it->c_req_info(req).done() || it->produces(*this)!=No ) action = RunAction::Run          ; // else, we know job does not produce us, no reason to run it
							}
						break ;
						default : FAIL(ri.action) ;
					}
				}
				trace("make_job",ri,clean,action,*it) ;
				Job::ReqInfo& jri = it->req_info(req) ;
				jri.live_out = ri.live_out ;                                                       // transmit user request to job for last level live output
				//                           vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (action!=RunAction::None) it->make(jri,action, {JobReasonTag::NoTarget,+*this} ) ;
				//                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (jri.waiting()          ) { it->add_watcher(jri,*this,ri,ri.pressure) ; continue ; }
				if (it->produces(*this)==No) {                                             continue ; }
				if (prod_idx==NoIdx        ) { prod_idx = it.idx ;                                    }
				else                         { multi    = true   ;                                    }
			}
			ri.n_wait-- ;                                                      // restore
			if (ri.waiting()   ) goto Wait ;
			if (prod_idx!=NoIdx) break     ;
			ri.prio_idx = it.idx ;
		}
	DoWakeup :
		if (multi) {
			UNode            un  {*this} ;
			::vector<JobTgt> jts ;
			for( JobTgt jt : conform_job_tgts(ri) ) if (jt.produces(*this)!=No) jts.push_back(jt) ;
			trace("multi",ri,(*this)->job_tgts.size(),conform_job_tgts(ri),jts) ;
			un->conform_idx = NoIdx ;
			un->multi       = true  ;
			un->uphill      = false ;
			audit_multi(req,jts) ;
		} else {
			if ((*this)->conform_idx!=prod_idx) UNode(*this)->conform_idx = prod_idx ;
			if ((*this)->multi                ) UNode(*this)->multi       = false    ;
			if ((*this)->uphill               ) UNode(*this)->uphill      = false    ;
			if (prod_idx!=NoIdx) {
				JobTgt prod_job = (*this)->job_tgts[prod_idx] ;
				if (prod_job->rule.is_special()) UNode(*this)->uphill = prod_job->rule.special()==Special::Uphill ;
			}
		}
		ri.done = true ;
	Wakeup :
		SWEAR(done(ri)) ;
		trace("wakeup",ri) ;
		ri.wakeup_watchers() ;
	Wait :
		return ri ;
	}

	void Node::audit_multi( Req req , ::vector<JobTgt> const& jts ) {
		/**/                   req->audit_node(Color::Err ,"multi",*this            ) ;
		/**/                   req->audit_info(Color::Note,"several rules match :",1) ;
		for( JobTgt jt : jts ) req->audit_info(Color::Note,jt->rule->user_name()  ,2) ;
	}

	bool/*ok*/ Node::forget() {
		Trace trace("Nforget",*this,STR(waiting()),conform_job_tgts()) ;
		if (waiting()) return false ;
		bool res = true ;
		for( Job j : conform_job_tgts() ) res &= j.forget() ;
		_set_buildable() ;
		return res ;
	}

	void Node::mk_old() {
		Trace trace("mk_old",*this) ;
		if ((*this)->match_gen==NMatchGen) { trace("locked") ; return ; }      // node is locked
		if (shared()) {
			mk_shared(0) ;
		} else {
			UNode un{*this} ;
			if ( +un->actual_job_tgt && un->actual_job_tgt->rule.old() )
				un->actual_job_tgt.clear() ;                                   // old jobs may be collected, do not refer to them anymore
			un._set_buildable() ;
			share() ;
		}
	}

	void Node::mk_no_src() {
		Trace trace("mk_no_src",*this) ;
		if (shared()) { mk_shared(0) ; return ; }
		UNode un{*this} ;
		un._set_buildable() ;
		fence() ;
		un->rule_tgts     .clear() ;
		un->job_tgts      .clear() ;
		un->actual_job_tgt.clear() ;
		un.refresh() ;
		share() ;
	}

	void Node::mk_anti_src() {
		Trace trace("mk_anti_src",*this) ;
		if (shared()) { mk_shared(NMatchGen,No) ; return ; }
		UNode un{*this} ;
		un._set_buildable(No) ;
		fence() ;
		un->rule_tgts     .clear() ;
		un->job_tgts      .clear() ;
		un->actual_job_tgt.clear() ;
		un->match_gen = NMatchGen ;                                            // sources are locked match_ok
		un.refresh( false/*is_lnk*/ , Crc::None , DiskDate::s_now() ) ;
		share() ;
	}

	void Node::mk_src() {
		Trace trace("mk_src",*this) ;
		trace.hide() ;
		mk_anti_src() ;
		set_special(Special::Src) ;
		UNode un{*this} ;
		un->actual_job_tgt = { (*this)->job_tgts[0] , true/*is_sure*/ } ;
		un.refresh() ;
	}

	//
	// UNode
	//

	::ostream& operator<<( ::ostream& os , UNode const n ) {
		return os<<'U'<<Node(n) ;
	}

	bool/*modified*/ UNode::refresh( bool is_lnk , Crc crc , DiskDate date ) {
		if (is_lnk) SWEAR(crc!=Crc::None) ;                                    // cannot be a link without existing
		bool steady = (*this)->crc.match(crc) ;
		Trace trace("refresh",*this,STR(steady),STR((*this)->is_lnk),"->",STR(is_lnk),(*this)->crc,"->",crc,(*this)->date,"->",date) ;
		if (steady) {                               SWEAR((*this)->is_lnk==is_lnk) ; (*this)->date = date ;                                } // regulars and links cannot have the same crc
		else        { (*this)->crc = {} ; fence() ;       (*this)->is_lnk = is_lnk ; (*this)->date = date ; fence() ; (*this)->crc = crc ; } // ensure crc is never associated with a wrong date
		//
		if ((*this)->unlinked) trace("!unlinked") ;
		(*this)->unlinked = false ;                                            // dont care whether file exists, it have been generated according to its job
		return !steady ;
	}

	//
	// NodeData
	//

	::ostream& operator<<( ::ostream& os , NodeData const& nd ) {
		os << '(' << nd.is_lnk ;
		os << ',' << nd.crc    ;
		os << ',' << nd.date   ;
		os << ',' ;
		if (!nd.match_ok()) os << '~' ;
		os << "job:" ;
		os << +Job(nd.actual_job_tgt) ;
		if (nd.actual_job_tgt.is_sure()) os << '+' ;
		os << ")" ;
		return os ;
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

	::ostream& operator<<( ::ostream& os , Dep const& d ) { return os << static_cast<DepDigestBase<Node> const&>(d) ; }

}
