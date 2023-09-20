// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "time.hh"
#include "rpc_job.hh"

#include "core.hh"

using namespace Disk ;

namespace Engine {

	//
	// jobs thread
	//

	// we want to unlink dir knowing that :
	// - create_dirs will be created, so no need to unlink them
	// - keep_enclosing_dirs must be kept, together with all its recursive children
	// result is reported through in/out param to_del_dirs that is used to manage recursion :
	// - on the way up we notice that we hit a create_dirs to avoid unlinking a dir that will have to be recreated
	// - if we hit a keep_enclosing_dirs, we bounce down with a false return value saying that we must not unlink anything
	// - on the way down, we accumulate to to_del_dirs dirs if we did not bounce on a keep_enclosing_dirs and we are not a father of a create_dirs
	static bool/*ok*/ _acc_to_del_dirs( ::set_s& to_del_dirs , ::umap_s<NodeIdx> const& keep_enclosing_dirs , ::set_s const& create_dirs , ::string const& dir , bool keep=false ) {
		if (dir.empty()                      ) return true  ;                  // bounce at root, accumulating to to_del_dirs on the way down
		if (to_del_dirs        .contains(dir)) return true  ;                  // return true to indicate that above has already been analyzed and is ok, propagate downward
		if (keep_enclosing_dirs.contains(dir)) return false ;                  // return false to indicate that nothing must be unlinked here and below , propagate downward
		//
		keep |= create_dirs.contains(dir) ;                                    // set keep     to indicate that nothing must be unlinked here and above , propagate upward
		//
		if ( !_acc_to_del_dirs( to_del_dirs , keep_enclosing_dirs , create_dirs , dir_name(dir) , keep ) ) return false ;
		//
		if (!keep) to_del_dirs.insert(dir) ;
		return true ;
	}

	::pair<vector_s,vector<Node>/*report*/> Job::targets_to_wash(Rule::SimpleMatch const& match) const {
		Rule           rule      = (*this)->rule ;
		::vector<Node> to_report ;
		::vector_s     to_wash   ;
		// handle static targets
		::vector_view_c_s sts = match.static_targets() ;
		for( VarIdx ti=0 ; ti<sts.size() ; ti++ ) {
			Node target{sts[ti]} ;
			if (target->crc==Crc::None) continue ;                             // no interest to wash file if it does not exist
			Tflags tf = rule->tflags(ti) ;
			if (tf[Tflag::Incremental]) continue ;                             // keep file for incremental targets
			//
			if ( !target->has_actual_job(*this) && target->has_actual_job() && tf[Tflag::Warning] ) to_report.push_back(target ) ;
			/**/                                                                                    to_wash  .push_back(sts[ti]) ;
		}
		// handle star targets
		Rule::FullMatch fm ;                                                   // lazy evaluated, if we find any target to_report
		for( Target t : (*this)->star_targets ) {
			if (t->crc==Crc::None) continue ;                                  // no interest to wash file if it does not exist
			::string tn ;                                                      // lazy evaluated
			if (t.lazy_tflag(Tflag::Incremental,match,fm,tn)) continue ;       // may solve fm & tn lazy evalution
			//
			bool has_other_actual_job = !t->has_actual_job(*this) && t->has_actual_job() ;
			if ( has_other_actual_job && t.lazy_tflag(Tflag::Warning,match,fm,tn) ) to_report.push_back(t) ; // may solve fm & tn lazy evalution
			//
			if (tn.empty()) tn = t.name() ;                                    // solve lazy evaluation if not already done
			to_wash.push_back(t.name()) ;
		}
		return {to_wash,to_report} ;
	}

	::vector<Node>/*report*/ Job::wash(Rule::SimpleMatch const& match) const {
		Trace trace("wash") ;
		::vector<Node> to_report ;
		::vector_s     to_wash   ;
		// compute targets to wash
		tie(to_wash,to_report) = targets_to_wash(match) ;
		// remove old_targets
		::set_s       to_del_dirs   ;                                          // ordered to ensure to_del_dirs are removed deepest first
		::vector_s    to_mk_dirs    = match.target_dirs()    ;
		::set_s       to_mk_dir_set = mk_set(to_mk_dirs)     ;                 // uncomfortable on how a hash tab may work with repetitive calls to begin/erase, safer with a set
		::unique_lock lock          { _s_target_dirs_mutex } ;
		for( ::string const& t : to_wash ) {
			trace("unlink_target",t) ;
			//vvvvvvv
			unlink(t) ;
			//^^^^^^^
			_acc_to_del_dirs( to_del_dirs , _s_target_dirs , to_mk_dir_set , dir_name(t) ) ; // _s_target_dirs must protect all dirs beneath it
		}
		// create target dirs
		while (to_mk_dir_set.size()) {
			auto dir = to_mk_dir_set.cbegin() ;                                // process by starting top most : as to_mk_dirs is ordered, parent necessarily appears before child
			//    vvvvvvvvvvvvvvvvvvvvvvvv
			if (::mkdir(dir->c_str(),0755)==0) {
			//    ^^^^^^^^^^^^^^^^^^^^^^^^
				to_mk_dir_set.erase(dir) ;                                     // created, ok
			} else if (errno==EEXIST) {
				if      (is_dir(*dir)                           ) to_mk_dir_set.erase(dir)  ;                            // already exists, ok
				else if (Node(*dir).manual_ok_refresh(*this)==No) throw to_string("must unlink but is manual : ",*dir) ;
				else                                              ::unlink(dir->c_str()) ;                               // exists but is not a dir : unlink file and retry
			} else {
				::string parent = dir_name(*dir) ;
				swear_prod( (errno==ENOENT||errno==ENOTDIR) && !parent.empty() , "cannot create dir ",*dir ) ; // if ENOTDIR, a parent dir is not a dir, it will be fixed up
				to_mk_dir_set.insert(::move(parent)) ;                                                         // retry after parent is created
			}
		}
		// remove containing dirs accumulated in to_del_dirs
		::uset_s not_empty_dirs ;
		for( auto it=to_del_dirs.rbegin() ; it!=to_del_dirs.crend() ; it++ ) { // proceed in reverse order to guarantee subdirs are seen first
			::string const& dir = *it ;
			if (not_empty_dirs.contains(dir)) continue ;
			//         vvvvvvvvvvvvvvvvv
			if      (::rmdir(dir.c_str())==0) { trace("unlink_dir"          ,dir) ; }
			//         ^^^^^^^^^^^^^^^^^
			else if (errno==ENOENT          ) { trace("dir_already_unlinked",dir) ; }
			else                              { trace("dir_not_empty"       ,dir) ;
				for( ::string d=dir_name(dir) ; !d.empty() ; d=dir_name(d) ) { // no hope to unlink a dir if a sub-dir still exists
					if (not_empty_dirs.contains(d)) break ;                    // enclosing dirs are already recorded, no need to proceed
					not_empty_dirs.insert(d) ;
				}
			}
		}
		for( ::string const& dir : to_mk_dirs ) { trace("create_dir",dir) ; _s_target_dirs[dir]++ ; } // update _target_dirs once we are sure job will start
		return to_report ;
	}

	void Job::end_exec() const {
		::unique_lock lock(_s_target_dirs_mutex) ;
		for( ::string const& d : simple_match().target_dirs() ) {
			auto it = _s_target_dirs.find(d) ;
			SWEAR(it!=_s_target_dirs.end()) ;
			if (it->second==1) _s_target_dirs.erase(it) ;
			else               it->second--             ;
		}
	}

	//
	// main thread
	//

	//
	// JobTgts
	//

	::ostream& operator<<( ::ostream& os , JobTgts const jts ) {
		return os<<jts.view() ;
	}

	//
	// JobReqInfo
	//

	::ostream& operator<<( ::ostream& os , JobReqInfo const& ri ) {
		return os<<"JRI(" << ri.req <<','<< ri.action <<','<< ri.lvl<<':'<<ri.dep_lvl <<','<< ri.n_wait <<')' ;
	}

	//
	// Job
	//

	::ostream& operator<<( ::ostream& os , Job const j ) {
		os << "J(" ;
		if (+j) os << +j ;
		return os << ')' ;
	}
	::ostream& operator<<( ::ostream& os , JobTgt const jt ) {
		if (!jt) return os << "JT()" ;
		os << "JobTgt(" << Job(jt) ;
		if (jt.is_sure()) os << ",sure" ;
		return os << ')' ;
	}
	::ostream& operator<<( ::ostream& os , JobExec const je ) {
		if (!je) return os << "JT()" ;
		os << "JobExec(" << Job(je) ;
		if (!je.host.empty()) os <<','<< je.host ;
		return os <<','<< je.start <<')' ;
	}

	::shared_mutex    Job::_s_target_dirs_mutex ;
	::umap_s<NodeIdx> Job::_s_target_dirs       ;

	Job::Job( RuleTgt rule_tgt , ::string const& target , Req req , DepDepth lvl ) {
		Trace trace("Job",rule_tgt,target,lvl) ;
		Rule::FullMatch match{rule_tgt,target} ;
		if (!match) { trace("no_match") ; return ; }
		::vmap_s<AccDflags> dep_names ;
		try {
			dep_names = mk_val_vector(rule_tgt->create_match_attrs.eval(match)) ;
		} catch (::string const& e) {
			trace("no_dep_subst") ;
			if (+req) {
				req->audit_job(Color::Note,"no_deps",rule_tgt,match.user_name()) ;
				req->audit_stderr({{rule_tgt->create_match_attrs.s_exc_msg(false/*using_static*/),{}}},e,-1,1) ;
			}
			return ;
		}
		::vmap<Node,AccDflags> deps ; deps.reserve(dep_names.size()) ;
		for( auto [dn,af] : dep_names ) {
			Node d{dn} ;
			//vvvvvvvvvvvvvvvvvv
			d.set_buildable(lvl) ;
			//^^^^^^^^^^^^^^^^^^
			if (d->buildable==No) { trace("no_dep",d) ; return ; }
			deps.emplace_back(d,af) ;
		}
		//      vvvvvvvvvvvvvvvvv
		*this = Job(
			match.name() , Dflt                                                // args for store
		,	rule_tgt , Deps(deps)                                              // args for JobData
		) ;
		//^^^^^^^^^^^^^^^^^^^^^^^
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		try {
			(*this)->tokens1 = rule_tgt->create_none_attrs.eval(*this,match).tokens1 ;
		} catch (::string const& e) {
			(*this)->tokens1 = rule_tgt->create_none_attrs.spec.tokens1 ;
			req->audit_job(Color::Note,"dynamic",*this) ;
			req->audit_stderr({{rule_tgt->create_none_attrs.s_exc_msg(true/*using_static*/),{}}},e,-1,1) ;
		}
		trace("found",*this) ;
	}

	::string Job::ancillary_file(AncillaryTag tag) const {
		::string str        = to_string('0',+*this) ;                          // ensure size is even as we group by 100
		bool     skip_first = str.size()&0x1        ;                          // need initial 0 if required to have an even size
		size_t   i          ;
		::string res        ;
		switch (tag) {
			case AncillaryTag::Data    : res = g_config.local_admin_dir + "/job_data" ; break ;
			case AncillaryTag::KeepTmp : res = AdminDir + "/job_keep_tmp"s            ; break ;
			default : FAIL(tag) ;
		}
		res.reserve( res.size() + str.size() + str.size()/2 + 1 ) ;                                // 1.5*str.size() as there is a / for 2 digits + final _
		for( i=skip_first ; i<str.size()-1 ; i+=2 ) { res.push_back('/') ; res.append(str,i,2) ; } // create a dir hierarchy with 100 files at each level
		res.push_back('_') ;                                                                       // avoid name clashes with directories
		return res ;
	}

	::vector<Req> Job::running_reqs() const {                                               // sorted by start
		::vector<Req> res ; res.reserve(Req::s_n_reqs()) ;                                  // pessimistic, so no realloc
		for( Req r : Req::s_reqs_by_start ) if (c_req_info(r).running()) res.push_back(r) ;
		return res ;
	}

	::vector<Req> Job::old_done_reqs() const {                                 // sorted by start
		::vector<Req> res ; res.reserve(Req::s_n_reqs()) ;                     // pessimistic, so no realloc
		for( Req r : Req::s_reqs_by_start ) {
			if (c_req_info(r).running()) break ;
			if (c_req_info(r).done()   ) res.push_back(r) ;
		}
		return res ;
	}

	void JobExec::premature_end( Req req , bool report ) {
		Trace trace("premature_end",*this,req,STR(report)) ;
		ReqInfo& ri = req_info(req) ;
		make( ri , RunAction::None , {}/*reason*/ , MakeAction::PrematureEnd ) ;
		if (report) req->audit_job(Color::Note,"continue",*this) ;
		req.chk_end() ;
	}

	void JobExec::not_started() {
		Trace trace("not_started",*this) ;
		for( Req req : running_reqs() ) premature_end(req,false/*report*/) ;
	}

	::pair_s<NodeIdx> Job::s_reason_str(JobReason reason) {
		if (reason.tag<JobReasonTag::HasNode) return { JobReasonTagStrs[+reason.tag] , 0            } ;
		else                                  return { JobReasonTagStrs[+reason.tag] , +reason.node } ;
	}

	// answer to job execution requests
	JobRpcReply JobExec::job_info( JobProc proc , ::vector<Node> const& deps ) const {
		::vector<Req> reqs = running_reqs() ;
		Trace trace("job_info",proc,deps.size()) ;
		if (reqs.empty()) return proc ;                                        // if job is not running, it is too late
		//
		switch (proc) {
			case JobProc::DepInfos : {
				::vector<pair<Bool3/*ok*/,Crc>> res ; res.reserve(deps.size()) ;
				for( Node dep : deps ) {
					for( Req req : reqs ) {
						// we need to compute crc if it can be done immediately, as is done in make
						// or there is a risk that the job is not rerun if dep is remade steady and leave a bad crc leak to the job
						dep.make( dep.c_req_info(req) , RunAction::Status ) ;  // XXX : avoid actually launching jobs if it is behind a critical modif
						trace("dep_info",dep,req) ;
					}
					Bool3 ok ;
					if      (!dep->actual_job_tgt                                 ) ok = Maybe ;
					else if ( dep->actual_job_tgt->run_status!=RunStatus::Complete) ok = Maybe ;
					else if ( dep->actual_job_tgt->status<=Status::Garbage        ) ok = Maybe ;
					else if ( dep->actual_job_tgt->status>=Status::Err            ) ok = No    ;
					else                                                            ok = Yes   ;
					res.emplace_back(ok,dep->crc) ;
				}
				return {proc,res} ;
			}
			case JobProc::ChkDeps : {
				bool err = false ;
				for( Node dep : deps ) {
					for( Req req : reqs ) {
						// we do not need dep for our purpose, but it will soon be necessary, it is simpler just to call plain make()
						// use Dsk as we promess file is available
						NodeReqInfo const& cdri = dep.make( dep.c_req_info(req) , RunAction::Dsk ) ; // XXX : avoid actually launching jobs if it is behind a critical modif
						// if dep is waiting for any req, stop analysis as it is complicated what we want to rebuild after
						// and there is no loss of parallelism as we do not wait for completion before doing a full analysis in make()
						if (cdri.waiting()) { trace("waiting",dep) ; return {proc,Maybe} ; }
						bool dep_err = dep.err(cdri) ;
						err |= dep_err ;
						trace("chk_dep",dep,req,STR(dep_err)) ;
					}
				}
				trace("done",STR(err)) ;
				return {proc,Yes&!err} ;
			}
			default : FAIL(proc) ;
		}
	}

	void JobExec::live_out(::string const& txt) const {
		for( Req r : running_reqs() ) {
			ReqInfo& ri = req_info(r) ;
			if (!ri.live_out) continue ;
			report_start(ri) ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			r->audit_info(Color::None,txt,1) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
	}

	void JobExec::report_start( ReqInfo& ri , ::vector<Node> const& report_unlink , ::string const& txt ) const {
		if ( ri.start_reported ) {
			SWEAR(report_unlink.empty()) ;
			return ;
		}
		ri.req->audit_job(Color::HiddenNote,"start",*this) ;
		size_t w = 0 ;
		for( Node t : report_unlink ) w = ::max( w , t->actual_job_tgt->rule->user_name().size() ) ;
		for( Node t : report_unlink ) ri.req->audit_node( Color::Warning , to_string("unlinked target (generated by ",::setw(w),t->actual_job_tgt->rule->user_name(),')') , t , 1 ) ;
		if (!txt.empty()) ri.req->audit_stderr({{(*this)->rule->start_none_attrs.s_exc_msg(true/*using_static*/),{}}},txt,-1,1) ;
		ri.start_reported = true ;
	}
	void JobExec::report_start() const {
		Trace trace("report_start",*this) ;
		for( Req req : running_reqs() ) report_start(req_info(req)) ;
	}

	void JobExec::started( bool report , ::vector<Node> const& report_unlink , ::string const& txt ) {
		Trace trace("started",*this) ;
		SWEAR(!(*this)->rule->is_special()) ;
		for( Req req : running_reqs() ) {
			ReqInfo& ri = req_info(req) ;
			ri.start_reported = false ;
			if ( report || !report_unlink.empty() || !txt.empty() ) report_start(ri,report_unlink,txt) ;
			//
			if (ri.lvl==ReqInfo::Lvl::Queued) {
				req->stats.cur(ReqInfo::Lvl::Queued)-- ;
				req->stats.cur(ReqInfo::Lvl::Exec  )++ ;
				ri.lvl = ReqInfo::Lvl::Exec ;
			}
		}
	}

	bool/*modified*/ JobExec::end( ::vmap_ss const& rsrcs , JobDigest const& digest ) {
		Status            status           = digest.status                                      ; // status will be modified, need to make a copy
		bool              err              = status>=Status::Err                                ;
		bool              killed           = status<=Status::Killed                             ;
		JobReason         local_reason     = killed ? JobReasonTag::Killed : JobReasonTag::None ;
		bool              any_modified     = false                                              ;
		Rule              rule             = (*this)->rule                                      ;
		::vector<Req>     running_reqs_    = running_reqs()                                     ;
		AnalysisErr       analysis_err     ;
		CacheNoneAttrs    cache_none_attrs ;
		EndCmdAttrs       end_cmd_attrs    ;
		Rule::SimpleMatch match            ;
		//
		SWEAR( status!=Status::New && !JobData::s_frozen(status) ) ;           // we just executed the job, it can be neither new nor frozen
		SWEAR(!rule->is_special()) ;
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		try {
			cache_none_attrs = rule->cache_none_attrs.eval(*this,match) ;
		} catch (::string const& e) {
			cache_none_attrs = rule->cache_none_attrs.spec ;
			for( Req req : running_reqs_ ) {
				req->audit_job(Color::Note,"dynamic",*this) ;
				req->audit_stderr({{rule->cache_none_attrs.s_exc_msg(true/*using_static*/),{}}},e,-1,1) ;
			}
		}
		try {
			end_cmd_attrs = rule->end_cmd_attrs.eval(*this,match) ;
		} catch (::string const& e) {
			analysis_err.emplace_back( to_string("cannot compute ",EndCmdAttrs::Msg) , 0 ) ;
		}
		//
		switch (status) {
			case Status::Lost    : local_reason = JobReasonTag::Lost    ; break ;
			case Status::Killed  : local_reason = JobReasonTag::Killed  ; break ;
			case Status::ChkDeps : local_reason = JobReasonTag::ChkDeps ; break ;
			case Status::Garbage :                                        break ; // Garbage is caught as a default message if none other ones is available
			default              : SWEAR(status>Status::Garbage) ;                // ensure we have not forgotten a case
		}
		//
		(*this)->end_date = ProcessDate::s_now()                            ;
		(*this)->status   = status<=Status::Garbage ? status : Status::Lost ;  // ensure we cannot appear up to date while working on data
		fence() ;
		//
		Trace trace("end",*this,status) ;
		//
		// handle targets
		//
		auto report_missing_target = [&](::string const& tn)->void {
			FileInfo fi{tn} ;
			analysis_err.emplace_back( to_string("missing target",(+fi?" (existing)":fi.tag==FileTag::Dir?" (dir)":"")," :") , +Node(tn) ) ;
		} ;
		::uset<Node> seen_static_targets ;

		for( Unode t : (*this)->star_targets ) if (t->has_actual_job(*this)) t->actual_job_tgt.clear() ; // ensure targets we no more generate do not keep pointing to us

		::vector<Target> star_targets ;                                        // typically, there is either no star targets or they are most of them, lazy reserve if one is seen
		for( auto const& [tn,td] : digest.targets ) {
			Tflags tflags = td.tflags                                 ;
			Unode  target { tn }                                      ;
			bool   unlink = td.crc==Crc::None                         ;
			Crc    crc    = td.write || unlink ? td.crc : target->crc ;
			//
			if ( !tflags[Tflag::SourceOk] && td.write && target->is_src() ) {
				err = true ;
				if      (unlink  ) analysis_err.emplace_back("unexpected unlink of source",+target) ;
				else if (td.write) analysis_err.emplace_back("unexpected write to source" ,+target) ;
			}
			if (
				td.write                                                       // we actually wrote
			&&	target->has_actual_job() && !target->has_actual_job(*this)     // there is another job
			&&	target->actual_job_tgt->end_date>start                         // dates overlap, which means both jobs were running concurrently (we are the second to end)
			) {
				Job    aj       = target->actual_job_tgt   ;                   // common_tflags cannot be tried as target may be unexpected for aj
				VarIdx aj_idx   = aj.full_match().idx(tn)  ;                   // this is expensive, but pretty exceptional
				Tflags aj_flags = aj->rule->tflags(aj_idx) ;
				trace("clash",*this,tflags,aj,aj_idx,aj_flags,target) ;
				// /!\ This may be very annoying !
				//     Even completed Req's may have been poluted as at the time t->actual_job_tgt completed, it was not aware of the clash.
				//     Putting target in clash_nodes will generate a frightening message to user asking to relaunch all concurrent commands, even past ones.
				//     Note that once we have detected the frightening situation and warned the user, we do not care masking further clashes by overwriting actual_job_tgt.
				if (tflags  [Tflag::Crc]) local_reason |= {JobReasonTag::ClashTarget,+target} ; // if we care about content, we must rerun
				if (aj_flags[Tflag::Crc]) {                                                     // if actual job cares about content, we may have the annoying case mentioned above
					Rule::SimpleMatch aj_match{aj} ;
					for( Req r : reqs() ) {
						ReqInfo& ajri = aj.req_info(r) ;
						ajri.done_ = ajri.done_ & RunAction::Status ;                             // whether there is clash or not, this job must be rerun if we need the actual files
						for( Node ajt : aj_match.static_targets() ) if (ajt.done(r)) goto Clash ;
						for( Node ajt : aj->star_targets          ) if (ajt.done(r)) goto Clash ;
						continue ;
					Clash :                                                    // one of the targets is done, this is the annoying case
						trace("critical_clash") ;
						r->clash_nodes.insert(target) ;
					}
				}
			}
			if ( !tflags[Tflag::Incremental] && target->read(td.accesses) ) local_reason |= {JobReasonTag::PrevTarget,+target} ;
			if (crc==Crc::None) {
				// if we have written then unlinked, then there has been a transcient state where the file existed
				// we must consider this is a real target with full clash detection.
				// the unlinked bit is for situations where the file has just been unlinked with no weird intermediate, which is a less dangerous situation
				if ( !RuleData::s_sure(tflags) && !td.write ) {
					target->unlinked = target->crc!=Crc::None ;                // if target was actually unlinked, note it as it is not considered a target of the job
					trace("unlink",target,STR(target->unlinked)) ;
					continue ;                                                 // if we are not sure, a target is not generated if it does not exist
				}
				if ( !tflags[Tflag::Star] && !tflags[Tflag::Phony] ) {
					err = true ;
					report_missing_target(tn) ;
				}
			}
			if ( td.write && !unlink && !tflags[Tflag::Write] ) {
				err = true ;
				analysis_err.emplace_back("unexpected write to",+target) ;
			}
			//
			if (tflags[Tflag::Star]) {
				if (star_targets.empty()) star_targets.reserve(digest.targets.size()) ; // solve lazy reserve
				star_targets.emplace_back( target , tflags[Tflag::Unexpected] ) ;
			} else {
				seen_static_targets.insert(target) ;
			}
			//
			bool         modified = false ;
			FileInfoDate fid      { tn }  ;
			if (!td.write) {
				if ( tflags[Tflag::ManualOk] && target.manual_ok(fid)!=Yes ) crc = {tn,g_config.hash_algo} ;
				else                                                         goto NoRefresh ;
			}
			//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			modified = target.refresh( crc , fid.date_or_now() ) ;
			//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		NoRefresh :
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			target->actual_job_tgt = { *this , RuleData::s_sure(tflags) } ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			any_modified |= modified && tflags[Tflag::Match] ;
			trace("target",target,td,STR(modified),status) ;
		}
		if (seen_static_targets.size()<rule->n_static_targets) {               // some static targets have not been seen
			Rule::SimpleMatch match          { *this }                ;        // match must stay alive as long as we use static_targets
			::vector_view_c_s static_targets = match.static_targets() ;
			for( VarIdx t=0 ; t<rule->n_static_targets ; t++ ) {
				::string const&  tn = static_targets[t] ;
				Unode            tu { tn }              ;
				if (seen_static_targets.contains(tu)) continue ;
				Tflags tflags = rule->tflags(t) ;
				tu->actual_job_tgt = { *this , true/*is_sure*/ } ;
				//                               vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				if (!tflags[Tflag::Incremental]) tu.refresh( Crc::None , DiskDate::s_now() ) ; // if incremental, target is preserved, else it has been washed at start time
				//                               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (!tflags[Tflag::Phony]) {
					err = true ;
					if (status==Status::Ok) report_missing_target(tn) ;        // only report if job was ok, else it is quite normal
				}
			}
		}
		::sort(star_targets) ;                                                 // ease search in targets
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		(*this)->star_targets.assign(star_targets) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		//
		// handle deps
		//
		if (!killed) {                                                         // if killed, old deps are better than new ones, if job did not run, we have no deps, not even static deps
			DiskDate      db_date    ;
			::vector<Dep> dep_vector ; dep_vector.reserve(digest.deps.size()) ; // typically, static deps are all accessed
			::uset<Node>  old_deps   = mk_uset<Node>((*this)->deps) ;
			//
			for( auto const& [dn,dd] : digest.deps ) {                         // static deps are guaranteed to appear first
				Node d{dn} ;
				Dep dep{ d , dd.accesses , dd.dflags , dd.parallel } ;
				dep.known = old_deps.contains(d) ;
				if (dd.garbage) { dep.crc     ({}) ; local_reason |= {JobReasonTag::DepNotReady,+dep} ; } // garbage : force unknown crc
				else            { dep.crc_date(dd) ;                                                    } // date will be transformed into crc in make if possible
				trace("dep",dep,dd,dep->db_date()) ;
				dep_vector.emplace_back(dep) ;
				if ( +dd.accesses && !dd.garbage ) db_date = ::max(db_date,d->db_date()) ;
			}
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			(*this)->deps.assign(dep_vector) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (any_modified) (*this)->db_date = db_date ;
		}
		//
		// wrap up
		//
		switch (status) {
			case Status::Ok      : if ( !digest.stderr.empty() && !end_cmd_attrs.allow_stderr ) { analysis_err.emplace_back("non-empty stderr",0) ; err = true ; } break ;
			case Status::Timeout :                                                              { analysis_err.emplace_back("timeout"         ,0) ;              } break ;
			default : ;
		}
		EndNoneAttrs end_none_attrs   ;
		::string     analysis_err_txt ;
		try {
			end_none_attrs = rule->end_none_attrs.eval(*this,match,rsrcs) ;
		} catch (::string const& e) {
			end_none_attrs = rule->end_none_attrs.spec ;
			analysis_err.emplace_back(rule->end_none_attrs.s_exc_msg(true/*using_static*/),0) ;
			analysis_err_txt = e ;
			if ( !analysis_err_txt.empty() && analysis_err_txt.back()!='\n' ) analysis_err_txt.push_back('\n') ;
		}
		//
		(*this)->exec_ok(true) ;                                               // effect of old cmd has gone away with job execution
		fence() ;
		//                      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if      (+local_reason) (*this)->status = ::min(status,Status::Garbage) ; // only update status once every other info is set in case of crash and avoid transforming garbage into Err
		else if (err          ) (*this)->status = ::max(status,Status::Err    ) ; // .
		else                    (*this)->status =       status                  ; // .
		//                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		bool        report_stats     = status==Status::Ok              ;
		CoarseDelay old_exec_time    = (*this)->best_exec_time().first ;
		bool        cached           = false                           ;
		bool        analysis_stamped = false                           ;
		if (report_stats) {
			SWEAR(+digest.stats.total) ;
			(*this)->exec_time = digest.stats.total ;
			rule.new_job_exec_time( digest.stats.total , (*this)->tokens1 ) ;
		}
		for( Req req : running_reqs_ ) {
			ReqInfo& ri = req_info(req) ;
			SWEAR(ri.lvl==JobLvl::Exec) ;                                      // update statistics if this does not hold
			ri.lvl = JobLvl::End ;                                             // we must not appear as Exec while other reqs are analysing or we will wrongly think job is on going
		}
		for( Req req : running_reqs_ ) {
			ReqInfo& ri = req_info(req) ;
			trace("req_before",local_reason,status,ri) ;
			req->missing_audits.erase(*this) ;                                 // old missing audit is obsolete as soon as we have rerun the job
			// we call wakeup_watchers ourselves once reports are done to avoid anti-intuitive report order
			//                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			JobReason reason = make( ri , RunAction::Status , local_reason , MakeAction::End , &old_exec_time , false/*wakeup_watchers*/ ) ;
			//                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (status<=Status::Garbage) reason |= JobReasonTag::Garbage ;                                                                 // default message
			AnalysisErr        ae_reason ;                                                                                                 // we need a variable to own the data
			AnalysisErr const& ae        = reason.err() ? (ae_reason={s_reason_str(reason)}) : analysis_err ;
			if (ri.done()) {
				audit_end(
					{}
				,	ri
				,	reason.err() ? analysis_err_txt : analysis_err_txt.empty() ? digest.stderr : analysis_err_txt+digest.stderr // avoid concatenation unless necessary
				,	ae
				,	end_none_attrs.stderr_len
				,	any_modified
				,	digest.stats.total                                         // report exec time even if not recording it
				) ;
				trace("wakeup_watchers",ri) ;
				// it is not comfortable to store req-dependent info in a req-independent place, but we need reason from make()
				if ( !ae.empty() && !analysis_stamped ) {                      // this is code is done in such a way as to be fast in the common case (ae empty)
					::string jaf = ancillary_file() ;
					try {
						IFStream is{jaf} ;
						auto report_start = deserialize<JobInfoStart>(is) ;
						auto report_end   = deserialize<JobInfoEnd  >(is) ;
						//
						report_end.end.digest.analysis_err = ae ;
						//
						OFStream os{jaf} ;
						serialize(os,report_start) ;
						serialize(os,report_end  ) ;
					}
					catch (...) {}                                             // in case ancillary file cannot be read, dont record and ignore
					analysis_stamped = true ;
				}
				// it is not comfortable to store req-dependent info in a req-independent place, but we need to ensure job is done
				if ( !cache_none_attrs.key.empty() && !cached && (*this)->run_status==RunStatus::Complete && status==Status::Ok ) { // cache only successful results
					Cache::s_tab.at(cache_none_attrs.key)->upload( *this , digest ) ;
					cached = true ;
				}
				ri.wakeup_watchers() ;
			} else {
				audit_end( +local_reason?"":"may_" , ri , analysis_err_txt , {s_reason_str(reason)} , -1/*stderr_len*/ , any_modified , digest.stats.total ) ; // report 'rerun' rather than status
				req->missing_audits[*this] = { false/*hit*/ , any_modified , ae } ;
			}
			trace("req_after",ri) ;
			req.chk_end() ;
		}
		trace("summary",*this) ;
		return any_modified ;
	}

	void JobExec::audit_end( ::string const& pfx , ReqInfo const& cri , ::string const& stderr , AnalysisErr const& analysis_err , size_t stderr_len , bool modified , Delay exec_time ) const {
		Req            req  = cri.req            ;
		::string       step ;
		Color          c    = Color::Ok          ;
		JobReport      jr   = JobReport::Unknown ;
		JobData const& jd   = **this             ;
		if (req->zombie) {
			if (jd.status<=Status::Garbage) { step = mk_snake(jd.status) ; c = Color::Err  ; }
			else                            { step = "completed"         ; c = Color::Note ; }
		} else {
			if (jd.status==Status::Killed) { step = mk_snake(jd.status) ; c = Color::Err  ; }
			else {
				if      (!cri.done()                       ) { jr = JobReport::Rerun                           ; step = mk_snake(jr           ) ;                      c = Color::Note    ; }
				else if (jd.run_status!=RunStatus::Complete) { jr = JobReport::Failed                          ; step = mk_snake(jd.run_status) ;                      c = Color::Err     ; }
				else if (jd.status    ==Status   ::Timeout ) { jr = JobReport::Failed                          ; step = mk_snake(jd.status    ) ;                      c = Color::Err     ; }
				else if (jd.err()                          ) { jr = JobReport::Failed                          ; step = mk_snake(jr           ) ;                      c = Color::Err     ; }
				else                                         { jr = modified?JobReport::Done:JobReport::Steady ; step = mk_snake(jr           ) ; if (!stderr.empty()) c = Color::Warning ; }
				//
				if (+exec_time) {                                                  // if no exec time, no job was actually run
					req->stats.ended(jr)++ ;
					req->stats.jobs_time[cri.done()/*useful*/] += exec_time ;
				}
			}
		}
		if (!pfx.empty()) step = pfx+step ;
		Trace trace("audit_end",c,step,*this,cri,STR(modified)) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		req->audit_job(c,step,*this,exec_time) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (jr==JobReport::Unknown) return ;
		req->audit_stderr(analysis_err,stderr,stderr_len,1) ;
	}

	void Job::_set_pressure_raw(ReqInfo& ri , CoarseDelay pressure ) const {
		Trace("set_pressure",*this,ri,pressure) ;
		Req         req          = ri.req                                        ;
		CoarseDelay dep_pressure = ri.pressure + (*this)->best_exec_time().first ;
		switch (ri.lvl) {
			//                                                                        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case ReqInfo::Lvl::Dep    : for( Dep const& d : (*this)->deps.subvec(ri.dep_lvl) ) d.         set_pressure( d.req_info(req) ,                            dep_pressure  ) ; break ;
			case ReqInfo::Lvl::Queued :                                                        Backend::s_set_pressure( ri.backend      , +*this , +req , {.pressure=dep_pressure} ) ; break ;
			//                                                                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			default : ;
		}
	}

	ENUM(State
	,	Ok
	,	DanglingModif                  // modified dep has been seen but still processing parallel deps
	,	Modif
	,	Err
	,	MissingStatic
	)

	static inline bool _inc_cur( Req req , JobLvl jl , int inc) {
		if (jl==JobLvl::None) return false ;
		JobIdx& stat = req->stats.cur(jl==JobLvl::End?JobLvl::Exec:jl) ;
		if (inc<0) SWEAR(stat>=JobIdx(-inc)) ;
		stat += inc ;
		return jl!=JobLvl::Done ;
	}
	JobReason Job::_make_raw( ReqInfo& ri , RunAction run_action , JobReason reason , MakeAction make_action , CoarseDelay const* old_exec_time , bool wakeup_watchers ) {
		using Lvl = ReqInfo::Lvl ;
		SWEAR(!reason.err()) ;
		Lvl  before_lvl = ri.lvl        ;                                      // capture previous state before any update
		Req  req        = ri.req        ;
		Rule rule       = (*this)->rule ;
		ri.update( run_action , make_action , *this ) ;
		if (!ri.waiting()) {                                                   // we may have looped in which case stats update is meaningless and may fail()
			//
			Special special      = rule->special                                                 ;
			bool    dep_live_out = special==Special::Req && req->options.flags[ReqFlag::LiveOut] ;
			//
			Trace trace("Jmake",*this,ri,before_lvl,run_action,reason,make_action,old_exec_time?*old_exec_time:CoarseDelay(),STR(wakeup_watchers)) ;
			if (ri.done(ri.action)) goto Wakeup ;
			for (;;) {                                                                             // loop in case analysis must be restarted (only in case of flash execution)
				State       state        = State::Ok                                             ;
				bool        sure         = rule->is_sure()                                       ; // if rule is not sure, it means targets are never sure
				CoarseDelay dep_pressure = ri.pressure + (*this)->best_exec_time().first         ;
				Idx         n_deps       = special==Special::Infinite ? 0 : (*this)->deps.size() ; // special case : Infinite actually has no dep, just a list of node showing infinity
				//
				RunAction dep_action = req->options.flags[ReqFlag::Archive] ? RunAction::Dsk : RunAction::Status ;
				//
				Status status = (*this)->status ;
				if (status<=Status::Garbage) ri.action = RunAction::Run ;
				//
				if (make_action==MakeAction::End) { dep_action = RunAction::Dsk ; ri.dep_lvl  = 0 ; } // if analysing end of job, we need to be certain of presence of all deps on disk
				if (ri.action  ==RunAction::Run ) { dep_action = RunAction::Dsk ;                   } // if we must run the job , .
				//
				switch (ri.lvl) {
					case Lvl::None :
						if (ri.action>=RunAction::Status) {                                                                      // only once, not in case of analysis restart
							if ( rule->force || (req->options.flags[ReqFlag::ForgetOldErrors]&&(*this)->status>=Status::Err) ) {
								ri.action   = RunAction::Run                                           ;
								dep_action  = RunAction::Dsk                                           ;
								reason     |= rule->force ? JobReasonTag::Force : JobReasonTag::OldErr ;
							} else if (JobData::s_frozen(status)) {
								ri.action = RunAction::Run ;                   // ensure crc are updated, akin sources
							}
						}
						ri.lvl = Lvl::Dep ;
						if (JobData::s_frozen(status)) break ;
					/*fall through*/
					case Lvl::Dep : {
					RestartAnalysis :                                                                    // restart analysis here when it is discovered we need deps to run the job
						if ( ri.dep_lvl==0 && !(*this)->exec_ok() ) {                                    // process command like a dep in parallel with static_deps
							SWEAR(state==State::Ok) ;                                                    // did not have time to be anything else
							state       = State::DanglingModif                                         ;
							reason     |= !(*this)->cmd_ok() ? JobReasonTag::Cmd : JobReasonTag::Rsrcs ;
							ri.action   = RunAction::Run                                               ;
							dep_action  = RunAction::Dsk                                               ;
							trace("new_cmd") ;
						}
						bool critical_modif   = false ;
						bool critical_waiting = false ;
						Dep  sentinel ;
						for ( NodeIdx i_dep = ri.dep_lvl ; SWEAR(i_dep<=n_deps),true ; i_dep++ ) {
							State dep_state   = State::Ok                                  ;
							bool  seen_all    = i_dep==n_deps                              ;
							Dep&  dep         = seen_all ? sentinel : (*this)->deps[i_dep] ; // use empty dep as sentinel
							bool  is_static   =  dep.dflags[Dflag::Static     ]            ;
							bool  is_critical =  dep.dflags[Dflag::Critical   ]            ;
							bool  sense_err   = !dep.dflags[Dflag::IgnoreError]            ;
							bool  required    =  dep.dflags[Dflag::Required   ]            ;
							bool  care        = +dep.accesses                              ; // we care about this dep if we access it somehow
							//
							if (!dep.parallel) {
								if (state==State::DanglingModif) state = State::Modif ; // dangling modifs become modifs when stamped by a sequential dep
								if ( critical_modif && !seen_all ) {
									NodeIdx j = i_dep ;
									for( NodeIdx i=i_dep ; i<n_deps ; i++ ) {         // suppress deps following modified critical one, except keep static deps as no-access
										if ((*this)->deps[i].dflags[Dflag::Static]) {
											Dep& d = (*this)->deps[j++] ;
											d         = (*this)->deps[i] ;
											d.accesses = Accesses::None  ;
										}
									}
									if (j!=n_deps) {
										(*this)->deps.shorten_by(n_deps-j) ;
										n_deps   = j             ;
										seen_all = i_dep==n_deps ;
									}
								}
								if ( state==State::Ok && !ri.waiting() ) ri.dep_lvl = i_dep ; // fast path : all is ok till now, next time, restart analysis after this
								if ( critical_waiting                  ) goto Wait ;          // stop analysis as critical dep may be modified
								if ( seen_all                          ) break     ;          // we are done
							}
							SWEAR( is_static <= required ) ;                          // static deps are necessarily required
							Node::ReqInfo const* cdri        = &dep.c_req_info(req) ; // avoid allocating req_info as long as not necessary
							bool                 overwritten = false                ;
							//
							if ( !care && !required ) {                        // dep is useless
								SWEAR(special==Special::Infinite) ;            // this is the only case
								goto Continue ;
							}
							if (!cdri->waiting()) {
								dep.acquire_crc() ;                                          // 1st chance : before calling make as it can be destroyed in case of flash execution
								ri.n_wait++ ;                                                // appear waiting in case of recursion loop (loop will be caught because of no job on going)
								if (dep_live_out) {                                          // ask live output for last level if user asked it
									Node::ReqInfo& dri = dep.req_info(*cdri) ; cdri = &dri ; // refresh cdri in case dri allocated a new one
									dri.live_out = true ;
								}
								//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								if      (care     ) cdri = &dep.make( *cdri , dep_action         ) ; // refresh cdri if make changed it
								else if (sense_err) cdri = &dep.make( *cdri , RunAction::Status  ) ; // .
								else                cdri = &dep.make( *cdri , RunAction::Makable ) ; // .
								//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
								ri.n_wait-- ;                                                      // restore
							}
							if ( is_static && dep->buildable!=Yes ) sure = false ; // buildable is better after make()
							if (cdri->waiting()) {
								reason |= {JobReasonTag::DepNotReady,+dep} ;
								Node::ReqInfo& dri = dep.req_info(*cdri) ; cdri = &dri ; // refresh cdri in case dri allocated a new one
								dep.add_watcher(dri,*this,ri,dep_pressure) ;
								critical_waiting |= is_critical ;
								goto Continue ;
							}
							{	SWEAR(dep.done(*cdri)) ;                       // after having called make, dep must be either waiting or done
								dep.acquire_crc() ;                            // 2nd chance : after make is called as if dep is steady (typically a source), crc may have been computed
								bool is_modif = !dep.up_to_date() ;
								if ( is_modif                          ) dep_state = State::DanglingModif ; // if not overridden by an error
								if ( !is_static && state>=State::Modif ) goto Continue ;                    // if not static, maybe all the following errors will be washed by previous modif
								//
								bool makable = dep->makable(special==Special::Uphill/*uphill_ok*/) ; // sub-files of makable dir are not buildable, except for Uphill so sub-sub-files are not buildable
								if (!makable) {
									if (is_static) {
										dep_state  = State::MissingStatic                  ;
										reason    |= {JobReasonTag::DepMissingStatic,+dep} ;
										trace("missing_static",dep) ;
										goto Continue ;
									}
									if (care) {
										bool seen_existing = +dep.accesses && (dep.is_date?+dep.date():!dep.crc().match(Crc::None)) ;
										if (seen_existing) {
											if (is_target(dep.name())) {                                  // file still exists, still dangling
												req->audit_node(Color::Err ,"dangling"          ,dep  ) ;
												req->audit_node(Color::Note,"consider : git add",dep,1) ;
												trace("dangling",dep) ;
												goto MarkDep ;
											} else {
												dep.crc({}) ;                  // file does not exist any more, it has been removed
											}
										}
									}
									if (required) {
										dep_state  = State::Err                              ;
										reason    |= {JobReasonTag::DepMissingRequired,+dep} ;
										trace("missing_required",dep) ;
										goto Continue ;
									}
								}
								switch (cdri->err) {
									case No    :                      break    ;
									case Maybe : overwritten = true ; goto Err ;                                     // dep is already in error
									case Yes   :                      if (sense_err) goto Err ; else goto Continue ; // .
									default: FAIL(cdri->err) ;
								}
								if ( sense_err && dep->err() ) {
									trace("dep_err",dep) ;
									goto Err ;
								}
								if (
									( dep.is_date                                                              ) // if still waiting for a crc here, it will never come
								||	( +dep.accesses && dep.known && make_action==MakeAction::End && !dep.crc() ) // when ending a job, known accessed deps should have a crc
								) {
									if (is_target(dep.name())) {               // file still exists, still manual
										if (dep->is_src()) goto Overwriting ;
										for( Job j : dep.conform_job_tgts(*cdri) )
											for( Req r : j.running_reqs() )
												if (j.c_req_info(r).lvl==Lvl::Exec) goto Overwriting ;
										req->audit_node(Color::Err,"manual",dep) ;                     // well, maybe a job is writing to dep as an unknown target, but we then cant distinguish
										trace("manual",dep) ;
										goto MarkDep ;
									Overwriting :
										trace("overwriting",dep,STR(dep->is_src())) ;
										req->audit_node(Color::Err,"overwriting",dep) ;
										overwritten = true ; goto MarkDep ;
									} else {
										dep.crc({}) ;                          // file does not exist any more, no more manual
									}
								}
								if (dep->db_date()>req->start) {
									req->audit_node(Color::Err,"overwritten",dep) ;
									trace("overwritten",dep,dep->db_date(),req->start) ;
									overwritten = true ; goto MarkDep ;
								}
								if (state>=State::Modif) goto Continue ;           // in case dep is static, it has not been caught earlier
								if (is_modif) {                                    // this modif is not preceded by an error, we will really run the job
									reason    |= {JobReasonTag::DepChanged,+dep} ;
									ri.action  = RunAction::Run                  ;
									if (dep_action<RunAction::Dsk) {
										ri.dep_lvl = 0              ;
										dep_action = RunAction::Dsk ;
										state      = State::Ok      ;
										trace("restart_analysis") ;
										goto RestartAnalysis ;
									}
								}
								goto Continue ;
							}
						MarkDep :
							{	Node::ReqInfo& dri = dep.req_info(*cdri) ; cdri = &dri ; // refresh cdri in case dri allocated a new one
								dri.err = overwritten?Maybe:Yes ;
							}
						Err :
							dep_state  = State::Err                                                               ;
							reason    |= { overwritten?JobReasonTag::DepOverwritten:JobReasonTag::DepErr , +dep } ;
						Continue :
							trace("dep",dep,STR(is_static),STR(dep.done(*cdri)),STR(dep.err(*cdri)),ri,dep->crc,dep_state,state,STR(critical_modif),STR(critical_waiting),reason) ;
							//
							SWEAR(dep_state!=State::Modif) ;                                                            // dep_state only generates dangling modifs
							if ( is_critical && care && dep_state==State::DanglingModif  ) critical_modif = true      ;
							if ( dep_state>state && ( is_static || state!=State::Modif ) ) state          = dep_state ; // Modif blocks errors, unless dep is static
						}
						if (ri.waiting()) goto Wait ;
					} break ;
					default : FAIL(ri.lvl) ;
				}
				if (sure) (*this)->mk_sure() ;                                 // improve sure (sure is pessimistic)
				switch (state) {
					case State::Ok            :
					case State::DanglingModif :                                                     // if last dep is parallel, we have not transformed DanglingModif into Modif
					case State::Modif         : (*this)->run_status = RunStatus::Complete ; break ;
					case State::Err           : (*this)->run_status = RunStatus::DepErr   ; break ;
					case State::MissingStatic : (*this)->run_status = RunStatus::NoDep    ; break ;
					default : fail(state) ;
				}
				trace("run",ri,(*this)->run_status,state) ;
				//
				if (ri.action          !=RunAction::Run     ) break ;          // we are done with the analysis and we do not need to run : we're done
				if ((*this)->run_status!=RunStatus::Complete) break ;          // we cant run the job, error is set and we're done
				//                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				bool maybe_new_deps = submit(ri,reason,dep_pressure) ;
				//                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (ri.waiting()   ) goto Wait ;
				if (!maybe_new_deps) break     ;                               // if no new deps, we are done
				make_action = MakeAction::End    ;                             // restart analysis as if called by end() as in case of flash execution, submit has called end()
				ri.action   = RunAction ::Status ;                             // .
				ri.lvl      = Lvl       ::Dep    ;                             // .
				trace("restart_analysis",ri) ;
			}
			ri.lvl   = Lvl::Done            ;
			ri.done_ = ri.done_ | ri.action ;
		Wakeup :
			if ( auto it = req->missing_audits.find(*this) ; it!=req->missing_audits.end() && !req->zombie ) {
				JobAudit const& ja = it->second ;
				trace("report_missing",ja) ;
				IFStream job_stream   { ancillary_file() }                    ;
				/**/                    deserialize<JobInfoStart>(job_stream) ;
				auto     report_end   = deserialize<JobInfoEnd  >(job_stream) ;
				//
				if (!ja.hit) {
					SWEAR(req->stats.ended(JobReport::Rerun)>0) ;
					req->stats.ended(JobReport::Rerun)-- ;                         // we tranform a rerun into a completed job, subtract what was accumulated as rerun
					req->stats.jobs_time[false/*useful*/] -= (*this)->exec_time ;  // exec time is not added to useful as it is not provided to audit_end
					req->stats.jobs_time[true /*useful*/] += (*this)->exec_time ;  // .
				}
				//
				EndNoneAttrs end_none_attrs ;
				AnalysisErr  analysis_err   ;
				bool         no_info        = false ;
				// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
				try {
					Rule::SimpleMatch match ;
					::vmap_ss         rsrcs ;
					if (+(rule->end_none_attrs.need&NeedRsrcs)) {
						try         { rsrcs = deserialize<JobInfoStart>(IFStream(ancillary_file())).rsrcs ; }
						catch (...) {}                                                                        // ignore error as it is purely for cosmetic usage
					}
					end_none_attrs = rule->end_none_attrs.eval(*this,match,rsrcs) ;
				} catch (::string const& e) {
					end_none_attrs = rule->end_none_attrs.spec ;
					req->audit_job(Color::Note,"dynamic",*this) ;
					req->audit_stderr({{rule->end_none_attrs.s_exc_msg(true/*using_static*/),{}}},e,-1,1) ;
				}
				analysis_err.push_back(s_reason_str(reason)) ;
				if ( reason.err() || no_info ) audit_end( ja.hit?"hit_":"was_" , ri , report_end.end.digest.stderr , analysis_err    , end_none_attrs.stderr_len , ja.modified ) ;
				else                           audit_end( ja.hit?"hit_":"was_" , ri , report_end.end.digest.stderr , ja.analysis_err , end_none_attrs.stderr_len , ja.modified ) ;
				req->missing_audits.erase(it) ;
			}
			trace("wakeup",ri) ;
			//                                           vvvvvvvvvvvvvvvvvvvv
			if ( wakeup_watchers && ri.done(ri.action) ) ri.wakeup_watchers() ;
			//                                           ^^^^^^^^^^^^^^^^^^^^
		}
	Wait :
		if ( !rule->is_special() && ri.lvl!=before_lvl ) {
			bool remove_old = _inc_cur(req,before_lvl,-1) ;
			bool add_new    = _inc_cur(req,ri.lvl    ,+1) ;
			req.new_exec_time( *this , remove_old , add_new , old_exec_time?*old_exec_time:(*this)->exec_time ) ;
		}
		return reason ;
	}

	::string Job::special_stderr(Node node) const {
		OStringStream res ;
		switch ((*this)->rule->special) {
			case Special::Plain :
				SWEAR((*this)->frozen()) ;
				if ((*this)->run_status>=RunStatus::Err) {
					if (+node) res << to_string("frozen file does not exist while not phony : ",node.name(),'\n') ;
					else       res <<           "frozen file does not exist while not phony\n"                    ;
				}
			break ;
			case Special::Infinite : {
				Deps const& deps        = (*this)->deps ;
				size_t      n_all_deps  = deps.size()   ;
				size_t      n_show_deps = n_all_deps    ; if ( g_config.max_err_lines && n_show_deps>g_config.max_err_lines ) n_show_deps = g_config.max_err_lines-1 ; // including last line (...)
				for( size_t i=1 ; i<=n_show_deps ; i++ ) res << deps[n_all_deps-i].name() << '\n' ;
				if ( g_config.max_err_lines && deps.size()>g_config.max_err_lines ) res << "...\n" ;
			} break ;
			case Special::Src :
				if ((*this)->status>=Status::Err) {
					if ((*this)->frozen()) res << "frozen file does not exist\n" ;
					else                   res << "file does not exist\n"        ;
				}
			break ;
			default : ;
		}
		return res.str() ;
	}

	static ::pair<SpecialStep,Bool3/*modified*/> _update_frozen_target( Bool3 is_src , Job j , Unode t , ::string const& tn , VarIdx ti=-1/*star*/ ) {
		Rule         r   = j->rule ;
		FileInfoDate fid { tn }    ;
		if ( +fid && fid.date==t->date && +t->crc ) return {SpecialStep::Idle,No/*modified*/} ;
		Trace trace("src",fid.date,t->date) ;
		Crc      crc      { tn , g_config.hash_algo }                                           ;
		Bool3    modified = crc.match(t->crc) ? No : !t->crc || t->crc==Crc::None ? Maybe : Yes ;
		DiskDate date     = +fid ? fid.date : t->date                                           ;
		//vvvvvvvvvvvvvvvvvvvvv
		t.refresh( crc , date ) ;
		//^^^^^^^^^^^^^^^^^^^^^
		// if file disappeared, there is not way to know at which date, we are optimistic here as being pessimistic implies false overwrites
		if (+fid                       ) { j->db_date = date ;         return { SpecialStep::Ok        , modified } ; }
		if (ti==VarIdx(-1)             ) { t->actual_job_tgt.clear() ; return { SpecialStep::Idle      , modified } ; } // unlink of a star target is nothing
		if (is_src==Maybe              ) {                             return { SpecialStep::NoFile    , modified } ; }
		if (is_src==Yes                ) {                             return { SpecialStep::ErrNoFile , modified } ; }
		if (r->tflags(ti)[Tflag::Phony]) {                             return { SpecialStep::NoFile    , modified } ; }
		else                             {                             return { SpecialStep::ErrNoFile , modified } ; }
	}
	bool/*may_new_dep*/ Job::_submit_special(ReqInfo& ri) {
		Trace trace("submit_special",*this,ri) ;
		Req     req     = ri.req                 ;
		Special special = (*this)->rule->special ;
		//
		if ((*this)->frozen()) req->frozens.push_back(*this) ;
		//
		switch (special) {
			case Special::Plain : {
				SWEAR((*this)->frozen()) ;                                     // only case where we are here without special rule
				Rule::SimpleMatch match          { *this }                ;    // match lifetime must be at least as long as static_targets lifetime
				::vector_view_c_s static_targets = match.static_targets() ;
				SpecialStep       special_step   = SpecialStep::Idle      ;
				Node              worst_target   ;
				Bool3             modified       = No                     ;
				for( VarIdx ti=0 ; ti<static_targets.size() ; ti++ ) {
					::string const& tn     = static_targets[ti]                                          ;
					Unode           t      { tn }                                                        ;
					auto            [ss,m] = _update_frozen_target( No/*is_src*/ , *this , t , tn , ti ) ;
					if (ss>special_step) { special_step = ss ; worst_target = t ; }
					modified |= m ;
				}
				for( Unode t : (*this)->star_targets ) {
					auto [ss,m] = _update_frozen_target( No/*is_src*/ , *this , t , t.name() ) ;
					if (ss>special_step) { special_step = ss ; worst_target = t ; }
					modified |= m ;
				}
				(*this)->status = special_step<SpecialStep::HasErr ? Status::Frozen : Status::ErrFrozen ;
				audit_end_special( req , special_step , modified , worst_target ) ;
			} break ;
			case Special::Src        :
			case Special::GenericSrc : {
				::string    tn          = name()                                                                 ;
				Unode       un          { tn }                                                                   ;
				bool        is_true_src = special==Special::Src                                                  ;
				auto        [ss,m]      = _update_frozen_target( Maybe|is_true_src , *this , un , tn , 0/*ti*/ ) ;
				un->actual_job_tgt = {*this,is_true_src/*is_sure*/} ;
				if ((*this)->frozen()) (*this)->status = ss<SpecialStep::HasErr ? Status::Frozen : Status::ErrFrozen ;
				else                   (*this)->status = ss<SpecialStep::HasErr ? Status::Ok     : Status::Err       ;
				if (ss==SpecialStep::NoFile) (*this)->run_status = RunStatus::NoFile ;
				audit_end_special(req,ss,m) ;
			} break ;
			case Special::Req :
				(*this)->status = Status::Ok ;
			break ;
			case Special::Infinite :
				(*this)->status = Status::Err ;
				audit_end_special( req , SpecialStep::Err , No/*modified*/ ) ;
			break ;
			case Special::Uphill :
				for( Dep const& d : (*this)->deps ) {
					// if we see a link uphill, then our crc is unknown to trigger rebuild of dependents
					// there is no such stable situation as link will be resolved when dep is acquired, only when link appeared, until next rebuild
					Unode un{name()} ;
					un->actual_job_tgt = {*this,true/*is_sure*/} ;
					if ( d->crc.is_lnk() || !d->crc ) un.refresh( {}        , {}                ) ;
					else                              un.refresh( Crc::None , DiskDate::s_now() ) ;
				}
				(*this)->status = Status::Ok ;
			break ;
			default : fail() ;
		}
		return false/*may_new_dep*/ ;
	}

	bool/*targets_ok*/ Job::_targets_ok( Req req , Rule::SimpleMatch const& match ) {
		Trace trace("_targets_ok",*this,req) ;
		Rule                rule              = (*this)->rule          ;
		::vector_view_c_s   static_target_names = match.static_targets() ;
		::umap<Node,VarIdx> static_target_map   ;
		::vector<Node>      static_target_nodes ; static_target_nodes.reserve(static_target_names.size()) ;
		for( VarIdx ti=0 ; ti<static_target_names.size() ; ti++ ) {
			Node n = static_target_names[ti] ;
			static_target_map[n] = ti ;
			static_target_nodes.push_back(n) ;
		}
		// check clashes
		NodeIdx d = 0 ;
		for( Dep const& dep : (*this)->deps ) {
			if (!dep.dflags[Dflag::Static]      ) {       break    ; }
			if (!static_target_map.contains(dep)) { d++ ; continue ; }
			::string dep_key = rule->create_match_attrs.spec.full_dynamic ? ""s : rule->create_match_attrs.spec.deps[d].first ;
			::string err_msg = to_string("simultaneously static target ",rule->targets[static_target_map[dep]].first," and static dep ",dep_key," : ") ;
			req->audit_job ( Color::Err  , "clash" , *this     ) ;
			req->audit_node( Color::Note , err_msg , dep   , 1 ) ;
			(*this)->run_status = RunStatus::DepErr ;
			trace("clash") ;
			return false ;
		}
		if ((*this)->status==Status::Lost) {
			trace("job_lost") ;
			SWEAR((*this)->star_targets.empty()) ;                             // lost jobs report no targets at all
			return true ;                                                      // targets may have been modified but job may not have reported it
		}
		// check manual targets
		::vmap<Node,bool/*ok*/> manual_targets ;
		for( VarIdx ti=0 ; ti<static_target_nodes.size() ; ti++ ) {
			Node t = static_target_nodes[ti] ;
			if (t.manual_ok_refresh(req,FileInfoDate(static_target_names[ti]))==No)
				manual_targets.emplace_back(t,rule->tflags(ti)[Tflag::ManualOk]) ;
		}
		Rule::FullMatch fm ;                                               // lazy evaluated
		for( Target t : (*this)->star_targets ) {
			::string tn = t.name() ;
			if (t.manual_ok_refresh(req,FileInfoDate(tn))==No)
				manual_targets.emplace_back( t , t.lazy_tflag(Tflag::ManualOk,match,fm,tn) ) ; // may solve fm lazy evaluation, tn is already ok
		}
		//
		bool job_ok = true ;
		for( auto const& [t,ok] : manual_targets ) {
			trace("manual",t,STR(ok)) ;
			bool target_ok = ok || req->options.flags[ReqFlag::ManualOk] ;
			req->audit_job( target_ok?Color::Note:Color::Err , "manual" , rule , t.name() ) ;
			job_ok &= target_ok ;
		}
		if (job_ok) return true ;
		// generate a message that is simultaneously consise, informative and executable (with a copy/paste) with sh & csh syntaxes
		req->audit_info( Color::Note , "consider :" , 1 ) ;
		for( auto const& [t,ti] : static_target_map     ) if (!t->is_src()) { req->audit_node( Color::Note , "lmake -m" , t , 2 ) ; goto Advised ; }
		for( Node         t     : (*this)->star_targets ) if (!t->is_src()) { req->audit_node( Color::Note , "lmake -m" , t , 2 ) ; goto Advised ; }
	Advised :
		for( auto const& [t,ok] : manual_targets ) {
			if (ok) continue ;
			DiskDate td    = file_date(t.name()) ;
			uint8_t  n_dec = (td-t->date)>Delay(2.) ? 0 : 3 ;                  // if dates are far apart, probably a human action and short date is more comfortable, else be precise
			req->audit_node(
				Color::Note
			,	t->crc==Crc::None ?
					to_string( ": touched " , td.str(0    ) , " not generated"                   , " ; rm" )
				:	to_string( ": touched " , td.str(n_dec) , " generated " , t->date.str(n_dec) , " ; rm" )
			,	t
			,	2
			) ;
		}
		(*this)->run_status = RunStatus::TargetErr ;
		trace("target_is_manual") ;
		return false ;
	}

	bool/*maybe_new_deps*/ Job::_submit_plain( ReqInfo& ri , JobReason reason , CoarseDelay pressure ) {
		using Lvl = ReqInfo::Lvl ;
		Req               req                = ri.req        ;
		Rule              rule               = (*this)->rule ;
		SubmitRsrcsAttrs  submit_rsrcs_attrs ;
		SubmitNoneAttrs   submit_none_attrs  ;
		CacheNoneAttrs    cache_none_attrs   ;
		Rule::SimpleMatch match              { *this }       ;
		Trace trace("submit_plain",*this,ri,reason,pressure) ;
		SWEAR(!ri.waiting()) ;
		try {
			submit_rsrcs_attrs = rule->submit_rsrcs_attrs.eval(*this,match) ;
		} catch (::string const& e) {
			req->audit_job ( Color::Err  , "failed" , *this                                                                 ) ;
			req->audit_info( Color::Note , to_string(rule->submit_rsrcs_attrs.s_exc_msg(false/*using_static*/),'\n',e) , 1 ) ;
			(*this)->run_status = RunStatus::RsrcsErr ;
			trace("no_rsrcs",ri) ;
			return false/*may_new_deps*/ ;
		}
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		try {
			submit_none_attrs = rule->submit_none_attrs.eval(*this,match) ;
		} catch (::string const& e) {
			submit_none_attrs = rule->submit_none_attrs.spec ;
			req->audit_job(Color::Note,"no_dynamic",*this) ;
			req->audit_stderr({{rule->submit_none_attrs.s_exc_msg(true/*using_static*/),{}}},e,-1,1) ;
		}
		try {
			cache_none_attrs = rule->cache_none_attrs.eval(*this,match) ;
		} catch (::string const& e) {
			cache_none_attrs = rule->cache_none_attrs.spec ;
			req->audit_job(Color::Note,"no_dynamic",*this) ;
			req->audit_stderr({{rule->cache_none_attrs.s_exc_msg(true/*using_static*/),{}}},e,-1,1) ;
		}
		ri.backend = submit_rsrcs_attrs.backend ;
		for( Req r : running_reqs() ) if (r!=req) {
			ReqInfo const& cri = c_req_info(r) ;
			SWEAR( cri.backend == ri.backend ) ;
			ri.n_wait++ ;
			ri.lvl = cri.lvl ;                                                   // Exec or Queued, same as other reqs
			if (ri.lvl==Lvl::Exec) req->audit_job(Color::Note,"started",*this) ;
			Backend::s_add_pressure( ri.backend , +*this , +req , {.live_out=ri.live_out,.pressure=pressure} ) ; // tell backend of new Req, even if job is started and pressure has become meaningless
			trace("other_req",r,ri) ;
			return false/*may_new_deps*/ ;
		}
		//
		if (!_targets_ok(req,match)) return false /*may_new_deps*/ ;
		//
		if (!cache_none_attrs.key.empty()) {
			Cache*       cache       = Cache::s_tab.at(cache_none_attrs.key) ;
			Cache::Match cache_match = cache->match(*this,req)               ;
			if (!cache_match.completed) {
				FAIL("delayed cache not yet implemented") ;
			}
			switch (cache_match.hit) {
				case Yes :
					try {
						JobExec        je            { *this , ProcessDate::s_now() }        ;
						::vector<Node> report_unlink = wash(match)                           ;
						JobDigest      digest        = cache->download(*this,cache_match.id) ;
						ri.lvl = Lvl::Hit ;
						je.report_start(ri,report_unlink) ;
						trace("hit_result") ;
						bool modified = je.end({},digest) ;                      // no resources available for cached jobs
						req->stats.ended(JobReport::Hit)++ ;
						req->missing_audits[*this] = { true/*hit*/ , modified , {} } ;
						return true/*maybe_new_deps*/ ;
					} catch (::string const&) {}                                 // if we cant download result, it is like a miss
				break ;
				case Maybe :
					for( Node d : cache_match.new_deps ) {
						Node::ReqInfo const& cdri = d.make( d.c_req_info(req) , RunAction::Status ) ;
						if (cdri.waiting()) d.add_watcher(d.req_info(cdri),*this,ri,pressure) ;
					}
					trace("hit_deps") ;
					return true/*maybe_new_deps*/ ;
				case No :
				break ;
				default : FAIL(cache_match.hit) ;
			}
		}
		ri.n_wait++ ;                                                          // set before calling submit call back as in case of flash execution, we must be clean
		ri.lvl = Lvl::Queued ;
		try {
			SubmitAttrs sa = {
				.live_out  = ri.live_out
			,	.n_retries = submit_none_attrs.n_retries
			,	.pressure  = pressure
			,	.reason    = reason
			} ;
			//       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Backend::s_submit( ri.backend , +*this , +req , ::move(sa) , ::move(submit_rsrcs_attrs.rsrcs) ) ;
			//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			ri.n_wait-- ;                                                      // restore n_wait as we prepared to wait
			(*this)->status = Status::Err ;
			req->audit_job ( Color::Err  , "failed" , *this     ) ;
			req->audit_info( Color::Note , e                , 1 ) ;
			trace("submit_err",ri) ;
			return false/*may_new_deps*/ ;
		} ;
		trace("submitted",ri) ;
		return true/*maybe_new_deps*/ ;
	}

	void Job::audit_end_special( Req req , SpecialStep step , Bool3 modified , Node node ) const {
		Status status = (*this)->status                                                                          ;
		Color  color  = status==Status::Ok ? Color::HiddenOk : status>=Status::Err ? Color::Err : Color::Warning ;
		bool   frozen = JobData::s_frozen(status)                                                                ;
		//
		SWEAR(status>Status::Garbage) ;
		Trace trace("audit_end_special",*this,req,step,modified,color,status) ;
		//
		::string stderr   = special_stderr(node) ;
		::string step_str ;
		switch (step) {
			case SpecialStep::Idle      :                                                                             break ;
			case SpecialStep::NoFile    : step_str = modified!=No || frozen ? "no_file" : ""                        ; break ;
			case SpecialStep::Ok        : step_str = modified==Yes ? "changed" : modified==Maybe ? "new" : "steady" ; break ;
			case SpecialStep::Err       :
			case SpecialStep::ErrNoFile : step_str = "failed"                                                       ; break ;
			default : FAIL(step) ;
		}
		if (frozen) {
			if (step_str.empty()) step_str  = "frozen"  ;
			else                  step_str += "_frozen" ;
		}
		if (!step_str.empty()) {
			/**/                 req->audit_job (color      ,step_str,*this  ) ;
			if (!stderr.empty()) req->audit_info(Color::None,stderr        ,1) ;
		}
	}

	bool/*ok*/ Job::forget() {
		Trace trace("Jforget",*this,(*this)->deps,(*this)->deps.size()) ;
		for( Req r : running_reqs() ) { (void)r ; return false ; }             // ensure job is not running
		(*this)->status = Status::New ;
		fence() ;                                                              // once status is New, we are sure target is not up to date, we can safely modify it
		(*this)->run_status = RunStatus::Complete ;
		NodeIdx n_static_deps = 0 ;
		for ( Dep const& d : (*this)->deps ) {
			if (!d.dflags[Dflag::Static]) break ;
			n_static_deps++ ;
		}
		(*this)->deps.shorten_by( (*this)->deps.size() - n_static_deps ) ;     // forget hidden deps
		if (!(*this)->rule->is_special()) {
			(*this)->exec_gen = 0 ;
			(*this)->star_targets.clear() ;
		}
		trace("summary",(*this)->deps) ;
		return true ;
	}

}
