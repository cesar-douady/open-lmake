// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/wait.h>

#include "disk.hh"
#include "rpc_job.hh"
#include "time.hh"

#include "autodep_env.hh"

// When several sockets are opened to send depend & target data, we are not sure of the order between these reports because of system buffers.
// We could have decided to synchronize each report, which may be expensive in performance.
// We chose to lose some errors, i.e. Update's may be seen as Write's, as each ambiguity is resolved by considering the Write is earliest and read latest possible.
// This way, we do not generate spurious errors.
// To do so, we maintain, for each access entry (i.e. a file), a list of sockets that are unordered, i.e. for which a following Write could actually have been done before by the user.

struct GatherDeps {
	friend ::ostream& operator<<( ::ostream& os , GatherDeps const& ad ) ;
	using Accesses = Disk::Accesses    ;
	using Proc     = JobExecRpcProc    ;
	using DD       = Time::DiskDate    ;
	using PD       = Time::ProcessDate ;
	using FI       = Disk::FileInfo    ;
	struct AccessInfo {
		friend ::ostream& operator<<( ::ostream& , AccessInfo const& ) ;
		// cxtors & casts
		AccessInfo(TFlags tfs_=UnexpectedTFlags) : tflags{tfs_} {}
		// accesses
		bool is_dep() const { return info.idle() && tflags[TFlag::Dep] ; }
		// services
		bool/*modified*/ update( PD pd , DD dd , JobExecRpcReq::AccessInfo const& ai , NodeIdx parallel_id_ ) {
			Bool3 after =                                                      // new entries have after==No
				!info.idle()   && pd>write_date ? Yes
			:	+info.accesses && pd>read_date  ? Maybe
			:	                                  No
			;
			// if we do not write, do book-keeping as read, even if we do not access the file
			if ( (+ai.accesses||+ai.dflags) || ai.idle() ) {
				if      (after==No     ) { file_date = dd ; read_date = pd ; parallel_id = parallel_id_ ; } // update read info if we are first to read
				else if (!info.accesses) { file_date = dd ;                                               } // if no previous access, record our date
			}
			if ( !ai.idle() && after!=Yes ) write_date = pd ;
			//
			JobExecRpcReq::AccessInfo old_ai = info  ;                                                 // for trace only
			info.update(ai,after) ;                                                                    // execute actions in actual order as provided by dates
			SWEAR( !( (old_ai.neg_tflags|old_ai.pos_tflags) & ~(info.neg_tflags|info.pos_tflags) ) ) ; // info.tflags is less and less transparent
			tflags = ( tflags & ~info.neg_tflags ) | info.pos_tflags ;                                 // thus we can recompute new tfs from old value
			return after!=Yes || info!=old_ai ;
		}
		// data
		PD                        read_date   = {} ;       // if +info.dfs   , first read date : must appear first so deps are sorted by first access date
		PD                        write_date  = {} ;       // if !info.idle(), first write date
		JobExecRpcReq::AccessInfo info        = {} ;
		DD                        file_date   = {} ;       // if +info.dfs   , date of file when read as first access
		NodeIdx                   parallel_id = 0  ;
		TFlags                    tflags           ;       // resulting flags after appliation of info flags modifiers
	} ;
	// cxtors & casts
	GatherDeps(       ) = default ;
	GatherDeps(NewType) { init() ; }
	void init() {
		master_sock.listen() ;
	}
	// services
private :
	bool/*new*/ _new_access( PD , ::string const& , DD , JobExecRpcReq::AccessInfo const& , NodeIdx parallel_id , ::string const& comment={} ) ;
	//
	void _new_accesses( PD pd , ::vector_s const& files , JobExecRpcReq::AccessInfo const& info , ::string const& comment={} ) {
		parallel_id++ ;
		for( auto const& f : files ) _new_access(pd,f,{},info,parallel_id,comment) ;
	}
	void _new_accesses( PD pd , ::vmap_s<DD> const& dds , JobExecRpcReq::AccessInfo const& info , ::string const& comment={} ) {
		parallel_id++ ;
		for( auto const& [f,dd] : dds ) _new_access(pd,f,dd,info,parallel_id,comment) ;
	}
public :
	void new_target( PD pd , ::string const& t         , TFlags n , TFlags p     , ::string const& c="target" ) { _new_access(pd,t,{},{.write=true,.neg_tflags=n,.pos_tflags=p},0/*parallel_id*/,c) ; }
	void new_dep   ( PD pd , ::string const& d , DD dd , Accesses a , DFlags dfs , ::string const& c="dep"    ) { _new_access(pd,d,dd,{.accesses=a,.dflags=dfs                },0/*parallel_id*/,c) ; }
	//
	void new_static_deps( PD pd , ::vmap_s<DFlags> const& ds , ::string const& c="static_deps" ) {
		SWEAR(accesses.empty()) ;                                                                                  // ensure we do not insert static deps after hidden ones
		parallel_id++ ;
		for( auto const& [f,d] : ds ) _new_access(pd,f,{},{.dflags=d},parallel_id,c) ;
	}
	//
	void new_exec( PD pd , ::string const& exe , ::string const& c="exec" ) {
		for( auto&& [file,a] : Disk::RealPath(autodep_env.lnk_support,autodep_env.src_dirs_s).exec(Fd::Cwd,exe) ) {
			DD       dd = Disk::file_date(file) ;
			new_dep( pd , ::move(file) , dd , a , {} , c ) ;
		}
	}

	//
	void sync( Fd sock , JobExecRpcReply const&  jerr ) {
		OMsgBuf().send(sock,jerr) ;
	}
	//
	Status exec_child( ::vector_s const& args , Fd child_stdin=Fd::Stdin , Fd child_stdout=Fd::Stdout , Fd child_stderr=Fd::Stderr ) ;
	//
	void reorder() ;                                                           // reorder accesses by first read access
	// date
	::function<Fd/*reply*/(JobExecRpcReq     &&)> server_cb    = [](JobExecRpcReq     &&)->Fd     { return {}               ; } ; // function to contact server when necessary, return error by default
	::function<void       (::string_view const&)> live_out_cb  = [](::string_view const&)->void   {                           } ; // function to report live output, dont report by default
	::function<TFlags     (::string      const&)> tflags_cb    = [](::string      const&)->TFlags { return UnexpectedTFlags ; } ; // function to compute tflags from target before modifiers
	ServerSockFd                                  master_sock  ;
	in_addr_t                                     addr         = SockFd::LoopBackAddr                                           ; // local addr to which we can be contacted by running job
	bool                                          create_group = false                                                          ; // if true <=> process is launched in its own group
	AutodepMethod                                 method       = AutodepMethod::Dflt                                            ;
	AutodepEnv                                    autodep_env  ;
	Time::Delay                                   timeout      ;
	::vector<uint8_t>                             kill_sigs    ;                                                                  // signals used to kill job
	::string                                      chroot       ;
	::string                                      cwd          ;
	 ::map_ss const*                              env          = nullptr                                                        ;
	vmap_s<AccessInfo>                            accesses     ;
	umap_s<NodeIdx   >                            access_map   ;
	NodeIdx                                       parallel_id  = 0                                                              ; // id to identify parallel deps
	bool                                          seen_tmp     = false                                                          ;
	int                                           wstatus      = 0                                                              ;
	::string                                      stdout       ;                                                                  // contains child stdout if child_stdout==Pipe
	::string                                      stderr       ;                                                                  // contains child stderr if child_stderr==Pipe
} ;
