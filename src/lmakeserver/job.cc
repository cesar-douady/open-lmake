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

	::pair<vmap_s<bool/*uniquify*/>,vmap<Node,bool/*uniquify*/>/*report*/> JobData::targets_to_wash(Rule::SimpleMatch const& match) const {
		::vmap<Node,bool/*uniquify*/> to_report ;
		::vmap_s<   bool/*uniquify*/> to_wash   ;
		Trace trace("targets_to_wash",idx()) ;
		// handle static targets
		::vector_view_c_s sts = match.static_targets() ;
		for( VarIdx ti=0 ; ti<sts.size() ; ti++ ) {
			::string const& tn = sts[ti] ;
			Node   t     { tn }                                                        ; if (t->crc==Crc::None) { trace("static_no_file",t) ; continue ; } // nothing to wash
			Tflags tf    = rule->tflags(ti)                                            ;
			bool   inc   = tf[Tflag::Uniquify]                                         ;
			bool   warn  = tf[Tflag::Warning ]                                         ;
			bool   weird = inc || ( !t->has_actual_job(idx()) && t->has_actual_job() ) ;
			if (inc) {
				struct ::stat st ;
				if (::lstat(tn.c_str(),&st)<0            ) continue ;
				if (!(S_ISREG(st.st_mode)&&st.st_nlink>1)) continue ;          // no need to manage symlinks as they cannot be modified in place
			}
			if ( weird && warn ) { trace("star",t,STR(inc)) ; to_report.emplace_back(t ,inc) ; }
			/**/                                              to_wash  .emplace_back(tn,inc) ;
		}
		// handle star targets
		Rule::FullMatch fm ;                                                   // lazy evaluated
		for( Target t : star_targets ) {
			if (t->crc==Crc::None) { trace("star_no_file",t) ; continue ; }    // no interest to wash file if it does not exist
			::string tn    ;
			bool     inc   = t.lazy_tflag(Tflag::Uniquify,match,fm,tn)                   ; // may solve fm & tn lazy evalution
			bool     warn  = t.lazy_tflag(Tflag::Warning ,match,fm,tn)                   ; // .
			bool     weird = inc || ( !t->has_actual_job(idx()) && t->has_actual_job() ) ;
			if (inc) {
				struct ::stat st ;
				if (::lstat(tn.c_str(),&st)<0            ) continue ;
				if (!(S_ISREG(st.st_mode)&&st.st_nlink>1)) continue ;          // no need to manage symlinks as they cannot be modified in place
			}
			if ( weird && warn ) { trace("star",t,STR(inc)) ; to_report.emplace_back(t ,inc) ; }
			if ( tn.empty()    ) tn = t.name() ;                                                 // solve lazy evaluation if not already done
			/**/                                              to_wash  .emplace_back(tn,inc) ;
		}
		return {to_wash,to_report} ;
	}

	::vmap<Node,bool/*uniquify*/>/*report*/ JobData::wash(Rule::SimpleMatch const& match) const {
		Trace trace("wash",idx()) ;
		::vmap_s<   bool/*uniquify*/> to_wash   ;
		::vmap<Node,bool/*uniquify*/> to_report ;
		// compute targets to wash
		::tie(to_wash,to_report) = targets_to_wash(match) ;
		// remove old_targets
		::set_s       to_del_dirs   ;                                          // ordered to ensure to_del_dirs are removed deepest first
		::vector_s    to_mk_dirs    = match.target_dirs()    ;
		::set_s       to_mk_dir_set = mk_set(to_mk_dirs)     ;                 // uncomfortable on how a hash tab may work with repetitive calls to begin/erase, safer with a set
		::unique_lock lock          { _s_target_dirs_mutex } ;
		for( auto const& [t,u] : to_wash ) {
			trace("unlink_target",t,STR(u)) ;
			if (u) {
				// uniquify file so as to ensure modifications do not alter other hard links
				int           rfd     = ::open(t.c_str(),O_RDONLY|O_NOFOLLOW)                  ; if (rfd             <0) throw to_string("cannot open for reading : ",t) ;
				struct ::stat st      ;                                                          if (::fstat(rfd,&st)<0) throw to_string("cannot stat : "            ,t) ;
				void*         content = ::mmap(nullptr,st.st_size,PROT_READ,MAP_PRIVATE,rfd,0) ; if (!content          ) throw to_string("cannot map : "             ,t) ;
				int           rc      = ::unlink(t.c_str())                                    ; if (rc              <0) throw to_string("cannot unlink : "          ,t) ;
				int           wfd     = ::open(t.c_str(),O_WRONLY|O_CREAT,st.st_mode&07777)    ; if (wfd             <0) throw to_string("cannot open for writing : ",t) ;
				ssize_t       cnt     = 0/*garbage*/                                           ;
				//
				for( off_t pos=0 ; pos<st.st_size ; pos+=cnt ) {
					cnt = ::write( wfd , reinterpret_cast<char const*>(content)+cnt , st.st_size-pos)  ;
					if (cnt<=0) throw to_string("cannot write : ",t) ;
				}
				//
				struct ::timespec times[2] = { {.tv_sec=0,.tv_nsec=UTIME_OMIT} , st.st_mtim } ;
				::futimens(wfd,times) ;
				//
				::close(wfd) ;
				::munmap(content,st.st_size) ;
				::close(rfd) ;
			} else {
				unlink(t) ;
			}
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
				if      (is_dir(*dir)                          ) to_mk_dir_set.erase(dir)  ;                            // already exists, ok
				else if (Node(*dir)->manual_refresh(*this)==Yes) throw to_string("must unlink but is manual : ",*dir) ;
				else                                             ::unlink(dir->c_str()) ;                               // exists but is not a dir : unlink file and retry
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

	void JobData::end_exec() const {
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
		/**/    os << "J(" ;
		if (+j) os << +j   ;
		return  os << ')'  ;
	}
	::ostream& operator<<( ::ostream& os , JobTgt const jt ) {
		if (!jt) return   os << "JT()"         ;
		/**/              os << "(" << Job(jt) ;
		if (jt.is_sure()) os << ",sure"        ;
		return            os << ')'            ;
	}
	::ostream& operator<<( ::ostream& os , JobExec const je ) {
		if (!je                       ) return os << "JE()"                           ;
		/**/                                   os <<'('<< Job(je)                     ;
		if (je.host!=NoSockAddr       )        os <<','<< SockFd::s_addr_str(je.host) ;
		/**/                                   os <<','<< je.start_date               ;
		if (je.end_date!=je.start_date)        os <<','<< je.end_date                 ;
		/**/                            return os <<')'                               ;
	}

	Job::Job( Rule::FullMatch&& match , Req req , DepDepth lvl ) {
		Trace trace("Job",match,lvl) ;
		if (!match) { trace("no_match") ; return ; }
		Rule rule = match.rule ;
		::vmap_s<AccDflags> dep_names ;
		try {
			dep_names = mk_val_vector(rule->deps_attrs.eval(match)) ;
		} catch (::string const& e) {
			trace("no_dep_subst") ;
			if (+req) {
				req->audit_job(Color::Note,"no_deps",rule,match.name()) ;
				req->audit_stderr( {{rule->deps_attrs.s_exc_msg(false/*using_static*/),{}}} , e , -1 , 1 ) ;
			}
			return ;
		}
		::vmap<Node,AccDflags> deps ; deps.reserve(dep_names.size()) ;
		for( auto [dn,af] : dep_names ) {
			Node d{dn} ;
			//vvvvvvvvvvvvvvvvvvv
			d->set_buildable(lvl) ;
			//^^^^^^^^^^^^^^^^^^^
			if (d->buildable==No) { trace("no_dep",d) ; return ; }
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
			req->audit_stderr( {{rule->create_none_attrs.s_exc_msg(true/*using_static*/),{}}} , e , -1 , 1 ) ;
		}
		trace("found",*this) ;
	}

	//
	// JobExec
	//

	void JobExec::continue_( Req req , bool report ) {
		Trace trace("continue_",*this,req,STR(report)) ;
		ReqInfo& ri = (*this)->req_info(req) ;
		(*this)->make( ri , RunAction::None , {}/*reason*/ , MakeAction::GiveUp ) ;
		if (report) req->audit_job(Color::Note,"continue",*this,true/*at_end*/) ;
		req.chk_end() ;
	}

	void JobExec::not_started() {
		Trace trace("not_started",*this) ;
		for( Req req : (*this)->running_reqs() ) continue_(req,false/*report*/) ;
	}

	// answer to job execution requests
	JobRpcReply JobExec::job_info( JobProc proc , ::vector<Node> const& deps ) const {
		::vector<Req> reqs = (*this)->running_reqs() ;
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
						dep->make( dep->c_req_info(req) , RunAction::Status ) ; // XXX : avoid actually launching jobs if it is behind a critical modif
						trace("dep_info",dep,req) ;
					}
					JobTgt jt = dep->actual_job_tgt                                                    ;
					Bool3  ok = +jt && jt->run_status==RunStatus::Complete ? is_ok(jt->status) : Maybe ;
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
						NodeReqInfo const& cdri = dep->make( dep->c_req_info(req) , RunAction::Dsk ) ; // XXX : avoid actually launching jobs if it is behind a critical modif
						// if dep is waiting for any req, stop analysis as it is complicated what we want to rebuild after
						// and there is no loss of parallelism as we do not wait for completion before doing a full analysis in make()
						if (cdri.waiting()) { trace("waiting",dep) ; return {proc,Maybe} ; }
						bool dep_err = dep->err(cdri) ;
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
		for( Req r : (*this)->running_reqs() ) {
			ReqInfo& ri = (*this)->req_info(r) ;
			if (!ri.live_out) continue ;
			SWEAR(ri.start_reported) ;                                                  // if live_out, start message should not have been deferred
			if (r->last_info!=*this) r->audit_job(Color::HiddenNote,"continue",*this) ; // identify job for which we generate output message
			r->last_info = *this ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			r->audit_info(Color::None,txt,0) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
	}

	void JobExec::report_start( ReqInfo& ri , ::vmap<Node,bool/*uniquify*/> const& report_unlink , ::string const& stderr , ::string const& backend_msg ) const {
		if ( ri.start_reported ) {
			SWEAR( report_unlink.empty() , report_unlink ) ;
			return ;
		}
		ri.req->audit_job( stderr.empty()?Color::HiddenNote:Color::Warning , "start" , *this ) ;
		ri.req->last_info = *this ;
		size_t      w   = 0  ;
		::vmap_s<Node> report_lst ;
		for( auto [t,u] : report_unlink ) {
			::string pfx = u?"uniquified":"unlinked" ;
			if (Job(t->actual_job_tgt)!=Job(*this)) append_to_string(pfx," (generated by",t->actual_job_tgt->rule->name,')') ;
			w = ::max( w , pfx.size() ) ;
			report_lst.emplace_back(pfx,t) ;
		}
		for( auto const& [pfx,t] : report_lst ) ri.req->audit_node( Color::Warning , to_string(::setw(w),pfx) , t , 1 ) ;
		if (!stderr.empty()) ri.req->audit_stderr( backend_msg , {} , stderr , -1 , 1 ) ;
		ri.start_reported = true ;
	}
	void JobExec::report_start() const {
		Trace trace("report_start",*this) ;
		for( Req req : (*this)->running_reqs() ) report_start((*this)->req_info(req)) ;
	}

	void JobExec::started( bool report , ::vmap<Node,bool/*uniquify*/> const& report_unlink , ::string const& stderr , ::string const& backend_msg ) {
		Trace trace("started",*this) ;
		SWEAR( !(*this)->rule->is_special() , (*this)->rule->special ) ;
		for( Req req : (*this)->running_reqs() ) {
			ReqInfo& ri = (*this)->req_info(req) ;
			ri.start_reported = false ;
			if ( report || !report_unlink.empty() || !stderr.empty() ) report_start(ri,report_unlink,stderr,backend_msg) ;
			//
			if (ri.lvl==ReqInfo::Lvl::Queued) {
				req->stats.cur(ReqInfo::Lvl::Queued)-- ;
				req->stats.cur(ReqInfo::Lvl::Exec  )++ ;
				ri.lvl = ReqInfo::Lvl::Exec ;
			}
		}
	}

	bool/*modified*/ JobExec::end( ::vmap_ss const& rsrcs , JobDigest const& digest , ::string const& backend_msg ) {
		Status            status           = digest.status                              ;                             // status will be modified, need to make a copy
		Bool3             ok               = is_ok(status)                              ;
		JobReason         local_reason     = JobReasonTag::None                         ;
		bool              local_err        = false                                      ;
		bool              any_modified     = false                                      ;
		bool              fresh_deps       = status!=Status::Killed && !is_lost(status) ; // if killed or lost, old deps are better than new ones
		Rule              rule             = (*this)->rule                              ;
		::vector<Req>     running_reqs_    = (*this)->running_reqs()                    ;
		AnalysisErr       analysis_err     ;
		CacheNoneAttrs    cache_none_attrs ;
		EndCmdAttrs       end_cmd_attrs    ;
		Rule::SimpleMatch match            ;
		//
		SWEAR(status!=Status::New) ;                                           // we just executed the job, it can be neither new, frozen or special
		SWEAR(!frozen()          ) ;                                           // .
		SWEAR(!rule->is_special()) ;                                           // .
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		try {
			cache_none_attrs = rule->cache_none_attrs.eval(*this,match) ;
		} catch (::string const& e) {
			cache_none_attrs = rule->cache_none_attrs.spec ;
			for( Req req : running_reqs_ ) {
				req->audit_job(Color::Note,"dynamic",*this,true/*at_end*/) ;
				req->audit_stderr( backend_msg , {{rule->cache_none_attrs.s_exc_msg(true/*using_static*/),{}}} , e , -1 , 1 ) ;
			}
		}
		try                       { end_cmd_attrs = rule->end_cmd_attrs.eval(*this,match) ;                               }
		catch (::string const& e) { analysis_err.emplace_back( to_string("cannot compute ",EndCmdAttrs::Msg) , Node() ) ; }
		//
		if (status<=Status::Garbage) {
			if (ok!=No)
				switch (status) {
					case Status::EarlyLost :
					case Status::LateLost  : local_reason = JobReasonTag::Lost    ; break ;
					case Status::Killed    : local_reason = JobReasonTag::Killed  ; break ;
					case Status::ChkDeps   : local_reason = JobReasonTag::ChkDeps ; break ;
					case Status::Garbage   : local_reason = JobReasonTag::Garbage ; break ;
					default : FAIL(status) ;
				}
		} else {
			if (Node::s_has_manual_oks()) {                                    // fast path : no need to wash if there is no manual_ok nodes
				Rule::SimpleMatch match   = (*this)->simple_match() ;          // match must stay alive to hold the static_target names
				::vector<Node>    to_wash ;
				for( ::string const& tn : match.static_targets() ) { Node t{tn} ; if (t.manual_ok()) to_wash.push_back(t) ; }
				for( Node t             : (*this)->star_targets  )                if (t.manual_ok()) to_wash.push_back(t) ;
				Node::s_manual_oks(false/*add*/,to_wash) ;                                                                  // manual_ok is one shot, suppress marks when targets are generated
			}
		}
		//
		(*this)->end_date = Pdate::s_now()                ;
		(*this)->status   = ::min(status,Status::Garbage) ;                    // ensure we cannot appear up to date while working on data
		fence() ;
		//
		Trace trace("end",*this,status) ;
		//
		// handle targets
		//
		if (status>Status::Early) {                                            // if early, we have not touched the targets
			auto report_missing_target = [&](::string const& tn)->void {
				FileInfo fi{tn} ;
				analysis_err.emplace_back( to_string("missing target",(+fi?" (existing)":fi.tag==FileTag::Dir?" (dir)":"")," :") , Node(tn) ) ;
			} ;
			::uset<Node> seen_static_targets ;
			//
			for( Node t : (*this)->star_targets ) if (t->has_actual_job(*this)) t->actual_job_tgt.clear() ; // ensure targets we no more generate do not keep pointing to us
			//
			::vector<Target> star_targets ;                                    // typically, there is either no star targets or they are most of them, lazy reserve if one is seen
			for( auto const& [tn,td] : digest.targets ) {
				Tflags tflags     = td.tflags                                 ;
				Node   target     { tn }                                      ;
				bool   unlink     = td.crc==Crc::None                         ;
				Crc    crc        = td.write || unlink ? td.crc : target->crc ;
				bool   target_err = false                                     ;
				//
				if ( !tflags[Tflag::SourceOk] && td.write && target->is_src() ) {
					local_err = target_err = true ;
					if (unlink) analysis_err.emplace_back("unexpected unlink of source",target) ;
					else        analysis_err.emplace_back("unexpected write to source" ,target) ;
				}
				if (
					td.write                                                   // we actually wrote
				&&	target->has_actual_job() && !target->has_actual_job(*this) // there is another job
				&&	target->actual_job_tgt->end_date>start_date                // dates overlap, which means both jobs were running concurrently (we are the second to end)
				) {
					Job    aj       = target->actual_job_tgt   ;               // common_tflags cannot be tried as target may be unexpected for aj
					VarIdx aj_idx   = aj->full_match().idx(tn) ;               // this is expensive, but pretty exceptional
					Tflags aj_flags = aj->rule->tflags(aj_idx) ;
					trace("clash",*this,tflags,aj,aj_idx,aj_flags,target) ;
					// /!\ This may be very annoying !
					//     Even completed Req's may have been poluted as at the time t->actual_job_tgt completed, it was not aware of the clash.
					//     Putting target in clash_nodes will generate a frightening message to user asking to relaunch all concurrent commands, even past ones.
					//     Note that once we have detected the frightening situation and warned the user, we do not care masking further clashes by overwriting actual_job_tgt.
					if (tflags  [Tflag::Crc]) local_reason |= {JobReasonTag::ClashTarget,+target} ; // if we care about content, we must rerun
					if (aj_flags[Tflag::Crc]) {                                                     // if actual job cares about content, we may have the annoying case mentioned above
						Rule::SimpleMatch aj_match{aj} ;
						for( Req r : (*this)->reqs() ) {
							ReqInfo& ajri = aj->req_info(r) ;
							ajri.done_ = ajri.done_ & RunAction::Status ;                              // whether there is clash or not, this job must be rerun if we need the actual files
							for( Node ajt : aj_match.static_targets() ) if (ajt->done(r)) goto Clash ;
							for( Node ajt : aj->star_targets          ) if (ajt->done(r)) goto Clash ;
							continue ;
						Clash :                                                // one of the targets is done, this is the annoying case
							trace("critical_clash") ;
							r->clash_nodes.emplace(target,r->clash_nodes.size()) ;
						}
					}
				}
				if ( !tflags[Tflag::Incremental] && target->read(td.accesses) ) local_reason |= {JobReasonTag::PrevTarget,+target} ;
				if (crc==Crc::None) {
					// if we have written then unlinked, then there has been a transcient state where the file existed
					// we must consider this is a real target with full clash detection.
					// the unlinked bit is for situations where the file has just been unlinked with no weird intermediate, which is a less dangerous situation
					if ( !RuleData::s_sure(tflags) && !td.write ) {
						target->unlinked = target->crc!=Crc::None ;            // if target was actually unlinked, note it as it is not considered a target of the job
						trace("unlink",target,STR(target->unlinked)) ;
						continue ;                                             // if we are not sure, a target is not generated if it does not exist
					}
					if ( !tflags[Tflag::Star] && !tflags[Tflag::Phony] ) {
						local_err = true ;
						report_missing_target(tn) ;
					}
				}
				if ( !td.write && !tflags[Tflag::Match] ) {
					trace("no_target",target) ;
					continue ;                                                 // not written, no match => not a target
				}
				if ( !target_err && td.write && !unlink && !tflags[Tflag::Write] ) {
					local_err = true ;
					analysis_err.emplace_back("unexpected write to",target) ;
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
					if ( tflags[Tflag::ManualOk] && target->manual(fid)!=No ) crc = {tn,g_config.hash_algo} ;
					else                                                      goto NoRefresh ;
				}
				//         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				modified = target->refresh( crc , fid.date_or_now() ) ;
				//         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			NoRefresh :
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				target->actual_job_tgt = { *this , RuleData::s_sure(tflags) } ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				any_modified |= modified && tflags[Tflag::Match] ;
				trace("target",target,td,STR(modified),status) ;
			}
			if (seen_static_targets.size()<rule->n_static_targets) {           // some static targets have not been seen
				Rule::SimpleMatch match          { *this }                ;    // match must stay alive as long as we use static_targets
				::vector_view_c_s static_targets = match.static_targets() ;
				for( VarIdx ti=0 ; ti<rule->n_static_targets ; ti++ ) {
					::string const& tn = static_targets[ti] ;
					Node            t  { tn }               ;
					if (seen_static_targets.contains(t)) continue ;
					Tflags tflags = rule->tflags(ti) ;
					t->actual_job_tgt = { *this , true/*is_sure*/ } ;
					//                               vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					if (!tflags[Tflag::Incremental]) t->refresh( Crc::None , Ddate::s_now() ) ; // if incremental, target is preserved, else it has been washed at start time
					//                               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					if (!tflags[Tflag::Phony]) {
						local_err = true ;
						if (status==Status::Ok) report_missing_target(tn) ;    // only report if job was ok, else it is quite normal
					}
				}
			}
			::sort(star_targets) ;                                             // ease search in targets
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			(*this)->star_targets.assign(star_targets) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		//
		// handle deps
		//
		if (fresh_deps) {
			Ddate         db_date    ;
			::vector<Dep> dep_vector ; dep_vector.reserve(digest.deps.size()) ;
			::uset<Node>  old_deps   = mk_uset<Node>((*this)->deps) ;
			//
			for( auto const& [dn,dd] : digest.deps ) {                         // static deps are guaranteed to appear first
				Node d{dn} ;
				Dep dep{ d , dd.accesses , dd.dflags , dd.parallel } ;
				dep.known = old_deps.contains(d) ;
				if (dd.garbage) { dep.crc     ({}) ; local_reason |= {JobReasonTag::DepNotReady,+dep} ; } // garbage : force unknown crc
				else            { dep.crc_date(dd) ;                                                    } // date will be transformed into crc in make if possible
				trace("dep",dep,dd,d->db_date()) ;
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
			case Status::Ok      : if ( !digest.stderr.empty() && !end_cmd_attrs.allow_stderr ) { analysis_err.emplace_back("non-empty stderr",Node()) ; local_err = true ; } break ;
			case Status::Timeout :                                                              { analysis_err.emplace_back("timeout"         ,Node()) ;                    } break ;
			default : ;
		}
		EndNoneAttrs end_none_attrs ;
		::string     stderr         ;
		try {
			end_none_attrs = rule->end_none_attrs.eval(*this,match,rsrcs) ;
		} catch (::string const& e) {
			end_none_attrs = rule->end_none_attrs.spec ;
			analysis_err.emplace_back(rule->end_none_attrs.s_exc_msg(true/*using_static*/),Node()) ;
			stderr = ensure_nl(e) ;
		}
		//
		(*this)->exec_ok(true) ;                                               // effect of old cmd has gone away with job execution
		fence() ;                                                              // only update status once every other info is set in case of crash and avoid transforming garbage into Err
		//                                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if      ( +local_reason                   ) (*this)->status = ::min(status,Status::Garbage)  ;
		else if ( local_err && status==Status::Ok ) (*this)->status =              Status::Err       ;
		else if ( !is_lost(status)                ) (*this)->status =       status                   ;
		else if ( status<=Status::Early           ) (*this)->status =              Status::EarlyLost ;
		else                                        (*this)->status =              Status::LateLost  ;
		//                                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
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
			SWEAR( ri.lvl==JobLvl::Exec , ri.lvl ) ;                           // update statistics if this does not hold
			ri.lvl = JobLvl::End ;                                             // we must not appear as Exec while other reqs are analysing or we will wrongly think job is on going
		}
		for( Req req : running_reqs_ ) {
			ReqInfo& ri = (*this)->req_info(req) ;
			trace("req_before",local_reason,status,ri) ;
			req->missing_audits.erase(*this) ;                                 // old missing audit is obsolete as soon as we have rerun the job
			// we call wakeup_watchers ourselves once reports are done to avoid anti-intuitive report order
			//                 vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			JobReason reason = (*this)->make( ri , RunAction::Status , local_reason , end_action , &old_exec_time , false/*wakeup_watchers*/ ) ;
			//                 ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			bool               has_analysis_err = reason.err() || !analysis_err.empty() ;
			AnalysisErr        ae_buf           ;
			AnalysisErr const* ae               = &ae_buf                               ;
			//
			if      (reason.err()               ) ae_buf.push_back(reason.str()) ;
			else if (digest.analysis_err.empty()) ae = &analysis_err             ;
			else {                                                                                           // only concatenate if necessary
				/**/         for( auto const& [t,f] : digest.analysis_err ) ae_buf.emplace_back(t,Node(f)) ;
				if (ok==Yes) for( auto const& [t,n] : analysis_err        ) ae_buf.emplace_back(t,n      ) ; // only show analysis errors if they are at the origin of the failure
			}
			//
			if (ri.done()) {
				if      (reason.err()         ) {}                                // dont show meaningless job stderr
				else if (digest.stderr.empty()) {}                                // avoid concatenation unless necessary
				else if (stderr       .empty()) stderr  = ::move(digest.stderr) ; // .
				else                            stderr +=        digest.stderr  ;
				audit_end( {}/*pfx*/ , ri , backend_msg , *ae , stderr , end_none_attrs.stderr_len , any_modified , digest.stats.total ) ; // report exec time even if not recording it
				trace("wakeup_watchers",ri) ;
				// it is not comfortable to store req-dependent info in a req-independent place, but we need reason from make()
				if ( has_analysis_err && !analysis_stamped ) {                 // this is code is done in such a way as to be fast in the common case (ae empty)
					::string jaf = (*this)->ancillary_file() ;
					try {
						IFStream is{jaf} ;
						auto report_start = deserialize<JobInfoStart>(is) ;
						auto report_end   = deserialize<JobInfoEnd  >(is) ;
						//
						report_end.end.digest.analysis_err.clear() ;
						for( auto const& [t,n] : *ae ) report_end.end.digest.analysis_err.emplace_back(t,+n?n.name():""s) ;
						//
						OFStream os{jaf} ;
						serialize(os,report_start) ;
						serialize(os,report_end  ) ;
					}
					catch (...) {}                                             // in case ancillary file cannot be read, dont record and ignore
					analysis_stamped = true ;
				}
				// as soon as job is done for a req, it is meaningful and justifies to be cached, in practice all reqs agree most of the time
				if ( !cached && !cache_none_attrs.key.empty() && (*this)->run_status==RunStatus::Complete && status==Status::Ok ) {           // cache only successful results
					Cache::s_tab.at(cache_none_attrs.key)->upload( *this , digest ) ;
					cached = true ;
				}
				ri.wakeup_watchers() ;
			} else {
				audit_end( +local_reason?"":"may_" , ri , backend_msg , {reason.str()} , stderr , -1/*stderr_len*/ , any_modified , digest.stats.total ) ; // report 'rerun' rather than status
				req->missing_audits[*this] = { false/*hit*/ , any_modified , *ae } ;
			}
			trace("req_after",ri) ;
			req.chk_end() ;
		}
		trace("summary",*this) ;
		return any_modified ;
	}

	void JobExec::audit_end(
		::string    const& pfx
	,	ReqInfo     const& cri
	,	::string    const& backend_msg
	,	AnalysisErr const& analysis_err
	,	::string    const& stderr
	,	size_t             stderr_len
	,	bool               modified
	,	Delay              exec_time
	) const {
		static const AnalysisErr s_no_ae ;
		//
		Req                req   = cri.req                       ;
		JobData const&     jd    = **this                        ;
		Color              color = Color    ::Unknown/*garbage*/ ;
		JobReport          jr    = JobReport::Unknown            ;
		::string           step  ;
		AnalysisErr const* ae    = &analysis_err                 ;
		//
		if      (!cri.done()                       ) { jr = JobReport::Rerun  ;                                  color = Color::Note ; }
		else if (jd.run_status!=RunStatus::Complete) { jr = JobReport::Failed ; step = mk_snake(jd.run_status) ; color = Color::Err  ; }
		else if (jd.status==Status::Killed         ) {                          step = mk_snake(jd.status    ) ; color = Color::Err  ; }
		else if (jd.status==Status::Timeout        ) { jr = JobReport::Failed ; step = mk_snake(jd.status    ) ; color = Color::Err  ; }
		else if (jd.err()                          ) { jr = JobReport::Failed ;                                  color = Color::Err  ; }
		else if (modified                          ) { jr = JobReport::Done   ;                                  color = Color::Ok   ; }
		else                                         { jr = JobReport::Steady ;                                  color = Color::Ok   ; }
		//
		if ( color==Color::Ok && !stderr.empty() )   color = Color::Warning ;
		if ( is_lost(jd.status)                  ) { step  = "lost"         ; ae = &s_no_ae ; req->losts.emplace(*this,req->losts.size()) ; } // req->losts for summary
		if ( step.empty()                        )   step  = mk_snake(jr)   ;
		if ( !pfx.empty()                        )   step  = pfx+step       ;
		//
		Trace trace("audit_end",color,step,*this,cri,STR(modified)) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		req->audit_job(color,step,*this,true/*at_end*/,exec_time) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (jr!=JobReport::Unknown) {
			if (+exec_time) {                                                  // if no exec time, no job was actually run
				req->stats.ended(jr)++ ;
				req->stats.jobs_time[cri.done()/*useful*/] += exec_time ;
			}
			req->audit_stderr(backend_msg,*ae,stderr,stderr_len,1) ;
		}
	}

	//
	// JobData
	//

	::shared_mutex    JobData::_s_target_dirs_mutex ;
	::umap_s<NodeIdx> JobData::_s_target_dirs       ;

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

	ENUM(State
	,	Ok
	,	ProtoModif                     // modified dep has been seen but still processing parallel deps
	,	Modif
	,	Err
	,	MissingStatic
	)

	static inline bool _inc_cur( Req req , JobLvl jl , int inc ) {
		if (jl==JobLvl::None) return false ;
		JobIdx& stat = req->stats.cur(jl==JobLvl::End?JobLvl::Exec:jl) ;
		if (inc<0) SWEAR( stat>=JobIdx(-inc) , stat , inc ) ;
		stat += inc ;
		return jl!=JobLvl::Done ;
	}
	JobReason JobData::_make_raw( ReqInfo& ri , RunAction run_action , JobReason reason , MakeAction make_action , CoarseDelay const* old_exec_time , bool wakeup_watchers ) {
		using Lvl = ReqInfo::Lvl ;
		SWEAR( !reason.err() , reason ) ;
		Lvl  before_lvl = ri.lvl        ;                                      // capture previous state before any update
		Req  req        = ri.req        ;
		if (+reason) run_action = RunAction::Run ;                             // we already have a reason to run
		ri.update( run_action , make_action , *this ) ;
		if (!ri.waiting()) {                                                   // we may have looped in which case stats update is meaningless and may fail()
			//
			Special special      = rule->special                                                    ;
			bool    is_uphill    = special==Special::Uphill                                         ;
			bool    dep_live_out = special==Special::Req    && req->options.flags[ReqFlag::LiveOut] ;
			bool    frozen_      = frozen()                                                         ;
			//
			Trace trace("Jmake",idx(),ri,before_lvl,run_action,reason,make_action,old_exec_time?*old_exec_time:CoarseDelay(),STR(wakeup_watchers)) ;
			if (ri.done(ri.action)) goto Wakeup ;
			for (;;) {                                                                    // loop in case analysis must be restarted (only in case of flash execution)
				State       state        = State::Ok                                    ;
				bool        sure         = rule->is_sure()                              ; // if rule is not sure, it means targets are never sure
				CoarseDelay dep_pressure = ri.pressure + best_exec_time().first         ;
				Idx         n_deps       = special==Special::Infinite ? 0 : deps.size() ; // special case : Infinite actually has no dep, just a list of node showing infinity
				//
				RunAction dep_action = req->options.flags[ReqFlag::Archive] ? RunAction::Dsk : RunAction::Status ;
				//
				//
				if (make_action==MakeAction::End) { dep_action = RunAction::Dsk ; ri.dep_lvl  = 0 ; } // if analysing end of job, we need to be certain of presence of all deps on disk
				if (ri.action  ==RunAction::Run ) { dep_action = RunAction::Dsk ;                   } // if we must run the job , .
				//
				switch (ri.lvl) {
					case Lvl::None :
						if (ri.action>=RunAction::Status) {                                                                     // only once, not in case of analysis restart
							if      ( !cmd_ok  ()                                           ) ri.force = JobReasonTag::Cmd    ;
							else if ( !rsrcs_ok()                                           ) ri.force = JobReasonTag::Rsrcs  ;
							else if ( frozen_                                               ) ri.force = JobReasonTag::Force  ; // ensure crc are updated, akin sources
							else if ( rule->force                                           ) ri.force = JobReasonTag::Force  ;
							else if ( req->options.flags[ReqFlag::ForgetOldErrors] && err() ) ri.force = JobReasonTag::OldErr ;
						}
						ri.lvl = Lvl::Dep ;
					[[fallthrough]] ;
					case Lvl::Dep : {
					RestartAnalysis :                                          // restart analysis here when it is discovered we need deps to run the job
						if ( ri.dep_lvl==0 && +ri.force ) {                    // process command like a dep in parallel with static_deps
							trace("force",ri.force) ;
							SWEAR( state<=State::ProtoModif , state ) ;        // ensure we dot mask anything important
							state       = State::ProtoModif ;
							reason     |= ri.force          ;
							ri.action   = RunAction::Run    ;
							dep_action  = RunAction::Dsk    ;
							if (frozen_) break ;                               // no dep analysis for frozen jobs
						}
						ri.speculative = false ;                               // initiallly, we are not speculatively waiting
						bool  critical_modif   = false ;
						bool  critical_waiting = false ;
						Bool3 seen_waiting     = No    ;                       // Maybe means that waiting has been seen in the same parallel deps (much like ProtoModif for modifs)
						Dep   sentinel         ;
						for ( NodeIdx i_dep = ri.dep_lvl ; SWEAR(i_dep<=n_deps,i_dep,n_deps),true ; i_dep++ ) {
							State dep_state   = State::Ok                         ;
							bool  seen_all    = i_dep==n_deps                     ;
							Dep&  dep         = seen_all ? sentinel : deps[i_dep] ; // use empty dep as sentinel
							bool  is_static   =  dep.dflags[Dflag::Static     ]   ;
							bool  is_critical =  dep.dflags[Dflag::Critical   ]   ;
							bool  sense_err   = !dep.dflags[Dflag::IgnoreError]   ;
							bool  required    =  dep.dflags[Dflag::Required   ]   ;
							bool  care        = +dep.accesses                     ; // we care about this dep if we access it somehow
							//
							if (!dep.parallel) {
								if ( state       ==State::ProtoModif ) state        = State::Modif ; // proto-modifs become modifs when stamped by a sequential dep
								if ( seen_waiting==Maybe             ) seen_waiting = Yes          ; // seen_waiting becomes Yes when stamped by a sequential dep
								if ( critical_modif && !seen_all     ) {
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
								if ( state==State::Ok && !ri.waiting() ) ri.dep_lvl = i_dep ; // fast path : all is ok till now, next time, restart analysis after this
								if ( critical_waiting                  ) goto Wait ;          // stop analysis as critical dep may be modified
								if ( seen_all                          ) break     ;          // we are done
							}
							SWEAR(is_static<=required) ;                                                        // static deps are necessarily required
							Node::ReqInfo const* cdri              = &dep->c_req_info(req)                    ; // avoid allocating req_info as long as not necessary
							bool                 overwritten       = false                                    ;
							bool                 maybe_speculative = state==State::Modif || seen_waiting==Yes ; // this dep may disappear
							//
							if ( !care && !required ) {                         // dep is useless
								SWEAR( special==Special::Infinite , special ) ; // this is the only case
								goto Continue ;
							}
							if (!cdri->waiting()) {
								dep.acquire_crc() ;                                           // 1st chance : before calling make as it can be destroyed in case of flash execution
								SaveInc save_wait{ri.n_wait} ;                                // appear waiting in case of recursion loop (loop will be caught because of no job on going)
								if (dep_live_out) {                                           // ask live output for last level if user asked it
									Node::ReqInfo& dri = dep->req_info(*cdri) ; cdri = &dri ; // refresh cdri in case dri allocated a new one
									dri.live_out = true ;
								}
								//                  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
								if      (care     ) cdri = &dep->make( *cdri , dep_action         ) ; // refresh cdri if make changed it
								else if (sense_err) cdri = &dep->make( *cdri , RunAction::Status  ) ; // .
								else                cdri = &dep->make( *cdri , RunAction::Makable ) ; // .
								//                  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
							}
							if ( is_static && dep->buildable!=Yes ) sure = false ; // buildable is better after make()
							if (cdri->waiting()) {
							ri.speculative |= maybe_speculative && !is_static ; // we are speculatively waiting
								reason |= {JobReasonTag::DepNotReady,+dep} ;
								Node::ReqInfo& dri = dep->req_info(*cdri) ; cdri = &dri ; // refresh cdri in case dri allocated a new one
								dep->add_watcher(dri,idx(),ri,dep_pressure) ;
								critical_waiting |= is_critical ;
								seen_waiting     |= Maybe       ;              // transformed into Yes upon sequential dep
								goto Continue ;
							}
							{	SWEAR(dep->done(*cdri)) ;                      // after having called make, dep must be either waiting or done
								dep.acquire_crc() ;                            // 2nd chance : after make is called as if dep is steady (typically a source), crc may have been computed
								bool is_modif = !dep.up_to_date() ;
								if ( is_modif && status==Status::Ok && dep.no_trigger() ) { // no_trigger only applies to successful jobs
									req->no_triggers.emplace(dep,req->no_triggers.size()) ; // record to repeat in summary, value is just to order summary in discovery order
									is_modif = false ;
								}
								if ( is_modif                          ) dep_state = State::ProtoModif ; // if not overridden by an error
								if ( !is_static && state>=State::Modif ) goto Continue ;                 // if not static, maybe all the following errors will be washed by previous modif
								//
								bool makable = dep->makable(is_uphill) ;       // sub-files of makable dir are not buildable, except for Uphill so sub-sub-files are not buildable
								if (!makable) {
									if (is_static) {
										dep_state  = State::MissingStatic                  ;
										reason    |= {JobReasonTag::DepMissingStatic,+dep} ;
										trace("missing_static",dep) ;
										goto Continue ;
									}
									if ( care && (dep.is_date?+dep.date():!dep.crc().match(Crc::None)) ) { // file has been seen by job as existing
										if (is_target(dep.name())) {                                       // file still exists, still dangling
											if (!dep->makable(true/*uphill_ok*/)) {
												req->audit_node(Color::Err ,"dangling"          ,dep  ) ;
												req->audit_node(Color::Note,"consider : git add",dep,1) ;
												trace("dangling",dep) ;
											}
											goto MarkDep ;
										} else {
											dep.crc({}) ;                      // file does not exist any more, it has been removed
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
									case NodeErr::None        :                      break                                        ;
									case NodeErr::Overwritten : overwritten = true ; goto Err                                     ;
									case NodeErr::Dangling    :                      if (sense_err) goto Err ; else goto Continue ;
									default : FAIL(cdri->err) ;
								}
								if ( sense_err && dep->err(is_uphill) ) {      // dep errors implies it makable, which takes an argument, we must provide it
									trace("dep_err",dep) ;
									goto Err ;
								}
								if (
									( dep.is_date                                                              ) // if still waiting for a crc here, it will never come
								||	( +dep.accesses && dep.known && make_action==MakeAction::End && !dep.crc() ) // when ending a job, known accessed deps should have a crc
								) {
									if (is_target(dep.name())) {               // file still exists, still manual
										if (dep->is_src()) goto Overwriting ;
										for( Job j : dep->conform_job_tgts(*cdri) )
											for( Req r : j->running_reqs() )
												if (j->c_req_info(r).lvl==Lvl::Exec) goto Overwriting ;
										req->audit_node(Color::Err,"manual",dep) ;                          // well, maybe a job is writing to dep as an unknown target, but we then cant distinguish
										req->manuals.emplace(dep,::pair(false/*ok*/,req->manuals.size())) ;
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
							{	Node::ReqInfo& dri = dep->req_info(*cdri) ; cdri = &dri ;          // refresh cdri in case dri allocated a new one
								dri.err = overwritten ? NodeErr::Overwritten : NodeErr::Dangling ;
							}
						Err :
							dep_state  = State::Err                                                               ;
							reason    |= { overwritten?JobReasonTag::DepOverwritten:JobReasonTag::DepErr , +dep } ;
						Continue :
							trace("dep",dep,STR(dep->done(*cdri)),STR(dep->err(*cdri)),ri,dep->crc,dep_state,state,STR(critical_modif),STR(critical_waiting),reason) ;
							//
							SWEAR(dep_state!=State::Modif) ;                                                            // dep_state only generates dangling modifs
							if ( is_critical && care && dep_state==State::ProtoModif     ) critical_modif = true      ;
							if ( dep_state>state && ( is_static || state!=State::Modif ) ) state          = dep_state ; // Modif blocks errors, unless dep is static
						}
						if (ri.waiting()) goto Wait ;
					} break ;
					default : FAIL(ri.lvl) ;
				}
				if (sure) mk_sure() ;                                          // improve sure (sure is pessimistic)
				switch (state) {
					case State::Ok            :
					case State::ProtoModif    :                                                // if last dep is parallel, we have not transformed ProtoModif into Modif
					case State::Modif         : run_status = RunStatus::Complete ; break     ;
					case State::Err           : run_status = RunStatus::DepErr   ; goto Done ; // we cant run the job, error is set and we're done
					case State::MissingStatic : run_status = RunStatus::NoDep    ; goto Done ; // .
					default : fail(state) ;
				}
				trace("run",ri,run_status,state) ;
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
			}
		Done :
			ri.lvl   = Lvl::Done            ;
			ri.done_ = ri.done_ | ri.action ;
		Wakeup :
			if ( auto it = req->missing_audits.find(idx()) ; it!=req->missing_audits.end() && !req->zombie ) {
				JobAudit const& ja = it->second ;
				trace("report_missing",ja) ;
				IFStream job_stream   { ancillary_file() }                    ;
				/**/                    deserialize<JobInfoStart>(job_stream) ;
				auto     report_end   = deserialize<JobInfoEnd  >(job_stream) ;
				//
				if (!ja.hit) {
					SWEAR(req->stats.ended(JobReport::Rerun)>0) ;
					req->stats.ended(JobReport::Rerun)-- ;                     // we tranform a rerun into a completed job, subtract what was accumulated as rerun
					req->stats.jobs_time[false/*useful*/] -= exec_time ;       // exec time is not added to useful as it is not provided to audit_end
					req->stats.jobs_time[true /*useful*/] += exec_time ;       // .
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
					end_none_attrs = rule->end_none_attrs.eval(idx(),match,rsrcs) ;
				} catch (::string const& e) {
					end_none_attrs = rule->end_none_attrs.spec ;
					req->audit_job(Color::Note,"dynamic",idx()) ;
					req->audit_stderr( {{rule->end_none_attrs.s_exc_msg(true/*using_static*/),{}}} , e , -1 , 1 ) ;
				}
				analysis_err.push_back(reason.str()) ;
				if ( reason.err() || no_info ) audit_end( ja.hit?"hit_":"was_" , ri , analysis_err    , report_end.end.digest.stderr , end_none_attrs.stderr_len , ja.modified ) ;
				else                           audit_end( ja.hit?"hit_":"was_" , ri , ja.analysis_err , report_end.end.digest.stderr , end_none_attrs.stderr_len , ja.modified ) ;
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
			req.new_exec_time( *this , remove_old , add_new , old_exec_time?*old_exec_time:exec_time ) ;
		}
		return reason ;
	}

	::string JobData::special_stderr(Node node) const {
		if (is_ok(status)!=No) return {} ;
		switch (rule->special) {
			case Special::Plain :
				SWEAR(frozen()) ;
				if (+node) return to_string("frozen file does not exist while not phony : ",node.name(),'\n') ;
				else       return           "frozen file does not exist while not phony\n"                    ;
			case Special::Infinite : {
				size_t   n_deps      = deps.size()                                             ;
				bool     truncate    = g_config.max_err_lines && n_deps>g_config.max_err_lines ;
				size_t   n_show_deps = truncate ? g_config.max_err_lines-1 : n_deps            ; // limit output length, including last line (...)
				::string res         ;
				for( size_t i=1 ; i<=n_show_deps ; i++ ) { res+=deps[n_deps-i].name() ; res+='\n' ; }
				if (truncate)                            { res+="...\n"               ;             }
				return res ;
			}
			case Special::Src :
				if (frozen()) return "frozen file does not exist\n" ;
				else          return "file does not exist\n"        ;
			default :
				return to_string(rule->special," error\n") ;
		}
	}

	static ::pair<SpecialStep,Bool3/*modified*/> _update_target( JobData& j , Node t , ::string const& tn , VarIdx ti=-1/*star*/ ) {
		FileInfoDate fid{tn} ;
		if ( +fid && fid.date==t->date && +t->crc ) return {SpecialStep::Idle,No/*modified*/} ;
		Trace trace("src",fid.date,t->date) ;
		Crc   crc      { tn , g_config.hash_algo }                                           ;
		Bool3 modified = crc.match(t->crc) ? No : !t->crc || t->crc==Crc::None ? Maybe : Yes ;
		Ddate date     = +fid ? fid.date : t->date                                           ;
		//vvvvvvvvvvvvvvvvvvvvvv
		t->refresh( crc , date ) ;
		//^^^^^^^^^^^^^^^^^^^^^^
		// if file disappeared, there is not way to know at which date, we are optimistic here as being pessimistic implies false overwrites
		if (+fid                       ) { j.db_date = date ;          return { SpecialStep::Ok     , modified } ; }
		if (ti==VarIdx(-1)             ) { t->actual_job_tgt.clear() ; return { SpecialStep::Idle   , modified } ; } // unlink of a star target is nothing
		else                             {                             return { SpecialStep::NoFile , modified } ; }
	}
	bool/*may_new_dep*/ JobData::_submit_special(ReqInfo& ri) {
		Trace trace("submit_special",idx(),ri) ;
		Req     req     = ri.req        ;
		Special special = rule->special ;
		bool    frozen_ = frozen()      ;
		//
		if (frozen_) req->frozens.emplace(idx(),req->frozens.size()) ;         // record to repeat in summary, value is only for ordering summary in discovery order
		//
		switch (special) {
			case Special::Plain : {
				SWEAR(frozen_) ;                                               // only case where we are here without special rule
				Rule::SimpleMatch match          { idx() }                ;    // match lifetime must be at least as long as static_targets lifetime
				::vector_view_c_s static_targets = match.static_targets() ;
				SpecialStep       special_step   = SpecialStep::Idle      ;
				Node              worst_target   ;
				Bool3             modified       = No                     ;
				for( VarIdx ti=0 ; ti<static_targets.size() ; ti++ ) {
					::string const& tn     = static_targets[ti]                    ;
					Node            t      { tn }                                  ;
					auto            [ss,m] = _update_target( *this , t , tn , ti ) ;
					if ( ss==SpecialStep::NoFile && !rule->tflags(ti)[Tflag::Phony] ) ss = SpecialStep::Err ;
					if ( ss>special_step                                            ) { special_step = ss ; worst_target = t ; }
					modified |= m ;
				}
				for( Node t : star_targets ) {
					auto [ss,m] = _update_target( *this , t , t.name() ) ;
					if (ss==SpecialStep::NoFile) ss = SpecialStep::Err ;
					if (ss>special_step        ) { special_step = ss ; worst_target = t ; }
					modified |= m ;
				}
				status = special_step==SpecialStep::Err ? Status::Err : Status::Ok ;
				audit_end_special( req , special_step , modified , worst_target ) ;
			} break ;
			case Special::Src        :
			case Special::GenericSrc : {
				::string tn          = name()                                     ;
				Node     t           { tn }                                       ;
				bool     is_true_src = special==Special::Src                      ;
				auto     [ss,m]      = _update_target( *this , t , tn , 0/*ti*/ ) ;
				t->actual_job_tgt = {idx(),is_true_src/*is_sure*/} ;
				if      (ss!=SpecialStep::NoFile) { status = Status::Ok  ;                                  }
				else if (is_true_src            ) { status = Status::Err ; ss         = SpecialStep::Err  ; }
				else                              { status = Status::Ok  ; run_status = RunStatus::NoFile ; }
				audit_end_special(req,ss,m) ;
			} break ;
			case Special::Req :
				status = Status::Ok ;
			break ;
			case Special::Infinite :
				status = Status::Err ;
				audit_end_special( req , SpecialStep::Err , No/*modified*/ ) ;
			break ;
			case Special::Uphill :
				for( Dep const& d : deps ) {
					// if we see a link uphill, then our crc is unknown to trigger rebuild of dependents
					// there is no such stable situation as link will be resolved when dep is acquired, only when link appeared, until next rebuild
					Node t{name()} ;
					t->actual_job_tgt = {idx(),true/*is_sure*/} ;
					if ( d->crc.is_lnk() || !d->crc ) t->refresh( {}        , {}             ) ;
					else                              t->refresh( Crc::None , Ddate::s_now() ) ;
				}
				status = Status::Ok ;
			break ;
			default : fail() ;
		}
		return false/*may_new_dep*/ ;
	}

	bool/*targets_ok*/ JobData::_targets_ok( Req req , Rule::SimpleMatch const& match ) {
		Trace trace("_targets_ok",idx(),req) ;
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
		for( Dep const& dep : deps ) {
			if (!dep.dflags[Dflag::Static]      ) {       break    ; }
			if (!static_target_map.contains(dep)) { d++ ; continue ; }
			::string dep_key = rule->deps_attrs.spec.full_dynamic ? ""s : rule->deps_attrs.spec.deps[d].first ;
			::string err_msg = to_string("simultaneously static target ",rule->targets[static_target_map[dep]].first," and static dep ",dep_key," : ") ;
			req->audit_job ( Color::Err  , "clash" , idx()     ) ;
			req->audit_node( Color::Note , err_msg , dep   , 1 ) ;
			run_status = RunStatus::DepErr ;
			trace("clash") ;
			return false ;
		}
		if ( is_lost(status) && status>Status::Early ) {
			trace("job_lost") ;
			return true ;                                                      // targets may have been modified but job may not have reported it
		}
		// check manual targets
		::vmap<Node,bool/*ok*/> manual_targets ;
		for( VarIdx ti=0 ; ti<static_target_nodes.size() ; ti++ ) {
			Node t = static_target_nodes[ti] ;
			if (t->manual_refresh(req,FileInfoDate(static_target_names[ti]))==Yes)
				manual_targets.emplace_back( t , rule->tflags(ti)[Tflag::ManualOk]||t.manual_ok() ) ;
		}
		Rule::FullMatch fm ;                                                   // lazy evaluated
		for( Target t : star_targets ) {
			::string tn = t.name() ;
			if (t->manual_refresh(req,FileInfoDate(tn))==Yes)
				manual_targets.emplace_back( t , t.lazy_tflag(Tflag::ManualOk,match,fm,tn)||t.manual_ok() ) ; // may solve fm lazy evaluation, tn is already ok
		}
		//
		bool job_ok = true ;
		for( auto const& [t,ok] : manual_targets ) {
			trace("manual",t,STR(ok)) ;
			bool target_ok = ok || req->options.flags[ReqFlag::ManualOk] ;
			req->audit_job( target_ok?Color::Note:Color::Err , "manual" , rule , t.name() ) ;
			job_ok &= target_ok ;
			req->manuals.emplace(t,::pair(target_ok,req->manuals.size())) ;
		}
		if (job_ok) return true ;
		// generate a message that is simultaneously consise, informative and executable (with a copy/paste) with sh & csh syntaxes
		req->audit_info( Color::Note , "consider :" , 1 ) ;
		for( ::string const& tn : static_target_names ) { Node t{tn} ; if (!t->is_src()) { req->audit_node( Color::Note , "lmake -m" , t , 2 ) ; goto Advised ; } }
		for( Node            t  : star_targets        ) {              if (!t->is_src()) { req->audit_node( Color::Note , "lmake -m" , t , 2 ) ; goto Advised ; } }
	Advised :
		for( auto const& [t,ok] : manual_targets ) {
			if (ok) continue ;
			Ddate   td    = file_date(t.name())            ;
			uint8_t n_dec = (td-t->date)>Delay(2.) ? 0 : 3 ;                   // if dates are far apart, probably a human action and short date is more comfortable, else be precise
			req->audit_node(
				Color::Note
			,	t->crc==Crc::None ?
					to_string( ": touched " , td.str(0    ) , " not generated"                   , " ; rm" )
				:	to_string( ": touched " , td.str(n_dec) , " generated " , t->date.str(n_dec) , " ; rm" )
			,	t
			,	2
			) ;
		}
		run_status = RunStatus::TargetErr ;
		trace("target_is_manual") ;
		return false ;
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
			req->audit_stderr( {{rule->submit_none_attrs.s_exc_msg(true/*using_static*/),{}}} , e , -1 , 1 ) ;
		}
		try {
			cache_none_attrs = rule->cache_none_attrs.eval(idx(),match) ;
		} catch (::string const& e) {
			cache_none_attrs = rule->cache_none_attrs.spec ;
			req->audit_job(Color::Note,"no_dynamic",idx()) ;
			req->audit_stderr( {{rule->cache_none_attrs.s_exc_msg(true/*using_static*/),{}}} , e , -1 , 1 ) ;
		}
		ri.backend = submit_rsrcs_attrs.backend ;
		for( Req r : running_reqs() ) if (r!=req) {
			ReqInfo const& cri = c_req_info(r) ;
			SWEAR( cri.backend==ri.backend , cri.backend , ri.backend ) ;
			ri.n_wait++ ;
			ri.lvl = cri.lvl ;                                                   // Exec or Queued, same as other reqs
			if (ri.lvl==Lvl::Exec) req->audit_job(Color::Note,"started",idx()) ;
			Backend::s_add_pressure( ri.backend , +idx() , +req , {.live_out=ri.live_out,.pressure=pressure} ) ; // tell backend of new Req, even if job is started and pressure has become meaningless
			trace("other_req",r,ri) ;
			return false/*may_new_deps*/ ;
		}
		//
		if (!_targets_ok(req,match)) return false /*may_new_deps*/ ;
		//
		if (!cache_none_attrs.key.empty()) {
			Cache*       cache       = Cache::s_tab.at(cache_none_attrs.key) ;
			Cache::Match cache_match = cache->match(idx(),req)               ;
			if (!cache_match.completed) {
				FAIL("delayed cache not yet implemented") ;
			}
			switch (cache_match.hit) {
				case Yes :
					try {
						JobExec                       je            { idx() , Pdate::s_now() }              ;
						::vmap<Node,bool/*uniquify*/> report_unlink = wash(match)                           ;
						JobDigest                     digest        = cache->download(idx(),cache_match.id) ;
						ri.lvl = Lvl::Hit ;
						je.report_start(ri,report_unlink) ;
						trace("hit_result") ;
						bool modified = je.end({}/*rsrcs*/,digest,{}/*backend_msg*/) ; // no resources nor backend for cached jobs
						req->stats.ended(JobReport::Hit)++ ;
						req->missing_audits[idx()] = { true/*hit*/ , modified , {} } ;
						return true/*maybe_new_deps*/ ;
					} catch (::string const&) {}                               // if we cant download result, it is like a miss
				break ;
				case Maybe :
					for( Node d : cache_match.new_deps ) {
						Node::ReqInfo const& cdri = d->make( d->c_req_info(req) , RunAction::Status ) ;
						if (cdri.waiting()) d->add_watcher(d->req_info(cdri),idx(),ri,pressure) ;
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
			Backend::s_submit( ri.backend , +idx() , +req , ::move(sa) , ::move(submit_rsrcs_attrs.rsrcs) ) ;
			//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {
			ri.n_wait-- ;                                                      // restore n_wait as we prepared to wait
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
		bool     frozen_  = frozen()             ;
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
			if (step_str.empty()) step_str  = "frozen"  ;
			else                  step_str += "_frozen" ;
		}
		if (!step_str.empty()) {
			/**/                 req->audit_job (color      ,step_str,idx()  ) ;
			if (!stderr.empty()) req->audit_info(Color::None,stderr        ,1) ;
		}
	}

	bool/*ok*/ JobData::forget( bool targets , bool deps_ ) {
		Trace trace("Jforget",idx(),STR(targets),STR(deps_),deps,deps.size()) ;
		if (is_src()) throw "cannot forget source"s ;
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

	::vector<Req> JobData::running_reqs() const {                                           // sorted by start
		::vector<Req> res ; res.reserve(Req::s_n_reqs()) ;                                  // pessimistic, so no realloc
		for( Req r : Req::s_reqs_by_start ) if (c_req_info(r).running()) res.push_back(r) ;
		return res ;
	}

	::vector<Req> JobData::old_done_reqs() const {                             // sorted by start
		::vector<Req> res ; res.reserve(Req::s_n_reqs()) ;                     // pessimistic, so no realloc
		for( Req r : Req::s_reqs_by_start ) {
			ReqInfo const& cri = c_req_info(r) ;
			if (cri.running()) break ;
			if (cri.done()   ) res.push_back(r) ;
		}
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
