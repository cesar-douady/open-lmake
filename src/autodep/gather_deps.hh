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
		size_t pos1 = env.find(':'       ) ; if (pos1==Npos) fail_prod( "bad autodep env format : " , env.empty()?"(not found)"s:env ) ;
		/**/   pos1 = env.find(':',pos1+1) ; if (pos1==Npos) fail_prod( "bad autodep env format : " , env.empty()?"(not found)"s:env ) ;
		size_t pos2 = env.find(':',pos1+1) ; if (pos2==Npos) fail_prod( "bad autodep env format : " , env.empty()?"(not found)"s:env ) ;
		//
		service   = env.substr(0     ,pos1) ;
		root_dir  = env.substr(pos2+1     ) ;
		for( char c : ::string_view(env).substr(pos1+1,pos2-pos1-1) )
			switch (c) {
				case 'd' : auto_mkdir  = true             ; break ;
				case 'i' : ignore_stat = true             ; break ;
				case 'e' : report_ext  = true             ; break ;
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
		if (report_ext ) res += 'e' ;
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
	bool       report_ext  = false            ;
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
		PD                        read_date  = {}                ;             // if +info.dfs   , first read date : must appear first so deps are sorted by first access date
		PD                        write_date = {}                ;             // if !info.idle(), first write date
		JobExecRpcReq::AccessInfo info       = {}                ;
		DD                        file_date  = {}                ;             // if +info.dfs   , date of file when read as first access
		DepOrder                  order      = DepOrder::Unknown ;
	} ;
	// cxtors & casts
	GatherDeps(       ) = default ;
	GatherDeps(NewType) { init() ; }
	void init() {
		master_sock.listen() ;
	}
	// services
private :
	bool/*new*/ _new_access( PD , ::string const& , DD , JobExecRpcReq::AccessInfo const& , bool parallel , bool force_new=false , ::string const& comment={} ) ;
	//
	void _new_accesses( PD pd , ::vector_s const& files , JobExecRpcReq::AccessInfo const& info , bool force_new=false , ::string const& comment={} ) {
		bool parallel = false ;
		for( auto const& f : files ) parallel |= _new_access(pd,f,{},info,parallel,force_new,comment) ;
	}
	void _new_accesses( PD pd , ::vmap_s<DD> const& dds , JobExecRpcReq::AccessInfo const& info , bool force_new=false , ::string const& comment={} ) {
		bool parallel = false ;
		for( auto const& [f,dd] : dds ) parallel |= _new_access(pd,f,dd,info,parallel,force_new,comment) ;
	}
public :
	void new_target( PD pd , S const& t         , TFs n , TFs p , S const& c="target" ) { _new_access(pd,t,{},{.write=true,.neg_tfs=n,.pos_tfs=p},false/*parallel*/,false/*force_new*/,c) ; }
	void new_dep   ( PD pd , S const& d , DD dd , DFs dfs       , S const& c="dep"    ) { _new_access(pd,d,dd,{.dfs=dfs}                         ,false/*parallel*/,false/*force_new*/,c) ; }
	void new_static_deps( PD pd , vector_s const& ds , S const& c="static_deps" ) {
		SWEAR(accesses.empty()) ;                                                               // ensure we do not insert static deps after hidden ones
		_new_accesses( pd , ds  , {.dfs=StaticDFlags&~AccessDFlags} , true/*force_new*/ , c ) ; // ensure there is one entry for each static dep
		n_statics = accesses.size() ;                                                           // ensure static deps are kept in original order
	}
	//
	void new_exec( PD pd , ::string const& exe , ::string const& c="exec" ) {
		for( auto&& [file,a] : Disk::RealPath(autodep_env.lnk_support).exec(Fd::Cwd,exe) ) {
			DD     dd = Disk::file_date(file) ;
			DFlags fs ;
			if (a.as_lnk) fs |= DFlag::Lnk ;
			if (a.as_reg) fs |= DFlag::Reg ;
			if ( file[0]!='/' || autodep_env.report_ext ) new_dep( pd , ::move(file) , dd , fs , c ) ;
		}
	}

	//
	void sync( Fd sock , JobExecRpcReply const&  jerr ) {
		OMsgBuf().send(sock,jerr) ;
	}
	//
	Status exec_child( ::vector_s const& args , Fd child_stdin=Fd::Stdin , Fd child_stdout=Fd::Stdout , Fd child_stderr=Fd::Stderr ) ;
	//
	void reorder() ;                                                           // reorder accesses by first read access and manage Critical in AccessInfo.order
	// data
	::function<Fd/*reply*/(JobExecRpcReq     &&)> server_cb         = [](JobExecRpcReq     &&)->Fd   { return {} ; } ; // function used to contact server when necessary, by default, return error
	::function<void       (::string_view const&)> live_out_cb       = [](::string_view const&)->void {             } ; // function used to report live output, by default dont report
	ServerSockFd                                  master_sock       ;
	in_addr_t                                     addr              = 0x7f000001                                     ; // local addr to which we can be contacted by running job
	bool                                          create_group      = false                                          ; // if true <=> process is launched in its own group
	AutodepMethod                                 method            = AutodepMethod::Dflt                            ;
	AutodepEnv                                    autodep_env       ;
	Time::Delay                                   timeout           ;
	::vector<uint8_t>                             kill_sigs         ;                                                  // signals used to kill job
	::string                                      chroot            ;
	::string                                      cwd               ;
	 ::map_ss const*                              env               = nullptr                                        ;
	vmap_s<AccessInfo>                            accesses          ;
	umap_s<NodeIdx   >                            access_map        ;
	bool                                          seen_tmp          = false                                          ;
	int                                           wstatus           = 0                                              ;
	::string                                      stdout            ;                                                  // contains child stdout if child_stdout==Pipe
	::string                                      stderr            ;                                                  // contains child stderr if child_stderr==Pipe
	::vector<PD>                                  critical_barriers ;                                                  // dates of critical barriers
	NodeIdx                                       n_statics         = 0                                              ; // the first n_static entries are kept in original order
} ;
