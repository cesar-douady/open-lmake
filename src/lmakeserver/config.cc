// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // must be first to include Python.h first

using namespace Disk ;
using namespace Hash ;
using namespace Py   ;
using namespace Time ;

namespace Engine {

	::string& operator+=( ::string& os , Config::Backend const& be ) {
		os << "Backend(" ;
		if (be.configured) {
			if (+be.ifce) os << be.ifce <<',' ;
			/**/          os << be.dct        ;
		}
		return os <<')' ;
	}

	::string& operator+=( ::string& os , ConfigStatic::Cache const& c ) {
		return os << "Cache(" << c.tag <<','<< c.dct << ')' ;
	}

	::string& operator+=( ::string& os , Config const& sc ) {
		/**/                                          os << "Config(" << sc.lnk_support       ;
		if (sc.max_dep_depth       )                  os <<",MD" << sc.max_dep_depth          ;
		if (sc.max_err_lines       )                  os <<",EL" << sc.max_err_lines          ;
		if (sc.path_max!=size_t(-1))                  os <<",PM" << sc.path_max               ;
		if (+sc.caches             )                  os <<','   << sc.caches                 ;
		if (+sc.sub_repos_s        )                  os <<','   << sc.sub_repos_s            ;
		for( BackendTag t : iota(1,All<BackendTag>) ) os <<','   << t <<':'<< sc.backends[+t] ; // local backend is always present
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
		throw_unless( found_tag , "tag not found" ) ;
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
			throw "while processing "+field+e ;
		}
	}

	Config::Config(Dict const& py_map) : booted{true} {                                                                                // if config is read from makefiles, it is booted
		// generate a random key
		::string buf_char = AcFd("/dev/urandom").read(false/*no_file_ok*/,sizeof(uint64_t)) ;
		uint64_t buf_int  ;                                                                   ::memcpy( &buf_int , buf_char.data() , sizeof(buf_int) ) ;
		key = to_hex(buf_int) ;
		//
		::vector_s fields = {{}} ;
		try {
			fields[0] = "disk_date_precision" ; if (py_map.contains(fields[0])) ddate_prec             = Time::Delay               (py_map[fields[0]].as_a<Float>())           ;
			fields[0] = "local_admin_dir"     ; if (py_map.contains(fields[0])) user_local_admin_dir_s = with_slash                (py_map[fields[0]].as_a<Str  >())           ;
			fields[0] = "heartbeat"           ; if (py_map.contains(fields[0])) heartbeat              = +py_map[fields[0]] ? Delay(py_map[fields[0]].as_a<Float>()) : Delay() ;
			fields[0] = "heartbeat_tick"      ; if (py_map.contains(fields[0])) heartbeat_tick         = +py_map[fields[0]] ? Delay(py_map[fields[0]].as_a<Float>()) : Delay() ;
			fields[0] = "max_dep_depth"       ; if (py_map.contains(fields[0])) max_dep_depth          = size_t                    (py_map[fields[0]].as_a<Int  >())           ;
			fields[0] = "max_error_lines"     ; if (py_map.contains(fields[0])) max_err_lines          = size_t                    (py_map[fields[0]].as_a<Int  >())           ;
			fields[0] = "network_delay"       ; if (py_map.contains(fields[0])) network_delay          = Time::Delay               (py_map[fields[0]].as_a<Float>())           ;
			fields[0] = "reliable_dirs"       ; if (py_map.contains(fields[0])) reliable_dirs          =                           +py_map[fields[0]]                          ;
			//
			fields[0] = "path_max" ;
			if (py_map.contains(fields[0])) {
				Object const& py_path_max = py_map[fields[0]] ;
				if (py_path_max==None) path_max = size_t(-1                     ) ; // deactivate
				else                   path_max = size_t(py_path_max.as_a<Int>()) ;
			}
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
					if (py_date_prec==None)   console.date_prec = uint8_t(-1                      ) ;
					else                    { console.date_prec = uint8_t(py_date_prec.as_a<Int>()) ; throw_unless(console.date_prec<=9,"must be at most 9") ; }
				}
				fields[1] = "has_exec_time" ;
				if (py_console.contains(fields[1])) console.has_exec_time = +py_console[fields[1]] ;
				fields[1] = "history_days" ;
				if (py_console.contains(fields[1])) {
					Object const& py_history_days = py_console[fields[1]] ;
					if (+py_history_days) console.history_days = static_cast<uint32_t>(py_history_days.as_a<Int>()) ;
				}
				fields[1] = "host_length" ;
				if (py_console.contains(fields[1])) {
					Object const& py_host_len = py_console[fields[1]] ;
					if (+py_host_len) console.host_len = static_cast<uint8_t>(py_host_len.as_a<Int>()) ;
				}
				fields[1] = "show_eta" ;
				if (py_console.contains(fields[1])) console.show_eta      = +py_console[fields[1]] ;
				fields.pop_back() ;
			}
			//
			fields[0] = "debug" ;
			if (py_map.contains(fields[0])) {
				fields.emplace_back() ;
				for( auto const& [py_key,py_val] : py_map[fields[0]].as_a<Dict>() ) {
					fields[1] = py_key.as_a<Str>() ;
					dbg_tab[fields[1]] = py_val.as_a<Str>() ;
				}
				fields.pop_back() ;
			}
			//
			fields[0] = "backends" ;
			throw_unless( py_map.contains(fields[0]) , "not found" ) ;
			Dict const& py_backends = py_map[fields[0]].as_a<Dict>() ;
			fields.emplace_back() ;
			fields[1] = "precisions" ;
			if (py_backends.contains(fields[1])) {
				Dict const&    py_precs = py_backends[fields[1]].as_a<Dict>() ;
				fields.emplace_back() ;
				for( StdRsrc r : iota(All<StdRsrc>) ) {
					fields[2] = snake(r) ;
					if (!py_precs.contains(fields[2])) continue ;
					ulong prec = py_precs[fields[2]].as_a<Int>() ;
					if (prec==0                ) continue ;
					throw_unless( ::has_single_bit(prec) , prec," is not a power of 2" ) ;
					throw_unless( prec!=1                , "must be 0 or at least 2"   ) ;
					rsrc_digits[+r] = ::bit_width(prec)-1 ;                                                                            // number of kept digits
				}
				fields.pop_back() ;
			}
			for( BackendTag t : iota(1,All<BackendTag>) ) {                                                                            // local backend is always present
				fields[1] = snake(t) ;
				Backends::Backend const* bbe = Backends::Backend::s_tab[+t] ;
				if (!bbe                            ) continue ;                                                                       // not implemented
				if (!py_backends.contains(fields[1])) continue ;                                                                       // not configured
				try                       { backends[+t] = Backend( py_backends[fields[1]].as_a<Dict>() ) ;                         }
				catch (::string const& e) { Fd::Stderr.write("Warning : backend "+fields[1]+" could not be configured : "+e+'\n') ; }
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
			throw_unless( py_map.contains(fields[0]) , "not found" ) ;
			Dict const& py_colors = py_map[fields[0]].as_a<Dict>() ;
			fields.emplace_back() ;
			for( Color c : iota(1,All<Color>) ) {
				fields[1] = snake(c) ;
				throw_unless( py_colors.contains(fields[1]) , "not found" ) ;
				Sequence const& py_c1 = py_colors[fields[1]].as_a<Sequence>() ;
				throw_unless( py_c1.size()==2 , "size is ",py_c1.size(),"!=2" ) ;
				fields.emplace_back() ;
				for( bool r : {false,true} ) {
					fields[2] = r?"reverse":"normal" ;
					Sequence const& py_c2 = py_c1[r].as_a<Sequence>() ;
					throw_unless( py_c2.size()==3 , "size is ",py_c2.size(),"!=3" ) ;
					fields.emplace_back() ;
					for( size_t rgb : iota(3) ) {
						fields[3] = ::string( &"rgb"[rgb] , 1 ) ;
						size_t cc = py_c2[rgb].as_a<Int>() ;
						throw_unless( cc<256 , "color is ",cc,">=256" ) ;
						colors[+c][r][rgb] = py_c2[rgb].as_a<Int>() ;
					}
					fields.pop_back() ;
				}
				fields.pop_back() ;
			}
			fields.pop_back() ;
			//
			fields[0] = "sub_repos" ;
			if (py_map.contains(fields[0])) {
				for( Object const& py_sr : py_map[fields[0]].as_a<Sequence>() ) sub_repos_s.push_back(with_slash(py_sr.as_a<Str>())) ;
				::sort(sub_repos_s) ;                                                                                                  // stabilize
			}
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
			for( BackendTag t : iota(1,All<BackendTag>) ) {                                                                            // local backend is not remote
				if (!backends[+t].configured         ) continue        ;
				if (!Backends::Backend::s_ready   (t)) continue        ;
				if (!Backends::Backend::s_is_local(t)) goto SeenRemote ;
			}
			reliable_dirs    = true ;                                                                                                  // all backends are local, dirs are necessarily reliable
			console.host_len = 0    ;                                                                                                  // host has no interest if all jobs are local
		SeenRemote : ;
		} catch(::string& e) {
			::string field = "config" ; for( ::string const& f : fields ) field<<'.'<<f ;
			e = "while processing "+field+" :\n"+indent(e) ;
			throw ;
		}
	}

	::string Config::pretty_str() const {
		::string res ;
		//
		// clean
		//
		res << "clean :\n" ;
		/**/                         res << "\tlink_support    : " << lnk_support                      <<'\n' ;
		/**/                         res << "\tkey             : " << key                              <<'\n' ;
		if (+user_local_admin_dir_s) res << "\tlocal_admin_dir : " << no_slash(user_local_admin_dir_s) <<'\n' ;
		//
		// static
		//
		res << "static :\n" ;
		/**/                             res << "\tdisk_date_precision : " << ddate_prec.short_str()     <<'\n' ;
		if (heartbeat     >Delay()     ) res << "\theartbeat           : " << heartbeat     .short_str() <<'\n' ;
		if (heartbeat_tick>Delay()     ) res << "\theartbeat_tick      : " << heartbeat_tick.short_str() <<'\n' ;
		if (max_dep_depth!=DepDepth(-1)) res << "\tmax_dep_depth       : " << size_t(max_dep_depth)      <<'\n' ;
		/**/                             res << "\tnetwork_delay       : " << network_delay .short_str() <<'\n' ;
		if (path_max!=size_t(-1)       ) res << "\tpath_max            : " << size_t(path_max     )      <<'\n' ;
		else                             res << "\tpath_max            : " <<        "<unlimited>"       <<'\n' ;
		//
		if (+caches) {
			res << "\tcaches :\n" ;
			for( auto const& [key,cache] : caches ) {
				size_t w = 3 ;                                               // room for tag
				for( auto const& [k,v] : cache.dct ) w = ::max(w,k.size()) ;
				res <<"\t\t"<< key <<" :\n" ;
				/**/                                 res <<"\t\t\t"<< widen("tag",w) <<" : "<< cache.tag <<'\n' ;
				for( auto const& [k,v] : cache.dct ) res <<"\t\t\t"<< widen(k    ,w) <<" : "<< v         <<'\n' ;
			}
		}
		if (+sub_repos_s) {
			res << "\tsub_repos :\n" ;
			for( ::string const& sr : sub_repos_s ) res <<"\t\t"<< no_slash(sr) <<'\n' ;
		}
		//
		// dynamic
		//
		/**/               res << "dynamic :\n"                                  ;
		if (max_err_lines) res << "\tmax_error_lines : " << max_err_lines <<'\n' ;
		/**/               res << "\treliable_dirs   : " << reliable_dirs <<'\n' ;
		//
		res << "\tconsole :\n" ;
		if (console.date_prec!=uint8_t(-1)) res << "\t\tdate_precision : " << console.date_prec     <<'\n' ;
		/**/                                res << "\t\thas_exec_time  : " << console.has_exec_time <<'\n' ;
		if (console.history_days          ) res << "\t\thistory_days   : " << console.history_days  <<'\n' ;
		if (console.host_len              ) res << "\t\thost_length    : " << console.host_len      <<'\n' ;
		/**/                                res << "\t\tshow_eta       : " << console.show_eta      <<'\n' ;
		//
		bool has_digits = false ; for( StdRsrc r : iota(All<StdRsrc>) ) { if (rsrc_digits[+r]) has_digits = true ; break ; }
		if (has_digits) {
			res << "\tresource precisions :\n" ;
			for( StdRsrc r : iota(All<StdRsrc>) ) if (rsrc_digits[+r]) res << "\t\t"<<r<<" : "<<(1<<rsrc_digits[+r])<<'\n' ;
		}
		//
		res << "\tbackends :\n" ;
		for( BackendTag t : iota(1,All<BackendTag>) ) {                      // local backend is always present
			Backend           const& be  = backends[+t]                 ;
			Backends::Backend const* bbe = Backends::Backend::s_tab[+t] ;
			if (!bbe                          ) continue ;                   // not implemented
			if (!be.configured                ) continue ;                   // not configured
			if (!Backends::Backend::s_ready(t)) {
				res <<"\t\t"<< t <<" : "<< Backends::Backend::s_config_err(t) << '\n' ;
				continue ;
			}
			res <<"\t\t"<< t <<'('<< (bbe->is_local()?"local":"remote") <<") :\n" ;
			::vmap_ss descr = bbe->descr()   ;
			size_t    w     = 4/*len(addr)*/ ;
			if ( !bbe->is_local() )           w = ::max(w,size_t(4)/*len(addr)*/) ;
			for( auto const& [k,v] : be.dct ) w = ::max(w,k.size()              ) ;
			for( auto const& [k,v] : descr  ) w = ::max(w,k.size()              ) ;
			if ( !bbe->is_local() )           res <<"\t\t\t"<< widen("addr",w) <<" : "<< SockFd::s_addr_str(bbe->addr) <<'\n' ;
			for( auto const& [k,v] : be.dct ) res <<"\t\t\t"<< widen(k     ,w) <<" : "<< v                             <<'\n' ;
			for( auto const& [k,v] : descr  ) res <<"\t\t\t"<< widen(k     ,w) <<" : "<< v                             <<'\n' ;
			if (+be.ifce)                     res <<indent<'\t'>(be.ifce,3)                                            <<'\n' ;
		}
		//
		if (trace!=TraceConfig()) {
			res << "\ttrace :\n" ;
			if (trace.sz      !=TraceConfig().sz      ) res << "\t\tsize     : " << trace.sz     << '\n' ;
			if (trace.n_jobs  !=TraceConfig().n_jobs  ) res << "\t\tn_jobs   : " << trace.n_jobs << '\n' ;
			if (trace.channels!=TraceConfig().channels) {
				/**/                                                         res <<"\t\t"<< "channels :" ;
				for( Channel c : iota(All<Channel>) ) if (trace.channels[c]) res <<' '   << c            ;
				/**/                                                         res <<'\n'                  ;
			}
		}
		//
		return res ;
	}

	void Config::open(bool dynamic) {
		// dont trust user to provide a unique directory for each repo, so add a sub-dir that is garanteed unique
		// if not set by user, these dirs lies within the repo and are unique by nature
		//
		SWEAR(+key) ;                                           // ensure no init problem
		::string std_dir_s = PrivateAdminDirS+"local_admin/"s ;
		if (!user_local_admin_dir_s) {
			local_admin_dir_s = ::move(std_dir_s) ;
		} else {
			local_admin_dir_s = user_local_admin_dir_s+key+"-la/" ;
			::string lnk_target_s = mk_rel( local_admin_dir_s , dir_name_s(std_dir_s) ) ;
			if (read_lnk(no_slash(std_dir_s))!=no_slash(lnk_target_s)) {
				unlnk( no_slash(std_dir_s) , true/*dir_ok*/         ) ;
				lnk  ( no_slash(std_dir_s) , no_slash(lnk_target_s) ) ;
			}
		}
		mk_dir_s(local_admin_dir_s,true/*unlnk_ok*/) ;
		//
		Backends::Backend::s_config(backends,dynamic) ;
		//
		if (dynamic) return ;
		//
		Caches::Cache::s_config(caches) ;
	}

}
