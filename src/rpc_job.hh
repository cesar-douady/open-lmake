// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "config.hh"

#include "disk.hh"
#include "hash.hh"
#include "serialize.hh"
#include "time.hh"

#include "autodep/env.hh"

#include "rpc_job_common.hh"

// START_OF_VERSIONING
ENUM_2( AutodepMethod                     // PER_AUTODEP_METHOD : add entry here
,	Ld   = LdAudit                        // >=Ld means a lib is pre-loaded (through LD_AUDIT or LD_PRELOAD)
,	Dflt = HAS_LD_AUDIT?LdAudit:LdPreload // by default, use  a compromize between speed an reliability
,	None
,	Fuse
,	Ptrace
,	LdAudit
,	LdPreload
,	LdPreloadJemalloc
)
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM_1( BackendTag // PER_BACKEND : add a tag for each backend
,	Dflt = Local
,	Unknown        // must be first
,	Local
,	Slurm
)
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM_1( FileActionTag
,	HasFile = Uniquify // <=HasFile means action acts on file
,	Src                // file is src, no action
,	Unlink
,	UnlinkWarning
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
ENUM_3( JobReasonTag                                // see explanations in table below
,	HasNode = NoTarget                              // if >=HasNode, a node is associated
,	Err     = DepOverwritten
,	Missing = DepMissingStatic
	//
,	None
//	with reason
,	OldErr
,	Rsrcs
,	PollutedTargets
,	ChkDeps
,	Cmd
,	Force
,	Garbage
,	Killed
,	Lost
,	New
//	with node
,	NoTarget
,	PollutedTarget
,	PrevTarget
,	ClashTarget
,	DepOutOfDate
,	DepTransient
,	DepUnlnked
,	DepUnstable
//	with error
,	DepOverwritten
,	DepDangling
,	DepErr
,	DepMissingRequired                              // this is actually an error
// with missing
,	DepMissingStatic                                // this prevents the job from being selected
)
// END_OF_VERSIONING
static constexpr const char* JobReasonTagStrs[] = {
	"no reason"                                     // None
//	with reason
,	"job was in error"                              // OldErr
,	"resources changed and job was in error"        // Rsrcs
,	"polluted targets"                              // PollutedTargets
,	"dep check requires rerun"                      // ChkDeps
,	"command changed"                               // Cmd
,	"job forced"                                    // Force
,	"job ran with unstable data"                    // Garbage
,	"job was killed"                                // Killed
,	"job was lost"                                  // Lost
,	"job was never run"                             // New
//	with node
,	"missing target"                                // NoTarget
,	"target polluted by another job"                // PollutedTarget
,	"target previously existed"                     // PrevTarget
,	"multiple simultaneous writes"                  // ClashTarget
,	"dep out of date"                               // DepOutOfDate
,	"dep dir is a symbolic link"                    // DepTransient
,	"dep not on disk"                               // DepUnlnked
,	"dep changed during job execution"              // DepUnstable
//	with error
,	"dep has been overwritten"                      // DepOverwritten
,	"dep is dangling"                               // DepDangling
,	"dep in error"                                  // DepErr
,	"required dep missing"                          // DepMissingRequired
// with missing
,	"static dep missing"                            // DepMissingStatic
} ;
static_assert(::size(JobReasonTagStrs)==N<JobReasonTag>) ;
static constexpr uint8_t JobReasonTagPrios[] = {
//	no reason, must be 0
	0                                               // None
//	with reason
,	10                                              // OldErr
,	11                                              // Rsrcs
,	12                                              // PollutedTargets
,	30                                              // ChkDeps
,	50                                              // Cmd
,	51                                              // Force
,	51                                              // Garbage
,	51                                              // Killed
,	51                                              // Lost
,	52                                              // New
//	with node
,	20                                              // NoTarget
,	21                                              // PollutedTarget
,	21                                              // PrevTarget
,	22                                              // ClashTarget
,	40                                              // DepOutOfDate
,	41                                              // DepTransient
,	41                                              // DepUnlnked
,	41                                              // DepUnstable
//	with error, must be higher than ok reasons
,	100                                             // DepOverwritten
,	101                                             // DepDangling
,	101                                             // DepErr
,	101                                             // DepMissingRequired
// with missing, must be higher than err reasons
,	200                                             // DepMissingStatic
} ;
static_assert(::size(JobReasonTagPrios)==N<JobReasonTag>) ;

// START_OF_VERSIONING
ENUM( MatchKind
,	Target
,	SideTargets
,	SideDeps
)
// END_OF_VERSIONING

// START_OF_VERSIONING
ENUM_3( Status                           // result of job execution
,	Early   = EarlyLostErr               // <=Early means output has not been modified
,	Async   = Killed                     // <=Async means job was interrupted asynchronously
,	Garbage = BadTarget                  // <=Garbage means job has not run reliably
,	New                                  // job was never run
,	EarlyChkDeps                         // dep check failed before job actually started
,	EarlyErr                             // job was not started because of error
,	EarlyLost                            // job was lost before starting     , retry
,	EarlyLostErr                         // job was lost before starting     , do not retry
,	LateLost                             // job was lost after having started, retry
,	LateLostErr                          // job was lost after having started, do not retry
,	Killed                               // job was killed
,	ChkDeps                              // dep check failed
,	BadTarget                            // target was not correctly initialized or simultaneously written by another job
,	Ok                                   // job execution ended successfully
,	Err                                  // job execution ended in error
)
// END_OF_VERSIONING
inline bool  is_lost(Status s) { return s<=Status::LateLostErr && s>=Status::EarlyLost ; }
inline Bool3 is_ok  (Status s) {
	static constexpr Bool3 IsOkTab[] = {
		Maybe                            // New
	,	Maybe                            // EarlyChkDeps
	,	No                               // EarlyErr
	,	Maybe                            // EarlyLost
	,	No                               // EarlyLostErr
	,	Maybe                            // LateLost
	,	No                               // LateLostErr
	,	Maybe                            // Killed
	,	Maybe                            // ChkDeps
	,	Maybe                            // BadTarget
	,	Yes                              // Ok
	,	No                               // Err
	} ;
	static_assert(sizeof(IsOkTab)==N<Status>) ;
	return IsOkTab[+s] ;
}
inline Status mk_err(Status s) {
	switch (s) {
		case Status::New       : return Status::EarlyErr     ;
		case Status::EarlyLost : return Status::EarlyLostErr ;
		case Status::LateLost  : return Status::LateLostErr  ;
		case Status::Ok        : return Status::Err          ;
	DF}
}

static const ::string EnvPassMrkr = {'\0','p'} ; // special illegal value to ask for value from environment
static const ::string EnvDynMrkr  = {'\0','d'} ; // special illegal value to mark dynamically computed env variables

static constexpr char QuarantineDirS[] = ADMIN_DIR_S "quarantine/" ;

struct FileAction {
	friend ::ostream& operator<<( ::ostream& , FileAction const& ) ;
	// cxtors & casts
	FileAction(FileActionTag t={} , Hash::Crc c={} , Disk::FileSig s={} ) : tag{t} , crc{c} , sig{s}  {} // should be automatic but clang lacks some automatic conversions in some cases
	// data
	FileActionTag tag = {} ;
	Hash::Crc     crc ;                                                                                  // expected (else, quarantine)
	Disk::FileSig sig ;                                                                                  // .
} ;
/**/   ::pair_s<bool/*ok*/> do_file_actions( ::vector_s* unlnks/*out*/ , ::vmap_s<FileAction>&&    , Disk::NfsGuard&    , Algo   ) ;
inline ::pair_s<bool/*ok*/> do_file_actions( ::vector_s& unlnks/*out*/ , ::vmap_s<FileAction>&& pa , Disk::NfsGuard& ng , Algo a ) { return do_file_actions(&unlnks,::move(pa),ng,a) ; }
inline ::pair_s<bool/*ok*/> do_file_actions(                             ::vmap_s<FileAction>&& pa , Disk::NfsGuard& ng , Algo a ) { return do_file_actions(nullptr,::move(pa),ng,a) ; }

struct AccDflags {
	// services
	AccDflags  operator| (AccDflags other) const { return { accesses|other.accesses , dflags|other.dflags } ; }
	AccDflags& operator|=(AccDflags other)       { *this = *this | other ; return *this ;                     }
	// data
	// START_OF_VERSIONING
	Accesses accesses ;
	Dflags   dflags   ;
	// END_OF_VERSIONING
} ;

struct JobReason {
	friend ::ostream& operator<<( ::ostream& , JobReason const& ) ;
	using Tag = JobReasonTag ;
	// cxtors & casts
	JobReason(                   ) = default ;
	JobReason( Tag t             ) : tag{t}           { SWEAR( t< Tag::HasNode       , t     ) ; }
	JobReason( Tag t , NodeIdx n ) : tag{t} , node{n} { SWEAR( t>=Tag::HasNode && +n , t , n ) ; }
	// accesses
	bool operator+() const { return +tag ; }
	bool operator!() const { return !tag ; }
	// services
	JobReason operator|(JobReason jr) const {
		if (JobReasonTagPrios[+tag]>=JobReasonTagPrios[+jr.tag]) return *this ; // at equal level, prefer older reason
		else                                                     return jr    ;
	}
	JobReason& operator|=(JobReason jr) { *this = *this | jr ; return *this ; }
	::string msg() const {
		if (tag<Tag::HasNode) SWEAR(node==0,tag,node) ;
		return JobReasonTagStrs[+tag] ;
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
	friend ::ostream& operator<<( ::ostream& , DepInfo const& ) ;
	using Crc      = Hash::Crc      ;
	using FileSig  = Disk::FileSig  ;
	using FileInfo = Disk::FileInfo ;
	using Kind     = DepInfoKind    ;
	//cxtors & casts
	DepInfo(           ) :                    _crc {  } {}
	DepInfo(Crc      c ) : kind{Kind::Crc } , _crc {c } {}
	DepInfo(FileSig  s ) : kind{Kind::Sig } , _sig {s } {}
	DepInfo(FileInfo fi) : kind{Kind::Info} , _info{fi} {}
	//
	template<class B> DepInfo(DepDigestBase<B> const& ddb) {
		if      (!ddb.accesses) *this = Crc()     ;
		else if ( ddb.is_crc  ) *this = ddb.crc() ;
		else                    *this = ddb.sig() ;
	}
	// accesses
	bool operator==(DepInfo const& di) const {
		if (kind!=di.kind) return false ;
		switch (kind) {
			case Kind::Crc  : return crc ()==di.crc () ;
			case Kind::Sig  : return sig ()==di.sig () ;
			case Kind::Info : return info()==di.info() ;
		DF}
	}
	bool     operator+() const {                                return kind!=Kind::Crc || +_crc             ; }
	bool     operator!() const {                                return !+*this                              ; }
	Crc      crc      () const { SWEAR(kind==Kind::Crc ,kind) ; return _crc                                 ; }
	FileSig  sig      () const { SWEAR(kind!=Kind::Crc ,kind) ; return kind==Kind::Sig ? _sig : _info.sig() ; }
	FileInfo info     () const { SWEAR(kind==Kind::Info,kind) ; return _info                                ; }
	//
	bool seen(Accesses a) const { // return true if accesses could perceive the existence of file
		if (!a) return false ;
		SWEAR(+*this,*this,a) ;
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
		Crc      _crc  ;          // ~46< 64 bits
		FileSig  _sig  ;          //      64 bits
		FileInfo _info ;          //     128 bits
	} ;
	// END_OF_VERSIONING
} ;

// for Dep recording in book-keeping, we want to derive from Node
// but if we derive from Node and have a field DepDigest, it is impossible to have a compact layout because of alignment constraints
// hence this solution : derive from a template argument
template<class B> ::ostream& operator<<( ::ostream& , DepDigestBase<B> const& ) ;
template<class B> struct DepDigestBase : NoVoid<B> {
	friend ::ostream& operator<< <>( ::ostream& , DepDigestBase const& ) ;
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
	constexpr Crc     crc        () const { SWEAR( +accesses &&  is_crc , accesses , is_crc ) ; return _crc                       ; }
	constexpr FileSig sig        () const { SWEAR( +accesses && !is_crc , accesses , is_crc ) ; return _sig                       ; }
	constexpr bool    never_match() const { SWEAR(               is_crc , accesses , is_crc ) ; return _crc.never_match(accesses) ; }
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
	constexpr DepDigestBase& operator|=(DepDigestBase const& ddb) {              // assumes ddb has been accessed after us
		if constexpr (HasBase) SWEAR(Base::operator==(ddb),*this,ddb) ;
		if (!accesses) {
			crc_sig(ddb) ;
			parallel = ddb.parallel ;
		} else if (+ddb.accesses) {
			if      (is_crc!=ddb.is_crc)                         crc({}) ;       // destroy info if digests disagree
			else if (is_crc            ) { if (crc()!=ddb.crc()) crc({}) ; }     // .
			else                         { if (sig()!=ddb.sig()) crc({}) ; }     // .
			// parallel is kept untouched as ddb follows us
		}
		dflags   |= ddb.dflags   ;
		accesses |= ddb.accesses ;
		return *this ;
	}
	constexpr void tag(Tag tag) {
		SWEAR(!is_crc,*this) ;
		if (!_sig) { crc(Crc::None) ; return ; }                                 // even if file appears, the whole job has been executed seeing the file as absent
		switch (tag) {
			case Tag::Reg  :
			case Tag::Exe  :
			case Tag::Lnk  : if (!Crc::s_sense(accesses,tag)) crc(tag) ; break ; // just record the tag if enough to match (e.g. accesses==Lnk and tag==Reg)
			case Tag::None :
			case Tag::Dir  : if (+_sig                      ) crc({} ) ; break ;
		DF}
	}
	// data
	// START_OF_VERSIONING
	static constexpr uint8_t NSzBits = 5 ;                                       // XXX : set to 8 by making room by storeing accesses on 3 bits rather than 8
	Accesses accesses               ;                                            // 3<8 bits
	Dflags   dflags                 ;                                            // 6<8 bits
	bool     parallel      :1       = false ;                                    //   1 bit
	bool     is_crc        :1       = true  ;                                    //   1 bit
	uint8_t  sz            :NSzBits = 0     ;                                    //   6 bits, number of items in chunk following header (semantically before)
	bool     hot           :1       = false ;                                    //   1 bit , if true <= file date was very close from access date (within date granularity)
	Accesses chunk_accesses         ;                                            // 3<8 bits
private :
	union {
		Crc     _crc = {} ;                                                      // ~45<64 bits
		FileSig _sig ;                                                           // ~40<64 bits
	} ;
	// END_OF_VERSIONING
} ;
template<class B> ::ostream& operator<<( ::ostream& os , DepDigestBase<B> const& dd ) {
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
	friend ::ostream& operator<<( ::ostream& , TargetDigest const& ) ;
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
	friend ::ostream& operator<<( ::ostream& , JobDigest const& ) ;
	// data
	// START_OF_VERSIONING
	Status                 status   = Status::New ;
	::vmap_s<TargetDigest> targets  = {}          ;
	::vmap_s<DepDigest   > deps     = {}          ; // INVARIANT : sorted in first access order
	::string               stderr   = {}          ;
	::string               stdout   = {}          ;
	int                    wstatus  = 0           ;
	Time::Pdate            end_date = {}          ;
	JobStats               stats    = {}          ;
	// END_OF_VERSIONING
} ;

struct MatchFlags {
	friend ::ostream& operator<<( ::ostream& , MatchFlags const& ) ;
	// cxtors & casts
	MatchFlags(                                ) = default ;
	MatchFlags( Tflags tf , ExtraTflags etf={} ) : is_target{Yes} , _tflags{tf} , _extra_tflags{etf} {}
	MatchFlags( Dflags df , ExtraDflags edf={} ) : is_target{No } , _dflags{df} , _extra_dflags{edf} {}
	// accesses
	bool        operator+   () const {                               return is_target!=Maybe ; }
	bool        operator!   () const {                               return !+*this          ; }
	Tflags      tflags      () const { SWEAR(is_target==Yes,*this) ; return _tflags          ; }
	Dflags      dflags      () const { SWEAR(is_target==No ,*this) ; return _dflags          ; }
	ExtraTflags extra_tflags() const { SWEAR(is_target==Yes,*this) ; return _extra_tflags    ; }
	ExtraDflags extra_dflags() const { SWEAR(is_target==No ,*this) ; return _extra_dflags    ; }
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

struct JobSpace {
	friend ::ostream& operator<<( ::ostream& , JobSpace const& ) ;
	// accesses
	bool operator+() const { return +chroot_dir_s || +root_view_s || +tmp_view_s || +views ; }
	bool operator!() const { return !+*this                                                ; }
	// services
	bool/*entered*/ enter(
		::string const&   phy_root_dir_s
	,	::string const&   phy_tmp_dir_s
	,	size_t            tmp_sz_mb
	,	::string const&   work_dir_s
	,	::vector_s const& src_dirs_s = {}
	,	bool              use_fuse   = false
	) const ;
	//
	::vmap_s<::vector_s> flat_views() const ; // views after dereferencing indirections (i.e. if a/->b/ and b/->c/, returns a/->c/ and b/->c/)
	//
	void chk() const ;
	// data
	// START_OF_VERSIONING
	::string             chroot_dir_s = {} ;  // absolute dir which job chroot's to before execution (empty if unused)
	::string             root_view_s  = {} ;  // absolute dir under which job sees repo root dir     (empty if unused)
	::string             tmp_view_s   = {} ;  // absolute dir under which job sees tmp dir           (empty if unused)
	::vmap_s<::vector_s> views        = {} ;  // map logical views to physical locations ( file->(file,) or dir->(upper,lower...) )
	// END_OF_VERSIONING
} ;

struct JobRpcReq {
	using P   = JobRpcProc          ;
	using SI  = SeqId               ;
	using JI  = JobIdx              ;
	using MDD = ::vmap_s<DepDigest> ;
	friend ::ostream& operator<<( ::ostream& , JobRpcReq const& ) ;
	// cxtors & casts
	JobRpcReq() = default ;
	JobRpcReq( P p , SI si , JI j                                    ) : proc{p} , seq_id{si} , job{j}                                      { SWEAR(p==P::None ,p) ; }
	JobRpcReq( P p , SI si , JI j , in_port_t   pt , ::string&& m={} ) : proc{p} , seq_id{si} , job{j} , port  {pt       } , msg{::move(m)} { SWEAR(p==P::Start,p) ; }
	JobRpcReq( P p , SI si , JI j , JobDigest&& d  , ::string&& m={} ) : proc{p} , seq_id{si} , job{j} , digest{::move(d)} , msg{::move(m)} { SWEAR(p==P::End  ,p) ; }
	// accesses
	bool operator+() const { return +proc   ; }
	bool operator!() const { return !+*this ; }
	// services
	template<IsStream T> void serdes(T& s) {
		if (::is_base_of_v<::istream,T>) *this = {} ;
		::serdes(s,proc  ) ;
		::serdes(s,seq_id) ;
		::serdes(s,job   ) ;
		switch (proc) {
			case P::None  : break ;
			case P::Start :
				::serdes(s,port) ;
				::serdes(s,msg ) ;
			break ;
			case P::End :
				::serdes(s,digest     ) ;
				::serdes(s,dynamic_env) ;
				::serdes(s,msg        ) ;
			break ;
		DF}
	}
	// data
	// START_OF_VERSIONING
	P         proc        = P::None ;
	SI        seq_id      = 0       ;
	JI        job         = 0       ;
	in_port_t port        = 0       ; // if proc==Start
	JobDigest digest      ;           // if proc==End
	::vmap_ss dynamic_env ;           // if proc==End  , env variables computed in job_exec
	::string  msg         ;
	// END_OF_VERSIONING)
} ;

struct JobRpcReply {
	friend ::ostream& operator<<( ::ostream& , JobRpcReply const& ) ;
	using Crc  = Hash::Crc  ;
	using Proc = JobRpcProc ;
	// cxtors & casts
	JobRpcReply(      ) = default ;
	JobRpcReply(Proc p) : proc{p} {}
	// services
	template<IsStream S> void serdes(S& s) {
		if (is_base_of_v<::istream,S>) *this = {} ;
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::None :
			case Proc::End  : break ;
			case Proc::Start :
				::serdes(s,addr          ) ;
				::serdes(s,autodep_env   ) ;
				::serdes(s,cmd           ) ;
				::serdes(s,cwd_s         ) ;
				::serdes(s,date_prec     ) ;
				::serdes(s,deps          ) ;
				::serdes(s,env           ) ;
				::serdes(s,hash_algo     ) ;
				::serdes(s,interpreter   ) ;
				::serdes(s,job_space     ) ;
				::serdes(s,keep_tmp_dir  ) ;
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
				::serdes(s,tmp_sz_mb     ) ;
				::serdes(s,use_script    ) ;
			break ;
		DF}
	}
	// data
	// START_OF_VERSIONING
	Proc                     proc           = {}                  ;
	in_addr_t                addr           = 0                   ; // proc==Start , the address at which server and subproccesses can contact job_exec
	AutodepEnv               autodep_env    ;                       // proc==Start
	::pair_ss/*script,call*/ cmd            ;                       // proc==Start
	::string                 cwd_s          ;                       // proc==Start
	Time::Delay              date_prec      ;                       // proc==Start
	::vmap_s<DepDigest>      deps           ;                       // proc==Start , deps already accessed (always includes static deps)
	::vmap_ss                env            ;                       // proc==Start
	Algo                     hash_algo      = {}                  ; // proc==Start
	::vector_s               interpreter    ;                       // proc==Start , actual interpreter used to execute cmd
	JobSpace                 job_space      ;                       // proc==Start
	bool                     keep_tmp_dir   = false               ; // proc==Start
	::string                 key            ;                       // proc==Start , key used to uniquely identify repo
	vector<uint8_t>          kill_sigs      ;                       // proc==Start
	bool                     live_out       = false               ; // proc==Start
	AutodepMethod            method         = AutodepMethod::Dflt ; // proc==Start
	Time::Delay              network_delay  ;                       // proc==Start
	::vmap_s<FileAction>     pre_actions    ;                       // proc==Start
	SmallId                  small_id       = 0                   ; // proc==Start
	::vmap_s<MatchFlags>     star_matches   ;                       // proc==Start , maps regexprs to flags
	::vmap_s<MatchFlags>     static_matches ;                       // proc==Start , maps individual files to flags
	::string                 stdin          ;                       // proc==Start
	::string                 stdout         ;                       // proc==Start
	Time::Delay              timeout        ;                       // proc==Start
	size_t                   tmp_sz_mb      = Npos                ; // proc==Start , if not Npos and TMPDIR not defined, tmp size in MB
	bool                     use_script     = false               ; // proc==Start
	// END_OF_VERSIONING
} ;

struct JobMngtRpcReq {
	using JMMR = JobMngtRpcReq       ;
	using P    = JobMngtProc         ;
	using SI   = SeqId               ;
	using JI   = JobIdx              ;
	using MDD  = ::vmap_s<DepDigest> ;
	friend ::ostream& operator<<( ::ostream& , JobMngtRpcReq const& ) ;
	// statics
	// cxtors & casts
	#define S ::string
	#define M ::move
	JobMngtRpcReq(                             ) = default ;
	JobMngtRpcReq( P p , SI si , JI j , Fd fd_ ) : proc{p} , seq_id{si} , job{j} , fd{fd_} {}
	//
	JobMngtRpcReq(P p,SI si,JI j,       S&& t   ) : proc{p},seq_id{si},job{j},        txt{M(t)}   { SWEAR(p==P::LiveOut                  ,p) ; }
	JobMngtRpcReq(P p,SI si,JI j,Fd fd_,MDD&& ds) : proc{p},seq_id{si},job{j},fd{fd_},deps{M(ds)} { SWEAR(p==P::ChkDeps||p==P::DepVerbose,p) ; }
	//
	JobMngtRpcReq(P p,SI si,JI j,Fd fd_,S&& code,S&& f,S&& c           ) : proc{p},seq_id{si},job{j},fd{fd_},ctx{M(c)},file{M(f)},txt{M(code)}             { SWEAR(p==P::Decode,p) ; }
	JobMngtRpcReq(P p,SI si,JI j,Fd fd_,S&& val ,S&& f,S&& c,uint8_t ml) : proc{p},seq_id{si},job{j},fd{fd_},ctx{M(c)},file{M(f)},txt{M(val )},min_len{ml} { SWEAR(p==P::Encode,p) ; }
	#undef M
	#undef S
	// services
	template<IsStream T> void serdes(T& s) {
		if (::is_base_of_v<::istream,T>) *this = {} ;
		::serdes(s,proc  ) ;
		::serdes(s,seq_id) ;
		::serdes(s,job   ) ;
		switch (proc) {
			case P::None       :                    break ;
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
	friend ::ostream& operator<<( ::ostream& , JobMngtRpcReply const& ) ;
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
		if (is_base_of_v<::istream,S>) *this = {} ;
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
	Proc                      proc      = {}    ;
	SeqId                     seq_id    = 0     ;
	Fd                        fd        ;         // proc == ChkDeps|DepVerbose|Decode|Encode , fd to which reply must be forwarded
	::vector<pair<Bool3,Crc>> dep_infos ;         // proc ==         DepVerbose
	Bool3                     ok        = Maybe ; // proc == ChkDeps|           Decode|Encode , if No <=> deps in error, if Maybe <=> deps not ready
	::string                  txt       ;         // proc ==                    Decode|Encode , value for Decode, code for Encode
	Crc                       crc       ;         // proc ==                    Decode|Encode , crc of txt
} ;

struct SubmitAttrs {
	friend ::ostream& operator<<( ::ostream& , SubmitAttrs const& ) ;
	// services
	SubmitAttrs& operator|=(SubmitAttrs const& other) {
		// tag, deps and n_retries are independent of req but may not always be present
		if      ( tag==BackendTag::Unknown) tag       = other.tag       ; else if ( other.tag!=BackendTag::Unknown) SWEAR(tag      ==other.tag      ,tag      ,other.tag      ) ;
		if      (!deps                    ) deps      = other.deps      ; else if (+other.deps                    ) SWEAR(deps     ==other.deps     ,deps     ,other.deps     ) ;
		if      (!n_retries               ) n_retries = other.n_retries ; else if ( other.n_retries               ) SWEAR(n_retries==other.n_retries,n_retries,other.n_retries) ;
		pressure   = ::max(pressure ,other.pressure ) ;
		live_out  |= other.live_out                   ;
		reason    |= other.reason                     ;
		return *this ;
	}
	SubmitAttrs operator|(SubmitAttrs const& other) const {
		SubmitAttrs res = *this ;
		res |= other ;
		return res ;
	}
	// data
	// START_OF_VERSIONING
	BackendTag          tag       = {}    ;
	bool                live_out  = false ;
	uint8_t             n_retries = 0     ;
	Time::CoarseDelay   pressure  = {}    ;
	::vmap_s<DepDigest> deps      = {}    ;
	JobReason           reason    = {}    ;
	// END_OF_VERSIONING
} ;

struct JobInfoStart {
	friend ::ostream& operator<<( ::ostream& , JobInfoStart const& ) ;
	// accesses
	bool operator+() const { return +pre_start ; }
	bool operator!() const { return !+*this    ; }
	// data
	// START_OF_VERSIONING
	Hash::Crc   rule_cmd_crc = {}         ;
	::vector_s  stems        = {}         ;
	Time::Pdate eta          = {}         ;
	SubmitAttrs submit_attrs = {}         ;
	::vmap_ss   rsrcs        = {}         ;
	in_addr_t   host         = NoSockAddr ;
	JobRpcReq   pre_start    = {}         ;
	JobRpcReply start        = {}         ;
	::string    stderr       = {}         ;
	// END_OF_VERSIONING
} ;

struct JobInfoEnd {
	friend ::ostream& operator<<( ::ostream& , JobInfoEnd const& ) ;
	// accesses
	bool operator+() const { return +end    ; }
	bool operator!() const { return !+*this ; }
	// data
	// START_OF_VERSIONING
	JobRpcReq end = {} ;
	// END_OF_VERSIONING
} ;

struct JobInfo {
	// cxtors & casts
	JobInfo(                                       ) = default ;
	JobInfo( ::string const& ancillary_file        ) ;
	JobInfo( JobInfoStart&& jis                    ) : start{::move(jis)}                    {}
	JobInfo(                      JobInfoEnd&& jie ) :                      end{::move(jie)} {}
	JobInfo( JobInfoStart&& jis , JobInfoEnd&& jie ) : start{::move(jis)} , end{::move(jie)} {}
	// services
	void write(::string const& filename) const ;
	// data
	// START_OF_VERSIONING
	JobInfoStart start ;
	JobInfoEnd   end   ;
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
