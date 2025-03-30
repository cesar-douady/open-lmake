// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
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

template<class T,Ptr<T>(*Func)( Tuple const& args , Dict const& kwds )>
	static PyObject* py_func( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
		NoGil no_gil ; // tell our mutex we already have the GIL
		try {
			if (kwds) return Func( *from_py<Tuple const>(args) , *from_py<Dict const>(kwds) )->to_py_boost() ;
			else      return Func( *from_py<Tuple const>(args) , *Ptr<Dict>(New)            )->to_py_boost() ;
		}
		catch (                 ::string  const& e) { return py_err_set(Exception::TypeErr,e       ) ; }
		catch (::pair<Exception,::string> const& e) { return py_err_set(e.first           ,e.second) ; }
	}
template<void(*Func)( Tuple const& args , Dict const& kwds )>
	static Ptr<> add_none( Tuple const& args , Dict const& kwds ) {
		Func(args,kwds) ;
		return &None ;
	}
template<void(*Func)( Tuple const& args , Dict const& kwds )>
	static PyObject* py_func( PyObject* null , PyObject* args , PyObject* kwds ) {
		return py_func<Object,add_none<Func>>(null,args,kwds) ;
	} ;

static uint8_t _mk_uint8( Object const& o , ::string const& arg_name={} ) {
	try                       { return o.as_a<Int>() ;                                        }
	catch (::string const& e) { throw "bad type/value for arg"s+(+arg_name?" ":"")+arg_name ; }
}

static ::vector_s _get_files(Tuple const& py_args) {
	::vector_s res  ;
	//
	auto push = [&](Object const& o)->void {
		if (+o) res.push_back(*o.str()) ;
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

static Ptr<> depend( Tuple const& py_args , Dict const& py_kwds ) {
	bool         no_follow = true                      ;
	bool         verbose   = false                     ;
	AccessDigest ad        { .dflags=Dflag::Required } ;
	//
	for( auto const& [py_key,py_val] : py_kwds ) {
		::string key = py_key.template as_a<Str>() ;
		bool     val = +py_val                     ;
		switch (key[0]) {
			case 'f' : if (key=="follow_symlinks") {          no_follow   = !val         ; continue ; } break ;
			case 'r' : if (key=="read"           ) { if (val) ad.accesses =  ~Accesses() ; continue ; } break ;
			case 'v' : if (key=="verbose"        ) {          verbose     =  val         ; continue ; } break ;
		DN}
		if      (can_mk_enum<Dflag     >(key)) { if ( Dflag      df =mk_enum<Dflag     >(key) ; df<Dflag::NDyn ) { ad.dflags      .set(df ,val) ; continue ; } }
		else if (can_mk_enum<ExtraDflag>(key)) {      ExtraDflag edf=mk_enum<ExtraDflag>(key) ;                    ad.extra_dflags.set(edf,val) ; continue ;   }
		throw "unexpected keyword arg "+key ;
	}
	//
	::vector_s files = _get_files(py_args) ;
	//
	::vector<pair<Bool3/*ok*/,Crc>> dep_infos ;
	try                       { dep_infos = JobSupport::depend( _g_record , ::copy(files) , ad , no_follow , verbose ) ; }
	catch (::string const& e) { throw ::pair(Exception::ValueErr,e) ;                                                    }
	//
	if (!verbose) return &None ;
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
	return res ;
}

static void target( Tuple const& py_args , Dict const& py_kwds ) {
	AccessDigest ad { .extra_tflags=ExtraTflag::Allow } ;
	//
	for( auto const& [py_key,py_val] : py_kwds ) {
		::string key = py_key.template as_a<Str>() ;
		bool     val = +py_val                     ;
		if      (key=="write"                ) {                                                                   ad.write = No|val ;            continue ;   }
		if      (can_mk_enum<Tflag     >(key)) { if ( Tflag      tf =mk_enum<Tflag     >(key) ; tf<Tflag::NDyn ) { ad.tflags      .set(tf ,val) ; continue ; } }
		else if (can_mk_enum<ExtraTflag>(key)) {      ExtraTflag etf=mk_enum<ExtraTflag>(key) ;                    ad.extra_tflags.set(etf,val) ; continue ;   }
		throw "unexpected keyword arg "+key ;
	}
	::vector_s files = _get_files(py_args) ;
	try                       { JobSupport::target( _g_record , ::move(files) , ad ) ; }
	catch (::string const& e) { throw ::pair(Exception::ValueErr,e) ;                  }
}

static Ptr<> check_deps( Tuple const& py_args , Dict const& py_kwds ) {
	if (py_args.size()>1) throw "too many args" ;
	bool verbose = +py_args && +py_args[0] ;
	if ( +py_args && +py_kwds ) throw "too many args" ;
	for( auto const& [py_key,py_val] : py_kwds ) {
		::string key = py_key.template as_a<Str>() ;
		if (key=="verbose") verbose = +py_val ;
		else                throw "unexpected keyword arg "+key ;
	}
	Bool3 ok = JobSupport::check_deps(_g_record,verbose) ;
	if (!verbose ) return &None                                                     ;
	if (ok==Maybe) throw ::pair(Exception::RuntimeErr,"some deps are out-of-date"s) ; // defensive only : job should be killed in that case
	/**/           return Ptr<Bool>(ok==Yes)                                        ;
}

// encode and decode are very similar, it is easier to define a template for both
// cv means code for decode and val for encode
template<bool Encode> static Ptr<Str> codec( Tuple const& py_args , Dict const& py_kwds ) {
	static constexpr const char* Cv = Encode ? "val" : "code" ;
	size_t n_args = py_args.size() ;
	//
	::string file    ;     bool has_file    = false ;
	::string ctx     ;     bool has_ctx     = false ;
	::string cv      ;     bool has_cv      = false ;
	uint8_t  min_len = 1 ; bool has_min_len = false ;
	if (n_args>(Encode?4:3)) throw "too many args : "s+n_args+'>'+(Encode?4:3) ;
	switch (n_args) {
		case 4 : min_len = _mk_uint8(py_args[3],"min_len") ; has_min_len = true ; [[fallthrough]] ;
		case 3 : cv      =          *py_args[2].str()      ; has_cv      = true ; [[fallthrough]] ;
		case 2 : ctx     =          *py_args[1].str()      ; has_ctx     = true ; [[fallthrough]] ;
		case 1 : file    =          *py_args[0].str()      ; has_file    = true ; [[fallthrough]] ;
		case 0 : break ;
	DF}
	for( auto const& [py_key,py_val] : py_kwds ) {
		static constexpr const char* MsgEnd = " passed both as positional and keyword" ;
		::string key = py_key.template as_a<Str>() ;
		switch (key[0]) {
			case 'f' : if (key=="file"   ) { throw_if(has_file   ,"arg file"   ,MsgEnd) ; file    =          *py_val.str() ; has_file    = true ; continue ; } break ;
			case 'c' : if (key=="ctx"    ) { throw_if(has_ctx    ,"arg ctx"    ,MsgEnd) ; ctx     =          *py_val.str() ; has_ctx     = true ; continue ; }
			/**/       if (key==Cv       ) { throw_if(has_cv     ,"arg ",Cv,' ',MsgEnd) ; cv      =          *py_val.str() ; has_cv      = true ; continue ; } break ;
			case 'v' : if (key==Cv       ) { throw_if(has_cv     ,"arg ",Cv,' ',MsgEnd) ; cv      =          *py_val.str() ; has_cv      = true ; continue ; } break ;
			case 'm' : if (key=="min_len") { throw_if(has_min_len,"arg min_len",MsgEnd) ; min_len = _mk_uint8(py_val,key)  ; has_min_len = true ; continue ; } break ;
		DN}
		throw "unexpected keyword arg "+key ;
	}
	/**/         throw_unless( has_file     , "missing arg ","file"    ) ;
	/**/         throw_unless( has_ctx      , "missing arg ","ctx"     ) ;
	/**/         throw_unless( has_cv       , "missing arg ",Cv        ) ;
	if (!Encode) throw_unless( !has_min_len , "unexpected arg min_len" ) ;
	//
	::pair_s<bool/*ok*/> reply =
		Encode ? JobSupport::encode( _g_record , ::move(file) , ::move(cv/*val*/ ) , ::move(ctx) , min_len )
		:        JobSupport::decode( _g_record , ::move(file) , ::move(cv/*code*/) , ::move(ctx)           )
	;
	throw_unless( reply.second , reply.first ) ;
	return reply.first ;
}

template<bool IsFile> static Ptr<Str> xxhsum( Tuple const& py_args , Dict const& py_kwds ) {
	static constexpr const char* Ft = IsFile ? "file" : "text" ;
	size_t n_args = py_args.size() ;
	::string ft ;
	bool has_ft = false ;
	if (n_args>1) throw "too many args : "s+n_args+'>'+1 ;
	if (n_args) {
		ft     = *py_args[0].str() ;
		has_ft = true              ;
	}
	for( auto const& [py_key,py_val] : py_kwds ) {
		::string key = py_key.template as_a<Str>() ;
		if (key!=Ft) throw cat("unexpected keyword arg ",Ft) ;
		throw_if( has_ft , "arg ",Ft," passed both as positional and keyword" ) ;
		ft     = *py_val.str() ;
		has_ft = true          ;
	}
	throw_unless( has_ft , "missing arg ",ft ) ;
	if (IsFile) {
		return ::string(Crc(ft)) ;
	} else {
		Xxh h ;
		if (+ft) h += ft ;
		return h.digest().hex() ;
	}
}

static Ptr<Bool> get_autodep( Tuple const& py_args , Dict const& py_kwds ) {
	if ( +py_args || +py_kwds ) throw "expected no args"s ;
	//     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	return Backdoor::call(Backdoor::Enable()) ;
	//     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}

static void set_autodep( Tuple const& py_args , Dict const& py_kwds ) {
	size_t n_args = py_args.size() ;
	//
	if (+py_kwds) throw "unexpected keyword args"s ;
	if (n_args>1) throw "too many args"s           ;
	if (n_args<1) throw "missing arg"s             ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Backdoor::call(Backdoor::Enable{No|+py_args[0]}) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}

#pragma GCC visibility push(default)
PyMODINIT_FUNC
#if PY_MAJOR_VERSION<3
	initclmake2()
#else
	PyInit_clmake()
#endif
{

	NoGil no_gil ; // tell our mutex we already have the GIL

	#define F(name,func,descr) { name , reinterpret_cast<PyCFunction>(func) , METH_VARARGS|METH_KEYWORDS , descr }
	PyMethodDef _g_funcs[] = {
		F( "depend" , (py_func<Object,depend>) ,
			"depend(\n"
			"\t*deps\n"
			",\tfollow_symlinks=False\n"
			",\tverbose        =False    # return a report as a dict  dep:(ok,crc) for dep in deps} ok=True if dep ok, False if dep is in error, None if dep is out-of-date\n"
			",\tread           =False    # pretend deps are read in addition to setting flags\n"
			"# flags :\n"
			",\tcritical       =False    # if modified, ignore following deps\n"
			",\tessential      =False    # show when generating user oriented graphs\n"
			",\tignore         =False    # ignore deps, used to mask out further accesses\n"
			",\tignore_error   =False    # dont propagate error if dep is in error (Error instead of Err because name is visible from user)\n"
			",\trequired       =True     # dep must be buildable\n"
			")\n"
			"Pretend parallel read of deps (if read==True) and mark them with flags mentioned as True.\n"
			"Flags accumulate and are never reset.\n"
		)
	,	F( "target" , py_func<target> ,
			"target(\n"
			"\t*targets\n"
			",\twrite      =False        # pretend targets are written in addition to setting flags\n"
			"# flags :\n"
			",\tallow      =True         # writing to this target is allowed\n"
			",\tessential  =False        # show when generating user oriented graphs\n"
			",\tignore     =False        # ignore targets, used to mask out further accesses\n"
			",\tincremental=False        # reads are allowed (before earliest write if any)\n"
			",\tno_uniquify=False        # target is uniquified if it has several links and is incremental\n"
			",\tno_warning =False        # warn if target is either uniquified or unlinked and generated by another rule\n"
			",\tsource_ok  =False        # ok to overwrite source files\n"
			")\n"
			"Pretend write to targets (if write==True) and mark them with flags mentioned as True.\n"
			"Flags accumulate and are never reset.\n"
		)
	,	F( "check_deps" , (py_func<Object,check_deps>) ,
			"check_deps(verbose=False)\n"
			"Ensure that all previously seen deps are up-to-date.\n"
			"Job will be killed in case some deps are not up-to-date.\n"
			"If verbose, wait for server reply. Return value is False if at least a dep is in error.\n"
			"This is necessary, even without checking return value, to ensure that after this call,\n"
			"the directories of previous deps actually exist if such deps are not read (such as with lmake.depend).\n"
		)
	,	F( "get_autodep" , (py_func<Bool,get_autodep>) ,
			"get_autodep()\n"
			"Return True if autodep is currenly activated (else False).\n"
		)
	,	F( "set_autodep" , py_func<set_autodep> ,
			"set_autodep(active,/)\n"
			"Activate (if active) or deactivate (if not active) autodep recording.\n"
		)
	,	F( "decode" , (py_func<Str,codec<false/*Encode*/>>) ,
			"decode(file,ctx,code)\n"
			"Return the associated (long) value passed by encode(file,ctx,val) when it returned (short) code.\n"
			"This call to encode must have been done before calling decode.\n"
		)
	,	F( "encode" , (py_func<Str,codec<true/*Encode*/>>) ,
			"encode(file,ctx,val,min_length=1)\n"
			"Return a (short) code associated with (long) val. If necessary create such a code of\n"
			"length at least min_length based on a checksum computed from value.\n"
			"val can be retrieve from code using decode(file,ctx,code),\n"
			"even from another job (as long as it is called after the call to encode).\n"
			"This means that decode(file,ctx,encode(file,ctx,val,min_length)) always return val for any min_length.\n"
		)
	,	F( "xxhsum_file" , (py_func<Str,xxhsum<true/*IsFile*/>>) ,
			"xxhsum_file(file)\n"
			"Return a checksum of provided file.\n"
			"The checksum is :\n"
			"	none                                         if file does not exist, is a directory or a special file\n"
			"	empty-R                                      if file is empty\n"
			"	xxxxxxxxxxxxxxxx-R (where x is a hexa digit) if file is regular and non-empty\n"
			"	xxxxxxxxxxxxxxxx-L                           if file is a symbolic link\n"
			"Note : this checksum is *not* crypto-robust.\n"
			"Cf man xxhsum for a description of the algorithm.\n"
		)
	,	F( "xxhsum" , (py_func<Str,xxhsum<false/*IsFile*/>>) ,
			"xxhsum(text)\n"
			"Return a checksum of provided text.\n"
			"It is a 16-digit hex value with no suffix.\n"
			"Note : the empty string lead to 0100000000000000 so as to be easily recognizable.\n"
			"Note : this checksum is not the same as the checksum of a file with same content.\n"
			"Note : this checksum is *not* crypto-robust.\n"
			"Cf man xxhsum for a description of the algorithm.\n"
		)
	,	{nullptr,nullptr,0,nullptr}/*sentinel*/
	} ;
	#undef F

	_g_record = {New,Yes/*enabled*/} ;
	//
	//
	Ptr<Tuple> py_ads { HAS_LD_AUDIT+3} ;        // PER_AUTODEP_METHOD : add entries here
	size_t i = 0 ;
	if (HAS_LD_AUDIT) py_ads->set_item( i++ , *Ptr<Str>("ld_audit"           ) ) ;
	/**/              py_ads->set_item( i++ , *Ptr<Str>("ld_preload"         ) ) ;
	/**/              py_ads->set_item( i++ , *Ptr<Str>("ld_preload_jemalloc") ) ;
	/**/              py_ads->set_item( i++ , *Ptr<Str>("ptrace"             ) ) ;
	SWEAR(i==py_ads->size(),i,py_ads->size()) ;
	//
	Ptr<Tuple>  py_bes { 1+HAS_SGE+HAS_SLURM } ; // PER_BACKEND : add entries here
	i = 0 ;
	/**/           py_bes->set_item(i++,*Ptr<Str>("local")) ;
	if (HAS_SGE  ) py_bes->set_item(i++,*Ptr<Str>("sge"  )) ;
	if (HAS_SLURM) py_bes->set_item(i++,*Ptr<Str>("slurm")) ;
	SWEAR(i==py_bes->size(),i,py_bes->size()) ;
	//
	AutodepEnv  ade { New }                                                    ;
	Ptr<Module> mod { New , PY_MAJOR_VERSION<3?"clmake2":"clmake" , _g_funcs } ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	mod->set_attr( "top_repo_root" , *Ptr<Str>(no_slash(ade.repo_root_s               ).c_str()) ) ;
	mod->set_attr( "repo_root"     , *Ptr<Str>(no_slash(ade.repo_root_s+ade.sub_repo_s).c_str()) ) ;
	mod->set_attr( "backends"      , *py_bes                                                     ) ;
	mod->set_attr( "autodeps"      , *py_ads                                                     ) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	mod.boost() ;                               // avoid problems at finalization
	#if PY_MAJOR_VERSION>=3
		return mod->to_py() ;
	#endif

}
#pragma GCC visibility pop
