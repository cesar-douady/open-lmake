// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh"   // /!\ must be included first as Python.h must be included first
#include "disk.hh"

#include "cache_utils.hh"
#include "engine.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Py   ;
using namespace Time ;

CacheConfig g_cache_config ;
DiskSz      g_reserved_sz  = 0                            ;
::string    g_store_dir_s  = PRIVATE_ADMIN_DIR_S "store/" ;
FileSync    g_file_sync    = {}/*garbage*/                ;

CkeyFile      _g_key_file       ;
CjobNameFile  _g_job_name_file  ;
CnodeNameFile _g_node_name_file ;
CjobFile      _g_job_file       ;
CrunFile      _g_run_file       ;
CnodeFile     _g_node_file      ;
CnodesFile    _g_nodes_file     ;
CcrcsFile     _g_crcs_file      ;

// in order to take into account exe time relative to target size, several LRU lists are stored
// each bucket correspond to a give target_size/exe_time (a rate in B/s) with a margin of ~5% to avoid too many buckets
// aging for entries in different buckets are proportional to its associated rate to minimize overall cpu in case of miss due to victimization
// higher rates means easier to recompute, so vicimitization is favored
// to avoid searching all buckets for each victimization, a sorted tab is maintained, but order changes with time, so it is regularly refreshed (at most once every second)

struct RateCmp {
	// statics
	static void s_init() {
		bool first_seen = false ;
		s_lrus = CrunData::s_hdr().lrus ;
		for( Rate r : iota(NRates) ) {
			s_rates[r] = from_rate( g_cache_config , r ) ;
			if (+s_lrus[r]) {
				if (!first_seen) { s_iota.bounds[0] = r   ; first_seen = true ; }
				/**/               s_iota.bounds[1] = r+1 ;
			}
		}
		s_refresh() ;
	}
	static float s_score(Rate r) {
		return float(s_now-s_lrus[r].newer/*oldest*/->last_access) * s_rates[r] ;
	}
	static Pdate s_stable( Rate a , Rate b ) {                                                                                                 // date until which lru_cmp is stable
		float delta_score = s_score(a) - s_score(b) ; if (  delta_score==0                    ) return Pdate::Future                         ; // ordered by rates in that case
		float delta_rate  = s_rates[a] - s_rates[b] ; if ( (delta_score> 0) == (delta_rate>0) ) return Pdate::Future                         ;
		/**/                                                                                    return s_now - Delay(delta_score/delta_rate) ;
	}
	static void s_refresh() {
		Pdate now { New } ;
		if (now<=s_limit       ) return ;        // order is still valid, nothing to do
		if (now<=s_now+Delay(1)) return ;        // ensure s_tab is not refreshed more than every second (as it is expensive) (at the expense of less precise bucket selection)
		//
		s_tab.clear() ;
		s_now   = now           ;
		s_limit = Pdate::Future ;
		//
		for( Rate r : s_iota ) if (+s_lrus[r]) s_tab.insert(r) ;
		//
		::optional<Rate> prev_r ;
		for( Rate r : s_tab ) {
			if (+prev_r) s_limit = ::min( s_limit , s_stable(r,*prev_r) ) ;
			prev_r = r ;
		}
	}
	static void s_insert(Rate r) {
		auto it = s_tab.insert(r).first ;
		/**/                    if (it !=s_tab.begin()) { auto it1 = it ; it1-- ; s_limit = ::min( s_limit , s_stable( *it1 , r    ) ) ; }
		auto it2 = it ; it2++ ; if (it2!=s_tab.end  ())                           s_limit = ::min( s_limit , s_stable( r    , *it2 ) ) ;
	}
	static void s_chk() {
		::set<Rate> rates = mk_set(s_tab) ;
		for( Rate r : iota(NRates) )
			if (+s_lrus[r]) {
				throw_unless( r>=s_iota.bounds[0] , "rate ",r," below lower bound " ,s_iota.bounds[0]," has newest ",s_lrus[r].older/*newest*/," and oldest ",s_lrus[r].newer/*oldest*/ ) ;
				throw_unless( r< s_iota.bounds[1] , "rate ",r," above higher bound ",s_iota.bounds[1]," has newest ",s_lrus[r].older/*newest*/," and oldest ",s_lrus[r].newer/*oldest*/ ) ;
				throw_unless( s_tab.contains(r)   , "rate ",r," not in s_tab"                        ," has newest ",s_lrus[r].older/*newest*/," and oldest ",s_lrus[r].newer/*oldest*/ ) ;
			} else {
				throw_unless( !rates.contains(r)  , "rate ",r," in s_tab"                            ," has no run"                                                                     ) ;
			}
	}
	// static data
	static Pdate               s_now           ; // date at which _g_lru_tab is sorted, g_lru_tab must be refreshed when modified
	static Pdate               s_limit         ; // date until which _g_lru_tab order is stable
	static Iota2<Rate>         s_iota          ; // range of rates that may have entries
	static float               s_rates[NRates] ; // actual rates in B/s per bucket
	static LruEntry*           s_lrus          ; // CrunData::s_hdr().lrus
	static ::set<Rate,RateCmp> s_tab           ; // rates that have entries, ordered by decreasing score (which change as time pass)
	// services
	bool operator()( Rate a , Rate b ) const {                                 // XXX/ : cannot be a static function with gcc-11
		return ::pair(s_score(a),s_rates[a]) > ::pair(s_score(b),s_rates[b]) ; // default to s_rates order so as to improve s_stable()
	}
} ;
Pdate               RateCmp::s_now           ;
Pdate               RateCmp::s_limit         ;
Iota2<Rate>         RateCmp::s_iota          ;
float               RateCmp::s_rates[NRates] = {}      ;
LruEntry*           RateCmp::s_lrus          = nullptr ;
::set<Rate,RateCmp> RateCmp::s_tab           ;

void cache_chk() {
	Trace trace("cache_chk") ;
	//
	_g_job_name_file .chk() ;
	_g_node_name_file.chk() ;
	_g_job_file      .chk() ;
	_g_run_file      .chk() ;
	_g_node_file     .chk() ;
	_g_nodes_file    .chk() ;
	_g_crcs_file     .chk() ;
	//
	CrunData::s_chk() ;
	RateCmp ::s_chk() ;
	//
	trace("done") ;
}

void cache_init( bool rescue , bool read_only ) {
	Trace trace("cache_init",STR(rescue),STR(read_only)) ;
	//
	try {
		::string             config_file = ADMIN_DIR_S "config.py"  ;
		AcFd                 config_fd   { config_file }            ;
		Gil                  gil         ;
		Ptr<Dict>            py_config   = py_run(config_fd.read()) ;
		::optional<::uset_s> all         ;
		for( auto const& [py_k,py_v] : *py_config )
			if (::string(py_k.as_a<Str>())=="__all__") {
				all = ::uset_s() ;
				for( Object& py_e : py_v.as_a<Sequence>() ) all->insert(py_e.as_a<Str>()) ;
				break ;
			}
		for( auto const& [k,v] : ::vmap_ss(*py_config) ) {
			try {
				if (+all) { if (!all->contains(k)) continue ; }                               // ignore private variables
				else      { if (k[0]=='_'        ) continue ; }                               // .
				CacheConfig& ccfg = g_cache_config ;
				switch (k[0]) {
					case 'd' : if (k=="domain_name"     ) { ccfg.domain_name      = ::move               (v) ;                                                                continue ; } break ;
					case 'f' : if (k=="file_sync"       ) { ccfg.file_sync        = mk_enum<FileSync>    (v) ;                                                                continue ; } break ;
					case 'm' : if (k=="max_rate"        ) { ccfg.max_rate         = from_string_with_unit(v) ; throw_unless( ccfg.max_rate        >0 , "must be positive" ) ; continue ; }
					/**/       if (k=="max_runs_per_job") { ccfg.max_runs_per_job = from_string<uint16_t>(v) ; throw_unless( ccfg.max_runs_per_job>0 , "must be positive" ) ; continue ; } break ;
					case 'p' : if (k=="perm"            ) { Fd::Stderr.write(cat("while configuring cache, perm is now automatic and deprecated : ",cwd_s(),rm_slash,'\n')) ; continue ; } break ;
					case 's' : if (k=="size"            ) { ccfg.max_sz           = from_string_with_unit(v) ;                                                                continue ; } break ;
				DN}
			} catch (::string const& e) {
				trace("bad_val",k,v) ;
				throw cat("wrong value (",e,") for entry ",k," : ",v) ;
			}
			trace("bad_cache_key",k) ;
			throw cat("wrong key (",k,") in ",config_file) ;
		}
		throw_unless( g_cache_config.max_sz , "size must be defined as non-zero" ) ;
		// auto-config permissions by analyzing dir permissions
		g_cache_config.umask = auto_umask( "." , "cache" ) ;
		try                       { g_file_sync = auto_file_sync(g_cache_config.file_sync) ;                                                            }
		catch (::string const& e) { throw cat("cannot auto-configure file_sync : ",e,"\n  consider setting file_sync to an adequate value in config") ; }
	} catch (::string const& e) {
		if (e.find('\n')==Npos) exit( Rc::Usage , "while configuring ",*g_exe_name," in dir ",*g_repo_root_s,rm_slash," : " ,              e  ) ;
		else                    exit( Rc::Usage , "while configuring ",*g_exe_name," in dir ",*g_repo_root_s,rm_slash," :\n",indent<' ',2>(e) ) ;
	}
	// record what has been understood from config
	CacheConfig ref_config        ;
	::string    sensed_config_str ;
	/**/                                                              sensed_config_str << "size             : "<<g_cache_config.max_sz          <<'\n' ;
	if (g_cache_config.max_rate        !=ref_config.max_rate        ) sensed_config_str << "max_rate         : "<<g_cache_config.max_rate        <<'\n' ;
	if (g_cache_config.max_runs_per_job!=ref_config.max_runs_per_job) sensed_config_str << "max_runs_per_job : "<<g_cache_config.max_runs_per_job<<'\n' ;
	if (g_cache_config.file_sync       !=ref_config.file_sync       ) sensed_config_str << "file_sync        : "<<g_cache_config.file_sync       <<'\n' ;
	try {
		::string sensed_config_file = ADMIN_DIR_S "config" ;
		unlnk( sensed_config_file                              )                            ; // in case it exists with insufficient perm
		AcFd ( sensed_config_file , {O_WRONLY|O_TRUNC|O_CREAT} ).write( sensed_config_str ) ;
	} catch (::string const&) {}                                                              // best effort
	//
	NfsGuard nfs_guard { g_file_sync } ;
	// START_OF_VERSIONING CACHE
	{ ::string file=g_store_dir_s+"key"       ; nfs_guard.access(file) ; _g_key_file      .init( file , !read_only ) ; }
	{ ::string file=g_store_dir_s+"job_name"  ; nfs_guard.access(file) ; _g_job_name_file .init( file , !read_only ) ; }
	{ ::string file=g_store_dir_s+"node_name" ; nfs_guard.access(file) ; _g_node_name_file.init( file , !read_only ) ; }
	{ ::string file=g_store_dir_s+"job"       ; nfs_guard.access(file) ; _g_job_file      .init( file , !read_only ) ; }
	{ ::string file=g_store_dir_s+"run"       ; nfs_guard.access(file) ; _g_run_file      .init( file , !read_only ) ; }
	{ ::string file=g_store_dir_s+"node"      ; nfs_guard.access(file) ; _g_node_file     .init( file , !read_only ) ; }
	{ ::string file=g_store_dir_s+"nodes"     ; nfs_guard.access(file) ; _g_nodes_file    .init( file , !read_only ) ; }
	{ ::string file=g_store_dir_s+"crcs"      ; nfs_guard.access(file) ; _g_crcs_file     .init( file , !read_only ) ; }
	// END_OF_VERSIONING
	if (rescue) {
		Fd::Stderr.write(cat("crash detected, check and rescueing cache ",cwd_s(),rm_slash,'\n')) ;
		CjobData ::s_rescue()                                                                     ;
		CnodeData::s_rescue()                                                                     ;
	}
	RateCmp::s_init() ;
	if (rescue) {
		cache_chk()                               ;
		Fd::Stderr.write("everything seems ok\n") ;
	}
	trace("done") ;
}

void cache_empty_trash() {
	Trace trace("cache_empty_trash",CjobData::s_trash.size(),CnodeData::s_trash.size()) ;
	CjobData ::s_empty_trash() ;
	CnodeData::s_empty_trash() ;
	trace("done") ;
}

void cache_finalize() {
	NfsGuard nfs_guard { g_file_sync } ;
	Trace trace("cache_finalize") ;
	nfs_guard.change(g_store_dir_s+"key"      ) ;
	nfs_guard.change(g_store_dir_s+"job_name" ) ;
	nfs_guard.change(g_store_dir_s+"node_name") ;
	nfs_guard.change(g_store_dir_s+"job"      ) ;
	nfs_guard.change(g_store_dir_s+"run"      ) ;
	nfs_guard.change(g_store_dir_s+"node"     ) ;
	nfs_guard.change(g_store_dir_s+"nodes"    ) ;
	nfs_guard.change(g_store_dir_s+"crcs"     ) ;
	trace("done") ;
}

void mk_room( DiskSz sz , Cjob keep_job ) {
	CrunHdr& hdr       = CrunData::s_hdr() ;
	NfsGuard nfs_guard { g_file_sync }     ;
	Trace trace("mk_room",sz,hdr.total_sz,g_reserved_sz) ;
	//
	if (g_reserved_sz+sz>g_cache_config.max_sz) {
		::string msg ;
		/**/               msg << "cache too small ("<<to_short_string_with_unit(g_cache_config.max_sz)<<"B)"         ;
		/**/               msg << " while needing "  <<to_short_string_with_unit(sz                   )<<'B'          ;
		if (g_reserved_sz) msg << " with "           <<to_short_string_with_unit(g_reserved_sz        )<<"B reserved" ;
		throw msg ;
	}
	RateCmp::s_refresh() ;
	while ( hdr.total_sz && hdr.total_sz+g_reserved_sz+sz>g_cache_config.max_sz ) {
		SWEAR( +RateCmp::s_tab ) ;                                                  // if total size is non-zero, we must have entries
		Rate best_rate = *RateCmp::s_tab.begin()                    ;
		Crun best_run  = RateCmp::s_lrus[best_rate].newer/*oldest*/ ;
		//
		if (best_run->victimize( best_run->job!=keep_job , &nfs_guard )) {
		}
	}
	trace("done",sz,hdr.total_sz) ;
}

// START_OF_NO_COV
void Ckey     ::operator>>(::string& os) const { os << "Ckey("      ; if (+self ) os << +self        ;                                  os << ')' ; }
void CjobName ::operator>>(::string& os) const { os << "CjobName("  ; if (+self ) os << +self        ;                                  os << ')' ; }
void CnodeName::operator>>(::string& os) const { os << "CnodeName(" ; if (+self ) os << +self        ;                                  os << ')' ; }
void Cjob     ::operator>>(::string& os) const { os << "CJ("        ; if (+self ) os << +self        ;                                  os << ')' ; }
void Crun     ::operator>>(::string& os) const { os << "CR("        ; if (+self ) os << +self        ;                                  os << ')' ; }
void Cnode    ::operator>>(::string& os) const { os << "CN("        ; if (+self ) os << +self        ;                                  os << ')' ; }
void LruEntry ::operator>>(::string& os) const { os << "LruEntry("  ; if (+newer) os <<"N:"<< +newer ; if (+older) os <<"O:"<< +older ; os << ')' ; }
// END_OF_NO_COV

//
// Ckey
//

Ckey::Ckey(           ::string const& name ) { self = _g_key_file.search(name) ; }
Ckey::Ckey( NewType , ::string const& name ) { self = _g_key_file.insert(name) ; }

void Ckey::victimize() {
	_g_key_file.pop(self) ;
}

//
// Cjob
//

Cjob::Cjob(::string const& name_) {
	CjobName jn = _g_job_name_file.search(name_) ; if (!jn) return ;
	self = _g_job_name_file.at(jn) ;
	SWEAR( +self , name_ ) ;
}

Cjob::Cjob( NewType , ::string const& name_ , VarIdx n_statics ) {
	CjobName jn = _g_job_name_file.insert(name_) ;
	Cjob&    j  = _g_job_name_file.at(jn)        ;
	if (!j) j = _g_job_file.emplace(jn,n_statics) ;
	else    SWEAR( j->n_statics==n_statics , name_,n_statics,j ) ;
	self = j ;
	SWEAR( +self , name_,n_statics ) ;
}

//
// Cnode
//

Cnode::Cnode(::string const& name_) {
	CnodeName nn = _g_node_name_file.search(name_) ; if (!nn) return ;
	self = _g_node_name_file.at(nn) ;
	SWEAR( +self , name_ ) ;
}

Cnode::Cnode( NewType , ::string const& name_ ) {
	CnodeName nn = _g_node_name_file.insert(name_) ;
	Cnode&    n  = _g_node_name_file.at(nn)        ;
	if (!n) n = _g_node_file.emplace(nn) ;
	self = n ;
	SWEAR( +self , name_ ) ;
}

//
// CompileDigest
//

void CompileDigest::operator>>(::string& os) const {              // START_OF_NO_COV
	First first ;
	/**/            os << "CompileDigest("                      ;
	if ( n_statics) os << first("",",")<<"NS:"<<n_statics       ;
	if (+deps     ) os << first("",",")<<"D:" <<deps    .size() ;
	if (+dep_crcs ) os << first("",",")<<"DC:"<<dep_crcs.size() ;
	/**/            os << ')'                                   ;
}                                                                 // END_OF_NO_COV

CompileDigest::~CompileDigest() {
	for( Cnode& d : deps ) d->dec() ;
}

CompileDigest::CompileDigest( ::vmap<StrId<CnodeIdx>,DepDigest> const& repo_deps , bool for_download , ::vector<CnodeIdx>* dep_ids ) {
	struct Dep {
		// services
		bool operator<(Dep const& other) const { return ::pair(bucket,+node) < ::pair(other.bucket,+other.node) ; }
		// data
		int   bucket = 0/*garbage*/ ;                                                                     // deps are sorted statics first, then existing, then non-existing
		Cnode node   ;
		Crc   crc    ;
	} ;
	::vector<Dep> deps_ ;
	Trace trace("compile",repo_deps.size(),STR(for_download),STR(bool(dep_ids))) ;
	for( auto& [n,dd] : repo_deps ) {
		bool      is_name = n.is_name()                                              ;
		CnodeIdx* dep_id  = is_name && dep_ids ? &dep_ids->emplace_back(0) : nullptr ;
		Accesses  a       = dd.accesses()                                            ;
		if      (!dd.dflags[Dflag::Full] )   a = {} ;                                                     // dep is used for resources only, no real accesses
		else if (!for_download           )   SWEAR_PROD( !dd.never_match()     , n,dd ) ;                 // meaningless, should not have reached here
		if      (dd.dflags[Dflag::Static]) { SWEAR_PROD( n_statics<Max<VarIdx>        ) ; n_statics++ ; } // n_statics must be data independent
		else if (!a                      ) { has_hidden = true ; continue ;                             } // dep was not accessed, ignore but keep static deps as they must not depend on run
		//
		Cnode node ;
		if (is_name) {
			if ( for_download          ) node    = {       n.name } ;
			else                         node    = { New , n.name } ;
			if ( dep_id                ) *dep_id = +node            ;
			if ( for_download && !node ) continue ;                                                       // if it is not known in cache, it has no impact on matching
		} else {
			node = {n.id} ;
		}
		//
		Crc crc = dd.crc() ;
		if (!for_download)                                                                                // Crc::Unknown means any existing file
			switch (+(a&Accesses(Access::Lnk,Access::Reg,Access::Stat))) {
				case +Accesses(                        ) :                      crc = +Crc::Unknown|CrcOrNone ; break ;
				case +Accesses(Access::Lnk             ) : if (!crc.is_lnk()  ) crc = +Crc::Reg    |CrcOrNone ; break ;
				case +Accesses(Access::Reg             ) : if (!crc.is_reg()  ) crc = +Crc::Lnk    |CrcOrNone ; break ;
				case +Accesses(            Access::Stat) : if ( crc!=Crc::None) crc =  Crc::Unknown           ; break ;
				case +Accesses(Access::Lnk,Access::Stat) : if ( crc.is_reg()  ) crc =  Crc::Reg               ; break ;
				case +Accesses(Access::Reg,Access::Stat) : if ( crc.is_lnk()  ) crc =  Crc::Lnk               ; break ;
			DN}
		// we lose 1 bit of crc but we must manage errors and it does not desserv an additional field
		if ( dd.err && a[Access::Err] ) crc = +crc |  CrcErr ;                                            // if not sensed, ignore error status (lmake would stop if not ignored anyway)
		else                            crc = +crc & ~CrcErr ;
		//
		deps_.push_back({
			.bucket = dd.dflags[Dflag::Static] ? 0 : crc!=Crc::None ? 1 : 2
		,	.node   = node
		,	.crc    = crc
		}) ;
	}
	::sort(deps_) ;
	for( Dep      & dep : deps_ ) { dep.node->inc() ;          deps    .push_back(dep.node) ; }
	for( Dep const& dep : deps_ ) { if (dep.bucket==2) break ; dep_crcs.push_back(dep.crc ) ; }
	trace("done",dep_ids?dep_ids->size():size_t(0)) ;
}

//
// LruEntry
//

bool/*first*/ LruEntry::insert_top( LruEntry& hdr , Crun run , LruEntry CrunData::* lru ) {
	SWEAR(+run) ;
	bool first = !hdr.older ;
	/**/       older                     = hdr.older/*newest*/ ;
	/**/       newer                     = {}                  ;
	if (first) hdr.newer/*oldest*/       = run                 ;
	else       ((*hdr.older).*lru).newer = run                 ;
	/**/       hdr.older/*newest*/       = run                 ;
	return first ;
}

bool/*last*/ LruEntry::erase( LruEntry& hdr , LruEntry CrunData::* lru ) {
	bool last = true ;
	if (+older) { ((*older).*lru).newer = newer ; last = false ; }
	else          hdr.newer/*oldest*/   = newer ;
	if (+newer) { ((*newer).*lru).older = older ; last = false ; }
	else          hdr.older/*newest*/   = older ;
	older = {} ;
	newer = {} ;
	return last ;
}

void LruEntry::mv_to_top( LruEntry& hdr , Crun run , LruEntry CrunData::* lru ) {
	if (!newer) return ;                                                          // fast path : nothing to do if already mru
	erase     ( hdr ,       lru ) ;
	insert_top( hdr , run , lru ) ;
}

void LruEntry::chk( LruEntry const& hdr , Crun run , LruEntry CrunData::* lru ) const {
	if (+newer) throw_unless( ((*newer).*lru).older          ==run , "for "       ,run,"newer=",newer," and newer.older=",((*newer).*lru).older           ) ;
	else        throw_unless( hdr            .older/*newest*/==run , "for newest ",run,               " newest="         ,hdr            .older/*newest*/ ) ;
	if (+older) throw_unless( ((*older).*lru).newer          ==run , "for "       ,run,"older=",older," and older.newer=",((*older).*lru).newer           ) ;
	else        throw_unless( hdr            .newer/*oldest*/==run , "for oldest ",run,               " oldest="         ,hdr            .newer/*oldest*/ ) ;
}

//
// CkeyData
//

void CkeyData::operator>>(::string& os) const { // START_OF_NO_COV
	/**/         os << "CkeyData(" ;
	if (ref_cnt) os << ref_cnt     ;
	/**/         os << ')'         ;
}                                               // END_OF_NO_COV

//
// CjobData
//

::vector<Cjob> CjobData::s_trash ;

void CjobData::s_empty_trash() {
	Cjob prev_job ;
	::sort(s_trash) ;                                 // simplify check for unicity
	for( Cjob j : s_trash ) {
		/**/              if (j==prev_job) continue ; //ensure each job is popped only once
		CjobData& jd=*j ; if (+jd        ) continue ; // job has been retrieved from trash
		_g_job_name_file.pop(jd._name) ;
		_g_job_file     .pop(j       ) ;
		prev_job = j ;
	}
	s_trash.clear() ;
}

void CjobData::s_rescue() {
	for( Cjob j : lst<Cjob>() ) {
		CjobData& jd=*j ; if (+jd) continue ; // job is valid
		_g_job_name_file.pop(jd._name) ;
		_g_job_file     .pop(j       ) ;
	}
}

void CjobData::operator>>(::string& os) const { // START_OF_NO_COV
	/**/      os << "CjobData(" ;
	if (+lru) os << +lru        ;
	/**/      os << ')'         ;
}                                               // END_OF_NO_COV

::pair<Crun,CacheHitInfo> CjobData::match( CompileDigest const& compile_digest ) {
	Trace trace("match",idx(),compile_digest) ;
	CacheHitInfo hit_info = CacheHitInfo::NoJob ;                                           // as long as we have seen no job
	for( Crun r=lru.older/*newest*/ ; +r ; r = r->job_lru.older ) {
		hit_info = r->match(compile_digest) ;
		if (hit_info<=CacheHitInfo::Hit ) { RateCmp::s_refresh() ; r->access()          ; }
		if (hit_info< CacheHitInfo::Miss) { trace(r,hit_info) ; return { r , hit_info } ; }
		hit_info = CacheHitInfo::BadDeps ;                                                  // we have seen a job, but it does not match
	}
	trace("miss") ;
	return { {} , hit_info } ;
}

bool/*done*/ CjobData::insert(
	CompileDigest const& compile_digest
,	Ckey key , KeyIsLast key_is_last , Time::Pdate last_access , Disk::DiskSz sz , Rate rate
,	::string const& reserved_file , NfsGuard* nfs_guard
) {
	Trace trace("insert",idx(),key,key_is_last,last_access,sz,rate,compile_digest) ;
	::array<Crun,2> found_runs ;                                                                                 // first and last with same key
	for( Crun r=lru.older/*newest*/ ; +r ; r = r->job_lru.older ) {
		CrunData& rd = *r ;
		if (rd.key==key) {
			SWEAR( !found_runs[rd.key_is_last] , r,found_runs[rd.key_is_last] ) ;
			found_runs[rd.key_is_last] = r ;
		}
		CacheHitInfo hit_info = rd.match( compile_digest ) ;
		if (hit_info<CacheHitInfo::Miss) {
			trace(r,hit_info) ;
			if (+reserved_file) unlnk_run( reserved_file , nfs_guard ) ;
			return false/*done*/ ;
		}
	}
	bool last     = false ;
	bool mk_first = false ;
	switch (key_is_last) { //!                                               last
		case KeyIsLast::No            :                                                                            break ;
		case KeyIsLast::OverrideFirst : mk_first = true ; last = +found_runs[true]                               ; break ;
		case KeyIsLast::Plain         : mk_first = true ; last = +found_runs[true] || +found_runs[false/*last*/] ; break ;
		case KeyIsLast::Yes           :                   last = true                                            ; break ;
	}
	if (+found_runs[last]) {
		if ( mk_first && last && !found_runs[false/*last*/] ) {
			::string last_name  = found_runs[last]->name() ; found_runs[last]->key_is_last = false ;
			::string first_name = found_runs[last]->name() ; rename_run( last_name , first_name , &::ref(NfsGuard(g_file_sync)) ) ;
		} else {
			found_runs[last]->victimize( false/*victimize_job*/ , nfs_guard ) ;
		}
	}
	while (n_runs>=g_cache_config.max_runs_per_job) lru.newer->victimize( false/*victimize_job*/ , nfs_guard ) ; // maybe several pass in case.max_runs_per_job has been reduced
	mk_room( sz , idx() ) ;
	Crun run { New , key , last , idx() , last_access , sz , rate , compile_digest } ;
	trace("miss",run,found_runs,STR(last)) ;
	if (+reserved_file) rename_run( reserved_file , run->name() , nfs_guard ) ;
	return true/*done*/ ;
}

void CjobData::victimize(NfsGuard* nfs_guard) {
	Trace trace("victimize",idx()) ;
	s_trash.push_back(idx()) ;
	::string n = with_slash(name()) ;
	try {
		rmdir_s( n , {.uphill=true,.nfs_guard=nfs_guard} ) ;
	} catch (::string const& e) {
		Trace trace("cannot_rmdir",n,e) ;
		exit( Rc::System , "cache error : ",e,"\n  consider : lcache_repair ",mk_shell_str(no_slash(cwd_s())) ) ;
	}
	trace("rmdir",n) ;
}

//
// CrunData
//

void CrunData::s_chk() {
	for( Crun r : lst<Crun>() ) r->chk() ;
	// check node ref_cnt
	::umap<Cnode,CnodeIdx> node_tab ;
	for( Crun  r : lst<Crun >() ) for( Cnode n : r->deps ) node_tab[n]++ ;
	for( Cnode n : lst<Cnode>() ) throw_unless( n->ref_cnt>=node_tab[n] , "bad ref cnt for ",n," : ",n->ref_cnt,"<",node_tab[n]) ; // there may be other temporary sources for ref_cnt
	// check key ref_cnt
	::umap<Ckey,CkeyIdx> key_tab ;
	for( Crun r : lst<Crun>() ) key_tab[r->key]++ ;
	for( Ckey k : lst<Ckey>() ) throw_unless( k->ref_cnt>=key_tab[k] , "bad ref cnt for ",k," : ",k->ref_cnt,"<",key_tab[k]) ;     // there might be other temporary sources for ref_cnt
}

CrunData::CrunData( Ckey k , bool kil , Cjob j , Pdate la , DiskSz sz_ , Rate r , CompileDigest const& compile_digest ) :
	last_access { la                        }
,	sz          { sz_                       }
,	job         { j                         }
,	deps        { compile_digest.deps       }
,	dep_crcs    { compile_digest.dep_crcs   }
,	key         { k                         }
,	rate        { r                         }
,	key_is_last { kil                       }
,	has_hidden  { compile_digest.has_hidden }
{
	CrunHdr& hdr = s_hdr() ;
	Trace trace("CrunData",key,STR(key_is_last),job,sz,rate,hdr.total_sz,compile_digest) ;
	bool first = !RateCmp::s_lrus[rate] ;
	hdr.total_sz += sz ;
	//
	if (first) RateCmp::s_refresh() ;
	//
	job_lru.insert_top( job->lru              , idx() , &CrunData::job_lru ) ;
	glb_lru.insert_top( RateCmp::s_lrus[rate] , idx() , &CrunData::glb_lru ) ;
	//
	if (first) {
		if (rate< RateCmp::s_iota.bounds[0]) RateCmp::s_iota.bounds[0] = rate   ;
		if (rate>=RateCmp::s_iota.bounds[1]) RateCmp::s_iota.bounds[1] = rate+1 ;
		RateCmp::s_insert(rate) ;
	}
	//
	/**/                                                         key.inc()     ;
	SWEAR( job->n_runs<g_cache_config.max_runs_per_job , job ) ; job->n_runs++ ;
	for( Cnode d : compile_digest.deps ) d->inc() ;
}

void CrunData::operator>>(::string& os) const { // START_OF_NO_COV
	/**/              os << "CrunData("<<key ;
	if (+key_is_last) os << ",last"          ;
	else              os << ",first"         ;
	/**/              os << ')'              ;
}                                               // END_OF_NO_COV

void CrunData::access() {
	Trace trace("access",idx(),rate) ;
	RateCmp::s_tab.erase(rate) ;                                              // erase from tab before manipulating glb_lru chain as lru is necessary for comparison
	job_lru.mv_to_top( job->lru              , idx() , &CrunData::job_lru ) ; // manage job-local LRU
	glb_lru.mv_to_top( RateCmp::s_lrus[rate] , idx() , &CrunData::glb_lru ) ; // manage global    LRU
	//
	last_access = New ;
	//
	RateCmp::s_insert(rate) ;                                                 // erase and re-insert is necessary when glb_lru chain is modified
}

bool/*job_victimized*/ CrunData::victimize( bool victimize_job , NfsGuard* nfs_guard ) {
	CrunHdr& hdr            = s_hdr() ;
	bool     job_victimized = false   ;
	Trace trace("victimize",idx(),STR(victimize_job),hdr.total_sz,sz) ;
	RateCmp::s_tab.erase(rate) ;                                              // erase from tab before pruning glb_lru chain as lru is necessary for comparison
	bool last = job_lru.erase( job->lru              , &CrunData::job_lru ) ; // manage job-local LRU
	/**/        glb_lru.erase( RateCmp::s_lrus[rate] , &CrunData::glb_lru ) ; // manage global    LRU
	//
	if (!RateCmp::s_lrus[rate].newer/*oldest*/) {
		while ( RateCmp::s_iota.bounds[0]<RateCmp::s_iota.bounds[1] && !RateCmp::s_lrus[RateCmp::s_iota.bounds[0]  ]) RateCmp::s_iota.bounds[0]++ ;
		while ( RateCmp::s_iota.bounds[0]<RateCmp::s_iota.bounds[1] && !RateCmp::s_lrus[RateCmp::s_iota.bounds[1]-1]) RateCmp::s_iota.bounds[1]-- ;
	} else {
		RateCmp::s_insert(rate) ;                                             // erase and re-insert is necessary when glb_lru chain is modified
	}
	//
	/**/                           key.dec()     ;
	SWEAR( job->n_runs>0 , job ) ; job->n_runs-- ;
	for( Cnode d : deps ) d->dec() ;
	//
	unlnk_run( name() , nfs_guard ) ;
	if ( victimize_job && last ) { trace("victimize_job",job) ; job->victimize(nfs_guard) ; job_victimized = true  ; }
	//
	SWEAR( hdr.total_sz >= sz , hdr.total_sz,sz,idx() ) ;
	hdr.total_sz -= sz ;
	_g_nodes_file.pop(deps    ) ;
	_g_crcs_file .pop(dep_crcs) ;
	_g_run_file  .pop(idx()   ) ;
	//
	return job_victimized ;
}

CacheHitInfo CrunData::match(CompileDigest const& compile_digest) const {
	VarIdx              n_statics        = job->n_statics                                                     ;
	CacheHitInfo        res              = has_hidden ? CacheHitInfo::HitHidden : CacheHitInfo::HitExhaustive ;
	::span<Cnode const> deps_view        = deps    .view()                                                    ;
	::span<Crc   const> dep_crcs_view    = dep_crcs.view()                                                    ;
	::vector<Cnode>     const& deps_     = compile_digest.deps                                                ;
	::vector<Hash::Crc> const& dep_crcs_ = compile_digest.dep_crcs                                            ;
	//
	Trace trace("match",idx(),compile_digest,n_statics,deps_.size(),dep_crcs_.size()) ;
	//
	SWEAR( n_statics<=dep_crcs_    .size() && dep_crcs_    .size()<=deps_    .size() , n_statics,deps_    ,dep_crcs_     ) ;
	SWEAR( n_statics<=dep_crcs_view.size() && dep_crcs_view.size()<=deps_view.size() , n_statics,deps_view,dep_crcs_view ) ;
	// first check static deps
	for( NodeIdx i : iota(n_statics) ) {
		SWEAR( deps_view[i]==deps_[i] , i,deps_,deps_view ) ;                                                                                                 // static deps only depend on job
		if (!crc_ok(dep_crcs_view[i],dep_crcs_[i])) { trace("miss1",i,deps_view[i],dep_crcs_view[i],dep_crcs_[i]) ; return CacheHitInfo::BadDeps ; }          // found with a different crc
	}
	NodeIdx j1 = n_statics        ;                                                                                                                           // index into provided deps, with    crc
	NodeIdx j2 = dep_crcs_.size() ;                                                                                                                           // index into provided deps, without crc
	// then search for exising deps
	for( NodeIdx i : iota(n_statics,dep_crcs_view.size()) ) {
		while ( j1<dep_crcs_.size() && +deps_[j1]< +deps_view[i] ) j1++ ;
		if    ( j1<dep_crcs_.size() &&  deps_[j1]== deps_view[i] ) {
			if (!crc_ok(dep_crcs_view[i],dep_crcs_[j1])) { trace("miss2",i,j1,deps_view[i],dep_crcs_view[i],dep_crcs_[j1]) ; return CacheHitInfo::BadDeps ; } // found with a different crc
			j1++ ;                                                                                                                                            // fast path : j1 is consumed
		} else {
			while ( j2<deps_.size() && +deps_[j2]< +deps_view[i] ) j2++ ;
			if    ( j2<deps_.size() &&  deps_[j2]== deps_view[i] ) {
				if (!crc_ok(dep_crcs_view[i],Crc::None)) { trace("miss3",i,j2,deps_view[i],dep_crcs_view[i]) ; return CacheHitInfo::BadDeps ; }               // found without crc while expecting one
				j2++ ;                                                                                                                                        // fast path : j1 is consumed
			} else if (res<=CacheHitInfo::Hit) { trace("match1",i,deps_view[i],j1,j2) ; res = CacheHitInfo::Match ; }                                         // not found
		}
	}
	// then search for non-existing deps
	if ( res<=CacheHitInfo::Hit && dep_crcs_.size()==dep_crcs_view.size() ) {                                                               // fast path : all existing deps are consumed
		SWEAR( j2==dep_crcs_.size() , j2,dep_crcs_.size() )  ;                                                                              // all existing deps were found existing
		for( NodeIdx i : iota(dep_crcs_view.size(),deps_view.size()) ) {
			while   ( j2<deps_.size() && +deps_[j2]< +deps_view[i] ) j2++ ;
			if      ( j2<deps_.size() &&  deps_[j2]== deps_view[i] ) j2++ ;                                                                 // fast path : j2 is consumed
			else if ( res<=CacheHitInfo::Hit                       ) { trace("match2",i,j1,j2,deps_view[i]) ; res = CacheHitInfo::Match ; } // not found
		}
	} else {
		j1 = n_statics        ;                                          // reset search as deps are ordered separately existing/non-existing
		j2 = dep_crcs_.size() ;                                          // .
		for( NodeIdx i : iota(dep_crcs_view.size(),deps_view.size()) ) {
			while ( j1<dep_crcs_.size() && +deps_[j1]< +deps_view[i] ) j1++ ;
			if    ( j1<dep_crcs_.size() &&  deps_[j1]== deps_view[i] ) {
				if (!crc_ok(Crc::None,dep_crcs_[j1])) { trace("miss4",i,j1,deps_view[i],dep_crcs_[j1]) ; return CacheHitInfo::BadDeps ; }       // found with crc while expecting none
				j1++ ;                                                                                                                          // fast path : j1 is consumed
			} else {
				while   ( j2<deps_.size() && +deps_[j2]< +deps_view[i] ) j2++ ;
				if      ( j2<deps_.size() &&  deps_[j2]== deps_view[i] ) j2++ ;                                                                 // fast path : j2 is consumed
				else if ( res<=CacheHitInfo::Hit                       ) { trace("match3",i,j1,j2,deps_view[i]) ; res = CacheHitInfo::Match ; } // not found
			}
		}
	}
	trace(res) ;
	return res ;
}

void CrunData::chk() const {
	if (!self) return ;
	job_lru.chk( job->lru              , idx() , &CrunData::job_lru ) ;
	glb_lru.chk( RateCmp::s_lrus[rate] , idx() , &CrunData::glb_lru ) ;
}

//
// CnodeData
//

::vector<Cnode> CnodeData::s_trash ;

void CnodeData::s_empty_trash() {
	Cnode prev_node ;
	::sort(s_trash) ;                                   // simplify check for unicity
	for( Cnode n : s_trash ) {
		/**/               if (n==prev_node) continue ; //ensure each node is popped only once
		CnodeData& nd=*n ; if (+nd         ) continue ; // node has been retrived from trash
		_g_node_name_file.pop(nd._name) ;
		_g_node_file     .pop(n       ) ;
		prev_node = n ;
	}
	s_trash.clear() ;
}

void CnodeData::s_rescue() {
	for( Cnode n : lst<Cnode>() ) {
		CnodeData& nd=*n ; if (+nd) continue ; // node is valid
		_g_node_name_file.pop(nd._name) ;
		_g_node_file     .pop(n       ) ;
	}
}

void CnodeData::operator>>(::string& os) const { // START_OF_NO_COV
	/**/         os << "CnodeData(" ;
	if (ref_cnt) os << ref_cnt      ;
	/**/         os << ')'          ;
}                                                // END_OF_NO_COV
