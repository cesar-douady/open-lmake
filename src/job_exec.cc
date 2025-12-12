// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/utsname.h>

#include "disk.hh"
#include "fd.hh"
#include "hash.hh"
#include "re.hh"
#include "thread.hh"
#include "time.hh"
#include "trace.hh"

#include "repo.hh"
#include "rpc_job.hh"
#include "rpc_job_exec.hh"

#include "autodep/gather.hh"

using namespace Caches ;
using namespace Disk   ;
using namespace Hash   ;
using namespace Re     ;
using namespace Time   ;

::vector<ExecTraceEntry>* g_exec_trace    = nullptr      ;
Gather                    g_gather        ;
JobIdx                    g_job           = 0/*garbage*/ ;
SeqId                     g_seq_id        = 0/*garbage*/ ;
ServerSockFd              g_server_fd     ;
KeyedService              g_service_start ;
KeyedService              g_service_mngt  ;
KeyedService              g_service_end   ;
JobStartRpcReply          g_start_info    ;

JobStartRpcReply get_start_info() {
	g_server_fd = { 0/*backlog*/ } ;                                       // server socket must be listening before connecting to server and last to the very end to ensure we can handle heartbeats
	//
	Bool3        found_server = No                    ;                    // for trace only
	KeyedService service      = g_server_fd.service() ;
	JobStartRpcReply res          ;
	Trace trace("get_start_info",g_service_start,service) ;
	try {
		ClientSockFd fd { g_service_start } ;
		g_service_mngt.addr = g_service_end.addr = fd.addr(true/*peer*/) ; // server address is only passed to g_service_start
		fd.set_timeout(Delay(100)) ;                                       // ensure we dont stay stuck in case server is in the coma : 100s = 1000 simultaneous connections @ 10 jobs/s
		throw_unless(+fd) ; //!      vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		found_server = Maybe ; /**/  OMsgBuf( JobStartRpcReq({g_seq_id,g_job},service) ).send                     ( fd                          ) ;
		found_server = Yes   ; res = IMsgBuf(                                          ).receive<JobStartRpcReply>( fd , No/*once*/ , {}/*key*/ ) ; // read without limit as there is a single message
	} catch (::string const& e) { //!^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		trace("no_start_info",STR(found_server),e) ;
		if (+e) exit(Rc::Fail,"while connecting to server : ",e             ) ; // this may be a server config problem, better to report if verbose
		else    exit(Rc::Fail,"cannot connect to server at ",g_service_start) ; // .
	}
	if (+g_lmake_root_s) {
		if (+res.phy_lmake_root_s) res.chk_lmake_root   = true            ;     // if using a different lmake, we must check compatibility
		else                       res.phy_lmake_root_s = *g_lmake_root_s ;
	}
	g_exec_trace->emplace_back( New/*date*/ , Comment::StartInfo , CommentExt::Reply ) ;
	trace(res) ;
	return res ;
}

// /!\ must stay in sync with _lib/lmake/rules.src.py:_get_os_info
::string get_os_info( RealPath& real_path , ::string const& file={} ) {
	Trace trace("get_os_info",file) ;
	::string res ;
	if (+file) {
		Pdate                 now { New }                 ;
		RealPath::SolveReport sr  = real_path.solve(file) ;
		for( ::string& l : sr.lnks )   g_gather.new_access( now , ::move(l      ) , {.accesses=Access::Lnk} , FileInfo(l      ) , Comment::OsInfo,CommentExt::Lnk  ) ;
		if (sr.file_loc<=FileLoc::Dep) g_gather.new_access( now , ::move(sr.real) , {.accesses=Access::Reg} , FileInfo(sr.real) , Comment::OsInfo,CommentExt::Read ) ;
		//
		try                     { res = AcFd(file).read() ; }
		catch (::string const&) {                         ; }                                                                  // report empty in case of error
	} else {
		::string         id             ;
		::string         version_id     ;
		struct ::utsname uname_info     ; if (::uname(&uname_info)!=0) uname_info.machine[0] = 0 ;                             // .
		::string         etc_os_release ; try { etc_os_release = AcFd("/etc/os-release").read() ; } catch (::string const&) {} // .
		for( ::string const& line : split(etc_os_release,'\n') ) {
			size_t   pos = line.find('=')            ; if (pos==Npos) continue ;
			::string key = strip(line.substr(0,pos)) ;
			switch (key[0]) {
				case 'I' : if (key=="ID"        ) id         = line.substr(pos+1) ; break ;
				case 'V' : if (key=="VERSION_ID") version_id = line.substr(pos+1) ; break ;
			DN}
		}
		res = cat(id,'/',version_id,'/',uname_info.machine) ;
	}
	trace("done",res) ;
	return res ;
}

::string g_to_unlnk ;                                                                                                   // XXX/ : suppress when CentOS7 bug is fixed
::vector_s cmd_line( ::map_ss const& cmd_env , ::string const& repo_root_s ) {
	static const size_t ArgMax = ::sysconf(_SC_ARG_MAX) ;
	if (g_start_info.use_script) {
		// XXX/ : fix the bug with CentOS7 where the write seems not to be seen and old script is executed instead of new one
	//	::string cmd_file = cat(PrivateAdminDirS,"cmds/",g_start_info.small_id) ;                                       // correct code
		::string cmd_file = cat(PrivateAdminDirS,"cmds/",g_seq_id) ;
		AcFd( cmd_file , {O_WRONLY|O_TRUNC|O_CREAT,0666/*mod*/} ).write( g_start_info.cmd ) ;
		::vector_s res = ::move(g_start_info.interpreter) ; res.reserve(res.size()+1) ;                                 // avoid copying as interpreter is used only here
		res.push_back(mk_glb(cmd_file,repo_root_s)) ;                                                                   // provide absolute script so as to support cwd
		g_to_unlnk = ::move(cmd_file) ;                                                                                 // XXX/ : suppress when CentOS7 bug is fixed
		Trace("cmd_line","use_script",res) ;
		return res ;
	} else {
		// large commands are forced use_script=true in server
		SWEAR( g_start_info.cmd.size()<=ArgMax/2 , g_start_info.cmd.size() ) ;                                          // env+cmd line must not be larger than ARG_MAX, keep some margin for env
		bool is_simple = mk_simple_cmd_line( /*inout*/g_start_info.interpreter , ::move(g_start_info.cmd) , cmd_env ) ; // interpreter becomes full cmd line
		Trace("cmd_line",STR(is_simple),g_start_info.interpreter) ;
		return ::move(g_start_info.interpreter) ;
	}
}

void crc_thread_func( size_t id , ::vmap_s<TargetDigest>* tgts , ::vector<NodeIdx> const* crcs , ::string* msg , Mutex<MutexLvl::JobExec>* msg_mutex , ::vector<FileInfo>* target_fis , size_t* sz ) {
	static Atomic<NodeIdx> crc_idx = 0 ;
	t_thread_key = '0'+id ;
	Trace trace("crc_thread_func",tgts->size(),crcs->size()) ;
	NodeIdx cnt = 0 ;                                                      // cnt is for trace only
	*sz = 0 ;
	for( NodeIdx ci=0 ; (ci=crc_idx++)<crcs->size() ; cnt++ ) {
		NodeIdx                 ti     = (*crcs)[ci] ;
		::pair_s<TargetDigest>& e      = (*tgts)[ti] ;
		Pdate                   before = New         ;
		FileInfo                fi     ;
		try {
			//             vvvvvvvvvvvvvvvvvvvvvvvvvv
			e.second.crc = Crc( e.first , /*out*/fi ) ;
			//             ^^^^^^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {                                      // START_OF_NO_COV defensive programming
			Lock lock{*msg_mutex} ;
			*msg <<add_nl<< "while computing checksum for "<<e<<" : "<<e ;
		}                                                                  // END_OF_NO_COV
		e.second.sig       = fi.sig() ;
		(*target_fis)[ti]  = fi       ;
		*sz               += fi.sz    ;
		trace("crc_date",ci,before,Pdate(New)-before,e.second.crc,fi,e.first) ;
		if (!e.second.crc.valid()) {
			Lock lock{*msg_mutex} ;
			*msg <<add_nl<< "cannot compute checksum for "<<e.first ;
		}
	}
	trace("done",cnt) ;
}

::string/*msg*/ compute_crcs( Gather::Digest& digest , ::vector<FileInfo>&/*out*/ target_fis , size_t&/*out*/ total_sz ) {
	size_t                            n_threads = thread::hardware_concurrency() ;
	if (n_threads<1                 ) n_threads = 1                              ;
	if (n_threads>8                 ) n_threads = 8                              ;
	if (n_threads>digest.crcs.size()) n_threads = digest.crcs.size()             ;
	//
	Trace trace("compute_crcs",digest.crcs.size(),n_threads) ;
	::string                 msg       ;
	Mutex<MutexLvl::JobExec> msg_mutex ;
	::vector<size_t>         szs       ( n_threads ) ;
	target_fis.resize(digest.targets.size()) ;
	{	::vector<::jthread> crc_threads ; crc_threads.reserve(n_threads) ;
		for( size_t i  : iota(n_threads) ) crc_threads.emplace_back( crc_thread_func , i , &digest.targets , &digest.crcs , &msg , &msg_mutex , &target_fis , &szs[i] ) ;
	}
	total_sz = 0 ;
	for( size_t s : szs ) total_sz += s ;
	g_exec_trace->emplace_back( New/*date*/ , Comment::ComputedCrcs ) ;
	return msg ;
}

int main( int argc , char* argv[] ) {
	Pdate    start_overhead  { New }        ;
	uint64_t upload_key      = 0            ;                              // key used to identify temporary data uploaded to the cache
	::string phy_repo_root_s ;
	SeqId    trace_id        = 0/*garbage*/ ;
	//
	swear_prod(argc==8,argc) ;                                             // syntax is : job_exec server:port/*start*/ server:port/*mngt*/ server:port/*end*/ seq_id job_idx repo_root trace_file
	try { g_service_start = {                   argv[1],true/*name_ok*/} ; } catch (::string const& e) { exit(Rc::Fail,"cannot connect to server : ",e) ; }
	/**/  g_service_mngt  = {                   argv[2]}                 ;
	/**/  g_service_end   = {                   argv[3]}                 ;
	/**/  g_seq_id        = from_string<SeqId >(argv[4])                 ;
	/**/  g_job           = from_string<JobIdx>(argv[5])                 ;
	/**/  phy_repo_root_s =                     argv[6]                  ; // passed early so we can chdir and trace early
	/**/  trace_id        = from_string<SeqId >(argv[7])                 ;
	//
	g_trace_file = new ::string{cat(phy_repo_root_s,PrivateAdminDirS,"trace/job_exec/",trace_id)} ;
	//
	JobEndRpcReq end_report { {g_seq_id,g_job} } ;
	end_report.digest   = { .status=Status::EarlyErr } ;                   // prepare to return an error, so we can goto End anytime
	end_report.wstatus  = 255<<8                       ;                   // prepare to return an error, so we can goto End anytime
	end_report.end_date = start_overhead               ;
	g_exec_trace        = &end_report.exec_trace       ;
	g_exec_trace->emplace_back( start_overhead , Comment::StartOverhead ) ;
	//
	if (::chdir(phy_repo_root_s.c_str())!=0) {                                                               // START_OF_NO_COV defensive programming
		g_start_info = get_start_info() ; if (!g_start_info) return 0 ;                                      // if !g_start_info, server ask us to give up
		end_report.msg_stderr.msg << "cannot chdir to root : "<<phy_repo_root_s<<rm_slash ;
		goto End ;
	}                                                                                                        // END_OF_NO_COV
	g_exec_trace->emplace_back( New/*date*/ , Comment::chdir , CommentExts() , no_slash(phy_repo_root_s) ) ;
	Trace::s_sz = 10<<20 ;                                                                                   // this is more than enough
	block_sigs({SIGCHLD}) ;                                                                                  // necessary to capture it using signalfd
	repo_app_init({ .chk_version=No , .trace=Yes }) ;                                                        // dont check version for perf, but trace nevertheless
	//
	{	Trace trace("main",Pdate(New),::span<char*>(argv,argc)) ;
		trace("pid",::getpid(),::getpgrp()) ;
		trace("start_overhead",start_overhead) ;
		//
		g_start_info = get_start_info() ; if (!g_start_info) return 0 ;                                      // if !g_start_info, server ask us to give up
		try                       { g_start_info.mk_canon( phy_repo_root_s ) ;  }
		catch (::string const& e) { end_report.msg_stderr.msg += e ; goto End ; }                            // NO_COV defensive programming
		//
		NfsGuard   nfs_guard    { g_start_info.autodep_env.file_sync } ;
		bool       incremental  = false/*garbage*/                     ;
		::vector_s washed_files ;
		//
		try {
			end_report.msg_stderr.msg += with_nl(do_file_actions( /*out*/washed_files , /*out*/incremental , ::move(g_start_info.pre_actions) , &nfs_guard )) ;
		} catch (::string const& e) {                                                                                                                           // START_OF_NO_COV defensive programming
			trace("bad_file_actions",e) ;
			end_report.msg_stderr.msg += e                   ;
			end_report.digest.status   = Status::LateLostErr ;
			goto End ;
		}                                                                                                                                                       // END_OF_NO_COV
		Pdate washed { New } ;
		g_exec_trace->emplace_back( washed , Comment::Washed ) ;
		//
		SWEAR( !end_report.phy_tmp_dir_s , end_report.phy_tmp_dir_s ) ;
		{	auto it = g_start_info.env.begin() ;
			for(; it!=g_start_info.env.end() ; it++ ) if (it->first=="TMPDIR") break ;
			if ( it==g_start_info.env.end() || +it->second ) {                         // if TMPDIR is set and empty, no tmp dir is prepared/cleaned
				if (g_start_info.keep_tmp) {
					end_report.phy_tmp_dir_s << phy_repo_root_s<<AdminDirS<<"tmp/"<<g_job<<'/' ;
				} else {
					// use seq id instead of small id to make tmp dir to ensure that even if user mistakenly record tmp dir name, there no chance of porosity between jobs
					// as with small id, by the time the (bad) old tmp dir is referenced by a new job, it may be in use by another job
					// such a situation cannot occur with seq id
					if      (it==g_start_info.env.end()         ) {}
					else if (!it->second                        ) {}
					else if (it->second!=PassMrkr               ) end_report.phy_tmp_dir_s << it->second       <<add_slash<<g_start_info.key<<'/'<<g_seq_id<<'/' ;
					else if (has_env("TMPDIR",false/*empty_ok*/)) end_report.phy_tmp_dir_s << get_env("TMPDIR")<<add_slash<<g_start_info.key<<'/'<<g_seq_id<<'/' ;
					if      (!end_report.phy_tmp_dir_s          ) end_report.phy_tmp_dir_s << phy_repo_root_s<<AdminDirS<<"auto_tmp/"            <<g_seq_id<<'/' ;
					else if (!is_abs(end_report.phy_tmp_dir_s)) {
						end_report.msg_stderr.msg << "$TMPDIR ("<<end_report.phy_tmp_dir_s<<") must be absolute" ;
						goto End ;
					}
				}
			}
		}
		//
		::map_ss   cmd_env        ;
		::vector_s enter_accesses ;
		::string   repo_root_s    ;
		try {
			bool entered = g_start_info.enter(
				/*out*/  enter_accesses
			,	/*.  */  cmd_env
			,	/*.  */  end_report.dyn_env
			,	/*.  */  g_gather.first_pid
			,	/*.  */  repo_root_s
			,	/*inout*/*g_exec_trace
			,	         phy_repo_root_s
			,	         end_report.phy_tmp_dir_s
			,	         g_seq_id
			) ;
			RealPath real_path { g_start_info.autodep_env } ;
			if (+g_start_info.os_info) {
				RegExpr  os_info_re { g_start_info.os_info }                               ;
				::string os_info    = get_os_info( real_path , g_start_info.os_info_file ) ;
				if (!os_info_re.match(os_info)) {
					trace("os_info_mismatch",g_start_info.os_info) ;
					/**/                            end_report.msg_stderr.msg << "unexpected os_info ("<<os_info<<')'                                                                        ;
					if (+g_start_info.os_info_file) end_report.msg_stderr.msg << " from file "         <<                                                   g_start_info.os_info_file        ;
					/**/                            end_report.msg_stderr.msg << " does not match expected regexpr from "<<g_start_info.rule<<".os_info ("<<g_start_info.os_info<<')' <<'\n' ;
					/**/                            end_report.msg_stderr.msg << "  consider :"                                                                                       <<'\n' ;
					/**/                            end_report.msg_stderr.msg << "  - set "<<g_start_info.rule<<".os_info = r"<<mk_py_str(escape(os_info))                            <<'\n' ;
					/**/                            end_report.msg_stderr.msg << "  - set "<<g_start_info.rule<<".os_info_file"                                                              ;
					goto End ;
				}
				trace("os_info_match",g_start_info.os_info) ;
			}
			if (entered) {
				for( ::string& d_s : enter_accesses ) {
					RealPath::SolveReport sr = real_path.solve(no_slash(::move(d_s)),true/*no_follow*/) ;
					for( ::string& l : sr.lnks ) {
						FileInfo fi { l } ;                                                                                                  // capture before l is moved
						g_gather.new_access( washed , ::move(l) , {.accesses=Access::Lnk} , fi , Comment::mount , CommentExt::Lnk ) ;
					}
					if ( sr.file_loc<=FileLoc::Dep && sr.file_accessed==Yes ) {
						FileInfo fi { sr.real } ;                                                                                            // capture before l is moved
						g_gather.new_access( washed , ::move(sr.real) , {.accesses=Access::Lnk} , fi , Comment::mount , CommentExt::Read ) ;
					}
				}
				g_exec_trace->emplace_back( New/*date*/ , Comment::EnteredNamespace ) ;
			}
			g_start_info.update_env( /*inout*/cmd_env , phy_repo_root_s , end_report.phy_tmp_dir_s , g_seq_id ) ;
		} catch (::string const& e) {
			end_report.msg_stderr.msg += e ;
			goto End ;
		}
		g_start_info.autodep_env.fast_mail        = mail()                                                                  ;                // user@host on which fast_report_pipe works
		g_start_info.autodep_env.fast_report_pipe = cat(repo_root_s,PrivateAdminDirS,"fast_reports/",g_start_info.small_id) ;                // fast_report_pipe is a pipe and only works locally
		g_start_info.autodep_env.views_s          = g_start_info.job_space.flat_phys_s()                                    ;
		trace("prepared",g_start_info.autodep_env) ;
		//
		g_gather.addr             =        g_server_fd.addr(false/*peer*/) ;
		g_gather.as_session       =        true                            ;
		g_gather.autodep_env      = ::move(g_start_info.autodep_env      ) ;
		g_gather.ddate_prec       =        g_start_info.ddate_prec         ;
		g_gather.env              =        &cmd_env                        ;
		g_gather.exec_trace       =        g_exec_trace                    ;
		g_gather.job              =        g_job                           ;
		g_gather.kill_sigs        = ::move(g_start_info.kill_sigs        ) ;
		g_gather.live_out         =        g_start_info.live_out           ;
		g_gather.lmake_root_s     =        g_start_info.phy_lmake_root_s   ;
		g_gather.method           =        g_start_info.method             ;
		g_gather.network_delay    =        g_start_info.network_delay      ;
		g_gather.nice             =        g_start_info.nice               ;
		g_gather.no_tmp           =       !end_report.phy_tmp_dir_s        ;
		g_gather.rule             = ::move(g_start_info.rule             ) ;
		g_gather.seq_id           =        g_seq_id                        ;
		g_gather.server_master_fd = ::move(g_server_fd                   ) ;
		g_gather.service_mngt     =        g_service_mngt                  ;
		g_gather.timeout          =        g_start_info.timeout            ;
		//
		if (!g_start_info.method)                                                                             // if no autodep, consider all static deps are fully accessed as we have no precise report
			for( auto& [d,dd_edf] : g_start_info.deps ) if (dd_edf.first.dflags[Dflag::Static]) {
				DepDigest& dd = dd_edf.first ;
				dd.accesses = FullAccesses ;
				if ( dd.is_crc && !dd.crc().valid() ) dd.set_sig(FileSig(d)) ;
			}
		//
		for( auto& [d,dd_edf] : g_start_info.deps ) {
			DepDigest  & dd       = dd_edf.first          ;
			ExtraDflags& edf      = dd_edf.second         ;
			bool         is_stdin = d==g_start_info.stdin ;
			if (is_stdin) {                                                                                   // stdin is read
				if (!dd.accesses) dd.set_sig(FileInfo(d)) ;                                                   // record now if not previously accessed
				dd.accesses |= Access::Reg ;
			}
			g_gather.new_access( washed , ::move(d) , {.accesses=dd.accesses,.flags{.dflags=dd.dflags,.extra_dflags=edf}} , dd , is_stdin?Comment::Stdin:Comment::StaticDep ) ;
		}
		for( auto& [dt,mf] : g_start_info.static_matches ) {
			if (mf.tflags[Tflag::Target]) {
				g_gather.static_targets.insert(dt) ;
				mf.tflags       &= ~Tflag     ::Target ;
				mf.extra_tflags &= ~ExtraTflag::Allow  ;
			}
			if (mf.extra_tflags[ExtraTflag::Optional]) {                                                      // consider Optional as a star target with a fixed pattern
				if (+mf) g_gather.pattern_flags.emplace_back( Re::escape(dt) , ::pair(washed,mf) ) ;          // fast path : no need to match against a pattern that brings nothing
			} else {
				g_gather.new_access( washed , ::move(dt) , {.flags=mf} , DepInfo() , Comment::StaticMatch ) ; // always insert an entry for static targets, even with no flags
			}
		}
		for( auto& [p ,mf] : g_start_info.star_matches ) {
			if (mf.tflags[Tflag::Target]) {
				g_gather.star_targets.push_back(p) ;                                // XXX : find a way to compile p only once when put in both g_gather.star_targets and g_gather.pattern_flags
				mf.tflags       &= ~Tflag     ::Target ;
				mf.extra_tflags &= ~ExtraTflag::Allow  ;
			}
			if (+mf) g_gather.pattern_flags.emplace_back( p , ::pair(washed,mf) ) ; // fast path : no need to match against a pattern that brings nothing
		}
		for( ::string& t : washed_files )
			g_gather.new_access( washed , ::move(t) , {.write=Yes} , DepInfo() , No/*late*/ , Comment::Wash ) ;
		//
		if (+g_start_info.stdin) g_gather.child_stdin = Fd( g_start_info.stdin , {.err_ok=true} ) ;
		else                     g_gather.child_stdin = Fd( "/dev/null"                         ) ;
		g_gather.child_stdin.no_std() ;
		g_gather.child_stderr = Child::PipeFd ;
		if (!g_start_info.stdout) {
			g_gather.child_stdout = Child::PipeFd ;
		} else {
			g_gather.child_stdout = Fd( g_start_info.stdout , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.err_ok=true} ) ;
			g_gather.new_access( washed , ::copy(g_start_info.stdout) , {.write=Yes} , DepInfo() , Yes/*late*/ , Comment::Stdout ) ;  // writing to stdout last for the whole job
			g_gather.child_stdout.no_std() ;
		}
		g_gather.cmd_line = cmd_line( cmd_env , repo_root_s ) ;
		Status status ;
		try { //!    vvvvvvvvvvvvvvvvvvvvv
			status = g_gather.exec_child() ;
			//       ^^^^^^^^^^^^^^^^^^^^^
		} catch (::string const& e) {                                                                                                 // START_OF_NO_COV defensive programming
			end_report.digest.status = g_gather.started ? Status::LateLost : Status::EarlyLost ;                                      // not early as soon as job is started
			end_report.msg_stderr.msg << "open-lmake error : " << e ;
			goto End ;
		}                                                                                                                             // END_OF_NO_COV
		struct rusage rsrcs ; ::getrusage(RUSAGE_CHILDREN,&rsrcs) ;
		//
		if (+g_to_unlnk) unlnk(g_to_unlnk) ;                                                                                          // XXX/ : suppress when CentOS7 bug is fixed
		//
		Gather::Digest digest = g_gather.analyze(status) ;
		trace("analysis",g_gather.start_date,g_gather.end_date,status,g_gather.msg,digest.msg) ;
		//
		::vector<FileInfo> target_fis ;
		end_report.msg_stderr.msg += compute_crcs( digest , /*out*/target_fis , /*out*/end_report.total_sz ) ;
		//
		if (g_start_info.cache) {
			try {
				::tie(upload_key,end_report.total_z_sz) = g_start_info.cache->upload( digest.targets , target_fis , g_start_info.zlvl ) ;
				trace("cache",upload_key) ;
			} catch (::string const& e) {
				trace("cache_upload_throw",e) ;
				end_report.msg_stderr.msg <<"cannot cache : "<<e<<'\n' ;
			}
			CommentExts ces ; if (!upload_key) ces |= CommentExt::Err ;
			g_exec_trace->emplace_back( New/*date*/ , Comment::UploadedToCache , ces , cat(g_start_info.cache->tag(),':',g_start_info.zlvl) ) ;
		}
		//
		if (+g_start_info.autodep_env.file_sync) {                                                                                    // fast path : avoid listing targets & guards if !file_sync
			for( auto const& [t,_] : digest.targets  ) nfs_guard.change(t) ;
			for( auto const&  f    : g_gather.guards ) nfs_guard.change(f) ;
		}
		//
		if ( status==Status::Ok && ( +digest.msg || (+g_gather.stderr&&!g_start_info.stderr_ok) ) )
			status = Status::Err ;
		//
		/**/                        end_report.msg_stderr.msg += g_gather.msg ;
		if (status!=Status::Killed) end_report.msg_stderr.msg += digest  .msg ;
		JobStats stats {
			.mem = size_t(rsrcs.ru_maxrss<<10)
		,	.cpu = Delay(rsrcs.ru_utime) + Delay(rsrcs.ru_stime)
		,	.job = g_gather.end_date-g_gather.start_date
		} ;
		end_report.digest = {
			.upload_key     = upload_key
		,	.targets        = ::move   (digest.targets       )
		,	.deps           = ::move   (digest.deps          )
		,	.refresh_codecs = mk_vector(digest.refresh_codecs)
		,	.cache_idx1     = g_start_info.cache_idx1
		,	.status         = status
		,	.incremental    = incremental
		} ;
		end_report.end_date          =        g_gather.end_date  ;
		end_report.stats             = ::move(stats            ) ;
		end_report.msg_stderr.stderr = ::move(g_gather.stderr  ) ;
		end_report.stdout            = ::move(g_gather.stdout  ) ;
		end_report.wstatus           =        g_gather.wstatus   ;
	}
End :
	{	Trace trace("end",end_report.digest) ;
		end_report.digest.has_msg_stderr = +end_report.msg_stderr ;
		try {
			ClientSockFd fd           { g_service_end } ;
			Pdate        end_overhead = New             ;
			g_exec_trace->emplace_back( end_overhead , Comment::EndOverhead , CommentExts() , snake_str(end_report.digest.status) ) ;
			end_report.digest.exec_time = end_overhead - start_overhead ;                                                             // measure overhead as late as possible
			//vvvvvvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf(end_report).send(fd) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("done",end_overhead) ;
		} catch (::string const& e) {
			if (+upload_key) g_start_info.cache->dismiss(upload_key) ;                                                                // suppress temporary data if server cannot handle them
			exit(Rc::Fail,"after job execution : ",e) ;
		}
	}
	try                       { g_start_info.exit() ;                             }
	catch (::string const& e) { exit(Rc::Fail,"cannot cleanup namespaces : ",e) ; }                                                   // NO_COV defensive programming
	//
	return 0 ;
}
