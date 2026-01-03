// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "hash.hh"

#include "rpc_job_exec.hh"

using namespace Disk ;
using namespace Hash ;

//
// AccessDigest
//

::string& operator+=( ::string& os , AccessDigest const& ad ) {                                      // START_OF_NO_COV
	First first ;
	/**/                                  os << "AccessDigest("                                    ;
	if (+ad.accesses                    ) os <<first("",",")<< ad.accesses                         ;
	if ( ad.read_dir                    ) os <<first("",",")<< "read_dir"                          ;
	if ( ad.flags!=AccessDigest().flags ) os <<first("",",")<< ad.flags                            ;
	if ( ad.write!=No                   ) os <<first("",",")<< "written"<<(ad.write==Maybe?"?":"") ;
	return                                os <<')'                                                 ;
}                                                                                                    // END_OF_NO_COV

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

::string& operator+=( ::string& os , JobExecRpcReq const& jerr ) { // START_OF_NO_COV
	/**/                    os << "JobExecRpcReq(" << jerr.proc ;
	if (+jerr.date        ) os <<','  << jerr.date              ;
	if ( jerr.sync!=No    ) os <<",S:"<< jerr.sync              ;
	/**/                    os <<','  << jerr.digest            ;
	if (+jerr.id          ) os <<','  << jerr.id                ;
	if (+jerr.files       ) os <<','  << jerr.files             ;
	if (+jerr.comment     ) os <<','  << jerr.comment           ;
	if (+jerr.comment_exts) os <<','  << jerr.comment_exts      ;
	return                  os <<')'                            ;
}                                                                  // END_OF_NO_COV

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

::string& operator+=( ::string& os , JobExecRpcReply const& jerr ) {                 // START_OF_NO_COV
	os << "JobExecRpcReply(" << jerr.proc ;
	switch (jerr.proc) {
		case JobExecProc::None       :                                     ; break ;
		case JobExecProc::ChkDeps    :
		case JobExecProc::DepDirect  : os <<','<< jerr.ok                  ; break ;
		case JobExecProc::DepVerbose : os <<','<< jerr.verbose_infos       ; break ;
		case JobExecProc::List       : os <<','<< jerr.files               ; break ;
	DF}                                                                              // NO_COV
	return os << ')' ;
}                                                                                    // END_OF_NO_COV

//
// codec
//

namespace Codec {

	::string& operator+=( ::string& os , CodecFile const& cf ) {         // START_OF_NO_COV
		/**/                os <<"CodecFile("<< cf.file <<','<< cf.ctx ;
		if (cf.is_encode()) os <<",E:"<< cf.val_crc()                  ;
		else                os <<",D:"<< cf.code   ()                  ;
		return              os <<')'                                   ;
	}                                                                    // END_OF_NO_COV

	CodecFile::CodecFile(::string const& node) {
		throw_unless( s_is_codec(node) , "not a codec node : ",node ) ;
		static_assert( Infx[InfxSz-1]=='\t' ) ;
		size_t pos1 = s_pfx_s().size()        ;
		size_t pos3 = node.rfind('\t'       ) ; SWEAR( pos1<pos3 && pos3!=Npos , node,pos1,     pos3 ) ;
		size_t pos2 = node.rfind('\t',pos3-1) ; SWEAR( pos1<pos2 && pos2<pos3  , node,pos1,pos2,pos3 ) ;
		//
		/**/     file = node.substr(pos1,pos2-(InfxSz-1)-pos1) ; if (!is_lcl(node)) file += '/' ;                                                // dir based codec
		pos2++ ; ctx  = parse_printable(node,pos2)             ; SWEAR( pos2==pos3 , node,pos1,pos2,pos3 ) ; ctx.resize(ctx.size()-(InfxSz-1)) ;
		pos3++ ;
		if      (node.ends_with(DecodeSfx)) _code_val_crc = parse_printable      (node.substr(pos3,node.size()-(sizeof(DecodeSfx)-1)-pos3)) ;    // account for terminating null
		else if (node.ends_with(EncodeSfx)) _code_val_crc = Hash::Crc::s_from_hex(node.substr(pos3,node.size()-(sizeof(EncodeSfx)-1)-pos3)) ;    // .
		else    FAIL(node) ;
	}

	// START_OF_VERSIONING
	::string CodecFile::name(bool tmp) const {
		if (is_encode()) return cat(s_file(file,tmp?CodecDir::Tmp:CodecDir::Plain),Infx,mk_printable(ctx),Infx,val_crc().hex()     ,EncodeSfx) ;
		else             return cat(s_file(file,tmp?CodecDir::Tmp:CodecDir::Plain),Infx,mk_printable(ctx),Infx,mk_printable(code()),DecodeSfx) ;
	}
	// END_OF_VERSIONING

	::string& operator+=( ::string& os , Entry const& e ) {               // START_OF_NO_COV
		return os <<"Entry("<< e.ctx <<','<< e.code <<','<< e.val <<')' ;
	}                                                                     // END_OF_NO_COV

	Entry::Entry(::string const& line) {
		size_t pos = 0 ;
		/**/                               throw_unless( line[pos]=='\t' , "bad codec line format : ",line ) ; pos++ ;
		code = parse_printable(line,pos) ; throw_unless( line[pos]=='\t' , "bad codec line format : ",line ) ; pos++ ;
		ctx  = parse_printable(line,pos) ; throw_unless( line[pos]=='\t' , "bad codec line format : ",line ) ; pos++ ;
		val  = parse_printable(line,pos) ; throw_unless( line[pos]==0    , "bad codec line format : ",line ) ;
	}

	::string Entry::line(bool with_nl_) const {
		::string res = cat('\t',mk_printable(code),'\t',mk_printable(ctx),'\t',mk_printable(val)) ;
		if (with_nl_) add_nl(res) ;
		return res ;
	}

	// while lock is held, lock_file contains the size of new_codes_file at the time of the lock
	// in case of interruption, this info is used to determine last action to be replayed

	void s_init() {
		try { unlnk_inside_s(CodecFile::s_pfx_s(CodecDir::Lock)) ; } catch (::string const&) {} // if dir does not exist, nothing to do
	}

	CodecGuardLock::CodecGuardLock( FileRef file_ , Action action ) : NfsGuardLock{ action.file_sync , {file_.at,CodecFile::s_lock_file(file_.file)} , {.err_ok=action.err_ok} } {
		if (!self) return ;                                                                                                                                                        // nothing to lock
		file = file_ ;
		//
		::string new_codes_file    = CodecFile::s_new_codes_file(file.file)                                                                    ;
		::string new_codes_sz_file = new_codes_file+"_sz"                                                                                      ;
		DiskSz   actual_sz         = FileInfo({file.at,new_codes_file   }, {                                             .nfs_guard=this} ).sz ;
		AcFd     known_sz_fd       {          {file.at,new_codes_sz_file}, {.flags=O_RDWR|O_CREAT,.mod=0666,.err_ok=true,.nfs_guard=this} }    ;
		::string known_sz_str      = known_sz_fd.read(sizeof(DiskSz))                                                                          ;
		if (+known_sz_str) {                                                                                                                      // empty means nothing to replay
			/**/                                                        SWEAR( known_sz_str.size()==sizeof(DiskSz) , file,known_sz_str.size() ) ;
			DiskSz known_sz = decode_int<DiskSz>(known_sz_str.data()) ; SWEAR( known_sz           <=actual_sz      , file,known_sz,actual_sz  ) ;
			//
			if (actual_sz>known_sz) {
				AcFd     new_codes_fd { {file.at,new_codes_file}} ; ::lseek( new_codes_fd , known_sz , SEEK_SET ) ;
				::string line         = new_codes_fd.read()  ;                                                      // no more than a single action can be on going
				if (line.back()=='\n') {                                                                            // action is valid, replay it
					line.pop_back() ; //!      encode
					Entry e      { line                                                     } ;
					File  dn     { file.at , CodecFile(false,file.file,e.ctx,e.code).name() } ;
					File  en     { file.at , CodecFile(true ,file.file,e.ctx,e.val ).name() } ;
					File  dn_tmp { file.at , dn.file+".tmp"                                 } ;                     // ensure nodes are always correct if they exist
					File  en_tmp { file.at , en.file+".tmp"                                 } ;                     // .
					AcFd( dn_tmp , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444,.nfs_guard=this} ).write( e.code ) ;  // .
					AcFd( en_tmp , {.flags=O_WRONLY|O_TRUNC|O_CREAT,.mod=0444,.nfs_guard=this} ).write( e.val  ) ;  // .
					//      src     dst
					rename( dn_tmp , dn , {.nfs_guard=this} ) ;
					rename( en_tmp , en , {.nfs_guard=this} ) ;
				} else {                                                                                            // action is invalid, forget it as no file has been created and info is incomplete
					int rc = ::ftruncate( new_codes_fd , known_sz ) ; SWEAR( rc==0 , file,rc ) ;
					actual_sz = known_sz ;
					change({file.at,new_codes_file}) ;
				}
			}
			::lseek( known_sz_fd , 0 , SEEK_SET ) ;
		} else {
			known_sz_str.resize(sizeof(DiskSz)) ;
		}
		encode_int<DiskSz>( known_sz_str.data() , actual_sz ) ; // record new_codes_file size for next locker to replay in case of crash before closing cleanly
		known_sz_fd.write(known_sz_str)                       ;
	}

	CodecGuardLock::~CodecGuardLock() {
		if (!self) return ;
		unlnk( {file.at,CodecFile::s_new_codes_file(file.file)+"_sz"} , {.abs_ok=is_dir_name(file.file)} ) ;
	}

}
