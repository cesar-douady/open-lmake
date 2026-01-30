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

enum class Key  : uint8_t { None } ;
enum class Flag : uint8_t {
	DryRun
,	Force
} ;

enum class FileKind : uint8_t {
	Data
,	Info
} ;

struct RunEntry {
	BitMap<FileKind> files   ;
	bool             is_last = false/*garbage*/ ;
	CkeyIdx          key     ;
} ;

struct DryRunDigest {
	::umap<CkeyIdx,::string> keys        ;     // map keys to repo
	::umap_s<RunEntry>       runs        ;     // repaired jobs
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
		res.keys[from_string<CkeyIdx>(line.substr(0,pos))] = line.substr(pos+1) ;
	}
	//
	:: string reserved_s = cat(PrivateAdminDirS,"reserved/") ;
	if (+FileInfo(reserved_s)) res.to_rm.emplace_back(reserved_s,"reserved dir") ;
	//
	::string          admin_dir = cat("./",AdminDirS,rm_slash)                                                                              ;
	::vmap_s<FileTag> files     = walk( Fd::Cwd , FileTag::Reg , {}/*pfx*/ , [&](::string const& f) { return f.starts_with(admin_dir) ; } ) ; ::sort(files) ;
	for( auto& [file,_] : files ) {
		try {
			SWEAR(file[0]=='/') ; file = file.substr(1/* / */) ;
			FileKind fk ;
			if      (file.ends_with("-data") ) { file.resize(file.size()-5/*-data*/) ; fk = FileKind::Data ; }
			else if (file.ends_with("-info") ) { file.resize(file.size()-5/*-info*/) ; fk = FileKind::Info ; }
			else                                 throw "unrecognized data/info suffix"s ;
			size_t dash    ;
			bool   is_last ;
			if      (file.ends_with("-first")) { is_last = false ; dash = file.size()-6/*-first*/ ; }
			else if (file.ends_with("-last" )) { is_last = true  ; dash = file.size()-5/*-last */ ; }
			else                                 throw "unrecognized first/last suffix"s ;
			size_t  slash1 = file.rfind('/',dash-1)+1 ;
			CkeyIdx key    ;
			try                     { key = from_string<CkeyIdx>(substr_view(file,slash1,dash-slash1)) ; }
			catch (::string const&) { throw "unrecognized key"s ;                                        }
			if (!res.keys.contains(key)) throw "unrecognized repo"s ;
			RunEntry& entry = res.runs[::move(file)] ;
			entry.files   |= fk      ;
			entry.is_last  = is_last ;
			entry.key      = key     ;
		} catch (::string const& e) {
			res.to_rm.emplace_back(file,e) ;
		}
	}
	for( auto const& [run,entry] : res.runs ) {
		::string info_file = run+"-info" ;
		::string data_file = run+"-data" ;
		res.n_processed++ ;
		try {
			throw_unless( entry.files[FileKind::Data] , "no accompanying data" ) ;
			throw_unless( entry.files[FileKind::Info] , "no accompanying info" ) ;
			JobInfo job_info = deserialize<JobInfo>(AcFd(info_file).read()) ;
			job_info.chk(true/*for_cache*/) ;
			throw_unless( is_ok(job_info.end.digest.status)==Yes , "bad status" ) ;
		} catch (::string const& e) {
			if (entry.files[FileKind::Info]) res.to_rm.emplace_back(info_file,e) ;
			if (entry.files[FileKind::Data]) res.to_rm.emplace_back(data_file,e) ;
			continue ;
		}
		res.n_repaired++ ;
	}
	return res ;
}

static void _repair(DryRunDigest const& dry_run) {
	Trace trace("_repair") ;
	::umap<CkeyIdx,Ckey> keys ;                                                                                                                        // map old keys to new keys
	for( auto const& [run,entry] : dry_run.runs ) {
		::string      job_info_str = AcFd(run+"-info").read()                                                               ;
		JobInfo       job_info     = deserialize<JobInfo>(job_info_str)                                                     ;
		CompileDigest deps         { mk_vmap<StrId<CnodeIdx>,DepDigest>(job_info.end.digest.deps) , false/*for_download*/ } ;
		DiskSz        sz           = run_sz( job_info.end.total_z_sz , job_info_str.size() , deps )                         ;
		Ckey          key          = keys.try_emplace( entry.key , New , dry_run.keys.at(entry.key) ).first->second         ;
		Cjob          job          { New , no_slash(dir_name_s(run)) , deps.n_statics }                                     ;
		FileStat      data_stat    ;                                                                                          ::lstat( (run+"-data").c_str() , &data_stat ) ;
		//
		::pair<Crun,CacheHitInfo> digest = job->insert(
			deps.deps , deps.dep_crcs                                                                                                                  // to search entry
		,	key , entry.is_last?KeyIsLast::Yes:KeyIsLast::No , Pdate(data_stat.st_atim) , sz , to_rate(g_cache_config,sz,job_info.end.digest.exe_time) // to create entry
		) ;
		throw_unless( digest.second>=CacheHitInfo::Miss , "conflict" ) ;
	}
	::string keys_str ; for( auto const& [old_key,new_key] : keys ) keys_str << +new_key<<' '<<dry_run.keys.at(old_key)<<'\n' ;
	AcFd(g_repo_keys_file,{O_WRONLY|O_TRUNC|O_CREAT}).write(keys_str) ;
}

int main( int argc , char* argv[] ) {
	Syntax<Key,Flag> syntax {{
		{ Flag::DryRun , { .short_name='n' , .doc="report actions but dont execute them" } }
	,	{ Flag::Force  , { .short_name='f' , .doc="execute actions without confirmation" } }
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
	FileStat st ; if (::lstat(".",&st)!=0) FAIL() ; SWEAR( S_ISDIR(st.st_mode) ) ;
	::umask(~st.st_mode) ;                                                         // ensure permissions on top-level dir are propagated to all underlying dirs and files
	//
	app_init({
		.cd_root      = false                                                      // we have already chdir'ed to top
	,	.chk_version  = Yes
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
	for( auto const& [file,reason] : drd.to_rm ) if ( is_dir_name(file)) Fd::Stdout.write(cat("rm -r ",widen(mk_shell_str(no_slash(file)),wd)," # ",reason,'\n')) ;
	/**/                                         if ( wd && wf         ) Fd::Stdout.write(                                                                 "\n" ) ;
	for( auto const& [file,reason] : drd.to_rm ) if (!is_dir_name(file)) Fd::Stdout.write(cat("rm "   ,widen(mk_shell_str(         file ),wf)," # ",reason,'\n')) ;
	Fd::Stdout.write(                                                      "\n" ) ;
	Fd::Stdout.write(cat("repair ",drd.n_repaired,'/',drd.n_processed," jobs\n")) ;
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
	for( auto const& [file,reason] : drd.to_rm ) unlnk( file          , {.dir_ok=is_dir_name(file),.abs_ok=true} ) ;
	/**/                                         unlnk( g_store_dir_s , {.dir_ok=true                          } ) ;
	cache_init(false/*rescue*/) ;
	//vvvvvvvvvv
	_repair(drd) ;
	//^^^^^^^^^^
	//
	exit(Rc::Ok) ;
}
