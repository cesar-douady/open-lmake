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
	struct AccessInfo {
		friend ::ostream& operator<<( ::ostream& , AccessInfo const& ) ;
		// cxtors & casts
		AccessInfo(     ) = default ;
		AccessInfo(PD pd) : access_date{pd} {}
		//
		bool operator==(AccessInfo const&) const = default ;                                                // XXX : why is this necessary ?!?
		// accesses
		bool is_dep() const { return digest.idle() ; }
		// services
		void update( PD pd , AccessDigest const& ad , bool tok , NodeIdx parallel_id_ ) ;
		void chk() const {
			if      (seen            ) SWEAR( +digest.accesses                                          ) ; // cannot see a file as existing without accessing it
			else if (+digest.accesses) SWEAR( digest.is_date ? !digest.date() : digest.crc()==Crc::None ) ; // cannot have a date or a crc without seeing the file
		}
		// data
		PD           access_date      ;         // first access date
		PD           first_write_date ;         // if !digest.idle(), first write/unlink date
		PD           last_write_date  ;         // if !digest.idle(), last  write/unlink date
		AccessDigest digest           ;
		NodeIdx      parallel_id      = 0     ;
		bool         seen             = false ; // if true <= file has been seen existing, this bit is important if file does not exist when first read, then is created externally, ...
		bool         target_ok        = false ; // ... is seen as existing and is then unlinked as this incoherence is not seen by just checking file at first read and at job end
	} ;
	struct ServerReply {
		IMsgBuf  buf        ;                   // buf to assemble the reply
		Fd       fd         ;                   // fd to forward reply to
		::string codec_file ;
	} ;
	// cxtors & casts
public :
	GatherDeps(       ) = default ;
	GatherDeps(NewType) { init() ; }
	//
	void init() { master_fd.listen() ; }
	// accesses
	bool all_confirmed() const { return !to_confirm_write && !to_confirm_unlnk ; }
	// services
private :
	void _fix_auto_date( Fd , JobExecRpcReq& jerr) ;                                                                                                             // Fd for trace purpose only
	//
	void _new_access( Fd    , PD    , ::string&&   , AccessDigest const&    , bool tok , ::string const& comment ) ;                                             // .
	void _new_access(         PD pd , ::string&& f , AccessDigest const& ad , bool tok , ::string const& c       ) { _new_access({},pd,::move(f),ad,tok  ,c) ; }
	void _new_access( Fd fd , PD pd , ::string&& f , AccessDigest const& ad ,            ::string const& c       ) { _new_access(fd,pd,::move(f),ad,false,c) ; }
	void _new_access(         PD pd , ::string&& f , AccessDigest const& ad ,            ::string const& c       ) { _new_access({},pd,::move(f),ad,false,c) ; } // .
	//
	void _new_accesses( Fd fd , JobExecRpcReq&& jerr , bool confirmed=false ) {
		if ( !confirmed && !jerr.digest.idle() ) {
			if (jerr.digest.write) { bool inserted = to_confirm_write.emplace(fd,::move(jerr)).second ; SWEAR(inserted,fd) ; }
			if (jerr.digest.unlnk) { bool inserted = to_confirm_unlnk.emplace(fd,::move(jerr)).second ; SWEAR(inserted,fd) ; }
			return ;
		}
		parallel_id++ ;
		for( auto& [f,dd] : jerr.files ) {
			jerr.digest.date(dd) ;
			_new_access( fd , jerr.date , ::move(f) , jerr.digest , jerr.ok , jerr.txt ) ;
		}
	}
	void _confirm( Fd fd , JobExecRpcReq const& jerr ) {
		Trace trace("_confirm",fd,STR(jerr.ok)) ;
		::umap<Fd,JobExecRpcReq>& to_confirm = jerr.unlnk ? to_confirm_unlnk : to_confirm_write ;
		auto it = to_confirm.find(fd) ;
		SWEAR(it!=to_confirm.end(),fd) ;
		if (jerr.ok) _new_accesses( fd , ::move(it->second) , true/*confirmed*/ ) ;
		to_confirm.erase(it) ;
	}
	void _new_guards( Fd fd , JobExecRpcReq&& jerr ) {                                                                                                           // fd for trace purpose only
		Trace trace("_new_guards",fd,jerr.txt) ;
		for( auto& [f,_] : jerr.files ) { trace(f) ; guards.insert(::move(f)) ; }
	}
	void _codec( ServerReply&& sr , JobRpcReply const& jrr , ::string const& comment="codec" ) {
		AccessDigest ad { Access::Reg , jrr.crc } ;
		ad.crc(jrr.crc) ;
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		_new_access( sr.fd , PD::s_now() , ::move(sr.codec_file) , ad , comment ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}
public :
	void new_target( PD pd , ::string const& t , ::string const& c="s_target" ) {
		AccessDigest ad ;
		ad.write = true ;
		_new_access(pd,::copy(t),ad,c) ;
	}
	void new_unlnk( PD pd , ::string const& t , ::string const& c="s_unlnk" ) {
		AccessDigest ad ;
		ad.unlnk = true ;
		_new_access(pd,::copy(t),ad,c) ;
	}
	void new_guard (::string const& f) { guards.insert(f) ; }
	//
	void new_deps( PD , ::vmap_s<DepDigest>&& deps , ::string const& stdin={}       ) ;
	void new_exec( PD , ::string const& exe        , ::string const&      ="s_exec" ) ;

	//
	void sync( Fd sock , JobExecRpcReply const&  jerr ) {
		try { OMsgBuf().send(sock,jerr) ; } catch (::string const&) {}                                                  // dont care if we cannot report the reply to job
	}
	//
	Status exec_child( ::vector_s const& args , Fd child_stdin=Fd::Stdin , Fd child_stdout=Fd::Stdout , Fd child_stderr=Fd::Stderr ) ;
	//
	bool/*done*/ kill(int sig=-1) ;                                                                                     // is sig==-1, use best effort to kill job
	//
	void reorder() ;                                                                                                    // reorder accesses by first read access
	// data
	::function<Fd/*reply*/(JobExecRpcReq     &&)> server_cb        = [](JobExecRpcReq     &&)->Fd     { return {} ; } ; // function to contact server when necessary, return error by default
	::function<void       (::string_view const&)> live_out_cb      = [](::string_view const&)->void   {             } ; // function to report live output, dont report by default
	::function<void       (                    )> kill_job_cb      = [](                    )->void   {             } ; // function to kill job
	ServerSockFd                                  master_fd        ;
	in_addr_t                                     addr             = NoSockAddr                                       ; // local addr to which we can be contacted by running job
	::atomic<bool>                                create_group     = false                                            ; // if true <=> process is launched in its own group
	AutodepMethod                                 method           = AutodepMethod::Dflt                              ;
	AutodepEnv                                    autodep_env      ;
	Time::Delay                                   timeout          ;
	pid_t                                         pid              = -1                                               ; // pid to kill
	bool                                          killed           = false                                            ; // do not start as child is supposed to be already killed
	::vector<uint8_t>                             kill_sigs        ;                                                    // signals used to kill job
	::string                                      chroot           ;
	::string                                      cwd              ;
	 ::map_ss const*                              env              = nullptr                                          ;
	vmap_s<AccessInfo>                            accesses         ;
	umap_s<NodeIdx   >                            access_map       ;
	uset_s                                        guards           ;                                                    // dir creation/deletion that must be guarded against NFS
	NodeIdx                                       parallel_id      = 0                                                ; // id to identify parallel deps
	bool                                          seen_tmp         = false                                            ;
	int                                           wstatus          = 0/*garbage*/                                     ;
	Fd                                            child_stdout     ;                                                    // fd used to gather stdout
	Fd                                            child_stderr     ;                                                    // fd used to gather stderr
	::string                                      stdout           ;                                                    // contains child stdout if child_stdout==Pipe
	::string                                      stderr           ;                                                    // contains child stderr if child_stderr==Pipe
	::string                                      msg              ;                                                    // contains error messages not from job
	::umap<Fd,JobExecRpcReq>                      to_confirm_write ;
	::umap<Fd,JobExecRpcReq>                      to_confirm_unlnk ;
private :
	mutable ::mutex _pid_mutex ;
} ;
