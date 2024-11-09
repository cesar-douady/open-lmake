// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
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
	g_out << "backend  : "  << sa.tag                  <<'\n' ;
	g_out << "pressure : "  << sa.pressure.short_str() <<'\n' ;
	g_out << "live_out : "  << sa.live_out             <<'\n' ;
	g_out << "reason   : "  << sa.reason               <<'\n' ;
}

void print_pre_start(JobRpcReq const& jrr) {
	SWEAR( jrr.proc==JobRpcProc::Start , jrr.proc ) ;
	g_out << "--req--\n" ;
	//
	g_out << "seq_id : " << jrr.seq_id <<'\n' ;
	g_out << "job    : " << jrr.job    <<'\n' ;
	//
	g_out << "backend_msg :\n" ; g_out << ensure_nl(indent(jrr.msg)) ;
}

void print_start(JobRpcReply const& jrr) {
	SWEAR( jrr.proc==JobRpcProc::Start , jrr.proc ) ;
	g_out << "--start--\n" ;
	//
	g_out << "addr         : "  << to_hex(jrr.addr)            <<'\n' ;
	g_out << "auto_mkdir   : "  << jrr.autodep_env.auto_mkdir  <<'\n' ;
	g_out << "chroot_dir_s : "  << jrr.job_space.chroot_dir_s  <<'\n' ;
	g_out << "cwd_s        : "  << jrr.cwd_s                   <<'\n' ;
	g_out << "date_prec    : "  << jrr.date_prec               <<'\n' ;
	g_out << "ignore_stat  : "  << jrr.autodep_env.ignore_stat <<'\n' ;
	g_out << "interpreter  : "  << jrr.interpreter             <<'\n' ;
	g_out << "keep_tmp     : "  << jrr.keep_tmp                <<'\n' ;
	g_out << "key          : "  << jrr.key                     <<'\n' ;
	g_out << "kill_sigs    : "  << jrr.kill_sigs               <<'\n' ;
	g_out << "live_out     : "  << jrr.live_out                <<'\n' ;
	g_out << "method       : "  << jrr.method                  <<'\n' ;
	g_out << "tmp_dir_s    : "  << jrr.autodep_env.tmp_dir_s   <<'\n' ; // tmp directory on disk
	g_out << "root_view_s  : "  << jrr.job_space.root_view_s   <<'\n' ;
	g_out << "small_id     : "  << jrr.small_id                <<'\n' ;
	g_out << "stdin        : "  << jrr.stdin                   <<'\n' ;
	g_out << "stdout       : "  << jrr.stdout                  <<'\n' ;
	g_out << "timeout      : "  << jrr.timeout                 <<'\n' ;
	g_out << "tmp_sz_mb    : "  << jrr.tmp_sz_mb               <<'\n' ;
	g_out << "tmp_view_s   : "  << jrr.job_space.tmp_view_s    <<'\n' ;
	g_out << "use_script   : "  << jrr.use_script              <<'\n' ;
	//
	g_out << "deps :\n"           ; _print_map  (jrr.deps           )                        ;
	g_out << "env :\n"            ; _print_map  (jrr.env            )                        ;
	g_out << "star matches :\n"   ; _print_map  (jrr.star_matches   )                        ;
	g_out << "static matches :\n" ; _print_map  (jrr.static_matches )                        ;
	g_out << "views :\n"          ; _print_views(jrr.job_space.views)                        ;
	g_out << "cmd :\n"            ; g_out << ensure_nl(indent(jrr.cmd.first+jrr.cmd.second)) ;
}

void print_end(JobRpcReq const& jrr) {
	JobDigest const& jd = jrr.digest ;
	JobStats  const& st = jd.stats   ;
	SWEAR( jrr.proc==JobRpcProc::End , jrr.proc ) ;
	//
	g_out << "--end--\n" ;
	//
	g_out << "phy_dynamic_tmp_s  : " << jrr.phy_tmp_dir_s <<'\n' ;
	//
	g_out << "digest.status      : " << jd.status         <<'\n' ;
	g_out << "digest.wstatus     : " << jd.wstatus        <<'\n' ;
	g_out << "digest.end_date    : " << jd.end_date       <<'\n' ;
	g_out << "digest.stats.cpu   : " << st.cpu            <<'\n' ;
	g_out << "digest.stats.job   : " << st.job            <<'\n' ;
	g_out << "digest.stats.total : " << st.total          <<'\n' ;
	g_out << "digest.stats.mem   : " << st.mem            <<'\n' ;
	//
	g_out << "dynamic_env :\n"         ; _print_map(jrr.dynamic_env)           ;
	//
	g_out << "digest.targets :\n"      ; _print_map(jd.targets     )           ;
	g_out << "digest.deps :\n"         ; _print_map(jd.deps        )           ;
	g_out << "digest.stderr :\n"       ; g_out << ensure_nl(indent(jd.stderr)) ;
	g_out << "digest.stdout :\n"       ; g_out << ensure_nl(indent(jd.stdout)) ;
	//
	g_out << "_msg :\n" ; g_out << ensure_nl(indent(localize(jrr.msg))) ;
}

int main( int argc , char* argv[] ) {
	if (argc!=2) exit(Rc::Usage,"usage : ldump_job file") ;
	app_init(true/*read_only_ok*/) ;
	//
	JobInfo job_info { argv[1] } ;
	if (+job_info.start) {
		g_out << "eta  : " << job_info.start.eta                  <<'\n' ;
		g_out << "host : " << SockFd::s_host(job_info.start.host) <<'\n' ;
		print_submit_attrs(job_info.start.submit_attrs) ;
		g_out << "rsrcs :\n" ; _print_map(job_info.start.rsrcs) ;
		print_pre_start   (job_info.start.pre_start   ) ;
		print_start       (job_info.start.start       ) ;
	}
	//
	if (+job_info.end) print_end(job_info.end.end) ;
	Fd::Stdout.write(g_out) ;
	return 0 ;
}
