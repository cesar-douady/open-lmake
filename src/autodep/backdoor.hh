// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "record.hh"

using namespace Disk ;

enum class BackdoorErr : uint8_t {
	Ok
,	OfficialReadlinkErr // value -1 is the normal readlink error and we need to distinguish backdoor errors
,	ParseErr
,	PokeErr
} ;

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
		size_t sz   = 0     ; // required size to serialize
		T      data = {}    ; // the data if the buffer size was ok to deserialize
	} ;

	static constexpr Fd     MagicFd     { +Fd::Cwd - 100 }                ; // any improbable negative value (to avoid conflict with real fd) will do
	static constexpr char   MagicPfx[]  = PRIVATE_ADMIN_DIR_S "backdoor/" ; // any improbable prefix will do
	static constexpr size_t MagicPfxLen = sizeof(MagicPfx)-1              ; // -1 to account for terminating null

	using Func = ::function<::pair_s<ssize_t/*len*/>(Record&,::string const& args,char* buf,size_t sz)> ;

	::umap_s<Func> const& get_func_tab() ;

	template<class T> using _Reply = decltype(::declval<T>().process(::declval<Record&>())) ;

	template<class T> _Reply<T> call(T const& args) {
		::string file = cat(MagicPfx,T::Cmd,'/',mk_printable(serialize(args))) ;
		size_t   sz   = ::max( T::MaxReplySz , Expected<T>::MinSz ) + 1        ;                                       // +1 to distinguish truncation
		for( int i=0 ;; i++ ) {
			::string buf ( sz , 0 )                                                 ;
			ssize_t  cnt = ::readlinkat( MagicFd , file.c_str() , buf.data() , sz ) ;                                  // try to go through autodep to process args
			if (cnt<0) {
				throw_if( cnt<-1 , "backdoor error ",BackdoorErr(-cnt) ) ;
				Lock lock { Record::s_mutex } ;
				return ::copy(args).process(::ref(Record(New,Yes/*enabled*/))) ;                                       // no autodep available, directly process args
			}
			SWEAR( size_t(cnt)<buf.size() , cnt,buf.size() ) ;
			buf.resize(size_t(cnt)) ;
			auto reply = deserialize<Expected<_Reply<T>>>(buf) ;
			if ( reply.ok                      ) return ::move(reply.data)                                           ;
			if ( T::ReliableMaxReplySz || i>=7 ) throw cat("backdoor length error provided ",sz," needed ",reply.sz) ; // result may not be stable, so we may require more than 2 trials
			sz = ::max( 2*sz , reply.sz ) ;                                                                            // ensure logarithmic behavior, limited to 256*hint size
		}
	}

	template<class T> ::pair_s<ssize_t/*len*/> func( Record& r , ::string const& args_str , char* buf , size_t sz ) {
		::string            parsed    ;
		T                   cmd       ;
		::string            descr     = "backdoor" ;
		Expected<_Reply<T>> reply     { .ok=true } ;
		::string            reply_str ;
		try {
			try { size_t pos = 0 ; parsed     = parse_printable(args_str,pos) ; throw_unless(pos==args_str.size()) ; } catch (::string const&) { throw "parse error"s           ; }
			try {                  cmd        = deserialize<T> (parsed      ) ;                                      } catch (::string const&) { throw "deserialization error"s ; }
			/**/                   descr      = cmd.descr      (            ) ;
			/**/                   reply.data = cmd.process    (r           ) ;
			try {                  reply_str  = serialize      (reply       ) ;                                      } catch (::string const&) { throw "serialization error"s   ; }
		} catch (::string const& e) {
			Fd::Stderr.write(cat(e," while procesing ",descr,'\n')) ;
			errno = EINVAL ;
			return { descr , -+BackdoorErr::ParseErr } ;
		}
		if (reply_str.size()>=sz) {
			reply.ok  = false            ;
			reply.sz  = reply_str.size() ;
			reply_str = serialize(reply) ;
		}
		SWEAR( reply_str.size()<sz , reply_str.size(),sz ) ;
		::memcpy( buf , reply_str.data() , reply_str.size() ) ;
		return { descr , reply_str.size() } ;
	}

	struct Enable {
		friend ::string& operator+=( ::string& , Enable const& ) ;
		static constexpr char   Cmd[]              = "enable"     ;
		static constexpr bool   ReliableMaxReplySz = true         ;
		static constexpr size_t MaxReplySz         = sizeof(bool) ;
		// services
		bool/*enabled*/ process(Record& r)       ;
		::string        descr  (         ) const ;
		// data
		Bool3 enable = Maybe ; // Maybe means dont update record state
	} ;

	struct Regexpr {
		friend ::string& operator+=( ::string& , Regexpr const& ) ;
		static constexpr char   Cmd[]              = "regexpr" ;
		static constexpr bool   ReliableMaxReplySz = true      ;
		static constexpr size_t MaxReplySz         = 0         ;
		// services
		Void     process(Record& r)       ;
		::string descr  (         ) const ;
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
		::vmap_s<FileInfo> _mk_deps( Record& r , bool sync , ::vector<NodeIdx>* /*out*/ dep_idxs1=nullptr , CommentExts ces={} ) ;
		// data
	public :
		::vector_s   files         = {}    ;
		AccessDigest access_digest = {}    ;
		bool         no_follow     = false ;
	} ;

	struct Depend : AccessBase {
		friend ::string& operator+=( ::string& , Depend const& ) ;
		static constexpr char   Cmd[]              = "depend" ;
		static constexpr bool   ReliableMaxReplySz = true     ;
		static constexpr size_t MaxReplySz         = 0        ;
		// services
		Void     process(Record& r)       ;
		::string descr  (         ) const { return cat(Cmd,' ',files) ; }
	} ;

	struct DependVerbose : AccessBase {
		friend ::string& operator+=( ::string& , DependVerbose const& ) ;
		static constexpr char   Cmd[]              = "depend_verbose"                           ;
		static constexpr bool   ReliableMaxReplySz = false                                      ;
		static constexpr size_t MaxReplySz         = sizeof(SerdesSz) + 256*sizeof(VerboseInfo) ;
		// services
		::vector<VerboseInfo> process(Record& r)       ;
		::string              descr  (         ) const { return cat(Cmd,' ',files) ; }
	} ;

	struct DependDirect : AccessBase {
		friend ::string& operator+=( ::string& , DependDirect const& ) ;
		static constexpr char   Cmd[]              = "depend_direct" ;
		static constexpr bool   ReliableMaxReplySz = true            ;
		static constexpr size_t MaxReplySz         = sizeof(bool)    ;
		// services
		bool/*ok*/ process(Record& r)       ;
		::string   descr  (         ) const { return cat(Cmd,' ',files) ; }
	} ;

	struct Target : AccessBase {
		friend ::string& operator+=( ::string& , Target const& ) ;
		static constexpr char   Cmd[]              = "target" ;
		static constexpr bool   ReliableMaxReplySz = true     ;
		static constexpr size_t MaxReplySz         = 0        ;
		// services
		Void     process(Record& r)       ;
		::string descr  (         ) const { return cat(Cmd,' ',files) ; }
	} ;

	struct ChkDeps {
		friend ::string& operator+=( ::string& , ChkDeps const& ) ;
		static constexpr char   Cmd[]              = "check_deps"  ;
		static constexpr bool   ReliableMaxReplySz = true          ;
		static constexpr size_t MaxReplySz         = sizeof(Bool3) ;
		// services
		Bool3    process(Record& r)       ;
		::string descr  (         ) const { return Cmd ; }
		// data
		Time::Delay delay = {}    ;
		bool        sync  = false ;
	} ;

	struct List {
		friend ::string& operator+=( ::string& , List const& ) ;
		static constexpr char   Cmd[]              = "list" ;
		static constexpr bool   ReliableMaxReplySz = false  ;
		static constexpr size_t MaxReplySz         = 1<<16  ; // well, not many clues to choose a good fit, 64k should be ok
		// services
		::vector_s process(Record& r)       ;
		::string   descr  (         ) const ;
		// data
		Bool3        write   = Maybe ;                        // No:deps, Yes:targets, Maybe:both
		::optional_s dir     = {}    ;
		::optional_s regexpr = {}    ;
	} ;

	struct ListRootS {
		friend ::string& operator+=( ::string& , ListRootS const& ) ;
		static constexpr char   Cmd[]              = "list_root"               ;
		static constexpr bool   ReliableMaxReplySz = true                      ;
		static constexpr size_t MaxReplySz         = sizeof(::string)+PATH_MAX ;
		// services
		::string process(Record& r)       ;
		::string descr  (         ) const { return cat("list root in ",dir) ; }
		// data
		::string dir = {} ;
	} ;

	template<bool Encode> struct Codec ;
	template<bool Encode> ::string& operator+=( ::string& os , Codec<Encode> const& cd ) ;                       // START_OF_NO_COV
	template<bool Encode> struct Codec {
		friend ::string& operator+=<Encode>( ::string& , Codec const& D) ;
		static const     char   Cmd[7]             ;
		static constexpr bool   ReliableMaxReplySz = Encode ? true                                     : false ; // when replying a code, the size is guaranteed short
		static constexpr size_t MaxReplySz         = Encode ? sizeof(::optional_s)+sizeof(Hash::Crc)*2 : 1<<16 ; // 2 digit per Crc byte, 64k is already a pretty comfortable size for values
		// services
		::optional_s process(Record& r)       ;
		::string     descr  (         ) const ;
		// data
		::string file     = {}               ;
		::string ctx      = {}               ;
		::string code_val = {}               ;
		uint8_t  min_len  = 0                ;
	} ;
	template<> constexpr char Codec<false/*Encode*/>::Cmd[] = "decode" ;
	template<> constexpr char Codec<true /*Encode*/>::Cmd[] = "encode" ;
	template<bool Encode> ::string& operator+=( ::string& os , Codec<Encode> const& cd ) {                       // START_OF_NO_COV
		/**/        os << (Encode?"Encode":"Decode") <<'(' ;
		if (Encode) os <<      "encode"                    ;
		else        os <<      "decode"                    ;
		/**/        os <<','<< cd.file                     ;
		/**/        os <<','<< cd.ctx                      ;
		if (Encode) os <<','<< cd.code_val.size()          ;
		else        os <<','<< cd.code_val                 ;
		if (Encode) os <<','<< cd.min_len                  ;
		return      os <<')'                               ;
	}                                                                                                            // END_OF_NO_COV
	template<bool Encode> ::optional_s Codec<Encode>::process(Record& r) {
		Comment                      comment = Encode ? Comment::Encode : Comment::Decode                                         ;
		Record::Solve<false/*Send*/> sr      { r , ::move(file) , false/*no_follow*/ , true/*read*/ , false/*create*/ , comment } ;
		//
		if (+sr.accesses) r.report_access( sr.file_loc , { .comment=comment , .digest={.accesses=sr.accesses} , .files={{::copy(sr.real),FileInfo(sr.real)}} } , true/*force*/ ) ;
		// transport as sync to use the same fd as Encode/Decode
		JobExecRpcReq jerr { .sync=Yes   , .comment=comment , .date=New , .files={{::move(sr.real),{}},{::move(ctx),{}},{::move(code_val),{}}} } ;
		if (Encode) { jerr.proc = JobExecProc::Encode ; jerr.min_len = min_len ; }
		else          jerr.proc = JobExecProc::Decode ;
		//
		JobExecRpcReply reply = r.report_sync(::move(jerr)) ;
		if (reply.ok==Yes) return ::move(reply.txt) ;
		else               return {}                ;
	}
	template<bool Encode> ::string Codec<Encode>::descr() const {
		if (Encode) return cat("encode in file ",file," with context ",ctx," value of size ",code_val.size()) ;
		else        return cat("decode in file ",file," with context ",ctx," code "         ,code_val       ) ;
	}

}
