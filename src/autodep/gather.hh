// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "fd.hh"
#include "hash.hh"
#include "msg.hh"
#include "process.hh"
#include "re.hh"
#include "time.hh"
#include "trace.hh"

#include "rpc_job.hh"
#include "rpc_job_exec.hh"

#include "env.hh"

// When several sockets are opened to send depend & target data, we are not sure of the order between these reports because of system buffers.
// We could have decided to synchronize each report, which may be expensive in performance.
// We chose to lose some errors, i.e. Update's may be seen as Write's, as each ambiguity is resolved by considering the Write is earliest and read latest possible.
// This way, we do not generate spurious errors.
// To do so, we maintain, for each access entry (i.e. a file), a list of sockets that are unordered, i.e. for which a following Write could actually have been done before by the user.

enum class GatherKind : uint8_t { // epoll events
	Stdout
,	Stderr
,	ServerReply
,	ChildStart                    // just a marker, not actually used as epoll event
,	ChildEnd
,	ChildEndFd
,	JobMaster
,	JobSlave
,	ServerMaster
,	ServerSlave
} ;

enum class KillStep : uint8_t {
	None
,	Report
,	Kill   // must be last as following values are used
} ;

struct Gather {                                                       // NOLINT(clang-analyzer-optin.performance.Padding) prefer alphabetical order
	friend ::string& operator+=( ::string& , Gather const& ) ;
	using Kind = GatherKind    ;
	using Proc = JobExecProc   ;
	using Jerr = JobExecRpcReq ;
	using Crc  = Hash::Crc     ;
	using PD   = Time::Pdate   ;
	using DI   = DepInfo       ;
	static constexpr Time::Delay HeartbeatTick { 10 } ;               // heartbeat to probe server when waiting for it, there may be 1000's job_exec's waiting for it, 100s seems a good compromize
	struct AccessInfo {
		friend ::string& operator+=( ::string& , AccessInfo const& ) ;
		// cxtors & casts
		AccessInfo() = default ;
		//
		bool operator==(AccessInfo const&) const = default ;
		// accesses
		PD                       first_read (bool with_readdir=true) const ;
		PD                       first_write(                      ) const ;
		::pair<PD,bool/*write*/> sort_key   (                      ) const ;
		Accesses                 accesses   (                      ) const ;
		//
		void clear_accesses() { for( PD& d : _read ) d                   = PD::Future ; }
		void clear_lnk     () {                      _read[+Access::Lnk] = PD::Future ; }
		void clear_readdir () {                      _read_dir           = PD::Future ; }
		//                                                  phys
		bool seen    () const { return _seen    <_max_read (true) ; } // if true <=> file has been observed existing, we want real info because this is to trigger rerun
		bool read_dir() const { return _read_dir<_max_read (true) ; } // if true <=> file has been read as a dir    , we want real info because this is to generate error
		bool allow   () const { return _allow   <_max_write(    ) ; } // if true <=> file has been declared target
	private :
		PD _max_write(         ) const { return _write_ignore ; }     // max date for a write to be taken into account, always <Future
		PD _max_read (bool phys) const ;                              // max date for a read  to be taken into account, always <Future
		// services
	public :
		void update( PD , AccessDigest    , bool late , DI const&   ={} ) ;
		void update(      AccessDigest ad , bool late , DI const& di={} ) { update( {} , ad , late , di ) ; }
		void no_hot( PD                                            ) ;
		bool is_hot( Time::Delay prec                              ) const {
			// if file date is not comfortable enough, we make it hot and server will ensure job producing dep was done before this job started
			if (!dep_info.is_a<DepInfoKind::Info>()) return false ;                    // we have a crc, no risk to gather bad crc from sig
			PD fr = first_read() ;
			if (fr>=_no_hot) return false                                       ;      // file has been rebuilt and we are guarded against nfs, no risk
			/**/             return !dep_info.info().date.avail_at( fr , prec ) ;      // mark hot if dep is not old enough
			;
		}
		//
		void chk() const ;
		// data
		// seen detection : we record the earliest date at which file has been as existing to detect situations where file is non-existing, then existing, then non-existing
		// this cannot be seen on file date has there is no date for non-existing files
		MatchFlags flags        { .dflags={} } ;                                       // initially, no dflags, not even default ones (as they accumulate)
		bool       force_is_dep = false        ;                                       // if true => access must be a dep even if written to beforehand
		DI         dep_info     ;                                                      // state when first read
	private :
		::array<PD,N<Access>> _read         { mk_array<N<Access>>(PD::Future) } ;      // first access date for each access
		PD                    _read_dir     = PD::Future                        ;      // first date at which file has been read as a dir
		PD                    _write        = PD::Future                        ;      // first sure write
		PD                    _allow        = PD::Future                        ;      // first date at which file was known to be a target
		PD                    _required     = PD::Future                        ;      // first date at which file was required
		PD                    _seen         = PD::Future                        ;      // first date at which file has been seen existing
		PD                    _read_ignore  = PD::Future1                       ;      // first date at which reads  are ignored, always <Future
		PD                    _write_ignore = PD::Future1                       ;      // first date at which writes are ignored, always <Future
		PD                    _no_hot       = PD::Future                        ;      // first date at which dep is known sync'ed on disk
		bool                  _washed       = false                             ;
	} ;
	struct Digest {
		::vmap_s<TargetDigest> targets        ;
		::vmap_s<DepDigest   > deps           ;
		::vector<NodeIdx     > crcs           ;                                        // index in targets of entry for which we need to compute a crc
		::set_s                refresh_codecs ;
		::string               msg            ;
	} ;
	struct ServerSlaveEntry {
		friend ::string& operator+=( ::string& , ServerSlaveEntry const& ) ;
		// data
		IMsgBuf     buf = {} ;
		SockFd::Key key = {} ;
	} ;
	struct JobSlaveEntry {
		friend ::string& operator+=( ::string& , JobSlaveEntry const& ) ;
		// data
		Jerr                            jerr       = {} ;                              // used for DepDirect/DepVerbose until server reply
		::umap<Jerr::Id,::vector<Jerr>> to_confirm = {} ;                              // jerrs waiting for confirmation
		IMsgBuf                         buf        = {} ;
		SockFd::Key                     key        = {} ;
	} ;
	// statics
private :
	static void _s_ptrace_child( void* self_ , Fd report_fd , ::latch* ready ) { reinterpret_cast<Gather*>(self_)->_ptrace_child(report_fd,ready) ; }
	// services
	void _send_to_server( JobMngtRpcReq const&                  ) ;
	void _send_to_server( Fd , Jerr&& , JobSlaveEntry&/*inout*/ ) ;                    // files are required for DepVerbose and forbidden for other
	//
	void _new_accesses( Fd fd , Jerr&& jerr ) {
		for( auto& [f,fi] : jerr.files ) new_access( fd , jerr.date , ::move(f) , jerr.digest , fi , Yes/*late*/, jerr.comment , jerr.comment_exts ) ;
	}
	::pair_s<AccessInfo>& _access_info(::string&& file) {
		auto        [it,is_new] = access_map.emplace(file,accesses.size()) ;
		if (is_new) return accesses.emplace_back(::move(file),AccessInfo()) ;
		else        return accesses[it->second]                             ;
	}
public :
	// Fd for trace purpose only
	void new_access( Fd , PD    , ::string&& file , AccessDigest    , DI const&    , Bool3 late , Comment  =Comment::None , CommentExts    ={} ) ; //!        Fd                    late
	void new_access(      PD pd , ::string&& f    , AccessDigest ad , DI const& di , Bool3 l    , Comment c=Comment::None , CommentExts ces={} ) { new_access({},pd,::move(f),ad,di,l    ,c,ces) ; }
	void new_access(      PD pd , ::string&& f    , AccessDigest ad , DI const& di ,              Comment c=Comment::None , CommentExts ces={} ) { new_access({},pd,::move(f),ad,di,Maybe,c,ces) ; }
	//
	void new_exec  ( PD , ::string const& exe , Comment=Comment::StaticExec ) ;
	//
	void new_guard (::string const& f) { guards.insert(f) ; }
	//
	void sync( Fd fd , JobExecRpcReply const&  jerr ) {
		jerr.chk() ;
		try { OMsgBuf(jerr).send(fd,{}/*key*/) ; } catch (::string const&) {}          // dont care if we cannot report the reply to job
	}
	//
	Status exec_child() ;
	//
	void   reorder(bool at_end              ) ;                                        // reorder accesses by first read access and suppress superfluous accesses
	Digest analyze(Status status=Status::New) ;                                        // status==New means job is not done
private :
	Fd     _spawn_child (                               ) ;
	Status _exec_child  (                               ) ;
	void   _ptrace_child( Fd report_fd , ::latch* ready ) ;
	//
	void _exec_trace( PD pd , Comment c , CommentExts ces={} , ::string const& file={} ) const { if (exec_trace) exec_trace->emplace_back(pd,c,ces,file) ; }
	void _exec_trace(         Comment c , CommentExts ces={} , ::string const& file={} ) const { _exec_trace( New , c , ces , file ) ;                     }
	void _exec_trace( PD pd , Comment c ,                      ::string const& file    ) const { _exec_trace( pd  , c , {}  , file ) ;                     }
	void _exec_trace(         Comment c ,                      ::string const& file    ) const { _exec_trace( New , c , {}  , file ) ;                     }
	// data
public :
	::umap_s<NodeIdx   >                      access_map       ;
	::vmap_s<AccessInfo>                      accesses         ;
	in_addr_t                                 addr             = 0                   ; // local addr to which we can be contacted by running job
	bool                                      as_session       = false               ; // if true <=> process is launched in its own group
	::vector_s                                cmd_line         ;
	Fd                                        child_stdin      = Fd::Stdin           ;
	Fd                                        child_stderr     = Fd::Stderr          ;
	Fd                                        child_stdout     = Fd::Stdout          ;
	Time::Delay                               ddate_prec       ;
	uint8_t                                   nice             = 0                   ;
	AutodepEnv                                autodep_env      ;
	PD                                        end_date         ;
	::map_ss const*                           env              = nullptr             ;
	::vector<ExecTraceEntry>*                 exec_trace       = nullptr             ;
	pid_t                                     first_pid        = 0                   ;
	uset_s                                    guards           ;                       // dir creation/deletion that must be guarded against NFS
	JobIdx                                    job              = 0                   ;
	::vector<uint8_t>                         kill_sigs        ;                       // signals used to kill job
	bool                                      live_out         = false               ;
	::string                                  lmake_root_s     ;                       // contains error messages not from job
	AutodepMethod                             method           = AutodepMethod::Dflt ;
	::string                                  msg              ;                       // contains error messages not from job
	Time::Delay                               network_delay    = Time::Delay(1)      ; // 1s is reasonable when nothing is said
	bool                                      no_tmp           = false               ; // if true <=> no tmp access is allowed
	::vmap<Re::RegExpr,::pair<PD,MatchFlags>> pattern_flags    ;                       // apply flags to matching accesses
	pid_t                                     pid              = -1                  ; // pid to kill
	::string                                  rule             ;
	bool                                      seen_tmp         = false               ;
	SeqId                                     seq_id           = 0                   ;
	ServerSockFd                              server_master_fd ;
	SockFd::Service                           service_mngt     ;                       // no server if empty
	::vector<Re::RegExpr>                     star_targets     ;                       // excludes Target flag as it must be fully predictible to ensure a sound rule selection process
	PD                                        start_date       ;
	bool                                      started          = false               ;
	::uset_s                                  static_targets   ;                       // .
	::string                                  stderr           ;                       // contains child stderr if child_stderr==Pipe
	::string                                  stdout           ;                       // contains child stdout if child_stdout==Pipe
	Time::Delay                               timeout          ;
	Atomic<int>                               wstatus          = 0                   ;
private :
	::map_ss     _add_env              ;
	Child        _child                ;
	size_t       _n_server_req_pending = 0 ;
	NodeIdx      _parallel_id          = 0 ;                                           // id to identify parallel deps
	BitMap<Kind> _wait                 ;                                               // events we are waiting for
	::jthread    _ptrace_thread        ;
} ;
