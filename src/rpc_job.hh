// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "config.hh"

#include "disk.hh"
#include "hash.hh"
#include "lib.hh"
#include "serialize.hh"
#include "time.hh"

#include "autodep/env.hh"

ENUM_1( BackendTag                     // PER_BACKEND : add a tag for each backend
,	Dflt = Local
,	Local
,	Slurm
)

ENUM_4( Dflag                          // flags for deps
,	RuleMin    = Top                   // rules may have flags RuleMin<=flag<RuleMax1
,	RuleMax1   = Required              // .
,	HiddenMin  = Critical              // ldepend may report flags HiddenMin<=flag<HiddenMax1
,	HiddenMax1 = Static                // .
//
,	Top                                // dep is relative to repo top, not rule's cwd
//
,	Critical                           // if modified, ignore following deps
,	Essential                          // show when generating user oriented graphs
,	IgnoreError                        // propagate error if dep is in error (Error instead of Err because name is visible from user)
//
,	Required                           // dep must be buildable or job is in error
//
,	Static                             // dep is static
)
static constexpr char DflagChars[] = {
	't'                                // Top
//
,	'c'                                // Critical
,	's'                                // Essential
,	'e'                                // IgnoreError
//
,	'r'                                // Required
//
,	'S'                                // Static
} ;
static_assert(::size(DflagChars)==+Dflag::N) ;
using Dflags = BitMap<Dflag> ;
constexpr Dflags StaticDflags { Dflag::Essential , Dflag::Required , Dflag::Static } ; // used for static deps
constexpr Dflags DfltDflags   {                    Dflag::Required                 } ; // used with ldepend

ENUM_1( FileActionTag
,	HasFile                            // <=HasFile means action acts on file
,	Keep                               // no action, just check integrity
,	Unlink
,	Uniquify
,	Mkdir
,	Rmdir
)
struct FileAction {                                                            // XXX : add Crc so as to recognize when a file has been touched without modif
	friend ::ostream& operator<<( ::ostream& , FileAction const& ) ;
	FileActionTag tag       = FileActionTag::Unknown ;
	bool          manual_ok = false                  ;
	Hash::Crc     crc       ;
	Disk::Ddate   date      ;                              // expected date, mandatory if !manual_ok
} ;
::pair<vector_s/*unlinks*/,pair_s<bool/*ok*/>> do_file_actions( ::vmap_s<FileAction> const& pre_actions , Disk::NfsGuard& nfs_guard , Hash::Algo ) ;

ENUM( JobProc
,	None
,	Start
,	ReportStart
,	Continue       // req is killed but job is necessary for some other req
,	NotStarted     // req was killed before it actually started
,	ChkDeps
,	DepInfos
,	Decode
,	Encode
,	LiveOut
,	End
)

ENUM_2( JobReasonTag                   // see explanations in table below
,	HasNode = ClashTarget              // if >=HasNode, a node is associated
,	Err     = DepErr                   // if >=Err, job did not complete because of a dep
//
,	None
// with reason
,	ChkDeps
,	Cmd
,	Force
,	Garbage
,	Killed
,	Lost
,	New
,	OldErr
,	Rsrcs
// with node
,	ClashTarget
,	DepChanged
,	DepUnstable
,	DepNotReady
,	DepOutOfDate
,	NoTarget
,	PolutedTarget
,	PrevTarget
// with error
,	DepErr
,	DepMissingStatic
,	DepMissingRequired
,	DepOverwritten
)
static constexpr const char* JobReasonTagStrs[] = {
	"no reason"                                            // None
// with reason
,	"dep check requires rerun"                             // ChkDeps
,	"command changed"                                      // Cmd
,	"job forced"                                           // Force
,	"job ran with unstable data"                           // Garbage
,	"job was killed"                                       // Killed
,	"job was lost"                                         // Lost
,	"job was never run"                                    // New
,	"job was in error"                                     // OldErr
,	"resources changed and job was in error"               // Rsrcs
// with node
,	"multiple simultaneous writes"                         // ClashTarget
,	"dep changed"                                          // DepChanged
,	"dep unstable"                                         // DepUnstable
,	"dep not ready"                                        // DepNotReady
,	"dep out of date"                                      // DepOutOfDate
,	"missing target"                                       // NoTarget
,	"poluted target"                                       // PolutedTarget
,	"target previously existed"                            // PrevTarget
// with error
,	"dep in error"                                         // DepErr
,	"static dep missing"                                   // DepMissingStatic
,	"required dep missing"                                 // DepMissingRequired
,	"dep has been overwritten"                             // DepOverwritten
} ;
static_assert(::size(JobReasonTagStrs)==+JobReasonTag::N) ;

ENUM_2( Status                         // result of job execution
,	Early = EarlyLostErr               // <=Early means output has not been modified
,	Async = Killed                     // <=Async means job was interrupted asynchronously
,	New                                // job was never run
,	Manual                             // job was not started because some targets were manual
,	EarlyErr                           // job was not started because of error
,	EarlyLost                          // job was lost before starting     , retry
,	EarlyLostErr                       // job was lost before starting     , do not retry
,	LateLost                           // job was lost after having started, retry
,	LateLostErr                        // job was lost after having started, do not retry
,	Killed                             // job was killed
,	ChkDeps                            // dep check failed
,	Garbage                            // <=Garbage means job has not run reliably
,	Ok                                 // job execution ended successfully
,	Err                                // job execution ended in error
,	Timeout                            // job timed out
)
static inline bool  is_lost(Status s) { return s<=Status::LateLostErr && s>=Status::EarlyLost ; }
static inline Bool3 is_ok  (Status s) {
	switch (s) {
		case Status::New          : return Maybe ;
		case Status::Manual       : return No    ;
		case Status::EarlyErr     : return No    ;
		case Status::EarlyLost    : return Maybe ;
		case Status::EarlyLostErr : return No    ;
		case Status::LateLost     : return Maybe ;
		case Status::LateLostErr  : return No    ;
		case Status::Killed       :
		case Status::ChkDeps      :
		case Status::Garbage      : return Maybe ;
		case Status::Ok           : return Yes   ;
		case Status::Err          :
		case Status::Timeout      : return No    ;
		default : FAIL(s) ;
	}
}
static inline Status mk_err(Status s) {
	switch (s) {
		case Status::New          : return Status::EarlyErr     ;
		case Status::EarlyLost    : return Status::EarlyLostErr ;
		case Status::LateLost     : return Status::LateLostErr  ;
		case Status::Ok           : return Status::Err          ;
		case Status::Err          :
		default : FAIL(s) ;
	}
}

ENUM_4( Tflag                          // flags for targets
,	RuleMin    = Incremental           // rules may have flags RuleMin<=flag<RuleMax1
,	RuleMax1   = Unexpected            // .
,	HiddenMin  = Crc                   // ltarget may report flags HiddenMin<=flag<HiddenMax1
,	HiddenMax1 = Unexpected            // .
//
,	Incremental                        // reads are allowed (before earliest write if any)
,	ManualOk                           // ok to overwrite manual files
,	Match                              // make target non-official (no match on it)
,	Star                               // target is a star target, even if no star stems
,	Stat                               // inode accesses (stat-like) are not ignored if accessed as a dep
,	Top                                // target is defined with reference to repo top rather than cwd
,	Uniquify                           // target is uniquified if it has several links and is incremental
,	Warning                            // warn if target is unlinked and was generated by another rule
//
,	Crc                                // generate a crc for this target (compulsery if Match)
,	Dep                                // reads not followed by writes trigger dependencies
,	Essential                          // show when generating user oriented graphs
,	Phony                              // unlinks are allowed (possibly followed by reads which are ignored)
,	SourceOk                           // ok to overwrite source files
,	Write                              // writes are allowed (possibly followed by reads which are ignored)
//
,	Unexpected                         // target is not declared   , for internal use only
)
static constexpr char TflagChars[] = {
	'I'                                // Incremental
,	'M'                                // ManualOk
,	'N'                                // Match
,	'S'                                // Star
,	'T'                                // Stat (use T                as S already used)
,	'R'                                // Top  (use R, meaning root, as T already used)
,	'U'                                // Uniquify
,	'W'                                // Warning
//
,	'c'                                // Crc
,	'd'                                // Dep
,	'e'                                // Essential
,	'f'                                // Phony
,	's'                                // SourceOk
,	'w'                                // Write
//
,	'!'                                // Unexpected
} ;
static_assert(::size(TflagChars)==+Tflag::N) ;
using Tflags = BitMap<Tflag> ;
static constexpr Tflags DfltTflags      { Tflag::Match , Tflag::Warning , Tflag::Crc    , Tflag::Stat , Tflag::Write      } ; // default flags for targets
static constexpr Tflags UnexpectedTflags{ Tflag::Incremental , Tflag::Star , Tflag::Dep , Tflag::Stat , Tflag::Unexpected } ; // flags used for accesses that are not targets
static inline void chk(Tflags tf) {
	if ( tf[Tflag::Match   ] &&  tf[Tflag::Dep        ] ) throw "cannot match on target and be a potential dep"s            ;
	if ( tf[Tflag::Match   ] && !tf[Tflag::Crc        ] ) throw "cannot match on target without computing checksum"s        ;
	if ( tf[Tflag::Star    ] &&  tf[Tflag::Phony      ] ) throw "phony star targets not yet supported"s                     ;
	if ( tf[Tflag::Uniquify] && !tf[Tflag::Incremental] ) throw "flag uniquify is meaningless for non-incremental targets"s ;
	if ( tf[Tflag::SourceOk] && !tf[Tflag::Crc        ] ) throw "cannot update source without computing checksum"s          ;
}

static const ::string EnvPassMrkr = {'\0','p'} ;           // special illegal value to ask for value from environment
static const ::string EnvDynMrkr  = {'\0','d'} ;           // special illegal value to mark dynamically computed env variables

struct AccDflags {
	Disk::Accesses accesses ;
	Dflags         dflags   ;
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
		if (   tag>=Tag::Err    ) return *this ;
		if (jr.tag>=Tag::Err    ) return jr    ;
		if (   tag>=Tag::HasNode) return *this ;
		if (jr.tag>=Tag::HasNode) return jr    ;
		if (+*this              ) return *this ;
		/**/                      return jr    ;
	}
	JobReason& operator|=(JobReason jr) { *this = *this | jr ; return *this ; }
	::string msg() const {
		if (tag<Tag::HasNode) SWEAR(node==0,tag,node) ;
		return JobReasonTagStrs[+tag] ;
	}

	// data
	Tag     tag  = JobReasonTag::None ;
	NodeIdx node = 0                  ;
} ;

struct SubmitAttrs {
	friend ::ostream& operator<<( ::ostream& , SubmitAttrs const& ) ;
	// services
	SubmitAttrs& operator|=(SubmitAttrs const& other) {
		if      (      tag==BackendTag::Unknown) tag = other.tag ;
		else if (other.tag!=BackendTag::Unknown) SWEAR(tag==other.tag,tag,other.tag) ;
		SWEAR( !n_retries || !other.n_retries || n_retries==other.n_retries , n_retries , other.n_retries ) ; // n_retries does not depend on req, but may not always be present
		n_retries  = ::max(n_retries,other.n_retries) ;
		pressure   = ::max(pressure ,other.pressure ) ;
		live_out  |= other.live_out                   ;
		manual_ok |= other.manual_ok                  ;
		reason    |= other.reason                     ;
		return *this ;
	}
	SubmitAttrs operator|(SubmitAttrs const& other) const {
		SubmitAttrs res = *this ;
		res |= other ;
		return res ;
	}
	// data
	BackendTag        tag       = BackendTag::Unknown ;
	bool              live_out  = false               ;
	bool              manual_ok = false               ;
	uint8_t           n_retries = 0                   ;
	Time::CoarseDelay pressure  = {}                  ;
	JobReason         reason    = {}                  ;
} ;

struct JobStats {
	using Delay = Time::Delay ;
	// data
	Delay  cpu   = {} ;
	Delay  job   = {} ;                // elapsed in job
	Delay  total = {} ;                // elapsed including overhead
	size_t mem   = 0  ;                // in bytes
} ;

// for Dep recording in book-keeping, we want to derive from Node
// but if we derive from Node and have a field DepDigest, it is impossible to have a compact layout because of alignment constraints
// hence this solution : derive from a template argument
template<class B> struct DepDigestBase ;
template<class B> ::ostream& operator<<( ::ostream& , DepDigestBase<B> const& ) ;
template<class B> struct DepDigestBase : NoVoid<B> {
	friend ::ostream& operator<< <>( ::ostream& , DepDigestBase const& ) ;
	using Base = NoVoid<B> ;
	static constexpr bool HasBase = !::is_same_v<B,void> ;
	//
	using Accesses = Disk::Accesses ;
	using Crc      = Hash::Crc      ;
	using Ddate    = Time::Ddate    ;
	//cxtors & casts
	DepDigestBase(                                                     ) :                                                                      _crc { } {}
	DepDigestBase(          Accesses a , Dflags dfs , bool p           ) :           accesses{a} , dflags(dfs) , parallel{p} ,                  _crc { } {}
	DepDigestBase(          Accesses a , Dflags dfs , bool p , Crc   c ) :           accesses{a} , dflags(dfs) , parallel{p} , is_date{false} , _crc {c} {}
	DepDigestBase(          Accesses a , Dflags dfs , bool p , Ddate d ) :           accesses{a} , dflags(dfs) , parallel{p} , is_date{true } , _date{d} {}
	DepDigestBase( Base b , Accesses a , Dflags dfs , bool p           ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} ,                  _crc { } {}
	DepDigestBase( Base b , Accesses a , Dflags dfs , bool p , Crc   c ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} , is_date{false} , _crc {c} {}
	DepDigestBase( Base b , Accesses a , Dflags dfs , bool p , Ddate d ) : Base{b} , accesses{a} , dflags(dfs) , parallel{p} , is_date{true } , _date{d} {}
	//
	template<class B2> DepDigestBase( Base b , DepDigestBase<B2> const& dd ) : Base{b} , accesses{dd.accesses} , dflags(dd.dflags) , parallel{dd.parallel} {
		if (!accesses ) return ;
		if (dd.is_date) date(dd.date()) ;
		else            crc (dd.crc ()) ;
	}
	// accesses
	Crc   crc () const { SWEAR( +accesses && !is_date ) ; return _crc  ; }
	Ddate date() const { SWEAR( +accesses &&  is_date ) ; return _date ; }
	//
	void crc (Crc   c) { _crc  = c ; is_date = false ; }
	void date(Ddate d) { _date = d ; is_date = true  ; }
	//
	template<class X> void crc_date(DepDigestBase<X> const& dd) {
		if (dd.is_date) date(dd.date()) ;
		else            crc (dd.crc ()) ;
	}
	// data
	Accesses accesses   ;              // 3<=8 bits
	Dflags   dflags     ;              // 6<=8 bits
	bool     parallel:1 = false ;      //    1 bit
	bool     is_date :1 = false ;      //    1 bit
private :
	union {
		Crc   _crc  ;                  // ~46<64 bits
		Ddate _date ;                  // ~45<64 bits
	} ;
} ;
template<class B> ::ostream& operator<<( ::ostream& os , DepDigestBase<B> const& dd ) {
	const char* sep = "" ;
	/**/                                          os << "D("                           ;
	if constexpr ( !::is_void_v<B>            ) { os <<sep<< static_cast<B const&>(dd) ; sep = "," ; }
	if           ( +dd.accesses               ) { os <<sep<< dd.accesses               ; sep = "," ; }
	if           ( +dd.dflags                 ) { os <<sep<< dd.dflags                 ; sep = "," ; }
	if           ( dd.parallel                ) { os <<sep<< "parallel"                ; sep = "," ; }
	if           ( +dd.accesses && dd.is_date ) { os <<sep<< dd._date                  ; sep = "," ; }
	else if      ( +dd.accesses && +dd.crc()  ) { os <<sep<< dd._crc                   ; sep = "," ; }
	return                                        os <<')'                             ;
}

using DepDigest = DepDigestBase<void> ;
static_assert(::is_trivially_copyable_v<DepDigest>) ;                          // as long as this holds, we do not have to bother about union member cxtor/dxtor

struct TargetDigest {
	friend ::ostream& operator<<( ::ostream& , TargetDigest const& ) ;
	using Accesses = Disk::Accesses ;
	using Crc      = Hash::Crc      ;
	// data
	Accesses    accesses ;                                 // how target was accessed before it was written
	Tflags      tflags   ;
	bool        write    = false        ;                  // if true <=> file was written (and possibly further unlinked)
	Crc         crc      = Crc::Unknown ;                  // if None <=> file was unlinked, if Unknown <=> file is idle (not written, not unlinked)
	Time::Ddate date     = {}           ;
} ;

struct JobDigest {
	friend ::ostream& operator<<( ::ostream& , JobDigest const& ) ;
	// data
	Status                 status   = Status::New ;
	::vmap_s<TargetDigest> targets  = {}          ;
	::vmap_s<DepDigest   > deps     = {}          ;        // INVARIANT : sorted in first access order
	::string               stderr   = {}          ;
	::string               stdout   = {}          ;
	int                    wstatus  = 0           ;
	Time::Pdate            end_date = {}          ;
	JobStats               stats    = {}          ;
} ;

struct JobExecRpcReq ;

struct JobRpcReq {
	using P   = JobProc             ;
	using SI  = SeqId               ;
	using JI  = JobIdx              ;
	using MDD = ::vmap_s<DepDigest> ;
	friend ::ostream& operator<<( ::ostream& , JobRpcReq const& ) ;
	// statics
	static ::string s_mk_decode_file( ::string const& code , ::string const& file , ::string const& ctx ) {
		//
		return to_string("LMAKE/decode",file,".ctx_dir/",ctx,".code_dir/",code) ; // XXX : make a more robust file name (e.g. compute crc on file and ctx, or use mk_printable or ...)
	}
	static ::string s_mk_encode_file( ::string const& val , ::string const& file , ::string const& ctx ) {
		Hash::Xxh h ;
		h.update(val) ;
		return to_string("LMAKE/encode",file,".ctx_dir/",ctx,".val_dir/",::move(h).digest()) ; // XXX : make a more robust file name (e.g. compute crc on file and ctx, or use mk_printable or ...)
	}
	// cxtors & casts
	JobRpcReq() = default ;
	JobRpcReq( P p , SI si , JI j , in_port_t   pt , ::string&& m={} ) : proc{p} , seq_id{si} , job{j} , port  {pt              } , msg{::move(m)} { SWEAR( p==P::Start                     ) ; }
	JobRpcReq( P p , SI si , JI j , Status      s  , ::string&& m={} ) : proc{p} , seq_id{si} , job{j} , digest{.status=s       } , msg{::move(m)} { SWEAR( p==P::End && s<=Status::Garbage ) ; }
	JobRpcReq( P p , SI si , JI j , JobDigest&& d  , ::string&& m={} ) : proc{p} , seq_id{si} , job{j} , digest{::move(d)       } , msg{::move(m)} { SWEAR( p==P::End                       ) ; }
	JobRpcReq( P p , SI si , JI j ,                  ::string&& m={} ) : proc{p} , seq_id{si} , job{j} ,                            msg{::move(m)} { SWEAR( p==P::LiveOut                   ) ; }
	JobRpcReq( P p , SI si , JI j , MDD&&       ds                   ) : proc{p} , seq_id{si} , job{j} , digest{.deps=::move(ds)}                  { SWEAR( p==P::ChkDeps || p==P::DepInfos ) ; }
	//
	JobRpcReq( P p , SI si , JI j , ::string&& code , ::string&& f , ::string&& c              ) : proc{p} , seq_id{si} , job{j} , msg{code} , file{f} , ctx{c}               { SWEAR(p==P::Decode) ; }
	JobRpcReq( P p , SI si , JI j , ::string&& val  , ::string&& f , ::string&& c , uint8_t ml ) : proc{p} , seq_id{si} , job{j} , msg{val } , file{f} , ctx{c} , min_len{ml} { SWEAR(p==P::Encode) ; }
	//
	JobRpcReq( SI si , JI j , JobExecRpcReq&& jerr ) ;
	// services
	template<IsStream T> void serdes(T& s) {
		if (::is_base_of_v<::istream,T>) *this = JobRpcReq() ;
		::serdes(s,proc  ) ;
		::serdes(s,seq_id) ;
		::serdes(s,job   ) ;
		switch (proc) {
			case P::Start :
				::serdes(s,port) ;
				::serdes(s,msg ) ;
			break ;
			case P::LiveOut  : ::serdes(s,msg   ) ; break ;
			case P::ChkDeps  : ::serdes(s,digest) ; break ;
			case P::DepInfos : ::serdes(s,digest) ; break ;
			case P::Encode :
				::serdes(s,min_len) ;
				[[fallthrough]] ;
			case P::Decode :
				::serdes(s,msg ) ;
				::serdes(s,file) ;
				::serdes(s,ctx ) ;
			break ;
			case P::End :
				::serdes(s,digest) ;
				::serdes(s,msg   ) ;
			break ;
			default : FAIL(proc) ;
		}
	}
	// data
	P         proc    = P::None ;
	SI        seq_id  = 0       ;
	JI        job     = 0       ;
	in_port_t port    = 0       ;      // if proc == Start
	JobDigest digest  ;                // if proc ==         ChkDeps | DepInfos |                              End
	::string  msg     ;                // if proc == Start |                      LiveOut  | Decode | Encode | End
	::string  file    ;                // if proc ==                                         Decode | Encode
	::string  ctx     ;                // if proc ==                                         Decode | Encode
	uint8_t   min_len ;                // if proc ==                                                  Encode
} ;

struct TargetSpec {
	friend ::ostream& operator<<( ::ostream& , TargetSpec const& ) ;
	::string pattern ;
	Tflags   tflags  = DfltTflags ;
} ;

ENUM_2( AutodepMethod
,	Ld   = LdAudit                                         // >=Ld means a lib is pre-loaded (through LD_AUDIT or LD_PRELOAD)
,	Dflt = AutodepMethod::LdPreload                        // by default, use  a compromize between speed an reliability, might sense HAS_LD_AUDIT and HAS_SECCOMP if necessary
,	None
,	Ptrace
,	LdAudit
,	LdPreload
)

struct JobRpcReply {
	friend ::ostream& operator<<( ::ostream& , JobRpcReply const& ) ;
	using Accesses = Disk::Accesses ;
	using Crc      = Hash::Crc      ;
	using Proc     = JobProc        ;
	// cxtors & casts
	JobRpcReply(                                                    ) = default ;
	JobRpcReply( Proc p                                             ) : proc{p}                 {                                                       }
	JobRpcReply( Proc p , Bool3                                  o  ) : proc{p} , ok       {o } { SWEAR( proc==Proc::ChkDeps                        ) ; }
	JobRpcReply( Proc p , ::vector<pair<Bool3/*ok*/,Crc>> const& is ) : proc{p} , dep_infos{is} { SWEAR( proc==Proc::DepInfos                       ) ; }
	JobRpcReply( Proc p , ::string                        const& t  ) : proc{p} , txt      {t } { SWEAR( proc==Proc::Decode   || proc==Proc::Encode ) ; }
	// services
	template<IsStream S> void serdes(S& s) {
		if (is_base_of_v<::istream,S>) *this = JobRpcReply() ;
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::None     :
			case Proc::End      :                         break ;
			case Proc::DepInfos : ::serdes(s,dep_infos) ; break ;
			case Proc::ChkDeps  : ::serdes(s,ok       ) ; break ;
			case Proc::Decode   :
			case Proc::Encode   :
				::serdes(s,ok ) ;
				::serdes(s,txt) ;
			break ;
			case Proc::Start :
				::serdes(s,addr            ) ;
				::serdes(s,autodep_env     ) ;
				::serdes(s,chroot          ) ;
				::serdes(s,cmd             ) ;
				::serdes(s,cwd_s           ) ;
				::serdes(s,env             ) ;
				::serdes(s,hash_algo       ) ;
				::serdes(s,interpreter     ) ;
				::serdes(s,keep_tmp        ) ;
				::serdes(s,kill_sigs       ) ;
				::serdes(s,live_out        ) ;
				::serdes(s,method          ) ;
				::serdes(s,pre_actions     ) ;
				::serdes(s,remote_admin_dir) ;
				::serdes(s,small_id        ) ;
				::serdes(s,static_deps     ) ;
				::serdes(s,stdin           ) ;
				::serdes(s,stdout          ) ;
				::serdes(s,targets         ) ;
				::serdes(s,timeout         ) ;
				::serdes(s,trace_n_jobs    ) ;
				::serdes(s,use_script      ) ;
			break ;
			default : FAIL(proc) ;
		}
	}
	// data
	Proc                      proc             = Proc::None          ;
	in_addr_t                 addr             = 0                   ;         // proc == Start                 , the address at which server and subproccesses can contact job_exec
	AutodepEnv                autodep_env      ;                               // proc == Start
	::string                  chroot           ;                               // proc == Start
	::pair_ss/*script,call*/  cmd              ;                               // proc == Start
	::string                  cwd_s            ;                               // proc == Start
	::vmap_ss                 env              ;                               // proc == Start
	Hash::Algo                hash_algo        = Hash::Algo::Unknown ;         // proc == Start
	::vector_s                interpreter      ;                               // proc == Start                 , actual interpreter used to execute cmd
	bool                      keep_tmp         = false               ;         // proc == Start
	vector<uint8_t>           kill_sigs        ;                               // proc == Start
	bool                      live_out         = false               ;         // proc == Start
	AutodepMethod             method           = AutodepMethod::Dflt ;         // proc == Start
	::vmap_s<FileAction>      pre_actions      ;                               // proc == Start
	::string                  remote_admin_dir ;                               // proc == Start
	SmallId                   small_id         = 0                   ;         // proc == Start
	::vmap_s<DepDigest>       static_deps      ;                               // proc == Start                 , deps that may clash with targets
	::string                  stdin            ;                               // proc == Start
	::string                  stdout           ;                               // proc == Start
	::vmap_s<Tflags>          targets          ;                               // proc == Start
	Time::Delay               timeout          ;                               // proc == Start
	JobIdx                    trace_n_jobs     = 0                   ;         // proc == Start
	bool                      use_script       = false               ;         // proc == Start
	Bool3                     ok               = Maybe               ;         // proc == ChkDeps|Decode|Encode , if No <=> deps in error, if Maybe <=> deps not ready
	::vector<pair<Bool3,Crc>> dep_infos        ;                               // proc == DepInfos
	::string                  txt              ;                               // proc ==         Decode|Encode , value for Decode, code for Encode
} ;

ENUM_1( JobExecRpcProc
,	HasFile = Access                   // >=HasFile means files field is significative
,	None
,	ChkDeps
,	CriticalBarrier
,	Tmp                                // write activity in tmp has been detected (hence clean up is required)
,	Trace                              // no algorithmic info, just for tracing purpose
,	Access
,	Confirm
,	Decode
,	DepInfos
,	Encode
,	Guard
)

ENUM_1( AccessOrder
,	Write = InbetweenWrites            // >=Write means access comes after first write
,	Before
,	BetweenReadAndWrite
,	InbetweenWrites
,	After
)

struct JobExecRpcReq {
	friend ::ostream& operator<<( ::ostream& , JobExecRpcReq const& ) ;
	// make short lines
	using Accesses = Disk::Accesses ;
	using P        = JobExecRpcProc ;
	using PD       = Time::Pdate    ;
	using DD       = Time::Ddate    ;
	//
	struct AccessDigest {                                                      // order is read, then write, then unlink
		friend ::ostream& operator<<( ::ostream& , AccessDigest const& ) ;
		// accesses
		bool idle() const { return !write && !unlink ; }                       // for state management, handle Maybe as Yes
		// services
		bool operator==(AccessDigest const& ad) const = default ;              // XXX : why is this necessary at all ?!?
		AccessDigest operator|(AccessDigest const& ad) const {                 // *this, then other
			AccessDigest res = *this ;
			res |= ad ;
			return res ;
		}
		AccessDigest& operator|=(AccessDigest const& ad) {
			update(ad,AccessOrder::After) ;
			return *this ;
		}
		// update this with access from ad, which may be before or after this (or between the read part and the write part is after==Maybe)
		void update( AccessDigest const& , AccessOrder ) ;
		//
		void confirm(bool ok) {
			if (ok) { prev_write = write      ; prev_unlink = unlink      ; }
			else    { write      = prev_write ; unlink      = prev_unlink ; }
		}
		// data
		Accesses accesses    = {}    ; // if +accesses <=> files are read
		Dflags   dflags      = {}    ;
		Tflags   neg_tflags  = {}    ; // if write, removed Tflags
		Tflags   pos_tflags  = {}    ; // if write, added   Tflags
		bool     prev_write  = false ;
		bool     write       = false ; // if true <=> files are written, possibly unlinked later
		bool     prev_unlink = false ; // if true <=> files are unlinked at the end, possibly written before
		bool     unlink      = false ; // if true <=> files are unlinked at the end, possibly written before
	} ;
	// statics
private :
	static ::vmap_s<DD> _s_mk_mdd(::vector_s&& fs) { ::vmap_s<DD> res ; for( ::string& f : fs ) res.emplace_back(::move(f),DD()) ; return res ; }
	// cxtors & casts
public :
	JobExecRpcReq(                                )                                      {                                                                    }
	JobExecRpcReq( P p , bool s , ::string&& t={} ) : proc{p} , sync{s} , txt{::move(t)} { if (+t) SWEAR(p==P::Tmp||p==P::Trace) ; else SWEAR(p<P::HasFile) ; }
	JobExecRpcReq( P p ,          ::string&& t={} ) : proc{p} ,           txt{::move(t)} { if (+t) SWEAR(p==P::Tmp||p==P::Trace) ; else SWEAR(p<P::HasFile) ; }
	//
	JobExecRpcReq( P p , ::vmap_s<DD>&& fs , bool ad , Accesses a , Dflags dfs , bool nf ) :
		proc     {p                      }
	,	sync     {true                   }
	,	auto_date{ad                     }
	,	no_follow{nf                     }
	,	files    {::move(fs)             }
	,	digest   {.accesses=a,.dflags=dfs}
	{ SWEAR(p==P::DepInfos) ; }
	//                                                                                                                   auto_date,      no_follow
	JobExecRpcReq( P p , ::vmap_s<DD>&& fs , Accesses a , Dflags dfs           ) : JobExecRpcReq{p,          ::move(fs) ,false    ,a,dfs,false    } {}
	JobExecRpcReq( P p , ::vector_s  && fs , Accesses a , Dflags dfs , bool nf ) : JobExecRpcReq{p,_s_mk_mdd(::move(fs)),true     ,a,dfs,nf       } {}
	//
	JobExecRpcReq( P p , ::vmap_s<DD>&& fs , bool ad , AccessDigest const& d , bool nf , bool s , ::string const& comment={} ) :
		proc     {p         }
	,	sync     {s         }
	,	auto_date{ad        }
	,	no_follow{nf        }
	,	files    {::move(fs)}
	,	digest   {d         }
	,	txt      {comment   }
	{ SWEAR(p==P::Access) ; }
	//                                                                                                                                                  auto_date,   no_follow,sync
	JobExecRpcReq( P p , ::vmap_s<DD>&& fs , AccessDigest const& ad ,           bool s , ::string const& c={} ) : JobExecRpcReq{p,          ::move(fs) ,false    ,ad,false    ,s    ,c} {}
	JobExecRpcReq( P p , ::vmap_s<DD>&& fs , AccessDigest const& ad ,                    ::string const& c={} ) : JobExecRpcReq{p,          ::move(fs) ,false    ,ad,false    ,false,c} {}
	JobExecRpcReq( P p , ::vector_s  && fs , AccessDigest const& ad , bool nf , bool s , ::string const& c={} ) : JobExecRpcReq{p,_s_mk_mdd(::move(fs)),true     ,ad,nf       ,s    ,c} {}
	JobExecRpcReq( P p , ::vector_s  && fs , AccessDigest const& ad , bool nf ,          ::string const& c={} ) : JobExecRpcReq{p,_s_mk_mdd(::move(fs)),true     ,ad,nf       ,false,c} {}
	//
	JobExecRpcReq( P p , ::vector_s&& fs , bool ok_        ) : proc{p} , ok{ok_} , files{_s_mk_mdd(::move(fs))}                  { SWEAR(p==P::Confirm) ; }
	JobExecRpcReq( P p , ::vector_s&& fs , ::string&& c={} ) : proc{p} ,           files{_s_mk_mdd(::move(fs))} , txt{::move(c)} { SWEAR(p==P::Guard  ) ; }
	//
	JobExecRpcReq( P p , ::string&& f , ::string&& code , ::string&& ctx_              ) : proc{p} ,               files{{{::move(f),{}}}} , txt{code} , ctx{ctx_} { SWEAR(p==P::Decode) ; }
	JobExecRpcReq( P p , ::string&& f , ::string&& val  , ::string&& ctx_ , uint8_t ml ) : proc{p} , min_len{ml} , files{{{::move(f),{}}}} , txt{val } , ctx{ctx_} { SWEAR(p==P::Encode) ; }
	// services
public :
	template<IsStream T> void serdes(T& s) {
		if (::is_base_of_v<::istream,T>) *this = {} ;
		::serdes(s,proc) ;
		::serdes(s,date) ;
		::serdes(s,sync) ;
		if (proc>=P::HasFile) ::serdes(s,files) ;
		switch (proc) {
			case P::Confirm : ::serdes(s,ok ) ; break ;
			case P::Guard   :
			case P::Tmp     :
			case P::Trace   : ::serdes(s,txt) ; break ;
			case P::Access   :
				::serdes(s,txt) ;
				[[fallthrough]] ;
			case P::DepInfos :
				::serdes(s,digest   ) ;
				::serdes(s,auto_date) ;
				::serdes(s,no_follow) ;
			break ;
			case P::Encode :
				::serdes(s,min_len) ;
				[[fallthrough]] ;
			case P::Decode :
				::serdes(s,ctx) ;
				::serdes(s,txt) ;
			break ;
			default : ;
		}
	}
	// data
	P            proc      = P::None     ;
	bool         sync      = false       ;
	bool         auto_date = false       ;                 // if proc>=HasFile, if true <=> files must be solved and dates added by probing disk (for autodep internal use, not to be sent to job_exec)
	bool         no_follow = false       ;                 // if files have yet to be made real, whether links should not be followed
	bool         ok        = false       ;                 // if proc==Confirm
	uint8_t      min_len   = 0           ;                 // if proc==Encode
	PD           date      = PD::s_now() ;                 // access date to reorder accesses during analysis
	::vmap_s<DD> files     ;
	AccessDigest digest    ;
	::string     txt       ;                               // if proc==Access|Decode|Encode|Trace (comment for Access, code for Decode, value for Encode)
	::string     ctx       ;                               // if proc==Decode|Encode
} ;

struct JobExecRpcReply {
	friend ::ostream& operator<<( ::ostream& , JobExecRpcReply const& ) ;
	using Proc = JobExecRpcProc ;
	using Crc  = Hash::Crc      ;
	// cxtors & casts
	JobExecRpcReply(                                                    ) = default ;
	JobExecRpcReply( Proc p                                             ) : proc{p}                 { SWEAR( proc!=Proc::ChkDeps && proc!=Proc::DepInfos ) ; }
	JobExecRpcReply( Proc p , Bool3 o                                   ) : proc{p} , ok       {o } { SWEAR( proc==Proc::ChkDeps                         ) ; }
	JobExecRpcReply( Proc p , ::vector<pair<Bool3/*ok*/,Crc>> const& is ) : proc{p} , dep_infos{is} { SWEAR( proc==Proc::DepInfos                        ) ; }
	JobExecRpcReply( Proc p , ::string const&                        t  ) : proc{p} , txt      {t } { SWEAR( proc==Proc::Decode  || proc==Proc::Encode   ) ; }
	//
	JobExecRpcReply( JobRpcReply const& jrr ) ;
	// services
	template<IsStream S> void serdes(S& s) {
		if (::is_base_of_v<::istream,S>) *this = JobExecRpcReply() ;
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::ChkDeps  : ::serdes(s,ok       ) ; break ;
			case Proc::DepInfos : ::serdes(s,dep_infos) ; break ;
			default : ;
		}
	}
	// data
	Proc                            proc      = Proc::None ;
	Bool3                           ok        = Maybe      ;                   // if proc==ChkDeps
	::vector<pair<Bool3/*ok*/,Crc>> dep_infos ;                                // if proc==DepInfos
	::string                        txt       ;                                // if proc==Decode|Encode (value for Decode, code for Encode)
} ;

//
// JobSserverRpcReq
//

ENUM( JobServerRpcProc
,	Heartbeat
,	Kill
)

struct JobServerRpcReq {
	friend ::ostream& operator<<( ::ostream& , JobServerRpcReq const& ) ;
	using Proc = JobServerRpcProc ;
	// cxtors & casts
	JobServerRpcReq(                              ) = default ;
	JobServerRpcReq( Proc p , SeqId si            ) : proc{p} , seq_id{si}          { SWEAR(proc==Proc::Kill     ) ; }
	JobServerRpcReq( Proc p , SeqId si , JobIdx j ) : proc{p} , seq_id{si} , job{j} {                                } // need a job for heartbeat as we may have to reply on its behalf
	// services
	template<IsStream S> void serdes(S& s) {
		::serdes(s,proc  ) ;
		::serdes(s,seq_id) ;
		switch (proc) {
			case Proc::Heartbeat : ::serdes(s,job) ;                          break ;
			case Proc::Kill      : if (::is_base_of_v<::istream,S>) job = 0 ; break ;
			default : FAIL(proc) ;
		}
	}
	// data
	Proc   proc   = Proc::Unknown ;
	SeqId  seq_id = 0             ;
	JobIdx job    = 0             ;
} ;

struct JobInfoStart {
	friend ::ostream& operator<<( ::ostream& , JobInfoStart const& ) ;
	// data
	Time::Pdate eta          = {}         ;
	SubmitAttrs submit_attrs = {}         ;
	::vmap_ss   rsrcs        = {}         ;
	in_addr_t   host         = NoSockAddr ;
	JobRpcReq   pre_start    = {}         ;
	JobRpcReply start        = {}         ;
	::string    stderr       = {}         ;
} ;

struct JobInfoEnd {
	friend ::ostream& operator<<( ::ostream& , JobInfoEnd const& ) ;
	// data
	JobRpcReq end = {} ;
} ;
