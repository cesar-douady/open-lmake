// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be first because Python.h must be first

#include "lib.hh"
#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "backdoor.hh"
#include "env.hh"
#include "job_support.hh"
#include "record.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Py   ;
using namespace Time ;

using Proc = JobExecProc ;

static Record _g_record ;

static ::string _mk_str( Object const* o , ::string const& arg_name={} ) {
	throw_unless( o , "missing argument",(+arg_name?" ":""),arg_name ) ;
	return *o->str() ;
}

static uint8_t _mk_uint8( Object const* o , uint8_t dflt , ::string const& arg_name={} ) {
	if (!o) return dflt ;
	try                       { return o->as_a<Int>() ;                                            }
	catch (::string const& e) { throw "bad type/value for argument"s+(+arg_name?" ":"")+arg_name ; }
}

static ::vector_s _get_files(Tuple const& py_args) {
	::vector_s res  ;
	//
	auto push = [&](Object const& o)->void {
		if (+o) res.push_back(_mk_str(&o)) ;
	} ;
	//
	if (py_args.size()==1) {
		Object const& py_arg0 = py_args[0] ;
		if (py_arg0.is_a<Sequence>()) { Sequence const& py_seq0 = py_arg0.as_a<Sequence>() ; res.reserve(py_seq0.size()) ; for( Object const& py : py_seq0 ) push(py     ) ; }
		else                          {                                                      res.reserve(1             ) ;                                   push(py_arg0) ; }
	} else {
		/**/                                                                                 res.reserve(py_args.size()) ; for( Object const& py : py_args ) push(py     ) ;
	}
	for( size_t i : iota(res.size()) ) throw_unless( +res[i] , "argument ",i+1," is empty" ) ;
	return res ;
}

static Object const* _gather_arg( Tuple const& py_args , size_t idx , Dict const* kwds , const char* kw , size_t& n_kwds ) {
	if (idx<py_args.size() ) return &py_args[idx] ;
	if (!kwds              ) return nullptr       ;
	if (!kwds->contains(kw)) return nullptr       ;
	n_kwds-- ;
	return &(*kwds)[kw] ;
}

static PyObject* depend( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args   = *from_py<Tuple const>(args) ;
	Dict  const* py_kwds   =  from_py<Dict  const>(kwds) ;
	bool         no_follow = true                        ;
	bool         verbose   = false                       ;
	bool         read      = true                        ;
	AccessDigest ad        { .dflags=Dflag::Required }   ;
	if (py_kwds) {
		size_t n = py_kwds->size() ;
		/**/                                          if ( const char* s="follow_symlinks" ;                                 py_kwds->contains(s) ) { n-- ; no_follow =             !(*py_kwds)[s]  ; }
		/**/                                          if ( const char* s="verbose"         ;                                 py_kwds->contains(s) ) { n-- ; verbose   =             +(*py_kwds)[s]  ; }
		/**/                                          if ( const char* s="read"            ;                                 py_kwds->contains(s) ) { n-- ; read      =             +(*py_kwds)[s]  ; }
		for( Dflag      df  : iota(Dflag::NDyn    ) ) if ( ::string    s=snake_str(df )    ;                                 py_kwds->contains(s) ) { n-- ; ad.dflags      .set(df ,+(*py_kwds)[s]) ; }
		for( ExtraDflag edf : iota(All<ExtraDflag>) ) if ( ::string    s=snake_str(edf)    ; ExtraDflagChars[+edf].second && py_kwds->contains(s) ) { n-- ; ad.extra_dflags.set(edf,+(*py_kwds)[s]) ; }
		//
		if (n) return py_err_set(Exception::TypeErr,"unexpected keyword arg") ;
	}
	if (read) ad.accesses = ~Accesses() ;
	::vector_s files ;
	try                       { files = _get_files(py_args) ;             }
	catch (::string const& e) { return py_err_set(Exception::TypeErr,e) ; }
	//
	::vector<pair<Bool3/*ok*/,Crc>> dep_infos ;
	try                       { dep_infos = JobSupport::depend( _g_record , ::copy(files) , ad , no_follow , verbose ) ; }
	catch (::string const& e) { return py_err_set(Exception::ValueErr,e) ;                                               }
	//
	if (!verbose) return None.to_py_boost() ;
	//
	Ptr<Dict> res { New } ;
	//
	SWEAR( dep_infos.size()==files.size() , dep_infos.size() , files.size() ) ;
	for( size_t i : iota(dep_infos.size()) ) {
		Object* py_ok = nullptr/*garbage*/ ;
		switch (dep_infos[i].first) {
			case Yes   : py_ok = &True  ; break ;
			case Maybe : py_ok = &None  ; break ;
			case No    : py_ok = &False ; break ;
		}
		res->set_item( files[i] , *Ptr<Tuple>( *py_ok , *Ptr<Str>(::string(dep_infos[i].second)) ) ) ;
	}
	return res->to_py_boost() ;
}

static PyObject* target( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args)                    ;
	Dict  const* py_kwds =  from_py<Dict  const>(kwds)                    ;
	AccessDigest ad      { .write=Yes , .extra_tflags=ExtraTflag::Allow } ;
	if (py_kwds) {
		size_t n = py_kwds->size() ;
		/**/                                          if ( const char* s="write"        ;                                 py_kwds->contains(s) ) { n-- ; ad.write  = No |        +(*py_kwds)[s]  ; }
		for( Tflag      tf  : iota(Tflag::NDyn    ) ) if ( ::string    s=snake_str(tf ) ;                                 py_kwds->contains(s) ) { n-- ; ad.tflags      .set(tf ,+(*py_kwds)[s]) ; }
		for( ExtraTflag etf : iota(All<ExtraTflag>) ) if ( ::string    s=snake_str(etf) ; ExtraTflagChars[+etf].second && py_kwds->contains(s) ) { n-- ; ad.extra_tflags.set(etf,+(*py_kwds)[s]) ; }
		//
		if (n) return py_err_set(Exception::TypeErr,"unexpected keyword arg") ;
	}
	::vector_s files ;
	try                       { files = _get_files(py_args) ;                          }
	catch (::string const& e) { return py_err_set(Exception::TypeErr,e) ;              }
	try                       { JobSupport::target( _g_record , ::move(files) , ad ) ; }
	catch (::string const& e) { return py_err_set(Exception::ValueErr,e) ;             }
	//
	return None.to_py_boost() ;
}

static PyObject* check_deps( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args)                  ;
	Dict  const* py_kwds =  from_py<Dict  const>(kwds)                  ;
	size_t       n_args  = py_args.size() + (py_kwds?py_kwds->size():0) ;
	if (n_args>1) return py_err_set(Exception::TypeErr,"too many args") ;
	bool  verbose = +py_args ? +py_args[0] : !py_kwds ? false : !py_kwds->contains("verbose") ? false : +(*py_kwds)["verbose"] ;
	Bool3 ok      = JobSupport::check_deps(_g_record,verbose)                                                                  ;
	if (!verbose) return None.to_py_boost() ;
	switch (ok) {
		case Yes   : return True .to_py_boost()                                           ;
		case Maybe : return py_err_set(Exception::RuntimeErr,"some deps are out-of-date") ;
		case No    : return False.to_py_boost()                                           ;
	DF}
}

static PyObject* decode( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args)   ;
	Dict  const* py_kwds =  from_py<Dict  const>(kwds)   ;
	size_t       n       = py_kwds ? py_kwds->size() : 0 ;
	try {
		::string file = _mk_str( _gather_arg( py_args , 0 , py_kwds , "file" , n ) , "file" ) ;
		::string ctx  = _mk_str( _gather_arg( py_args , 1 , py_kwds , "ctx"  , n ) , "ctx"  ) ;
		::string code = _mk_str( _gather_arg( py_args , 2 , py_kwds , "code" , n ) , "code" ) ;
		//
		throw_unless( !n , "unexpected keyword arg" ) ;
		//
		::pair_s<bool/*ok*/> reply = JobSupport::decode( _g_record , ::move(file) , ::move(code) , ::move(ctx) ) ;
		throw_unless( reply.second , reply.first ) ;
		return Ptr<Str>(reply.first)->to_py_boost() ;
	} catch (::string const& e) {
		return py_err_set(Exception::TypeErr,e) ;
	}
}

static PyObject* encode( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args)   ;
	Dict  const* py_kwds =  from_py<Dict  const>(kwds)   ;
	size_t       n       = py_kwds ? py_kwds->size() : 0 ;
	try {
		::string file    = _mk_str  ( _gather_arg( py_args , 0 , py_kwds , "file"    , n ) ,     "file"    ) ;
		::string ctx     = _mk_str  ( _gather_arg( py_args , 1 , py_kwds , "ctx"     , n ) ,     "ctx"     ) ;
		::string val     = _mk_str  ( _gather_arg( py_args , 2 , py_kwds , "val"     , n ) ,     "val"     ) ;
		uint8_t  min_len = _mk_uint8( _gather_arg( py_args , 3 , py_kwds , "min_len" , n ) , 1 , "min_len" ) ;
		//
		throw_unless( !n                     , "unexpected keyword arg"                                                     ) ;
		throw_unless( min_len<=sizeof(Crc)*2 , "min_len (",min_len,") cannot be larger than crc length (",sizeof(Crc)*2,')' ) ; // codes are output in hex, 4 bits/digit
		//
		::pair_s<bool/*ok*/> reply = JobSupport::encode( _g_record , ::move(file) , ::move(val) , ::move(ctx) , min_len ) ;
		throw_unless( reply.second , reply.first ) ;
		return Ptr<Str>(reply.first)->to_py_boost() ;
	} catch (::string const& e) {
		return py_err_set(Exception::TypeErr,e) ;
	}
}

static PyObject* get_autodep( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args) ;
	size_t       n_args  = py_args.size()              ;
	if (kwds    ) return py_err_set(Exception::TypeErr,"expected no keyword args") ;
	if (n_args>0) return py_err_set(Exception::TypeErr,"expected no args"        ) ;
	//               vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	return Ptr<Bool>(Backdoor::call(Backdoor::Enable()))->to_py_boost() ;
	//               ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}

static PyObject* set_autodep( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args) ;
	size_t       n_args  = py_args.size()              ;
	if (kwds    ) return py_err_set(Exception::TypeErr,"no keyword args") ;
	if (n_args>1) return py_err_set(Exception::TypeErr,"too many args"  ) ;
	if (n_args<1) return py_err_set(Exception::TypeErr,"missing arg"    ) ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Backdoor::call(Backdoor::Enable{No|+py_args[0]}) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	return None.to_py_boost() ;
}

#pragma GCC visibility push(default)
PyMODINIT_FUNC
#if PY_MAJOR_VERSION<3
	initclmake2()
#else
	PyInit_clmake()
#endif
{

	#define F(name,descr) { #name , reinterpret_cast<PyCFunction>(name) , METH_VARARGS|METH_KEYWORDS , descr }
	PyMethodDef _g_funcs[] = {
		F( check_deps ,
			"check_deps(verbose=false)\n"
			"Ensure that all previously seen deps are up-to-date.\n"
			"Job will be killed in case some deps are not up-to-date.\n"
			"If verbose, wait for server reply (it is unclear that this be useful in any way).\n"
		)
	,	F( decode ,
			"decode(file,ctx,code)\n"
			"Return the associated (long) value passed by encode(file,ctx,val) when it returned (short) code.\n"
			"This call to encode must have been done before calling decode.\n"
		)
	,	F( depend ,
			"depend(\n"
			"\t*deps\n"
			",\tfollow_symlinks=False\n"
			",\tverbose        =False # return a report as a dict  dep:(ok,crc) for dep in deps} ok=True if dep ok, False if dep is in error, None if dep is out-of-date\n"
			",\tread           =True  # pretend deps are read in addition to setting flags\n"
			"# flags :\n"
			",\tcritical       =False # if modified, ignore following deps\n"
			",\tessential      =False # show when generating user oriented graphs\n"
			",\tignore         =False # ignore deps, used to mask out further accesses\n"
			",\tignore_error   =False # dont propagate error if dep is in error (Error instead of Err because name is visible from user)\n"
			",\trequired       =True  # dep must be buildable\n"
			")\n"
			"Pretend parallel read of deps (unless read=False) and mark them with flags mentioned as True.\n"
			"Flags accumulate and are never reset.\n"
		)
	,	F( encode ,
			"encode(file,ctx,val,min_length=1)\n"
			"Return a (short) code associated with (long) val. If necessary create such a code of\n"
			"length at least min_length based on a checksum computed from value.\n"
			"val can be retrieve from code using decode(file,ctx,code),\n"
			"even from another job (as long as it is called after the call to encode).\n"
			"This means that decode(file,ctx,encode(file,ctx,val,min_length)) always return val for any min_length.\n"
		)
	,	F( get_autodep ,
			"get_autodep()\n"
			"Return True if autodep is currenly activated (else False).\n"
		)
	,	F( set_autodep ,
			"set_autodep(active)\n"
			"Activate (if active) or deactivate (if not active) autodep recording.\n"
		)
	,	F( target ,
			"target(\n"
			"\t*targets\n"
			",\twrite      =True  # pretend targets are written in addition to setting flags\n"
			"# flags :\n"
			",\tallow      =True  # writing to this target is allowed\n"
			",\tessential  =False # show when generating user oriented graphs\n"
			",\tignore     =False # ignore targets, used to mask out further accesses\n"
			",\tincremental=False # reads are allowed (before earliest write if any)\n"
			",\tno_uniquify=False # target is uniquified if it has several links and is incremental\n"
			",\tno_warning =False # warn if target is either uniquified or unlinked and generated by another rule\n"
			",\tsource_ok  =False # ok to overwrite source files\n"
			")\n"
			"Pretend write to targets and mark them with flags mentioned as True.\n"
			"Flags accumulate and are never reset.\n"
		)
	,	{nullptr,nullptr,0,nullptr}/*sentinel*/
	} ;
	#undef F

	_g_record = {New,Yes/*enabled*/} ;
	//
	//
	Ptr<Tuple> py_ads { HAS_LD_AUDIT+3} ; // PER_AUTODEP_METHOD : add entries here
	size_t i = 0 ;
	if (HAS_LD_AUDIT) py_ads->set_item( i++ , *Ptr<Str>("ld_audit"           ) ) ;
	/**/              py_ads->set_item( i++ , *Ptr<Str>("ld_preload"         ) ) ;
	/**/              py_ads->set_item( i++ , *Ptr<Str>("ld_preload_jemalloc") ) ;
	/**/              py_ads->set_item( i++ , *Ptr<Str>("ptrace"             ) ) ;
	SWEAR(i==py_ads->size(),i,py_ads->size()) ;
	//
	Ptr<Tuple>  py_bes { 1+HAS_SGE+HAS_SLURM } ;      // PER_BACKEND : add entries here
	i = 0 ;
	/**/           py_bes->set_item(i++,*Ptr<Str>("local")) ;
	if (HAS_SGE  ) py_bes->set_item(i++,*Ptr<Str>("sge"  )) ;
	if (HAS_SLURM) py_bes->set_item(i++,*Ptr<Str>("slurm")) ;
	SWEAR(i==py_bes->size(),i,py_bes->size()) ;
	//
	Ptr<Module> mod { PY_MAJOR_VERSION<3?"clmake2":"clmake" , _g_funcs } ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	mod->set_attr( "top_repo_root" , *Ptr<Str>(no_slash(Record::s_autodep_env().repo_root_s).c_str()) ) ;
	mod->set_attr( "backends"      , *py_bes                                                          ) ;
	mod->set_attr( "autodeps"      , *py_ads                                                          ) ;
	mod->set_attr( "no_crc"        , *Ptr<Int>(+Crc::Unknown)                                         ) ;
	mod->set_attr( "crc_a_link"    , *Ptr<Int>(+Crc::Lnk    )                                         ) ;
	mod->set_attr( "crc_a_reg"     , *Ptr<Int>(+Crc::Reg    )                                         ) ;
	mod->set_attr( "crc_no_file"   , *Ptr<Int>(+Crc::None   )                                         ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	mod->boost() ;
	#if PY_MAJOR_VERSION>=3
		return mod->to_py() ;
	#endif

}
#pragma GCC visibility pop
