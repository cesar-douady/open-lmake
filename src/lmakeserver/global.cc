// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

using namespace Disk ;
using namespace Hash ;

namespace Engine {

	ThreadQueue<EngineClosure> g_engine_queue ;

	static inline ::string _audit_indent( ::string const& txt , size_t lvl ) { return indent<' ',2>(txt,lvl) ; }

	void audit( Fd out_fd, ::ostream& log , ReqOptions const& ro , Color c , DepDepth lvl , ::string const& pfx , ::string const& name , ::string const& sfx ) {
		::string report_txt = color_pfx(ro,c) ;
		::string log_txt    ;
		//
		if (!pfx .empty()) {                                                            report_txt += pfx                                         ; log_txt += pfx                 ; }
		if (!name.empty()) { if (!log_txt.empty()) { report_txt+=' ' ; log_txt+=' ' ; } report_txt += mk_printable(mk_rel(name,ro.startup_dir_s)) ; log_txt += mk_printable(name ) ; }
		if (!sfx .empty()) { if (!log_txt.empty()) { report_txt+=' ' ; log_txt+=' ' ; } report_txt += sfx                                         ; log_txt += sfx                 ; }
		//
		if (log_txt.empty()     ) return ;
		if (log_txt.back()=='\n') { report_txt.pop_back() ; log_txt.pop_back() ; } // ensure color suffix is not at start-of-line to avoid indent adding space at end of report
		report_txt += color_sfx(ro,c) ;
		//
		if (lvl) { report_txt  = _audit_indent(report_txt,lvl) ; log_txt  = _audit_indent(log_txt,lvl) ; }
		/**/     { report_txt += '\n'                          ; log_txt += '\n'                       ; }
		// if we lose connection, there is nothing much we can do about it (hoping that we can still trace
		try { OMsgBuf().send( out_fd , ReqRpcReply(report_txt) ) ; } catch (::string const& e) { Trace("audit","lost_client",e,report_txt) ; }
		try { log << log_txt << ::flush ;                          } catch (::string const& e) { Trace("audit","lost_log"   ,e,log_txt   ) ; }
	}

	//
	// Config
	//

	::ostream& operator<<( ::ostream& os , Config::Backend const& be ) {
		os << "Backend(" ;
		if (be.configured) {
			if (be.addr!=NoSockAddr) os << ::hex<<be.addr<<::dec <<',' ;
			/**/                     os << be.dct                      ;
		}
		return os <<')' ;
	}

	::ostream& operator<<( ::ostream& os , ConfigStatic::Cache const& c ) {
		return os << "Cache(" << c.tag <<','<< c.dct << ')' ;
	}

	::ostream& operator<<( ::ostream& os , Config const& sc ) {
		using Tag = BackendTag ;
		os << "Config("
			/**/ << sc.db_version.major <<'.'<< sc.db_version.minor
			<<','<< sc.hash_algo
			<<','<< sc.lnk_support
		;
		if (sc.max_dep_depth       ) os <<",MD" << sc.max_dep_depth          ;
		if (sc.max_err_lines       ) os <<",EL" << sc.max_err_lines          ;
		if (sc.path_max!=size_t(-1)) os <<",PM" << sc.path_max               ;
		if (!sc.caches.empty()     ) os <<','   << sc.caches                 ;
		for( Tag t : Tag::N        ) os <<','   << t <<':'<< sc.backends[+t] ;
		return os<<')' ;
	}

	ConfigStatic::Cache::Cache(Py::Mapping const& py_map) {
		::string field     ;
		bool     found_tag = false ;
		for( auto const& [k,v] : Py::Mapping(py_map) ) {
			field = Py::String(k) ;
			if (field=="tag") { tag = mk_enum<Tag>(Py::String(v)) ; found_tag = true ; }
			else              { dct.emplace_back(field,v.str())   ;                    }
		}
		if (!found_tag) throw "tag not found"s ;
	}

	ConfigDynamic::Backend::Backend( Py::Mapping const& py_map , bool is_local ) : configured{true} {
		::string field ;
		try {
			bool found_addr = false ;
			for( auto const& [py_k,py_v] : Py::Mapping(py_map) ) {
				field = Py::String(py_k) ;
				::string v = py_v.ptr()==Py_True ? "1"s : py_v.ptr()==Py_False ? "0"s : ::string(py_v.str()) ;
				if (field=="interface") {
					if (is_local) throw "interface is not supported for local backends"s ;
					found_addr = true ;
					try {
						addr = ServerSockFd::s_addr(v) ;
						continue ;
					} catch (::string const&) {}                               // if we cannot interpret interface, leave decision to backend what to do
				}
				dct.emplace_back(field,v) ;
			}
			field = "interface" ;
			if ( !found_addr && !is_local ) addr = ServerSockFd::s_addr(host()) ;
		} catch(::string const& e) {
			throw to_string("while processing ",field,e) ;
		}
	}

	Config::Config(Py::Mapping const& py_map) : booted{true} {                 // if config is read from makefiles, it is booted
		db_version = Version::Db ;                                             // record current version
		::vector_s fields = {{}} ;
		try {
			fields[0] = "hash_algo"        ; if (py_map.hasKey(fields[0])) hash_algo             = mk_enum<Hash::Algo>                   (Py::String(py_map[fields[0]]))               ;
			fields[0] = "local_admin_dir"  ; if (py_map.hasKey(fields[0])) user_local_admin_dir  =                                        Py::String(py_map[fields[0]])                ;
			fields[0] = "heartbeat"        ; if (py_map.hasKey(fields[0])) heartbeat             = py_map[fields[0]].isTrue()?Time::Delay(Py::Float (py_map[fields[0]])):Time::Delay() ;
			fields[0] = "heartbeat_tick"   ; if (py_map.hasKey(fields[0])) heartbeat_tick        = py_map[fields[0]].isTrue()?Time::Delay(Py::Float (py_map[fields[0]])):Time::Delay() ;
			fields[0] = "max_dep_depth"    ; if (py_map.hasKey(fields[0])) max_dep_depth         = size_t                                (Py::Long  (py_map[fields[0]]))               ;
			fields[0] = "network_delay"    ; if (py_map.hasKey(fields[0])) network_delay         = Time::Delay                           (Py::Float (py_map[fields[0]]))               ;
			fields[0] = "path_max"         ; if (py_map.hasKey(fields[0])) path_max              = size_t                                (Py::Long  (py_map[fields[0]]))               ;
			fields[0] = "rules_module"     ; if (py_map.hasKey(fields[0])) rules_module          =                                        Py::String(py_map[fields[0]])                ;
			fields[0] = "sources_module"   ; if (py_map.hasKey(fields[0])) srcs_module           =                                        Py::String(py_map[fields[0]])                ;
			fields[0] = "max_error_lines"  ; if (py_map.hasKey(fields[0])) max_err_lines         = size_t                                (Py::Long  (py_map[fields[0]]))               ;
			fields[0] = "remote_admin_dir" ; if (py_map.hasKey(fields[0])) user_remote_admin_dir =                                        Py::String(py_map[fields[0]])                ;
			fields[0] = "remote_tmp_dir"   ; if (py_map.hasKey(fields[0])) user_remote_tmp_dir   =                                        Py::String(py_map[fields[0]])                ;
			//
			fields[0] = "link_support" ;
			if (py_map.hasKey(fields[0])) {
				Py::Object py_lnk_support = py_map[fields[0]] ;
				if      (!py_lnk_support.isTrue()  ) lnk_support = LnkSupport::None                                          ;
				else if (py_lnk_support==Py::True()) lnk_support = LnkSupport::Full                                          ;
				else                                 lnk_support = mk_enum<LnkSupport>(::string(Py::String(py_lnk_support))) ;
			}
			//
			fields[0] = "console" ;
			if (py_map.hasKey(fields[0])) {
				Py::Mapping py_console = py_map[fields[0]] ;
				fields.emplace_back() ;
				fields[1] = "date_precision" ;
				if (py_console.hasKey(fields[1])) {
					Py::Object py_date_prec = py_console[fields[1]] ;
					if (py_date_prec==Py::None()) console.date_prec = uint8_t(-1)                                        ;
					else                          console.date_prec = static_cast<unsigned long>(Py::Long(py_date_prec)) ;
				}
				fields[1] = "host_length" ;
				if (py_console.hasKey(fields[1])) {
					Py::Object py_host_len = py_console[fields[1]] ;
					if (py_host_len.isTrue()) console.host_len = static_cast<unsigned long>(Py::Long(py_host_len)) ;
				}
				fields[1] = "has_exec_time" ;
				if (py_console.hasKey(fields[1])) console.has_exec_time = Py::Object(py_console[fields[1]]).as_bool() ;
				fields.pop_back() ;
			}
			//
			fields[0] = "n_tokens_tab" ;
			if (py_map.hasKey(fields[0])) {
				Py::Mapping py_n_tokens_tab = py_map[fields[0]] ;
				for( auto const& [py_key,py_val] : py_n_tokens_tab ) {
					fields[1] = Py::String(py_key) ;
					size_t v = Py::Long(py_val) ;
					if (v) static_n_tokenss[fields[1]] = v ;                   // n_tokens cannot be zero as it is used as a divisor when computing rule ETA's
				}
			}
			//
			fields[0] = "backends" ;
			if (!py_map.hasKey(fields[0])) throw "not found"s ;
			Py::Mapping py_backends = py_map[fields[0]] ;
			fields.emplace_back() ;
			fields[1] = "precision" ;
			if (py_backends.hasKey(fields[1])) {
				Py::Mapping    py_precs = Py::Object(py_backends[fields[1]]) ;
				fields.emplace_back() ;
				for( StdRsrc r : StdRsrc::N ) {
					fields[2] = mk_snake(r) ;
					if (!py_precs.hasKey(fields[2])) continue ;
					unsigned long  prec = static_cast<unsigned long>(Py::Long(py_precs[fields[2]])) ;
					if (prec==0                ) continue ;
					if (!::has_single_bit(prec)) throw to_string(prec," is not a power of 2") ;
					if (prec==1                ) throw "must be 0 or at least 2"s             ;
					rsrc_digits[+r] = ::bit_width(prec)-1 ;                                     // number of kept digits
				}
				fields.pop_back() ;
			}
			for( BackendTag t : BackendTag::N ) {
				fields[1] = mk_snake(t) ;
				Backends::Backend const* bbe = Backends::Backend::s_tab[+t] ;
				if (!bbe                          ) continue ;                                                                  // not implemented
				if (!py_backends.hasKey(fields[1])) continue ;                                                                  // not configured
				try                       { backends[+t] = Backend( Py::Mapping(py_backends[fields[1]]) , bbe->is_local() ) ; }
				catch (::string const& e) { ::cerr<<"Warning : backend "<<fields[1]<<" could not be configured : "<<e<<endl ; }
			}
			fields.pop_back() ;
			//
			fields[0] = "caches" ;
			if (py_map.hasKey(fields[0])) {
				fields.emplace_back() ;
				for( auto const& [py_key,py_val] : Py::Mapping(py_map[fields[0]]) ) {
					fields[1] = Py::String(py_key) ;
					caches[fields[1]] = Cache(Py::Mapping(py_val)) ;
				}
				fields.pop_back() ;
			}
			//
			fields[0] = "colors" ;
			if (!py_map.hasKey(fields[0])) throw "not found"s ;
			Py::Mapping py_colors = py_map[fields[0]] ;
			fields.emplace_back() ;
			for( Color c{1} ; c<Color::N ; c++ ) {
				fields[1] = mk_snake(c) ;
				if (!py_colors.hasKey(fields[1])) throw "not found"s ;
				Py::Sequence py_c1 = Py::Object(py_colors[fields[1]]) ;
				if (py_c1.size()!=2) throw to_string("size is ",py_c1.size(),"!=2") ;
				fields.emplace_back() ;
				for( bool r : {false,true} ) {
					fields[2] = r?"reverse":"normal" ;
					Py::Sequence py_c2 = Py::Object(py_c1[r]) ;
					if (py_c2.size()!=3) throw to_string("size is ",py_c2.size(),"!=3") ;
					fields.emplace_back() ;
					for( size_t rgb=0 ; rgb<3 ; rgb++ ) {
						fields[3] = ::string( "rgb"+rgb , 1 ) ;
						size_t cc = size_t(Py::Long(py_c2[rgb])) ;
						if (cc>=256) throw to_string("color is ",cc,">=256") ;
						colors[+c][r][rgb] = size_t(Py::Long(py_c2[rgb])) ;
					}
					fields.pop_back() ;
				}
				fields.pop_back() ;
			}
			fields.pop_back() ;
			//
			fields[0] = "trace" ;
			if (py_map.hasKey(fields[0])) {
				Py::Mapping py_trace = py_map[fields[0]] ;
				fields.emplace_back() ;
				fields[1] = "size"     ; if (py_trace.hasKey(fields[1])) trace.sz     = from_string_with_units<0,size_t>(Py::Object(py_trace[fields[1]]).str()) ;
				fields[1] = "n_jobs"   ; if (py_trace.hasKey(fields[1])) trace.n_jobs = size_t(Py::Long(py_trace[fields[1]]))                                   ;
				fields[1] = "channels" ; if (py_trace.hasKey(fields[1])) {
					trace.channels = Channels() ;
					Py::Sequence py_cs = Py::Object(py_trace[fields[1]]) ;
					for( auto const& py_c : py_cs ) trace.channels |= mk_enum<Channel>(Py::String(py_c)) ;
				}
				fields.pop_back() ;
			}
		} catch(::string& e) {
			::string field = "config" ; for( ::string const& f : fields ) append_to_string(field,'.',f) ;
			e = to_string("while processing ",field," :\n",indent(e)) ;
			throw ;
		} catch(Py::Exception& e) {
			::string field = "config" ; for( ::string const& f : fields ) append_to_string(field,'.',f) ;
			throw to_string("while processing ",field," :\n\t",e.errorValue()) ;
		}
	}

	::string Config::pretty_str() const {
		OStringStream res ;
		//
		// clean
		//
		res << "clean :\n" ;
		/**/                                res << "\tdb_version      : " << db_version.major<<'.'<<db_version.minor <<'\n' ;
		if (hash_algo!=Algo::Xxh          ) res << "\thash_algo       : " << mk_snake(hash_algo    )                 <<'\n' ;
		/**/                                res << "\tlink_support    : " << mk_snake(lnk_support  )                 <<'\n' ;
		if (!user_local_admin_dir .empty()) res << "\tlocal_admin_dir : " << user_local_admin_dir                    <<'\n' ;
		//
		// static
		//
		res << "static :\n" ;
		if (heartbeat     >Delay()     ) res << "\theartbeat      : " << heartbeat     .short_str() <<'\n' ;
		if (heartbeat_tick>Delay()     ) res << "\theartbeat_tick : " << heartbeat_tick.short_str() <<'\n' ;
		if (max_dep_depth!=DepDepth(-1)) res << "\tmax_dep_depth  : " << size_t(max_dep_depth)      <<'\n' ;
		/**/                             res << "\tnetwork_delay  : " << network_delay .short_str() <<'\n' ;
		if (path_max!=size_t(-1)       ) res << "\tpath_max       : " << size_t(path_max     )      <<'\n' ;
		else                             res << "\tpath_max       : " <<        "<unlimited>"       <<'\n' ;
		if (!rules_module.empty()      ) res << "\trules_module   : " <<        rules_module        <<'\n' ;
		if (!srcs_module .empty()      ) res << "\tsources_module : " <<        srcs_module         <<'\n' ;
		//
		if (!caches.empty()) {
			res << "\tcaches :\n" ;
			for( auto const& [key,cache] : caches ) {
				size_t w = 3 ;                                                 // room for tag
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
		/**/                                res << "\tmax_error_lines  : " << max_err_lines             <<'\n' ;
		if (!user_remote_admin_dir.empty()) res << "\tremote_admin_dir : " << user_remote_admin_dir     <<'\n' ;
		if (!user_remote_tmp_dir  .empty()) res << "\tremote_tmp_dir   : " << user_remote_tmp_dir       <<'\n' ;
		//
		res << "\tconsole :\n" ;
		if (console.date_prec!=uint8_t(-1)) res << "\t\tdate_precision : " << console.date_prec     <<'\n' ;
		if (console.host_len              ) res << "\t\thost_length    : " << console.host_len      <<'\n' ;
		/**/                                res << "\t\thas_exec_time  : " << console.has_exec_time <<'\n' ;
		//
		bool has_digits = false ; for( StdRsrc r : StdRsrc::N ) { if (rsrc_digits[+r]) has_digits = true ; break ; }
		if (has_digits) {
			res << "\tresource precisions :\n" ;
			for( StdRsrc r : StdRsrc::N ) if (rsrc_digits[+r]) res << to_string("\t\t",mk_snake(r)," : ",1<<rsrc_digits[+r],'\n') ;
		}
		//
		res << "\tn_tokens :\n" ;
		size_t wk = 0 ;
		size_t wv = 0 ;
		for( ::map_s<size_t> const& tab : {static_n_tokenss,dyn_n_tokenss} ) {
			for( auto const& [k,v] : tab ) {
				wk = ::max( wk , k           .size() ) ;
				wv = ::max( wv , to_string(v).size() ) ;
			}
		}
		for( auto const& [k,v] : static_n_tokenss ) if (!dyn_n_tokenss.contains(k)) res << "\t\t" << ::setw(wk)<<k <<" : "<< ::right<<::setw(wv)<<v<<::left <<'\n' ;
		for( auto const& [k,v] : dyn_n_tokenss    )                                 res << "\t\t" << ::setw(wk)<<k <<" : "<< ::right<<::setw(wv)<<v<<::left <<'\n' ;
		//
		res << "\tbackends :\n" ;
		for( BackendTag t : BackendTag::N ) {
			Backend           const& be  = backends[+t]                 ;
			Backends::Backend const* bbe = Backends::Backend::s_tab[+t] ;
			if (!bbe                          ) continue ;                     // not implemented
			if (!be.configured                ) continue ;                     // not configured
			if (!Backends::Backend::s_ready(t)) {
				res <<"\t\t"<< mk_snake(t) <<" : "<< Backends::Backend::s_config_err(t) ;
				continue ;
			}
			res <<"\t\t"<< mk_snake(t) <<" :\n" ;
			size_t w  = 9 ;                                                    // room for interface
			for( auto const& [k,v] : be.dct ) w = ::max(w,k.size()) ;
			if (be.addr!=NoSockAddr)          res <<"\t\t\t"<< ::setw(w)<<"interface" <<" : "<< ServerSockFd::s_addr_str(be.addr) <<'\n' ;
			/**/                              res <<"\t\t\t"<< ::setw(w)<<"local"     <<" : "<< bbe->is_local()                   <<'\n' ;
			for( auto const& [k,v] : be.dct ) res <<"\t\t\t"<< ::setw(w)<<k           <<" : "<< v                                 <<'\n' ;
		}
		//
		if (trace!=TraceConfig()) {
			res << "\ttrace :\n" ;
			if (trace.sz      !=TraceConfig().sz      )   res << "\t\tsize     : " << trace.sz     ;
			if (trace.n_jobs  !=TraceConfig().n_jobs  )   res << "\t\tn_jobs   : " << trace.n_jobs ;
			if (trace.channels!=TraceConfig().channels) { res << "\t\tchannels :" ; for( Channel c : Channel::N ) if (trace.channels[c]) res <<' '<< mk_snake(c) ; }
		}
		//
		return res.str() ;
	}

	void Config::open(bool dynamic) {
		// dont trust user to provide a unique directory for each repo, so add a sub-dir that is garanteed unique
		// if not set by user, these dirs lies within the repo and are unique by nature
		static ::string repo_key ;
		if (repo_key.empty()) {
			Hash::Xxh key_hash ;
			key_hash.update(*g_root_dir) ;
			repo_key = '/' + ::string(::move(key_hash).digest()) ;
		}
		//
		if ( user_local_admin_dir .empty() ) local_admin_dir  = PrivateAdminDir+"/local_admin"s  ; else local_admin_dir  = user_local_admin_dir  + repo_key ;
		if ( user_remote_admin_dir.empty() ) remote_admin_dir = PrivateAdminDir+"/remote_admin"s ; else remote_admin_dir = user_remote_admin_dir + repo_key ;
		if ( user_remote_tmp_dir  .empty() ) remote_tmp_dir   = PrivateAdminDir+"/remote_tmp"s   ; else remote_tmp_dir   = user_remote_tmp_dir   + repo_key ;
		//
		Backends::Backend::s_config(backends,dynamic) ;
		dyn_n_tokenss.clear() ;
		for( BackendTag t : BackendTag::N )
			if (Backends::Backend::s_ready(t))
				for( auto const& [k,v] : Backends::Backend::s_n_tokenss(t) )
					if (v) dyn_n_tokenss[to_string(mk_snake(k),'.',k)] = v ;   // n_tokens cannot be zero as it is used as a divisor when computing rule ETA's
		//
		if (dynamic) return ;
		//
		Caches::Cache::s_config(caches) ;
		// check non-local backends have non-local addresses
		for( BackendTag t : BackendTag::N ) {
			if (!Backends::Backend::s_ready(t)) continue ;                     // backend is not supposed to be used
			Backend           const& be  = backends[+t]                 ;
			Backends::Backend const* bbe = Backends::Backend::s_tab[+t] ;
			if (bbe->is_local()    ) continue ;
			if (be.addr!=NoSockAddr) continue ;                                                              // backend has an address
			::string ifce ; for( auto const& [k,v] : be.dct ) { if (k=="interface") { ifce = v ; break ; } }
			::string ts   = mk_snake(t) ;
			if (ifce.empty()) throw to_string("cannot find host address. Consider adding in Lmakefile.py : lmake.config.backends.",ts,".interface = <something that works>") ;
			else              throw to_string("bad interface ",ifce," for backend ",ts) ;
		}
	}

	//
	// EngineClosure
	//

	::ostream& operator<<( ::ostream& os , EngineClosureReq const& ecr ) {
		os << "Req(" << ecr.proc <<',' ;
		switch (ecr.proc) {
			case ReqProc::Debug  :                                             // PER_CMD : format for tracing
			case ReqProc::Forget :
			case ReqProc::Mark   :
			case ReqProc::Make   :
			case ReqProc::Show   : os << ecr.in_fd  <<','<< ecr.out_fd <<','<< ecr.options <<','<< ecr.files ; break ;
			case ReqProc::Kill   : os << ecr.in_fd  <<','<< ecr.out_fd                                        ; break ;
			case ReqProc::Close  : os << ecr.req                                                              ; break ;
			default : FAIL(ecr.proc) ;
		}
		return os << ')' ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosureJob const& ecj ) {
		os << "Job(" << ecj.proc <<','<< ecj.exec ;
		switch (ecj.proc) {
			case JobProc::Start       : if (ecj.report) os <<",report" ; break ;
			case JobProc::LiveOut     : os <<','<< ecj.txt.size()      ; break ;
			case JobProc::Continue    : os <<','<< ecj.req             ; break ;
			case JobProc::ReportStart :                                  break ;
			case JobProc::NotStarted  :                                  break ;
			case JobProc::End         : os <<','<< ecj.digest          ; break ;
			case JobProc::ChkDeps     : os <<','<< ecj.digest.deps     ; break ;
			default : FAIL(ecj.proc) ;
		}
		return os << ')' ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosure const& ec ) {
		os << "EngineClosure(" << ec.kind <<',' ;
		switch (ec.kind) {
			case EngineClosure::Kind::Global : os << ec.global_proc ; break ;
			case EngineClosure::Kind::Job    : os << ec.job         ; break ;
			case EngineClosure::Kind::Req    : os << ec.req         ; break ;
			default : FAIL(ec.kind) ;
		}
		return os << ')' ;
	}

	::vector<Node> EngineClosureReq::targets(::string const& startup_dir_s) const {
		SWEAR(!as_job()) ;
		RealPath       real_path {{ .lnk_support=g_config.lnk_support , .root_dir=*g_root_dir }} ;
		::vector<Node> targets   ; targets.reserve(files.size()) ;                                 // typically, there is no bads
		::string       err_str   ;
		for( ::string const& target : files ) {
			RealPath::SolveReport rp = real_path.solve(target,true/*no_follow*/) ;                                    // we may refer to a symbolic link
			if (rp.kind==Kind::Repo) { targets.emplace_back(rp.real) ;                                              }
			else                     { err_str += _audit_indent(mk_rel(target,startup_dir_s),1) ; err_str += '\n' ; }
		}
		//
		if (err_str.empty()) return targets                                         ;
		else                 throw  to_string("files are outside repo :\n",err_str) ;
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
		//
		if (candidates.empty())
			throw to_string("cannot find job ",mk_rel(files[0],startup_dir_s)) ;
		::string err_str = "several rules match, consider :\n" ;
		for( Job j : candidates ) {
			err_str += _audit_indent(to_string( "lmake -R " , mk_shell_str(j->rule->name) , " -J " , files[0] ),1) ;
			err_str += '\n' ;
		}
		throw err_str ;
	}

}
