// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include "rpc_job.hh"

using namespace Caches ;
using namespace Disk   ;

enum class NoRunReason : uint8_t {
	None
,	Dep        // dont run because deps are not new
,	SubmitLoop // dont run because job submission limit is reached
,	RetryLoop  // dont run because job retry      limit is reached
,	LostLoop   // dont run because job lost       limit is reached
} ;

namespace Engine {

	//
	// jobs thread
	//

	vmap<Node,FileAction> JobData::pre_actions( Rule::RuleMatch const& match , bool mark_target_dirs ) const {    // thread-safe
		Trace trace("pre_actions",idx(),STR(mark_target_dirs)) ;
		::uset<Node>                  to_mkdirs          = match.target_dirs() ;
		::uset<Node>                  to_mkdir_uphills   ;
		::uset<Node>                  dep_locked_dirs    ;
		::uset<Node>                  target_locked_dirs ;
		::umap<Node,NodeIdx/*depth*/> to_rmdirs          ;
		::vmap<Node,FileAction>       actions            ;
		for( Node d : to_mkdirs )
			for( Node hd=d->dir() ; +hd ; hd=hd->dir() )
				if (!to_mkdir_uphills.insert(hd).second) break ;
		auto end_it = deps.end() ;
		for( auto it = deps.begin().next_existing(end_it) ; it!=end_it ; it++.next_existing(end_it) ) {
			Node dep = *it ;
			if (dep->has_file()==Maybe)
				for( Node hd=dep->dir() ; +hd ; hd=hd->dir() )
					if (!dep_locked_dirs.insert(hd).second) break ;                                               // if dir contains an existing dep, it cannot be rmdir'ed
		}
		//
		// remove old targets
		for( Target t : targets ) {
			FileActionTag fat = {}/*garbage*/ ;
			//
			if      (  t->crc==Crc::None                                  ) fat = FileActionTag::None           ; // nothing to wash
			else if (  t->is_src_anti()                                   ) fat = FileActionTag::Src            ; // dont touch sources, not even integrity check
			else if ( +t->polluted       &&  t.tflags[Tflag::Target     ] ) fat = FileActionTag::UnlinkPolluted ; // wash     polluted targets
			else if ( +t->polluted       && !t.tflags[Tflag::Incremental] ) fat = FileActionTag::UnlinkPolluted ; // wash     polluted non-incremental
			else if (                       !t.tflags[Tflag::Incremental] ) fat = FileActionTag::Unlink         ; // wahs non-polluted non-incremental
			else                                                            fat = FileActionTag::Uniquify       ;
			FileAction fa { .tag=fat , .no_warning=t.tflags[Tflag::NoWarning] , .crc=t->crc , .sig=t->date().sig } ;
			//
			trace("wash_target",t,fa) ;
			switch (fat) {
				case FileActionTag::Src      : if ( +t->dir() && t->crc!=Crc::None ) target_locked_dirs.insert(t->dir()) ;                              break ; // no action, not even integrity check
				case FileActionTag::Uniquify : if ( +t->dir()                      ) target_locked_dirs.insert(t->dir()) ; actions.emplace_back(t,fa) ; break ;
				case FileActionTag::Unlink :
					if ( !t->has_actual_job(idx()) && t->has_actual_job() && !t.tflags[Tflag::NoWarning] ) fa.tag = FileActionTag::UnlinkWarning ;
				[[fallthrough]] ;
				case FileActionTag::UnlinkPolluted :
				case FileActionTag::None           :
					actions.emplace_back(t,fa) ;
					if ( Node td=t->dir() ; +td ) {
						Lock    lock  { _s_target_dirs_mutex } ;
						NodeIdx depth = 0                      ;
						for( Node hd=td ; +hd ; (hd=hd->dir()),depth++ )
							if (_s_target_dirs.contains(hd)) goto NextTarget ; // everything under a protected dir is protected, dont even start walking from td
						for( Node hd=td ; +hd ; hd=hd->dir() ) {
							if (_s_hier_target_dirs.contains(hd)) break ;      // dir is protected
							if (dep_locked_dirs    .contains(hd)) break ;      // dir contains a dep    => no     hope and no desire to remove it
							if (target_locked_dirs .contains(hd)) break ;      // dir contains a target => little hope and no desire to remove it
							if (to_mkdirs          .contains(hd)) break ;      // dir must exist, it is silly to spend time to rmdir it, then again to mkdir it
							if (to_mkdir_uphills   .contains(hd)) break ;      // .
							//
							if (!to_rmdirs.emplace(td,depth).second) break ;   // if it is already in to_rmdirs, so is all pertinent dirs uphill
							depth-- ;
						}
					}
				break ;
			DF}                                                                // NO_COV
		NextTarget : ;
		}
		// make target dirs
		for( Node d : to_mkdirs ) {
			if (dep_locked_dirs .contains(d)) continue ;                       // dir contains a dep          => it already exists (targets may have been unlinked)
			if (to_mkdir_uphills.contains(d)) continue ;                       // dir is a dir of another dir => it will be automatically created
			actions.emplace_back( d , ::FileAction({FileActionTag::Mkdir}) ) ; // note that protected dirs (in _s_target_dirs and _s_hier_target_dirs) may not be created yet, so mkdir them to be sure
		}
		// rm enclosing dirs of unlinked targets
		::vmap<Node,NodeIdx/*depth*/> to_rmdir_vec ; for( auto [k,v] : to_rmdirs ) to_rmdir_vec.emplace_back(k,v) ;
		::sort( to_rmdir_vec , [&]( ::pair<Node,NodeIdx/*depth*/> const& a , ::pair<Node,NodeIdx/*depth*/> const& b ) { return a.second>b.second ; } ) ; // sort deeper first, to rmdir after children
		for( auto [d,_] : to_rmdir_vec ) actions.emplace_back(d,FileAction({FileActionTag::Rmdir})) ;
		//
		// mark target dirs to protect from deletion by other jobs
		// this must be perfectly predictible as this mark is undone in end_exec below
		if (mark_target_dirs) {
			Lock lock{_s_target_dirs_mutex} ;
			for( Node d : to_mkdirs        ) { trace("protect_dir"     ,d) ; _s_target_dirs     [d]++ ; }
			for( Node d : to_mkdir_uphills ) { trace("protect_hier_dir",d) ; _s_hier_target_dirs[d]++ ; }
		}
		return actions ;
	}

	void JobData::end_exec() const {
		Trace trace("end_exec",idx()) ;
		::uset<Node> dirs        = rule_match().target_dirs() ;
		::uset<Node> dir_uphills ;
		for( Node d : dirs )
			for( Node hd=d->dir() ; +hd ; hd=hd->dir() )
				if (!dir_uphills.insert(hd).second) break ;
		//
		auto dec = [&]( ::umap<Node,Idx/*cnt*/>& dirs , Node d )->void {
			auto it = dirs.find(d) ;
			SWEAR(it!=dirs.end()) ;
			if (it->second==1) dirs.erase(it) ;
			else               it->second--   ;
		} ;
		Lock lock(_s_target_dirs_mutex) ;
		for( Node d : dirs        ) { trace("unprotect_dir"     ,d) ; dec(_s_target_dirs     ,d) ; }
		for( Node d : dir_uphills ) { trace("unprotect_hier_dir",d) ; dec(_s_hier_target_dirs,d) ; }
	}

	//
	// main thread
	//

	//
	// JobTgts
	//

	::string& operator+=( ::string& os , JobTgts jts ) { // START_OF_NO_COV
		return os<<jts.view() ;
	}                                                    // END_OF_NO_COV

	//
	// JobReqInfo
	//

	::string& operator+=( ::string& os , JobReqInfo::State const& ris ) {                                           // START_OF_NO_COV
		const char* sep = "" ;
		/**/                                            os <<'('                                                  ;
		if (+ris.reason                             ) { os <<            ris.reason                               ; sep = "," ; }
		if ( +ris.stamped_err   || +ris.proto_err   ) { os <<sep<<"E:"<< ris.stamped_err  <<"->"<<ris.proto_err   ; sep = "," ; }
		if ( +ris.stamped_modif || +ris.proto_modif )   os <<sep<<"M:"<< ris.stamped_modif<<"->"<<ris.proto_modif ;
		return                                          os <<')'                                                  ;
	}                                                                                                               // END_OF_NO_COV

	::string& operator+=( ::string& os , JobReqInfo const& ri ) { // START_OF_NO_COV
		/**/                   os << "JRI(" << ri.req     ;
		if ( ri.speculate!=No) os <<",S:" << ri.speculate ;
		if ( ri.modified     ) os <<",modified"           ;
		/**/                   os <<','   << ri.step()    ;
		/**/                   os <<'@'   << ri.iter      ;
		/**/                   os <<':'   << ri.state     ;
		if ( ri.n_wait       ) os <<",W:" << ri.n_wait    ;
		if (+ri.reason       ) os <<','   << ri.reason    ;
		if (+ri.n_losts      ) os <<",NL:"<< ri.n_losts   ;
		if (+ri.n_retries    ) os <<",NR:"<< ri.n_retries ;
		if (+ri.n_submits    ) os <<",NS:"<< ri.n_submits ;
		if ( ri.miss_live_out) os <<",miss_live_out"      ;
		return                 os <<')'                   ;
	}                                                             // END_OF_NO_COV

	void JobReqInfo::step( Step s , Job j ) {
		if (_step==s) return ;                // fast path
		//
		if ( _step>=Step::MinCurStats && _step<Step::MaxCurStats1 ) { req->stats.cur(_step)-- ; if (_step==Step::Dep) req->stats.waiting_cost -= j->cost ; }
		if ( s    >=Step::MinCurStats && s    <Step::MaxCurStats1 ) { req->stats.cur(s    )++ ; if (s    ==Step::Dep) req->stats.waiting_cost += j->cost ; }
		_step = s ;
	}

	//
	// Job
	//

	QueueThread<::pair<Job,JobInfo1>,true/*Flush*/,true/*QueueAccess*/> Job::s_record_thread ;

	::string& operator+=( ::string& os , Job j ) {             // START_OF_NO_COV
		/**/    os << "J(" ;
		if (+j) os << +j   ;
		return  os << ')'  ;
	}                                                          // END_OF_NO_COV
	::string& operator+=( ::string& os , JobTgt jt ) {         // START_OF_NO_COV
		if (!jt) return           os << "(J())"        ;
		/**/                      os << "(" << Job(jt) ;
		if (jt.is_static_phony()) os << ",static"      ;
		else                      os << ",star"        ;
		return                    os << ')'            ;
	}                                                          // END_OF_NO_COV
	::string& operator+=( ::string& os , JobExec const& je ) { // START_OF_NO_COV
		if (!je) return os << "JE()" ;
		//
		/**/         os <<'('<< Job(je)                     ;
		if (je.host) os <<','<< SockFd::s_addr_str(je.host) ;
		if (je.start_date==je.end_date) {
			os <<','<< je.start_date ;
		} else {
			if (+je.start_date) os <<",S:"<< je.start_date ;
			if (+je.end_date  ) os <<",E:"<< je.end_date   ;
		}
		return os <<')' ;
	}                                                          // END_OF_NO_COV

	Job::Job( Rule::RuleMatch&& match , Req req , DepDepth lvl ) {
		Trace trace("Job",match,req,lvl) ;
		if (!match) { trace("no_match") ; return ; }
		Rule rule = match.rule    ;
		if ( ::pair_s<VarIdx> msg=match.reject_msg() ; +msg.first ) {
			trace("not_accepted") ;
			::pair_s<RuleData::MatchEntry> const& k_me = rule->matches[msg.second] ;
			MatchKind                             mk   = k_me.second.flags.kind()  ;
			req->audit_job( Color::Warning , cat("bad_",mk) , rule->name , match.name() ) ;
			req->audit_stderr( self , {.msg=cat(mk,' ',k_me.first," : ",msg.first)} , 0/*max_stderr_len*/ , 1/*lvl*/ ) ;
			return ;
		}
		//
		::pair_s</*msg*/::vmap_s<DepSpec>> digest          ;
		/**/            ::vmap_s<DepSpec>& dep_specs_holes = digest.second ;                                                                  // contains holes
		try {
			digest = rule->deps_attrs.eval(match) ;
		} catch (MsgStderr const& msg_err) {
			trace("no_dep_subst") ;
			if (+req) {
				req->audit_job( Color::Note , "deps_not_avail" , rule->name , match.name() ) ;
				req->audit_stderr( self , {ensure_nl(rule->deps_attrs.s_exc_msg(false/*using_static*/))+msg_err.msg,msg_err.stderr} , 0/*max_stderr_len*/ , 1 ) ;
			}
			return ;
		}
		::vector<Dep>       deps ; deps.reserve(dep_specs_holes.size()) ;
		::umap<Node,VarIdx> dis  ;
		for( auto const& k_ds : dep_specs_holes ) {
			DepSpec const& ds = k_ds.second ;
			if (!ds.txt) continue ;                                                                                                           // filter out holes
			Node           d  { New , ds.txt }                                                 ;
			Accesses       a  = ds.extra_dflags[ExtraDflag::Ignore] ? Accesses() : ~Accesses() ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			d->set_buildable( req , lvl , true/*throw_if_infinite*/ ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (d->buildable<=Buildable::No) {
				trace("no_dep",d) ;
				g_kpi.n_aborted_job_creation++ ;
				return ;
			}
			if ( auto [it,ok] = dis.emplace(d,deps.size()) ; ok )   deps.emplace_back( d , a , ds.dflags , true/*parallel*/ ) ;
			else                                                  { deps[it->second].dflags |= ds.dflags ; deps[it->second].accesses &= a ; } // uniquify deps by combining accesses and flags
		}
		if (+digest.first) {                                                           // only bother user for bad deps if job otherwise applies, so handle them once static deps have been analyzed
			req->audit_job( Color::Warning , "bad_dep" , rule->name , match.name() ) ;
			req->audit_stderr( self , {.msg=digest.first} , 0/*max_stderr_len*/ , 1 ) ;
			return ;
		}
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		//          args for store         args for JobData
		self = Job( match.full_name(),Dflt , match,deps   ) ;                          // initially, static deps are deemed read, then actual accesses will be considered
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		trace("found",self,self->deps) ;
	}

	::string Job::ancillary_file(AncillaryTag tag) const {
		switch (tag) {
			case AncillaryTag::Backend : return cat(PrivateAdminDirS           ,"backend/" ,+self) ;
			case AncillaryTag::Data    : return cat(g_config->local_admin_dir_s,"job_data/",+self) ;
			case AncillaryTag::Dbg     : return cat(AdminDirS                  ,"debug/"   ,+self) ;
			case AncillaryTag::KeepTmp : return cat(AdminDirS                  ,"tmp/"     ,+self) ;
		DF}                                                                                          // NO_COV
	}

	JobInfo Job::job_info(BitMap<JobInfoKind> need) const {
		JobInfo             res   ;
		BitMap<JobInfoKind> found ;
		SWEAR(+(need&~JobInfoKind::None)) ;                                            // else, this is useless
		auto do_entry = [&](::pair<Job,JobInfo1> const& jji)->void {
			if (jji.first!=self) return ;
			JobInfo1 const& ji = jji.second ;
			if ( ji.is_a<JobInfoKind::Start>() && +found ) {                           // start event is a new record
				res   = {} ;
				found = {} ;
			}
			JobInfoKind k = ji.kind() ;
			found |= k ;
			if (need[k])
				switch (k) {
					case JobInfoKind::Start   : res.start    = ji.start   () ; break ;
					case JobInfoKind::End     : res.end      = ji.end     () ; break ;
					case JobInfoKind::DepCrcs : res.dep_crcs = ji.dep_crcs() ; break ;
				DF}                                                                    // NO_COV
		} ;
		// first search queue
		{	Lock lock { s_record_thread } ;                                            // lock for minimal time
			/**/                                     do_entry(s_record_thread.cur()) ; // dont forget entry being processed (handle first as this is this the oldest entry)
			for( auto const& jji : s_record_thread ) do_entry(jji                  ) ; // linear searching is not fast, but this is rather exceptional and this queue is small (actually mostly empty)
		}
		Trace trace("job_info",self,need,found,res.dep_crcs.size()) ;
		// then search recorded info
		if (!found[JobInfoKind::Start]) res.fill_from( ancillary_file() , need&~found ) ; // else, recorded info is obsolete
		trace("after",res.dep_crcs.size()) ;
		return res ;
	}

	void Job::record(JobInfo1 const& ji) const {
		Trace trace("record",self,ji.kind()) ;
		::string jaf    ;
		FdAction action ;
		switch (ji.kind()) {
			case JobInfoKind::Start   : serialize( jaf , ji.start   () ) ; action = FdAction::Create ; break ; // start event write to file (new record)
			case JobInfoKind::End     : serialize( jaf , ji.end     () ) ; action = FdAction::Append ; break ; // other events append to it
			case JobInfoKind::DepCrcs : serialize( jaf , ji.dep_crcs() ) ; action = FdAction::Append ; break ; // .
		DF}                                                                                                    // NO_COV ensure something to record
		AcFd( ancillary_file() , action ).write(jaf) ;
	}

	void Job::record(JobInfo const& ji) const {
		Trace trace("record",self) ;
		::string jaf ;
		serialize( jaf , ji.start    ) ;
		serialize( jaf , ji.end      ) ;
		serialize( jaf , ji.dep_crcs ) ;
		AcFd( ancillary_file() , FdAction::Create ).write(jaf) ;
	}

	//
	// JobExec
	//

	void JobExec::give_up( Req req , bool report ) {
		Trace trace("give_up",self,req) ;
		JobData& jd = *self ;
		if (+req) {
			ReqInfo& ri = jd.req_info(req) ;
			ri.step(Step::End,self) ;                                                    // ensure no confusion with previous run
			jd.make( ri , MakeAction::GiveUp ) ;
			for( Req r : jd.running_reqs(false/*with_zombies*/) ) {
				SWEAR(r!=req) ;
				if (report) req->audit_job(Color::Note,"continue",self,true/*at_end*/) ; // generate a continue line if some other req is still active
				goto NotKilled ;
			}
			for( Node t : self->targets ) t->busy = false ;                              // if job does not continue, targets are no more busy
		NotKilled :
			req.chk_end() ;
		} else {
			for( Req r : jd.running_reqs(true/*with_zombies*/) ) give_up(r,false/*report*/)                                ;
			for( Req r : jd.running_reqs(true/*with_zombies*/) ) FAIL(jd.name(),"is still running for",r,"after kill all") ;
		}
	}

	// answer to job execution requests
	JobMngtRpcReply JobExec::job_analysis( JobMngtProc proc , ::vector<Dep> const& deps ) const {
		::vector<Req> reqs = self->running_reqs(false/*with_zombies*/) ;
		Trace trace("job_analysis",proc,deps.size(),reqs.size()) ;
		//
		if (!reqs) return {.proc=proc} ;                                                       // if job is not running, it is too late, seq_id will be filled in later
		JobMngtRpcReply res { .proc=proc } ;                                                   // seq_id will be filled in later
		switch (proc) {
			case JobMngtProc::DepVerbose :
				res.dep_infos.reserve(deps.size()) ;
				for( Dep const& dep : deps ) {
					Node(dep)->full_refresh(false/*report_no_file*/,{}) ;                      // dep is const
					Bool3 dep_ok = Yes ;
					for( Req req : reqs ) {
						NodeReqInfo& dri = dep->req_info(req) ;
						Node(dep)->make(dri,NodeMakeAction::Query) ;
						if      (!dri.done(NodeGoal::Status)  ) { trace("waiting",dep,req) ; dep_ok = Maybe ;         }
						else if (dep->ok(dri,dep.accesses)==No) { trace("bad"    ,dep,req) ; dep_ok = No    ; break ; }
					}
					trace("dep_info",dep,dep_ok) ;
					DepVerboseInfo dvi { .ok=dep_ok } ;
					if (dep_ok!=Maybe) dvi.crc = dep->crc ;
					if (dep->is_src_anti()) {
						dvi.special = cat(dep->buildable) ;
					} else if ( Job j = dep->actual_job() ; +j ) {
						if ( Rule r = j->rule() ; +r ) {
							if (r->is_special()) {
								dvi.special = cat(r->special) ;
							} else {
								Rule::RuleMatch rm { j } ;
								dvi.rule = r->user_name() ;
								for( VarIdx i : iota(rm.stems.size()) ) dvi.stems.emplace_back( r->stems[i].first , ::move(rm.stems[i]) ) ;
							}
						}
					}
					res.dep_infos.push_back(::move(dvi)) ;
				}
			break ;
			case JobMngtProc::ChkDeps :
				res.ok = Yes ;
				for( Dep const& dep : deps ) {
					Node(dep)->full_refresh(false/*report_no_file*/,{}) ;                      // dep is const
					Bool3 dep_ok = Yes ;
					for( Req req : reqs ) {
						NodeReqInfo& dri  = dep->req_info(req)                               ;
						NodeGoal     goal = +dep.accesses ? NodeGoal::Dsk : NodeGoal::Status ; // if no access, we do not care about file on disk
						Node(dep)->make(dri,NodeMakeAction::Query) ;
						if      (!dri.done(goal)              ) { trace("waiting",dep,req) ; dep_ok = Maybe ;         }
						else if (dep->ok(dri,dep.accesses)==No) { trace("bad"    ,dep,req) ; dep_ok = No    ; break ; }
					}
					if (dep_ok!=Yes) {
						res.ok  = dep_ok      ;
						res.txt = dep->name() ;
						break ;
					}
					trace("ok",dep) ;
				}
			break ;
		DF}                                                                                    // NO_COV
		trace("done") ;
		return res ;
	}

	void JobExec::live_out( ReqInfo& ri , ::string const& txt ) const {
		if (!txt) return ;
		Req r = ri.req ;
		if ( !report_start(ri) && r->last_info!=self ) {
			Pdate now = New ;
			r->audit_job( Color::HiddenNote , "continue" , JobExec(self,host,start_date,now) , true/*at_end*/ , now-start_date ) ; // identify job (with a continue message if no start message)
		}
		r->last_info = self ;
		//vvvvvvvvvvvvvvvvv
		r->audit_as_is(txt) ;
		//^^^^^^^^^^^^^^^^^
	}

	void JobExec::live_out(::string const& txt) const {
		Trace trace("live_out",self) ;
		for( Req req : self->running_reqs(false/*with_zombies*/) ) {
			ReqInfo& ri = self->req_info(req) ;
			if ( ri.live_out && !ri.miss_live_out ) live_out(ri,txt) ; // if we missed already sent info, wait for them to ensure coherent reports
		}
	}

	void JobExec::add_live_out(::string const& txt) const {
		Trace trace("add_live_out",self) ;
		for( Req req : self->running_reqs(false/*with_zombies*/) ) {
			ReqInfo& ri = self->req_info(req) ;
			if ( ri.live_out && ri.miss_live_out ) {
				live_out(ri,txt) ;                   // report missed info
				ri.miss_live_out = false ;           // we are now up to date with reports
			}
		}
	}

	bool/*reported*/ JobExec::report_start( ReqInfo& ri , ::vmap<Node,FileActionTag> const& report_unlnks , MsgStderr const& msg_stderr ) const {
		if      (ri.start_reported      ) return false ;
		if      (is_retry(ri.reason.tag)) ri.req->audit_job( Color::Warning    , "retry" , self ) ;
		else if (+msg_stderr.stderr     ) ri.req->audit_job( Color::Warning    , "start" , self ) ;
		else                              ri.req->audit_job( Color::HiddenNote , "start" , self ) ;
		ri.req->last_info = self ;
		for( auto [t,fat] : report_unlnks ) {
			Job aj = t->actual_job() ;
			switch (fat) {
				case FileActionTag::UnlinkWarning  :                              ri.req->audit_node( Color::Warning , "unlinked"          , t , 1 ) ; break ;
				case FileActionTag::UnlinkPolluted : { if (aj==self) continue ; } ri.req->audit_node( Color::Warning , "unlinked polluted" , t , 1 ) ; break ;
				default : continue ;
			}
			if (+aj) ri.req->audit_info( Color::Note , "generated by" , aj->name() , 2 ) ;
		}
		if (+msg_stderr.stderr) ri.req->audit_stderr( self , msg_stderr , 0/*max_stderr_len*/ , 1 ) ;
		ri.start_reported = true ;
		return true ;
	}
	void JobExec::report_start() const {
		Trace trace("report_start",self) ;
		for( Req req : self->running_reqs(false/*with_zombies*/) ) report_start(self->req_info(req)) ;
	}

	void JobExec::started( bool report , ::vmap<Node,FileActionTag> const& report_unlnks , MsgStderr const& msg_stderr ) {
		Trace trace("started",self) ;
		SWEAR( !self->rule()->is_special() , self->rule()->special ) ;
		report |= +report_unlnks || +msg_stderr.stderr ;
		for( Req req : self->running_reqs() ) {
			ReqInfo& ri = self->req_info(req) ;
			ri.start_reported = false ;
			if (report) report_start( ri , report_unlnks , msg_stderr ) ;
			ri.step(JobStep::Exec,self) ;
		}
	}

	void JobExec::end(JobDigest<Node>&& digest) {
		JobData&                         jd            = *self                                                             ;
		Status                           status        = digest.status                                                     ; // status will be modified, need to make a copy
		Bool3                            ok            = is_ok  (status)                                                   ;
		bool                             lost          = is_lost(status)                                                   ;
		JobReason                        target_reason = JobReasonTag::None                                                ;
		bool                             unstable_dep  = false                                                             ;
		bool                             modified      = false                                                             ;
		bool                             fresh_deps    = (status<=Status::Early&&!is_lost(status)) || status>Status::Async ; // if job did not go through, old deps are better than new ones
		Rule                             rule          = jd.rule()                                                         ;
		::vector<Req>                    running_reqs_ = jd.running_reqs(true/*with_zombies*/)                             ;
		::string                         severe_msg    ;                                                                     // to be reported always
		Rule::RuleMatch                  match         ;
		::umap<Node,::pair<FileSig,Crc>> old_srcs      ; // remember old src infos before they are updated
		//
		Trace trace("end",self,digest) ;
		//
		SWEAR(status!=Status::New) ;                     // we just executed the job, it can be neither new, frozen or special
		SWEAR(!frozen()          ) ;                     // .
		SWEAR(!rule->is_special()) ;                     // .
		//
		jd.status = Status::New ;                        // ensure we cannot appear up-to-date while working on data
		fence() ;
		//
		// handle targets
		//
		for( Node t : jd.targets ) t->busy = false ;     // old targets are no more busy
		//
		if ( !lost && status>Status::Early ) {           // if early, we have not touched the targets, not even washed them, if lost, old targets are better than new ones
			//
			::umap  <Node,Tflags> old_incremental_targets ;
			::vector<Target     > targets                 ; targets.reserve(digest.targets.size()) ;
			for( Target t : jd.targets )
				if (t->has_actual_job(self)) {
					if (t.tflags[Tflag::Incremental]) old_incremental_targets.try_emplace(t,t.tflags) ;
					else                              t->actual_job() = {} ;                                     // ensure targets we no more generate do not keep pointing to us
				}
			//
			for( auto& [target,td] : digest.targets ) {
				target->set_buildable() ;
				Tflags tflags          = td.tflags              ;
				bool   static_phony    = ::static_phony(tflags) ;
				bool   is_src_anti     = target->is_src_anti()  ;
				Crc    crc             = td.crc                 ;
				bool   target_modified = false                  ;
				bool   unexpected      = false                  ;
				//
				old_incremental_targets.erase(target) ;
				//
				if      ( td.pre_exist                               )   target->polluted = Polluted::PreExist ; // pre-existed while not incremental => polluted even if generated by official job
				else if ( tflags[Tflag::Target]                      )   target->polluted = {}                 ;
				else if ( +crc && crc!=Crc::None && crc!=target->crc ) { target->polluted = Polluted::Job      ; target->polluting_job() = self ; } // we pollute the official job
				//
				if (+target->polluted) trace("polluted",target->polluted,target->polluting_job()) ;
				//
				if (+crc) {
					// file dates are very fuzzy and unreliable, at least, filter out targets we generated ourselves
					if ( +start_date && target->date().date>start_date ) {                                                        // if no start_date.p, job did not execute, it cannot generate a clash
						// /!\ This may be very annoying !
						// A job was running in parallel with us and there was a clash on this target.
						// There are 2 problems : for us and for them.
						// For us, it's ok, we will rerun.
						// But for them, they are already done,  possibly some dependent jobs are done, possibly even Req's are already done and we may have reported ok to the user,
						// and all that is wrong.
						// This is too complex and too rare to detect (and ideally handle).
						// Putting target in clash_nodes will generate a frightening message to user asking to relaunch all commands that were running in parallel.
						if ( crc.valid() && td.tflags[Tflag::Target] ) {                                                          // official targets should have a valid crc, but if not, we dont care
							trace("clash",start_date,target->date().date) ;
							target_reason |= {JobReasonTag::ClashTarget,+target} ;                                                // crc is actually unreliable, rerun
						}
						Job aj = target->actual_job() ;
						if ( +aj && aj!=self && target->crc.valid() && target->actual_tflags()[Tflag::Target] && !is_src_anti ) { // existing crc was believed to be reliable but actually was not
							SWEAR(+aj->rule()) ;                                                                                  // what could be this ruleless job that has been run ?!?
							trace("critical_clash",start_date,target->date().date) ;
							for( Req r : target->reqs() ) {
								r->clash_nodes.push(target,{aj,Job(self)}) ;
								target->req_info(r).done_ = NodeGoal::None ;                             // best effort to trigger re-analysis but this cannot be guaranteed (fooled req may be gone)
							}
						}
					}
					//
					if (is_src_anti) {                                                                   // source may have been modified
						old_srcs.try_emplace( target , ::pair(target->date().sig,target->crc) ) ;        // if it is also a dep, at best the dep will see old sig, and we want to associate old crc
						if (!crc.valid()) crc = {target->name()} ;                                       // force crc computation if updating a source
						//
						switch (target->buildable) {
							case Buildable::DynSrc :
							case Buildable::Src    :
								/**/                           if (td.extra_tflags[ExtraTflag::SourceOk]) goto SourceOk ;
								for( Req req : running_reqs_ ) if (req->options.flags[ReqFlag::SourceOk]) goto SourceOk ;
							break ;
						DN}
						{	::string msg = "unexpected" ;
							if (crc==Crc::None) msg += " unlink of" ;
							else                msg += " write to"  ;
							switch (target->buildable) {
								case Buildable::PathTooLong : msg << " path too long"  ; break ;
								case Buildable::Anti        :
								case Buildable::DynAnti     : msg << " anti-file"      ; break ;
								case Buildable::SrcDir      : msg << " source dir"     ; break ;
								case Buildable::SubSrcDir   : msg << " source sub-dir" ; break ;
								case Buildable::DynSrc      :
								case Buildable::Src         : msg << " source"         ; break ;
								case Buildable::SubSrc      : msg << " sub-source"     ; break ;
							DF}                                                                          // NO_COV
							severe_msg << msg <<" : "<< mk_file(target->name(),No /*exists*/) <<'\n' ;
							unexpected = true ;
						}
						if (ok==Yes) status = Status::Err ;
					SourceOk : ;
					}
					//
					if (unexpected) target_modified = target->crc.match(crc)                                                                      ;
					else            target_modified = target->refresh( crc , { td.sig , td.extra_tflags[ExtraTflag::Late]?end_date:start_date } ) ;
					modified |= target_modified && tflags[Tflag::Target] ;
				}
				if ( crc==Crc::None && !static_phony ) {
					target->actual_job   () = {} ;
					target->actual_tflags() = {} ;
					trace("unlink",target,td,STR(target_modified)) ;
				} else if ( +crc || tflags[Tflag::Target] ) {                                            // if not actually writing, dont pollute targets of other jobs
					target->actual_job   () = self      ;
					target->actual_tflags() = td.tflags ;
					//
					targets.emplace_back( target , tflags ) ;
					bool is_src = is_src_anti && target->buildable>Buildable::No ;
					if ( td.pre_exist && !is_src ) target_reason |= {JobReasonTag::PrevTarget,+target} ; // sources are not unlinked, hence not marked PrevTarget
					trace("target",target,td,STR(target_modified)) ;
				} else {
					trace("not_target",target,td) ;
				}
			}
			for( auto t : old_incremental_targets ) targets.emplace_back(t.first,t.second) ; // if an old incremental target has not been touched, it is still there as it has not been washed
			::sort(targets) ;                                                                // ease search in targets
			//vvvvvvvvvvvvvvvvvvvvvvvv
			jd.targets.assign(targets) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^
		}
		//
		// handle deps
		//
		bool          has_new_deps         = false ;
		bool          has_updated_dep_crcs = false ;                                         // record acquired dep crc's if we acquired any
		::vector<Crc> dep_crcs             ;
		if (fresh_deps) {
			::uset<Node>  old_deps ;
			::vector<Dep> deps     ; deps.reserve(digest.deps.size()) ;
			for( Dep const& d : jd.deps )
				if (d->is_plain())
					for( Node dd=d ; +dd ; dd=dd->dir() )
						if (!old_deps.insert(dd).second) break ;                                          // record old deps and all uphill dirs as these are implicit deps
			for( auto& [dn,dd] : digest.deps ) {
				Dep dep { dn , dd } ;
				if (!old_deps.contains(dep)) {
					has_new_deps = true ;
					// dep.hot means dep has been accessed within g_config->date_prc after its mtime (according to Pdate::now())
					// because of disk date granularity (usually a few ms) and because of date discrepancy between executing host and disk server (usually a few ms when using NTP)
					// this means that the file could actually have been accessed before and have gotten wrong data.
					// if this occurs, consider dep as unstable if it was not a known dep (we know known deps have been finished before job started).
					if (dep.hot) { trace("reset",dep) ; dep.crc({}) ; }
				}
				bool updated_dep_crc = false ;
				if (!dep.is_crc) {
					dep->full_refresh(true/*report_no_file*/,running_reqs_) ;
					dep.acquire_crc() ;
					if (!dep.is_crc) {
						auto it = old_srcs.find(dep) ;
						if (it!=old_srcs.end()) {
							if (dep.sig()==it->second.first) dep.crc(it->second.second) ;
						}
					}
					if (dep.is_crc) {                                                                     // if a dep has become a crc, update digest so that ancillary file reflects it
						dd.crc_sig(dep) ;
						updated_dep_crc = true ;
					}
				} else if (dep.never_match()) {
					dep->set_buildable() ;
					if (dep->is_src_anti()) dep->refresh_src_anti(true/*report_no_file*/,running_reqs_) ; // the goal is to detect overwritten
					unstable_dep = true ;
				}
				trace("dep",dep,STR(dep.is_crc),STR(dep.is_crc&&dep.crc().valid())) ;
				/**/                   deps    .push_back( dep     ) ;
				if (updated_dep_crc) { dep_crcs.push_back(dep.crc()) ; has_updated_dep_crcs = true ; }
				else                   dep_crcs.push_back(Crc()    ) ;
			}
			//vvvvvvvvvvvvvvvvvv
			jd.deps.assign(deps) ;
			//^^^^^^^^^^^^^^^^^^
		}
		//
		// wrap up
		//
		jd.set_exec_ok() ;                                                  // effect of old cmd has gone away with job execution
		fence() ;                                                           // only update status once every other info is set in case of crash and avoid transforming garbage into Err
		if ( !lost && +target_reason && status>Status::Garbage ) status = Status::BadTarget ;
		//vvvvvvvvvvvvvvvv
		jd.status = status ;
		//^^^^^^^^^^^^^^^^
		// job_data file must be updated before make is called as job could be remade immediately (if cached), also info may be fetched if issue becomes known
		MsgStderr      msg_stderr  ;
		bool           upload      = jd.run_status==RunStatus::Ok && ok==Yes ;
		::vector<bool> must_wakeup ;
		//
		trace("wrap_up",ok,digest.cache_idx,jd.run_status,STR(upload),digest.upload_key) ;
		if ( +severe_msg || digest.has_msg_stderr ) {
			JobInfo ji = job_info() ;
			if (digest.has_msg_stderr) msg_stderr = ji.end.msg_stderr ;
			if (+severe_msg) {
				ji.end.msg_stderr.msg <<set_nl<< severe_msg    ;
				s_record_thread.emplace(self,::move(ji.start)) ;            // necessary to restart recording, else ji.end would be appended
				s_record_thread.emplace(self,::move(ji.end  )) ;
			}
		}
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		s_record_thread.emplace( self , has_updated_dep_crcs ? ::move(dep_crcs) : ::vector<Crc>() ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (ok==Yes) jd.record_stats( digest.exec_time , cost , tokens1 ) ; // only update rule based exec time estimate when job is ok as jobs in error may be much faster and are not representative
		//
		for( Req req : jd.running_reqs(true/*with_zombies*/,true/*hit_ok*/)) {
			ReqInfo& ri = jd.req_info(req) ;
			ri.modified |= modified ;                                       // accumulate modifications until reported
			if (!ri.running()) continue ;
			SWEAR(ri.step()==Step::Exec) ;
			ri.step(Step::End,self) ;                                       // ensure no confusion with previous run, all steps must be updated before any make() is called
		}
		for( Req req : running_reqs_ ) {
			ReqInfo& ri = jd.req_info(req) ;
			trace("req_before",target_reason,status,ri,STR(modified)) ;
			req->missing_audits.erase(self) ;                                                                                      // old missing audit is obsolete as soon as we have rerun the job
			//                     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			JobReason job_reason = jd.make( ri , MakeAction::End , target_reason , Yes/*speculate*/ , false/*wakeup_watchers*/ ) ; // we call wakeup_watchers ourselves once reports ...
			//                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   // ... are done to avoid anti-intuitive report order
			bool     done        = ri.done()                         ;
			bool     full_report = done || !has_new_deps             ;                                                        // if not done, does a full report anyway if this is not due to new deps
			bool     job_err     = job_reason.tag>=JobReasonTag::Err ;
			::string job_msg     ;
			if (full_report) {
				/**/         job_msg << msg_stderr.msg        <<set_nl ;
				if (job_err) job_msg << reason_str(job_reason)<<'\n'   ;
				/**/         job_msg << severe_msg                     ;
			} else if (req->options.flags[ReqFlag::Verbose]) {
				/**/         job_msg << reason_str(job_reason)<<'\n'   ;
			}
			//
			::string  pfx = !done && !ri.running() && status>Status::Garbage && !unstable_dep ? "may_" : "" ;
			JobReport jr  = audit_end(
				ri
			,	true/*with_stats*/
			,	pfx
			,	MsgStderr{ .msg=job_msg , .stderr=msg_stderr.stderr }
			,	digest.max_stderr_len
			,	digest.exec_time
			,	is_retry(job_reason.tag)
			) ;
			upload &= done ;
			must_wakeup.push_back(done) ;
			if (!done) req->missing_audits[self] = { .report=jr , .has_stderr=digest.has_msg_stderr , .msg=msg_stderr.msg } ; // stderr may be empty if digest.has_mg_stderr, no harm
			trace("req_after",ri,job_reason,STR(done)) ;
		}
		if (+digest.upload_key) {
			Cache* cache = Cache::s_tab[digest.cache_idx] ;
			SWEAR(cache,digest.cache_idx) ;                                                          // cannot commit/dismiss without cache
			try {
				if (upload) cache->commit ( digest.upload_key , self->unique_name() , job_info() ) ; // cache only successful results
				else        cache->dismiss( digest.upload_key                                    ) ; // free up temporary storage copied in job_exec
			} catch (::string const& e) {
				for( Req req : running_reqs_ ) {
					req->audit_job( Color::Warning , "bad_cache" , self , true/*at_end*/ ) ;
					req->audit_stderr( self , {.msg=e} , 0/*max_stderr_len*/ , 1/*lvl*/ ) ;
				}
			}
		}
		for( ReqIdx i : iota(running_reqs_.size()) ) {                                               // wakeup only after all messages are reported to user and cache->commit may generate user messages
			Req      req = running_reqs_[i] ;
			ReqInfo& ri  = jd.req_info(req) ;
			trace("wakeup_watchers",ri) ;
			if (must_wakeup[i]) ri.wakeup_watchers() ;
			req.chk_end() ;
		}
		trace("summary",self) ;
	}

	JobReport JobExec::audit_end( ReqInfo& ri , bool with_stats , ::string const& pfx , MsgStderr const& msg_stderr , uint16_t max_stderr_len , Delay exec_time , bool retry ) const {
		using JR = JobReport ;
		//
		Req            req         = ri.req           ;
		JobData const& jd          = *self            ;
		Color          color       = {}/*garbage*/    ;
		JR             res         = {}/*garbage*/    ;     // report if not Rerun
		::string_view  step        ;
		bool           with_stderr = true             ;
		bool           speculate   = ri.speculate!=No ;
		bool           done        = ri.done()        ;
		//
		if      ( jd.run_status!=RunStatus::Ok               ) { res = JR::Failed     ; color = Color::Err     ; step = snake(jd.run_status) ; }
		else if ( jd.status==Status::Killed                  ) { res = JR::Killed     ; color = Color::Note    ; with_stderr = false ;         }
		else if ( is_lost(jd.status) && is_ok(jd.status)==No ) { res = JR::LostErr    ; color = Color::Err     ;                               }
		else if ( is_lost(jd.status)                         ) { res = JR::Lost       ; color = Color::Warning ; with_stderr = false ;         }
		else if ( jd.status==Status::SubmitLoop              ) { res = JR::SubmitLoop ; color = Color::Err     ;                               }
		else if ( req.zombie()                               ) { res = JR::Completed  ; color = Color::Note    ; with_stderr = false ;         }
		else if ( jd.err()                                   ) { res = JR::Failed     ; color = Color::Err     ;                               }
		else if ( ri.modified                                ) { res = JR::Done       ; color = Color::Ok      ;                               }
		else                                                   { res = JR::Steady     ; color = Color::Ok      ;                               }
		//
		JR jr = res ;                                       // report to do now
		if (done) {
			ri.modified_speculate = ri.modified ;           // remember to accumulate stats in the right slot
			ri.modified           = false       ;           // for the user, this is the base of future modifications
		} else {
			with_stderr = false                           ; // dont report user stderr if analysis made it meaningless
			step        = {}                              ;
			color       = ::min( color , Color::Warning ) ;
			if      (is_lost(jd.status)             ) {                                             }
			else if (jd.status==Status::EarlyChkDeps) { jr = JR::EarlyRerun ; color = Color::Note ; }
			else if (!retry                         ) { jr = JR::Rerun      ; color = Color::Note ; }
		}
		//
		switch (color) {
			case Color::Err : if ( speculate                         ) color = Color::SpeculateErr ; break ;
			case Color::Ok  : if ( with_stderr && +msg_stderr.stderr ) color = Color::Warning      ; break ;
		DN}
		if (!step) step = snake(jr) ;
		Trace trace("audit_end",color,pfx,step,self,ri,STR(with_stats),STR(retry),STR(with_stderr),STR(done),STR(speculate),jr,STR(+msg_stderr.msg),STR(+msg_stderr.stderr)) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		req->audit_job( color , pfx+step , self , true/*at_end*/ , exec_time ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		ri.reported = true  ;
		req->audit_stderr( self , { msg_stderr.msg , with_stderr?msg_stderr.stderr:""s } , max_stderr_len , 1/*lvl*/ ) ;
		//
		if ( speculate && done ) jr = JR::Speculative ;
		if ( with_stats        ) req->stats.add(jr,exec_time) ;
		//
		return res ;
	}

	//
	// JobData
	//

	Mutex<MutexLvl::TargetDir      > JobData::_s_target_dirs_mutex ;
	::umap<Node,JobData::Idx/*cnt*/> JobData::_s_target_dirs       ;
	::umap<Node,JobData::Idx/*cnt*/> JobData::_s_hier_target_dirs  ;

	::string JobData::unique_name() const {
		Rule     r         = rule()                       ;
		::string fn        = full_name()                  ; r->validate(fn) ;                                          // only name suffix is considered to make Rule
		size_t   user_sz   = fn.size() - r->job_sfx_len() ;
		::string res       = fn.substr(0,user_sz)         ; res.reserve(res.size()+1+r->n_static_stems*(2*(3+1))+16) ; // allocate 2x3 digits per stem, this is comfortable
		//
		for( char& c : res ) if (c==Rule::StarMrkr) c = '*' ;
		res.push_back('/') ;
		//
		char* p = &fn[user_sz+1] ;                                                                                     // start of suffix
		for( [[maybe_unused]] VarIdx _ : iota(r->n_static_stems) ) {
			FileNameIdx pos = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			FileNameIdx sz  = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			res << pos << '-' << sz << '+' ;
		}
		res << "rule-" << r->crc->cmd.hex() ;
		return res ;
	}

	void JobData::_reset_targets(Rule::RuleMatch const& match) {
		Rule             r     = rule()                       ;
		::vector<Target> ts    ;                                ts.reserve(r->matches_iotas[false/*star*/][+MatchKind::Target].size()) ; // there are usually no duplicates
		::vector_s       sts   = match.targets(false/*star*/) ;
		VarIdx           i     = 0                            ;
		::uset_s         seens ;
		for( VarIdx mi : r->matches_iotas[false/*star*/][+MatchKind::Target] ) {
			::string const& t = sts[i++] ;
			if (!seens.insert(t).second) continue ;                                                                                      // remove duplicates
			ts.emplace_back( Node(New,t) , r->tflags(mi) ) ;
		}
		::sort(ts) ;                                                                                                                     // ease search in targets
		targets.assign(ts) ;
	}

	void JobData::_do_set_pressure(ReqInfo& ri , CoarseDelay pressure ) const {
		Trace trace("set_pressure",idx(),ri,pressure) ;
		g_kpi.n_job_set_pressure++ ;
		//
		Req         req          = ri.req                  ;
		CoarseDelay dep_pressure = ri.pressure + exec_time ;
		switch (ri.step()) { //!                                                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case JobStep::Dep    : for( DepsIter it{deps,ri.iter } ; it!=deps.end() ; it++ ) (*it)->    set_pressure( (*it)->req_info(req) ,               dep_pressure  ) ; break ;
			case JobStep::Queued :                                                           Backend::s_set_pressure( backend , +idx() , +req , {.pressure=dep_pressure} ) ; break ;
		DN} //!                                                                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

	static JobReasonTag _mk_pre_reason(Status s) {
		static constexpr ::amap<Status,JobReasonTag,N<Status>> ReasonTab {{
			{ Status::New          , JobReasonTag::New             }
		,	{ Status::EarlyChkDeps , JobReasonTag::ChkDeps         }
		,	{ Status::EarlyErr     , JobReasonTag::Retry           }
		,	{ Status::EarlyLost    , JobReasonTag::Lost            }                            // becomes WasLost if end
		,	{ Status::EarlyLostErr , JobReasonTag::LostRetry       }
		,	{ Status::LateLost     , JobReasonTag::Lost            }                            // becomes WasLost if end
		,	{ Status::LateLostErr  , JobReasonTag::LostRetry       }
		,	{ Status::Killed       , JobReasonTag::Killed          }
		,	{ Status::ChkDeps      , JobReasonTag::ChkDeps         }
		,	{ Status::CacheMatch   , JobReasonTag::CacheMatch      }
		,	{ Status::BadTarget    , JobReasonTag::PollutedTargets }
		,	{ Status::Ok           , JobReasonTag::None            }
		,	{ Status::SubmitLoop   , JobReasonTag::None            }
		,	{ Status::Err          , JobReasonTag::Retry           }
		}} ;
		static auto no_node = []()->bool { { for( auto [_,v] : ReasonTab ) if (v>=JobReasonTag::HasNode) return false ; } return true ; } ;
		static_assert(chk_enum_tab(ReasonTab)) ;
		static_assert(no_node()              ) ;
		return ReasonTab[+s].second ;
	}
	JobReason JobData::make( ReqInfo& ri , MakeAction make_action , JobReason asked_reason , Bool3 speculate , bool wakeup_watchers ) {
		using Step = JobStep ;
		static constexpr Dep Sentinel { false/*parallel*/ } ;                                   // used to clean up after all deps are processed
		Trace trace("Jmake",idx(),ri,make_action,asked_reason,speculate,STR(wakeup_watchers)) ;
		//
		SWEAR( asked_reason.tag<JobReasonTag::Err,asked_reason) ;
		Rule              r            = rule()                                              ;
		bool              query        = make_action==MakeAction::Query                      ;
		bool              at_end       = make_action==MakeAction::End                        ;
		Req               req          = ri.req                                              ;
		ReqOptions const& ro           = req->options                                        ;
		Special           special      = r->special                                          ;
		bool              dep_live_out = special==Special::Req && ro.flags[ReqFlag::LiveOut] ;
		CoarseDelay       dep_pressure = ri.pressure + exec_time                             ;
		bool              archive      = ro.flags[ReqFlag::Archive]                          ;
		//
	RestartFullAnalysis :
		JobReason pre_reason    ;                                                               // reason to run job when deps are ready before deps analysis
		JobReason report_reason ;
		auto reason = [&](ReqInfo::State const& s)->JobReason {
			if (ri.force) return pre_reason | ri.reason | s.reason             ;
			else          return pre_reason |             s.reason | ri.reason ;
		} ;
		// /!\ no_run_reason_tag and inc_submits must stay in sync
		auto no_run_reason_tag = [&](JobReasonTag const& jrt)->NoRunReason {                    // roughly equivalent to !jrt||jrt>=Err but give reason and take care of limits
			switch (jrt) {
				case JobReasonTag::None      :                             return NoRunReason::Dep ;
				case JobReasonTag::Retry     :
				case JobReasonTag::LostRetry :                             goto Retry              ;
				default                      : if (jrt>=JobReasonTag::Err) return NoRunReason::Dep ;
			}
			switch (pre_reason.tag) {
				case JobReasonTag::Lost      :             goto Lost   ;
				case JobReasonTag::LostRetry : if (at_end) goto Retry  ; [[fallthrough]] ;      // retry if lost error (other reasons are not reliable)
				default                      :             goto Submit ;
			}
			Retry  : return ri.n_retries>=req->n_retries ? NoRunReason::RetryLoop : NoRunReason::None ;
			Lost   : return ri.n_losts  >=r  ->n_losts   ? NoRunReason::LostLoop  : NoRunReason::None ;
			Submit :
				bool rule_constraint   = r  ->n_submits && ri.n_submits>=r  ->n_submits ;
				bool option_constraint = req->n_submits && ri.n_submits>=req->n_submits ;
				return rule_constraint || option_constraint ? NoRunReason::SubmitLoop : NoRunReason::None ;
		} ;
		auto no_run_reason = [&](ReqInfo::State const& s)->NoRunReason {
			return no_run_reason_tag(reason(s).tag) ;
		} ;
		// /!\ no_run_reason_tag and inc_submits must stay in sync
		auto inc_submits = [&](JobReasonTag jrt)->void {                                        // inc counter associated with no_run_reason_tag (returning None)
			NoRunReason nrr = no_run_reason_tag(jrt) ;
			SWEAR(!nrr,jrt,pre_reason,nrr) ;
			switch (jrt) {
				case JobReasonTag::Retry     :
				case JobReasonTag::LostRetry : ri.n_retries++ ; return ;
			DN}
			switch (pre_reason.tag) {
				case JobReasonTag::Lost      :             ri.n_losts  ++ ; break ;
				case JobReasonTag::LostRetry : if (at_end) ri.n_retries++ ; [[fallthrough]] ;   // retry if lost error (other reasons are not reliable)
				default                      :             ri.n_submits++ ;
			}
		} ;
		switch (make_action) {
			case MakeAction::End    : ri.reset(idx(),true/*has_run*/) ; [[fallthrough]] ;       // deps have changed
			case MakeAction::Wakeup : ri.dec_wait()                   ; break           ;
			case MakeAction::GiveUp : ri.dec_wait()                   ; goto Done       ;
		DN}
		if (+asked_reason) {
			if (ri.state.missing_dsk) { trace("reset",asked_reason) ; ri.reset(idx()) ; }
			ri.reason |= asked_reason ;
		}
		ri.speculate = ri.speculate & speculate ;                                               // cannot use &= with bit fields
		if (ri.done()) {
			if (!reason(ri.state).need_run()) goto Wakeup ;
			if (req.zombie()                ) goto Wakeup ;
			/**/                              goto Run    ;
		} else {
			if (ri.waiting()                ) goto Wait   ;                                     // we may have looped in which case stats update is meaningless and may fail()
			if (req.zombie()                ) goto Done   ;
			if (idx().frozen()              ) goto Run    ;                                     // ensure crc are updated, akin sources
			if (is_infinite(special)        ) goto Run    ;                                     // special case : Infinite's actually have no dep, just a list of node showing infinity
		}
		if (ri.step()==Step::None) {
			estimate_stats() ;                                                                  // initial guestimate to accumulate waiting costs while resources are not fully known yet
			ri.step(Step::Dep,idx()) ;
			JobReasonTag jrt = {} ;
			if      ( r->force                                                                         ) jrt = JobReasonTag::Force  ;
			else if ( !cmd_ok()                                                                        ) jrt = JobReasonTag::Cmd    ;
			else if ( (ro.flags[ReqFlag::ForgetOldErrors]&&err()) || (is_lost(status)&&!is_ok(status)) ) jrt = JobReasonTag::OldErr ; // probably a transient error
			else if ( !rsrcs_ok()                                                                      ) jrt = JobReasonTag::Rsrcs  ; // probably a resource  error
			else                                                                                         goto NoReason ;
			ri.reason              = jrt  ;
			ri.force               = true ;
			ri.state.proto_modif   = true ;                                                               // ensure we can copy proto_modif to stamped_modif anytime when pertinent
			ri.state.stamped_modif = true ;
		NoReason : ;
		}
		g_kpi.n_job_make++ ;
		SWEAR(ri.step()==Step::Dep) ;
		{
		RestartAnalysis :                                                                                 // restart analysis here when it is discovered we need deps to run the job
			bool           stamped_seen_waiting = false    ;
			bool           proto_seen_waiting   = false    ;
			bool           critical_modif       = false    ;
			bool           critical_waiting     = false    ;
			bool           sure                 = true     ;
			ReqInfo::State state                = ri.state ;
			//
			ri.speculative_wait = false                  ;                                                // initially, we are not waiting at all
			report_reason       = {}                     ;
			pre_reason          = _mk_pre_reason(status) ;
			if ( pre_reason.tag==JobReasonTag::Lost && !at_end ) pre_reason = JobReasonTag::WasLost ;
			trace("pre_reason",pre_reason) ;
			for( DepsIter iter {deps,ri.iter} ;; iter++ ) {
				bool       seen_all = iter==deps.end()            ;
				Dep const& dep      = seen_all ? Sentinel : *iter ;                                       // use empty dep as sentinel
				//
				if (!dep.parallel) {
					state.stamped_err   = state.proto_err   ;                                             // proto become stamped upon sequential dep
					state.stamped_modif = state.proto_modif ;                                             // .
					if ( critical_modif && !seen_all ) {                                                  // suppress deps following modified critical one, except static deps as no-access
						::vector<Dep> static_deps ; static_deps.reserve(r->deps_attrs.spec.deps.size()) ; // anticipate no dynamic static deps
						for( DepsIter it=iter ; it!=deps.end() ; it++ ) if (it->dflags[Dflag::Static]) {
							static_deps.push_back(*it) ;
							static_deps.back().accesses = {} ;
						}
						deps.replace_tail(iter,static_deps) ;
						seen_all = !static_deps ;
					}
					stamped_seen_waiting = proto_seen_waiting ;
					if ( query && (stamped_seen_waiting||state.stamped_modif||+state.stamped_err) ) {     // no reason to analyze any further, we have the answer
						report_reason = reason(ri.state) ;
						goto Return ;
					}
				}
				if (!proto_seen_waiting) {
					ri.iter  = iter.digest(deps) ;                                                        // fast path : info is recorded in ri, next time, restart analysis here
					ri.state = state             ;                                                        // .
				}
				if ( seen_all || (!dep.parallel&&critical_waiting) ) break ;
				NodeData &         dnd         = *Node(dep)                                   ;
				bool               dep_modif   = false                                        ;
				RunStatus          dep_err     = RunStatus::Ok                                ;
				bool               is_static   =  dep.dflags[Dflag::Static     ]              ;
				bool               required    =  dep.dflags[Dflag::Required   ]              ;
				bool               sense_err   = !dep.dflags[Dflag::IgnoreError]              ;
				bool               is_critical = +dep.accesses && dep.dflags[Dflag::Critical] ;
				bool               modif       = state.stamped_modif || ri.force              ;
				bool               may_care    = +dep.accesses || (modif&&is_static)          ;           // if previous modif, consider static deps as fully accessed, as initially
				NodeReqInfo const* cdri        = &dep->c_req_info(req)                        ;           // avoid allocating req_info as long as not necessary
				NodeReqInfo      * dri         = nullptr                                      ;           // .
				NodeGoal           dep_goal    =
					query                                        ? NodeGoal::Dsk
				:	(may_care&&!no_run_reason(state)) || archive ? NodeGoal::Dsk
				:	may_care || sense_err                        ? NodeGoal::Status
				:	is_static || required                        ? NodeGoal::Status
				:	                                               NodeGoal::None
				;
				if (!dep_goal) continue ;                                                                 // this is not a dep (not static while asked for makable only)
			RestartDep :
				if (!cdri->waiting()) {
					ReqInfo::WaitInc sav_n_wait { ri } ;                                                  // appear waiting in case of recursion loop (loop will be caught because of no job on going)
					if (!dri        ) cdri = dri    = &dep->req_info(*cdri) ;                             // refresh cdri in case dri allocated a new one
					if (dep_live_out) dri->live_out = true                  ;                             // ask live output for last level if user asked it
					Bool3 speculate_dep =
						is_static                     ? ri.speculate                                      // static deps do not disappear
					:	stamped_seen_waiting || modif ?              Yes                                  // this dep may disappear
					:	+state.stamped_err            ? ri.speculate|Maybe                                // this dep is not the origin of the error
					:	                                ri.speculate                                      // this dep will not disappear from us
					;
					if (special!=Special::Req) dnd.asking = idx() ;                                       // Req jobs are fugitive, dont record them
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					dnd.make( *dri , mk_action(dep_goal,query) , speculate_dep ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				}
				if ( is_static && dnd.buildable<Buildable::Yes ) sure = false ;                           // buildable (remember it is pessimistic) is better after make() (i.e. less pessimistic)
				if (cdri->waiting()) {
					if      ( is_static                                            ) ri.speculative_wait = false ; // we are non-speculatively waiting, even if after a speculative wait
					else if ( !stamped_seen_waiting && (+state.stamped_err||modif) ) ri.speculative_wait = true  ;
					proto_seen_waiting = true ;
					if (!dri) cdri = dri = &dnd.req_info(*cdri) ;                                                  // refresh cdri in case dri allocated a new one
					dnd.add_watcher(*dri,idx(),ri,dep_pressure) ;
					critical_waiting |= is_critical                            ;
					report_reason    |= { JobReasonTag::BusyDep , +Node(dep) } ;
				} else if (!dnd.done(*cdri,dep_goal)) {
					SWEAR(query) ;                                                                                 // unless query, after having called make, dep must be either waiting or done
					proto_seen_waiting  = true                              ;                                      // if queried dep is not done, it would have been waiting if not queried
					state.reason       |= {JobReasonTag::DepOutOfDate,+dep} ;
				} else {
					bool dep_missing_dsk = !query && may_care && !dnd.done(*cdri,NodeGoal::Dsk) ;
					state.missing_dsk |= dep_missing_dsk   ;                                                       // job needs this dep if it must run
					dep_modif          = !dep.up_to_date() ;
					if ( dep_modif && status==Status::Ok ) {                                                       // no_trigger only applies to successful jobs
						// if not full, a dep is only used to compute resources
						// if asked by user, record to repeat in summary
						if      (!dep.dflags[Dflag::Full])   dep_modif = false ;
						else if ( dep.no_trigger()       ) { dep_modif = false ; trace("no_trigger",dep) ; req->no_triggers.push(dep) ; }
					}
					if ( +state.stamped_err  ) goto Continue ;                                                     // we are already in error, no need to analyze errors any further
					if ( !is_static && modif ) goto Continue ;                                                     // if not static, errors may be washed by previous modifs, dont record them
					// analyze error
					if (dep_modif) {
						if ( dep.is_crc && dep.never_match() ) { state.reason |= {JobReasonTag::DepUnstable ,+dep} ; trace("unstable_modif",dep) ; }
						else                                     state.reason |= {JobReasonTag::DepOutOfDate,+dep} ;
					}
					if ( may_care && +(cdri->overwritten&dep.accesses) ) {
						state.reason |= {JobReasonTag::DepOverwritten,+dep} ;
						dep_err       = RunStatus::DepError                 ;
						goto Continue ;
					}
					Bool3 ok = dnd.ok() ; if ( ok==No && !sense_err ) ok = Yes ;
					switch (ok) {
						case No :
							trace("dep_err",dep,STR(sense_err)) ;
							state.reason |= {JobReasonTag::DepErr,+dep} ;
							dep_err       = RunStatus::DepError         ;
						break ;
						case Maybe :                                                                               // dep is not buidlable, check if required
							if (dnd.status()==NodeStatus::Transient) {                                             // dep uphill is a symlink, it will disappear at next run
								trace("transient",dep) ;
								state.reason |= {JobReasonTag::DepTransient,+dep} ;
								break ;
							}
							if      (is_static) { trace("missing_static"  ,dep) ; state.reason |= {JobReasonTag::DepMissingStatic  ,+dep} ; dep_err = RunStatus::MissingStatic ; break ; }
							else if (required ) { trace("missing_required",dep) ; state.reason |= {JobReasonTag::DepMissingRequired,+dep} ; dep_err = RunStatus::DepError      ; break ; }
							dep_missing_dsk |= !query && cdri->manual>=Manual::Changed ;                           // ensure dangling are correctly handled
						[[fallthrough]] ;
						case Yes :
							if (dep_goal==NodeGoal::Dsk) {                                                         // if asking for disk, we must check disk integrity
								switch(cdri->manual) {
									case Manual::Empty   :
									case Manual::Modif   : state.reason |= {JobReasonTag::DepUnstable,+dep} ; dep_err = RunStatus::DepError ; trace("dangling",dep,cdri->manual) ; break ;
									case Manual::Unlnked : state.reason |= {JobReasonTag::DepUnlnked ,+dep} ;                                 trace("unlnked" ,dep             ) ; break ;
								DN}
							} else if ( dep_modif && at_end && dep_missing_dsk ) {                                 // dep out of date but we do not wait for it being rebuilt
								dep_goal = NodeGoal::Dsk ;                                                         // we must ensure disk integrity for detailed analysis
								trace("restart_dep",dep) ;
								goto RestartDep/*BACKWARD*/ ;
							}
						break ;
					DF}                                                                                            // NO_COV
				}
			Continue :
				trace("dep",ri,dep,dep_goal,*cdri,STR(dnd.done(*cdri)),STR(dnd.ok()),dnd.crc,dep_err,STR(dep_modif),STR(critical_modif),STR(critical_waiting),state.reason) ;
				//
				if ( state.missing_dsk && !no_run_reason(state) ) {
					SWEAR(!query) ;                                       // when query, we cannot miss dsk
					trace("restart_analysis") ;
					SWEAR(!ri.reason,ri.reason) ;                         // we should have asked for dep on disk if we had a reason to run
					ri.reason = state.reason ;                            // record that we must ask for dep on disk
					ri.reset(idx()) ;
					goto RestartAnalysis/*BACKWARD*/ ;
				}
				SWEAR(!( +dep_err && modif && !is_static )) ;             // if earlier modifs have been seen, we do not want to record errors as they can be washed, unless static
				state.proto_err    = ::max( state.proto_err , dep_err ) ; // |= is forbidden for bit fields
				state.proto_modif  = state.proto_modif | dep_modif      ; // .
				critical_modif    |= dep_modif && is_critical           ;
			}
			if (ri.waiting()                             ) goto Wait ;
			if (sure                                     ) mk_sure() ;    // improve sure (sure is pessimistic)
			if (+(run_status=ri.state.stamped_err)       ) goto Done ;
			if (no_run_reason(ri.state)==NoRunReason::Dep) goto Done ;
		}
	Run :
		switch (no_run_reason(ri.state)) {
			case NoRunReason::RetryLoop  : trace("fail_loop"  ) ; pre_reason = JobReasonTag::None                                                ; goto Done ;
			case NoRunReason::LostLoop   : trace("lost_loop"  ) ; status     = status<Status::Early ? Status::EarlyLostErr : Status::LateLostErr ; goto Done ;
			case NoRunReason::SubmitLoop : trace("submit_loop") ; status     = Status::SubmitLoop                                                ; goto Done ;
		DN}
		report_reason = ri.reason = reason(ri.state) ;                    // ensure we have a reason to report that we would have run if not queried
		trace("run",ri,pre_reason,run_status) ;
		if (query) goto Return ;
		if (ri.state.missing_dsk) {                                       // cant run if we are missing some deps on disk, XXX! : rework so that this never fires up
			SWEAR( !is_infinite(special) , special,idx() ) ;              // Infinite do not process their deps
			ri.reset(idx()) ;
			goto RestartAnalysis/*BACKWARD*/ ;
		}
		//                                                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (is_special()) {                                   _submit_special( ri                ) ; goto Done ; } // special never report new deps
		else              { inc_submits(ri.reason.tag) ; if (!_submit_plain  ( ri , dep_pressure ))  goto Done ; } // if no new deps, we cannot be waiting and we are done
		//                                                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (ri.waiting()) goto Wait ;
		SWEAR(!ri.running()) ;
		make_action = MakeAction::End ;                                                            // restart analysis as if called by end() as in case of flash execution, submit has called end()
		ri.inc_wait() ;                                                                            // .
		asked_reason = {} ;                                                                        // .
		ri.reason    = {} ;                                                                        // .
		trace("restart_analysis",ri) ;
		goto RestartFullAnalysis/*BACKWARD*/ ;
	Done :
		SWEAR( !ri.running() && !ri.waiting() , idx() , ri ) ;
		ri.step(Step::Done,idx()) ;
		ri.reason = {} ;                                                                           // no more reason to run as analysis showed it is ok now
	Wakeup :
		if ( auto it = req->missing_audits.find(idx()) ; it!=req->missing_audits.end() && !req.zombie() ) {
			JobAudit const& ja = it->second ;
			trace("report_missing",ja) ;
			//
			if (ja.report!=JobReport::Hit) req->stats.move(JobReport::Rerun,ja.report,exec_time) ; // if not Hit, then job was rerun and ja.report is the report that would have been done w/o rerun
			//
			JobReason jr  = reason(ri.state)                                                              ;
			::string  pfx = status==Status::SubmitLoop ? "" : ja.report==JobReport::Hit ? "hit_" : "was_" ;
			if (ja.has_stderr) {
				JobEndRpcReq jerr = idx().job_info(JobInfoKind::End).end ;
				if (jr.tag>=JobReasonTag::Err) audit_end( ri , true/*with_stats*/ , pfx , MsgStderr{.msg=reason_str(jr),.stderr=jerr.msg_stderr.stderr} , jerr.digest.max_stderr_len ) ;
				else                           audit_end( ri , true/*with_stats*/ , pfx , MsgStderr{.msg=ja.msg        ,.stderr=jerr.msg_stderr.stderr} , jerr.digest.max_stderr_len ) ;
			} else {
				if (jr.tag>=JobReasonTag::Err) audit_end( ri , true/*with_stats*/ , pfx , MsgStderr{.msg=reason_str(jr)                               } ) ;
				else                           audit_end( ri , true/*with_stats*/ , pfx , MsgStderr{.msg=ja.msg                                       } ) ;
			}
			req->missing_audits.erase(it) ;
		}
		trace("wakeup",ri) ;
		//                                  vvvvvvvvvvvvvvvvvvvv
		if ( ri.done() && wakeup_watchers ) ri.wakeup_watchers() ;
		//                                  ^^^^^^^^^^^^^^^^^^^^
		report_reason = reason(ri.state) ;
		goto Return ;
	Wait :
		trace("wait",ri) ;
	Return :
		return report_reason ;
	}

	void JobData::_propag_speculate(ReqInfo const& cri) const {
		Bool3 proto_speculate = No ;
		Bool3 speculate       = No ;
		for ( Dep const& dep : deps ) {
			if (!dep.parallel) speculate |= proto_speculate ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			dep->propag_speculate( cri.req , cri.speculate | (speculate&(!dep.dflags[Dflag::Static])) ) ;                                          // static deps are never speculative
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			NodeReqInfo const& cdri = dep->c_req_info(cri.req) ;
			if ( !dep.is_crc || cdri.waiting() ) { proto_speculate = Yes ; continue ; }
			Bool3 dep_ok = cdri.done(NodeGoal::Status) ? dep->ok(cdri,dep.accesses) : Maybe ;
			switch (dep_ok) {
				case Yes   :                                                                                                               break ;
				case Maybe : if (  dep.dflags[Dflag::Required   ] || dep.dflags[Dflag::Static] ) { proto_speculate |= Maybe ; continue ; } break ;
				case No    : if ( !dep.dflags[Dflag::IgnoreError] || +cdri.overwritten         ) { proto_speculate |= Maybe ; continue ; } break ;
				default : FAIL(dep_ok) ;                                                                                                           // NO_COV
			}
			if ( +dep.accesses && !dep.up_to_date() ) proto_speculate = Yes ;
		}
	}

	MsgStderr JobData::special_msg_stderr( Node node , bool short_msg ) const {
		if (is_ok(status)!=No) return {} ;
		Rule      r          = rule()            ;
		MsgStderr msg_stderr ;
		::string& msg        = msg_stderr.msg    ;
		::string& stderr     = msg_stderr.stderr ;
		switch (r->special) {
			case Special::Plain :
				SWEAR(idx().frozen()) ;
				if (+node) return {.msg="frozen file does not exist while not phony : "+node->name()+'\n'} ;
				else       return {.msg="frozen file does not exist while not phony\n"                   } ;
			case Special::InfiniteDep  :
				msg << cat("max dep depth limit (",g_config->max_dep_depth,") reached, consider : lmake.config.max_dep_depth = ",g_config->max_dep_depth+1     ," (or larger)") ;
				goto DoInfinite ;
			case Special::InfinitePath : {
				msg << cat("max path limit ("     ,g_config->path_max     ,") reached, consider : lmake.config.max_path = "     ,(*deps.begin())->name().size()," (or larger)") ;
			DoInfinite :
				if (short_msg) {
					auto gen_dep = [&](::string const& dn)->void {
						if (dn.size()>111) stderr << dn.substr(0,50)<<"...("<<widen(cat(dn.size()-100),3,true/*right*/)<<")..."<<dn.substr(dn.size()-50) ;
						else               stderr << dn                                                                                                  ;
						stderr <<'\n' ;
					} ;
					::vector_s dns ;
					for( Dep const& d : deps ) dns.push_back(d->name()) ;
					if (dns.size()>23) {
						for(                  NodeIdx i : iota(10) ) gen_dep(dns[              i]) ;
						for( [[maybe_unused]] NodeIdx _ : iota( 3) ) stderr << ".\n.\n.\n"         ;
						for(                  NodeIdx i : iota(10) ) gen_dep(dns[dns.size()-10+i]) ;
					} else {
						for( ::string const& dn : dns ) gen_dep(dn) ;
					}
				} else {
					for( Dep const& d : deps ) stderr << d->name() << '\n' ;
				}
				return msg_stderr ;
			}
			default :
				return {.msg=cat(r->special," error\n")} ;
		}
	}

	void JobData::_submit_special(ReqInfo& ri) {                   // never report new deps
		Trace trace("submit_special",idx(),ri) ;
		Req     req     = ri.req          ;
		Special special = rule()->special ;
		bool    frozen_ = idx().frozen()  ;
		//
		if (frozen_) req->frozen_jobs.push(idx()) ;                // record to repeat in summary
		//
		switch (special) {
			case Special::Plain : {
				SWEAR(frozen_) ;                                   // only case where we are here without special rule
				SpecialStep special_step = SpecialStep::Idle     ;
				Node        worst_target ;
				Bool3       modified     = No                    ;
				NfsGuard    nfs_guard    { g_config->file_sync } ;
				for( Target t : targets ) {
					::string    tn = t->name()         ;
					SpecialStep ss = SpecialStep::Idle ;
					if (!( t->crc.valid() && FileSig(nfs_guard.access(tn))==t->date().sig )) {
						FileSig sig  ;
						Crc   crc { tn , /*out*/sig } ;
						modified |= crc.match(t->crc) ? No : t->crc.valid() ? Yes : Maybe ;
						Trace trace( "frozen" , t->crc ,"->", crc , t->date() ,"->", sig ) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvv
						t->refresh( crc , {sig,{}} ) ;             // if file disappeared, there is not way to know at which date, we are optimistic here as being pessimistic implies false overwrites
						//^^^^^^^^^^^^^^^^^^^^^^^^^^
						if      ( crc!=Crc::None || t.tflags[Tflag::Phony]           ) ss = SpecialStep::Ok  ;
						else if ( t.tflags[Tflag::Target] && t.tflags[Tflag::Static] ) ss = SpecialStep::Err ;
						else                                                           t->actual_job() = {} ;  // unlink of a star or side target is nothing
					}
					if (ss>special_step) { special_step = ss ; worst_target = t ; }
				}
				status = special_step==SpecialStep::Err ? Status::Err : Status::Ok ;
				audit_end_special( req , special_step , modified , worst_target ) ;
			} break ;
			case Special::Req :
				status = Status::Ok ;
			break ;
			case Special::InfiniteDep  :
			case Special::InfinitePath :
				status = Status::Err ;
				audit_end_special( req , SpecialStep::Err , No/*modified*/ ) ;
			break ;
		DF}                                                                                                    // NO_COV
	}

	bool/*maybe_new_deps*/ JobData::_submit_plain( ReqInfo& ri , CoarseDelay pressure ) {
		using Step = JobStep ;
		Rule            r     = rule()  ;
		Req             req   = ri.req  ;
		Rule::RuleMatch match { idx() } ;
		Trace trace("_submit_plain",idx(),ri,pressure) ;
		SWEAR(!ri.waiting(),ri) ;
		SWEAR(!ri.running(),ri) ;
		for( Req rr : running_reqs(false/*with_zombies*/) ) if (rr!=req) {
			ReqInfo const& cri = c_req_info(rr) ;
			ri.step(cri.step(),idx()) ;                                                  // Exec or Queued, same as other reqs
			ri.inc_wait() ;
			if (ri.step()==Step::Exec) req->audit_job(Color::Note,"started",idx()) ;
			SubmitAttrs sa = {
				.pressure = pressure
			,	.live_out = ri.live_out
			,	.nice     = rr->nice
			} ;
			//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			ri.miss_live_out = Backend::s_add_pressure( backend , +idx() , +req , sa ) ; // tell backend of new Req, even if job is started and pressure has become meaningless
			//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("other_req",rr,ri) ;
			return true/*maybe_new_deps*/ ;
		}
		//
		for( Node t : targets ) t->set_buildable() ;                                     // we will need to know if target is a source, possibly in another thread, we'd better call set_buildable here
		// do not generate error if *_ancillary_attrs is not available, as we will not restart job when fixed : do our best by using static info
		::vmap_s<DepDigest>  early_deps             ;
		SubmitAncillaryAttrs submit_ancillary_attrs ;
		try {
			submit_ancillary_attrs = r->submit_ancillary_attrs.eval( idx() , match , &early_deps ) ; // dont care about dependencies as these attributes have no impact on result
		} catch (MsgStderr const& msg_err) {
			submit_ancillary_attrs = r->submit_ancillary_attrs.spec ;
			req->audit_job(Color::Note,"no_dynamic",idx()) ;
			req->audit_stderr( idx() , { ensure_nl(r->submit_ancillary_attrs.s_exc_msg(true/*using_static*/))+msg_err.msg , msg_err.stderr } , 0/*max_stderr_len*/ , 1 ) ;
		}
		for( auto& [k,dd] : early_deps ) { dd.accesses = {} ; dd.dflags = {} ; }                     // suppress sensitiviy to read files as ancillary has no impact on job result nor status ...
		CacheIdx cache_idx = 0 ;                                                                     // ... just record deps to trigger building on a best effort basis
		if ( +req->cache_method && +submit_ancillary_attrs.cache ) {
			auto it = g_config->cache_idxs.find(submit_ancillary_attrs.cache) ;
			if (it!=g_config->cache_idxs.end()) {
				cache_idx = it->second ;
				if ( cache_idx && has_download(req->cache_method) ) {
					::vmap_s<DepDigest> dns ;
					for( Dep const& d : deps ) {
						DepDigest dd = d ; dd.crc(d->crc) ;                                                                       // provide node actual crc as this is the hit criteria
						dns.emplace_back(d->name(),dd) ;
					}
					Cache*       cache       = Cache::s_tab[cache_idx]             ;
					Cache::Match cache_match = cache->match( unique_name() , dns ) ;
					if (!cache_match.completed) FAIL("delayed cache not yet implemented") ;
					switch (cache_match.hit) {
						case Yes :
							try {
								NfsGuard nfs_guard { g_config->file_sync } ;
								//
								vmap<Node,FileAction> fas     = pre_actions(match) ;
								::vmap_s<FileAction>  actions ;                                                  for( auto [t,a] : fas ) actions.emplace_back( t->name() , a ) ;
								::string              dfa_msg = do_file_actions( ::move(actions) , nfs_guard ) ;
								//
								if (+dfa_msg) {
									req->audit_job ( Color::Note , "wash"  , idx()     ) ;
									req->audit_info( Color::Note , dfa_msg         , 1 ) ;
									trace("hit_msg",dfa_msg,ri) ;
								}
								//
								JobExec je       { idx() , New }                              ;                                   // job starts and ends, no host
								JobInfo job_info = cache->download(cache_match.key,nfs_guard) ;
								job_info.start.pre_start.job       = +idx()    ;                                                  // repo dependent
								job_info.start.submit_attrs.reason = ri.reason ;                                                  // context dependent
								job_info.end  .end_date            = New       ;                                                  // execution dependnt
								//
								JobDigest<Node> digest = job_info.end.digest ;                                                    // gather info before being moved
								Job::s_record_thread.emplace(idx(),::move(job_info.start)) ;
								Job::s_record_thread.emplace(idx(),::move(job_info.end  )) ;
								//
								if (ri.live_out) je.live_out(ri,job_info.end.stdout) ;
								//
								ri.step(Step::Hit,idx()) ;
								trace("hit_result") ;
								je.end(::move(digest)) ;
								req->stats.add(JobReport::Hit) ;
								req->missing_audits[idx()] = { .report=JobReport::Hit , .has_stderr=+job_info.end.msg_stderr.stderr } ;
								goto ResetReqInfo ;
							} catch (::string const&e) {
								trace("hit_throw",e) ;
							}                                                                                                     // if we cant download result, it is like a miss
						break ;
						case Maybe : {
							::vector<Dep> ds ; ds.reserve(cache_match.new_deps.size()) ; for( auto& [dn,dd] : cache_match.new_deps ) ds.emplace_back( Node(New,dn) , dd ) ;
							deps.assign(ds) ;
							status = Status::CacheMatch ;
							trace("hit_deps") ;
						}
						ResetReqInfo :
							for( Req r : reqs() ) if (c_req_info(r).step()==Step::Dep) req_info(r).reset(idx(),true/*has_run*/) ; // there are new deps and req_info is not reset spontaneously, ...
							return true/*maybe_new_deps*/ ;                                                                       // ... so we have to ensure ri.iter is still a legal iterator
						case No :
							trace("hit_miss") ;
						break ;
					DF}                                                                                                           // NO_COV
				}
			}
		}
		//
		SubmitRsrcsAttrs submit_rsrcs_attrs ;
		size_t           n_ancillary_deps   = early_deps.size() ;
		try {
			submit_rsrcs_attrs = r->submit_rsrcs_attrs.eval( idx() , match , &early_deps ) ;
		} catch (MsgStderr const& msg_err) {
			req->audit_job   ( Color::Err  , "failed" , idx()                                                                                                       ) ;
			req->audit_stderr( idx() , { ensure_nl(r->submit_rsrcs_attrs.s_exc_msg(false/*using_static*/))+msg_err.msg , msg_err.stderr } , 0/*max_stderr_len*/ , 1 ) ;
			run_status = RunStatus::Error ;
			trace("no_rsrcs",ri) ;
			return false/*maybe_new_deps*/ ;
		}
		for( NodeIdx i : iota(n_ancillary_deps,early_deps.size()) ) early_deps[i].second.dflags &= ~Dflag::Full ;                 // mark new deps as resources only
		for( auto const& [dn,dd] : early_deps ) {
			Node         d   { New , dn }       ;
			NodeReqInfo& dri = d->req_info(req) ;
			d->make(dri,NodeMakeAction::Dsk) ;
			if (dri.waiting()) d->add_watcher(dri,idx(),ri,pressure) ;
		}
		if (ri.waiting()) {
			trace("waiting_rsrcs") ;
			return true/*maybe_new_deps*/ ;
		}
		//
		ri.inc_wait() ;                              // set before calling submit call back as in case of flash execution, we must be clean
		ri.step(Step::Queued,idx()) ;
		backend = submit_rsrcs_attrs.backend ;
		if (!has_upload(req->cache_method)) cache_idx = 0 ;
		try {
			Tokens1 tokens1 = submit_rsrcs_attrs.tokens1() ;
			SubmitAttrs sa {
				.deps      = ::move(early_deps )
			,	.reason    =        ri.reason
			,	.pressure  =        pressure
			,	.cache_idx =        cache_idx
			,	.tokens1   =        tokens1
			,	.live_out  =        ri.live_out
			,	.nice      =        req->nice
			} ;
			estimate_stats(tokens1) ;                // refine estimate with best available info just before submitting
			//       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Backend::s_submit( backend , +idx() , +req , ::move(sa) , ::move(submit_rsrcs_attrs.rsrcs) ) ;
			//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			for( Node t : targets ) t->busy = true ; // make targets busy once we are sure job is submitted
		} catch (::string const& e) {
			ri.dec_wait() ;                          // restore n_wait as we prepared to wait
			ri.step(Step::None,idx()) ;
			status  = Status::EarlyErr ;
			req->audit_job ( Color::Err  , "failed" , idx()     ) ;
			req->audit_info( Color::Note , e                , 1 ) ;
			trace("submit_err",ri) ;
			return false/*maybe_new_deps*/ ;
		} ;
		trace("submitted",ri) ;
		return true/*maybe_new_deps*/ ;
	}

	void JobData::audit_end_special( Req req , SpecialStep step , Bool3 modified , Node node ) const {
		//
		SWEAR( status>Status::Garbage , status ) ;
		Trace trace("audit_end_special",idx(),req,step,modified,status) ;
		//
		bool      frozen_    = idx().frozen()           ;
		MsgStderr msg_stderr = special_msg_stderr(node) ;
		::string  step_str   ;
		switch (step) {
			case SpecialStep::Idle :                                                                             break ;
			case SpecialStep::Ok   : step_str = modified==Yes ? "changed" : modified==Maybe ? "new" : "steady" ; break ;
			case SpecialStep::Err  : step_str = "failed"                                                       ; break ;
			case SpecialStep::Loop : step_str = "loop"                                                         ; break ;
		DF}                                                                                                              // NO_COV
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
			/**/                    req->audit_job (color      ,step_str,idx()     ) ;
			if (+msg_stderr.msg   ) req->audit_info(Color::Note,msg_stderr.msg   ,1) ;
			if (+msg_stderr.stderr) req->audit_info(Color::None,msg_stderr.stderr,1) ;
		}
	}

	bool/*ok*/ JobData::forget( bool targets_ , bool deps_ ) {
		Trace trace("Jforget",idx(),STR(targets_),STR(deps_)) ;
		for( [[maybe_unused]] Req r : running_reqs() ) return false ; // ensure job is not running
		status = Status::New ;
		fence() ;                                                     // once status is New, we are sure target is not up to date, we can safely modify it
		run_status = RunStatus::Ok ;
		if (deps_) {
			::vector<Dep> static_deps ;
			for( Dep const& d : deps )  if (d.dflags[Dflag::Static]) static_deps.push_back(d) ;
			deps.assign(static_deps) ;
		}
		if ( targets_ && !rule()->is_special()) _reset_targets() ;
		trace("summary",deps) ;
		return true ;
	}

	bool JobData::running( bool with_zombies , bool hit_ok ) const {
		for( Req r : Req::s_reqs_by_start ) if ( (with_zombies||!r.zombie()) && c_req_info(r).running(hit_ok) ) return true ;
		return false ;
	}

	::vector<Req> JobData::running_reqs( bool with_zombies , bool hit_ok ) const {                                                 // sorted by start
		::vector<Req> res ; res.reserve(Req::s_n_reqs()) ;                                                                         // pessimistic, so no realloc
		for( Req r : Req::s_reqs_by_start ) if ( (with_zombies||!r.zombie()) && c_req_info(r).running(hit_ok) ) res.push_back(r) ;
		return res ;
	}

}
