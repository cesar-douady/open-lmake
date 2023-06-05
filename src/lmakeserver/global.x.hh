// This file is part of the lmake distribution (https://wgit.doliam.net/cdy/lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "hash.hh"
#include "pycxx.hh"
#include "rpc_client.hh"
#include "serialize.hh"

#ifdef STRUCT_DECL
namespace Engine {

	struct ServerConfig  ;
	struct EngineClosure ;

	ENUM( Color
	,	None
	,	HiddenNote
	,	HiddenOk
	,	Note
	,	Ok
	,	Warning
	,	Err
	)

	ENUM( CmdVar
	,	Stem
	,	Target
	,	Dep
	,	Rsrc
	,	Stdout
	,	Stdin
	,	Stems
	,	Targets
	,	Deps
	,	Rsrcs
	,	Tokens
	)

	ENUM( EngineClosureKind
	,	Global
	,	Req
	,	Job
	)

	ENUM( GlobalProc
	,	None
	,	Int
	,	Wakeup
	)

	ENUM( JobEvent
	,	Submit                                                                 // job is submitted
	,	Add                                                                    // add a Req monitoring job
	,	Start                                                                  // job execution starts
	,	Done                                                                   // job done successfully
	,	Steady                                                                 // job done successfully
	,	Rerun                                                                  // job must rerun
	,	Killed                                                                 // job has been killed
	,	Del                                                                    // delete a Req monitoring job
	,	Err                                                                    // job done in error
	)

	ENUM( NodeEvent
	,	Done                                                                   // node was modified
	,	Steady                                                                 // node was remade w/o modification
	,	Uphill                                                                 // uphill dir was makable
	)

	ENUM( ReportBool
	,	No
	,	Yes
	,	Reported
	)

	ENUM( ServerConfigTag
	,	Local                                                                  // PER_BACKEND : add a tag for each backend
	)

}
#endif
#ifdef STRUCT_DEF
namespace Engine {

	struct Version {
		size_t major ;
		size_t minor ;
	} ;

	struct ServerConfig {
		friend ::ostream& operator<<( ::ostream& , ServerConfig const& ) ;

		using Tag = ServerConfigTag ;

		struct Backend {
			friend ::ostream& operator<<( ::ostream& , Backend const& ) ;
			// cxtors & casts
			Backend() = default ;
			Backend( Py::Mapping const& py_map , bool is_local ) ;
			// services
			template<IsStream T> void serdes(T& s) {
				::serdes(s,margin) ;
				::serdes(s,addr  ) ;
				::serdes(s,dct   ) ;
			}
			// data
			Time::Delay margin ;
			in_addr_t   addr   = ServerSockFd::LoopBackAddr ;
			::vmap_ss   dct    ;
		} ;

		// cxtors & casts
		ServerConfig() = default ;
		ServerConfig(Py::Mapping const& py_map) ;
		// services
		template<IsStream T> void serdes(T& s) {
			::serdes(s,db_version    ) ;                                       // must always stay first field to ensure it is always understood
			//
			::serdes(s,hash_algo     ) ;
			::serdes(s,heartbeat     ) ;
			::serdes(s,lnk_support   ) ;
			::serdes(s,max_dep_depth ) ;
			::serdes(s,trace_sz      ) ;
			::serdes(s,path_max      ) ;
			::serdes(s,sub_prio_boost) ;
			::serdes(s,backends      ) ;
			::serdes(s,colors        ) ;
		}
		::string pretty_str() const ;
		// data
		Version     db_version                                      = {}               ; // by default, db cannot be understood
		//
		Hash::Algo  hash_algo                                       = Hash::Algo::Xxh  ;
		Time::Delay heartbeat                                       ;
		LnkSupport  lnk_support                                     = LnkSupport::Full ;
		DepDepth    max_dep_depth                                   = 0                ; // uninitialized
		size_t      trace_sz                                        = 0                ;
		size_t      path_max                                        = 0                ; // if 0 <=> unlimited
		Prio        sub_prio_boost                                  = 0                ; // increment to add to prio when defined in a sub repository to boost local rules
		Backend     backends[+Tag::N]                               ;
		uint8_t     colors[+Color::N][2/*reverse_video*/][3/*RGB*/] = {}               ;
	} ;

	/**/          void audit( Fd out_fd , ::ostream& trace , ReqOptions const&    , Color   , DepDepth     , ::string const& pfx , ::string const& name={} , ::string const& sfx={} ) ;
	static inline void audit( Fd out_fd ,                    ReqOptions const& ro , Color c , DepDepth lvl , ::string const& pfx , ::string const& name={} , ::string const& sfx={} ) {
		OFakeStream fake ;
		audit( out_fd , fake , ro , c , lvl , pfx , name , sfx ) ;
	}

	template<class... A> static inline ::string title    ( ReqOptions const& , A&&... ) ;
	/**/                 static inline ::string color_pfx( ReqOptions const& , Color  ) ;
	/**/                 static inline ::string color_sfx( ReqOptions const& , Color  ) ;

}
#endif
#ifdef DATA_DEF
namespace Engine {

	struct EngineClosure {
		using Kind = EngineClosureKind ;
		using Req_ = Engine::Req       ;
		using Job_ = Engine::Job       ;
		using GP = GlobalProc     ;
		using RP = ReqProc        ;
		using JP = JobProc        ;
		using K  = Kind           ;
		using RO = ReqOptions     ;
		using VN = ::vector<Node> ;
		friend ::ostream& operator<<( ::ostream& , EngineClosure const& ) ;
		//
		struct Req {
			friend ::ostream& operator<<( ::ostream& , Req const& ) ;
			// accesses
			bool has_targets() const {
				switch (proc) {
					case RP::Forget :
					case RP::Freeze :
					case RP::Make   :
					case RP::Show   : return true  ;
					default         : return false ;
				}
			}
			// data
			ReqProc        proc    = RP::None ;
			Req_           req     = {}       ;            // if proc== Close
			Fd             in_fd   = {}       ;            // if proc==          Forget or Kill or Make or Show
			Fd             out_fd  = {}       ;            // if proc==          Forget or Kill or Make or Show
			::vector<Node> targets = {}       ;            // if proc==          Forget or         Make or Show
			ReqOptions     options = {}       ;            // if proc==          Forget or         Make or Show
		} ;
		struct Job {
			friend ::ostream& operator<<( ::ostream& , Job const& ) ;
			JobProc        proc          = JP::None ;
			Job_           job           ;
			bool           report        = false    ;      // if proc==Start
			::vector<Node> report_unlink = {}       ;      // if proc==Start
			::string       txt           = {}       ;      // if proc==LiveOut
			Req_           req           = {}       ;      // if proc==Continue
			ProcessDate    start         = {}       ;      // if proce==End
			JobDigest      digest        = {}       ;      // if proc==End
			Fd             reply_fd      = {}       ;      // if proc==ChkDeps
		} ;
		// cxtors & casts
		EngineClosure(GlobalProc p) : kind{Kind::Global} , global_proc{p} {}
		//
		EngineClosure(RP p,Fd ifd,Fd ofd,VN const& ts,RO const& ro) : kind{K::Req},req{.proc=p,.in_fd=ifd,.out_fd=ofd,.targets=ts,.options=ro} { SWEAR(req.has_targets()) ; }
		EngineClosure(RP p,Fd ifd,Fd ofd                          ) : kind{K::Req},req{.proc=p,.in_fd=ifd,.out_fd=ofd                        } { SWEAR(p==RP::Kill )      ; }
		EngineClosure(RP p,Req_ r                                 ) : kind{K::Req},req{.proc=p,.req=r                                        } { SWEAR(p==RP::Close)      ; }
		//
		EngineClosure( JP p , Job_ j , ::vector<Node> const& ru , bool r ) : kind{K::Job} , job{.proc=p,.job=j,.report=r,.report_unlink=ru} { SWEAR( p==JP::Start                            ) ; }
		EngineClosure( JP p , Job_ j , ::string const& t                 ) : kind{K::Job} , job{.proc=p,.job=j,.txt   =t                  } { SWEAR( p==JP::LiveOut                          ) ; }
		EngineClosure( JP p , Job_ j , Req_            r                 ) : kind{K::Job} , job{.proc=p,.job=j,.req   =r                  } { SWEAR( p==JP::Continue                         ) ; }
		EngineClosure( JP p , Job_ j , Status          s                 ) : kind{K::Job} , job{.proc=p,.job=j,.digest={.status=s}        } { SWEAR( p==JP::End && s<=Status::Garbage        ) ; }
		EngineClosure( JP p , Job_ j , ProcessDate s , JobDigest&& jd    ) : kind{K::Job} , job{.proc=p,.job=j,.start=s,.digest=::move(jd)} { SWEAR( p==JP::End                              ) ; }
		EngineClosure( JP p , Job_ j                                     ) : kind{K::Job} , job{.proc=p,.job=j                            } { SWEAR( p==JP::ReportStart || p==JP::NotStarted ) ; }
		//
		EngineClosure( JP p , Job_ j , ::vmap_s<DepDigest>&& dds , Fd rfd ) : kind{K::Job} , job{.proc=p,.job=j,.digest={.deps{::move(dds)}},.reply_fd=rfd} {
			SWEAR( p==JP::DepCrcs || p==JP::ChkDeps ) ;
		}
		//
		EngineClosure(EngineClosure&& ec) : kind(ec.kind) {
			switch (ec.kind) {
				case K::Global : new(&global_proc) GlobalProc{::move(ec.global_proc)} ; break ;
				case K::Req    : new(&req        ) Req       {::move(ec.req        )} ; break ;
				case K::Job    : new(&job        ) Job       {::move(ec.job        )} ; break ;
				default : FAIL(ec.kind) ;
			}
		}
		~EngineClosure() {
			switch (kind) {
				case K::Global : global_proc.~GlobalProc() ; break ;
				case K::Req    : req        .~Req       () ; break ;
				case K::Job    : job        .~Job       () ; break ;
				default : FAIL(kind) ;
			}
		}
		EngineClosure& operator=(EngineClosure const&  ec) = delete ;
		EngineClosure& operator=(EngineClosure      && ec) = delete ;
		// data
		Kind kind = K::Global ;
		union {
			GlobalProc global_proc = GP::None ;            // if kind==Global
			Req        req         ;
			Job        job         ;
		} ;
	} ;

	extern ThreadQueue<EngineClosure> g_engine_queue ;

}
#endif
#ifdef IMPL
namespace Engine {

	template<class... A> static inline ::string title( ReqOptions const& ro , A&&... args ) {
		if (ro.reverse_video==Maybe) return {} ;
		return to_string( "\x1b]0;" , ::forward<A>(args)... , '\a' ) ;
	}

	static inline ::string color_pfx( ReqOptions const& ro , Color color ) {
		Bool3 rv = ro.reverse_video ;
		if ( color==Color::None || rv==Maybe ) return {} ;
		uint8_t const* colors = g_config.colors[+color][rv==Yes] ;
		return to_string( "\x1b[38;2;" , int(colors[0]) ,';', int(colors[1]) ,';', int(colors[2]) , 'm' ) ;
	}

	static inline ::string color_sfx(ReqOptions const& ro , Color color ) {
		if ( color==Color::None || ro.reverse_video==Maybe ) return {} ;
		return "\x1b[0m" ;
	}

}
#endif
