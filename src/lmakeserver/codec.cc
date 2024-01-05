// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"
#include "rpc_job.hh"
#include "time.hh"

#include "core.hh"

#include "codec.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

using namespace Engine ;

namespace Codec::Persistent {

	ValFile  val_file  ;
	CodeFile code_file ;

}

namespace Codec {

	bool writable = false ;

	QueueThread<Codec::Closure>* g_codec_queue = nullptr ;

	::umap_s<Closure::Entry> Closure::s_tab      ;

	void codec_thread_func(Closure const& cc) ;

	::ostream& operator<<( ::ostream& os , Closure const& cc ) {
		/**/                          os << "Closure(" << cc.proc                                              ;
		if (cc.proc==JobProc::Encode) os <<','<< cc.min_len()                                                  ;
		return                        os <<','<< cc.file <<','<< cc.ctx << ',' << cc.txt <<','<< cc.reqs <<')' ;
	}

	void Closure::s_init() {
		static QueueThread<Closure> s_queue{'D',Codec::codec_thread_func} ;
		g_codec_queue = &s_queue ;
		//
		Persistent::val_file .init(CodecPfx+"/vals"s ,writable) ;
		Persistent::code_file.init(CodecPfx+"/codes"s,writable) ;
	}

	void _create_node( ::string const& file , Node node , Buildable buildable , ::string const& txt ) {
		Crc  crc    = Xxh(txt).digest() ;
		bool mk_new = node->buildable!=buildable ;
		if      (mk_new        ) node->buildable = buildable ;
		else if (node->crc==crc) return ;
		switch (buildable) {
			case Buildable::Decode : if (mk_new) node->codec_val () = txt ; else node->codec_val ().assign(txt) ; break ;
			case Buildable::Encode : if (mk_new) node->codec_code() = txt ; else node->codec_code().assign(txt) ; break ;
			default : FAIL(buildable) ;
		}
		Trace trace("_create_node",node,crc,Closure::s_tab.at(file).log_date) ;
		node->crc = crc ;
	}
	void _create_pair( ::string const& file , Node decode_node , ::string const& val , Node encode_node , ::string const& code ) {
		_create_node( file , decode_node , Buildable::Decode , val  ) ;
		_create_node( file , encode_node , Buildable::Encode , code ) ;
	}

	static ::string _codec_line( ::string const& ctx , ::string const& code , ::string const& val , bool with_nl ) {
		::string res ;
		append_to_string(res,' ',mk_printable<' '>(ctx),' ',mk_printable<' '>(code),' ',mk_printable(val)) ; // format : " <ctx> <code> <val>" exactly
		if (with_nl) res += '\n' ;
		return res ;
	}

	static bool _buildable_ok( ::string const& file , Node node ) {
		switch (node->buildable) {
			case Buildable::No      :
			case Buildable::Unknown : return false                                          ;
			case Buildable::Decode  :
			case Buildable::Encode  : return node->date()==Closure::s_tab.at(file).log_date ;
			default : FAIL(node->buildable) ;
		}
	}

	void Closure::_s_canonicalize( ::string const& file , ::vector<ReqIdx> const& reqs ) {
		bool                              is_canonic = true             ;
		::map_s<map_ss>/*ctx->val->code*/ encode_tab ;
		bool                              first      = true             ;
		::string                          prev_ctx   ;
		::string                          prev_code  ;
		::vector<Node>                    nodes      ;
		::vector_s                        lines      = read_lines(file) ;
		//
		auto process_node = [&]( ::string const& ctx , ::string const& code , ::string const& val )->void {
			Node dn { mk_decode_node(file,ctx,code) , true/*no_dir*/ } ; nodes.emplace_back(dn) ;
			Node en { mk_encode_node(file,ctx,val ) , true/*no_dir*/ } ; nodes.emplace_back(en) ;
			_create_pair( file , dn , val , en , code ) ;
		} ;
		Trace trace("_s_canonicalize",file,lines.size()) ;
		//
		for( ::string const& line : lines ) {
			::string ctx  ;
			::string code ;
			::string val  ;
			size_t   pos  = 0 ;
			/**/                                               if (line[pos]!=' ') goto BadFormat ; // bad format
			tie(ctx ,pos) = parse_printable<' '>(line,pos+1) ; if (line[pos]!=' ') goto BadFormat ; // .
			tie(code,pos) = parse_printable<' '>(line,pos+1) ; if (line[pos]!=' ') goto BadFormat ; // .
			tie(val ,pos) = parse_printable     (line,pos+1) ; if (line[pos]!=0  ) goto BadFormat ; // .
			//
			is_canonic &= first || ::pair(prev_ctx,prev_code)<::pair(ctx,code) ;                    // use same order as in decode_tab below when rewriting file
			is_canonic &= line==_codec_line(ctx,code,val,false/*with_nl*/)     ;                    // in case user encoded line in a non-canonical way, such as using \x0a for \n
			//
			{	auto [it,inserted] = encode_tab[ctx].try_emplace(val,code) ;
				if (inserted        )                             goto Continue ;                   // new entry
				else                    is_canonic = false ;
				if (it->second==code) { trace("duplicate",line) ; goto Continue ; }
				::string const& prev_code = it->second          ;                                   // 2 codes for the same val, keep best one (preference to user codes, then shorter)
				::string        crc       = ::string(Xxh(val).digest()) ;
				trace("val_conflict",prev_code,code) ;
				if (                                                                                // keep best : user is better than auto then shorter is better than longer
					pair( code     ==crc.substr(0,code     .size()) , code     .size() )
				<	pair( prev_code==crc.substr(0,prev_code.size()) , prev_code.size() )
				) it->second = code ;
				trace("keep",it->second) ;
				goto Continue ;
			}
		BadFormat :
			is_canonic = false ;
			trace("bad_format",line) ;
		Continue :
			prev_ctx  = ::move(ctx ) ;
			prev_code = ::move(code) ;
			first       = false      ;
		}
		trace(STR(is_canonic)) ;
		//
		if (!is_canonic) {                                                                // if already canonic, nothing to do
			// disambiguate in case the same code is used for the several values
			OFStream                          os         { file } ;
			::map_s<map_ss>/*ctx->code->val*/ decode_tab ;
			//
			for( auto const& [ctx,e_entry] : encode_tab ) {
				::uset_s codes   = mk_key_uset(e_entry) ;
				::map_ss d_entry = decode_tab[ctx]      ;
				for( auto const& [val,code] : e_entry ) {
					if (d_entry.try_emplace(code,val).second) continue ;                  // 2 vals for the same code, need to disambiguate
					::string crc      = ::string(Xxh(val).digest()) ;
					::string new_code ;
					if (code==crc.substr(0,code.size())) {
						for( uint8_t i=code.size() ; i<=crc.size() ; i++ ) {
							new_code = crc.substr(0,i) ;
							if (!codes.contains(new_code)) goto NewCode ;
						}
						FAIL("codec crc clash for code",crc) ;
					} else {
						uint8_t d ; for ( d=code.size() ; d>=1 && '0'<=code[d-1] && code[d-1]<='9' ; d-- ) ;
						for( size_t inc=1 ; inc<codes.size() ; inc++ ) {
							new_code = to_string( code.substr(0,d) , from_chars<size_t>(code.substr(d),true/*empty_ok*/)+inc ) ;
							if (!codes.contains(new_code)) goto NewCode ;
						}
						FAIL("cannot find new code from",code) ;
					}
				NewCode :
					trace("code_conflict",code,"new",new_code,d_entry.at(code),val) ;
					d_entry.try_emplace(new_code,val) ;
					codes.insert(new_code) ;
				}
				for( auto const& [code,val] : d_entry ) {
					os << _codec_line(ctx,code,val,true/*with_nl*/) ;
					process_node(ctx,code,val) ;
				}
			}
			for( ReqIdx r : reqs ) Req(r)->audit_node(Color::Note,"refresh",Node(file)) ;
		} else {                                                                          // file needs no update, but we must record file content into nodes
			for( auto const& [ctx,e_entry] : encode_tab )
				for( auto const& [val,code] : e_entry ) process_node(ctx,code,val) ;
		}
		// wrap up
		Ddate log_date = s_tab.at(file).log_date ;
		for( Node n : nodes ) n->date() = log_date ;
		trace("done",nodes.size()/2) ;
	}

	bool/*ok*/ Closure::s_refresh( ::string const& file , NodeIdx ni , ::vector<ReqIdx> const& reqs ) {
		auto   [it,inserted] = s_tab.try_emplace(file,Entry()) ;
		Entry& entry         = it->second                      ;
		if (!inserted) {
			for( ReqIdx r : reqs ) if ( entry.sample_date < Req(r)->start_pdate ) goto Refresh ;                    // we sample disk once per Req
			return true/*ok*/ ;
		}
	Refresh :
		Trace trace("refresh",file,reqs) ;
		//
		Node file_node{file} ;
		file_node->set_buildable() ;
		if (!file_node->is_src()) {
			for( ReqIdx r : reqs ) {
				Req(r)->audit_node(Color::Err ,"encode/decode association file must be a source :",file_node  ) ;
				Req(r)->audit_node(Color::Note,"consider : git add"                               ,file_node,1) ;
			}
			return false/*ok*/ ;
		}
		//
		Ddate phys_date = file_date(file) ;
		entry.sample_date = Pdate::s_now() ;
		if (inserted) {
			Node node{ni} ;
			if ( inserted && node->buildable==Buildable::Decode ) entry.phys_date = entry.log_date = node->date() ; // initialize from known info
		}
		if ( phys_date==entry.phys_date                         ) return true/*ok*/ ;                               // file has not changed, nothing to do
		entry.log_date = phys_date ;
		//
		_s_canonicalize(file,reqs) ;
		return true/*ok*/ ;
	}

	JobRpcReply Closure::decode() const {
		Trace trace("decode",*this) ;
		SWEAR(proc==JobProc::Decode,proc) ;
		Node decode_node { mk_decode_node(file,ctx,txt) , true/*no_dir*/ } ;
		bool refreshed = s_refresh( file , +decode_node , reqs ) ;
		if (refreshed) {                                        // else codec file not available
			if (_buildable_ok(file,decode_node)) {
				::string val { decode_node->codec_val().str_view() } ;
				trace("found",val) ;
				return JobRpcReply( JobProc::Decode , val , decode_node->crc , Yes ) ;
			}
		}
		trace("fail",STR(refreshed)) ;
		return JobRpcReply(JobProc::Decode,{}/*val*/,Crc::None,No) ;
	}

	JobRpcReply Closure::encode() const {
		Trace trace("encode",*this) ;
		SWEAR(proc==JobProc::Encode,proc) ;
		Node encode_node { mk_encode_node(file,ctx,txt) , true/*no_dir*/ } ;
		if ( !s_refresh( file , +encode_node , reqs ) ) {
			trace("no_refresh") ;
			return JobRpcReply(JobProc::Encode,{}/*code*/,Crc::None,No) ; // codec file not available
		}
		//
		if (_buildable_ok(file,encode_node)) {
			::string code { encode_node->codec_code().str_view() } ;
			trace("found",code) ;
			return JobRpcReply( JobProc::Encode , code , encode_node->crc , Yes ) ;
		}
		//
		::string full_code   = ::string(Xxh(txt).digest())   ;
		::string code        = full_code.substr(0,min_len()) ;
		Node     decode_node ;
		for(; code.size()<=full_code.size() ; code=full_code.substr(0,code.size()+1) ) {
			decode_node = { mk_decode_node(file,ctx,code) , true/*no_dir*/ } ;
			if (!_buildable_ok(file,decode_node)) goto NewCode ;
		}
		trace("clash") ;
		return JobRpcReply(JobProc::Encode,"crc clash",{},No) ;           // this is a true full crc clash
	NewCode :
		trace("new_code",code) ;
		{	OFStream os { file , ::ios_base::app } ;
			os << _codec_line(ctx,code,txt,true/*with_nl*/) ;
		}
		Entry& entry = s_tab.at(file) ;
		_create_pair( file , decode_node , txt , encode_node , code ) ;
		decode_node->date() = entry.log_date  ;
		encode_node->date() = entry.log_date  ;
		entry.phys_date     = file_date(file) ;                           // we have touched the file but not the semantic, update phys_date but not log_date
		//
		trace("found",code) ;
		return JobRpcReply( JobProc::Encode , code , encode_node->crc , Yes ) ;
	}

	bool/*ok*/ refresh( NodeIdx ni , ReqIdx r ) {
		Node     node { ni }                  ; SWEAR( node->is_decode() || node->is_encode() ) ;
		::string file = mk_file(node->name()) ;
		if ( !Closure::s_refresh( file , ni , {r} ) ) {
			node->refresh( Crc::None , Closure::s_tab.at(file).log_date ) ;
			return false/*ok*/ ;
		}
		return node->date()!=Closure::s_tab.at(file).log_date ;
	}

	void codec_thread_func(Closure const& cc) {
		switch (cc.proc) {
			case JobProc::Decode : OMsgBuf().send( cc.reply_fd , cc.decode() ) ; break ;
			case JobProc::Encode : OMsgBuf().send( cc.reply_fd , cc.encode() ) ; break ;
			default : FAIL(cc.proc) ;
		}
		::close(cc.reply_fd) ;
	}

}
