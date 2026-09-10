// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "lmake_server/core.hh" // /!\ must be first to include Python.h first

#include <iostream> // exceptional use of iostream to prompt user

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

enum class Flag : uint8_t {
	DryRun
,	Force
} ;

enum class FileKind : uint8_t {
	Data
,	Info
} ;

struct RunEntry {
	// services
	bool operator<(RunEntry const& other) const { return ::tuple(job,last_access,name) < ::tuple(other.job,other.last_access,other.name) ; } // oldest first within a job so as to rebuild LRU
	// data
	::string         name        ;                    // <job>/<key>-first or <job>/<key>-last
	::string         job         ;
	BitMap<FileKind> files       ;
	bool             is_last     = false/*garbage*/ ;
	CkeyIdx          key         = 0    /*.      */ ; // key idx as found on disk, may be different from the one in the rebuilt store
	Pdate            last_access ;
} ;

struct DryRunDigest {
	::umap<CkeyIdx,::string> keys        ;     // map keys to repo
	::vector<RunEntry>       runs        ;     // recognized runs, sorted by job, then by last_access
	::vmap_ss                to_rm       ;     // map files to reasons
	CrunIdx                  n_repaired  = 0 ;
	CrunIdx                  n_processed = 0 ;
} ;

::string g_repo_keys_file = cat(PrivateAdminDirS,"repo_keys") ;

static DryRunDigest _dry_run() {
	Trace trace("_dry_run") ;
	DryRunDigest res ;
	//
	for( ::string const& line : AcFd(g_repo_keys_file,{.err_ok=true}).read_lines() ) {
		size_t pos = line.find(' ') ;
		try                     { res.keys[from_string<CkeyIdx>(line.substr(0,pos))] = line.substr(pos+1) ; }
		catch (::string const&) { trace("bad_repo_key",line) ;                                              }
	}
	//
	::string reserved_s = cat(PrivateAdminDirS,"reserved/") ;
	if (FileInfo(reserved_s).exists()) res.to_rm.emplace_back(reserved_s,"reserved dir") ;
	//
	::string           admin_dir = cat("./",AdminDirS,rm_slash)                                                                                 ;
	::vmap_s<FileTag>  files     = walk( Fd::Cwd , ~FileTags() , {}/*pfx*/ , [&](::string const& f) { return f.starts_with(admin_dir) ; } ) ; ::sort(files) ;
	::umap_s<RunEntry> runs      ;
	::vector_s         dirs      ;                                                                                                           // all dirs, sorted
	::vmap_ss          bad_files ;                                                                                                           // map files to reasons
	for( auto& [file,tag] : files ) {
		if (!file) continue ;                                                                                                                // top-level dir
		SWEAR(file[0]=='/') ; file = file.substr(1/* / */) ;
		if ( with_slash(file)==AdminDirS ) continue ;                                                                                        // admin dir is pruned, but is listed itself
		if ( tag==FileTag::Dir           ) { dirs.push_back(with_slash(file)) ; continue ; }
		try {
			throw_unless( tag>=FileTag::Reg , "not a regular file" ) ;
			::string run = file    ;
			FileKind fk  ;
			if      (run.ends_with("-data") ) { run.resize(run.size()-5/*-data*/) ; fk = FileKind::Data ; }
			else if (run.ends_with("-info") ) { run.resize(run.size()-5/*-info*/) ; fk = FileKind::Info ; }
			else                                throw "unrecognized data/info suffix"s ;
			size_t dash    ;
			bool   is_last ;
			if      (run.ends_with("-first")) { is_last = false ; dash = run.size()-6/*-first*/ ; }
			else if (run.ends_with("-last" )) { is_last = true  ; dash = run.size()-5/*-last */ ; }
			else                                throw "unrecognized first/last suffix"s ;
			size_t  slash  = run.rfind('/',dash-1) ; throw_unless( slash!=Npos && slash>0 , "no job dir" ) ;
			CkeyIdx key    ;
			try                     { key = from_string<CkeyIdx>(substr_view(run,slash+1,dash-slash-1)) ; }
			catch (::string const&) { throw "unrecognized key"s ;                                         }
			if (!res.keys.contains(key)) throw "unrecognized repo"s ;
			RunEntry& entry = runs[run] ;
			entry.name     = run                 ;
			entry.job      = run.substr(0,slash) ;
			entry.files   |= fk                  ;
			entry.is_last  = is_last             ;
			entry.key      = key                 ;
		} catch (::string const& e) {
			bad_files.emplace_back(file,e) ;
		}
	}
	::sort(dirs) ;
	for( auto& [run,entry] : runs ) {
		::string info_file = run+"-info" ;
		::string data_file = run+"-data" ;
		res.n_processed++ ;
		try {
			throw_unless( entry.files[FileKind::Data] , "no accompanying data" ) ;
			throw_unless( entry.files[FileKind::Info] , "no accompanying info" ) ;
			::string job_info_str = AcFd(info_file).read()                ;
			JobInfo  job_info     = deserialize<JobInfo>(job_info_str)    ;
			job_info.chk(true/*for_cache*/) ;
			throw_unless( is_ok(job_info.end.digest.status)==Yes , "bad status" ) ;
			FileStat data_stat ;
			throw_unless( ::lstat(data_file.c_str(),&data_stat)==0           , "cannot stat data" ) ;
			throw_unless( DiskSz(data_stat.st_size)>=job_info.end.total_z_sz , "truncated data"   ) ; // data file contains target sizes + compressed targets
			entry.last_access = Pdate(data_stat.st_atim) ;
		} catch (::string const& e) {
			if (entry.files[FileKind::Info]) bad_files.emplace_back(info_file,e) ;
			if (entry.files[FileKind::Data]) bad_files.emplace_back(data_file,e) ;
			continue ;
		}
		res.n_repaired++ ;
		res.runs.push_back(::move(entry)) ;
	}
	::sort(res.runs) ;
	// dirs containing no kept run (recursively) are useless, and would prevent job dir removal upon victimization
	::uset_s keep_dirs_s ;
	for( RunEntry const& entry : res.runs )
		for( ::string d_s=dir_name_s(entry.name) ; +d_s ; d_s=dir_name_s(d_s) )
			if (!keep_dirs_s.insert(d_s).second) break ;
	::string last_rm_dir_s ;
	for( ::string const& d_s : dirs ) {
		if ( keep_dirs_s.contains(d_s)                        ) continue ;
		if ( +last_rm_dir_s && d_s.starts_with(last_rm_dir_s) ) continue ; // already removed as part of last_rm_dir_s
		res.to_rm.emplace_back( d_s , "no run" ) ;
		last_rm_dir_s = d_s ;
	}
	for( auto& [file,reason] : bad_files ) res.to_rm.emplace_back(file,reason) ; // report all files with their reason, even if already removed as part of a dir
	return res ;
}

static CrunIdx/*n_conflicts*/ _repair(DryRunDigest const& dry_run) {
	Trace trace("_repair") ;
	SyncGuard          sync_guard  { g_file_sync }                          ;
	::string           reserved_s  = cat(PrivateAdminDirS,"reserved/")      ;
	::umap<CkeyIdx,Ckey> keys      ;                                          // map old keys to new keys
	::string           keys_str    ;
	CacheUploadKey     n_reserved  = 0                                      ;
	CrunIdx            n_conflicts = 0                                      ;
	//
	mk_dir_s(reserved_s) ;
	for( RunEntry const& entry : dry_run.runs )                               // create keys in a deterministic order (sorted runs), as new key idx's may differ from old ones
		if (keys.try_emplace( entry.key , New , dry_run.keys.at(entry.key) ).second) keys_str << +keys.at(entry.key)<<' '<<dry_run.keys.at(entry.key)<<'\n' ;
	//
	for( size_t i=0 ; i<dry_run.runs.size() ;) {
		size_t   start = i                    ;
		::string job   = dry_run.runs[i].job  ;
		// move all runs of a job whose name changes out of the way before inserting any of them, as their new names may collide with old names of other runs
		::vector<::pair_ss> reserved_files ;                                  // (new_name,reserved_file) for each run of job
		for( ; i<dry_run.runs.size() && dry_run.runs[i].job==job ; i++ ) {
			RunEntry const& entry    = dry_run.runs[i]                                    ;
			::string        new_name = run_file( job , +keys.at(entry.key) , entry.is_last ) ;
			::string        reserved ;
			if (new_name!=entry.name) {
				reserved = reserved_file(++n_reserved) ;
				rename_run( entry.name , reserved , &sync_guard ) ;
			}
			reserved_files.emplace_back( ::move(new_name) , ::move(reserved) ) ;
		}
		for( size_t j : iota(start,i) ) {
			RunEntry const& entry    = dry_run.runs[j]          ;
			::string const& reserved = reserved_files[j-start].second ;
			::string const& src      = +reserved ? reserved : entry.name ;
			try {
				::string      job_info_str = AcFd(src+"-info").read()                                                               ;
				JobInfo       job_info     = deserialize<JobInfo>(job_info_str)                                                     ;
				CompileDigest deps         { mk_vmap<StrId<CnodeIdx>,DepDigest>(job_info.end.digest.deps) , false/*for_download*/ } ;
				DiskSz        sz           = run_sz( job_info.end.total_z_sz , job_info_str.size() , deps )                         ;
				Cjob          cjob         { New , job , deps.n_statics }                                                           ;
				//
				bool done = cjob->insert(
					deps                                                                                                                                             // to search entry
				,	keys.at(entry.key) , entry.is_last?KeyIsLast::Yes:KeyIsLast::No , entry.last_access , sz , to_rate(g_cache_config,sz,job_info.end.digest.exe_time) // to create entry
				,	false/*force*/ , job_info.end.digest.targets_crc
				,	reserved , &sync_guard                                                                                                                             // reserved file is renamed (or unlinked if not done)
				) ;
				if (done) { trace("done",entry.name,reserved_files[j-start].first) ; continue ; }
				trace("conflict",entry.name) ;
				Fd::Stdout.write(cat("rm ",mk_shell_str(entry.name),"-{data,info} # conflicts with a better entry\n")) ;
				if (!reserved) unlnk_run( entry.name , &sync_guard ) ;                                                                                                 // not unlinked by insert in that case
			} catch (::string const& e) {
				trace("throw",entry.name,e) ;
				Fd::Stdout.write(cat("rm ",mk_shell_str(entry.name),"-{data,info} # ",e,'\n')) ;
				unlnk_run( src , &sync_guard ) ;
			}
			n_conflicts++ ;
		}
	}
	try                       { rmdir_s(reserved_s) ;                                                    } // all reserved files must have been renamed or unlinked
	catch (::string const& e) { Fd::Stderr.write(cat("cannot clean up ",reserved_s,rm_slash," : ",e,'\n')) ; }
	//
	AcFd(g_repo_keys_file,{O_WRONLY|O_TRUNC|O_CREAT}).write(keys_str) ;
	cache_empty_trash() ;
	cache_finalize   () ;
	return n_conflicts ;
}

int main( int argc , char* argv[] ) {
	Syntax<Flag> syntax {{
		{ Flag::DryRun , { .short_name='n' , .doc="report actions but dont execute them" } }
	,	{ Flag::Force  , { .short_name='f' , .doc="execute actions without confirmation" } }
	}} ;
	CmdLine<Flag> cmd_line { syntax,argc,argv } ;
	if ( cmd_line.args.size()>1                                 ) syntax.usage("cannot repair several cache dirs")                                                     ;
	if ( +cmd_line.args && ::chdir(cmd_line.args[0].c_str())!=0 ) exit( Rc::System   , "cannot chdir (",StrErr(),") to ",cmd_line.args[0]                            ) ;
	if ( FileInfo(File(ServerMrkr)).exists()                    ) exit( Rc::BadState , "after having ensured no lcache_server is running, consider : rm ",ServerMrkr ) ;
	//
	FileStat st ; if (::lstat(".",&st)!=0) FAIL_PROD() ; SWEAR( S_ISDIR(st.st_mode) ) ;
	::umask(~st.st_mode&0777) ;                                                         // ensure permissions on top-level dir are propagated to all underlying dirs and files
	//
	app_init({
		.cd_root      = false                                                           // we have already chdir'ed to top
	,	.chk_version  = Yes
	,	.key          = "cache dir"
	,	.clean_msg    = cache_clean_msg()
	,	.read_only_ok = cmd_line.flags[Flag::DryRun]
	,	.root_mrkrs   = { cat(AdminDirS,"config.py") }
	,	.version      = Version::Cache
	}) ;
	Py::init(*g_lmake_root_s) ;
	//
	//                 vvvvvvvvvv
	DryRunDigest drd = _dry_run() ;
	//                 ^^^^^^^^^^
	size_t wd = ::max<size_t>( drd.to_rm , [](::pair_ss const& d_r) { return  is_dir_name(d_r.first) ? mk_shell_str(no_slash(d_r.first)).size() : 0 ; } ) ;
	size_t wf = ::max<size_t>( drd.to_rm , [](::pair_ss const& f_r) { return !is_dir_name(f_r.first) ? mk_shell_str(         f_r.first ).size() : 0 ; } ) ;
	//
	for( auto const& [file,reason] : drd.to_rm ) if ( is_dir_name(file)) Fd::Stdout.write(cat("rm -r ",widen(mk_shell_str(no_slash(file)),wd)," # ",reason,'\n')) ;
	/**/                                         if ( wd && wf         ) Fd::Stdout.write(                                                                 "\n" ) ;
	for( auto const& [file,reason] : drd.to_rm ) if (!is_dir_name(file)) Fd::Stdout.write(cat("rm "   ,widen(mk_shell_str(         file ),wf)," # ",reason,'\n')) ;
	/**/                                                                 Fd::Stdout.write(                                                                 "\n" ) ;
	//
	if (drd.n_repaired==drd.n_processed) Fd::Stdout.write(cat("update book-keeping to reflect all of "                        ,drd.n_processed," kept jobs\n"     )) ;
	else                                 Fd::Stdout.write(cat("update book-keeping to reflect ",drd.n_repaired," kept out of ",drd.n_processed," processed jobs\n")) ;
	//
	if ( cmd_line.flags[Flag::DryRun]) exit(Rc::Ok) ;
	if (!cmd_line.flags[Flag::Force ]) {
		for(;;) {
			::string user_reply ;
			std::cout << "continue [y/n] ? " ;
			std::getline( std::cin , user_reply ) ;
			if (user_reply=="n") exit(Rc::Ok) ;
			if (user_reply=="y") break        ;
		}
	}
	//
	for( auto const& [file,reason] : drd.to_rm ) unlnk( file          , {.abs_ok=true,.dir_ok=is_dir_name(file)} ) ;
	/**/                                         unlnk( g_store_dir_s , {             .dir_ok=true             } ) ;
	cache_init(false/*rescue*/) ;
	//                    vvvvvvvvvvvv
	CrunIdx n_conflicts = _repair(drd) ;
	//                    ^^^^^^^^^^^^
	if (n_conflicts) Fd::Stdout.write(cat("\n",n_conflicts," jobs were finally not kept\n")) ;
	//
	exit(Rc::Ok) ;
}
