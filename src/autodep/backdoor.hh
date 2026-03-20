// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "record.hh"

namespace Backdoor {

	template<class T> struct Expected {
		static constexpr size_t MinSz = sizeof(bool/*ok*/) + sizeof(size_t/*sz*/) ; // in all cases, we must be able to reply the required size
		template<IsStream S> void serdes(S& s) {
			/**/    ::serdes( s , ok   ) ;
			if (ok) ::serdes( s , data ) ;
			else    ::serdes( s , sz   ) ;
		}
		// data
		bool   ok   = false ;
		size_t sz   = 0     ;                                                       // required size to serialize
		T      data = {}    ;                                                       // the data if the buffer size was ok to deserialize
	} ;

	static constexpr Fd     MagicFd     { Fd::Cwd.fd - 100 }              ; // any improbable negative value (to avoid conflict with real fd) will do
	static constexpr char   MagicPfx[]  = PRIVATE_ADMIN_DIR_S "backdoor/" ; // any improbable prefix will do
	static constexpr size_t MagicPfxLen = sizeof(MagicPfx)-1              ; // -1 to account for terminating null

	using Func = ::function<ssize_t/*len*/(Record&,::string const& args,char* buf,size_t sz)> ;

	::umap_s<Func> const& get_func_tab() ;

	template<class T> using _Reply = decltype(::declval<T>().process(::declval<Record&>())) ;

	template<class T> _Reply<T> call(T const& args) {
		static constexpr size_t ErrMsgSz = 1000 ;                                                                      // quite comfortable for an error message
		::string file = cat(MagicPfx,T::Cmd,'/',mk_printable(serialize(args)))              ;
		size_t   sz   = ::max( ErrMsgSz , ::max( T::MaxReplySz , Expected<T>::MinSz ) ) + 1 ;                          // +1 to distinguish truncation
		for( int i=0 ;; i++ ) {
			::string buf ( sz , 0 )                                                 ;
			ssize_t  cnt = ::readlinkat( MagicFd , file.c_str() , buf.data() , sz ) ;                                  // try to go through autodep to process args
			if (cnt<0)
				switch (errno) {
					case ECOMM        : throw cat("cannot poke reply while ",args.descr()) ;
					case EPROTO       : throw cat("internal error while "   ,args.descr()) ;
					case ECONNABORTED : {
						size_t pos = buf.find(char(0)) ;
						if (pos==Npos) {
							if (sz>=4) buf.resize(sz-4) ;
							/**/       buf += " ..." ;
						}
						if (pos==0) throw cat("cannot ",args.descr(                                   )) ;
						else        throw cat("cannot ",args.descr(cat('(',substr_view(buf,0,pos),')'))) ;
					} break ;
					default : {                                                                                        // non-magic error
						Lock lock { Record::s_mutex } ;
						return ::copy(args).process(::ref(Record(New,Yes/*enabled*/))) ;                               // no autodep available, directly process args
					}
				}                                                                                                      // NO_COV
			SWEAR( size_t(cnt)<buf.size() , cnt,buf.size() ) ;
			buf.resize(size_t(cnt)) ;
			auto reply = deserialize<Expected<_Reply<T>>>(buf) ;
			if ( reply.ok                      ) return ::move(reply.data)                                           ;
			if ( T::ReliableMaxReplySz || i>=7 ) throw cat("backdoor length error provided ",sz," needed ",reply.sz) ; // result may not be stable, so we may require more than 2 trials
			sz = ::max( 2*sz , reply.sz ) ;                                                                            // ensure logarithmic behavior, limited to 256*hint size
		}
	}

	template<class T> ssize_t/*len*/ func( Record& r , ::string const& args_str , char* buf , size_t sz ) {
		::string            parsed    ;
		T                   cmd       ;
		Expected<_Reply<T>> reply     { .ok=true } ;
		::string            reply_str ;
		int                 err       = EPROTO     ;
		::string            msg       ;
		//
		try { size_t pos=0 ; parsed    =parse_printable(args_str,pos) ; throw_unless(pos==args_str.size(),"parse args") ; } catch (::string const& e) {                    msg=e ; goto Err ; }
		try {                cmd       =deserialize<T>(parsed)        ;                                                   } catch (::string const& e) {                    msg=e ; goto Err ; }
		try {                reply.data=cmd.process(r)                ;                                                   } catch (::string const& e) { err=ECONNABORTED ; msg=e ; goto Err ; }
		try {                reply_str =serialize(reply)              ;                                                   } catch (::string const& e) {                    msg=e ; goto Err ; }
		//
		if (reply_str.size()>=sz) {                             // if reply does not fit, replace with a short reply that provides the necessary size and caller will retry
			reply.ok  = false            ;
			reply.sz  = reply_str.size() ;
			reply_str = serialize(reply) ;
		}
		SWEAR( reply_str.size()<sz , reply_str.size(),sz ) ;    // ensure final reply fits
		::memcpy( buf , reply_str.data() , reply_str.size() ) ;
		return reply_str.size() ;
	Err :
		::memcpy( buf , msg.c_str() , ::min(sz,msg.size()+1 ) ) ;
		return -err ;
	}

	struct Enable {
		static constexpr char   Cmd[]              = "enable"     ;
		static constexpr bool   ReliableMaxReplySz = true         ;
		static constexpr size_t MaxReplySz         = sizeof(bool) ;
		// accesses
		void operator>>(::string&) const ;
		// services
		bool/*enabled*/ process(Record&         r        )       ;
		::string        descr  (::string const& reason={}) const ;
		// data
		Bool3 enable = Maybe ; // Maybe means dont update record state
	} ;

	struct Regexpr {
		static constexpr char   Cmd[]              = "regexpr" ;
		static constexpr bool   ReliableMaxReplySz = true      ;
		static constexpr size_t MaxReplySz         = 0         ;
		// accesses
		void operator>>(::string&) const ;
		// services
		::monostate process(Record&         r        )       ;
		::string    descr  (::string const& reason={}) const ;
		// data
		::vector_s   files         = {} ;
		AccessDigest access_digest = {} ;
	} ;

	struct AccessBase {
		// services
		template<IsStream S> void serdes(S& s) {
			::serdes( s , files,access_digest,no_follow ) ;
		}
	protected :
		::vmap_s<Disk::FileInfo> _mk_deps( Record& r , bool sync , ::vector<NodeIdx>* /*out*/ dep_idxs1=nullptr )       ;
		::string                 _descr  (const char* cmd,::string const& reason={}                             ) const { return cat(cmd,reason,' ',files) ; }
		// data
	public :
		::vector_s   files         = {}    ;
		AccessDigest access_digest = {}    ;
		bool         no_follow     = false ;
	} ;

	struct Depend : AccessBase {
		static constexpr char   Cmd[]              = "depend" ;
		static constexpr bool   ReliableMaxReplySz = true     ;
		static constexpr size_t MaxReplySz         = 0        ;
		// accesses
		void operator>>(::string&) const ;
		// services
		::monostate process(Record& r                )       ;
		::string    descr  (::string const& reason={}) const { return AccessBase::_descr(Cmd,reason) ; }
	} ;

	struct DependVerbose : AccessBase {
		static constexpr char   Cmd[]              = "depend_verbose"                           ;
		static constexpr bool   ReliableMaxReplySz = false                                      ;
		static constexpr size_t MaxReplySz         = sizeof(SerdesSz) + 256*sizeof(VerboseInfo) ;
		// accesses
		void operator>>(::string&) const ;
		// services
		::vector<VerboseInfo> process(Record&         r        )       ;
		::string              descr  (::string const& reason={}) const { return AccessBase::_descr(Cmd,reason) ; }
	} ;

	struct DependDirect : AccessBase {
		static constexpr char   Cmd[]              = "depend_direct" ;
		static constexpr bool   ReliableMaxReplySz = true            ;
		static constexpr size_t MaxReplySz         = sizeof(bool)    ;
		// accesses
		void operator>>(::string&) const ;
		// services
		bool/*ok*/ process(Record&         r        )       ;
		::string   descr  (::string const& reason={}) const { return AccessBase::_descr(Cmd,reason) ; }
	} ;

	struct Target : AccessBase {
		static constexpr char   Cmd[]              = "target" ;
		static constexpr bool   ReliableMaxReplySz = true     ;
		static constexpr size_t MaxReplySz         = 0        ;
		// accesses
		void operator>>(::string&) const ;
		// services
		::monostate process(Record&         r        )       ;
		::string    descr  (::string const& reason={}) const { return AccessBase::_descr(Cmd,reason) ; }
	} ;

	struct ChkDeps {
		static constexpr char   Cmd[]              = "check_deps"  ;
		static constexpr bool   ReliableMaxReplySz = true          ;
		static constexpr size_t MaxReplySz         = sizeof(Bool3) ;
		// accesses
		void operator>>(::string&) const ;
		// services
		Bool3    process(Record& r                )       ;
		::string descr  (::string const& reason={}) const { return cat(Cmd,reason) ; }
		// data
		Time::Delay delay = {}    ;
		bool        sync  = false ;
	} ;

	struct List {
		static constexpr char   Cmd[]              = "list" ;
		static constexpr bool   ReliableMaxReplySz = false  ;
		static constexpr size_t MaxReplySz         = 1<<16  ; // well, not many clues to choose a good fit, 64k should be ok
		// accesses
		void operator>>(::string&) const ;
		// services
		::vector_s process(Record& r                )       ;
		::string   descr  (::string const& reason={}) const ;
		// data
		Bool3        write   = Maybe ;                        // No:deps, Yes:targets, Maybe:both
		::optional_s dir     = {}    ;
		::optional_s regexpr = {}    ;
	} ;

	struct ListRootS {
		static constexpr char   Cmd[]              = "list_root"               ;
		static constexpr bool   ReliableMaxReplySz = true                      ;
		static constexpr size_t MaxReplySz         = sizeof(::string)+PATH_MAX ;
		// accesses
		void operator>>(::string&) const ;
		// services
		::string process(Record& r                )       ;
		::string descr  (::string const& reason={}) const { return cat("list root",reason," in ",dir) ; }
		// data
		::string dir = {} ;
	} ;

	struct Decode {
		static constexpr char   Cmd[]              = "decode" ;
		static constexpr bool   ReliableMaxReplySz = false    ;
		static constexpr size_t MaxReplySz         = 1<<16    ; // 64k is already a pretty comfortable size for values
		// accesses
		void operator>>(::string&) const ;
		// services
		::string process(Record& r                )       ;
		::string descr  (::string const& reason={}) const ;
		// data
		::string tab  = {} ;
		::string ctx  = {} ;
		::string code = {} ;
	} ;

	struct Encode {
		static constexpr char   Cmd[]              = "encode"                                       ;
		static constexpr bool   ReliableMaxReplySz = true                                           ;
		static constexpr size_t MaxReplySz         = sizeof(::optional_s)+Codec::CodecCrc::Base64Sz ;
		// accesses
		void operator>>(::string&) const ;
		// services
		::string process(Record& r                )       ;
		::string descr  (::string const& reason={}) const ;
		// data
		::string tab     = {} ;
		::string ctx     = {} ;
		::string val     = {} ;
		uint8_t  min_len = 0  ;
	} ;

}
