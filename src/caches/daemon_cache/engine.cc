// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "engine.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

CjobNameFile  _g_job_name_file  ;
CnodeNameFile _g_node_name_file ;
CjobFile      _g_job_file       ;
CrunFile      _g_run_file       ;
CnodeFile     _g_node_file      ;
CnodesFile    _g_nodes_file     ;
CcrcsFile     _g_crcs_file      ;

DiskSz _g_reserved_sz = 0 ;

static void _daemon_cache_chk() {
	_g_job_name_file .chk() ;
	_g_node_name_file.chk() ;
	_g_job_file      .chk() ;
	_g_run_file      .chk() ;
	_g_node_file     .chk() ;
	_g_nodes_file    .chk() ;
	_g_crcs_file     .chk() ;
}

void daemon_cache_init(bool rescue) {
	Trace trace("daemon_cache_init",STR(rescue)) ;
	//
	// START_OF_VERSIONING DAEMON_CACHE
	::string dir_s = cat(PrivateAdminDirS,"store/") ;
	//                                        writable
	_g_job_name_file .init( dir_s+"job_name"  , true ) ;
	_g_node_name_file.init( dir_s+"node_name" , true ) ;
	_g_job_file      .init( dir_s+"job"       , true ) ;
	_g_run_file      .init( dir_s+"run"       , true ) ;
	_g_node_file     .init( dir_s+"node"      , true ) ;
	_g_nodes_file    .init( dir_s+"nodes"     , true ) ;
	_g_crcs_file     .init( dir_s+"crcs"      , true ) ;
	// END_OF_VERSIONING
	if (rescue) _daemon_cache_chk() ;
	trace("done") ;
}

void mk_room(DiskSz sz) {
	CrunHdr& hdr = CrunData::s_hdr() ;
	Pdate    now { New }             ;
	Trace trace("mk_room",sz,hdr.total_sz,_g_reserved_sz) ;
	while ( hdr.total_sz+_g_reserved_sz+sz > g_max_sz ) {
		Crun     best_run ;
		uint64_t merit    = 0 ;
		for( size_t r : iota(NRates) ) {
			Crun     run = hdr.lrus[r].newer                                    ; if (!run) continue ;
			uint64_t m   = float(now-run->last_access) / ::expf(::ldexpf(r,-4)) ;
			if (m>merit) {
				merit    = m   ;
				best_run = run ;
			}
		}
		throw_unless( +best_run , "no victim found to make room" ) ;
		DiskSz entry_sz = best_run->sz ;
		best_run->victimize() ;
		hdr.total_sz -= entry_sz ;
		trace("victimize",best_run,entry_sz,hdr.total_sz) ;
	}
	_g_reserved_sz += sz ;
	trace("done",sz,hdr.total_sz,_g_reserved_sz) ;
}

void release_room(DiskSz sz) {
	CrunHdr& hdr = CrunData::s_hdr() ;
	Trace trace("release_room",sz,hdr.total_sz,_g_reserved_sz) ;
	SWEAR( _g_reserved_sz>=sz , _g_reserved_sz,sz ) ;
	_g_reserved_sz -= sz ;
	SWEAR( hdr.total_sz+_g_reserved_sz <= g_max_sz , hdr.total_sz,_g_reserved_sz,g_max_sz ) ;
}

::string& operator+=( ::string& os , CjobName  const& jn ) { os << "CjobName("  ; if (+jn     ) os << +jn            ;                                      return os << ')' ; }
::string& operator+=( ::string& os , CnodeName const& nn ) { os << "CnodeName(" ; if (+nn     ) os << +nn            ;                                      return os << ')' ; }
::string& operator+=( ::string& os , Cjob      const& j  ) { os << "Cjob("      ; if (+j      ) os << +j             ;                                      return os << ')' ; }
::string& operator+=( ::string& os , Crun      const& r  ) { os << "Crun("      ; if (+r      ) os << +r             ;                                      return os << ')' ; }
::string& operator+=( ::string& os , Cnode     const& n  ) { os << "Cnode("     ; if (+n      ) os << +n             ;                                      return os << ')' ; }
::string& operator+=( ::string& os , LruEntry  const& e  ) { os << "LruEntry("  ; if (+e.newer) os <<"N:"<< +e.newer ; if (+e.older) os <<"O:"<< +e.older ; return os << ')' ; }

//
// LruEntry
//

bool/*first*/ LruEntry::insert_top( LruEntry& hdr , Crun run , LruEntry CrunData::* lru ) {
	bool first = !hdr.older ;
	/**/            older                     = hdr.older/*newest*/ ;
	/**/            newer                     = {}                  ;
	if (+hdr.older) ((*hdr.older).*lru).newer = run                 ;
	else            hdr.newer/*oldest*/       = run                 ;
	/**/            hdr.older/*newest*/       = run                 ;
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
	if (!n) n = _g_node_file.emplace_back(nn) ;
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
			case CacheHitInfo::Hit   : r->access()     ; [[fallthrough]]         ;
			case CacheHitInfo::Match : trace(hit_info) ; return { r , hit_info } ;
		DN}
	}
	trace("miss") ;
	return { {} , CacheHitInfo::Miss } ;
}

::pair<Crun,CacheHitInfo> CjobData::insert( Hash::Crc key , Disk::DiskSz sz , Rate rate , ::vector<Cnode> const& deps , ::vector<Hash::Crc> const& dep_crcs ) {
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
	if (+found_runs[true/*last*/]) {
		if (+found_runs[false/*last*/]) found_runs[true/*last*/]->victimize() ;
		else                            found_runs[true/*last*/]->key_is_last = false ;
	}
	Crun run { New , key , true/*key_is_last*/ , idx() , sz , rate , deps, dep_crcs } ;
	trace("miss") ;
	return { run , CacheHitInfo::Miss } ;
}

//
// CrunData
//

CrunData::CrunData( Hash::Crc k , bool kil , Cjob j , Disk::DiskSz sz_ , Rate r , ::vector<Cnode> const& ds , ::vector<Hash::Crc> const& dcs )
:	key         { k   }
,	last_access { New }
,	sz          { sz_ }
,	job         { j   }
,	deps        { ds  }
,	dep_crcs    { dcs }
,	rate        { r   }
,	key_is_last { kil }
{
	Trace trace("CrunData",k,STR(kil),j,sz_,r,ds,dcs) ;
	job_lru.insert_top( job->lru                     , idx() , &CrunData::job_lru ) ;
	glb_lru.insert_top( CrunData::s_hdr().lrus[rate] , idx() , &CrunData::glb_lru ) ;
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
	last_access = New ;
	job_lru.mv_to_top( job->lru                     , idx() , &CrunData::job_lru ) ; // manage job-local LRU
	glb_lru.mv_to_top( CrunData::s_hdr().lrus[rate] , idx() , &CrunData::glb_lru ) ; // manage global    LRU
}

void CrunData::victimize() {
	Trace trace("victimize",idx()) ;
	CrunHdr& hdr  = CrunData::s_hdr()                                            ;
	bool     last = job_lru.erase( job->lru       , idx() , &CrunData::job_lru ) ; // manage job-local LRU
	/**/            glb_lru.erase( hdr.lrus[rate] , idx() , &CrunData::glb_lru ) ; // manage global    LRU
	if (last) job->victimize() ;
	SWEAR( hdr.total_sz >= sz ) ;
	hdr.total_sz -= sz ;
	_g_nodes_file.pop(deps    ) ;
	_g_crcs_file .pop(dep_crcs) ;
	_g_run_file  .pop(idx()   ) ;
}

CacheHitInfo CrunData::match( ::vector<Cnode> const& deps_ , ::vector<Hash::Crc> const& dep_crcs_ ) const {
	Trace trace("match",idx(),deps_.size(),"in",deps.size(),"and",dep_crcs_.size(),"in",dep_crcs.size()) ;
	//
	VarIdx              n_statics     = job->n_statics    ;
	CacheHitInfo        res           = CacheHitInfo::Hit ;
	::span<Cnode const> deps_view     = deps    .view()   ;
	::span<Crc   const> dep_crcs_view = dep_crcs.view()   ;
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
			if (dep_crcs_view[i]!=dep_crcs_[j1])                   { trace("miss2",i,j1   ) ; return CacheHitInfo::Miss ; }     // found with a different crc
			j1++ ;                                                                                                              // fast path : j1 is consumed
		} else {
			while ( j2<deps_.size() && +deps_[j2]< +deps_view[i] ) j2++ ;
			if    ( j2<deps_.size() &&  deps_[j2]== deps_view[i] ) { trace("miss3",i,   j2) ; return CacheHitInfo::Miss ; }     // found without crc while expecting one
			else                                                   { trace("match",i,j1,j2) ; res = CacheHitInfo::Match ; }     // not found
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
				/**/                                                      trace("miss4",i,j1   ) ; return CacheHitInfo::Miss ;  // found with crc while expecting none
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
