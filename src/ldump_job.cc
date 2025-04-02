// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "disk.hh"
#include "rpc_job.hh"

using namespace Disk ;

::string g_out ;

template<class V> void _print_map(::vmap_s<V> const& m) {
	size_t w = 0 ;
	for( auto const& [k,v] : m ) w = ::max(w,k.size()) ;
	for( auto const& [k,v] : m ) g_out <<'\t'<< widen(k,w) <<" : "<< v <<'\n' ;
}

void _print_views(::vmap_s<JobSpace::ViewDescr> const& m) {
	size_t w = 0 ;
	for( auto const& [k,v] : m ) w = ::max(w,k.size()) ;
	for( auto const& [k,v] : m ) g_out <<'\t'<< widen(k,w) <<" : "<< v.phys <<' '<< v.copy_up <<'\n' ;
}

void print_submit_attrs(SubmitAttrs const& sa) {
	g_out << "--submit attrs--\n" ;
	//
	g_out << "used_backend : "  << sa.used_tag             <<'\n' ;
	g_out << "pressure     : "  << sa.pressure.short_str() <<'\n' ;
	g_out << "live_out     : "  << sa.live_out             <<'\n' ;
	g_out << "reason       : "  << sa.reason               <<'\n' ;
}

void print_pre_start(JobStartRpcReq const& jsrr) {
	g_out << "--req--\n" ;
	//
	g_out << "seq_id : " << jsrr.seq_id <<'\n' ;
	g_out << "job    : " << jsrr.job    <<'\n' ;
	//
	g_out << "backend_msg :\n" ; g_out << ensure_nl(indent(jsrr.msg)) ;
}

void print_start(JobStartRpcReply const& jsrr) {
	g_out << "--start--\n" ;
	//
	g_out << "addr          : "  << to_hex(jsrr.addr)            <<'\n' ;
	g_out << "auto_mkdir    : "  << jsrr.autodep_env.auto_mkdir  <<'\n' ;
	g_out << "chroot_dir_s  : "  << jsrr.job_space.chroot_dir_s  <<'\n' ;
	g_out << "sub_repo_s    : "  << jsrr.autodep_env.sub_repo_s  <<'\n' ;
	g_out << "ddate_prec    : "  << jsrr.ddate_prec              <<'\n' ;
	g_out << "interpreter   : "  << jsrr.interpreter             <<'\n' ;
	g_out << "keep_tmp      : "  << jsrr.keep_tmp                <<'\n' ;
	g_out << "key           : "  << jsrr.key                     <<'\n' ;
	g_out << "kill_sigs     : "  << jsrr.kill_sigs               <<'\n' ;
	g_out << "live_out      : "  << jsrr.live_out                <<'\n' ;
	g_out << "lmake_view_s  : "  << jsrr.job_space.lmake_view_s  <<'\n' ;
	g_out << "method        : "  << jsrr.method                  <<'\n' ;
	g_out << "tmp_dir_s     : "  << jsrr.autodep_env.tmp_dir_s   <<'\n' ; // tmp directory on disk
	g_out << "repo_view_s   : "  << jsrr.job_space.repo_view_s   <<'\n' ;
	g_out << "small_id      : "  << jsrr.small_id                <<'\n' ;
	g_out << "stdin         : "  << jsrr.stdin                   <<'\n' ;
	g_out << "stdout        : "  << jsrr.stdout                  <<'\n' ;
	g_out << "timeout       : "  << jsrr.timeout                 <<'\n' ;
	g_out << "tmp_view_s    : "  << jsrr.job_space.tmp_view_s    <<'\n' ;
	g_out << "use_script    : "  << jsrr.use_script              <<'\n' ;
	//
	g_out << "deps :\n"           ; _print_map  (jsrr.deps           )   ;
	g_out << "env :\n"            ; _print_map  (jsrr.env            )   ;
	g_out << "star matches :\n"   ; _print_map  (jsrr.star_matches   )   ;
	g_out << "static matches :\n" ; _print_map  (jsrr.static_matches )   ;
	g_out << "views :\n"          ; _print_views(jsrr.job_space.views)   ;
	g_out << "cmd :\n"            ; g_out << ensure_nl(indent(jsrr.cmd)) ;
}

void print_end(JobEndRpcReq const& jerr) {
	g_out << "--end--\n" ;
	//
	g_out << "phy_dynamic_tmp_s : " << jerr.phy_tmp_dir_s    <<'\n' ;
	g_out << "wstatus           : " << jerr.wstatus          <<'\n' ;
	g_out << "end_date          : " << jerr.end_date         <<'\n' ;
	//
	g_out << "stats.cpu         : " << jerr.stats.cpu        <<'\n' ;
	g_out << "stats.job         : " << jerr.stats.job        <<'\n' ;
	g_out << "stats.mem         : " << jerr.stats.mem        <<'\n' ;
	//
	g_out << "digest.status     : " << jerr.digest.status    <<'\n' ;
	g_out << "digest.exec_time  : " << jerr.digest.exec_time <<'\n' ;
	//
	g_out << "dynamic_env :\n" ; _print_map(jerr.dyn_env) ;
	//
	g_out << "digest.targets :\n" ; _print_map(jerr.digest.targets)                ;
	g_out << "digest.deps :\n"    ; _print_map(jerr.digest.deps   )                ;
	g_out << "stderr :\n"         ; g_out << ensure_nl(indent(jerr.stderr       )) ;
	g_out << "stdout :\n"         ; g_out << ensure_nl(indent(jerr.stdout       )) ;
	g_out << "msg :\n"            ; g_out << ensure_nl(indent(localize(jerr.msg))) ;
}

int main( int argc , char* argv[] ) {
	if (argc!=2) exit(Rc::Usage,"usage : ldump_job file") ;
	app_init(true/*read_only_ok*/) ;
	//
	JobInfo job_info { argv[1] } ;
	if (+job_info.start) {
		g_out << "eta  : " << job_info.start.eta                        <<'\n' ;
		g_out << "host : " << SockFd::s_host(job_info.start.start.addr) <<'\n' ;
		print_submit_attrs(job_info.start.submit_attrs) ;
		g_out << "rsrcs :\n" ; _print_map(job_info.start.rsrcs) ;
		print_pre_start   (job_info.start.pre_start   ) ;
		print_start       (job_info.start.start       ) ;
	}
	//
	if (+job_info.end) print_end(job_info.end) ;
	Fd::Stdout.write(g_out) ;
	return 0 ;
}
