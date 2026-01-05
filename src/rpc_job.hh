// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "hash.hh"
#include "re.hh"
#include "serialize.hh"
#include "time.hh"
#include "trace.hh"

#include "autodep/env.hh"

#include "rpc_job_common.hh"

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
// PER_AUTODEP_METHOD : add entry here
// >=Ld means a lib is pre-loaded (through LD_AUDIT or LD_PRELOAD)
// by default, use a compromize between speed an reliability
#if HAS_LD_AUDIT
	enum class AutodepMethod : uint8_t { None , Ptrace , LdAudit , LdPreload , LdPreloadJemalloc , Ld=LdAudit   , Dflt=LdAudit   } ;
#else
	enum class AutodepMethod : uint8_t { None , Ptrace ,           LdPreload , LdPreloadJemalloc , Ld=LdPreload , Dflt=LdPreload } ;
#endif
// END_OF_VERSIONING

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
enum class BackendTag : uint8_t { // PER_BACKEND : add a tag for each backend
	Unknown                       // must be first
,	Local
,	Sge
,	Slurm
//
// aliases
,	Dflt   = Local
,	Remote = Sge                  // if >=Remote, backend is remote
} ;
// END_OF_VERSIONING

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
enum class CacheHitInfo : uint8_t {
	Hit                             // cache hit
,	Match                           // cache matches, but not hit (some deps are missing, hence dont know if hit or miss)
,	BadDeps
,	NoJob
,	NoRule
,	BadDownload
,	NoDownload
,	BadCache
,	NoCache
// aliases
,	Miss = BadDeps                  // >=Miss means cache miss
} ;
// END_OF_VERSIONING
static constexpr ::amap<CacheHitInfo,const char*,N<CacheHitInfo>> CacheHitInfoStrs = {{
	{ CacheHitInfo::Hit         , "hit"                                      }
,	{ CacheHitInfo::Match       , "deps are uncertain"                       }
,	{ CacheHitInfo::BadDeps     , "deps do not match"                        }
,	{ CacheHitInfo::NoJob       , "job (rule+stems) not found"               }
,	{ CacheHitInfo::NoRule      , "rule not found or with different command" }
,	{ CacheHitInfo::BadDownload , "download failed"                          }
,	{ CacheHitInfo::NoDownload  , "no download asked by user"                }
,	{ CacheHitInfo::BadCache    , "cache not found"                          }
,	{ CacheHitInfo::NoCache     , "no cache asked by user"                   }
}} ;
static_assert(chk_enum_tab(CacheHitInfoStrs)) ;

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
enum class ChrootAction : uint8_t {
	ResolvConf                      // /etc/resolv.conf is copied from native env to chroot'ed env
,	UserName                        // user and root and their groups have a name, existing ones are not preserved
} ;
using ChrootActions = BitMap<ChrootAction> ;

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
enum class FileActionTag : uint8_t {
	Src                              // file is src, no action
,	None                             // same as unlink except expect file not to exist
,	Unlink                           // used in ldebug, so it cannot be Unlnk
,	UnlinkWarning                    // .
,	UnlinkPolluted                   // .
,	Uniquify
,	Mkdir
,	Rmdir
//
// aliases
,	HasFile = Uniquify               // <=HasFile means action acts on file
} ;
// END_OF_VERSIONING

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
enum class JobInfoKind : uint8_t {
	None
,	Start
,	End
,	DepCrcs
} ;
// END_OF_VERSIONING
using JobInfoKinds = BitMap<JobInfoKind> ;

// START_OF_VERSIONING REPO CACHE
enum class JobMngtProc : uint8_t {
	None
,	ChkDeps
,	ChkTargets // used in JobMngtRpcReply to signal a pre-existing target
,	DepDirect
,	DepVerbose
,	LiveOut
,	AddLiveOut // report missing live_out info (Req) or tell job_exec to send missing live_out info (Reply)
,	Heartbeat
,	Kill
} ;
// END_OF_VERSIONING

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
enum class JobRpcProc : uint8_t {
	None
,	Start
,	ReportStart
,	GiveUp      // Req (all if 0) was killed and job was not (either because of other Req's or it did not start yet)
,	End
} ;
// END_OF_VERSIONING

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
enum class JobReasonTag : uint8_t {            // see explanations in table below
	None
,	Retry                                      // job is retried in case of error      if asked so by user
,	LostRetry                                  // job is retried in case of lost_error if asked so by user
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
,	WasIncremental
,	WasLost
//	with node
,	BusyTarget
,	NoTarget
,	OldTarget
,	PrevTarget
,	PollutedTarget
,	ManualTarget
,	ClashTarget
,	BusyDep                                    // job is waiting for an unknown dep
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
//
// aliases
,	HasNode = BusyTarget                       // if >=HasNode, a node is associated
,	HasDep  = BusyDep                          // if >=HasDep , a dep  is associated
,	Err     = DepOverwritten
,	Missing = DepMissingStatic
} ;
// END_OF_VERSIONING
static constexpr ::amap<JobReasonTag,const char*,N<JobReasonTag>> JobReasonTagStrs = {{
	{ JobReasonTag::None               , "no reason"                                  }
,	{ JobReasonTag::Retry              , "job is retried after error"                 }
,	{ JobReasonTag::LostRetry          , "job is retried after lost_error"            }
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
,	{ JobReasonTag::WasIncremental     , "job was built incremental"                  }
,	{ JobReasonTag::WasLost            , "job was lost"                               }
//	with node
,	{ JobReasonTag::BusyTarget         , "busy target"                                }
,	{ JobReasonTag::NoTarget           , "missing target"                             }
,	{ JobReasonTag::OldTarget          , "target produced by an old job"              }
,	{ JobReasonTag::PrevTarget         , "target previously existed"                  }
,	{ JobReasonTag::PollutedTarget     , "polluted target"                            }
,	{ JobReasonTag::ManualTarget       , "target manually polluted"                   }
,	{ JobReasonTag::ClashTarget        , "multiple simultaneous writes"               }
,	{ JobReasonTag::BusyDep            , "waiting dep"                                }
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
,	{ JobReasonTag::LostRetry          ,   1 } // .
//	with reason
,	{ JobReasonTag::OldErr             ,  20 }
,	{ JobReasonTag::Rsrcs              ,  21 }
,	{ JobReasonTag::PollutedTargets    ,  22 }
,	{ JobReasonTag::ChkDeps            ,  41 }
,	{ JobReasonTag::CacheMatch         ,  40 }
,	{ JobReasonTag::Cmd                ,  64 }
,	{ JobReasonTag::Force              ,  62 }
,	{ JobReasonTag::Killed             ,  63 }
,	{ JobReasonTag::Lost               ,  61 }
,	{ JobReasonTag::New                , 100 }
,	{ JobReasonTag::WasIncremental     ,  60 } // job was built incrementally but asked not-incremental
,	{ JobReasonTag::WasLost            ,  61 }
//	with node
,	{ JobReasonTag::BusyTarget         ,  10 } // this should not occur as there is certainly another reason to be running
,	{ JobReasonTag::NoTarget           ,  30 }
,	{ JobReasonTag::OldTarget          ,  31 }
,	{ JobReasonTag::PrevTarget         ,  32 }
,	{ JobReasonTag::PollutedTarget     ,  33 }
,	{ JobReasonTag::ManualTarget       ,  34 }
,	{ JobReasonTag::ClashTarget        ,  35 }
,	{ JobReasonTag::BusyDep            ,  11 }
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
inline bool is_retry(JobReasonTag jrt) { return jrt==JobReasonTag::Retry || jrt==JobReasonTag::LostRetry ; }

enum class MountAction : uint8_t {
	Access
,	Read
,	Write
} ;

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
enum class Status : uint8_t { // result of job execution
	New                       // job was never run
,	EarlyChkDeps              // dep check failed before job actually started
,	EarlyErr                  // job was not started because of error
,	EarlyLost                 // job was lost before starting     , retry
,	EarlyLostErr              // job was lost before starting     , do not retry
,	LateLost                  // job was lost after having started, retry
,	LateLostErr               // job was lost after having started, do not retry
,	Killed                    // job was killed
,	ChkDeps                   // dep check failed
,	CacheMatch                // cache just reported deps, not result
,	BadTarget                 // target was not correctly initialized or simultaneously written by another job
,	Ok                        // job execution ended successfully
,	RunLoop                   // job needs to be rerun but we have already run       it too many times
,	SubmitLoop                // job needs to be rerun but we have already submitted it too many times
,	Err                       // job execution ended in error
//
// aliases
,	Early   = EarlyLostErr    // <=Early means output has not been modified
,	Async   = Killed          // <=Async means job was interrupted asynchronously
,	Garbage = BadTarget       // <=Garbage means job has not run reliably
} ;
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
,	{ Status::RunLoop      , {No   ,false} }
,	{ Status::SubmitLoop   , {No   ,false} }
,	{ Status::Err          , {No   ,false} }
}} ;
static_assert(chk_enum_tab(StatusAttrs)) ;
inline Bool3 is_ok  (Status s) { return StatusAttrs[+s].second.first  ; }
inline bool  is_lost(Status s) { return StatusAttrs[+s].second.second ; }

// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
enum class ZlvlTag : uint8_t {
	None
,	Zlib
,	Zstd
// aliases
,	Dflt =
		#if HAS_STD
			Zstd
		#elif HAS_ZLIB
			Zlib
		#else
			None
		#endif
} ;
// END_OF_VERSIONING

static const ::string PassMrkr = {'\0','p'} ; // special illegal value to ask for value from environment
static const ::string DynMrkr  = {'\0','d'} ; // special illegal value to mark dynamically computed variable

namespace Caches {

	using CacheKey = uint64_t ; // used to identify temporary data to upload

}

//
// get_os_info
//

::string get_os_info() ;

//
// quarantine
//

void quarantine( ::string const& file , NfsGuard* =nullptr ) ;

//
// mk_simple_cmd_line
//

// replace call to bash by direct execution if a single command can be identified
// cmd_line initially contains interpreter and finally contains the enire cmd line
bool/*is_simple*/ mk_simple_cmd_line( ::vector_s&/*inout*/ cmd_line , ::string&& cmd , ::string const& std_shell , ::vmap_ss const& cmd_env ) ;

struct FileAction {
	friend ::string& operator+=( ::string& , FileAction const& ) ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	FileActionTag tag    = {} ;
	Tflags        tflags = {} ;
	Hash::Crc     crc    = {} ; // expected (else, quarantine)
	Disk::FileSig sig    = {} ; // .
	// END_OF_VERSIONING
} ;
// incremental means existing increment targets have been seen
::string do_file_actions( ::vector_s&/*out*/ unlnks , bool&/*out*/ incremental , ::vmap_s<FileAction>&& , NfsGuard* ) ;

struct ChrootInfo {
	friend ::string& operator+=( ::string& , ChrootInfo const& ) ;
	template<IsStream S> void serdes(S& s) {
		::serdes( s , dir_s ) ;
		if (+dir_s) {
			/**/                                  ::serdes( s , actions    ) ;
			if (+actions[ChrootAction::UserName]) ::serdes( s , user,group ) ;
		}
	}
	// accesses
	bool operator+() const { return +dir_s ; }
	// data
	::string      dir_s   ;      // absolute dir which job must chroot to before execution (empty if unused)
	ChrootActions actions = {} ; // valid if +dir_s
	::string      user    ;      // valid if +dir_s and +action, user  name to transport in namespace
	::string      group   ;      // .                          , group name to transport in namespace
} ;

struct AccDflags {
	// services
	AccDflags  operator| (AccDflags other) const { return { accesses|other.accesses , dflags|other.dflags } ; }
	AccDflags& operator|=(AccDflags other)       { self = self | other ; return self ;                        }
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	Accesses accesses ;
	Dflags   dflags   ;
	// END_OF_VERSIONING
} ;

struct JobReason {
	friend ::string& operator+=( ::string& , JobReason const& ) ;
	using Tag = JobReasonTag ;
	// cxtors & casts
	JobReason() = default ;
	JobReason( Tag t             ) :           tag{t} { SWEAR( t< Tag::HasNode          , t        ) ; }
	JobReason( Tag t , NodeIdx n ) : node{n} , tag{t} { SWEAR( t>=Tag::HasNode && +node , t , node ) ; }
	// accesses
	bool operator+() const { return +tag                           ; }
	bool need_run () const { return +tag and tag<JobReasonTag::Err ; }
	// services
	template<IsStream S> void serdes(S& s) {
		/**/                            ::serdes( s , tag  ) ;
		if (tag>=JobReasonTag::HasNode) ::serdes( s , node ) ;
	}
	JobReason operator|(JobReason jr) const {
		if (JobReasonTagPrios[+tag].second>=JobReasonTagPrios[+jr.tag].second) return self ; // at equal level, prefer older reason
		else                                                                   return jr   ;
	}
	JobReason& operator|=(JobReason jr) { self = self | jr ; return self ; }
	::string msg() const {
		if (tag<Tag::HasNode) SWEAR( !node , tag,node ) ;
		return JobReasonTagStrs[+tag].second ;
	}
	void chk() const ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	NodeIdx node = 0                  ;
	Tag     tag  = JobReasonTag::None ;
	// END_OF_VERSIONING
} ;

struct JobStats {
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	size_t            mem = 0  ; // in bytes
	Time::CoarseDelay cpu = {} ;
	Time::CoarseDelay job = {} ; // elapsed in job
	// END_OF_VERSIONING
} ;

struct MsgStderr {
	friend ::string& operator+=( ::string& , MsgStderr const& ) ;
	// accesses
	bool operator+() const { return +msg || +stderr ; }
	// data
	::string msg    = {} ;
	::string stderr = {} ;
} ;

template<class B> struct DepDigestBase ;

enum class DepInfoKind : uint8_t {
	Crc
,	Sig
,	Info
} ;
struct DepInfo : ::variant< Hash::Crc , Disk::FileSig , Disk::FileInfo > {
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	using Base = ::variant< Hash::Crc , Disk::FileSig , Disk::FileInfo > ;
	// END_OF_VERSIONING
	friend ::string& operator+=( ::string& , DepInfo const& ) ;
	//
	using Kind     = DepInfoKind    ;
	using Crc      = Hash::Crc      ;
	using FileSig  = Disk::FileSig  ;
	using FileInfo = Disk::FileInfo ;
	//cxtors & casts
	constexpr DepInfo() : Base{Crc()} {}
	using Base::Base ;
	//
	template<class B> DepInfo(DepDigestBase<B> const& ddb) {
		if      (!ddb.accesses) self = Crc()     ;
		else if ( ddb.is_crc  ) self = ddb.crc() ;
		else                    self = ddb.sig() ;
	}
	// accesses
	bool operator==(DepInfo const& di) const { // if true => self and di are identical (but there may be false negative if one is a Crc)
		if      ( kind()==di.kind()                         ) return static_cast<Base const&>(self)==static_cast<Base const&>(di) ;
		else if ( is_a<Kind::Crc>() || di.is_a<Kind::Crc>() ) return exists()==No && di.exists()==No                              ; // this is all we can check with one Crc (and not the other)
		else                                                  return sig()==di.sig()                                              ; // if one is Info and the other is Sig, convert Info into Sig
	}
	bool operator+() const { return !is_a<Kind::Crc>() || +crc() ; }
	//
	/**/             Kind kind() const { return Kind(index()) ; }
	template<Kind K> bool is_a() const { return index()==+K   ; }
	//
	Crc      crc () const { return ::get<Crc     >(self) ; }
	FileInfo info() const { return ::get<FileInfo>(self) ; }
	FileSig  sig () const {
		if (is_a<Kind::Sig >()) return ::get<FileSig >(self)       ;
		if (is_a<Kind::Info>()) return ::get<FileInfo>(self).sig() ;
		FAIL(self) ;                                                                                                                // NO_COV
	}
	//
	bool seen(Accesses a) const {                                                                                                   // return true if accesses could perceive the existence of file
		if (!a) return false ;
		SWEAR( +self , self,a ) ;
		switch (kind()) {
			case Kind::Crc  : return !Crc::None.match( crc()             , a ) ;
			case Kind::Sig  : return !Crc::None.match( Crc(sig ().tag()) , a ) ;
			case Kind::Info : return !Crc::None.match( Crc(info().tag()) , a ) ;
		DF}                                                                                                                         // NO_COV
	}
	Bool3 exists() const {
		switch (kind()) {
			case Kind::Crc  : return +crc() ? No|(crc()!=Crc::None) : Maybe ;
			case Kind::Sig  : return          No|+sig()                     ;
			case Kind::Info : return          No|info().exists()            ;
		DF}                                                                                                                         // NO_COV
	}
} ;

// for Dep recording in book-keeping, we want to derive from Node
// but if we derive from Node and have a field DepDigest, it is impossible to have a compact layout because of alignment constraints
// hence this solution : derive from a template argument
template<class B> struct DepDigestBase : NoVoid<B> {
	using Base = NoVoid<B> ;
	static constexpr bool    HasBase = !::is_same_v<B,void> ;
	//
	using Tag      = FileTag        ;
	using Crc      = Hash::Crc      ;
	using FileSig  = Disk::FileSig  ;
	using FileInfo = Disk::FileInfo ;
	//cxtors & casts
	constexpr DepDigestBase(                                                                             bool p=false ) :                                       parallel{p} { del_crc    (    ) ; }
	constexpr DepDigestBase(          Accesses a ,                               Dflags dfs=DflagsDflt , bool p=false ) :           accesses{a} , dflags(dfs) , parallel{p} { del_crc    (    ) ; }
	constexpr DepDigestBase(          Accesses a , Crc             c  , bool e , Dflags dfs=DflagsDflt , bool p=false ) :           accesses{a} , dflags(dfs) , parallel{p} { set_crc    (c ,e) ; }
	constexpr DepDigestBase(          Accesses a , FileInfo const& fi ,          Dflags dfs=DflagsDflt , bool p=false ) :           accesses{a} , dflags(dfs) , parallel{p} { set_sig    (fi  ) ; }
	constexpr DepDigestBase(          Accesses a , DepInfo  const& di , bool e , Dflags dfs=DflagsDflt , bool p=false ) :           accesses{a} , dflags(dfs) , parallel{p} { set_crc_sig(di,e) ; }
	constexpr DepDigestBase( Base b , Accesses a ,                               Dflags dfs=DflagsDflt , bool p=false ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} { del_crc    (    ) ; }
	constexpr DepDigestBase( Base b , Accesses a , Crc             c  , bool e , Dflags dfs=DflagsDflt , bool p=false ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} { set_crc    (c ,e) ; }
	constexpr DepDigestBase( Base b , Accesses a , FileInfo const& fi ,          Dflags dfs=DflagsDflt , bool p=false ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} { set_sig    (fi  ) ; }
	constexpr DepDigestBase( Base b , Accesses a , DepInfo  const& di , bool e , Dflags dfs=DflagsDflt , bool p=false ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} { set_crc_sig(di,e) ; }
	// initializing _crc in all cases (which crc_sig does not do) is important to please compiler (gcc-11 -O3)
	template<class B2> constexpr DepDigestBase(          DepDigestBase<B2> const& dd ) :         accesses{dd.accesses},dflags(dd.dflags),parallel{dd.parallel},hot{dd.hot},_crc{} { set_crc_sig(dd) ; }
	template<class B2> constexpr DepDigestBase( Base b , DepDigestBase<B2> const& dd ) : Base{b},accesses{dd.accesses},dflags(dd.dflags),parallel{dd.parallel},hot{dd.hot},_crc{} { set_crc_sig(dd) ; }
	//
	constexpr bool operator==(DepDigestBase const& ddb) const {
		SWEAR( !sz     , self ) ;
		SWEAR( !ddb.sz , ddb  ) ;
		if constexpr (HasBase) if (Base::operator!=(ddb) ) return false          ;
		/**/                   if (dflags  !=ddb.dflags  ) return false          ;
		/**/                   if (accesses!=ddb.accesses) return false          ;
		/**/                   if (parallel!=ddb.parallel) return false          ;
		/**/                   if (is_crc  !=ddb.is_crc  ) return false          ;
		/**/                   if (is_crc                ) return _crc==ddb._crc ;
		/**/                                               return _sig==ddb._sig ;
	}
	// accesses
	constexpr Crc     crc        () const { SWEAR( is_crc) ; return _crc                       ; }
	constexpr FileSig sig        () const { SWEAR(!is_crc) ; return _sig                       ; }
	constexpr bool    never_match() const { SWEAR( is_crc) ; return _crc.never_match(accesses) ; }
	//
	constexpr void set_crc    ( Crc             c  , bool e ) { is_crc = true  ; _crc = c        ; err = e ; }
	constexpr void set_sig    ( FileSig  const& s           ) { is_crc = false ; _sig = s        ;           }
	constexpr void set_sig    ( FileInfo const& fi          ) { is_crc = false ; _sig = fi.sig() ;           }
	constexpr void set_crc_sig( DepInfo  const& di , bool e ) {
		if (di.is_a<DepInfoKind::Crc>()) set_crc(di.crc(),e) ;
		else                             set_sig(di.sig()  ) ;
	}
	template<class B2> constexpr void set_crc_sig( DepDigestBase<B2> const& dd ) {
		if (!dd.accesses) return ;
		if ( dd.is_crc  ) set_crc(dd.crc(),dd.err) ;
		else              set_sig(dd.sig()       ) ;
	} //!                                                                                                                                                 err
	constexpr void del_crc        (                 ) {                                                                                    set_crc    ({},false) ; }
	constexpr void may_set_crc    (Crc            c ) { if (!(                                c       .valid() && accesses[Access::Err] )) set_crc    (c ,false) ; } // only set crc if err is useless
	constexpr void may_set_crc_sig(DepInfo const& di) { if (!( di.is_a<DepInfoKind::Crc>() && di.crc().valid() && accesses[Access::Err] )) set_crc_sig(di,false) ; } // .
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes( s , sz,accesses,dflags ) ;
		// bitfields cannot be serialized directly as no ref is allowed
		/**/                bool      parallel_ ; bool    is_crc_ ; bool hot_ ; Accesses::Val   chunk_accesses_ ; bool err_ ;
		if (IsOStream<S>) { parallel_=parallel  ; is_crc_=is_crc  ; hot_=hot  ; chunk_accesses_=chunk_accesses  ; err_=err  ; }
		::serdes( s ,       parallel_           , is_crc_         , hot_      , chunk_accesses_                 , err_      ) ;
		if (IsIStream<S>) { parallel =parallel_ ; is_crc =is_crc_ ; hot =hot_ ; chunk_accesses =chunk_accesses_ ; err =err_ ; }
		//
		if (is_crc) ::serdes( s , _crc ) ;
		else        ::serdes( s , _sig ) ;
	}
	constexpr DepDigestBase& operator|=(DepDigestBase const& ddb) {                      // assumes ddb has been accessed after us
		if constexpr (HasBase) SWEAR( Base::operator==(ddb) , self,ddb ) ;
		/**/                   SWEAR( !sz                   , self     ) ;
		/**/                   SWEAR( !ddb.sz               , ddb      ) ;
		if (!accesses) {
			set_crc_sig(ddb) ;
			parallel = ddb.parallel ;
		} else if (+ddb.accesses) {
			if      (is_crc!=ddb.is_crc)                         del_crc() ;             // destroy info if digests disagree
			else if (is_crc            ) { if (crc()!=ddb.crc()) del_crc() ; }           // .
			else                         { if (sig()!=ddb.sig()) del_crc() ; }           // .
			// parallel is kept untouched as ddb follows us
		}
		dflags   |= ddb.dflags   ;
		accesses |= ddb.accesses ;
		return self ;
	}
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	uint8_t       sz                       = 0          ;                                //   8 bits, number of items in chunk following header (semantically before)
	Accesses      accesses                 ;                                             // 3<8 bits
	Dflags        dflags                   = DflagsDflt ;                                // 5<8 bits
	bool          parallel      :1         = false      ;                                //   1 bit , dep is parallel with prev dep
	bool          is_crc        :1         = true       ;                                //   1 bit
	bool          hot           :1         = false      ;                                //   1 bit , if true <= file date was very close from access date (within date granularity)
	Accesses::Val chunk_accesses:N<Access> = 0          ;                                //   4 bits
	bool          err           :1         = false      ;                                //   1 bit , if true <=> dep is in error (useful if IgnoreErr), valid only if is_crc
private :
	union {
		Crc     _crc = {} ;                                                              // ~45<64 bits
		FileSig _sig ;                                                                   // ~40<64 bits
	} ;
	// END_OF_VERSIONING
} ;
template<class B> ::string& operator+=( ::string& os , DepDigestBase<B> const& dd ) {    // START_OF_NO_COV
	const char* sep = "" ;
	/**/                                          os << "D("                           ;
	if constexpr ( !::is_void_v<B>            ) { os <<sep<< static_cast<B const&>(dd) ; sep = "," ; }
	if           ( +dd.accesses               ) { os <<sep<< dd.accesses               ; sep = "," ; }
	if           (  dd.dflags!=DflagsDflt     ) { os <<sep<< dd.dflags                 ; sep = "," ; }
	if           (  dd.parallel               ) { os <<sep<< "parallel"                ; sep = "," ; }
	if           ( +dd.accesses && !dd.is_crc ) { os <<sep<< dd.sig()                  ; sep = "," ; }
	else if      ( +dd.accesses && +dd.crc()  ) { os <<sep<< dd.crc()                  ; sep = "," ; }
	if           (  dd.hot                    )   os <<sep<< "hot"                     ;
	return                                        os <<')'                             ;
}                                                                                        // END_OF_NO_COV

using DepDigest = DepDigestBase<void> ;
static_assert(::is_trivially_copyable_v<DepDigest>) ; // as long as this holds, we do not have to bother about union member cxtor/dxtor

struct TargetDigest {
	friend ::string& operator+=( ::string& , TargetDigest const& ) ;
	using Crc = Hash::Crc ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	Tflags        tflags       = {}    ;
	ExtraTflags   extra_tflags = {}    ;
	bool          pre_exist    = false ; // if true <=> file was seen as existing while not incremental
	bool          written      = false ; // if true <=> file was written or unlinked (if crc==None)
	Crc           crc          = {}    ; // if None <=> file was unlinked, if Unknown => file is idle (not written, not unlinked)
	Disk::FileSig sig          = {}    ;
	// END_OF_VERSIONING
} ;

template<class Key=::string> struct JobDigest {                                            // Key may be ::string or Node
	// cxtors & casts
	template<class KeyTo> operator JobDigest<KeyTo>() const {
		JobDigest<KeyTo> res {
			.upload_key     = upload_key
		,	.refresh_codecs = refresh_codecs
		,	.exe_time       = exe_time
		,	.max_stderr_len = max_stderr_len
		,	.cache_idx1     = cache_idx1
		,	.status         = status
		,	.has_msg_stderr = has_msg_stderr
		,	.incremental    = incremental
		} ;
		static constexpr bool NeedNew = requires(Key k) { KeyTo(New,k) ; } ;
		if constexpr (NeedNew) {
			for( auto const& [k,v] : targets ) res.targets.emplace_back(KeyTo(New,k),v) ;
			for( auto const& [k,v] : deps    ) res.deps   .emplace_back(KeyTo(New,k),v) ;
		} else {
			for( auto const& [k,v] : targets ) res.targets.emplace_back(KeyTo(    k),v) ;
			for( auto const& [k,v] : deps    ) res.deps   .emplace_back(KeyTo(    k),v) ;
		}
		return res ;
	}
	void chk(bool for_cache=false) const ;
	// services
	void cache_cleanup() ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	uint64_t                 upload_key     = {}          ;
	::vmap<Key,TargetDigest> targets        = {}          ;
	::vmap<Key,DepDigest   > deps           = {}          ;                                // INVARIANT : sorted in first access order
	::vector_s               refresh_codecs = {}          ;
	Time::CoarseDelay        exe_time       = {}          ;
	uint16_t                 max_stderr_len = {}          ;
	CacheIdx                 cache_idx1     = 0           ;
	Status                   status         = Status::New ;
	bool                     has_msg_stderr = false       ;                                // if true <= msg or stderr are non-empty in englobing JobEndRpcReq
	bool                     incremental    = false       ;                                // if true <= job was run with existing incremental targets
	// END_OF_VERSIONING
} ;
template<class Key> ::string& operator+=( ::string& os , JobDigest<Key> const& jd ) {      // START_OF_NO_COV
	/**/                    os <<"JobDigest("<< jd.status                                ;
	if ( jd.upload_key    ) os <<','<<          to_hex(jd.upload_key)                    ;
	if ( jd.has_msg_stderr) os <<','<<          'E'                                      ;
	/**/                    os <<','<<          jd.targets.size() <<','<< jd.deps.size() ;
	if (+jd.refresh_codecs) os <<','<<          jd.refresh_codecs                        ;
	if (+jd.upload_key    ) os <<','<<          jd.upload_key                            ;
	return                  os << ')'                                                    ;
}                                                                                          // END_OF_NO_COV
template<class Key> void JobDigest<Key>::chk(bool for_cache) const {
	if constexpr (::is_same_v<Key,::string>) { //!                                                 ext_ok
		for( auto const& [t,_] : targets ) throw_unless( +t && !Disk::is_abs(t) && Disk::is_canon(t,false) , "bad target" ) ;
		for( auto const& [d,_] : deps    ) throw_unless( +d &&                     Disk::is_canon(d,true ) , "bad dep"    ) ;
	}
	throw_unless( status<All<Status> , "bad status" ) ;
	if (for_cache) {
		throw_unless( !cache_idx1 , "bad cache_idx1" ) ;
		for( auto const& [_,d] : deps ) {
			throw_unless( d.is_crc                       , "dep not crc" ) ;
			throw_unless( !d.accesses || d.crc().valid() , "bad dep crc" ) ;
		}
	}
}

struct JobInfo ;

struct Zlvl {
	friend ::string& operator+=( ::string& , Zlvl ) ;
	bool operator+() const { return +tag && lvl ; }
	ZlvlTag tag = {} ;
	uint8_t lvl = 0  ;
} ;

namespace Caches {

	struct Cache {
		using Sz  = Disk::DiskSz        ;
		using MDD = ::vmap_s<DepDigest> ;
		static constexpr Channel CacheChnl = Channel::Cache ;
		struct DownloadDigest ;
		struct Hdr {
			// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
			::vector<Sz> target_szs ;
			// END_OF_VERSIONING
		} ;
		struct SubUploadDigest {
			friend ::string& operator+=( ::string& , SubUploadDigest const& ) ;
			// services
			bool operator+() const { return upload_key ; }
			// data
			::string file       = {} ;
			::string pfx        = {} ;                                                  // to be written to file before data
			uint64_t upload_key = 0  ;
			::string msg        = {} ;
			PermExt  perm_ext   = {} ;
		} ;
		// statics
		static Cache* s_new   (                          ) ;
		static void   s_config(::vmap_s<::vmap_ss> const&) ;
		// static data
		static ::vector<Cache*> s_tab ;
		// cxtors & casts
		virtual ~Cache() = default ;
		// services
		// if match returns empty, answer is delayed and an action will be posted to the main loop when ready
		DownloadDigest                                  download( ::string const& job , MDD const& deps , bool incremental , ::function<void()> pre_download         , NfsGuard* ) ;
		::pair<uint64_t/*upload_key*/,Sz/*compressed*/> upload  ( Time::Delay exe_time , ::vmap_s<TargetDigest> const& , ::vector<Disk::FileInfo> const& , Zlvl zlvl , NfsGuard* ) ;
		//
		void commit ( uint64_t upload_key , ::string const& /*job*/ , JobInfo&& ) ;
		void dismiss( uint64_t upload_key                                       ) { Trace trace(CacheChnl,"Cache::dismiss",upload_key) ; sub_dismiss(upload_key) ; }
		// default implementation : no caching, but enforce protocol
		virtual void      config( ::vmap_ss const& , bool /*may_init*/=false )       {}
		virtual ::vmap_ss descr (                                            ) const { return {}        ; }
		virtual void      repair( bool /*dry_run*/                           )       {}
		virtual void      serdes( ::string     &                             )       {} // serialize
		virtual void      serdes( ::string_view&                             )       {} // deserialize
		//
		virtual ::pair<DownloadDigest,AcFd> sub_download( ::string const& /*job*/ , MDD const&                          ) ;
		virtual SubUploadDigest             sub_upload  ( Time::Delay /*exe_time*/ , Sz /*max_sz*/                      ) { return {} ; }
		virtual void                        sub_commit  ( uint64_t /*upload_key*/ , ::string const& /*job*/ , JobInfo&& ) {             }
		virtual void                        sub_dismiss ( uint64_t /*upload_key*/                                       ) {             }
	} ;

}

struct UserTraceEntry {
	friend ::string& operator+=( ::string& , UserTraceEntry const& ) ;
	// cxtor & casts
	// mimic aggregate cxtors as clang would not accept emplace_back if not explicitely provided
	UserTraceEntry() = default ;
	UserTraceEntry( Time::Pdate d , Comment c , CommentExts ces={} , ::string const& f={} ) : date{d} , comment{c} , comment_exts{ces} , file{       f } {}
	UserTraceEntry( Time::Pdate d , Comment c , CommentExts ces    , ::string     && f    ) : date{d} , comment{c} , comment_exts{ces} , file{::move(f)} {}
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes( s , date                   ) ;
		::serdes( s , comment , comment_exts ) ;
		::serdes( s , file                   ) ;
	}
	::string step() const {
		if (+comment_exts) return cat(comment,comment_exts) ;
		else               return cat(comment             ) ;
	}
	// data
	Time::Pdate date         ;
	Comment     comment      = Comment::None ;
	CommentExts comment_exts = {}            ;
	::string    file         = {}            ;
} ;

struct JobSpace {
	friend ::string& operator+=( ::string& , JobSpace const& ) ;
	struct ViewDescr {
		friend ::string& operator+=( ::string& , ViewDescr const& ) ;
		// accesses
		bool operator+() const { return +phys_s ; }
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , phys_s,copy_up ) ;
		}
		// data
		// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
		::vector_s phys_s  = {} ;                                                              // (upper,lower...)
		::vector_s copy_up = {} ;                                                              // dirs & files or dirs to create in upper (mkdir or cp <file> from lower...)
		// END_OF_VERSIONING
		bool is_dyn = false ;                                                                  // only used in rule attributes
	} ;
	// accesses
	bool operator+() const { return +lmake_view_s || +repo_view_s || +tmp_view_s || +views ; } // true if namespace needed
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes( s , lmake_view_s,repo_view_s,tmp_view_s ) ;
		::serdes( s , views                               ) ;
	}
	bool/*entered*/ enter(
		::vector_s&              /*out  */ accesses
	,	::string  &              /*.    */ repo_root_s
	,	::vector<UserTraceEntry>&/*inout*/
	,	SmallId
	,	::string   const&                  phy_lmake_root_s
	,	::string   const&                  phy_repo_root_s
	,	::string   const&                  phy_tmp_dir_s    , bool keep_tmp
	,	ChrootInfo const&                  chroot_info
	,	::string   const&                  sub_repo_s
	,	::vector_s const&                  src_dirs_s
	,	bool                               is_ld_audit
	) ;
	void exit() ;
	//
	::vmap_s<::vector_s> flat_phys_s() const ;                                                 // view phys after dereferencing indirections (i.e. if a/->b/ and b/->c/, returns a/->c/ and b/->c/)
	//
	void mk_canon( ::string const& phy_repo_root_s , ::string const& sub_repo_s , bool has_chroot )       ;
	void chk     (                                                                                ) const ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	::string            lmake_view_s = {} ;                                                    // absolute dir under which job sees open-lmake root dir (empty if unused)
	::string            repo_view_s  = {} ;                                                    // absolute dir under which job sees repo root dir       (empty if unused)
	::string            tmp_view_s   = {} ;                                                    // absolute dir under which job sees tmp dir             (empty if unused)
	::vmap_s<ViewDescr> views        = {} ;                                                    // dir_s->descr, relative to sub_repo when not _is_canon, relative to repo_root when _is_canon
	// END_OF_VERSIONING
private :
	::string _tmp_dir_s   ;                                                                    // to be unlinked upon exit
	bool     _is_canon    = false ;
	bool     _force_creat = false ;                                                            // valid if _is_canon, if true => create a chroot
} ;

struct JobRpcReq {
	// accesses
	bool operator+() const { return +seq_id ; }
	// services
	void cache_cleanup() ;
	void chk(bool for_cache=false) const ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	SeqId  seq_id = 0 ;
	JobIdx job    = 0 ;
	// END_OF_VERSIONING)
} ;

struct JobStartRpcReq : JobRpcReq {
	friend ::string& operator+=( ::string& , JobStartRpcReq const& ) ;
	// cxtors & casts
	JobStartRpcReq() = default ;
	JobStartRpcReq( JobRpcReq jrr , KeyedService s , ::string&& msg_={} ) : JobRpcReq{jrr} , service{s} , msg{::move(msg_)} {}
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes( s , static_cast<JobRpcReq&>(self) ) ;
		::serdes( s , service                       ) ;
	}
	void cache_cleanup() ;
	void chk(bool for_cache=false) const ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	KeyedService service ; // where job_exec can be contacted (except addr which is discovered by server from peer_addr
	::string     msg     ;
	// END_OF_VERSIONING)
} ;

struct JobStartRpcReply {                                                // NOLINT(clang-analyzer-optin.performance.Padding) prefer alphabetical order
	friend ::string& operator+=( ::string& , JobStartRpcReply const& ) ;
	using Crc  = Hash::Crc  ;
	using Proc = JobRpcProc ;
	struct LmakeVersion {
		bool     is_remote           = false ;
		::string std_path            ;
		::string python              ;
		::string py_ld_library_path  ;
		::string python2             ;
		::string py2_ld_library_path ;
	} ;
	// accesses
	bool operator+() const { return +interpreter ; }                     // there is always an interpreter for any job, even if no actual execution as is the case when downloaded from cache
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes( s , autodep_env                   ) ;
		::serdes( s , cache_idx1                    ) ;
		::serdes( s , chk_abs_paths                 ) ;
		::serdes( s , chroot_info                   ) ;
		::serdes( s , cmd                           ) ;
		::serdes( s , ddate_prec                    ) ;
		::serdes( s , deps                          ) ;
		::serdes( s , env                           ) ;
		::serdes( s , interpreter                   ) ;
		::serdes( s , job_space                     ) ;
		::serdes( s , keep_tmp                      ) ;
		::serdes( s , key                           ) ;
		::serdes( s , kill_sigs                     ) ;
		::serdes( s , live_out                      ) ;
		::serdes( s , method                        ) ;
		::serdes( s , network_delay                 ) ;
		::serdes( s , nice                          ) ;
		::serdes( s , phy_lmake_root_s              ) ;
		::serdes( s , pre_actions                   ) ;
		::serdes( s , rule                          ) ;
		::serdes( s , small_id                      ) ;
		::serdes( s , star_matches , static_matches ) ;
		::serdes( s , stderr_ok                     ) ;
		::serdes( s , stdin        , stdout         ) ;
		::serdes( s , timeout                       ) ;
		::serdes( s , use_script                    ) ;
		::serdes( s , zlvl                          ) ;
		//
		bool has_cache = cache ;
		::serdes(s,has_cache) ;
		if (IsIStream<S>) cache = has_cache ? Caches::Cache::s_new() : nullptr ;
		if (has_cache   ) cache->serdes(s) ;
	}
	void            mk_canon( ::string const& phy_repo_root_s ) ;
	bool/*entered*/ enter   (
		::vector_s&              /*out  */ accesses
	,	pid_t     &              /*.    */ first_pid
	,	::string  &              /*.    */ repo_dir_s
	,	::vector<UserTraceEntry>&/*inout*/
	,	::string const&                    phy_repo_root_s
	,	::string const&                    phy_tmp_dir_s
	) ;
	void update_val   ( ::string&/*inout*/ v       , ::string const& phy_repo_root_s , ::string const& phy_tmp_dir_s , SeqId=0 ) const ;
	void update_env   ( ::vmap_ss&/*out*/  dyn_env , ::string const& phy_repo_root_s , ::string const& phy_tmp_dir_s , SeqId=0 )       ;
	void exit         (                                                                                                        )       ;
	void cache_cleanup(                                                                                                        )       ;
	void chk          ( bool for_cache=false                                                                                   ) const ;
private :
	void _mk_lmake_version() ;
	// data
public :
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	AutodepEnv                              autodep_env      ;
	Caches::Cache*                          cache            = nullptr             ;
	CacheIdx                                cache_idx1       ;                       // to be repeated in JobEndRpcReq to ensure it is available when processing
	bool                                    chk_abs_paths    = false               ;
	ChrootInfo                              chroot_info      ;
	::string                                cmd              ;
	Time::Delay                             ddate_prec       ;
	::vmap_s<::pair<DepDigest,ExtraDflags>> deps             ;                       // deps already accessed (always includes static deps), DepDigest does not include extra_dflags, so add them
	::vmap_ss                               env              ;
	::vector_s                              interpreter      ;                       // actual interpreter used to execute cmd
	JobSpace                                job_space        ;
	bool                                    keep_tmp         = false               ;
	::string                                key              ;                       // key used to uniquely identify repo
	::vector<uint8_t>                       kill_sigs        ;
	bool                                    live_out         = false               ;
	AutodepMethod                           method           = AutodepMethod::Dflt ;
	Time::Delay                             network_delay    ;
	uint8_t                                 nice             = 0                   ;
	::string                                phy_lmake_root_s ;
	::vmap_s<FileAction>                    pre_actions      ;
	::string                                rule             ;                       // rule name
	SmallId                                 small_id         = 0                   ;
	::vmap<Re::Pattern,MatchFlags>          star_matches     ;                       // maps regexprs to flags
	::vmap_s<MatchFlags>                    static_matches   ;                       // maps individual files to flags
	bool                                    stderr_ok        = false               ;
	::string                                stdin            ;
	::string                                stdout           ;
	Time::Delay                             timeout          ;
	bool                                    use_script       = false               ;
	Zlvl                                    zlvl             {}                    ;
	// END_OF_VERSIONING
	LmakeVersion lmake_version ;                                                     // not transported
private :
	::string _tmp_dir_s ;                                                            // for use in exit (autodep.tmp_dir_s may be moved)
} ;

struct JobEndRpcReq : JobRpcReq {
	using P   = JobRpcProc          ;
	using SI  = SeqId               ;
	using JI  = JobIdx              ;
	using MDD = ::vmap_s<DepDigest> ;
	friend ::string& operator+=( ::string& , JobEndRpcReq const& ) ;
	// cxtors & casts
	JobEndRpcReq(JobRpcReq jrr={}) : JobRpcReq{jrr} {}
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes( s , static_cast<JobRpcReq&>(self) ) ;
		::serdes( s , digest                        ) ;
		::serdes( s , dyn_env                       ) ;
		::serdes( s , end_date                      ) ;
		::serdes( s , msg_stderr                    ) ;
		::serdes( s , os_info                       ) ;
		::serdes( s , phy_tmp_dir_s                 ) ;
		::serdes( s , stats                         ) ;
		::serdes( s , stdout                        ) ;
		::serdes( s , total_sz                      ) ;
		::serdes( s , total_z_sz                    ) ;
		::serdes( s , user_trace                    ) ;
		::serdes( s , wstatus                       ) ;
	}
	void cache_cleanup() ;                   // clean up info before uploading to cache
	void chk(bool for_cache=false) const ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	JobDigest<>              digest        ;
	::vmap_ss                dyn_env       ; // env variables computed in job_exec
	Time::Pdate              end_date      ;
	MsgStderr                msg_stderr    ;
	::string                 os_info       ;
	::string                 phy_tmp_dir_s ;
	JobStats                 stats         ;
	::string                 stdout        ;
	Disk::DiskSz             total_sz      = 0 ;
	Disk::DiskSz             total_z_sz    = 0 ;
	::vector<UserTraceEntry> user_trace    ;
	int                      wstatus       = 0 ;
	// END_OF_VERSIONING)
} ;

struct JobMngtRpcReq : JobRpcReq {
	using JMMR = JobMngtRpcReq       ;
	using Proc = JobMngtProc         ;
	using SI   = SeqId               ;
	using JI   = JobIdx              ;
	using MDD  = ::vmap_s<DepDigest> ;
	friend ::string& operator+=( ::string& , JobMngtRpcReq const& ) ;
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes( s , static_cast<JobRpcReq&>(self) ) ;
		::serdes( s , proc                          ) ;
		switch (proc) {
			case Proc::None       :
			case Proc::Heartbeat  :                                                                    break ;
			case Proc::LiveOut    :
			case Proc::AddLiveOut : ::serdes( s ,                                    txt           ) ; break ;
			case Proc::ChkDeps    : ::serdes( s , fd , targets , deps                              ) ; break ;
			case Proc::DepDirect  :
			case Proc::DepVerbose : ::serdes( s , fd ,           deps                              ) ; break ;
		DF}                                                                                                    // NO_COV
	}
	// data
	Proc                   proc    = Proc::None ;
	Fd                     fd      = {}         ;                                                              // fd to which reply must be forwarded
	::vmap_s<TargetDigest> targets = {}         ;                                                              // proc==ChkDeps
	::vmap_s<DepDigest   > deps    = {}         ;                                                              // proc==ChkDeps|DepDirect|DepVerbose
	::string               txt     = {}         ;                                                              // proc==LiveOut
} ;

struct JobMngtRpcReply {
	friend ::string& operator+=( ::string& , JobMngtRpcReply const& ) ;
	using Crc  = Hash::Crc   ;
	using Proc = JobMngtProc ;
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes(s,proc  ) ;
		::serdes(s,seq_id) ;
		switch (proc) {
			case Proc::None       :
			case Proc::Kill       :
			case Proc::Heartbeat  :
			case Proc::AddLiveOut :                                      break ;
			case Proc::DepDirect  : ::serdes( s , fd , ok            ) ; break ;
			case Proc::DepVerbose : ::serdes( s , fd , verbose_infos ) ; break ;
			case Proc::ChkDeps    :
			case Proc::ChkTargets : ::serdes( s , fd , ok , txt      ) ; break ;
		DF}                                                                      // NO_COV
	}
	// data
	Proc                  proc          = {}    ;
	SeqId                 seq_id        = 0     ;
	Fd                    fd            = {}    ;                                // proc == ChkDeps|DepDirect|DepVerbose , fd to which reply must be forwarded
	::vector<VerboseInfo> verbose_infos = {}    ;                                // proc ==                   DepVerbose
	::string              txt           = {}    ;                                // proc == ChkDeps|                     , reason for ChkDeps
	Bool3                 ok            = Maybe ;                                // proc == ChkDeps|DepDirect            , if No <=> deps in error, if Maybe <=> deps not ready
} ;

struct SubmitAttrs {
	friend ::string& operator+=( ::string& , SubmitAttrs const& ) ;
	// services
	SubmitAttrs& operator|=(SubmitAttrs const& other) {
		// cache, deps and tag are independent of req but may not always be present
		if (!cache_idx1  ) cache_idx1    =                other.cache_idx1   ; else if (+other.cache_idx1  ) SWEAR( cache_idx1  ==other.cache_idx1   , cache_idx1  ,other.cache_idx1   ) ;
		if (!deps        ) deps          =                other.deps         ; else if (+other.deps        ) SWEAR( deps        ==other.deps         , deps        ,other.deps         ) ;
		if (!used_backend) used_backend  =                other.used_backend ; else if (+other.used_backend) SWEAR( used_backend==other.used_backend , used_backend,other.used_backend ) ;
		/**/               live_out     |=                other.live_out     ;
		/**/               nice          = ::min(nice    ,other.nice     )   ;
		/**/               pressure      = ::max(pressure,other.pressure )   ;
		/**/               reason       |=                other.reason       ;
		/**/               tokens1       = ::max(tokens1 ,other.tokens1  )   ;
		return self ;
	}
	SubmitAttrs operator|(SubmitAttrs const& other) const {
		SubmitAttrs res = self ;
		res |= other ;
		return res ;
	}
	void cache_cleanup() ;
	void chk(bool for_cache=false) const ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	::vmap_s<DepDigest> deps         = {}    ;
	JobReason           reason       = {}    ;
	Time::CoarseDelay   pressure     = {}    ;
	CacheIdx            cache_idx1   = {}    ;
	Tokens1             tokens1      = 0     ;
	BackendTag          used_backend = {}    ; // tag actually used (possibly made local because asked tag is not available)
	bool                live_out     = false ;
	uint8_t             nice         = -1    ; // -1 means not specified
	// END_OF_VERSIONING
} ;

struct JobInfoStart {
	friend ::string& operator+=( ::string& , JobInfoStart const& ) ;
	// accesses
	bool operator+() const { return +pre_start ; }
	// services
	void cache_cleanup() ;                 // clean up info before uploading to cache
	void chk(bool for_cache=false) const ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	Hash::Crc        rule_crc_cmd = {} ;
	::vector_s       stems        = {} ;
	Time::Pdate      eta          = {} ;
	SubmitAttrs      submit_attrs = {} ;
	::vmap_ss        rsrcs        = {} ;
	JobStartRpcReq   pre_start    = {} ;
	JobStartRpcReply start        = {} ;
	// END_OF_VERSIONING
} ;

struct JobInfo {
	JobInfo() = default ;
	JobInfo( ::string const& ancillary_file , JobInfoKinds need=~JobInfoKinds() ) { fill_from(ancillary_file,need) ; }
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes( s , start,end,dep_crcs ) ;
	}
	void fill_from( ::string const& ancillary_file , JobInfoKinds need=~JobInfoKinds() ) ;
	//
	void update_digest(                    ) ;         // update crc in digest from dep_crcs
	void cache_cleanup(                    ) ;         // clean up info before uploading to cache
	void chk          (bool for_cache=false) const ;
	// data
	// START_OF_VERSIONING REPO DAEMON_CACHE DIR_CACHE
	JobInfoStart                            start    ;
	JobEndRpcReq                            end      ;
	::vector<::pair<Hash::Crc,bool/*err*/>> dep_crcs ; // optional, if not provided in end.digest.deps
	// END_OF_VERSIONING
} ;
::string cache_repo_cmp( JobInfo const& info_cache , JobInfo const& info_repo ) ;

//
// implementation
//

namespace Caches {
	struct Cache::DownloadDigest {
		// data
		CacheHitInfo hit_info = CacheHitInfo::NoCache ;
		JobInfo      job_info = {}                    ;
	} ;

	inline ::pair<Cache::DownloadDigest,AcFd> Cache::sub_download( ::string const& /*job*/ , ::vmap_s<DepDigest> const& ) {
		return {} ;
	}

}
