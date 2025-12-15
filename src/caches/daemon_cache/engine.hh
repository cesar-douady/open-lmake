// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "rpc_job.hh"

#include "store/alloc.hh"
#include "store/idxed.hh"
#include "store/prefix.hh"
#include "store/vector.hh"

extern FileSync     g_file_sync ;
extern PermExt      g_perm_ext  ;
extern Disk::DiskSz g_max_sz    ;

// START_OF_VERSIONING DAEMON_CACHE

using Rate = uint8_t ;

// can be tailored to fit needs
static constexpr uint8_t NCjobNameIdxBits  = 32 ;
static constexpr uint8_t NCnodeNameIdxBits = 32 ;
static constexpr uint8_t NCjobIdxBits      = 32 ;
static constexpr uint8_t NCrunIdxBits      = 32 ;
static constexpr uint8_t NCnodeIdxBits     = 32 ;
static constexpr uint8_t NCnodesIdxBits    = 32 ;
static constexpr uint8_t NCcrcsIdxBits     = 32 ;

// used for cache efficiency
// rate=0 means 1GB/s (clamped if above)
// +1 means job took 13.3% more time per byte of generated data
// slowest rate is 1B/s (clamped if below)
static constexpr Rate NRates = 166 ;

// END_OF_VERSIONING

// rest cannot be tailored

using CjobNameIdx  = Uint<NCjobNameIdxBits > ;
using CnodeNameIdx = Uint<NCnodeNameIdxBits> ;
using CjobIdx      = Uint<NCjobIdxBits     > ;
using CrunIdx      = Uint<NCrunIdxBits     > ;
using CnodeIdx     = Uint<NCnodeIdxBits    > ;
using CnodesIdx    = Uint<NCnodesIdxBits   > ;
using CcrcsIdx     = Uint<NCcrcsIdxBits    > ;

//
// free functions
//

void daemon_cache_init(bool         rescue) ;
void mk_room          (Disk::DiskSz       ) ;
void release_room     (Disk::DiskSz       ) ;

//
// structs
//

struct Cjob  ;
struct Crun  ;
struct Cnode ;
struct CjobData  ;
struct CrunData  ;
struct CnodeData ;

struct CjobName : Idxed<CjobNameIdx> {
	using Base = Idxed<CjobNameIdx> ;
	friend ::string& operator+=( ::string& , CjobName const& ) ;
	// cxtors & casts
	using Base::Base ;
	// accesses
	::string str() const ;
} ;

struct CnodeName : Idxed<CnodeNameIdx> {
	using Base = Idxed<CnodeNameIdx> ;
	friend ::string& operator+=( ::string& , CnodeName const& ) ;
	// cxtors & casts
	using Base::Base ;
	// accesses
	::string str() const ;
} ;

struct Cjob : Idxed<CjobIdx> {
	using Base = Idxed<CjobIdx> ;
	friend ::string& operator+=( ::string& , Cjob const& ) ;
	// cxtors & casts
	using Base::Base ;
	Cjob(           ::string const& name ) ; // make empty if not found
	Cjob( NewType , ::string const& name ) ; // create new if not found
	// accesses
	CjobData const& operator* () const ;
	CjobData      & operator* ()       ;
	CjobData const* operator->() const { return &*self ; }
	CjobData      * operator->()       { return &*self ; }
} ;

struct Crun : Idxed<CrunIdx> {
	using Base = Idxed<CrunIdx> ;
	friend ::string& operator+=( ::string& , Crun const& ) ;
	// cxtors& casts
	using Base::Base ;
	template<class... A> Crun( NewType , A&&... ) ; // args are passed to CrunData cxtor
	// accesses
	CrunData const& operator* () const ;
	CrunData      & operator* ()       ;
	CrunData const* operator->() const { return &*self ; }
	CrunData      * operator->()       { return &*self ; }
} ;

struct Cnode : Idxed<CnodeIdx> {
	using Base = Idxed<CnodeIdx> ;
	friend ::string& operator+=( ::string& , Cnode const& ) ;
	// cxtors & casts
	using Base::Base ;
	Cnode(           ::string const& name ) ; // make empty if not found
	Cnode( NewType , ::string const& name ) ; // create new if not found
	// accesses
	CnodeData const& operator* () const ;
	CnodeData      & operator* ()       ;
	CnodeData const* operator->() const { return &*self ; }
	CnodeData      * operator->()       { return &*self ; }
} ;

struct DaemonCacheMrkr {} ;
using Cnodes = Vector::Simple<CnodesIdx,Cnode    ,DaemonCacheMrkr> ;
using Ccrcs  = Vector::Simple<CcrcsIdx ,Hash::Crc,DaemonCacheMrkr> ;

// START_OF_VERSIONING DAEMON_CACHE
struct LruEntry {
	friend ::string& operator+=( ::string& , LruEntry const& ) ;
	// accesses
	bool operator+() const { return +newer || +older ; }
	// services
	bool/*first*/ insert_top( LruEntry& hdr , Crun , LruEntry CrunData::* lru ) ;
	bool/*last*/  erase     ( LruEntry& hdr , Crun , LruEntry CrunData::* lru ) ;
	void          mv_to_top ( LruEntry& hdr , Crun , LruEntry CrunData::* lru ) ;
	// data
	Crun newer ;                                         // for headers : oldest
	Crun older ;                                         // for headers : newest
} ;
// END_OF_VERSIONING

struct CjobData {
	friend struct Cjob ;
	friend ::string& operator+=( ::string& , CjobData const& ) ;
	// cxtors & casts
	CjobData() = default ;
	CjobData(CjobName n) : _name{n} {}
	// accesses
	Cjob     idx () const ;
	::string name() const { return _name.str() ; }
	// services
	::pair<Crun,CacheHitInfo> match    (                                               NodeIdx n_statics , ::vector<Cnode> const& , ::vector<Hash::Crc> const& ) ; // updates lru related info when hit
	::pair<Crun,CacheHitInfo> insert   ( Hash::Crc key , Disk::DiskSz sz , Rate rate , NodeIdx n_statics , ::vector<Cnode> const& , ::vector<Hash::Crc> const& ) ; // like match, but create when miss
	void                      victimize(                                                                                                                       ) ;
	// data
	// START_OF_VERSIONING DAEMON_CACHE
	LruEntry lru = {} ;
private :
	CjobName _name ;
	// END_OF_VERSIONING
} ;

struct CrunHdr {
	// START_OF_VERSIONING DAEMON_CACHE
	Disk::DiskSz total_sz     = 0 ;
	LruEntry     lrus[NRates] ;
	// END_OF_VERSIONING
} ;

struct CrunData {
	friend struct Crun ;
	friend ::string& operator+=( ::string& , CrunData const& ) ;
	// statics
	static CrunHdr      & s_hdr  () ;
	static CrunHdr const& s_c_hdr() ;
	// cxtors & casts
	CrunData() = default ;
	CrunData( Hash::Crc key , bool key_is_last , Cjob job , Disk::DiskSz , Rate , ::vector<Cnode> const& deps , ::vector<Hash::Crc> const& dep_crcs ) ;
	// accesses
	Crun     idx (    ) const ;
	::string name(Cjob) const ;
	// services
	void         access   (                                                                         )       ; // move to top in LRU (both job and glb)
	void         victimize(                                                                         )       ;
	CacheHitInfo match    ( NodeIdx n_statics , ::vector<Cnode> const& , ::vector<Hash::Crc> const& ) const ;
	// data
	// START_OF_VERSIONING DAEMON_CACHE
	Hash::Crc    key         ;                                                                                // identifies origin (repo+git_sha1)
	Time::Pdate  last_access ;
	Disk::DiskSz sz          = 0                ;                                                             // size occupied by run
	LruEntry     glb_lru     ;                                                                                // global LRU within rate
	LruEntry     job_lru     ;                                                                                // job LRU
	Cjob         job         ;
	Cnodes       deps        ;                                                                                // owned sorted by (is_static,existing,idx)
	Ccrcs        dep_crcs    ;                                                                                // owned crcs for static and existing deps
	Rate         rate        ;
	bool         key_is_last = false/*garbage*/ ;                                                             // 2 runs may be stored for each key : the first and the last
	// END_OF_VERSIONING
} ;
static_assert( sizeof(CrunData)==56 ) ;

struct CnodeData {
	friend struct Cnode ;
	friend ::string& operator+=( ::string& , CnodeData const& ) ;
	// cxtors & casts
	CnodeData() = default ;
	CnodeData(CnodeName n) : _name{n} {}
	// accesses
	Cnode    idx () const ;
	::string name() const { return _name.str() ; }
	// data
	// START_OF_VERSIONING DAEMON_CACHE
private :
	CnodeName _name ;
	// END_OF_VERSIONING
} ;

//                                           ThreadKey header    index       n_index_bits        key    data        misc
using CjobNameFile  = Store::SinglePrefixFile< '='   , void    , CjobName  , NCjobNameIdxBits  , char , Cjob                     > ;
using CnodeNameFile = Store::SinglePrefixFile< '='   , void    , CnodeName , NCnodeNameIdxBits , char , Cnode                    > ;
using CjobFile      = Store::AllocFile       < '='   , void    , Cjob      , NCjobIdxBits      ,        CjobData                 > ;
using CrunFile      = Store::AllocFile       < '='   , CrunHdr , Crun      , NCrunIdxBits      ,        CrunData                 > ;
using CnodeFile     = Store::StructFile      < '='   , void    , Cnode     , NCnodeIdxBits     ,        CnodeData                > ;
using CnodesFile    = Store::VectorFile      < '='   , void    , Cnodes    , NCnodesIdxBits    ,        Cnode     , CnodeIdx , 4 > ;
using CcrcsFile     = Store::VectorFile      < '='   , void    , Ccrcs     , NCcrcsIdxBits     ,        Hash::Crc , CnodeIdx , 4 > ;

extern CjobNameFile  _g_job_name_file  ;
extern CnodeNameFile _g_node_name_file ;
extern CjobFile      _g_job_file       ;
extern CrunFile      _g_run_file       ;
extern CnodeFile     _g_node_file      ;
extern CnodesFile    _g_nodes_file     ;
extern CcrcsFile     _g_crcs_file      ;

namespace Vector {
	template<> struct Descr<Cnodes> { static constexpr CnodesFile& file = _g_nodes_file ; } ;
	template<> struct Descr<Ccrcs > { static constexpr CcrcsFile & file = _g_crcs_file  ; } ;
}

//
// implementation
//

inline CrunHdr      & CrunData::s_hdr  () { return _g_run_file.hdr  () ; }
inline CrunHdr const& CrunData::s_c_hdr() { return _g_run_file.c_hdr() ; }

inline ::string CjobName ::str() const { return _g_job_name_file .str_key(self) ; }
inline ::string CnodeName::str() const { return _g_node_name_file.str_key(self) ; }

inline CjobData  const& Cjob ::operator*() const { return _g_job_file .c_at(self) ; }
inline CjobData       & Cjob ::operator*()       { return _g_job_file .at  (self) ; }
inline CrunData  const& Crun ::operator*() const { return _g_run_file .c_at(self) ; }
inline CrunData       & Crun ::operator*()       { return _g_run_file .at  (self) ; }
inline CnodeData const& Cnode::operator*() const { return _g_node_file.c_at(self) ; }
inline CnodeData      & Cnode::operator*()       { return _g_node_file.at  (self) ; }

inline Cjob  CjobData ::idx() const { return _g_job_file .idx(self) ; }
inline Crun  CrunData ::idx() const { return _g_run_file .idx(self) ; }
inline Cnode CnodeData::idx() const { return _g_node_file.idx(self) ; }

template<class... A> Crun::Crun( NewType , A&&... args ) {
	self = _g_run_file.emplace(::forward<A>(args)...) ;
}
