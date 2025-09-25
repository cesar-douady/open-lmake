// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sys/utsname.h>

#include "app.hh"
#include "disk.hh"
#include "fd.hh"
#include "hash.hh"
#include "re.hh"
#include "thread.hh"
#include "time.hh"
#include "trace.hh"

#include "autodep/gather.hh"

#include "rpc_job.hh"
#include "rpc_job_exec.hh"

using namespace Caches ;
using namespace Disk   ;
using namespace Hash   ;
using namespace Re     ;
using namespace Time   ;

::vector<ExecTraceEntry>* g_exec_trace      = nullptr      ;
Gather                    g_gather          ;
JobIdx                    g_job             = 0/*garbage*/ ;
SeqId                     g_seq_id          = 0/*garbage*/ ;
::string                  g_phy_repo_root_s ;
::string                  g_service_start   ;
::string                  g_service_mngt    ;
::string                  g_service_end     ;
JobStartRpcReply          g_start_info      ;
SeqId                     g_trace_id        = 0/*garbage*/ ;
::vector_s                g_washed          ;

JobStartRpcReply get_start_info(ServerSockFd const& server_fd) {
	Trace trace("get_start_info",g_service_start) ;
	bool             found_server = false ;
	JobStartRpcReply res          ;
	try {
		ClientSockFd fd { g_service_start } ;
		fd.set_timeout(Delay(100)) ;          // ensure we dont stay stuck in case server is in the coma : 100s = 1000 simultaneous connections, 10 jobs/s
		throw_unless(+fd) ;
		found_server = true ;
		//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		/**/  OMsgBuf().send                     ( fd , JobStartRpcReq({g_seq_id,g_job},server_fd.port()) ) ;
		res = IMsgBuf().receive<JobStartRpcReply>( fd                                                     ) ;
		//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	} catch (::string const& e) {
		trace("no_start_info",STR(found_server),e) ;
		if      (found_server) exit(Rc::Fail                                                    ) ; // this is typically a ^C
		else if (+e          ) exit(Rc::Fail,"cannot connect to server at",g_service_start,':',e) ; // this may be a server config problem, better to report if verbose
		else                   exit(Rc::Fail,"cannot connect to server at",g_service_start      ) ; // .
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
		for( ::string& l : sr.lnks )   g_gather.new_access(now,::move(l      ),{.accesses= Access::Lnk },FileInfo(l      ),Comment::OsInfo,CommentExt::Lnk  ) ;
		if (sr.file_loc<=FileLoc::Dep) g_gather.new_access(now,::move(sr.real),{.accesses=~Access::Stat},FileInfo(sr.real),Comment::OsInfo,CommentExt::Read ) ;
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

static const ::string StdPath = STD_PATH ;
static const ::uset_s SpecialWords {
	":"       , "."         , "{"        , "}"       , "!"
,	"alias"
,	"bind"    , "break"     , "builtin"
,	"caller"  , "case"      , "cd"       , "command" , "continue" , "coproc"
,	"declare" , "do"        , "done"
,	"echo"    , "elif"      , "else"     , "enable"  , "esac"     , "eval"    , "exec" , "exit" , "export"
,	"fi"      , "for"       , "function"
,	"getopts"
,	"if"      , "in"
,	"hash"    , "help"
,	"let"     , "local"     , "logout"
,	"mapfile"
,	"printf"  , "pwd"
,	"read"    , "readarray" , "readonly" , "return"
,	"select"  , "shift"     , "source"
,	"test"    , "then"      , "time"     , "times"   , "type"     , "typeset" , "trap"
,	"ulimit"  , "umask"     , "unalias"  , "unset"   , "until"
,	"while"
} ;
static const ::vector_s SpecialVars {
	"BASH_ALIASES"
,	"BASH_ENV"
,	"BASHOPTS"
,	"ENV"
,	"IFS"
,	"SHELLOPTS"
} ;
enum class State : uint8_t {
	None
,	SingleQuote
,	DoubleQuote
,	BackSlash
,	DoubleQuoteBackSlash                                                                             // after a \ within ""
} ;
static bool is_special( char c , int esc_lvl , bool first=false ) {
	switch (c) {
		case '$' :
		case '`' :            return true ;                                                          // recognized even in ""
		case '#' :
		case '&' :
		case '(' : case ')' :
		case '*' :
		case ';' :
		case '<' : case '>' :
		case '?' :
		case '[' : case ']' :
		case '|' :            return esc_lvl<2          ;                                            // recognized anywhere if not quoted
		case '~' :            return esc_lvl<2 && first ;                                            // recognized as first char of any word
		case '=' :            return esc_lvl<1          ;                                            // recognized only in first word
		default  :            return false ;
	}
}
// replace call to BASH by direct execution if a single command can be identified
bool/*done*/ mk_simple( ::vector_s&/*inout*/ res , ::string const& cmd , ::map_ss const& cmd_env ) {
	if (res.size()!=1) return false/*done*/ ;                                                        // options passed to bash
	if (res[0]!=BASH ) return false/*done*/ ;                                                        // not standard bash
	//
	for( ::string const& v : SpecialVars )
		if (cmd_env.contains(v)) return false/*done*/ ;                                              // complex environment
	//
	::vector_s v          { {} }        ;
	State      state      = State::None ;
	bool       slash_seen = false       ;
	bool       word_seen  = false       ;                                                            // if true <=> a new argument has been detected, maybe empty because of quotes
	bool       nl_seen    = false       ;
	bool       cmd_seen   = false       ;
	//
	auto special_cmd = [&]() {
		return v.size()==1 && !slash_seen && SpecialWords.contains(v[0]) ;
	} ;
	//
	for( char c : cmd ) {
		slash_seen |= c=='/' && v.size()==1 ;                                                        // / are only recorgnized in first word
		switch (state) {
			case State::None :
				if (is_special( c , v.size()>1/*esc_lvl*/ , !v.back() )) return false/*done*/ ;      // complex syntax
				switch (c) {
					case '\n' : nl_seen |= cmd_seen ; [[fallthrough]] ;
					case ' '  :
					case '\t' :
						if (cmd_seen) {
							if (special_cmd()) return false/*done*/ ;                                // need to search in $PATH and may be a reserved word or builtin command
							if (word_seen) {
								v.emplace_back() ;
								word_seen = false ;
								cmd_seen  = true  ;
							}
						}
					break ;
					case '\\' : state = State::BackSlash   ;                                      if (nl_seen) return false/*done*/ ; break ; // multi-line
					case '\'' : state = State::SingleQuote ;                                      if (nl_seen) return false/*done*/ ; break ; // .
					case '"'  : state = State::DoubleQuote ;                                      if (nl_seen) return false/*done*/ ; break ; // .
					default   : v.back().push_back(c)      ; word_seen = true ; cmd_seen = true ; if (nl_seen) return false/*done*/ ; break ; // .
				}
			break ;
			case State::BackSlash :
				v.back().push_back(c) ;
				state     = State::None ;
				word_seen = true        ;
			break ;
			case State::SingleQuote :
				if (c=='\'') { state = State::None ; word_seen = true ; }
				else           v.back().push_back(c) ;
			break ;
			case State::DoubleQuote :
				if (is_special( c , 2/*esc_lvl*/ )) return false/*done*/ ;                                                                    // complex syntax
				switch (c) {
					case '\\' : state = State::DoubleQuoteBackSlash ;                    break ;
					case '"'  : state = State::None                 ; word_seen = true ; break ;
					default   : v.back().push_back(c)               ;
				}
			break ;
			case State::DoubleQuoteBackSlash :
				if (!is_special( c , 2/*esc_lvl*/ ))
					switch (c) {
						case '\\' :
						case '\n' :
						case '"'  :                            break ;
						default   : v.back().push_back('\\') ;
					}
				v.back().push_back(c) ;
				state = State::DoubleQuote ;
			break ;
		}
	}
	if (state!=State::None)   return false/*done*/ ;                                                                                          // syntax error
	if (!word_seen        ) { SWEAR(!v.back()) ; v.pop_back() ; }                                                                             // suppress empty entry created by space after last word
	if (!v                )   return false/*done*/ ;                                                                                          // no command
	if (special_cmd()     )   return false/*done*/ ;                                                                                          // complex syntax
	if (!slash_seen) {                                                                                                                        // search PATH
		if (cmd_env.contains("EXECIGNORE")) return false/*done*/ ;                                                                            // complex environment
		auto            it       = cmd_env.find("PATH")                     ;
		::string const& path     = it==cmd_env.end() ? StdPath : it->second ;
		::vector_s      path_vec = split(path,':')                          ;
		for( ::string& p : split(path,':') ) {
			::string candidate = with_slash(::move(p)) + v[0] ;
			if (FileInfo(candidate).tag()==FileTag::Exe) {
				v[0] = ::move(candidate) ;
				goto CmdFound ;
			}
		}
		return false/*done*/ ;                                                                                                                // command not found
	CmdFound : ;
	}
	res = ::move(v) ;
	return true/*done*/ ;
}

::string g_to_unlnk ;                                                             // XXX> : suppress when CentOS7 bug is fixed
::vector_s cmd_line(::map_ss const& cmd_env) {
	static const size_t ArgMax = ::sysconf(_SC_ARG_MAX) ;
	::vector_s res = ::move(g_start_info.interpreter) ;                           // avoid copying as interpreter is used only here
	if (g_start_info.use_script) {
		// XXX> : fix the bug with CentOS7 where the write seems not to be seen and old script is executed instead of new one
	//	::string cmd_file = cat(PrivateAdminDirS,"cmds/",g_start_info.small_id) ; // correct code
		::string cmd_file = cat(PrivateAdminDirS,"cmds/",g_seq_id) ;
		AcFd( dir_guard(cmd_file) , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666} ).write(g_start_info.cmd) ;
		res.reserve(res.size()+1) ;
		res.push_back(mk_glb(cmd_file,*g_repo_root_s)) ;                          // provide absolute script so as to support cwd
		g_to_unlnk = ::move(cmd_file) ;
	} else {
		// large commands are forced use_script=true in server
		SWEAR( g_start_info.cmd.size()<=ArgMax/2 , g_start_info.cmd.size() ) ;    // env+cmd line must not be larger than ARG_MAX, keep some margin for env
		if (!mk_simple( res , g_start_info.cmd , cmd_env )) {                     // res is set if simple
			res.reserve(res.size()+2) ;
			res.emplace_back("-c"                    ) ;
			res.push_back   (::move(g_start_info.cmd)) ;
		}
	}
	return res ;
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
			*msg <<set_nl<< "while computing checksum for "<<e<<" : "<<e ;
		}                                                                  // END_OF_NO_COV
		e.second.sig       = fi.sig() ;
		(*target_fis)[ti]  = fi       ;
		*sz               += fi.sz    ;
		trace("crc_date",ci,before,Pdate(New)-before,e.second.crc,fi,e.first) ;
		if (!e.second.crc.valid()) {
			Lock lock{*msg_mutex} ;
			*msg <<set_nl<< "cannot compute checksum for "<<e.first ;
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
	Pdate        start_overhead { New } ;
	ServerSockFd server_fd      { New } ;              // server socket must be listening before connecting to server and last to the very end to ensure we can handle heartbeats
	uint64_t     upload_key     = 0     ;              // key used to identify temporary data uploaded to the cache
	//
	swear_prod(argc==8,argc) ;                         // syntax is : job_exec server:port/*start*/ server:port/*mngt*/ server:port/*end*/ seq_id job_idx repo_root trace_file
	g_service_start   =                     argv[1]  ;
	g_service_mngt    =                     argv[2]  ;
	g_service_end     =                     argv[3]  ;
	g_seq_id          = from_string<SeqId >(argv[4]) ;
	g_job             = from_string<JobIdx>(argv[5]) ;
	g_phy_repo_root_s =                     argv[6]  ; // passed early so we can chdir and trace early
	g_trace_id        = from_string<SeqId >(argv[7]) ;
	//
	g_repo_root_s = new ::string{g_phy_repo_root_s} ;  // no need to search for it
	//
	g_trace_file = new ::string{cat(g_phy_repo_root_s,PrivateAdminDirS,"trace/job_exec/",g_trace_id)} ;
	//
	JobEndRpcReq end_report { {g_seq_id,g_job} } ;
	end_report.digest   = {.status=Status::EarlyErr} ; // prepare to return an error, so we can goto End anytime
	end_report.end_date = start_overhead             ;
	g_exec_trace        = &end_report.exec_trace     ;
	g_exec_trace->emplace_back( start_overhead , Comment::StartOverhead ) ;
	//
	if (::chdir(g_phy_repo_root_s.c_str())!=0) {                                              // START_OF_NO_COV defensive programming
		get_start_info(server_fd) ;                                                           // getting start_info is useless, but necessary to be allowed to report end
		end_report.msg_stderr.msg << "cannot chdir to root : "<<no_slash(g_phy_repo_root_s) ;
		goto End ;
	}                                                                                         // END_OF_NO_COV
	Trace::s_sz = 10<<20 ;                                                                    // this is more than enough
	block_sigs({SIGCHLD}) ;                                                                   // necessary to capture it using signalfd
	app_init(false/*read_only_ok*/,No/*chk_version*/,Maybe/*cd_root*/) ;                      // dont cd, but check we are in a repo
	//
	{	Trace trace("main",Pdate(New),::span<char*>(argv,argc)) ;
		trace("pid",::getpid(),::getpgrp()) ;
		trace("start_overhead",start_overhead) ;
		//
		g_start_info = get_start_info(server_fd) ; if (!g_start_info) return 0 ;              // if !g_start_info, server ask us to give up
		try                       { g_start_info.job_space.mk_canon(g_phy_repo_root_s) ; }
		catch (::string const& e) { end_report.msg_stderr.msg += e ; goto End ;          }    // NO_COV defensive programming
		//
		if (+g_start_info.job_space.repo_view_s) g_repo_root_s = new ::string{g_start_info.job_space.repo_view_s} ;
		//
		NfsGuard nfs_guard   { g_start_info.autodep_env.file_sync } ;
		bool     incremental = false/*garbage*/                     ;
		//
		try {
			end_report.msg_stderr.msg += ensure_nl(do_file_actions( /*out*/g_washed , /*out*/incremental , ::move(g_start_info.pre_actions) , nfs_guard )) ;
		} catch (::string const& e) {                                                                                                                        // START_OF_NO_COV defensive programming
			trace("bad_file_actions",e) ;
			end_report.msg_stderr.msg += e                   ;
			end_report.digest.status   = Status::LateLostErr ;
			goto End ;
		}                                                                                                                                                    // END_OF_NO_COV
		Pdate washed { New } ;
		g_exec_trace->emplace_back( washed , Comment::Washed ) ;
		//
		SWEAR( !end_report.phy_tmp_dir_s , end_report.phy_tmp_dir_s ) ;
		{	auto it = g_start_info.env.begin() ;
			for(; it!=g_start_info.env.end() ; it++ ) if (it->first=="TMPDIR") break ;
			if ( it==g_start_info.env.end() || +it->second ) {                                                                    // if TMPDIR is set and empty, no tmp dir is prepared/cleaned
				if (g_start_info.keep_tmp) {
					end_report.phy_tmp_dir_s << g_phy_repo_root_s<<AdminDirS<<"tmp/"<<g_job<<'/' ;
				} else {
					// use seq id instead of small id to make tmp dir to ensure that even if user mistakenly record tmp dir name, there no chance of porosity between jobs
					// as with small id, by the time the (bad) old tmp dir is referenced by a new job, it may be in use by another job
					// such a situation cannot occur with seq id
					if      (it==g_start_info.env.end()         ) {}
					else if (it->second!=PassMrkr               ) end_report.phy_tmp_dir_s << with_slash(it->second       )<<g_start_info.key<<'/'<<g_seq_id<<'/' ;
					else if (has_env("TMPDIR")                  ) end_report.phy_tmp_dir_s << with_slash(get_env("TMPDIR"))<<g_start_info.key<<'/'<<g_seq_id<<'/' ;
					if      (!end_report.phy_tmp_dir_s          ) end_report.phy_tmp_dir_s << g_phy_repo_root_s<<AdminDirS<<"auto_tmp/"           <<g_seq_id<<'/' ;
					else if (!is_abs_s(end_report.phy_tmp_dir_s)) {
						end_report.msg_stderr.msg << "$TMPDIR ("<<end_report.phy_tmp_dir_s<<") must be absolute" ;
						goto End ;
					}
				}
			}
		}
		//
		::map_ss              cmd_env         ;
		::vmap_s<MountAction> enter_actions   ;
		::string              top_repo_root_s ;
		try {
			bool entered = g_start_info.enter(
				/*out*/enter_actions
			,	/*out*/cmd_env
			,	/*out*/end_report.dyn_env
			,	/*out*/g_gather.first_pid
			,	/*out*/top_repo_root_s
			,	       *g_lmake_root_s
			,	       g_phy_repo_root_s
			,	       end_report.phy_tmp_dir_s
			,	       g_seq_id
			) ;
			RealPath real_path { g_start_info.autodep_env } ;
			if (+g_start_info.os_info) {
				RegExpr  os_info_re { g_start_info.os_info }                               ;
				::string os_info    = get_os_info( real_path , g_start_info.os_info_file ) ;
				if (!os_info_re.match(os_info)) {
					trace("os_info_mismatch",g_start_info.os_info) ;
					/**/                            end_report.msg_stderr.msg << "unexpected os_info (" << os_info <<')'                                                                         ;
					if (+g_start_info.os_info_file) end_report.msg_stderr.msg << " from file "          <<                                                      g_start_info.os_info_file        ;
					/**/                            end_report.msg_stderr.msg << " does not match expected regexpr from " << g_start_info.rule <<".os_info ("<< g_start_info.os_info <<')'<<'\n' ;
					/**/                            end_report.msg_stderr.msg << "  consider :"                                                                                           <<'\n' ;
					/**/                            end_report.msg_stderr.msg << "  - "<< (+g_start_info.os_info     ?"fix":"set") <<' '<< g_start_info.rule <<".os_info"                 <<'\n' ;
					/**/                            end_report.msg_stderr.msg << "  - "<< (+g_start_info.os_info_file?"fix":"set") <<' '<< g_start_info.rule <<".os_info_file"                   ;
					goto End ;
				}
				trace("os_info_match",g_start_info.os_info) ;
			}
			if (entered) {
				for( auto& [f,a] : enter_actions ) {
					RealPath::SolveReport sr = real_path.solve(f,true/*no_follow*/) ;
					for( ::string& l : sr.lnks ) //!                                                                                          late
						/**/                            g_gather.new_access(washed,::move(l      ),{.accesses= Access::Lnk },FileInfo(l      ),    Comment::mount,CommentExt::Lnk  ) ;
					if (sr.file_loc<=FileLoc::Dep) {
						if      (a==MountAction::Read ) g_gather.new_access(washed,::move(sr.real),{.accesses=~Access::Stat},FileInfo(sr.real),    Comment::mount,CommentExt::Read ) ;
						else if (sr.file_accessed==Yes) g_gather.new_access(washed,::move(sr.real),{.accesses= Access::Lnk },FileInfo(sr.real),    Comment::mount,CommentExt::Read ) ;
					}
					if (sr.file_loc<=FileLoc::Repo) {
						if      (a==MountAction::Write) g_gather.new_access(washed,::move(sr.real),{.write=Yes             },FileInfo(       ),Yes,Comment::mount,CommentExt::Write) ;
					}
				}
				g_exec_trace->emplace_back( New/*date*/ , Comment::EnteredNamespace ) ;
			}
			g_start_info.job_space.update_env(
				/*inout*/cmd_env
			,	         *g_lmake_root_s
			,	         g_phy_repo_root_s
			,	         end_report.phy_tmp_dir_s
			,	         g_start_info.autodep_env.sub_repo_s
			,	         g_seq_id
			,	         g_start_info.small_id
			) ;
			//
		} catch (::string const& e) {
			end_report.msg_stderr.msg += e ;
			goto End ;
		}
		g_start_info.autodep_env.fast_host        = host()                                                                      ; // host on which fast_report_pipe works
		g_start_info.autodep_env.fast_report_pipe = cat(top_repo_root_s,PrivateAdminDirS,"fast_reports/",g_start_info.small_id) ; // fast_report_pipe is a pipe and only works locally
		g_start_info.autodep_env.views            = g_start_info.job_space.flat_phys()                                          ;
		trace("prepared",g_start_info.autodep_env) ;
		//
		g_gather.addr             =        g_start_info.addr           ;
		g_gather.as_session       =        true                        ;
		g_gather.autodep_env      = ::move(g_start_info.autodep_env  ) ;
		g_gather.ddate_prec       =        g_start_info.ddate_prec     ;
		g_gather.env              =        &cmd_env                    ;
		g_gather.exec_trace       =        g_exec_trace                ;
		g_gather.job              =        g_job                       ;
		g_gather.kill_sigs        = ::move(g_start_info.kill_sigs    ) ;
		g_gather.live_out         =        g_start_info.live_out       ;
		g_gather.method           =        g_start_info.method         ;
		g_gather.network_delay    =        g_start_info.network_delay  ;
		g_gather.nice             =        g_start_info.nice           ;
		g_gather.no_tmp           =       !end_report.phy_tmp_dir_s    ;
		g_gather.rule             = ::move(g_start_info.rule         ) ;
		g_gather.seq_id           =        g_seq_id                    ;
		g_gather.server_master_fd = ::move(server_fd                 ) ;
		g_gather.service_mngt     =        g_service_mngt              ;
		g_gather.timeout          =        g_start_info.timeout        ;
		//
		if (!g_start_info.method)                                                                             // if no autodep, consider all static deps are fully accessed as we have no precise report
			for( auto& [d,dd_edf] : g_start_info.deps ) if (dd_edf.first.dflags[Dflag::Static]) {
				DepDigest& dd = dd_edf.first ;
				dd.accesses = ~Accesses() ;
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
		for( ::string& t : g_washed )
			g_gather.new_access( washed , ::move(t) , {.write=Yes} , DepInfo() , No/*late*/ , Comment::Wash ) ;
		//                                                                      err_ok
		if (+g_start_info.stdin) g_gather.child_stdin = Fd( g_start_info.stdin , true  ) ;
		else                     g_gather.child_stdin = Fd( "/dev/null"        , false ) ;
		g_gather.child_stdin.no_std() ;
		g_gather.child_stderr = Child::PipeFd ;
		if (!g_start_info.stdout) {
			g_gather.child_stdout = Child::PipeFd ;
		} else {
			g_gather.child_stdout = Fd( dir_guard(g_start_info.stdout) , true/*err_ok*/ , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666} ) ;
			g_gather.new_access( washed , ::copy(g_start_info.stdout) , {.write=Yes} , DepInfo() , Yes/*late*/ , Comment::Stdout ) ;      // writing to stdout last for the whole job
			g_gather.child_stdout.no_std() ;
		}
		g_gather.cmd_line = cmd_line(cmd_env) ;
		Status status ;
		//                                   vvvvvvvvvvvvvvvvvvvvv
		try                       { status = g_gather.exec_child() ;            }
		//                                   ^^^^^^^^^^^^^^^^^^^^^
		catch (::string const& e) { end_report.msg_stderr.msg += e ; goto End ; }                                                         // NO_COV defensive programming
		struct rusage rsrcs ; ::getrusage(RUSAGE_CHILDREN,&rsrcs) ;
		//
		if (+g_to_unlnk) unlnk(g_to_unlnk) ;                                                                                              // XXX> : suppress when CentOS7 bug is fixed
		//
		Gather::Digest digest = g_gather.analyze(status) ;
		trace("analysis",g_gather.start_date,g_gather.end_date,status,g_gather.msg,digest.msg) ;
		//
		::vector<FileInfo> target_fis ;
		end_report.msg_stderr.msg += compute_crcs( digest , /*out*/target_fis , /*out*/end_report.total_sz ) ;
		//
		if (g_start_info.cache) {
			try {
				upload_key = g_start_info.cache->upload( digest.targets , target_fis , ::move(g_gather.codec_map) , g_start_info.zlvl ) ;
				trace("cache",upload_key) ;
			} catch (::string const& e) {
				trace("cache_upload_throw",e) ;
				end_report.msg_stderr.msg <<"cannot cache : "<<e<<'\n' ;
			}
			CommentExts ces ; if (!upload_key) ces |= CommentExt::Err ;
			g_exec_trace->emplace_back( New/*date*/ , Comment::UploadedToCache , ces , cat(g_start_info.cache->tag(),':',g_start_info.zlvl) ) ;
		}
		//
		if (+g_start_info.autodep_env.file_sync) {                                                                                        // fast path : avoid listing targets & guards if !file_sync
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
			.upload_key  = upload_key
		,	.targets     = ::move(digest.targets)
		,	.deps        = ::move(digest.deps   )
		,	.cache_idx   = g_start_info.cache_idx
		,	.status      = status
		,	.incremental = incremental
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
			end_report.digest.exec_time      = end_overhead - start_overhead ;                                                            // measure overhead as late as possible
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send( fd , end_report ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("done",end_overhead) ;
		} catch (::string const& e) {
			if (+upload_key) g_start_info.cache->dismiss(upload_key) ;                                                                    // suppress temporary data if server cannot handle them
			exit(Rc::Fail,"after job execution : ",e) ;
		}
	}
	try                       { g_start_info.exit() ;                             }
	catch (::string const& e) { exit(Rc::Fail,"cannot cleanup namespaces : ",e) ; }                                                       // NO_COV defensive programming
	//
	return 0 ;
}
