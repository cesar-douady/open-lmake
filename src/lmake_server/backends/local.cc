// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "generic.hh" // /!\ must be first because Python.h must be first

// PER_BACKEND : there must be a file describing each backend (providing the sub-backend class, deriving from GenericBackend if possible (simpler), else Backend)

using namespace Disk ;
using namespace Hash ;

namespace Backends::Local {

	struct LocalBackend ;

	//
	// resources
	//

	using Rsrc = uint32_t ;

	struct RsrcsData : ::vector<Rsrc> {
		// cxtors & casts
		RsrcsData() = default ;
		RsrcsData( size_t sz                                                          ) : ::vector<Rsrc>(sz) {}
		RsrcsData( ::vmap_ss const& , ::umap_s<size_t> const& idxs , bool rnd_up=true ) ;
		//
		::vmap_ss mk_vmap(::vector_s const& keys) const ;
		// services
		RsrcsData& operator+=(RsrcsData const& rsrcs) { SWEAR(size()==rsrcs.size(),size(),rsrcs.size()) ; for( size_t i : iota(size()) ) self[i] += rsrcs[i] ; return self ; }
		RsrcsData& operator-=(RsrcsData const& rsrcs) { SWEAR(size()==rsrcs.size(),size(),rsrcs.size()) ; for( size_t i : iota(size()) ) self[i] -= rsrcs[i] ; return self ; }
		RsrcsData round(Backend const&) const ;
		size_t    hash (              ) const {
			return +Crc( New , static_cast<::vector<Backends::Local::Rsrc> const&>(self) ) ;
		}
	} ;

}

//
// LocalBackend
//

namespace Backends::Local {

	constexpr Tag MyTag = Tag::Local ;

	struct LocalBackend : GenericBackend<MyTag,'L'/*LaunchThreadKey*/,RsrcsData> {
		// init
		static void s_init() {
			static bool once=false ; if (once) return ; else once = true ;
			LocalBackend& self_ = *new LocalBackend ;
			s_register(MyTag,self_) ;
		}

		// statics
	private :
		static void _s_wait_job(pid_t pid) { // execute in a separate thread
			Trace trace(BeChnl,"wait",pid) ;
			::waitpid( pid , nullptr/*wstatus*/ , 0/*options*/ ) ;
			trace("waited",pid) ;
		}

		// accesses
	public :
		bool call_launch_after_end() const override { return true ; }

		// services

		void sub_config( ::vmap_ss const& dct , ::vmap_ss const& env_ ) override {
			// add an implicit resource <single> to manage jobs localized from remote backends
			Trace trace(BeChnl,"Local::config",dct) ;
			static bool s_first_time = true ; bool first_time = s_first_time ; s_first_time = false ;
			//
			rsrc_keys.reserve(dct.size()+1/*<single>*/) ;
			bool seen_single = false ;
			if (first_time) {
				SWEAR( !rsrc_keys , rsrc_keys ) ;
				for( auto const& [k,v] : dct ) { rsrc_idxs[k         ] = rsrc_keys.size() ; rsrc_keys.push_back   (k         ) ; seen_single |= k=="<single>" ; }
				if (!seen_single)              { rsrc_idxs["<single>"] = rsrc_keys.size() ; rsrc_keys.emplace_back("<single>") ;                                }
				occupied  = RsrcsData(rsrc_keys.size()) ;
			} else {                                                                                                // keep currently used resources
				::set_s old_names = mk_set    (rsrc_keys) ;                                                         // use ordered set for reporting
				::set_s new_names = mk_key_set(dct      ) ;
				seen_single = !new_names.insert("<single>").second ;
				if (new_names!=old_names) throw cat("cannot change resource names from ",old_names," to ",new_names," while lmake is running") ;
			}
			trace(BeChnl,"occupied_rsrcs",'=',occupied) ;
			//
			capacity_ = RsrcsData( dct , rsrc_idxs , false/*rnd_up*/ ) ;
			if (!seen_single) capacity_.back()/*<single>*/ = 1 ;                                                    // if not mentioned in dct => force to 1 instead of default 0
			//
			SWEAR( rsrc_keys.size()==capacity_.size() , rsrc_keys.size() , capacity_.size() ) ;
			for( size_t i : iota(capacity_.size()) ) public_capacity.emplace_back( rsrc_keys[i] , capacity_[i] ) ;
			trace("capacity",capacity()) ;
			_wait_queue.open( 'T' , _s_wait_job ) ; s_record_thread('T',_wait_queue.thread) ;
			//
			if ( first_time && rsrc_idxs.contains("cpu") ) {                                                        // ensure each job can compute CRC on all cpu's in parallel
				struct rlimit rl ;
				::getrlimit(RLIMIT_NPROC,&rl) ;
				if ( rl.rlim_cur!=RLIM_INFINITY && rl.rlim_cur<rl.rlim_max ) {
					::rlim_t new_limit = rl.rlim_cur + capacity_[rsrc_idxs["cpu"]]*thread::hardware_concurrency() ;
					if ( rl.rlim_max!=RLIM_INFINITY && new_limit>rl.rlim_max ) new_limit = rl.rlim_max ;            // hard limit overflow
					rl.rlim_cur = new_limit ;
					::setrlimit(RLIMIT_NPROC,&rl) ;
				}
			}
			_env.reset(new const char*[env_.size()+1]) ;
			{	size_t i = 0 ;
				_env_vec.clear() ;
				for( auto const& [k,v] : env_ ) {
					_env_vec.push_back(cat(k,'=',v)) ;
					_env[i++] = _env_vec.back().c_str() ;
				}
				_env[i] = nullptr ;
			}
			trace("done") ;
		}
		::vmap_s<size_t> const& capacity() const override {
			return public_capacity ;
		}
		::vmap_ss mk_lcl( ::vmap_ss&& rsrcs , ::vmap_s<size_t> const& /*capacity*/ , JobIdx ) const override {      // START_OF_NO_COV defensive programming (cannot make local local)
			return ::move(rsrcs) ;
		}                                                                                                           // END_OF_NO_COV
		//
		::vmap_ss  export_   ( RsrcsData const& rs             ) const override { return rs.mk_vmap(rsrc_keys)           ; }
		RsrcsData  import_   ( ::vmap_ss     && rs , Req , Job ) const override { return RsrcsData(::move(rs),rsrc_idxs) ; }
		::string lacking_rsrc( RsrcsData const& rs             ) const override {
			for( size_t i : iota(rs.size()) ) if (rs[i]>capacity_[i]) return cat("not enough resource ",rsrc_keys[i]," (asked ",rs[i]," but only ",capacity_[i]," available)") ;
			return {} ;
		}
		bool/*ok*/ fit_now(Rsrcs const& rs) const override {
			RsrcsData const& rds = *rs ;
			for( size_t i : iota(occupied.size()) ) if ( occupied[i]+rds[i] > capacity_[i] ) return false ;
			return true ;
		}
		void acquire_rsrcs(Rsrcs const& rs) const override {
			occupied += *rs ;
			Trace trace(BeChnl,"occupied_rsrcs",rs,'+',occupied) ;
			for( size_t i : iota(occupied.size()) ) SWEAR(occupied[i]<=capacity_[i]) ;
		}
		void end_rsrcs(Rsrcs const& rs) const override {
			occupied -= *rs ;
			Trace trace(BeChnl,"occupied_rsrcs",rs,'-',occupied) ;
			for( size_t i : iota(occupied.size()) ) SWEAR(occupied[i]<=capacity_[i]) ;
		}
		//
		::string start_job( Job , SpawnedEntry const& se ) const override {
			return cat("pid:",se.id.load()) ;
		}
		::pair_s<bool/*retry*/> end_job( Job job , SpawnedEntry const& se , Status status ) const override {
			_wait_queue.push(se.id) ;                                                                               // defer wait in case job_exec process does some time consuming book-keeping
			if (!se.verbose) return {{}/*msg*/,true/*retry*/} ;                                            // common case, must be fast, if job is in error, better to ask slurm why, e.g. could be OOM
			::string msg ;
			if (status!=Status::Ok) msg <<"return status : "<< status <<'\n' ;
			try                       { msg = AcFd(get_stderr_file(job)).read() ; }
			catch (::string const& e) { msg = e                                 ; }
			return { ::move(msg) , status==Status::Ok/*retry*/ } ;                                         // retry if garbage
		}
		::pair_s<HeartbeatState> heartbeat_queued_job( Job job , SpawnedEntry const& se ) const override { // called after job_exec has had time to start
			SWEAR(se.id) ;
			int wstatus = 0 ;
			if (::waitpid(se.id,&wstatus,WNOHANG)==0) return { {}/*msg*/ , HeartbeatState::Alive } ;       // process is still alive
			::string msg ;
			if (se.verbose)
				try                       { msg = AcFd(get_stderr_file(job)).read() ; }
				catch (::string const& e) { msg = e                                 ; }
			if (wstatus_ok(wstatus)) return { ::move(msg) , HeartbeatState::Lost } ;                       // process died long before (already waited) or just died with no error
			else                     return { ::move(msg) , HeartbeatState::Err  } ;                       // process just died with an error
		}
		void kill_queued_job(SpawnedEntry const& se) const override {
			if (se.zombie) return ;
			kill_process(se.id,SIGHUP) ;                                                                   // jobs killed here have not started yet, so we just want to kill job_exec
			_wait_queue.push(se.id) ;                                                                      // defer wait in case job_exec process does some time consuming book-keeping
		}
		SpawnId launch_job( ::stop_token , Job job , ::vector<ReqIdx> const& , Pdate /*prio*/ , ::vector_s const& cmd_line , SpawnedEntry const& se ) const override {
			::vector<const char*> cmd_line_ ; cmd_line_.reserve(cmd_line.size()+1) ;
			for( ::string const& a : cmd_line ) cmd_line_.push_back(a.c_str()) ;
			/**/                                cmd_line_.push_back(nullptr  ) ;
			// calling ::vfork is significantly faster as lmake_server is a heavy process, so walking the page table is a significant perf hit
			pid_t pid = ::vfork() ;                                                                                      // NOLINT(clang-analyzer-security.insecureAPI.vfork)
			// NOLINTBEGIN(clang-analyzer-unix.Vfork) allowed in Linux
			if (!pid) {                                                                                                  // in child
				if (se.verbose) {
					AcFd stderr_fd { get_stderr_file(job) , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666,.no_std=true} } ; // close fd once it has been dup2'ed
					::dup2(stderr_fd,Fd::Stderr) ;                                                                       // we do *not* want the O_CLOEXEC flag as we are precisely preparing ...
				}                                                                                                        // ... fd for child
				::execve( cmd_line_[0] , const_cast<char**>(cmd_line_.data()) , const_cast<char**>(_env.get()) ) ;
				Fd::Stderr.write("cannot exec job_exec\n") ;                                                             // NO_COV defensive programming
				::_exit(+Rc::System) ;                                                                                   // NO_COV defensive programming, in case exec fails
			}
			SWEAR(pid>0) ;
			// NOLINTEND(clang-analyzer-unix.Vfork) allowed in Linux
			return pid ;
		}

		// data
		::umap_s<size_t>  rsrc_idxs       ;
		::vector_s        rsrc_keys       ;
		RsrcsData         capacity_       ;
		RsrcsData mutable occupied        ;
		::vmap_s<size_t>  public_capacity ;
	private :
		QueueThread<pid_t> mutable  _wait_queue ;
		::unique_ptr<const char*[]> _env        ; // directly call ::execve without going through Child to improve perf
		::vector_s                  _env_vec    ; // hold _env strings of the form key=value

	} ;

	bool _inited = (LocalBackend::s_init(),true) ;

	inline RsrcsData::RsrcsData( ::vmap_ss const& m , ::umap_s<size_t> const& idxs , bool rnd_up ) {
		resize(idxs.size()) ;
		bool non_null = false ;
		for( auto const& [k,v] : m ) {
			auto it = idxs.find(k) ;
			throw_unless( it!=idxs.end() , "no resource ",k," for backend ",MyTag ) ;
			SWEAR( it->second<size() , it->second , size() ) ;
			try {
				Rsrc rsrc =
					rnd_up ? from_string_rsrc<Rsrc,true /*RndUp*/>(k,v)
					:        from_string_rsrc<Rsrc,false/*RndUp*/>(k,v)
				;
				self[it->second]  = rsrc ;
				non_null         |= rsrc ;
			} catch(...) {
				throw cat("cannot convert resource ",k," from ",v," to a int") ;
			}
		}
		throw_unless( non_null ,"cannot launch local job with no resources" ) ;
	}

	inline ::vmap_ss RsrcsData::mk_vmap(::vector_s const& keys) const {
		::vmap_ss res ; res.reserve(keys.size()) ;
		for( size_t i : iota(keys.size()) ) if (self[i]) res.emplace_back( keys[i] , to_string_rsrc(keys[i],self[i]) ) ;
		return res ;
	}

	inline RsrcsData RsrcsData::round(Backend const& be) const {
		LocalBackend const& lbe = dynamic_cast<LocalBackend const&>(be) ;
		RsrcsData const&    c   = lbe.capacity_                         ;
		//
		RsrcsData res ; res.reserve(size()) ;
		for( size_t i : iota(size()) ) {
			SWEAR( self[i]<=c[i] , lbe.rsrc_keys[i] , self[i] , c[i] ) ; // self must have been checked to fit within capacity
			res.push_back( ::min(round_rsrc(self[i]),c[i]) ) ;           // round up, but not above capacity or job will never be launched
		}
		return res ;
	}

}
