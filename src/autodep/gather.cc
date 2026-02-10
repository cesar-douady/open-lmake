// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "app.hh"
#include "thread.hh"

#include "ptrace.hh"
#include "record.hh"

#include "gather.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Re   ;
using namespace Time ;

//
// Gather::AccessInfo
//

::string& operator+=( ::string& os , Gather::AccessInfo const& ai ) { // START_OF_NO_COV
	Pdate fr = ai.first_read() ;
	/**/                          os << "AccessInfo("           ;
	if (fr       !=Pdate::Future) os << "R:" <<fr         <<',' ;
	if (ai._allow!=Pdate::Future) os << "A:" <<ai._allow  <<',' ;
	if (ai._write!=Pdate::Future) os << "W:" <<ai._write  <<',' ;
	if (+ai.dep_info            ) os << ai.dep_info       <<',' ;
	/**/                          os << ai.flags                ;
	if (ai._seen!=Pdate::Future ) os <<",seen"                  ;
	return                        os <<')'                      ;
}                                                                     // END_OF_NO_COV

Pdate Gather::AccessInfo::_max_read(bool phys) const {
	if (_washed) {
		if (phys                       ) return {} ;                                  // washing has a physical impact
		if (flags.tflags[Tflag::Target]) return {} ;                                  // if a target, washing is a logical write
	}
	PD                                         res = ::min( _read_ignore , _write ) ;
	if ( !phys && !flags.dep_and_target_ok() ) res = ::min( res          , _allow ) ; // logically, once file is a target, reads are ignored, unless it is also a dep
	return res ;
}

Accesses Gather::AccessInfo::accesses() const {
	PD       ma  = _max_read(false/*phys*/) ;
	Accesses res ;
	for( Access a : iota(All<Access>) ) if (_read[+a]<=ma) res |= a ;
	return res ;
}

Pdate Gather::AccessInfo::first_read(bool with_readdir) const {
	PD    res = PD::Future               ;
	Pdate mr  = _max_read(false/*phys*/) ;
	//
	for( PD d : _read ) if ( d        <res                 ) res = d         ;
	/**/                if ( _read_dir<res && with_readdir ) res = _read_dir ;
	/**/                if ( _required<res                 ) res = _required ;
	//
	if (res<=mr) return res           ;
	else         return Pdate::Future ;
}

Pdate Gather::AccessInfo::first_write() const {
	if ( _washed && flags.tflags[Tflag::Target] ) return {}         ;
	if ( _write<=_max_write()                   ) return _write     ;
	else                                          return PD::Future ;
}

::pair<Pdate,bool/*write*/> Gather::AccessInfo::sort_key() const {
	PD fr = first_read() ;
	if (fr<PD::Future) return { fr            , false } ;
	else               return { first_write() , true  } ;
}

void Gather::AccessInfo::update( PD pd , AccessDigest ad , bool late , DI const& di ) {
	SWEAR(ad.write!=Maybe) ;                                                                                                        // this must have been solved by caller
	if ( ad.flags.extra_tflags[ExtraTflag::Ignore] ) ad.flags.extra_dflags |= ExtraDflag::Ignore ;                                  // ignore target implies ignore dep
	if ( ad.write==Yes && late                     ) ad.flags.extra_tflags |= ExtraTflag::Late   ;
	flags        |= ad.flags        ;
	force_is_dep |= ad.force_is_dep ;
	//
	if ( +di && ::all_of( _read , [&](PD d) { return pd<d ; } ) ) dep_info = di ;
	//
	for( Access a : iota(All<Access>) )   if ( pd<_read[+a] && ad.accesses[a]                           ) _read[+a] = pd   ;
	/**/                                  if ( pd<_read_dir && ad.read_dir                              ) _read_dir = pd   ;
	if (late)                           { if ( pd<_write    && ad.write==Yes                            ) _write    = pd   ; }
	else                                { if (                 ad.write==Yes                            ) _washed   = true ; }
	/**/                                  if ( pd<_allow    && ad.flags.extra_tflags[ExtraTflag::Allow] ) _allow    = pd   ;
	/**/                                  if ( pd<_required && ad.flags.dflags[Dflag::Required]         ) _required = pd   ;
	/**/                                  if ( pd<_seen     && di.seen(ad.accesses)                     ) _seen     = pd   ;
	/**/                                  if ( pd<_no_hot   && ad.flags.extra_dflags[ExtraDflag::NoHot] ) _no_hot   = pd   ;
	if (+pd) pd-- ;                                                                                                                 // ignore applies to simultaneous accesses
	/**/                                  if ( pd<_read_ignore  && ad.flags.extra_dflags[ExtraDflag::Ignore] ) _read_ignore  = pd ;
	/**/                                  if ( pd<_write_ignore && ad.flags.extra_tflags[ExtraTflag::Ignore] ) _write_ignore = pd ;
}

//
// Gather
//

::string& operator+=( ::string& os , Gather::ServerSlaveEntry const& sse ) { // START_OF_NO_COV
	First first ;
	/**/          os << "ServerSlaveEntry("           ;
	if (+sse.buf) os <<first("",",")<< sse.buf.size() ;
	if (+sse.key) os <<first("",",")<< sse.key        ;
	return        os <<')'                            ;
}                                                                            // END_OF_NO_COV

::string& operator+=( ::string& os , Gather::JobSlaveEntry const& jse ) { // START_OF_NO_COV
	First first ;
	/**/          os << "JobSlaveEntry("              ;
	if (+jse.buf) os <<first("",",")<< jse.buf.size() ;
	if (+jse.key) os <<first("",",")<< jse.key        ;
	return        os <<')'                            ;
}                                                                         // END_OF_NO_COV

::string& operator+=( ::string& os , Gather const& gd ) { // START_OF_NO_COV
	return os << "Gather("<<gd.accesses<<')' ;
}                                                         // END_OF_NO_COV

void Gather::new_access( Fd fd , PD pd , ::string&& file , AccessDigest ad , DI const& di , Bool3 late , Comment c , CommentExts ces ) {
	SWEAR( +file , c , ces        ) ;
	SWEAR( +pd   , c , ces , file ) ;
	if (late==Maybe) SWEAR( ad.write==No ) ;                                                                          // when writing, we must know if job is started
	size_t                old_sz   = accesses.size()  ;
	::pair_s<AccessInfo>& file_info = _access_info(::move(file)) ;
	bool                  is_new   = accesses.size() > old_sz    ;
	::string       const& f        = file_info.first             ;
	AccessInfo&           info     = file_info.second            ;
	AccessInfo            old_info = info                        ;                                                    // for tracing only
	if (ad.write==Maybe) {
		// wait until file state can be safely inspected as in case of interrupted write, syscall may continue past end of process
		// this may be long, but is exceptionnal
		(pd+network_delay).sleep_until() ;
		if (info.dep_info.is_a<DepInfoKind::Crc>()) ad.write = No | (Crc    (f)!=info.dep_info.crc()) ;
		else                                        ad.write = No | (FileSig(f)!=info.dep_info.sig()) ;
	}
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	info.update( pd , ad , late==Yes , di ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	if ( is_new || info!=old_info ) {
		if (+c) _user_trace( pd , c , ces , f ) ;
		Trace("new_access", fd , STR(is_new) , pd , ad , di , _parallel_id , c , ces , old_info , "->" , info , f ) ; // only trace if something changes
	}
}

void Gather::new_exec( PD pd , ::string const& exe , Comment c ) {
	RealPath              rp { autodep_env }                    ;
	RealPath::SolveReport sr = rp.solve(exe,false/*no_follow*/) ;
	for( auto&& [f,a] : rp.exec(::move(sr)) )
		if (!Record::s_is_simple(f)) new_access( pd , ::move(f) , {.accesses=a} , FileInfo(f) , c ) ;
}

Gather::Digest Gather::analyze(Status status) {
	Trace trace("analyze",status,accesses.size()) ;
	Digest  res                   ;                 res.deps.reserve(accesses.size()) ;                                 // typically most of accesses are deps
	Pdate   prev_first_read       = Pdate::Future ;
	bool    readdir_warned        = false         ;
	bool    seen_unexpected_write = false         ;
	//
	reorder(status!=Status::New) ;
	for( auto& [file,info] : accesses ) {
		static constexpr MatchFlags TargetFlags { .tflags=Tflag::Target , .extra_tflags=ExtraTflag::Allow } ;
		//                                                                                                     started
		if (static_targets.contains(file))                                  info.update( {.flags=TargetFlags} , false ) ;
		else for( RegExpr const& re : star_targets ) if (+re.match(file)) { info.update( {.flags=TargetFlags} , false ) ; break ; }
		//
		MatchFlags flags = info.flags ;
		//
		// handle read_dir
		if ( info.read_dir() && !(flags.extra_dflags[ExtraDflag::ReaddirOk]||flags.tflags[Tflag::Incremental]) ) {      // if incremental, user handle previous values
			res.msg << "readdir without readdir_ok : "<<mk_file(file,No)<<'\n' ;
			if (!readdir_warned) {
				res.msg << "  consider (ordered by decreasing reliability) :\n"                                       ;
				res.msg << "  - if files non-generated by this job exist in this dir, avoid reading it if possible\n" ; // XXX? : improve by detecting whether condition is met (is it worth?)
				res.msg << "  - if this is due to python imports, call : lmake.report_import()\n"                     ;
				res.msg << "  - set  : "<<rule<<".side_deps = {'READ_DIR':("<<mk_py_str(file)<<",'readdir_ok')}\n"    ;
				res.msg << "  - call : lmake.depend("<<mk_file(file,FileDisplay::Py   )<<",readdir_ok=True)\n"        ;
				res.msg << "    or   : ldepend -D "  <<mk_file(file,FileDisplay::Shell)<<'\n'                         ;
				res.msg << "  - set  : "<<rule<<".readdir_ok = True\n"                                                ;
				readdir_warned = true ;
			}
		}
		// handle codec
		if (flags.extra_dflags[ExtraDflag::CreateEncode]) {
			trace("codec  ",file) ;
			_user_trace( info.first_read(false/*with_readdir*/) , Comment::CreateCodec , file ) ;
			if (is_lcl(file)) res.refresh_codecs.insert(Codec::CodecFile(New,file).file) ;
		}
		//
		Accesses accesses     = info.accesses()                  ;
		bool     was_written  = info.first_write()<Pdate::Future ;
		bool     force_is_dep = info.force_is_dep                ;
		//
		if (file==".") continue ;                                                                             // . is only reported when reading dir but otherwise is an external file
		//
		Pdate first_read = info.first_read(false/*with_readdir*/)                                               ;
		bool  was_read   = first_read<Pdate::Future                                                             ;
		bool  is_dep     = force_is_dep || +accesses || (was_read&&!was_written) || flags.dflags[Dflag::Static] ;
		bool  allow      = info.allow()                                                                         ;
		bool  is_tgt     = was_written || allow                                                                 ;
		//
		if ( !is_dep && !is_tgt ) {
			trace("ignore ",file) ;
			continue ;
		}
		// handle deps
		if (is_dep) {
			DepDigest dd       { accesses , info.dep_info , false/*err*/ , flags.dflags } ;
			bool      unstable = false                                                    ;
			//
			// if file is not old enough, we make it hot and server will ensure job producing dep was done before this job started
			dd.hot           = info.is_hot(ddate_prec)                                 ;
			dd.parallel      = first_read<Pdate::Future && first_read==prev_first_read ;
			dd.create_encode = flags.extra_dflags[ExtraDflag::CreateEncode]            ;
			prev_first_read  = first_read                                              ;
			// try to transform date into crc as far as possible
			if      ( dd.is_crc                         )   {}                                                // already a crc => nothing to do
			else if ( !accesses                         )   {}                                                // no access     => nothing to do
			else if ( !info.seen()                      ) { dd.may_set_crc(Crc::None ) ; dd.hot   = false ; } // job has been executed without seeing the file (before possibly writing to it)
			else if ( !dd.sig()                         ) { dd.del_crc    (          ) ; unstable = true  ; } // file was absent initially but was seen, it is incoherent even if absent finally
			else if ( was_written                       )   {}                                                // cannot check stability, clash will be detected in server if any
			else if ( FileSig sig{file} ; sig!=dd.sig() ) { dd.del_crc    (          ) ; unstable = true  ; } // file dates are incoherent from first access to end of job, no stable content
			else if ( sig.tag()==FileTag::Empty         )   dd.may_set_crc(Crc::Empty) ;                      // crc is easy to compute (empty file), record it
			else if ( !Crc::s_sense(accesses,sig.tag()) )   dd.may_set_crc(sig.tag() ) ;                      // just record the tag if enough to match (e.g. accesses==Lnk and tag==Reg)
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.deps.emplace_back( file , dd ) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			if (status!=Status::New) {                                       // only trace for user at end of job as intermediate analyses are of marginal interest for user
				if      (unstable) _user_trace( Comment::Unstable , file ) ;
				else if (dd.hot  ) _user_trace( Comment::Hot      , file ) ;
			}
			if (dd.hot) trace("dep_hot",dd,info.dep_info,first_read,ddate_prec,file) ;
			else        trace("dep    ",dd,                                    file) ;
		}
		// handle targets
		if (is_tgt) {
			FileStat     st        ;                                                                                                  if (::lstat(file.c_str(),/*out*/&st)!=0) st.st_mode = 0 ;
			FileSig      sig       { st }                                                                                           ;
			TargetDigest td        { .tflags=flags.tflags , .extra_tflags=flags.extra_tflags }                                      ;
			bool         unlnk     = !sig                                                                                           ;
			bool         mandatory = td.tflags[Tflag::Target] && td.tflags[Tflag::Static] && !td.extra_tflags[ExtraTflag::Optional] ;
			//
			if (is_dep) td.tflags    |= Tflag::Incremental                            ;                            // if is_dep, previous target state is guaranteed by being a dep, use it
			/**/        td.pre_exist  = info.seen() && !td.tflags[Tflag::Incremental] ;
			/**/        td.written    = was_written                                   ;
			if ( !allow || (is_dep&&!flags.dep_and_target_ok()) ) {                                                // if SourceOk => ok to simultaneously be a dep and a target
				const char* write_msg = unlnk ? "unlink of" : was_written ? "write to" : "target declaration of" ;
				if (flags.dflags[Dflag::Static]) {
					if (unlnk) _user_trace( Comment::StaticDepAndTarget , CommentExt::Unlink , file ) ;
					else       _user_trace( Comment::StaticDepAndTarget ,                      file ) ;
					res.msg << write_msg<<" static dep "<<mk_file(file)<<'\n' ;
					if (!flags.extra_tflags[ExtraTflag::SourceOk]) {
						res.msg << "  if file is a source, consider calling :\n"                                  ;
						res.msg << "       lmake.target("<<mk_file(file,FileDisplay::Py   )<<",source_ok=True)\n" ;
						res.msg << "    or ltarget -s "  <<mk_file(file,FileDisplay::Shell)<<'\n'                 ;
					}
				} else if (!unlnk) { // if file is unlinked, ignore writing to it even if not allowed as it is common practice to write besides the final target and mv to it
					if (!allow) {
						_user_trace( Comment::UnexpectedTarget , file ) ;
						res.msg << "unexpected "<<write_msg<<' '<<mk_file(file)<<'\n' ;
					} else {
						bool        read_lnk = false   ;
						const char* read     = nullptr ;
						if      (accesses[Access::Reg ]       )   read = "read"        ;
						else if (accesses[Access::Lnk ]       ) { read = "readlink'ed" ; read_lnk = true ; }
						else if (accesses[Access::Stat]       )   read = "stat'ed"     ;
						else if (flags.dflags[Dflag::Required])   read = "required"    ;
						else                                      read = "accessed"    ;
						_user_trace( Comment::DepAndTarget , file ) ;
						/**/          res.msg << "unexpected "<<write_msg<<" file after it has been "<<read<<" : "<<mk_file(file)<<'\n'  ;
						if (read_lnk) res.msg << "  note : readlink is implicit when writing to a file while following symbolic links\n" ;
					}
					if (!seen_unexpected_write) {                                                                           // only give a single advice to avoid pullution
						res.msg << "  consider calling before file is accessed :\n"                ;
						res.msg << "       lmake.target("<<mk_file(file,FileDisplay::Py   )<<")\n" ;
						res.msg << "    or ltarget "     <<mk_file(file,FileDisplay::Shell)<<'\n'  ;
						seen_unexpected_write = true ;
					}
				}
			}
			if (unlnk) {
				td.crc = Crc::None ;
			} else if ( was_written || (+sig&&st.st_nlink>1) ) {                                                            // file may change through another link if any
				if ( status<=Status::Garbage || !td.tflags[Tflag::Target] ) { td.sig = sig ; td.crc = td.sig.tag() ;      } // no crc if meaningless
				else                                                          res.crcs.emplace_back(res.targets.size()) ;   // record index in res.targets for deferred (parallel) crc computation
			}
			if ( mandatory && !td.tflags[Tflag::Phony] && unlnk && status==Status::Ok )                                     // target is expected, not produced and no more important reason
				res.msg << "missing static target " << mk_file(file,No/*exists*/) << '\n' ;                                 // warn specifically
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			res.targets.emplace_back(file,td) ;
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("target ",td,STR(unlnk),STR(was_written),st.st_nlink,file) ;
		}
	}
	_user_trace( Comment::Analyzed ) ;
	trace("done",res.deps.size(),res.targets.size(),res.crcs.size(),res.msg) ;
	return res ;
}

void Gather::_send_to_server( JobMngtRpcReq const& jmrr ) {
	Trace trace("_send_to_server",jmrr) ;
	//    vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	try { OMsgBuf(jmrr).send(ClientSockFd(service_mngt)) ; } catch (::string const& e) { trace("no_server",e) ; throw ; }
	//    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}

void Gather::_send_to_server( Fd fd , Jerr&& jerr , JobSlaveEntry&/*inout*/ jse=::ref(JobSlaveEntry()) ) {
	Trace trace("_send_to_server",fd,jerr) ;
	//
	if (!jerr.sync) fd = {} ;                                                                             // dont reply if not sync
	JobMngtRpcReq jmrr   ;
	jmrr.seq_id = seq_id ;
	jmrr.job    = job    ;
	jmrr.fd     = fd     ;
	//
	switch (jerr.proc) {
		case Proc::ChkDeps : {
			_user_trace( jerr.date , jerr.comment , jerr.comment_exts ) ;
			Digest digest = analyze() ;
			jmrr.proc    = JobMngtProc::ChkDeps   ;
			jmrr.targets = ::move(digest.targets) ;
			jmrr.deps    = ::move(digest.deps   ) ;
		} break ;
		case Proc::DepDirect  :
		case Proc::DepVerbose :
			jmrr.proc = jerr.proc==Proc::DepVerbose ? JobMngtProc::DepVerbose : JobMngtProc::DepDirect ;
			jmrr.deps.reserve(jerr.files.size()) ;
			for( auto const& [f,_] : jerr.files )
				jmrr.deps.emplace_back( f , DepDigest(jerr.digest.accesses,Dflags(),true/*parallel*/) ) ; // no need for flags to ask info
			jse.jerr = ::move(jerr) ;
		break ;
	DF}                                                                                                   // NO_COV
	_send_to_server(jmrr) ;
	_n_server_req_pending++ ; trace("wait_server",_n_server_req_pending) ;
}

void Gather::_ptrace_child( Fd report_fd , ::latch* ready ) {
	t_thread_key = 'P' ;
	AutodepPtrace::s_init(autodep_env) ;
	_child.pre_exec = AutodepPtrace::s_prepare_child  ;
	//vvvvvvvvvvvv
	_child.spawn() ;                                                            // /!\ although not mentioned in man ptrace, child must be launched by the tracing thread
	//^^^^^^^^^^^^
	ready->count_down() ;                                                       // signal main thread that _child.pid is available
	AutodepPtrace autodep_ptrace{_child.pid} ;
	wstatus = autodep_ptrace.process() ;
	ssize_t cnt = ::write(report_fd,&::ref(char()),1) ; SWEAR( cnt==1 , cnt ) ; // report child end
	Record::s_close_reports() ;
}

Fd Gather::_spawn_child() {
	SWEAR(+cmd_line) ;
	//
	Fd   child_fd  ;
	Fd   report_fd ;
	bool is_ptrace = method==AutodepMethod::Ptrace ;
	//
	autodep_env.fast_mail = mail() ;
	//
	Trace trace("_spawn_child",child_stdin,child_stdout,child_stderr,method,autodep_env) ;
	//
	_add_env              = { {"LMAKE_AUTODEP_ENV",autodep_env} } ; // required even with method==None or ptrace to allow support (ldepend, lmake module, ...) to work
	_child.as_session     = as_session                            ;
	_child.nice           = nice                                  ;
	_child.stdin          = child_stdin                           ;
	_child.stdout         = child_stdout                          ;
	_child.stderr         = child_stderr                          ;
	if (is_ptrace) {                                                // PER_AUTODEP_METHOD : handle case
		// we split the responsability into 2 threads :
		// - parent watches for data (stdin, stdout, stderr & incoming connections to report deps)
		// - child launches target process using ptrace and watches it using direct wait (without signalfd) then report deps using normal socket report
		AcPipe pipe { New , 0/*flags*/ , true/*no_std*/ } ;
		child_fd  = pipe.read .detach() ;
		report_fd = pipe.write.detach() ;
	} else {
		if (method>=AutodepMethod::Ld) {                                                                                                                          // PER_AUTODEP_METHOD : handle case
			::string env_var ;
			switch (method) {                                                                                                                                     // PER_AUTODEP_METHOD : handle case
				#if HAS_LD_AUDIT
					case AutodepMethod::LdAudit           : env_var = "LD_AUDIT"   ; _add_env[env_var] = lmake_root_s + "_d$LIB/ld_audit.so"            ; break ;
				#endif
				#if 1                                                                                                                                             // LD_PRELOAD is always available
					case AutodepMethod::LdPreload         : env_var = "LD_PRELOAD" ; _add_env[env_var] = lmake_root_s + "_d$LIB/ld_preload.so"          ; break ;
					case AutodepMethod::LdPreloadJemalloc : env_var = "LD_PRELOAD" ; _add_env[env_var] = lmake_root_s + "_d$LIB/ld_preload_jemalloc.so" ; break ;
				#endif
			DF}                                                                                                                                                   // NO_COV
			if (env) { if (env->contains(env_var                  )) _add_env[env_var] += ':' + env->at(env_var) ; }
			else     { if (has_env      (env_var,false/*empty_ok*/)) _add_env[env_var] += ':' + get_env(env_var) ; }
			trace("ld",env_var,_add_env.at(env_var)) ;
		}
		new_exec( New , mk_glb(cmd_line[0],autodep_env.sub_repo_s) ) ;
	}
	start_date      = New                    ;                                      // record job start time as late as possible
	_child.cmd_line = cmd_line               ;
	_child.env      = env                    ;
	_child.add_env  = &_add_env              ;
	_child.cwd_s    = autodep_env.sub_repo_s ;
	if (is_ptrace) {
		::latch ready{1} ;
		_ptrace_thread = ::jthread( _s_ptrace_child , this , report_fd , &ready ) ; // /!\ _child must be spawned from tracing thread
		ready.wait() ;                                                              // wait until _child.pid is available
	} else {
		//vvvvvvvvvvvv
		_child.spawn() ;
		//^^^^^^^^^^^^
	}
	trace("child_pid",_child.pid) ;
	return child_fd ;                                                               // child_fd is only used with ptrace
}

Status Gather::exec_child() {
	try {
		return _exec_child() ;
	} catch (::string const& e) {      // START_OF_NO_COV defensive programming
		if (started) _child.waited() ;
		throw ;
	}                                  // END_OF_NO_COV
}

Status Gather::_exec_child() {
	using Event = Epoll<Kind>::Event ;
	Trace trace("exec_child",STR(as_session),method,autodep_env,cmd_line) ;
	//
	bool                        has_server        = +service_mngt  ;
	ServerSockFd                job_master_fd     { 0/*backlog*/ } ;
	AcFd                        fast_report_fd    ;                        // always open, never waited for
	AcFd                        child_fd          ;
	Epoll<Kind>                 epoll             { New          } ;
	Status                      status            = Status::New    ;
	::map<PD,::pair<Fd,Jerr>>   delayed_jerrs     ;                        // events that analyze deps and targets are delayed until all accesses are processed to ensure complete info
	size_t                      live_out_pos      = 0              ;
	::umap<Fd,ServerSlaveEntry> server_slaves     ;
	::umap<Fd,JobSlaveEntry   > job_slaves        ;                        // Jerr's waiting for confirmation
	PD                          end_timeout       = PD::Future     ;
	PD                          end_child         = PD::Future     ;
	PD                          end_kill          = PD::Future     ;
	PD                          end_heartbeat     = PD::Future     ;       // heartbeat to probe server when waiting for it
	bool                        timeout_fired     = false          ;
	size_t                      kill_step         = 0              ;
	bool                        seen_mount_chroot = false          ;
	bool                        seen_panic        = false          ;
	bool                        seen_tmp          = false          ;
	//
	auto set_status = [&]( Status status_ , ::string const& msg_={} ) {
		if (status==Status::New) status = status_ ;                     // only record first status
		if (+msg_              ) msg << add_nl << msg_ ;
	} ;
	auto kill = [&](bool next_step=false) {
		trace("kill",STR(next_step),kill_step,STR(as_session),_child.pid,_wait) ;
		if      (next_step             ) SWEAR(kill_step<=kill_sigs.size()) ;
		else if (kill_step             ) return ;
		if      (!_wait[Kind::ChildEnd]) return ;
		int   sig = kill_step<kill_sigs.size() ? kill_sigs[kill_step] : SIGKILL ;
		Pdate now { New }                                                       ;
		trace("kill_sig",sig) ;
		//                         vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		if ( sig && _child.pid>1 ) kill_process(_child.pid,sig,as_session/*as_group*/) ;
		//                         ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		set_status(Status::Killed) ;
		if      (kill_step==kill_sigs.size()) end_kill = Pdate::Future       ;
		else if (end_kill==Pdate::Future    ) end_kill = now      + Delay(1) ;
		else                                  end_kill = end_kill + Delay(1) ;
		_user_trace( now , Comment::Kill , cat(sig) ) ;
		kill_step++ ;
		trace("kill_done",end_kill) ;
	} ;
	auto open_fast_report_fd = [&]() {
		if (!autodep_env.fast_report_pipe) return ;
		fast_report_fd = AcFd( autodep_env.fast_report_pipe , {.flags=O_RDONLY|O_NONBLOCK,.err_ok=true} ) ; // avoid blocking waiting for child, no impact on epoll-controled ops
		if (+fast_report_fd) {                                                                              // work w/o fast report if it does not work (seen on some instances of Centos7)
			trace("open_fast_report_fd",autodep_env.fast_report_pipe,fast_report_fd) ;
			epoll.add_read( fast_report_fd , Kind::JobSlave ) ;
			epoll.dec() ;                                                           // fast_report_fd is always open and never waited for as we never know when a job may want to report on this fd
			job_slaves[fast_report_fd] ;                                            // allocate entry
		} else {
			trace("open_fast_report_fd",autodep_env.fast_report_pipe,StrErr()) ;
			autodep_env.fast_report_pipe.clear() ;
		}
	} ;
	//
	autodep_env.service = job_master_fd.service(addr) ;
	trace("autodep_env",::string(autodep_env)) ;
	//
	if (+autodep_env.fast_report_pipe) {
		bool first = true ;
	Retry :
		if ( ::mkfifo( autodep_env.fast_report_pipe.c_str() , 0600/*mode*/ )!=0 ) { // there is no reason for any other user to read/write this fifo
			if ( errno==ENOENT && first ) {
				dir_guard(autodep_env.fast_report_pipe) ;
				first = false ;                                                     // ensure at most one retry
				goto Retry ;
			} else if (errno!=EEXIST) {                                             // if it already exists, assume it is already a fifo
				autodep_env.fast_report_pipe.clear() ;                              // we'll live with no fast report
			}
		}
		open_fast_report_fd() ;
	}
	if (+server_master_fd) {
		epoll.add_read(server_master_fd,Kind::ServerMaster) ;
		trace("read_server_master",server_master_fd,"wait",_wait,+epoll) ;
	}
	_wait = Kind::ChildStart ;
	trace("start","wait",_wait,+epoll) ;
	for(;;) {
		Pdate now = New ;
		if (now>=end_child) {
			SWEAR( !_wait[Kind::ChildEnd] , _wait,end_child ) ;
			_user_trace( now , Comment::StillAlive ) ;
			::string dead  = ( (now-end_child) + network_delay + Delay(1) ).short_str() ;
			if ( _wait[Kind::Stdout] || _wait[Kind::Stderr] ) {
				First first ;
				//
				::string                 msg_                                                                                                             ;
				if (_wait[Kind::Stdout]) msg_ << first(         )<<"stdout "                                                                              ;
				if (_wait[Kind::Stderr]) msg_ << first("","and ")<<"stderr "                                                                              ;
				/**/                     msg_ << "still open after job has terminated "<<dead<<" ago (networkd_delay is "<<network_delay.short_str()<<')' ;
				set_status(Status::Err,msg_) ;
			}
			::string kill_msg = "still alive after having " ;
			if      (timeout_fired              ) kill_msg << "timed out and "                                                            ;
			if      (!kill_step                 ) kill_msg << "exited "<<dead<<" ago (networkd_delay is "<<network_delay.short_str()<<')' ;
			else if (kill_step<=kill_sigs.size()) kill_msg << "been killed "<<kill_step       <<" times"                                  ;
			else if (+kill_sigs.size()          ) kill_msg << "been killed "<<kill_sigs.size()<<" times followed by SIGKILL"              ;
			else                                  kill_msg << "been killed with SIGKILL"                                                  ;
			kill_msg << '\n' ;
			set_status( Status::Err , kill_msg ) ;
			break ;                                              // exit loop
		}
		if (now>=end_kill) {
			kill(true/*next*/) ;
		}
		if ( now>=end_timeout && !timeout_fired ) {
			_user_trace( now , Comment::Timeout ) ;
			set_status(Status::Err,"timeout after "+timeout.short_str()) ;
			kill() ;
			timeout_fired = true          ;
			end_timeout   = Pdate::Future ;
		}
		if (!kill_step) {
			if (end_heartbeat==Pdate::Future) { if ( _n_server_req_pending) end_heartbeat = now + HeartbeatTick ; }
			else                              { if (!_n_server_req_pending) end_heartbeat = Pdate::Future       ; }
			if (now>end_heartbeat           ) {
				SWEAR(has_server,_n_server_req_pending) ;
				trace("server_heartbeat",_n_server_req_pending) ;
				JobMngtRpcReq jmrr ;
				jmrr.seq_id = seq_id                 ;
				jmrr.job    = job                    ;
				jmrr.proc   = JobMngtProc::Heartbeat ;
				try                     { _send_to_server(jmrr) ; end_heartbeat += HeartbeatTick ; }
				catch (::string const&) { kill() ;                                                 }
			}
		}
		bool  must_wait      = +epoll || +_wait ;
		Pdate max_event_date = now              ;
		if ( must_wait && !_wait[Kind::ChildStart] ) {
			/**/                max_event_date = ::min({ end_child , end_kill , end_timeout , end_heartbeat }) ;
			if (+delayed_jerrs) max_event_date = ::min(  max_event_date , delayed_jerrs.begin()->first       ) ;
		}
		::vector<Event> events = epoll.wait(max_event_date) ;
		if (!events) {
			if (+delayed_jerrs) {                                // process delayed jerrs after all other events
				while (+delayed_jerrs) {
					auto  it           = delayed_jerrs.begin() ;
					auto& [pd,fd_jerr] = *it                   ;
					if (pd>now) break ;                          // delayed_jerrs is ordered by date, so all following jerr's are scheduled later
					trace("delayed_jerr",fd_jerr) ;
					auto& [fd,jerr] = fd_jerr ;
					switch (jerr.proc) {
						case Proc::ChkDeps :
							if (has_server) _send_to_server( fd , ::move(jerr) ) ;
							else            sync( fd , {.proc=Proc::ChkDeps,.ok=Yes} ) ;
						break ;
						case Proc::List : {
							CommentExts     ces    ;
							JobExecRpcReply reply  { .proc=Proc::List } ;
							Digest          digest = analyze()          ;
							if (jerr.digest.write!=No ) { ces |= CommentExt::Write ; for( auto& [tn,td] : digest.targets ) if (td.crc!=Crc::None) reply.files.push_back(::move(tn)) ; }
							if (jerr.digest.write!=Yes) { ces |= CommentExt::Read  ; for( auto& [dn,_ ] : digest.deps    )                        reply.files.push_back(::move(dn)) ; }
							_user_trace( jerr.date , Comment::List , ces ) ;
							sync( fd , ::move(reply) ) ;
						} break ;
					DF}                                          // NO_COV
					delayed_jerrs.erase(it) ;
				}
			} else if (_wait[Kind::ChildStart]) {                // handle case where we are killed before starting : create child when we have processed waiting connections from server
				try {
					child_fd = _spawn_child() ;
				} catch(::string const& e) {
					trace("spawn_failed",e) ;
					if (child_stderr==Child::PipeFd) stderr = with_nl(e) ;
					else                             child_stderr.write(with_nl(e)) ;
					status = Status::EarlyErr ;
					break ;                                      // cannot start, exit loop
				}
				if (+timeout) end_timeout = start_date + timeout ;
				_user_trace( start_date , Comment::StartJob ) ;
				trace("started","wait",_wait,+epoll) ;
				started = true ;
				//
				if (child_stdout==Child::PipeFd) { epoll.add_read( _child.stdout , Kind::Stdout     ) ; _wait |= Kind::Stdout   ; trace("read_stdout    ",_child.stdout ,"wait",_wait,+epoll) ; }
				if (child_stderr==Child::PipeFd) { epoll.add_read( _child.stderr , Kind::Stderr     ) ; _wait |= Kind::Stderr   ; trace("read_stderr    ",_child.stderr ,"wait",_wait,+epoll) ; }
				if (+child_fd                  ) { epoll.add_read( child_fd      , Kind::ChildEndFd ) ; _wait |= Kind::ChildEnd ; trace("read_child     ",child_fd      ,"wait",_wait,+epoll) ; }
				else                             { epoll.add_pid ( _child.pid    , Kind::ChildEnd   ) ; _wait |= Kind::ChildEnd ; trace("read_child_proc",               "wait",_wait,+epoll) ; }
				/**/                               epoll.add_read( job_master_fd , Kind::JobMaster  ) ;                           trace("read_job_master",job_master_fd ,"wait",_wait,+epoll) ;
				_wait &= ~Kind::ChildStart ;
			} else if (!must_wait) {
				break ;                                          // we are done, exit loop
			}
		}
		for( Event const& event : events ) {
			Kind kind = event.data() ;
			Fd   fd   ;                if (kind!=Kind::ChildEnd) fd = event.fd() ;                                              // no fd available for ChildEnd
			switch (kind) {
				case Kind::Stdout :
				case Kind::Stderr : {
					char          buf[4096] ;
					int           cnt       = ::read( fd , buf , sizeof(buf) ) ; SWEAR( cnt>=0 , cnt ) ;
					::string_view buf_view  { buf , size_t(cnt) }                                      ;
					if (cnt) {
						trace(kind,fd,cnt) ;
						if (kind==Kind::Stderr) {
							stderr.append(buf_view) ;
						} else {
							size_t old_sz = stdout.size() ;
							stdout.append(buf_view) ;
							if ( live_out && has_server )
								if ( size_t pos = buf_view.rfind('\n')+1 ;  pos ) {
									size_t        len  = old_sz + pos - live_out_pos ;
									JobMngtRpcReq jmrr ;
									jmrr.seq_id = seq_id                          ;
									jmrr.job    = job                             ;
									jmrr.proc   = JobMngtProc::LiveOut            ;
									jmrr.txt    = stdout.substr(live_out_pos,len) ;
									//vvvvvvvvvvvvvvvvvvv
									_send_to_server(jmrr) ;
									//^^^^^^^^^^^^^^^^^^^
									trace("live_out",live_out_pos,len) ;
									live_out_pos += len ;
								}
						}
					} else {
						epoll.del(false/*write*/,fd) ;
						_wait &= ~kind ;
						trace(kind,fd,"close","wait",_wait,+epoll) ;
					}
				} break ;
				case Kind::ChildEnd   :
				case Kind::ChildEndFd : {
					int ws ;
					if (kind==Kind::ChildEnd) { ::waitpid(_child.pid,&ws,0/*flags*/) ;                    wstatus = ws      ; } // wstatus is atomic, cant take its addresss as a int*
					else                      { int cnt=::read(fd,&::ref(char()),1) ; SWEAR(cnt==1,cnt) ; ws      = wstatus ; } // wstatus is already set, just flush fd
					trace(kind,fd,_child.pid,ws) ;
					SWEAR( !WIFSTOPPED(ws) , _child.pid ) ;                            // child must have ended if we are here
					//
					end_date   = New                                ;
					_wait     &= ~Kind::ChildEnd                    ;
					end_child  = end_date + network_delay +Delay(1) ;                  // wait at most network_delay (+ 1s for our own processing) for reporting & stdout & stderr to settle down
					_user_trace( end_date , Comment::EndJob , to_hex(uint16_t(ws)) ) ;
					//
					if      (WIFEXITED  (ws)) set_status(             WEXITSTATUS(ws)!=0 ? Status::Err : Status::Ok       ) ;
					else if (WIFSIGNALED(ws)) set_status( is_sig_sync(WTERMSIG   (ws))   ? Status::Err : Status::LateLost ) ; // synchronous signals are actually errors
					else                      FAIL("unexpected wstatus : ",ws) ;                                              // NO_COV defensive programming
					if (kind==Kind::ChildEnd) epoll.del_pid(_child.pid       ) ;
					else                      epoll.del    (false/*write*/,fd) ;
					_child.waited() ;                                                                                         // _child has been waited without calling _child.wait()
					/**/                   epoll.dec() ;                                                                      // dont wait for new connections from job (but process those that come)
					if (+server_master_fd) epoll.dec() ;                                                                      // idem for connections from server
					trace(kind,fd,"close",status,"wait",_wait,+epoll) ;
				} break ;
				case Kind::JobMaster    : {
					SWEAR( fd==job_master_fd , fd,job_master_fd ) ;
					Fd sfd = job_master_fd.accept().detach() ;
					epoll.add_read(sfd,Kind::JobSlave) ;
					job_slaves[sfd] = {.key=job_master_fd.key} ;
					trace(kind,fd,"job_slave",sfd,"wait",_wait,+epoll) ;
				} break ;
				case Kind::ServerMaster : {
					SWEAR( fd==server_master_fd , fd,server_master_fd ) ;
					Fd sfd = server_master_fd.accept().detach() ;
					epoll.add_read(sfd,Kind::ServerSlave) ;
					server_slaves[sfd] = {.key=server_master_fd.key} ;                                                        // allocate entry
					trace(kind,fd,"server_slave",sfd,"wait",_wait,+epoll) ;
				} break ;
				case Kind::ServerSlave : {
					auto                        sit      = server_slaves.find(fd)                                         ; SWEAR(sit!=server_slaves.end(),fd,server_slaves) ;
					ServerSlaveEntry&           sse      = sit->second                                                    ;
					::optional<JobMngtRpcReply> received = sse.buf.receive_step<JobMngtRpcReply>(fd,Yes/*fetch*/,sse.key) ; if (!received) { trace(kind,fd,"...") ; break ; } // partial message
					JobMngtRpcReply&            jmrr     = *received                                                      ;
					trace(kind,fd,"received",_n_server_req_pending,jmrr,jmrr.seq_id) ;
					Fd rfd = jmrr.fd ;                                                                                                 // capture before move
					if (jmrr.seq_id!=seq_id) goto ServerNextEvent ;                                                                    // message is not for us
					switch (jmrr.proc) {
						case JobMngtProc::None      :                                                                                  // eof or message is not for us
						case JobMngtProc::Heartbeat : goto ServerNextEvent ;                                                           // just receiving the message is enough, nothing to do
						case JobMngtProc::Kill      :
							_user_trace( Comment::Kill , CommentExt::Reply ) ;
							set_status(Status::Killed)                       ;
							kill()                                           ;
						break ;
						case JobMngtProc::DepDirect  :
						case JobMngtProc::DepVerbose : {
							_n_server_req_pending-- ;
							if ( auto sit=job_slaves.find(rfd) ; sit==job_slaves.end() ) {
								rfd = {} ;                                                                                             // job is dead, ignore server reply
							} else {
								JobSlaveEntry& jse     = sit->second                        ;
								bool           verbose = jmrr.proc==JobMngtProc::DepVerbose ;
								Pdate          now     { New }                              ;
								//
								if (verbose) {
									for( VerboseInfo& vi : jmrr.verbose_infos ) {
										::string ok_str  ;
										::string crc_str ;
										if (!jse.jerr.digest.flags.dflags[Dflag::IgnoreError]) vi.ok  = Maybe                                         ;
										else                                                   ok_str = vi.ok==Yes ? "ok" : vi.ok==No ? "error" : "-" ;
										if ( !(jse.jerr.digest.accesses&DataAccesses) ) {
											vi.crc = {} ;
										} else {
											if      ( !jse.jerr.digest.accesses[Access::Lnk] && vi.crc.is_lnk() ) vi.crc = Crc::None ; // does not distinguish regular from no file
											else if ( !jse.jerr.digest.accesses[Access::Reg] && vi.crc.is_reg() ) vi.crc = Crc::None ; // does not distinguish link    from no file
											crc_str = ::string(vi.crc) ;
										}
										_user_trace( now , Comment::Depend , {CommentExt::Verbose,CommentExt::Reply} , cat( ok_str , +ok_str&&+crc_str?"/":"" , crc_str ) ) ;
									}
								} else {
									NfsGuard nfs_guard { autodep_env.file_sync } ;
									for( auto const& [f,_] : jse.jerr.files ) nfs_guard.access(f) ;
									jse.jerr.digest.flags.extra_dflags |= ExtraDflag::NoHot ;                                  // dep has been built and we are guarded : it cannot be hot from now on
									_user_trace( now , Comment::Depend , CommentExts(CommentExt::Direct,CommentExt::Reply) ) ;
								}
								for( auto& [f,_] : jse.jerr.files ) {
									FileInfo fi { f } ;
									new_access( rfd , now , ::move(f) , jse.jerr.digest , fi , Yes/*late*/ , jse.jerr.comment , jse.jerr.comment_exts ) ;
								}
								jse.jerr = {} ;
							}
						} break ;
						case JobMngtProc::ChkDeps    :
						case JobMngtProc::ChkTargets : {
							bool        is_target = jmrr.proc==JobMngtProc::ChkTargets ;
							CommentExts ces       = CommentExt::Reply                  ;
							_n_server_req_pending-- ;
							switch (jmrr.ok) {
								case Maybe :
									ces |= CommentExt::Killed ;
									set_status( Status::ChkDeps , cat(is_target?"pre-existing target":"waiting dep"," : ",jmrr.txt) ) ;
									kill() ;
									rfd = {} ;                                                                                 // dont reply to ensure job waits if sync
								break ;
								case No :
									ces |= CommentExt::Err ;
								break ;
							DN}
							_user_trace( is_target?Comment::CheckTargets:Comment::CheckDeps , CommentExts(CommentExt::Reply) , jmrr.txt ) ;
						} break ;
						case JobMngtProc::AddLiveOut : {
							trace("add_live_out",STR(live_out),live_out_pos) ;
							if (!live_out) {
								live_out     = true                 ;
								live_out_pos = stdout.rfind('\n')+1 ;
							}
							if ( live_out_pos && has_server ) {
								JobMngtRpcReq jmrr ;
								jmrr.seq_id = seq_id                        ;
								jmrr.job    = job                           ;
								jmrr.proc   = JobMngtProc::AddLiveOut       ;
								jmrr.txt    = stdout.substr(0,live_out_pos) ;
								//vvvvvvvvvvvvvvvvvvv
								_send_to_server(jmrr) ;
								//^^^^^^^^^^^^^^^^^^^
							}
						} break ;
					DF}                                                                                                        // NO_COV
					if (+rfd) {
						// for ChkDeps and DepDirect, jmrr.ok may be Maybe if job is now useless (due to ^C) and check was not performed
						// in that case, dont reply and job will be killed
						JobExecRpcReply jerr ;
						switch (jmrr.proc) {
							case JobMngtProc::None       :                                                                                                      break ;
							case JobMngtProc::ChkDeps    : if (jmrr.ok!=Maybe) jerr = { .proc=Proc::ChkDeps    , .ok=jmrr.ok                                } ; break ; // cf above
							case JobMngtProc::DepDirect  : if (jmrr.ok!=Maybe) jerr = { .proc=Proc::DepDirect  , .ok=jmrr.ok                                } ; break ; // .
							case JobMngtProc::DepVerbose :                     jerr = { .proc=Proc::DepVerbose , .verbose_infos=::move(jmrr.verbose_infos ) } ; break ;
						DF}                                                                                                                                             // NO_COV
						trace("reply",jerr) ;
						//         vvvvvvvvvvvvvvvvvvvvvvvvvv
						if (+jerr) sync( rfd , ::move(jerr) ) ;
						//         ^^^^^^^^^^^^^^^^^^^^^^^^^^
					}
				ServerNextEvent :
					epoll.close(false/*write*/,fd) ;
					trace(kind,fd,"close","wait",_wait,+epoll) ;
				} break ;
				case Kind::JobSlave : {
					auto           sit            = job_slaves.find(fd) ; SWEAR(sit!=job_slaves.end(),fd,job_slaves) ;
					JobSlaveEntry& jse            = sit->second         ;
					bool           is_fast_report = fd==fast_report_fd  ;
					for( Bool3 fetch=Yes ;; fetch=No ) {
						::optional<Jerr> received = jse.buf.receive_step<Jerr>(fd,fetch,jse.key) ; if (!received) goto JobNextEvent ;  // partial message
						Jerr&            jerr     = *received                                    ;
						Proc             proc     = jerr.proc                                    ;                                     // capture before jerr is ::move()'ed
						bool             sync_    = jerr.sync==Yes                               ; if (is_fast_report) SWEAR(!sync_) ; // Maybe means not sync, only for transport ...
						switch (proc) {                                                                                                // ... cannot reply on fast_report_fd
							case Proc::None :
								if (is_fast_report) {
									epoll.del(false/*write*/,fd,false/*wait*/) ;               // fast_report_fd is not waited as it is always open and will be closed as it is an AcFd
									open_fast_report_fd() ;                                    // reopen as job may close the pipe and reopen it later
								} else {
									epoll.close(false/*write*/,fd) ;
								}
								trace(kind,fd,proc,"wait",_wait,+epoll) ;
								for( auto& [_,um] : jse.to_confirm )
									for( Jerr& j : um )
										_new_accesses(fd,::move(j)) ;                          // process deferred entries although with uncertain outcome
								job_slaves.erase(sit) ;
								goto JobNextEvent ;
							case Proc::ChkDeps :
							case Proc::List    :
								trace(kind,fd,proc) ;
								delayed_jerrs.try_emplace(jerr.date,::pair(fd,::move(jerr))) ;
								sync_ = false ;                                                // if sync, reply is delayed
							break ;
							case Proc::Confirm : {
								trace(kind,fd,proc,jerr.digest.write,jerr.id) ;
								Trace trace2 ;
								auto it = jse.to_confirm.find(jerr.id) ; SWEAR( it!=jse.to_confirm.end() , jerr.id , jse.to_confirm ) ;
								SWEAR(jerr.digest.write!=Maybe) ;                                                                                      // ensure we confirm/infirm
								for ( Jerr& j : it->second ) {
									SWEAR(j.digest.write==Maybe) ;
									/**/                    j.digest.write  = jerr.digest.write ;
									if (!jerr.digest.write) j.comment_exts |= CommentExt::Err   ;
									_new_accesses(fd,::move(j)) ;
								}
								jse.to_confirm.erase(it) ;
							} break ;
							case Proc::Tmp :
								if (!seen_tmp) {
									trace(kind,fd,proc) ;
									if (no_tmp) {
										_user_trace( jerr.date , Comment::Tmp , CommentExt::Err ) ;
										set_status(Status::Err,"tmp access with no tmp dir") ;
										kill() ;
									} else {
										_user_trace( jerr.date , Comment::Tmp ) ;
									}
									seen_tmp = true ;
								}
							break ;
							case Proc::Chroot :
							case Proc::Mount  : {
								::string const& dst = jerr.files[0].first                            ;
								::string        msg = cat("forbidden ",jerr.comment," to ",dst,'\n') ;
								trace(kind,fd,proc) ;
								_user_trace( New , jerr.comment , CommentExt::Err , dst ) ;
								if (!seen_mount_chroot) {
									msg << "  mount and chroot make deps recording unreliable, but carefully used, a combination of them may be reliable"<<'\n' ;
									if (proc==Proc::Mount) {
										static constexpr char Pfx[] = "  consider a reliable alternative to " ;
										::string d = no_slash(dst) ;
										switch (jerr.files[0].second.tag()) {
											case FileTag::Dir : msg << Pfx<<"mount source_dir as " <<dst<<" :\n  - "<<rule<<".views = { "<<mk_py_str   (d+'/')<<" : 'source_dir/' }"<<'\n' ; break ;
											case FileTag::Lnk : msg << Pfx<<"copy source_link to " <<dst<<" :\n  - cp source_lnk "       <<mk_shell_str(d    )                      <<'\n' ; break ;
											case FileTag::Reg : msg << Pfx<<"mount source_file as "<<dst<<" :\n  - "<<rule<<".views = { "<<mk_py_str   (d    )<<" : 'source_file' }"<<'\n' ; break ;
										DF}
									}
									msg << "  consider, if you are certain you want to proceed with "<<jerr.comment<<" :"<<'\n' ;
									msg << "  - "<<rule<<".mount_chroot_ok = True"                                       <<'\n' ;
									msg << "  consider, if you are ready to manage deps by hand :"                       <<'\n' ;
									msg << "  - "<<rule<<".autodep = 'none'"                                             <<'\n' ;
								}
								set_status(Status::Err,msg) ;
								if (!seen_mount_chroot) {
									seen_mount_chroot = true ;
									kill() ;
								}
							} break ;
							case Proc::DepDirect  :
							case Proc::DepVerbose :
								trace(kind,fd,proc) ;
								if (has_server) { _send_to_server( fd , ::move(jerr) , jse ) ; sync_ = false ; }                                       // if sent to server, reply is delayed
							break ;
							case Proc::Guard      :
								trace(kind,fd,proc,jerr.files.size()) ;
								for( auto& [f,_] : jerr.files ) guards.insert(::move(f)) ;
							break ;
							case Proc::Panic :                                                                                                         // START_OF_NO_COV defensive programming
								if (!seen_panic) {                                                                                                     // report only first panic
									trace(kind,fd,proc,jerr.txt()) ;
									_user_trace( jerr.date , Comment::Panic , jerr.txt() ) ;
									set_status(Status::Err,jerr.txt()) ;
									kill() ;
									seen_panic = true ;
								}
							[[fallthrough]] ;                                                                                                          // END_OF_NO_COV
							case Proc::Trace :                                                                                                         // START_OF_NO_COV debug only
								trace(kind,fd,proc,jerr.txt()) ;
								_user_trace( jerr.date , Comment::Trace , jerr.txt() ) ;
							break ;                                                                                                                    // END_OF_NO_COV
							case Proc::Access :
								// for read accesses, trying is enough to trigger a dep, so confirm is useless
								if (jerr.digest.write==Maybe) { trace(kind,fd,proc,"maybe",jerr) ; jse.to_confirm[jerr.id].push_back(::move(jerr)) ; } // delay until confirmed/infirmed
								else                            _new_accesses(fd,::move(jerr)) ;
							break ;
							case Proc::AccessPattern :
								trace(kind,fd,proc,jerr.date,jerr.digest,jerr.files) ;
								for( auto const& [f,_] : jerr.files ) pattern_flags.emplace_back( f/*pattern*/ , ::pair(jerr.date,jerr.digest.flags) ) ;
							break ;
						DF}                                                                                                                            // NO_COV
						if (sync_) sync( fd , ::move(jerr).mimic_server() ) ;
					}
				JobNextEvent : ;
				} break ;
			DF}                                                                                                                                        // NO_COV
		}
	}
	SWEAR(!_child) ;                                                                                                                                   // _child must have been waited by now
	trace("done",status) ;
	return status ;
}

// reorder accesses in chronological order and suppress implied dependencies :
// - when a file is depended upon, its uphill directories are implicitly depended upon under the following conditions, no need to keep them and this significantly decreases the number of deps
//   - either file exists
//   - or dir is only accessed as link
// - suppress dir when one of its sub-files appears before            (and condition above is satisfied)
// - suppress dir when one of its sub-files appears immediately after (and condition above is satisfied)
void Gather::reorder(bool at_end) {
	Trace trace("reorder") ;
	// update accesses to take pattern_flags into account
	if (+pattern_flags)                                                             // fast path : if no patterns, nothing to do
		for ( auto& [file,ai] : accesses ) {
			if (ai.flags.extra_dflags[ExtraDflag::NoStar]) continue ;
			for ( auto const& [re,date_flags] : pattern_flags )
				if (+re.match(file)) {
					trace("pattern_flags",file,date_flags) ;
					ai.update( date_flags.first , {.flags=date_flags.second} , date_flags.first<=start_date ) ;
				}
		}
	// although not strictly necessary, use a stable sort so that order presented to user is as close as possible to what is expected
	::stable_sort(                                                                  // reorder by date, keeping parallel entries together (which must have the same date)
		accesses
	,	[]( ::pair_s<AccessInfo> const& a , ::pair_s<AccessInfo> const& b )->bool { return a.second.sort_key()<b.second.sort_key() ; }
	) ;
	// 1st pass (backward) : note dirs immediately preceding sub-files
	::vector<::vmap_s<AccessInfo>::reverse_iterator> lasts   ;                      // because of parallel deps, there may be several last deps
	Pdate                                            last_pd = Pdate::Future ;
	for( auto it=accesses.rbegin() ; it!=accesses.rend() ; it++ ) {
		{	AccessInfo&     ai       = it->second       ;
			Pdate           fw       = ai.first_write() ; if (fw<Pdate::Future              )                   goto NextDep ;
			/**/                                          if (ai.flags.dflags!=DflagsDfltDyn) { lasts.clear() ; goto NextDep ; }
			Accesses        accesses = ai.accesses()    ; if (!accesses                     )                   goto NextDep ;
			::string const& file     = it->first        ;
			for( auto last : lasts ) {
				if (!lies_within(last->first,file)     )   continue ;
				if (last->second.dep_info.exists()==Yes) { trace("skip_from_next"  ,file) ; ai.clear_accesses() ;                     goto NextDep ; }
				else                                     { trace("no_lnk_from_next",file) ; ai.clear_lnk     () ; if (!ai.accesses()) goto NextDep ; }
			}
			if ( Pdate fr=ai.first_read() ; fr<last_pd ) {
				lasts.clear() ;                                                     // not a parallel dep => clear old ones that are no more last
				last_pd = fr ;
			}
			lasts.push_back(it) ;
		}
	NextDep : ;
	}
	// 2nd pass (forward) : suppress dirs of seen files and previously noted dirs
	::umap_s<bool/*sub-file exists*/> dirs  ;
	size_t                            i_dst = 0     ;
	bool                              cpy   = false ;
	for( auto& access : accesses ) {
		::string   const& file = access.first  ;
		AccessInfo      & ai   = access.second ;
		if ( ai.first_write()==Pdate::Future && ai.flags.dflags==DflagsDfltDyn && !ai.flags.tflags ) {
			auto it = dirs.find(file+'/') ;
			if (it!=dirs.end()) {
				if (it->second) { trace("skip_from_prev"  ,file) ; ai.clear_accesses() ; }
				else            { trace("no_lnk_from_prev",file) ; ai.clear_lnk     () ; }
			}
			if (ai.first_read()==PD::Future) {
				if (!at_end) access_map.erase(file) ;
				cpy = true ;
				continue ;
			}
		}
		bool exists = ai.dep_info.exists()==Yes ;
		try {
			for( ::string dir_s=dir_name_s(file) ;; dir_s=dir_name_s(dir_s) ) {     // walk all accessible dirs
				auto [it,inserted] = dirs.try_emplace(dir_s,exists) ;
				if (!inserted) {
					if (it->second>=exists) break ;                                 // all uphill dirs are already inserted if a dir has been inserted
					it->second = exists ;                                           // record existence of a sub-file as soon as one if found
				}
			}
		} catch (::string const&) {}
		if (cpy) accesses[i_dst] = ::move(access) ;
		i_dst++ ;
	}
	accesses.resize(i_dst) ;
	for( NodeIdx i : iota(accesses.size()) ) access_map.at(accesses[i].first) = i ; // always recompute access_map as accesses has been sorted
}
