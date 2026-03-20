// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "rpc_job_exec.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Time ;

//
// AccessDigest
//

void AccessDigest::operator>>(::string& os) const {                                            // START_OF_NO_COV
	First first ;
	/**/                               os << "AccessDigest("                                 ;
	if (+accesses                    ) os << first("",",")<<accesses                         ;
	if ( read_dir                    ) os << first("",",")<<"read_dir"                       ;
	if ( flags!=AccessDigest().flags ) os << first("",",")<<flags                            ;
	if ( write!=No                   ) os << first("",",")<<"written"<<(write==Maybe?"?":"") ;
	/**/                               os << ')'                                             ;
}                                                                                              // END_OF_NO_COV

AccessDigest& AccessDigest::operator|=(AccessDigest const& ad) {
	if (write!=Yes) accesses     |= ad.accesses     ;
	/**/            write        |= ad.write        ;
	/**/            flags        |= ad.flags        ;
	/**/            force_is_dep |= ad.force_is_dep ;
	return self ;
}

//
// JobExecRpcReq
//

void JobExecRpcReq::operator>>(::string& os) const {  // START_OF_NO_COV
	/**/               os << "JobExecRpcReq("<<proc ;
	if (+date        ) os << ','  <<date            ;
	if ( sync!=No    ) os << ",S:"<<sync            ;
	/**/               os << ','  <<digest          ;
	if (+id          ) os << ','  <<id              ;
	if (+files       ) os << ','  <<files           ;
	if (+comment     ) os << ','  <<comment         ;
	if (+comment_exts) os << ','  <<comment_exts    ;
	/**/               os << ')'                    ;
}                                                     // END_OF_NO_COV

JobExecRpcReply JobExecRpcReq::mimic_server() && {
	if (proc==Proc::DepVerbose) {
		::vector<VerboseInfo> verbose_infos ; for( auto& f_fi : files ) verbose_infos.push_back({ .ok=Yes , .crc=Crc(f_fi.first) }) ;
		return { .proc=proc , .verbose_infos=::move(verbose_infos) } ;
	}
	return { .proc=proc , .ok=Yes } ;
}

//
// JobExecRpcReply
//

void JobExecRpcReply::operator>>(::string& os) const { // START_OF_NO_COV
	os << "JobExecRpcReply("<<proc ;
	switch (proc) {
		case JobExecProc::None       :                          ; break ;
		case JobExecProc::ChkDeps    :
		case JobExecProc::DepDirect  : os << ','<<ok            ; break ;
		case JobExecProc::DepVerbose : os << ','<<verbose_infos ; break ;
		case JobExecProc::List       : os << ','<<files         ; break ;
	DF}
	os << ')' ;
}                                                      // END_OF_NO_COV

//
// codec
//

namespace Codec {

	void creat_store( FileRef dir_s , ::string const& crc_str , ::string const& val , mode_t umask , NfsGuard* nfs_guard ) {
		SWEAR( crc_str.size()==CodecCrc::Base64Sz , dir_s,crc_str ) ;
		// START_OF_VERSIONING CODEC
		::string data = cat(dir_s.file,"store/",substr_view(crc_str,0,2),'/',substr_view(crc_str,2)) ;
		// END_OF_VERSIONING
		if (!FileInfo(data).exists()) {
			uint64_t r        = random<uint64_t>() ;
			::string tmp_data = cat(data,'-',r)    ;
			// START_OF_VERSIONING CODEC
			AcFd( {dir_s.at,tmp_data} , {.flags=O_WRONLY|O_CREAT,.mod=0444,.umask=umask} ).write( val ) ;
			// END_OF_VERSIONING
			rename( {dir_s.at,tmp_data} , {dir_s.at,data} , {.nfs_guard=nfs_guard} ) ; // ok even if created concurrently as this is content addressable
		}
	}

	void CodecFile::operator>>(::string& os) const {          // START_OF_NO_COV
		/**/             os << "CodecFile("<<file<<','<<ctx ;
		if (is_encode()) os << ",E:"<<val_crc()             ;
		else             os << ",D:"<<code   ()             ;
		/**/             os << ')'                          ;
	}                                                         // END_OF_NO_COV

	CodecFile::CodecFile( NewType , ::string const& node ) {
		// START_OF_VERSIONING CACHE JOB REPO
		SWEAR( is_lcl(node) , node ) ;
		size_t pos1 = s_pfx_s().size()            ;
		size_t pos3 = node.rfind('/'            ) ; SWEAR( pos3!=Npos && pos1<pos3                      , node,pos1,     pos3 ) ;
		size_t pos2 = node.rfind(CodecSep,pos3-1) ; SWEAR( pos2!=Npos && pos1<pos2 && node[pos2-1]=='/' , node,pos1,pos2,pos3 ) ;
		//
		file = node.substr(pos1,pos2-pos1) ; file.pop_back() ;
		pos3++/* / */ ;
		if      (node.ends_with(DecodeSfx)) { size_t sz = node.size()-DecodeSfxSz-pos3 ;                                    _code_val_crc = parse_printable<'/'>(node.substr(pos3,sz))    ; }
		else if (node.ends_with(EncodeSfx)) { size_t sz = node.size()-EncodeSfxSz-pos3 ; SWEAR(sz==CodecCrc::Base64Sz,sz) ; _code_val_crc = CodecCrc::s_from_base64(node.substr(pos3,sz)) ; }
		else                                  FAIL(node) ;
		pos2++/*CodecSep*/ ;
		ctx = parse_printable<CodecSep>( node.substr( pos2 , pos3-1/* / */-pos2 ) ) ;
		// END_OF_VERSIONING
	}

	CodecFile::CodecFile( NewType , ::string const& node , ::string const& ext_codec_dir_s ) {
		// START_OF_VERSIONING CACHE JOB REPO CODEC
		SWEAR( !is_lcl(node) , node ) ;
		size_t pos3 = node.rfind('/')        ; SWEAR( pos3!=Npos && 0<pos3              , node,pos3            ) ;
		size_t pos2 = ext_codec_dir_s.size() ; SWEAR( node.starts_with(ext_codec_dir_s) , node,ext_codec_dir_s ) ;
		throw_unless( substr_view(node,pos2).starts_with("tab/") , node,"is not a codec file" ) ;
		//
		file = node.substr(0,pos2) ;
		pos3++/* / */ ;
		if      (node.ends_with(DecodeSfx)) { size_t sz = node.size()-DecodeSfxSz-pos3 ;                                    _code_val_crc = parse_printable<'/'>(node.substr(pos3,sz))    ; }
		else if (node.ends_with(EncodeSfx)) { size_t sz = node.size()-EncodeSfxSz-pos3 ; SWEAR(sz==CodecCrc::Base64Sz,sz) ; _code_val_crc = CodecCrc::s_from_base64(node.substr(pos3,sz)) ; }
		else                                  FAIL(node) ;
		pos3 -= 1/* / */                        ;
		pos2 += 4/*tab/ */                      ;
		ctx   = node.substr( pos2 , pos3-pos2 ) ;
		// END_OF_VERSIONING
	}

	void CodecFile::chk() const {
		// START_OF_VERSIONING CODEC
		static const ::string DecodeSfxS = with_slash(DecodeSfx) ;
		static const ::string EncodeSfxS = with_slash(EncodeSfx) ;
		if (          is_abs     (ctx)                              ) throw cat("context must be a local filename"                         ," : ",ctx," (consider ",ctx.substr(1),')') ;
		if ( +ctx &&  is_dir_name(ctx)                              ) throw cat("context must not end with /"                              ," : ",ctx," (consider ",ctx,rm_slash ,')') ;
		if (         !is_lcl     (ctx)                              ) throw cat("context must be a local filename"                         ," : ",ctx                                ) ;
		if ( ctx.find(DecodeSfxS)!=Npos || ctx.ends_with(DecodeSfx) ) throw cat("context must not contain component ending with ",DecodeSfx," : ",ctx                                ) ;
		if ( ctx.find(EncodeSfxS)!=Npos || ctx.ends_with(EncodeSfx) ) throw cat("context must not contain component ending with ",EncodeSfx," : ",ctx                                ) ;
		if ( with_slash(ctx).starts_with(AdminDirS)                 ) throw cat("context must not start with ",no_slash(AdminDirS)         ," : ",ctx                                ) ;
		if (!is_canon(ctx)) {
			::string c = mk_canon(ctx) ;
			if (c==ctx) throw cat("context must be canonical : ",ctx                    ) ;
			else        throw cat("context must be canonical : ",ctx," (consider ",c,')') ;
		}
		// END_OF_VERSIONING
	}

	// START_OF_VERSIONING CACHE JOB REPO CODEC
	::string CodecFile::ctx_dir_s(bool tmp) const {
		::string res = s_dir_s(file,tmp) ;
		if (is_dir_name(file)) res << "tab/"  <<                       ctx  ;
		else                   res << CodecSep<<mk_printable<CodecSep>(ctx) ;
		/**/                   res << '/'                                   ;
		return res ;
	}
	::string CodecFile::name(bool tmp) const {
		::string res = ctx_dir_s(tmp) ;
		if (is_encode()) res << val_crc().base64()       <<EncodeSfx ;
		else             res << mk_printable<'/'>(code())<<DecodeSfx ;
		return res ;
	}
	// END_OF_VERSIONING

	void Entry::operator>>(::string& os) const {        // START_OF_NO_COV
		os << "Entry("<<ctx<<','<<code<<','<<val<<')' ;
	}                                                   // END_OF_NO_COV

	Entry::Entry(::string const& line) {
		// START_OF_VERSIONING CODEC
		size_t pos = 0 ;
		/**/                               throw_unless( line[pos]=='\t' , "bad codec line format : ",line ) ; pos++ ;
		code = parse_printable(line,pos) ; throw_unless( line[pos]=='\t' , "bad codec line format : ",line ) ; pos++ ;
		ctx  = parse_printable(line,pos) ; throw_unless( line[pos]=='\t' , "bad codec line format : ",line ) ; pos++ ;
		val  = parse_printable(line,pos) ; throw_unless( line[pos]==0    , "bad codec line format : ",line ) ;
		// END_OF_VERSIONING
	}

	::string Entry::line(bool with_nl_) const {
		// START_OF_VERSIONING CODEC
		::string res = cat('\t',mk_printable(code),'\t',mk_printable(ctx),'\t',mk_printable(val)) ;
		if (with_nl_) add_nl(res) ;
		return res ;
		// END_OF_VERSIONING
	}

	//
	// CodecLock
	//

	static ::string _lock_dir_s() { return cat(PrivateAdminDirS,"codec_lock/") ; }

	void CodecLock::s_init() {
		mk_dir_empty_s( _lock_dir_s() ) ; // assumes cwd is root
	}

	CodecLock::~CodecLock() {
		switch (_num) {
			case 0     :                                                                                                 return ;
			case _Excl : for( uint8_t n : iota(1,_NId+1) ) unlnk( { _root_fd , cat(_lock_dir_s(),_tab.hex(),'-',n) } ) ; return ;
			default :
				Pdate    now { New                                               } ;
				File     lnk { _root_fd , cat(_lock_dir_s(),_tab.hex(),'-',_num) } ;
				FileInfo fi  { lnk                                               } ;
				SWEAR_PROD( fi.date.val() >= (now-_SharedTimeout).val() , now,fi.date,_tab ) ; // if this fires, increase _SharedTimeout
				unlnk(lnk) ;
		}
	}

	void CodecLock::lock_shared(::string const& id) {                                                                 // lock one lock at random
		SWEAR( !_num , _num,_tab ) ;
		uint8_t r = (Pdate(New).val()%_NId)+1 ;                                                                       // random, 0 reserved to mean not locked
		for(;;) {
			for( uint8_t i : iota(_NId) ) {
				_num = r+i ; if (_num>_NId) _num -= _NId ;
				File lnk { _root_fd , cat(_lock_dir_s(),_tab.hex(),'-',_num) } ;
			Retry :
				try {
					sym_lnk( lnk , "shared-"+id , {.mk_dir=false} ) ;
					return ;                                                                                          // locked
				} catch (::string const&) {
					if ( read_lnk(lnk)=="excl"                                        )                break      ;   // if held exclusively, no hope for now
					if ( FileInfo(lnk).date.val() < (Pdate(New)-_SharedTimeout).val() ) { unlnk(lnk) ; goto Retry ; }
				}                                                                                                     // try another id
			}
			Delay(1).sleep_for() ;                                                                                    // if all locks are taken, server is probably holding exclusive lock, try later
		}
	}

	void CodecLock::lock_excl() {    // lock all locks
		SWEAR( !_num , _num,_tab ) ;
		bool done[_NId] = {} ;
		for( uint8_t n_done=0 ; n_done<_NId ;)
			for( uint8_t i : iota(_NId) ) {
				if (done[i]) continue ;
			Retry :
				File lnk { _root_fd , cat(_lock_dir_s(),_tab.hex(),'-',i+1) } ;
				try {
					sym_lnk( lnk , "excl" , {.mk_dir=false} ) ;
					done[i] = true ;
					n_done++ ;
				} catch (::string const&) {
					if ( FileInfo(lnk).date.val() < (Pdate(New)-_SharedTimeout).val() ) { unlnk(lnk) ; goto Retry ; }
				}
			}
		_num = _Excl ;
	}

}
