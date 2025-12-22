// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "app.hh"
#include "disk.hh"

#include "caches/daemon_cache.hh"
#include "engine.hh"
#include "daemon_cache_utils.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

enum class Key  : uint8_t { None   } ;
enum class Flag : uint8_t { DryRun } ;

struct RepairDigest {
	CjobIdx n_repaired  = 0 ;
	CjobIdx n_processed = 0 ;
} ;

Config g_config ;

static RepairDigest _repair(bool dry_run) {
	struct RunEntry {
		bool info = false ;
		bool data = false ;
	} ;
	Trace trace("_repair",STR(dry_run)) ;
	RepairDigest res           ;
	AcFd         repaired_runs ; if (!dry_run) repaired_runs = AcFd{ cat(AdminDirS,"repaired_runs") , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} } ;
	//
	::umap_s<RunEntry> tab       ;
	::uset_s           to_rm     ;
	::string           admin_dir = cat("./",AdminDirS,rm_slash) ;
	for( auto const& [file,_] : walk( Fd::Cwd , FileTag::Reg , {}/*pfx*/ , [&](::string const& f) { return f.starts_with(admin_dir) ; } ) ) {
		SWEAR(file[0]=='/') ;
		::string f = file.substr(1) ;
		if (f.ends_with("/info")) { tab[dir_name_s(f)].info = true ; continue ; }
		if (f.ends_with("/data")) { tab[dir_name_s(f)].data = true ; continue ; }
		to_rm.insert(f) ;
	}
	for( auto const& [dir_s,e] : tab ) {
		::string info_file = dir_s+"info" ;
		::string data_file = dir_s+"data" ;
		res.n_processed++ ;
		if (!( e.info && e.data )) {
			if (e.info) to_rm.insert(info_file) ;
			if (e.data) to_rm.insert(data_file) ;
			continue ;
		}
		try {
			JobInfo  job_info  = deserialize<JobInfo>(AcFd(info_file).read()) ;
			FileStat data_stat ;                                                ::lstat( data_file.c_str() , &data_stat ) ;
			job_info.chk(true/*for_cache*/) ;
			throw_unless( job_info.end.digest.status==Status::Ok , "bad status" ) ;
			//
			::string repo_key_str = base_name(dir_s) ;
			bool     key_is_last  ;
			if      (repo_key_str.ends_with("-first/")) key_is_last = false ;
			else if (repo_key_str.ends_with("-last/" )) key_is_last = true  ;
			else                                        throw cat("unexpected run entry",dir_s,rm_slash) ;
			repo_key_str = repo_key_str.substr( 0 , repo_key_str.rfind('-') ) ;
			//
			CompileDigest deps     = compile( job_info.end.digest.deps , false/*for_download*/ ) ;
			Cjob          job      { New , no_slash(dir_name_s(dir_s)) , deps.n_statics }        ;
			Crc           repo_key = Crc::s_from_hex(repo_key_str)                               ;
			//
			if (!dry_run) {
				::pair<Crun,CacheHitInfo> digest = job->insert(
					deps.deps , deps.dep_crcs                                                                                                                // to search entry
				,	repo_key , key_is_last , Pdate(data_stat.st_atim) , job_info.end.total_z_sz , rate(job_info.end.total_z_sz,job_info.end.digest.exe_time) // to create entry
				) ;
				throw_unless( digest.second>=CacheHitInfo::Miss , "conflict" ) ;
			}
		} catch (::string const&) {
			to_rm.insert(dir_s+"info") ;
			to_rm.insert(dir_s+"data") ;
			continue ;
		}
		res.n_repaired++ ;
	}
	:: string reserved = cat(PrivateAdminDirS,"reserved") ;
	if (+FileInfo(reserved)) {
		Fd::Stdout.write(cat("rm -r ",reserved,'\n')) ;
		if (!dry_run) unlnk( reserved , {.dir_ok=true} ) ;
	}
	for( ::string const& f : to_rm ) {
		Fd::Stdout.write(cat("rm ",f,'\n')) ;
		if (!dry_run) unlnk(f) ;
	}
	return res ;
}

int main( int argc , char* argv[] ) {
	Syntax<Key,Flag> syntax {{
		{ Flag::DryRun , { .short_name='n' , .doc="report actions but dont execute them" } }
	}} ;
	CmdLine<Key,Flag> cmd_line { syntax,argc,argv } ;
	if (cmd_line.args.size()<1) syntax.usage("must provide a cache dir to repair") ;
	if (cmd_line.args.size()>1) syntax.usage("cannot repair several cache dirs"  ) ;
	//
	if ( FileInfo(File(ServerMrkr)).exists() ) exit(Rc::BadState,"after having ensured no ldaemon_cache_server is running, consider : rm ",ServerMrkr) ;
	//
	::string const& top_dir_s = with_slash(cmd_line.args[0]) ;
	if (::chdir(top_dir_s.c_str())!=0) exit(Rc::System  ,"cannot chdir (",StrErr(),") to ",top_dir_s,rm_slash ) ;
	//
	app_init({
		.chk_version  = Yes
	,	.cd_root      = false                                                                          // we have already chdir'ed to top
	,	.read_only_ok = cmd_line.flags[Flag::DryRun]
	,	.root_mrkrs   = { cat(AdminDirS,"config.py") }
	,	.version      = Version::DaemonCache
	}) ;
	Py::init(*g_lmake_root_s) ;
	//
	::string lcl_repair_mrkr     = cat(AdminDirS,"repairing")             ;
	::string lcl_store_dir_s     = Config::s_store_dir_s(               ) ;
	::string lcl_bck_store_dir_s = Config::s_store_dir_s(true/*for_bck*/) ;
	::string repair_mrkr         = top_dir_s + lcl_repair_mrkr            ;
	::string store_dir_s         = top_dir_s + lcl_store_dir_s            ;
	::string bck_store_dir_s     = top_dir_s + lcl_bck_store_dir_s        ;
	//
	if (!cmd_line.flags[Flag::DryRun]) {
		if (FileInfo(lcl_repair_mrkr).tag()>=FileTag::Reg) unlnk( bck_store_dir_s , {.dir_ok=true} ) ; // if last ldaemon_cache_repair was interrupted, reset unfinished state
		//
		if (FileInfo(bck_store_dir_s).tag()!=FileTag::Dir) {
			try                       { rename( lcl_store_dir_s/*src*/ , lcl_bck_store_dir_s/*dst*/ ) ; }
			catch (::string const& e) { fail_prod(e) ;                                                  }
		} else if (FileInfo(lcl_store_dir_s).tag()==FileTag::Dir) {
			exit( Rc::BadState
			,	"both ",store_dir_s,rm_slash," and ",bck_store_dir_s,rm_slash," exist, consider one of :",'\n'
			,	"\trm -r ",store_dir_s    ,rm_slash                                                      ,'\n'
			,	"\trm -r ",bck_store_dir_s,rm_slash
			) ;
		}
		//
		if ( !AcFd( lcl_repair_mrkr , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.err_ok=true} ) ) exit(Rc::System,"cannot create ",repair_mrkr) ; // create marker
	}
	try                       { g_config = New ;                                                                             }
	catch (::string const& e) { exit( Rc::Usage , "while configuring ",*g_exe_name," in dir ",top_dir_s,rm_slash," : ",e ) ; }
	daemon_cache_init( false/*rescue*/, cmd_line.flags[Flag::DryRun] ) ;                                                                           // no need to rescue as store is fresh
	 if (!cmd_line.flags[Flag::DryRun]) {
	 	::string msg ;
		msg << "the repair process is starting, if something goes wrong :"                                                                                          <<'\n' ;
		msg << "to restore old state,                    consider : rm -rf "<<store_dir_s<<rm_slash<<" ; mv "<<bck_store_dir_s<<rm_slash<<' '<<store_dir_s<<rm_slash<<'\n' ;
		msg << "to restart the repair process,           consider : "<<*g_exe_name                                                                                  <<'\n' ;
		msg << "to continue with what has been repaired, consider : rm "<<repair_mrkr<<" ; rm -r "<<bck_store_dir_s<<rm_slash                                       <<'\n' ;
		Fd::Stdout.write(msg) ;
	}
	//                    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	RepairDigest digest = _repair(cmd_line.flags[Flag::DryRun]) ;
	//                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	unlnk(lcl_repair_mrkr) ;
	 if (!cmd_line.flags[Flag::DryRun]) {
	 	::string msg ;
		msg <<                                                                                                                                                                         '\n' ;
		msg <<                                                                                                                                                                         '\n' ;
		msg << "repo has been satisfactorily repaired : "<<digest.n_repaired<<'/'<<digest.n_processed<<" jobs"                                                                       <<'\n' ;
		msg <<                                                                                                                                                                         '\n' ;
		msg << "to restore old state,                                      consider : rm -r "<<store_dir_s<<rm_slash<<" ; mv "<<bck_store_dir_s<<rm_slash<<' '<<store_dir_s<<rm_slash<<'\n' ;
		msg << "to restart the repair process,                             consider : rm -r "<<store_dir_s<<rm_slash<<" ; "<<*g_exe_name                                             <<'\n' ;
		msg << "to clean up after having ensured everything runs smoothly, consider : rm -r "<<bck_store_dir_s<<rm_slash                                                             <<'\n' ;
		Fd::Stdout.write(msg) ;
	}
	exit(Rc::Ok) ;
}
