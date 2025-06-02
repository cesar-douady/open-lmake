// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "lmakeserver/core.hh" // /!\ must be first to include Python.h first

#include "app.hh"
#include "disk.hh"
#include "lmakeserver/makefiles.hh"

using namespace Disk ;

using namespace Engine ;

struct RepairDigest {
	JobIdx n_repaired  = 0 ;
	JobIdx n_processed = 0 ;
} ;

RepairDigest repair(::string const& from_dir) {
	Trace trace("repair",from_dir) ;
	RepairDigest     res      ;
	::umap<Crc,Rule> rule_tab ; for( Rule r : Persistent::rule_lst() ) rule_tab[r->crc->cmd] = r ; SWEAR(rule_tab.size()==Persistent::rule_lst().size()) ;
	for( ::string const& jd : walk(from_dir,from_dir) ) {
		{	JobInfo job_info { jd } ;
			// qualify report
			if (job_info.end.digest.status!=Status::Ok) { trace("not_ok",jd) ; goto NextJob ; }                         // repairing jobs in error is useless
			// find rule
			auto it = rule_tab.find(job_info.start.rule_crc_cmd) ;
			if (it==rule_tab.end()) { trace("no_rule",jd,job_info.start.rule_crc_cmd) ; goto NextJob ; }                // no rule
			Rule rule = it->second ;
			// find targets
			::vector<Target> targets ; targets.reserve(job_info.end.digest.targets.size()) ;
			for( auto const& [tn,td] : job_info.end.digest.targets ) {
				if ( !is_canon(tn)                                 ) { trace("nul_in_target" ,jd,tn) ; goto NextJob ; } // this should never happen, there is a problem with this job
				if ( td.crc==Crc::None && !static_phony(td.tflags) )                                   continue     ;   // this is not a target
				if ( !td.crc.valid()                               ) { trace("invalid_target",jd,tn) ; goto NextJob ; } // XXX? : handle this case (is it worth?)
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
			// adjust job_info
			job_info.start.pre_start.job            = +job ;
			job_info.start.submit_attrs.reason.node = 0    ;                                               // reason node is stored as a idx, not a name, cannot restore it
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
	::string admin_dir_s      = AdminDirS                                                 ;
	::string admin_dir        = no_slash(admin_dir_s)                                     ;
	::string bck_admin_dir    = admin_dir+".bck"                                          ;
	::string bck_admin_dir_s  = with_slash(bck_admin_dir)                                 ;
	::string std_lad          = cat(admin_dir_s    ,PRIVATE_ADMIN_SUBDIR_S,"local_admin") ;
	::string bck_std_lad      = cat(bck_admin_dir_s,PRIVATE_ADMIN_SUBDIR_S,"local_admin") ;
	::string repair_mrkr      = admin_dir_s+"repairing"                                   ;
	::string phy_lad          ;
	::string bck_phy_lad      ;
	::string rm_admin_dir     ;
	::string rm_bck_admin_dir ;
	::string startup_s        ;
	//
	auto mk_lad = [&]()->void {
		phy_lad          = {}                     ; if (FileInfo(std_lad    ).tag()==FileTag::Lnk) phy_lad           = mk_glb(read_lnk(std_lad    ),dir_name_s(std_lad    )) ;
		bck_phy_lad      = {}                     ; if (FileInfo(bck_std_lad).tag()==FileTag::Lnk) bck_phy_lad       = mk_glb(read_lnk(bck_std_lad),dir_name_s(bck_std_lad)) ;
		rm_admin_dir     = "rm -r "+admin_dir     ; if (+phy_lad                                 ) rm_admin_dir     << ' '<<phy_lad                                          ;
		rm_bck_admin_dir = "rm -r "+bck_admin_dir ; if (+bck_phy_lad                             ) rm_bck_admin_dir << ' '<<bck_phy_lad                                      ;
		//
		if ( +phy_lad && +bck_phy_lad ) SWEAR( phy_lad!=bck_phy_lad , phy_lad , bck_phy_lad ) ;
	} ;
	//
	if (argc!=1)                                                              exit(Rc::Usage ,"must be called without arg"                                               ) ;
	try { startup_s = search_root().startup_s ; } catch (::string const& e) { exit(Rc::Usage ,e                                                                          ) ; }
	if (+startup_s)                                                           exit(Rc::Usage ,"lrepair must be started from repo root, not from ",no_slash(startup_s)    ) ;
	if (is_target(ServerMrkr))                                                exit(Rc::Format,"after having ensured no lmakeserver is running, consider : rm ",ServerMrkr) ;
	//
	if (FileInfo(repair_mrkr).tag()>=FileTag::Reg) unlnk(admin_dir,true/*dir_ok*/) ;                            // if last lrepair was interrupted, admin_dir contains no useful information
	if (is_dir(bck_admin_dir)) {
		if (is_dir(admin_dir)) {
			mk_lad() ;
			exit(Rc::Format,"both ",admin_dir," and ",bck_admin_dir," exist, consider one of :\n\t",rm_admin_dir,"\n\t",rm_bck_admin_dir) ;
		}
		if (::rename(bck_admin_dir.c_str(),admin_dir.c_str())!=0) FAIL("cannot rename",bck_admin_dir,"to",admin_dir) ;
	}
	if (!is_dir(cat(PrivateAdminDirS,"local_admin/job_data"))) exit(Rc::Fail,"nothing to repair") ;
	//
	g_trace_file = New ;
	block_sigs({SIGCHLD}) ;
	app_init(false/*read_only_ok*/) ;
	Py::init(*g_lmake_root_s) ;
	AutodepEnv ade ;
	ade.repo_root_s         = *g_repo_root_s ;
	Record::s_static_report = true           ;
	Record::s_autodep_env(ade) ;
	//
	if (::rename(admin_dir.c_str(),bck_admin_dir.c_str())!=0) FAIL("cannot rename",admin_dir,"to",bck_admin_dir) ;
	//
	if ( AcFd fd { dir_guard(repair_mrkr) , Fd::Write } ; !fd ) exit(Rc::System,"cannot create ",repair_mrkr) ; // create marker
	g_writable = true ;
	//
	mk_dir_s(PrivateAdminDirS) ;
	g_trace_file = new ::string{cat(PrivateAdminDirS,"trace/",*g_exe_name)} ;
	Trace::s_start() ;
	//
	{	::string msg ;
		try                       { Makefiles::refresh( msg , false/*crashed*/ , true/*refresh*/ ) ; if (+msg) Fd::Stderr.write(ensure_nl(msg)) ;                      }
		catch (::string const& e) {                                                                  if (+msg) Fd::Stderr.write(ensure_nl(msg)) ; exit(Rc::Format,e) ; }
	}
	//
	for( AncillaryTag tag : iota(All<AncillaryTag>) ) dir_guard(Job().ancillary_file(tag)) ;
	mk_lad() ;
	//
	{	::string msg ;
		msg << "the repair process is starting, if something goes wrong :"                                                  <<'\n' ;
		msg << "to restore old state,                    consider : "<<rm_admin_dir<<" ; mv "<<bck_admin_dir<<' '<<admin_dir<<'\n' ;
		msg << "to restart the repair process,           consider : lrepair"                                                <<'\n' ;
		msg << "to continue with what has been repaired, consider : rm "<<repair_mrkr<<" ; "<<rm_bck_admin_dir              <<'\n' ;
		Fd::Stdout.write(msg) ;
	}
	//                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	RepairDigest digest = repair(bck_std_lad+"/job_data") ;
	//                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	chk_version(true/*may_init*/) ;                                                                             // mark repo as initialized
	unlnk(repair_mrkr) ;
	{	::string msg ;
		msg <<                                                                                                                                  '\n' ;
		msg << "repo has been satisfactorily repaired : "<<digest.n_repaired<<'/'<<digest.n_processed<<" jobs"                                <<'\n' ;
		msg <<                                                                                                                                  '\n' ;
		msg << "to restore old state,                                      consider : "<<rm_admin_dir<<" ; mv "<<bck_admin_dir<<' '<<admin_dir<<'\n' ;
		msg << "to restart the repair process,                             consider : "<<rm_admin_dir<<" ; lrepair"                           <<'\n' ;
		msg << "to clean up after having ensured everything runs smoothly, consider : "<<rm_bck_admin_dir                                     <<'\n' ;
		Fd::Stdout.write(msg) ;
	}
	return 0 ;
}
