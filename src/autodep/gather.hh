// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/wait.h>

#include "disk.hh"
#include "hash.hh"
#include "msg.hh"
#include "process.hh"
#include "rpc_job.hh"
#include "time.hh"
#include "trace.hh"

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
,	ChildStart  // just a marker, not actually used as epoll event
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

struct Gather {
	friend ::ostream& operator<<( ::ostream& os , Gather const& ad ) ;
	using Kind = GatherKind    ;
	using Proc = JobExecProc   ;
	using Jerr = JobExecRpcReq ;
	using Crc  = Hash::Crc     ;
	using PD   = Time::Pdate   ;
	using CD   = CrcDate       ;
	struct AccessInfo {
		friend ::ostream& operator<<( ::ostream& , AccessInfo const& ) ;
		// cxtors & casts
		AccessInfo() = default ;
		//
		bool operator==(AccessInfo const&) const = default ;
		// accesses
		PD first_read() const {
			PD res = PD::Future ;
			for( Access a : All<Access> ) if ( digest.accesses[a] && res>read[+a] ) res = read[+a] ;
			return res ;
		}
		// services
		void update( PD , AccessDigest , CD const& , NodeIdx parallel_id_ ) ;
		//
		void chk() const ;
		// data
		// seen detection : we record the earliest date at which file has been as existing to detect situations where file is non-existing, then existing, then non-existing
		// this cannot be seen on file date has there is no date for non-existing files
		PD           read[N<Access>] { PD::Future , PD::Future , PD::Future } ; static_assert((N<Access>)==3) ;                                // first access (or ignore) date for each access
		PD           write           = PD::Future                             ;                                                                // first write  (or ignore) date
		PD           target          = PD::Future                             ;                                                                // first date at which file was known to be a target
		PD           seen            = PD::Future                             ;                                                                // first date at which file has been seen existing
		CD           crc_date        ;                                                                                                         // state when first read
		NodeIdx      parallel_id     = 0                                      ;
		AccessDigest digest          ;
	} ;
	// services
private :
	void _solve( Fd , Jerr& jerr) ;
	// Fd for trace purpose only
	void _new_access( Fd , PD    , ::string&& file , AccessDigest    , CD const&    , bool parallel , ::string const& comment ) ;
	void _new_access(      PD pd , ::string&& f    , AccessDigest ad , CD const& cd , bool p        , ::string const& c       ) { _new_access({},pd,::move(f),ad,cd,p,c) ; }
	//
	void _new_accesses( Fd fd , Jerr&& jerr ) {
		bool parallel = false ;
		for( auto& [f,dd] : jerr.files ) { _new_access( fd , jerr.date , ::move(f) , jerr.digest , dd , parallel , jerr.txt ) ; parallel = true ; }
	}
	void _new_guards( Fd fd , Jerr&& jerr ) {                                                                                                  // fd for trace purpose only
		Trace trace("_new_guards",fd,jerr.txt) ;
		for( auto& [f,_] : jerr.files ) { trace(f) ; guards.insert(::move(f)) ; }
	}
	void _kill          ( KillStep , Child const& ) ;
	void _send_to_server( Fd fd , Jerr&& jerr     ) ;
public : //!                                                                                                           crc_date parallel
	void new_target( PD pd , ::string const& t , ::string const& c="s_target" ) { _new_access(pd,::copy(t),{.write=Yes},{}     ,false  ,c) ; }
	void new_unlnk ( PD pd , ::string const& t , ::string const& c="s_unlnk"  ) { _new_access(pd,::copy(t),{.write=Yes},{}     ,false  ,c) ; } // new_unlnk is used for internal wash
	void new_guard (         ::string const& f                                ) { guards.insert(f) ;                                         }
	//
	void new_deps( PD , ::vmap_s<DepDigest>&& deps , ::string const& stdin={}       ) ;
	void new_exec( PD , ::string const& exe        , ::string const&      ="s_exec" ) ;
	//
	void sync( Fd sock , JobExecRpcReply const&  jerr ) {
		try { OMsgBuf().send(sock,jerr) ; } catch (::string const&) {}         // dont care if we cannot report the reply to job
	}
	//
	Status exec_child( ::vector_s const& args , Fd child_stdin=Fd::Stdin , Fd child_stdout=Fd::Stdout , Fd child_stderr=Fd::Stderr ) ;
	//
	void reorder(bool at_end) ;                                                // reorder accesses by first read access and suppress superfluous accesses
private :
	void _spawn_child( Child& , ::vector_s const& args , Fd child_stdin=Fd::Stdin , Fd child_stdout=Fd::Stdout , Fd child_stderr=Fd::Stderr ) ;
	// data
public :
	umap_s<NodeIdx   >                access_map       ;
	vmap_s<AccessInfo>                accesses         ;
	in_addr_t                         addr             = NoSockAddr          ; // local addr to which we can be contacted by running job
	::atomic<bool>                    as_session       = false               ; // if true <=> process is launched in its own group
	AutodepEnv                        autodep_env      ;
	::string                          chroot           ;
	::function<::vmap_s<DepDigest>()> cur_deps_cb      ;
	::string                          cwd              ;
	Time::Pdate                       end_time         ;
	 ::map_ss const*                  env              = nullptr             ;
	uset_s                            guards           ;                       // dir creation/deletion that must be guarded against NFS
	JobIdx                            job              = 0                   ;
	::vector<uint8_t>                 kill_sigs        ;                       // signals used to kill job
	bool                              live_out         = false               ;
	AutodepMethod                     method           = AutodepMethod::Dflt ;
	::string                          msg              ;                       // contains error messages not from job
	Time::Delay                       network_delay    ;
	pid_t                             pid              = -1                  ; // pid to kill
	bool                              seen_tmp         = false               ;
	SeqId                             seq_id           = 0                   ;
	ServerSockFd                      server_master_fd ;
	::string                          service_mngt     ;
	Time::Pdate                       start_time       ;
	::string                          stdout           ;                       // contains child stdout if child_stdout==Pipe
	::string                          stderr           ;                       // contains child stderr if child_stderr==Pipe
	Time::Delay                       timeout          ;
	int                               wstatus          = 0                   ;
private :
	::umap<Fd,::string> _codec_files   ;
	bool                _kill_reported = false ;
	NodeIdx             _parallel_id   = 0     ;                               // id to identify parallel deps
	BitMap<Kind>        _wait          ;                                       // events we are waiting for
} ;
