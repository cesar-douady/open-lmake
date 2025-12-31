// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be included first as Python.h must be included first

#include "daemon_cache_utils.hh"

#include "engine.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Py   ;
using namespace Time ;

DaemonCache::Config g_config ;

CjobNameFile  _g_job_name_file  ;
CnodeNameFile _g_node_name_file ;
CjobFile      _g_job_file       ;
CrunFile      _g_run_file       ;
CnodeFile     _g_node_file      ;
CnodesFile    _g_nodes_file     ;
CcrcsFile     _g_crcs_file      ;

static DiskSz _g_reserved_sz = 0 ;

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
			s_rates[r] = from_rate( g_config , r ) ;
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
	// static data
	static Pdate               s_now           ; // date at which _g_lru_tab is sorted, g_lru_tab must be refreshed when modified
	static Pdate               s_limit         ; // date until which _g_lru_tab order is stable
	static Iota2<Rate>         s_iota          ; // range of rates that may have entries
	static float               s_rates[NRates] ; // actual rates in B/s per bucket
	static LruEntry*           s_lrus          ; // CrunData::s_hdr().lrus
	static ::set<Rate,RateCmp> s_tab           ; // ordered by decreasing score (which change as time pass)
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

static void _daemon_cache_chk() {
	_g_job_name_file .chk() ;
	_g_node_name_file.chk() ;
	_g_job_file      .chk() ;
	_g_run_file      .chk() ;
	_g_node_file     .chk() ;
	_g_nodes_file    .chk() ;
	_g_crcs_file     .chk() ;
}

void daemon_cache_init( bool rescue , bool read_only ) {
	Trace trace("daemon_cache_init",STR(rescue),STR(read_only)) ;
	//
	// START_OF_VERSIONING DAEMON_CACHE
	::string dir_s     = DaemonCache::Config::s_store_dir_s() ;
	NfsGuard nfs_guard { g_config.file_sync }               ;
	{ ::string file=dir_s+"job_name"  ; nfs_guard.access(file) ; _g_job_name_file .init( file , !read_only ) ; }
	{ ::string file=dir_s+"node_name" ; nfs_guard.access(file) ; _g_node_name_file.init( file , !read_only ) ; }
	{ ::string file=dir_s+"job"       ; nfs_guard.access(file) ; _g_job_file      .init( file , !read_only ) ; }
	{ ::string file=dir_s+"run"       ; nfs_guard.access(file) ; _g_run_file      .init( file , !read_only ) ; }
	{ ::string file=dir_s+"node"      ; nfs_guard.access(file) ; _g_node_file     .init( file , !read_only ) ; }
	{ ::string file=dir_s+"nodes"     ; nfs_guard.access(file) ; _g_nodes_file    .init( file , !read_only ) ; }
	{ ::string file=dir_s+"crcs"      ; nfs_guard.access(file) ; _g_crcs_file     .init( file , !read_only ) ; }
	// END_OF_VERSIONING
	if (rescue) _daemon_cache_chk() ;
	RateCmp::s_init() ;
	trace("done") ;
}

void daemon_cache_finalize() {
	::string dir_s = cat(PrivateAdminDirS,"store/") ;
	NfsGuard nfs_guard { g_config.file_sync } ;
	nfs_guard.change(dir_s+"job_name" ) ;
	nfs_guard.change(dir_s+"node_name") ;
	nfs_guard.change(dir_s+"job"      ) ;
	nfs_guard.change(dir_s+"run"      ) ;
	nfs_guard.change(dir_s+"node"     ) ;
	nfs_guard.change(dir_s+"nodes"    ) ;
	nfs_guard.change(dir_s+"crcs"     ) ;
}

bool/*ok*/ mk_room(DiskSz sz) {
	CrunHdr& hdr = CrunData::s_hdr() ;
	Trace trace("mk_room",sz,hdr.total_sz,_g_reserved_sz) ;
	//
	if (_g_reserved_sz+sz>g_config.max_sz) { trace("not_done") ; return false/*ok*/ ; }
	//
	RateCmp::s_refresh() ;
	while ( hdr.total_sz && hdr.total_sz+_g_reserved_sz+sz>g_config.max_sz ) {
		SWEAR( +RateCmp::s_tab ) ;                                             // if total size is non-zero, we must have entries
		Rate best_rate = *RateCmp::s_tab.begin()                    ;
		Crun best_run  = RateCmp::s_lrus[best_rate].newer/*oldest*/ ;
		best_run->victimize() ;
	}
	_g_reserved_sz += sz ;
	trace("done",sz,hdr.total_sz,_g_reserved_sz) ;
	return true/*ok*/ ;
}

void release_room(DiskSz sz) {
	CrunHdr& hdr = CrunData::s_hdr() ;
	Trace trace("release_room",sz,hdr.total_sz,_g_reserved_sz) ;
	SWEAR( _g_reserved_sz>=sz , _g_reserved_sz,sz ) ;
	_g_reserved_sz -= sz ;
	SWEAR( hdr.total_sz+_g_reserved_sz <= g_config.max_sz , hdr.total_sz,_g_reserved_sz,g_config.max_sz ) ;
}

::string& operator+=( ::string& os , CjobName  const& jn ) { os << "CjobName("  ; if (+jn     ) os << +jn            ;                                      return os << ')' ; }
::string& operator+=( ::string& os , CnodeName const& nn ) { os << "CnodeName(" ; if (+nn     ) os << +nn            ;                                      return os << ')' ; }
::string& operator+=( ::string& os , Cjob      const& j  ) { os << "CJ("        ; if (+j      ) os << +j             ;                                      return os << ')' ; }
::string& operator+=( ::string& os , Crun      const& r  ) { os << "CR("        ; if (+r      ) os << +r             ;                                      return os << ')' ; }
::string& operator+=( ::string& os , Cnode     const& n  ) { os << "CN("        ; if (+n      ) os << +n             ;                                      return os << ')' ; }
::string& operator+=( ::string& os , LruEntry  const& e  ) { os << "LruEntry("  ; if (+e.newer) os <<"N:"<< +e.newer ; if (+e.older) os <<"O:"<< +e.older ; return os << ')' ; }

namespace Caches {

	//
	// DaemonCache::Config
	//

	::string DaemonCache::Config::s_store_dir_s(bool for_bck) {
		::string res = cat(PrivateAdminDirS,"store") ;
		if (for_bck) res << ".bck" ;
		add_slash(res) ;
		return res ;
	}

	DaemonCache::Config::Config(NewType) {
		Trace trace("config") ;
		//
		::string config_file = ADMIN_DIR_S "config.py" ;
		AcFd     config_fd   { config_file }           ;
		Gil      gil         ;
		for( auto const& [key,val] : ::vmap_ss(*py_run(config_fd.read())) ) {
			try {
				switch (key[0]) {
					case 'f' : if (key=="file_sync") { file_sync = mk_enum<FileSync>    (val) ;                                                        continue ; } break ;
					case 'i' : if (key=="inf"      ) {                                                                                                 continue ; } break ;
					case 'm' : if (key=="max_rate" ) { max_rate  = from_string_with_unit(val) ; throw_unless(max_rate>0,"max rate must be positive") ; continue ; } break ;
					case 'n' : if (key=="nan"      ) {                                                                                                 continue ; } break ;
					case 'p' : if (key=="perm"     ) { perm_ext  = mk_enum<PermExt>     (val) ;                                                        continue ; } break ;
					case 's' : if (key=="size"     ) { max_sz    = from_string_with_unit(val) ;                                                        continue ; } break ;
				DN}
			} catch (::string const& e) { trace("bad_val",key,val) ; throw cat("wrong value for entry ",key," : ",val) ; }
			trace("bad_cache_key",key) ;
			throw cat("wrong key (",key,") in ",config_file) ;
		}
		throw_unless( max_sz , "size must be defined as non-zero" ) ;
		trace("done") ;
	}

}

//
// LruEntry
//

bool/*first*/ LruEntry::insert_top( LruEntry& hdr , Crun run , LruEntry CrunData::* lru ) {
	bool first = !hdr.older ;
	/**/       older                     = hdr.older/*newest*/ ;
	/**/       newer                     = {}                  ;
	if (first) hdr.newer/*oldest*/       = run                 ;
	else       ((*hdr.older).*lru).newer = run                 ;
	/**/       hdr.older/*newest*/       = run                 ;
	return first ;
}
bool/*last*/ LruEntry::erase( LruEntry& hdr , Crun , LruEntry CrunData::* lru ) {
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
	erase     ( hdr , run , lru ) ;
	insert_top( hdr , run , lru ) ;
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
// CjobData
//

::string& operator+=( ::string& os , CjobData const& jd ) {
	/**/         os << "CjobData(" ;
	if (+jd.lru) os << +jd.lru     ;
	return       os << ')'         ;
}

void CjobData::victimize() {
	SWEAR( !lru , idx() ) ;
	_g_job_name_file.pop(_name) ;
	_g_job_file     .pop(idx()) ;
}

::pair<Crun,CacheHitInfo> CjobData::match( ::vector<Cnode> const& deps , ::vector<Hash::Crc> const& dep_crcs ) {
	Trace trace("match",idx(),deps.size(),dep_crcs.size()) ;
	for( Crun r=lru.older/*newest*/ ; +r ; r = r->job_lru.older ) {
		CacheHitInfo hit_info = r->match( deps , dep_crcs ) ;
		switch (hit_info) {
			case CacheHitInfo::Hit   : RateCmp::s_refresh() ; r->access() ; [[fallthrough]]         ;
			case CacheHitInfo::Match : trace(hit_info) ;                    return { r , hit_info } ;
		DN}
	}
	trace("miss") ;
	return { {} , CacheHitInfo::Miss } ;
}

::pair<Crun,CacheHitInfo> CjobData::insert(
	::vector<Cnode> const& deps , ::vector<Hash::Crc> const& dep_crcs
,	Hash::Crc key , bool key_is_last , Time::Pdate last_access , Disk::DiskSz sz , Rate rate
) {
	Trace trace("insert",idx(),key,sz,rate,deps.size(),dep_crcs.size()) ;
	Crun found_runs[2] ;                                                  // first and last with same key
	for( Crun r=lru.older/*newest*/ ; +r ; r = r->job_lru.older ) {
		CrunData& rd = *r ;
		if (rd.key==key) {
			SWEAR( !found_runs[rd.key_is_last] , r,found_runs[rd.key_is_last] ) ;
			found_runs[rd.key_is_last] = r ;
		}
		CacheHitInfo hit_info = rd.match( deps , dep_crcs ) ;
		switch (hit_info) {
			case CacheHitInfo::Hit   :
			case CacheHitInfo::Match : trace(hit_info) ; return { r , hit_info } ;
		DN}
	}
	if (+found_runs[true/*last*/]) { //!           last
		if (+found_runs[false/*last*/]) found_runs[true]->victimize() ;
		else                            found_runs[true]->key_is_last = false ;
	}
	Crun run { New , key , key_is_last , idx() , last_access , sz , rate , deps, dep_crcs } ;
	trace("miss") ;
	return { run , CacheHitInfo::Miss } ;
}

//
// CrunData
//

CrunData::CrunData( Hash::Crc k , bool kil , Cjob j , Time::Pdate la , Disk::DiskSz sz_ , Rate r , ::vector<Cnode> const& ds , ::vector<Hash::Crc> const& dcs )
:	key         { k   }
,	last_access { la  }
,	sz          { sz_ }
,	job         { j   }
,	deps        { ds  }
,	dep_crcs    { dcs }
,	rate        { r   }
,	key_is_last { kil }
{
	CrunHdr& hdr = CrunData::s_hdr() ;
	Trace trace("CrunData",key,STR(key_is_last),job,sz,rate,hdr.total_sz,ds,dcs) ;
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
	for( Cnode d : ds ) d->ref_cnt++ ;
}

::string& operator+=( ::string& os , CrunData const& rd ) {
	/**/                 os << "CrunData("<<rd.key ;
	if (+rd.key_is_last) os << ",last"             ;
	else                 os << ",first"            ;
	return               os << ')'                 ;
}

::string CrunData::name(Cjob job) const {
	::string         res =  job->name()         ;
	/**/             res << '/'<<key.hex()<<'-' ;
	if (key_is_last) res << "last"              ;
	else             res << "first"             ;
	return res ;
}

void CrunData::access() {
	Trace trace("access",idx()) ;
	job_lru.mv_to_top( job->lru              , idx() , &CrunData::job_lru ) ; // manage job-local LRU
	glb_lru.mv_to_top( RateCmp::s_lrus[rate] , idx() , &CrunData::glb_lru ) ; // manage global    LRU
	//
	last_access = New ;
	//
	RateCmp::s_insert(rate) ;
}

void CrunData::victimize() {
	CrunHdr& hdr = CrunData::s_hdr() ;
	Trace trace("victimize",idx(),hdr.total_sz,sz) ;
	bool last = job_lru.erase( job->lru              , idx() , &CrunData::job_lru ) ; // manage job-local LRU
	/**/        glb_lru.erase( RateCmp::s_lrus[rate] , idx() , &CrunData::glb_lru ) ; // manage global    LRU
	//
	RateCmp::s_tab.erase(rate) ;
	if (!RateCmp::s_lrus[rate].newer/*oldest*/) {
		while ( RateCmp::s_iota.bounds[0]<RateCmp::s_iota.bounds[1] && !RateCmp::s_lrus[RateCmp::s_iota.bounds[0]  ]) RateCmp::s_iota.bounds[0]++ ;
		while ( RateCmp::s_iota.bounds[0]<RateCmp::s_iota.bounds[1] && !RateCmp::s_lrus[RateCmp::s_iota.bounds[1]-1]) RateCmp::s_iota.bounds[1]-- ;
	} else {
		RateCmp::s_insert(rate) ;
	}
	//
	for( Cnode d : deps ) {
		d->ref_cnt-- ;
		if (!d->ref_cnt) d->victimize() ;
	}
	//
	if (last) job->victimize() ;
	SWEAR( hdr.total_sz >= sz , hdr.total_sz,sz,idx() ) ;
	hdr.total_sz -= sz ;
	_g_nodes_file.pop(deps    ) ;
	_g_crcs_file .pop(dep_crcs) ;
	_g_run_file  .pop(idx()   ) ;
}

CacheHitInfo CrunData::match( ::vector<Cnode> const& deps_ , ::vector<Hash::Crc> const& dep_crcs_ ) const {
	VarIdx              n_statics     = job->n_statics    ;
	CacheHitInfo        res           = CacheHitInfo::Hit ;
	::span<Cnode const> deps_view     = deps    .view()   ;
	::span<Crc   const> dep_crcs_view = dep_crcs.view()   ;
	//
	Trace trace("match",idx(),n_statics,deps_.size(),"in",deps_view.size(),"and",dep_crcs_.size(),"in",dep_crcs_view.size()) ;
	//
	SWEAR( n_statics<=dep_crcs_    .size() && dep_crcs_    .size()<=deps_    .size() , n_statics,deps_    ,dep_crcs_     ) ;
	SWEAR( n_statics<=dep_crcs_view.size() && dep_crcs_view.size()<=deps_view.size() , n_statics,deps_view,dep_crcs_view ) ;
	// first check static deps
	for( NodeIdx i : iota(n_statics) ) {
		SWEAR( deps_view[i]==deps_[i] , i,deps,deps_view ) ;                                                                    // static deps only depend on job
		if (dep_crcs_view[i]!=dep_crcs_[i]) { trace("miss1",i) ; return CacheHitInfo::Miss ; }                                  // found with a different crc
	}
	NodeIdx j1 = n_statics        ;                                                                                             // index into provided deps, with    crc
	NodeIdx j2 = dep_crcs_.size() ;                                                                                             // index into provided deps, without crc
	// then search for exising deps
	for( NodeIdx i : iota(n_statics,dep_crcs_view.size()) ) {
		while ( j1<dep_crcs_.size() && +deps_[j1]< +deps_view[i] ) j1++ ;
		if    ( j1<dep_crcs_.size() &&  deps_[j1]== deps_view[i] ) {
			if (!crc_ok(dep_crcs_view[i],dep_crcs_[j1])) { trace("miss2",i,j1) ; return CacheHitInfo::Miss ; }                  // found with a different crc
			j1++ ;                                                                                                              // fast path : j1 is consumed
		} else {
			while ( j2<deps_.size() && +deps_[j2]< +deps_view[i] ) j2++ ;
			if    ( j2<deps_.size() &&  deps_[j2]== deps_view[i] ) {
				if (!crc_ok(dep_crcs_view[i],Crc::None)) { trace("miss3",i,j2) ; return CacheHitInfo::Miss ; }                  // found without crc while expecting one
				j2++ ;                                                                                                          // fast path : j1 is consumed
			} else { trace("match",i,j1,j2) ; res = CacheHitInfo::Match ; }                                                     // not found
		}
	}
	// then search for non-existing deps
	if ( res==CacheHitInfo::Hit && dep_crcs_.size()==dep_crcs_view.size() ) {                                                   // fast path : all existing deps are consumed
		SWEAR( j2==dep_crcs_.size() , j2,dep_crcs_.size() )  ;                                                                  // all existing deps were found existing
		for( NodeIdx i : iota(dep_crcs_view.size(),deps_view.size()) ) {
			while ( j2<deps_.size() && +deps_[j2]< +deps_view[i] ) j2++ ;
			if    ( j2<deps_.size() &&  deps_[j2]== deps_view[i] ) j2++ ;                                                       // fast path : j2 is consumed
			else                                                   { trace("match",i,j1,j2) ; res = CacheHitInfo::Match ; }     // not found
		}
	} else {
		j1 = n_statics        ;                                                                                                 // reset search as desp are ordered separately existing/non-existing
		j2 = dep_crcs_.size() ;                                                                                                 // .
		for( NodeIdx i : iota(dep_crcs_view.size(),deps_view.size()) ) {
			while ( j1<dep_crcs_.size() && +deps_[j1]< +deps_view[i] ) j1++ ;
			if    ( j1<dep_crcs_.size() &&  deps_[j1]== deps_view[i] ) {
				if (!crc_ok(Crc::None,dep_crcs_[j1])) { trace("miss4",i,j1   ) ; return CacheHitInfo::Miss ; }                  // found with crc while expecting none
				j1++ ;                                                                                                          // fast path : j1 is consumed
			} else {
				while ( j2<deps_.size() && +deps_[j2]< +deps_view[i] ) j2++ ;
				if    ( j2<deps_.size() &&  deps_[j2]== deps_view[i] ) j2++ ;                                                   // fast path : j2 is consumed
				else                                                   { trace("match",i,j1,j2) ; res = CacheHitInfo::Match ; } // not found
			}
		}
	}
	trace(res) ;
	return res ;
}

void CnodeData::victimize() {
	_g_node_name_file.pop(_name) ;
	_g_node_file     .pop(idx()) ;
}
