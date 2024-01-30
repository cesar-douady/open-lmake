// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "time.hh"
#include "rpc_job.hh"

#include "core.hh"

using namespace Disk ;

namespace Engine {

	::map <Pdate,Job  > Job::_s_start_date_to_jobs ;
	::umap<Job  ,Pdate> Job::_s_job_to_end_dates   ;
	::map <Pdate,Job  > Job::_s_end_date_to_jobs   ;

	//
	// jobs thread
	//

	// we want to unlink dir knowing that :
	// - create_dirs will be created, so no need to unlink them
	// - keep_enclosing_dirs must be kept, together with all its recursive children
	// result is reported through in/out param to_rmdirs that is used to manage recursion :
	// - on the way up we notice that we hit a create_dirs to avoid unlinking a dir that will have to be recreated
	// - if we hit a keep_enclosing_dirs, we bounce down with a false return value saying that we must not unlink anything
	// - on the way down, we accumulate to to_rmdirs dirs if we did not bounce on a keep_enclosing_dirs and we are not a father of a create_dirs
	static bool/*ok*/ _acc_to_rmdirs( ::set_s& to_rmdirs , ::umap_s<NodeIdx> const& keep_enclosing_dirs , ::set_s const& create_dirs , ::string const& dir , bool keep=false ) {
		if (!dir                             ) return true  ;                  // bounce at root, accumulating to to_rmdirs on the way down
		if (to_rmdirs          .contains(dir)) return true  ;                  // return true to indicate that above has already been analyzed and is ok, propagate downward
		if (keep_enclosing_dirs.contains(dir)) return false ;                  // return false to indicate that nothing must be unlinked here and below , propagate downward
		//
		keep |= create_dirs.contains(dir) ;                                    // set keep     to indicate that nothing must be unlinked here and above , propagate upward
		//
		if ( !_acc_to_rmdirs( to_rmdirs , keep_enclosing_dirs , create_dirs , dir_name(dir) , keep ) ) return false ;
		//
		if (!keep) to_rmdirs.insert(dir) ;
		return true ;
	}

	static inline ::vmap<Node,pair<FileAction,bool/*warn*/>> _targets_to_wash( JobData const& jd , Rule::SimpleMatch const& match , bool manual_ok ) { // thread-safe
		::vmap<Node,pair<FileAction,bool/*warn*/>> res ;
		Job                                        j    = jd.idx() ;
		Trace trace("targets_to_wash",j) ;
		auto handle_target = [&]( Node t , ::function<bool(Tflag)> has_flag )->void {
trace(t) ;
			FileActionTag at   = FileActionTag::Unknown/*garbage*/ ;
			bool          warn = false                             ;
			//
			if      (t->crc==Crc::None            ) { at = FileActionTag::None     ; goto Return ; } // nothing to wash
			if      (t->unlinked                  ) { at = FileActionTag::None     ; goto Return ; } // already unlinked somehow
			if      (t->is_src()                  ) { at = FileActionTag::Src      ; goto Return ; } // never touch a source, and dont even check manual
			if      (!has_flag(Tflag::Incremental))   at = FileActionTag::Unlink   ;
			else if (!has_flag(Tflag::Uniquify)   ) { at = FileActionTag::None     ; goto Return ; }
			else                                      at = FileActionTag::Uniquify ;
			warn = !t->has_actual_job(j) && t->has_actual_job() && has_flag(Tflag::Warning) ;
		Return :
			FileAction a{ at , manual_ok || t.manual_ok() || has_flag(Tflag::ManualOk) , t->crc , t->crc==Crc::None?Ddate():t->date() } ;
			res.emplace_back(t,pair(a,warn)) ;
			trace(t,at,STR(warn)) ;
		} ;
		// handle static targets
		::vector_s sts = match.static_targets() ;
		for( VarIdx ti=0 ; ti<sts.size() ; ti++ ) {
			::string const& tn  = sts[ti]             ;
			Tflags          tfs = jd.rule->tflags(ti) ;
			handle_target( Node(tn) , [&](Tflag tf)->bool { return tfs[tf] ; } ) ;
		}
		// handle star targets
		for( Target t : jd.star_targets ) {
			::string tn ;                                                                                                      // lazy evaluated
			handle_target( t , [&](Tflag tf)->bool { return t.lazy_star_tflag(tf,match,tn) ; } ) ;                             // may lazy solve tn
		}
		return res ;
	}
	::pair<vmap<Node,FileAction>,vector<Node>/*warn_unlink*/> JobData::pre_actions( Rule::SimpleMatch const& match , bool manual_ok , bool mark_target_dirs ) const { // thread-safe
		Trace trace("pre_actions",idx(),STR(manual_ok),STR(mark_target_dirs)) ;
		::uset<Node>                                 to_mkdirs        = match.target_dirs() ;
		::uset<Node>                                 to_mkdir_uphills ;
		::uset<Node>                                 locked_dirs      ;
		::vmap  <Node,pair<FileAction,bool/*warn*/>> to_wash          = _targets_to_wash(*this,match,manual_ok) ;
		::umap  <Node,NodeIdx/*depth*/>              to_rmdirs        ;
		::vmap<Node,FileAction>                      actions          ;
		::vector<Node>                               warnings         ;
		for( Node d : to_mkdirs )
			for( Node hd=d->dir() ; +hd ; hd = hd->dir() )
				if (!to_mkdir_uphills.insert(hd).second) break ;
		for( auto const& [dn,_] : match.deps() )                         // no need to mkdir a target dir if it is also a static dep dir (which necessarily already exists)
			for( Node hd=Node(dn)->dir() ; +hd ; hd = hd->dir() )
				if (!locked_dirs.insert(hd).second) break ;              // if dir contains a dep, it cannot be rmdir'ed
		//
		// remove old_targets
		for( auto const& [t,aw] : to_wash ) {
			auto [a,w] = aw       ;
			Node td    = t->dir() ;
			trace("wash_target",t,a,STR(w)) ;
			switch (a.tag) {
				case FileActionTag::Src      :                        locked_dirs.insert(td) ;                             continue ; // nothing to do in job_exec, not even integrity check
				case FileActionTag::Uniquify :                        locked_dirs.insert(td) ; actions.emplace_back(t,a) ; continue ;
				case FileActionTag::None     : if (t->crc!=Crc::None) locked_dirs.insert(td) ; actions.emplace_back(t,a) ; continue ;
				case FileActionTag::Unlink   :                                                 actions.emplace_back(t,a) ; break    ;
				default : FAIL(a.tag) ;
			}
			if (w  ) warnings.push_back(t) ;
			if (!td) continue ;
			NodeIdx depth = 0 ;
			for( Node hd=td ; +hd ; (hd=hd->dir()),depth++ ) if (_s_target_dirs.contains(hd)) goto NextTarget ; // everything under a protected dir is protected, dont even start walking from td
			for( Node hd=td ; +hd ; hd = hd->dir() ) {
				if (_s_hier_target_dirs.contains(hd)) break ;           // dir is protected
				if (locked_dirs        .contains(hd)) break ;           // dir contains a file, no hope to remove it
				if (to_mkdirs          .contains(hd)) break ;           // dir must exist, it is silly to spend time to rmdir it, then again to mkdir it
				if (to_mkdir_uphills   .contains(hd)) break ;           // .
				//
				if (!to_rmdirs.emplace(td,depth).second) break ;        // if it is already in to_rmdirs, so is all pertinent dirs uphill
				depth-- ;
			}
		NextTarget : ;
		}
		// make target dirs
		for( Node d : to_mkdirs ) {
			if (locked_dirs     .contains(d)) continue ;                // dir contains a file         => it already exists
			if (to_mkdir_uphills.contains(d)) continue ;                // dir is a dir of another dir => it will be automatically created
			actions.emplace_back( d , FileActionTag::Mkdir ) ;          // note that protected dirs (in _s_target_dirs and _s_hier_target_dirs) may not be created yet, so mkdir them to be sure
		}
		// rm enclosing dirs of unlinked targets
		::vmap<Node,NodeIdx/*depth*/> to_rmdir_vec ; for( auto [k,v] : to_rmdirs ) to_rmdir_vec.emplace_back(k,v) ;
		::sort( to_rmdir_vec , [&]( ::pair<Node,NodeIdx> const& a , ::pair<Node,NodeIdx> const& b ) { return a.second>b.second ; } ) ; // sort deeper first, so we rmdir after its children
		for( auto [d,_] : to_rmdir_vec ) actions.emplace_back(d,FileActionTag::Rmdir) ;
		//
		// mark target dirs to protect from deletion by other jobs
		// this must be perfectly predictible as this mark is undone in end_exec below
		if (mark_target_dirs) {
			::unique_lock lock{_s_target_dirs_mutex} ;
			for( Node d : to_mkdirs        ) { trace("protect_dir"     ,d) ; _s_target_dirs     [d]++ ; }
			for( Node d : to_mkdir_uphills ) { trace("protect_hier_dir",d) ; _s_hier_target_dirs[d]++ ; }
		}
		return {actions,warnings} ;
	}

	void JobData::end_exec() const {
		Trace trace("end_exec",idx()) ;
		::uset<Node> dirs        = simple_match().target_dirs() ;
		::uset<Node> dir_uphills ;
		for( Node d : dirs )
			for( Node hd=d->dir() ; +hd ; hd = hd->dir() )
				if (!dir_uphills.insert(hd).second) break ;
		//
		auto dec = [&]( ::umap<Node,NodeIdx/*cnt*/>& dirs , Node d )->void {
			auto it = dirs.find(d) ;
			SWEAR(it!=dirs.end()) ;
			if (it->second==1) dirs.erase(it) ;
			else               it->second--   ;
		} ;
		::unique_lock lock(_s_target_dirs_mutex) ;
		for( Node d : dirs        ) { trace("unprotect_dir"     ,d) ; dec(_s_target_dirs     ,d) ; }
		for( Node d : dir_uphills ) { trace("unprotect_hier_dir",d) ; dec(_s_hier_target_dirs,d) ; }
	}

	//
	// main thread
	//

	//
	// JobTgts
	//

	::ostream& operator<<( ::ostream& os , JobTgts jts ) {
		return os<<jts.view() ;
	}

	//
	// JobReqInfo
	//

	::ostream& operator<<( ::ostream& os , JobReqInfo const& ri ) {
		return os<<"JRI(" << ri.req <<','<< ri.action <<','<< ri.speculate <<','<< ri.lvl<<':'<<ri.dep_lvl <<','<< ri.n_wait <<')' ;
	}

	//
	// Job
	//

	::ostream& operator<<( ::ostream& os , Job j ) {
		/**/    os << "J(" ;
		if (+j) os << +j   ;
		return  os << ')'  ;
	}
	::ostream& operator<<( ::ostream& os , JobTgt jt ) {
		if (!jt) return   os << "JT()"         ;
		/**/              os << "(" << Job(jt) ;
		if (jt.is_sure()) os << ",sure"        ;
		return            os << ')'            ;
	}
	::ostream& operator<<( ::ostream& os , JobExec const& je ) {
		if (!je) return os << "JE()" ;
		//
		/**/                     os <<'('<< Job(je)                     ;
		if (je.host!=NoSockAddr) os <<','<< SockFd::s_addr_str(je.host) ;
		if (je.start_==je.end_) {
			os <<','<< je.start_ ;
		} else {
			if (+je.start_) os <<",F:"<< je.start_ ;
			if (+je.end_  ) os <<",T:"<< je.end_   ;
		}
		return os <<')' ;
	}

	Job::Job( Rule::SimpleMatch&& match , Req req , DepDepth lvl ) {
		Trace trace("Job",match,lvl) ;
		if (!match) { trace("no_match") ; return ; }
		Rule                rule      = match.rule ; SWEAR( rule->special<=Special::HasJobs , rule->special ) ;
		::vmap_s<AccDflags> dep_names ;
		try {
			dep_names = mk_val_vector(rule->deps_attrs.eval(match)) ;
		} catch (::string const& e) {
			trace("no_dep_subst") ;
			if (+req) {
				req->audit_job(Color::Note,"no_deps",rule->name,match.name()) ;
				req->audit_stderr( rule->deps_attrs.s_exc_msg(false/*using_static*/) , e , -1 , 1 ) ;
			}
			return ;
		}
		::vmap<Node,AccDflags> deps ; deps.reserve(dep_names.size()) ;
		for( auto [dn,af] : dep_names ) {
			Node d{dn} ;
			//vvvvvvvvvvvvvvvvvvv
			d->set_buildable(lvl) ;
			//^^^^^^^^^^^^^^^^^^^
			if (d->buildable<=Buildable::No) { trace("no_dep",d) ; return ; }
			deps.emplace_back(d,af) ;
		}
		//      vvvvvvvvvvvvvvvvv
		*this = Job(
			match.full_name() , Dflt                                           // args for store
		,	rule , Deps(deps)                                                  // args for JobData
		) ;
		//^^^^^^^^^^^^^^^^^^^^^^^
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		try {
			(*this)->tokens1 = rule->create_none_attrs.eval(*this,match).tokens1 ;
		} catch (::string const& e) {
			(*this)->tokens1 = rule->create_none_attrs.spec.tokens1 ;
			req->audit_job(Color::Note,"dynamic",*this) ;
			req->audit_stderr( rule->create_none_attrs.s_exc_msg(true/*using_static*/) , e , -1 , 1 ) ;
		}
		trace("found",*this) ;
	}

	//
	// JobExec
	//

	void JobExec::_set_start_date() {
		if (!_s_start_date_to_jobs.emplace(start_,*this).second) FAIL("date conflict",*this,_s_start_date_to_jobs.at(start_)) ;
	}

	void JobExec::_set_end_date() {
		if ( auto it = _s_start_date_to_jobs.find(start_) ; it!=_s_start_date_to_jobs.end() ) { // if started was not called (e.g. if cache hit), job is not recorded in _s_start_date_to_jobs
			SWEAR(it->second==*this) ;
			_s_start_date_to_jobs.erase(it) ;
		}
		if (!_s_start_date_to_jobs) {
			_s_job_to_end_dates.clear() ;
			_s_end_date_to_jobs.clear() ;
		} else {
			Pdate earliest = _s_start_date_to_jobs.begin()->first ;
			for( auto it=_s_end_date_to_jobs.begin() ; it!=_s_end_date_to_jobs.end() ;) {
				if (it->first>earliest) break ;
				_s_job_to_end_dates.erase(it->second) ;
				_s_end_date_to_jobs.erase(it++      ) ;
			}
			if (end_>earliest) {
				/**/  _s_job_to_end_dates.emplace(*this,end_ )          ;
				swear(_s_end_date_to_jobs.emplace(end_ ,*this).second ) ;
			}
		}
	}

	void JobExec::give_up( Req req , bool report ) {
		Trace trace("give_up",*this,req) ;
		if (+req) {
			(*this)->make( (*this)->req_info(req) , RunAction::None , {}/*reason*/ , {}/*asking*/ , Yes/*speculate*/ , MakeAction::GiveUp ) ;
			if (report)
				for( Req r : (*this)->running_reqs(false/*with_zombies*/) ) {
					SWEAR(r!=req) ;
					req->audit_job(Color::Note,"continue",*this,true/*at_end*/) ;        // generate a continue line if some other req is still active
					break ;
				}
			req.chk_end() ;
		} else {
			for( Req r : (*this)->running_reqs(true/*with_zombies*/) ) give_up(r,false/*report*/)                                      ;
			for( Req r : (*this)->running_reqs(true/*with_zombies*/) ) FAIL((*this)->name(),"is still running for",r,"after kill all") ;
		}
	}

	// answer to job execution requests
	JobRpcReply JobExec::job_info( JobProc proc , ::vector<Dep> const& deps ) const {
		::vector<Req> reqs = (*this)->running_reqs(false/*with_zombies*/) ;
		Trace trace("job_info",proc,deps.size()) ;
		if (!reqs) return proc ;                                                      // if job is not running, it is too late
		//
		switch (proc) {
			case JobProc::DepInfos : {
				::vector<pair<Bool3/*ok*/,Crc>> res ; res.reserve(deps.size()) ;
				for( Dep const& dep : deps ) {
					Bool3 dep_ok = Yes ;
					for( Req req : reqs ) {
						// we need to compute crc if it can be done immediately, as is done in make
						// or there is a risk that the job is not rerun if dep is remade steady and leave a bad crc leak to the job
						NodeReqInfo& dri = dep->req_info(req) ;
						Node(dep)->make( dri , RunAction::Status ) ;                  // XXX : avoid actually launching jobs if it is behind a critical modif
						trace("dep_info",dep,req,dri) ;
						if (dri.waiting()) {
							dep_ok = Maybe ;
							break ;
						}
						dep_ok &= dep->ok(dri,dep.accesses) ;
					}
					res.emplace_back(dep_ok,dep->crc) ;
				}
				return {proc,res} ;
			}
			case JobProc::ChkDeps : {
				bool ok = true ;
				for( Dep const& dep : deps ) {
					for( Req req : reqs ) {
						// we do not need dep for our purpose, but it will soon be necessary, it is simpler just to call plain make()
						// use Dsk as we promess file is available
						NodeReqInfo& dri = dep->req_info(req) ;
						Node(dep)->make( dri , RunAction::Dsk ) ;                     // XXX : avoid actually launching jobs if it is behind a critical modif
						// if dep is waiting for any req, stop analysis as we dont know what we want to rebuild after
						// and there is no loss of parallelism as we do not wait for completion before doing a full analysis in make()
						if (dri.waiting()) { trace("waiting",dep) ; return {proc,Maybe} ; }
						bool dep_ok = dep->ok(dri,dep.accesses)!=No ;
						ok &= dep_ok ;
						trace("chk_dep",dep,req,STR(dep_ok)) ;
					}
				}
				trace("done",STR(ok)) ;
				return {proc,No|ok} ;
			}
			default : FAIL(proc) ;
		}
	}

	void JobExec::live_out( ReqInfo& ri , ::string const& txt ) const {
		if (!txt        ) return ;
		if (!ri.live_out) return ;
		Req r = ri.req ;
		// identify job (with a continue message if no start message), dated as now and with current exec time
		if ( !report_start(ri) && r->last_info!=*this ) r->audit_job(Color::HiddenNote,"continue",JobExec(*this,host,New),false/*at_end*/,Pdate::s_now()-start_) ;
		r->last_info = *this ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		r->audit_info(Color::None,txt,0) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

	void JobExec::live_out(::string const& txt) const {
		Trace trace("report_start",*this) ;
		for( Req req : (*this)->running_reqs(false/*with_zombies*/) ) live_out((*this)->req_info(req),txt) ;
	}

	bool/*reported*/ JobExec::report_start( ReqInfo& ri , ::vector<Node> const& report_unlink , ::string const& stderr , ::string const& msg ) const {
		if ( ri.start_reported ) return false ;
		ri.req->audit_job( +stderr?Color::Warning:Color::HiddenNote , "start" , *this ) ;
		ri.req->last_info = *this ;
		size_t      w   = 0  ;
		::vmap_s<Node> report_lst ;
		for( auto t : report_unlink ) {
			::string pfx = "unlinked" ;
			if (Job(t->actual_job_tgt())!=Job(*this)) append_to_string(pfx," (generated by ",t->actual_job_tgt()->rule->name,')') ;
			w = ::max( w , pfx.size() ) ;
			report_lst.emplace_back(pfx,t) ;
		}
		for( auto const& [pfx,t] : report_lst ) ri.req->audit_node( Color::Warning , to_string(::setw(w),pfx) , t , 1 ) ;
		if (+stderr) ri.req->audit_stderr( msg , stderr , -1 , 1 ) ;
		ri.start_reported = true ;
		return true ;
	}
	void JobExec::report_start() const {
		Trace trace("report_start",*this) ;
		for( Req req : (*this)->running_reqs(false/*with_zombies*/) ) report_start((*this)->req_info(req)) ;
	}

	void JobExec::started( bool report , ::vector<Node> const& report_unlink , ::string const& stderr , ::string const& msg ) {
		Trace trace("started",*this) ;
		SWEAR( !(*this)->rule->is_special() , (*this)->rule->special ) ;
		_set_start_date() ;
		report |= +report_unlink || +stderr ;
		for( Req req : (*this)->running_reqs() ) {
			ReqInfo& ri = (*this)->req_info(req) ;
			ri.start_reported = false ;
			if (report) report_start(ri,report_unlink,stderr,msg) ;
			//
			if (ri.lvl==ReqInfo::Lvl::Queued) {
				req->stats.cur(ReqInfo::Lvl::Queued)-- ;
				req->stats.cur(ReqInfo::Lvl::Exec  )++ ;
				ri.lvl = ReqInfo::Lvl::Exec ;
			}
		}
	}

	// XXX generate consider line if status==Manual
	bool/*modified*/ JobExec::end( ::vmap_ss const& rsrcs , JobDigest const& digest , ::string const& backend_msg ) {
		Status            status           = digest.status                               ;                            // status will be modified, need to make a copy
		Bool3             ok               = is_ok  (status)                             ;
		bool              lost             = is_lost(status)                             ;
		JobReason         local_reason     = JobReasonTag::None                          ;
		bool              local_err        = false                                       ;
		bool              any_modified     = false                                       ;
		bool              fresh_deps       = status>Status::Async                        ; // if job did not go through, old deps are better than new ones
		Rule              rule             = (*this)->rule                               ;
		::vector<Req>     running_reqs_    = (*this)->running_reqs(true/*with_zombies*/) ;
		::string          msg              ;                                               // to be reported if job was otherwise ok
		::string          severe_msg       ;                                               // to be reported always
		CacheNoneAttrs    cache_none_attrs ;
		EndCmdAttrs       end_cmd_attrs    ;
		Rule::SimpleMatch match            ;
		//
		SWEAR(status!=Status::New) ;                                   // we just executed the job, it can be neither new, frozen or special
		SWEAR(!frozen()          ) ;                                   // .
		SWEAR(!rule->is_special()) ;                                   // .
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		try {
			cache_none_attrs = rule->cache_none_attrs.eval(*this,match) ;
		} catch (::string const& e) {
			cache_none_attrs = rule->cache_none_attrs.spec ;
			for( Req req : running_reqs_ ) {
				req->audit_job(Color::Note,"dynamic",*this,true/*at_end*/) ;
				if (backend_msg.back()=='\n') req->audit_stderr( to_string(backend_msg,     rule->cache_none_attrs.s_exc_msg(true/*using_static*/)) , e , -1 , 1 ) ;
				else                          req->audit_stderr( to_string(backend_msg,'\n',rule->cache_none_attrs.s_exc_msg(true/*using_static*/)) , e , -1 , 1 ) ;
			}
		}
		try                       { end_cmd_attrs = rule->end_cmd_attrs.eval(*this,match) ;                        }
		catch (::string const& e) { append_to_string( severe_msg , "cannot compute " , EndCmdAttrs::Msg , '\n' ) ; }
		//
		if ( status<=Status::Garbage && ok!=No )
			switch (status) {
				case Status::EarlyLost :
				case Status::LateLost  : local_reason = JobReasonTag::Lost    ; break ;
				case Status::Killed    : local_reason = JobReasonTag::Killed  ; break ;
				case Status::ChkDeps   : local_reason = JobReasonTag::ChkDeps ; break ;
				case Status::Garbage   : local_reason = JobReasonTag::Garbage ; break ;
				default : FAIL(status) ;
			}
		//
		(*this)->status = ::min(status,Status::Garbage) ;                          // ensure we cannot appear up to date while working on data
		fence() ;
		//
		Trace trace("end",*this,status) ;
		//
		// handle targets
		//
		s_manual_oks( lost && status>Status::Early /*add*/ , {*this} ) ;           // if late lost, we have not reported written targets, else wash manual_ok state
		//
		if ( !lost && status>Status::Early ) {                                     // if early, we have not touched the targets, not even washed them, if lost, old targets are better than new ones
			auto report_if_missing = [&]( ::string const& tn , Tflags tf )->void {
				if (+(tf&Tflags(Tflag::Star,Tflag::Phony))) return ;               // these are not required targets
				if (status!=Status::Ok                    ) return ;               // it is quite normal to have missing targets when job is in error
				FileInfo fi{tn} ;
				to_string( msg , "missing target", (+fi?" (existing)":fi.tag==FileTag::Dir?" (dir)":"") , " : " , mk_file(tn) , '\n' ) ;
				local_err = true ;
			} ;
			::uset  <Node> seen_static_targets ;
			//
			for( Node t : (*this)->star_targets ) if (t->has_actual_job(*this)) t->actual_job_tgt().clear() ; // ensure targets we no more generate do not keep pointing to us
			//
			::vector<Target> star_targets ;                                    // typically, there is either no star targets or they are most of them, lazy reserve if one is seen
			for( auto const& [tn,td] : digest.targets ) {
				Tflags tflags     = td.tflags                                                     ;
				Node   target     { tn }                                                          ;
				bool   inc        = tflags[Tflag::Incremental]                                    ;
				bool   unlink     = td.crc==Crc::None                                             ;
				bool   touched    = td.write || unlink                                            ;
				Crc    crc        = touched ? td.crc : target->unlinked ? Crc::None : target->crc ;
				bool   target_err = false                                                         ;
				//
				target->set_buildable() ;
				//
				if ( td.write && target->is_src() ) {
					if (!crc.valid()) crc = Crc(tn,g_config.hash_algo) ;       // force crc computation if updating a source
					if (!tflags[Tflag::SourceOk]) {
						local_err = target_err = true ;
						if (unlink) append_to_string( severe_msg , "unexpected unlink of source " , mk_file(tn) , '\n' ) ;
						else        append_to_string( severe_msg , "unexpected write to source "  , mk_file(tn) , '\n' ) ;
					}
				}
				if (
					td.write                                                   // we actually wrote
				&&	target->has_actual_job() && !target->has_actual_job(*this) // there is another job
				&&	target->actual_job_tgt().end_date() > start_
				) {
					Job    aj       = target->actual_job_tgt()   ;             // common_tflags cannot be tried as target may be unexpected for aj
					VarIdx aj_idx   = aj->simple_match().idx(tn) ;             // this is expensive, but pretty exceptional
					Tflags aj_flags = aj->rule->tflags(aj_idx)   ;
					trace("clash",*this,tflags,aj,aj_idx,aj_flags,target) ;
					// /!\ This may be very annoying !
					//     Even completed Req's may have been poluted as at the time t->actual_job_tgt() completed, it was not aware of the clash. But this is too complexe and too rare to detect.
					//     Putting target in clash_nodes will generate a message to user asking to relaunch command.
					if (tflags  [Tflag::Crc]) local_reason |= {JobReasonTag::ClashTarget,+target} ; // if we care about content, we must rerun
					if (aj_flags[Tflag::Crc]) {                                                     // if actual job cares about content, we may have the annoying case mentioned above
						for( Req r : target->reqs() ) {
							Node::ReqInfo& tri = target->req_info(r) ;
							if (!tri.done(RunAction::Dsk)) continue ;
							tri.done_ = RunAction::Status ;                           // target must be re-analyzed if we need the actual files
							trace("critical_clash") ;
							r->clash_nodes.emplace(target,r->clash_nodes.size()) ;    // but req r may not be aware of it
						}
					}
				}
				if ( !inc && target->read(td.accesses) ) local_reason |= {JobReasonTag::PrevTarget,+target} ;
				if (crc==Crc::None) {
					// if we have written then unlinked, then there has been a transcient state where the file existed
					// we must consider this is a real target with full clash detection.
					// the unlinked bit is for situations where the file has just been unlinked with no weird intermediate, which is a less dangerous situation
					if ( !RuleData::s_sure(tflags) && !td.write ) {
						target->unlinked = target->crc!=Crc::None ;    // if target was actually unlinked, note it as it is not considered a target of the job
						trace("unlink",target,STR(target->unlinked)) ;
						continue ;                                     // if we are not sure, a target is not generated if it does not exist
					}
					report_if_missing(tn,tflags) ;
				}
				if ( !td.write && !tflags[Tflag::Match] ) {
					trace("no_target",target) ;
					continue ;                                         // not written, no match => not a target
				}
				if ( !target_err && td.write && !unlink && !tflags[Tflag::Write] ) {
					local_err = true ;
					append_to_string( severe_msg , "unexpected write to " , mk_file(tn) , '\n' ) ;
				}
				//
				if (tflags[Tflag::Star]) {
					if (!star_targets) star_targets.reserve(digest.targets.size()) ;  // solve lazy reserve
					star_targets.emplace_back( target , tflags[Tflag::Unexpected] ) ;
				} else {
					seen_static_targets.insert(target) ;
				}
				//
				bool  modified = false   ;
				Ddate date     = td.date ;
				if (!touched) {
					if      ( target->unlinked                                           ) date = Ddate()                         ; // ideally, should be start date
					else if ( tflags[Tflag::ManualOk] && file_date(target->name())!=date ) crc  = Crc(date,tn,g_config.hash_algo) ; // updating date is not mandatory ...
					else                                                                   goto NoRefresh ;                         // ... but it is safer to update crc with corresponding date
				}
				//         vvvvvvvvvvvvvvvvvvvvvvvvv
				modified = target->refresh(crc,date) ;
				//         ^^^^^^^^^^^^^^^^^^^^^^^^^
			NoRefresh :
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				target->actual_job_tgt() = { *this , RuleData::s_sure(tflags) } ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				any_modified |= modified && tflags[Tflag::Match] ;
				trace("target",target,td,STR(modified),status) ;
			}
			if (seen_static_targets.size()<rule->n_static_targets) {                     // some static targets have not been seen
				::vector_s static_targets = (*this)->simple_match().static_targets() ;
				for( VarIdx ti=0 ; ti<static_targets.size() ; ti++ ) {
					::string const& tn = static_targets[ti] ;
					Node            t  { tn }               ;
					if (seen_static_targets.contains(t)) continue ;
					Tflags tflags = rule->tflags(ti) ;
					t->actual_job_tgt() = { *this , true/*is_sure*/ } ;
					//                               vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					if (!tflags[Tflag::Incremental]) t->refresh( Crc::None , Ddate() ) ; // if incremental, target is preserved, else it has been washed at start time
					//                               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					report_if_missing(tn,tflags) ;
				}
			}
			::sort(star_targets) ;                                                       // ease search in targets
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			(*this)->star_targets.assign(star_targets) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (Node::s_has_manual_oks()) {
				for( Node t : seen_static_targets ) star_targets.push_back(t) ;
				Node::s_manual_oks( false/*add*/ , mk_vector<Node>(star_targets) ) ;     // actually all targets
			}
		}
		//
		// handle deps
		//
		if (fresh_deps) {
			::vector<Dep> dep_vector ; dep_vector.reserve(digest.deps.size()) ;
			for( auto const& [dn,dd] : digest.deps ) {                          // static deps are guaranteed to appear first
				Node d   { dn     } ;
				Dep  dep { d , dd } ;
				dep.acquire_crc() ;
				if ( +dep.accesses && !dep.is_date && !dep.crc().valid() ) local_reason |= {JobReasonTag::DepNotReady,+dep} ;
				dep_vector.emplace_back(dep) ;
				trace("dep",dep,dd) ;
			}
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			(*this)->deps.assign(dep_vector) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		//
		// wrap up
		//
		_set_end_date() ;                                                      // must be called after target analysis to ensure clash detection
		//
		if ( status==Status::Ok && +digest.stderr && !end_cmd_attrs.allow_stderr ) { append_to_string(msg,"non-empty stderr\n") ; local_err = true ; }
		EndNoneAttrs end_none_attrs ;
		::string     stderr         ;
		try {
			end_none_attrs = rule->end_none_attrs.eval(*this,match,rsrcs) ;
			stderr = ::move(digest.stderr) ;
		} catch (::string const& e) {
			end_none_attrs = rule->end_none_attrs.spec ;
			append_to_string( severe_msg , rule->end_none_attrs.s_exc_msg(true/*using_static*/) , '\n' ) ;
			stderr  = ensure_nl(e)  ;
			stderr += digest.stderr ;
		}
		//
		(*this)->exec_ok(true) ;                                               // effect of old cmd has gone away with job execution
		fence() ;                                                              // only update status once every other info is set in case of crash and avoid transforming garbage into Err
		//                                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if      ( +local_reason                   ) (*this)->status = ::min(status,Status::Garbage  ) ;
		else if ( local_err && status==Status::Ok ) (*this)->status =              Status::Err        ;
		else if ( !lost                           ) (*this)->status =       status                    ;
		else if ( status<=Status::Early           ) (*this)->status =              Status::EarlyLost  ;
		else                                        (*this)->status =              Status::LateLost   ;
		//                                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		bool        report_stats     = status==Status::Ok                                           ;
		CoarseDelay old_exec_time    = (*this)->best_exec_time().first                              ;
		bool        cached           = false                                                        ;
		bool        analysis_stamped = false                                                        ;
		MakeAction  end_action       = fresh_deps||ok==Maybe ? MakeAction::End : MakeAction::GiveUp ;
		if (report_stats) {
			SWEAR(+digest.stats.total) ;
			(*this)->exec_time = digest.stats.total ;
			rule.new_job_exec_time( digest.stats.total , (*this)->tokens1 ) ;
		}
		for( Req req : running_reqs_ ) {
			ReqInfo& ri = (*this)->req_info(req) ;
			switch (ri.lvl) {
				case JobLvl::Queued :
					req->stats.cur(ReqInfo::Lvl::Queued)-- ;
					req->stats.cur(ReqInfo::Lvl::Exec  )++ ;
				[[fallthrough]] ;
				case JobLvl::Exec :
					ri.lvl = JobLvl::End ;                                     // we must not appear as Exec while other reqs are analysing or we will wrongly think job is on going
				break ;
				default :
					FAIL(ri.lvl) ;
			}
		}
		for( Req req : running_reqs_ ) {
			ReqInfo& ri = (*this)->req_info(req) ;
			trace("req_before",local_reason,status,ri) ;
			req->missing_audits.erase(*this) ;                                 // old missing audit is obsolete as soon as we have rerun the job
			// we call wakeup_watchers ourselves once reports are done to avoid anti-intuitive report order
			//                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			JobReason reason = (*this)->make( ri , RunAction::Status , local_reason ,  {}/*asking*/ , Yes/*speculate*/ , end_action , &old_exec_time , false/*wakeup_watchers*/ ) ;
			//                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			::string bem = ensure_nl(backend_msg) ;
			//
			if (reason.tag>=JobReasonTag::Err) {
				append_to_string( bem , reason_str(reason) , '\n' ) ;
			} else if (ri.done()) {
				bem += msg ;
			}
			bem += severe_msg ;
			//
			if (ri.done()) {
				// it is not comfortable to store req-dependent info in a req-independent place, but we need reason from make()
				if ( +bem && !analysis_stamped ) {                             // this code is done in such a way as to be fast in the common case (bem empty)
					::string jaf = (*this)->ancillary_file() ;
					try {
						IFStream is{jaf} ;
						auto report_start = deserialize<JobInfoStart>(is) ;
						auto report_end   = deserialize<JobInfoEnd  >(is) ;
						//
						report_end.end.msg = bem ;
						//
						OFStream os{jaf} ;
						serialize(os,report_start) ;
						serialize(os,report_end  ) ;
					}
					catch (...) {}                                             // in case ancillary file cannot be read, dont record and ignore
					analysis_stamped = true ;
				}
				// report exec time even if not recording it
				// report user stderr if make analysis does not make these errors meaningless
				audit_end( {}/*pfx*/ , ri , bem , reason.tag>=JobReasonTag::Err?""s:stderr , end_none_attrs.max_stderr_len , any_modified , digest.stats.total ) ;
				trace("wakeup_watchers",ri) ;
				// as soon as job is done for a req, it is meaningful and justifies to be cached, in practice all reqs agree most of the time
				if ( !cached && +cache_none_attrs.key && (*this)->run_status==RunStatus::Complete && status==Status::Ok ) { // cache only successful results
					NfsGuard nfs_guard{g_config.reliable_dirs} ;
					cached |= Cache::s_tab.at(cache_none_attrs.key)->upload( *this , digest , nfs_guard ) ;
				}
				ri.wakeup_watchers() ;
			} else {
				JobReport jr = audit_end( +local_reason?"":"may_" , ri , bem , {}/*stderr*/ , -1/*max_stderr_len*/ , any_modified , digest.stats.total ) ; // report 'rerun' rather than status
				req->missing_audits[*this] = { jr , any_modified , bem } ;
			}
			trace("req_after",ri) ;
			req.chk_end() ;
		}
		trace("summary",*this) ;
		return any_modified ;
	}

	JobReport JobExec::audit_end( ::string const& pfx , ReqInfo const& cri , ::string const& msg , ::string const& stderr , size_t max_stderr_len , bool modified , Delay exec_time) const {
		using JR = JobReport ;
		//
		Req            req         = cri.req                   ;
		JobData const& jd          = **this                    ;
		Color          color       = Color::Unknown/*garbage*/ ;
		JR             res         = JR   ::Unknown/*garbage*/ ; // report if not Rerun
		JR             jr          = JR   ::Unknown/*garbage*/ ; // report to do now
		::string       step        ;
		bool           with_stderr = true                      ;
		//
		if      (jd.run_status!=RunStatus::Complete) { res = JR::Failed    ; color = Color::Err     ;                       step = mk_snake(jd.run_status) ; }
		else if (jd.status==Status::Killed         ) { res = JR::Killed    ; color = Color::Note    ; with_stderr = false ;                                  }
		else if (is_lost(jd.status) && jd.err()    ) { res = JR::LostErr   ; color = Color::Err     ;                       step = "lost_err"              ; }
		else if (is_lost(jd.status)                ) { res = JR::Lost      ; color = Color::Warning ; with_stderr = false ;                                  }
		else if (req->zombie                       ) { res = JR::Completed ; color = Color::Note    ; with_stderr = false ;                                  }
		else if (jd.err()                          ) { res = JR::Failed    ; color = Color::Err     ;                                                        }
		else if (modified                          ) { res = JR::Done      ; color = Color::Ok      ;                                                        }
		else                                         { res = JR::Steady    ; color = Color::Ok      ;                                                        }
		if      (cri.done()                        )   jr  = res           ;
		else                                         { jr  = JR::Rerun     ; color = Color::Note    ; with_stderr = false ; step = {}                      ; }
		//
		switch (color) {
			case Color::Ok  : if (+stderr          ) color = Color::Warning      ; break ;
			case Color::Err : if (cri.speculate!=No) color = Color::SpeculateErr ; break ;
			default : ;
		}
		if (!step) step = mk_snake(jr) ;
		Trace trace("audit_end",color,pfx,step,*this,cri,STR(modified),jr,STR(+msg),STR(+stderr)) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		req->audit_job(color,pfx+step,*this,true/*at_end*/,exec_time) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		req->stats.ended(jr)++ ;
		req->stats.jobs_time[jr<=JR::Useful] += exec_time ;
		if (with_stderr) req->audit_stderr(msg,stderr,max_stderr_len,1) ;
		return res ;
	}

	//
	// JobData
	//

	::shared_mutex       JobData::_s_target_dirs_mutex ;
	::umap<Node,NodeIdx> JobData::_s_target_dirs       ;
	::umap<Node,NodeIdx> JobData::_s_hier_target_dirs  ;

	::vector<Node> JobData::targets() const {
		::vector<Node> res ;
		for( ::string const& tn : simple_match().static_targets() ) res.emplace_back(tn) ;
		for( Target          t  : star_targets                    ) res.push_back   (t ) ;
		return res ;
	}

	void JobData::_set_pressure_raw(ReqInfo& ri , CoarseDelay pressure ) const {
		Trace trace("set_pressure",idx(),ri,pressure) ;
		Req         req          = ri.req                               ;
		CoarseDelay dep_pressure = ri.pressure + best_exec_time().first ;
		switch (ri.lvl) {
			//                                                                        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case ReqInfo::Lvl::Dep    : for( Dep const& d : deps.subvec(ri.dep_lvl) ) d->        set_pressure( d->req_info(req) ,                            dep_pressure  ) ; break ;
			case ReqInfo::Lvl::Queued :                                               Backend::s_set_pressure( ri.backend       , +idx() , +req , {.pressure=dep_pressure} ) ; break ;
			//                                                                        ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			default : ;
		}
	}

	ENUM(DepErr
	,	Ok
	,	Err                            // >=Err means error
	,	MissingStatic
	)

	static inline bool _inc_cur( Req req , JobLvl jl , int inc ) {
		if (jl==JobLvl::None) return false ;
		JobIdx& stat = req->stats.cur(jl==JobLvl::End?JobLvl::Exec:jl) ;
		if (inc<0) SWEAR( stat>=JobIdx(-inc) , stat , inc ) ;
		stat += inc ;
		return jl!=JobLvl::Done ;
	}
	JobReason JobData::make(
		ReqInfo&           ri
	,	RunAction          run_action
	,	JobReason          reason
	,	Node               asking_
	,	Bool3              speculate
	,	MakeAction         make_action
	,	CoarseDelay const* old_exec_time
	,	bool               wakeup_watchers
	) {
		using Lvl = ReqInfo::Lvl ;
		SWEAR( reason.tag<JobReasonTag::Err , reason ) ;
		Lvl  prev_lvl       = ri.lvl                               ;           // capture previous level before any update
		Req  req            = ri.req                               ;
		bool stop_speculate = speculate<ri.speculate && +ri.action ;
		if (+reason ) run_action = RunAction::Run ;                            // we already have a reason to run
		if (+asking_) asking     = asking_        ;
		ri.speculate = ri.speculate & speculate           ;
		reason       = reason       | JobReason(ri.force) ;                    // ri.force was anterior to incoming reason
		ri.update( run_action , make_action , *this ) ;
		if (!ri.waiting()) {                                                   // we may have looped in which case stats update is meaningless and may fail()
			//
			Special special      = rule->special                                                 ;
			bool    dep_live_out = special==Special::Req && req->options.flags[ReqFlag::LiveOut] ;
			bool    frozen_      = idx().frozen()                                                ;
			//
			Trace trace("Jmake",idx(),ri,prev_lvl,run_action,reason,asking_,STR(speculate),STR(stop_speculate),make_action,old_exec_time?*old_exec_time:CoarseDelay(),STR(wakeup_watchers)) ;
			if (ri.done(ri.action)) goto Wakeup ;
			for (;;) {                                                                     // loop in case analysis must be restarted (only in case of flash execution)
				DepErr      err_          = DepErr::Ok /*garbage*/                       ;
				bool        modif         = false      /*garbage*/                       ;
				bool        sure          = true                                         ;
				CoarseDelay dep_pressure  = ri.pressure + best_exec_time().first         ;
				Idx         n_deps        = special==Special::Infinite ? 0 : deps.size() ; // special case : Infinite actually has no dep, just a list of node showing infinity
				//
				RunAction dep_action = req->options.flags[ReqFlag::Archive] ? RunAction::Dsk : RunAction::Status ;
				//
				if (make_action==MakeAction::End) { dep_action = RunAction::Dsk ; ri.dep_lvl = 0 ; } // if analysing end of job, we need to be certain of presence of all deps on disk
				if (ri.action  ==RunAction::Run )   dep_action = RunAction::Dsk ;                    // if we must run the job , .
				//
				switch (ri.lvl) {
					case Lvl::None :
						if ( ri.action>=RunAction::Status && !ri.force ) {                                                       // only once, not in case of analysis restart
							if      ( rule->force                                           ) ri.force = JobReasonTag::Force   ;
							else if ( frozen_                                               ) ri.force = JobReasonTag::Force   ; // ensure crc are updated, akin sources
							else if ( status==Status::New                                   ) ri.force = JobReasonTag::New     ;
							else if ( status<=Status::Garbage                               ) ri.force = JobReasonTag::Garbage ;
							else if ( !cmd_ok  ()                                           ) ri.force = JobReasonTag::Cmd     ;
							else if ( req->options.flags[ReqFlag::ForgetOldErrors] && err() ) ri.force = JobReasonTag::OldErr  ;
							else if ( !rsrcs_ok()                                           ) ri.force = JobReasonTag::Rsrcs   ;
						}
						ri.lvl = Lvl::Dep ;
					[[fallthrough]] ;
					case Lvl::Dep : {
					RestartAnalysis :                                          // restart analysis here when it is discovered we need deps to run the job
						/**/   err_               = DepErr::Ok ;
						/**/   modif              = false      ;
						bool   seen_waiting       = false      ;
						DepErr proto_err          = DepErr::Ok ;
						bool   proto_modif        = false      ;
						bool   proto_seen_waiting = false      ;
						if ( ri.dep_lvl==0 && +ri.force ) {                    // process command like a dep in parallel with static_deps
							trace("force",ri.force) ;
							SWEAR( !err_ && !proto_err && !modif ) ;           // ensure we dot mask anything important
							proto_modif = true           ;
							reason     |= ri.force       ;
							ri.action   = RunAction::Run ;
							dep_action  = RunAction::Dsk ;
							if (frozen_) break ;                               // no dep analysis for frozen jobs
						}
						ri.speculative_deps = false ;                          // initially, we are not speculatively waiting
						bool  critical_modif   = false               ;
						bool  critical_waiting = false               ;
						Dep   sentinel         { false/*parallel*/ } ;
						for ( NodeIdx i_dep = ri.dep_lvl ; SWEAR(i_dep<=n_deps,i_dep,n_deps),true ; i_dep++ ) {
							DepErr dep_err     = DepErr::Ok                              ;
							bool   dep_modif   = false                                   ;
							bool   seen_all    = i_dep==n_deps                           ;
							Dep&   dep         = seen_all ? sentinel : deps[i_dep]       ; // use empty dep as sentinel
							bool   care        = +dep.accesses                           ; // we care about this dep if we access it somehow
							bool   is_static   =  dep.dflags[Dflag::Static     ]         ;
							bool   is_critical =  dep.dflags[Dflag::Critical   ] && care ;
							bool   sense_err   = !dep.dflags[Dflag::IgnoreError]         ;
							bool   required    =  dep.dflags[Dflag::Required   ]         ;
							//
							if (!dep.parallel) {
								err_         = proto_err          ;                   // proto-modifs become modifs when stamped by a sequential dep
								modif        = proto_modif        ;                   // .
								seen_waiting = proto_seen_waiting ;                   // .
								if ( modif && !err_ ) {                               // this modif is not preceded by an error, we will definitely run the job, prepare for it
									ri.action = RunAction::Run ;
									if (dep_action<RunAction::Dsk) {
										ri.dep_lvl = 0              ;
										dep_action = RunAction::Dsk ;
										trace("restart_analysis") ;
										goto RestartAnalysis ;
									}
								}
								if ( critical_modif && !seen_all ) {
									NodeIdx j = i_dep ;
									for( NodeIdx i=i_dep ; i<n_deps ; i++ )    // suppress deps following modified critical one, except keep static deps as no-access
										if (deps[i].dflags[Dflag::Static]) {
											Dep& d = deps[j++] ;
											d          = deps[i]        ;
											d.accesses = Accesses::None ;
										}
									if (j!=n_deps) {
										deps.shorten_by(n_deps-j) ;
										n_deps   = j             ;
										seen_all = i_dep==n_deps ;
									}
								}
								if ( !err_ && !modif && !ri.waiting() ) ri.dep_lvl = i_dep ; // fast path : all is ok till now, next time, restart analysis after this
								if ( critical_waiting                 ) goto Wait ;          // stop analysis as critical dep may be modified
								if ( seen_all                         ) break     ;          // we are done
							}
							SWEAR(is_static<=required) ;                        // static deps are necessarily required
							Node::ReqInfo const* cdri = &dep->c_req_info(req) ; // avoid allocating req_info as long as not necessary
							Node::ReqInfo      * dri  = nullptr               ; // .
							//
							//
							SWEAR( care || sense_err || required ) ;            // else, what is this useless dep ?
							if (!cdri->waiting()) {
								ReqInfo::WaitInc sav_n_wait{ri} ;                         // appear waiting in case of recursion loop (loop will be caught because of no job on going)
								if (!dri        ) cdri = dri    = &dep->req_info(*cdri) ; // refresh cdri in case dri allocated a new one
								if (dep_live_out) dri->live_out = true                  ; // ask live output for last level if user asked it
								Bool3 speculate_dep =
									is_static             ? ri.speculate          // static deps do not disappear
								:	seen_waiting || modif ?              Yes      // this dep may disappear
								:	+err_                 ? ri.speculate|Maybe    // this dep is not the origin of the error
								:	                        ri.speculate          // this dep will not disappear from us (but we may disappear from asking deps)
								;
								//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								if      (care     ) dep->make( *dri , dep_action         , special==Special::Req?Job():idx() , speculate_dep ) ; // Req jobs are fugitive, dont record it
								else if (sense_err) dep->make( *dri , RunAction::Status  , special==Special::Req?Job():idx() , speculate_dep ) ; // .
								else if (required ) dep->make( *dri , RunAction::Makable , special==Special::Req?Job():idx() , speculate_dep ) ; // .
								//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							}
							if ( is_static && dep->buildable<Buildable::Yes ) sure = false ;              // buildable is better after make()
							if (cdri->waiting()) {
								if      ( is_static                       ) ri.speculative_deps = false ; // we are non-speculatively waiting, even if after a speculative wait
								else if ( !seen_waiting && (+err_||modif) ) ri.speculative_deps = true  ;
								proto_seen_waiting  = true                             ;
								reason             |= {JobReasonTag::DepNotReady,+dep} ;
								if (!dri) cdri = dri = &dep->req_info(*cdri) ;           // refresh cdri in case dri allocated a new one
								dep->add_watcher(*dri,idx(),ri,dep_pressure) ;
								critical_waiting |= is_critical ;
								goto Continue ;
							}
							SWEAR(dep->done(*cdri)) ;                                    // after having called make, dep must be either waiting or done
							dep.acquire_crc() ;                                          // 2nd chance : after make is called as if dep is steady (typically a source), crc may have been computed
							dep_modif = care && !dep.up_to_date() ;
							if ( dep_modif && status==Status::Ok && dep.no_trigger() ) { // no_trigger only applies to successful jobs
								req->no_triggers.emplace(dep,req->no_triggers.size()) ;  // record to repeat in summary, value is just to order summary in discovery order
								dep_modif = false ;
							}
							if ( +err_               ) goto Continue ;                   // we are already in error, no need to analyze errors any further
							if ( !is_static && modif ) goto Continue ;                   // if not static, errors may be washed by previous modifs, dont record them
							//
							{	Bool3 dep_ok = dep->ok(*cdri,dep.accesses) ;
								switch (dep_ok) {
									case Yes   : break ;
									case Maybe :
										if (required) {
											if (is_static) { dep_err = DepErr::MissingStatic ; reason |= {JobReasonTag::DepMissingStatic  ,+dep} ; }
											else           { dep_err = DepErr::Err           ; reason |= {JobReasonTag::DepMissingRequired,+dep} ; }
											trace("missing",STR(is_static),dep) ;
											goto Continue ;
										}
									break ;
									case No :
										trace("dep_err",dep,STR(sense_err)) ;
										if (+cdri->overwritten) { reason |= {JobReasonTag::DepOverwritten,+dep} ; goto Err ; } // overwritten dep is unacceptable, even with !sense_err
										if (sense_err         )                                                   goto Err ;
									break ;
									default : FAIL(dep_ok) ;
								}
								if ( care && dep.is_date && +dep.date() ) {                    // if still waiting for a crc here, it will never come
									if (dep->is_src()) {
										if ( dep->crc!=Crc::None && dep.date()>dep->date() ) { // try to reconcile dates in case dep has been touched with no modification
											dep->manual_refresh(*this) ;
											dep.acquire_crc() ;
											if (dep.is_date) {                                                               // dep has been modified since it was done, make it overwritten
												if (!dri) cdri = dri = &dep->req_info(*cdri) ;                               // refresh cdri in case dri allocated a new one
												dri->overwritten  = dep->crc.diff_accesses(Crc(FileInfo(dep->name()).tag)) ; // overwritten only marked for the accesses that can perceive it
												reason           |= {JobReasonTag::DepOverwritten,+dep} ;                    // overwritten dep is unacceptable, even with !sense_err
												trace("src_overwritten",dep) ;
												goto Err ;
											}
										}
									} else if (dep->running(*cdri)) {
										trace("unstable",dep) ;
										req->audit_node(Color::Warning,"unstable",dep) ;
										dep_modif  = true                             ;                                      // this dep was moving, retry job
										reason    |= {JobReasonTag::DepUnstable,+dep} ;
									} else {
										if (!dri) cdri = dri = &dep->req_info(*cdri) ;                                       // refresh cdri in case dri allocated a new one
										Manual manual = dep->manual_wash(*dri) ;
										if (manual>=Manual::Changed) {                                                       // else condition has been washed
											trace("manual",dep,dep_ok,dep.date(),dep->crc==Crc::None?Ddate():dep->date()) ;
											goto Err ;
										} else if (manual==Manual::Unlinked) {
											trace("washed",dep) ;
											dep_modif  = true                             ;                                  // this dep was moving, retry job
											reason    |= {JobReasonTag::DepNotReady,+dep} ;
										}
									}
								}
								if (dep_modif) reason |= {JobReasonTag::DepChanged,+dep} ;
								goto Continue ;
							}
						Err :
							dep_err  = DepErr::Err                     ;
							reason  |= { JobReasonTag::DepErr , +dep } ;
						Continue :
							trace("dep",ri,dep,*cdri,STR(dep->done(*cdri)),STR(dep->ok()),dep->crc,dep_err,STR(dep_modif),err_,STR(modif),STR(critical_modif),STR(critical_waiting),reason) ;
							//
							SWEAR( !dep_err || !modif || is_static ) ;         // if earlier modifs have been seen, we do not want to record errors as they can be washed, unless static
							proto_err   |= dep_err   ;
							proto_modif |= dep_modif ;
							if ( dep_modif && is_critical ) critical_modif = true ;
						}
						if (ri.waiting()) goto Wait ;
					} break ;
					default : FAIL(ri.lvl) ;
				}
				if (sure) mk_sure() ;                                          // improve sure (sure is pessimistic)
				switch (err_) {
					case DepErr::Ok            : run_status = RunStatus::Complete ; break     ;
					case DepErr::Err           : run_status = RunStatus::DepErr   ; goto Done ; // we cant run the job, error is set and we're done
					case DepErr::MissingStatic : run_status = RunStatus::NoDep    ; goto Done ; // .
					default : FAIL(err_) ;
				}
				trace("run",ri,run_status,err_,STR(modif)) ;
				if (ri.action!=RunAction::Run) goto Done ;                     // we are done with the analysis and we do not need to run : we're done
				//                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				bool maybe_new_deps = submit(ri,reason,dep_pressure) ;
				//                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				if (ri.waiting()   ) goto Wait ;
				if (!maybe_new_deps) goto Done ;                                          // if no new deps, we are done
				make_action = MakeAction::End                                           ; // restart analysis as if called by end() (after ri.update()) as if flash execution, submit has called end()
				ri.lvl      = Lvl       ::Dep                                           ; // .
				ri.action   = is_ok(status)==Maybe ? RunAction::Run : RunAction::Status ; // .
				trace("restart_analysis",ri) ;
			/*never exit*/ }
		Done :
			ri.lvl   = Lvl::Done            ;
			ri.done_ = ri.done_ | ri.action ;
		Wakeup :
			if ( auto it = req->missing_audits.find(idx()) ; it!=req->missing_audits.end() && !req->zombie ) {
				JobAudit const& ja = it->second ;
				trace("report_missing",ja) ;
				IFStream job_stream { ancillary_file() }                                      ;
				/**/                  deserialize<JobInfoStart>(job_stream)                   ;
				::string stderr     = deserialize<JobInfoEnd  >(job_stream).end.digest.stderr ;
				//
				if (ja.report!=JobReport::Hit) {                               // if not Hit, then job was rerun and ja.report is the report that would have been done w/o rerun
					SWEAR(req->stats.ended(JobReport::Rerun)>0) ;
					req->stats.ended(JobReport::Rerun)-- ;                     // we tranform a rerun into a completed job, subtract what was accumulated as rerun
					req->stats.ended(ja.report       )++ ;                     // we tranform a rerun into a completed job, subtract what was accumulated as rerun
					req->stats.jobs_time[false/*useful*/] -= exec_time ;       // exec time is not added to useful as it is not provided to audit_end
					req->stats.jobs_time[true /*useful*/] += exec_time ;       // .
				}
				//
				size_t max_stderr_len ;
				// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
				try {
					Rule::SimpleMatch match ;
					::vmap_ss         rsrcs ;
					if (+(rule->end_none_attrs.need&NeedRsrcs)) {
						try         { rsrcs = deserialize<JobInfoStart>(IFStream(ancillary_file())).rsrcs ; }
						catch (...) {}                                                                        // ignore error as it is purely for cosmetic usage
					}
					max_stderr_len = rule->end_none_attrs.eval(idx(),match,rsrcs).max_stderr_len ;
				} catch (::string const& e) {
					max_stderr_len = rule->end_none_attrs.spec.max_stderr_len ;
					req->audit_job(Color::Note,"dynamic",idx()) ;
					req->audit_stderr( ensure_nl(rule->end_none_attrs.s_exc_msg(true/*using_static*/))+e , {}/*stderr*/ , -1 , 1 ) ;
				}
				if (reason.tag>=JobReasonTag::Err) audit_end( ja.report==JobReport::Hit?"hit_":"was_" , ri , reason_str(reason) , stderr , max_stderr_len , ja.modified ) ;
				else                               audit_end( ja.report==JobReport::Hit?"hit_":"was_" , ri , ja.backend_msg     , stderr , max_stderr_len , ja.modified ) ;
				req->missing_audits.erase(it) ;
			}
			trace("wakeup",ri) ;
			//                                           vvvvvvvvvvvvvvvvvvvv
			if ( wakeup_watchers && ri.done(ri.action) ) ri.wakeup_watchers() ;
			//                                           ^^^^^^^^^^^^^^^^^^^^
		}
	Wait :
		if (stop_speculate) _propag_speculate(ri) ;
		if ( !rule->is_special() && ri.lvl!=prev_lvl ) {
			bool remove_old = _inc_cur(req,prev_lvl,-1) ;
			bool add_new    = _inc_cur(req,ri.lvl  ,+1) ;
			req.new_exec_time( *this , remove_old , add_new , old_exec_time?*old_exec_time:exec_time ) ;
		}
		return reason ;
	}

	void JobData::_propag_speculate(ReqInfo const& cri) const {
		Bool3 proto_speculate = No ;
		Bool3 speculate       = No ;
		for ( Dep const& dep : deps ) {
			if (!dep.parallel) speculate |= proto_speculate ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			dep->propag_speculate( cri.req , cri.speculate | (speculate&(!dep.dflags[Dflag::Static])) ) ; // static deps are never speculative
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			Node::ReqInfo const& cdri = dep->c_req_info(cri.req) ;
			if ( dep.is_date || cdri.waiting() ) { proto_speculate = Yes ; continue ; }
			Bool3 dep_ok = dep->ok(cdri,dep.accesses) ;
			switch (dep_ok) {
				case Yes   :                                                                                                       break ;
				case Maybe : if (  dep.dflags[Dflag::Required   ]                      ) { proto_speculate |= Maybe ; continue ; } break ;
				case No    : if ( !dep.dflags[Dflag::IgnoreError] || +cdri.overwritten ) { proto_speculate |= Maybe ; continue ; } break ;
				default : FAIL(dep_ok) ;
			}
			if ( +dep.accesses && !dep.up_to_date() ) proto_speculate = Yes ;
		}
	}

	::string JobData::special_stderr(Node node) const {
		if (is_ok(status)!=No) return {} ;
		switch (rule->special) {
			case Special::Plain :
				SWEAR(idx().frozen()) ;
				if (+node) return to_string("frozen file does not exist while not phony : ",node->name(),'\n') ;
				else       return           "frozen file does not exist while not phony\n"                     ;
			case Special::Infinite : {
				::string res ;
				for( Dep const& d : ::c_vector_view(deps.items(),g_config.n_errs(deps.size())) ) append_to_string( res , d->name() , '\n' ) ;
				if ( g_config.errs_overflow(deps.size())                                       ) append_to_string( res , "..."     , '\n' ) ;
				return res ;
			}
			default :
				return to_string(rule->special," error\n") ;
		}
	}

	::pair<SpecialStep,Bool3/*modified*/> JobData::_update_target( Node t , ::string const& tn , bool star , NfsGuard& nfs_guard ) {
		FileInfo fi    { nfs_guard.access(tn) }            ;
		bool     plain = t->crc.valid() && t->crc.exists() ;
		if ( plain && +fi && fi.date==t->date() ) return {SpecialStep::Idle,No/*modified*/} ;
		Trace trace("src",fi.date,t->crc==Crc::None?Ddate():t->date()) ;
		Crc   crc      { tn , g_config.hash_algo }                               ;
		Bool3 modified = crc.match(t->crc) ? No : !plain ? Maybe : Yes           ;
		Ddate date     = +fi ? fi.date : t->crc==Crc::None ? Ddate() : t->date() ;
		//vvvvvvvvvvvvvvvvvvvvvv
		t->refresh( crc , date ) ;
		//^^^^^^^^^^^^^^^^^^^^^^
		// if file disappeared, there is not way to know at which date, we are optimistic here as being pessimistic implies false overwrites
		if (+fi )                                 return { SpecialStep::Ok     , modified } ;
		if (star) { t->actual_job_tgt().clear() ; return { SpecialStep::Idle   , modified } ; } // unlink of a star target is nothing
		else                                      return { SpecialStep::NoFile , modified } ;
	}
	bool/*may_new_dep*/ JobData::_submit_special(ReqInfo& ri) {
		Trace trace("submit_special",idx(),ri) ;
		Req     req     = ri.req         ;
		Special special = rule->special  ;
		bool    frozen_ = idx().frozen() ;
		//
		if (frozen_) req->frozen_jobs.emplace(idx(),req->frozen_jobs.size()) ; // record to repeat in summary, value is only for ordering summary in discovery order
		//
		switch (special) {
			case Special::Plain : {
				SWEAR(frozen_) ;                                                      // only case where we are here without special rule
				::vector_s        static_targets = simple_match().static_targets() ;
				SpecialStep       special_step   = SpecialStep::Idle               ;
				Node              worst_target   ;
				Bool3             modified       = No                              ;
				NfsGuard          nfs_guard      { g_config.reliable_dirs }        ;
				for( VarIdx ti=0 ; ti<static_targets.size() ; ti++ ) {
					::string const& tn     = static_targets[ti]      ;
					Node            t      { tn }                    ;
					auto            [ss,m] = _update_target(t,tn,false/*star*/,nfs_guard) ;
					if ( ss==SpecialStep::NoFile && !rule->tflags(ti)[Tflag::Phony] ) ss = SpecialStep::Err ;
					if ( ss>special_step                                            ) { special_step = ss ; worst_target = t ; }
					modified |= m ;
				}
				for( Node t : star_targets ) {
					auto [ss,m] = _update_target(t,t->name(),true/*star*/,nfs_guard) ;
					if (ss==SpecialStep::NoFile) ss = SpecialStep::Err ;
					if (ss>special_step        ) { special_step = ss ; worst_target = t ; }
					modified |= m ;
				}
				status = special_step==SpecialStep::Err ? Status::Err : Status::Ok ;
				audit_end_special( req , special_step , modified , worst_target ) ;
			} break ;
			case Special::Req :
				status = Status::Ok ;
			break ;
			case Special::Infinite :
				status = Status::Err ;
				audit_end_special( req , SpecialStep::Err , No/*modified*/ ) ;
			break ;
			default : FAIL(special) ;
		}
		return false/*may_new_dep*/ ;
	}

	bool/*maybe_new_deps*/ JobData::_submit_plain( ReqInfo& ri , JobReason reason , CoarseDelay pressure ) {
		using Lvl = ReqInfo::Lvl ;
		Req               req                = ri.req  ;
		SubmitRsrcsAttrs  submit_rsrcs_attrs ;
		SubmitNoneAttrs   submit_none_attrs  ;
		CacheNoneAttrs    cache_none_attrs   ;
		Rule::SimpleMatch match              { idx() } ;
		Trace trace("submit_plain",idx(),ri,reason,pressure) ;
		SWEAR(!ri.waiting()) ;
		try {
			submit_rsrcs_attrs = rule->submit_rsrcs_attrs.eval(idx(),match) ;
		} catch (::string const& e) {
			req->audit_job ( Color::Err  , "failed" , idx()                                                                ) ;
			req->audit_info( Color::Note , to_string(rule->submit_rsrcs_attrs.s_exc_msg(false/*using_static*/),'\n',e) , 1 ) ;
			run_status = RunStatus::RsrcsErr ;
			trace("no_rsrcs",ri) ;
			return false/*may_new_deps*/ ;
		}
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		try {
			submit_none_attrs = rule->submit_none_attrs.eval(idx(),match) ;
		} catch (::string const& e) {
			submit_none_attrs = rule->submit_none_attrs.spec ;
			req->audit_job(Color::Note,"no_dynamic",idx()) ;
			req->audit_stderr( rule->submit_none_attrs.s_exc_msg(true/*using_static*/) , e , -1 , 1 ) ;
		}
		try {
			cache_none_attrs = rule->cache_none_attrs.eval(idx(),match) ;
		} catch (::string const& e) {
			cache_none_attrs = rule->cache_none_attrs.spec ;
			req->audit_job(Color::Note,"no_dynamic",idx()) ;
			req->audit_stderr( rule->cache_none_attrs.s_exc_msg(true/*using_static*/) , e , -1 , 1 ) ;
		}
		ri.backend = submit_rsrcs_attrs.backend ;
		for( Req r : running_reqs(false/*with_zombies*/) ) if (r!=req) {
			ReqInfo const& cri = c_req_info(r) ;
			SWEAR( cri.backend==ri.backend , cri.backend , ri.backend ) ;
			ri.n_wait++ ;
			ri.lvl = cri.lvl ;                                                                                   // Exec or Queued, same as other reqs
			if (ri.lvl==Lvl::Exec) req->audit_job(Color::Note,"started",idx()) ;
			Backend::s_add_pressure( ri.backend , +idx() , +req , {.live_out=ri.live_out,.pressure=pressure} ) ; // tell backend of new Req, even if job is started and pressure has become meaningless
			trace("other_req",r,ri) ;
			return false/*may_new_deps*/ ;
		}
		//
		for( ::string const& tn : match.static_targets() ) Node(tn)->set_buildable() ; // we will need to know if target is a source, possibly in another thread, better to call set_buildable here
		for( Node            t  : star_targets           )      t  ->set_buildable() ; // .
		//
		if (+cache_none_attrs.key) {
			Cache*       cache       = Cache::s_tab.at(cache_none_attrs.key) ;
			Cache::Match cache_match = cache->match(idx(),req)               ;
			if (!cache_match.completed) {
				FAIL("delayed cache not yet implemented") ;
			}
			switch (cache_match.hit) {
				case Yes :
					try {
						JobExec  je        { idx() , New , New }      ;                                          // job starts and ends, no host
						NfsGuard nfs_guard { g_config.reliable_dirs } ;
						//
						::pair<vmap<Node,FileAction>,vector<Node>/*warn*/> pa      = pre_actions( match , req->options.flags[ReqFlag::ManualOk] ) ;
						::vmap_s<FileAction>                               actions ; for( auto [t,a] : pa.first ) actions.emplace_back( t->name() , a ) ;
						::pair_s<bool/*ok*/>                               dfa_msg = do_file_actions( ::move(actions) , nfs_guard , g_config.hash_algo ).second/*msg*/ ;
						//
						if ( +dfa_msg.first || !dfa_msg.second ) {
							run_status = RunStatus::TargetErr ;
							req->audit_job ( dfa_msg.second?Color::Note:Color::Err , "manual" , idx()     ) ;
							req->audit_info( Color::Note , dfa_msg.first                              , 1 ) ;
							trace("hit_err",dfa_msg,ri) ;
							if (!dfa_msg.second) return false/*may_new_deps*/ ;
						}
						//
						JobDigest digest = cache->download(idx(),cache_match.id,reason,nfs_guard) ;
						if (+pa.second ) je.report_start(ri,pa.second    ) ;
						if (ri.live_out) je.live_out    (ri,digest.stdout) ;
						ri.lvl = Lvl::Hit ;
						trace("hit_result") ;
						bool modified = je.end({}/*rsrcs*/,digest,{}/*backend_msg*/) ;                           // no resources nor backend for cached jobs
						req->stats.ended(JobReport::Hit)++ ;
						req->missing_audits[idx()] = { JobReport::Hit , modified , {} } ;
						return true/*maybe_new_deps*/ ;
					} catch (::string const&) {}                                                                 // if we cant download result, it is like a miss
				break ;
				case Maybe :
					for( Node d : cache_match.new_deps ) {
						Node::ReqInfo& dri = d->req_info(req) ;
						d->make( dri , RunAction::Status ) ;
						if (dri.waiting()) d->add_watcher(dri,idx(),ri,pressure) ;
					}
					trace("hit_deps") ;
					return true/*maybe_new_deps*/ ;
				case No :
				break ;
				default : FAIL(cache_match.hit) ;
			}
		}
		ri.n_wait++ ;                                                                                            // set before calling submit call back as in case of flash execution, we must be clean
		ri.lvl = Lvl::Queued ;
		try {
			SubmitAttrs sa = {
				.live_out  = ri.live_out
			,	.manual_ok = req->options.flags[ReqFlag::ManualOk] || idx().manual_ok()
			,	.n_retries = submit_none_attrs.n_retries
			,	.pressure  = pressure
			,	.reason    = reason
			} ;
			//       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Backend::s_submit( ri.backend , +idx() , +req , ::move(sa) , ::move(submit_rsrcs_attrs.rsrcs) ) ;
			//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			ri.n_wait-- ;                                                                                        // restore n_wait as we prepared to wait
			status = Status::EarlyErr ;
			req->audit_job ( Color::Err  , "failed" , idx()     ) ;
			req->audit_info( Color::Note , e                , 1 ) ;
			trace("submit_err",ri) ;
			return false/*may_new_deps*/ ;
		} ;
		trace("submitted",ri) ;
		return true/*maybe_new_deps*/ ;
	}

	void JobData::audit_end_special( Req req , SpecialStep step , Bool3 modified , Node node ) const {
		//
		SWEAR( status>Status::Garbage , status ) ;
		Trace trace("audit_end_special",idx(),req,step,modified,status) ;
		//
		bool     frozen_  = idx().frozen()       ;
		::string stderr   = special_stderr(node) ;
		::string step_str ;
		switch (step) {
			case SpecialStep::Idle   :                                                                             break ;
			case SpecialStep::NoFile : step_str = modified!=No || frozen_ ? "no_file" : ""                       ; break ;
			case SpecialStep::Ok     : step_str = modified==Yes ? "changed" : modified==Maybe ? "new" : "steady" ; break ;
			case SpecialStep::Err    : step_str = "failed"                                                       ; break ;
			default : FAIL(step) ;
		}
		Color color =
			status==Status::Ok && !frozen_ ? Color::HiddenOk
		:	status>=Status::Err            ? Color::Err
		:	                                 Color::Warning
		;
		if (frozen_) {
			if (+step_str) step_str += "_frozen" ;
			else           step_str  = "frozen"  ;
		}
		if (+step_str) {
			/**/         req->audit_job (color      ,step_str,idx()  ) ;
			if (+stderr) req->audit_info(Color::None,stderr        ,1) ;
		}
	}

	bool/*ok*/ JobData::forget( bool targets , bool deps_ ) {
		Trace trace("Jforget",idx(),STR(targets),STR(deps_),deps,deps.size()) ;
		for( Req r : running_reqs() ) { (void)r ; return false ; }             // ensure job is not running
		status = Status::New ;
		fence() ;                                                              // once status is New, we are sure target is not up to date, we can safely modify it
		run_status = RunStatus::Complete ;
		if (deps_) {
			NodeIdx j = 0 ;
			for( Dep const& d : deps ) if (d.dflags[Dflag::Static]) deps[j++] = d ;
			if (j!=deps.size()) deps.shorten_by(deps.size()-j) ;
		}
		if (!rule->is_special()) {
			exec_gen = 0 ;
			if (targets) star_targets.clear() ;
		}
		trace("summary",deps) ;
		return true ;
	}

	::vector<Req> JobData::running_reqs(bool with_zombies) const {                                                          // sorted by start
		::vector<Req> res ; res.reserve(Req::s_n_reqs()) ;                                                                  // pessimistic, so no realloc
		for( Req r : Req::s_reqs_by_start ) if ( (with_zombies||!r->zombie) && c_req_info(r).running() ) res.push_back(r) ;
		return res ;
	}

	::string JobData::ancillary_file(AncillaryTag tag) const {
		::string str        = to_string('0',+idx()) ;                          // ensure size is even as we group by 100
		bool     skip_first = str.size()&0x1        ;                          // need initial 0 if required to have an even size
		size_t   i          ;
		::string res        ;
		switch (tag) {
			case AncillaryTag::Backend : res = PrivateAdminDir          + "/backend"s ; break ;
			case AncillaryTag::Data    : res = g_config.local_admin_dir + "/job_data" ; break ;
			case AncillaryTag::Dbg     : res = AdminDir                 + "/debug"s   ; break ;
			case AncillaryTag::KeepTmp : res = AdminDir                 + "/tmp"s     ; break ;
			default : FAIL(tag) ;
		}
		res.reserve( res.size() + str.size() + str.size()/2 + 1 ) ;                                // 1.5*str.size() as there is a / for 2 digits + final _
		for( i=skip_first ; i<str.size()-1 ; i+=2 ) { res.push_back('/') ; res.append(str,i,2) ; } // create a dir hierarchy with 100 files at each level
		res.push_back('_') ;                                                                       // avoid name clashes with directories
		return res ;
	}

}
