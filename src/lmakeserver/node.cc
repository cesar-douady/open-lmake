// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

namespace Engine {
	using namespace Disk ;
	using namespace Time ;

	//
	// NodeReqInfo
	//

	::ostream& operator<<( ::ostream& os , NodeReqInfo const& ri ) {
		/**/                          os << "NRI(" << ri.req <<','<< ri.goal <<',' ;
		if (ri.prio_idx==Node::NoIdx) os << "NoIdx"                                ;
		else                          os <<                  ri.prio_idx           ;
		if (+ri.done_               ) os <<",Done@"       << ri.done_              ;
		if ( ri.n_wait              ) os <<",wait:"       << ri.n_wait             ;
		if (+ri.overwritten         ) os <<",overwritten:"<<ri.overwritten         ;
		return                        os <<')'                                     ;
	}

	//
	// Node
	//

	Hash::Crc Node::_s_src_dirs_crc ;

	::ostream& operator<<( ::ostream& os , Node const n ) {
		/**/    os << "N(" ;
		if (+n) os << +n   ;
		return  os << ')'  ;
	}

	Hash::Crc Node::s_src_dirs_crc() {
		if (!_s_src_dirs_crc) {
			Hash::Xxh h ;
			for( const Node s : s_srcs(true/*dirs*/) ) h.update(s->name()) ; // ensure it works with in RO mode
			_s_src_dirs_crc = h.digest() ;
		}
		return _s_src_dirs_crc ;
	}

	//
	// NodeData
	//

	Mutex<MutexLvl::NodeCrcDate> NodeData::s_crc_date_mutex ;

	::ostream& operator<<( ::ostream& os , NodeData const& nd ) {
		/**/                    os <<'('<< nd.crc ;
		if (nd.is_plain()) {
			/**/                os <<',' << nd.date()       ;
			if (!nd.match_ok()) os << ",~job:"              ;
			/**/                os << ",job:"               ;
			/**/                os << +Job(nd.actual_job()) ;
		} else {
			/**/                os <<','<< nd.log_date()   ;
			if (nd.is_encode()) os <<','<< nd.codec_code() ;
			else                os <<','<< nd.codec_val () ;
		}
		return                  os <<')' ;
	}

	Manual NodeData::manual_wash( ReqInfo& ri , bool lazy ) {
		if ( !lazy || ri.manual==Manual::Unknown ) {
			Req  req      = ri.req                   ;
			bool dangling = buildable<=Buildable::No ;
			ri.manual = manual_refresh(req) ;
			switch (ri.manual) {
				case Manual::Ok      :
				case Manual::Unlnked : break ;
				case Manual::Empty :
					if (!dangling) {
						Trace trace("manual_wash","unlnk",idx()) ;
						::string n = name() ;
						SWEAR(is_lcl(n),n) ;
						unlnk(n) ;
						req->audit_node( Color::Note , "unlinked (empty)" , idx() ) ;
						ri.manual = Manual::Unlnked ;
						break ;
					}
				[[fallthrough]] ;
				case Manual::Modif : {
					Trace trace("manual_wash","modif",STR(dangling),idx()) ;
					if (dangling) {
						/**/                  req->audit_node( Color::Err  , "dangling"           , idx()                                        ) ;
						if (has_actual_job()) req->audit_info( Color::Note , "generated as a side effect of "+mk_file(actual_job()->name())  , 1 ) ;
						else                  req->audit_node( Color::Note , "consider : git add" , idx()                                    , 1 ) ;
					} else {
						::string n = name() ;
						if (::rename( n.c_str() , dir_guard(QuarantineDirS+n).c_str() )==0) {
							req->audit_node( Color::Warning , "quarantined" , idx() ) ;
							ri.manual = Manual::Unlnked ;
						} else {
							req->audit_node( Color::Err , "failed to quarantine" , idx() ) ;
						}
					}
				} break ;
			DF}
		}
		return ri.manual ;
	}

	void NodeData::_set_pressure_raw( ReqInfo& ri ) const {
		for( Job j : conform_job_tgts(ri) ) j->set_pressure(j->req_info(ri.req),ri.pressure) ; // go through current analysis level as this is where we may have deps we are waiting for
	}

	bool/*modified*/ NodeData::refresh_src_anti( bool report_no_file , ::vector<Req> const& reqs_ , ::string const& name_ ) { // reqss_ are for reporting only
		bool        prev_ok   = crc.valid() && crc.exists() ;
		bool        frozen    = idx().frozen()              ;
		const char* msg       = frozen ? "frozen" : "src"   ;
		NfsGuard    nfs_guard { g_config.reliable_dirs }    ;
		FileInfo    fi        { nfs_guard.access(name_) }   ;
		FileSig     sig       { fi  }                       ;
		Trace trace("refresh_src_anti",STR(report_no_file),reqs_,sig) ;
		if (frozen) for( Req r : reqs_  ) r->frozen_nodes.emplace(idx(),r->frozen_nodes.size()) ;
		if (!fi) {
			if (report_no_file) for( Req r : reqs_  ) r->audit_job( Color::Err , "missing" , msg , name_ ) ;
			if (crc==Crc::None) return false/*updated*/ ;
			//vvvvvvvvvvvvvvvv
			refresh(Crc::None) ;
			//^^^^^^^^^^^^^^^^
		} else {
			if ( crc.valid() && sig==date().sig ) return false/*updated*/ ;
			Crc crc_ = Crc::Reg ;
			while ( crc_==Crc::Reg || crc_==Crc::Lnk ) crc_ = Crc(sig,name_,g_config.hash_algo) ;                             // ensure file is stable when computing crc
			Accesses mismatch = crc.diff_accesses(crc_) ;
			//vvvvvvvvvvvvvvvvvvv
			refresh( crc_ , sig ) ;
			//^^^^^^^^^^^^^^^^^^^
			const char* step = !prev_ok ? "new" : +mismatch ? "changed" : "steady" ;
			Color       c    = frozen ? Color::Warning : Color::HiddenOk           ;
			for( Req r : reqs() ) { ReqInfo      & ri  = req_info  (r) ; if (fi.date>r->start_ddate              ) ri.overwritten |= mismatch ;             }
			for( Req r : reqs_  ) { ReqInfo const& cri = c_req_info(r) ; if (!cri.done(cri.goal|NodeGoal::Status)) r->audit_job( c , step , msg , name_ ) ; }
			if (!mismatch) return false/*updated*/ ;
		}
		return true/*updated*/ ;
	}

	void NodeData::set_infinite(::vector<Node> const& deps) {
		Trace trace("set_infinite",idx(),deps) ;
		job_tgts().assign(::vector<JobTgt>({{
			Job( Special::Infinite , idx() , Deps(deps,{}/*accesses*/,{}/*dflags*/,false/*parallel*/) )
		,	true/*is_sure*/
		}})) ;
		Buildable buildable_ = Buildable::Yes ;
		for( Node const& d : deps )
			if (d->buildable==Buildable::Unknown) buildable_ &= Buildable::Maybe ; // if not computed yet, well note we do not know
			else                                  buildable_ &= d->buildable     ; // could break as soon as !Yes is seen, but this way, we can have a more agressive swear
		SWEAR(buildable_>Buildable::No) ;
		if (buildable_>=Buildable::Yes) rule_tgts().clear() ;
		buildable = buildable_ ;
	}

	::c_vector_view<JobTgt> NodeData::prio_job_tgts(RuleIdx prio_idx) const {
		if (prio_idx==NoIdx) return {} ;
		JobTgts const& jts = job_tgts() ; // /!\ jts is a CrunchVector, so if single element, a subvec would point to it, so it *must* be a ref
		if (prio_idx>=jts.size()) {
			SWEAR( prio_idx==jts.size() , prio_idx , jts.size() ) ;
			return {} ;
		}
		RuleIdx                 sz   = 0                    ;
		::c_vector_view<JobTgt> sjts = jts.subvec(prio_idx) ;
		Prio                    prio = -Infinity            ;
		for( JobTgt jt : sjts ) {
			Prio new_prio = jt->rule->prio ;
			if (new_prio<prio) break ;
			prio = new_prio ;
			sz++ ;
		}
		return sjts.subvec(0,sz) ;
	}

	::c_vector_view<JobTgt> NodeData::candidate_job_tgts() const {
		RuleIdx ci = conform_idx() ;
		if (ci==NoIdx) return {} ;
		JobTgts const& jts  = job_tgts()          ; // /!\ jts is a CrunchVector, so if single element, a subvec would point to it, so it *must* be a ref
		Prio           prio = jts[ci]->rule->prio ;
		RuleIdx        idx  = ci                  ;
		for( JobTgt jt : jts.subvec(ci) ) {
			if (jt->rule->prio<prio) break ;
			idx++ ;
		}
		return jts.subvec(0,idx) ;
	}

	struct JobTgtIter {
		// cxtors
		JobTgtIter( NodeData& n , NodeReqInfo const& ri ) : node{n} , idx{ri.prio_idx} , single{ri.single} {}
		// services
		JobTgtIter& operator++(int) {
			_prev_prio = _cur_prio() ;
			if (single) idx = node.job_tgts().size() ;
			else        idx++ ;
			return *this ;
		}
		JobTgt        operator* (   ) const { return node.job_tgts()[idx] ;                                  }
		JobTgt const* operator->(   ) const { return node.job_tgts().begin()+idx ;                           }
		JobTgt      * operator->(   )       { return node.job_tgts().begin()+idx ;                           }
		operator bool           (   ) const { return idx<node.job_tgts().size() && _cur_prio()>=_prev_prio ; }
		void reset(RuleIdx i=0) {
			idx        = i         ;
			_prev_prio = -Infinity ;
		}
	private :
		Prio _cur_prio() const { return (**this)->rule->prio ; }
		// data
	public :
		NodeData& node   ;
		RuleIdx   idx    = 0    /*garbage*/ ;
		bool      single = false/*garbage*/ ;
	private :
		Prio _prev_prio = -Infinity ;
	} ;

	// check rule_tgts special rules and set rule_tgts accordingly
	Buildable NodeData::_gather_special_rule_tgts(::string const& name_) {
		job_tgts().clear() ;
		rule_tgts() = Node::s_rule_tgts(name_) ;
		//
		RuleIdx  n_skip = 0 ;
		for( RuleTgt const& rt : rule_tgts().view() ) {
			if (!rt.pattern().match(name_)) { n_skip++ ; continue ; }
			switch (rt->special) {
				case Special::GenericSrc : rule_tgts() = ::vector<RuleTgt>({rt}) ; return Buildable::DynSrc  ;
				case Special::Anti       : rule_tgts() = ::vector<RuleTgt>({rt}) ; return Buildable::DynAnti ;
				case Special::Plain      : rule_tgts().shorten_by(n_skip) ;        return Buildable::Maybe   ; // no special rule applies
			DF}
		}
		rule_tgts().clear() ;
		return Buildable::Maybe ;                                                                              // node may be buildable from dir
	}

	// instantiate rule_tgts into job_tgts by taking the first iso-prio chunk and set rule_tgts accordingly
	// - special rules (always first) are already processed
	// - if a sure job is found, then all rule_tgts are consumed as there will be no further match
	Buildable NodeData::_gather_prio_job_tgts( ::string const& name_ , Req req , DepDepth lvl ) {
		//
		Prio              prio       = -Infinity          ;                    // initially, we are ready to accept any rule
		RuleIdx           n          = 0                  ;
		Buildable         buildable  = Buildable::No      ;                    // return val if we find no job candidate
		::vector<RuleTgt> rule_tgts_ = rule_tgts().view() ;
		//
		SWEAR(is_lcl(name_),name_) ;
		::vector<JobTgt> jts ; jts.reserve(rule_tgts_.size()) ;                // typically, there is a single priority
		for( RuleTgt const& rt : rule_tgts_ ) {
			SWEAR(!rt->is_special()) ;
			if (rt->prio<prio) goto Done ;
			//          vvvvvvvvvvvvvvvvvvvvvvvvvv
			JobTgt jt = JobTgt(rt,name_,req,lvl+1) ;
			//          ^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (+jt) {
				if (jt.sure()) { buildable  = Buildable::Yes   ; n = NoIdx ; } // after a sure job, we can forget about rules at lower prio
				else             buildable |= Buildable::Maybe ;
				jts.push_back(jt) ;
				prio = rt->prio ;
			}
			if (n!=NoIdx) n++ ;
		}
		n = NoIdx ;                                                            // we have exhausted all rules
	Done :
		//            vvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (+jts    ) job_tgts ().append    (jts) ;
		if (n==NoIdx) rule_tgts().clear     (   ) ;
		else          rule_tgts().shorten_by(n  ) ;
		//            ^^^^^^^^^^^^^^^^^^^^^^^^^^^
		return buildable ;
	}

	void NodeData::_set_buildable_raw( Req req , DepDepth lvl ) {
		Trace trace("set_buildable",idx(),lvl) ;
		switch (buildable) {                                                                            // ensure we do no update sources
			case Buildable::Src    :
			case Buildable::SrcDir :
			case Buildable::Anti   : SWEAR(!rule_tgts(),rule_tgts()) ; goto Return ;
			case Buildable::Decode :
			case Buildable::Encode :
			case Buildable::Loop   :                                   goto Return ;
			default : ;
		}
		status(NodeStatus::Unknown) ;
		//
		{	::string name_ = name() ;
			//
			{	Buildable buildable_ = _gather_special_rule_tgts(name_) ;
				//                                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (buildable_<=Buildable::No     ) { buildable = Buildable::No       ; goto Return ; } // AntiRule have priority so no warning message is generated
				if (name_.size()>g_config.path_max) { buildable = Buildable::LongName ; goto Return ; } // path is ridiculously long, make it unbuildable
				if (buildable_>=Buildable::Yes    ) { buildable = buildable_          ; goto Return ; }
				//                                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			}
			buildable = Buildable::Loop ;                                                               // during analysis, temporarily set buildable to break loops (will be caught at exec time) ...
			try {                                                                                       // ... in case of crash, rescue mode is used and ensures all matches are recomputed
				Buildable db = Buildable::No ;
				if (+dir()) {
					if (lvl>=g_config.max_dep_depth) throw ::vector<Node>() ;                           // infinite dep path
					//vvvvvvvvvvvvvvvvvvvvvvvvvvv
					dir()->set_buildable(req,lvl+1) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^
					switch ((db=dir()->buildable)) {
						case Buildable::DynAnti   :
						case Buildable::Anti      :
						case Buildable::No        :
						case Buildable::Maybe     :                                    break       ;
						//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						case Buildable::Yes       : buildable = Buildable::Yes       ; goto Return ;
						case Buildable::DynSrc    :
						case Buildable::Src       :
						case Buildable::SubSrc    : buildable = Buildable::SubSrc    ; goto Return ;
						case Buildable::SrcDir    :
						case Buildable::SubSrcDir : buildable = Buildable::SubSrcDir ; goto Return ;
						//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					DF}
				}
				//                    vvvvvvvvvvvvvvvvvvvvvvvvv
				if (!is_lcl(name_)) { buildable = Buildable::No ; goto Return ; }
				//                    ^^^^^^^^^^^^^^^^^^^^^^^^^
				//
				Buildable buildable_ = _gather_prio_job_tgts(name_,req,lvl) ;
				if (db==Buildable::Maybe) buildable_ |= Buildable::Maybe ;                              // we are at least as buildable as our dir
				//vvvvvvvvvvvvvvvvvvvv
				buildable = buildable_ ;
				//^^^^^^^^^^^^^^^^^^^^
				goto Return ;
			} catch (::vector<Node>& e) {
				_set_match_gen(false/*ok*/) ;                                                           // restore Unknown as we do not want to appear as having been analyzed
				e.push_back(idx()) ;
				throw ;
			}
		}
	Return :
		_set_match_gen(true/*ok*/) ;
		trace("done",buildable) ;
		return ;
	}

	bool/*done*/ NodeData::_make_pre(ReqInfo& ri) {
		Trace trace("Nmake_pre",idx(),ri) ;
		Req      req   = ri.req ;
		::string name_ ;                                                                                // lazy evaluated
		auto lazy_name = [&]()->::string const& {
			if (!name_) name_ = name() ;
			return name_ ;
		} ;
		// step 1 : handle what can be done without dir
		switch (buildable) {
			case Buildable::LongName :
				if (req->long_names.try_emplace(idx(),req->long_names.size()).second) {                 // if inserted
					size_t sz = lazy_name().size() ;
					SWEAR( sz>g_config.path_max , sz , g_config.path_max ) ;
					req->audit_node( Color::Warning , to_string("name is too long (",sz,'>',g_config.path_max,") for") , idx() ) ;
				}
				[[fallthrough]] ;
			case Buildable::DynAnti   :
			case Buildable::Anti      :
			case Buildable::SrcDir    :
			case Buildable::No        : status(NodeStatus::None) ; goto NoSrc ;
			case Buildable::DynSrc    :
			case Buildable::Src       : status(NodeStatus::Src ) ; goto Src   ;
			case Buildable::Decode    :
			case Buildable::Encode    : status(NodeStatus::Src ) ; goto Codec ;
			case Buildable::Unknown   :                            FAIL()     ;
			default                   :                            break      ;
		}
		if (!dir()) goto NotDone ;
		// step 2 : handle what can be done without making dir
		switch (dir()->buildable) {
			case Buildable::DynAnti :
			case Buildable::Anti    :
			case Buildable::No      :                            goto NotDone ;
			case Buildable::SrcDir  : status(NodeStatus::None) ; goto Src     ;                         // status is overwritten Src if node actually exists
			default                 :                            break        ;
		}
		if ( ReqInfo& dri = dir()->req_info(req) ; !dir()->done(dri,NodeGoal::Status) ) {               // fast path : no need to call make if dir is done
			if (!dri.waiting()) {
				ReqInfo::WaitInc sav_n_wait{ri} ;                                                       // appear waiting in case of recursion loop (loop will be caught because of no job on going)
				dir()->asking = idx() ;
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				dir()->make( dri , MakeAction::Status , ri.speculate ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			}
			trace("dir",dir(),STR(dir()->done(dri,NodeGoal::Status)),ri) ;
			//
			if (dri.waiting()) {
				dir()->add_watcher(dri,idx(),ri,ri.pressure) ;
				status(NodeStatus::Uphill) ;                                                            // temporarily, until dir() is built and we know the definitive answer
				goto NotDone ;                                                                          // return value is meaningless when waiting
			}
			SWEAR(dir()->done(dri)) ;                                                                   // after having called make, dep must be either waiting or done
		}
		// step 3 : handle what needs dir status
		switch (dir()->buildable) {
			case Buildable::Maybe :
				if (dir()->status()==NodeStatus::None) { status(NodeStatus::Unknown) ; goto NotDone ; } // not Uphill anymore
				[[fallthrough]] ;
			case Buildable::Yes :
				if (buildable==Buildable::Maybe) buildable = Buildable::Yes ;                           // propagate as dir->buildable may have changed from Maybe to Yes when made
				[[fallthrough]] ;
			case Buildable::SubSrc    :
			case Buildable::SubSrcDir :
				switch (dir()->status()) {
					case NodeStatus::Transcient : status(NodeStatus::Transcient) ; goto NoSrc ;         // forward
					case NodeStatus::Uphill     : status(NodeStatus::Uphill    ) ; goto NoSrc ;         // .
					default : ;
				}
			break ;
			default : ;
		}
		// step 4 : handle what needs dir crc
		switch (dir()->buildable) {
			case Buildable::Maybe     :
			case Buildable::Yes       :
			case Buildable::SubSrcDir :
				if (dir()->crc==Crc::None) { status(NodeStatus::None) ; goto Src ; }                    // status is overwritten Src if node actually exists
				[[fallthrough]] ;
			case Buildable::DynSrc :
			case Buildable::Src    :
				if (dir()->crc.is_lnk()) status(NodeStatus::Transcient) ;                               // our dir is a link, we are transcient
				else                     status(NodeStatus::Uphill    ) ;                               // a non-existent source stays a source, hence its sub-files are uphill
				goto NoSrc ;
		DF}
	Src :
		{	bool modified = refresh_src_anti( status()!=NodeStatus::None , {req} , lazy_name() ) ;
			if (crc     !=Crc::None       ) status(NodeStatus::Src) ;                                   // overwrite status if it was pre-set to None
			if (status()==NodeStatus::None) goto NoSrc           ;                                      // if status was pre-set to None, it means we accept NoSrc
			if (modified                  ) goto ActuallyDoneDsk ;                                      // sources are always done on disk, as it is by probing it that we are done
			else                            goto DoneDsk         ;                                      // .
		}
	Codec :
		{	SWEAR(crc.valid()) ;
			if (!Codec::refresh(+idx(),+ri.req)) status(NodeStatus::None) ;
			if (log_date()>req->start_ddate    ) ri.overwritten = Access::Reg ;                         // date is only updated when actual content is modified and codec cannot be links
			trace("codec",ri.overwritten) ;
			goto Done ;
		}
	NoSrc :
		{	if (status()==NodeStatus::Transcient) {
				refresh(Crc::Unknown) ;                                                                 // if depending on a transcient node, a job must be rerun in all cases
				goto ActuallyDoneDsk ;
			}
			trace("no_src",crc) ;
			if (ri.goal>=NodeGoal::Dsk) {
				manual_wash(ri,true/*lazy*/) ;                                                          // always check manual if asking for disk
				if (crc==Crc::None       ) goto Done ;                                                  // node is not polluted
				if (ri.manual==Manual::Ok) {                                                            // if already unlinked, no need to unlink it again
					SWEAR(is_lcl(lazy_name()),lazy_name()) ;
					unlnk(lazy_name(),true/*dir_ok*/) ;                                                 // wash pollution if not manual
					req->audit_job( Color::Warning , "unlink" , "no_rule" , lazy_name() ) ;
				}
			} else {
				if (crc==Crc::None) goto Done ;                                                         // node is not polluted
			}
			refresh(Crc::None) ;                                                                        // if not physically unlinked, node will be manual
			goto ActuallyDone ;
		}
	ActuallyDone :
		actual_job() = {} ;
	Done :
		ri.done_ = ri.goal ;                                                                            // disk is de facto updated
		goto NotDone ;
	ActuallyDoneDsk :
		actual_job() = {} ;
	DoneDsk :
		ri.done_ = NodeGoal::Dsk ;                                                                      // disk is de facto updated
		goto NotDone ;
	NotDone :
		trace("done",idx(),status(),crc,ri) ;
		return ri.done() ;
	}

	static bool _may_need_regenerate( NodeData const& nd , NodeReqInfo& ri , NodeMakeAction make_action ) {
		/**/                        if (make_action==NodeMakeAction::Wakeup) return false ;                 // do plain analysis
		/**/                        if (!ri.done(NodeGoal::Status)         ) return false ;                 // do plain analysis
		Job cj = nd.conform_job() ; if (!cj                                ) return false ;                 // no hope to regenerate, proceed normally
		/**/                        if ( cj->err()                         ) return false ;                 // err() is up to date and cannot regenerate error
		Job aj = nd.actual_job () ;
		/**/                        if (cj!=aj                             ) ri.done_ &= NodeGoal::Status ; // disk cannot be ok if node was polluted, does not change conform_job()
		/**/                        if (ri.done()                          ) return false ;
		Trace trace("_may_need_regenerate",nd.idx(),ri,cj,aj) ;
		ri.prio_idx = nd.conform_idx() ;                                                                    // ask to run only conform job
		ri.single   = true             ;                                                                    // .
		return true ;
	}
	void NodeData::_make_raw( ReqInfo& ri , MakeAction make_action , Bool3 speculate ) {
		RuleIdx prod_idx       = NoIdx                              ;
		Req     req            = ri.req                             ;
		Bool3   clean          = Maybe                              ;                                       // lazy evaluate manual()==No
		bool    multi          = false                              ;
		bool    stop_speculate = speculate<ri.speculate && +ri.goal ;
		Trace trace("Nmake",idx(),ri,make_action) ;
		ri.speculate &= speculate ;
		//                                vvvvvvvvvvvvvvvvvv
		try                             { set_buildable(req) ; }
		//                                ^^^^^^^^^^^^^^^^^^
		catch (::vector<Node> const& e) { set_infinite(e)    ; }
		//
		if (make_action==MakeAction::Wakeup                               ) ri.dec_wait() ;
		else                                                                ri.goal = ri.goal | mk_goal(make_action) ;
		if      ( ri.waiting()                                            ) goto Wait ;
		else if ( req.zombie()                                            ) ri.done_ |= NodeGoal::Dsk     ;
		else if ( buildable>=Buildable::Yes && ri.goal==NodeGoal::Makable ) ri.done_ |= NodeGoal::Makable ;
		//
		if (ri.prio_idx==NoIdx) {
			if (ri.done()) goto Wakeup ;
			//vvvvvvvvvvv
			_make_pre(ri) ;
			//^^^^^^^^^^^
			if (ri.waiting()) goto Wait   ;
			if (ri.done()   ) goto Wakeup ;
			ri.prio_idx = 0 ;
		} else {
			// check if we need to regenerate node
			if ( ri.done(NodeGoal::Status) && _may_need_regenerate(*this,ri,make_action) ) goto Make   ;
			if ( ri.done()                                                               ) goto Wakeup ;
			// fast path : check jobs we were waiting for, lighter than full analysis
			JobTgtIter it{*this,ri} ;
			for(; it ; it++ ) {
				JobTgt jt   = *it                                                 ;
				bool   done = jt->c_req_info(req).done(ri.goal>=NodeGoal::Status) ;
				trace("check",jt,jt->c_req_info(req)) ;
				if (!done             ) { prod_idx = NoIdx ; goto Make ;                               }    // we waited for it and it is not done, retry
				if (jt.produces(idx())) { if (prod_idx==NoIdx) prod_idx = it.idx ; else multi = true ; }    // jobs in error are deemed to produce all their potential targets
			}
			if (prod_idx!=NoIdx) goto DoWakeup ;                                                            // we have at least one done job, no need to investigate any further
			if (ri.single) ri.single   = false  ;                                                           // if regenerating but job does not generate us, something strange happened, retry this prio
			else           ri.prio_idx = it.idx ;                                                           // else go on with next prio
		}
	Make :
		for(;;) {
			SWEAR(prod_idx==NoIdx,prod_idx) ;
			if (ri.prio_idx>=job_tgts().size()) {                                                           // gather new job_tgts from rule_tgts
				SWEAR(!ri.single) ;                                                                         // we only regenerate using an existing job
				try {
					//                      vvvvvvvvvvvvvvvvvvvvvvvvvv
					buildable = buildable | _gather_prio_job_tgts(req) ;
					//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (ri.prio_idx>=job_tgts().size()) break ;                                             // fast path
				} catch (::vector<Node> const& e) {
					set_infinite(e) ;
					break ;
				}
			}
			JobTgtIter it{*this,ri} ;
			if (!ri.single) {                                                                               // fast path : cannot have several jobs if we consider only a single job
				for(; it ; it++ ) {                                                                         // check if we obviously have several jobs, in which case make nothing
					JobTgt jt = *it ;
					if      ( jt.sure()                 )   buildable = Buildable::Yes ;                    // buildable is data independent & pessimistic (may be Maybe instead of Yes)
					else if (!jt->c_req_info(req).done())   continue ;
					else if (!jt.produces(idx())        )   continue ;
					if      (prod_idx!=NoIdx            ) { multi = true ; goto DoWakeup ; }
					prod_idx = it.idx ;
				}
				it.reset(ri.prio_idx) ;
				prod_idx = NoIdx ;
			}
			// make eligible jobs
			{	ReqInfo::WaitInc sav_n_wait{ri} ;                                                           // ensure we appear waiting while making jobs to block loops (caught in Req::chk_end)
				for(; it ; it++ ) {
					JobTgt     jt   = *it                    ;
					JobMakeAction ma = JobMakeAction::Status ;
					JobReason reason ;
					switch (ri.goal) {
						case NodeGoal::Makable : if (jt.sure()) ma = JobMakeAction::Makable ; break ;                    // if star, job must be run to know if we are generated
						case NodeGoal::Status  :
						case NodeGoal::Dsk     :
							if      (!jt.produces(idx())              ) {}
							else if (!has_actual_job(  )              ) reason = {JobReasonTag::NoTarget      ,+idx()} ; // this is important for NodeGoal::Status as crc is not correct
							else if (!has_actual_job(jt)              ) reason = {JobReasonTag::PollutedTarget,+idx()} ; // .
							else if (ri.goal==NodeGoal::Status        ) {}                                               // dont check disk if asked for Status
							else if (jt->running(true/*with_zombies*/)) reason =  JobReasonTag::Garbage                ; // be pessimistic and dont check target as it is not manual ...
							else                                                                                         // ... and checking may modify it
								switch (manual_wash(ri,true/*lazy*/)) {
									case Manual::Ok      :                                                  break ;
									case Manual::Unlnked : reason = {JobReasonTag::NoTarget      ,+idx()} ; break ;
									case Manual::Empty   :
									case Manual::Modif   : reason = {JobReasonTag::PollutedTarget,+idx()} ; break ;
								DF}
						break ;
					DF}
					JobReqInfo& jri = jt->req_info(req) ;
					if (ri.live_out) jri.live_out = ri.live_out ;                                                        // transmit user request to job for last level live output
					jt->asking = idx() ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					jt->make( jri , ma , reason , ri.speculate ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					trace("job",ri,clean,ma,jt,STR(jri.waiting()),STR(jt.produces(idx()))) ;
					if      (jri.waiting()     ) jt->add_watcher(jri,idx(),ri,ri.pressure) ;
					else if (jt.produces(idx())) { if (prod_idx==NoIdx) prod_idx = it.idx ; else multi = true ; }        // jobs in error are deemed to produce all their potential targets
				}
			}
			if (ri.waiting()   ) goto Wait ;
			if (prod_idx!=NoIdx) break     ;
			ri.prio_idx = it.idx ;
		}
	DoWakeup :
		if      (prod_idx==NoIdx) status     (NodeStatus::None) ;
		else if (!multi         ) conform_idx(prod_idx        ) ;
		else {
			status(NodeStatus::Multi) ;
			::vector<JobTgt> jts ;
			for( JobTgt jt : conform_job_tgts(ri) ) if (jt.produces(idx())) jts.push_back(jt) ;
			trace("multi",ri,job_tgts().size(),conform_job_tgts(ri),jts) ;
			/**/                   req->audit_node(Color::Err ,"multi",idx()            ) ;
			/**/                   req->audit_info(Color::Note,"several rules match :",1) ;
			for( JobTgt jt : jts ) req->audit_info(Color::Note,jt->rule->name         ,2) ;
		}
		ri.done_ = ri.goal ;
		if (_may_need_regenerate(*this,ri,make_action)) { prod_idx = NoIdx ; goto Make ; }                               // BACKWARD
	Wakeup :
		SWEAR(done(ri)) ;
		trace("wakeup",ri,conform_idx(),is_plain()?actual_job():Job()) ;
		ri.wakeup_watchers() ;
	Wait :
		if (stop_speculate) _propag_speculate(ri) ;
		trace("done",ri) ;
	}

	void NodeData::_propag_speculate(ReqInfo const& cri) const {
		switch (status()) {
			case NodeStatus::Uphill     :
			case NodeStatus::Transcient : { Node n = dir()         ;             n->propag_speculate( cri.req , cri.speculate ) ; } break ;
			case NodeStatus::Plain      : { Job  j = conform_job() ;             j->propag_speculate( cri.req , cri.speculate ) ; } break ;
			case NodeStatus::Multi      : { for( Job j : conform_job_tgts(cri) ) j->propag_speculate( cri.req , cri.speculate ) ; } break ;
			case NodeStatus::Unknown    :                                                                                           break ; // node is not built, nowhere to propagate
			default :
				SWEAR(status()<NodeStatus::Uphill,status()) ;                                                                               // ensure we have not forgotten a case
		}
	}

	bool/*ok*/ NodeData::forget( bool targets , bool deps ) {
		Trace trace("Nforget",idx(),STR(targets),STR(deps),STR(waiting()),conform_job_tgts()) ;
		if (waiting()) return false ;
		//
		bool    res  = true      ;
		RuleIdx k    = 0         ;
		Prio    prio = -Infinity ;
		for( Job j : job_tgts() ) {
			if (j->rule->prio<prio) break ;  // all jobs above or besides conform job(s)
			res &= j->forget(targets,deps) ;
			if (k==conform_idx()) prio = j->rule->prio ;
			k++ ;
		}
		_set_match_gen(false/*ok*/) ;
		return res ;
	}

	void NodeData::mk_old() {
		Trace trace("mk_old",idx()) ;
		if ( +actual_job() && actual_job()->rule.old() ) actual_job() = {} ; // old jobs may be collected, do not refer to them anymore
		_set_match_gen(false/*ok*/) ;
	}

	void NodeData::mk_no_src() {
		Trace trace("mk_no_src",idx()) ;
		_set_match_gen(false/*ok*/) ;
		fence() ;
		rule_tgts ().clear() ;
		job_tgts  ().clear() ;
		actual_job().clear() ;
		refresh() ;
	}

	void NodeData::mk_src(Buildable b) {
		Trace trace("mk_src",idx()) ;
		buildable = b ;
		fence() ;
		rule_tgts ().clear() ;
		job_tgts  ().clear() ;
		actual_job().clear() ;
		_set_match_gen(true/*ok*/) ;
	}

	void NodeData::mk_src(FileTag tag) {
		Trace trace("mk_src",idx(),tag) ;
		switch (tag) {
			case FileTag::None  : mk_src(Buildable::Anti  ) ;                      break ;
			case FileTag::Dir   : mk_src(Buildable::SrcDir) ;                      break ;
			case FileTag::Empty : mk_src(Buildable::Src   ) ; tag = FileTag::Reg ; break ; // do not remember file is empty, so it is marked new instead of steady/changed when first seen
			default             : mk_src(Buildable::Src   ) ;                      break ;
		}
		refresh(tag) ;
	}

	bool/*modified*/ NodeData::refresh( Crc crc_ , SigDate const& sd ) {
		bool modified = !crc.match(crc_) ;
		//
		Trace trace( "refresh" , STR(modified) , idx() , reqs() , crc ,"->", crc_ , date() ,"->", sd ) ;
		//
		{	Lock lock { s_crc_date_mutex } ;
			if (modified) crc_date(crc_,sd) ;
			else          date() = sd ;
		}
		if (modified) for( Req r : reqs() ) req_info(r).reset(NodeGoal::Status) ; // target is not conform on disk any more
		return modified ;
	}

	static ::pair<Manual,bool/*refreshed*/> _manual_refresh( NodeData& nd , FileSig const& sig ) {
		Manual m = nd.manual(sig) ;
		if (m<Manual::Changed) return {m,false/*refreshed*/} ;      // file was not modified
		if (nd.crc==Crc::None) return {m,false/*refreshed*/} ;      // file appeared, it cannot be steady
		//
		::string ndn = nd.name() ;
		if ( m==Manual::Empty && nd.crc==Crc::Empty ) {             // fast path : no need to open file
			nd.date() = FileSig(ndn) ;
		} else {
			FileSig sig ;
			Crc     crc { sig , ndn , g_config.hash_algo } ;
			if (!nd.crc.match(crc)) return {m,false/*refreshed*/} ; // real modif
			nd.date() = sig ;
		}
		return {Manual::Ok,true/*refreshed*/} ;                     // file is steady
	}
	Manual NodeData::manual_refresh( Req req , FileSig const& sig ) {
		auto [m,refreshed] = _manual_refresh(*this,sig) ;
		if ( refreshed && +req ) req->audit_node(Color::Note,"manual_steady",idx()) ;
		return m ;
	}
	Manual NodeData::manual_refresh( JobData const& j , FileSig const& sig ) {
		auto [m,refreshed] = _manual_refresh(*this,sig) ;
		if (refreshed) for( Req r : j.reqs() ) r->audit_node(Color::Note,"manual_steady",idx()) ;
		return m ;
	}

	//
	// Target
	//

	::ostream& operator<<( ::ostream& os , Target const t ) {
		/**/           os << "T("         ;
		if (+t       ) os << +t           ;
		if (+t.tflags) os <<','<<t.tflags ;
		return         os << ')'          ;
	}


	//
	// Dep
	//

	::ostream& operator<<( ::ostream& os , Dep const& d ) {
		return os << static_cast<DepDigestBase<Node> const&>(d) ;
	}

	::string Dep::accesses_str() const {
		::string res ; res.reserve(N<Access>) ;
		for( Access a : All<Access> ) res.push_back( accesses[a] ? AccessChars[+a] : '-' ) ;
		return res ;
	}

	::string Dep::dflags_str() const {
		::string res ; res.reserve(N<Dflag>) ;
		for( Dflag df : All<Dflag> ) res.push_back( dflags[df] ? DflagChars[+df].second : '-' ) ;
		return res ;
	}

	//
	// Deps
	//

	::ostream& operator<<( ::ostream& os , DepsIter::Digest const& did ) {
		return os <<'('<< did.hdr <<','<< did.i_chunk <<')'  ;
	}

	static void _append_dep( ::vector<GenericDep>& deps , Dep const& dep , size_t& hole ) {
		bool can_compress = dep.is_crc && +dep.accesses && dep.crc()==Crc::None && !dep.dflags && !dep.parallel ;
		if (hole==Npos) {
			if (can_compress) {                                                                       // create new open chunk
				/**/ hole                         = deps.size()             ;
				Dep& hdr                          = deps.emplace_back().hdr ;
				/**/ hdr.sz                       = 1                       ;
				/**/ hdr.chunk_accesses           = dep.accesses            ;
				/**/ deps.emplace_back().chunk[0] = dep                     ;
			} else {                                                                                  // create a chunk just for dep
				deps.push_back(dep) ;
			}
		} else {
			Dep& hdr = deps[hole].hdr ;
			if ( can_compress && dep.accesses==hdr.chunk_accesses && hdr.sz<lsb_msk(Dep::NSzBits) ) { // append dep to open chunk
				uint8_t i = hdr.sz%GenericDep::NodesPerDep ;
				if (i==0) deps.emplace_back() ;
				deps.back().chunk[i] = dep ;
				hdr.sz++ ;
			} else {                                                                                  // close chunk : copy dep to hdr, excetp sz and chunk_accesses fields
				uint8_t  sz                 = hdr.sz             ;
				Accesses chunk_accesses     = hdr.chunk_accesses ;
				/**/     hdr                = dep                ;
				/**/     hdr.sz             = sz                 ;
				/**/     hdr.chunk_accesses = chunk_accesses     ;
				/**/     hole               = Npos               ;
			}
		}
	}
	static void _fill_hole(GenericDep& hdr) {
		SWEAR(hdr.hdr.sz!=0) ;
		uint8_t  sz                     = hdr.hdr.sz-1                                                 ;
		Accesses chunk_accesses         = hdr.hdr.chunk_accesses                                       ;
		/**/     hdr.hdr                = { (&hdr)[1].chunk[sz] , hdr.hdr.chunk_accesses , Crc::None } ;
		/**/     hdr.hdr.sz             = sz                                                           ;
		/**/     hdr.hdr.chunk_accesses = chunk_accesses                                               ;
	}
	static void _fill_hole( ::vector<GenericDep>& deps , size_t hole ) {
		if (hole==Npos) return ;
		GenericDep& d = deps[hole] ;
		_fill_hole(d) ;
		if (d.hdr.sz%GenericDep::NodesPerDep==0) deps.pop_back() ;
	}

	Deps::Deps(::vmap<Node,Dflags> const& deps , Accesses accesses , bool parallel ) {
		::vector<GenericDep> ds   ;        ds.reserve(deps.size()) ;                   // reserving deps.size() is comfortable and guarantees no reallocaiton
		size_t               hole = Npos ;
		for( auto const& [d,df] : deps ) _append_dep( ds , {d,accesses,df,parallel} , hole ) ;
		_fill_hole(ds,hole) ;
		*this = {ds} ;
	}

	Deps::Deps( ::vector<Node> const& deps , Accesses accesses , Dflags dflags , bool parallel ) {
		::vector<GenericDep> ds   ;        ds.reserve(deps.size()) ;                               // reserving deps.size() is comfortable and guarantees no reallocaiton
		size_t               hole = Npos ;
		for( auto const& d : deps ) _append_dep( ds , {d,accesses,dflags,parallel} , hole ) ;
		_fill_hole(ds,hole) ;
		*this = {ds} ;
	}

	void Deps::assign(::vector<Dep> const& deps) {
		::vector<GenericDep> ds   ;        ds.reserve(deps.size()) ; // reserving deps.size() is comfortable and guarantees no reallocaiton
		size_t               hole = Npos ;
		for( auto const& d : deps ) _append_dep( ds , d , hole ) ;
		_fill_hole(ds,hole) ;
		DepsBase::assign(ds) ;
	}

	void Deps::replace_tail( DepsIter it , ::vector<Dep> const& deps ) {
		// close current chunk
		GenericDep* cur_dep = const_cast<GenericDep*>(it.hdr) ;
		cur_dep->hdr.sz = it.i_chunk ;
		if (it.i_chunk!=0) {
			_fill_hole(*cur_dep) ;
			cur_dep = cur_dep->next() ;
		}
		// create new tail
		::vector<GenericDep> ds   ;
		size_t               hole = Npos ;
		for( auto const& d : deps ) _append_dep( ds , d , hole ) ;
		_fill_hole(ds,hole) ;
		// splice it
		NodeIdx tail_sz = items()+DepsBase::size()-cur_dep ;
		if (ds.size()<=tail_sz) {
			for( GenericDep const& d : ds ) *cur_dep++ = d ;                               // copy all
			shorten_by(tail_sz-ds.size()) ;                                                // and shorten
		} else {
			for( GenericDep const& d : ::vector_view(ds.data(),tail_sz) ) *cur_dep++ = d ; // copy what can be fitted
			append(::vector_view( &ds[tail_sz] , ds.size()-tail_sz ) ) ;                   // and append for the remaining
		}
	}

}
