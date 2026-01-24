// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "lmake_server/core.hh" // /!\ must be first to include Python.h first

#include "app.hh"
#include "disk.hh"

#include "cache_utils.hh"
#include "engine.hh"
#include "rpc_cache.hh"

using namespace Cache  ;
using namespace Disk   ;
using namespace Engine ;
using namespace Hash   ;
using namespace Time   ;

enum class Key  : uint8_t { None   } ;
enum class Flag : uint8_t { DryRun } ;

struct RepairDigest {
	CjobIdx n_repaired  = 0 ;
	CjobIdx n_processed = 0 ;
} ;

static RepairDigest _repair(bool dry_run) {
	struct RunEntry {
		bool info = false ;
		bool data = false ;
	} ;
	Trace trace("_repair",STR(dry_run)) ;
	RepairDigest             res            ;
	AcFd                     repaired_runs  ; if (!dry_run) repaired_runs = AcFd{ cat(AdminDirS,"repaired_runs") , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} } ;
	::umap<CkeyIdx,::string> old_keys       ;
	::map <CkeyIdx,::string> new_keys       ;                                     // ordered to generate a nicer repo_keys file
	::string                 repo_keys_file = cat(PrivateAdminDirS,"repo_keys") ;
	for( ::string const& line : AcFd(repo_keys_file,{.err_ok=true}).read_lines() ) {
		size_t pos = line.find(' ') ;
		old_keys[from_string<CkeyIdx>(line.substr(0,pos))] = line.substr(pos+1) ;
	}
	//
	::umap_s<RunEntry> tab       ;
	::uset_s           to_rm     ;
	::string           admin_dir = cat("./",AdminDirS,rm_slash) ;
	for( auto const& [file,_] : walk( Fd::Cwd , FileTag::Reg , {}/*pfx*/ , [&](::string const& f) { return f.starts_with(admin_dir) ; } ) ) {
		SWEAR(file[0]=='/') ;
		::string f = file.substr(1) ;
		if (f.ends_with("-info")) { tab[f.substr(0,f.size()-5)].info = true ; continue ; }
		if (f.ends_with("-data")) { tab[f.substr(0,f.size()-5)].data = true ; continue ; }
		to_rm.insert(f) ;
	}
	for( auto const& [run,e] : tab ) {
		::string info_file = run+"-info" ;
		::string data_file = run+"-data" ;
		res.n_processed++ ;
		if (!( e.info && e.data )) {
			if (e.info) to_rm.insert(info_file) ;
			if (e.data) to_rm.insert(data_file) ;
			continue ;
		}
		try {
			::string job_info_str = AcFd(info_file).read()             ;
			JobInfo  job_info     = deserialize<JobInfo>(job_info_str) ;
			FileStat data_stat ;                                         ::lstat( data_file.c_str() , &data_stat ) ;
			job_info.chk(true/*for_cache*/) ;
			throw_unless( job_info.end.digest.status==Status::Ok , "bad status" ) ;
			//
			::string old_key_str = base_name(run) ;
			bool     key_is_last ;
			if      (old_key_str.ends_with("-first")) key_is_last = false ;
			else if (old_key_str.ends_with("-last" )) key_is_last = true  ;
			else                                      throw cat("unexpected run entry",run,rm_slash) ;
			old_key_str = old_key_str.substr( 0 , old_key_str.rfind('-') ) ;
			//
			if (!dry_run) {
				CompileDigest deps    { mk_vmap<StrId<CnodeIdx>,DepDigest>(job_info.end.digest.deps) , false/*for_download*/ } ;
				DiskSz        sz      = run_sz( job_info.end.total_z_sz , job_info_str.size() , deps )                         ;
				CkeyIdx       old_key = from_string<CkeyIdx>(old_key_str)                                                      ;
				auto          it      = old_keys.find(old_key)                                                                 ;
				::string      repo    = cat("repaired-",old_key)                                                               ;                                 // if key is unknown, invent a repo
				Ckey          new_key { New , it==old_keys.end()?repo:it->second         }                                     ;                                 // .
				Cjob          job     { New , no_slash(dir_name_s(run)) , deps.n_statics }                                     ;
				//
				if (!new_key->ref_cnt) new_keys[+new_key] = repo ;                                                                                               // record new association
				::pair<Crun,CacheHitInfo> digest = job->insert(
					deps.deps , deps.dep_crcs                                                                                                                    // to search entry
				,	new_key , key_is_last?KeyIsLast::Yes:KeyIsLast::No , Pdate(data_stat.st_atim) , sz , to_rate(g_cache_config,sz,job_info.end.digest.exe_time) // to create entry
				) ;
				throw_unless( digest.second>=CacheHitInfo::Miss , "conflict" ) ;
			}
		} catch (::string const&) {
			to_rm.insert(info_file) ;
			to_rm.insert(data_file) ;
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
	try { rename( repo_keys_file , repo_keys_file+".bck" ) ; } catch (::string const&) {}                                                                        // no harm if file did not exist
	AcFd repo_keys_fd { repo_keys_file , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.perm_ext=g_cache_config.perm_ext} } ;
	for( auto const& [k,r] : new_keys ) repo_keys_fd.write(cat(+k,' ',r,'\n')) ;
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
	if ( FileInfo(File(ServerMrkr)).exists() ) exit(Rc::BadState,"after having ensured no lcache_server is running, consider : rm ",ServerMrkr) ;
	//
	::string const& top_dir_s = with_slash(cmd_line.args[0]) ;
	if (::chdir(top_dir_s.c_str())!=0) exit(Rc::System  ,"cannot chdir (",StrErr(),") to ",top_dir_s,rm_slash ) ;
	//
	app_init({
		.cd_root      = false                                                                                       // we have already chdir'ed to top
	,	.chk_version  = Yes
	,	.clean_msg    = cache_clean_msg()
	,	.read_only_ok = cmd_line.flags[Flag::DryRun]
	,	.root_mrkrs   = { cat(AdminDirS,"config.py") }
	,	.version      = Version::Cache
	}) ;
	Py::init(*g_lmake_root_s) ;
	//
	::string lcl_repair_mrkr     = cat(AdminDirS,"repairing")      ;
	::string lcl_store_dir_s     = store_dir_s(               )    ;
	::string lcl_bck_store_dir_s = store_dir_s(true/*for_bck*/)    ;
	::string repair_mrkr         = top_dir_s + lcl_repair_mrkr     ;
	::string store_dir_s         = top_dir_s + lcl_store_dir_s     ;
	::string bck_store_dir_s     = top_dir_s + lcl_bck_store_dir_s ;
	//
	if (!cmd_line.flags[Flag::DryRun]) {
		if (FileInfo(lcl_repair_mrkr).tag()>=FileTag::Reg) unlnk( bck_store_dir_s , {.dir_ok=true,.abs_ok=true} ) ; // if last lcache_repair was interrupted, reset unfinished state
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
	cache_init( false/*rescue*/, cmd_line.flags[Flag::DryRun] ) ;                                                                                  // no need to rescue as store is fresh
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
