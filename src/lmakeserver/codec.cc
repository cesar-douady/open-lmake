// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include "rpc_job.hh"
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

	StaticUniqPtr<QueueThread<Codec::Closure>> g_codec_queue ;

	::umap_s<Closure::Entry> Closure::s_tab ;

	void codec_thread_func(Closure const& cc) ;

	::string& operator+=( ::string& os , Closure const& cc ) {                                       // START_OF_NO_COV
		/**/                              os << "Closure(" << cc.proc                              ;
		if (cc.proc==JobMngtProc::Encode) os <<','<< cc.min_len()                                  ;
		return                            os <<','<< cc.file <<','<< cc.ctx << ',' << cc.txt <<')' ;
	}                                                                                                // END_OF_NO_COV

	void Closure::s_init() {
		g_codec_queue = new QueueThread<Closure>{'D',Codec::codec_thread_func} ;
		// START_OF_VERSIONING
		Persistent::val_file .init( cat(g_config->local_admin_dir_s,"codec/vals" ) , g_writable ) ;
		Persistent::code_file.init( cat(g_config->local_admin_dir_s,"codec/codes") , g_writable ) ;
		// END_OF_VERSIONING
	}

	void _create_node( ::string const& file , Node node , Buildable buildable , ::string const& txt ) {
		Crc  crc    = Crc(New,txt)               ;
		bool mk_new = node->buildable!=buildable ;
		if      (mk_new        ) node->buildable = buildable ;
		else if (node->crc==crc) return ;
		switch (buildable) {
			case Buildable::Decode : if (mk_new) node->codec_val () = txt ; else node->codec_val ().assign(txt) ; break ;
			case Buildable::Encode : if (mk_new) node->codec_code() = txt ; else node->codec_code().assign(txt) ; break ;
		DF}                                                                                                               // NO_COV
		Trace trace("_create_node",node,crc,Closure::s_tab.at(file).log_date) ;
		node->crc = crc ;
	}
	void _create_pair( ::string const& file , Node decode_node , ::string const& val , Node encode_node , ::string const& code ) {
		_create_node( file , decode_node , Buildable::Decode , val  ) ;
		_create_node( file , encode_node , Buildable::Encode , code ) ;
	}

	static ::string _codec_line( ::string const& ctx , ::string const& code , ::string const& val , bool with_nl ) {
		::string res = ' '+mk_printable<' '>(ctx)+' '+mk_printable<' '>(code)+' '+mk_printable(val) ;                // format : " <ctx> <code> <val>" exactly
		if (with_nl) res += '\n' ;
		return res ;
	}

	static void _create_entry( ::string const& file , ::string const& ctx , Node decode_node , ::string const& val ,  Node encode_node , ::string const& code ) {
		AcFd(file,{.flags=O_WRONLY|O_APPEND}).write(_codec_line(ctx,code,val,true/*with_nl*/)) ;                                                                  // Maybe means append
		//
		Closure::Entry& entry = Closure::s_tab.at(file) ;
		_create_pair( file , decode_node , val , encode_node , code ) ;
		decode_node->log_date() = entry.log_date  ;
		encode_node->log_date() = entry.log_date  ;
		entry.phy_date          = file_date(file) ; // we have touched the file but not the semantic, update phy_date but not log_date
	}

	static bool _buildable_ok( ::string const& file , Node node ) {
		if (!node) return false ;
		switch (node->buildable) {
			case Buildable::No      :
			case Buildable::Unknown : return false                                              ;
			case Buildable::Decode  :
			case Buildable::Encode  : return node->log_date()==Closure::s_tab.at(file).log_date ;
		DF}                                                                                       // NO_COV
	}

	static FileNameIdx _code_prio( ::string const& code , ::string const& crc ) {
		static_assert( 3*PATH_MAX<= Max<FileNameIdx> ) ;                           // ensure highest possible value fits in range
		SWEAR( code.size()<=PATH_MAX , code ) ;
		char    last = code.back() ;
		uint8_t lvl  = 3           ;
		if ( +code && crc.starts_with(substr_view(code,0,code.size()-1)) ) {
			if      ( last==code.back()                                ) lvl = 2 ; // an automatic code, not as good as a user provided one
			else if ( ((last>='0'&&last<='9')||(last>='a'&&last<='f')) ) lvl = 1 ; // an automatic replacement code in case of clash, the worst
		}
		return PATH_MAX*lvl-code.size() ;                                          // prefer shorter codes
	}

	static ::string _mk_new_code( ::string const& code , ::string const& val , ::map_ss const& codes ) {
		::string crc = Crc(val).hex()                ;
		uint8_t  d   = ::min(code.size(),crc.size()) ; while (!code.ends_with(substr_view(crc,0,d))) d-- ;
		::string res = code                          ; res.reserve(code.size()+1) ;                        // most of the time, adding a single char is enough
		for( char c : substr_view(crc,d) ) {
			res.push_back(c) ;
			if (!codes.contains(res)) return res ;
		}
		FAIL("codec checksum clash for code",code,crc,val) ;                                               // NO_COV
	}

	void Closure::_s_canonicalize( ::string const& file , ::vector<ReqIdx> const& reqs ) {
		::map_s<umap_ss>/*ctx->val->code*/ encode_tab ;
		::string                           prev_ctx   ;
		::string                           prev_code  ;
		::vector<Node>                     nodes      ;
		bool                               is_canonic = true                                   ;
		::vector_s                         lines      = AcFd(file,true/*err_ok*/).read_lines() ;
		//
		auto process_node = [&]( ::string const& ctx , ::string const& code , ::string const& val ) {
			//                                             no_dir
			Node dn { New , mk_decode_node(file,ctx,code) , true } ; nodes.emplace_back(dn) ;
			Node en { New , mk_encode_node(file,ctx,val ) , true } ; nodes.emplace_back(en) ;
			_create_pair( file , dn , val , en , code ) ;
		} ;
		Trace trace("_s_canonicalize",file,lines.size()) ;
		//
		bool first = true ;
		for( ::string const& line : lines ) {
			size_t pos = 0 ;
			// /!\ format must stay in sync with Record::report_sync_direct
			/**/                                             if (line[pos++]!=' ') { trace("no_space_0") ; is_canonic=false ; continue ; }
			::string ctx  = parse_printable<' '>(line,pos) ; if (line[pos++]!=' ') { trace("no_space_1") ; is_canonic=false ; continue ; }
			::string code = parse_printable<' '>(line,pos) ; if (line[pos++]!=' ') { trace("no_space_2") ; is_canonic=false ; continue ; }
			::string val  = parse_printable     (line,pos) ; if (line[pos  ]!=0  ) { trace("no_end"    ) ; is_canonic=false ; continue ; }
			//
			if (is_canonic) {
				// use same order as in decode_tab below when rewriting file and ensure standard line formatting
				if      ( !first && ::pair(prev_ctx,prev_code)>=::pair(ctx,code)          ) { trace("wrong_order",prev_ctx,prev_code,ctx,code) ; is_canonic=false ; }
				else if ( ::string l=_codec_line(ctx,code,val,false/*with_nl*/) ; line!=l ) { trace("fancy_line" ,'"'+line+'"',"!=",'"'+l+'"') ; is_canonic=false ; }
			}
			//
			auto [it,inserted] = encode_tab[ctx].try_emplace(val,code) ;
			if (inserted) {
				prev_ctx  = ::move(ctx ) ;
				prev_code = ::move(code) ;
				first     = false        ;
			} else {
				is_canonic = false ;
				if (it->second==code) {
					trace("duplicate",line) ;
				} else {
					::string crc = Crc(New,val).hex() ;
					if (_code_prio(code,crc)>_code_prio(it->second,crc)) it->second = code ; // keep best code
					trace("val_conflict",prev_code,code,it->second) ;
				}
			}
		}
		trace(STR(is_canonic)) ;
		//
		if (!is_canonic) {                                                                   // if already canonic, nothing to do, there may not be any code conflict as they are strictly increasing
			// disambiguate in case the same code is used for the several vals
			::map_s<map_ss>/*ctx->code->val*/ decode_tab ;
			bool                              has_clash  = false ;
			for( auto const& [ctx,e_entry] : encode_tab ) {
				::map_ss& d_entry = decode_tab[ctx] ;
				for( auto const& [val,code] : e_entry ) has_clash |= d_entry.try_emplace(code,val).second ;
			}
			if (has_clash) {
				for( auto const& [ctx,e_entry] : encode_tab ) {
					::map_ss& d_entry = decode_tab.at(ctx) ;
					for( auto const& [val,code] : e_entry ) if (d_entry.at(code)!=val) {
						bool inserted = d_entry.try_emplace(_mk_new_code(code,val,d_entry),val).second ;
						SWEAR(inserted) ;                                                                // purpose of new_code is to be unique
					}
				}
			}
			::string lines ;
			for( auto const& [ctx,d_entry] : decode_tab )
				for( auto const& [code,val] : d_entry )
					lines << _codec_line(ctx,code,val,true/*with_nl*/) ;
			AcFd(file,{.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0666}).write(lines) ;
			for( ReqIdx r : reqs ) Req(r)->audit_info( Color::Note , "reformat" , file ) ;
		}
		for( auto const& [ctx,e_entry] : encode_tab )
			for( auto const& [val,code] : e_entry )
				process_node(ctx,code,val) ;
		// wrap up
		Ddate log_date = s_tab.at(file).log_date ;
		for( Node n : nodes ) n->log_date() = log_date ;
		trace("done",nodes.size()/2) ;
	}

	bool/*ok*/ Closure::s_refresh( ::string const& file , NodeIdx ni , ::vector<ReqIdx> const& reqs ) {
		auto   [it,inserted] = s_tab.try_emplace(file,Entry()) ;
		Entry& entry         = it->second                      ;
		//
		if ( !inserted && ::none_of( reqs , [&](ReqIdx ri) { return entry.sample_date<Req(ri)->start_pdate ; } ) ) return true/*ok*/ ; // we sample disk once per Req
		Trace trace("refresh",file,reqs) ;
		//
		Node file_node { file } ;
		if ( !file_node || (file_node->set_buildable(),file_node->buildable!=Buildable::Src) ) {
			for( ReqIdx r : reqs ) {
				Req(r)->audit_node(Color::Err ,"encode/decode association file must be a plain source :",file_node  ) ;
				Req(r)->audit_node(Color::Note,"consider : git add"                                     ,file_node,1) ;
			}
			return false/*ok*/ ;
		}
		//
		Ddate phy_date = file_date(file) ;
		entry.sample_date = New ;
		if ( inserted && ni ) {
			if ( Node node{ni} ; node->buildable==Buildable::Decode )
				entry.phy_date = entry.log_date  = node->log_date() ;                                                                  // initialize from known info
		}
		if (phy_date==entry.phy_date) return true/*ok*/ ;                                                                              // file has not changed, nothing to do
		entry.log_date = phy_date ;
		//
		_s_canonicalize( file , reqs ) ;
		return true/*ok*/ ;
	}

	JobMngtRpcReply Closure::decode() const {
		Trace trace("decode",self) ;
		SWEAR( proc==JobMngtProc::Decode , proc ) ;
		Node             decode_node { New , mk_decode_node(file,ctx,txt) , true/*no_dir*/ } ;
		::vector<ReqIdx> reqs        ;                                                       ; for( Req r : Job(job)->running_reqs() ) reqs.push_back(+r) ;
		bool             refreshed   = s_refresh( file , +decode_node , reqs )               ;
		if (refreshed) {                                                                            // else codec file not available
			if (_buildable_ok(file,decode_node)) {
				::string val { decode_node->codec_val().str_view() } ;
				trace("found",val) ;
				return { .proc=JobMngtProc::Decode , .txt=val , .crc=decode_node->crc , .ok=Yes } ; // seq_id and fd will be filled in later
			}
		}
		trace("fail",STR(refreshed)) ;
		return { .proc=JobMngtProc::Decode , .crc=Crc::None , .ok=No } ;                            // seq_id and fd will be filled in later
	}

	JobMngtRpcReply Closure::encode() const {
		Trace trace("encode",self) ;
		SWEAR( proc==JobMngtProc::Encode , proc ) ;
		Node             encode_node { New , mk_encode_node(file,ctx,txt) , true/*no_dir*/ } ;
		::vector<ReqIdx> reqs        ;                                                         for( Req r : Job(job)->running_reqs() ) reqs.push_back(+r) ;
		//
		if (!s_refresh( file , +encode_node , reqs )) {
			trace("no_refresh") ;
			return { .proc=JobMngtProc::Encode , .crc=Crc::None , .ok=No } ;                     // codec file not available, seq_id and fd will be filled in later
		}
		//
		if (_buildable_ok(file,encode_node)) {
			::string code { encode_node->codec_code().str_view() } ;
			trace("found",code) ;
			return { .proc=JobMngtProc::Encode , .txt=code , .crc=encode_node->crc , .ok=Yes } ; // seq_id and fd will be filled in later
		}
		//
		::string crc         = Crc(New,txt).hex()      ;
		::string code        = crc.substr(0,min_len()) ;
		Node     decode_node ;
		for(; code.size()<=crc.size() ; code.push_back(crc[code.size()]) ) {
			decode_node = { New , mk_decode_node(file,ctx,code) , true/*no_dir*/ } ;
			if (!_buildable_ok(file,decode_node)) {
				trace("new_code",code) ;
				_create_entry( file , ctx , decode_node , txt , encode_node , code ) ;
				return { .proc=JobMngtProc::Encode , .txt=code , .crc=encode_node->crc , .ok=Yes } ;
			}
		}
		trace("clash") ;
		return { .proc=JobMngtProc::Encode , .txt="checksum clash" , .ok=No } ;                  // this is a true full crc clash, seq_id and fd will be filled in later
	}

	bool/*ok*/ refresh( NodeIdx ni , ReqIdx r ) {
		Node     node { ni }                          ; SWEAR( node->is_decode() || node->is_encode() ) ;
		::string file = Codec::get_file(node->name()) ;                                                   // extract codec file
		if ( !Closure::s_refresh( file , ni , {r} ) ) {
			node->refresh(Crc::None) ;
			return false/*ok*/ ;
		}
		return node->crc!=Crc::None && node->log_date()!=Closure::s_tab.at(file).log_date ;
	}

	Bool3/*ok*/ _mk_codec_entries( CodecMap const& map , ReqIdx r , bool create ) {
		bool must_create = false ;
		//
		for( auto const& [file,file_entry] : map ) {
			if (!Closure::s_refresh(file,0/*node*/,{r})) goto Bad ;
			for( auto const& [ctx,ctx_entry] : file_entry )
				for( auto const& [code,val] : ctx_entry ) {
					Trace trace("mk_codec_entries",file,ctx,code,val) ;
					//
					//
					::string decode_name = mk_decode_node(file,ctx,code) ;
					::string encode_name = mk_encode_node(file,ctx,val ) ;
					//                                              no_dir
					Node decode_node = create ? Node(New,decode_name,true) : Node(decode_name) ;
					Node encode_node = create ? Node(New,encode_name,true) : Node(encode_name) ;
					if (_buildable_ok(file,encode_node)) {
						::string_view found_code = encode_node->codec_code().str_view() ; //! ok
						if (code==found_code) { trace("found"                      ) ; continue ; }
						else                  { trace("bad_code_for_val",found_code) ; goto Bad ; } // when create, we should have verified it's possible
					}
					if (_buildable_ok(file,decode_node)) {
						::string_view found_val = decode_node->codec_val().str_view() ;
						SWEAR( val!=found_val ) ;                                                   // else we would have found encode_node
						trace("bad_val_for_code",found_val) ;
						goto Bad ;
					}
					trace("new_entry") ;
					must_create = true ;
					if (create) _create_entry( file , ctx , decode_node , val , encode_node , code ) ;
				}
		}
		return must_create ? Maybe : Yes ;
		//
	Bad :
		SWEAR( !create , map ) ;                                                                    // when create, we should have verified it's possible
		return No/*ok*/ ;
	}

	void codec_thread_func(Closure const& cc) {
		JobMngtRpcReply jmrr ;
		switch (cc.proc) {
			case JobMngtProc::Decode : jmrr = cc.decode() ; break ;
			case JobMngtProc::Encode : jmrr = cc.encode() ; break ;
		DF}                                                         // NO_COV
		jmrr.fd     = cc.fd     ;
		jmrr.seq_id = cc.seq_id ;
		Backends::send_reply( cc.job , ::move(jmrr) ) ;
	}

}
