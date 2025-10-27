// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "disk.hh"
#include "re.hh"
#include "time.hh"

#include "backdoor.hh"
#include "job_support.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Re   ;
using namespace Time ;

using Proc = JobExecProc ;

namespace JobSupport {

	static void _chk_files(::vector_s const& files) {
		for( ::string const& f : files ) throw_unless( f.size()<=PATH_MAX , "file name too long (",f.size()," characters)" ) ;
	}

	::pair<::vector<VerboseInfo>,bool/*ok*/> depend( ::vector_s&& files , AccessDigest ad , bool no_follow , bool regexpr , bool direct , bool verbose ) {
		bool readdir_ok = ad.flags.extra_dflags[ExtraDflag::ReaddirOk] ;
		throw_if( regexpr + verbose + direct > 1 , "regexpr, verbose and direct are mutually exclusive" ) ;
		if (regexpr) {
			SWEAR(ad.write==No) ;
			throw_if( !no_follow   , "regexpr and follow_symlinks are exclusive" ) ;
			throw_if( +ad.accesses , "regexpr and read are exclusive"            ) ;
			ad.flags.extra_dflags &= ~ExtraDflag::NoStar ;                              // it is meaningless to exclude regexpr when we are a regexpr
		}
		if (readdir_ok) {
			ad.flags.dflags &= ~Dflag::Required ;                                       // ReaddirOk means dep is expected to be a dir, it is non-sens to require it to be buidlable
			ad.read_dir     |= +ad.accesses     ;                                       // if reading and allow dir access, assume user meant reading a dir
		}
		if (verbose) {
			if (+(ad.accesses&FullAccesses)          ) ad.force_is_dep  = true        ; // we access the content of the file even if file has been written to
			if (  ad.flags.dflags[Dflag::IgnoreError]) ad.accesses     |= Access::Err ; // if errors are not ignored, reporting them is meaningless as deps are necessarily ok
		}
		_chk_files(files) ;
		//
		if (regexpr) {               Backdoor::call<Backdoor::Regexpr      >( { .files=files , .access_digest=ad                        } )                ; return {{},true/*ok*/} ; }
		if (verbose)   return {      Backdoor::call<Backdoor::DependVerbose>({{ .files=files , .access_digest=ad , .no_follow=no_follow }}) , true/*ok*/ } ;
		if (direct )   return { {} , Backdoor::call<Backdoor::DependDirect >({{ .files=files , .access_digest=ad , .no_follow=no_follow }})              } ;
		/**/                         Backdoor::call<Backdoor::Depend       >({{ .files=files , .access_digest=ad , .no_follow=no_follow }})                ; return {{},true/*ok*/} ;
	}

	void target( ::vector_s&& files , AccessDigest ad , bool no_follow , bool regexpr ) {
		SWEAR(ad.write!=Maybe) ;                                                          // useless, but if necessary, implement a confirmation mecanism
		if (regexpr) {
			throw_unless( ad.write==No , "regexpr and write are exclusive" ) ;
			ad.flags.extra_dflags &= ~ExtraDflag::NoStar ;                                // it is meaningless to exclude regexpr when we are a regexpr
		}
		_chk_files(files) ;
		if (regexpr) Backdoor::call<Backdoor::Regexpr>( { .files=files , .access_digest=ad                        } ) ;
		else         Backdoor::call<Backdoor::Target >({{ .files=files , .access_digest=ad , .no_follow=no_follow }}) ;
	}

	Bool3 chk_deps( Delay delay , bool sync ) {
		return Backdoor::call<Backdoor::ChkDeps>({ .delay=delay , .sync=sync }) ;
	}

	::vector_s list( Bool3 write , ::optional_s&& dir , ::optional_s&& regexpr ) {
		return Backdoor::call<Backdoor::List>({ .write=write , .dir=::move(dir) , .regexpr=::move(regexpr) }) ;
	}

	::string list_root_s(::string&& dir) {
		return Backdoor::call<Backdoor::ListRootS>({.dir=::move(dir)}) ;
	}

	::string decode( ::string&& file , ::string&& ctx , ::string&& code ) {
		return Backdoor::call<Backdoor::Decode>({ .file=::move(file) , .ctx=::move(ctx) , .code=::move(code) }) ;
	}
	::string encode( ::string&& file , ::string&& ctx , ::string&& val  , uint8_t min_len ) {
		throw_unless( min_len>=1             , "min_len (",min_len,") must be at least 1"                                  ) ;
		throw_unless( min_len<=sizeof(Crc)*2 , "min_len (",min_len,") must be at most checksum length (",sizeof(Crc)*2,')' ) ;     // codes are output in hex, 4 bits/digit
		return Backdoor::call<Backdoor::Encode>({ .file=::move(file) , .ctx=::move(ctx) , .val=::move(val) , .min_len=min_len }) ;
	}

}
