// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "serialize.hh"
#include "time.hh"

#include "rpc_job_common.hh"

ENUM_1( JobExecProc
,	HasFiles = Access // >=HasFiles means files field is significative
,	None
,	ChkDeps
,	Tmp               // write activity in tmp has been detected (hence clean up is required)
,	Trace             // no algorithmic info, just for tracing purpose
,	Panic             // ensure job is in error
,	Confirm
,	Access
,	Guard
,	DepVerbose
,	Decode
,	Encode
)

struct AccessDigest {                                                  // order is first read, first write, last write, unlink
	friend ::ostream& operator<<( ::ostream& , AccessDigest const& ) ;
	// accesses
	bool operator+() const { return +accesses || write!=No ; }         // true if some access of some sort is done
	bool operator!() const { return !+*this                ; }
	// services
	bool          operator==(AccessDigest const&      ) const = default ;
	AccessDigest& operator|=(AccessDigest const&      ) ;
	AccessDigest  operator| (AccessDigest const& other) const { return ::copy(*this)|= other ; }
	// data
	Bool3       write        = No ;                                    // if Maybe, write is not confirmed
	Accesses    accesses     = {} ;
	Tflags      tflags       = {} ;                                    // dflags are inherited from DepDigest
	ExtraTflags extra_tflags = {} ;
	Dflags      dflags       = {} ;
	ExtraDflags extra_dflags = {} ;
} ;

struct JobExecRpcReq {
	friend ::ostream& operator<<( ::ostream& , JobExecRpcReq const& ) ;
	// make short lines
	using Pdate    = Time::Pdate    ;
	using FileInfo = Disk::FileInfo ;
	using AD       = AccessDigest   ;
	using P        = JobExecProc    ;
	// statics
private :
	static ::vmap_s<FileInfo> _s_mk_mdd(::vector_s&& fs) { ::vmap_s<FileInfo> res ; for( ::string& f : fs ) res.emplace_back(::move(f),FileInfo()) ; return res ; }
	// cxtors & casts
public :
	JobExecRpcReq() = default ;
	// Confirm always has a confirm argument
	static constexpr P Confirm = P::Confirm ;
	JobExecRpcReq( P p ,                   ::string&& t={} ) : proc{p} ,                                                              txt{::move(t)} { SWEAR( p<P::HasFiles && p!=Confirm ) ; }
	JobExecRpcReq( P p , bool sc         , ::string&& t={} ) : proc{p} , sync{sc&&p!=Confirm} , digest{.write=(No|sc)&(p==Confirm)} , txt{::move(t)} { SWEAR( p<P::HasFiles               ) ; }
	JobExecRpcReq( P p , bool s , bool c , ::string&& t={} ) : proc{p} , sync{s             } , digest{.write= No|c               } , txt{::move(t)} { SWEAR(                  p==Confirm ) ; }
	//
private :
	JobExecRpcReq( P p , bool slv , ::string&& cwd_ , ::vmap_s<FileInfo>&& fs , AccessDigest const& d , bool nf , bool s , ::string&& comment ) :
		proc     { p               }
	,	sync     { s               }
	,	solve    { slv             }
	,	no_follow{ nf              }
	,	cwd      { ::move(cwd_)    }
	,	files    { ::move(fs)      }
	,	digest   { d               }
	,	txt      { ::move(comment) }
	{ SWEAR( p==P::Access || p==P::DepVerbose ) ; }
public : //!                                                                                                         solve cwd                                 no_follow sync
	JobExecRpcReq( P p , ::vmap_s<FileInfo>&& fs , AD const& ad ,           bool s , ::string&& c ) : JobExecRpcReq{p,false,{}         ,          ::move(fs) ,ad,false   ,s    ,::move(c)} {}
	JobExecRpcReq( P p , ::vmap_s<FileInfo>&& fs , AD const& ad ,                    ::string&& c ) : JobExecRpcReq{p,false,{}         ,          ::move(fs) ,ad,false   ,false,::move(c)} {}
	JobExecRpcReq( P p , ::vector_s        && fs , AD const& ad , bool nf , bool s , ::string&& c ) : JobExecRpcReq{p,true ,Disk::cwd(),_s_mk_mdd(::move(fs)),ad,nf      ,s    ,::move(c)} {}
	JobExecRpcReq( P p , ::vector_s        && fs , AD const& ad , bool nf ,          ::string&& c ) : JobExecRpcReq{p,true ,Disk::cwd(),_s_mk_mdd(::move(fs)),ad,nf      ,false,::move(c)} {}
	//
	JobExecRpcReq( P p , ::vector_s&& fs , ::string&& c={} ) : proc{p} , files{_s_mk_mdd(::move(fs))} , txt{::move(c)} { SWEAR(p==P::Guard) ; }
	//
	JobExecRpcReq( P p , ::string&& f , ::string&& code , ::string&& c ) :
		proc   { p                     }
	,	sync   { true                  }
	,	solve  { true                  }
	,	cwd    { Disk::cwd()           }
	,	files  { {{::move(f),{}}}      }                       // no need for date for codec
	,	digest { .accesses=Access::Reg }
	,	txt    { code                  }
	,	ctx    { c                     }
	{ SWEAR(p==P::Decode) ; }
	JobExecRpcReq( P p , ::string&& f , ::string&& val , ::string&& c , uint8_t ml ) :
		proc    { p                     }
	,	sync    { true                  }
	,	solve   { true                  }
	,	min_len { ml                    }
	,	cwd     { Disk::cwd()           }
	,	files   { {{::move(f),{}}}      }                      // no need for date for codec
	,	digest  { .accesses=Access::Reg }
	,	txt     { val                   }
	,	ctx     { c                     }
	{ SWEAR(p==P::Encode) ; }
	// services
public :
	template<IsStream T> void serdes(T& s) {
		if (::is_base_of_v<::istream,T>) *this = {} ;
		::serdes(s,proc) ;
		::serdes(s,date) ;
		::serdes(s,sync) ;
		if (proc>=P::HasFiles) {
			::serdes(s,solve) ;
			::serdes(s,files) ;
			if (solve) {
				::serdes(s,cwd      ) ;
				::serdes(s,no_follow) ;
			}
		}
		switch (proc) {
			case P::ChkDeps    :
			case P::Tmp        :
			case P::Trace      :
			case P::Panic      :
			case P::Guard      :                                                  break ;
			case P::Confirm    : ::serdes(s,digest.write) ;                       break ;
			case P::Access     :
			case P::DepVerbose : ::serdes(s,digest      ) ;                       break ;
			case P::Decode     : ::serdes(s,ctx         ) ;                       break ;
			case P::Encode     : ::serdes(s,ctx         ) ; ::serdes(s,min_len) ; break ;
			default : ;
		}
		::serdes(s,txt) ;
	}
	// data
	P                  proc      = P::None                   ;
	bool               sync      = false                     ;
	bool               solve     = false                     ; // if proc>=HasFiles, if true <=> files must be solved and dates added by probing disk
	bool               no_follow = false                     ; // if solve, whether links should not be followed
	uint8_t            min_len   = 0                         ; // if proc==Encode
	Pdate              date      = New                       ; // access date to reorder accesses during analysis
	::string           cwd       ;                             // if solve, cwd to use to solve files
	::vmap_s<FileInfo> files     ;
	AccessDigest       digest    ;
	::string           txt       ;                             // if proc==Access|Decode|Encode|Trace (comment for Access, code for Decode, value for Encode)
	::string           ctx       ;                             // if proc==Decode|Encode
} ;

struct JobExecRpcReply {
	friend ::ostream& operator<<( ::ostream& , JobExecRpcReply const& ) ;
	using Proc = JobExecProc ;
	using Crc  = Hash::Crc   ;
	// cxtors & casts
	JobExecRpcReply(                                                              ) = default ;
	JobExecRpcReply( Proc p                                                       ) : proc{p}                         { SWEAR( proc!=Proc::ChkDeps && proc!=Proc::DepVerbose ) ; }
	JobExecRpcReply( Proc p , Bool3 o                                             ) : proc{p} , ok{o}                 { SWEAR( proc==Proc::ChkDeps                           ) ; }
	JobExecRpcReply( Proc p ,           ::vector<pair<Bool3/*ok*/,Crc>> const& is ) : proc{p} ,         dep_infos{is} { SWEAR( proc==Proc::DepVerbose                        ) ; }
	JobExecRpcReply( Proc p , Bool3 o , ::string const&                        t  ) : proc{p} , ok{o} , txt      {t } { SWEAR( proc==Proc::Decode || proc==Proc::Encode      ) ; }
	// services
	template<IsStream S> void serdes(S& s) {
		if (::is_base_of_v<::istream,S>) *this = {} ;
		::serdes(s,proc) ;
		switch (proc) {
			case Proc::Access     :                         break ;
			case Proc::ChkDeps    : ::serdes(s,ok       ) ; break ;
			case Proc::DepVerbose : ::serdes(s,dep_infos) ; break ;
			case Proc::Decode :
			case Proc::Encode :
				::serdes(s,ok ) ;
				::serdes(s,txt) ;
			break ;
		DF}
	}
	// data
	Proc                            proc      = Proc::None ;
	Bool3                           ok        = Maybe      ; // if proc==ChkDeps |Decode|Encode
	::vector<pair<Bool3/*ok*/,Crc>> dep_infos ;              // if proc==DepVerbose
	::string                        txt       ;              // if proc==         Decode|Encode (value for Decode, code for Encode)
} ;
