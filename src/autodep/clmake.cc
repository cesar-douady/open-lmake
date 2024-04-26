// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be first because Python.h must be first

#include "lib.hh"
#include "disk.hh"
#include "hash.hh"
#include "time.hh"

#include "env.hh"
#include "record.hh"

using namespace Disk ;
using namespace Hash ;
using namespace Py   ;
using namespace Time ;

using Proc = JobExecProc ;

static Record _g_record ;

static ::string _mk_str( Object const* o , ::string const& arg_name={} ) {
	if (!o) throw to_string("missing argument",+arg_name?" ":"",arg_name) ;
	return *o->str() ;
}

static uint8_t _mk_uint8( Object const* o , uint8_t dflt , ::string const& arg_name={} ) {
	if (!o) return dflt ;
	try                       { return o->as_a<Int>() ;                                                    }
	catch (::string const& e) { throw to_string("bad type/value for argument",+arg_name?" ":"",arg_name) ; }
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
	for( size_t i=0 ; i<res.size() ; i++ ) if(!res[i]) throw to_string("argument ",i+1," is empty") ;
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
	Tuple const& py_args   = *from_py<Tuple const>(args)                       ;
	Dict  const* py_kwds   =  from_py<Dict  const>(kwds)                       ;
	bool         no_follow = true                                              ;
	bool         verbose   = false                                             ;
	bool         read      = true                                              ;
	AccessDigest ad        { .accesses=~Accesses() , .dflags=Dflag::Required } ;
	if (py_kwds) {
		size_t n_kwds = py_kwds->size() ;
		/**/                                    if ( const char* s="follow_symlinks" ;                                 py_kwds->contains(s) ) { n_kwds-- ; no_follow =             !(*py_kwds)[s]  ; }
		/**/                                    if ( const char* s="verbose"         ;                                 py_kwds->contains(s) ) { n_kwds-- ; verbose   =             +(*py_kwds)[s]  ; }
		/**/                                    if ( const char* s="read"            ;                                 py_kwds->contains(s) ) { n_kwds-- ; read      =             +(*py_kwds)[s]  ; }
		for( Dflag      df  : Dflag::NDyn     ) if ( ::string    s=snake_str(df )    ;                                 py_kwds->contains(s) ) { n_kwds-- ; ad.dflags      .set(df ,+(*py_kwds)[s]) ; }
		for( ExtraDflag edf : All<ExtraDflag> ) if ( ::string    s=snake_str(edf)    ; ExtraDflagChars[+edf].second && py_kwds->contains(s) ) { n_kwds-- ; ad.extra_dflags.set(edf,+(*py_kwds)[s]) ; }
		//
		if (n_kwds) return py_err_set(Exception::TypeErr,"unexpected keyword arg") ;
	}
	if (!read) ad.accesses = {} ;
	::vector_s files ;
	try                       { files = _get_files(py_args) ;             }
	catch (::string const& e) { return py_err_set(Exception::TypeErr,e) ; }
	//
	if (verbose) {
		Ptr<Dict> res { New } ;
		if (+files) {           // fast path : else, depend on no files
			//
			JobExecRpcReply reply = _g_record.direct(JobExecRpcReq( Proc::DepVerbose , ::copy(files) , ad , no_follow , true/*sync*/ , "depend" )) ;
			//
			SWEAR( reply.dep_infos.size()==files.size() , reply.dep_infos.size() , files.size() ) ;
			for( size_t i=0 ; i<reply.dep_infos.size() ; i++ ) {
				Object* py_ok = nullptr/*garbage*/ ;
				switch (reply.dep_infos[i].first) {
					case Yes   : py_ok = &True  ; break ;
					case Maybe : py_ok = &None  ; break ;
					case No    : py_ok = &False ; break ;
				}
				res->set_item( files[i] , *Ptr<Tuple>( *py_ok , *Ptr<Str>(::string(reply.dep_infos[i].second)) ) ) ;
			}
		}
		return res->to_py_boost() ;
	} else {
		_g_record.direct(JobExecRpcReq( Proc::Access , ::move(files) , ad , no_follow , "depend" )) ;
		return None.to_py_boost() ;
	}
}

static PyObject* target( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args   = *from_py<Tuple const>(args)                    ;
	Dict  const* py_kwds   =  from_py<Dict  const>(kwds)                    ;
	bool         no_follow = true                                           ;
	AccessDigest ad        { .write=Yes , .extra_tflags=ExtraTflag::Allow } ;
	if (py_kwds) {
		size_t n_kwds = py_kwds->size() ;
		/**/                                    if ( const char* s="follow_symlinks" ;                                 py_kwds->contains(s) ) { n_kwds-- ; no_follow =             !(*py_kwds)[s]  ; }
		/**/                                    if ( const char* s="write"           ;                                 py_kwds->contains(s) ) { n_kwds-- ; ad.write  = No |        +(*py_kwds)[s]  ; }
		for( Tflag      tf  : Tflag::NDyn     ) if ( ::string    s=snake_str(tf )    ;                                 py_kwds->contains(s) ) { n_kwds-- ; ad.tflags      .set(tf ,+(*py_kwds)[s]) ; }
		for( ExtraTflag etf : All<ExtraTflag> ) if ( ::string    s=snake_str(etf)    ; ExtraTflagChars[+etf].second && py_kwds->contains(s) ) { n_kwds-- ; ad.extra_tflags.set(etf,+(*py_kwds)[s]) ; }
		//
		if (n_kwds) return py_err_set(Exception::TypeErr,"unexpected keyword arg") ;
	}
	::vector_s files ;
	try                       { files = _get_files(py_args) ;             }
	catch (::string const& e) { return py_err_set(Exception::TypeErr,e) ; }
	//
	JobExecRpcReq jerr { Proc::Access , ::move(files) , ad , no_follow , false/*sync*/ , "target" } ;
	_g_record.direct(::move(jerr)) ;
	//
	return None.to_py_boost() ;
}

static PyObject* check_deps( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args)                  ;
	Dict  const* py_kwds =  from_py<Dict  const>(kwds)                  ;
	size_t       n_args  = py_args.size() + (py_kwds?py_kwds->size():0) ;
	if (n_args>1) return py_err_set(Exception::TypeErr,"too many args") ;
	bool            verbose = +py_args ? +py_args[0] : !py_kwds ? false : !py_kwds->contains("verbose") ? false : +(*py_kwds)["verbose"] ;
	JobExecRpcReply reply   = _g_record.direct(JobExecRpcReq(Proc::ChkDeps,verbose/*sync*/))                                             ;
	if (!verbose) return None.to_py_boost() ;
	switch (reply.ok) {
		case Yes   : return True .to_py_boost()                                           ;
		case Maybe : return py_err_set(Exception::RuntimeErr,"some deps are out-of-date") ;
		case No    : return False.to_py_boost()                                           ;
	DF}
}

static PyObject* decode( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args)   ;
	Dict  const* py_kwds =  from_py<Dict  const>(kwds)   ;
	size_t       n_kwds  = py_kwds ? py_kwds->size() : 0 ;
	try {
		::string file = _mk_str( _gather_arg( py_args , 0 , py_kwds , "file" , n_kwds ) , "file" ) ;
		::string ctx  = _mk_str( _gather_arg( py_args , 1 , py_kwds , "ctx"  , n_kwds ) , "ctx"  ) ;
		::string code = _mk_str( _gather_arg( py_args , 2 , py_kwds , "code" , n_kwds ) , "code" ) ;
		//
		if (n_kwds) throw "unexpected keyword arg"s ;
		//
		JobExecRpcReply reply = _g_record.direct(JobExecRpcReq( Proc::Decode , ::move(file) , ::move(code) , ::move(ctx) )) ;
		if (reply.ok!=Yes) throw reply.txt ;
		return Ptr<Str>(reply.txt)->to_py_boost() ;
	} catch (::string const& e) {
		return py_err_set(Exception::TypeErr,e) ;
	}
}

static PyObject* encode( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args)   ;
	Dict  const* py_kwds =  from_py<Dict  const>(kwds)   ;
	size_t       n_kwds  = py_kwds ? py_kwds->size() : 0 ;
	try {
		::string file    = _mk_str  ( _gather_arg( py_args , 0 , py_kwds , "file"    , n_kwds ) ,     "file"    ) ;
		::string ctx     = _mk_str  ( _gather_arg( py_args , 1 , py_kwds , "ctx"     , n_kwds ) ,     "ctx"     ) ;
		::string val     = _mk_str  ( _gather_arg( py_args , 2 , py_kwds , "val"     , n_kwds ) ,     "code"    ) ;
		uint8_t  min_len = _mk_uint8( _gather_arg( py_args , 3 , py_kwds , "min_len" , n_kwds ) , 1 , "min_len" ) ;
		//
		if (n_kwds                ) throw "unexpected keyword arg"s                                                                      ;
		if (min_len>MaxCodecBits/4) throw to_string("min_len (",min_len,") cannot be larger max allowed code bits (",MaxCodecBits/4,')') ; // codes are output in hex, 4 bits/digit
		//
		JobExecRpcReply reply = _g_record.direct(JobExecRpcReq( Proc::Encode , ::move(file) , ::move(val) , ::move(ctx) , min_len )) ;
		if (reply.ok!=Yes) throw reply.txt ;
		return Ptr<Str>(reply.txt)->to_py_boost() ;
	} catch (::string const& e) {
		return py_err_set(Exception::TypeErr,e) ;
	}
}

static PyObject* has_backend( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args) ;
	if ( py_args.size()!=1 || kwds ) return py_err_set(Exception::TypeErr,"expect exactly a single positional argument") ;
	::string   be  = py_args[0].as_a<Str>() ;
	BackendTag tag {}/*garbage*/            ;
	try                     { tag = mk_enum<BackendTag>(be) ; if (!tag) throw ""s ;          }
	catch (::string const&) { return py_err_set(Exception::ValueErr,"unknown backend "+be) ; }
	switch (tag) {                                                                             // PER BACKEND
		case BackendTag::Local : return            True       .to_py_boost() ;
		case BackendTag::Slurm : return (HAS_SLURM?True:False).to_py_boost() ;
	DF} ;
}

static PyObject* search_sub_root_dir( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args) ;
	Dict  const* py_kwds =  from_py<Dict  const>(kwds) ;
	if (py_args.size()>1) return py_err_set(Exception::TypeErr,"expect at most a single argument") ;
	bool no_follow = false ;
	if (py_kwds) {
		ssize_t n_kwds = py_kwds->size() ;
		if ( const char* s="follow_symlinks" ; py_kwds->contains(s) ) { n_kwds-- ; no_follow = !(*py_kwds)[s] ; }
		//
		if (n_kwds) return py_err_set(Exception::TypeErr,"unexpected keyword arg") ;
	}
	::vector_s views = _get_files(py_args) ;
	if (views.size()==0) views.push_back(cwd()) ;
	SWEAR( views.size()==1 , views.size() ) ;
	::string const& view = views[0] ;
	if (!view) return Ptr<Str>(""s)->to_py_boost() ;
	//
	RealPath::SolveReport solve_report = RealPath(Record::s_autodep_env()).solve(view,no_follow) ;
	//
	switch (solve_report.file_loc) {
		case FileLoc::Root :
			return Ptr<Str>(""s)->to_py_boost() ;
		case FileLoc::Repo :
			try {
				::string abs_path         = mk_abs(solve_report.real,Record::s_autodep_env().root_dir+'/') ;
				::string abs_sub_root_dir = search_root_dir(abs_path).first                                ; abs_sub_root_dir += '/' ;
				return Ptr<Str>(abs_sub_root_dir.c_str()+Record::s_autodep_env().root_dir.size()+1)->to_py_boost() ;
			} catch (::string const&e) {
				return py_err_set(Exception::ValueErr,e) ;
			}
		default :
			return py_err_set(Exception::ValueErr,"cannot find sub root dir in repository") ;
	}
}

static PyObject* autodep_env( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args) ;
	size_t       n_args  = py_args.size()              ;
	if (kwds    ) return py_err_set(Exception::TypeErr,"no keyword args") ;
	if (n_args>0) return py_err_set(Exception::TypeErr,"no arg expected") ;
	return Ptr<Str>(Record::s_autodep_env())->to_py_boost() ;
}

static PyObject* get_autodep( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args) ;
	size_t       n_args  = py_args.size()              ;
	if (kwds    ) return py_err_set(Exception::TypeErr,"expected no keyword args") ;
	if (n_args>0) return py_err_set(Exception::TypeErr,"expected no args"        ) ;
	char c = 0/*garbage*/ ;
	// we have a private Record with a private AutodepEnv, so we must go through the backdoor to alter the regular AutodepEnv
	int rc [[maybe_unused]] = ::readlinkat( Record::s_root_fd() , (PrivateAdminDir+"/backdoor/autodep"s ).c_str() , &c , 1 ) ;
	if (rc<0) return None.to_py_boost() ;
	SWEAR( c=='0' || c=='1' , int(c) ) ;
	SWEAR( rc==1            , rc     ) ;
	return Ptr<Bool>(c!='0')->to_py_boost() ;
}

static PyObject* set_autodep( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
	Tuple const& py_args = *from_py<Tuple const>(args) ;
	size_t       n_args  = py_args.size()              ;
	if (kwds    ) return py_err_set(Exception::TypeErr,"no keyword args") ;
	if (n_args>1) return py_err_set(Exception::TypeErr,"too many args"  ) ;
	if (n_args<1) return py_err_set(Exception::TypeErr,"missing arg"    ) ;
	char c ;
	// we have a private Record with a private AutodepEnv, so we must go through the backdoor to alter the regular AutodepEnv
	int rc [[maybe_unused]] ;                                                                     // avoid compiler warning
	if (+py_args[0]) rc = ::readlinkat( Record::s_root_fd() , (PrivateAdminDir+"/backdoor/enable"s ).c_str() , &c , 1 ) ;
	else             rc = ::readlinkat( Record::s_root_fd() , (PrivateAdminDir+"/backdoor/disable"s).c_str() , &c , 1 ) ; // note that the depend and target functions are still working while disabled
	return None.to_py_boost() ;
}

#define F(name,descr) { #name , reinterpret_cast<PyCFunction>(name) , METH_VARARGS|METH_KEYWORDS , descr }
static PyMethodDef funcs[] = {
	F( autodep_env         , "autodep_env()"                                                                         " Return the value to put in $LMAKE_AUTODEP_ENV to set the current behavior." )
,	F( check_deps          , "check_deps(verbose=False)"                                                             " Ensure that all previously seen deps are up-to-date."                       )
,	F( decode              , "decode(code,file,ctx)"                                                                 " Return the associated value passed by encode(value,file,ctx)."              )
,	F( depend              , "depend(dep1,dep2,...,verbose=False,follow_symlinks=True,<dep flags=True>,...)"         " Pretend read of all argument and mark them with flags mentioned as True."   )
,	F( encode              , "encode(value,file,ctx,min_length=1)"                                                   " Return a code associated with value. If necessary create such a code of"
	/**/                                                                                                             " length at least min_length after a checksum computed after value."          )
,	F( get_autodep         , "get_autodep()"                                                                         " Return whether autodep is currenly activated."                              )
,	F( has_backend         , "has_backend(backend)"                                                                  " Return true if the corresponding backend is implememented."                 )
,	F( search_sub_root_dir , "search_sub_root_dir(cwd=os.getcwd())"                                                  " Return the nearest hierarchical root dir relative to the actual root dir."  )
,	F( set_autodep         , "set_autodep(active)"                                                                   " Activate (True) or deactivate(Fale) autodep recording."                     )
,	F( target              , "target(target1,target2,...,follow_symlinks=True,unlink=False,<target flags=True>,...)" " Pretend write to/unlink all arguments and mark them with flags mentioned"
	/**/                                                                                                             " as True."                                                                   )
,	{nullptr,nullptr,0,nullptr}/*sentinel*/
} ;
#undef F

#pragma GCC visibility push(default)
PyMODINIT_FUNC
#if PY_MAJOR_VERSION<3
	initclmake2()
#else
	PyInit_clmake()
#endif
{
	Ptr<Module> mod { PY_MAJOR_VERSION<3?"clmake2":"clmake" , funcs } ;
	//
	_g_record = {New,Yes/*enable*/} ;
	Ptr<Tuple> py_bes{ 1+HAS_SLURM } ;                      // PER_BACKEND : add an entry here
	/**/           py_bes->set_item(0,*Ptr<Str>("local")) ;
	if (HAS_SLURM) py_bes->set_item(1,*Ptr<Str>("slurm")) ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	mod->set_attr( "root_dir"                , *Ptr<Str>(Record::s_autodep_env().root_dir.c_str()) ) ;
	mod->set_attr( "backends"                , *py_bes                                             ) ;
	mod->set_attr( "has_ld_audit"            , *Ptr<Bool>(bool(HAS_LD_AUDIT))                      ) ;
	mod->set_attr( "has_ld_preload"          ,                True                                 ) ;
	mod->set_attr( "has_ld_preload_jemalloc" ,                True                                 ) ;
	mod->set_attr( "has_ptrace"              ,                True                                 ) ;
	mod->set_attr( "no_crc"                  , *Ptr<Int>(+Crc::Unknown)                            ) ;
	mod->set_attr( "crc_a_link"              , *Ptr<Int>(+Crc::Lnk    )                            ) ;
	mod->set_attr( "crc_a_reg"               , *Ptr<Int>(+Crc::Reg    )                            ) ;
	mod->set_attr( "crc_no_file"             , *Ptr<Int>(+Crc::None   )                            ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	mod->boost() ;
	#if PY_MAJOR_VERSION>=3
		return mod->to_py() ;
	#endif
}
#pragma GCC visibility pop
