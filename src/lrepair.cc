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
	RepairDigest res           ;
	AcFd         repaired_jobs { cat(AdminDirS,"repaired_jobs") , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} } ;
	//
	::umap<Crc,Rule> rule_tab ; rule_tab.reserve(Rule::s_rules->size()) ; for( Rule r : Persistent::rule_lst() ) rule_tab[r->crc->cmd] = r ; SWEAR(rule_tab.size()==Persistent::rule_lst().size()) ;
	//
	for( auto const& [jd,_] : walk(from_dir,TargetTags,from_dir) ) {
		{	JobInfo job_info { jd } ;
			try                       { job_info.chk() ;                        }
			catch (::string const& e) { trace("bad_info",jd,e) ; goto NextJob ; }
			// qualify report
			if (job_info.end.digest.status!=Status::Ok) { trace("not_ok",jd) ; goto NextJob ; }                            // repairing jobs in error is useless
			// find rule
			auto it = rule_tab.find(job_info.start.rule_crc_cmd) ;
			if (it==rule_tab.end()) { trace("no_rule",jd,job_info.start.rule_crc_cmd) ; goto NextJob ; }                   // no rule
			Rule rule = it->second ;
			// find targets
			::vector<Target> targets ; targets.reserve(job_info.end.digest.targets.size()) ;
			for( auto const& [tn,td] : job_info.end.digest.targets ) {
				if ( !tn                                           ) { trace("empty target"    ,jd   ) ; goto NextJob ; }
				if ( !is_lcl(tn)                                   ) { trace("non-local target",jd,tn) ; goto NextJob ; }
				if ( td.crc==Crc::None && !static_phony(td.tflags) )   continue ;                                          // not a target
				FileSig sig { tn } ;
				if ( (td.crc==Crc::None) != !sig                 ) { trace("disk_mismatch_none" ,jd,tn) ; goto NextJob ; } // do not agree on file existence
				if ( td.sig              !=  sig                 ) { trace("disk_mismatch"      ,jd,tn) ; goto NextJob ; } // if dates do not match, we will rerun the job anyway
				if ( !td.crc.valid() && td.tflags[Tflag::Target] ) { trace("no_vadid_target_crc",jd,tn) ; goto NextJob ; }
				if ( !td.crc                                     ) { trace("no_crc"             ,jd,tn) ; goto NextJob ; }
				//
				Node t { New , tn } ;
				t->set_crc_date( td.crc , {td.sig,{}} ) ;                                                                  // if file does not exist, the Epoch as a date is fine
				targets.emplace_back( t , td.tflags ) ;
			}
			::sort(targets) ;                                                                              // ease search in targets
			// find deps
			::vector_s    src_dirs_s ; for( Node s : Node::s_srcs(true/*dirs*/) ) src_dirs_s.push_back(with_slash(s->name())) ;
			::vector<Dep> deps       ; deps.reserve(job_info.end.digest.deps.size()) ;
			job_info.update_digest() ;                                                                     // gather newer dep crcs
			for( auto const& [dn,dd] : job_info.end.digest.deps ) {
				if (!dn        ) { trace("empty dep",jd) ; goto NextJob ; }
				if (!is_lcl(dn)) {
					for( ::string const& sd_s : src_dirs_s ) if (lies_within(dn,sd_s)) goto KeepDep ;      // this could be optimized by searching the longest match in the name prefix tree
					trace("non-local dep",jd,dn) ;
					goto NextJob ;                                                                         // this should never happen as src_dirs_s are part of cmd definition
				KeepDep : ;
				}
				Dep dep { Node(New,dn) , dd } ;
				if ( !dep.is_crc                         ) { trace("no_dep_crc" ,jd,dn) ; goto NextJob ; } // dep could not be identified when job ran, hum, better not to repair that
				if ( +dep.accesses && !dep.crc().valid() ) { trace("invalid_dep",jd,dn) ; goto NextJob ; } // no valid crc, no interest to repair as job will rerun anyway
				deps.push_back(dep) ;
			}
			// set job
			Rule::RuleMatch m   { rule , ::move(job_info.start.stems) } ; if ( ::string msg=m.reject_msg().first ; +msg ) { trace("rejected"         ,jd,msg) ; goto NextJob ; }
			Job             job { ::move(m)                           } ; if ( !job                                     ) { trace("no_job_from_match",jd    ) ; goto NextJob ; }
			//
			job->targets().assign(targets) ;
			job->deps     .assign(deps   ) ;
			job->status = job_info.end.digest.status ;
			job->set_exec_ok() ;                                                                           // pretend job just ran
			// set target actual_job's
			for( Target t : targets ) {
				t->actual_job    = job      ;
				t->actual_tflags = t.tflags ;
			}
			// adjust job_info
			job_info.start.pre_start.job            = +job ;
			job_info.start.submit_attrs.reason.node = 0    ;                                               // reason node is stored as a idx, not a name, cannot restore it
			// restore job_data
			job.record(job_info) ;
			::string jn = job->name() ;
			repaired_jobs.write(cat(jn,'\n')) ;
			trace("restored",jd,jn) ;
		}
		res.n_repaired++ ;
	NextJob : ;
		res.n_processed++ ;
	}
	return res ;
}

// lad = local_admin_dir
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
	auto mk_lad = [&]() {
		phy_lad          = {}                     ; if (FileInfo(std_lad    ).tag()==FileTag::Lnk) phy_lad           = mk_glb( read_lnk(std_lad    ) , dir_name_s(std_lad    ) ) ;
		bck_phy_lad      = {}                     ; if (FileInfo(bck_std_lad).tag()==FileTag::Lnk) bck_phy_lad       = mk_glb( read_lnk(bck_std_lad) , dir_name_s(bck_std_lad) ) ;
		rm_admin_dir     = "rm -r "+admin_dir     ; if (+phy_lad                                 ) rm_admin_dir     << ' '<<phy_lad                                              ;
		rm_bck_admin_dir = "rm -r "+bck_admin_dir ; if (+bck_phy_lad                             ) rm_bck_admin_dir << ' '<<bck_phy_lad                                          ;
		//
		if ( +phy_lad && +bck_phy_lad ) SWEAR( phy_lad!=bck_phy_lad , phy_lad , bck_phy_lad ) ;
	} ;
	//
	app_init(false/*read_only_ok*/) ;
	//
	if (argc!=1              )                                                exit(Rc::Usage   ,"must be called without arg"                                               ) ;
	try { startup_s = search_root().startup_s ; } catch (::string const& e) { exit(Rc::Usage   ,e                                                                          ) ; }
	if (+startup_s           )                                                exit(Rc::Usage   ,"lrepair must be started from repo root, not from ",no_slash(startup_s)    ) ;
	if (FileInfo(File(ServerMrkr)).exists())                                  exit(Rc::BadState,"after having ensured no lmakeserver is running, consider : rm ",ServerMrkr) ;
	//
	if (FileInfo(repair_mrkr).tag()>=FileTag::Reg) unlnk(admin_dir,{.dir_ok=true}) ; // if last lrepair was interrupted, admin_dir contains no useful information
	if (FileInfo(bck_admin_dir_s).tag()==FileTag::Dir) {
		if (FileInfo(admin_dir_s).tag()==FileTag::Dir) {
			mk_lad() ;
			exit(Rc::BadState,"both ",admin_dir," and ",bck_admin_dir," exist, consider one of :\n\t",rm_admin_dir,"\n\t",rm_bck_admin_dir) ;
		}
		try                       { rename( bck_admin_dir/*src*/ , admin_dir/*dst*/ ) ; }
		catch (::string const& e) { fail_prod(e) ;                                      }
	}
	if (FileInfo(cat(PrivateAdminDirS,"local_admin/job_data/")).tag()!=FileTag::Dir) exit(Rc::Fail,"nothing to repair") ;
	//
	g_trace_file = New ;
	block_sigs({SIGCHLD}) ;
	Py::init(*g_lmake_root_s) ;
	AutodepEnv ade ;
	ade.repo_root_s         = *g_repo_root_s ;
	Record::s_static_report = true           ;
	Record::s_autodep_env(ade) ;
	//
	try                       { rename( admin_dir/*src*/ , bck_admin_dir/*dst*/ ) ; }
	catch (::string const& e) { fail_prod(e) ;                                      }
	//
	if ( !AcFd( repair_mrkr , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.err_ok=true} ) ) exit(Rc::System,"cannot create ",repair_mrkr) ; // create marker
	g_writable = true ;
	//
	g_trace_file = new ::string{cat(PrivateAdminDirS,"trace/",*g_exe_name)} ;
	Trace::s_start() ;
	//
	// make a fresh local admin dir
	{	::string msg ;
		try                       { Makefiles::refresh(/*out*/msg,mk_environ(),false/*rescue*/,true/*refresh*/,*g_startup_dir_s) ; if (+msg) Fd::Stderr.write(with_nl(msg)) ;                        }
		catch (::string const& e) {                                                                                                if (+msg) Fd::Stderr.write(with_nl(msg)) ; exit(Rc::BadState,e) ; }
	}
	//
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
	Persistent::chk() ;
	chk_version(true/*may_init*/) ;                                                                                                        // mark repo as initialized
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
	exit(Rc::Ok) ;
}
