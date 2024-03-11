// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/wait.h>

#include "disk.hh"
#include "hash.hh"
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

struct GatherDeps {
	friend ::ostream& operator<<( ::ostream& os , GatherDeps const& ad ) ;
	using Proc = JobExecRpcProc ;
	using Crc  = Hash::Crc      ;
	using PD   = Time::Pdate    ;
	using DD   = Time::Ddate    ;
	using CD   = CrcDate        ;
	struct AccessInfo {
		friend ::ostream& operator<<( ::ostream& , AccessInfo const& ) ;
		// cxtors & casts
		AccessInfo() = default ;
		//
		bool operator==(AccessInfo const&) const = default ;
		// accesses
		bool is_dep() const { return digest.write==No ; }
		// services
		void update( PD , bool phony_ok_ , AccessDigest , CD const& , Bool3 confirm , NodeIdx parallel_id_ ) ;
		//
		void chk() const ;
		// data
		PD           first_read        ;         // if +digest.accesses , first access date
		PD           first_write       ;         // if  digest.write!=No, first write/unlink date
		PD           last_write        ;         // if  digest.write!=No, last  write/unlink date (always confirmed)
		CD           crc_date          ;         // state when first read
		AccessDigest digest            ;
		NodeIdx      parallel_id       = 0     ;
		bool         phony_ok       :1 = false ; // if false <=> prevent phony flag so as to hide washing to user
		bool         first_confirmed:1 = false ; // if digest.write!=No, first write is confirmed
		bool         last_confirmed :1 = false ; // if digest.write!=No, last  write is confirmed (for user report only)
		bool         seen           :1 = false ; // if true <= file has been seen existing, this bit is important if file does not exist when first read, then is created externally, ...
	} ;                                          // ... then is read again, then unlinked (if fugitive state is not recorded, everything looks as if file never existed)
	struct ServerReply {
		IMsgBuf  buf        ;                    // buf to assemble the reply
		Fd       fd         ;                    // fd to forward reply to
		::string codec_file ;
	} ;
	// cxtors & casts
public :
	GatherDeps(       ) = default ;
	GatherDeps(NewType) { init() ; }
	//
	void init() { master_fd.listen() ; }
	// services
private :
	void _fix_auto_date( Fd , JobExecRpcReq& jerr) ;
	// Fd for trace purpose only
	void _new_access( Fd    , PD    , bool phony_ok , ::string&&   , AccessDigest    , CD const&    , Bool3 confirm , ::string const& comment ) ;
	void _new_access(         PD pd , bool po       , ::string&& f , AccessDigest ad , CD const& cd , Bool3 confirm , ::string const& c       ) { _new_access({},pd,po  ,::move(f),ad,cd,confirm,c) ; }
	void _new_access( Fd fd , PD pd ,                 ::string&& f , AccessDigest ad , CD const& cd , Bool3 confirm , ::string const& c       ) { _new_access(fd,pd,true,::move(f),ad,cd,confirm,c) ; }
	void _new_access(         PD pd ,                 ::string&& f , AccessDigest ad , CD const& cd , Bool3 confirm , ::string const& c       ) { _new_access({},pd,true,::move(f),ad,cd,confirm,c) ; }
	//
	void _new_accesses( Fd fd , JobExecRpcReq&& jerr ) {
		parallel_id++ ;
		for( auto& [f,dd] : jerr.files ) _new_access( fd , jerr.date , ::move(f) , jerr.digest , dd , jerr.confirm , jerr.txt ) ;
	}
	void _new_guards( Fd fd , JobExecRpcReq&& jerr ) {                                                            // fd for trace purpose only
		Trace trace("_new_guards",fd,jerr.txt) ;
		for( auto& [f,_] : jerr.files ) { trace(f) ; guards.insert(::move(f)) ; }
	}
	void _codec( ServerReply&& sr , JobRpcReply const& jrr , ::string const& comment="codec" ) {
		Trace trace("_codec",jrr) ;
		_new_access( sr.fd , PD::s_now() , ::move(sr.codec_file) , {.accesses=Access::Reg} , jrr.crc , Yes/*confirm*/ , comment ) ;
	}
public : //!                                                                                    phony_ok                        crc_date confirm
	void new_target( PD pd , ::string const& t , ::string const& c="s_target" ) { _new_access(pd,       ::copy(t),{.write=Yes  },{}     ,Yes   ,c) ; }
	void new_unlnk ( PD pd , ::string const& t , ::string const& c="s_unlnk"  ) { _new_access(pd,false ,::copy(t),{.write=Maybe},{}     ,Yes   ,c) ; } // new_unlnk is used for internal wash
	void new_guard (         ::string const& f                                ) { guards.insert(f) ;                                                 }
	//
	void new_deps( PD , ::vmap_s<DepDigest>&& deps , ::string const& stdin={}       ) ;
	void new_exec( PD , ::string const& exe        , ::string const&      ="s_exec" ) ;
	//
	void sync( Fd sock , JobExecRpcReply const&  jerr ) {
		try { OMsgBuf().send(sock,jerr) ; } catch (::string const&) {}                                            // dont care if we cannot report the reply to job
	}
	//
	Status exec_child( ::vector_s const& args , Fd child_stdin=Fd::Stdin , Fd child_stdout=Fd::Stdout , Fd child_stderr=Fd::Stderr ) ;
	//
	bool/*done*/ kill(int sig=-1) ;                                                                               // is sig==-1, use best effort to kill job
	//
	void reorder() ;                                                                                              // reorder accesses by first read access
	// data
	::function<Fd/*reply*/(JobExecRpcReq     &&)> server_cb    = [](JobExecRpcReq     &&)->Fd   { return {} ; } ; // function to contact server when necessary, return error by default
	::function<void       (::string_view const&)> live_out_cb  = [](::string_view const&)->void {             } ; // function to report live output, dont report by default
	::function<void       (                    )> kill_job_cb  = [](                    )->void {             } ; // function to kill job
	ServerSockFd                                  master_fd    ;
	in_addr_t                                     addr         = NoSockAddr                                     ; // local addr to which we can be contacted by running job
	::atomic<bool>                                as_session   = false                                          ; // if true <=> process is launched in its own group
	AutodepMethod                                 method       = AutodepMethod::Dflt                            ;
	AutodepEnv                                    autodep_env  ;
	Time::Delay                                   timeout      ;
	pid_t                                         pid          = -1                                             ; // pid to kill
	bool                                          killed       = false                                          ; // do not start as child is supposed to be already killed
	::vector<uint8_t>                             kill_sigs    ;                                                  // signals used to kill job
	::string                                      chroot       ;
	::string                                      cwd          ;
	 ::map_ss const*                              env          = nullptr                                        ;
	vmap_s<AccessInfo>                            accesses     ;
	umap_s<NodeIdx   >                            access_map   ;
	uset_s                                        guards       ;                                                  // dir creation/deletion that must be guarded against NFS
	NodeIdx                                       parallel_id  = 0                                              ; // id to identify parallel deps
	bool                                          seen_tmp     = false                                          ;
	int                                           wstatus      = 0/*garbage*/                                   ;
	Fd                                            child_stdout ;                                                  // fd used to gather stdout
	Fd                                            child_stderr ;                                                  // fd used to gather stderr
	::string                                      stdout       ;                                                  // contains child stdout if child_stdout==Pipe
	::string                                      stderr       ;                                                  // contains child stderr if child_stderr==Pipe
	::string                                      msg          ;                                                  // contains error messages not from job
private :
	mutable ::mutex _pid_mutex ;
} ;
