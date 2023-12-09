// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/wait.h>

#include "disk.hh"
#include "process.hh"
#include "rpc_job.hh"
#include "time.hh"

#include "env.hh"

// When several sockets are opened to send depend & target data, we are not sure of the order between these reports because of system buffers.
// We could have decided to synchronize each report, which may be expensive in performance.
// We chose to lose some errors, i.e. Update's may be seen as Write's, as each ambiguity is resolved by considering the Write is earliest and read latest possible.
// This way, we do not generate spurious errors.
// To do so, we maintain, for each access entry (i.e. a file), a list of sockets that are unordered, i.e. for which a following Write could actually have been done before by the user.

struct GatherDeps {
	friend ::ostream& operator<<( ::ostream& os , GatherDeps const& ad ) ;
	using Accesses     = Disk::Accesses              ;
	using AccessDigest = JobExecRpcReq::AccessDigest ;
	using Proc         = JobExecRpcProc              ;
	using DD           = Time::Ddate                 ;
	using PD           = Time::Pdate                 ;
	using FI           = Disk::FileInfo              ;
	struct AccessInfo {
		friend ::ostream& operator<<( ::ostream& , AccessInfo const& ) ;
		// cxtors & casts
		AccessInfo() = default ;
		AccessInfo( PD pd , Tflags tfs_ ) : access_date{pd} , tflags{tfs_} {}
		//
		bool operator==(AccessInfo const&) const = default ;                   // XXX : why is this necessary ?!?
		// accesses
		bool is_dep() const { return digest.idle() && tflags[Tflag::Dep] ; }
		// services
		void update( PD pd , DD dd , AccessDigest const& ad , NodeIdx parallel_id_ ) ;
		// data
		PD           access_date      ;                    // first access date
		PD           first_write_date ;                    // if !digest.idle(), first write/unlink date
		PD           last_write_date  ;                    // if !digest.idle(), last  write/unlink date
		AccessDigest digest           ;
		DD           file_date        ;                    // if +digest.accesses , date of file when read as first access
		NodeIdx      parallel_id      = 0 ;
		Tflags       tflags           ;                    // resulting flags after appliation of info flags modifiers
	} ;
	// cxtors & casts
public :
	GatherDeps(       ) = default ;
	GatherDeps(NewType) { init() ; }
	void init() {
		master_fd.listen() ;
	}
	// services
private :
	bool/*new*/ _new_access( PD , ::string const& , DD , AccessDigest const& , NodeIdx parallel_id , ::string const& comment={} ) ;
	//
	void _new_accesses(JobExecRpcReq const& jerr) {
		SWEAR(!jerr.auto_date) ;
		parallel_id++ ;
		for( auto const& [f,dd] : jerr.files ) _new_access( jerr.date , f , dd , jerr.digest , parallel_id , jerr.comment ) ;
	}
public :
	void new_target( PD pd , ::string const& t , Tflags n , Tflags p             , ::string const& c="target" ) { _new_access(pd,t,{},{.write=true,.neg_tflags=n,.pos_tflags=p},0/*parallel_id*/,c) ; }
	void new_dep   ( PD pd , ::string const& d , DD dd , Accesses a , Dflags dfs , ::string const& c="dep"    ) { _new_access(pd,d,dd,{.accesses=a,.dflags=dfs                },0/*parallel_id*/,c) ; }
	//
	void static_deps( PD , ::vmap_s<DepDigest> const& static_deps , ::string const& stdin={}                           ) ;
	void new_exec   ( PD , ::string const& exe                                               , ::string const& ="exec" ) ;

	//
	void sync( Fd sock , JobExecRpcReply const&  jerr ) {
		OMsgBuf().send(sock,jerr) ;
	}
	//
	Status exec_child( ::vector_s const& args , Fd child_stdin=Fd::Stdin , Fd child_stdout=Fd::Stdout , Fd child_stderr=Fd::Stderr ) ;
	//
	bool/*done*/ kill(int sig) {
		::unique_lock lock{_pid_mutex} ;
		killed = true ;                                                        // prevent child from starting if killed before
		if (pid<=1      ) return false                 ;
		if (create_group) return kill_group  (pid,sig) ;
		else              return kill_process(pid,sig) ;
	}
	//
	void reorder() ;                                                           // reorder accesses by first read access
	// data
	::function<Fd/*reply*/(JobExecRpcReq     &&)> server_cb    = [](JobExecRpcReq     &&)->Fd     { return {}               ; } ; // function to contact server when necessary, return error by default
	::function<void       (::string_view const&)> live_out_cb  = [](::string_view const&)->void   {                           } ; // function to report live output, dont report by default
	::function<Tflags     (::string      const&)> tflags_cb    = [](::string      const&)->Tflags { return UnexpectedTflags ; } ; // function to compute tflags from target before modifiers
	::function<void       (                    )> kill_job_cb  = [](                    )->void   {                           } ; // function to kill job
	ServerSockFd                                  master_fd    ;
	in_addr_t                                     addr         = NoSockAddr                                                     ; // local addr to which we can be contacted by running job
	bool                                          create_group = false                                                          ; // if true <=> process is launched in its own group
	AutodepMethod                                 method       = AutodepMethod::Dflt                                            ;
	AutodepEnv                                    autodep_env  ;
	Time::Delay                                   timeout      ;
	pid_t                                         pid          = -1                                                             ; // pid to kill
	bool                                          killed       = false                                                          ; // do not start as child is supposed to be already killed
	::vector<uint8_t>                             kill_sigs    ;                                                                  // signals used to kill job
	::string                                      chroot       ;
	::string                                      cwd          ;
	 ::map_ss const*                              env          = nullptr                                                        ;
	vmap_s<AccessInfo>                            accesses     ;
	umap_s<NodeIdx   >                            access_map   ;
	NodeIdx                                       parallel_id  = 0                                                              ; // id to identify parallel deps
	bool                                          seen_tmp     = false                                                          ;
	int                                           wstatus      = 0/*garbage*/                                                   ;
	::string                                      stdout       ;                                                                  // contains child stdout if child_stdout==Pipe
	::string                                      stderr       ;                                                                  // contains child stderr if child_stderr==Pipe
private :
	mutable ::mutex _pid_mutex ;
} ;
