// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "sys_config.h"

#if HAS_SLURM
#include <filesystem>
#include <charconv>
#include <sys/sysinfo.h>
#include <slurm/slurm.h>

#include "core.hh"
#include "hash.hh"

// // PER_BACKEND : there must be a file describing each backend
//
// // XXX : rework to maintain an ordered list of waiting_queues in ReqEntry to avoid walking through all rsrcs for each launched job

namespace Backends::Slurm {

	struct SlurmBackend ;
	struct RsrcsDataSingle {
		uint16_t cpu     = 1   ; // number of logical cpu (sbatch --cpus-per-task option)
		uint32_t mem     = 0   ; // memory in MB          (sbatch --mem           option)
		uint32_t tmp     = 0   ; // tmp disk in MB        (sbatch --tmp           option)
		string   part    = ""  ; // partition name        (sbatch --partition     option)
		string   gres    = ""  ; // generic resources     (sbatch --gres          option)
		string   licence = ""  ; // licence               (sbtach --licenses      option)
		string   feature = ""  ; // feature/contraint     (sbatch --constraint    option)
		bool operator==(RsrcsDataSingle const&) const = default;
	} ;

	struct RsrcsData : vector<RsrcsDataSingle> {
		RsrcsData(                ) = default ;
		RsrcsData(::vmap_ss const&);
		::vmap_ss vmap(void) const ;
	} ;
}

namespace std {
	template<> struct hash<Backends::Slurm::RsrcsData> {
		using Rsrc = Backends::Slurm::RsrcsDataSingle;
		uint64_t operator()(Backends::Slurm::RsrcsData const& r) const {
			Hash::Xxh h ;
			h.update(r.size());
			for(Rsrc v : r) {
				h.update(v.cpu    ) ;
				h.update(v.mem    ) ;
				h.update(v.tmp    ) ;
				h.update(v.part   ) ;
				h.update(v.gres   ) ;
				h.update(v.licence) ;
				h.update(v.feature) ;
			}
			return +(h.digest());
		}
	} ;
}

namespace Backends::Slurm {
	// share actual resources data as we typically have a lot of jobs with the same resources
	template< class Data , ::unsigned_integral Cnt > struct Shared {
		friend ostream& operator<<( ostream& os , Shared const& s ) { return os<<*s ; }
		// static data
	private :
		static ::umap<Data,Cnt> _s_store ;                                     // map rsrcs to count of pointers to it, always >0 (erased when reaching 0)
		// cxtors & casts
	public :
		Shared() = default ;
		//
		Shared(Shared const& r) : data{r.data} { if (data) _s_store.at(*data)++ ; }
		Shared(Shared      & r) : data{r.data} { if (data) _s_store.at(*data)++ ; }
		Shared(Shared     && r) : data{r.data} { r.data = nullptr               ; }
		Shared(::vmap_ss const& rsrcs) {
			Data d{rsrcs};
			auto it = _s_store.find(d) ;
			if (it==_s_store.end()) it = _s_store.insert({d,1}).first ;
			else                    it->second++ ;
			data = &it->first ;
		}
		~Shared() {
			if (!data) return ;
			auto it = _s_store.find(*data) ;
			SWEAR(it!=_s_store.end()) ;
			if (it->second==1) _s_store.erase(it) ;
			else               it->second--       ;
		}
		//
		Shared& operator=(Shared const& r) { this->~Shared() ; new(this) Shared(       r ) ; return *this ; }
		Shared& operator=(Shared     && r) { this->~Shared() ; new(this) Shared(::move(r)) ; return *this ; }
		//
		bool operator==(Shared const&) const = default ;
		// access
		Data const& operator* () const { return *data   ; }
		Data const* operator->() const { return &**this ; }
		// data
		Data const* data = nullptr ;
	} ;
	template< class Data , ::unsigned_integral Cnt > ::umap<Data,Cnt> Shared<Data,Cnt>::_s_store ;

	using Rsrcs     = Shared<RsrcsData,JobIdx> ;
}

namespace std {
	template<> struct hash<Backends::Slurm::Rsrcs > { size_t operator()(Backends::Slurm::Rsrcs  const& r ) const { return hash<Backends::Slurm::RsrcsData  const*>()(r .data) ; } } ;
}

namespace Backends::Slurm {

	constexpr Tag MyTag = Tag::Slurm ;

	static inline uint32_t s2u32(const string& s) {
		uint32_t r=0;
		//from_chars is supposed to be faster than stoi (x4.5)
		auto [ptr, ec] = ::from_chars(s.data(), s.data()+s.size(), r);
		swear(ec == std::errc(), to_string("Wrong string convertion to uint32_t: ", s));
		return r;
	}

	// we could maintain a list of reqs sorted by eta as we have open_req to create entries, close_req to erase them and new_req_eta to reorder them upon need
	// but this is too heavy to code and because there are few reqs and probably most of them have local jobs if there are local jobs at all, the perf gain would be marginal, if at all
	struct SlurmBackend : Backend {

		struct WaitingEntry {
			WaitingEntry() = default ;
			WaitingEntry( Rsrcs const& rs , SubmitAttrs const& sa ) : rsrcs{rs} , n_reqs{1} , submit_attrs{sa} {}
			// data
			Rsrcs       rsrcs        ;
			ReqIdx      n_reqs       = 0 ;                // number of reqs waiting for this job
			SubmitAttrs submit_attrs ;
		} ;

		struct SpawnedEntry {
			Rsrcs    rsrcs               ;
			uint32_t slurm_jobid = -1    ;
			ReqIdx   n_reqs      =  0    ;                // number of reqs waiting for this job to start
			bool     verbose     = false ;
			bool     started     = false ;                // if true <=> start() has been called for this job
		} ;

		struct PressureEntry {
			// services
			bool              operator== (PressureEntry const&      ) const = default ;
			::strong_ordering operator<=>(PressureEntry const& other) const {
				if (pressure!=other.pressure) return other.pressure<=>pressure  ; // higher pressure first
				else                          return job           <=>other.job ;
			}
			// data
			CoarseDelay pressure ;
			JobIdx      job      ;
		} ;

		struct ReqEntry {
			ReqEntry(         ) = default ;
			ReqEntry(JobIdx nj, bool verbose=false) : n_jobs{nj}, verbose{verbose} {}
			// service
			void clear() {
				waiting_queues.clear() ;
				waiting_jobs  .clear() ;
				spawned_jobs  .clear() ;
			}
			// data
			::umap<Rsrcs,::set<PressureEntry>> waiting_queues ;
			::umap<JobIdx,CoarseDelay        > waiting_jobs   ;
			::uset<JobIdx                    > spawned_jobs   ;
			const JobIdx                       n_jobs         = 0 ; // option -j, 0 if unlimited
			const bool                         verbose        = false ;
		} ;

		struct SlurmPressure {
			uint32_t n_submit_jobs  = 1; //submitted but not spawned
			uint32_t n_waiting_jobs = 0; //spawned (waiting in slurm queue)
		} ;

		// init
		static void s_init() {
			static bool once=false ; if (once) return ; else once = true ;
			SlurmBackend& self = *new SlurmBackend ;
			s_register(MyTag,self) ;
		}

		// services
		virtual void config(Config::Backend const& config) {
			for( auto const& [k,v] : config.dct ) {
				if(k=="n_max_queue_jobs") {
					auto [ptr, ec] = ::from_chars(v.data(), v.data()+v.size(), n_max_queue_jobs);
					if (ec != std::errc())   throw to_string("Wrong configuration setting for n_max_queue_jobs: ", v);
					if (n_max_queue_jobs==0) throw to_string("n_max_queue_jobs must be > 0");
				}
			}
		}
		virtual ::vmap_ss mk_lcl( ::vmap_ss&& rsrcs , ::vmap_s<size_t> const& capacity ) const {
			bool             single  = false;
			::umap_s<size_t> capa    = mk_umap(capacity);
			::umap_s<uint32_t> rs;
			for(auto [k,v] : rsrcs) {
				if     (capa.contains(k))                     rs[k]  = s2u32(v);
				else if(k=="gres" && !v.starts_with("shard")) single = true;
			}

			::vmap_ss lclRsrc;
			for(auto [k,v] : rs) {
				uint32_t capaVal = static_cast<uint32_t>(capa[k]);
				lclRsrc.emplace_back(k, to_string(single ? capaVal : ::min(v,capaVal)));
			}
			return lclRsrc;
		}
		virtual void open_req( ReqIdx req , JobIdx n_jobs ) {
 			SWEAR(!req_map.contains(req)) ;
			bool verbose = Req(req)->options.flags[ReqFlag::VerboseBackend];
 			req_map.insert({req,{n_jobs,verbose}}) ;
		}
		virtual void close_req(ReqIdx req) {
			if(req_map.size()==1) {
				SWEAR(slurm_map  .empty());
				SWEAR(waiting_map.empty());
				SWEAR(spawned_map.empty());
			}
			SWEAR(req_map.at(req).waiting_jobs.empty()) ;
			SWEAR(req_map.at(req).spawned_jobs.empty()) ;
			req_map.erase(req) ;
		}
	private :
		uint32_t n_max_queue_jobs = -1; //no limit by default

	public :
		::umap<ReqIdx,ReqEntry     > req_map    ;
		::umap<JobIdx,WaitingEntry > waiting_map;
		::umap<JobIdx,SpawnedEntry > spawned_map;
		::umap<Rsrcs ,SlurmPressure> slurm_map  ;

		// do not launch immediately to have a better view of which job should be launched first
		virtual void submit( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs , ::vmap_ss&& rsrcs ) {
			Rsrcs rs {rsrcs};  // compile rsrcs
 			ReqEntry& entry = req_map.at(req) ;
			SWEAR(!waiting_map       .contains(job)) ;                         // job must be a new one
			SWEAR(!entry.waiting_jobs.contains(job)) ;                         // in particular for this req
			CoarseDelay pressure = submit_attrs.pressure;
			waiting_map.emplace( job , WaitingEntry(rs,submit_attrs) ) ;
			if(slurm_map.contains(rs)) {
				slurm_map[rs].n_submit_jobs++;
			} else {
				slurm_map[rs] = SlurmPressure();
			}
			entry.waiting_jobs[job] = pressure             ;
			entry.waiting_queues[rs].insert({pressure,job}) ;
		}
		virtual void add_pressure( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs ) {
			ReqEntry    & entry = req_map.at(req)       ;
			auto          it    = waiting_map.find(job) ;
			if (it==waiting_map.end()) return ;                                // job is not waiting anymore, ignore
			WaitingEntry& we = it->second ;
			SWEAR(!entry.waiting_jobs.contains(job)) ;                         // job must be new for this req
			CoarseDelay pressure = submit_attrs.pressure;
			entry.waiting_jobs[job] = pressure ;
			entry.waiting_queues[we.rsrcs].insert({pressure,job}) ;            // job must be known
			we.submit_attrs |= submit_attrs ;
			we.n_reqs++ ;
		}
		virtual void set_pressure( JobIdx job , ReqIdx req , SubmitAttrs const& submit_attrs ) {
			ReqEntry& entry = req_map.at(req)       ;                                // req must be known to already know job
			auto      it    = waiting_map.find(job) ;
			if (it==waiting_map.end()) return ;                                      // job is not waiting anymore, ignore
			WaitingEntry        & we           = it->second                        ;
			CoarseDelay         & old_pressure = entry.waiting_jobs  .at(job     ) ; // job must be known
			::set<PressureEntry>& q            = entry.waiting_queues.at(we.rsrcs) ; // including for this req
			CoarseDelay           pressure     = submit_attrs.pressure;
			we.submit_attrs |= submit_attrs ;
			q.erase ({old_pressure,job}) ;
			q.insert({pressure    ,job}) ;
			old_pressure = pressure ;
		}
		virtual ::pair_s<uset<ReqIdx>> start(JobIdx job) {
			::uset<ReqIdx> res ;
			auto           it  = spawned_map.find(job) ;
			if (it==spawned_map.end()) return {} ;                             // job was killed in the mean time
			SpawnedEntry&  entry = it->second ;
			entry.started = true ;
			entry.n_reqs  = 0    ;
			auto sm = slurm_map.find(entry.rsrcs);
			SWEAR(sm != slurm_map.end());
			SWEAR(sm->second.n_waiting_jobs>0);
			sm->second.n_waiting_jobs--;
			if(sm->second.n_submit_jobs==0 && sm->second.n_waiting_jobs==0) {
				//We could erase the entry even if n_waiting_jobs>0 but it avoid manipulation
				slurm_map.erase(entry.rsrcs);
			}
			for( auto& [r,re] : req_map )
				if (re.spawned_jobs.erase(job)) res.insert(r) ;
			return {to_string("jobid:",entry.slurm_jobid),res} ;
		}
		virtual ::string end(JobIdx job, Status s) {
			auto it = spawned_map.find(job) ;
			if (it==spawned_map.end()) return {} ;                             // job was killed in the mean time
			::string msg = {};
			if (s==Status::Err && it->second.verbose) {
				sleep(1);                                                      // Let slurm flush its stderr
				msg = readStderrLog(job);
			}
			spawned_map.erase(it) ;
			return msg;
		}
		inline ::string readStderrLog(JobIdx job) const {
			::string          errLog = getLogStderrPath(job);
			::string          msg    = to_string("Error from: ", errLog, "\n");
			std::ifstream     ferr(errLog);
			std::stringstream buffer;
			buffer << ferr.rdbuf();
			msg    += buffer.str();
			return msg;
		}
		virtual ::vmap<JobIdx,pair_s<bool/*err*/>> heartbeat() {
			// as soon as jobs are started, top-backend handles heart beat
			::vmap<JobIdx,pair_s<bool/*err*/>> res ;
			for( auto& [job,entry] : spawned_map ) {
				job_states js = slurm_job_state(entry.slurm_jobid);
				if(
					js != JOB_PENDING
				&&	js != JOB_RUNNING
				&&	js != JOB_SUSPENDED
				&&	js != JOB_COMPLETE
				) {
					bool isErr   = js==JOB_FAILED || js==JOB_OOM;
					::string msg = isErr && entry.verbose ? readStderrLog(job) : ::string({});
					Trace trace("heartbeat job: ",job, " slurm jobid: ",entry.slurm_jobid," state is: ", js);
					res.emplace_back(job,pair(msg,isErr)) ;
				}
			}
			for(auto& [job,entry] : res) spawned_map.erase(job);
			return res ;
		}
		// kill all if req==0
		virtual ::uset<JobIdx> kill_req(ReqIdx req=0) {
			::uset<JobIdx> res ;
			auto           it  = req_map.find(req) ;
			if ( req && it==req_map.end() ) return {} ;
			Trace trace("kill_req",MyTag,req) ;
			if ( !req || req_map.size()==1 ) {       // fast path if req_map.size()==1
				for( auto const& [j,we] : waiting_map ) res.insert(j) ;
				for( auto const& [j,se] : spawned_map ) {
					if (!se.started) {               // if started, not our responsibility to kill it
						slurm_cancel(se.slurm_jobid);
						res.insert(j) ;
					}
				}
				for( auto& [r,re] : req_map ) re.clear() ;
				waiting_map.clear() ;
				spawned_map.clear() ;
				slurm_map  .clear() ;
			} else {
				ReqEntry& req_entry = it->second ;
				for( auto const& [j,je] : req_entry.waiting_jobs ) {
					WaitingEntry& we = waiting_map.at(j) ;
					if (we.n_reqs==1) { waiting_map.erase(j) ; res.insert(j) ; }
					else              { we.n_reqs--          ;                 }
				}
				for( JobIdx j : req_entry.spawned_jobs ) {
					SpawnedEntry& se = spawned_map.at(j) ;
					SWEAR(!se.started) ;       // when job starts, it is not in ReqMap->spawned_jobs any more
					if (--se.n_reqs) continue ;// do not cancel jobs needed for other request
					res.insert(j) ;
					slurm_cancel(se.slurm_jobid);
					auto sm = slurm_map.find(se.rsrcs);
					sm->second.n_waiting_jobs--;
					if(sm->second.n_submit_jobs==0) slurm_map.erase(se.rsrcs);
					spawned_map.erase(j) ;
				}
				req_entry.clear() ;
			}
			return res ;
		}
		void launch() {
			::vmap<JobIdx,pair_s<vmap_ss/*rsrcs*/>> job_error;
			for( Req req : Req::s_reqs_by_eta() ) {
				auto req_it = req_map.find(+req) ;
				if (req_it==req_map.end()) continue ;
				JobIdx n_jobs = req_it->second.n_jobs ;
				::umap<Rsrcs,set<PressureEntry>>& queues = req_it->second.waiting_queues ;
				for(;;) {
					if ( n_jobs && spawned_map.size()>=n_jobs ) break ;        // cannot have more than n_jobs running jobs because of this req
					auto candidate = queues.end() ;
 					for( auto it=queues.begin() ; it!=queues.end() ; it++ ) {
						SWEAR(!it->second.empty()) ;
						if ( candidate!=queues.end() && it->second.begin()->pressure<=candidate->second.begin()->pressure ) continue ;
						auto sp = slurm_map.find(it->first);
						SWEAR(sp != slurm_map.end());
						if ( sp->second.n_waiting_jobs >= n_max_queue_jobs                                                ) continue ;
						candidate = it ;                                       // found a candidate, continue to see if we can find one with a higher pressure
					}
					if (candidate==queues.end()) break ;                       // nothing for this req, process next req
					//
					uint32_t              slurm_jobid                            ;
					::set<PressureEntry>& pressure_set   = candidate->second     ;
					auto                  pressure_first = pressure_set.begin()  ;
					JobIdx                job            = pressure_first->job   ;
					auto                  wit            = waiting_map.find(job) ;
					Rsrcs  const&         rsrcs          = candidate->first      ;
					auto                  rmap           = rsrcs->vmap()         ;
					::vector_s            cmd_line       = acquire_cmd_line( MyTag, job, ::move(rmap), wit->second.submit_attrs ) ;
					//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					if(auto ret_s = slurm_spawn_job(job, cmd_line, rsrcs, req_it->second.verbose, &slurm_jobid)) [[unlikely]] {
						job_error.push_back({job,{ret_s.value(),rmap}});
					} else [[likely]] {
						spawned_map[job] = {rsrcs, slurm_jobid, wit->second.n_reqs, req_it->second.verbose} ;
						waiting_map.erase(wit) ;

						auto sm = slurm_map.find(rsrcs);
						SWEAR(sm != slurm_map.end());
						sm->second.n_waiting_jobs++;
						sm->second.n_submit_jobs--;
					}
					//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					for( auto& [r,re] : req_map ) {
						if (r!=+req) {
							auto wit1 = re.waiting_jobs.find(job) ;
							if (wit1==re.waiting_jobs.end()) continue ;
							auto wit2 = re.waiting_queues.find(rsrcs) ;
							::set<PressureEntry>& pes = wit2->second ;
							PressureEntry         pe  { wit1->second , job } ; // /!\ pressure is job's pressure for r, not for req
							SWEAR(pes.contains(pe)) ;
							if (pes.size()==1) re.waiting_queues.erase(wit2) ; // last entry for this rsrcs, erase the entire queue
							else               pes              .erase(pe  ) ;
						}
						re.waiting_jobs.erase (job) ;
						re.spawned_jobs.insert(job) ;
					}
					if (pressure_set.size()==1) queues      .erase(candidate     ) ; // last entry for this rsrcs, erase the entire queue
					else                        pressure_set.erase(pressure_first) ;
				}
			}
			if(job_error.size() > 0) throw job_error;
		}

	private :
		static void slurm_cancel(uint32_t jobid) {
			int err;
			//This for loop with a retry comes from the scancel Slurm utility code
			//Normally we kill mainly waiting jobs, but some "just started jobs" could be killed like that also
			//Running jobs are killed by lmake/job_exec
			for(int i=0;i<10/*MAX_CANCEL_RETRY*/;i++){
				err = slurm_kill_job(jobid, SIGKILL, KILL_FULL_JOB);
				if (err == SLURM_SUCCESS || errno != ESLURM_TRANSITION_STATE_NO_UPDATE) {
					break;
				}
				sleep(5 + i); // Retry
			}
			if(err == SLURM_SUCCESS || errno == ESLURM_ALREADY_DONE) {
				Trace trace("Cancel slurm jodid: ",jobid) ;
				return;
			}
			Trace trace("Error while killing job: ", jobid, "error: ", slurm_strerror(errno));
		}
		static job_states slurm_job_state(uint32_t jobid) {
			//possible job_states values are (see slurm.h) :
			//	JOB_PENDING    : queued waiting for initiation
			//	JOB_RUNNING    : allocated resources and executing
			//	JOB_SUSPENDED  : allocated resources, execution suspended
			//	JOB_COMPLETE   : completed execution successfully
			//	JOB_CANCELLED  : cancelled by user
			//	JOB_FAILED     : completed execution unsuccessfully
			//	JOB_TIMEOUT    : terminated on reaching time limit
			//	JOB_NODE_FAIL  : terminated on node failure
			//	JOB_PREEMPTED  : terminated due to preemption
			//	JOB_BOOT_FAIL  : terminated due to node boot failure
			//	JOB_DEADLINE   : terminated on deadline
			//	JOB_OOM        : experienced out of memory error
			//	JOB_END        : not a real state, last entry in table
			job_info_msg_t *resp;
			if(slurm_load_job(&resp, jobid, SHOW_LOCAL) == SLURM_SUCCESS) {
				job_states js = static_cast<job_states>(resp->job_array[0].job_state & JOB_STATE_BASE);
				for(uint32_t i=1;i<resp->record_count;i++) {
					slurm_job_info_t * ji = &resp->job_array[i];
					SWEAR(js==(ji->job_state & JOB_STATE_BASE));
				}
				slurm_free_job_info_msg( resp );
				swear(js<JOB_END, to_string("Slurm: wrong job state return for job (", jobid, "): ", js));
				return js;
			} else {
				swear(0,to_string("Error while loading job info (", jobid, "): ", slurm_strerror(errno)));
				return JOB_RUNNING;
			}
		}
		inline string cmd_to_string(::vector_s& cmd_line) const {
			std::stringstream ss;
			ss << "#!/bin/sh\n";
			for(string s : cmd_line) {
				ss << s  ;
				ss << " ";
			}
			return ss.str();
		}
		inline ::string getLogPath      (JobIdx job) const {return to_string(AdminDir, "/slurm_log/"s, job);}
		inline ::string getLogStderrPath(JobIdx job) const {return to_string(getLogPath(job), "/stderr"   );}
		inline ::string getLogStdoutPath(JobIdx job) const {return to_string(getLogPath(job), "/stdout"   );}

		inline ::optional<::string> slurm_spawn_job(JobIdx job, ::vector_s& cmd_line, Rsrcs  const& rsrcs, bool verbose, uint32_t * slurmJobId) {
			uint32_t  n_comp   = (*rsrcs).size(); SWEAR(n_comp>0) ;
			char     *env[1]   = {const_cast<char *>("")};
			string    wd       = *g_root_dir;
			auto      job_name = *(--::filesystem::path(wd).end()) / Job(job).full_name(); // ="repoDir/target"
			string    script   = cmd_to_string(cmd_line);
			string    s_errPath;
			string    s_outPath;
			if(verbose) {
				s_errPath      = getLogStderrPath(job);
				s_outPath      = getLogStdoutPath(job);
				Disk::make_dir(getLogPath(job));
			}
			::vector<job_desc_msg_t> jDesc(n_comp);
			for(uint32_t i=0; RsrcsDataSingle r: *rsrcs) {
				job_desc_msg_t* j = &jDesc[i];
				slurm_init_job_desc_msg(j);
				j->env_size         = 1;
				j->environment      = env;
				j->cpus_per_task    = r.cpu  ;
				j->pn_min_memory    = r.mem  ;    //in MB
				j->pn_min_tmp_disk  = r.tmp  ;    //in MB
				j->std_err          = const_cast<char *>(verbose ? s_errPath.data() : "/dev/null");
				j->std_out          = const_cast<char *>(verbose ? s_outPath.data() : "/dev/null");
				j->work_dir         = const_cast<char *>(wd       .data());
				j->name             = const_cast<char *>(job_name .c_str());
				if(!r.feature.empty()) j->features      = const_cast<char *>(r.feature.data());
				if(!r.licence.empty()) j->licenses      = const_cast<char *>(r.licence.data());
				if(!r.part   .empty()) j->partition     = const_cast<char *>(r.part   .data());
				if(!r.gres   .empty()) j->tres_per_node = const_cast<char *>(r.gres   .data());
				if(i==0)               j->script        = const_cast<char *>(script   .data());
				i++;
			}
			int ret;
			submit_response_msg_t *jMsg;
			if(n_comp==1) {
				ret = slurm_submit_batch_job(&jDesc[0], &jMsg);
			} else {
				List jList = slurm_list_create(NULL);
				for(uint32_t i=0;i<n_comp;i++) slurm_list_append(jList, &jDesc[i]);
				ret = slurm_submit_batch_het_job(jList,&jMsg);
				slurm_list_destroy(jList);
			}
			if(ret == SLURM_SUCCESS) {
				*slurmJobId = jMsg->job_id;
				slurm_free_submit_response_response_msg(jMsg);
				return {};
			} else {
				string err = "Launch slurm job error: " + string(slurm_strerror(slurm_get_errno()));
				return err;
			}
		}
	} ;

	bool _inited = (SlurmBackend::s_init(),true) ;

	template<class T>
	static inline T& grow(vector<T>& v, uint32_t idx) {
		if(idx>=v.size()) v.resize(idx+1);
		return v[idx];
	}
	static inline void rsrcThrow(const string& k) {throw to_string("no resource ", k," for backend ",mk_snake(MyTag));}
	static uint32_t mtoi(const string& mem) {
		char c = mem.back();
		switch (c) {
			case 'M': return s2u32(mem.substr(0,mem.size()-1))       ;
			case 'G': return s2u32(mem.substr(0,mem.size()-1)) * 1024;
			default : return s2u32(mem);
		}
		return 0;
	}
	inline RsrcsData::RsrcsData(::vmap_ss const& m) {
		for( auto const& [k,v] : m ) {
			switch (k[0]) {
				case 'p' : if (k.starts_with("part"   )) grow(*this,::atoi(&k[4])).part    =         v  ; else rsrcThrow(k); break ;
				case 't' : if (k.starts_with("tmp"    )) grow(*this,::atoi(&k[3])).tmp     =    mtoi(v) ; else rsrcThrow(k); break ;
				case 'g' : if (k.starts_with("gres"   )) grow(*this,::atoi(&k[4])).gres    = "gres:"+v  ; else rsrcThrow(k); break ;
				case 'f' : if (k.starts_with("feature")) grow(*this,::atoi(&k[7])).feature =         v  ; else rsrcThrow(k); break ;
				case 'l' : if (k.starts_with("licence")) grow(*this,::atoi(&k[7])).licence =         v  ; else rsrcThrow(k); break ;
				case 'c' : if (k.starts_with("cpu"    )) grow(*this,::atoi(&k[3])).cpu     =   s2u32(v) ; else rsrcThrow(k); break ;
				case 'm' : if (k.starts_with("mem"    )) grow(*this,::atoi(&k[3])).mem     =    mtoi(v) ; else rsrcThrow(k); break ;
				default  : rsrcThrow(k);
			}
		}
	}
	::vmap_ss RsrcsData::vmap(void) const {
		::vmap_ss res ;
		// It may be interesting to know the number of cpu reserved to know how many thread to launch in some situation
		res.emplace_back("cpu", to_string((*this)[0].cpu));
		res.emplace_back("mem", to_string((*this)[0].mem));
		res.emplace_back("tmp", to_string((*this)[0].tmp));
		return res ;
	}
}

#endif