// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "lmakeserver/core.hh" // must be first to include Python.h first

#include "app.hh"
#include "disk.hh"
#include "lmakeserver/makefiles.hh"

using namespace Disk ;

using namespace Engine ;

struct RepairDigest {
	JobIdx n_repaired  = 0 ;
	JobIdx n_processed = 0 ;
} ;

RepairDigest repair(::string const& from_dir_s) {
	Trace trace("repair",from_dir_s) ;
	RepairDigest     res      ;
	::umap<Crc,Rule> rule_tab ; for( Rule r : Persistent::rule_lst() ) rule_tab[r->crc->cmd] = r ; SWEAR(rule_tab.size()==Persistent::rule_lst().size()) ;
	for( ::string const& jd : walk(no_slash(from_dir_s),no_slash(from_dir_s)) ) {
		{	JobInfo job_info { jd } ;
			// qualify report
			if (job_info.end.digest.status!=Status::Ok) { trace("not_ok",jd) ; goto NextJob ; }                         // repairing jobs in error is useless
			// find rule
			auto it = rule_tab.find(job_info.start.rule_cmd_crc) ;
			if (it==rule_tab.end()) { trace("no_rule",jd) ; goto NextJob ; }                                            // no rule
			Rule rule = it->second ;
			// find targets
			::vector<Target> targets ; targets.reserve(job_info.end.digest.targets.size()) ;
			for( auto const& [tn,td] : job_info.end.digest.targets ) {
				if ( !is_canon(tn)                                 ) { trace("nul_in_target" ,jd,tn) ; goto NextJob ; } // this should never happen, there is a problem with this job
				if ( td.crc==Crc::None && !static_phony(td.tflags) )                                   continue     ;   // this is not a target
				if ( !td.crc.valid()                               ) { trace("invalid_target",jd,tn) ; goto NextJob ; } // XXX? : handle this case (maybe not worthwhile)
				if ( td.sig!=FileSig(tn)                           ) { trace("disk_mismatch" ,jd,tn) ; goto NextJob ; } // if dates do not match, we will rerun the job anyway
				//
				Node t{tn} ;
				t->refresh( td.crc , {td.sig,{}} ) ;                                                                    // if file does not exist, the Epoch as a date is fine
				targets.emplace_back( t , td.tflags ) ;
			}
			::sort(targets) ;                                                                              // ease search in targets
			// find deps
			::vector_s    src_dirs ; for( Node s : Node::s_srcs(true/*dirs*/) ) src_dirs.push_back(s->name()) ;
			::vector<Dep> deps     ; deps.reserve(job_info.end.digest.deps.size()) ;
			job_info.update_digest() ;                                                                     // gather newer dep crcs
			for( auto const& [dn,dd] : job_info.end.digest.deps ) {
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
			job->status = job_info.end.digest.status ;
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

int main( int argc , char* /*argv*/[] ) {
	::string admin_dir_s = AdminDirS ;
	//
	if (argc!=1) exit(Rc::Usage,"must be called without arg") ;
	g_trace_file = new ::string() ;                                // no trace as we are repairing admin_dir_s in which traces are made
	block_sigs({SIGCHLD}) ;
	app_init(false/*read_only_ok*/) ;
	Py::init(*g_lmake_root_s      ) ;
	if (+*g_startup_dir_s) {
		g_startup_dir_s->pop_back() ;
		FAIL("lrepair must be started from repo root, not from ",*g_startup_dir_s) ;
	}
	if (is_target(ServerMrkr)) exit(Rc::Format,"after having ensured no lmakeserver is running, consider : rm ",ServerMrkr) ;
	//
	::string backup_admin_dir_s = no_slash(admin_dir_s)+".bck/"s ; // rename in same dir to be sure not to break sym links that can be inside (e.g. lmake/local_admin_dir and lmake/remote_admin_dir)
	::string repair_mrkr      = admin_dir_s+"repairing"          ;
	if (FileInfo(repair_mrkr).tag()>=FileTag::Reg) unlnk(no_slash(admin_dir_s),true/*dir_ok*/) ;                // if last lrepair was interrupted, admin_dir_s contains no useful information
	if (is_dir(no_slash(backup_admin_dir_s))) {
		if      (is_dir(no_slash(admin_dir_s))                                                  ) exit(Rc::Format,"backup already exists, consider : rm -r ",no_slash(backup_admin_dir_s)) ;
	} else {
		if      (!is_dir(PrivateAdminDirS+"local_admin/job_data"s)                              ) exit(Rc::Fail  ,"nothing to repair"                                                    ) ;
		else if (::rename(no_slash(admin_dir_s).c_str(),no_slash(backup_admin_dir_s).c_str())!=0) exit(Rc::System,"backup failed to ",no_slash(backup_admin_dir_s)                       ) ;
	}
	if ( AcFd fd { dir_guard(repair_mrkr) , Fd::Write } ; !fd ) exit(Rc::System,"cannot create ",repair_mrkr) ; // create marker
	g_writable = true ;
	{	::string msg ;
		msg << "the repair process is starting, if something goes wrong :"                                                                                             << '\n' ;
		msg << "to restore old state,                    consider : rm -r "<<no_slash(admin_dir_s)<<" ; mv "<<no_slash(backup_admin_dir_s)<<' '<<no_slash(admin_dir_s) << '\n' ;
		msg << "to restart the repair process,           consider : lrepair"                                                                                           << '\n' ;
		msg << "to continue with what has been repaired, consider : rm "<<repair_mrkr<<" ; rm -r "<<no_slash(backup_admin_dir_s)                                       << '\n' ;
		Fd::Stdout.write(msg) ;
	}
	try                       { chk_version( false/*may_init*/ , backup_admin_dir_s ) ; }
	catch (::string const& e) { exit(Rc::Format,e) ;                                    }
	//
	mk_dir_s(PrivateAdminDirS) ;
	//
	{	::string msg ;
		try                       { Makefiles::refresh( msg , false/*crashed*/ , true/*refresh*/ ) ; if (+msg) Fd::Stderr.write(ensure_nl(msg)) ;                      }
		catch (::string const& e) {                                                                  if (+msg) Fd::Stderr.write(ensure_nl(msg)) ; exit(Rc::Format,e) ; }
	}
	//
	Trace::s_new_trace_file( g_config->local_admin_dir_s+"trace/"+*g_exe_name ) ;
	for( AncillaryTag tag : iota(All<AncillaryTag>) ) dir_guard(Job().ancillary_file(tag)) ;
	//
	//                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	RepairDigest digest = repair(backup_admin_dir_s+PRIVATE_ADMIN_SUBDIR_S+"local_admin/job_data") ;
	//                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	chk_version(true/*may_init*/) ;
	unlnk(repair_mrkr) ;
	{	::string msg ;
		msg << "repo has been satisfactorily repaired "<<digest.n_repaired<<'/'<<digest.n_processed<<" jobs"                                                                             << '\n' ;
		msg << "to clean up after having ensured everything runs smoothly, consider : rm -r "<<no_slash(backup_admin_dir_s)                                                              << '\n' ;
		msg << "to restore old state,                                      consider : rm -r "<<no_slash(admin_dir_s)<<" ; mv "<<no_slash(backup_admin_dir_s)<<' '<<no_slash(admin_dir_s) << '\n' ;
		msg << "to restart the repair process,                             consider : rm -r "<<no_slash(admin_dir_s)<<" ; lrepair"                                                       << '\n' ;
		Fd::Stdout.write(msg) ;
	}
	return 0 ;
}
