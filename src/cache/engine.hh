// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "rpc_job.hh"

#include "store/alloc.hh"
#include "store/idxed.hh"
#include "store/prefix.hh"
#include "store/vector.hh"

#include "rpc_cache.hh"

using namespace Cache ;

enum class KeyIsLast : uint8_t {
	No
,	OverrideFirst
,	Plain
,	Yes
} ;

static constexpr Hash::Crc::Val CrcOrNone = 1<< NBits<CrcSpecial>    ;
static constexpr Hash::Crc::Val CrcErr    = 1<<(NBits<CrcSpecial>+1) ;

struct Ckey          ;
struct Cjob          ;
struct Crun          ;
struct Cnode         ;
struct CompileDigest ;
struct CkeyData      ;
struct CjobData      ;
struct CrunData      ;
struct CnodeData     ;

//
// globals
//

extern CacheConfig  g_cache_config ;
extern Disk::DiskSz g_reserved_sz  ;
extern ::string     g_store_dir_s  ;
extern FileSync     g_file_sync    ; // file sync to be used by cache_server

//
// free functions
//

void cache_init       ( bool rescue , bool read_only=false ) ;
void cache_empty_trash(                                    ) ;
void cache_finalize   (                                    ) ;
void cache_chk        (                                    ) ;
void mk_room          ( Disk::DiskSz , Cjob keep_job       ) ;
void mk_room          ( Disk::DiskSz                       ) ;

//
// structs
//

struct Ckey : Idxed<CkeyIdx> {
	using Base = Idxed<CkeyIdx> ;
	// cxtors & casts
	using Base::Base ;
	Ckey(           ::string const& ) ; // make empty if not found
	Ckey( NewType , ::string const& ) ; // create new if not found
	// accesses
	void            operator>>(::string&) const ;
	CkeyData const& operator* (         ) const ;
	CkeyData      & operator* (         )       ;
	CkeyData const* operator->(         ) const { return &*self ; }
	CkeyData      * operator->(         )       { return &*self ; }
	::string str() const ;
	// services
	void inc      () ;
	void dec      () ;
	void victimize() ;
} ;

struct CjobName : Idxed<CjobNameIdx> {
	using Base = Idxed<CjobNameIdx> ;
	// cxtors & casts
	using Base::Base ;
	// accesses
	void     operator>>(::string&) const ;
	::string str       (         ) const ;
} ;

struct CnodeName : Idxed<CnodeNameIdx> {
	using Base = Idxed<CnodeNameIdx> ;
	// cxtors & casts
	using Base::Base ;
	// accesses
	void     operator>>(::string&) const ;
	::string str       (         ) const ;
} ;

struct Cjob : Idxed<CjobIdx> {
	using Base = Idxed<CjobIdx> ;
	// statics
	// cxtors & casts
	using Base::Base ;
	Cjob(           ::string const& name                    ) ; // make empty if not found
	Cjob( NewType , ::string const& name , VarIdx n_statics ) ; // create new if not found
	// accesses
	void            operator>>(::string&) const ;
	CjobData const& operator* (         ) const ;
	CjobData      & operator* (         )       ;
	CjobData const* operator->(         ) const { return &*self ; }
	CjobData      * operator->(         )       { return &*self ; }
} ;

struct Crun : Idxed<CrunIdx> {
	using Base = Idxed<CrunIdx> ;
	// cxtors& casts
	using Base::Base ;
	template<class... A> Crun( NewType , A&&... ) ; // args are passed to CrunData cxtor
	// accesses
	void            operator>>(::string&) const ;
	CrunData const& operator* (         ) const ;
	CrunData      & operator* (         )       ;
	CrunData const* operator->(         ) const { return &*self ; }
	CrunData      * operator->(         )       { return &*self ; }
} ;

struct Cnode : Idxed<CnodeIdx> {
	using Base = Idxed<CnodeIdx> ;
	// cxtors & casts
	using Base::Base ;
	Cnode(           ::string const& name ) ; // make empty if not found
	Cnode( NewType , ::string const& name ) ; // create new if not found
	// accesses
	void             operator>>(::string&) const ;
	CnodeData const& operator* (         ) const ;
	CnodeData      & operator* (         )       ;
	CnodeData const* operator->(         ) const { return &*self ; }
	CnodeData      * operator->(         )       { return &*self ; }
} ;

struct CompileDigest {
	// cxtors & casts
	CompileDigest() = default ;
	CompileDigest( ::vmap<Cache::StrId<Cache::CnodeIdx>,DepDigest> const& repo_deps , bool for_download , ::vector<Cache::CnodeIdx>* dep_ids=nullptr ) ; // dep_ids are filled for each named dep ...
	~CompileDigest() ;                                                                                                                                   // ... in repo_deps
	// accesses
	void operator>>(::string&) const ;
	// data
	VarIdx              n_statics  = 0     ;
	bool                has_hidden = false ;
	::vector<Cnode>     deps       ;
	::vector<Hash::Crc> dep_crcs   ;
} ;

struct DaemonCacheMrkr {} ;
using Cnodes = Vector::Simple<CnodesIdx,Cnode    ,DaemonCacheMrkr> ;
using Ccrcs  = Vector::Simple<CcrcsIdx ,Hash::Crc,DaemonCacheMrkr> ;

// START_OF_VERSIONING CACHE
struct LruEntry {
	// accesses
	void operator>>(::string&) const ;
	bool operator+ (         ) const { return +newer || +older ; }
	// services
	bool/*first*/ insert_top( LruEntry      & hdr , Crun , LruEntry CrunData::* lru )       ;
	bool/*last*/  erase     ( LruEntry      & hdr ,        LruEntry CrunData::* lru )       ;
	void          mv_to_top ( LruEntry      & hdr , Crun , LruEntry CrunData::* lru )       ;
	void          chk       ( LruEntry const& hdr , Crun , LruEntry CrunData::* lru ) const ;
	// data
	Crun newer ; // for headers : oldest
	Crun older ; // for headers : newest
} ;
// END_OF_VERSIONING

struct CkeyData {
	friend struct Ckey ;
	// accesses
	void operator>>(::string&) const ;
	// services
	bool operator==(CkeyData const&) const = default ;
	// data
	// START_OF_VERSIONING CACHE
	CrunIdx ref_cnt = 0 ;
	// END_OF_VERSIONING
} ;

struct CjobData {
	friend struct Cjob ;
	// statics
	static CnodeIdx s_size       () ;
	static void     s_empty_trash() ;
	static void     s_rescue     () ;
	// static data
	static ::vector<Cjob> s_trash ;
	// cxtors & casts
	CjobData() = default ;
	CjobData( CjobName n , VarIdx nss ) : n_statics{nss},_name{n} {}
	// accesses
	void     operator>>(::string&) const ;
	Cjob     idx       (         ) const ;
	bool     operator+ (         ) const { return +n_runs     ; }
	::string name      (         ) const { return _name.str() ; }
	// services
	::pair<Crun,CacheHitInfo> match( CompileDigest const& ) ;                                    // updates lru related info when hit
	bool/*done*/ insert(                                                                         // like match, but create when miss
		CompileDigest const&                                                                     // to search entry
	,	Ckey key , KeyIsLast key_is_last , Time::Pdate last_access , Disk::DiskSz sz , Rate rate // to create entry
	,	::string const& reserved_file={} , NfsGuard* =nullptr                                    // to manage files
	) ;
	void victimize(NfsGuard* =nullptr) ;
	// data
	// START_OF_VERSIONING CACHE
	LruEntry lru       ;
	uint16_t n_runs    = 0 ;
	VarIdx   n_statics = 0 ;
private :
	CjobName _name ;
	// END_OF_VERSIONING
} ;

struct CrunHdr {
	// START_OF_VERSIONING CACHE
	LruEntry     lrus[NRates] ;
	Disk::DiskSz total_sz     = 0  ;
	// END_OF_VERSIONING
} ;

struct CrunData {
	friend struct Crun ;
	// statics
	static CrunHdr      & s_hdr  () ;
	static CrunHdr const& s_c_hdr() ;
	static CrunIdx        s_size () ;
	static void           s_chk  () ;
	// cxtors & casts
	CrunData() = default ;
	CrunData( Ckey , bool key_is_last , Cjob , Time::Pdate last_access , Disk::DiskSz , Rate , CompileDigest const& ) ;
	// accesses
	void     operator>>(::string&) const ;
	bool     operator+ (         ) const { return +job                                         ; }
	Crun     idx       (         ) const ;
	::string name      (         ) const { return run_file( job->name() , +key , key_is_last ) ; }
	// services
	void                   access   (                                                     )       ; // move to top in LRU (both job and glb)
	bool/*job_victimzied*/ victimize( bool victimize_job=true , NfsGuard* =nullptr        )       ; // if victimize_job, victimize job if last run
	CacheHitInfo           match    ( CompileDigest const&                                ) const ;
	void                   chk      (                                                     ) const ;
	// data
	// START_OF_VERSIONING CACHE
	Time::Pdate  last_access ;                                                                      //    64 bits
	Disk::DiskSz sz          = 0                ;                                                   //    64 bits, size occupied by run
	LruEntry     glb_lru     ;                                                                      //    64 bits, global LRU within rate
	LruEntry     job_lru     ;                                                                      //    64 bits, job LRU
	Cjob         job         ;                                                                      //    32 bits
	Cnodes       deps        ;                                                                      //    32 bits, owned sorted by (is_static,existing,idx)
	Ccrcs        dep_crcs    ;                                                                      //    32 bits, owned crcs for static and existing deps
	Ckey         key         ;                                                                      //    32 bits, identifies origin (repo+git_sha1)
	Rate         rate        = 0    /*garbage*/ ;                                                   //     8 bits
	bool         key_is_last = false/*.      */ ;                                                   // 1<= 8 bit , 2 runs may be stored for each key : the first and the last
	bool         has_hidden  = false            ;                                                   // 1<= 8 bit , some hidden deps exist (i.e. that need not match)
	// END_OF_VERSIONING
} ;
static_assert( sizeof(CrunData)==56 ) ;

struct CnodeData {
	friend struct Cnode ;
	// statics
	static CnodeIdx s_size       () ;
	static void     s_empty_trash() ;
	static void     s_rescue     () ;
	// static data
	static ::vector<Cnode> s_trash ;
	// cxtors & casts
	CnodeData() = default ;
	CnodeData(CnodeName n) : _name{n} {}
	// accesses
	void     operator>>(::string&) const ;
	Cnode    idx       (         ) const ;
	bool     operator+ (         ) const { return ref_cnt>0   ; }
	::string name      (         ) const { return _name.str() ; }
	// services
	void inc      () {                          ref_cnt++ ;                             }
	void dec      () { SWEAR(ref_cnt>0,idx()) ; ref_cnt-- ; if (!ref_cnt) victimize() ; }
	void victimize() { s_trash.push_back(idx()) ;                                       }
	// data
	// START_OF_VERSIONING CACHE
	CrunIdx ref_cnt = 0 ;
private :
	CnodeName _name ;
	// END_OF_VERSIONING
} ;

// START_OF_VERSIONING CACHE
//                                           ThreadKey header    index       n_index_bits        key    data        misc
using CkeyFile      = Store::SinglePrefixFile< '='   , void    , Ckey      , NCkeyIdxBits      , char , CkeyData                          > ;
using CjobNameFile  = Store::SinglePrefixFile< '='   , void    , CjobName  , NCjobNameIdxBits  , char , Cjob                              > ;
using CnodeNameFile = Store::SinglePrefixFile< '='   , void    , CnodeName , NCnodeNameIdxBits , char , Cnode                             > ;
using CjobFile      = Store::AllocFile       < '='   , void    , Cjob      , NCjobIdxBits      ,        CjobData  , 0/*Mantissa*/         > ;
using CrunFile      = Store::AllocFile       < '='   , CrunHdr , Crun      , NCrunIdxBits      ,        CrunData                          > ;
using CnodeFile     = Store::AllocFile       < '='   , void    , Cnode     , NCnodeIdxBits     ,        CnodeData , 0/*Mantissa*/         > ;
using CnodesFile    = Store::VectorFile      < '='   , void    , Cnodes    , NCnodesIdxBits    ,        Cnode     , CnodeIdx , 4/*MinSz*/ > ;
using CcrcsFile     = Store::VectorFile      < '='   , void    , Ccrcs     , NCcrcsIdxBits     ,        Hash::Crc , CnodeIdx , 4/*.    */ > ;
// END_OF_VERSIONING

extern CkeyFile      _g_key_file       ;
extern CjobNameFile  _g_job_name_file  ;
extern CnodeNameFile _g_node_name_file ;
extern CjobFile      _g_job_file       ;
extern CrunFile      _g_run_file       ;
extern CnodeFile     _g_node_file      ;
extern CnodesFile    _g_nodes_file     ;
extern CcrcsFile     _g_crcs_file      ;

template<class T> auto lst() ;
template<> inline auto lst<Ckey >() { return _g_key_file .lst() ; }
template<> inline auto lst<Cjob >() { return _g_job_file .lst() ; }
template<> inline auto lst<Crun >() { return _g_run_file .lst() ; }
template<> inline auto lst<Cnode>() { return _g_node_file.lst() ; }

namespace Vector {
	template<> struct Descr<Cnodes> { static constexpr CnodesFile& file = _g_nodes_file ; } ;
	template<> struct Descr<Ccrcs > { static constexpr CcrcsFile & file = _g_crcs_file  ; } ;
}

//
// implementation
//

inline void mk_room( Disk::DiskSz sz ) { mk_room(sz,{}) ; }

inline ::string Ckey     ::str() const { return _g_key_file      .str_key(self) ; }
inline ::string CjobName ::str() const { return _g_job_name_file .str_key(self) ; }
inline ::string CnodeName::str() const { return _g_node_name_file.str_key(self) ; }

inline CkeyData  const& Ckey ::operator*() const { return _g_key_file .c_at(self) ; }
inline CkeyData       & Ckey ::operator*()       { return _g_key_file .at  (self) ; }
inline CjobData  const& Cjob ::operator*() const { return _g_job_file .c_at(self) ; }
inline CjobData       & Cjob ::operator*()       { return _g_job_file .at  (self) ; }
inline CrunData  const& Crun ::operator*() const { return _g_run_file .c_at(self) ; }
inline CrunData       & Crun ::operator*()       { return _g_run_file .at  (self) ; }
inline CnodeData const& Cnode::operator*() const { return _g_node_file.c_at(self) ; }
inline CnodeData      & Cnode::operator*()       { return _g_node_file.at  (self) ; }

inline void Ckey::inc() { CrunIdx& ref_cnt = self->ref_cnt ;                  ref_cnt++ ;                             }
inline void Ckey::dec() { CrunIdx& ref_cnt = self->ref_cnt ; SWEAR(ref_cnt) ; ref_cnt-- ; if (!ref_cnt) victimize() ; }

template<class... A> Crun::Crun( NewType , A&&... args ) {
	self = _g_run_file.emplace(::forward<A>(args)...) ;
}

inline CrunHdr      & CrunData ::s_hdr  ()       { return _g_run_file .hdr  (    ) ; }
inline CrunHdr const& CrunData ::s_c_hdr()       { return _g_run_file .c_hdr(    ) ; }

inline CjobIdx        CjobData ::s_size ()       { return _g_job_file .size (    ) ; }
inline CrunIdx        CrunData ::s_size ()       { return _g_run_file .size (    ) ; }
inline CnodeIdx       CnodeData::s_size ()       { return _g_node_file.size (    ) ; }

inline Cjob           CjobData ::idx    () const { return _g_job_file .idx  (self) ; }
inline Crun           CrunData ::idx    () const { return _g_run_file .idx  (self) ; }
inline Cnode          CnodeData::idx    () const { return _g_node_file.idx  (self) ; }
