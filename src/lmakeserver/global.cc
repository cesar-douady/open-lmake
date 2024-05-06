// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Py   ;
using namespace Time ;

namespace Engine {

	ThreadDeque<EngineClosure> g_engine_queue ;

	static ::string _audit_indent( ::string&& t , DepDepth l , char sep=0 ) {
		if (!l) {
			SWEAR(!sep) ;      // cannot have a sep if we have no room to put it
			return ::move(t) ;
		}
		::string res = indent<' ',2>(t,l) ;
		if (sep) res[2*(l-1)] = sep ;
		return res ;
	}

	void _audit( Fd out_fd , ::ostream* log , ReqOptions const& ro , Color c , ::string const& txt , bool as_is , DepDepth lvl , char sep ) {
		if (!txt) return ;
		//
		::string   report_txt  = color_pfx(ro,c)                              ;
		if (as_is) report_txt += ensure_no_nl(         txt                  ) ;
		else       report_txt += ensure_no_nl(localize(txt,ro.startup_dir_s)) ; // ensure color suffix is not at start-of-line to avoid indent adding space at end of report
		/**/       report_txt += color_sfx(ro,c)                              ;
		/**/       report_txt += '\n'                                         ;
		//
		using Proc = ReqRpcReplyProc ;
		try                       { OMsgBuf().send( out_fd , ReqRpcReply(Proc::Txt,_audit_indent(::move(report_txt),lvl,sep)) ) ; } // if we lose connection, there is nothing much we can do ...
		catch (::string const& e) { Trace("audit","lost_client",e) ;                                                              } // ... about it (hoping that we can still trace)
		if (log)
			try                       { *log << _audit_indent(ensure_nl(as_is?txt:localize(txt,{})),lvl,sep) << ::flush ; }         // .
			catch (::string const& e) { Trace("audit","lost_log",e) ;                                                     }
	}

	void audit_file( Fd out_fd , ::string&& file ) {
		using Proc = ReqRpcReplyProc ;
		try                       { OMsgBuf().send( out_fd , ReqRpcReply(Proc::File,::move(file)) ) ; } // if we lose connection, there is nothing much we can do ...
		catch (::string const& e) { Trace("audit_file","lost_client",e) ;                             } // ... about it (hoping that we can still trace)
	}

	void _audit_status( Fd out_fd , ::ostream* log , ReqOptions const& , bool ok ) {
		using Proc = ReqRpcReplyProc ;
		try                       { OMsgBuf().send( out_fd , ReqRpcReply(Proc::Status,ok) ) ; }        // if we lose connection, there is nothing much we can do ...
		catch (::string const& e) { Trace("audit_status","lost_client",e) ;                   }        // ... about it (hoping that we can still trace)
		if (log)
			try                       { *log << "status : " << (ok?"ok":"failed") <<'\n'<< ::flush ; } // .
			catch (::string const& e) { Trace("audit_status","lost_log",e) ;                         }
	}

	void _audit_ctrl_c( Fd , ::ostream* log , ReqOptions const& ) {
		// lmake echos a \n as soon as it sees ^C (and it does that much faster than we could), no need to do it here
		if (log)
			try                       { *log << "^C\n"<< ::flush ;           }
			catch (::string const& e) { Trace("audit_ctrl_c","lost_log",e) ; }
	}

	//
	// Config
	//

	::ostream& operator<<( ::ostream& os , Config::Backend const& be ) {
		os << "Backend(" ;
		if (be.configured) {
			if (+be.ifce) os << ::hex<<be.ifce<<::dec <<',' ;
			/**/          os << be.dct                      ;
		}
		return os <<')' ;
	}

	::ostream& operator<<( ::ostream& os , ConfigStatic::Cache const& c ) {
		return os << "Cache(" << c.tag <<','<< c.dct << ')' ;
	}

	::ostream& operator<<( ::ostream& os , Config const& sc ) {
		os << "Config("
			/**/ << sc.db_version.major <<'.'<< sc.db_version.minor
			<<','<< sc.hash_algo
			<<','<< sc.lnk_support
		;
		if (sc.max_dep_depth       )                  os <<",MD" << sc.max_dep_depth          ;
		if (sc.max_err_lines       )                  os <<",EL" << sc.max_err_lines          ;
		if (sc.path_max!=size_t(-1))                  os <<",PM" << sc.path_max               ;
		if (+sc.caches             )                  os <<','   << sc.caches                 ;
		for( BackendTag t : All<BackendTag> ) if (+t) os <<','   << t <<':'<< sc.backends[+t] ;
		return os<<')' ;
	}

	ConfigStatic::Cache::Cache(Dict const& py_map) {
		::string field     ;
		bool     found_tag = false ;
		for( auto const& [py_k,py_v] : py_map ) {
			field = py_k.as_a<Str>() ;
			if (field=="tag") { tag = mk_enum<Tag>(py_v.as_a<Str>()) ; found_tag = true ; }
			else                dct.emplace_back(field,*py_v.str()) ;
		}
		if (!found_tag) throw "tag not found"s ;
	}

	ConfigDynamic::Backend::Backend(Dict const& py_map) : configured{true} {
		::string field ;
		try {
			for( auto const& [py_k,py_v] : py_map ) {
				field = py_k.as_a<Str>() ;
				::string v = py_v==True ? "1"s : py_v==False ? "0"s : ::string(*py_v.str()) ;
				if (field=="interface") ifce = v ;
				else                    dct.emplace_back(field,v) ;
			}
		} catch(::string const& e) {
			throw to_string("while processing ",field,e) ;
		}
	}

	Config::Config(Dict const& py_map) : booted{true} {                                                                         // if config is read from makefiles, it is booted
		db_version = Version::Db ;                                                                                              // record current version
		// generate a random key
		char     buf_char[8] ; IFStream("/dev/urandom").read(buf_char,sizeof(buf_char)) ;
		uint64_t buf_int     ; ::memcpy( &buf_int , buf_char , sizeof(buf_int) )        ;
		key = to_string( ::hex , ::setfill('0') , ::setw(sizeof(buf_int)*2) , buf_int ) ;
		//
		::vector_s fields = {{}} ;
		try {
			fields[0] = "disk_date_precision" ; if (py_map.contains(fields[0])) date_prec             = Time::Delay               (py_map[fields[0]].as_a<Float>())           ;
			fields[0] = "hash_algo"           ; if (py_map.contains(fields[0])) hash_algo             = mk_enum<Algo>             (py_map[fields[0]].as_a<Str  >())           ;
			fields[0] = "local_admin_dir"     ; if (py_map.contains(fields[0])) user_local_admin_dir  =                           (py_map[fields[0]].as_a<Str  >())           ;
			fields[0] = "heartbeat"           ; if (py_map.contains(fields[0])) heartbeat             = +py_map[fields[0]] ? Delay(py_map[fields[0]].as_a<Float>()) : Delay() ;
			fields[0] = "heartbeat_tick"      ; if (py_map.contains(fields[0])) heartbeat_tick        = +py_map[fields[0]] ? Delay(py_map[fields[0]].as_a<Float>()) : Delay() ;
			fields[0] = "max_dep_depth"       ; if (py_map.contains(fields[0])) max_dep_depth         = size_t                    (py_map[fields[0]].as_a<Int  >())           ;
			fields[0] = "max_error_lines"     ; if (py_map.contains(fields[0])) max_err_lines         = size_t                    (py_map[fields[0]].as_a<Int  >())           ;
			fields[0] = "network_delay"       ; if (py_map.contains(fields[0])) network_delay         = Time::Delay               (py_map[fields[0]].as_a<Float>())           ;
			fields[0] = "path_max"            ; if (py_map.contains(fields[0])) path_max              = size_t                    (py_map[fields[0]].as_a<Int  >())           ;
			fields[0] = "reliable_dirs"       ; if (py_map.contains(fields[0])) reliable_dirs         =                           +py_map[fields[0]]                          ;
			fields[0] = "rules_module"        ; if (py_map.contains(fields[0])) rules_module          =                            py_map[fields[0]].as_a<Str  >()            ;
			fields[0] = "sources_module"      ; if (py_map.contains(fields[0])) srcs_module           =                            py_map[fields[0]].as_a<Str  >()            ;
			//
			fields[0] = "link_support" ;
			if (py_map.contains(fields[0])) {
				Object const& py_lnk_support = py_map[fields[0]] ;
				if      (!py_lnk_support     ) lnk_support = LnkSupport::None                                ;
				else if (py_lnk_support==True) lnk_support = LnkSupport::Full                                ;
				else                           lnk_support = mk_enum<LnkSupport>(py_lnk_support.as_a<Str>()) ;
			}
			//
			fields[0] = "console" ;
			if (py_map.contains(fields[0])) {
				Dict const& py_console = py_map[fields[0]].as_a<Dict>() ;
				fields.emplace_back() ;
				fields[1] = "date_precision" ;
				if (py_console.contains(fields[1])) {
					Object const& py_date_prec = py_console[fields[1]] ;
					if (py_date_prec==None) console.date_prec = uint8_t(-1)                                    ;
					else                    console.date_prec = static_cast<uint8_t>(py_date_prec.as_a<Int>()) ;
				}
				fields[1] = "host_length" ;
				if (py_console.contains(fields[1])) {
					Object const& py_host_len = py_console[fields[1]] ;
					if (+py_host_len) console.host_len = static_cast<uint8_t>(py_host_len.as_a<Int>()) ;
				}
				fields[1] = "has_exec_time" ;
				if (py_console.contains(fields[1])) console.has_exec_time = +py_console[fields[1]] ;
				fields.pop_back() ;
			}
			//
			fields[0] = "backends" ;
			if (!py_map.contains(fields[0])) throw "not found"s ;
			Dict const& py_backends = py_map[fields[0]].as_a<Dict>() ;
			fields.emplace_back() ;
			fields[1] = "precisions" ;
			if (py_backends.contains(fields[1])) {
				Dict const&    py_precs = py_backends[fields[1]].as_a<Dict>() ;
				fields.emplace_back() ;
				for( StdRsrc r : All<StdRsrc> ) {
					fields[2] = snake(r) ;
					if (!py_precs.contains(fields[2])) continue ;
					unsigned long prec = py_precs[fields[2]].as_a<Int>() ;
					if (prec==0                ) continue ;
					if (!::has_single_bit(prec)) throw to_string(prec," is not a power of 2") ;
					if (prec==1                ) throw "must be 0 or at least 2"s             ;
					rsrc_digits[+r] = ::bit_width(prec)-1 ;                                                                     // number of kept digits
				}
				fields.pop_back() ;
			}
			for( BackendTag t : All<BackendTag> ) if (+t) {
				fields[1] = snake(t) ;
				Backends::Backend const* bbe = Backends::Backend::s_tab[+t] ;
				if (!bbe                            ) continue ;                                                                // not implemented
				if (!py_backends.contains(fields[1])) continue ;                                                                // not configured
				try                       { backends[+t] = Backend( py_backends[fields[1]].as_a<Dict>() ) ;                   }
				catch (::string const& e) { ::cerr<<"Warning : backend "<<fields[1]<<" could not be configured : "<<e<<endl ; }
			}
			fields.pop_back() ;
			//
			fields[0] = "caches" ;
			if (py_map.contains(fields[0])) {
				fields.emplace_back() ;
				for( auto const& [py_key,py_val] : py_map[fields[0]].as_a<Dict>() ) {
					fields[1] = py_key.as_a<Str>() ;
					caches[fields[1]] = Cache(py_val.as_a<Dict>()) ;
				}
				fields.pop_back() ;
			}
			//
			fields[0] = "colors" ;
			if (!py_map.contains(fields[0])) throw "not found"s ;
			Dict const& py_colors = py_map[fields[0]].as_a<Dict>() ;
			fields.emplace_back() ;
			for( Color c{1} ; c<All<Color> ; c++ ) {
				fields[1] = snake(c) ;
				if (!py_colors.contains(fields[1])) throw "not found"s ;
				Sequence const& py_c1 = py_colors[fields[1]].as_a<Sequence>() ;
				if (py_c1.size()!=2) throw to_string("size is ",py_c1.size(),"!=2") ;
				fields.emplace_back() ;
				for( bool r : {false,true} ) {
					fields[2] = r?"reverse":"normal" ;
					Sequence const& py_c2 = py_c1[r].as_a<Sequence>() ;
					if (py_c2.size()!=3) throw to_string("size is ",py_c2.size(),"!=3") ;
					fields.emplace_back() ;
					for( size_t rgb=0 ; rgb<3 ; rgb++ ) {
						fields[3] = ::string( &"rgb"[rgb] , 1 ) ;
						size_t cc = py_c2[rgb].as_a<Int>() ;
						if (cc>=256) throw to_string("color is ",cc,">=256") ;
						colors[+c][r][rgb] = py_c2[rgb].as_a<Int>() ;
					}
					fields.pop_back() ;
				}
				fields.pop_back() ;
			}
			fields.pop_back() ;
			//
			fields[0] = "trace" ;
			if (py_map.contains(fields[0])) {
				Dict const& py_trace = py_map[fields[0]].as_a<Dict>() ;
				fields.emplace_back() ;
				fields[1] = "size"     ; if (py_trace.contains(fields[1])) trace.sz     = from_string_with_units<0,size_t>(*py_trace[fields[1]].str()) ;
				fields[1] = "n_jobs"   ; if (py_trace.contains(fields[1])) trace.n_jobs = py_trace[fields[1]].as_a<Int>()                              ;
				fields[1] = "channels" ; if (py_trace.contains(fields[1])) {
					trace.channels = {} ;
					for( Object const& py_c : py_trace[fields[1]].as_a<Sequence>() ) trace.channels |= mk_enum<Channel>(py_c.as_a<Str>()) ;
				}
				fields.pop_back() ;
			}
			// do some adjustments
			for( BackendTag t : All<BackendTag> ) if (+t) {
				if (!backends[+t].configured         ) continue        ;
				if (!Backends::Backend::s_ready   (t)) continue        ;
				if (!Backends::Backend::s_is_local(t)) goto SeenRemote ;
			}
			reliable_dirs    = true ;                                                                                           // all backends are local, dirs are necessarily reliable
			console.host_len = 0    ;                                                                                           // host has no interest if all jobs are local
		SeenRemote : ;
		} catch(::string& e) {
			::string field = "config" ; for( ::string const& f : fields ) append_to_string(field,'.',f) ;
			e = to_string("while processing ",field," :\n",indent(e)) ;
			throw ;
		}
	}

	::string Config::pretty_str() const {
		OStringStream res ;
		//
		// clean
		//
		res << "clean :\n" ;
		/**/                       res << "\tdb_version      : " << db_version.major<<'.'<<db_version.minor <<'\n' ;
		if (+hash_algo           ) res << "\thash_algo       : " << snake(hash_algo  )                      <<'\n' ;
		/**/                       res << "\tlink_support    : " << snake(lnk_support)                      <<'\n' ;
		/**/                       res << "\tkey             : " << key                                     <<'\n' ;
		if (+user_local_admin_dir) res << "\tlocal_admin_dir : " << user_local_admin_dir                    <<'\n' ;
		//
		// static
		//
		res << "static :\n" ;
		/**/                             res << "\tdisk_date_precision : " << date_prec     .short_str() <<'\n' ;
		if (heartbeat     >Delay()     ) res << "\theartbeat           : " << heartbeat     .short_str() <<'\n' ;
		if (heartbeat_tick>Delay()     ) res << "\theartbeat_tick      : " << heartbeat_tick.short_str() <<'\n' ;
		if (max_dep_depth!=DepDepth(-1)) res << "\tmax_dep_depth       : " << size_t(max_dep_depth)      <<'\n' ;
		/**/                             res << "\tnetwork_delay       : " << network_delay .short_str() <<'\n' ;
		if (path_max!=size_t(-1)       ) res << "\tpath_max            : " << size_t(path_max     )      <<'\n' ;
		else                             res << "\tpath_max            : " <<        "<unlimited>"       <<'\n' ;
		if (+rules_module              ) res << "\trules_module        : " <<        rules_module        <<'\n' ;
		if (+srcs_module               ) res << "\tsources_module      : " <<        srcs_module         <<'\n' ;
		//
		if (+caches) {
			res << "\tcaches :\n" ;
			for( auto const& [key,cache] : caches ) {
				size_t w = 3 ;                                               // room for tag
				for( auto const& [k,v] : cache.dct ) w = ::max(w,k.size()) ;
				res <<"\t\t"<< key <<" :\n" ;
				/**/                                 res <<"\t\t\t"<< ::setw(w)<<"tag" <<" : "<< cache.tag <<'\n' ;
				for( auto const& [k,v] : cache.dct ) res <<"\t\t\t"<< ::setw(w)<<k     <<" : "<< v         <<'\n' ;
			}
		}
		//
		// dynamic
		//
		res << "dynamic :\n" ;
		res << "\tmax_error_lines : " << max_err_lines <<'\n' ;
		res << "\treliable_dirs   : " << reliable_dirs <<'\n' ;
		//
		res << "\tconsole :\n" ;
		if (console.date_prec!=uint8_t(-1)) res << "\t\tdate_precision : " << console.date_prec     <<'\n' ;
		if (console.host_len              ) res << "\t\thost_length    : " << console.host_len      <<'\n' ;
		/**/                                res << "\t\thas_exec_time  : " << console.has_exec_time <<'\n' ;
		//
		bool has_digits = false ; for( StdRsrc r : All<StdRsrc> ) { if (rsrc_digits[+r]) has_digits = true ; break ; }
		if (has_digits) {
			res << "\tresource precisions :\n" ;
			for( StdRsrc r : All<StdRsrc> ) if (rsrc_digits[+r]) res << to_string("\t\t",snake(r)," : ",1<<rsrc_digits[+r],'\n') ;
		}
		//
		res << "\tbackends :\n" ;
		for( BackendTag t : All<BackendTag> ) if (+t) {
			Backend           const& be  = backends[+t]                 ;
			Backends::Backend const* bbe = Backends::Backend::s_tab[+t] ;
			if (!bbe                          ) continue ;                   // not implemented
			if (!be.configured                ) continue ;                   // not configured
			if (!Backends::Backend::s_ready(t)) {
				res <<"\t\t"<< snake(t) <<" : "<< Backends::Backend::s_config_err(t) ;
				continue ;
			}
			res <<"\t\t"<< snake(t) <<'('<< (bbe->is_local()?"local":"remote") <<") :\n" ;
			::vmap_ss descr = bbe->descr() ;
			size_t w = 9 ;                                                   // room for interface
			for( auto const& [k,v] : be.dct ) w = ::max(w,k.size()) ;
			for( auto const& [k,v] : descr  ) w = ::max(w,k.size()) ;
			for( auto const& [k,v] : be.dct ) res <<"\t\t\t"<< ::setw(w)<<k <<" : "<< v <<'\n' ;
			for( auto const& [k,v] : descr  ) res <<"\t\t\t"<< ::setw(w)<<k <<" : "<< v <<'\n' ;
			if (+be.ifce)                     res <<"\t\t\t"<< indent<'\t'>(be.ifce,3)  <<'\n' ;
		}
		//
		if (trace!=TraceConfig()) {
			res << "\ttrace :\n" ;
			if (trace.sz      !=TraceConfig().sz      )   res << "\t\tsize     : " << trace.sz     << '\n' ;
			if (trace.n_jobs  !=TraceConfig().n_jobs  )   res << "\t\tn_jobs   : " << trace.n_jobs << '\n' ;
			if (trace.channels!=TraceConfig().channels) {
				/**/                                                   res << "\t\tchannels :" ;
				for( Channel c : All<Channel> ) if (trace.channels[c]) res <<' '<< snake(c)    ;
				/**/                                                   res << '\n'             ;
			}
		}
		//
		return res.str() ;
	}

	void Config::open(bool dynamic) {
		// dont trust user to provide a unique directory for each repo, so add a sub-dir that is garanteed unique
		// if not set by user, these dirs lies within the repo and are unique by nature
		//
		SWEAR(+key) ;                                                                    // ensure no init problem
		::string std_file = to_string(PrivateAdminDir,"/local_admin") ;
		if (!user_local_admin_dir) {
			local_admin_dir = ::move(std_file) ;
		} else {
			local_admin_dir = to_string(user_local_admin_dir,'/',key+"-la") ;
			::string lnk_target   = mk_rel( local_admin_dir , dir_name(std_file)+'/' ) ;
			if (read_lnk(std_file)!=lnk_target) {
				unlnk( std_file , true/*dir_ok*/ ) ;
				lnk  ( std_file , lnk_target     ) ;
			}
		}
		mk_dir(local_admin_dir,true/*unlnk_ok*/) ;
		//
		Backends::Backend::s_config(backends,dynamic) ;
		//
		if (dynamic) return ;
		//
		Caches::Cache::s_config(caches) ;
	}

	//
	// EngineClosure
	//

	::ostream& operator<<( ::ostream& os , EngineClosureGlobal const& ecg ) {
		return os << "Glb(" << ecg.proc <<')' ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosureReq const& ecr ) {
		/**/                       os << "Ecr(" << ecr.proc <<',' ;
		switch (ecr.proc) {
			case ReqProc::Debug  : // PER_CMD : format for tracing
			case ReqProc::Forget :
			case ReqProc::Mark   :
			case ReqProc::Make   :
			case ReqProc::Show   : os << ecr.in_fd  <<','<< ecr.out_fd <<','<< ecr.options <<','<< ecr.files ; break ;
			case ReqProc::Kill   : os << ecr.in_fd  <<','<< ecr.out_fd                                       ; break ;
			case ReqProc::Close  : os << ecr.req                                                             ; break ;
		DF}
		return                     os <<')' ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosureJobStart const& ecjs ) {
		/**/                     os << "Ecjs(" << ecjs.start   ;
		if (ecjs.report        ) os <<",report"                ;
		if (+ecjs.report_unlnks) os <<','<< ecjs.report_unlnks ;
		if (+ecjs.txt          ) os <<','<< ecjs.txt           ;
		if (+ecjs.msg          ) os <<','<< ecjs.msg           ;
		return                   os <<')'                      ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosureJobEtc const& ecje ) {
		const char* sep = "" ;
		/**/                os << "Ecje("       ;
		if ( ecje.report) { os <<      "report" ; sep = "," ; }
		if (+ecje.req   )   os <<sep<< ecje.req ;
		return              os <<')'            ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosureJobEnd const& ecje ) {
		/**/   os << "Ecje("        ;
		return os << ecje.end <<')' ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosureJob const& ecj ) {
		/**/                               os << "(" << ecj.proc <<','<< ecj.job_exec ;
		switch (ecj.proc) {
			case JobRpcProc::Start       : os << ecj.start ; break ;
			case JobRpcProc::ReportStart :
			case JobRpcProc::GiveUp      : os << ecj.etc   ; break ;
			case JobRpcProc::End         : os << ecj.end   ; break ;
		DF}
		return                             os <<')' ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosureJobMngt const& ecjm ) {
		/**/                               os << "JobMngt(" << ecjm.proc <<','<< ecjm.job_exec ;
		switch (ecjm.proc) {
			case JobMngtProc::LiveOut    : os <<','<< ecjm.txt.size() ; break ;
			case JobMngtProc::DepVerbose : os <<','<< ecjm.deps       ; break ;
			case JobMngtProc::ChkDeps    : os <<','<< ecjm.deps       ; break ;
		DF}
		return                             os << ')' ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosure const& ec ) {
		/**/                                    os << "EngineClosure(" << ec.kind <<',' ;
		switch (ec.kind) {
			case EngineClosure::Kind::Global  : os << ec.ecg  ; break ;
			case EngineClosure::Kind::Req     : os << ec.ecr  ; break ;
			case EngineClosure::Kind::Job     : os << ec.ecj  ; break ;
			case EngineClosure::Kind::JobMngt : os << ec.ecjm ; break ;
		DF}
		return                                  os << ')' ;
	}

	::vector<Node> EngineClosureReq::targets(::string const& startup_dir_s) const {
		SWEAR(!as_job()) ;
		RealPathEnv    rpe       { .lnk_support=g_config.lnk_support , .root_dir=*g_root_dir } ;
		RealPath       real_path { rpe }                                                       ;
		::vector<Node> targets   ; targets.reserve(files.size())                               ;                                     // typically, there is no bads
		::string       err_str   ;
		for( ::string const& target : files ) {
			RealPath::SolveReport rp = real_path.solve(target,true/*no_follow*/) ;                                                   // we may refer to a symbolic link
			if (rp.file_loc==FileLoc::Repo) { targets.emplace_back(rp.real) ;                                                      }
			else                            { append_to_string( err_str , _audit_indent(mk_rel(target,startup_dir_s),1) , '\n' ) ; }
		}
		//
		if (+err_str) throw to_string("files are outside repo :\n",err_str) ;
		return targets ;
	}

	Job EngineClosureReq::job(::string const& startup_dir_s) const {
		SWEAR(as_job()) ;
		Rule r ;
		if (options.flags[ReqFlag::Rule]) {
			::string const& rule_name = options.flag_args[+ReqFlag::Rule] ;
			auto it = Rule::s_by_name.find(rule_name) ;
			if (it!=Rule::s_by_name.end()) r = it->second ;
			else                           throw to_string("cannot find rule ",rule_name) ;
			Job j{r,files[0]} ;
			if (!j) throw to_string("cannot find job ",mk_rel(files[0],startup_dir_s)," using rule ",rule_name) ;
			return j ;
		}
		::vector<Job> candidates ;
		for( Rule r : Rule::s_lst() ) {
			if ( Job j{r,files[0]} ; +j ) candidates.push_back(j) ;
		}
		if (candidates.size()==1) return candidates[0] ;
		if (!candidates         ) throw to_string("cannot find job ",mk_rel(files[0],startup_dir_s)) ;
		//
		::string err_str = "several rules match, consider :\n" ;
		for( Job j : candidates ) append_to_string( err_str , _audit_indent(to_string( "lmake -R " , mk_shell_str(j->rule->name) , " -J " , files[0] ),1) , '\n' ) ;
		throw err_str ;
	}

}
