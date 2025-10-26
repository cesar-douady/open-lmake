// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include "rpc_job.hh"

using namespace Caches ;
using namespace Disk   ;

namespace Engine {

	//
	// JobTgts
	//

	::string& operator+=( ::string& os , JobTgts jts ) { // START_OF_NO_COV
		return os<<jts.view() ;
	}                                                    // END_OF_NO_COV

	//
	// JobTgt
	//

	::string& operator+=( ::string& os , JobTgt jt ) {    // START_OF_NO_COV
		if (!jt) return            os << "(J())"        ;
		/**/                       os << "(" << Job(jt) ;
		if (jt._is_static_phony()) os << ",static"      ;
		else                       os << ",star"        ;
		return                     os << ')'            ;
	}                                                     // END_OF_NO_COV

	//
	// JobReqInfo
	//

	::string& operator+=( ::string& os , JobReqInfo::State const& ris ) {                                                            // START_OF_NO_COV
		First first ;
		/**/                                                os <<'('                                                               ;
		if ( +ris.reason                                  ) os <<first("",",")<<       ris.reason                                  ;
		if (  ris.missing_dsk                             ) os <<first("",",")<<'D'                                                ;
		if ( +ris.stamped.err      || +ris.proto.err      ) os <<first("",",")<<"E:"<< ris.stamped.err     <<"->"<<ris.proto.err   ;
		if (  ris.stamped.modif    ||  ris.proto.modif    ) os <<first("",",")<<"M:"<< ris.stamped.modif   <<"->"<<ris.proto.modif ;
		return                                              os <<')'                                                               ;
	}                                                                                                                                // END_OF_NO_COV

	::string& operator+=( ::string& os , JobReqInfo const& ri ) { // START_OF_NO_COV
		/**/                   os << "JRI(" << ri.req     ;
		if ( ri.speculate!=No) os <<",S:" << ri.speculate ;
		if ( ri.modified     ) os <<",mod"                ;
		/**/                   os <<','   << ri.step()    ;
		/**/                   os <<'@'   << ri.iter      ;
		/**/                   os <<':'   << ri.state     ;
		if ( ri.n_wait       ) os <<",W:" << ri.n_wait    ;
		if (+ri.reason       ) os <<','   << ri.reason    ;
		if (+ri.n_losts      ) os <<",NL:"<< ri.n_losts   ;
		if (+ri.n_retries    ) os <<",NR:"<< ri.n_retries ;
		if (+ri.n_submits    ) os <<",NS:"<< ri.n_submits ;
		if (+ri.n_runs       ) os <<",NX:"<< ri.n_runs    ;
		if ( ri.miss_live_out) os <<",miss_live_out"      ;
		return                 os <<')'                   ;
	}                                                             // END_OF_NO_COV

	void JobReqInfo::step( Step s , Job j ) {
		if (_step==s) return ;                // fast path
		//
		if ( _step>=Step::MinCurStats && _step<Step::MaxCurStats1 ) { req->stats.cur(_step)-- ; if (_step==Step::Dep) req->stats.waiting_cost -= j->c_cost() ; }
		if ( s    >=Step::MinCurStats && s    <Step::MaxCurStats1 ) { req->stats.cur(s    )++ ; if (s    ==Step::Dep) req->stats.waiting_cost += j->c_cost() ; }
		_step = s ;
	}

	//
	// Job
	//

	QueueThread<::pair<Job,JobInfo1>,true/*Flush*/,true/*QueueAccess*/> Job::s_record_thread ;

	::string& operator+=( ::string& os , Job j ) { // START_OF_NO_COV
		/**/    os << "J(" ;
		if (+j) os << +j   ;
		return  os << ')'  ;
	}                                              // END_OF_NO_COV

	void Job::pop(Req req) {
		Trace trace("pop",self,req) ;
		req->jobs.erase(self) ;
		pop() ;
	}

	Job::Job( Rule::RuleMatch&& match , Req req , DepDepth lvl ) {
		Trace trace("Job",match,req,lvl) ;
		if (!match) { trace("no_match") ; return ; }
		Rule rule = match.rule    ;
		if ( ::pair_s<VarIdx> msg=match.reject_msg() ; +msg.first ) {
			trace("not_accepted") ;
			::pair_s<RuleData::MatchEntry> const& k_me = rule->matches[msg.second] ;
			MatchKind                             mk   = k_me.second.flags.kind()  ;
			req->audit_job   ( Color::Warning , cat("bad_",mk) , rule , match.name() ) ;
			req->audit_stderr( self , {.msg=cat(mk,' ',k_me.first," : ",msg.first)}  ) ;
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
				req->audit_job   ( Color::Note , "deps_not_avail" , rule , match.name()                                             ) ;
				req->audit_stderr( self , {ensure_nl(rule->deps_attrs.s_exc_msg(false/*using_static*/))+msg_err.msg,msg_err.stderr} ) ;
			}
			return ;
		}
		::vector<Dep>       deps ; deps.reserve(dep_specs_holes.size()) ;
		::umap<Node,VarIdx> dis  ; dis .reserve(dep_specs_holes.size()) ;
		for( auto const& k_ds : dep_specs_holes ) {
			DepSpec const& ds = k_ds.second ;
			if (!ds.txt) continue ;                                                                                                           // filter out holes
			Node           d  { New , ds.txt }                                                  ;
			Accesses       a  = ds.extra_dflags[ExtraDflag::Ignore] ? Accesses() : FullAccesses ;
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
		if (+digest.first) {                                                        // only bother user for bad deps if job otherwise applies, so handle them once static deps have been analyzed
			req->audit_job   ( Color::Warning , "bad_dep" , rule , match.name() ) ;
			req->audit_stderr( self , {.msg=digest.first}                       ) ;
			return ;
		}
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		//          args for store         args for JobData
		self = Job( match.full_name(),Dflt , match,deps   ) ;                       // initially, static deps are deemed read, then actual accesses will be considered
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
		auto do_entry = [&](::pair<Job,JobInfo1> const& jji) {
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
		::string jaf   ;
		bool     creat ;
		switch (ji.kind()) {
			case JobInfoKind::Start   : serialize( jaf , ji.start   () ) ; creat = true  ; break ;      // start event write to file (new record)
			case JobInfoKind::End     : serialize( jaf , ji.end     () ) ; creat = false ; break ;      // other events append to it
			case JobInfoKind::DepCrcs : serialize( jaf , ji.dep_crcs() ) ; creat = false ; break ;      // .
		DF}                                                                                             // NO_COV ensure something to record
		AcFd( ancillary_file() , {O_WRONLY|(creat?O_TRUNC|O_CREAT:O_APPEND),0666/*mod*/} ).write( jaf ) ;
	}

	void Job::record(JobInfo const& ji) const {
		Trace trace("record",self) ;
		::string jaf ;
		serialize( jaf , ji.start    ) ;
		serialize( jaf , ji.end      ) ;
		serialize( jaf , ji.dep_crcs ) ;
		AcFd( ancillary_file() , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} ).write( jaf ) ;
	}

	//
	// JobExec
	//

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

	void JobExec::give_up( Req req , bool report ) {
		Trace trace("give_up",self,req) ;
		JobData& jd = *self ;
		if (+req) {
			ReqInfo& ri = jd.req_info(req) ;
			ri.step(Step::End,self)            ;                                                                           // ensure no confusion with previous run
			jd.make( ri , MakeAction::GiveUp ) ;
			if      (!jd.running_reqs(false/*with_zombies*/)) for( Node t : self->targets() ) t->busy = false ;            // if job does not continue, targets are no more busy
			else if (report                                 ) req->audit_job(Color::Note,"continue",self,true/*at_end*/) ; // generate a continue line if some other req is still active
			req.chk_end() ;
		} else {
			for( Req r : jd.running_reqs(true/*with_zombies*/) ) give_up(r,false/*report*/)                                ;
			for( Req r : jd.running_reqs(true/*with_zombies*/) ) FAIL(jd.name(),"is still running for",r,"after kill all") ;
		}
	}

	// answer to job execution requests
	JobMngtRpcReply JobExec::manage(EngineClosureJobMngt const& ecjm) const {
		::vector<Req> reqs = self->running_reqs(false/*with_zombies*/) ;
		Trace trace("manage",self,ecjm.proc,ecjm.targets.size(),ecjm.deps.size(),reqs.size()) ;
		//
		JobMngtRpcReply res { .proc=ecjm.proc } ;                                                       // seq_id will be filled in later
		if (+reqs)                                                                                      // if job is not running, it is too late
			switch (ecjm.proc) {
				case JobMngtProc::DepDirect : {
					Job job { Special::Dep , Deps(mk_vector<Node>(ecjm.deps),FullAccesses,DflagsDfltStatic,true/*parallel*/) } ;
					//
					job->fd        () = ecjm.fd     ;
					job->seq_id    () = ecjm.seq_id ;
					job->asking_job() = self        ;
					//
					for( Req req : reqs ) job->make( job->req_info(req) , JobMakeAction::Status , {}/*JobReason*/ , No/*speculate*/ ) ;
					//
					res.proc = JobMngtProc::None ;                                                      // job->make will answer for us
				} break ;
				case JobMngtProc::DepVerbose :
					res.verbose_infos.reserve(ecjm.deps.size()) ;
					for( Dep const& dep : ecjm.deps ) {
						dep.full_refresh( false/*report_no_file*/ , reqs ) ;
						Bool3 dep_ok = Yes ;
						for( Req req : reqs ) {
							NodeReqInfo& dri = dep->req_info(req) ;
							Node(dep)->make(dri,NodeMakeAction::Query) ;
							if      (!dri.done(NodeGoal::Status)  ) { trace("waiting",dep,req) ; dep_ok = Maybe ;         }
							else if (dep->ok(dri,dep.accesses)==No) { trace("bad"    ,dep,req) ; dep_ok = No    ; break ; }
						}
						trace("dep_info",dep,dep_ok) ;
						res.verbose_infos.push_back({ .ok=dep_ok , .crc=dep_ok!=Maybe?dep->crc:Crc() }) ;
					}
				break ;
				case JobMngtProc::ChkDeps : {
					::uset<Node>  old_deps ;
					for( Dep const& d : self->deps )
						for( Node dd=d ; +dd ; dd=dd->dir )
							if (!old_deps.insert(dd).second) break ;                                    // record old deps and all uphill dirs as these are implicit deps
					res.ok = Yes ;
					for( auto const& [t,td] : ecjm.targets ) {
						if (td.pre_exist) {
							Node(t)->set_buildable() ;
							if (!( t->is_src_anti() && t->buildable>Buildable::No )) {
								trace("pre_exist",t) ;
								res.proc = JobMngtProc::ChkTargets ;
								res.ok   = Maybe                   ;
								res.txt  = t->name()               ;
								goto EndChkDeps ;
							}
						}
						trace("target",t) ;
					}
					for( Dep const& dep : ecjm.deps ) {
						dep.full_refresh( false/*report_no_file*/ , reqs ) ;
						if ( dep->buildable!=Buildable::Codec && dep.hot && !old_deps.contains(dep) ) { // hot deps are out-of-date if they are just discovered, but codec files are always correct
							trace("hot",dep) ;
							res.ok  = Maybe       ;
							res.txt = dep->name() ;
							goto EndChkDeps ;
						}
						for( Req req : reqs ) {
							NodeReqInfo& dri  = dep->req_info(req)                               ;
							NodeGoal     goal = +dep.accesses ? NodeGoal::Dsk : NodeGoal::Status ;      // if no access, we do not care about file on disk
							Node(dep)->make(dri,NodeMakeAction::Query) ;
							if      (!dri.done(goal)                                                  ) { trace("waiting",dep,req) ; res.ok = Maybe ; res.txt = dep->name() ; goto EndChkDeps ; }
							else if (dep->ok(dri,dep.accesses)==No && !dep.dflags[Dflag::IgnoreError] ) { trace("bad"    ,dep,req) ; res.ok = No    ; res.txt = dep->name() ;                   }
						}
						trace("dep",dep) ;
					}
				EndChkDeps : ;
				} break ;
			DF}                                                                                         // NO_COV
		trace("done",res) ;
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
			Job aj = t->actual_job ;
			switch (fat) {
				case FileActionTag::UnlinkWarning  :                              ri.req->audit_node( Color::Warning , "unlinked"          , t , 1 ) ; break ;
				case FileActionTag::UnlinkPolluted : { if (aj==self) continue ; } ri.req->audit_node( Color::Warning , "unlinked polluted" , t , 1 ) ; break ;
				default : continue ;
			}
			if (+aj) ri.req->audit_info( Color::Note , "generated by" , aj->name() , 2 ) ;
		}
		if (+msg_stderr.stderr) ri.req->audit_stderr( self , msg_stderr ) ;
		ri.start_reported = true ;
		return true ;
	}

	void JobExec::report_start() const {
		Trace trace("report_start",self) ;
		for( Req req : self->running_reqs(false/*with_zombies*/) ) report_start(self->req_info(req)) ;
	}

	void JobExec::started( bool report , ::vmap<Node,FileActionTag> const& report_unlnks , MsgStderr const& msg_stderr ) {
		Trace trace("started",self) ;
		SWEAR( self->is_plain() , self->rule()->special ) ;
		report |= +report_unlnks || +msg_stderr.stderr ;
		for( Req req : self->running_reqs() ) {
			ReqInfo& ri = self->req_info(req) ;
			ri.start_reported = false ;
			if (report) report_start( ri , report_unlnks , msg_stderr ) ;
			ri.step(JobStep::Exec,self) ;
		}
	}

	JobExec::EndDigest JobExec::end_analyze(JobDigest<Node>&/*inout*/ digest) {
		EndDigest                        res            ;
		JobData&                         jd             = *self                                                             ;
		Status                           status         = digest.status                                                     ; // status may be modified later
		Bool3                            ok             = is_ok  (status)                                                   ;
		bool                             lost           = is_lost(status)                                                   ;
		bool                             modified       = false                                                             ;
		bool                             fresh_deps     = (status<=Status::Early&&!is_lost(status)) || status>Status::Async ; // if job did not go through, old deps are better than new ones
		Rule                             rule           = jd.rule()                                                         ;
		::umap<Node,::pair<FileSig,Crc>> old_srcs       ;                                                                     // remember old src infos before they are updated
		::uset<Node>                     create_encodes ;
		//
		Trace trace("end_analyze",self,digest) ;
		//
		SWEAR(status!=Status::New) ;                   // we just executed the job, it can be neither new, frozen or special
		SWEAR(!frozen()          ) ;                   // .
		SWEAR(rule->is_plain()   ) ;                   // .
		//
		jd.status = Status::New ;                      // ensure we cannot appear up-to-date while working on data
		fence() ;
		res.running_reqs = jd.running_reqs(true/*with_zombies*/) ;
		//
		// handle targets
		//
		for( Node t : jd.targets() ) t->busy = false ; // old targets are no more busy
		//
		if ( !lost && status>Status::Early ) {         // if early, we have not touched the targets, not even washed them, if lost, old targets are better than new ones
			//
			::umap  <Node,Tflags> old_targets ; old_targets.reserve(jd.targets()  .size()) ; for( Target t : jd.targets() ) if (t->has_actual_job(self)) old_targets.try_emplace(t,t.tflags) ;
			::vector<Target     > targets     ; targets    .reserve(digest.targets.size()) ;
			//
			for( auto& [target,td] : digest.targets ) {
				target->set_buildable() ;
				Tflags tflags          = td.tflags              ;
				bool   static_phony    = ::static_phony(tflags) ;
				bool   is_src_anti     = target->is_src_anti()  ;
				Crc    crc             = td.crc                 ;
				bool   unexpected      = false                  ;
				//
				old_targets.erase(target) ;
				//
				if      ( td.pre_exist                               )   target->polluted = Polluted::PreExist ; // pre-existed while not incremental => polluted even if generated by official job
				else if ( tflags[Tflag::Target]                      )   target->polluted = {}                 ;
				else if ( +crc && crc!=Crc::None && crc!=target->crc ) { target->polluted = Polluted::Job      ; target->polluting_job = self ; } // we pollute the official job
				//
				if (+target->polluted) trace("polluted",target->polluted,target->polluting_job) ;
				//
				if (+crc) {
					if (td.written) {
						// file dates are very fuzzy and unreliable, at least, filter out targets we generated ourselves
						if ( +start_date && target->date.date>start_date ) {               // if no start_date.p, job did not execute, it cannot generate a clash
							// /!\ This may be very annoying !
							// A job was running in parallel with us and there was a clash on this target.
							// There are 2 problems : for us and for them.
							// For us, it's ok, we will rerun.
							// But for them, they are already done,  possibly some dependent jobs are done, possibly even Req's are already done and we may have reported ok to the user,
							// and all that is wrong.
							// This is too complex and too rare to detect (and ideally handle).
							// Putting target in clash_nodes will generate a frightening message to user asking to relaunch all commands that were running in parallel.
							if ( crc.valid() && td.tflags[Tflag::Target] ) {               // official targets should have a valid crc, but if not, we dont care
								trace("clash",start_date,target->date.date) ;
								res.target_reason |= {JobReasonTag::ClashTarget,+target} ; // crc is actually unreliable, rerun
							}
							Job aj = target->actual_job ;
							if ( +aj && aj!=self && target->crc.valid() && target->actual_tflags[Tflag::Target] && !is_src_anti ) { // existing crc was believed to be reliable but actually was not
								SWEAR(+aj->rule()) ;                                                                                // what could be this ruleless job that has been run ?!?
								trace("critical_clash",start_date,target->date.date) ;
								for( Req r : target->reqs() ) {
									r->clash_nodes.push(target,{aj,Job(self)}) ;
									target->req_info(r).done_ = NodeGoal::None ;                            // best effort to trigger re-analysis but this cannot be guaranteed (fooled req may be gone)
								}
							}
						}
						//
						if (is_src_anti) {                                                                  // source may have been modified
							old_srcs.try_emplace( target , ::pair(target->date.sig,target->crc) ) ;         // if it is also a dep, at best the dep will see old sig, and we want to associate old crc
							if (!crc.valid()) crc = {target->name()} ;                                      // force crc computation if updating a source
							//
							switch (target->buildable) {
								case Buildable::DynSrc :
								case Buildable::Src    :
									/**/                              if (td.extra_tflags[ExtraTflag::SourceOk]) goto SourceOk ;
									for( Req req : res.running_reqs ) if (req->options.flags[ReqFlag::SourceOk]) goto SourceOk ;
								break ;
							DN}
							/**/                res.severe_msg << "unexpected" ;
							if (crc==Crc::None) res.severe_msg << " unlink of" ;
							else                res.severe_msg << " write to"  ;
							switch (target->buildable) {
								case Buildable::PathTooLong : res.severe_msg << " path too long"  ; break ;
								case Buildable::Anti        :
								case Buildable::DynAnti     : res.severe_msg << " anti-file"      ; break ;
								case Buildable::SrcDir      : res.severe_msg << " source dir"     ; break ;
								case Buildable::SubSrcDir   : res.severe_msg << " source sub-dir" ; break ;
								case Buildable::DynSrc      :
								case Buildable::Src         : res.severe_msg << " source"         ; break ;
								case Buildable::SubSrc      : res.severe_msg << " sub-source"     ; break ;
							DF}                                                                             // NO_COV
							res.severe_msg <<" : "<< mk_file(target->name(),No/*exists*/) <<'\n' ;
							unexpected = true ;
							if (ok==Yes) status = Status::Err ;
						SourceOk : ;
						}
					}
					//
					bool target_modified ;
					if (unexpected) target_modified = !target->crc.match(crc)                                                                          ;
					else            target_modified = target->set_crc_date( crc , { td.sig , td.extra_tflags[ExtraTflag::Late]?end_date:start_date } ) ;
					modified |= target_modified && tflags[Tflag::Target] ;
				}
				if ( crc==Crc::None && !static_phony ) {
					target->actual_job    = {} ;
					target->actual_tflags = {} ;
					trace("unlink",target,td) ;
				} else {                                                                                     // if not actually writing, dont pollute targets of other jobs
					target->actual_job    = self      ;
					target->actual_tflags = td.tflags ;
					//
					targets.emplace_back( target , tflags ) ;
					bool is_src = is_src_anti && target->buildable>Buildable::No ;
					if ( td.pre_exist && !is_src ) res.target_reason |= {JobReasonTag::PrevTarget,+target} ; // sources are not unlinked, hence not marked PrevTarget
					trace("target",target,td) ;
				}
			}
			for( auto [t,tf] : old_targets ) {
				if (tf[Tflag::Incremental]) {
					targets.emplace_back(t,tf) ;                // if an old incremental target has not been touched, it is still there as it has not been washed
				} else {
					NodeData& td = *Node(t) ;                   // t is const
					td.actual_job    = {} ;                     // target is no more generated by us
					td.actual_tflags = {} ;                     // .
					td.polluted      = {} ;                     // .
					td.set_crc_date( Crc::None , start_date ) ; // non-incremental old targets were unlinked at start-of-job time, during washing
				}
			}
			::sort(targets) ;                                   // ease search in targets
			//vvvvvvvvvvvvvvvvvvvvvvvvvv
			jd.targets().assign(targets) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		//
		// handle deps
		//
		bool                              has_updated_dep_crcs = false ;                                    // record acquired dep crc's if we acquired any
		::vector<::pair<Crc,bool/*err*/>> dep_crcs             ;
		if (fresh_deps) {
			::uset<Node>  old_deps ;
			::vector<Dep> deps     ; deps    .reserve(digest.deps.size()) ;
			/**/                     dep_crcs.reserve(digest.deps.size()) ;
			for( Dep const& d : jd.deps )
				for( Node dd=d ; +dd ; dd=dd->dir )
					if (!old_deps.insert(dd).second) break ;                                                // record old deps and all uphill dirs as these are implicit deps
			for( auto& [dn,dd] : digest.deps ) {
				Dep dep { dn , dd } ;
				dep->set_buildable() ;
				if (!old_deps.contains(dep)) {
					res.has_new_deps = true ;
					// dep.hot means dep has been accessed within g_config->date_prc after its mtime (according to Pdate::now()).
					// Because of disk date granularity (usually a few ms) and because of date discrepancy between executing host and disk server (usually a few ms when using NTP).
					// This means that the file could actually have been accessed before and have gotten wrong data.
					// If this occurs, consider dep as unstable if it was not a known dep (we know known deps have been finished before job started).
					if ( dep->buildable!=Buildable::Codec && dep.hot ) {                                    // codec files are always correct
						trace("reset",dep) ;
						dep.del_crc() ;
						res.can_upload = false ;                                                            // dont upload cache with unstable execution
					}
				}
				bool updated_dep_crc = false ;
				if (!dep.is_crc) {
					dep.full_refresh( true/*report_no_file*/ , res.running_reqs ) ;
					dep.acquire_crc() ;
					if (!dep.is_crc) {
						auto it = old_srcs.find(dep) ;
						if (it!=old_srcs.end()) {
							if (dep.sig()==it->second.first) dep.set_crc(it->second.second,false/*err*/) ;
						}
					}
					if (dep.is_crc) {                                                                       // if a dep has become a crc, update digest so that ancillary file reflects it
						dd.set_crc_sig(dep) ;
						updated_dep_crc = true ;
					}
				} else if (dep.never_match()) {
					if (dep->is_src_anti()) dep.full_refresh( true/*report_no_file*/ , res.running_reqs ) ; // the goal is to detect overwritten
					res.has_unstable_deps = true ;
				}
				trace("dep",dep,STR(dep.is_crc),STR(dep.is_crc&&dep.crc().valid())) ;
				/**/                   deps    .push_back   (dep                    ) ;
				if (updated_dep_crc) { dep_crcs.emplace_back(dep.crc(),bool(dep.err)) ; has_updated_dep_crcs = true ; }
				else                   dep_crcs.emplace_back(Crc()    ,false        ) ;
			}
			//vvvvvvvvvvvvvvvvvv
			jd.deps.assign(deps) ;
			//^^^^^^^^^^^^^^^^^^
		}
		//
		// wrap up
		//
		jd.set_exec_ok() ;                                                                                  // effect of old cmd has gone away with job execution
		fence() ;                                                                                           // only update status once other info is set to anticipate crashes
		if ( !lost && +res.target_reason && status>Status::Garbage ) status = Status::BadTarget ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		jd.incremental = digest.incremental ;
		jd.status      = status             ;
		//^^^^^^^^^^^^^^^^^^^^^
		// job_data file must be updated before make is called as job could be remade immediately (if cached), also info may be fetched if issue becomes known
		res.can_upload &= jd.run_status==RunStatus::Ok && ok==Yes ;         // only cache execution without errors
		//
		trace("wrap_up",ok,digest.cache_idx,jd.run_status,STR(res.can_upload),digest.upload_key,STR(digest.incremental)) ;
		if ( +res.severe_msg || digest.has_msg_stderr ) {
			JobInfo ji = job_info() ;
			if (digest.has_msg_stderr) res.msg_stderr = ji.end.msg_stderr ;
			if (+res.severe_msg) {
				ji.end.msg_stderr.msg <<set_nl<< res.severe_msg ;
				s_record_thread.emplace(self,::move(ji.start))  ;           // necessary to restart recording, else ji.end would be appended
				s_record_thread.emplace(self,::move(ji.end  ))  ;
			}
		}
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (has_updated_dep_crcs) s_record_thread.emplace( self , ::move(dep_crcs)                    ) ;
		else                      s_record_thread.emplace( self , ::vector<::pair<Crc,bool/*err*/>>() ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (ok==Yes) jd.record_stats( digest.exec_time , cost , tokens1 ) ; // only update rule based exec time estimate when job is ok as jobs in error may be much faster and are not representative
		//
		for( Req req : jd.running_reqs(true/*with_zombies*/,true/*hit_ok*/) ) {
			ReqInfo& ri = jd.req_info(req) ;
			ri.modified |= modified ;                                       // accumulate modifications until reported
			if (!ri.running()) continue ;
			SWEAR(ri.step()==Step::Exec) ;
			ri.step(Step::End,self) ;                                       // ensure no confusion with previous run, all steps must be updated before any make() is called
		}
		//
		if (+digest.refresh_codecs) {
			trace("refresh_codecs",digest.refresh_codecs) ;
			for( Req req : res.running_reqs )
				for( ::string const& f : digest.refresh_codecs )
					req->refresh_codecs.insert(f) ;
		}
		trace("done",self,STR(modified)) ;
		return res ;
	}

	void JobExec::end(JobDigest<Node>&& digest) {
		Trace trace("end",self,digest) ;
		JobData&       jd          = *self                        ;
		EndDigest      end_digest  = end_analyze(/*inout*/digest) ;
		::vector<bool> must_wakeup ;
		//
		for( Req req : end_digest.running_reqs ) {
			ReqInfo& ri = jd.req_info(req) ;
			trace("req_before",end_digest.target_reason,jd.status,ri) ;
			req->missing_audits.erase(self) ;                           // old missing audit is obsolete as soon as we have rerun the job
			//                     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			JobReason job_reason = jd.make( ri , MakeAction::End , end_digest.target_reason , Yes/*speculate*/ , false/*wakeup_watchers*/ ) ; // we call wakeup_watchers ourselves once reports ...
			//                     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^   // ... are done to avoid anti-intuitive report order
			bool     done        = ri.done()                         ;
			bool     full_report = done || !end_digest.has_new_deps  ; // if not done, does a full report anyway if not due to new deps
			bool     job_err     = job_reason.tag>=JobReasonTag::Err ;
			::string job_msg     ;
			if (full_report) {
				/**/         job_msg << end_digest.msg_stderr.msg <<set_nl ;
				if (job_err) job_msg << reason_str(job_reason)<<'\n'       ;
				/**/         job_msg << end_digest.severe_msg              ;
			} else if (req->options.flags[ReqFlag::Verbose]) {
				/**/         job_msg << reason_str(job_reason)<<'\n'       ;
			}
			//
			bool      maybe_done = !ri.running() && jd.status>Status::Garbage && !end_digest.has_unstable_deps ;
			::string  pfx        = !done && maybe_done ? "may_" : ""                                        ;
			JobReport jr = audit_end(
				ri
			,	true/*with_stats*/
			,	pfx
			,	MsgStderr{ .msg=job_msg , .stderr=end_digest.msg_stderr.stderr }
			,	digest.max_stderr_len
			,	digest.exec_time
			,	is_retry(job_reason.tag)
			) ;
			end_digest.can_upload &= maybe_done ;                      // if job is not done, cache entry will be overwritten (with dir_cache at least) when actually rerun
			must_wakeup.push_back(done) ;
			if (!done) req->missing_audits[self] = { .report=jr , .has_stderr=digest.has_msg_stderr , .msg=end_digest.msg_stderr.msg } ; // stderr may be empty if digest.has_mg_stderr, no harm
			trace("req_after",ri,job_reason,STR(done)) ;
		}
		if (+digest.upload_key) {
			Cache* cache = Cache::s_tab[digest.cache_idx] ;
			SWEAR( cache , digest.cache_idx ) ;                                                                                          // cannot commit/dismiss without cache
			try {
				if (end_digest.can_upload) cache->commit ( digest.upload_key , self->unique_name() , job_info() ) ;
				else                       cache->dismiss( digest.upload_key                                    ) ;                      // free up temporary storage copied in job_exec
			} catch (::string const& e) {
				const char* action = end_digest.can_upload ? "upload" : "dismiss" ;
				trace("cache_throw",action,e) ;
				for( Req req : end_digest.running_reqs ) {
					req->audit_job   ( Color::Warning , cat("bad_cache_",action) , self , true/*at_end*/ ) ;
					req->audit_stderr(                                             self , {.msg=e}       ) ;
				}
			}
		}
		for( ReqIdx i : iota(end_digest.running_reqs.size()) ) {
			Req      req = end_digest.running_reqs[i] ;
			ReqInfo& ri  = jd.req_info(req) ;
			trace("wakeup_watchers",ri) ;
			if (must_wakeup[i]) ri.wakeup_watchers() ; // wakeup only after all messages are reported to user and cache->commit may generate user messages
			req.chk_end() ;
		}
		trace("done",self) ;
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
		else if ( jd.status==Status::RunLoop                 ) { res = JR::RunLoop    ; color = Color::Err     ;                               }
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
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		req->audit_job   ( color , pfx+step , self , true/*at_end*/ , exec_time                                              ) ;
		req->audit_stderr(                    self , { msg_stderr.msg , with_stderr?msg_stderr.stderr:""s } , max_stderr_len ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		ri.reported = true  ;
		//
		if ( speculate && done ) jr = JR::Speculative ;
		if ( with_stats        ) req->stats.add(jr,exec_time) ;
		//
		return res ;
	}

}
