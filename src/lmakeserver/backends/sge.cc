// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

// doc : https://wiki.archlinux.org/title/Son_of_Grid_Engine

#include "generic.hh" // /!\ must be first because Python.h must be first

#include "disk.hh"
#include "process.hh"

using namespace Disk ;

namespace Backends::Sge {

	struct SgeBackend ;

	//
	// resources
	//

	struct RsrcsData {
		friend ::string& operator+=( ::string& , RsrcsData const& ) ;
		// cxtors & casts
		RsrcsData(           ) = default ;
		RsrcsData(::vmap_ss&&) ;
		// accesses
		bool operator==(RsrcsData const&) const = default ;
		// services
		RsrcsData round(Backend const&) const {
			// rounding is only used to avoid too many waiting queues, only criteria to take into account are those that decide launch/not launch
			RsrcsData res ;
			//                         prio is not significant for launching/not launching, not pertinent
			/**/                       res.cpu  = round_rsrc(cpu) ;
			/**/                       res.mem  = round_rsrc(mem) ;
			/**/                       res.tmp  = round_rsrc(tmp) ;
			/**/                       res.hard = hard            ; // cannot round as syntax is not managed
			//                         soft are not signficant for launching/not launching, not pertinent
			for( auto const& [k,t] : tokens ) res.tokens.emplace_back(k,round_rsrc(t)) ;
			return res ;
		}
		// data
		int16_t            prio   = 0 ; // priority              : qsub -p <prio>     (prio comes from lmake -b               )
		uint32_t           cpu    = 0 ; // number of logical cpu : qsub -l <cpu_rsrc> (cpu_rsrc comes from config, always hard)
		uint32_t           mem    = 0 ; // memory   in MB        : qsub -l <mem_rsrc> (mem_rsrc comes from config, always hard)
		uint32_t           tmp    = 0 ; // tmp disk in MB        : qsub -l <tmp_rsrc> (tmp_rsrc comes from config, always hard) default : dont manage tmp size (provide infinite storage, reserve none)
		::vector_s         hard   ;     // hard options          : qsub -hard <val>
		::vector_s         soft   ;     // soft options          : qsub -soft <val>
		::vmap_s<uint64_t> tokens ;     // generic resources     : qsub -l<key>=<val> (for each entry            , always hard)
		// services
		::vmap_ss mk_vmap() const ;
		size_t hash() const {
			Hash::Xxh h ;
			h += prio   ;
			h += cpu    ;
			h += mem    ;
			h += tmp    ;
			h += tokens ;
			h += hard   ;
			h += soft   ;
			return +h.digest() ;
		}
	} ;

}

//
// SgeBackend
//

namespace Backends::Sge {

	using SgeId = uint32_t ;

	void     sge_cancel      (::pair<SgeBackend const*,SgeId> const&) ;
	void     sge_sense_daemon(SgeBackend const&                     ) ;
	::string sge_mk_name     (::string&&                            ) ;

	constexpr Tag MyTag = Tag::Sge ;

	struct SgeBackend
	:	             GenericBackend<MyTag,'G'/*LaunchThreadKey*/,RsrcsData>
	{	using Base = GenericBackend<MyTag,'G'/*LaunchThreadKey*/,RsrcsData> ;

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
		static QueueThread<::pair<SgeBackend const*,SgeId>> _s_sge_cancel_thread ; // when a req is killed, a lot of queued jobs may be canceled, better to do it in a separate thread

		// accesses

		bool call_launch_after_start() const override { return true ; }

		// services

		void sub_config( ::vmap_ss const& dct , ::vmap_ss const& env_ , bool dyn ) override {
			Trace trace(BeChnl,"Sge::config",STR(dyn),dct) ;
			//
			repo_key = base_name(no_slash(*g_repo_root_s))+':' ; // cannot put this code directly as init value as g_repo_root_s is not available early enough
			for( auto const& [k,v] : dct ) {
				try {
					switch (k[0]) {
						case 'b' : if (k=="bin"              ) { sge_bin_s         = with_slash               (v)  ; continue ; } break ;
						case 'c' : if (k=="cell"             ) { sge_cell          =                           v   ; continue ; }
						/**/       if (k=="cluster"          ) { sge_cluster       =                           v   ; continue ; }
						/**/       if (k=="cpu_resource"     ) { cpu_rsrc          =                           v   ; continue ; } break ;
						case 'd' : if (k=="default_prio"     ) { dflt_prio         = from_string<int16_t >    (v)  ; continue ; } break ;
						case 'm' : if (k=="mem_resource"     ) { mem_rsrc          =                           v   ; continue ; } break ;
						case 'n' : if (k=="n_max_queued_jobs") { n_max_queued_jobs = from_string<uint32_t>    (v)  ; continue ; } break ;
						case 'r' : if (k=="repo_key"         ) { repo_key          =                           v   ; continue ; }
						/**/       if (k=="root"             ) { sge_root_s        = with_slash               (v)  ; continue ; } break ;
						case 't' : if (k=="tmp_resource"     ) { tmp_rsrc          =                           v   ; continue ; } break ;
					DN}
				} catch (::string const& e) { trace("bad_val",k,v) ; throw "wrong value for entry "   +k+": "+v ; }
				/**/                        { trace("bad_key",k  ) ; throw "unexpected config entry: "+k        ; }
			}
			throw_unless( +sge_bin_s  , "must specify bin to configure SGE" ) ;
			throw_unless( +sge_root_s , "must specify root to configure SGE") ;
			env = env_ ;
			_sge_env_vec.clear() ;
			/**/              _sge_env_vec.push_back("SGE_ROOT="   +no_slash(sge_root_s )) ;
			if (+sge_cell   ) _sge_env_vec.push_back("SGE_CELL="   +         sge_cell    ) ;
			if (+sge_cluster) _sge_env_vec.push_back("SGE_CLUSTER="+         sge_cluster ) ;
			_sge_env.reset(new const char*[_sge_env_vec.size()+1]) ;
			{	size_t i = 0 ;
				for( ::string const& kv : _sge_env_vec ) _sge_env[i++] = kv.c_str() ;
				/**/                                     _sge_env[i  ] = nullptr    ;
			}
			if (!dyn) {
				sge_sense_daemon(self) ;
				_s_sge_cancel_thread.open('C',sge_cancel) ; s_record_thread('C',_s_sge_cancel_thread.thread) ;
			}
			trace("done") ;
		}

		void open_req( Req req , JobIdx n_jobs ) override {
			Base::open_req(req,n_jobs) ;
			::string const& prio = req->options.flag_args[+ReqFlag::Backend] ;
			grow(req_prios,+req) = +prio ? from_string<int16_t>(prio) : dflt_prio ;
		}

		void close_req(Req req) override {
			Base::close_req(req) ;
			if(!reqs) SWEAR(!spawned_rsrcs,spawned_rsrcs) ;
		}

		::vmap_ss export_( RsrcsData const& rs              ) const override { return rs.mk_vmap()  ; }
		RsrcsData import_( ::vmap_ss     && rsa , Req , Job ) const override { return {::move(rsa)} ; }
		//
		bool/*ok*/ fit_now(Rsrcs const& rs) const override {
			return spawned_rsrcs.n_spawned(rs) < n_max_queued_jobs ;
		}
		void acquire_rsrcs(Rsrcs const& rs) const override {
			spawned_rsrcs.inc(rs) ;
		}
		void start_rsrcs(Rsrcs const& rs) const override {
			spawned_rsrcs.dec(rs) ;
		}
		::string start_job( Job , SpawnedEntry const& se ) const override {
			SWEAR(+se.rsrcs) ;
			return cat("sge_id:",se.id.load()) ;
		}
		::pair_s<bool/*retry*/> end_job( Job j , SpawnedEntry const& se , Status ) const override {
			if (!se.verbose) return {{}/*msg*/,true/*retry*/} ;                                        // common case, must be fast, if job is in error, better to ask slurm why, e.g. could be OOM
			::string msg ;
			try                       { msg = AcFd(get_stderr_file(j)).read() ; }
			catch (::string const& e) { msg = e                               ; }
			return { ::move(msg) , true/*retry*/  } ;
		}
		::pair_s<HeartbeatState> heartbeat_queued_job( Job job , SpawnedEntry const& se ) const override {
			if (sge_exec_client({"qstat","-j",::to_string(se.id)})) return { {}/*msg*/ , HeartbeatState::Alive } ;
			::string msg ;
			if (se.verbose)
				try                       { msg = AcFd(get_stderr_file(job)).read() ; }
				catch (::string const& e) { msg = e                                 ; }
			else
				msg = "lost job "+::to_string(se.id) ;
			return { ::move(msg) , HeartbeatState::Lost } ;                                            // XXX! : try to distinguish between Lost and Err
		}
		void kill_queued_job(SpawnedEntry const& se) const override {
			if (!se.zombie) _s_sge_cancel_thread.push(::pair(this,se.id.load())) ;                     // asynchronous (as faster and no return value) cancel
		}
		SpawnId launch_job( ::stop_token , Job j , ::vector<ReqIdx> const& reqs , Pdate /*prio*/ , ::vector_s const& cmd_line , SpawnedEntry const& se ) const override {
			::string stderr = se.verbose ? dir_guard(get_stderr_file(j)) : "/dev/null"s ;
			::vector_s sge_cmd_line = {
				"qsub"
			,	"-terse"
			,	"-b"     , "y"
			,	"-o"     ,                                              "/dev/null"s
			,	"-e"     , se.verbose ? dir_guard(get_stderr_file(j)) : "/dev/null"s
			,	"-shell" , "n"
			,	"-N"     , sge_mk_name(repo_key+Job(j)->name())
			} ;
			if (+env) {
				::string env_str ;
				First    first   ;
				for( auto const& [k,v] : env ) env_str <<first("",",")<< k <<'='<< v ;
				sge_cmd_line.emplace_back("-v"   ) ;
				sge_cmd_line.emplace_back(env_str) ;
			}
			SWEAR(+reqs) ;                                                                             // why launch a job if for no req ?
			int16_t prio = Min<int16_t> ; for( ReqIdx r : reqs ) prio = ::max( prio , req_prios[r] ) ;
			//
			Rsrcs const& rs = se.rsrcs ;
			if ( prio                 )            { sge_cmd_line.push_back("-p"   ) ; sge_cmd_line.push_back(               to_string(prio     )) ; }
			if ( +cpu_rsrc && rs->cpu )            { sge_cmd_line.push_back("-l"   ) ; sge_cmd_line.push_back(cpu_rsrc+'='+::to_string(rs->cpu  )) ; }
			if ( +mem_rsrc && rs->mem )            { sge_cmd_line.push_back("-l"   ) ; sge_cmd_line.push_back(mem_rsrc+'='+::to_string(rs->mem  )) ; }
			if ( +tmp_rsrc && rs->tmp )            { sge_cmd_line.push_back("-l"   ) ; sge_cmd_line.push_back(tmp_rsrc+'='+::to_string(rs->tmp  )) ; }
			for( auto const& [k,v] : rs ->tokens ) { sge_cmd_line.push_back("-l"   ) ; sge_cmd_line.push_back(k       +'='+::to_string(v        )) ; }
			if ( +rs->hard            )            {                                   for( ::string const& s : rs->hard ) sge_cmd_line.push_back(s) ; }
			if ( +rs->soft            )            { sge_cmd_line.push_back("-soft") ; for( ::string const& s : rs->soft ) sge_cmd_line.push_back(s) ; }
			//
			for( ::string const& c : cmd_line ) sge_cmd_line.push_back(c) ;
			//
			Trace trace(BeChnl,"Sge::launch_job",repo_key,j,sge_cmd_line,rs) ;
			//
			return sge_exec_qsub(::move(sge_cmd_line)) ;
		}

		bool/*ok*/ sge_exec_client(::vector_s&& cmd_line) const {
			Trace trace(BeChnl,"sge_exec_client",cmd_line) ;
			TraceLock lock { _sge_mutex , BeChnl , "sge_client" } ;
			cmd_line[0] = sge_bin_s+cmd_line[0] ;
			//
			const char** cmd_line_ = new const char*[cmd_line.size()+1] ;
			{	size_t i = 0 ;
				for( ::string const& a : cmd_line ) cmd_line_[i++] = a.c_str() ;
				/**/                                cmd_line_[i  ] = nullptr   ;
			}
			// calling ::vfork is faster as lmakeserver is a heavy process, so walking the page table is a significant perf hit
			pid_t pid = ::vfork() ;                                                                             // NOLINT(clang-analyzer-security.insecureAPI.vfork)
			// NOLINTBEGIN(clang-analyzer-unix.Vfork) allowed in Linux
			if (!pid) {                                                                                         // in child
				::close(Fd::Stdin ) ;                                                                           // ensure no stdin (defensive programming)
				::close(Fd::Stdout) ;
				::execve( cmd_line_[0] , const_cast<char**>(cmd_line_) , const_cast<char**>(_sge_env.get()) ) ;
				Fd::Stderr.write(cat("cannot exec ",cmd_line[0],'\n')) ;                                        // NO_COV defensive programming
				::_exit(+Rc::System) ;                                                                          // NO_COV in case exec fails
			}
			SWEAR(pid>0) ;
			// NOLINTEND(clang-analyzer-unix.Vfork) allowed in Linux
			int  wstatus ;
			int  rc      = ::waitpid(pid,&wstatus,0) ; swear_prod(rc==pid,"cannot wait for pid",pid) ;
			trace("done_pid",wstatus) ;
			delete[] cmd_line_ ;         // safe even if not waiting pid, as thread is suspended by ::vfork until child has exec'ed
			return wstatus_ok(wstatus) ;
		}

		SgeId sge_exec_qsub(::vector_s&& cmd_line) const {
			SWEAR( cmd_line[0]=="qsub" && cmd_line[1]=="-terse" ) ;                                             // only meant to accept a short stdout
			Trace trace(BeChnl,"sge_exec_qsub",cmd_line) ;
			TraceLock lock { _sge_mutex , BeChnl , "sge_exec_qsub" } ;
			cmd_line[0] = sge_bin_s+cmd_line[0] ;
			//
			const char** cmd_line_ = new const char*[cmd_line.size()+1] ;
			{	size_t i = 0 ;
				for( ::string const& a : cmd_line ) cmd_line_[i++] = a.c_str() ;
				/**/                                cmd_line_[i  ] = nullptr   ;
			}
			AcPipe c2p { New , O_NONBLOCK , true/*no_std*/ } ;
			// calling ::vfork is faster as lmakeserver is a heavy process, so walking the page table is a significant perf hit
			pid_t  pid = ::vfork() ;                                                                            // NOLINT(clang-analyzer-security.insecureAPI.vfork)
			// NOLINTBEGIN(clang-analyzer-unix.Vfork) allowed in Linux
			if (!pid) {                                                                                         // in child
				SWEAR(c2p.read .fd>Fd::Std.fd,c2p.read ) ;                                                      // ensure we can safely close what needs to be closed
				SWEAR(c2p.write.fd>Fd::Std.fd,c2p.write) ;                                                      // .
				::dup2(c2p.write,Fd::Stdout) ;
				::close(Fd::Stdin) ;                                                                            // ensure no stdin (defensive programming)
				::close(c2p.read ) ;                                                                            // dont touch c2p object as it is shared with parent
				::close(c2p.write) ;                                                                            // .
				::execve( cmd_line_[0] , const_cast<char**>(cmd_line_) , const_cast<char**>(_sge_env.get()) ) ;
				Fd::Stderr.write(cat("cannot exec ",cmd_line[0],'\n')) ;                                        // NO_COV defensive programming
				::_exit(+Rc::System) ;                                                                          // NO_COV in case exec fails
			}
			SWEAR(pid>0) ;                                                                            // ensure vfork worked
			// NOLINTEND(clang-analyzer-unix.Vfork) allowed in Linux
			// Normal code to get the content of stdout is to read the c2p pipe, and when we see eof, waitpid until sub-process has terminated.
			// But it seems that if we do things this way, there are cases where c2p.read eof never occurs (or after a very long time, >300s).
			// So we do things the other way around : we first waitpid for sub-process to terminate, then we read (non-blocking) c2p.read.
			// Curiously, this is much better and does not exhibit the long waiting time case.
			// Pipe capacity is 16 pages, i.e. usually 64k (man 7 pipe), more than enough for a job id.
			int wstatus ;
			int rc      = ::waitpid(pid,&wstatus,0) ; swear_prod(rc==pid,"cannot wait for pid",pid) ;
			delete[] cmd_line_ ;                                                                      // safe even if not waiting pid, as thread is suspended by ::vfork until child has exec'ed
			if (!wstatus_ok(wstatus)){                                                                // START_OF_NO_COV defensive programming
				trace("fail_pid") ;
				::string msg = "cannot submit SGE job :" ;
				for( ::string const& c : cmd_line ) msg <<' '<< c ;
				throw msg ;
			}                                                                                         // END_OF_NO_COV
			::string cmd_out(100,0) ;                                                                 // 100 is plenty for a job id
			c2p.write.close() ;
			trace("wait_cmd_out",c2p.read) ;
			ssize_t cnt = ::read( c2p.read , cmd_out.data() , cmd_out.size() ) ;
			if (cnt==0                     ) FAIL("no data from"         ,cmd_line[0]                          ) ;
			if (cnt<0                      ) FAIL("cannot read stdout of",cmd_line[0],':',::strerror(errno)    ) ;
			if (size_t(cnt)==cmd_out.size()) FAIL("stdout overflow of"   ,cmd_line[0],':',cmd_out,"..."        ) ;
			if (cmd_out[cnt-1]!='\n'       ) FAIL("incomplete stdout of" ,cmd_line[0],':',cmd_out.substr(0,cnt)) ;
			cmd_out.resize(cnt) ;
			trace("done_cmd_out",cmd_out) ;
			return from_string<SgeId>(cmd_out) ;
		}

		// data
		SpawnedMap mutable spawned_rsrcs     ;      // number of spawned jobs queued in sge queue
		::vector<int16_t>  req_prios         ;      // indexed by req
		uint32_t           n_max_queued_jobs = 10 ; // by default, limit to 10 the number of jobs waiting for a given set of resources
		::string           repo_key          ;      // a short identifier of the repository
		int16_t            dflt_prio         = 0  ; // used when not specified with lmake -b
		::string           cpu_rsrc          ;      // key to use to ask for cpu
		::string           mem_rsrc          ;      // key to use to ask for memory (in MB)
		::string           tmp_rsrc          ;      // key to use to ask for tmp    (in MB)
		::string           sge_bin_s         ;
		::string           sge_cell          ;
		::string           sge_cluster       ;
		::string           sge_root_s        ;
		::vmap_ss          env               ;
	private :
		::unique_ptr<const char*[]>  _sge_env     ;
		::vector_s                   _sge_env_vec ; // hold _sge_env strings of the form key=value
		Mutex<MutexLvl::Sge> mutable _sge_mutex   ; // ensure no more than a single outstanding request to daemon

	} ;

	QueueThread<::pair<SgeBackend const*,SgeId>> SgeBackend::_s_sge_cancel_thread ;

	//
	// init
	//

	bool _inited = (SgeBackend::s_init(),true) ;

	//
	// RsrcsData
	//

	::string& operator+=( ::string& os , RsrcsData const& rsd ) {                  // START_OF_NO_COV
		/**/                                  os <<"(cpu="<<       rsd.cpu       ;
		if (rsd.mem   )                       os <<",mem="<<       rsd.mem<<"MB" ;
		if (rsd.tmp   )                       os <<",tmp="<<       rsd.tmp<<"MB" ;
		for( auto const& [k,v] : rsd.tokens ) os <<','<< k <<'='<< v             ;
		if (+rsd.hard )                       os <<",H:"<<         rsd.hard      ;
		if (+rsd.soft )                       os <<",S:"<<         rsd.soft      ;
		return                                os <<')'                           ;
	}                                                                              // END_OF_NO_COV

	::vector_s _split_rsrcs(::string const& s) {
		// validate syntax as violating it could lead to unexpected behavior, such as executing an unexpected command
		::vector_s res = split(s) ;
		size_t     i   ;
		for( i=0 ; i<res.size() ; i++ ) {
			::string const& v = res[i] ;
			throw_unless( v[0]=='-' , "bad option does not start with - : ",v ) ;
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
			DN}
			throw "unexpected option : "+v ;
		}
		throw_unless( i==res.size() , "option ",res.back()," expects an argument" ) ;
		return res ;
	}
	inline RsrcsData::RsrcsData(::vmap_ss&& m) {
		::sort(m) ;
		for( auto&& [k,v] : ::move(m) ) {
			switch (k[0]) {
				case 'c' : if (k=="cpu" ) { cpu  = from_string_with_unit<    uint32_t              >(v) ; continue ; } break ;
				case 'h' : if (k=="hard") { hard = _split_rsrcs                                     (v) ; continue ; } break ;
				case 'm' : if (k=="mem" ) { mem  = from_string_with_unit<'M',uint32_t,true/*RndUp*/>(v) ; continue ; } break ;
				case 's' : if (k=="soft") { soft = _split_rsrcs                                     (v) ; continue ; } break ;
				case 't' : if (k=="tmp" ) { tmp  = from_string_with_unit<'M',uint32_t,true/*RndUp*/>(v) ; continue ; } break ;
				case '-' : throw "resource cannot start with -:"+k ;
			DN}
			tokens.emplace_back( k , from_string_with_unit<uint64_t>(v) ) ;
		}
	}
	::vmap_ss RsrcsData::mk_vmap() const {
		::vmap_ss res ;
		// It may be interesting to know the number of cpu reserved to know how many thread to launch in some situation
		if (cpu              )            res.emplace_back( "cpu" , to_string_with_unit     (cpu) ) ;
		if (mem              )            res.emplace_back( "mem" , to_string_with_unit<'M'>(mem) ) ;
		if (tmp!=uint32_t(-1))            res.emplace_back( "tmp" , to_string_with_unit<'M'>(tmp) ) ;
		for( auto const& [k,v] : tokens ) res.emplace_back( k     , to_string_with_unit     (v  ) ) ;
		return res ;
	}

	//
	// sge API
	//

	void sge_sense_daemon(SgeBackend const& be) {
		Trace trace("sge_sense_daemon") ;
		try {
			SgeId id = be.sge_exec_qsub( { "qsub" , "-terse" , "-b" , "y" , "-N" , "<sense_daemon>" , "-o" , "/dev/null" , "-e" , "/dev/null" , "/dev/null" } ) ;
			trace("sense_id",id) ;
		} catch (::string const& e) {
			trace("no_sge_daemon",e) ;
			throw "no SGE daemon"s ;
		}
	}


	void sge_cancel(::pair<SgeBackend const*,SgeId> const& info) {
		info.first->sge_exec_client({"qdel",to_string(info.second)}) ; // if error, job is most certainly already dead, nothing to do
	}

	::string sge_mk_name(::string&& s) {
		for( size_t i : iota(s.size()) )
			switch (s[i]) {
				case '/'  : s[i] = '|' ; break ; // this char is forbidden in SGE names (cf man 5 sge_types), replace with best approximation (for cosmetic only, ambiguities are acceptable)
				case ':'  : s[i] = ';' ; break ;
				case '@'  : s[i] = 'a' ; break ;
				case '\\' : s[i] = '|' ; break ;
				case '*'  : s[i] = '#' ; break ;
				case '?'  : s[i] = '!' ; break ;
			DN}
		return ::move(s) ;
	}
}
