// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include <linux/capability.h>
#include <sched.h>            // unshare
#include <sys/syscall.h>
#include <sys/utsname.h>

#include "disk.hh"
#include "hash.hh"
#include "msg.hh"
#include "process.hh"
#include "time.hh"
#include "trace.hh"
#include "version.hh"

#include "cache/rpc_cache.hh"

#include "rpc_job.hh"

using namespace Cache ;
using namespace Disk  ;
using namespace Hash  ;
using namespace Time  ;

//
// get_os_info
//

::string get_os_info() {
	Trace trace("get_os_info") ;
	::string         res            ;
	::string         id             ;
	::string         version_id     ;
	struct ::utsname uname_info     ; if (::uname(&uname_info)!=0) uname_info.machine[0] = 0 ;                             // report empty in case of error
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
	trace("done",res) ;
	return res ;
}

//
// quarantine
//

void quarantine( ::string const& file , NfsGuard* nfs_guard ) {
	if (!FileInfo(file).tag()) return ;
	//
	::string qf = cat( ADMIN_DIR_S "quarantine/" , file ) ;
	try {
		unlnk (        qf , {.dir_ok=true,.force=true,.nfs_guard=nfs_guard} ) ;
		rename( file , qf , {             .force=true,.nfs_guard=nfs_guard} ) ;
	} catch (::string const& e) {
		throw cat("cannot quarantine : ",e) ;
	}
}

//
// mk_cmd_line
//

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
static const ::uset_s SpecialVars {
	"BASH_ALIASES"
,	"BASH_ENV"
,	"BASHOPTS"
,	"ENV"
,	"EXECIGNORE"
,	"IFS"
,	"SHELLOPTS"
} ;
enum class State : uint8_t {
	None
,	SingleQuote
,	DoubleQuote
,	BackSlash
,	DoubleQuoteBackSlash                                                                                                // after a \ within ""
} ;
static bool is_special( char c , int esc_lvl , bool first=false ) {
	switch (c) {
		case '$' :
		case '`' :            return true ;                                                                             // recognized even in ""
		case '#' :
		case '&' :
		case '(' : case ')' :
		case '*' :
		case ';' :
		case '<' : case '>' :
		case '?' :
		case '[' : case ']' :
		case '|' :            return esc_lvl<2          ;                                                               // recognized anywhere if not quoted
		case '~' :            return esc_lvl<2 && first ;                                                               // recognized as first char of any word
		case '=' :            return esc_lvl<1          ;                                                               // recognized only in first word
		default  :            return false ;
	}
}
bool/*is_simple*/ mk_simple_cmd_line( ::vector_s&/*inout*/ cmd_line , ::string&& cmd , ::string const& std_shell , ::vmap_ss const& cmd_env ) {
	::string const* path = &StdPath ;
	/**/                                 if (cmd_line.size()!=1     ) goto Complex ;                                    // options passed to bash
	/**/                                 if (cmd_line[0]!=std_shell ) goto Complex ;                                    // not standard bash
	for( auto const& [k,v] : cmd_env ) { if (SpecialVars.contains(k)) goto Complex ; if (k=="PATH") path=&v ; }
	{
		::vector_s simple_cmd_line { {} }        ;
		State      state           = State::None ;
		bool       slash_seen      = false       ;
		bool       word_seen       = false       ;                                                                      // if true <=> a new argument has been detected, maybe empty because of quotes
		bool       nl_seen         = false       ;
		bool       cmd_seen        = false       ;
		//
		auto special_cmd = [&]() {
			return simple_cmd_line.size()==1 && !slash_seen && SpecialWords.contains(simple_cmd_line[0]) ;
		} ;
		//
		for( char c : cmd ) {
			slash_seen |= c=='/' && simple_cmd_line.size()==1 ;                                                         // / are only recorgnized in first word
			switch (state) {
				case State::None :
					if (is_special( c , simple_cmd_line.size()>1/*esc_lvl*/ , !simple_cmd_line.back() )) goto Complex ; // complex syntax
					switch (c) {
						case '\n' : nl_seen |= cmd_seen ; [[fallthrough]] ;
						case ' '  :
						case '\t' :
							if (cmd_seen) {
								if (special_cmd()) goto Complex ;                                                       // need to search in $PATH and may be a reserved word or builtin command
								if (word_seen) {
									simple_cmd_line.emplace_back() ;
									word_seen = false ;
									cmd_seen  = true  ;
								}
							}
						break ;
						case '\\' : state = State::BackSlash   ;                                               if (nl_seen) goto Complex ; break ; // multi-line
						case '\'' : state = State::SingleQuote ;                                               if (nl_seen) goto Complex ; break ; // .
						case '"'  : state = State::DoubleQuote ;                                               if (nl_seen) goto Complex ; break ; // .
						default   : simple_cmd_line.back().push_back(c) ; word_seen = true ; cmd_seen = true ; if (nl_seen) goto Complex ; break ; // .
					}
				break ;
				case State::BackSlash :
					simple_cmd_line.back().push_back(c) ;
					state     = State::None ;
					word_seen = true        ;
				break ;
				case State::SingleQuote :
					if (c=='\'') { state = State::None ; word_seen = true ; }
					else           simple_cmd_line.back().push_back(c) ;
				break ;
				case State::DoubleQuote :
					if (is_special( c , 2/*esc_lvl*/ )) goto Complex ;                                                                             // complex syntax
					switch (c) {
						case '\\' : state = State::DoubleQuoteBackSlash ;                    break ;
						case '"'  : state = State::None                 ; word_seen = true ; break ;
						default   : simple_cmd_line.back().push_back(c) ;
					}
				break ;
				case State::DoubleQuoteBackSlash :
					if (!is_special( c , 2/*esc_lvl*/ ))
						switch (c) {
							case '\\' :
							case '\n' :
							case '"'  :                                          break ;
							default   : simple_cmd_line.back().push_back('\\') ;
						}
					simple_cmd_line.back().push_back(c) ;
					state = State::DoubleQuote ;
				break ;
			}
		}
		if (state!=State::None)   goto Complex ;                                                  // syntax error
		if (!word_seen        ) { SWEAR(!simple_cmd_line.back()) ; simple_cmd_line.pop_back() ; } // suppress empty entry created by space after last word
		if (!simple_cmd_line  )   goto Complex ;                                                  // no command
		if (special_cmd()     )   goto Complex ;                                                  // complex syntax
		if (!slash_seen) {                                                                        // search PATH
			for( ::string& p : split(*path,':') ) {
				p << add_slash << simple_cmd_line[0] ;
				if (FileInfo(p).tag()==FileTag::Exe) {
					simple_cmd_line[0] = ::move(p) ;
					goto CmdFound ;
				}
			}
			goto Complex ;                                                                        // command not found
		CmdFound : ;
		}
		cmd_line = ::move(simple_cmd_line) ;
		return true /*is_simple*/;
	}
Complex :
	cmd_line.reserve(cmd_line.size()+2) ;
	cmd_line.emplace_back("-c"       )  ;
	cmd_line.push_back   (::move(cmd))  ;
	return false/*is_simple*/ ;
}

//
// FileAction
//

struct UniqKey {
	// accesses
	bool operator==(UniqKey const&) const = default ;
	// services
	size_t hash() const { return dev+ino ; }
	// data
	dev_t dev = 0 ;
	ino_t ino = 0 ;
} ;

struct UniqEntry {
	size_t     n_lnks     = 0/*garbage*/ ;
	off_t      sz         = 0/*garbage*/ ;
	mode_t     mode       = 0/*garbage*/ ;
	TimeSpec   mtim       = {}           ;
	bool       no_warning = true         ;
	::vector_s files      ;
} ;

bool operator==( TimeSpec const& a , TimeSpec const& b ) {
	return a.tv_sec==b.tv_sec && a.tv_nsec==b.tv_nsec ;
}

::string& operator+=( ::string& os , FileAction const& fa ) {           // START_OF_NO_COV
	/**/                                os << "FileAction(" << fa.tag ;
	if (fa.tag<=FileActionTag::HasFile) os <<','<< fa.sig             ;
	return                              os <<')'                      ;
}                                                                       // END_OF_NO_COV

::string do_file_actions( ::vector_s&/*out*/ unlnks , bool&/*out*/ incremental , ::vmap_s<FileAction>&& pre_actions , NfsGuard* nfs_guard ) {
	::uset_s                  keep_dirs       ;
	::string                  msg             ;
	::string                  trash           ;
	::uset_s                  existing_dirs_s ;
	::umap<UniqKey,UniqEntry> uniq_tab        ;
	//
	auto dir_exists = [&](::string const& f) {
		for( ::string d_s=dir_name_s(f) ; +d_s ; d_s = dir_name_s(d_s) )
			if (!existing_dirs_s.insert(d_s).second) break ;
	} ;
	//
	Trace trace("do_file_actions") ;
	unlnks.reserve(unlnks.size()+pre_actions.size()) ;                                                                         // most actions are unlinks
	for( auto const& [f,a] : pre_actions ) {                                                                                   // pre_actions are adequately sorted
		SWEAR(+f) ;                                                                                                            // acting on root dir is non-sense
		switch (a.tag) {
			case FileActionTag::None           :
			case FileActionTag::Unlink         :
			case FileActionTag::UnlinkWarning  :
			case FileActionTag::UnlinkPolluted : {
				if (nfs_guard) nfs_guard->access(f) ;
				FileStat fs ;
				if (::lstat(f.c_str(),&fs)!=0) {                                                                               // file does not exist, nothing to do
					trace(a.tag,"no_file",f) ;
					continue ;
				}
				dir_exists(f) ;                                                                                                // if a file exists, its dir necessarily exists
				FileSig sig         { fs } ;
				bool    quarantine_ ;
				if (!sig) {
					trace(a.tag,"awkward",f,sig.tag()) ;
					quarantine_ = true ;
				} else {
					quarantine_ =
						sig!=a.sig
					&&	sig.tag()!=Crc::Empty
					&&	( a.crc==Crc::None || !a.crc.valid() || !a.crc.match(Crc(f)) )                                         // only compute crc if file has been modified
					;
				}
				if (quarantine_) {
					quarantine( f , nfs_guard ) ;
					msg << "quarantined "<<mk_file(f)<<'\n' ;
				} else {
					SWEAR(is_lcl(f)) ;
					unlnk(f,{.nfs_guard=nfs_guard}) ;
					if ( a.tag==FileActionTag::None && !a.tflags[Tflag::NoWarning] ) msg <<"unlinked "<<mk_file(f)<<'\n' ;     // if a file has been unlinked, its dir necessarily exists
				}
				trace(a.tag,STR(quarantine_),f) ;
				if (+sig) unlnks.push_back(f) ;
			} break ;
			case FileActionTag::Uniquify : {
				FileStat fs ;
				if (::lstat(f.c_str(),&fs)!=0                  ) { trace(a.tag,"no_file"    ,f) ; continue ;           }       // file does not exist, nothing to do
				if (a.tflags[Tflag::Target]                    ) { trace(a.tag,"incremental",f) ; incremental = true ; }
				dir_exists(f) ;                                                                                                // if file exists, certainly its dir exists as well
				if (   fs.st_nlink<=1                          ) { trace(a.tag,"single"     ,f) ; continue ;           }       // file is already unique (or unlinked in parallel), nothing to do
				if (!( fs.st_mode & (S_IWUSR|S_IWGRP|S_IWOTH) )) { trace(a.tag,"read-only"  ,f) ; continue ;           }       // if file is read-only, assume it is immutable
				if (!  S_ISREG(fs.st_mode)                     ) { trace(a.tag,"awkward"    ,f) ; continue ;           }       // do not handle awkward files and symlinks are immutable
				UniqEntry& e = uniq_tab[{fs.st_dev,fs.st_ino}] ;                                                               // accumulate all links per file identified by dev/inode
				if (!e.files) {      e.n_lnks= fs.st_nlink ;  e.sz= fs.st_size ;  e.mode= fs.st_mode ;  e.mtim= fs.st_mtim ; }
				else          SWEAR( e.n_lnks==fs.st_nlink && e.sz==fs.st_size && e.mode==fs.st_mode && e.mtim==fs.st_mtim ) ; // check consistency
				e.files.push_back(f) ;
				e.no_warning &= a.tflags[Tflag::NoWarning] ;
			} break ;
			case FileActionTag::Mkdir : {
				::string f_s = with_slash(f) ;
				if (!existing_dirs_s.contains(f_s)) mk_dir_s(f_s,{.nfs_guard=nfs_guard}) ;
			} break ;
			case FileActionTag::Rmdir :
				if (!keep_dirs.contains(f))
					try {
						rmdir_s(with_slash(f),nfs_guard) ;
					} catch (::string const&) {                                                                                // if a dir cannot rmdir'ed, no need to try those uphill
						keep_dirs.insert(f) ;
						for( ::string d_s=dir_name_s(f) ; +d_s ; d_s=dir_name_s(d_s) )
							if (!keep_dirs.insert(no_slash(d_s)).second) break ;
					}
			break ;
		DF}                                                                                                                    // NO_COV
	}
	for( auto const& [_,e] : uniq_tab ) {
		SWEAR( e.files.size()<=e.n_lnks , e.n_lnks,e.files ) ;                                                                 // check consistency
		if (e.n_lnks==e.files.size()) { trace("all_lnks",e.files) ; continue ; }                                               // we have all the links, nothing to do
		trace("uniquify",e.n_lnks,e.files) ;
		//
		const char* err = nullptr/*garbage*/ ;
		{	const char* f0   = e.files[0].c_str()                                 ;
			AcFd        rfd  = ::open    ( f0 , O_RDONLY|O_NOFOLLOW             ) ; if (!rfd   ) { err = "cannot open for reading" ; goto Bad ; }
			int         urc  = ::unlink  ( f0                                   ) ; if (urc !=0) { err = "cannot unlink"           ; goto Bad ; }
			AcFd        wfd  = ::open    ( f0 , O_WRONLY|O_CREAT , e.mode       ) ; if (!wfd   ) { err = "cannot open for writing" ; goto Bad ; }
			int         sfrc = ::sendfile( wfd , rfd , nullptr/*offset*/ , e.sz ) ; if (sfrc<0 ) { err = "cannot copy"             ; goto Bad ; }
			for( size_t i : iota(1,e.files.size()) ) {
				if (::unlink(   e.files[i].c_str())!=0) { err = "cannot unlink" ; goto Bad ; }
				if (::link  (f0,e.files[i].c_str())!=0) { err = "cannot link"   ; goto Bad ; }
			}
			TimeSpec times[2] = { {.tv_sec=0,.tv_nsec=UTIME_OMIT} , e.mtim } ;
			::futimens(wfd,times) ;                                                                                            // maintain original date
			if (!e.no_warning) {
				/**/                               msg <<"uniquified"  ;
				if (e.files.size()>1)              msg <<" as a group" ;
				/**/                               msg <<" :"          ;
				for( ::string const& f : e.files ) msg <<' '<< f       ;
				/**/                               msg <<'\n'          ;
			}
		}
		continue ;
	Bad :                                                                                                                      // NO_COV defensive programming
		throw cat(err," while uniquifying ",e.files) ;                                                                         // NO_COV .
	}
	trace("done",localize(msg)) ;
	return msg ;
}

//
// ChrootInfo
//

::string& operator+=( ::string& os , ChrootInfo const& ci ) {                    // START_OF_NO_COV
	/**/                                     os << "ChrootInfo("               ;
	if (+ci.dir_s                          ) os << ci.dir_s                    ;
	if (+ci.actions                        ) os << ','<<ci.actions             ;
	if (+ci.actions[ChrootAction::UserName]) os << ','<<ci.user<<','<<ci.group ;
	return                                   os << ')'                         ;
}                                                                                // END_OF_NO_COV

//
// JobReason
//

::string& operator+=( ::string& os , JobReason const& jr ) { // START_OF_NO_COV
	/**/                               os <<"JR("<< jr.tag ;
	if (jr.tag>=JobReasonTag::HasNode) os <<','<< jr.node  ;
	return                             os <<')'            ;
}                                                            // END_OF_NO_COV

void JobReason::chk() const {
	if (tag<JobReasonTag::HasNode) throw_unless( !node , "bad node" ) ;
}

//
// MsgStderr
//

::string& operator+=( ::string& os , MsgStderr const& ms ) { // START_OF_NO_COV
	return os <<'('<< ms.msg <<','<< ms.stderr <<')' ;
}                                                            // END_OF_NO_COV

//
// DepInfo
//

::string& operator+=( ::string& os , DepInfo const& di ) {           // START_OF_NO_COV
	switch (di.kind()) {
		case DepInfoKind::Crc  : return os <<'('<< di.crc () <<')' ;
		case DepInfoKind::Sig  : return os <<'('<< di.sig () <<')' ;
		case DepInfoKind::Info : return os <<'('<< di.info() <<')' ;
	DF}                                                              // NO_COV
}                                                                    // END_OF_NO_COV

//
// JobRpcReq
//

void JobRpcReq::cache_cleanup() {
	seq_id = -1 ;                 // execution dependnt, 0 is reserved to mean no info but we want a stable info
	job    = 0  ;                 // base dependent
}

void JobRpcReq::chk(bool for_cache) const {
	if (for_cache) {
		throw_unless( seq_id==SeqId(-1) , "bad seq_id",' ',seq_id ) ;
		throw_unless( !job              , "bad job"    ) ;
	}
}

//
// Zlvl
//

::string& operator+=( ::string& os , Zlvl zl ) { // START_OF_NO_COV
	/**/     os <<      zl.tag ;
	if (+zl) os <<':'<< zl.lvl ;
	return   os                ;
}                                                // END_OF_NO_COV

//
// JobSpace
//

::string& operator+=( ::string& os , JobSpace::ViewDescr const& vd ) { // START_OF_NO_COV
	/**/             os <<"ViewDescr("<< vd.phys_s ;
	if (+vd.copy_up) os <<",CU:"<< vd.copy_up      ;
	return           os <<')'                      ;
}                                                                      // END_OF_NO_COV

::string& operator+=( ::string& os , JobSpace const& js ) {                                        // START_OF_NO_COV
	First first ;
	/**/                         os <<"JobSpace("                                                ;
	if ( js._force_creat!=Maybe) os <<first("",",")<<(js._force_creat==No?"!":"")<<"force_creat" ;
	if (+js.lmake_view_s       ) os <<first("",",")<<"L:"<< js.lmake_view_s                      ;
	if (+js.repo_view_s        ) os <<first("",",")<<"R:"<< js.repo_view_s                       ;
	if (+js.tmp_view_s         ) os <<first("",",")<<"T:"<< js.tmp_view_s                        ;
	if (+js.views              ) os <<first("",",")<<"V:"<< js.views                             ;
	return                       os <<')'                                                        ;
}                                                                                                  // END_OF_NO_COV

static void _chroot(::string const& dir) { Trace trace("_chroot",dir) ; throw_unless( ::chroot(dir.c_str())==0 , "cannot chroot to ",dir,rm_slash," : ",StrErr() ) ; }
static void _chdir (::string const& dir) { Trace trace("_chdir" ,dir) ; throw_unless( ::chdir (dir.c_str())==0 , "cannot chdir to " ,dir,rm_slash," : ",StrErr() ) ; }

static void _mount_tmp( ::string const& dst , size_t sz , ::vector<UserTraceEntry>&/*inout*/ user_trace ) {                                       // dst must be dir
	Trace trace("_mount_tmp",dst) ;
	throw_unless( ::mount( nullptr/*src*/ , dst.c_str() , "tmpfs" , 0/*flags*/ , cat("size=",sz).c_str() )==0 , "cannot mount tmp ",dst,rm_slash," of size ",sz," : ",StrErr() ) ;
	user_trace.emplace_back( New/*date*/ , Comment::mount , CommentExt::Tmp , no_slash(dst) ) ;
}
static void _mount_tmp( ::string const& dst , ::vector<UserTraceEntry>&/*inout*/ user_trace ) { _mount_tmp( dst , 50<<20/*sz*/ , user_trace ) ; } // size must be large enough but is not allocated

static void _mount_bind( ::string const& dst , ::string const& src , ::vector<UserTraceEntry>&/*inout*/ user_trace ) { // src and dst may be files or dirs
	Trace trace("_mount_bind",dst,src) ;
	::string src_ = no_slash(src) ;
	::string dst_ = no_slash(dst) ;
	throw_unless( ::mount( src_.c_str() , dst_.c_str() , nullptr/*type*/ , MS_BIND|MS_REC , nullptr/*data*/ )==0 , "cannot bind mount ",src_," onto ",dst_," : ",StrErr() ) ;
	user_trace.emplace_back( New/*date*/ , Comment::mount , CommentExt::Bind , cat(dst_," : ",src_) ) ;
}

static void _mount_proc( ::string const& dst , ::vector<UserTraceEntry>&/*inout*/ user_trace ) {
	Trace trace("_mount_proc",dst) ;
	throw_unless( ::mount( nullptr/*src*/ , dst.c_str() , "proc" , 0/*flags*/ , nullptr/*data*/ )==0 , "cannot mount proc ",dst," : ",::getpid(),' ',StrErr() ) ;
	user_trace.emplace_back( New/*date*/ , Comment::mount , CommentExt::Proc , no_slash(dst) ) ;
}

static void _mount_overlay( ::string const& dst_s , ::vector_s const& srcs_s , ::string const& work_s , ::vector<UserTraceEntry>&/*inout*/ user_trace ) {
	SWEAR( srcs_s.size()>1 , dst_s,srcs_s,work_s ) ;                                                                                                      // use bind mount in that case
	//
	Trace trace("_mount_overlay",dst_s,srcs_s,work_s) ;
	char sep ;
	/**/                                     if (work_s   .find(',')!=Npos) { sep = ',' ; goto Bad1 ; }
	for( NodeIdx i : iota(0,srcs_s.size()) ) if (srcs_s[i].find(',')!=Npos) { sep = ',' ; goto Bad1 ; }
	for( NodeIdx i : iota(1,srcs_s.size()) ) if (srcs_s[i].find(':')!=Npos) { sep = ':' ; goto Bad1 ; }
	{
		::string dst   = no_slash(dst_s ) ;
		First    first ;
		::string                            data  = "userxattr"                                            ;
		for( ::string const& s_s : srcs_s ) data << first(",upperdir=",",lowerdir=",":")<<no_slash(s_s   ) ;
		/**/                                data << ",workdir="                         <<no_slash(work_s) ;
		//
		mk_dir_s(work_s) ;
		if ( ::mount( "overlay" , dst.c_str() , "overlay" , 0 , data.c_str() )!=0 ) {
			::string msg = cat(StrErr()) ;
			/**/                                if (errno!=ENOENT                       )                                             goto Bad2 ;
			/**/                                if (FileInfo(dst_s ).tag()!=FileTag::Dir) { msg << "missing dir "<<dst_s <<rm_slash ; goto Bad2 ; }
			for( ::string const& s_s : srcs_s ) if (FileInfo(s_s   ).tag()!=FileTag::Dir) { msg << "missing dir "<<s_s   <<rm_slash ; goto Bad2 ; }
			/**/                                if (FileInfo(work_s).tag()!=FileTag::Dir) { msg << "missing dir "<<work_s<<rm_slash ; goto Bad2 ; }
		Bad2 :
			throw cat("cannot overlay mount ",dst," to ",data," : ",msg) ;
		}
		user_trace.emplace_back( New/*date*/ , Comment::mount , CommentExt::Overlay , cat(dst," : ",data) ) ;
		return ;
	}
Bad1 :
	::vector_s srcs ; for( ::string const& s_s : srcs_s ) srcs.push_back(no_slash(s_s)) ;
	throw cat("cannot overlay mount ",no_slash(dst_s)," to ",srcs," using ",no_slash(work_s),"with embedded '",sep,'\'') ;
}

static void _atomic_write( ::string const& file , ::string const& data ) {
	Trace trace("_atomic_write",file,data) ;
	AcFd fd { file , {.flags=O_WRONLY,.err_ok=false} } ;
	//            vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	ssize_t cnt = ::write( fd , data.c_str() , data.size() ) ;
	//            ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	throw_unless( cnt>=0                   , "cannot write atomically ",data.size()," bytes to ",file," : ",StrErr()                  ) ;
	throw_unless( size_t(cnt)>=data.size() , "cannot write atomically ",data.size()," bytes to ",file," : only ",cnt," bytes written" ) ;
}

void JobSpace::chk() const {
	if (+lmake_view_s) throw_unless( lmake_view_s.front()=='/' && lmake_view_s.back()=='/'                          , "bad lmake_view" ) ;
	if (+repo_view_s ) throw_unless( repo_view_s .front()=='/' && repo_view_s .back()=='/' && is_canon(repo_view_s) , "bad repo_view"  ) ;
	if (+tmp_view_s  ) throw_unless( tmp_view_s  .front()=='/' && tmp_view_s  .back()=='/' && is_canon(tmp_view_s ) , "bad tmp_view"   ) ;
	for( auto const& [view_s,descr] : views ) {
		/**/                                       throw_unless( is_canon(view_s) , "bad views"        ) ;
		for( ::string const& p_s : descr.phys_s  ) throw_unless( is_canon(p_s   ) , "bad view phys"    ) ;
		for( ::string const& cu  : descr.copy_up ) throw_unless( is_canon(cu    ) , "bad view copy_up" ) ;
	}
}

static void _prepare_user( ::string const& dir_s , ChrootInfo const& chroot_info , uid_t uid , gid_t gid ) {
	if (!chroot_info.actions) return ;                                                                                             // fast path
	//
	if (chroot_info.actions[ChrootAction::UserName]) {
		::string passwd = "root:*:0:0:::\n" ; if (uid!=0) passwd << chroot_info.user  <<":*:"<< uid <<':'<< gid <<":::\n" ;        // cf format man 5 passwd
		::string group  = "root:*:0:\n"     ; if (gid!=0) group  << chroot_info.group <<":*:"<< gid             <<":\n"   ;        // cf format man 5 group
		AcFd( dir_s+"etc/nsswitch.conf" , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444} ).write( "passwd: files\ngroup: files\n" ) ; // ensure we access files
		AcFd( dir_s+"etc/passwd"        , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444} ).write( passwd                          ) ;
		AcFd( dir_s+"etc/group"         , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444} ).write( group                           ) ;
	}
	if (chroot_info.actions[ChrootAction::ResolvConf]) {
		try { AcFd( dir_s+"etc/resolv.conf" , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444} ).write( AcFd("/etc/resolv.conf").read() ) ; } catch (::string const&) {}
	}
}
void JobSpace::enter(
	::vector_s&              /*out  */ report
,	::string  &              /*.    */ repo_root_s
,	::vector<UserTraceEntry>&/*inout*/ user_trace
,	SmallId                            small_id
,	::string   const&                  phy_lmake_root_s
,	::string   const&                  phy_repo_root_s
,	::string   const&                  phy_tmp_dir_s    , bool keep_tmp
,	ChrootInfo const&                  chroot_info
,	::string   const&                  sub_repo_s
,	::vector_s const&                  src_dirs_s
,	bool                               kill_daemons
,	bool                               is_ld_audit
) {
	Trace trace("JobSpace::enter",self,small_id,phy_lmake_root_s,phy_repo_root_s,phy_tmp_dir_s,chroot_info,sub_repo_s,src_dirs_s,STR(kill_daemons),STR(is_ld_audit)) ;
	//
	bool need_chroot = +self || +chroot_info.dir_s ;
	repo_root_s = repo_view_s | phy_repo_root_s ;
	if ( !need_chroot && !kill_daemons ) {
		if (+sub_repo_s) _chdir(sub_repo_s) ;
		_tmp_dir_s = phy_tmp_dir_s ;
		trace("not_done",repo_root_s) ;
		return ;
	}
	//
	::string chroot_dir = chroot_info.dir_s ; if (+chroot_dir) chroot_dir.pop_back() ;                                             // dont use no_slash to properly manage the '/' case
	//
	mk_canon( phy_repo_root_s , sub_repo_s , +chroot_dir ) ;
	//
	bool creat = _force_creat!=No ;
	//
	SWEAR( +phy_lmake_root_s ) ;
	SWEAR( +phy_repo_root_s  ) ;
	if (+tmp_view_s) throw_unless( +phy_tmp_dir_s , "no physical dir for tmp view ",no_slash(tmp_view_s) ) ;
	//
	FileNameIdx repo_depth    = ::count(repo_root_s,'/') - 1                                                              ;        // account for initial and terminal /
	FileNameIdx src_dir_depth = ::max<FileNameIdx>( src_dirs_s , [](::string const& sd_s) { return uphill_lvl(sd_s) ; } ) ;
	if (src_dir_depth>=repo_depth)
		for( ::string const& sd_s : src_dirs_s )
			throw_if( uphill_lvl(sd_s)==src_dir_depth , "too many .. to access relative source dir ",sd_s," from repo view ",repo_root_s,rm_slash) ;
	::string repo_super_s      = dir_name_s( repo_root_s , src_dir_depth ) ; SWEAR(repo_super_s!="/") ;
	::string phy_repo_super_s_ ;
	if (+repo_view_s) {
		::string repo_base_s     = base_name(repo_root_s    ,src_dir_depth) ;
		::string phy_repo_base_s = base_name(phy_repo_root_s,src_dir_depth) ;
		// XXX : implement repo/view mismatch
		// if view and repo do not match, we should :
		// - mount view
		// - mount an empty dir on phy repo to ensure no local deps are accessed that appear to be external
		throw_unless(
			repo_base_s==phy_repo_base_s
		,	"the ",src_dir_depth," last dirs of repo root (",phy_repo_base_s,rm_slash,") and view (",repo_base_s,rm_slash,") must match (not yet implemented) to access relative source dirs"
		) ;
		phy_repo_super_s_ = dir_name_s(phy_repo_root_s,src_dir_depth) ; SWEAR(phy_repo_super_s_!="/") ;
	}
	::string const& phy_repo_super_s   = +repo_view_s ? phy_repo_super_s_ : repo_super_s        ;                                  // fast path : only compute phy_repo_super_s if necessary
	::string const& lmake_root_s       = lmake_view_s | phy_lmake_root_s                        ;
	::string const& tmp_dir_s          = tmp_view_s   | phy_tmp_dir_s                           ;
	bool            bind_lmake         =                 +lmake_view_s || +chroot_dir           ;
	bool            bind_repo          =                 +repo_view_s  || +chroot_dir           ;
	bool            bind_tmp           = +tmp_dir_s && ( +tmp_view_s   || +chroot_dir )         ;
	bool            creat_lmake        = bind_lmake && !FileInfo(chroot_dir+lmake_root_s).tag() ; creat |= creat_lmake ;
	bool            creat_repo         = bind_repo  && !FileInfo(chroot_dir+repo_super_s).tag() ; creat |= creat_repo  ;
	bool            creat_tmp          = bind_tmp   && !FileInfo(chroot_dir+tmp_dir_s   ).tag() ; creat |= creat_tmp   ;
	bool            clean_tmp_dir_here = true                                                   ;
	::vector_s      creat_views_s      ;
	for( auto const& [v_s,_] : views ) {
		if      ( +tmp_dir_s && v_s.starts_with(tmp_dir_s) ) { clean_tmp_dir_here = false ;                                                            }
		else if ( !is_lcl(v_s)                             ) { if (!FileInfo(chroot_dir+mk_glb(v_s,repo_root_s)).tag()) creat_views_s.push_back(v_s) ; }
	}
	creat |= +creat_views_s ;
	//
	// if a dir (or a file) is mounted in tmp dir, we cannot directly clean it up as we should umount it beforehand to avoid unlinking the mounted info
	// but umount is privileged, so what we do instead is forking
	// in child , we are outside the namespace where the mount is not seen and we can clean tmp dir safely
	if ( !clean_tmp_dir_here && +phy_tmp_dir_s ) {
		trace("clean_tmp_process",phy_tmp_dir_s) ;
		if ( pid_t pid=::fork() ; pid!=0 ) {                                                    // in parent
			throw_unless( pid!=-1 , "cannot set up to wait (",StrErr(),") to clean tmp" ) ;
			int   wstatus   ;
			pid_t child_pid = ::waitpid( pid , &wstatus , 0/*flags*/ );
			if (child_pid==-1) {
				Fd::Stderr.write(cat("cannot wait (",StrErr(),") for job to finsh")) ;
				::_exit(+Rc::System) ;                                                          // all the cleanup is done by the child, so nothing to do here
			}
			unlnk( phy_tmp_dir_s , {.dir_ok=true,.abs_ok=true} ) ;                              // clean tmp from outside namespace when child is done
			::_exit(mimic_wstatus(wstatus)) ;                                                   // all the cleanup is done by the child, so nothing to do here
		}
	}
	//
	uid_t uid = ::geteuid() ;                                                                   // must be done before unshare that invents a new user
	gid_t gid = ::getegid() ;                                                                   // .
	//
	trace("creat1",STR(_force_creat),STR(creat_lmake),STR(creat_repo),STR(creat_tmp),STR(kill_daemons),uid,gid) ;
	trace("creat2",lmake_root_s,repo_root_s,tmp_dir_s,repo_super_s                                            ) ;
	int unshare_flags = CLONE_NEWUSER | CLONE_NEWNS ; if (kill_daemons) unshare_flags |= CLONE_NEWPID ;
	//            vvvvvvvvvvvvvvvvvvvvvvvv
	throw_unless( ::unshare(unshare_flags)==0 , "cannot create mount namespace : ",StrErr() ) ;
	//            ^^^^^^^^^^^^^^^^^^^^^^^^
	if (kill_daemons) {
		trace("kill_daemons") ;
		if ( pid_t pid=::fork() ; pid!=0 ) {                                                    // in parent, /!\ must be first fork() after unshare as this is process 1 in namespace
			throw_unless( pid!=-1 , "cannot set up to wait (",StrErr(),") for job to finsh" ) ;
			int   wstatus   ;
			pid_t child_pid ;
			do {
				child_pid = ::wait(&wstatus) ;                                                  // reap orphan if child_pid!=pid
				if (child_pid==-1) {
					Fd::Stderr.write(cat("cannot wait (",StrErr(),") for job to finsh")) ;
					::_exit(+Rc::System) ;                                                      // all the cleanup is done by the child, so nothing to do here
				}
			} while (child_pid!=pid) ;
			::_exit(mimic_wstatus(wstatus)) ;                                                   // all the cleanup is done by the child, so nothing to do here
		}
		_mount_proc( "/proc" , user_trace ) ;
		// find a good starting pid
		// the goal is to minimize risks of pid conflicts between jobs in case pid is used to generate unique filenames as temporary file instead of using TMPDIR, which is quite common
		// to do that we spread pid's among the availale range by setting the first pid used by jos as apart from each other as possible
		// call phi the golden number and NPids the number of available pids
		// spreading is maximized by using phi*NPids as an elementary spacing and id (small_id) as an index modulo NPids
		// this way there is a conflict between job 1 and job 2 when (id2-id1)*phi is near an integer
		// because phi is the irrational which is as far from rationals as possible, and id's are as small as possible, this probability is minimized
		// note that this is over-quality : any more or less random number would do the job : motivation is mathematical beauty rather than practical efficiency
		static constexpr uint32_t FirstPid = 300                                 ;              // apparently, pid's wrap around back to 300
		static constexpr uint64_t NPids    = MAX_PID - FirstPid                  ;              // number of available pid's
		static constexpr uint64_t DeltaPid = (1640531527*NPids) >> n_bits(NPids) ;              // use golden number to ensure best spacing (see above), 1640531527 = (2-(1+sqrt(5))/2)<<32
		//
		pid_t first_pid = FirstPid + ((small_id*DeltaPid)>>(32-n_bits(NPids)))%NPids ;          // DeltaPid on 64 bits to avoid rare overflow in multiplication
		//
		AcFd( "/proc/sys/kernel/ns_last_pid" , {.flags=O_WRONLY|O_TRUNC} ).write(cat(first_pid)) ;
	}
	// mapping uid/gid is necessary to manage overlayfs
	_atomic_write( "/proc/self/setgroups" , "deny"                  ) ;                         // necessary to be allowed to write to gid_map (cf man 7 user_namespaces)
	_atomic_write( "/proc/self/uid_map"   , cat(uid,' ',uid," 1\n") ) ;                         // for each line, format is "id_in_namespace id_in_host size_of_range"
	_atomic_write( "/proc/self/gid_map"   , cat(gid,' ',gid," 1\n") ) ;                         // .
	//
	bool dev_sys_mapped = false ;
	//
	if (creat) {
		::string work_dir_s = cat("/tmp/",uid,"/open-lmake",phy_repo_root_s,small_id,'/') ;     // /run/user would be ideal instead of /tmp (certain to be usable as upper) but does not always exist
		try { unlnk_inside_s(work_dir_s,{.abs_ok=true}) ; } catch (::string const&) {}          // if we need a work dir, we must clean it first as it is not cleaned upon exit ...
		if (+chroot_dir) {                                                                      // ... (ignore errors as dir may not exist)
			::string upper   = work_dir_s+"upper" ;
			::string root    = work_dir_s+"root"  ;
			bool     retried = false              ;
		Retry :
			::string upper_s = with_slash(upper) ; trace("mkdir1",upper_s) ; mk_dir_s(upper_s) ;
			::string root_s  = with_slash(root ) ; trace("mkdir2",root_s ) ; mk_dir_s(root_s ) ;
			if (creat_lmake)                                              { trace("mkdir3",lmake_root_s) ; mk_dir_s(upper+lmake_root_s) ; }
			if (creat_repo )                                              { trace("mkdir4",repo_super_s) ; mk_dir_s(upper+repo_super_s) ; }
			if (creat_tmp  )                                              { trace("mkdir5",tmp_dir_s   ) ; mk_dir_s(upper+tmp_dir_s   ) ; }
			for( ::string const& v_s  : creat_views_s )                   { trace("mkdir6",v_s         ) ; mk_dir_s(upper+v_s         ) ; }
			for( ::string const& sd_s : src_dirs_s    ) if (is_abs(sd_s)) { trace("mkdir7",sd_s        ) ; mk_dir_s(upper+sd_s        ) ; }
			//
			_prepare_user( upper_s , chroot_info , uid , gid ) ;
			//
			try {
				//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				_mount_overlay( root_s , {upper_s,chroot_dir} , work_dir_s+"work/" , user_trace ) ;
				//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			} catch (::string const& e) {
				if (retried) throw ;
				retried = true ;
				_mount_tmp( work_dir_s , user_trace ) ;                                                       // /tmp cannot be used as overlay lower, make a fresh tmp-mount and retry
				goto Retry/*BACKWARD*/ ;
			}
			chroot_dir = ::move(root) ;
		} else {                                                                                              // mount replies ENOENT when trying to map /, so map all opt level dirs
			chroot_dir = work_dir_s+"root" ;
			//
			::vector_s top_lvls = lst_dir_s("/"s) ;
			trace("top_lvls",chroot_dir,top_lvls) ;
			mk_dir_s(with_slash(chroot_dir)) ;
			for( ::string const& f : top_lvls ) {
				::string src_f       = "/"   + f             ;
				::string src_f_s     = src_f + "/"           ;
				::string private_f   = chroot_dir + src_f    ;
				::string private_f_s = with_slash(private_f) ;
				if (
					( bind_lmake &&                                                 src_f_s==lmake_root_s )
				||	( bind_repo  &&                                                 src_f_s==repo_super_s )
				||	( bind_tmp   &&                                                 src_f_s==tmp_dir_s    )
				||	::any_of( views , [&](::pair_s<ViewDescr> const& vs_d) { return src_f_s==vs_d.first ; } ) // views are always bound
				) {
					trace("mkdir8",private_f_s) ;
					mk_dir_s(private_f_s) ;
					continue ;                                                                                // these will be mounted below
				}
				switch (FileInfo(src_f).tag()) { //!                                                                                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					case FileTag::Dir : trace("mkdir9",private_f_s) ; mk_dir_s( private_f_s                                               ) ; _mount_bind(private_f_s,src_f_s,user_trace) ; break ;
					case FileTag::Lnk : trace("symlnk",private_f_s) ; sym_lnk ( private_f   , read_lnk(src_f)                             ) ;                                               break ;
					default           : trace("file"  ,private_f_s) ; AcFd    ( private_f   , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0000} ) ; _mount_bind(private_f  ,src_f  ,user_trace) ;
				} //!                                                                                                                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			}
			dev_sys_mapped = true ;
			auto do_view = [&]( const char* key , ::string const& d_s , FileNameIdx n_uphill=0 ) {
				if (has_dir(d_s)) {                                                                           // XXX : implement non-top lvl non-existing views
					::string                                               msg =  key                                                                                 ;
					for( [[maybe_unused]] FileNameIdx i : iota(n_uphill) ) msg << "/.."                                                                               ;
					/**/                                                   msg << " must be a top level dir or already exist (not yet implemented) : "<<d_s<<rm_slash ;
					throw msg ;
				}
				trace("mkdir10",d_s) ;
				mk_dir_s(chroot_dir+d_s) ;
			} ;
			if (creat_lmake)                           do_view( "lmake_view" , lmake_root_s                 ) ;
			if (creat_repo )                           do_view( "repo_view"  , repo_super_s , src_dir_depth ) ;
			if (creat_tmp  )                           do_view( "tmp_view"   , tmp_dir_s                    ) ;
			for( ::string const& v_s : creat_views_s ) do_view( "view"       , v_s                          ) ;
		}
	} //!                                                                       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	if (bind_lmake )                                                            _mount_bind( chroot_dir+lmake_root_s , phy_lmake_root_s , user_trace ) ;
	if (bind_repo  )                                                            _mount_bind( chroot_dir+repo_super_s , phy_repo_super_s , user_trace ) ;
	if (bind_tmp   )                                                            _mount_bind( chroot_dir+tmp_dir_s    , phy_tmp_dir_s    , user_trace ) ;
	if (+chroot_dir) for( ::string const& sd_s : src_dirs_s ) if (is_abs(sd_s)) _mount_bind( chroot_dir+sd_s         , sd_s             , user_trace ) ;
	//                                                                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	auto mk_entry = [&]( ::string const& dir_s , ::string const& abs_dir_s , bool path_is_lcl ) {
		SWEAR( is_dir_name(dir_s) , dir_s ) ;
		if (path_is_lcl) report.emplace_back(no_slash(dir_s)) ;
		trace("mkdir11",abs_dir_s) ;
		mk_dir_s(abs_dir_s) ;
	} ;
	//
	size_t work_idx = 0 ;
	for( auto const& [view_s,descr] : views ) {
		bool       view_is_tmp = +tmp_dir_s && view_s.starts_with(tmp_dir_s)                                                                 ;
		bool       view_is_lcl = is_lcl(view_s)                                                                                              ;
		bool       view_is_ext = !view_is_lcl && !view_is_tmp                                                                                ;
		::string   abs_view_s  = chroot_dir + ( view_is_tmp ? tmp_view_s+substr_view(view_s,tmp_dir_s.size()) : mk_glb(view_s,repo_root_s) ) ;
		::vector_s abs_phys_s  ;
		::vector_s abs_cu_dsts ;
		//
		for( size_t i : iota(descr.phys_s.size()) ) {
			::string const& phy_s      = descr.phys_s[i]                                                                                ;
			bool            phy_is_tmp = +tmp_dir_s && phy_s.starts_with(tmp_dir_s)                                                     ;
			bool            phy_is_lcl = is_lcl(phy_s) && +phy_s                                                                        ;
			bool            phy_is_ext = !phy_is_lcl && !phy_is_tmp                                                                     ;
			::string        abs_phy_s  = phy_is_tmp ? phy_tmp_dir_s+substr_view(phy_s,tmp_dir_s.size()) : mk_glb(phy_s,phy_repo_root_s) ;
			//
			if (!phy_is_ext) {
				mk_entry( phy_s , abs_phy_s , phy_is_lcl ) ;
			} else {
				FileTag tag = FileInfo(abs_phy_s).tag() ;
				if (tag!=FileTag::Dir) throw cat("cannot map ",no_slash(view_s)," to non-existent ",no_slash(phy_s)) ;
			}
			abs_phys_s.push_back(abs_phy_s) ;
			if (i==0) {
				if      (phy_is_ext    ) SWEAR(descr.phys_s.size()==1) ; // else dont know where to create the work dir which must be on the same filesystem as upper
				else if (+descr.copy_up)                                 // prepare copy up destination in upper
					for( ::string const& cu  : descr.copy_up ) {
						if (is_dir_name(cu)) {                               mk_entry(phy_s+cu ,abs_phy_s+cu ,phy_is_lcl) ; abs_cu_dsts.push_back({}          ) ; } // for dirs, just create it
						else                 { ::string cud=dir_name_s(cu) ; mk_entry(phy_s+cud,abs_phy_s+cud,phy_is_lcl) ; abs_cu_dsts.push_back(abs_phy_s+cu) ; }
					}
			} else {
				if (+abs_cu_dsts) {                                                                         // try to copy up remaining destinations
					SWEAR( descr.copy_up.size()==abs_cu_dsts.size() , descr.copy_up,abs_cu_dsts ) ;
					for ( size_t j : iota(descr.copy_up.size()) )
						if ( +abs_cu_dsts[j] && +cpy( abs_phy_s+descr.copy_up[j] , abs_cu_dsts[j] ) )
							abs_cu_dsts[j] = {} ;                                                           // copy is done from this lower, dont try following lowers
				}
			}
		}
		//
		if (!view_is_ext) mk_entry( view_s , abs_view_s , view_is_lcl ) ;                                   // external views are created above
		//
		if ( view_is_tmp && !keep_tmp ) swear_prod( !clean_tmp_dir_here , abs_view_s ) ;                    // ensure we do not clean up dirs mounted tmp upon exit
		if (abs_phys_s.size()==1) {
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			_mount_bind( abs_view_s , abs_phys_s[0] , user_trace ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		} else {
			::string const& upper_s = descr.phys_s[0] ;
			::string        work_s  =                                                                       // if not in the repo, it must be in tmp
				is_lcl(upper_s) ? cat(phy_repo_root_s,PrivateAdminDirS,"work/",small_id,'.',work_idx++,'/')
				:                 cat(no_slash(abs_phys_s[0])         ,".work."            ,work_idx++,'/') // upper is in tmp (there may be several views to same upper)
			;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			_mount_overlay( abs_view_s , abs_phys_s , work_s , user_trace ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		}
	}
	if (+chroot_dir) {
		if (!dev_sys_mapped) { //!                                                        vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			{ ::string d_s = chroot_dir+"/dev/"  ; trace("mkdir12",d_s) ; mk_dir_s(d_s) ; _mount_bind( d_s , "/dev/"  , user_trace ) ; }
			{ ::string d_s = chroot_dir+"/sys/"  ; trace("mkdir13",d_s) ; mk_dir_s(d_s) ; _mount_bind( d_s , "/sys/"  , user_trace ) ; }
			{ ::string d_s = chroot_dir+"/proc/" ; trace("mkdir14",d_s) ; mk_dir_s(d_s) ; _mount_bind( d_s , "/proc/" , user_trace ) ; }
		} //!                                                                             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		//vvvvvvvvvvvvvvvvv
		_chroot(chroot_dir) ;
		//^^^^^^^^^^^^^^^^^
		user_trace.emplace_back( New/*date*/ , Comment::chroot , CommentExts() , chroot_dir ) ;
	}
	if ( +repo_view_s || +chroot_dir || +sub_repo_s ) {
		::string d = no_slash(repo_root_s+sub_repo_s) ;
		//vvvvvvv
		_chdir(d) ;
		//^^^^^^^
		user_trace.emplace_back( New/*date*/ , Comment::chdir , CommentExts() , d ) ;
	}
	// only set _tmp_dir_s once tmp mount and chroot are done so as to ensure not to unlink in the underlying dir
	if (clean_tmp_dir_here) _tmp_dir_s = tmp_dir_s ;                                                        // if we have mounted something in tmp, we cant clean it before unmount
	user_trace.emplace_back( New/*date*/ , Comment::EnteredNamespace ) ;
	trace("done",report) ;
}

void JobSpace::exit() {
	Trace trace("JobSpace::exit",_tmp_dir_s) ;
	if (+_tmp_dir_s) try { unlnk(_tmp_dir_s,{.dir_ok=true,.abs_ok=true}) ; } catch (::string const&) {} // best effort
}

// XXX! : implement recursive views
// for now, phys cannot englobe or lie within a view, but when it is to be implemented, it is here
::vmap_s<::vector_s> JobSpace::flat_phys_s() const {
	::vmap_s<::vector_s> res ; res.reserve(views.size()) ;
	for( auto const& [view_s,descr] : views ) res.emplace_back(view_s,descr.phys_s) ;
	return res ;
}

void _mk_canon( ::string&/*inout*/ dir_s , const char* key , bool root_ok , bool contains_repo_ok , ::string const& phy_repo_root_s ) {
	bool is_root = dir_s=="/" ;
	if ( !dir_s || (root_ok&&is_root)                                      ) return ;
	if ( !is_canon(dir_s)                                                  ) dir_s = Disk::mk_canon(dir_s) ;
	if ( is_root                                                           ) throw cat(key," cannot be /"                                          ) ;
	if ( !is_abs(dir_s)                                                    ) throw cat(key," must be absolute : ",no_slash(dir_s)                  ) ;
	if ( !contains_repo_ok && phy_repo_root_s.starts_with(dir_s          ) ) throw cat("repository cannot lie within ",key,' ',no_slash(dir_s)     ) ;
	if (                      dir_s          .starts_with(phy_repo_root_s) ) throw cat(key,' ',no_slash(dir_s)," cannot be local to the repository") ;
}

void JobSpace::mk_canon( ::string const& phy_repo_root_s , ::string const& sub_repo_s , bool has_chroot ) {
	if (_force_creat!=Maybe) return ;
	_force_creat = No | has_chroot ;
	//                                      root_ok contains_repo_ok
	_mk_canon( lmake_view_s , "lmake view" , false , true          , phy_repo_root_s ) ;
	_mk_canon( repo_view_s  , "repo view"  , false , true          , phy_repo_root_s ) ;
	_mk_canon( tmp_view_s   , "tmp view"   , false , false         , phy_repo_root_s ) ;                                     // deps would not be reported if recognized as tmp
	if ( +lmake_view_s && +repo_view_s ) {
		if (lmake_view_s.starts_with(repo_view_s )) throw cat("lmake view ",no_slash(lmake_view_s)," cannot lie within repo view " ,no_slash(repo_view_s )) ;
		if (repo_view_s .starts_with(lmake_view_s)) throw cat("repo view " ,no_slash(repo_view_s )," cannot lie within lmake view ",no_slash(lmake_view_s)) ;
	}
	if ( +lmake_view_s && +tmp_view_s ) {
		if (lmake_view_s.starts_with(tmp_view_s  )) throw cat("lmake view ",no_slash(lmake_view_s)," cannot lie within tmp view "  ,no_slash(tmp_view_s  )) ;
		if (tmp_view_s  .starts_with(lmake_view_s)) throw cat("tmp view "  ,no_slash(tmp_view_s  )," cannot lie within lmake view ",no_slash(lmake_view_s)) ;
	}
	if ( +repo_view_s && +tmp_view_s ) {
		if (repo_view_s .starts_with(tmp_view_s  )) throw cat("repo view " ,no_slash(repo_view_s )," cannot lie within tmp view "  ,no_slash(tmp_view_s  )) ;
		if (tmp_view_s  .starts_with(repo_view_s )) throw cat("tmp view "  ,no_slash(tmp_view_s  )," cannot lie within repo view " ,no_slash(repo_view_s )) ;
	}
	//
	::string const& repo_root_s = repo_view_s | phy_repo_root_s ;
	//
	auto do_dir = [&]( ::string& dir_s , bool for_view ) {
		SWEAR( is_dir_name(dir_s) , dir_s ) ;
		if (!is_canon(dir_s)) dir_s = Disk::mk_canon(dir_s)        ;
		/**/                  dir_s = mk_glb( dir_s , sub_repo_s ) ;
		if ( _force_creat==No && +repo_view_s ) {
			if (for_view) _force_creat |= dir_s.starts_with(phy_repo_root_s) || phy_repo_root_s.starts_with(dir_s) ;         // avoid confusion
			else          _force_creat |= dir_s.starts_with(repo_view_s    ) || repo_view_s    .starts_with(dir_s) ;         // .
		}
		if (for_view) { if (dir_s.starts_with(repo_root_s    )) dir_s.erase(0,repo_root_s    .size()) ; }                    // only mounted dir contains actual repo
		else          { if (dir_s.starts_with(phy_repo_root_s)) dir_s.erase(0,phy_repo_root_s.size()) ; }                    // .
	} ;
	//
	for( auto& [view_s,descr] : views ) { //!              for_view
		/**/                                  do_dir(view_s,true  ) ;
		for( ::string& phy_s : descr.phys_s ) do_dir(phy_s ,false ) ;
	}
	for( auto& [view_s,descr] : views ) {
		if (!view_s) throw cat("cannot map to full repo view") ;
		bool view_is_tmp = +tmp_view_s && view_s.starts_with(tmp_view_s) ;
		bool view_is_lcl = is_lcl(view_s)                                ;
		bool view_is_ext = !view_is_tmp && !view_is_lcl                  ;
		//
		if ( +descr.copy_up && descr.phys_s.size()<=1 ) throw cat("cannot copy up in non-overlay view ",no_slash(view_s)                         ) ;
		if ( repo_root_s .starts_with(view_s)         ) throw cat("cannot map to view "                ,no_slash(view_s)," containing repo"      ) ;
		if ( lmake_view_s.starts_with(view_s)         ) throw cat("cannot map to view "                ,no_slash(view_s)," containing lmake view") ;
		if ( tmp_view_s  .starts_with(view_s)         ) throw cat("cannot map to view "                ,no_slash(view_s)," containing tmp view"  ) ;
		bool first = true ;
		for( ::string const& phy_s : descr.phys_s ) {
			bool phy_is_tmp = +tmp_view_s && phy_s.starts_with(tmp_view_s) ;
			bool phy_is_lcl = is_lcl(phy_s)                                ;
			bool phy_is_ext = !phy_is_tmp && !phy_is_lcl                   ;
			if(  phy_is_ext && first && +descr.copy_up        ) throw cat("cannot copy up to external upper dir ",no_slash(phy_s)," in view "         ,no_slash(view_s)) ;
			if(  phy_is_ext && first && descr.phys_s.size()>1 ) throw cat("cannot map external upper dir "       ,no_slash(phy_s)," to overlay view " ,no_slash(view_s)) ;
			if( !phy_is_ext && view_is_ext                    ) throw cat("cannot map local or tmp dir "         ,no_slash(phy_s)," to external view ",no_slash(view_s)) ;
			//
			if (_force_creat==No) {                                                                                          // if we see recursive mapping, force chroot to cancel this recursion
				for( auto const& [v_s,_] : views ) {
					if (+phy_s) { if ( phy_s.starts_with(v_s) || v_s.starts_with(phy_s) ) { _force_creat = Yes ; break ; } }
					else        { if ( is_lcl(v_s)                                      ) { _force_creat = Yes ; break ; } } // empty phy_s is the full repo
				}
			}
			first = false ;
		}
		for( ::string const& cu : descr.copy_up ) {
			if (!cu       ) throw cat("cannot copy up full dir"    ," in view ",no_slash(view_s)) ;
			if (is_abs(cu)) throw cat("cannot copy up absolute ",cu," in view ",no_slash(view_s)) ;
		}
	}
}

//
// CacheRemoteSide
//

::string& operator+=( ::string& os , CacheRemoteSide const& crs ) {
	First first ;
	/**/                os << "CacheRemoteSide("           ;
	if (+crs.dir_s    ) os << first("",",")<<crs.dir_s     ;
	if (+crs.service  ) os << first("",",")<<crs.service   ;
	if (+crs.max_rate ) os << first("",",")<<crs.max_rate  ;
	if (+crs.file_sync) os << first("",",")<<crs.file_sync ;
	if (+crs.perm_ext ) os << first("",",")<<crs.perm_ext  ;
	return              os << ')'                          ;
}

CacheRemoteSide::UploadDigest CacheRemoteSide::upload( uint32_t conn_id , Delay exe_time , ::vmap_s<TargetDigest> const& targets , ::vector<FileInfo> const& target_fis , Zlvl zlvl ) const {
	Trace trace(CacheChnl,"upload",conn_id,targets.size(),zlvl) ;
	SWEAR( targets.size()==target_fis.size() , targets.size(),target_fis.size() ) ;
	//
	DiskSz targets_sz  = 0 ;
	::vector<DiskSz> target_szs ; target_szs.reserve(target_fis.size()) ;
	for( FileInfo fi : target_fis ) {
		targets_sz += fi.sz ;
		target_szs.push_back(fi.sz) ;
	}
	trace("size",targets_sz) ;
	//
	float        rate      = targets_sz/float(exe_time)           ; throw_unless( rate<=max_rate , "too fast : ",rate,'>',max_rate ) ; // job is too easy to reproduce, no interest to cache
	ClientSockFd fd        { service }                            ;
	::string     magic_str = fd.read(sizeof(CacheMagic))          ; throw_unless( magic_str.size()==sizeof(CacheMagic) , "bad_answer_sz" ) ;
	uint64_t     magic_    = decode_int<uint64_t>(&magic_str[0])  ; throw_unless( magic_          ==CacheMagic         , "bad_answer"    ) ;
	DiskSz       z_max_sz  = DeflateFd::s_max_sz(targets_sz,zlvl) ;
	//
	trace("z_max_size",z_max_sz) ;
	//
	OMsgBuf( CacheRpcReq{ .proc=CacheRpcProc::Upload , .conn_id=conn_id , .reserved_sz=z_max_sz } ).send(fd) ;
	auto reply = IMsgBuf().receive<CacheRpcReply>( fd , Maybe/*once*/ ) ;
	//
	throw_unless( +reply            , "no reply from cache" ) ; SWEAR( reply.proc==CacheRpcProc::Upload , reply.proc ) ;
	throw_unless(  reply.upload_key , reply.msg             ) ;
	//
	try {
		NfsGuard  nfs_guard { file_sync                                                                                                                            } ;
		AcFd      dfd       { dir_s+reserved_file(reply.upload_key)+"-data" , {.flags=O_WRONLY|O_CREAT|O_TRUNC,.mod=0444,.perm_ext=perm_ext,.nfs_guard=&nfs_guard} } ;
		DeflateFd data_fd   { ::move(dfd) , zlvl                                                                                                                   } ;
		OMsgBuf(target_szs).send( data_fd , {}/*key*/ ) ;
		//
		for( NodeIdx ti : iota(targets.size()) ) {
			::pair_s<TargetDigest> const& entry = targets[ti]            ;
			::string               const& tn    = entry.first            ;
			FileTag                       tag   = entry.second.sig.tag() ;
			DiskSz                        sz    = target_fis[ti].sz      ;
			switch (tag) {
				case FileTag::Lnk : {
					trace("lnk_from",tn,sz) ;
					::string l = read_lnk(tn) ; throw_unless( l.size()==sz                      , "cannot readlink ",tn ) ;
					data_fd.write(l) ;          throw_unless( FileSig(tn)==target_fis[ti].sig() , "unstable "       ,tn ) ;            // ensure cache entry is reliable by checking file *after* copy
				}
				break ;
				case FileTag::Reg :
				case FileTag::Exe :
					if (sz) {
						trace("read_from",tn,sz) ;
						data_fd.send_from( AcFd(tn,{.flags=O_RDONLY|O_NOFOLLOW}) , sz ) ;
						throw_unless( FileSig(tn)==target_fis[ti].sig() , "unstable ",tn ) ;                                           // ensure cache entry is reliable by checking file *after* copy
					}
				[[fallthrough]] ;                                                                                                      // empty executable is not tagged as Empty
				case FileTag::Empty : trace("empty_from",tn) ; break ;
			DN}
		}
		data_fd.flush() ;                                                                                                              // update data_fd.sz
		trace("done",data_fd.z_sz) ;
		return { .upload_key=reply.upload_key , .z_sz=data_fd.z_sz } ;
	} catch (::string const& e) {
		dismiss(reply.upload_key) ;
		trace("failed") ;
		throw ;
	}
}

void CacheRemoteSide::dismiss( CacheUploadKey upload_key , uint32_t conn_id ) const {
	OMsgBuf( CacheRpcReq{ .proc=CacheRpcProc::Dismiss , .conn_id=conn_id , .upload_key=upload_key } ).send( ClientSockFd(service) , {}/*key*/ ) ;
}

//
// JobStartRpcReq
//

::string& operator+=( ::string& os , JobStartRpcReq const& jsrr ) {                                              // START_OF_NO_COV
	return os << "JobStartRpcReq(" << jsrr.seq_id <<','<< jsrr.job <<','<< jsrr.service <<','<< jsrr.msg <<')' ;
}                                                                                                                // END_OF_NO_COV

void JobStartRpcReq::cache_cleanup() {
	JobRpcReq::cache_cleanup() ;
	service = {} ;               // execution dependent
}

void JobStartRpcReq::chk(bool for_cache) const {
	JobRpcReq::chk(for_cache) ;
	if (for_cache) throw_unless( !service , "bad connection info" ) ;
}

//
// JobDigest
//

template<> void JobDigest<>::cache_cleanup() {
	upload_key     = {} ;                      // no recursive info
	refresh_codecs = {} ;                      // execution dependent
	for( auto& [_,td] : targets ) {
		SWEAR(!td.pre_exist) ;                 // else cannot be a candidate for upload
		td.sig = td.sig.tag() ;                // forget date, just keep tag
	}
	for( auto& [_,dd] : deps )
		dd.hot = false ;                       // execution dependent
}

//
// JobEndRpcReq
//

::string& operator+=( ::string& os , UserTraceEntry const& ete ) {                       // START_OF_NO_COV
	return os <<"UserTraceEntry("<< ete.date <<','<< ete.step() <<','<< ete.file <<')' ;
}                                                                                        // END_OF_NO_COV

::string& operator+=( ::string& os , TargetDigest const& td ) {  // START_OF_NO_COV
	First first ;
	/**/                  os <<"TargetDigest("                 ;
	if ( td.pre_exist   ) os <<first("",",")<< "pre_exist"     ;
	if ( td.written     ) os <<first("",",")<< "written"       ;
	if (+td.tflags      ) os <<first("",",")<< td.tflags       ;
	if (+td.extra_tflags) os <<first("",",")<< td.extra_tflags ;
	if (+td.crc         ) os <<first("",",")<< td.crc          ;
	if (+td.sig         ) os <<first("",",")<< td.sig          ;
	return                os <<')'                             ;
}                                                                // END_OF_NO_COV

::string& operator+=( ::string& os , JobEndRpcReq const& jerr ) {                                                                            // START_OF_NO_COV
	return os << "JobEndRpcReq(" << jerr.seq_id <<','<< jerr.job <<','<< jerr.digest <<','<< jerr.phy_tmp_dir_s <<','<< jerr.dyn_env <<')' ;
}                                                                                                                                            // END_OF_NO_COV

void JobEndRpcReq::cache_cleanup() {
	JobRpcReq::cache_cleanup() ;
	digest.cache_cleanup() ;
	phy_tmp_dir_s = {} ;     // execution dependent
}

void JobEndRpcReq::chk(bool for_cache) const {
	JobRpcReq::chk(for_cache) ;
	digest.    chk(for_cache) ;
	/**/             throw_unless( !phy_tmp_dir_s || (phy_tmp_dir_s.front()=='/'&&phy_tmp_dir_s.back()=='/'&&is_canon(phy_tmp_dir_s)) , "bad phy_tmp_dir"       ) ;
	/**/             throw_unless( end_date<=New                                                                                      , "bad end_date"          ) ;
	if (+msg_stderr) throw_unless( digest.has_msg_stderr                                                                              , "incoherent msg/stderr" ) ;
}

//
// JobStartRpcReply
//

::string& operator+=( ::string& os , JobStartRpcReply const& jsrr ) {      // START_OF_NO_COV
	/**/                        os << "JobStartRpcReply("<<jsrr.rule     ;
	/**/                        os <<','  << jsrr.autodep_env            ;
	if (+jsrr.job_space       ) os <<','  << jsrr.job_space              ;
	if ( jsrr.keep_tmp        ) os <<','  << "keep"                      ;
	if (+jsrr.ddate_prec      ) os <<','  << jsrr.ddate_prec             ;
	/**/                        os <<','  << mk_printable(cat(jsrr.env)) ; // env may contain the non-printable PassMrkr value
	/**/                        os <<','  << jsrr.interpreter            ;
	/**/                        os <<','  << jsrr.kill_sigs              ;
	if ( jsrr.live_out        ) os <<','  << "live_out"                  ;
	if (+jsrr.phy_lmake_root_s) os <<','  << jsrr.phy_lmake_root_s       ;
	if ( jsrr.nice            ) os <<','  << "nice:"<<jsrr.nice          ;
	if ( jsrr.stderr_ok       ) os <<','  << "stderr_ok"                 ;
	/**/                        os <<','  << jsrr.method                 ;
	if (+jsrr.network_delay   ) os <<','  << jsrr.network_delay          ;
	if (+jsrr.pre_actions     ) os <<','  << jsrr.pre_actions            ;
	/**/                        os <<','  << jsrr.small_id               ;
	if (+jsrr.star_matches    ) os <<','  << jsrr.star_matches           ;
	if (+jsrr.deps            ) os <<'<'  << jsrr.deps                   ;
	if (+jsrr.static_matches  ) os <<'>'  << jsrr.static_matches         ;
	if (+jsrr.stdin           ) os <<'<'  << jsrr.stdin                  ;
	if (+jsrr.stdout          ) os <<'>'  << jsrr.stdout                 ;
	if (+jsrr.timeout         ) os <<','  << jsrr.timeout                ;
	/**/                        os <<','  << jsrr.cmd                    ; // last as it is most probably multi-line
	return                      os <<')'                                 ;
}                                                                          // END_OF_NO_COV

void JobStartRpcReply::mk_canon( ::string const& phy_repo_root_s ) {
	_mk_canon( chroot_info.dir_s , "chroot_dir" , true /*root_ok*/ , true/*contains_repo_ok*/ , phy_repo_root_s ) ;
	_mk_canon( phy_lmake_root_s  , "lmake_root" , false/*.      */ , true/*.               */ , phy_repo_root_s ) ;
	job_space.mk_canon( phy_repo_root_s , autodep_env.sub_repo_s , +chroot_info.dir_s ) ;
}

void JobStartRpcReply::_mk_lmake_version() {
	if (!lmake_version.is_remote) {
		lmake_version.std_path            = STD_PATH            ;
		lmake_version.python              = PYTHON              ;
		lmake_version.py_ld_library_path  = PY_LD_LIBRARY_PATH  ;
		lmake_version.python2             = PYTHON2             ;
		lmake_version.py2_ld_library_path = PY2_LD_LIBRARY_PATH ;
		return ;
	}
	try {
		[[maybe_unused]] bool has_ld_audit = false ;
		uint64_t              v_job        = 0     ;
		for( ::string const& line : AcFd(phy_lmake_root_s+"_lib/version.py",{.err_ok=true}).read_lines() ) {
			if (line[0]=='#') continue ;
			size_t   pos = line.find('=')                ; if (pos==Npos) continue ;
			::string key = strip(line.substr(0    ,pos)) ;
			::string val = strip(line.substr(pos+1    )) ;
			switch (key[0]) {
				case 'h' : if (key=="has_ld_audit"       ) has_ld_audit                      = (val=="True")              ; break ;
				case 'j' : if (key=="job"                ) v_job                             = from_string<uint64_t>(val) ; break ;
				case 'p' : if (key=="py_ld_library_path" ) lmake_version.py_ld_library_path  = val.substr(1,val.size()-2) ;         // suppress quotes
				else       if (key=="py2_ld_library_path") lmake_version.py2_ld_library_path = val.substr(1,val.size()-2) ;         // .
				else       if (key=="python"             ) lmake_version.python              = val.substr(1,val.size()-2) ;         // .
				else       if (key=="python2"            ) lmake_version.python2             = val.substr(1,val.size()-2) ;         // .
				break ;
				case 's' : if (key=="std_path"           ) lmake_version.std_path            = val.substr(1,val.size()-2) ; break ; // .
			DN}
		}
		throw_unless( v_job                   , "expected version ",Version::Job," (for jobs) not found" ) ;
		throw_unless( v_job==Version::Job     , "version ",v_job,"!=",Version::Job," (for jobs)"         ) ;
		throw_unless( +lmake_version.python   , "python path not found"                                  ) ;
		throw_unless( +lmake_version.std_path , "standard PATH not found"                                ) ;
		#if HAS_LD_AUDIT
			if (method==AutodepMethod::LdAudit) throw_unless( has_ld_audit , "ld_audit is not supported as autodep method" ) ;
		#endif
	} catch (::string const& e) {
		throw cat("cannot execute job with incompatible open-lmake (",e,") as per file _lib/version.py in root dir ",phy_lmake_root_s,rm_slash) ;
	}
}

void JobStartRpcReply::enter(
		::vector_s&              /*out  */ accesses
	,	::string  &              /*.    */ repo_root_s
	,	::vector<UserTraceEntry>&/*inout*/ user_trace
	,	::string const&                    phy_repo_root_s
	,	::string const&                    phy_tmp_dir_s
) {
	Trace trace("JobStartRpcReply::enter",phy_repo_root_s,phy_tmp_dir_s) ;
	_mk_lmake_version() ;
	//
	autodep_env.repo_root_s = job_space.repo_view_s | phy_repo_root_s ;
	autodep_env.tmp_dir_s   = job_space.tmp_view_s  | phy_tmp_dir_s   ;
	if (+phy_tmp_dir_s) {
		try                       { mk_dir_empty_s( phy_tmp_dir_s , {.abs_ok=true} ) ; }
		catch (::string const& e) { throw "cannot create tmp dir : "+e ;               }
	} else {
		if (+job_space.tmp_view_s) throw cat("cannot map tmp dir ",job_space.tmp_view_s," to nowhere") ;
	}
	//
	job_space.enter(
		/*out*/  accesses
	,	/*.  */  repo_root_s
	,	/*inout*/user_trace
	,	         small_id
	,	         phy_lmake_root_s
	,	         phy_repo_root_s
	,	         phy_tmp_dir_s          , keep_tmp
	,	         chroot_info
	,	         autodep_env.sub_repo_s
	,	         autodep_env.src_dirs_s
	,		     kill_daemons
	,
		#if HAS_LD_AUDIT
			method==AutodepMethod::LdAudit
		#else
			false
		#endif
	) ;
	trace("done",accesses,repo_root_s) ;
}

template<bool NoSlash,class T> static bool/*match*/ _handle_var( ::string& s/*inout*/ , size_t& d /*inout*/, const char* key , T const& val , bool brace ) {
	size_t len   = ::strlen(key)  ;
	size_t start = d+1/*$*/+brace ; //!                             match
	/**/         if (!substr_view(s,start).starts_with(key)) return false ;
	if (brace) { if (             s[start+len]!='}'        ) return false ; }
	else       { if (is_word_char(s[start+len])            ) return false ; }
	//
	::string v = val ; if constexpr (NoSlash) rm_slash(v) ;
	s.replace( d , 1/*$*/+brace+len+brace , v ) ;
	d += v.size() ;
	return true/*match*/ ;
}
void JobStartRpcReply::update_val( ::string&/*inout*/ v , ::string const& phy_repo_root_s , ::string const& phy_tmp_dir_s , SeqId seq_id ) const {
	for( size_t d=0 ;; d++ ) {
		d = v.find('$',d) ;
		if (d==Npos) break ;
		bool brace = v[d+1/*$*/]=='{' ;
		size_t k = d+1/*$*/+brace ;
		switch (v[k]) { //!       NoSlash inout inout
			case 'L' : _handle_var<true >( v   , d  , "LMAKE_ROOT" , job_space.lmake_view_s|phy_lmake_root_s                       , brace ) ; break ;
			case 'R' : _handle_var<true >( v   , d  , "REPO_ROOT" , (job_space.repo_view_s|phy_repo_root_s)+autodep_env.sub_repo_s , brace ) ; break ;
			case 'P' :
				switch (v[k+1/*P*/]) { //!                            NoSlash inout inout
					case 'H' :                             _handle_var<true >( v   , d  , "PHYSICAL_LMAKE_ROOT"     , phy_lmake_root_s                        , brace )
					||                                     _handle_var<true >( v   , d  , "PHYSICAL_REPO_ROOT"      , phy_repo_root_s +autodep_env.sub_repo_s , brace )
					||         ( +phy_tmp_dir_s         && _handle_var<true >( v   , d  , "PHYSICAL_TMPDIR"         , phy_tmp_dir_s                           , brace ) )
					||                                     _handle_var<true >( v   , d  , "PHYSICAL_TOP_REPO_ROOT"  , phy_repo_root_s                         , brace )   ; break ;
					case 'Y' :                             _handle_var<false>( v   , d  , "PYTHON"                  , lmake_version.python                    , brace )
					||                                     _handle_var<false>( v   , d  , "PYTHON_LD_LIBRARY_PATH"  , lmake_version.py_ld_library_path        , brace )
					||         ( +lmake_version.python2 && _handle_var<false>( v   , d  , "PYTHON2"                 , lmake_version.python2                   , brace ) )
					||         ( +lmake_version.python2 && _handle_var<false>( v   , d  , "PYTHON2_LD_LIBRARY_PATH" , lmake_version.py2_ld_library_path       , brace ) ) ; break ;
				DN}
			break ;
			case 'S' :
				switch (v[k+1/*S*/]) { //!NoSlash inout inout
					case 'E' : _handle_var<false>( v   , d  , "SEQUENCE_ID" , cat(seq_id  )          , brace ) ; break ;
					case 'H' : _handle_var<false>( v   , d  , "SHELL"       , Bash                   , brace ) ; break ;
					case 'M' : _handle_var<false>( v   , d  , "SMALL_ID"    , cat(small_id)          , brace ) ; break ;
					case 'T' : _handle_var<false>( v   , d  , "STD_PATH"    , lmake_version.std_path , brace ) ; break ;
				DN}
			break ;
			case 'T' :
				switch (v[k+1/*T*/]) { //!                  NoSlash inout inout
					case 'M' : +phy_tmp_dir_s && _handle_var<true >( v   , d  , "TMPDIR"        , job_space.tmp_view_s |phy_tmp_dir_s   , brace ) ; break ;
					case 'O' :                   _handle_var<true >( v   , d  , "TOP_REPO_ROOT" , job_space.repo_view_s|phy_repo_root_s , brace ) ; break ;
				DN}
			break ;
		DN}
	}
}

void JobStartRpcReply::update_env( ::vmap_ss&/*out*/  dyn_env , ::string const& phy_repo_root_s , ::string const& phy_tmp_dir_s , SeqId seq_id ) {
	::string const& tmp_dir_s    = job_space.tmp_view_s | phy_tmp_dir_s ;
	bool            seen_tmp_dir = !phy_tmp_dir_s                       ;
	//
	if (!phy_tmp_dir_s) ::erase_if( env , [](::pair_ss const& k_v) { return k_v.first=="TMPDIR" ; } ) ;
	for( auto& [k,v] : env ) {
		if      ( +phy_tmp_dir_s && k=="TMPDIR" ) { seen_tmp_dir = true ; v = no_slash(tmp_dir_s) ;                        }
		if      ( v!=PassMrkr                   )   update_val( v , phy_repo_root_s , phy_tmp_dir_s , seq_id ) ;
		else if ( has_env(k)                    ) { ::string ev=get_env(k) ; dyn_env.emplace_back(k,ev) ; v = ::move(ev) ; } // use value from environment (typically from slurm)
	}
	if (!seen_tmp_dir) env.emplace_back( "TMPDIR" , no_slash(tmp_dir_s) )                      ;
	if (+interpreter ) update_val( interpreter[0] , phy_repo_root_s , phy_tmp_dir_s , seq_id ) ;
}

void JobStartRpcReply::exit() {
	job_space.exit() ;
}

void JobStartRpcReply::cache_cleanup() {
	autodep_env.fast_report_pipe = {}    ; // execution dependent
	cache                        = {}    ; // no recursive info
	key                          = {}    ; // .
	live_out                     = false ; // execution dependent
	nice                         = -1    ; // .
	pre_actions                  = {}    ; // .
}

void JobStartRpcReply::chk(bool for_cache) const {
	autodep_env.chk(for_cache) ;
	job_space  .chk(         ) ;
	if (+phy_lmake_root_s)                    throw_unless( phy_lmake_root_s.front()=='/' && phy_lmake_root_s.back()=='/' , "bad lmake_root"     ) ;
	/**/                                      throw_unless( method<All<AutodepMethod>                                     , "bad autoded_method" ) ;
	/**/                                      throw_unless( network_delay>=Delay()                                        , "bad networkd_delay" ) ;
	for( auto const& [f,_] : pre_actions    ) throw_unless( is_canon(f)                                                   , "bad file_action"    ) ;
	for( auto const& [t,_] : static_matches ) throw_unless( is_canon(t)                                                   , "bad target"         ) ;
	/**/                                      throw_unless( is_canon(stdin ,true /*ext_ok*/,true/*empty_ok*/)             , "bad stdin"          ) ;
	/**/                                      throw_unless( is_canon(stdout,false/*.     */,true/*.       */)             , "bad stdout"         ) ;
	/**/                                      throw_unless( timeout>=Delay()                                              , "bad timeout"        ) ;
	if (for_cache) {
		throw_unless( !cache             , "bad cache"       ) ;
		throw_unless( !key               , "bad key"         ) ;
		throw_unless( !live_out          , "bad live_out"    ) ;
		throw_unless(  nice==uint8_t(-1) , "bad nice"        ) ;
		throw_unless( !pre_actions       , "bad pre_actions" ) ;
	}
}

//
// JobMngtRpcReq
//

::string& operator+=( ::string& os , JobMngtRpcReq const& jmrr ) {                                // START_OF_NO_COV
	/**/               os << "JobMngtRpcReq(" << jmrr.proc <<','<< jmrr.seq_id <<','<< jmrr.job ;
	if (+jmrr.fd     ) os <<','<< jmrr.fd                                                       ;
	if (+jmrr.targets) os <<','<< jmrr.targets                                                  ;
	if (+jmrr.deps   ) os <<','<< jmrr.deps                                                     ;
	if (+jmrr.txt    ) os <<','<< jmrr.txt                                                      ;
	return             os <<')'                                                                 ;
}                                                                                                 // END_OF_NO_COV

//
// JobMngtRpcReply
//

::string& operator+=( ::string& os , JobMngtRpcReply const& jmrr ) {                     // START_OF_NO_COV
	/**/                     os << "JobMngtRpcReply(" << jmrr.proc <<','<< jmrr.seq_id ;
	if (+jmrr.fd           ) os <<','<< jmrr.fd                                        ;
	if (+jmrr.verbose_infos) os <<','<< jmrr.verbose_infos                             ;
	if (+jmrr.txt          ) os <<','<< jmrr.txt                                       ;
	/**/                     os <<','<< jmrr.ok                                        ;
	return                   os <<')'                                                  ;
}                                                                                        // END_OF_NO_COV
