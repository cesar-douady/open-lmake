// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// doc : https://wiki.archlinux.org/title/Son_of_Grid_Engine

#include "disk.hh"
#include "process.hh"

#include "generic.hh"

using namespace Disk ;

namespace Backends::Sge {

	struct SgeBackend ;

	//
	// daemon info
	//

	struct Daemon {
		friend ::ostream& operator<<( ::ostream& , Daemon const& ) ;
		// data
		::map_s<size_t> licenses ; // licenses sampled from daemon
	} ;

	//
	// resources
	//

	struct RsrcsData {
		friend ::ostream& operator<<( ::ostream& , RsrcsData const& ) ;
		// cxtors & casts
		RsrcsData(           ) = default ;
		RsrcsData(::vmap_ss&&) ;
		// accesses
		bool operator==(RsrcsData const&) const = default ;
		// data
		int16_t            prio   = 0  ; // priority              : qsub -p <prio>     (prio comes from lmake -b               )
		uint16_t           cpu    = 0  ; // number of logical cpu : qsub -l <cpu_rsrc> (cpu_rsrc comes from config, always hard)
		uint32_t           mem    = 0  ; // memory   in MB        : qsub -l <mem_rsrc> (mem_rsrc comes from config, always hard)
		uint32_t           tmp    = -1 ; // tmp disk in MB        : qsub -l <tmp_rsrc> (tmp_rsrc comes from config, always hard) default : dont manage tmp size (provide infinite storage, reserve none)
		::vmap_s<uint64_t> tokens ;      // generic resources     : qsub -l<key>=<val> (for each entry            , always hard)
		::vector_s         hard   ;      // hard options          : qsub -hard <val>
		::vector_s         soft   ;      // soft options          : qsub -soft <val>
		// services
		::vmap_ss mk_vmap(void) const ;
	} ;

}

namespace std {
	template<> struct hash<Backends::Sge::RsrcsData> {
		size_t operator()(Backends::Sge::RsrcsData const& rd) const {
			Hash::Xxh h ;
			h.update(rd.prio         ) ;
			h.update(rd.cpu          ) ;
			h.update(rd.mem          ) ;
			h.update(rd.tmp          ) ;
			h.update(rd.tokens.size()) ; for( auto     const& [k,v] : rd.tokens ) { h.update(k) ; h.update(v) ; }
			h.update(rd.hard  .size()) ; for( ::string const&    v  : rd.hard   )                 h.update(v) ;
			h.update(rd.soft  .size()) ; for( ::string const&    v  : rd.soft   )                 h.update(v) ;
			return +h.digest() ;
		}
	} ;
}

//
// SgeBackend
//

namespace Backends::Sge {

	using SgeId = uint32_t ;

	Mutex<MutexLvl::Sge> _sge_mutex ; // ensure no more than a single outstanding request to daemon

	void     sge_cancel      (::pair<SgeBackend const*,SgeId> const&) ;
	Daemon   sge_sense_daemon(SgeBackend const&                     ) ;
	::string sge_mk_name     (::string&&                            ) ;
	//
	SgeId sge_spawn_job( ::stop_token , ::string const& key , Job , ::vector<ReqIdx> const& , ::vector_s const& cmd_line , RsrcsData const& rsrcs , bool verbose ) ;

	constexpr Tag MyTag = Tag::Sge ;

	struct SgeBackend
	:	             GenericBackend<MyTag,SgeId,RsrcsData,RsrcsData,false/*IsLocal*/>
	{	using Base = GenericBackend<MyTag,SgeId,RsrcsData,RsrcsData,false/*IsLocal*/> ;

		struct SpawnedMap : ::umap<Rsrcs,JobIdx> {
			// count number of jobs spawned but not started yet
			// no entry is equivalent to entry with 0
			void inc(Rsrcs rs) {
				try_emplace(rs,0).first->second++ ; // create 0 entry if necessary
			}
			void dec(Rsrcs rs) {                    // entry must exist
				auto sit = find(rs) ;
				if(!--sit->second) erase(sit) ;     // no entry means 0, so collect when possible (questionable)
			}
			JobIdx n_spawned(Rsrcs rs) {
				auto it = find(rs) ;
				if (it==end()) return 0          ;  // no entry means 0
				else           return it->second ;
			}
		} ;

		// init
		static void s_init() {
			static bool once=false ; if (once) return ; else once = true ;
			s_register(MyTag,*new SgeBackend) ;
		}

		// static data
		static DequeThread<::pair<SgeBackend const*,SgeId>> _s_sge_cancel_thread ; // when a req is killed, a lot of queued jobs may be canceled, better to do it in a separate thread

		// accesses

		virtual bool call_launch_after_start() const { return true ; }

		// services

		virtual void sub_config( vmap_ss const& dct , bool dynamic ) {
			Trace trace(BeChnl,"Sge::config",STR(dynamic),dct) ;
			//
			repo_key = base_name(no_slash(*g_root_dir_s))+':' ; // cannot put this code directly as init value as g_root_dir_s is not available early enough
			for( auto const& [k,v] : dct ) {
				try {
					switch (k[0]) {
						case 'b' : if (k=="bin_dir"          ) { sge_bin_dir_s     = with_slash           (v) ; continue ; } break ;
						case 'c' : if (k=="cell"             ) { sge_cell          =                       v  ; continue ; }
						/**/       if (k=="cluster"          ) { sge_cluster       =                       v  ; continue ; }
						/**/       if (k=="cpu_resource"     ) { cpu_rsrc          =                       v  ; continue ; } break ;
						case 'd' : if (k=="default_prio"     ) { dflt_prio         = from_string<int16_t >(v) ; continue ; } break ;
						case 'm' : if (k=="mem_resource"     ) { mem_rsrc          =                       v  ; continue ; } break ;
						case 'n' : if (k=="n_max_queued_jobs") { n_max_queued_jobs = from_string<uint32_t>(v) ; continue ; } break ;
						case 'r' : if (k=="repo_key"         ) { repo_key          =                       v  ; continue ; }
						/**/       if (k=="root_dir"         ) { sge_root_dir_s    = with_slash           (v) ; continue ; } break ;
						case 't' : if (k=="tmp_resource"     ) { tmp_rsrc          =                       v  ; continue ; } break ;
						default : ;
					}
				} catch (::string const& e) { trace("bad_val",k,v) ; throw "wrong value for entry "   +k+": "+v ; }
				/**/                        { trace("bad_key",k  ) ; throw "unexpected config entry: "+k        ; }
			}
			if (!sge_bin_dir_s ) throw "must specify bin_dir to configure SGE"s  ;
			if (!sge_root_dir_s) throw "must specify root_dir to configure SGE"s ;
			if (!dynamic) {
				daemon = sge_sense_daemon(*this) ;
				_s_sge_cancel_thread.open('C',sge_cancel) ;
			}
			trace("done") ;
		}

		virtual ::vmap_ss descr() const {
			return {} ;
		}

		virtual ::vmap_ss mk_lcl( ::vmap_ss&& rsrcs , ::vmap_s<size_t> const& capacity ) const {
			bool             single = false             ;
			::umap_s<size_t> capa   = mk_umap(capacity) ;
			::umap_s<size_t> rs     ;
			for( auto&& [k,v] : rsrcs ) {
				if (capa.contains(k)) { size_t s = from_string_rsrc<size_t>(k,v) ; rs[::move(k)] = s ; } // capacities of local backend are only integer information
			}
			::vmap_ss res ;
			if (single) for( auto&& [k,v] : rs ) { ::string s = to_string_rsrc(k,        capa[k] ) ; res.emplace_back( ::move(k) , ::move(s) ) ; }
			else        for( auto&& [k,v] : rs ) { ::string s = to_string_rsrc(k,::min(v,capa[k])) ; res.emplace_back( ::move(k) , ::move(s) ) ; }
			return res ;
		}

		virtual void open_req( Req req , JobIdx n_jobs ) {
			Base::open_req(req,n_jobs) ;
			::string const& prio = req->options.flag_args[+ReqFlag::Backend] ;
			grow(req_prios,+req) = +prio ? from_string<int16_t>(prio) : dflt_prio ;
		}

		virtual void close_req(Req req) {
			Base::close_req(req) ;
			if(!reqs) SWEAR(!spawned_rsrcs,spawned_rsrcs) ;
		}

		virtual ::vmap_ss export_( RsrcsData const& rs              ) const { return rs.mk_vmap()  ; }
		virtual RsrcsData import_( ::vmap_ss     && rsa , Req , Job ) const { return {::move(rsa)} ; }
		//
		virtual bool/*ok*/ fit_now(RsrcsAsk const& rsa) const {
			bool res = spawned_rsrcs.n_spawned(rsa) < n_max_queued_jobs ;
			return res ;
		}
		virtual Rsrcs acquire_rsrcs(RsrcsAsk const& rsa) const {
			spawned_rsrcs.inc(rsa) ;
			return rsa ;
		}
		virtual void start_rsrcs(Rsrcs const& rs) const {
			spawned_rsrcs.dec(rs) ;
		}
		virtual ::string start_job( Job , SpawnedEntry const& se ) const {
			SWEAR(+se.rsrcs) ;
			return "sge_id:"s+se.id.load() ;
		}
		// XXX : implement end_job to give explanations if verbose (mimic slurm)
		virtual ::pair_s<HeartbeatState> heartbeat_queued_job( Job , SpawnedEntry const& se ) const {
			if (sge_exec_client({"qstat","-j",::to_string(se.id)}).second) return { {}/*msg*/                      , HeartbeatState::Alive } ;
			else                                                           return { "lost job "+::to_string(se.id) , HeartbeatState::Lost  } ; // XXX : try to distinguish between Lost and Err
		}
		virtual void kill_queued_job(SpawnedEntry const& se) const {
			if (se.live) _s_sge_cancel_thread.push(::pair(this,se.id.load())) ;                                                                // asynchronous (as faster and no return value) cancel
		}
		virtual SgeId launch_job( ::stop_token , Job j , ::vector<ReqIdx> const& reqs , Pdate /*prio*/ , ::vector_s const& cmd_line , Rsrcs const& rs , bool verbose ) const {
			::vector_s sge_cmd_line = {
				"qsub"
			,	"-b"     , "y"
			,	"-o"     , "/dev/null"                                                                                                         // XXX : if verbose, collect stdout/sderr
			,	"-j"     , "y"
			,	"-shell" , "n"
			,	"-terse"
			,	"-N"     , sge_mk_name(repo_key+Job(j)->name())
			} ;
			SWEAR(+reqs) ;                                                                                                                     // why launch a job if for no req ?
			int16_t prio = ::numeric_limits<int16_t>::min() ; for( ReqIdx r : reqs ) prio = ::max( prio , req_prios[r] ) ;
			//
			if ( prio                                             ) { sge_cmd_line.push_back("-p"   ) ; sge_cmd_line.push_back(               to_string(prio     )) ; }
			if ( +cpu_rsrc && rs->cpu                             ) { sge_cmd_line.push_back("-l"   ) ; sge_cmd_line.push_back(cpu_rsrc+'='+::to_string(rs->cpu  )) ; }
			if ( +mem_rsrc && rs->mem                             ) { sge_cmd_line.push_back("-l"   ) ; sge_cmd_line.push_back(mem_rsrc+'='+::to_string(rs->mem  )) ; }
			if ( +tmp_rsrc && (rs->tmp!=0&&rs->tmp!=uint32_t(-1)) ) { sge_cmd_line.push_back("-l"   ) ; sge_cmd_line.push_back(tmp_rsrc+'='+::to_string(rs->tmp  )) ; }
			for( auto const& [k,v] : rs ->tokens )                  { sge_cmd_line.push_back("-l"   ) ; sge_cmd_line.push_back(k       +'='+::to_string(v        )) ; }
			if ( +rs->hard                                        ) {                                   for( ::string const& s : rs->hard ) sge_cmd_line.push_back(s) ; }
			if ( +rs->soft                                        ) { sge_cmd_line.push_back("-soft") ; for( ::string const& s : rs->soft ) sge_cmd_line.push_back(s) ; }
			//
			for( ::string const& c : cmd_line ) sge_cmd_line.push_back(c) ;
			//
			::pair_s<bool/*ok*/> digest = sge_exec_client( ::move(sge_cmd_line) , true/*gather_stdout*/ ) ;                                    // need to gather sge id
			Trace trace(BeChnl,"Sge::launch_job",repo_key,j,digest,sge_cmd_line,rs,STR(verbose)) ;
			if (!digest.second) throw "cannot submit SGE job "+Job(j)->name() ;
			return from_string<SgeId>(digest.first) ;
		}

		::pair_s<bool/*ok*/> sge_exec_client( ::vector_s&& cmd_line , bool gather_stdout=false ) const {
			::map_ss add_env = { { "SGE_ROOT" , no_slash(sge_root_dir_s) } } ;
			if (+sge_cell   ) add_env["SGE_CELL"        ] = sge_cell    ;
			if (+sge_cluster) add_env["SGE_CLUSTER_NAME"] = sge_cluster ;
			cmd_line[0] = sge_bin_dir_s+cmd_line[0] ;
			Child child {
				.cmd_line  = cmd_line
			,	.stdin_fd  =                                 Child::NoneFd
			,	.stdout_fd = gather_stdout ? Child::PipeFd : Child::NoneFd
			,	.stderr_fd =                                 Child::NoneFd
			,	.add_env   = &add_env
			} ;
			child.spawn() ;
			bool ok = child.wait_ok() ;
			if (!gather_stdout) return {{},ok} ;
			::string msg ;
			for(;;) {
				char buf[128] ;
				ssize_t cnt = ::read(child.stdout,buf,sizeof(buf)) ;
				if (cnt< 0) throw "cannot read stdout of child "+cmd_line[0] ;
				if (cnt==0) return {msg,ok} ;
				msg.append(buf,cnt) ;
			}
		}

		// data
		SpawnedMap mutable spawned_rsrcs     ;      // number of spawned jobs queued in sge queue
		::vector<int16_t>  req_prios         ;      // indexed by req
		uint32_t           n_max_queued_jobs = -1 ; // no limit by default
		::string           repo_key          ;      // a short identifier of the repository
		int16_t            dflt_prio         = 0  ; // used when not specified with lmake -b
		::string           cpu_rsrc          ;      // key to use to ask for cpu
		::string           mem_rsrc          ;      // key to use to ask for memory (in MB)
		::string           tmp_rsrc          ;      // key to use to ask for tmp    (in MB)
		::string           sge_bin_dir_s     ;
		::string           sge_cell          ;
		::string           sge_cluster       ;
		::string           sge_root_dir_s    ;
		Daemon             daemon            ;      // info sensed from sge daemon
	} ;

	DequeThread<::pair<SgeBackend const*,SgeId>> SgeBackend::_s_sge_cancel_thread ;

	//
	// init
	//

	bool _inited = (SgeBackend::s_init(),true) ;

	//
	// Daemon
	//

	::ostream& operator<<( ::ostream& os , Daemon const& d ) {
		return os << "Daemon(" << d.licenses <<')' ;
	}

	//
	// RsrcsData
	//

	::ostream& operator<<( ::ostream& os , RsrcsData const& rsd ) {
		/**/                                  os <<"(cpu="<<       rsd.cpu       ;
		if (rsd.mem              )            os <<",mem="<<       rsd.mem<<"MB" ;
		if (rsd.tmp!=uint32_t(-1))            os <<",tmp="<<       rsd.tmp<<"MB" ;
		for( auto const& [k,v] : rsd.tokens ) os <<','<< k <<'='<< v             ;
		if (+rsd.hard            )            os <<",H:"<<         rsd.hard      ;
		if (+rsd.soft            )            os <<",S:"<<         rsd.soft      ;
		return                                os <<')'                           ;
	}

	::vector_s _split_rsrcs(::string const& s) {
		// validate syntax as violating it could lead really unexpected behavior, such as executing an unexpected command
		::vector_s res = split(s) ;
		size_t     i   ;
		for( i=0 ; i<res.size() ; i++ ) {
			::string const& v = res[i] ;
			if (v[0]!='-') throw "bad option does not start with - : "+v ;
			switch (v[1]) {
				case 'a' : if (v=="-a"      ) { i++ ;                                                   continue ; }
				/**/       if (v=="-ac"     ) { i++ ;                                                   continue ; }
				/**/       if (v=="-ar"     ) { i++ ;                                                   continue ; } break ;
				case 'A' : if (v=="-A"      ) { i++ ;                                                   continue ; } break ;
				case 'b' : if (v=="-binding") { i++ ; i += res[i]=="env"||res[i]=="pe"||res[i]=="set" ; continue ; } break ;
				case 'c' : if (v=="-c"      ) { i++ ;                                                   continue ; }
				/**/       if (v=="-ckpt"   ) { i++ ;                                                   continue ; }
				/**/       if (v=="-clear"  ) {                                                         continue ; } break ;
				case 'd' : if (v=="-dc"     ) { i++ ;                                                   continue ; }
				/**/       if (v=="-display") { i++ ;                                                   continue ; }
				/**/       if (v=="-dl"     ) { i++ ;                                                   continue ; } break ;
				case 'h' : if (v=="-h"      ) { i++ ;                                                   continue ; } break ;
				case 'j' : if (v=="-js"     ) { i++ ;                                                   continue ; } break ;
				case 'l' : if (v=="-l"      ) { i++ ;                                                   continue ; } break ;
				case 'm' : if (v=="-m"      ) { i++ ;                                                   continue ; }
				/**/       if (v=="-masterq") { i++ ;                                                   continue ; } break ;
				case 'M' : if (v=="-M"      ) { i++ ;                                                   continue ; } break ;
				case 'n' : if (v=="-notify" ) {                                                         continue ; }
				/**/       if (v=="-now"    ) { i++ ;                                                   continue ; } break ;
				case 'N' : if (v=="-N"      ) { i++ ;                                                   continue ; } break ;
				case 'P' : if (v=="-P"      ) { i++ ;                                                   continue ; } break ;
				case 'p' : if (v=="-p"      ) { i++ ;                                                   continue ; }
				/**/       if (v=="-pe"     ) { i++ ; i++ ;                                             continue ; }
				/**/       if (v=="-pty"    ) { i++ ;                                                   continue ; } break ;
				case 'q' : if (v=="-q"      ) { i++ ;                                                   continue ; } break ;
				case 'R' : if (v=="-R"      ) { i++ ;                                                   continue ; } break ;
				case 'r' : if (v=="-r"      ) { i++ ;                                                   continue ; } break ;
				case 's' : if (v=="-sc"     ) { i++ ;                                                   continue ; } break ;
				case 'v' : if (v=="-v"      ) { i++ ;                                                   continue ; } break ;
				case 'V' : if (v=="-V"      ) {                                                         continue ; } break ;
				case 'w' : if (v=="-wd"     ) { i++ ;                                                   continue ; } break ;
				default : ;
			}
			throw "unexpected option : "+v ;
		}
		if (i!=res.size()) throw "option "+res.back()+" expects an argument" ;
		return res ;
	}
	inline RsrcsData::RsrcsData(::vmap_ss&& m) {
		sort(m) ;
		for( auto&& [k,v] : ::move(m) ) {
			switch (k[0]) {
				case 'c' : if (k=="cpu" ) { cpu  = from_string_with_units<    uint16_t>(v) ; continue ; } break ;
				case 'h' : if (k=="hard") { hard = _split_rsrcs                        (v) ; continue ; } break ;
				case 'm' : if (k=="mem" ) { mem  = from_string_with_units<'M',uint32_t>(v) ; continue ; } break ;
				case 's' : if (k=="soft") { soft = _split_rsrcs                        (v) ; continue ; } break ;
				case 't' : if (k=="tmp" ) { tmp  = from_string_with_units<'M',uint32_t>(v) ; continue ; } break ;
				case '-' : throw "resource cannot start with - :"+k ;
				default : ;
			}
			tokens.emplace_back( k , from_string_with_units<uint64_t>(v) ) ;
		}
	}
	::vmap_ss RsrcsData::mk_vmap(void) const {
		::vmap_ss res ;
		// It may be interesting to know the number of cpu reserved to know how many thread to launch in some situation
		if (cpu              )            res.emplace_back( "cpu" , to_string_with_units     (cpu) ) ;
		if (mem              )            res.emplace_back( "mem" , to_string_with_units<'M'>(mem) ) ;
		if (tmp!=uint32_t(-1))            res.emplace_back( "tmp" , to_string_with_units<'M'>(tmp) ) ;
		for( auto const& [k,v] : tokens ) res.emplace_back( k     , to_string_with_units     (v  ) ) ;
		return res ;
	}

	//
	// sge API
	//

	Daemon sge_sense_daemon(SgeBackend const& be) {
		if ( !be.sge_exec_client( { "qsub" , "-b" , "y" , "-o" , "/dev/null" , "-e" , "/dev/null" , "/dev/null" } ).second ) throw "no SGE daemon"s ;
		return {} ;
	}


	void sge_cancel(::pair<SgeBackend const*,SgeId> const& info) {
		info.first->sge_exec_client({"qdel",to_string(info.second)}) ; // if error, job is most certainly already dead, nothing to do
	}

	::string sge_mk_name(::string&& s) {
		for( size_t i=0 ; i<s.size() ; i++ )
			switch (s[i]) {
				case '/'  : s[i] = '|' ; break ; // this char is forbidden in SGE names (cf man 5 sge_types), replace with best approximation (for cosmetic only, ambiguities are acceptable)
				case ':'  : s[i] = ';' ; break ;
				case '@'  : s[i] = 'a' ; break ;
				case '\\' : s[i] = '|' ; break ;
				case '*'  : s[i] = '#' ; break ;
				case '?'  : s[i] = '!' ; break ;
				default : ;
			}
		return ::move(s) ;
	}
}
