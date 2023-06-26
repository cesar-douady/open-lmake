// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include <sys/wait.h>

#include "disk.hh"
#include "rpc_job.hh"
#include "time.hh"

struct AutodepEnv {
	friend ::ostream& operator<<( ::ostream& , AutodepEnv const& ) ;
	// cxtors & casts
	AutodepEnv() = default ;
	// env format : server:port:options:root_dir
	//if port is empty, server is considered a file to log deps to (which defaults to stderr if empty)
	AutodepEnv( ::string const& env ) {
		if (env.empty()) return ;
		size_t pos1 = env.find(':'       ) ; if (pos1==NPos) fail_prod( "bad autodep env format : " , env.empty()?"(not found)"s:env ) ;
		/**/   pos1 = env.find(':',pos1+1) ; if (pos1==NPos) fail_prod( "bad autodep env format : " , env.empty()?"(not found)"s:env ) ;
		size_t pos2 = env.find(':',pos1+1) ; if (pos2==NPos) fail_prod( "bad autodep env format : " , env.empty()?"(not found)"s:env ) ;
		//
		service   = env.substr(0     ,pos1) ;
		root_dir  = env.substr(pos2+1     ) ;
		for( char c : ::string_view(env).substr(pos1+1,pos2-pos1-1) )
			switch (c) {
				case 'd' : auto_mkdir  = true             ; break ;
				case 'i' : ignore_stat = true             ; break ;
				case 'n' : lnk_support = LnkSupport::None ; break ;
				case 'f' : lnk_support = LnkSupport::File ; break ;
				case 'a' : lnk_support = LnkSupport::Full ; break ;
				default : FAIL(c) ;
			}
	}
	operator ::string() const {
		::string res = service + ':' ;
		if (auto_mkdir ) res += 'd' ;
		if (ignore_stat) res += 'i' ;
		switch (lnk_support) {
			case LnkSupport::None : res += 'n' ; break ;
			case LnkSupport::File : res += 'f' ; break ;
			case LnkSupport::Full : res += 'a' ; break ;
			default : FAIL(lnk_support) ;
		}
		res += ':'      ;
		res += root_dir ;
		return res ;
	}
	// data
	::string   service     ;
	::string   root_dir    ;
	bool       auto_mkdir  = false            ;
	bool       ignore_stat = false            ;
	LnkSupport lnk_support = LnkSupport::Full ;
} ;

// When several sockets are opened to send depend & target data, we are not sure of the order between these reports because of system buffers.
// We could have decided to synchronize each report, which may be expensive in performance.
// We chose to lose some errors, i.e. Update's may be seen as Write's, as each ambiguity is resolved by considering the Write is earliest and read latest possible.
// This way, we do not generate spurious errors.
// To do so, we maintain, for each access entry (i.e. a file), a list of sockets that are unordered, i.e. for which a following Write could actually have been done before by the user.

struct GatherDeps {
	friend ::ostream& operator<<( ::ostream& os , GatherDeps const& ad ) ;
	using Proc = JobExecRpcProc    ;
	using DO   = DepOrder          ;
	using DD   = Time::DiskDate    ;
	using PD   = Time::ProcessDate ;
	using FI   = Disk::FileInfo    ;
	using DFs  = DFlags            ;
	using TFs  = TFlags            ;
	using S    = ::string          ;
	struct AccessInfo {
		friend ::ostream& operator<<( ::ostream& , AccessInfo const& ) ;
		// data
		JobExecRpcReq::AccessInfo info        ;
		DepOrder                  dep_order   ;            // if read and not written
		PD                        access_date ;            // first access date
		DD                        file_date   ;            // date of file when first accessed if it was a read
	} ;
	// cxtors & casts
	GatherDeps(       ) = default ;
	GatherDeps(NewType) { init() ; }
	void init() {
		master_sock.listen() ;
	}
	// services
private :
	AccessInfo& _info(::string const& name) ;
	//
	void _new_access( PD , ::string const& , DD , JobExecRpcReq::AccessInfo const& , ::string const& comment={} ) ;
	//
	void _new_accesses( PD d , ::vector_s const& files , JobExecRpcReq::AccessInfo const& info , ::string const& comment={} ) {
		//
		if (files.empty()) return ;                                            // do not update nxt_order if deps is empty
		for( ::string const& f : files ) _new_access(d,f,{},info,comment) ;
		_nxt_order = DO::Seq ;
	}
	void _new_accesses( PD pd , ::vmap_s<DD> const& dds , JobExecRpcReq::AccessInfo const& info , ::string const& comment={} ) {
		//
		if (dds.empty()) return ;                                              // do not update nxt_order if deps is empty
		for( auto const& [f,dd] : dds ) _new_access(pd,f,dd,info,comment) ;
		_nxt_order = DO::Seq ;
	}
public :
	void new_target( PD pd , S     && t , TFs n , TFs p , S const& c="target" ) { _new_access( pd , ::move(t) , {} , {.write=true,.neg_tfs=n,.pos_tfs=p} , c ) ; }
	void new_target( PD pd , S const& t , TFs n , TFs p , S const& c="target" ) { _new_access( pd ,        t  , {} , {.write=true,.neg_tfs=n,.pos_tfs=p} , c ) ; }
	void new_dep   ( PD pd , S     && d , DFs dfs       , S const& c="dep"    ) { _new_access( pd , ::move(d) , Disk::file_date(d) , {.dfs=dfs}          , c ) ; _nxt_order = DO::Seq ; }
	void new_dep   ( PD pd , S const& d , DFs dfs       , S const& c="dep"    ) { _new_access( pd ,        d  , Disk::file_date(d) , {.dfs=dfs}          , c ) ; _nxt_order = DO::Seq ; }
	//
	void new_exec( PD pd , ::string const& exe , ::string const& c="exec" ) {
		for( auto&& [file,a] : Disk::RealPath(autodep_env.lnk_support).exec(Fd::Cwd,exe) ) {
			DFlags fs ;
			if (a.as_lnk) fs |= DFlag::Lnk ;
			if (a.as_reg) fs |= DFlag::Reg ;
			new_dep( pd , ::move(file) , fs , c ) ;
		}
	}

	//
	void sync( Fd sock , JobExecRpcReply const&  jerr ) {
		OMsgBuf().send(sock,jerr) ;
	}
	//
	Status exec_child( ::vector_s const& args , Fd child_stdin=Fd::Stdin , Fd child_stdout=Fd::Stdout , Fd child_stderr=Fd::Stderr ) ;
	// data
	::function<Fd/*reply*/(JobExecRpcReq     &&)> server_cb      = [](JobExecRpcReq     &&)->Fd   { return {} ; } ; // function used to contact server when necessary, by default, return error
	::function<void       (::string_view const&)> live_out_cb    = [](::string_view const&)->void {             } ; // function used to report live output, by default dont report
	ServerSockFd                                  master_sock    ;
	in_addr_t                                     addr           = 0x7f000001                                     ; // local addr to which we can be contacted by running job
	bool                                          create_group   = false                                          ; // if true <=> process is launched in its own group
	AutodepMethod                                 autodep_method = AutodepMethod::Dflt                            ;
	AutodepEnv                                    autodep_env    ;
	Time::Delay                                   timeout        ;
	::vector<int>                                 kill_sigs      ;                                                  // signals used to kill job
	::string                                      chroot         ;
	::string                                      cwd            ;
	 ::map_ss const*                              env            = nullptr                                        ;
	vmap_s<AccessInfo>                            accesses       ;
	umap_s<NodeIdx   >                            access_map     ;
	bool                                          seen_tmp       = false                                          ;
	int                                           wstatus        = 0                                              ;
	::string                                      stdout         ;                                                  // contains child stdout if child_stdout==Pipe
	::string                                      stderr         ;                                                  // contains child stderr if child_stderr==Pipe
private :
	DepOrder _nxt_order = DepOrder::Seq ;
} ;
