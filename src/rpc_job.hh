// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "hash.hh"
#include "serialize.hh"
#include "time.hh"

#include "autodep/env.hh"

#include "rpc_job_common.hh"

ENUM( CacheTag // PER_CACHE : add a tag for each cache method
,	None
,	Dir
)

// START_OF_VERSIONING
// PER_AUTODEP_METHOD : add entry here
// >=Ld means a lib is pre-loaded (through LD_AUDIT or LD_PRELOAD)
// by default, use a compromize between speed an reliability
#if HAS_LD_AUDIT
	ENUM_2( AutodepMethod , Ld=LdAudit   , Dflt=LdAudit   , None , Ptrace , LdAudit , LdPreload , LdPreloadJemalloc )
#else
	ENUM_2( AutodepMethod , Ld=LdPreload , Dflt=LdPreload , None , Ptrace ,           LdPreload , LdPreloadJemalloc )
#endif
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM_1( BackendTag // PER_BACKEND : add a tag for each backend
,	Dflt = Local
,	Unknown        // must be first
,	Local
,	Sge
,	Slurm
)
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM_1( FileActionTag
,	HasFile = Uniquify // <=HasFile means action acts on file
,	Src                // file is src, no action
,	Unlink             // used in ldebug, so it cannot be Unlnk
,	UnlinkWarning      // .
,	UnlinkPolluted     // .
,	None
,	NoUniquify         // no action, just warn if file has several links
,	Uniquify
,	Mkdir
,	Rmdir
)
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM( JobMngtProc
,	None
,	ChkDeps
,	DepVerbose
,	LiveOut
,	Decode
,	Encode
,	Heartbeat
,	Kill
)
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM( JobRpcProc
,	None
,	Start
,	ReportStart
,	GiveUp      // Req (all if 0) was killed and job was not (either because of other Req's or it did not start yet)
,	End
)
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM_3( JobReasonTag                           // see explanations in table below
,	HasNode = BusyDep                          // if >=HasNode, a node is associated
,	Err     = DepOverwritten
,	Missing = DepMissingStatic
	//
,	None
,	Retry                                      // job is retried in case of error if asked so by user
//	with reason
,	OldErr
,	Rsrcs
,	PollutedTargets
,	ChkDeps
,	CacheMatch
,	Cmd
,	Force
,	Killed
,	Lost
,	New
,	WasLost
//	with node
,	BusyDep                                    // job is waiting for a known dep
,	BusyTarget
,	NoTarget
,	OldTarget
,	PrevTarget
,	PollutedTarget
,	ManualTarget
,	ClashTarget
,	DepOutOfDate
,	DepTransient
,	DepUnlnked
,	DepUnstable
//	with error
,	DepOverwritten
,	DepDangling
,	DepErr
,	DepMissingRequired                         // this is actually an error
// with missing
,	DepMissingStatic                           // this prevents the job from being selected
)
// END_OF_VERSIONING
static constexpr ::amap<JobReasonTag,const char*,N<JobReasonTag>> JobReasonTagStrs = {{
	{ JobReasonTag::None               , "no reason"                                  }
,	{ JobReasonTag::Retry              , "job is retried after error"                 }
//	with reason
,	{ JobReasonTag::OldErr             , "job was in error"                           }
,	{ JobReasonTag::Rsrcs              , "resources changed and job was in error"     }
,	{ JobReasonTag::PollutedTargets    , "polluted targets"                           }
,	{ JobReasonTag::ChkDeps            , "dep check requires rerun"                   }
,	{ JobReasonTag::CacheMatch         , "cache reported a match but job did not run" }
,	{ JobReasonTag::Cmd                , "command changed"                            }
,	{ JobReasonTag::Force              , "job forced"                                 }
,	{ JobReasonTag::Killed             , "job was killed"                             }
,	{ JobReasonTag::Lost               , "job lost"                                   }
,	{ JobReasonTag::New                , "job was never run"                          }
,	{ JobReasonTag::WasLost            , "job was lost"                               }
//	with node
,	{ JobReasonTag::BusyDep            , "waiting dep"                                }
,	{ JobReasonTag::BusyTarget         , "busy target"                                }
,	{ JobReasonTag::NoTarget           , "missing target"                             }
,	{ JobReasonTag::OldTarget          , "target produced by an old job"              }
,	{ JobReasonTag::PrevTarget         , "target previously existed"                  }
,	{ JobReasonTag::PollutedTarget     , "polluted target"                            }
,	{ JobReasonTag::ManualTarget       , "target manually polluted"                   }
,	{ JobReasonTag::ClashTarget        , "multiple simultaneous writes"               }
,	{ JobReasonTag::DepOutOfDate       , "dep out of date"                            }
,	{ JobReasonTag::DepTransient       , "dep dir is a symbolic link"                 }
,	{ JobReasonTag::DepUnlnked         , "dep not on disk"                            }
,	{ JobReasonTag::DepUnstable        , "dep changed during job execution"           }
//	with error
,	{ JobReasonTag::DepOverwritten     , "dep has been overwritten"                   }
,	{ JobReasonTag::DepDangling        , "dep is dangling"                            }
,	{ JobReasonTag::DepErr             , "dep in error"                               }
,	{ JobReasonTag::DepMissingRequired , "required dep missing"                       }
// with missing
,	{ JobReasonTag::DepMissingStatic   , "static dep missing"                         }
}} ;
static_assert(chk_enum_tab(JobReasonTagStrs)) ;
static constexpr ::amap<JobReasonTag,uint8_t,N<JobReasonTag>> JobReasonTagPrios = {{
//	no reason, must be 0
	{ JobReasonTag::None               ,   0 }
,	{ JobReasonTag::Retry              ,   1 } // must be least prio, below other reasons to run as retries are limited (normally 0)
//	with reason
,	{ JobReasonTag::OldErr             ,  20 }
,	{ JobReasonTag::Rsrcs              ,  21 }
,	{ JobReasonTag::PollutedTargets    ,  22 }
,	{ JobReasonTag::ChkDeps            ,  41 }
,	{ JobReasonTag::CacheMatch         ,  40 }
,	{ JobReasonTag::Cmd                ,  63 }
,	{ JobReasonTag::Force              ,  61 }
,	{ JobReasonTag::Killed             ,  62 }
,	{ JobReasonTag::Lost               ,  60 }
,	{ JobReasonTag::New                , 100 }
,	{ JobReasonTag::WasLost            ,  60 }
//	with node
,	{ JobReasonTag::BusyDep            ,  11 }
,	{ JobReasonTag::BusyTarget         ,  10 } // this should not occur as there is certainly another reason to be running
,	{ JobReasonTag::NoTarget           ,  30 }
,	{ JobReasonTag::OldTarget          ,  31 }
,	{ JobReasonTag::PrevTarget         ,  32 }
,	{ JobReasonTag::PollutedTarget     ,  33 }
,	{ JobReasonTag::ManualTarget       ,  34 }
,	{ JobReasonTag::ClashTarget        ,  35 }
,	{ JobReasonTag::DepOutOfDate       ,  50 }
,	{ JobReasonTag::DepTransient       ,  51 }
,	{ JobReasonTag::DepUnlnked         ,  51 }
,	{ JobReasonTag::DepUnstable        ,  51 }
//	with error, must be higher than ok reasons
,	{ JobReasonTag::DepOverwritten     ,  70 }
,	{ JobReasonTag::DepDangling        ,  71 }
,	{ JobReasonTag::DepErr             ,  71 }
,	{ JobReasonTag::DepMissingRequired ,  71 }
// with missing, must be higher than err reasons
,	{ JobReasonTag::DepMissingStatic   ,  80 }
}} ;
static_assert(chk_enum_tab(JobReasonTagPrios)) ;

// START_OF_VERSIONING
ENUM( MatchKind
,	Target
,	SideTarget
,	SideDep
)
// END_OF_VERSIONING

ENUM( MountAction
,	Access
,	Read
,	Write
)

// START_OF_VERSIONING
ENUM_3( Status             // result of job execution
,	Early   = EarlyLostErr // <=Early means output has not been modified
,	Async   = Killed       // <=Async means job was interrupted asynchronously
,	Garbage = BadTarget    // <=Garbage means job has not run reliably
,	New                    // job was never run
,	EarlyChkDeps           // dep check failed before job actually started
,	EarlyErr               // job was not started because of error
,	EarlyLost              // job was lost before starting     , retry
,	EarlyLostErr           // job was lost before starting     , do not retry
,	LateLost               // job was lost after having started, retry
,	LateLostErr            // job was lost after having started, do not retry
,	Killed                 // job was killed
,	ChkDeps                // dep check failed
,	CacheMatch             // cache just reported deps, not result
,	BadTarget              // target was not correctly initialized or simultaneously written by another job
,	Ok                     // job execution ended successfully
,	SubmitLoop             // job needs to be rerun but we have already submitted it too many times
,	Err                    // job execution ended in error
)
// END_OF_VERSIONING
static constexpr ::amap<Status,::pair<Bool3/*ok*/,bool/*lost*/>,N<Status>> StatusAttrs = {{
	//                        ok    lost
	{ Status::New          , {Maybe,false} }
,	{ Status::EarlyChkDeps , {Maybe,false} }
,	{ Status::EarlyErr     , {No   ,false} }
,	{ Status::EarlyLost    , {Maybe,true } }
,	{ Status::EarlyLostErr , {No   ,true } }
,	{ Status::LateLost     , {Maybe,true } }
,	{ Status::LateLostErr  , {No   ,true } }
,	{ Status::Killed       , {Maybe,false} }
,	{ Status::ChkDeps      , {Maybe,false} }
,	{ Status::CacheMatch   , {Maybe,false} }
,	{ Status::BadTarget    , {Maybe,false} }
,	{ Status::Ok           , {Yes  ,false} }
,	{ Status::SubmitLoop   , {No   ,false} }
,	{ Status::Err          , {No   ,false} }
}} ;
static_assert(chk_enum_tab(StatusAttrs)) ;
inline Bool3 is_ok  (Status s) { return StatusAttrs[+s].second.first  ; }
inline bool  is_lost(Status s) { return StatusAttrs[+s].second.second ; }

static const ::string EnvPassMrkr = {'\0','p'} ; // special illegal value to ask for value from environment
static const ::string EnvDynMrkr  = {'\0','d'} ; // special illegal value to mark dynamically computed env variables

static constexpr char QuarantineDirS[] = ADMIN_DIR_S "quarantine/" ;

namespace Caches {

	using CacheKey = uint64_t ; // used to identify temporary data to upload

}

struct EndAttrs {
	friend ::string& operator+=( ::string& , EndAttrs const& ) ;
	bool operator+() const { return +cache || max_stderr_len!=Npos ; }
	// START_OF_VERSIONING
	::string cache          = {}   ;
	size_t   max_stderr_len = Npos ;
	// END_OF_VERSIONING
} ;

struct FileAction {
	friend ::string& operator+=( ::string& , FileAction const& ) ;
	// cxtors & casts
	FileAction(FileActionTag t={} , Hash::Crc c={} , Disk::FileSig s={} ) : tag{t} , crc{c} , sig{s}  {} // should be automatic but clang lacks some automatic conversions in some cases
	// data
	FileActionTag tag = {} ;
	Hash::Crc     crc ;                                                                                  // expected (else, quarantine)
	Disk::FileSig sig ;                                                                                  // .
} ;
/**/   ::pair_s<bool/*ok*/> do_file_actions( ::vector_s* /*out*/ unlnks , ::vmap_s<FileAction>&&    , Disk::NfsGuard&    ) ;
inline ::pair_s<bool/*ok*/> do_file_actions( ::vector_s& /*out*/ unlnks , ::vmap_s<FileAction>&& pa , Disk::NfsGuard& ng ) { return do_file_actions(/*out*/&unlnks,::move(pa),ng) ; }
inline ::pair_s<bool/*ok*/> do_file_actions(                              ::vmap_s<FileAction>&& pa , Disk::NfsGuard& ng ) { return do_file_actions(/*out*/nullptr,::move(pa),ng) ; }

struct AccDflags {
	// services
	AccDflags  operator| (AccDflags other) const { return { accesses|other.accesses , dflags|other.dflags } ; }
	AccDflags& operator|=(AccDflags other)       { self = self | other ; return self ;                        }
	// data
	// START_OF_VERSIONING
	Accesses accesses ;
	Dflags   dflags   ;
	// END_OF_VERSIONING
} ;

struct JobReason {
	friend ::string& operator+=( ::string& , JobReason const& ) ;
	using Tag = JobReasonTag ;
	// cxtors & casts
	JobReason(                   ) = default ;
	JobReason( Tag t             ) : tag{t}           { SWEAR( t< Tag::HasNode       , t     ) ; }
	JobReason( Tag t , NodeIdx n ) : tag{t} , node{n} { SWEAR( t>=Tag::HasNode && +n , t , n ) ; }
	// accesses
	bool operator+() const { return +tag                           ; }
	bool need_run () const { return +tag and tag<JobReasonTag::Err ; }
	// services
	JobReason operator|(JobReason jr) const {
		if (JobReasonTagPrios[+tag].second>=JobReasonTagPrios[+jr.tag].second) return self ; // at equal level, prefer older reason
		else                                                                   return jr   ;
	}
	JobReason& operator|=(JobReason jr) { self = self | jr ; return self ; }
	::string msg() const {
		if (tag<Tag::HasNode) SWEAR(node==0,tag,node) ;
		return JobReasonTagStrs[+tag].second ;
	}
	// data
	// START_OF_VERSIONING
	Tag     tag  = JobReasonTag::None ;
	NodeIdx node = 0                  ;
	// END_OF_VERSIONING
} ;

struct JobStats {
	using Delay = Time::Delay ;
	// data
	// START_OF_VERSIONING
	Delay  cpu   = {} ;
	Delay  job   = {} ; // elapsed in job
	Delay  total = {} ; // elapsed including overhead
	size_t mem   = 0  ; // in bytes
	// END_OF_VERSIONING
} ;

template<class B> struct DepDigestBase ;


ENUM( DepInfoKind
,	Crc
,	Sig
,	Info
)
struct DepInfo {
	friend ::string& operator+=( ::string& , DepInfo const& ) ;
	using Crc      = Hash::Crc      ;
	using FileSig  = Disk::FileSig  ;
	using FileInfo = Disk::FileInfo ;
	using Kind     = DepInfoKind    ;
	//cxtors & casts
	constexpr DepInfo(           ) :                    _crc {  } {}
	constexpr DepInfo(Crc      c ) : kind{Kind::Crc } , _crc {c } {}
	constexpr DepInfo(FileSig  s ) : kind{Kind::Sig } , _sig {s } {}
	constexpr DepInfo(FileInfo fi) : kind{Kind::Info} , _info{fi} {}
	//
	template<class B> DepInfo(DepDigestBase<B> const& ddb) {
		if      (!ddb.accesses) self = Crc()     ;
		else if ( ddb.is_crc  ) self = ddb.crc() ;
		else                    self = ddb.sig() ;
	}
	// accesses
	bool operator==(DepInfo const& di) const {                                          // if true => self and di are idential (but there may be false negative if one is a Crc)
		if (kind!=di.kind) {
			if ( kind==Kind::Crc || di.kind==Kind::Crc ) return exists()==di.exists() ; // this is all we can check with one Crc (and not the other)
			else                                         return sig   ()==di.sig   () ; // one is Sig, the other is Info, convert Info into Sig
		}
		switch (kind) {
			case Kind::Crc  : return crc ()==di.crc () ;
			case Kind::Sig  : return sig ()==di.sig () ;
			case Kind::Info : return info()==di.info() ;
		DF}
	}
	bool     operator+() const {                                return kind!=Kind::Crc || +_crc             ; }
	Crc      crc      () const { SWEAR(kind==Kind::Crc ,kind) ; return _crc                                 ; }
	FileSig  sig      () const { SWEAR(kind!=Kind::Crc ,kind) ; return kind==Kind::Sig ? _sig : _info.sig() ; }
	FileInfo info     () const { SWEAR(kind==Kind::Info,kind) ; return _info                                ; }
	//
	bool seen(Accesses a) const {                                                       // return true if accesses could perceive the existence of file
		if (!a) return false ;
		SWEAR(+self,self,a) ;
		switch (kind) {
			case Kind::Crc  : return !Crc::None.match( _crc             , a ) ;
			case Kind::Sig  : return !Crc::None.match( Crc(_sig .tag()) , a ) ;
			case Kind::Info : return !Crc::None.match( Crc(_info.tag()) , a ) ;
		DF}
	}
	Bool3 exists() const {
		switch (kind) {
			case Kind::Crc  : return +_crc ? No|(_crc!=Crc::None) : Maybe ;
			case Kind::Sig  : return         No| +_sig                    ;
			case Kind::Info : return         No| +_info                   ;
		DF}
	}
	// data
	// START_OF_VERSIONING
	DepInfoKind kind = Kind::Crc ;
private :
	union {
		Crc      _crc  ;                                                                // ~46< 64 bits
		FileSig  _sig  ;                                                                //      64 bits
		FileInfo _info ;                                                                //     128 bits
	} ;
	// END_OF_VERSIONING
} ;

// for Dep recording in book-keeping, we want to derive from Node
// but if we derive from Node and have a field DepDigest, it is impossible to have a compact layout because of alignment constraints
// hence this solution : derive from a template argument
template<class B> ::string& operator+=( ::string& , DepDigestBase<B> const& ) ;
template<class B> struct DepDigestBase : NoVoid<B> {
	friend ::string& operator+= <>( ::string& , DepDigestBase const& ) ;
	using Base = NoVoid<B> ;
	static constexpr bool    HasBase = !::is_same_v<B,void> ;
	//
	using Tag      = FileTag        ;
	using Crc      = Hash::Crc      ;
	using FileSig  = Disk::FileSig  ;
	using FileInfo = Disk::FileInfo ;
	//cxtors & casts
	constexpr DepDigestBase(                                                            bool p=false ) :                                       parallel{p} { crc    ({}) ; }
	constexpr DepDigestBase(          Accesses a ,                      Dflags dfs={} , bool p=false ) :           accesses{a} , dflags(dfs) , parallel{p} { crc    ({}) ; }
	constexpr DepDigestBase(          Accesses a , Crc             c  , Dflags dfs={} , bool p=false ) :           accesses{a} , dflags(dfs) , parallel{p} { crc    (c ) ; }
	constexpr DepDigestBase(          Accesses a , FileInfo const& fi , Dflags dfs={} , bool p=false ) :           accesses{a} , dflags(dfs) , parallel{p} { sig    (fi) ; }
	constexpr DepDigestBase(          Accesses a , DepInfo  const& di , Dflags dfs={} , bool p=false ) :           accesses{a} , dflags(dfs) , parallel{p} { crc_sig(di) ; }
	constexpr DepDigestBase( Base b , Accesses a ,                      Dflags dfs={} , bool p=false ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} { crc    ({}) ; }
	constexpr DepDigestBase( Base b , Accesses a , Crc             c  , Dflags dfs={} , bool p=false ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} { crc    (c ) ; }
	constexpr DepDigestBase( Base b , Accesses a , FileInfo const& fi , Dflags dfs={} , bool p=false ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} { sig    (fi) ; }
	constexpr DepDigestBase( Base b , Accesses a , DepInfo  const& di , Dflags dfs={} , bool p=false ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} { crc_sig(di) ; }
	// initializing _crc in all cases (which crc_date does not do) is important to please compiler (gcc-11 -O3)
	template<class B2> constexpr DepDigestBase(          DepDigestBase<B2> const& dd ) :         accesses{dd.accesses},dflags(dd.dflags),parallel{dd.parallel},hot{dd.hot},_crc{} { crc_sig(dd) ; }
	template<class B2> constexpr DepDigestBase( Base b , DepDigestBase<B2> const& dd ) : Base{b},accesses{dd.accesses},dflags(dd.dflags),parallel{dd.parallel},hot{dd.hot},_crc{} { crc_sig(dd) ; }
	//
	constexpr bool operator==(DepDigestBase const& other) const {
		if constexpr (HasBase) if (Base::operator!=(other) ) return false            ;
		/**/                   if (dflags  !=other.dflags  ) return false            ;
		/**/                   if (accesses!=other.accesses) return false            ;
		/**/                   if (parallel!=other.parallel) return false            ;
		/**/                   if (is_crc  !=other.is_crc  ) return false            ;
		/**/                   if (is_crc                  ) return _crc==other._crc ;
		/**/                                                 return _sig==other._sig ;
	}
	// accesses
	constexpr Crc     crc        () const { SWEAR( is_crc) ; return _crc                       ; }
	constexpr FileSig sig        () const { SWEAR(!is_crc) ; return _sig                       ; }
	constexpr bool    never_match() const { SWEAR( is_crc) ; return _crc.never_match(accesses) ; }
	//
	constexpr void crc    (Crc             c ) { is_crc = true  ; _crc = c        ; }
	constexpr void sig    (FileSig  const& s ) { is_crc = false ; _sig = s        ; }
	constexpr void sig    (FileInfo const& fi) { is_crc = false ; _sig = fi.sig() ; }
	constexpr void crc_sig(DepInfo  const& di) {
		if (di.kind==DepInfoKind::Crc) crc(di.crc()) ;
		else                           sig(di.sig()) ;
	}
	template<class B2> constexpr void crc_sig(DepDigestBase<B2> const& dd) {
		if (!dd.accesses) return ;
		if ( dd.is_crc  ) crc(dd.crc()) ;
		else              sig(dd.sig()) ;
	}
	// services
	constexpr DepDigestBase& operator|=(DepDigestBase const& ddb) {                // assumes ddb has been accessed after us
		if constexpr (HasBase) SWEAR(Base::operator==(ddb),self,ddb) ;
		if (!accesses) {
			crc_sig(ddb) ;
			parallel = ddb.parallel ;
		} else if (+ddb.accesses) {
			if      (is_crc!=ddb.is_crc)                         crc({}) ;         // destroy info if digests disagree
			else if (is_crc            ) { if (crc()!=ddb.crc()) crc({}) ; }       // .
			else                         { if (sig()!=ddb.sig()) crc({}) ; }       // .
			// parallel is kept untouched as ddb follows us
		}
		dflags   |= ddb.dflags   ;
		accesses |= ddb.accesses ;
		return self ;
	}
	constexpr void tag(Tag tag_) {
		SWEAR(!is_crc,self) ;
		if (!_sig) { crc(Crc::None) ; return ; }                                   // even if file appears, the whole job has been executed seeing the file as absent
		switch (tag_) {
			case Tag::Reg  :
			case Tag::Exe  :
			case Tag::Lnk  : if (!Crc::s_sense(accesses,tag_)) crc(tag_) ; break ; // just record the tag if enough to match (e.g. accesses==Lnk and tag==Reg)
			case Tag::None :
			case Tag::Dir  : if (+_sig                       ) crc({}  ) ; break ;
		DF}
	}
	// data
	// START_OF_VERSIONING
	static constexpr uint8_t NSzBits = 5 ;                                         // XXX! : set to 8 by making room by storing accesses on 3 bits rather than 8
	Accesses accesses               ;                                              // 3<8 bits
	Dflags   dflags                 ;                                              // 6<8 bits
	bool     parallel      :1       = false ;                                      //   1 bit
	bool     is_crc        :1       = true  ;                                      //   1 bit
	uint8_t  sz            :NSzBits = 0     ;                                      //   6 bits, number of items in chunk following header (semantically before)
	bool     hot           :1       = false ;                                      //   1 bit , if true <= file date was very close from access date (within date granularity)
	Accesses chunk_accesses         ;                                              // 3<8 bits
private :
	union {
		Crc     _crc = {} ;                                                        // ~45<64 bits
		FileSig _sig ;                                                             // ~40<64 bits
	} ;
	// END_OF_VERSIONING
} ;
template<class B> ::string& operator+=( ::string& os , DepDigestBase<B> const& dd ) {
	const char* sep = "" ;
	/**/                                          os << "D("                           ;
	if constexpr ( !::is_void_v<B>            ) { os <<sep<< static_cast<B const&>(dd) ; sep = "," ; }
	if           ( +dd.accesses               ) { os <<sep<< dd.accesses               ; sep = "," ; }
	if           ( +dd.dflags                 ) { os <<sep<< dd.dflags                 ; sep = "," ; }
	if           (  dd.parallel               ) { os <<sep<< "parallel"                ; sep = "," ; }
	if           ( +dd.accesses && !dd.is_crc ) { os <<sep<< dd.sig()                  ; sep = "," ; }
	else if      ( +dd.accesses && +dd.crc()  ) { os <<sep<< dd.crc()                  ; sep = "," ; }
	if           (  dd.hot                    )   os <<sep<< "hot"                     ;
	return                                        os <<')'                             ;
}

using DepDigest = DepDigestBase<void> ;
static_assert(::is_trivially_copyable_v<DepDigest>) ; // as long as this holds, we do not have to bother about union member cxtor/dxtor

struct TargetDigest {
	friend ::string& operator+=( ::string& , TargetDigest const& ) ;
	using Crc = Hash::Crc ;
	// data
	// START_OF_VERSIONING
	Tflags        tflags       = {}    ;
	ExtraTflags   extra_tflags = {}    ;
	bool          pre_exist    = false ; // if true <=  file was seen as existing while not incremental
	Crc           crc          = {}    ; // if None <=> file was unlinked, if Unknown => file is idle (not written, not unlinked)
	Disk::FileSig sig          = {}    ;
	// END_OF_VERSIONING
} ;

struct JobDigest {
	friend ::string& operator+=( ::string& , JobDigest const& ) ;
	// START_OF_VERSIONING
	::string               upload_key = {}          ;
	::vmap_s<DepDigest   > deps       = {}          ; // INVARIANT : sorted in first access order
	EndAttrs               end_attrs  = {}          ;
	Time::Pdate            end_date   = {}          ;
	JobStats               stats      = {}          ;
	Status                 status     = Status::New ;
	::string               stderr     = {}          ;
	::string               stdout     = {}          ;
	::vmap_s<TargetDigest> targets    = {}          ;
	int                    wstatus    = 0           ;
	// END_OF_VERSIONING
} ;

struct MatchFlags {
	friend ::string& operator+=( ::string& , MatchFlags const& ) ;
	// cxtors & casts
	MatchFlags(                                ) = default ;
	MatchFlags( Tflags tf , ExtraTflags etf={} ) : is_target{Yes} , _tflags{tf} , _extra_tflags{etf} {}
	MatchFlags( Dflags df , ExtraDflags edf={} ) : is_target{No } , _dflags{df} , _extra_dflags{edf} {}
	// accesses
	bool        operator+   () const {                              return is_target!=Maybe ; }
	Tflags      tflags      () const { SWEAR(is_target==Yes,self) ; return _tflags          ; }
	Dflags      dflags      () const { SWEAR(is_target==No ,self) ; return _dflags          ; }
	ExtraTflags extra_tflags() const { SWEAR(is_target==Yes,self) ; return _extra_tflags    ; }
	ExtraDflags extra_dflags() const { SWEAR(is_target==No ,self) ; return _extra_dflags    ; }
	// data
	// START_OF_VERSIONING
	Bool3 is_target = Maybe ;
private :
	Tflags      _tflags       ; // if  is_target
	Dflags      _dflags       ; // if !is_target
	ExtraTflags _extra_tflags ; // if  is_target
	ExtraDflags _extra_dflags ; // if !is_target
	// END_OF_VERSIONING
} ;

struct JobInfo ;

namespace Caches {

	struct Cache {
		using Sz  = Disk::DiskSz ;
		using Tag = CacheTag     ;
		struct Match {
			bool                completed = true ;                   //                            if false <=> answer is delayed and an action will be post to the main loop when ready
			Bool3               hit       = No   ;                   // if completed
			::vmap_s<DepDigest> new_deps  = {}   ;                   // if completed&&hit==Maybe : deps that were not done and need to be done before answering hit/miss
			::string            key       = {}   ;                   // if completed&&hit==Yes   : an id to easily retrieve matched results when calling download
		} ;
		// statics
		static Cache* s_new   ( Tag                                          ) ;
		static void   s_config( ::string const& key , Tag , ::vmap_ss const& ) ;
		// static data
		static ::map_s<Cache*> s_tab ;
		// services
		Match                  match   ( ::string const& job , ::vmap_s<DepDigest> const& repo_deps                        ) { Trace trace("Cache::match"  ,job) ; return sub_match(job,repo_deps) ; }
		JobInfo                download( ::string const& upload_key , Disk::NfsGuard& repo_nfs_guard                       ) ;
		::string/*upload_key*/ upload  ( ::vmap_s<TargetDigest> const& , ::vector<Disk::FileInfo> const& , uint8_t z_lvl=0 ) ;
		bool/*ok*/             commit  ( ::string const& key , ::string const& /*job*/ , JobInfo&&                         ) ;
		void                   dismiss ( ::string const& key                                                               ) { Trace trace("Cache::dismiss",key) ;        sub_dismiss(key)         ; }
		// default implementation : no caching, but enforce protocol
		virtual void config(::vmap_ss const&) {}
		virtual Tag  tag   (                ) { return Tag::None ; }
		virtual void serdes(::string     &  ) {}                     // serialize
		virtual void serdes(::string_view&  ) {}                     // deserialize
		//
		virtual Match                        sub_match   ( ::string const& /*job*/ , ::vmap_s<DepDigest> const&          ) { return { .completed=true , .hit=No } ; }
		virtual ::pair<JobInfo,AcFd>         sub_download( ::string const& /*key*/                                       ) ;
		virtual ::pair_s/*upload_key*/<AcFd> sub_upload  ( Sz /*max_sz*/                                                 ) { return {}                            ; }
		virtual bool/*ok*/                   sub_commit  ( ::string const& /*key*/ , ::string const& /*job*/ , JobInfo&& ) { return false                         ; }
		virtual void                         sub_dismiss ( ::string const& /*key*/                                       ) {                                        }
	} ;

}

struct JobSpace {
	friend ::string& operator+=( ::string& , JobSpace const& ) ;
	struct ViewDescr {
		friend ::string& operator+=( ::string& , ViewDescr const& ) ;
		bool operator+() const { return +phys ; }
		// data
		// START_OF_VERSIONING
		::vector_s phys    ;                   // (upper,lower...)
		::vector_s copy_up ;                   // dirs & files or dirs to create in upper (mkdir or cp <file> from lower...)
		// END_OF_VERSIONING
	} ;
	// accesses
	bool operator+() const { return +chroot_dir_s || +repo_view_s || +tmp_view_s || +views ; }
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,chroot_dir_s) ;
		::serdes(s,repo_view_s ) ;
		::serdes(s,tmp_view_s  ) ;
		::serdes(s,views       ) ;
	}
	bool/*entered*/ enter(
		::vmap_s<MountAction>&/*out*/ deps
	,	::string   const&             phy_repo_root_s
	,	::string   const&             phy_tmp_dir_s
	,	::string   const&             cwd_s
	,	::string   const&             work_dir_s
	,	::vector_s const&             src_dirs_s={}
	) ;
	void exit() {}
	//
	::vmap_s<::vector_s> flat_phys() const ;   // view phys after dereferencing indirections (i.e. if a/->b/ and b/->c/, returns a/->c/ and b/->c/)
	//
	void mk_canon(::string const& phy_repo_root_s) ;
private :
	bool           _is_lcl_tmp( ::string const&                                                              ) const ;
	bool/*dst_ok*/ _create    ( ::vmap_s<MountAction>& report , ::string const& dst , ::string const& src={} ) const ;
	// data
public :
	// START_OF_VERSIONING
	::string            chroot_dir_s = {} ;    // absolute dir which job chroot's to before execution (empty if unused)
	::string            repo_view_s  = {} ;    // absolute dir under which job sees repo root dir     (empty if unused)
	::string            tmp_view_s   = {} ;    // absolute dir under which job sees tmp dir           (empty if unused)
	::vmap_s<ViewDescr> views        = {} ;    // map logical views to physical locations ( file->(file,) or dir->(upper,lower...) )
	// END_OF_VERSIONING
} ;

struct JobRpcReq {
	bool operator+() const { return +seq_id ; }
	// START_OF_VERSIONING
	SeqId  seq_id = 0 ;
	JobIdx job    = 0 ;
	// END_OF_VERSIONING)
} ;

struct JobStartRpcReq : JobRpcReq {
	friend ::string& operator+=( ::string& , JobStartRpcReq const& ) ;
	// cxtors & casts
	JobStartRpcReq() = default ;
	JobStartRpcReq( JobRpcReq jrr , in_port_t pt=0 , ::string&& m={} ) : JobRpcReq{jrr} , port{pt} , msg{::move(m)} {}
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,static_cast<JobRpcReq&>(self)) ;
		::serdes(s,port                         ) ;
		::serdes(s,msg                          ) ;
	}
	// data
	// START_OF_VERSIONING
	in_port_t port = 0 ;
	::string  msg  ;
	// END_OF_VERSIONING)
} ;

struct JobStartRpcReply {
	friend ::string& operator+=( ::string& , JobStartRpcReply const& ) ;
	using Crc  = Hash::Crc  ;
	using Proc = JobRpcProc ;
	// accesses
	bool operator+() const { return +interpreter ; }                // there is always an interpreter for any job, even if no actual execution as is the case when downloaded from cache
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes(s,addr          ) ;
		::serdes(s,allow_stderr  ) ;
		::serdes(s,autodep_env   ) ;
		::serdes(s,cmd           ) ;
		::serdes(s,cwd_s         ) ;
		::serdes(s,ddate_prec    ) ;
		::serdes(s,deps          ) ;
		::serdes(s,end_attrs     ) ;
		::serdes(s,env           ) ;
		::serdes(s,interpreter   ) ;
		::serdes(s,job_space     ) ;
		::serdes(s,keep_tmp      ) ;
		::serdes(s,key           ) ;
		::serdes(s,kill_sigs     ) ;
		::serdes(s,live_out      ) ;
		::serdes(s,method        ) ;
		::serdes(s,network_delay ) ;
		::serdes(s,pre_actions   ) ;
		::serdes(s,small_id      ) ;
		::serdes(s,star_matches  ) ;
		::serdes(s,static_matches) ;
		::serdes(s,stdin         ) ;
		::serdes(s,stdout        ) ;
		::serdes(s,timeout       ) ;
		::serdes(s,use_script    ) ;
		::serdes(s,z_lvl         ) ;
		//
		CacheTag tag ;
		if (IsIStream<S>) {
			::serdes(s,tag) ;
			if (+tag) cache = Caches::Cache::s_new(tag) ;
		} else {
			tag = cache ? cache->tag() : CacheTag::None ;
			::serdes(s,tag) ;
		}
		if (+tag) cache->serdes(s) ;
	}
	bool/*entered*/ enter(
		::vmap_s<MountAction>&/*out*/
	,	::map_ss             &/*out*/ cmd_env
	,	::vmap_ss            &/*out*/ dynamic_env
	,	pid_t                &/*out*/ first_pid
	,	::string        const&        phy_repo_root_s
	,	::string        const&        lmake_root_s
	,	::string        const&        phy_tmp_dir_s
	,	SeqId
	) ;
	void exit() ;
	// data
	// START_OF_VERSIONING
	in_addr_t                addr           = 0                   ; // the address at which server and subproccesses can contact job_exec
	bool                     allow_stderr   = false               ; //
	AutodepEnv               autodep_env    ;                       //
	Caches::Cache*           cache          = nullptr             ;
	::pair_ss/*script,call*/ cmd            ;                       //
	::string                 cwd_s          ;                       //
	Time::Delay              ddate_prec     ;                       //
	::vmap_s<DepDigest>      deps           ;                       // deps already accessed (always includes static deps)
	EndAttrs                 end_attrs      ;
	::vmap_ss                env            ;                       //
	::vector_s               interpreter    ;                       // actual interpreter used to execute cmd
	JobSpace                 job_space      ;                       //
	bool                     keep_tmp       = false               ; //
	::string                 key            ;                       // key used to uniquely identify repo
	vector<uint8_t>          kill_sigs      ;                       //
	bool                     live_out       = false               ; //
	AutodepMethod            method         = AutodepMethod::Dflt ; //
	Time::Delay              network_delay  ;                       //
	::vmap_s<FileAction>     pre_actions    ;                       //
	SmallId                  small_id       = 0                   ; //
	::vmap_s<MatchFlags>     star_matches   ;                       // maps regexprs to flags
	::vmap_s<MatchFlags>     static_matches ;                       // maps individual files to flags
	::string                 stdin          ;                       //
	::string                 stdout         ;                       //
	Time::Delay              timeout        ;                       //
	bool                     use_script     = false               ; //
	uint8_t                  z_lvl          = 0                   ;
	// END_OF_VERSIONING
private :
	::string _tmp_dir_s ;                                           // for use in exit (autodep.tmp_dir_s may be moved)
} ;

struct ExecTraceEntry {
	// cxtors & casts
	ExecTraceEntry() = default ;
	ExecTraceEntry( Time::Pdate pd , ::string const& s , ::string const& f={} ) : date{pd} , step{       s } , file{f} {}
	ExecTraceEntry( Time::Pdate pd , ::string     && s , ::string const& f={} ) : date{pd} , step{::move(s)} , file{f} {}
	// accesses
	bool              operator== (ExecTraceEntry const&) const = default ;
	::strong_ordering operator<=>(ExecTraceEntry const&) const = default ;
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,date) ;
		::serdes(s,step) ;
		::serdes(s,file) ;
	}
	// data
	Time::Pdate date ;
	::string    step ;
	::string    file ;
} ;
struct JobEndRpcReq : JobRpcReq {
	using P   = JobRpcProc          ;
	using SI  = SeqId               ;
	using JI  = JobIdx              ;
	using MDD = ::vmap_s<DepDigest> ;
	friend ::string& operator+=( ::string& , JobEndRpcReq const& ) ;
	// cxtors & casts
	JobEndRpcReq() = default ;
	JobEndRpcReq( JobRpcReq jrr , JobDigest&& d  , ::string&& m={} ) : JobRpcReq{jrr} , digest{::move(d)} , msg{::move(m)} {}
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,static_cast<JobRpcReq&>(self)) ;
		::serdes(s,digest                       ) ;
		::serdes(s,phy_tmp_dir_s                ) ;
		::serdes(s,dynamic_env                  ) ;
		::serdes(s,msg                          ) ;
		::serdes(s,exec_trace                   ) ;
		::serdes(s,total_sz                     ) ;
		::serdes(s,compressed_sz                ) ;
	}
	// data
	// START_OF_VERSIONING
	JobDigest              digest        ;
	::string               phy_tmp_dir_s ;
	::vmap_ss              dynamic_env   ; // env variables computed in job_exec
	::string               msg           ;
	vector<ExecTraceEntry> exec_trace    ;
	Disk::DiskSz           total_sz      = 0 ;
	Disk::DiskSz           compressed_sz = 0 ;
	// END_OF_VERSIONING)
} ;

struct JobMngtRpcReq {
	using JMMR = JobMngtRpcReq       ;
	using P    = JobMngtProc         ;
	using SI   = SeqId               ;
	using JI   = JobIdx              ;
	using MDD  = ::vmap_s<DepDigest> ;
	friend ::string& operator+=( ::string& , JobMngtRpcReq const& ) ;
	// statics
	// cxtors & casts
	#define S ::string
	#define M ::move
	JobMngtRpcReq(                                ) = default ;
	JobMngtRpcReq( P p , SI si , JI j , Fd fd_={} ) : proc{p} , seq_id{si} , job{j} , fd{fd_} {}
	//
	JobMngtRpcReq( P p , SI si , JI j ,          S&& t    ) : proc{p} , seq_id{si} , job{j} ,           txt{M(t)}   { SWEAR(p==P::LiveOut                  ,p) ; }
	JobMngtRpcReq( P p , SI si , JI j , Fd fd_ , MDD&& ds ) : proc{p} , seq_id{si} , job{j} , fd{fd_} , deps{M(ds)} { SWEAR(p==P::ChkDeps||p==P::DepVerbose,p) ; }
	//
	JobMngtRpcReq(P p,SI si,JI j,Fd fd_,S&& code,S&& f,S&& c           ) : proc{p},seq_id{si},job{j},fd{fd_},ctx{M(c)},file{M(f)},txt{M(code)}             { SWEAR(p==P::Decode,p) ; }
	JobMngtRpcReq(P p,SI si,JI j,Fd fd_,S&& val ,S&& f,S&& c,uint8_t ml) : proc{p},seq_id{si},job{j},fd{fd_},ctx{M(c)},file{M(f)},txt{M(val )},min_len{ml} { SWEAR(p==P::Encode,p) ; }
	#undef M
	#undef S
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,proc  ) ;
		::serdes(s,seq_id) ;
		::serdes(s,job   ) ;
		switch (proc) {
			case P::None       :
			case P::Heartbeat  :                    break ;
			case P::LiveOut    : ::serdes(s,txt ) ; break ;
			case P::ChkDeps    :
			case P::DepVerbose :
				::serdes(s,fd  ) ;
				::serdes(s,deps) ;
			break ;
			case P::Encode :
				::serdes(s,min_len) ;
				[[fallthrough]] ;
			case P::Decode :
				::serdes(s,fd  ) ;
				::serdes(s,ctx ) ;
				::serdes(s,file) ;
				::serdes(s,txt ) ;
			break ;
		DF}
	}
	// data
	P                   proc    = P::None ;
	SI                  seq_id  = 0       ;
	JI                  job     = 0       ;
	Fd                  fd      ;           // fd to which reply must be forwarded
	::vmap_s<DepDigest> deps    ;           // proc==ChkDeps|DepVerbose
	::string            ctx     ;           // proc==                           Decode|Encode
	::string            file    ;           // proc==                           Decode|Encode
	::string            txt     ;           // proc==                   LiveOut|Decode|Encode
	uint8_t             min_len = 0       ; // proc==                                  Encode
} ;

struct JobMngtRpcReply {
	friend ::string& operator+=( ::string& , JobMngtRpcReply const& ) ;
	using Crc  = Hash::Crc   ;
	using Proc = JobMngtProc ;
	// cxtors & casts
	JobMngtRpcReply() = default ;
	//
	JobMngtRpcReply( Proc p , SeqId si ) : proc{p} , seq_id{si} { SWEAR(p==Proc::Kill||p==Proc::Heartbeat,p) ; }
	//
	JobMngtRpcReply( Proc p , SeqId si , Fd fd_ , Bool3                                  o  ) : proc{p},seq_id{si},fd{fd_},ok{o}               { SWEAR(p==Proc::ChkDeps                   ,p) ; }
	JobMngtRpcReply( Proc p , SeqId si , Fd fd_ , ::vector<pair<Bool3/*ok*/,Crc>> const& is ) : proc{p},seq_id{si},fd{fd_},      dep_infos{is} { SWEAR(p==Proc::DepVerbose                ,p) ; }
	JobMngtRpcReply( Proc p , SeqId si , Fd fd_ , ::string const& t  , Crc c , Bool3 o      ) : proc{p},seq_id{si},fd{fd_},ok{o},txt{t},crc{c} { SWEAR(p==Proc::Decode||proc==Proc::Encode,p) ; }
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes(s,proc  ) ;
		::serdes(s,seq_id) ;
		switch (proc) {
			case Proc::None       :
			case Proc::Kill       :
			case Proc::Heartbeat  : break ;
			case Proc::DepVerbose :
				::serdes(s,fd       ) ;
				::serdes(s,dep_infos) ;
			break ;
			case Proc::ChkDeps :
				::serdes(s,fd) ;
				::serdes(s,ok) ;
			break ;
			case Proc::Decode :
			case Proc::Encode :
				::serdes(s,fd ) ;
				::serdes(s,ok ) ;
				::serdes(s,txt) ;
				::serdes(s,crc) ;
			break ;
		DF}
	}
	// data
	Proc                            proc      = {}    ;
	SeqId                           seq_id    = 0     ;
	Fd                              fd        ;         // proc == ChkDeps|DepVerbose|Decode|Encode , fd to which reply must be forwarded
	::vector<pair<Bool3/*ok*/,Crc>> dep_infos ;         // proc ==         DepVerbose
	Bool3                           ok        = Maybe ; // proc == ChkDeps|           Decode|Encode , if No <=> deps in error, if Maybe <=> deps not ready
	::string                        txt       ;         // proc ==                    Decode|Encode , value for Decode, code for Encode
	Crc                             crc       ;         // proc ==                    Decode|Encode , crc of txt
} ;

struct SubmitAttrs {
	friend ::string& operator+=( ::string& , SubmitAttrs const& ) ;
	// services
	SubmitAttrs& operator|=(SubmitAttrs const& other) {
		// cache, deps and tag are independent of req but may not always be present
		if      (!cache) cache = other.cache ; else if (+other.cache) SWEAR(cache==other.cache,cache,other.cache) ;
		if      (!deps ) deps  = other.deps  ; else if (+other.deps ) SWEAR(deps ==other.deps ,deps ,other.deps ) ;
		if      (!tag  ) tag   = other.tag   ; else if (+other.tag  ) SWEAR(tag  ==other.tag  ,tag  ,other.tag  ) ;
		live_out  |= other.live_out                   ;
		pressure   = ::max(pressure ,other.pressure ) ;
		reason    |= other.reason                     ;
		tokens1    = ::max(tokens1  ,other.tokens1  ) ;
		return self ;
	}
	SubmitAttrs operator|(SubmitAttrs const& other) const {
		SubmitAttrs res = self ;
		res |= other ;
		return res ;
	}
	// data
	// START_OF_VERSIONING
	::string            cache    = {}    ;
	::vmap_s<DepDigest> deps     = {}    ;
	bool                live_out = false ;
	Time::CoarseDelay   pressure = {}    ;
	JobReason           reason   = {}    ;
	BackendTag          tag      = {}    ;
	Tokens1             tokens1  = 0     ;
	// END_OF_VERSIONING
} ;

struct JobInfoStart {
	friend ::string& operator+=( ::string& , JobInfoStart const& ) ;
	// accesses
	bool operator+() const { return +pre_start ; }
	// data
	// START_OF_VERSIONING
	Hash::Crc        rule_cmd_crc = {} ;
	::vector_s       stems        = {} ;
	Time::Pdate      eta          = {} ;
	SubmitAttrs      submit_attrs = {} ;
	::vmap_ss        rsrcs        = {} ;
	in_addr_t        host         = 0  ;
	JobStartRpcReq   pre_start    = {} ;
	JobStartRpcReply start        = {} ;
	::string         stderr       = {} ;
	// END_OF_VERSIONING
} ;

struct JobInfo {
	// cxtors & casts
	JobInfo() = default ;
	JobInfo( ::string const& ancillary_file , Bool3 get_start=Maybe , Bool3 get_end=Maybe ) ;
	JobInfo( JobInfoStart&& jis                                                           ) : start{::move(jis)}                     {}
	JobInfo(                      JobEndRpcReq&& jerr                                     ) :                      end{::move(jerr)} {}
	JobInfo( JobInfoStart&& jis , JobEndRpcReq&& jerr                                     ) : start{::move(jis)} , end{::move(jerr)} {}
	// services
	template<IsStream T> void serdes(T& s) {
		::serdes(s,start) ;
		::serdes(s,end  ) ;
	}
	// data
	// START_OF_VERSIONING
	JobInfoStart start ;
	JobEndRpcReq end   ;
	// END_OF_VERSIONING
} ;

//
// codec
//

namespace Codec {

	static constexpr char CodecPfx[] = ADMIN_DIR_S "codec/" ;

	::string mk_decode_node( ::string const& file , ::string const& ctx , ::string const& code ) ;
	::string mk_encode_node( ::string const& file , ::string const& ctx , ::string const& val  ) ;

	::string mk_file(::string const& node) ; // node may have been obtained from mk_decode_node or mk_encode_node

}

//
// implementation
//

inline ::pair<JobInfo,AcFd> Caches::Cache::sub_download(::string const& /*key*/) { return {} ; }
