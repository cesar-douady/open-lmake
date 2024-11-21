// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // must be first to include Python.h first

#include "rpc_job.hh"

using namespace Disk ;

namespace Engine {

	//
	// jobs thread
	//

	vmap<Node,FileAction> JobData::pre_actions( Rule::SimpleMatch const& match , bool mark_target_dirs ) const { // thread-safe
		Trace trace("pre_actions",idx(),STR(mark_target_dirs)) ;
		::uset<Node>                  to_mkdirs        = match.target_dirs() ;
		::uset<Node>                  to_mkdir_uphills ;
		::uset<Node>                  locked_dirs      ;
		::umap<Node,NodeIdx/*depth*/> to_rmdirs        ;
		::vmap<Node,FileAction>       actions          ;
		for( Node d : to_mkdirs )
			for( Node hd=d->dir() ; +hd ; hd = hd->dir() )
				if (!to_mkdir_uphills.insert(hd).second) break ;
		for( auto const& [_,d] : match.deps() )                                           // no need to mkdir a target dir if it is also a static dep dir (which necessarily already exists)
			for( Node hd=Node(d.txt)->dir() ; +hd ; hd = hd->dir() )
				if (!locked_dirs.insert(hd).second) break ;                               // if dir contains a dep, it cannot be rmdir'ed
		//
		// remove old targets
		for( Target t : targets ) {
			FileActionTag fat = {}/*garbage*/ ;
			//
			if      ( t->crc==Crc::None           ) fat = FileActionTag::None           ; // nothing to wash
			else if ( t->is_src_anti()            ) fat = FileActionTag::Src            ; // dont touch sources, not even integrity check
			else if ( t->polluted>=Polluted::Dirty) fat = FileActionTag::UnlinkPolluted ; // wash pollution
			else if (!t.tflags[Tflag::Incremental]) fat = FileActionTag::Unlink         ;
			else if ( t.tflags[Tflag::NoUniquify ]) fat = FileActionTag::NoUniquify     ;
			else                                    fat = FileActionTag::Uniquify       ;
			FileAction fa { fat , t->crc , t->date().sig } ;
			//
			trace("wash_target",t,fa) ;
			switch (fat) {
				case FileActionTag::Src        : if ( +t->dir() && t->crc!=Crc::None ) locked_dirs.insert(t->dir()) ;                              break ; // nothing to do, not even integrity check
				case FileActionTag::Uniquify   : if ( +t->dir()                      ) locked_dirs.insert(t->dir()) ; actions.emplace_back(t,fa) ; break ;
				case FileActionTag::NoUniquify : if ( +t->dir() && t->crc!=Crc::None ) locked_dirs.insert(t->dir()) ; actions.emplace_back(t,fa) ; break ;
				case FileActionTag::Unlink     :
					if ( !t->has_actual_job(idx()) && t->has_actual_job() && !t.tflags[Tflag::NoWarning] ) fa.tag = FileActionTag::UnlinkWarning ;
				[[fallthrough]] ;
				case FileActionTag::UnlinkPolluted :
				case FileActionTag::None           :
					actions.emplace_back(t,fa) ;
					if ( Node td=t->dir() ; +td ) {
						Lock    lock  { _s_target_dirs_mutex } ;
						NodeIdx depth = 0 ;
						for( Node hd=td ; +hd ; (hd=hd->dir()),depth++ )
							if (_s_target_dirs.contains(hd)) goto NextTarget ; // everything under a protected dir is protected, dont even start walking from td
						for( Node hd=td ; +hd ; hd = hd->dir() ) {
							if (_s_hier_target_dirs.contains(hd)) break ;      // dir is protected
							if (locked_dirs        .contains(hd)) break ;      // dir contains a file, no hope to remove it
							if (to_mkdirs          .contains(hd)) break ;      // dir must exist, it is silly to spend time to rmdir it, then again to mkdir it
							if (to_mkdir_uphills   .contains(hd)) break ;      // .
							//
							if (!to_rmdirs.emplace(td,depth).second) break ;   // if it is already in to_rmdirs, so is all pertinent dirs uphill
							depth-- ;
						}
					}
				break ;
			DF}
		NextTarget : ;
		}
		// make target dirs
		for( Node d : to_mkdirs ) {
			if (locked_dirs     .contains(d)) continue ;                       // dir contains a file         => it already exists
			if (to_mkdir_uphills.contains(d)) continue ;                       // dir is a dir of another dir => it will be automatically created
			actions.emplace_back( d , FileActionTag::Mkdir ) ;                 // note that protected dirs (in _s_target_dirs and _s_hier_target_dirs) may not be created yet, so mkdir them to be sure
		}
		// rm enclosing dirs of unlinked targets
		::vmap<Node,NodeIdx/*depth*/> to_rmdir_vec ; for( auto [k,v] : to_rmdirs ) to_rmdir_vec.emplace_back(k,v) ;
		::sort( to_rmdir_vec , [&]( ::pair<Node,NodeIdx/*depth*/> const& a , ::pair<Node,NodeIdx/*depth*/> const& b ) { return a.second>b.second ; } ) ; // sort deeper first, to rmdir after children
		for( auto [d,_] : to_rmdir_vec ) actions.emplace_back(d,FileActionTag::Rmdir) ;
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
		::uset<Node> dirs        = simple_match().target_dirs() ;
		::uset<Node> dir_uphills ;
		for( Node d : dirs )
			for( Node hd=d->dir() ; +hd ; hd = hd->dir() )
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

	::string& operator+=( ::string& os , JobTgts jts ) {
		return os<<jts.view() ;
	}

	//
	// JobReqInfo
	//

	::string& operator+=( ::string& os , JobReqInfo::State const& ris ) {
		const char* sep = "" ;
		/**/                                            os <<'('                                                  ;
		if (+ris.reason                             ) { os <<            ris.reason                               ; sep = "," ; }
		if ( +ris.stamped_err   || +ris.proto_err   ) { os <<sep<<"E:"<< ris.proto_err  <<"->"<<ris.stamped_err   ; sep = "," ; }
		if ( +ris.stamped_modif || +ris.proto_modif )   os <<sep<<"M:"<< ris.proto_modif<<"->"<<ris.stamped_modif ;
		return                                          os <<')'                                                  ;
	}

	::string& operator+=( ::string& os , JobReqInfo const& ri ) {
		/**/                  os << "JRI(" << ri.req                  ;
		/**/                  os <<','  << (ri.full?"full":"makable") ;
		if (ri.speculate!=No) os <<",S:"<< ri.speculate               ;
		if (ri.modified     ) os <<",modified"                        ;
		/**/                  os <<','  << ri.step()                  ;
		/**/                  os <<'@'  << ri.iter                    ;
		/**/                  os <<':'  << ri.state                   ;
		if (ri.n_wait       ) os <<",W:"<< ri.n_wait                  ;
		if (+ri.reason      ) os <<','  << ri.reason                  ;
		return                os <<')'                                ;
	}

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

	DequeThread<::pair<Job,JobInfo>,true/*Flush*/,true/*QueueAccess*/> Job::_s_record_thread ;

	::string& operator+=( ::string& os , Job j ) {
		/**/    os << "J(" ;
		if (+j) os << +j   ;
		return  os << ')'  ;
	}
	::string& operator+=( ::string& os , JobTgt jt ) {
		if (!jt) return           os << "(J())"        ;
		/**/                      os << "(" << Job(jt) ;
		if (jt.is_static_phony()) os << ",static"      ;
		else                      os << ",star"        ;
		return                    os << ')'            ;
	}
	::string& operator+=( ::string& os , JobExec const& je ) {
		if (!je) return os << "JE()" ;
		//
		/**/                     os <<'('<< Job(je)                     ;
		if (je.host!=NoSockAddr) os <<','<< SockFd::s_addr_str(je.host) ;
		if (je.start_date==je.end_date) {
			os <<','<< je.start_date ;
		} else {
			if (+je.start_date) os <<",S:"<< je.start_date ;
			if (+je.end_date  ) os <<",E:"<< je.end_date   ;
		}
		return os <<')' ;
	}

	Job::Job( Rule::SimpleMatch&& match , Req req , DepDepth lvl ) {
		Trace trace("Job",match,req,lvl) ;
		if (!match) { trace("no_match") ; return ; }
		Rule              rule      = match.rule ; SWEAR( rule->special<=Special::HasJobs , rule->special ) ;
		::vmap_s<DepSpec> dep_names ;
		try {
			dep_names = rule->deps_attrs.eval(match) ;
		} catch (::pair_ss const& msg_err) {
			trace("no_dep_subst") ;
			if (+req) {
				req->audit_job( Color::Note , "no_deps" , rule->name , match.name() ) ;
				req->audit_stderr( self , ensure_nl(rule->deps_attrs.s_exc_msg(false/*using_static*/))+msg_err.first , msg_err.second , -1 , 1 ) ;
			}
			return ;
		}
		::vector<Dep> deps ; deps.reserve(dep_names.size()) ;
		::umap<Node,VarIdx> dis  ;
		for( auto const& [_,dn] : dep_names ) {
			Node     d { dn.txt }                                                       ;
			Accesses a = dn.extra_dflags[ExtraDflag::Ignore] ? Accesses() : ~Accesses() ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			d->set_buildable_throw(req,lvl) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (d->buildable<=Buildable::No) {
				trace("no_dep",d) ;
				g_kpi.n_aborted_job_creation++ ;
				return ;
			}
			if ( auto [it,ok] = dis.emplace(d,deps.size()) ; ok )   deps.emplace_back( d , a , dn.dflags , true/*parallel*/ ) ;
			else                                                  { deps[it->second].dflags |= dn.dflags ; deps[it->second].accesses &= a ; } // uniquify deps by combining accesses and flags
		}
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		//          args for store         args for JobData
		self = Job( match.full_name(),Dflt , match,deps   ) ; // initially, static deps are deemed read, then actual accesses will be considered
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		trace("found",self) ;
	}

	::string Job::ancillary_file(AncillaryTag tag) const {
		switch (tag) {
			case AncillaryTag::Backend : return PrivateAdminDirS           +"backend/"s+(+self) ;
			case AncillaryTag::Data    : return g_config->local_admin_dir_s+"job_data/"+(+self) ;
			case AncillaryTag::Dbg     : return AdminDirS                  +"debug/"s  +(+self) ;
			case AncillaryTag::KeepTmp : return AdminDirS                  +"tmp/"s    +(+self) ;
		DF}
	}

	JobInfo Job::job_info( bool need_start , bool need_end ) const {                 // read job info from ancillary file, taking care of queued events
		Lock    lock        { _s_record_thread } ;
		JobInfo res         ;
		bool    found_start = false              ;
		bool    found_end   = false              ;
		SWEAR( need_start || need_end ) ;                                            // else, this is useless
		auto do_entry = [&](::pair<Job,JobInfo> const& jji)->void {
			if (jji.first!=self) return ;
			JobInfo const& ji = jji.second ;
			if (+ji.start) {
				/**/              found_start = true     ;
				if (need_start)   res.start   = ji.start ;
				if (found_end ) { res.end     = {}       ; found_end = false ; }     // start event replace file
			}
			if (+ji.end) {                                                           // end event append to file
				/**/          found_end = true   ;
				if (need_end) res.end   = ji.end ;
			}
		} ;
		/**/                                      do_entry(_s_record_thread.cur()) ; // dont forget entry being processed (handle first as this is this the oldest entry)
		for( auto const& jji : _s_record_thread ) do_entry(jji                   ) ; // linear searching is not fast, but this is rather exceptional and this queue is small (actually mostly empty)
		Trace trace("job_info",STR(need_start),STR(need_end),STR(found_start),STR(found_end)) ;
		if (!found_start) try {                                                                                                                // ignore errors, we get what exists
			::string      jaf = AcFd(ancillary_file()).read() ;
			::string_view jas { jaf }                         ;
			trace("ancillary_file",jaf) ;
			/**/                          try { deserialize( jas , res.start ) ; trace("start_from_file") ; } catch (...) { res.start = {} ; } // even if we do not need start, we need to skip it
			if ( need_end && !found_end ) try { deserialize( jas , res.end   ) ; trace("end_from_file"  ) ; } catch (...) { res.end   = {} ; }
		} catch (...) {}
		return res ;
	}

	void Job::record(JobInfo const& ji) const {
		Trace trace("record",self,STR(+ji.start),STR(+ji.end)) ;
		::string jaf ;
		if (+ji.start) serialize( jaf , ji.start ) ;
		if (+ji.end  ) serialize( jaf , ji.end   ) ;
		AcFd( ancillary_file() , +ji.start?Fd::Write:Fd::Append ).write(jaf) ; // start event write to file, end event append to it
	}

	//
	// JobExec
	//

	void JobExec::give_up( Req req , bool report ) {
		Trace trace("give_up",self,req) ;
		if (+req) {
			ReqInfo& ri = (self)->req_info(req) ;
			ri.step(Step::End,self) ;                                            // ensure no confusion with previous run
			(self)->make( ri , MakeAction::GiveUp ) ;
			if (report)
				for( Req r : (self)->running_reqs(false/*with_zombies*/) ) {
					SWEAR(r!=req) ;
					req->audit_job(Color::Note,"continue",self,true/*at_end*/) ; // generate a continue line if some other req is still active
					break ;
				}
			req.chk_end() ;
		} else {
			for( Req r : self->running_reqs(true/*with_zombies*/) ) give_up(r,false/*report*/)                                   ;
			for( Req r : self->running_reqs(true/*with_zombies*/) ) FAIL(self->name(),"is still running for",r,"after kill all") ;
		}
	}

	// answer to job execution requests
	JobMngtRpcReply JobExec::job_analysis( JobMngtProc proc , ::vector<Dep> const& deps ) const {
		::vector<Req> reqs = self->running_reqs(false/*with_zombies*/) ;
		Trace trace("job_analysis",proc,deps.size()) ;
		//
		switch (proc) {
			case JobMngtProc::DepVerbose : {
				::vector<pair<Bool3/*ok*/,Crc>> res ;
				if (!reqs) return {proc,{}/*seq_id*/,{}/*fd*/,res} ; // if job is not running, it is too late, seq_id will be filled in later, /!\ {} would be interpreted as Bool3 !
				res.reserve(deps.size()) ;
				for( Dep const& dep : deps ) {
					Node(dep)->full_refresh(false/*report_no_file*/,{},dep->name()) ;
					Bool3 dep_ok = Yes ;
					for( Req req : reqs ) {
						NodeReqInfo& dri = dep->req_info(req) ;
						Node(dep)->make(dri,NodeMakeAction::Query) ;
						if      (!dri.done(NodeGoal::Status)  ) { trace("waiting",dep,req) ; dep_ok = Maybe ;         }
						else if (dep->ok(dri,dep.accesses)==No) { trace("bad"    ,dep,req) ; dep_ok = No    ; break ; }
					}
					trace("dep_info",dep,dep_ok) ;
					res.emplace_back(dep_ok,dep->crc) ;
				}
				return {proc,{}/*seq_id*/,{}/*fd*/,res} ;
			}
			case JobMngtProc::ChkDeps :
				if (!reqs) return {proc,{}/*seq_id*/,{}/*fd*/,Maybe} ;                         // if job is not running, it is too late, seq_id will be filled in later
				for( Dep const& dep : deps ) {
					Node(dep)->full_refresh(false/*report_no_file*/,{},dep->name()) ;
					Bool3 dep_ok = Yes ;
					for( Req req : reqs ) {
						NodeReqInfo& dri  = dep->req_info(req)                               ;
						NodeGoal     goal = +dep.accesses ? NodeGoal::Dsk : NodeGoal::Status ; // if no access, we do not care about file on disk
						Node(dep)->make(dri,NodeMakeAction::Query) ;
						if      (!dri.done(goal)              ) { trace("waiting",dep,req) ; dep_ok = Maybe ;         }
						else if (dep->ok(dri,dep.accesses)==No) { trace("bad"    ,dep,req) ; dep_ok = No    ; break ; }
					}
					if (dep_ok!=Yes) return {proc,{}/*seq_id*/,{}/*fd*/,dep_ok} ;              // seq_id will be filled in later
					trace("ok",dep) ;
				}
				trace("done") ;
				return {proc,{}/*seq_id*/,{}/*fd*/,Yes} ;                                      // seq_id will be filled in later
		DF}
	}

	void JobExec::live_out( ReqInfo& ri , ::string const& txt ) const {
		if (!txt        ) return ;
		if (!ri.live_out) return ;
		Req r = ri.req ;
		if ( !report_start(ri) && r->last_info!=self ) {
			Pdate now = New ;
			r->audit_job( Color::HiddenNote , "continue" , JobExec(self,host,start_date,now) , true/*at_end*/ , now-start_date ) ; // identify job (with a continue message if no start message)
		}
		r->last_info = self ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		r->audit_info(Color::None,txt,0) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

	void JobExec::live_out(::string const& txt) const {
		Trace trace("report_start",self) ;
		for( Req req : self->running_reqs(false/*with_zombies*/) ) live_out(self->req_info(req),txt) ;
	}

	bool/*reported*/ JobExec::report_start( ReqInfo& ri , ::vmap<Node,FileActionTag> const& report_unlnks , ::string const& stderr , ::string const& msg ) const {
		if ( ri.start_reported ) return false ;
		ri.req->audit_job( +stderr?Color::Warning:Color::HiddenNote , "start" , self ) ;
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
		if (+stderr) ri.req->audit_stderr( self , msg , stderr , -1 , 1 ) ;
		ri.start_reported = true ;
		return true ;
	}
	void JobExec::report_start() const {
		Trace trace("report_start",self) ;
		for( Req req : self->running_reqs(false/*with_zombies*/) ) report_start(self->req_info(req)) ;
	}

	void JobExec::started( JobInfoStart&& jis , bool report , ::vmap<Node,FileActionTag> const& report_unlnks , ::string const& stderr , ::string const& msg ) {
		Trace trace("started",self) ;
		SWEAR( !self->rule()->is_special() , self->rule()->special ) ;
		_s_record_thread.emplace(self,::move(jis)) ;
		report |= +report_unlnks || +stderr ;
		for( Req req : self->running_reqs() ) {
			ReqInfo& ri = self->req_info(req) ;
			ri.start_reported = false ;
			if (report) report_start(ri,report_unlnks,stderr,msg) ;
			ri.step(JobStep::Exec,self) ;
		}
	}

	void JobExec::end( JobRpcReq&& jrr , bool sav_jrr , ::vmap_ss const& rsrcs ) {
		JobData&          data             = *self                                                ;
		JobDigest&        digest           = jrr.digest                                           ;
		Status            status           = digest.status                                        ;          // status will be modified, need to make a copy
		Bool3             ok               = is_ok  (status)                                      ;
		bool              lost             = is_lost(status)                                      ;
		JobReason         target_reason    = JobReasonTag::None                                   ;
		bool              unstable_dep     = false                                                ;
		bool              modified         = false                                                ;
		bool              fresh_deps       = status==Status::EarlyChkDeps || status>Status::Async ;          // if job did not go through, old deps are better than new ones
		bool              seen_dep_date    = false                                                ;
		Rule              rule             = data.rule()                                          ;
		::vector<Req>     running_reqs_    = data.running_reqs(true/*with_zombies*/)              ;
		::string          local_msg        ;                                                                 // to be reported if job was otherwise ok
		::string          severe_msg       ;                                                                 // to be reported always
		CacheNoneAttrs    cache_none_attrs ;
		EndCmdAttrs       end_cmd_attrs    ;
		Rule::SimpleMatch match            ;
		//
		SWEAR(status!=Status::New) ;                                                                         // we just executed the job, it can be neither new, frozen or special
		SWEAR(!frozen()          ) ;                                                                         // .
		SWEAR(!rule->is_special()) ;                                                                         // .
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		try {
			cache_none_attrs = rule->cache_none_attrs.eval( self , match , &::ref(::vmap_s<DepDigest>()) ) ; // we cant record deps here, but we dont care, no impact on target
		} catch (::pair_ss const& msg_err) {
			cache_none_attrs = rule->cache_none_attrs.spec ;
			for( Req req : running_reqs_ ) {
				req->audit_job(Color::Note,"dynamic",self,true/*at_end*/) ;
				::string req_msg = rule->cache_none_attrs.s_exc_msg(true/*using_static*/) ;
				req_msg <<set_nl<< msg_err.first ;
				req->audit_stderr( self , req_msg , msg_err.second , -1 , 1 ) ;
			}
		}
		try                                  { end_cmd_attrs = rule->end_cmd_attrs.eval(self,match)  ;       }
		catch (::pair_ss const& /*msg,err*/) { severe_msg << "cannot compute " << EndCmdAttrs::Msg << '\n' ; }
		//
		data.status = Status::New ;                                                                          // ensure we cannot appear up to date while working on data
		fence() ;
		//
		Trace trace("end",self,status) ;
		//
		// handle targets
		//
		for( Node t : data.targets ) if (t->polluted==Polluted::Busy) t->polluted = Polluted::Clean ;        // old targets are no more busy
		//
		if ( !lost && status>Status::Early ) { // if early, we have not touched the targets, not even washed them, if lost, old targets are better than new ones
			//
			for( Node t : data.targets ) if (t->has_actual_job(self)) t->actual_job() = {} ;   // ensure targets we no more generate do not keep pointing to us
			//
			::vector<Target> targets ; targets.reserve(digest.targets.size()) ;
			for( auto const& [tn,td] : digest.targets ) {
				Tflags tflags          = td.tflags              ;
				Node   target          { tn }                   ;
				bool   static_phony    = ::static_phony(tflags) ;
				Crc    crc             = td.crc                 ;
				bool   target_modified = false                  ;
				//
				target->set_buildable() ;
				if      ( td.pre_exist             )   target->polluted = Polluted::PreExist ; // if target pre_existed while not incremental, it is polluted even if generated by official job
				else if ( tflags[Tflag::Target]    )   target->polluted = Polluted::Clean    ;
				else if ( +crc && crc!=target->crc ) { target->polluted = Polluted::Job      ; target->polluting_job = self ; } // we pollute the official job
				//
				if (target->polluted>=Polluted::Dirty) trace("polluted",target->polluted,target->polluting_job) ;
				//
				if (+crc) {
					// file dates are very fuzzy and unreliable, at least, filter out targets we generated ourselves
					if ( +start_date && target->date().date>start_date ) {                                                      // if no start_date.p, job did not execute, it cannot generate a clash
						// /!\ This may be very annoying !
						// A job was running in parallel with us and there was a clash on this target.
						// There are 2 problems : for us and for them.
						// For us, it's ok, we will rerun.
						// But for them, they are already done,  possibly some dependent jobs are done, possibly even Req's are already done and we may have reported ok to the user,
						// and all that is wron.
						// This is too complex and too rare to detect (and ideally handle).
						// Putting target in clash_nodes will generate a frightening message to user asking to relaunch all commands that were running in parallel.
						if ( crc.valid() && td.tflags[Tflag::Target] ) {                                                        // official targets should have a valid crc, but if not, we dont care
							trace("clash",start_date,target->date().date) ;
							target_reason |= {JobReasonTag::ClashTarget,+target} ;                                              // crc is actually unreliable, rerun
						}
						if ( target->crc.valid() && target->has_actual_job() && target->actual_tflags()[Tflag::Target] && !target->is_src_anti() ) { // existing crc was believed to be reliable ...
							trace("critical_clash",start_date,target->date().date) ;                                                                 // ... but actually was not (if no execution, ...
							for( Req r : target->reqs() ) {                                                                                          // ... there is no problem)
								r->clash_nodes.emplace(target,r->clash_nodes.size()) ;
								target->req_info(r).reset() ;                          // best effort to trigger re-analysis but this cannot be guaranteed (fooled req may be gone)
							}
						}
					}
					//
					if (target->is_src_anti()) {                                       // source may have been modified
						if (!crc.valid()) crc = Crc(tn) ;                              // force crc computation if updating a source
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
								case Buildable::DynAnti   :
								case Buildable::Anti      : msg << " anti-file"      ; break ;
								case Buildable::SrcDir    : msg << " source dir"     ; break ;
								case Buildable::SubSrcDir : msg << " source sub-dir" ; break ;
								case Buildable::DynSrc    :
								case Buildable::Src       : msg << " source"         ; break ;
								case Buildable::SubSrc    : msg << " sub-source"     ; break ;
							DF}
							severe_msg << msg <<" : "<< mk_file(tn,No /*exists*/) <<'\n' ;
						}
						if (ok==Yes) status = Status::Err ;
					SourceOk : ;
					}
					//
					target_modified  = target->refresh( crc , { td.sig , td.extra_tflags[ExtraTflag::Wash]?start_date:end_date } ) ;
					modified        |= target_modified && tflags[Tflag::Target]                                                    ;
				}
				if ( crc==Crc::None && !static_phony ) {
					target->actual_job   () = {} ;
					target->actual_tflags() = {} ;
					trace("unlink",target,td,STR(target_modified)) ;
				} else if ( +crc || tflags[Tflag::Target] ) {                          // if not actually writing, dont pollute targets of other jobs
					target->actual_job   () = self      ;
					target->actual_tflags() = td.tflags ;
					//
					targets.emplace_back( target , tflags ) ;
					if (td.pre_exist) target_reason |= {JobReasonTag::PrevTarget,+target} ;
					trace("target",target,td,STR(target_modified)) ;
				} else {
					trace("not_target",target,td) ;
				}
			}
			::sort(targets) ;                                                          // ease search in targets
			//vvvvvvvvvvvvvvvvvvvvvvvvvv
			data.targets.assign(targets) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
		//
		// handle deps
		//
		bool has_new_deps = false ;
		if (fresh_deps) {
			::uset<Node>  old_deps ;
			::vector<Dep> deps     ; deps.reserve(digest.deps.size()) ;
			for( Dep const& d : data.deps )
				if (d->is_plain())
					for( Node dd=d ; +dd ; dd=dd->dir() )
						if (!old_deps.insert(dd).second) break ;                                                 // record old deps and all uphill dirs as these are implicit deps
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
				if (!dep.is_crc) {
					dep->full_refresh(true/*report_no_file*/,running_reqs_,dn) ;
					dep.acquire_crc() ;
					if (dep.is_crc) { dd.crc_sig(dep) ; seen_dep_date = true ; }                                 // if a dep has become a crc, update digest so that ancillary file reflects it
				} else if (dep.never_match()) {
					dep->set_buildable() ;
					if (dep->is_src_anti()) dep->refresh_src_anti(true/*report_no_file*/,running_reqs_,dn) ;     // the goal is to detect overwritten
					unstable_dep = true ;
				}
				deps.push_back(dep) ;
				trace("dep",dep) ;
			}
			//vvvvvvvvvvvvvvvvvvvv
			data.deps.assign(deps) ;
			//^^^^^^^^^^^^^^^^^^^^
		}
		//
		// wrap up
		//
		if ( ok==Yes && +digest.stderr && !end_cmd_attrs.allow_stderr ) { local_msg+="non-empty stderr\n" ; status = Status::Err ; }
		EndNoneAttrs end_none_attrs ;
		::string     stderr         ;
		try {
			end_none_attrs = rule->end_none_attrs.eval( self , match , rsrcs , &::ref(::vmap_s<DepDigest>()) ) ; // we cant record deps here, but we dont care, no impact on target
			stderr = digest.stderr ;                                                                             // must copy because digest is move'd when jrr is move'd
		} catch (::pair_ss const& msg_err) {
			end_none_attrs = rule->end_none_attrs.spec ;
			severe_msg << rule->end_none_attrs.s_exc_msg(true/*using_static*/) << '\n' << msg_err.first ;
			stderr = ensure_nl(msg_err.second) + digest.stderr ;
			stderr <<set_nl<< digest.stderr ;
		}
		//
		data.set_exec_ok() ;                                                               // effect of old cmd has gone away with job execution
		fence() ;                                                                          // only update status once every other info is set in case of crash and avoid transforming garbage into Err
		if      ( lost           && status<=Status::Early   ) status = Status::EarlyLost ;
		else if ( lost                                      ) status = Status::LateLost  ;
		else if ( +target_reason && status> Status::Garbage ) status = Status::BadTarget ;
		//vvvvvvvvvvvvvvvvvv
		data.status = status ;
		//^^^^^^^^^^^^^^^^^^
		// job_data file must be updated before make is called as job could be remade immediately (if cached), also info may be fetched if issue becomes known
		bool     upload = sav_jrr && data.run_status==RunStatus::Ok && ok==Yes  && +cache_none_attrs.key ;
		::string msg    ;
		trace("wrap_up",STR(sav_jrr),ok,cache_none_attrs.key,data.run_status,STR(upload)) ;
		if (sav_jrr) {
			msg = jrr.msg ;
			jrr.msg <<set_nl<< local_msg << severe_msg ;
			if (upload) _s_record_thread.emplace(self,JobInfoEnd{::copy(jrr)}) ;           // leave jrr intact so upload can be done later on
			else        _s_record_thread.emplace(self,JobInfoEnd{::move(jrr)}) ;
		} else {
			SWEAR( !seen_dep_date && !local_msg && !severe_msg ) ;           // results from cache are always ok and all deps are crc, ensure there is nothing to updatea
			msg = ::move(jrr.msg) ;
		}
		//
		if (ok==Yes) {                                                       // only update rule based exec time estimate when job is ok as jobs in error may be much faster and are not representative
			SWEAR(+digest.stats.total) ;
			data.record_stats( digest.stats.total , cost , tokens1 ) ;
		}
		MakeAction end_action = fresh_deps||ok==Maybe ? MakeAction::End : MakeAction::GiveUp ;
		bool       one_done   = false                                                        ;
		for( Req req : data.running_reqs(true/*with_zombies*/,true/*hit_ok*/)) {
			ReqInfo& ri = data.req_info(req) ;
			if (ri.running()) ri.step(Step::End,self) ;                      // ensure no confusion with previous run
			/**/              ri.modified = modified ;
		}
		for( Req req : running_reqs_ ) {
			ReqInfo& ri = data.req_info(req) ;
			trace("req_before",target_reason,status,ri,STR(modified)) ;
			req->missing_audits.erase(self) ;                                // old missing audit is obsolete as soon as we have rerun the job
			// we call wakeup_watchers ourselves once reports are done to avoid anti-intuitive report order
			//                         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			JobReason job_err_reason = data.make( ri , end_action , target_reason , Yes/*speculate*/ , false/*wakeup_watchers*/ ) ;
			//                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			bool     full_report = ri.done() || !has_new_deps            ;   // if not done, does a full report anyway if this is not due to new deps
			bool     job_err     = job_err_reason.tag>=JobReasonTag::Err ;
			::string job_msg     ;
			if (full_report) {
				/**/         job_msg << msg                       <<set_nl ;
				if (job_err) job_msg << reason_str(job_err_reason)<<'\n'   ;
				else         job_msg << local_msg                 <<set_nl ; // report local_msg if no better message
				/**/         job_msg << severe_msg                         ;
			} else if (req->options.flags[ReqFlag::Verbose]) {
				job_msg << reason_str(job_err_reason)<<'\n' ;
			}
			//
			Delay    exec_time = digest.stats.total ;
			::string pfx       = !ri.done() && status>Status::Garbage && !unstable_dep ? "may_" : "" ;
			// dont report user stderr if analysis made it meaningless
			JobReport jr = audit_end( ri , true/*with_stats*/ , pfx , job_msg , !job_err?stderr:""s , end_none_attrs.max_stderr_len , exec_time ) ;
			if (ri.done()) {
				trace("wakeup_watchers",ri) ;
				ri.wakeup_watchers() ;
				one_done = true ;
			} else {
				req->missing_audits[self] = { jr , msg } ;
			}
			trace("req_after",ri) ;
			req.chk_end() ;
		}
		// as soon as job is done for a req, it is meaningful and justifies to be cached, in practice all reqs agree most of the time
		if ( upload && one_done ) {                                          // cache only successful results
			NfsGuard nfs_guard{g_config->reliable_dirs} ;
			Cache::s_tab.at(cache_none_attrs.key)->upload( self , digest , nfs_guard ) ;
		}
		trace("summary",self) ;
	}

	JobReport JobExec::audit_end( ReqInfo& ri , bool with_stats , ::string const& pfx , ::string const& msg , ::string const& stderr , size_t max_stderr_len , Delay exec_time ) const {
		using JR = JobReport ;
		//
		Req            req         = ri.req           ;
		JobData const& jd          = *self            ;
		Color          color       = {}/*garbage*/    ;
		JR             res         = {}/*garbage*/    ; // report if not Rerun
		const char*    step        = nullptr          ;
		bool           with_stderr = true             ;
		bool           speculate   = ri.speculate!=No ;
		bool           done        = ri.done()        ;
		//
		if      (jd.run_status!=RunStatus::Ok    ) { res = JR::Failed     ; color = Color::Err     ;                       step = snake_cstr(jd.run_status) ; }
		else if (jd.status==Status::Killed       ) { res = JR::Killed     ; color = Color::Note    ; with_stderr = false ;                                    }
		else if (is_lost (jd.status) && jd.err() ) { res = JR::LostErr    ; color = Color::Err     ;                       step = "lost_err"                ; }
		else if (is_lost (jd.status)             ) { res = JR::Lost       ; color = Color::Warning ; with_stderr = false ;                                    }
		else if (is_retry(jd.status)             ) { res = JR::Retry      ; color = Color::Warning ; with_stderr = false ;                                    }
		else if (req.zombie()                    ) { res = JR::Completed  ; color = Color::Note    ; with_stderr = false ;                                    }
		else if (jd.status==Status::SubmitLoop   ) { res = JR::SubmitLoop ; color = Color::Err     ;                                                          }
		else if (jd.err()                        ) { res = JR::Failed     ; color = Color::Err     ;                                                          }
		else if (ri.modified                     ) { res = JR::Done       ; color = Color::Ok      ;                                                          }
		else                                       { res = JR::Steady     ; color = Color::Ok      ;                                                          }
		//
		JR jr = res ;                                   // report to do now
		if (!done) {
			with_stderr = false ;
			if      (jd.status==Status::EarlyChkDeps) { jr = JR::Resubmit ; color = Color::Note ; step = nullptr ; }
			else if (res!=JR::Retry                 ) { jr = JR::Rerun    ; color = Color::Note ; step = nullptr ; }
		}
		//
		if (color==Color::Err) { if ( speculate              ) color = Color::SpeculateErr ; }
		else                   { if ( with_stderr && +stderr ) color = Color::Warning      ; }
		if (!step) step = snake_cstr(jr) ;
		Trace trace("audit_end",color,pfx,step,self,ri,STR(with_stats),STR(with_stderr),STR(done),STR(speculate),jr,STR(+msg),STR(+stderr)) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		req->audit_job(color,pfx+step,self,true/*at_end*/,exec_time) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		ri.reported = true ;
		req->audit_stderr( self , msg , with_stderr?stderr:""s , max_stderr_len , 1 ) ;
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

	void JobData::_reset_targets(Rule::SimpleMatch const& match) {
		Rule             r     = rule()                          ;
		::vector<Target> ts    ; ts.reserve(r->n_static_targets) ;
		::vector_s       sms   = match.static_matches() ;
		::uset_s         seens ;
		for( VarIdx ti : iota(r->n_static_targets) ) {
			if (!seens.insert(sms[ti]).second) continue ; // remove duplicates
			ts.emplace_back(sms[ti],r->tflags(ti)) ;
		}
		::sort(ts) ;                                      // ease search in targets
		targets.assign(ts) ;
	}

	void JobData::_do_set_pressure(ReqInfo& ri , CoarseDelay pressure ) const {
		Trace trace("set_pressure",idx(),ri,pressure) ;
		g_kpi.n_job_set_pressure++ ;
		//
		Req         req          = ri.req                  ;
		CoarseDelay dep_pressure = ri.pressure + exec_time ;
		switch (ri.step()) { //!                                                            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case JobStep::Dep    : for( DepsIter it{deps,ri.iter }; it!=deps.end() ; it++ ) (*it)->    set_pressure( (*it)->req_info(req) ,                  dep_pressure  ) ; break ;
			case JobStep::Queued :                                                          Backend::s_set_pressure( ri.backend , +idx() , +req , {.pressure=dep_pressure} ) ; break ;
		DN} //!                                                                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

	static JobReasonTag _mk_reason(Status s) {
		static constexpr JobReasonTag ReasonTab[/*Status*/] = {
			JobReasonTag::New                                                                                       // New
		,	JobReasonTag::ChkDeps                                                                                   // EarlyChkDeps
		,	JobReasonTag::None                                                                                      // EarlyErr
		,	JobReasonTag::Lost                                                                                      // EarlyLost
		,	JobReasonTag::None                                                                                      // EarlyLostErr
		,	JobReasonTag::Lost                                                                                      // LateLost
		,	JobReasonTag::None                                                                                      // LateLostErr
		,	JobReasonTag::Killed                                                                                    // Killed
		,	JobReasonTag::ChkDeps                                                                                   // ChkDeps
		,	JobReasonTag::PollutedTargets                                                                           // BadTarget
		,	JobReasonTag::Retry                                                                                     // Retry
		,	JobReasonTag::None                                                                                      // Ok
		,	JobReasonTag::None                                                                                      // SubmitLoop
		,	JobReasonTag::None                                                                                      // Err
		} ;
		static_assert(sizeof(ReasonTab)==N<Status>) ;                                                               // sanity check
		JobReasonTag res = ReasonTab[+s] ;
		/**/      SWEAR( res<JobReasonTag::HasNode , s , res ) ;
		if (+res) SWEAR( is_ok(s)==Maybe           , s , res ) ;                                                    // no reason to run a job if outcome is already known
		return res ;
	}
	JobReason JobData::make( ReqInfo& ri , MakeAction make_action , JobReason asked_reason , Bool3 speculate , bool wakeup_watchers ) {
		using Step = JobStep ;
		static constexpr Dep Sentinel { false/*parallel*/ } ;                                                       // used to clean up after all deps are processed
		//
		SWEAR( asked_reason.tag<JobReasonTag::Err,asked_reason) ;
		Rule        r              = rule()                                                        ;
		bool        query          = make_action==MakeAction::Query                                ;
		Step        prev_step      = make_action==MakeAction::End?Step::Exec:ri.step()             ;                // capture previous level before any update (compensate preparation in end)
		Req         req            = ri.req                                                        ;
		Special     special        = r->special                                                    ;
		bool        dep_live_out   = special==Special::Req && req->options.flags[ReqFlag::LiveOut] ;
		CoarseDelay dep_pressure   = ri.pressure + exec_time                                       ;
		bool        archive        = req->options.flags[ReqFlag::Archive]                          ;
		JobReason   pre_reason     ;                                                                                // reason to run job when deps are ready before deps analysis
		JobReason   report_reason  ;
		//
		auto reason = [&]()->JobReason {
			if (ri.force) return pre_reason | ri.reason       | ri.state.reason ;
			else          return pre_reason | ri.state.reason | ri.reason       ;
		} ;
		auto need_run = [&](ReqInfo::State const& s)->bool {
			SWEAR( pre_reason.tag<JobReasonTag::Err && ri.reason.tag<JobReasonTag::Err , pre_reason , ri.reason ) ;
			return ( +pre_reason || +s.reason || +ri.reason ) && s.reason.tag<JobReasonTag::Err ;                   // equivalent to +reason() && reason().tag<Err
		} ;
		Trace trace("Jmake",idx(),ri,make_action,asked_reason,speculate,STR(wakeup_watchers),prev_step) ;
	RestartFullAnalysis :
		switch (make_action) {
			case MakeAction::End :
				ri.force   = false ;                                                                           // cmd has been executed, it is not new any more
				ri.reason  = {}    ;                                                                           // reasons were to trigger the ending job, there are none now
				ri.reset(idx()) ;                                                                              // deps have changed
			[[fallthrough]] ;
			case MakeAction::Wakeup : ri.dec_wait() ;                                      break ;
			case MakeAction::GiveUp : ri.dec_wait() ; goto Done ;                          break ;
			case MakeAction::Query  :
			case MakeAction::Status : if (!ri.full) { ri.full = true ; ri.reset(idx()) ; } break ;
			case MakeAction::Makable :
				SWEAR(!asked_reason,asked_reason) ;
				ri.speculate = ri.speculate & speculate ;                                                      // cannot use &= with bit fields
				if ( !ri.waiting() && !ri.full && sure() ) {
					SWEAR( !ri.state.reason && !ri.reason , ri.state.reason , ri.reason ) ;                    // if we have a reason to run, we must not go to Done
					goto Done ;
				}
			break ;
		DF}
		if (+asked_reason) {
			if (ri.state.missing_dsk) { trace("reset",asked_reason) ; ri.reset(idx()) ; }
			ri.reason |= asked_reason ;
		}
		ri.speculate = ri.speculate & speculate ;                                                              // cannot use &= with bit fields
		if (ri.done()) {
			if ( !ri.reason && !ri.state.reason            )                              goto Wakeup ;
			if ( req.zombie()                              )                              goto Wakeup ;
			/**/                                                                          goto Run    ;
		} else {
			if ( ri.waiting()                              )                              goto Wait   ;        // we may have looped in which case stats update is meaningless and may fail()
			if ( req.zombie()                              )                              goto Done   ;
			if ( idx().frozen()                            )                              goto Run    ;        // ensure crc are updated, akin sources
			if ( special==Special::Infinite                )                              goto Run    ;        // special case : Infinite actually has no dep, just a list of node showing infinity
			if ( r->n_submits && ri.n_submits>r->n_submits ) { SWEAR(is_ok(status)==No) ; goto Done   ; }      // we do not analyze, ensure we have an error state
		}
		if (ri.step()==Step::None) {
			estimate_stats() ;                                                                                 // initial guestimate to accumulate waiting costs while resources are not fully known yet
			ri.step(Step::Dep,idx()) ;
			if (ri.full) {
				JobReasonTag jrt = {} ;
				if      ( r->force                                              ) jrt = JobReasonTag::Force  ;
				else if ( !cmd_ok()                                             ) jrt = JobReasonTag::Cmd    ;
				else if ( req->options.flags[ReqFlag::ForgetOldErrors] && err() ) jrt = JobReasonTag::OldErr ; // probably a transient error
				else if ( !rsrcs_ok()                                           ) jrt = JobReasonTag::Rsrcs  ; // probably a resource  error
				if (+jrt) {
					ri.reason              = jrt  ;
					ri.force               = true ;
					ri.state.proto_modif   = true ;                                                            // ensure we can copy proto_modif to stamped_modif anytime when pertinent
					ri.state.stamped_modif = true ;
				}
			}
		}
		g_kpi.n_job_make++ ;
		SWEAR(ri.step()==Step::Dep) ;
		{
		RestartAnalysis :                                                                             // restart analysis here when it is discovered we need deps to run the job
			bool stamped_seen_waiting = false ;
			bool proto_seen_waiting   = false ;
			bool critical_modif       = false ;
			bool critical_waiting     = false ;
			bool sure                 = true  ;
			//
			ri.speculative_wait = false ;                                                             // initially, we are not waiting at all
			//
			ReqInfo::State state = ri.state ;
			pre_reason = _mk_reason(status) ;
			for( DepsIter iter {deps,ri.iter} ;; iter++ ) {
				//
				bool       seen_all = iter==deps.end()            ;
				Dep const& dep      = seen_all ? Sentinel : *iter ;                                   // use empty dep as sentinel
				if (!dep.parallel) {
					state.stamped_err   = state.proto_err   ;                                         // proto become stamped upon sequential dep
					state.stamped_modif = state.proto_modif ;                                         // .
					if ( critical_modif && !seen_all ) {                                              // suppress deps following modified critical one, except static deps as no-access
						::vector<Dep> static_deps ;
						for( DepsIter it=iter ; it!=deps.end() ; it++ ) if (it->dflags[Dflag::Static]) {
							static_deps.push_back(*it) ;
							static_deps.back().accesses = {} ;
						}
						deps.replace_tail(iter,static_deps) ;
						seen_all = !static_deps ;
					}
					stamped_seen_waiting = proto_seen_waiting ;
					if ( query && (stamped_seen_waiting||state.stamped_modif||+state.stamped_err) ) { // no reason to analyze any further, we have the answer
						report_reason = reason() ;
						goto Return ;
					}
				}
				if (!proto_seen_waiting) {
					ri.iter  = iter.digest(deps) ;                                                    // fast path : info is recorded in ri, next time, restart analysis here
					ri.state = state             ;                                                    // .
				}
				if ( seen_all || (!dep.parallel&&critical_waiting) ) break ;
				NodeData &         dnd         = *Node(dep)                                   ;
				bool               dep_modif   = false                                        ;
				RunStatus          dep_err     = RunStatus::Ok                                ;
				bool               care        = +dep.accesses                                ;       // we care about this dep if we access it somehow
				bool               is_static   =  dep.dflags[Dflag::Static     ]              ;
				bool               is_critical =  dep.dflags[Dflag::Critical   ] && care      ;
				bool               sense_err   = !dep.dflags[Dflag::IgnoreError]              ;
				bool               required    =  dep.dflags[Dflag::Required   ] || is_static ;
				bool               modif       = state.stamped_modif || ri.force              ;
				bool               may_care    = care || (modif&&is_static)                   ;       // if previous modif, consider static deps as fully accessed, as initially
				NodeReqInfo const* cdri        = &dep->c_req_info(req)                        ;       // avoid allocating req_info as long as not necessary
				NodeReqInfo      * dri         = nullptr                                      ;       // .
				NodeGoal           dep_goal    =
					query                                             ? NodeGoal::Dsk
				:	ri.full && may_care && (need_run(state)||archive) ? NodeGoal::Dsk
				:	ri.full && (may_care||sense_err)                  ? NodeGoal::Status
				:	required                                          ? NodeGoal::Makable
				:	                                                    NodeGoal::None
				;
				if (!dep_goal) continue ;                                                             // this is not a dep (not static while asked for makable only)
			RestartDep :
				if (!cdri->waiting()) {
					ReqInfo::WaitInc sav_n_wait{ri} ;                                                 // appear waiting in case of recursion loop (loop will be caught because of no job on going)
					if (!dri        ) cdri = dri    = &dep->req_info(*cdri) ;                         // refresh cdri in case dri allocated a new one
					if (dep_live_out) dri->live_out = true                  ;                         // ask live output for last level if user asked it
					Bool3 speculate_dep =
						is_static                     ? ri.speculate                                  // static deps do not disappear
					:	stamped_seen_waiting || modif ?              Yes                              // this dep may disappear
					:	+state.stamped_err            ? ri.speculate|Maybe                            // this dep is not the origin of the error
					:	                                ri.speculate                                  // this dep will not disappear from us
					;
					if (special!=Special::Req) dnd.asking = idx() ;                                   // Req jobs are fugitive, dont record them
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					dnd.make( *dri , mk_action(dep_goal,query) , speculate_dep ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				}
				if ( is_static && dnd.buildable<Buildable::Yes ) sure = false ;                       // buildable (remember it is pessimistic) is better after make() (i.e. less pessimistic)
				if (cdri->waiting()) {
					if      ( is_static                                            ) ri.speculative_wait = false ; // we are non-speculatively waiting, even if after a speculative wait
					else if ( !stamped_seen_waiting && (+state.stamped_err||modif) ) ri.speculative_wait = true  ;
					proto_seen_waiting = true ;
					if (!dri) cdri = dri = &dnd.req_info(*cdri) ;                                                  // refresh cdri in case dri allocated a new one
					dnd.add_watcher(*dri,idx(),ri,dep_pressure) ;
					critical_waiting |= is_critical                            ;
					report_reason    |= { JobReasonTag::BusyDep , +Node(dep) } ;
				} else if ( bool dep_done=dnd.done(*cdri,dep_goal) ; query && !dep_done ) {
					proto_seen_waiting = true ;                                                                    // if queried dep is not done, it would have been waiting if not queried
					state.reason |= {JobReasonTag::DepOutOfDate,+dep} ;
				} else {
					SWEAR(dep_done) ;                                                                              // unless query, after having called make, dep must be either waiting or done
					bool dep_missing_dsk = ri.full && may_care && !dnd.done(*cdri,NodeGoal::Dsk) ;
					state.missing_dsk |= dep_missing_dsk ;                                                         // job needs this dep if it must run
					if (dep_goal==NodeGoal::Makable) {
						if ( is_static && required && dnd.ok(*cdri,dep.accesses)==Maybe )
							state.reason |= {JobReasonTag::DepMissingStatic,+dep} ;
						continue ;                                                  // dont check modifs and errors
					}
					if (!care) goto Continue ;
					dep_modif = !dep.up_to_date( is_static && modif ) ;             // after a modified dep, consider static deps as being fully accessed to avoid reruns due to previous errors
					if ( dep_modif && status==Status::Ok && dep.no_trigger() ) {    // no_trigger only applies to successful jobs
						trace("no_trigger",dep) ;
						req->no_triggers.emplace(dep,req->no_triggers.size()) ;     // record to repeat in summary, value is just to order summary in discovery order
						dep_modif = false ;
					}
					if ( +state.stamped_err  ) goto Continue ;                      // we are already in error, no need to analyze errors any further
					if ( !is_static && modif ) goto Continue ;                      // if not static, errors may be washed by previous modifs, dont record them
					if ( dep_modif           ) {
						if ( dep.is_crc && dep.never_match() ) { state.reason |= {JobReasonTag::DepUnstable ,+dep} ; trace("unstable_modif",dep) ; }
						else                                     state.reason |= {JobReasonTag::DepOutOfDate,+dep} ;
					}
					//
					switch (dnd.ok(*cdri,dep.accesses)) {
						case No :
							trace("dep_err",dep,STR(sense_err)) ;
							if      (+(cdri->overwritten&dep.accesses)) { state.reason |= {JobReasonTag::DepOverwritten,+dep} ; dep_err = RunStatus::DepErr ; } // even with !sense_err
							else if (sense_err                        ) { state.reason |= {JobReasonTag::DepErr        ,+dep} ; dep_err = RunStatus::DepErr ; }
						break ;
						case Maybe :                                                                     // dep is not buidlable, check if required
							if (dnd.status()==NodeStatus::Transient) {                                   // dep uphill is a symlink, it will disappear at next run
								trace("transient",dep) ;
								state.reason |= {JobReasonTag::DepTransient,+dep} ;
								break ;
							}
							if (required) {
								if (is_static) { state.reason |= {JobReasonTag::DepMissingStatic  ,+dep} ; dep_err = RunStatus::MissingStatic ; }
								else           { state.reason |= {JobReasonTag::DepMissingRequired,+dep} ; dep_err = RunStatus::DepErr        ; }
								trace("missing",STR(is_static),dep) ;
								break ;
							}
							dep_missing_dsk |= cdri->manual>=Manual::Changed ;                           // ensure dangling are correctly handled
						[[fallthrough]] ;
						case Yes :
							if (dep_goal==NodeGoal::Dsk) {                                               // if asking for disk, we must check disk integrity
								switch(cdri->manual) {
									case Manual::Empty   :
									case Manual::Modif   : state.reason |= {JobReasonTag::DepUnstable,+dep} ; dep_err = RunStatus::DepErr ; trace("dangling",dep,cdri->manual) ; break ;
									case Manual::Unlnked : state.reason |= {JobReasonTag::DepUnlnked ,+dep} ;                               trace("unlnked" ,dep             ) ; break ;
								DN}
							} else if ( dep_modif && make_action==MakeAction::End && dep_missing_dsk ) { // dep out of date but we do not wait for it being rebuilt
								dep_goal = NodeGoal::Dsk ;                                               // we must ensure disk integrity for detailed analysis
								trace("restart_dep",dep) ;
								goto RestartDep ;                                                        // BACKWARD
							}
						break ;
					DF}
				}
			Continue :
				trace("dep",ri,dep,*cdri,STR(dnd.done(*cdri)),STR(dnd.ok()),dnd.crc,dep_err,STR(dep_modif),STR(critical_modif),STR(critical_waiting),state.reason) ;
				//
				if ( state.missing_dsk && need_run(state) ) {
					SWEAR(!query) ;                                  // when query, we cannot miss dsk
					trace("restart_analysis") ;
					ri.reset(idx()) ;
					SWEAR(!ri.reason,ri.reason) ;                    // we should have asked for dep on disk if we had a reason to run
					ri.reason     = state.reason ;                   // record that we must ask for dep on disk
					report_reason = {}           ;
					goto RestartAnalysis ;                           // BACKWARD
				}
				SWEAR(!( +dep_err && modif && !is_static )) ;        // if earlier modifs have been seen, we do not want to record errors as they can be washed, unless static
				state.proto_err    = state.proto_err   | dep_err   ; // |= is forbidden for bit fields
				state.proto_modif  = state.proto_modif | dep_modif ; // .
				critical_modif    |= dep_modif && is_critical      ;
			}
			if ( ri.waiting()                       ) goto Wait ;
			if ( sure                               ) mk_sure() ;    // improve sure (sure is pessimistic)
			if ( +(run_status=ri.state.stamped_err) ) goto Done ;
			if ( !need_run(ri.state)                ) goto Done ;
		}
	Run :
		trace("run",pre_reason,ri,run_status) ;
		if ( query && !is_special() ) { report_reason = reason() ; goto Return          ; }        // ensure we have a reason to report that we would have run if not queried
		if ( ri.state.missing_dsk   ) { ri.reset(idx())          ; goto RestartAnalysis ; }
		SWEAR(!ri.state.missing_dsk) ;                                                             // cant run if we are missing some deps on disk
		if (r->n_submits) {
			if (ri.n_submits>=r->n_submits) {
				trace("submit_loop") ;
				status = Status::SubmitLoop ;
				goto Done ;
			}
			ri.n_submits++ ;
		}
		//                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if (is_special()) {      _submit_special( ri                           ) ; goto Done ; }   // special never report new deps
		else              { if (!_submit_plain  ( ri , reason() , dep_pressure ))  goto Done ; }   // if no new deps, we cannot be waiting and we are done
		//                       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		if (ri.waiting()) goto Wait ;
		SWEAR(!ri.running()) ;
		make_action = MakeAction::End  ;                                                           // restart analysis as if called by end() as in case of flash execution, submit has called end()
		ri.inc_wait() ;                                                                            // .
		asked_reason = {} ;                                                                        // .
		ri.reason    = {} ;                                                                        // .
		trace("restart_analysis",ri) ;
		goto RestartFullAnalysis ;                                                                 // BACKWARD
	Done :
		SWEAR( !ri.running() && !ri.waiting() , idx() , ri ) ;
		ri.step(Step::Done,idx()) ;
	Wakeup :
		if ( auto it = req->missing_audits.find(idx()) ; it!=req->missing_audits.end() && !req.zombie() ) {
			JobAudit const& ja = it->second ;
			trace("report_missing",ja) ;
			::string const& stderr = idx().job_info(false/*need_start*/,true/*need_end*/).end.end.digest.stderr ;
			//
			if (ja.report!=JobReport::Hit) req->stats.move(JobReport::Rerun,ja.report,exec_time) ; // if not Hit, then job was rerun and ja.report is the report that would have been done w/o rerun
			//
			size_t max_stderr_len ;
			// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
			try {
				Rule::SimpleMatch match ;
				max_stderr_len = r->end_none_attrs.eval( idx() , match , &::ref(::vmap_s<DepDigest>()) ).max_stderr_len ; // we cant record deps here, but we dont care, no impact on target
			} catch (::pair_ss const& msg_err) {
				max_stderr_len = r->end_none_attrs.spec.max_stderr_len ;
				req->audit_job(Color::Note,"dynamic",idx()) ;
				req->audit_stderr( idx() , ensure_nl(r->end_none_attrs.s_exc_msg(true/*using_static*/))+msg_err.first , msg_err.second , -1 , 1 ) ;
			}
			JobReason jr  = reason()                                                                      ;
			::string  pfx = status==Status::SubmitLoop ? "" : ja.report==JobReport::Hit ? "hit_" : "was_" ;
			if (jr.tag>=JobReasonTag::Err) audit_end( ri , true/*with_stats*/ , pfx , reason_str(jr) , stderr , max_stderr_len ) ;
			else                           audit_end( ri , true/*with_stats*/ , pfx , ja.backend_msg , stderr , max_stderr_len ) ;
			req->missing_audits.erase(it) ;
		}
		trace("wakeup",ri) ;
		//                                  vvvvvvvvvvvvvvvvvvvv
		if ( ri.done() && wakeup_watchers ) ri.wakeup_watchers() ;
		//                                  ^^^^^^^^^^^^^^^^^^^^
		report_reason = reason() ;
		goto Return ;
	Wait :
		switch (ri.step()) {
			case Step::Queued :
			case Step::Exec   : report_reason |= JobReasonTag::Busy        ; break ;
			default           : report_reason |= JobReasonTag::SomeBusyDep ;
		}
		SWEAR(!query) ;                                                                                                   // ensure we never wait when query
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
			dep->propag_speculate( cri.req , cri.speculate | (speculate&(!dep.dflags[Dflag::Static])) ) ; // static deps are never speculative
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			NodeReqInfo const& cdri = dep->c_req_info(cri.req) ;
			if ( !dep.is_crc || cdri.waiting() ) { proto_speculate = Yes ; continue ; }
			Bool3 dep_ok = cdri.done(NodeGoal::Status) ? dep->ok(cdri,dep.accesses) : Maybe ;
			switch (dep_ok) {
				case Yes   :                                                                                                               break ;
				case Maybe : if (  dep.dflags[Dflag::Required   ] || dep.dflags[Dflag::Static] ) { proto_speculate |= Maybe ; continue ; } break ;
				case No    : if ( !dep.dflags[Dflag::IgnoreError] || +cdri.overwritten         ) { proto_speculate |= Maybe ; continue ; } break ;
				default : FAIL(dep_ok) ;
			}
			if ( +dep.accesses && !dep.up_to_date() ) proto_speculate = Yes ;
		}
	}

	::string JobData::special_stderr(Node node) const {
		if (is_ok(status)!=No) return {} ;
		Rule r = rule() ;
		switch (r->special) {
			case Special::Plain :
				SWEAR(idx().frozen()) ;
				if (+node) return "frozen file does not exist while not phony : "+node->name()+'\n' ;
				else       return "frozen file does not exist while not phony\n"                    ;
			case Special::Infinite : {
				::string res ;
				for( Dep const& d : deps ) res << d->name() << '\n' ;
				return res ;
			}
			default :
				return r->special+" error\n"s ;
		}
	}

	void JobData::_submit_special(ReqInfo& ri) {                               // never report new deps
		Trace trace("submit_special",idx(),ri) ;
		Req     req     = ri.req           ;
		Special special = rule()->special  ;
		bool    frozen_ = idx().frozen()   ;
		//
		if (frozen_) req->frozen_jobs.emplace(idx(),req->frozen_jobs.size()) ; // record to repeat in summary, value is only for ordering summary in discovery order
		//
		switch (special) {
			case Special::Plain : {
				SWEAR(frozen_) ;                                               // only case where we are here without special rule
				SpecialStep special_step = SpecialStep::Idle         ;
				Node        worst_target ;
				Bool3       modified     = No                        ;
				NfsGuard    nfs_guard    { g_config->reliable_dirs } ;
				for( Target t : targets ) {
					::string    tn = t->name()         ;
					SpecialStep ss = SpecialStep::Idle ;
					if (!( t->crc.valid() && FileSig(nfs_guard.access(tn))==t->date().sig )) {
						FileSig sig  ;
						Crc   crc { sig , tn } ;
						modified |= crc.match(t->crc) ? No : t->crc.valid() ? Yes : Maybe ;
						Trace trace( "frozen" , t->crc ,"->", crc , t->date() ,"->", sig ) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvv
						t->refresh( crc , {sig,{}} ) ; // if file disappeared, there is not way to know at which date, we are optimistic here as being pessimistic implies false overwrites
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
			case Special::Infinite :
				status = Status::Err ;
				audit_end_special( req , SpecialStep::Err , No/*modified*/ ) ;
			break ;
		DF}
	}

	bool/*maybe_new_deps*/ JobData::_submit_plain( ReqInfo& ri , JobReason reason , CoarseDelay pressure ) {
		using Step = JobStep ;
		Rule              r     = rule()  ;
		Req               req   = ri.req  ;
		Rule::SimpleMatch match { idx() } ;
		Trace trace("_submit_plain",idx(),ri,reason,pressure) ;
		SWEAR(!ri.waiting(),ri) ;
		SWEAR(!ri.running(),ri) ;
		for( Req rr : running_reqs(false/*with_zombies*/) ) if (rr!=req) {
			ReqInfo const& cri = c_req_info(rr) ;
			ri.backend = cri.backend ;
			ri.step(cri.step(),idx()) ;                                  // Exec or Queued, same as other reqs
			ri.inc_wait() ;
			if (ri.step()==Step::Exec) req->audit_job(Color::Note,"started",idx()) ;
			SubmitAttrs sa = {
				.live_out = ri.live_out
			,	.pressure = pressure
			} ;
			if (req->options.flags[ReqFlag::RetryOnError]) sa.n_retries = from_string<uint8_t>(req->options.flag_args[+ReqFlag::RetryOnError]) ;
			//       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Backend::s_add_pressure( ri.backend , +idx() , +req , sa ) ; // tell backend of new Req, even if job is started and pressure has become meaningless
			//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("other_req",rr,ri) ;
			return true/*maybe_new_deps*/ ;
		}
		//
		for( Node t : targets ) t->set_buildable() ;                     // we will need to know if target is a source, possibly in another thread, we'd better call set_buildable here
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		CacheNoneAttrs cache_none_attrs ;
		try {
			cache_none_attrs = r->cache_none_attrs.eval( idx() , match , &::ref(::vmap_s<DepDigest>()) ) ; // dont care about dependencies as these attributes have no impact on result
		} catch (::pair_ss const& msg_err) {
			cache_none_attrs = r->cache_none_attrs.spec ;
			req->audit_job(Color::Note,"no_dynamic",idx()) ;
			req->audit_stderr( idx() , ensure_nl(r->cache_none_attrs.s_exc_msg(true/*using_static*/))+msg_err.first , msg_err.second , -1 , 1 ) ;
		}
		if (+cache_none_attrs.key) {
			Cache*       cache       = Cache::s_tab.at(cache_none_attrs.key) ;
			Cache::Match cache_match = cache->match(idx(),req)               ;
			if (!cache_match.completed) FAIL("delayed cache not yet implemented") ;
			switch (cache_match.hit) {
				case Yes :
					try {
						NfsGuard nfs_guard { g_config->reliable_dirs } ;
						//
						vmap<Node,FileAction> fas     = pre_actions(match) ;
						::vmap_s<FileAction>  actions ;                                                  for( auto [t,a] : fas ) actions.emplace_back( t->name() , a ) ;
						::pair_s<bool/*ok*/>  dfa_msg = do_file_actions( ::move(actions) , nfs_guard ) ;
						//
						if ( +dfa_msg.first || !dfa_msg.second ) {
							run_status = RunStatus::Err ;
							req->audit_job ( dfa_msg.second?Color::Note:Color::Err , "wash" , idx()     ) ;
							req->audit_info(                Color::Note            , dfa_msg.first  , 1 ) ;
							trace("hit_err",dfa_msg,ri) ;
							if (!dfa_msg.second) return false/*maybe_new_deps*/ ;
						}
						//
						JobExec je       { idx() , New }                                          ;        // job starts and ends, no host
						JobInfo job_info = cache->download(idx(),cache_match.id,reason,nfs_guard) ;
						Job::_s_record_thread.emplace(idx(),job_info) ;
						if (ri.live_out) je.live_out(ri,job_info.end.end.digest.stdout) ;
						ri.step(Step::Hit,idx()) ;
						trace("hit_result") ;
						je.end( ::move(job_info.end.end) , false/*sav_jrr*/ ) ;                            // no resources nor backend for cached jobs
						req->stats.add(JobReport::Hit) ;
						req->missing_audits[idx()] = { JobReport::Hit , {} } ;
						return true/*maybe_new_deps*/ ;
					} catch (::string const&) {}                                                           // if we cant download result, it is like a miss
				break ;
				case Maybe :
					for( Node d : cache_match.new_deps ) {
						NodeReqInfo& dri = d->req_info(req) ;
						d->make(dri,NodeMakeAction::Status) ;
						if (dri.waiting()) d->add_watcher(dri,idx(),ri,pressure) ;
					}
					trace("hit_deps") ;
					return true/*maybe_new_deps*/ ;
				case No :
				break ;
			DF}
		}
		//
		::vmap_s<DepDigest> early_deps         ;
		SubmitRsrcsAttrs    submit_rsrcs_attrs ;
		try {
			submit_rsrcs_attrs = r->submit_rsrcs_attrs.eval( idx() , match , &early_deps ) ;
		} catch (::pair_ss const& msg_err) {
			req->audit_job   ( Color::Err  , "failed" , idx()                                                                                    ) ;
			req->audit_stderr( idx() , ensure_nl(r->submit_rsrcs_attrs.s_exc_msg(false/*using_static*/))+msg_err.first , msg_err.second , -1 , 1 ) ;
			run_status = RunStatus::Err ;
			trace("no_rsrcs",ri) ;
			return false/*maybe_new_deps*/ ;
		}
		for( auto const& [dn,dd] : early_deps ) {
			Node         d   { dn }             ;
			NodeReqInfo& dri = d->req_info(req) ;
			d->make(dri,NodeMakeAction::Dsk) ;
			if (dri.waiting()) d->add_watcher(dri,idx(),ri,pressure) ;
		}
		if (ri.waiting()) {
			trace("waiting_rsrcs") ;
			return true/*maybe_new_deps*/ ;
		}
		// do not generate error if *_none_attrs is not available, as we will not restart job when fixed : do our best by using static info
		SubmitNoneAttrs submit_none_attrs ;
		try {
			submit_none_attrs = r->submit_none_attrs.eval( idx() , match , &early_deps ) ;
		} catch (::pair_ss const& msg_err) {
			submit_none_attrs = r->submit_none_attrs.spec ;
			req->audit_job(Color::Note,"no_dynamic",idx()) ;
			req->audit_stderr( idx() , ensure_nl(r->submit_none_attrs.s_exc_msg(true/*using_static*/))+msg_err.first , msg_err.second , -1 , 1 ) ;
		}
		//
		ri.inc_wait() ;                                                                                    // set before calling submit call back as in case of flash execution, we must be clean
		ri.step(Step::Queued,idx()) ;
		ri.backend = submit_rsrcs_attrs.backend ;
		try {
			Tokens1 tokens1 = submit_rsrcs_attrs.tokens1() ;
			SubmitAttrs sa = {
				.live_out  = ri.live_out
			,	.n_retries = submit_none_attrs.n_retries
			,	.tokens1   = tokens1
			,	.pressure  = pressure
			,	.deps      = ::move(early_deps)
			,	.reason    = reason
			} ;
			if (req->options.flags[ReqFlag::RetryOnError]) sa.n_retries = ::max( sa.n_retries , from_string<uint8_t>(req->options.flag_args[+ReqFlag::RetryOnError]) ) ;
			estimate_stats(tokens1) ;                                                                      // refine estimate with best available info just before submitting
			//       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Backend::s_submit( ri.backend , +idx() , +req , ::move(sa) , ::move(submit_rsrcs_attrs.rsrcs) ) ;
			//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			for( Node t : targets ) if (t->polluted==Polluted::Clean) t->polluted = Polluted::Busy ;       // make targets busy once we are sure job is submitted
		} catch (::string const& e) {
			ri.dec_wait() ;                                                                                // restore n_wait as we prepared to wait
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
		bool     frozen_  = idx().frozen()       ;
		::string stderr   = special_stderr(node) ;
		::string step_str ;
		switch (step) {
			case SpecialStep::Idle :                                                                             break ;
			case SpecialStep::Ok   : step_str = modified==Yes ? "changed" : modified==Maybe ? "new" : "steady" ; break ;
			case SpecialStep::Err  : step_str = "failed"                                                       ; break ;
			case SpecialStep::Loop : step_str = "loop"                                                         ; break ;
		DF}
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

	bool/*ok*/ JobData::forget( bool targets_ , bool deps_ ) {
		Trace trace("Jforget",idx(),STR(targets_),STR(deps_)) ;
		for( Req r : running_reqs() ) { (void)r ; return false ; } // ensure job is not running
		status = Status::New ;
		fence() ;                                                  // once status is New, we are sure target is not up to date, we can safely modify it
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
