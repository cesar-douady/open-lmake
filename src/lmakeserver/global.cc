// This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh"

using namespace Disk ;

namespace Engine {

	ThreadQueue<EngineClosure> g_engine_queue ;

	void audit( Fd out_fd, ::ostream& trace , ReqOptions const& ro , Color c , DepDepth lvl , ::string const& pfx , ::string const& name , ::string const& sfx ) {
		::string report_txt = color_pfx(ro,c) ;
		::string trace_txt  ;
		//
		if (!pfx .empty()) {                                                                report_txt += pfx                                         ; trace_txt += pfx                 ; }
		if (!name.empty()) { if (!trace_txt.empty()) { report_txt+=' ' ; trace_txt+=' ' ; } report_txt += mk_printable(mk_rel(name,ro.startup_dir_s)) ; trace_txt += mk_printable(name ) ; }
		if (!sfx .empty()) { if (!trace_txt.empty()) { report_txt+=' ' ; trace_txt+=' ' ; } report_txt += sfx                                         ; trace_txt += sfx                 ; }
		//
		if (trace_txt.empty()     ) return ;
		if (trace_txt.back()=='\n') { report_txt.pop_back() ; trace_txt.pop_back() ; } // ensure color suffix is not at start-of-line to avoid indent adding space at end of report
		report_txt += color_sfx(ro,c) ;
		//
		if (lvl) { report_txt  = indent<' ',2>(report_txt,lvl) ; trace_txt  = indent<' ',2>(trace_txt,lvl) ; }
		/**/     { report_txt += '\n'                          ; trace_txt += '\n'                         ; }
		//
		try                     { OMsgBuf().send( out_fd , ReqRpcReply(report_txt) ) ; }
		catch (::string const&) {                                                      } // we lost connection with client, what can we do about it ? ignore
		trace << trace_txt << flush ;
	}

	//
	// Config
	//

	::ostream& operator<<( ::ostream& os , Config::Backend const& be ) {
		os << "Backend(" ;
		if (be.configured) {
			if (be.addr!=ServerSockFd::LoopBackAddr) os << ::hex<<be.addr<<::dec <<',' ;
			/**/                                     os << be.dct                      ;
		}
		return os <<')' ;
	}

	::ostream& operator<<( ::ostream& os , Config::Cache const& c ) {
		return os << "Cache(" << c.tag <<','<< c.dct << ')' ;
	}

	::ostream& operator<<( ::ostream& os , Config const& sc ) {
		using Tag = BackendTag ;
		os << "Config("
			/**/ << sc.db_version.major <<'.'<< sc.db_version.minor
			<<','<< sc.hash_algo
			<<','<< sc.lnk_support
		;
		if (sc.max_dep_depth      ) os <<','<< sc.max_dep_depth          ;
		if (sc.max_err_lines      ) os <<','<< sc.max_err_lines          ;
		if (sc.path_max           ) os <<','<< sc.path_max               ;
		if (sc.sub_prio_boost     ) os <<','<< sc.sub_prio_boost         ;
		if (!sc.caches.empty()    ) os <<','<< sc.caches                 ;
		for( Tag t : Tag::N       ) os <<','<< t <<':'<< sc.backends[+t] ;
		if (!sc.src_dirs_s.empty()) os <<','<< sc.src_dirs_s             ;
		return os<<')' ;
	}

	Config::Backend::Backend( Py::Mapping const& py_map , bool is_local ) : configured{true} {
		::string field   ;
		try {
			bool found_addr = false ;
			for( auto const& [k,v] : Py::Mapping(py_map) ) {
				field = Py::String(k) ;
				if (field=="interface") {
					if (is_local) throw "interface is not supported for local backends"s ;
					addr       = ServerSockFd::s_addr(Py::String(v)) ;
					found_addr = true                                ;
					continue ;
				}
				dct.emplace_back(field,v.str()) ;
			}
			field = "interface" ;
			if ( !found_addr && !is_local ) addr = ServerSockFd::s_addr(host()) ;
		} catch(::string const& e) {
			throw to_string("while processing ",field,e) ;
		}
	}

	Config::Cache::Cache(Py::Mapping const& py_map) {
		::string field     ;
		bool     found_tag = false ;
		for( auto const& [k,v] : Py::Mapping(py_map) ) {
			field = Py::String(k) ;
			if (field=="tag") { tag = mk_enum<Tag>(Py::String(v)) ; found_tag = true ; }
			else              { dct.emplace_back(field,v.str())   ;                    }
		}
		if (!found_tag) throw "tag not found"s ;
	}

	Config::Config(Py::Mapping const& py_map) {
		::string field ;
		try {
			field = "hash_algo"        ; if (py_map.hasKey(field)) hash_algo        = mk_enum<Hash::Algo>(Py::String(py_map[field])) ; else throw "not found"s ;
			field = "heartbeat"        ; if (py_map.hasKey(field)) heartbeat        = Time::Delay        (Py::Float (py_map[field])) ;
			field = "local_admin_dir"  ; if (py_map.hasKey(field)) local_admin_dir  =                     Py::String(py_map[field])  ; else local_admin_dir = AdminDir ;
			field = "max_dep_depth"    ; if (py_map.hasKey(field)) max_dep_depth    = size_t             (Py::Long  (py_map[field])) ; else throw "not found"s ;
			field = "max_error_lines"  ; if (py_map.hasKey(field)) max_err_lines    = size_t             (Py::Long  (py_map[field])) ;
			field = "network_delay"    ; if (py_map.hasKey(field)) network_delay    = Time::Delay        (Py::Float (py_map[field])) ;
			field = "remote_admin_dir" ; if (py_map.hasKey(field)) remote_admin_dir =                     Py::String(py_map[field])  ; else remote_admin_dir = AdminDir ;
			field = "remote_tmp_dir"   ; if (py_map.hasKey(field)) remote_tmp_dir   =                     Py::String(py_map[field])  ; else remote_tmp_dir   = AdminDir ;
			field = "trace_size"       ; if (py_map.hasKey(field)) trace_sz         = size_t             (Py::Long  (py_map[field])) ;
			field = "path_max"         ; if (py_map.hasKey(field)) path_max         = size_t             (Py::Long  (py_map[field])) ;
			field = "sub_prio_boost"   ; if (py_map.hasKey(field)) sub_prio_boost   = Prio               (Py::Float (py_map[field])) ;
			//
			field = "link_support" ;
			if (py_map.hasKey(field)) {
				Py::Object py_lnk_support = py_map[field] ;
				if      (py_lnk_support==Py::None()) lnk_support = LnkSupport::None                                          ;
				else if (py_lnk_support==Py::True()) lnk_support = LnkSupport::Full                                          ;
				else                                 lnk_support = mk_enum<LnkSupport>(::string(Py::String(py_lnk_support))) ;
			}
			//
			field = "console" ;
			if (!py_map.hasKey(field)) throw "not found"s ;
			Py::Mapping py_console = py_map[field] ;
			field = "console.date_precision" ;
			if (py_console.hasKey("date_precision")) {
				Py::Object py_date_prec = py_console["date_precision"] ;
				if (py_date_prec==Py::None()) console.date_prec = uint8_t(-1)                                        ;
				else                          console.date_prec = static_cast<unsigned long>(Py::Long(py_date_prec)) ;
			}
			field = "console.host_length" ;
			if (py_console.hasKey("host_length")) {
				Py::Object py_host_len  = py_console["host_length"] ;
				if (py_host_len==Py::None()) console.host_len = uint8_t(-1)                                       ;
				else                         console.host_len = static_cast<unsigned long>(Py::Long(py_host_len)) ;
			}
			field = "console.has_exec_time" ;
			if (py_console.hasKey("has_exec_time")) console.has_exec_time = Py::Object(py_console["has_exec_time"]).as_bool() ;
			//
			field = "backends" ;
			if (!py_map.hasKey(field)) throw "not found"s ;
			Py::Mapping py_backends = py_map[field] ;
			bool        found       = false         ;
			if (py_backends.hasKey("precisions")) {
				Save<::string> sav      { field , field+".precisions" }         ;
				Py::Mapping    py_precs = Py::Object(py_backends["precisions"]) ;
				for( StdRsrc r : StdRsrc::N ) {
					::string r_str = mk_snake(r) ;
					if (!py_precs.hasKey(r_str)) continue ;
					Save<::string> sav{field,to_string(field,'.',r_str)} ;
					unsigned long prec = static_cast<unsigned long>(Py::Long(py_precs[r_str])) ;
					if (prec==0                ) { rsrc_digits[+r]=0; continue;}
					if (!::has_single_bit(prec)) throw to_string(prec," is not a power of 2") ;
					if (prec==1                ) throw "must be 0 or at least 2"s             ;
					rsrc_digits[+r] = ::bit_width(prec)-1 ;                                   // number of kept digits
				}
			}
			for( BackendTag t : BackendTag::N ) {
				::string ts = mk_snake(t) ;
				Backends::Backend const* bbe = Backends::Backend::s_tab[+t] ;
				field = "backends."+ts ;
				if (!py_backends.hasKey(ts)) continue ;
				if ( !bbe                  ) continue ;                                                                // silently ignore as long as no rule uses it
				found = true ;
				if (py_backends.hasKey(ts)) backends[+t] = Backend( Py::Mapping(py_backends[ts]) , bbe->is_local() ) ;
			}
			if (!found) throw "no available backend has been configured"s ;
			//
			field = "caches" ;
			if (py_map.hasKey(field)) {
				for( auto const& [py_key,py_val] : Py::Mapping(py_map[field]) ) {
					::string key = Py::String(py_key) ;
					field = "caches."+key ;
					caches[key] = Cache(Py::Mapping(py_val)) ;
				}
			}
			field = "colors" ;
			if (!py_map.hasKey(field)) throw "not found"s ;
			Py::Mapping py_colors = py_map[field] ;
			for( Color c{1} ; c<Color::N ; c++ ) {
				::string cs = mk_snake(c) ;
				field = "colors."+cs ;
				if (!py_colors.hasKey(cs)) throw "not found"s ;
				Py::Sequence py_c1 = Py::Object(py_colors[cs]) ;
				if (py_c1.size()!=2) throw to_string("size is ",py_c1.size(),"!=2") ;
				for( bool r : {false,true} ) {
					field = to_string( "colors." , cs , r?".reverse":".normal" ) ;
					Py::Sequence py_c2 = Py::Object(py_c1[r]) ;
					if (py_c2.size()!=3) throw to_string("size is ",py_c2.size(),"!=3") ;
					for( size_t rgb=0 ; rgb<3 ; rgb++ ) {
						field = to_string( "colors." , cs , r?".reverse":".normal" , rgb==0?".r":rgb==1?".g":".b" ) ;
						size_t cc = size_t(Py::Long(py_c2[rgb])) ;
						if (cc>=256) throw to_string("color is ",cc,">=256") ;
						colors[+c][r][rgb] = size_t(Py::Long(py_c2[rgb])) ;
					}
				}
			}
			field = "source_dirs" ;
			if (py_map.hasKey(field)) {
				::string root_dir_s = *g_root_dir+'/'                                                                  ;
				RealPath rp         { { .lnk_support=LnkSupport::Full , .root_dir=*g_root_dir , .src_dirs_s{{"/"}} } } ;
				for( auto const& py_sd : Py::Sequence(py_map[field]) ) {
					::string sd = Py::String(py_sd) ;
					if (sd.empty()    ) throw "empty source dir"s ;
					if (sd.back()=='/') sd.pop_back() ;
					RealPath::SolveReport sr = rp.solve(sd) ;
					switch (sr.kind) {
						case Kind::Tmp   : throw to_string("source dir ",sd," cannot be in temporary dir") ;
						case Kind::Proc  : throw to_string("source dir ",sd," cannot be in /proc"        ) ;
						case Kind::Admin : throw to_string("source dir ",sd," cannot be in ",AdminDir    ) ;
						default : ;
					}
					sr.real += '/' ;
					if ( sr.kind!=Kind::Repo && !is_abs(sd) ) src_dirs_s.push_back(mk_rel(sr.real,root_dir_s)) ; // keep source dir relative if asked so
					else                                      src_dirs_s.push_back(::move(sr.real           )) ;
				}
			}
		} catch(::string& e) {
			e = to_string("while processing config.",field," :\n",indent(e)) ;
			throw ;
		} catch(Py::Exception& e) {
			throw to_string("while processing config.",field," :\n\t",e.errorValue()) ;
		}
	}

	::string Config::pretty_str() const {
		OStringStream res ;
		/**/          res << "db_version       : " << db_version.major<<'.'<<db_version.minor <<'\n' ;
		/**/          res << "heartbeat        : " <<          heartbeat    .short_str()      <<'\n' ;
		/**/          res << "hash_algo        : " << mk_snake(hash_algo    )                 <<'\n' ;
		/**/          res << "link_support     : " << mk_snake(lnk_support  )                 <<'\n' ;
		/**/          res << "local_admin_dir  : " <<          local_admin_dir                <<'\n' ;
		/**/          res << "max_dep_depth    : " << size_t  (max_dep_depth)                 <<'\n' ;
		/**/          res << "max_error_lines  : " <<          max_err_lines                  <<'\n' ;
		/**/          res << "network_delay    : " <<          network_delay.short_str()      <<'\n' ;
		if (path_max) res << "path_max         : " << size_t  (path_max     )                 <<'\n' ;
		else          res << "path_max         : " <<          "<unlimited>"                  <<'\n' ;
		/**/          res << "remote_admin_dir : " <<          remote_admin_dir               <<'\n' ;
		/**/          res << "remote_tmp_dir   : " <<          remote_tmp_dir                 <<'\n' ;
		res << "console :\n" ;
		if (console.date_prec==uint8_t(-1)) res << "\tdate_precision : <no date>\n"                      ;
		else                                res << "\tdate_precision : " << console.date_prec     <<'\n' ;
		if (console.host_len ==         0 ) res << "\thost_length    : <no host>\n"                      ;
		else                                res << "\thost_length    : " << console.host_len      <<'\n' ;
		/**/                                res << "\thas_exec_time  : " << console.has_exec_time <<'\n' ;
		res << "backends :\n" ;
		bool has_digits = false ; for( StdRsrc r : StdRsrc::N ) { if (rsrc_digits[+r]) has_digits = true ; break ; }
		if (has_digits) {
			res << "\tprecisions :\n" ;
			for( StdRsrc r : StdRsrc::N ) if (rsrc_digits[+r]) res << to_string("\t\t",mk_snake(r)," : ",1<<rsrc_digits[+r],'\n') ;
		}
		for( BackendTag t : BackendTag::N ) {
			Backend           const& be  = backends[+t]                 ;
			Backends::Backend const* bbe = Backends::Backend::s_tab[+t] ;
			if (!bbe          ) continue ;                                     // not implemented
			if (!be.configured) continue ;                                     // not configured
			size_t w  = 9 ;                                                    // room for interface
			for( auto const& [k,v] : be.dct ) w = ::max(w,k.size()) ;
			res <<'\t'<< mk_snake(t) <<" :\n" ;
			if (be.addr!=ServerSockFd::LoopBackAddr) res <<"\t\t"<< ::setw(w)<<"interface" <<" : "<< ServerSockFd::s_addr_str(be.addr) <<'\n' ;
			/**/                                     res <<"\t\t"<< ::setw(w)<<"local"     <<" : "<< bbe->is_local()                   <<'\n' ;
			for( auto const& [k,v] : be.dct )        res <<"\t\t"<< ::setw(w)<<k           <<" : "<< v                                 <<'\n' ;
		}
		if (!caches.empty()) {
			res << "caches :\n" ;
			for( auto const& [key,cache] : caches ) {
				size_t w = 3 ;                                                 // room for tag
				for( auto const& [k,v] : cache.dct ) w = ::max(w,k.size()) ;
				res <<'\t'<< key <<" :\n" ;
				/**/                                 res <<"\t\t"<< ::setw(w)<<"tag" <<" : "<< cache.tag <<'\n' ;
				for( auto const& [k,v] : cache.dct ) res <<"\t\t"<< ::setw(w)<<k     <<" : "<< v         <<'\n' ;
			}
		}
		if (!src_dirs_s.empty()) {
			res << "source_dirs :\n" ;
			for( ::string const& sd_s : src_dirs_s ) {
				SWEAR(!sd_s.empty()) ;
				res <<'\t'<< ::string_view(sd_s).substr(0,sd_s.size()-1) <<'\n' ;
			}
		}
		return res.str() ;
	}

	void Config::open() const {
		Backends::Backend::s_config(backends) ;
		Caches  ::Cache  ::s_config(caches  ) ;
	}

	//
	// EngineClosure
	//

	::ostream& operator<<( ::ostream& os , EngineClosure::Req const& ecr ) {
		os << "Req(" << ecr.proc <<',' ;
		switch (ecr.proc) {
			case ReqProc::Forget :
			case ReqProc::Freeze :
			case ReqProc::Make   :
			case ReqProc::Show   : os << ecr.in_fd  <<','<< ecr.out_fd <<','<< ecr.options <<','<< ecr.targets ; break ;
			case ReqProc::Kill   : os << ecr.in_fd  <<','<< ecr.out_fd                     ; break ;
			case ReqProc::Close  : os << ecr.req                                           ; break ;
			default : FAIL(ecr.proc) ;
		}
		return os << ')' ;
	}

	::ostream& operator<<( ::ostream& os , EngineClosure::Job const& ecj ) {
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

}
