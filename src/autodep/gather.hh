// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/wait.h>

#include <latch>
#include <thread>

#include "disk.hh"
#include "hash.hh"
#include "msg.hh"
#include "process.hh"
#include "time.hh"
#include "trace.hh"

#include "rpc_job.hh"
#include "rpc_job_exec.hh"

#include "env.hh"

// When several sockets are opened to send depend & target data, we are not sure of the order between these reports because of system buffers.
// We could have decided to synchronize each report, which may be expensive in performance.
// We chose to lose some errors, i.e. Update's may be seen as Write's, as each ambiguity is resolved by considering the Write is earliest and read latest possible.
// This way, we do not generate spurious errors.
// To do so, we maintain, for each access entry (i.e. a file), a list of sockets that are unordered, i.e. for which a following Write could actually have been done before by the user.

ENUM( GatherKind // epoll events
,	Stdout
,	Stderr
,	ServerReply
,	ChildStart   // just a marker, not actually used as epoll event
,	ChildEnd
,	JobMaster
,	JobSlave
,	ServerMaster
,	ServerSlave
)

ENUM( KillStep
,	None
,	Report
,	Kill   // must be last as following values are used
)

struct Gather {                                                                                                 // NOLINT(clang-analyzer-optin.performance.Padding) prefer alphabetical order
	friend ::ostream& operator<<( ::ostream& , Gather const& ) ;
	using Kind = GatherKind    ;
	using Proc = JobExecProc   ;
	using Jerr = JobExecRpcReq ;
	using Crc  = Hash::Crc     ;
	using PD   = Time::Pdate   ;
	using DI   = DepInfo       ;
	struct AccessInfo {
		friend ::ostream& operator<<( ::ostream& , AccessInfo const& ) ;
		// cxtors & casts
		AccessInfo() = default ;
		//
		bool operator==(AccessInfo const&) const = default ;
		// accesses
		::pair<PD,Accesses> first_read() const {
			::pair<PD,Access> res = {PD::Future,{}} ;                                                  // normally, initial Access is not used, but in case, choose the most ubiquitous access
			for( Access a : iota(All<Access>) ) {
				if (!digest.accesses[a]) continue ;
				if (read[+a]>res.first ) continue ;
				if (read[+a]<res.first ) res = {read[+a],a} ;
				else                     res.second |= a ;
			}
			return res ;
		}
		// services
		void update( PD , AccessDigest , DI const& ) ;
		//
		void chk() const ;
		// data
		// seen detection : we record the earliest date at which file has been as existing to detect situations where file is non-existing, then existing, then non-existing
		// this cannot be seen on file date has there is no date for non-existing files
		PD           read[N<Access>] { PD::Future , PD::Future , PD::Future } ; static_assert((N<Access>)==3) ; // first access (or ignore) date for each access
		PD           write           = PD::Future                             ;                                 // first write  (or ignore) date
		PD           target          = PD::Future                             ;                                 // first date at which file was known to be a target
		PD           seen            = PD::Future                             ;                                 // first date at which file has been seen existing
		DI           dep_info        ;                                                                          // state when first read
		AccessDigest digest          ;
	} ;
	// statics
private :
	static void _s_do_child( void* self_ , Fd report_fd , ::latch* ready ) { reinterpret_cast<Gather*>(self_)->_do_child(report_fd,ready) ; }
	// services
	void _solve( Fd , Jerr& jerr) ;
	// Fd for trace purpose only
	void _new_access( Fd , PD    , ::string&& file , AccessDigest    , DI const&    , ::string const& comment ) ;
	void _new_access(      PD pd , ::string&& f    , AccessDigest ad , DI const& di , ::string const& c       ) { _new_access({},pd,::move(f),ad,di,c) ; }
	//
	void _new_accesses( Fd fd , Jerr&& jerr ) {
		for( auto& [f,dd] : jerr.files ) _new_access( fd , jerr.date , ::move(f) , jerr.digest , dd , jerr.txt ) ;
	}
	void _new_guards( Fd fd , Jerr&& jerr ) {                                                                   // fd for trace purpose only
		Trace trace("_new_guards",fd,jerr.txt) ;
		for( auto& [f,_] : jerr.files ) { trace(f) ; guards.insert(::move(f)) ; }
	}
	void _kill          ( bool force          ) ;
	void _send_to_server( Fd fd , Jerr&& jerr ) ;
public : //!                                                                                                           crc_file_info
	void new_target( PD pd , ::string const& t , ::string const& c="s_target" ) { _new_access(pd,::copy(t),{.write=Yes},{}          ,c) ; }
	void new_unlnk ( PD pd , ::string const& t , ::string const& c="s_unlnk"  ) { _new_access(pd,::copy(t),{.write=Yes},{}          ,c) ; } // new_unlnk is used for internal wash
	void new_guard (         ::string const& f                                ) { guards.insert(f) ;                                      }
	//
	void new_dep ( PD pd , ::string&&            dep  , Accesses a               , ::string const& c="s_dep"  ) { _new_access( pd , ::move(dep) , {.accesses=a} , Disk::FileInfo(dep) , c ) ; }
	void new_deps( PD    , ::vmap_s<DepDigest>&& deps , ::string const& stdin={}                              ) ;
	void new_exec( PD    , ::string const&       exe  ,                            ::string const&  ="s_exec" ) ;
	//
	void sync( Fd sock , JobExecRpcReply const&  jerr ) {
		try { OMsgBuf().send(sock,jerr) ; } catch (::string const&) {}                                // dont care if we cannot report the reply to job
	}
	//
	Status exec_child() ;
	//
	void reorder(bool at_end) ;                                                                       // reorder accesses by first read access and suppress superfluous accesses
private :
	Fd   _spawn_child(                               ) ;
	void _do_child   ( Fd report_fd , ::latch* ready ) ;
	// data
public :
	::vector_s                        cmd_line         ;
	Fd                                child_stdin      = Fd::Stdin                                  ;
	Fd                                child_stdout     = Fd::Stdout                                 ;
	Fd                                child_stderr     = Fd::Stderr                                 ;
	umap_s<NodeIdx   >                access_map       ;
	vmap_s<AccessInfo>                accesses         ;
	in_addr_t                         addr             = NoSockAddr                                 ; // local addr to which we can be contacted by running job
	::atomic<bool>                    as_session       = false                                      ; // if true <=> process is launched in its own group
	AutodepEnv                        autodep_env      ;
	::function<::vmap_s<DepDigest>()> cur_deps_cb      = [&]()->::vmap_s<DepDigest> { return {} ; } ;
	::string                          cwd_s            ;
	PD                                end_date         ;
	 ::map_ss const*                  env              = nullptr                                    ;
	pid_t                             first_pid        = 0                                          ;
	uset_s                            guards           ;                                              // dir creation/deletion that must be guarded against NFS
	JobIdx                            job              = 0                                          ;
	::vector<uint8_t>                 kill_sigs        ;                                              // signals used to kill job
	bool                              live_out         = false                                      ;
	AutodepMethod                     method           = AutodepMethod::Dflt                        ;
	::string                          msg              ;                                              // contains error messages not from job
	Time::Delay                       network_delay    = Time::Delay(1)                             ; // 1s is reasonable when nothing is said
	pid_t                             pid              = -1                                         ; // pid to kill
	bool                              seen_tmp         = false                                      ;
	SeqId                             seq_id           = 0                                          ;
	ServerSockFd                      server_master_fd ;
	::string                          service_mngt     ;
	PD                                start_date       ;
	::string                          stdout           ;                                              // contains child stdout if child_stdout==Pipe
	::string                          stderr           ;                                              // contains child stderr if child_stderr==Pipe
	Time::Delay                       timeout          ;
	int                               wstatus          = 0                                          ;
private :
	::map_ss            _add_env       ;
	Child               _child         ;
	::jthread           _ptrace_thread ;
	::umap<Fd,::string> _codec_files   ;
	PD                  _end_timeout   = PD::Future ;
	PD                  _end_child     = PD::Future ;
	PD                  _end_kill      = PD::Future ;
	size_t              _kill_step     = 0          ;
	NodeIdx             _parallel_id   = 0          ;                                                 // id to identify parallel deps
	bool                _timeout_fired = false      ;
	BitMap<Kind>        _wait          ;                                                              // events we are waiting for
} ;
