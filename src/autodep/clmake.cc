// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "py.hh" // /!\ must be first because Python.h must be first

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

static Record*    _g_record      = nullptr ;
static AutodepEnv _g_autodep_env ;

template<class T,Ptr<T>(*Func)( Tuple const& args , Dict const& kwds )>
	static PyObject* _py_func( PyObject* /*null*/ , PyObject* args , PyObject* kwds ) {
		NoGil no_gil ;                                                                  // tell our mutex we already have the GIL
		try {
			if (kwds) return Func( *from_py<Tuple const>(args) , *from_py<Dict const>(kwds) )->to_py_boost() ;
			else      return Func( *from_py<Tuple const>(args) , *Ptr<Dict>(New)            )->to_py_boost() ;
		}
		catch (                   ::string  const& e) { return py_err_set(PyException::TypeErr,e       ) ; }
		catch (::pair<PyException,::string> const& e) { return py_err_set(e.first             ,e.second) ; }
	}
template<void(*Func)( Tuple const& args , Dict const& kwds )>
	static Ptr<> _add_none( Tuple const& args , Dict const& kwds ) {
		Func(args,kwds) ;
		return &None ;
	}
template<void(*Func)( Tuple const& args , Dict const& kwds )>
	static PyObject* _py_func( PyObject* null , PyObject* args , PyObject* kwds ) {
		return _py_func<Object,_add_none<Func>>(null,args,kwds) ;
	} ;

static uint8_t _mk_uint8( Object const& o , ::string const& arg_name={} ) {
	try                       { return o.as_a<Int>() ;                                          }
	catch (::string const& e) { throw cat("bad type/value for arg",+arg_name?" ":"",arg_name) ; }
}

static ::vector_s _get_files(Tuple const& py_args) {
	::vector_s res  ;
	//
	auto push = [&](Object const& o) {
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

static Ptr<> _depend( Tuple const& py_args , Dict const& py_kwds ) {
	bool         no_follow = true                                                                   ;
	bool         regexpr   = false                                                                  ;
	bool         direct    = false                                                                  ;
	bool         verbose   = false                                                                  ;
	AccessDigest ad        { .flags{.dflags=DflagsDfltDepend,.extra_dflags=ExtraDflagsDfltDepend} } ;
	::vector_s   files     = _get_files(py_args)                                                    ;
	//
	for( auto const& [py_key,py_val] : py_kwds ) {
		::string key = py_key.template as_a<Str>() ;
		bool     val = +py_val                     ;
		switch (key[0]) {
			case 'd' : if (key=="direct"         ) {          direct      =  val          ; continue ; } break ;
			case 'f' : if (key=="follow_symlinks") {          no_follow   = !val          ; continue ; } break ;
			case 'r' : if (key=="read"           ) { if (val) ad.accesses =  DataAccesses ; continue ; }
			/**/       if (key=="regexpr"        ) {          regexpr     =  val          ; continue ; } break ;
			case 'v' : if (key=="verbose"        ) {          verbose     =  val          ; continue ; } break ;
		DN}
		if      (can_mk_enum<Dflag     >(key)) { if ( Dflag      df =mk_enum<Dflag     >(key) ; df<Dflag::NDyn ) { ad.flags.dflags      .set(df ,val) ; continue ; } }
		else if (can_mk_enum<ExtraDflag>(key)) {      ExtraDflag edf=mk_enum<ExtraDflag>(key) ;                    ad.flags.extra_dflags.set(edf,val) ; continue ;   }
		throw "unexpected keyword arg "+key ;
	}
	//
	//
	::pair<::vector<VerboseInfo>,bool/*ok*/> dep_infos ;
	try                       { dep_infos = JobSupport::depend( ::copy(files) , ad , no_follow , regexpr , direct , verbose ) ; }
	catch (::string const& e) { throw ::pair(PyException::ValueErr,e) ;                                                         }
	//
	if (!(verbose||direct)) return &None ;
	//
	if (direct) {
		return Ptr<Bool>(dep_infos.second) ;
	}
	if (verbose) {
		Ptr<Dict> res { New } ;
		//
		SWEAR( dep_infos.first.size()==files.size() , dep_infos.first.size() , files.size() ) ;
		for( size_t i : iota(dep_infos.first.size()) ) {
			VerboseInfo vi          = dep_infos.first[i] ;
			Ptr<Dict>   py_dep_info { New }              ;
			if ( vi.ok!=Maybe) py_dep_info->set_item( "ok"       , *Ptr<Bool>(         vi.ok==Yes) ) ;
			if (+vi.crc      ) py_dep_info->set_item( "checksum" , *Ptr<Str >(::string(vi.crc)   ) ) ;
			res->set_item( files[i] , *py_dep_info ) ;
		}
		return res ;
	}
	FAIL() ;
}

static void _target( Tuple const& py_args , Dict const& py_kwds ) {
	bool         no_follow = true                                                                       ;
	bool         regexpr   = false                                                                      ;
	AccessDigest ad        { .flags{.extra_tflags=ExtraTflag::Allow,.extra_dflags=ExtraDflag::NoStar} } ;
	::vector_s   files     = _get_files(py_args)                                                        ;
	//
	for( auto const& [py_key,py_val] : py_kwds ) {
		::string key = py_key.template as_a<Str>() ;
		bool     val = +py_val                     ;
		switch (key[0]) {
			case 'f' : if (key=="follow_symlinks") { no_follow =   !val ; continue ; } break ;
			case 'r' : if (key=="regexpr"        ) { regexpr   =    val ; continue ; } break ;
			case 'w' : if (key=="write"          ) { ad.write  = No|val ; continue ; } break ;
		DN}
		if      (can_mk_enum<Tflag     >(key)) { if ( Tflag      tf =mk_enum<Tflag     >(key) ; tf<Tflag::NDyn ) { ad.flags.tflags      .set(tf ,val) ; continue ; } }
		else if (can_mk_enum<ExtraTflag>(key)) {      ExtraTflag etf=mk_enum<ExtraTflag>(key) ;                    ad.flags.extra_tflags.set(etf,val) ; continue ;   }
		else if (can_mk_enum<Dflag     >(key)) { if ( Dflag      df =mk_enum<Dflag     >(key) ; df<Dflag::NDyn ) { ad.flags.dflags      .set(df ,val) ; continue ; } }
		else if (can_mk_enum<ExtraDflag>(key)) {      ExtraDflag edf=mk_enum<ExtraDflag>(key) ;                    ad.flags.extra_dflags.set(edf,val) ; continue ;   }
		throw "unexpected keyword arg "+key ;
	}
	//
	try                       { JobSupport::target( ::move(files) , ad , no_follow , regexpr ) ; }
	catch (::string const& e) { throw ::pair(PyException::ValueErr,e) ;                          }
}

static Ptr<> _chk_deps( Tuple const& py_args , Dict const& py_kwds ) {
	size_t n_args = py_args.size() ;
	//
	::optional<Delay> delay ;
	::optional<bool > sync  ;
	if (n_args>2) throw cat("too many args : ",n_args,">2") ;
	switch (n_args) {
		case 2 :                         sync  = +            py_args[1]                 ; [[fallthrough]] ;
		case 1 : if (&py_args[0]!=&None) delay = Delay(double(py_args[0].as_a<Float>())) ; [[fallthrough]] ;
		case 0 : break ;
	DF}                                                   // NO_COV
	for( auto const& [py_key,py_val] : py_kwds ) {
		static constexpr const char* MsgEnd = " passed both as positional and keyword" ;
		::string key = py_key.template as_a<Str>() ;
		switch (key[0]) {
			case 'd' : if (key=="delay") { throw_if(+delay,"arg ",key,MsgEnd) ; { if (&py_val!=&None) delay = Delay(double(py_val.as_a<Float>())) ; } continue ; } break ;
			case 's' : if (key=="sync" ) { throw_if(+sync ,"arg ",key,MsgEnd) ; { if (&py_val!=&None) sync  =             +py_val                 ; } continue ; } break ;
		DN}
		throw "unexpected keyword arg "+key ;
	}
	//
	try {
		bool  is_sync = sync.value_or(false)                                      ;
		Bool3 ok      = JobSupport::chk_deps( delay.value_or(Delay()) , is_sync ) ;
		if (!is_sync) return &None ;
		throw_if(ok==Maybe,"some deps are out-of-date") ; // in case handler catches killing signal as job will be killed in that case
		return Ptr<Bool>(ok==Yes) ;
	} catch (::string const& e) {
		throw ::pair(PyException::RuntimeErr,e) ;
	}
}

template<bool Target> static Ptr<Tuple> _list( Tuple const& py_args , Dict const& py_kwds ) {
	size_t n_args = py_args.size() ;
	//
	::optional_s dir     ;
	::optional_s regexpr ;
	if (n_args>2) throw cat("too many args : ",n_args,">2") ;
	switch (n_args) {
		case 2 : if (&py_args[1]!=&None) regexpr = *py_args[1].str() ; [[fallthrough]] ;
		case 1 : if (&py_args[0]!=&None) dir     = *py_args[0].str() ; [[fallthrough]] ;
		case 0 : break ;
	DF}                                            // NO_COV
	for( auto const& [py_key,py_val] : py_kwds ) {
		static constexpr const char* MsgEnd = " passed both as positional and keyword" ;
		::string key = py_key.template as_a<Str>() ;
		switch (key[0]) {
			case 'd' : if (key=="dir"    ) { throw_if(+dir    ,"arg ",key,MsgEnd) ; { if (&py_val!=&None) dir     = *py_val.str() ; } continue ; } break ;
			case 'r' : if (key=="regexpr") { throw_if(+regexpr,"arg ",key,MsgEnd) ; { if (&py_val!=&None) regexpr = *py_val.str() ; } continue ; } break ;
		DN}
		throw "unexpected keyword arg "+key ;
	}
	//
	try {
		::vector_s files = JobSupport::list( No|Target , ::move(dir) , ::move(regexpr) ) ;
		Ptr<Tuple> res   { files.size() }                                                ;
		for( size_t i : iota(files.size()) ) res->set_item( i , *Ptr<Str>(files[i]) ) ;
		return res ;
	} catch (::string const& e) {
		throw ::pair(PyException::RuntimeErr,e) ;
	}
}

static Ptr<Str> _list_root_s( Tuple const& py_args , Dict const& py_kwds ) {
	if (!( py_args.size()==1 && !py_kwds )) throw cat("accept only a single arg") ;
	try                       { return Ptr<Str>( no_slash( JobSupport::list_root_s(*py_args[0].str()) ) ) ; }
	catch (::string const& e) { throw ::pair(PyException::RuntimeErr,e) ;                                   }
}

// encode and decode are very similar, it is easier to define a template for both
// cv means code for decode and val for encode
template<bool Encode> static Ptr<Str> _codec( Tuple const& py_args , Dict const& py_kwds ) {
	static constexpr const char* Cv = Encode ? "val" : "code" ;
	size_t n_args = py_args.size() ;
	//
	::optional_s        file    ;
	::optional_s        ctx     ;
	::optional_s        cv      ;
	::optional<uint8_t> min_len ;
	if (n_args>(Encode?4:3)) throw cat("too many args : ",n_args,'>',Encode?4:3) ;
	switch (n_args) {
		case 4 : min_len = _mk_uint8(py_args[3],"min_len") ; [[fallthrough]] ;
		case 3 : cv      =          *py_args[2].str()      ; [[fallthrough]] ;
		case 2 : ctx     =          *py_args[1].str()      ; [[fallthrough]] ;
		case 1 : file    =          *py_args[0].str()      ; [[fallthrough]] ;
		case 0 : break ;
	DF}                                            // NO_COV
	for( auto const& [py_key,py_val] : py_kwds ) {
		static constexpr const char* MsgEnd = " passed both as positional and keyword" ;
		::string key = py_key.template as_a<Str>() ;
		switch (key[0]) {
			case 'f' : if (key=="file"   ) { throw_if(+file   ,"arg ",key,MsgEnd) ; file    =          *py_val.str() ; continue ; } break ;
			case 'c' : if (key=="ctx"    ) { throw_if(+ctx    ,"arg ",key,MsgEnd) ; ctx     =          *py_val.str() ; continue ; }
			/**/       if (key==Cv       ) { throw_if(+cv     ,"arg ",key,MsgEnd) ; cv      =          *py_val.str() ; continue ; } break ;
			case 'v' : if (key==Cv       ) { throw_if(+cv     ,"arg ",key,MsgEnd) ; cv      =          *py_val.str() ; continue ; } break ;
			case 'm' : if (key=="min_len") { throw_if(+min_len,"arg ",key,MsgEnd) ; min_len = _mk_uint8(py_val,key)  ; continue ; } break ;
		DN}
		throw "unexpected keyword arg "+key ;
	}
	/**/         throw_unless( +file    , "missing arg ","file"    ) ;
	/**/         throw_unless( +ctx     , "missing arg ","ctx"     ) ;
	/**/         throw_unless( +cv      , "missing arg ",Cv        ) ;
	if (!Encode) throw_if    ( +min_len , "unexpected arg min_len" ) ;
	//
	try {
		return
			Encode ? JobSupport::encode( ::move(*file) , ::move(*ctx) , ::move(*cv/*val*/ ) , min_len.value_or(1) )
			:        JobSupport::decode( ::move(*file) , ::move(*ctx) , ::move(*cv/*code*/)                       )
		;
	} catch (::string const& e) {
		throw ::pair(PyException::RuntimeErr,e) ;
	}
}

template<bool IsFile> static Ptr<Str> _xxhsum( Tuple const& py_args , Dict const& py_kwds ) {
	static constexpr const char* Ft = IsFile ? "file" : "text" ;
	size_t       n_args = py_args.size() ;
	::optional_s ft     ;
	if (n_args>1) throw cat("too many args : ",n_args,'>',1) ;
	if (n_args  ) ft = *py_args[0].str() ;
	//
	for( auto const& [py_key,py_val] : py_kwds ) {
		::string key = py_key.template as_a<Str>() ;
		if (key!=Ft) throw cat("unexpected keyword arg ",key) ;
		throw_unless( !ft , "arg ",Ft," passed both as positional and keyword" ) ;
		ft     = *py_val.str() ;
	}
	throw_unless( +ft , "missing arg ",Ft ) ;
	if (IsFile) return ::string(Crc(*ft)) ;
	else        return Crc(New,*ft).hex() ;
}

static Ptr<Bool> _get_autodep( Tuple const& py_args , Dict const& py_kwds ) {
	if ( +py_args || +py_kwds ) throw "expected no args"s ;
	//     vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	return Backdoor::call(Backdoor::Enable()) ;
	//     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}

static void _set_autodep( Tuple const& py_args , Dict const& py_kwds ) {
	size_t n_args = py_args.size() ;
	//
	if (+py_kwds) throw "unexpected keyword args"s ;
	if (n_args>1) throw "too many args"s           ;
	if (n_args<1) throw "missing arg"s             ;
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
	Backdoor::call(Backdoor::Enable{No|+py_args[0]}) ;
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
}

template<bool EmptyIsDot> static ::vector_s _get_seq( ::string const& key , Object const& py_val ) {
	throw_unless( py_val.is_a<Sequence>() , key , " is not a list/tuple" ) ;
	//
	Sequence const& py_seq = py_val.as_a<Sequence>() ;
	::vector_s      res    ;                           res.reserve(py_seq.size()) ;
	for( Object const& py : py_seq )
		if ( EmptyIsDot && !py ) res.emplace_back("."      ) ;
		else                     res.emplace_back(*py.str()) ;
	return res ;
}
static void _report_import( Tuple const& py_args , Dict const& py_kwds ) {
	static ::vector_s s_std_sfxs ;
	if (!s_std_sfxs) {
		#if PY_MAJOR_VERSION==2
			for( const char* pfx : {"/__init__",""} )
				for( const char* sfx : { ".so" , "module.so" , ".py" , ".pyc" } )
					s_std_sfxs.push_back(cat(pfx,sfx)) ;
		#else
			Ptr<Module  > py_machinery { "importlib.machinery" }                                            ;
			Ptr<Sequence> py_std_sfxs  = py_machinery->get_attr<Callable>("all_suffixes")->call<Sequence>() ;
			for( const char* pfx : {"/__init__",""} ) {
				// although not reflected in all_suffixes(), python3 tries .so files first
				for( Object const& py_sfx : *py_std_sfxs ) { ::string sfx = *py_sfx.str() ; if ( sfx.ends_with(".so")) s_std_sfxs.push_back(cat(pfx,sfx)) ; }
				for( Object const& py_sfx : *py_std_sfxs ) { ::string sfx = *py_sfx.str() ; if (!sfx.ends_with(".so")) s_std_sfxs.push_back(cat(pfx,sfx)) ; }
			}
		#endif
	}
	size_t        n_args  = py_args.size() ;
	Object const* py_name = nullptr        ;
	Object const* py_path = nullptr        ;
	Object const* py_sfxs = nullptr        ;
	switch (n_args) {
		case 3  : py_sfxs = &py_args[2] ; [[fallthrough]] ;
		case 2  : py_path = &py_args[1] ; [[fallthrough]] ;
		case 1  : py_name = &py_args[0] ; [[fallthrough]] ;
		case 0  :                         break           ;
		default : throw cat("too many args : ",n_args,'>',3) ;
	}
	for( auto const& [py_key,py_val] : py_kwds ) {
		static constexpr const char* MsgEnd = " passed both as positional and keyword" ;
		::string key = py_key.template as_a<Str>() ;
		switch (key[0]) {
			case 'm' : if (key=="module_name"    ) { throw_if(py_name,key,MsgEnd) ; py_name = &py_val ; continue ; }
			/**/       if (key=="module_suffixes") { throw_if(py_sfxs,key,MsgEnd) ; py_sfxs = &py_val ; continue ; } break ;
			case 'p' : if (key=="path"           ) { throw_if(py_path,key,MsgEnd) ; py_path = &py_val ; continue ; } break ;
		DN}
		throw "unexpected keyword arg "+key ;
	}
	//
	// path
	::vector_s path ; //!                           EmptyIsDot
	if ( py_path && py_path!=&None ) path = _get_seq<true    >("path"    ,*py_path          ) ;
	else                             path = _get_seq<true    >("sys.path",py_get_sys("path")) ;
	for( ::string& d : path ) rm_slash(d) ;
	#if PY_MAJOR_VERSION>2
		try                       { JobSupport::depend( ::copy(path) , {.flags{.extra_dflags=ExtraDflag::ReaddirOk}} , false/*no_follow*/ ) ; } // python3 reads dirs in path
		catch (::string const& e) { throw ::pair(PyException::ValueErr,e) ;                                                                   }
	#endif
	//
	// name
	if (!( py_name && +*py_name )) return ;                                   // if no module => no deps
	::string name = *py_name->str() ;
	//
	// sfxs
	bool              has_sfxs = py_sfxs && +*py_sfxs           ;
	::vector_s        sfxs_    ;                                  if (has_sfxs) sfxs_ = _get_seq<false/*EmptyIsDot*/>("module_suffixes",*py_sfxs) ;
	::vector_s const& sfxs     = has_sfxs ? sfxs_ : s_std_sfxs  ;
	::string          tail     = name.substr(name.rfind('.')+1) ;
	::string          cwd_s_   ;
	for( ::string& dir : path ) {
		::string abs_dir_s = with_slash(::move(dir)) ;
		if (!is_abs(abs_dir_s)) {                                             // fast path : dont compute cwd unless required
			if (!cwd_s_) cwd_s_ = cwd_s() ;
			abs_dir_s = mk_glb(abs_dir_s,cwd_s_) ;
		}
		bool     is_lcl = abs_dir_s.starts_with(_g_autodep_env.repo_root_s) ;
		::string base   = abs_dir_s + tail                                  ;
		for( ::string const& sfx : is_lcl?sfxs:s_std_sfxs )                   // for external modules, use standard suffixes, not user provided suffixes, as these are not subject to local conventions
			if (+AcFd(base+sfx,{.err_ok=true})) return ;                      // found module, dont explore path any further
	}
}

static int _populate_mod(PyObject* py_mod) {
	Gil::s_swear_locked() ;
	//
	Module* mod = from_py<Module>(py_mod) ;
	try {
		Ptr<Tuple> py_ads { HAS_LD_AUDIT+3 } ;                               // PER_AUTODEP_METHOD : add entries here
		{	size_t i = 0 ;
			if (HAS_LD_AUDIT) py_ads->set_item( i++ , *Ptr<Str>("ld_audit"           ) ) ;
			/**/              py_ads->set_item( i++ , *Ptr<Str>("ld_preload"         ) ) ;
			/**/              py_ads->set_item( i++ , *Ptr<Str>("ld_preload_jemalloc") ) ;
			/**/              py_ads->set_item( i++ , *Ptr<Str>("ptrace"             ) ) ;
			SWEAR( i==py_ads->size() , i,py_ads->size() ) ;
		}
		//
		static constexpr const char* Bes[] = { "local" , "sge" , "slurm" } ; // PER_BACKEND : add entries here
		static constexpr size_t      NBes  = sizeof(Bes)/sizeof(Bes[0])    ;
		Ptr<Tuple> py_bes { NBes } ; for( size_t i : iota(NBes) ) py_bes->set_item(i,*Ptr<Str>(Bes[i])) ;
		//
		//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
		mod->set_attr( "top_repo_root" , *Ptr<Str>(no_slash(_g_autodep_env.repo_root_s                          )) ) ;
		mod->set_attr( "repo_root"     , *Ptr<Str>(no_slash(_g_autodep_env.repo_root_s+_g_autodep_env.sub_repo_s)) ) ;
		mod->set_attr( "backends"      , *py_bes                                                                   ) ;
		mod->set_attr( "autodeps"      , *py_ads                                                                   ) ;
		//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
		return 0 ;
	} catch (::string const& e) {
		py_err_set( PyException::RuntimeErr , e ) ;
		return -1 ;
	}
}
#if PY_MAJOR_VERSION>=3
	static int _populate_mod_no_gil(PyObject* py_mod) {
		NoGil no_gil ;                                                       // tell our mutex we already have the GIL
		return _populate_mod(py_mod) ;
	}
#endif

#pragma GCC visibility push(default)
PyMODINIT_FUNC
#if PY_MAJOR_VERSION>=3
	PyInit_clmake()
#else
	initclmake2()
#endif
{

	NoGil no_gil ; // tell our mutex we already have the GIL

	#define F(name,func,descr) { name , reinterpret_cast<PyCFunction>(func) , METH_VARARGS|METH_KEYWORDS , descr }
	static PyMethodDef s_methods[] = {
		F( "depend" , (_py_func<Object,_depend>) ,
			"depend(\n"
			"\t*deps\n"
			",\tfollow_symlinks=False\n"
			",\tread           =False # pretend deps are read in addition to setting flags\n"
			",\tregexpr        =False # deps are regexprs\n"
			",\tno_star        =True  # do not take regexpr-based flags into account\n"
			",\tverbose        =False # return a report as a dict { dep:(ok,checksum) for dep in deps} ok=True if dep ok, False if dep is in error, None if dep is out-of-date\n"
			"# flags :\n"
			",\tcritical       =False # if modified, ignore following deps\n"
			",\tessential      =False # show when generating user oriented graphs\n"
			",\tignore         =False # ignore deps, used to mask out further accesses\n"
			",\tignore_error   =False # dont propagate error if dep is in error (Error instead of Err because name is visible from user)\n"
			",\treaddir_ok     =True  # allow readdir on dep (implies required=False)\n"
			",\trequired       =True  # dep must be buildable\n"
			")\n"
			"Pretend parallel read of deps (if read==True) and mark them with flags mentioned as True.\n"
			"Flags accumulate and are never reset.\n"
		)
	,	F( "target" , _py_func<_target> ,
			"target(\n"
			"\t*targets\n"
			",\tregexpr     =False # targets are regexprs\n"
			",\twrite       =False # pretend targets are written in addition to setting flags\n"
			",\tno_star     =True  # do not take regexpr-based flags into account\n"
			"# flags :\n"
			",\tallow       =True  # writing to this target is allowed\n"
			",\tessential   =False # show when generating user oriented graphs\n"
			",\tignore      =False # ignore targets, used to mask out further accesses\n"
			",\tincremental =False # target is built incrementally, i.e. it is not unlinked before job execution\n"
			",\tno_uniquify =False # target is uniquified if it has several links and is incremental\n"
			",\tno_warning  =False # warn if target is either uniquified or unlinked and generated by another rule\n"
			",\tsource_ok   =False # ok to overwrite source files\n"
			"# in case targets turn out to be deps, the depend flags are available as well :\n"
			",\tcritical    =False # if modified, ignore following deps\n"
			",\tignore_error=False # dont propagate error if dep is in error (Error instead of Err because name is visible from user)\n"
			",\treaddir_ok  =True  # allow readdir on dep (implies required=False)\n"
			",\trequired    =True  # dep must be buildable\n"
			")\n"
			"Pretend write to targets (if write==True) and mark them with flags mentioned as True.\n"
			"Flags accumulate and are never reset.\n"
		)
	,	F( "check_deps" , (_py_func<Object,_chk_deps>) ,
			"check_deps(verbose=False)\n"
			"Ensure that all previously seen deps are up-to-date.\n"
			"Job will be killed in case some deps are not up-to-date.\n"
			"If verbose, wait for server reply. Return value is False if at least a dep is in error.\n"
			"This is necessary, even without checking return value, to ensure that after this call,\n"
			"the directories of previous deps actually exist if such deps are not read (such as with lmake.depend).\n"
		)
	,	F( "get_autodep" , (_py_func<Bool,_get_autodep>) ,
			"get_autodep()\n"
			"Return True if autodep is currenly activated (else False).\n"
		)
	,	F( "set_autodep" , _py_func<_set_autodep> ,
			"set_autodep(active,/)\n"
			"Activate (if active) or deactivate (if not active) autodep recording.\n"
		)
	,	F( "list_deps" , (_py_func<Tuple,_list<false/*Target*/>>) ,
			"list_deps( dir=None , regexpr=None )\n"
			"Return the list of deps in dir that match regexpr, as currently known.\n"
		)
	,	F( "list_targets" , (_py_func<Tuple,_list<true/*Target*/>>) ,
			"list_targets( dir=None , regexpr=None )\n"
			"Return the list of targets in dir that match regexpr, as currently known.\n"
		)
	,	F( "list_root" , (_py_func<Str,_list_root_s>) ,
			"list_root(dir)\n"
			"Return passed dir as used as prefix in list_deps and list_targets.\n"
		)
	,	F( "decode" , (_py_func<Str,_codec<false/*Encode*/>>) ,
			"decode(file,ctx,code)\n"
			"Return the associated (long) value passed by encode(file,ctx,val) when it returned (short) code.\n"
			"This call to encode must have been done before calling decode.\n"
		)
	,	F( "encode" , (_py_func<Str,_codec<true/*Encode*/>>) ,
			"encode(file,ctx,val,min_length=1)\n"
			"Return a (short) code associated with (long) val. If necessary create such a code of\n"
			"length at least min_length based on a checksum computed from value.\n"
			"val can be retrieve from code using decode(file,ctx,code),\n"
			"even from another job (as long as it is called after the call to encode).\n"
			"This means that decode(file,ctx,encode(file,ctx,val,min_length)) always return val for any min_length.\n"
		)
	,	F( "xxhsum_file" , (_py_func<Str,_xxhsum<true/*IsFile*/>>) ,
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
	,	F( "xxhsum" , (_py_func<Str,_xxhsum<false/*IsFile*/>>) ,
			"xxhsum(text)\n"
			"Return a checksum of provided text.\n"
			"It is a 16-digit hex value with no suffix.\n"
			"Note : the empty string lead to 0100000000000000 so as to be easily recognizable.\n"
			"Note : this checksum is not the same as the checksum of a file with same content.\n"
			"Note : this checksum is *not* crypto-robust.\n"
			"Cf man xxhsum for a description of the algorithm.\n"
		)
	,	F( "report_import" , _py_func<_report_import> ,
			"report_import(module_name=None,path=None)\n"
			"Inform autodep that module_name is (or is about to be) accessed.\n"
			"the enclosing package is supposed to have already been loaded (i.e. this function must be called for each package along the hierarchy).\n"
			"If module_name is None, only path is handled, but no dep is actually set as a result of importing a module.\n"
			"If path is None, sys.path is used instead.\n"
		)
	,	{nullptr,nullptr,0,nullptr}/*sentinel*/
	} ;
	#undef F
	try {
		_g_record = new Record { New , Yes/*enabled*/ } ;
	} catch (::string const& e) {
		#if PY_MAJOR_VERSION>=3
			return py_err_set( PyException::FileNotFoundErr , e ) ;
		#else
			py_err_set( PyException::OsErr , e ) ;                                                     // FileNotFoundErr does not exist with python2
			return ;
		#endif
	}
	//
	_g_autodep_env = New ;
	Gil::s_swear_locked() ;
	#if PY_MAJOR_VERSION>=3
		static PyModuleDef_Slot s_slots[] = {
			{ Py_mod_exec , reinterpret_cast<void*>(_populate_mod_no_gil) }
		,	{ 0           , nullptr                                       }                            // for Py_mod_gil if available
		,	{ 0           , nullptr                                       }
		} ;
		::string version = Py_GetVersion()                                     ;                       // version format is officially documented, so it may be safely analyzed
		size_t   dot     = version.find('.')                                   ;
		size_t   end     = dot+1                                               ; while ( version[end]>='0' && version[end]<='9' ) end++ ;
		int      major   = from_string<int>(version.substr(0    ,dot        )) ;
		int      minor   = from_string<int>(version.substr(dot+1,end-(dot+1))) ;
		//
		if (::pair(major,minor)<::pair(3,6)) return py_err_set( PyException::RuntimeErr , cat("python version (",major,'.',minor,") must be at least 3.6") ) ;
		#if defined(Py_mod_gil) && Py_GIL_DISABLED
			if (::pair(major,minor)>=::pair(3,13)) s_slots[1] = { Py_mod_gil , Py_MOD_GIL_NOT_USED } ; // starting at 3.13, free-threading is available
		#endif
		static PyModuleDef def {                                                                       // must have the lifetime of the module
			PyModuleDef_HEAD_INIT
		,	"clmake" /*m_name    */
		,	nullptr  /*m_doc     */
		,	0        /*m_size    */
		,	s_methods/*m_methods */
		,	s_slots  /*m_slots   */
		,	nullptr  /*m_traverse*/
		,	nullptr  /*m_clear   */
		,	nullptr  /*m_free    */
		} ;
		return PyModuleDef_Init(&def) ;
	#else
		Module* mod = from_py<Module>( Py_InitModule( "clmake2" , s_methods ) ) ;
		_populate_mod(mod) ;
	#endif
}
#pragma GCC visibility pop
