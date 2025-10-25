// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include "re.hh"

using namespace Caches ;
using namespace Disk   ;
using namespace Hash   ;
using namespace Py     ;
using namespace Re     ;
using namespace Time   ;

namespace Engine {

	::string& operator+=( ::string& os , Config::Backend const& be ) { // START_OF_NO_COV
		os << "Backend(" ;
		if (be.configured) {
			if (+be.ifce) os <<      be.ifce <<',' ;
			/**/          os <<      be.dct        ;
			if (+be.env ) os <<','<< be.env        ;
		}
		return os <<')' ;
	}                                                                  // END_OF_NO_COV

	::string& operator+=( ::string& os , ConfigStatic::Cache const& c ) { // START_OF_NO_COV
		return os << "Cache(" << c.tag <<','<< c.dct << ')' ;
	}                                                                     // END_OF_NO_COV

	::string& operator+=( ::string& os , Config const& sc ) {                                   // START_OF_NO_COV
		/**/                                          os << "Config(" << sc.lnk_support       ;
		if (sc.max_dep_depth       )                  os <<",MD" << sc.max_dep_depth          ;
		if (sc.max_err_lines       )                  os <<",EL" << sc.max_err_lines          ;
		if (sc.path_max!=size_t(-1))                  os <<",PM" << sc.path_max               ;
		if (+sc.caches             )                  os <<','   << sc.caches                 ;
		if (+sc.sub_repos_s        )                  os <<','   << sc.sub_repos_s            ;
		for( BackendTag t : iota(1,All<BackendTag>) ) os <<','   << t <<':'<< sc.backends[+t] ; // local backend is always present
		return os<<')' ;
	}                                                                                           // END_OF_NO_COV

	::string ConfigStatic::system_tag_val() const {
		if (!system_tag) return {} ;
		Gil gil ;
		return *py_run(system_tag)->get_item("system_tag").repr() ;
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

	ConfigDyn::Backend::Backend(Dict const& py_map) : configured{true} {
		::string field ;
		try {
			for( auto const& [py_k,py_v] : py_map ) {
				field = py_k.as_a<Str>() ;
				switch (field[0]) {
					case 'e' : if (field=="environ"  ) { { for( auto const& [py_k2,py_v2] : py_v.as_a<Dict>() ) env.emplace_back( py_k2.as_a<Str>() , *py_v2.str() ) ; } continue ; } break ;
					case 'i' : if (field=="interface") { ifce = *py_v.str() ;                                                                                            continue ; } break ;
				DN}
				dct.emplace_back( field , py_v==True ? "1"s : py_v==False ? "0"s : ::string(*py_v.str()) ) ;
			}
		} catch(::string const& e) {
			throw cat("while processing ",field,e) ;
		}
	}

	Config::Config(Dict const& py_map) : booted{true} {                                                                               // if config is read from makefiles, it is booted
		key = to_hex(random<uint64_t>()) ;
		//
		::vector_s fields = {{}} ;
		try {
			fields[0] = "disk_date_precision" ; if (py_map.contains(fields[0])) ddate_prec             = Time::Delay               (py_map[fields[0]].as_a<Float>())                  ;
			fields[0] = "local_admin_dir"     ; if (py_map.contains(fields[0])) user_local_admin_dir_s = with_slash                (py_map[fields[0]].as_a<Str  >())                  ;
			fields[0] = "heartbeat"           ; if (py_map.contains(fields[0])) heartbeat              = +py_map[fields[0]] ? Delay(py_map[fields[0]].as_a<Float>()) : Delay()        ;
			fields[0] = "heartbeat_tick"      ; if (py_map.contains(fields[0])) heartbeat_tick         = +py_map[fields[0]] ? Delay(py_map[fields[0]].as_a<Float>()) : Delay()        ;
			fields[0] = "max_dep_depth"       ; if (py_map.contains(fields[0])) max_dep_depth          = size_t                    (py_map[fields[0]].as_a<Int  >())                  ;
			fields[0] = "max_error_lines"     ; if (py_map.contains(fields[0])) max_err_lines          = size_t                    (py_map[fields[0]].as_a<Int  >())                  ;
			fields[0] = "network_delay"       ; if (py_map.contains(fields[0])) network_delay          = Time::Delay               (py_map[fields[0]].as_a<Float>())                  ;
			fields[0] = "nice"                ; if (py_map.contains(fields[0])) nice                   = uint8_t                   (py_map[fields[0]].as_a<Int  >())                  ;
			fields[0] = "system_tag"          ; if (py_map.contains(fields[0])) system_tag             = ensure_nl                 (py_map[fields[0]].as_a<Str  >())                  ;
			//
			fields[0] = "path_max" ;
			if (py_map.contains(fields[0])) {
				Object const& py_path_max = py_map[fields[0]] ;
				if (py_path_max==None) path_max = size_t(-1                     ) ;                                                   // deactivate
				else                   path_max = size_t(py_path_max.as_a<Int>()) ;
			}
			fields[0] = "link_support" ;
			if (py_map.contains(fields[0])) {
				Object const& py_lnk_support = py_map[fields[0]] ;
				if      (!py_lnk_support     ) lnk_support = LnkSupport::None                                ;
				else if (py_lnk_support==True) lnk_support = LnkSupport::Full                                ;
				else                           lnk_support = mk_enum<LnkSupport>(py_lnk_support.as_a<Str>()) ;
			}
			fields[0] = "reliable_dirs" ;
			if (py_map.contains(fields[0])) file_sync = +py_map[fields[0]] ? FileSync::None : FileSync::Dflt ;                        // XXX> : suppress when backward compatibility is no more required
			fields[0] = "file_sync" ;
			if (py_map.contains(fields[0])) {
				Object const& py_file_sync = py_map[fields[0]] ;
				if (!py_file_sync) file_sync = FileSync::None                              ;
				else               file_sync = mk_enum<FileSync>(py_file_sync.as_a<Str>()) ;
			}
			//
			fields[0] = "backends" ;
			throw_unless( py_map.contains(fields[0]) , "not found" ) ;
			Dict const& py_backends = py_map[fields[0]].as_a<Dict>() ;
			fields.emplace_back() ;
			for( BackendTag t : iota(1,All<BackendTag>) ) {                                                                           // local backend is always present
				fields[1] = snake(t) ;
				if (!Backends::Backend::s_tab[+t]   ) continue ;                                                                      // not implemented
				if (!py_backends.contains(fields[1])) continue ;                                                                      // not configured
				try                       { backends[+t] = Backend( py_backends[fields[1]].as_a<Dict>() ) ;                         }
				catch (::string const& e) { Fd::Stderr.write("Warning : backend "+fields[1]+" could not be configured : "+e+'\n') ; }
			}
			fields.pop_back() ;
			//
			fields[0] = "caches" ;
			if (py_map.contains(fields[0])) {
				fields.emplace_back() ;
				caches.resize(1) ;                                                                             // idx 0 is reserved to mean no cache
				for( auto const& [py_key,py_val] : py_map[fields[0]].as_a<Dict>() ) {
					fields[1] = py_key.as_a<Str>() ;
					cache_idxs[fields[1]] = caches.size() ;
					caches.emplace_back(py_val.as_a<Dict>()) ;
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
			fields[0] = "collect" ;
			if (py_map.contains(fields[0])) {
				Dict const&      py_collect = py_map[fields[0]].as_a<Dict>() ;
				::umap_s<VarIdx> stem_idxs  ;
				fields.emplace_back() ;
				fields[1] = "stems" ;
				if (py_collect.contains(fields[1])) {
					Dict const& py_stems = py_collect[fields[1]].as_a<Dict>() ;
					fields.emplace_back() ;
					for( auto const& [py_key,py_val] : py_stems ) {
						fields[2] = py_key.as_a<Str>() ;
						::string key = fields[2]          ;
						::string val = py_val.as_a<Str>() ;
						stem_idxs           .try_emplace ( key , collect.stems.size() ) ;
						collect.stems       .emplace_back( key , val                  ) ;
						collect.stem_n_marks.push_back   ( RegExpr(val).n_marks()     ) ;
					}
					fields.pop_back() ;
				}
				fields[1] = "ignore" ;
				if (py_collect.contains(fields[1])) {
					Dict const& py_ignore = py_collect[fields[1]].as_a<Dict>() ;
					fields.emplace_back() ;
					for( auto const& [py_key,py_val] : py_ignore ) {
						fields[2] = py_key.as_a<Str>() ;
						Ptr<Sequence> py_seq ;
						if (py_val.is_a<Str>()) py_seq = Ptr<Tuple>(py_val) ;
						else                    py_seq = &py_val            ;
						for( Object const& py_item : *py_seq ) {
							::string item       = py_item.as_a<Str>() ;
							bool     found_stem = false               ;
							::string target     ;
							parse_py( item , &::ref(size_t()) ,                                                // unused unnamed_star_idx, but passed to allow presence of unnamed star stems
								[&]( ::string const& k , bool /*star*/ , bool unnamed , ::string const* re ) {
									auto [it,inserted] = stem_idxs.try_emplace( k , collect.stems.size() ) ;
									if (!re) {
										throw_if    ( unnamed   , "unnamed stems must be defined in ",item ) ;
										throw_unless( !inserted , "found undefined stem ",k," in "   ,item ) ;
									} else {
										if (inserted) {
											collect.stems       .emplace_back( k , *re                ) ;
											collect.stem_n_marks.push_back   ( RegExpr(*re).n_marks() ) ;
										} else {
											throw_unless( *re==collect.stems[it->second].second , "2 different definitions for stem ",k," : ",collect.stems[it->second].second," and ",*re ) ;
										}
									}
									char* p = target.data()+target.size() ;
									target.resize(target.size()+1+sizeof(VarIdx)) ;
									p[0] = Rule::StemMrkr ;
									encode_int( p+1 , it->second ) ;
									found_stem = true ;
								}
							,	[&]( ::string const& fixed , bool has_pfx , bool has_sfx ) {
									if ( !is_canon( fixed , false/*ext_ok*/ , true/*empty_ok*/ , has_pfx , has_sfx ) ) {
										if ( ::string c=mk_canon(item) ; c!=item ) throw cat("is not canonical, consider using : ",c) ;
										else                                       throw cat("is not canonical"                     ) ;
									}
									target << fixed ;
								}
							) ;
							if (found_stem) collect.star_ignore  .emplace_back( fields[2] , target ) ;
							else            collect.static_ignore.emplace_back( fields[2] , target ) ;
						}
					}
					fields.pop_back() ;
				}
				fields.pop_back() ;
			}
			//
			fields[0] = "console" ;
			if (py_map.contains(fields[0])) {
				Dict const& py_console = py_map[fields[0]].as_a<Dict>() ;
				fields.emplace_back() ;
				fields[1] = "has_exec_time" ; if (py_console.contains(fields[1])) console.has_exec_time = +py_console[fields[1]] ;
				fields[1] = "show_eta"      ; if (py_console.contains(fields[1])) console.show_eta      = +py_console[fields[1]] ;
				fields[1] = "show_ete"      ; if (py_console.contains(fields[1])) console.show_ete      = +py_console[fields[1]] ;
				fields[1] = "date_precision" ;
				if (py_console.contains(fields[1])) {
					Object const& py_date_prec = py_console[fields[1]] ;
					if (py_date_prec==None)   console.date_prec = uint8_t(-1                      ) ;
					else                    { console.date_prec = uint8_t(py_date_prec.as_a<Int>()) ; throw_unless(console.date_prec<=9,"must be at most 9") ; }
				}
				fields[1] = "history_days" ;
				if (py_console.contains(fields[1])) {
					Object const& py_history_days = py_console[fields[1]] ;
					if (+py_history_days) console.history_days = static_cast<uint32_t>(py_history_days.as_a<Int>()) ;
				}
				fields[1] = "host_len" ;
				if (py_console.contains(fields[1])) {
					Object const& py_host_len = py_console[fields[1]] ;
					if (+py_host_len) console.host_len = static_cast<uint8_t>(py_host_len.as_a<Int>()) ;
				}
				//
				fields.pop_back() ;
			}
			//
			fields[0] = "debug" ;
			if (py_map.contains(fields[0])) {
				Dict const& py_debug = py_map[fields[0]].as_a<Dict>() ;
				fields.emplace_back() ;
				for( auto const& [py_key,py_val] : py_debug ) {
					fields[1] = py_key.as_a<Str>() ;
					dbg_tab[fields[1]] = py_val.as_a<Str>() ;
				}
				fields.pop_back() ;
			}
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
				fields[1] = "size"     ; if (py_trace.contains(fields[1])) trace.sz     = from_string_with_unit(*py_trace[fields[1]].str()) ;
				fields[1] = "n_jobs"   ; if (py_trace.contains(fields[1])) trace.n_jobs = py_trace[fields[1]].as_a<Int>()                   ;
				fields[1] = "channels" ; if (py_trace.contains(fields[1])) {
					trace.channels = {} ;
					for( Object const& py_c : py_trace[fields[1]].as_a<Sequence>() ) trace.channels |= mk_enum<Channel>(py_c.as_a<Str>()) ;
				}
				fields.pop_back() ;
			}
			// do some adjustments
			if ( ::none_of( iota(BackendTag::Remote,All<BackendTag>) , [&](BackendTag t) { return backends[+t].configured && Backends::Backend::s_ready(t) ; } ) ) {
				file_sync        = FileSync::None ;                                                                                    // no remote backend, filesystem is necessarily reliable
				console.host_len = 0              ;                                                                                    // host has no interest if all jobs are local
			}
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
		if (+system_tag                ) res << "\tsystem_tag :\n"         << indent(system_tag,2)              ;
		//
		if (+caches) {
			res << "\tcaches :\n" ;
			for( auto const& [k,idx] : cache_idxs )
				if (!idx) {
					res <<"\t\t"<<k<<"(unavailable)\n" ;
				} else {
					Cache const& cache = caches[idx] ;
					::string avail ;
					::map_ss descr = mk_map(cache.dct) ;
					size_t   w     = ::max<size_t>( descr , [](auto const& k_v) { return k_v.first.size() ; } , 3/*tag*/ ) ;
					if ( Caches::Cache* c = Caches::Cache::s_tab[idx] ) for( auto const& [k,v] : c->descr() ) { descr[k] = v ; w = ::max(w,k.size()) ; }
					else                                                avail = "(unvailable)" ;
					res <<"\t\t"<<k<<avail<<" :\n" ;
					/**/                             res <<"\t\t\t"<< widen("tag",w) <<" : "<< cache.tag <<'\n' ;
					for( auto const& [k,v] : descr ) res <<"\t\t\t"<< widen(k    ,w) <<" : "<< v         <<'\n' ;
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
		/**/               res << "\tfile_sync       : " << file_sync     <<'\n' ;
		if (max_err_lines) res << "\tmax_error_lines : " << max_err_lines <<'\n' ;
		if (nice         ) res << "\tnice            : " << size_t(nice)  <<'\n' ;
		//
		res << "\tbackends :\n" ;
		for( BackendTag t : iota(1,All<BackendTag>) ) {                         // local backend is always present
			Backend     const& be  = backends[+t]          ;
			auto const& bbe = Backends::Backend::s_tab[+t] ;
			if (!bbe                          ) continue ;                      // not implemented
			if (!be.configured                ) continue ;                      // not configured
			if (!Backends::Backend::s_ready(t)) {
				res <<"\t\t"<< t <<" : "<< Backends::Backend::s_config_err(t) << '\n' ;
				continue ;
			}
			res <<"\t\t"<< t <<" :\n" ;
			::vmap_ss descr = bbe->descr() ;
			size_t    w     = 0            ;
			if ( +be.ifce && be.ifce.find('\n')==Npos ) w = ::max(w,::strlen("interface")) ;
			/**/                                        w = ::max(w,::strlen("address"  )) ;
			for( auto const& [k,v] : be.dct )           w = ::max(w,k.size()             ) ;
			for( auto const& [k,v] : descr  )           w = ::max(w,k.size()             ) ;
			//
			if      ( !be.ifce                 ) {}
			else if ( be.ifce.find('\n')==Npos ) res <<"\t\t\t"<< widen("interface",w) <<" : " << be.ifce                       <<'\n' ;
			else                                 res <<"\t\t\t"<<       "interface"    <<" :\n"<< indent(be.ifce,4)             <<'\n' ;
			/**/                                 res <<"\t\t\t"<< widen("address"  ,w) <<" : " << SockFd::s_addr_str(bbe->addr) <<'\n' ;
			for( auto const& [k,v] : be.dct )    res <<"\t\t\t"<< widen(k          ,w) <<" : " << v                             <<'\n' ;
			for( auto const& [k,v] : descr  )    res <<"\t\t\t"<< widen(k          ,w) <<" : " << v                             <<'\n' ;
			if (+be.env) {
				res <<"\t\t\tenviron :\n" ;
				size_t w2 = ::max<size_t>( be.env , [](auto const& k_v) { return k_v.first.size() ; } ) ;
				for( auto const& [k,v] : be.env ) res <<"\t\t\t\t"<< widen(k,w2) <<" : "<< v <<'\n' ;
			}
		}
		//
		if (+collect) {
			res << "\tcollect :\n" ;
			if (+collect.stems) {
				res << "\t\tstems :\n" ;
				size_t w = ::max<size_t>( collect.stems , [](auto const& k_v) { return k_v.first.size() ; } ) ;
				for( auto const& [k,v] : collect.stems ) res <<"\t\t\t"<< widen(k,w) <<" : "<< v <<'\n' ;
			}
			size_t w = 0 ; for( auto const& [k,_] : collect.static_ignore ) w = ::max( w , k.size() ) ;
			/**/           for( auto const& [k,_] : collect.star_ignore   ) w = ::max( w , k.size() ) ;
			res << "\t\tignore :\n" ;
			for( auto const& [k,v] : collect.static_ignore )
				res <<"\t\t\t"<< widen(k,w) <<" : "<< py_fstr_escape(v) <<'\n' ;
			for( auto const& [k,v] : collect.star_ignore ) {
				::string p = subst_target(
					v
				,	[&](VarIdx s) { return cat('{',collect.stems[s].first,'}') ; }
				,	py_fstr_escape
				) ;
				res <<"\t\t\t"<< widen(k,w) <<" : "<< p <<'\n' ;
			}
		}
		//
		res << "\tconsole :\n" ;
		if (console.date_prec!=uint8_t(-1)) res << "\t\tdate_precision : " << console.date_prec     <<'\n' ;
		/**/                                res << "\t\thas_exec_time  : " << console.has_exec_time <<'\n' ;
		if (console.history_days          ) res << "\t\thistory_days   : " << console.history_days  <<'\n' ;
		if (console.host_len              ) res << "\t\thost_len       : " << console.host_len      <<'\n' ;
		if (console.show_eta              ) res << "\t\tshow_eta       : " << console.show_eta      <<'\n' ;
		if (console.show_ete              ) res << "\t\tshow_ete       : " << console.show_ete      <<'\n' ;
		//
		if (trace!=TraceConfig()) {
			res << "\ttrace :\n" ;
			if (trace.sz      !=TraceConfig().sz      ) res <<"\t\tsize     : "<< trace.sz     <<'\n' ;
			if (trace.n_jobs  !=TraceConfig().n_jobs  ) res <<"\t\tn_jobs   : "<< trace.n_jobs <<'\n' ;
			if (trace.channels!=TraceConfig().channels) {
				/**/                                                         res <<"\t\t"<< "channels :" ;
				for( Channel c : iota(All<Channel>) ) if (trace.channels[c]) res <<' '   << c            ;
				/**/                                                         res <<'\n'                  ;
			}
		}
		//
		return res ;
	}

	void Config::open( bool dyn , bool first_time ) {
		// dont trust user to provide a unique directory for each repo, so add a sub-dir that is garanteed unique
		// if not set by user, these dirs lies within the repo and are unique by nature
		//
		Trace trace("Config::open",STR(dyn),STR(first_time)) ;
		SWEAR(+key) ;                                               // ensure no init problem
		::string std_dir_s = cat(PrivateAdminDirS,"local_admin/") ;
		if (!user_local_admin_dir_s) {
			local_admin_dir_s = ::move(std_dir_s) ;
		} else {
			local_admin_dir_s = user_local_admin_dir_s+key+"-la/" ;
			::string lnk_target_s = mk_rel_s( local_admin_dir_s , dir_name_s(std_dir_s) ) ;
			if (read_lnk(no_slash(std_dir_s))!=no_slash(lnk_target_s)) {
				unlnk  ( no_slash(std_dir_s) , {.dir_ok=true}         ) ;
				sym_lnk( no_slash(std_dir_s) , no_slash(lnk_target_s) ) ;
			}
		}
		mk_dir_s( local_admin_dir_s , {.unlnk_ok=true} ) ;
		//
		Backends::Backend::s_config( backends , dyn , first_time ) ;
		//
		if (dyn) return ;
		//
		for( auto& [k,idx] : cache_idxs ) {
			Cache const& cache = caches[idx] ;
			try {
				Caches::Cache::s_config(idx,cache.tag,cache.dct) ;
			} catch (::string const& e) {
				idx = 0 ;
				Fd::Stderr.write(cat("ignore (cannot configure) cache ",k," : ",e,'\n')) ;
			}
		}
	}

}
