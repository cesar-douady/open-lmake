// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>       // sysconf

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

struct PatternDict {
	static constexpr MatchFlags NotFound = {} ;
	// services
	MatchFlags const& at(::string const& x) const {
		if ( auto it=knowns.find(x) ; it!=knowns.end() )     return it->second ;
		for( auto const& [p,r] : patterns ) if (+p.match(x)) return r          ;
		/**/                                                 return NotFound   ;
	}
	void add( bool star , ::string const& key , MatchFlags const& val ) {
		if (star) patterns.emplace_back( RegExpr(key,false/*cache*/,true/*with_paren*/) , val ) ;
		else      knowns  .emplace     (         key                                    , val ) ;
	}
	// data
	::umap_s<MatchFlags>       knowns   = {} ;
	::vmap<RegExpr,MatchFlags> patterns = {} ;
} ;

::vector<ExecTraceEntry>* g_exec_trace      = nullptr      ;
Gather                    g_gather          ;
JobIdx                    g_job             = 0/*garbage*/ ;
PatternDict               g_match_dct       ;
NfsGuard                  g_nfs_guard       ;
SeqId                     g_seq_id          = 0/*garbage*/ ;
::string                  g_phy_repo_root_s ;
::string                  g_service_start   ;
::string                  g_service_mngt    ;
::string                  g_service_end     ;
JobStartRpcReply          g_start_info      ;
SeqId                     g_trace_id        = 0/*garbage*/ ;
::vector_s                g_washed          ;

struct Digest {
	::vmap_s<TargetDigest> targets ;
	::vmap_s<DepDigest   > deps    ;
	::vector<NodeIdx     > crcs    ; // index in targets of entry for which we need to compute a crc
	::string               msg     ;
} ;

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
	g_exec_trace->push_back({ New , Comment::startInfo , CommentExt::Reply }) ;
	trace(res) ;
	return res ;
}

Digest analyze(Status status=Status::New) {                               // status==New means job is not done
	Trace trace("analyze",status,g_gather.accesses.size()) ;
	Digest res             ; res.deps.reserve(g_gather.accesses.size()) ; // typically most of accesses are deps
	Pdate  prev_first_read ;
	Pdate  relax           = Pdate(New)+g_start_info.network_delay ;
	//
	for( auto& [file,info] : g_gather.accesses ) {
		MatchFlags    flags = g_match_dct.at(file) ;
		AccessDigest& ad    = info.digest          ;
		switch (flags.is_target) {
			// manage ignore flag if mentioned in the rule
			case Yes   : ad.tflags |= flags.tflags() ; ad.extra_tflags |= flags.extra_tflags() ; if (flags.extra_tflags()[ExtraTflag::Ignore]) { ad.accesses = {} ; ad.write = No ; } break ;
			case No    : ad.dflags |= flags.dflags() ; ad.extra_dflags |= flags.extra_dflags() ; if (flags.extra_dflags()[ExtraDflag::Ignore])   ad.accesses = {} ;                   break ;
			case Maybe :                                                                       ;                                                                                      break ;
		DF}                                                                                                                                                                                   // NO_COV
		//
		if (ad.write==Yes)                                                                                                                  // ignore reads after earliest confirmed write
			for( Access a : iota(All<Access>) )
				if ( info.read[+a]>info.write || info.read[+a]>info.target ) ad.accesses &= ~a ;
		::pair<Pdate,Accesses> first_read = info.first_read()                                                                             ;
		bool                   ignore  = ad.extra_dflags[ExtraDflag::Ignore] || ad.extra_tflags[ExtraTflag::Ignore]                       ;
		bool                   sense   = info.digest_required || !ad.dflags[Dflag::IgnoreError]                                           ;
		bool                   is_read = +ad.accesses || ( !ignore && sense )                                                             ;
		bool                   is_dep  = ad.dflags[Dflag::Static] || ( flags.is_target!=Yes && is_read && first_read.first<=info.target ) ; // if a (side) target, it is so since the beginning
		bool is_tgt =
			ad.write!=No
		||	(	(  flags.is_target==Yes || info.target!=Pdate::Future         )
			&&	!( !ad.tflags[Tflag::Target] && ad.tflags[Tflag::Incremental] )                           // fast path : no matching, no pollution, no washing => forget it
			)
		;
		// handle deps
		if (is_dep) {
			DepDigest dd { ad.accesses , info.dep_info , ad.dflags } ;
			//
			// if file is not old enough, we make it hot and server will ensure job producing dep was done before this job started
			dd.hot          = info.dep_info.is_a<DepInfoKind::Info>() && !info.dep_info.info().date.avail_at(first_read.first,g_start_info.ddate_prec) ;
			dd.parallel     = +first_read.first && first_read.first==prev_first_read                                                                   ;
			prev_first_read = first_read.first                                                                                                         ;
			// try to transform date into crc as far as possible
			bool unstable = false ;
			if      ( dd.is_crc                                 )   {}                                    // already a crc => nothing to do
			else if ( !is_read                                  )   {}                                    // no access     => nothing to do
			else if ( !info.digest_seen || info.seen>info.write ) { dd.crc(Crc::None) ; dd.hot=false  ; } // job has been executed without seeing the file (before possibly writing to it)
			else if ( !dd.sig()                                 ) { dd.crc({}       ) ; unstable=true ; } // file was not present initially but was seen, it is incoherent even if not present finally
			else if ( ad.write!=No                              )   {}                                    // cannot check stability as we wrote to it, clash will be detected in server if any
			else if ( FileSig sig{file} ; sig!=dd.sig()         ) { dd.crc({}       ) ; unstable=true ; } // file dates are incoherent from first access to end of job, dont know what has been read
			else if ( !sig                                      ) { dd.crc({}       ) ; unstable=true ; } // file is awkward
			else if ( !Crc::s_sense(dd.accesses,sig.tag())      )   dd.crc(sig.tag()) ;                   // just record the tag if enough to match (e.g. accesses==Lnk and tag==Reg)
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.deps.emplace_back(file,dd) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (status!=Status::New) {                 // only trace for user at end of job as intermediate analyses are of marginal interest for user
				if      (unstable) g_exec_trace->push_back({ New , Comment::unstable , CommentExts() , file }) ;
				else if (dd.hot  ) g_exec_trace->push_back({ New , Comment::hot      , CommentExts() , file }) ;
			}
			if (dd.hot) trace("dep_hot",dd,info.dep_info,first_read,g_start_info.ddate_prec,file) ;
			else        trace("dep    ",dd,                                                 file) ;
		}
		if (status==Status::New) continue ;            // we are handling chk_deps and we only care about deps
		// handle targets
		if (is_tgt) {
			if (ad.write==Maybe) relax.sleep_until() ; // /!\ if a write is interrupted, it may continue past the end of the process when accessing a network disk ...
			//                                         // ... no need to optimize (could compute other crcs while waiting) as this is exceptional
			bool    written = ad.write==Yes ;
			FileSig sig     ;
			Crc     crc     ;                          // lazy evaluated (not in parallel, but need is exceptional)
			if (ad.write==Maybe) {                     // if we dont know if file has been written, detect file update from disk
				if (info.dep_info.is_a<DepInfoKind::Crc>()) { crc = Crc(file,/*out*/sig) ; written |= info.dep_info.crc()!=crc ; } // solve lazy evaluation
				else                                                                       written |= info.dep_info.sig()!=sig ;
			}
			if (!crc) sig = file ;                                                                // sig is computed at the same time as crc, but we need it in all cases
			//
			TargetDigest td       { .tflags=ad.tflags , .extra_tflags=ad.extra_tflags } ;
			bool unlnk    = !sig  ;
			bool reported = false ;
			//
			if (is_dep                        ) td.tflags    |= Tflag::Incremental              ; // if is_dep, previous target state is guaranteed by being a dep, use it
			if (!td.tflags[Tflag::Incremental]) td.pre_exist  = info.dep_info.seen(ad.accesses) ;
			switch (flags.is_target) {
				case Yes   : break ;
				case Maybe :
					if (unlnk) break ;                                                            // it is ok to write and unlink temporary files
				[[fallthrough]] ;
				case No :
					if (!written                          ) break ;                               // it is ok to attempt writing as long as attempt does not succeed
					if (ad.extra_tflags[ExtraTflag::Allow]) break ;                               // it is ok if explicitly allowed by user
					trace("bad_access",ad,flags) ;
					if (ad.write==Maybe    ) res.msg << "maybe "                        ;
					/**/                     res.msg << "unexpected "                   ;
					/**/                     res.msg << (unlnk?"unlink ":"write to ")   ;
					if (flags.is_target==No) res.msg << "dep "                          ;
					/**/                     res.msg << mk_file(file,No|!unlnk) << '\n' ;
					reported = true ;
				break ;
			}
			if ( is_dep && !unlnk ) {
				g_exec_trace->push_back({ New , Comment::depAndTarget , CommentExts() , file }) ;
				if (!reported) {                                                                  // if dep and unexpected target, prefer unexpected message rather than this one
					const char* read = nullptr ;
					if      (ad.dflags[Dflag::Static]       ) read = "a static dep" ;
					else if (first_read.second[Access::Reg ]) read = "read"         ;
					else if (first_read.second[Access::Lnk ]) read = "readlink'ed"  ;
					else if (first_read.second[Access::Stat]) read = "stat'ed"      ;
					else if (ad.dflags[Dflag::Required]     ) read = "required"     ;
					SWEAR(read) ;
					res.msg << "file was "<<read<<" and later declared as target : "<<mk_file(file)<<'\n' ;
				}
			}
			if (written) {
				if      ( unlnk                                               )                  td.crc = Crc::None    ;
				else if ( status==Status::Killed || !td.tflags[Tflag::Target] ) { td.sig = sig ; td.crc = td.sig.tag() ; } // no crc if meaningless
				else if ( +crc                                                ) { td.sig = sig ; td.crc = crc          ; } // we already have a crc
				//
				if (!crc.valid()) res.crcs.emplace_back(res.targets.size()) ;                                              // record index in res.targets for deferred (parallel) crc computation
			}
			if (
				td.tflags[Tflag::Target] && !td.tflags[Tflag::Phony] && td.tflags[Tflag::Static] && !td.extra_tflags[ExtraTflag::Optional] // target is expected
			&&	unlnk                                                                                                                      // but not produced
			&&	status==Status::Ok                                                                                                         // and there no more important reason
			)
				res.msg << "missing static target " << mk_file(file,No/*exists*/) << '\n' ;                                                // warn specifically
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.targets.emplace_back(file,td) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("target ",ad,td,STR(unlnk),file) ;
		} else if (!is_dep) {
			trace("ignore ",ad,file) ;
		}
	}
	for( ::string const& t : g_washed ) if (!g_gather.access_map.contains(t)) {
		using ETF = ExtraTflag ;
		trace("wash",t) ;
		MatchFlags flags = g_match_dct.at(t) ;
		if      (flags.is_target!=Yes             ) res.targets.emplace_back( t , TargetDigest{                          .extra_tflags=                     ETF::Wash , .crc=Crc::None } ) ;
		else if (flags.extra_tflags()[ETF::Ignore]) {}
		else                                        res.targets.emplace_back( t , TargetDigest{ .tflags=flags.tflags() , .extra_tflags=flags.extra_tflags()|ETF::Wash , .crc=Crc::None } ) ;
	}
	g_exec_trace->push_back({ New , Comment::analyzed }) ;
	trace("done",res.deps.size(),res.targets.size(),res.crcs.size(),res.msg) ;
	return res ;
}

::vmap_s<DepDigest> cur_deps_cb() { return analyze().deps ; }

static const ::string StdPath = STD_PATH ;
static const ::uset_s SpecialWords {
	":"       , "."         , "{"        , "}"       , "!"
,	"alias"
,	"bind"    , "break"     , "builtin"
,	"caller"  , "case"      , "cd"       , "command" , "continue" , "coproc"
,	"declare" , "do"        , "done"
,	"echo"    , "elif"      , "else"     , "enable"  , "esac"     , "eval"    , "exec" , "exit"
,	"fi"      , "for"       , "function"
,	"export"
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

ENUM( State
,	None
,	SingleQuote
,	DoubleQuote
,	BackSlash
,	DoubleQuoteBackSlash // after a \ within ""
)

static bool is_special( char c , int esc_lvl , bool first=false ) {
	switch (c) {
		case '$' :
		case '`' :            return true ;               // recognized even in ""
		case '#' :
		case '&' :
		case '(' : case ')' :
		case '*' :
		case ';' :
		case '<' : case '>' :
		case '?' :
		case '[' : case ']' :
		case '|' :            return esc_lvl<2          ; // recognized anywhere if not quoted
		case '~' :            return esc_lvl<2 && first ; // recognized as first char of any word
		case '=' :            return esc_lvl<1          ; // recognized only in first word
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
	auto special_cmd = [&]()->bool {
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
		::string      &  v0      = v[0]                                     ;
		for( ::string& p : split(path,':') ) {
			::string candidate = with_slash(::move(p)) + v0 ;
			if (FileInfo(candidate).tag()==FileTag::Exe) {
				v0 = ::move(candidate) ;
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
		AcFd( dir_guard(cmd_file) , Fd::Write ).write(g_start_info.cmd) ;
		res.reserve(res.size()+1) ;
		res.push_back(mk_abs(cmd_file,*g_repo_root_s)) ;                          // provide absolute script so as to support cwd
		g_to_unlnk = ::move(cmd_file) ;
	} else {
		// large commands are forced use_script=true in server
		SWEAR( g_start_info.cmd.size()<=ArgMax/2 , g_start_info.cmd.size() ) ;    // env+cmd line must not be larger than ARG_MAX, keep some margin for env
		if (!mk_simple( res , g_start_info.cmd , cmd_env )) {                     // res is set if simple
			res.reserve(res.size()+2) ;
			res.push_back("-c"                    ) ;
			res.push_back(::move(g_start_info.cmd)) ;
		}
	}
	return res ;
}

void crc_thread_func( size_t id , vmap_s<TargetDigest>* targets , ::vector<NodeIdx> const* crcs , ::string* msg , Mutex<MutexLvl::JobExec>* msg_mutex , ::vector<FileInfo>* target_fis , size_t* sz ) {
	static Atomic<NodeIdx> crc_idx = 0 ;
	t_thread_key = '0'+id ;
	Trace trace("crc_thread_func",targets->size(),crcs->size()) ;
	NodeIdx cnt = 0 ;                                                      // cnt is for trace only
	*sz = 0 ;
	for( NodeIdx ci=0 ; (ci=crc_idx++)<crcs->size() ; cnt++ ) {
		NodeIdx                 ti     = (*crcs)[ci]    ;
		::pair_s<TargetDigest>& e      = (*targets)[ti] ;
		Pdate                   before = New            ;
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
		trace("crc_date",ci,before,Pdate(New)-before,e.second.crc,e.second.sig,e.first) ;
		if (!e.second.crc.valid()) {
			Lock lock{*msg_mutex} ;
			*msg <<set_nl<< "cannot compute checksum for "<<e.first ;
		}
	}
	trace("done",cnt) ;
}

::string/*msg*/ compute_crcs( Digest& digest , ::vector<FileInfo>&/*out*/ target_fis , size_t&/*out*/ total_sz ) {
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
	g_exec_trace->push_back({ New , Comment::computedCrcs }) ;
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
	g_phy_repo_root_s = with_slash         (argv[6]) ; // passed early so we can chdir and trace early
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
	g_exec_trace->push_back({ start_overhead , Comment::startOverhead }) ;
	//
	if (::chdir(no_slash(g_phy_repo_root_s).c_str())!=0) {                                          // START_OF_NO_COV defensive programming
		get_start_info(server_fd) ;                                                                 // getting start_info is useless, but necessary to be allowed to report end
		end_report.msg_stderr.msg << "cannot chdir to root : "<<no_slash(g_phy_repo_root_s)<<'\n' ;
		goto End ;
	}                                                                                               // END_OF_NO_COV
	Trace::s_sz = 10<<20 ;                                                                          // this is more than enough
	block_sigs({SIGCHLD}) ;                                                                         // necessary to capture it using signalfd
	app_init(false/*read_only_ok*/,No/*chk_version*/,Maybe/*cd_root*/) ;                            // dont cd, but check we are in a repo
	//
	{	Trace trace("main",Pdate(New),::span<char*>(argv,argc)) ;
		trace("pid",::getpid(),::getpgrp()) ;
		trace("start_overhead",start_overhead) ;
		//
		g_start_info = get_start_info(server_fd) ;
		if (!g_start_info) return 0 ;                                                                                                     // server ask us to give up
		try                       { g_start_info.job_space.mk_canon(g_phy_repo_root_s) ; }
		catch (::string const& e) { end_report.msg_stderr.msg += e ; goto End ;          }                                                // NO_COV defensive programming
		//
		if (+g_start_info.job_space.repo_view_s) g_repo_root_s = new ::string{g_start_info.job_space.repo_view_s} ;
		//
		g_nfs_guard.reliable_dirs = g_start_info.autodep_env.reliable_dirs ;
		//
		for( auto const& [d ,digest] : g_start_info.deps           ) if (digest.dflags[Dflag::Static]) g_match_dct.add( false/*star*/ , d  , digest.dflags ) ;
		for( auto const& [dt,mf    ] : g_start_info.static_matches )                                   g_match_dct.add( false/*star*/ , dt , mf            ) ;
		for( auto const& [p ,mf    ] : g_start_info.star_matches   )                                   g_match_dct.add( true /*star*/ , p  , mf            ) ;
		//
		try {
			end_report.msg_stderr.msg += ensure_nl(do_file_actions( /*out*/g_washed , ::move(g_start_info.pre_actions) , g_nfs_guard )) ;
		} catch (::string const& e) {                                                                                                     // START_OF_NO_COV defensive programming
			trace("bad_file_actions",e) ;
			end_report.msg_stderr.msg += ensure_nl(e) ;
			end_report.digest.status = Status::LateLostErr ;
			goto End ;
		}                                                                                                                                 // END_OF_NO_COV
		Pdate washed { New } ;
		g_exec_trace->push_back({ washed , Comment::washed }) ;
		//
		SWEAR(!end_report.phy_tmp_dir_s,end_report.phy_tmp_dir_s) ;
		{	auto it = g_start_info.env.begin() ;
			for(; it!=g_start_info.env.end() ; it++ ) if (it->first=="TMPDIR") break ;
			if ( it==g_start_info.env.end() || +it->second ) {                                                                            // if TMPDIR is set and empty, no tmp dir is prepared/cleaned
				if (g_start_info.keep_tmp) {
					end_report.phy_tmp_dir_s << g_phy_repo_root_s<<AdminDirS<<"tmp/"<<g_job<<'/' ;
				} else {
					if      (it==g_start_info.env.end()       ) {}
					else if (it->second!=EnvPassMrkr          ) end_report.phy_tmp_dir_s << with_slash(it->second       )<<g_start_info.key<<'/'<<g_start_info.small_id<<'/' ;
					else if (has_env("TMPDIR")                ) end_report.phy_tmp_dir_s << with_slash(get_env("TMPDIR"))<<g_start_info.key<<'/'<<g_start_info.small_id<<'/' ;
					if      (!end_report.phy_tmp_dir_s        ) end_report.phy_tmp_dir_s << g_phy_repo_root_s<<PrivateAdminDirS<<"tmp/"         <<g_start_info.small_id<<'/' ;
					else if (!is_abs(end_report.phy_tmp_dir_s)) {
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
			if (
				g_start_info.enter(
					/*out*/enter_actions
				,	/*out*/cmd_env
				,	/*out*/end_report.dyn_env
				,	/*out*/g_gather.first_pid
				,	/*out*/top_repo_root_s
				,	       *g_lmake_root_s
				,	       g_phy_repo_root_s
				,	       end_report.phy_tmp_dir_s
				,	       g_seq_id
				)
			) {
				RealPath real_path { g_start_info.autodep_env } ;
				for( auto& [f,a] : enter_actions ) {
					RealPath::SolveReport sr = real_path.solve(f,true/*no_follow*/) ;
					for( ::string& l : sr.lnks )
						/**/                            g_gather.new_dep   ( washed , ::move(l      ) ,  Access::Lnk  , Comment::mount , CommentExt::Lnk   ) ;
					if (sr.file_loc<=FileLoc::Dep) {
						if      (a==MountAction::Read ) g_gather.new_dep   ( washed , ::move(sr.real) , ~Access::Stat , Comment::mount , CommentExt::Read  ) ;
						else if (sr.file_accessed==Yes) g_gather.new_dep   ( washed , ::move(sr.real) ,  Access::Lnk  , Comment::mount , CommentExt::Read  ) ;
					}
					if (sr.file_loc<=FileLoc::Repo) {
						if      (a==MountAction::Write) g_gather.new_target( washed , ::move(sr.real) ,                 Comment::mount , CommentExt::Write ) ;
					}
				}
				g_exec_trace->push_back({ New , Comment::enteredNamespace }) ;
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
		g_start_info.autodep_env.fast_host        = host()                                                                      ;         // host on which fast_report_pipe works
		g_start_info.autodep_env.fast_report_pipe = cat(top_repo_root_s,PrivateAdminDirS,"fast_reports/",g_start_info.small_id) ;         // fast_report_pipe is a pipe and only works locally
		g_start_info.autodep_env.views            = g_start_info.job_space.flat_phys()                                          ;
		trace("prepared",g_start_info.autodep_env) ;
		//
		g_gather.addr             =        g_start_info.addr           ;
		g_gather.as_session       =        true                        ;
		g_gather.autodep_env      = ::move(g_start_info.autodep_env  ) ;
		g_gather.cur_deps_cb      =        cur_deps_cb                 ;
		g_gather.env              =        &cmd_env                    ;
		g_gather.exec_trace       =        g_exec_trace                ;
		g_gather.job              =        g_job                       ;
		g_gather.kill_sigs        = ::move(g_start_info.kill_sigs    ) ;
		g_gather.live_out         =        g_start_info.live_out       ;
		g_gather.method           =        g_start_info.method         ;
		g_gather.network_delay    =        g_start_info.network_delay  ;
		g_gather.no_tmp           =       !end_report.phy_tmp_dir_s    ;
		g_gather.seq_id           =        g_seq_id                    ;
		g_gather.server_master_fd = ::move(server_fd                 ) ;
		g_gather.service_mngt     =        g_service_mngt              ;
		g_gather.timeout          =        g_start_info.timeout        ;
		//
		if (!g_start_info.method)                                                           // if no autodep, consider all static deps are fully accessed as we have no precise report
			for( auto& [d,digest] : g_start_info.deps ) if (digest.dflags[Dflag::Static]) {
				digest.accesses = ~Accesses() ;
				if ( digest.is_crc && !digest.crc().valid() ) digest.sig(FileSig(d)) ;
			}
		//
		for( auto& [d,dd] : g_start_info.deps ) g_gather.new_dep( washed , ::move(d) , ::move(dd) , g_start_info.stdin ) ;
		for( auto const& [t,f] : g_match_dct.knowns )
			if ( f.is_target==Yes && !f.extra_tflags()[ExtraTflag::Optional] )
				g_gather.new_unlnk(washed,t) ;                                              // always report non-optional static targets
		//
		if (+g_start_info.stdin) g_gather.child_stdin = Fd(g_start_info.stdin) ;
		else                     g_gather.child_stdin = Fd("/dev/null"       ) ;
		g_gather.child_stdin.no_std() ;
		g_gather.child_stderr = Child::PipeFd ;
		if (!g_start_info.stdout) {
			g_gather.child_stdout = Child::PipeFd ;
		} else {
			g_gather.child_stdout = Fd(dir_guard(g_start_info.stdout),Fd::Write) ;
			g_gather.new_target( washed , g_start_info.stdout , Comment::stdout ) ;
			g_gather.child_stdout.no_std() ;
		}
		g_gather.cmd_line = cmd_line(cmd_env) ;
		Status status ;
		//                                   vvvvvvvvvvvvvvvvvvvvv
		try                       { status = g_gather.exec_child() ;            }
		//                                   ^^^^^^^^^^^^^^^^^^^^^
		catch (::string const& e) { end_report.msg_stderr.msg += e ; goto End ; }           // NO_COV defensive programming
		struct rusage rsrcs ; ::getrusage(RUSAGE_CHILDREN,&rsrcs) ;
		//
		if (+g_to_unlnk) unlnk(g_to_unlnk) ;                                                // XXX> : suppress when CentOS7 bug is fixed
		//
		Digest digest = analyze(status) ;
		trace("analysis",g_gather.start_date,g_gather.end_date,status,g_gather.msg,digest.msg) ;
		//
		::vector<FileInfo> target_fis ;
		end_report.msg_stderr.msg += compute_crcs( digest , /*out*/target_fis , /*out*/end_report.total_sz ) ;
		//
		if (g_start_info.cache) {
			upload_key = g_start_info.cache->upload( digest.targets , target_fis , g_start_info.z_lvl ) ;
			g_exec_trace->push_back({ New , Comment::uploadedToCache , CommentExts() , cat(g_start_info.cache->tag(),':',g_start_info.z_lvl) }) ;
			trace("cache",upload_key) ;
		}
		//
		if (!g_start_info.autodep_env.reliable_dirs) {                                      // fast path : avoid listing targets & guards if reliable_dirs
			for( auto const& [t,_] : digest.targets  ) g_nfs_guard.change(t) ;              // protect against NFS strange notion of coherence while computing crcs
			for( auto const&  f    : g_gather.guards ) g_nfs_guard.change(f) ;              // .
			g_nfs_guard.close() ;
		}
		//
		if ( status==Status::Ok && ( +digest.msg || (+g_gather.stderr&&!g_start_info.allow_stderr) ) )
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
		,	.targets        = ::move(digest.targets)
		,	.deps           = ::move(digest.deps   )
		,	.cache_idx      = g_start_info.cache_idx
		,	.status         = status
		,	.has_msg_stderr = +end_report.msg_stderr.msg || +g_gather.stderr
		} ;
		end_report.end_date          =        g_gather.end_date  ;
		end_report.stats             = ::move(stats            ) ;
		end_report.msg_stderr.stderr = ::move(g_gather.stderr  ) ;
		end_report.stdout            = ::move(g_gather.stdout  ) ;
		end_report.wstatus           =        g_gather.wstatus   ;
	}
End :
	{	Trace trace("end",end_report.digest.status) ;
		try {
			ClientSockFd fd           { g_service_end } ;
			Pdate        end_overhead = New             ;
			g_exec_trace->push_back(ExecTraceEntry{ end_overhead , Comment::endOverhead , {}/*CommentExt*/ , cat(end_report.digest.status) }) ;
			end_report.digest.exec_time = end_overhead - start_overhead ;                                                                       // measure overhead as late as possible
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			OMsgBuf().send( fd , end_report ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("done",end_overhead) ;
		} catch (::string const& e) {
			if (+upload_key) g_start_info.cache->dismiss(upload_key) ;                                                                          // suppress temporary data if server cannot handle them
			exit(Rc::Fail,"after job execution : ",e) ;
		}
	}
	try                       { g_start_info.exit() ;                             }
	catch (::string const& e) { exit(Rc::Fail,"cannot cleanup namespaces : ",e) ; }                                                             // NO_COV defensive programming
	//
	return 0 ;
}
