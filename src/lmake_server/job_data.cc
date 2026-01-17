// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2026 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#include "core.hh" // /!\ must be first to include Python.h first

#include "rpc_job.hh"
#include "rpc_job_exec.hh"

using namespace Disk ;
using namespace Hash ;

enum class NoRunReason : uint8_t {
	None
,	Dep            // dont run because deps are not new
,	RunLoopReq     // dont run because Req  job run        limit is reached
,	RunLoopRule    // dont run because Rule job run        limit is reached
,	SubmitLoopReq  // dont run because Req  job submission limit is reached
,	SubmitLoopRule // dont run because Rule job submission limit is reached
,	RetryLoop      // dont run because job retry           limit is reached
,	LostLoop       // dont run because job lost            limit is reached
} ;

//
// codec
//

namespace Codec {

	using namespace Engine ;

	::strong_ordering operator<=>( Entry const& a , Entry const& b ) {
		if ( ::strong_ordering r=a.ctx<=>b.ctx ; r!=0 ) return r               ;
		/**/                                            return a.code<=>b.code ;
	}

	static ::string _manifest_file(::string const& file) {
		return cat(g_config->local_admin_dir_s,"codec/",file,"/manifest") ;
	}

	static FileNameIdx _code_prio( ::string const& code , ::string const& crc ) {
		static_assert( 2*PATH_MAX<=Max<FileNameIdx> ) ;                           // ensure highest possible value fits in range
		SWEAR( code.size()<=PATH_MAX , code ) ;
		if ( +code && crc.starts_with(code) ) return PATH_MAX*1-code.size() ;     // an automatic code, not as good as a user provided one
		else                                  return PATH_MAX*2-code.size() ;     // prefer shorter codes
	}

	static ::umap_s/*ctx*/<::umap_s/*code*/<Crc>> _prepare_old_decode_tab(::string const& file_name) {
		::umap_s/*ctx*/<::umap_s/*code*/<Crc>> res ;
		::string                               ctx ;
		Trace trace("_prepare_old_decode_tab",file_name) ;
		for( ::string const& line : AcFd(_manifest_file(file_name),{.err_ok=true}).read_lines(false/*partial_ok*/) ) {
			if (!line) continue ;
			if (line[0]!='\t') {
				ctx = parse_printable(line) ;
			} else {
				size_t   pos  = 1                                   ;                                       // skip initial \t
				::string code = parse_printable(line,pos          ) ; SWEAR( line[pos]=='\t' , pos,line ) ;
				Crc      crc  = Crc::s_from_hex(line.substr(pos+1)) ;
				res[ctx].try_emplace( ::move(code) , crc ) ;
			}
		}
		trace("done",res.size()) ;
		return res ;
	}

	static void _update_old_decode_tab( ::string const& file_name , ::string const& new_codes_file_name , ::umap_s/*ctx*/<::umap_s/*code*/<Crc>>&/*inout*/ old_decode_tab ) {
		Trace trace("_update_old_decode_tab",file_name,new_codes_file_name,old_decode_tab.size()) ;
		for( ::string const& line : AcFd(new_codes_file_name,{.err_ok=true}).read_lines(false/*partial_ok*/) ) {
			Codec::Entry entry ;
			try                     { entry = {line} ; }
			catch (::string const&) { continue ;       }
			bool inserted = old_decode_tab[entry.ctx].try_emplace( ::move(entry.code) , Crc(New,entry.val) ).second ;
			SWEAR( inserted , file_name,line ) ;                                                                      // there must be no internal conflict
		}
		trace("done",old_decode_tab.size()) ;
	}

	static void _do_file( ::string const& file_name , bool do_new_codes , ::map_s/*ctx*/<::map_ss/*val->code*/>&/*inout*/ encode_tab , Bool3&/*inout*/ has_new_codes ) {
		::vector_s   lines      = AcFd(file_name,{.err_ok=do_new_codes}).read_lines(false/*partial_ok*/) ;                                                               // new codes may not exist
		Codec::Entry prev_entry ;
		Trace trace("_do_file",file_name,STR(do_new_codes),encode_tab.size(),has_new_codes,lines.size()) ;
		for( ::string const& line : lines ) {
			Codec::Entry entry ;
			try {
				entry = {line} ;
			} catch (::string const&) {
				trace("bad_format",line) ;
				if (!do_new_codes) has_new_codes |= Maybe ;
				continue ;
			}
			//
			bool first         = !encode_tab                                             ;
			auto [it,inserted] = encode_tab[entry.ctx].try_emplace(entry.val,entry.code) ;
			if (inserted) {
				if (do_new_codes) {
					has_new_codes = Yes ;
				} else {
					if ( has_new_codes==No && !first && prev_entry>=entry ) { has_new_codes = Maybe ; trace("wrong_order",prev_entry,entry) ; }
					prev_entry = ::move(entry) ;
				}
			} else {
				if (it->second==entry.code) {
					trace("duplicate",line) ;
					if (!do_new_codes) has_new_codes |= Maybe ;
				} else if (do_new_codes) {
					trace("val_conflict",it->second,entry.code,"keep") ;
				} else {
					has_new_codes |= Maybe ;
					::string crc = Crc(New,entry.val).hex() ;
					if (_code_prio(entry.code,crc)>_code_prio(it->second,crc)) { trace("val_conflict",it->second,entry.code,"keep"  ) ; it->second = entry.code ; }      // keep best code
					else                                                         trace("val_conflict",it->second,entry.code,"forget") ;
				}
			}
		}
		trace("done",encode_tab.size(),has_new_codes) ;
	}

	static ::map_s/*ctx*/<::map_ss/*val->code*/> _prepare_encode_tab( ::string const& file_name , Bool3&/*out*/ has_new_codes ) {
		::map_s/*ctx*/<::map_ss/*val->code*/> res ;
		has_new_codes = No ;
		_do_file( file_name , false/*do_new_codes*/ , /*inout*/res , /*inout*/has_new_codes ) ;
		return res ;
	}

	static void _update_encode_tab( ::string const& new_codes_file_name , ::map_s/*ctx*/<::map_ss/*val->code*/>&/*inout*/ encode_tab , Bool3&/*inout*/ has_new_codes ) {
		_do_file( new_codes_file_name , true/*do_new_codes*/ , /*inout*/encode_tab , /*inout*/has_new_codes ) ;
	}

	static ::map_s/*ctx*/<::map_ss/*code->val*/> _mk_decode_tab(::map_s/*ctx*/<::map_ss/*val->code*/> const& encode_tab) {
		Trace trace("_mk_decode_tab",encode_tab.size()) ;
		::map_s/*ctx*/<::map_ss/*code->val*/> res ;
		// create decode_tab and disambiguate in case the same code is used for the several vals
		for( auto const& [ctx,e_entry] : encode_tab ) {
			::map_ss&                            d_entry = res[ctx] ;
			::umap_s/*code*/<::vector_s/*vals*/> clashes ;
			for( auto const& [val,code] : e_entry )
				if (!d_entry.try_emplace(code,val).second)
					clashes[code].push_back(val) ;
			if (+clashes) {
				for( auto const& [code,vals] : clashes )
					for( ::string const& val : vals ) {
						::string crc      = Crc(val).hex()                ;
						uint8_t  d        = ::min(code.size(),crc.size()) ; while (!code.ends_with(substr_view(crc,0,d))) d-- ;
						::string new_code = code                          ; new_code.reserve(code.size()+1) ;                   // most of the time, adding a single char is enough
						for( char c : substr_view(crc,d) ) {
							new_code.push_back(c) ;
							if (d_entry.try_emplace(new_code,val).second) goto FoundNewCode ;
						}
						FAIL("codec checksum clash for code",code,crc,val) ;                                                    // NO_COV
					FoundNewCode : ;
					}
			}
		}
		trace("done",res.size()) ;
		return res ;
	}

	static Crc _refresh_codec_file( ::string const& file_name , ::map_s/*ctx*/<::map_ss/*code->val*/> const& decode_tab ) {
		::string lines   ;
		size_t   n_lines = 0 ;
		for( auto const& [ctx,d_entry] : decode_tab )
			for( auto const& [code,val] : d_entry ) {
				lines << Entry(ctx,code,val).line(true/*with_nl*/) ;
				n_lines++ ;
			}
		Trace trace("_refresh_codec_file",file_name,n_lines) ;
		AcFd( file_name , {O_WRONLY|O_TRUNC,0666/*mod*/} ).write( lines ) ;
		return { New , lines , No/*is_lnk*/ } ;
	}

}

namespace Engine {

	//
	// thread-safe
	//

	::vmap<Node,FileAction> JobData::pre_actions( Rule::RuleMatch const& match , bool no_incremental , bool mark_target_dirs ) const {
		Trace trace("pre_actions",idx(),STR(mark_target_dirs)) ;
		::uset<Node>                  to_mkdirs          = match.target_dirs() ;
		::uset<Node>                  to_mkdir_uphills   ;
		::uset<Node>                  target_locked_dirs ;
		::umap<Node,NodeIdx/*depth*/> to_rmdirs          ;
		::vmap<Node,FileAction>       actions            ;
		for( Node d : to_mkdirs )
			for( Node hd=d->dir ; +hd ; hd=hd->dir )
				if (!to_mkdir_uphills.insert(hd).second) break ;
		//
		// remove old targets
		for( Target t : targets() ) {
			bool          incremental = t.tflags[Tflag::Incremental] && (!t.tflags[Tflag::Target]||!no_incremental) ;
			FileActionTag fat         = {}/*garbage*/                                                               ;
			t->set_buildable() ;
			if      (  t->crc==Crc::None                            ) fat = FileActionTag::None           ; // nothing to wash
			else if (  t->is_src()                                  ) fat = FileActionTag::Src            ; // dont touch sources, not even integrity check
			else if ( +t->polluted       && t.tflags[Tflag::Target] ) fat = FileActionTag::UnlinkPolluted ; // wash     polluted targets
			else if ( +t->polluted       && !incremental            ) fat = FileActionTag::UnlinkPolluted ; // wash     polluted non-incremental
			else if (                       !incremental            ) fat = FileActionTag::Unlink         ; // wash non-polluted non-incremental
			else                                                      fat = FileActionTag::Uniquify       ;
			//
			FileAction fa { .tag=fat , .tflags=t.tflags , .crc=t->crc , .sig=t->sig.sig } ;
			//
			trace("wash_target",t,fa) ;
			switch (fat) {
				case FileActionTag::Src      : if ( +t->dir && t->crc!=Crc::None ) target_locked_dirs.insert(t->dir) ;                              break ; // no action, not even integrity check
				case FileActionTag::Uniquify : if ( +t->dir                      ) target_locked_dirs.insert(t->dir) ; actions.emplace_back(t,fa) ; break ;
				case FileActionTag::Unlink :
					if ( !t->has_actual_job(idx()) && t->has_actual_job() && !t.tflags[Tflag::NoWarning] ) fa.tag = FileActionTag::UnlinkWarning ;
				[[fallthrough]] ;
				case FileActionTag::UnlinkPolluted :
				case FileActionTag::None           :
					actions.emplace_back(t,fa) ;
					if ( Node td=t->dir ; +td ) {
						Lock    lock  { _s_target_dirs_mutex } ;
						NodeIdx depth = 0                      ;
						for( Node hd=td ; +hd ; (hd=hd->dir),depth++ )
							if (_s_target_dirs.contains(hd)) goto NextTarget ; // everything under a protected dir is protected, dont even start walking from td
						for( Node hd=td ; +hd ; hd=hd->dir ) {
							if (_s_hier_target_dirs.contains(hd)) break ;      // dir is protected
							if (target_locked_dirs .contains(hd)) break ;      // dir contains a target => little hope and no desire to remove it
							if (to_mkdirs          .contains(hd)) break ;      // dir must exist, it is silly to spend time to rmdir it, then again to mkdir it
							if (to_mkdir_uphills   .contains(hd)) break ;      // .
							//
							if (!to_rmdirs.emplace(td,depth).second) break ;   // if it is already in to_rmdirs, so is all pertinent dirs uphill
							depth-- ;
						}
					}
				break ;
			DF}                                                                // NO_COV
		NextTarget : ;
		}
		// make target dirs
		for( Node d : to_mkdirs ) {
			if (to_mkdir_uphills.contains(d)) continue ;                       // dir is a dir of another dir => it will be automatically created
			actions.emplace_back( d , ::FileAction({FileActionTag::Mkdir}) ) ; // note that protected dirs (in _s_target_dirs and _s_hier_target_dirs) may not be created yet, so mkdir them to be sure
		}
		// rm enclosing dirs of unlinked targets
		::vmap<Node,NodeIdx/*depth*/> to_rmdir_vec ; for( auto [k,v] : to_rmdirs ) to_rmdir_vec.emplace_back(k,v) ;
		::sort( to_rmdir_vec , [&]( auto const& a , auto const& b ) { return a.second>b.second ; } ) ;              // sort deeper first, to rmdir after children
		for( auto [d,_] : to_rmdir_vec ) actions.emplace_back(d,FileAction({FileActionTag::Rmdir})) ;
		//
		// mark target dirs to protect from deletion by other jobs
		// this must be perfectly predictible as this mark is undone in end_exec below
		if (mark_target_dirs) {
			Lock lock{_s_target_dirs_mutex} ;
			for( Node d : to_mkdirs        ) { trace("protect_dir"     ,d) ; _s_target_dirs     [d]++ ; }
			for( Node d : to_mkdir_uphills ) { trace("protect_hier_dir",d) ; _s_hier_target_dirs[d]++ ; }
		}
		return actions ;
	}

	void JobData::end_exec() const {
		Trace trace("end_exec",idx()) ;
		::uset<Node> dirs        = rule_match().target_dirs() ;
		::uset<Node> dir_uphills ;
		for( Node d : dirs )
			for( Node hd=d->dir ; +hd ; hd=hd->dir )
				if (!dir_uphills.insert(hd).second) break ;
		//
		auto dec = [&]( ::umap<Node,Idx/*cnt*/>& dirs , Node d ) {
			auto it = dirs.find(d) ;
			SWEAR(it!=dirs.end()) ;
			if (it->second==1) dirs.erase(it) ;
			else               it->second--   ;
		} ;
		Lock lock(_s_target_dirs_mutex) ;
		for( Node d : dirs        ) { trace("unprotect_dir"     ,d) ; dec(_s_target_dirs     ,d) ; }
		for( Node d : dir_uphills ) { trace("unprotect_hier_dir",d) ; dec(_s_hier_target_dirs,d) ; }
	}

	//
	// main thread
	//

	Mutex<MutexLvl::TargetDir      > JobData::_s_target_dirs_mutex ;
	::umap<Node,JobData::Idx/*cnt*/> JobData::_s_target_dirs       ;
	::umap<Node,JobData::Idx/*cnt*/> JobData::_s_hier_target_dirs  ;

	::string JobData::unique_name() const {
		Rule     r         = rule()                       ;
		::string fn        = full_name()                  ; r->validate(fn) ;                                          // only name suffix is considered to make Rule
		size_t   user_sz   = fn.size() - r->job_sfx_len() ;
		::string res       = fn.substr(0,user_sz)         ; res.reserve(res.size()+1+r->n_static_stems*(2*(3+1))+16) ; // allocate 2x3 digits per stem, this is comfortable
		//
		for( char& c : res ) if (c==Rule::StarMrkr) c = '*' ;
		res.push_back('/') ;
		//
		char* p = &fn[user_sz+1] ;                                                                                     // start of suffix
		for( [[maybe_unused]] VarIdx _ : iota(r->n_static_stems) ) {
			FileNameIdx pos = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			FileNameIdx sz  = decode_int<FileNameIdx>(p) ; p += sizeof(FileNameIdx) ;
			res << pos << '-' << sz << '+' ;
		}
		res << "rule-" << r->crc->cmd.hex() ;
		return res ;
	}

	void JobData::_reset_targets(Rule::RuleMatch const& match) {
		SWEAR( match.rule->special>=Special::HasMatches , idx(),match,match.rule,match.rule->special ) ;
		//
		Rule             r     = rule()                       ;
		::vector<Target> ts    ;                                ts.reserve(r->matches_iotas[false/*star*/][+MatchKind::Target].size()) ; // there are usually no duplicates
		::vector_s       sts   = match.targets(false/*star*/) ;
		VarIdx           i     = 0                            ;
		::uset_s         seens ;
		for( VarIdx mi : r->matches_iotas[false/*star*/][+MatchKind::Target] ) {
			::string const& t = sts[i++] ;
			if (!seens.insert(t).second) continue ;                                                                                      // remove duplicates
			ts.emplace_back( Node(New,t) , r->tflags(mi) ) ;
		}
		::sort(ts) ;                                                                                                                     // ease search in targets
		targets().assign(ts) ;
	}

	void JobData::_do_set_pressure(ReqInfo& ri , CoarseDelay pressure ) const {
		Trace trace("set_pressure",idx(),ri,pressure) ;
		g_kpi.n_job_set_pressure++ ;
		//
		Req         req          = ri.req                   ;
		CoarseDelay dep_pressure = ri.pressure + exe_time() ;
		switch (ri.step()) { //!                                                             vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			case JobStep::Dep    : for( DepsIter it{deps,ri.iter } ; it!=deps.end() ; it++ ) (*it)->    set_pressure( (*it)->req_info(req) ,               dep_pressure  ) ; break ;
			case JobStep::Queued :                                                           Backend::s_set_pressure( backend , +idx() , +req , {.pressure=dep_pressure} ) ; break ;
		DN} //!                                                                              ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
	}

	static JobReasonTag _mk_pre_reason(Status s) {
		static constexpr ::amap<Status,JobReasonTag,N<Status>> ReasonTab {{
			{ Status::New          , JobReasonTag::New             }
		,	{ Status::EarlyChkDeps , JobReasonTag::ChkDeps         }
		,	{ Status::EarlyErr     , JobReasonTag::Retry           }
		,	{ Status::EarlyLost    , JobReasonTag::Lost            }                                                 // becomes WasLost if end
		,	{ Status::EarlyLostErr , JobReasonTag::LostRetry       }
		,	{ Status::LateLost     , JobReasonTag::Lost            }                                                 // becomes WasLost if end
		,	{ Status::LateLostErr  , JobReasonTag::LostRetry       }
		,	{ Status::Killed       , JobReasonTag::Killed          }
		,	{ Status::ChkDeps      , JobReasonTag::ChkDeps         }
		,	{ Status::CacheMatch   , JobReasonTag::CacheMatch      }
		,	{ Status::BadTarget    , JobReasonTag::PollutedTargets }
		,	{ Status::Ok           , JobReasonTag::None            }
		,	{ Status::RunLoop      , JobReasonTag::None            }
		,	{ Status::SubmitLoop   , JobReasonTag::None            }
		,	{ Status::Err          , JobReasonTag::Retry           }
		}} ;
		static auto no_node = []()->bool { { for( auto [_,v] : ReasonTab ) if (v>=JobReasonTag::HasNode) return false ; } return true ; } ;
		static_assert(chk_enum_tab(ReasonTab)) ;
		static_assert(no_node()              ) ;
		return ReasonTab[+s].second ;
	}
	JobReason JobData::make( ReqInfo& ri , MakeAction make_action , JobReason asked_reason , Bool3 speculate , bool wakeup_watchers ) {
		using Step = JobStep ;
		static constexpr Dep Sentinel { false/*parallel*/ } ;                                                        // used to clean up after all deps are processed
		Trace trace("Jmake",idx(),ri,make_action,asked_reason,speculate,STR(wakeup_watchers)) ;
		//
		// in case we are a DepDirect, we want to pop ourselves when done, but only once this method is fully done
		// by declaring this variable first, its dxtor is executed last
		struct ToPop {
			~ToPop() {
				if (+job) job.pop(req) ;
			}
			Job job ;
			Req req ;
		} ;
		ToPop to_pop ;
		//
		SWEAR( asked_reason.tag<JobReasonTag::Err,asked_reason) ;
		Rule              r            = rule()                                              ;
		bool              query        = make_action==MakeAction::Query                      ;
		bool              at_end       = make_action==MakeAction::End                        ;
		Req               req          = ri.req                                              ;
		ReqOptions const& ro           = req->options                                        ;
		Special           special      = r->special                                          ;
		bool              dep_live_out = special==Special::Req && ro.flags[ReqFlag::LiveOut] ;
		CoarseDelay       dep_pressure = ri.pressure + c_exe_time()                          ;
		bool              archive      = ro.flags[ReqFlag::Archive]                          ;
		bool              report_loop  = false                                               ;
		//
	RestartFullAnalysis :
		JobReason pre_reason    ;                                                                                    // reason to run job when deps are ready before deps analysis
		JobReason report_reason ;
		auto reason = [&](ReqInfo::State const& s) {
			if (ri.force) return pre_reason | ri.reason | s.reason             ;
			else          return pre_reason |             s.reason | ri.reason ;
		} ;
		// /!\ no_run_reason_tag and inc_submits must stay in sync
		auto no_run_reason_tag = [&](JobReasonTag const& jrt) {                                                      // roughly equivalent to !jrt||jrt>=Err but give reason and take care of limits
			switch (jrt) {
				case JobReasonTag::None      :                             return NoRunReason::Dep ;
				case JobReasonTag::Retry     :
				case JobReasonTag::LostRetry :                             goto Retry              ;
				default                      : if (jrt>=JobReasonTag::Err) return NoRunReason::Dep ;
			}
			switch (pre_reason.tag) {
				case JobReasonTag::Lost      :             goto Lost   ;
				case JobReasonTag::LostRetry : if (at_end) goto Retry  ; [[fallthrough]] ;                           // retry if lost error (other reasons are not reliable)
				default                      :             goto Submit ;
			}
			Retry  : return ri.n_retries>=req->n_retries ? NoRunReason::RetryLoop : NoRunReason::None ;
			Lost   : return ri.n_losts  >=r  ->n_losts   ? NoRunReason::LostLoop  : NoRunReason::None ;
			Submit :
				if ( r  ->n_runs    && ri.n_runs   >=r  ->n_runs    ) return NoRunReason::RunLoopRule    ;
				if ( req->n_runs    && ri.n_runs   >=req->n_runs    ) return NoRunReason::RunLoopReq     ;
				if ( r  ->n_submits && ri.n_submits>=r  ->n_submits ) return NoRunReason::SubmitLoopRule ;
				if ( req->n_submits && ri.n_submits>=req->n_submits ) return NoRunReason::SubmitLoopReq  ;
				/**/                                                  return NoRunReason::None           ;
		} ;
		auto no_run_reason = [&](ReqInfo::State const& s) {
			return no_run_reason_tag(reason(s).tag) ;
		} ;
		// /!\ no_run_reason_tag and inc_submits must stay in sync
		auto inc_submits = [&]( JobReasonTag jrt , bool has_run ) {                                                  // inc counter associated with no_run_reason_tag (returning None)
			NoRunReason nrr = no_run_reason_tag(jrt) ;
			SWEAR( !nrr , jrt,pre_reason,nrr ) ;
			switch (jrt) {
				case JobReasonTag::Retry     :
				case JobReasonTag::LostRetry : ri.n_retries++ ; return ;
			DN}
			switch (pre_reason.tag) {
				case JobReasonTag::Lost      :             ri.n_losts  ++ ;                        break ;
				case JobReasonTag::LostRetry : if (at_end) ri.n_retries++ ;                        [[fallthrough]] ; // retry if lost error (other reasons are not reliable)
				default                      :             ri.n_submits++ ; ri.n_runs += has_run ;
			}
		} ;
		switch (make_action) {
			case MakeAction::End    : ri.reset(idx(),true/*has_run*/) ; [[fallthrough]] ;                            // deps have changed
			case MakeAction::Wakeup : ri.dec_wait()                   ; break           ;
			case MakeAction::GiveUp : ri.dec_wait()                   ; goto Done       ;
		DN}
		if (+asked_reason) {
			if (ri.state.missing_dsk) { trace("reset",asked_reason) ; ri.reset(idx()) ; }
			ri.reason |= asked_reason ;
		}
		ri.speculate = ri.speculate & speculate ;           // cannot use &= with bit fields
		if (ri.done()) {
			if (!reason(ri.state).need_run()) goto Wakeup ;
			if (req.zombie()                ) goto Wakeup ;
			/**/                              goto Run    ;
		} else {
			if (ri.waiting()                ) goto Wait   ; // we may have looped in which case stats update is meaningless and may fail()
			if (req.zombie()                ) goto Done   ;
			if (idx().frozen()              ) goto Run    ; // ensure crc are updated, akin sources
			if (is_infinite(special)        ) goto Run    ; // special case : Infinite's actually have no dep, just a list of node showing infinity
		}
		if (ri.step()==Step::None) {
			estimate_stats() ;                              // initial guestimate to accumulate waiting costs while resources are not fully known yet
			ri.step(Step::Dep,idx()) ;
			JobReasonTag jrt = {} ;
			if      ( r->force                                                                         ) jrt = JobReasonTag::Force  ;
			else if ( !cmd_ok()                                                                        ) jrt = JobReasonTag::Cmd    ;
			else if ( (ro.flags[ReqFlag::ForgetOldErrors]&&err()) || (is_lost(status)&&!is_ok(status)) ) jrt = JobReasonTag::OldErr ; // probably a transient error
			else if ( !rsrcs_ok()                                                                      ) jrt = JobReasonTag::Rsrcs  ; // probably a resource  error
			else                                                                                         goto NoReason ;
			ri.reason              = jrt  ;
			ri.force               = true ;
			ri.state.proto  .modif = true ;                                                           // ensure we can copy proto_modif to stamped_modif anytime when pertinent
			ri.state.stamped.modif = true ;
		NoReason : ;
		}
		g_kpi.n_job_make++ ;
		SWEAR(ri.step()==Step::Dep) ;
		{
		RestartAnalysis :                                                                             // restart analysis here when it is discovered we need deps to run the job
			bool           proto_seen_waiting    = false    ;
			bool           stamped_seen_waiting  = false    ;
			bool           proto_seen_critical   = false    ;                                         // seen critical modif or error or waiting
			bool           stamped_seen_critical = false    ;
			bool           sure                  = true     ;
			ReqInfo::State state                 = ri.state ;
			//
			ri.speculative_wait = false                  ;                                            // initially, we are not waiting at all
			report_reason       = {}                     ;
			if ( incremental && ro.flags[ReqFlag::NoIncremental] ) pre_reason  = JobReasonTag::WasIncremental ;
			/**/                                                   pre_reason |= _mk_pre_reason(status)       ;
			if ( pre_reason.tag==JobReasonTag::Lost && !at_end   ) pre_reason  = JobReasonTag::WasLost        ;
			trace("pre_reason",pre_reason) ;
			for( DepsIter iter {deps,ri.iter} ;; iter++ ) {
				bool       seen_all = iter==deps.end()            ;
				Dep const& dep      = seen_all ? Sentinel : *iter ;                                   // use empty dep as sentinel
				//
				if (!dep.parallel) {
					state.stamped.err     = state.proto.err     ;                                     // proto become stamped upon sequential dep
					state.stamped.modif   = state.proto.modif   ;                                     // .
					stamped_seen_waiting  = proto_seen_waiting  ;
					stamped_seen_critical = proto_seen_critical ;
					if ( query && (stamped_seen_waiting||state.stamped.modif||+state.stamped.err) ) { // no reason to analyze any further, we have the answer
						report_reason = reason(ri.state) ;
						goto Return ;
					}
				}
				if (!proto_seen_waiting) {
					ri.iter  = iter.digest(deps) ;                                                    // fast path : info is recorded in ri, next time, restart analysis here
					ri.state = state             ;                                                    // .
				}
				if (seen_all             ) break ;
				if (stamped_seen_critical) break ;
				NodeData &         dnd         = *Node(dep)                                   ;
				bool               dep_modif   = false                                        ;
				RunStatus          dep_err     = RunStatus::Ok                                ;
				bool               is_static   =  dep.dflags[Dflag::Static     ]              ;
				bool               required    =  dep.dflags[Dflag::Required   ]              ;
				bool               sense_err   = !dep.dflags[Dflag::IgnoreError]              ;
				bool               is_critical = +dep.accesses && dep.dflags[Dflag::Critical] ;
				bool               modif       = state.stamped.modif || ri.force              ;
				bool               may_care    = +dep.accesses || (modif&&is_static)          ;       // if previous modif, consider static deps as fully accessed, as initially
				NodeReqInfo const* cdri        = &dep->c_req_info(req)                        ;       // avoid allocating req_info as long as not necessary
				NodeReqInfo      * dri         = nullptr                                      ;       // .
				NodeGoal           dep_goal    =
					query                                        ? NodeGoal::Dsk
				:	(may_care&&!no_run_reason(state)) || archive ? NodeGoal::Dsk
				:	may_care || sense_err                        ? NodeGoal::Status
				:	is_static || required                        ? NodeGoal::Status
				:	                                               NodeGoal::None
				;
				if (!dep_goal) continue ;                                                             // this is not a dep (not static while asked for makable only)
			RestartDep :
				if (!cdri->waiting()) {
					ReqInfo::WaitInc sav_n_wait { ri } ;                                              // appear waiting in case of recursion loop (loop will be caught because of no job on going)
					if (!dri        ) cdri = dri    = &dep->req_info(*cdri) ;                         // refresh cdri in case dri allocated a new one
					if (dep_live_out) dri->live_out = true                  ;                         // ask live output for last level if user asked it
					Bool3 speculate_dep =
						is_static                     ? ri.speculate                                  // static deps do not disappear
					:	stamped_seen_waiting || modif ?              Yes                              // this dep may disappear
					:	+state.stamped.err            ? ri.speculate|Maybe                            // this dep is not the origin of the error
					:	                                ri.speculate                                  // this dep will not disappear from us
					;
					if (special!=Special::Req) dnd.asking = idx() ;                                   // Req jobs are fugitive, dont record them
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					dnd.make( *dri , mk_action(dep_goal,query) , speculate_dep ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
				}
				if ( is_static && dnd.buildable<Buildable::Yes ) sure = false ;                       // buildable (remember it is pessimistic) is better after make() (i.e. less pessimistic)
				if (cdri->waiting()) {
					if      ( is_static                                            ) ri.speculative_wait = false ; // we are non-speculatively waiting, even if after a speculative wait
					else if ( !stamped_seen_waiting && (+state.stamped.err||modif) ) ri.speculative_wait = true  ;
					proto_seen_waiting   = true        ;
					proto_seen_critical |= is_critical ;
					if (!dri) cdri = dri = &dnd.req_info(*cdri) ;                                                  // refresh cdri in case dri allocated a new one
					dnd.add_watcher(*dri,idx(),ri,dep_pressure) ;
					report_reason |= { JobReasonTag::BusyDep , +Node(dep) } ;
				} else if (!dnd.done(*cdri,dep_goal)) {
					SWEAR(query) ;                                                                                 // unless query, after having called make, dep must be either waiting or done
					proto_seen_waiting   = true                              ;                                     // if queried dep is not done, it would have been waiting if not queried
					proto_seen_critical |= is_critical                       ;
					state.reason        |= {JobReasonTag::DepOutOfDate,+dep} ;
				} else {
					bool dep_missing_dsk = !query && may_care && !dnd.done(*cdri,NodeGoal::Dsk) ;
					state.missing_dsk |= dep_missing_dsk   ;                                                                              // job needs this dep if it must run
					dep_modif          = !dep.up_to_date() ;
					if ( dep_modif && status==Status::Ok ) {                                                                              // no_trigger only applies to successful jobs
						if      (!dep.dflags[Dflag::Full])   dep_modif = false ;                                                          // if not full, a dep is only used to compute resources
						else if ( dep.no_trigger()       ) { dep_modif = false ; trace("no_trigger",dep) ; req->no_triggers.push(dep) ; } // record to repeat in summary
					}
					if ( +state.stamped.err  ) goto Continue ;                           // we are already in error, no need to analyze errors any further
					if ( !is_static && modif ) goto Continue ;                           // if not static, errors may be washed by previous modifs, dont record them
					// analyze error
					if (dep_modif) {
						if ( dep.is_crc && dep.never_match() ) { state.reason |= {JobReasonTag::DepUnstable ,+dep} ; trace("unstable_modif",dep) ; }
						else                                     state.reason |= {JobReasonTag::DepOutOfDate,+dep} ;
					}
					if ( may_care && cdri->overwritten ) {
						state.reason |= {JobReasonTag::DepOverwritten,+dep} ;
						dep_err       = RunStatus::DepError                 ;
						goto Continue ;
					}
					Bool3 ok = dnd.ok() ; if ( ok==No && !sense_err ) ok = Yes ;
					switch (ok) {
						case No :
							trace("dep_err",dep,STR(sense_err)) ;
							state.reason |= {JobReasonTag::DepErr,+dep} ;
							dep_err       = RunStatus::DepError         ;
						break ;
						case Maybe :                                                     // dep is not buidlable, check if required
							if (dnd.status()==NodeStatus::Transient) {                   // dep uphill is a symlink, it will disappear at next run
								trace("transient",dep) ;
								state.reason |= {JobReasonTag::DepTransient,+dep} ;
								break ;
							}
							if      (is_static) { trace("missing_static"  ,dep) ; state.reason |= {JobReasonTag::DepMissingStatic  ,+dep} ; dep_err = RunStatus::MissingStatic ; break ; }
							else if (required ) { trace("missing_required",dep) ; state.reason |= {JobReasonTag::DepMissingRequired,+dep} ; dep_err = RunStatus::DepError      ; break ; }
							dep_missing_dsk |= !query && cdri->manual>=Manual::Changed ; // ensure dangling are correctly handled
						[[fallthrough]] ;
						case Yes :
							if (dep_goal==NodeGoal::Dsk) {                               // if asking for disk, we must check disk integrity
								switch(cdri->manual) {
									case Manual::Empty   :
									case Manual::Modif   : state.reason |= {JobReasonTag::DepUnstable,+dep} ; dep_err = RunStatus::DepError ; trace("dangling",dep,cdri->manual) ; break ;
									case Manual::Unlnked : state.reason |= {JobReasonTag::DepUnlnked ,+dep} ;                                 trace("unlnked" ,dep             ) ; break ;
								DN}
							} else if ( dep_modif && at_end && dep_missing_dsk ) {       // dep out of date but we do not wait for it being rebuilt
								dep_goal = NodeGoal::Dsk ;                               // we must ensure disk integrity for detailed analysis
								trace("restart_dep",dep) ;
								goto RestartDep/*BACKWARD*/ ;
							}
						break ;
					DF}                                                                  // NO_COV
				}
			Continue :
				trace("dep",ri,dep,dep_goal,*cdri,dnd.done(*cdri)?"done":"!done",dnd.ok(),dnd.crc,dep_err,dep_modif?"mod":"!mod",state.reason,stamped_seen_critical?"stamped_critical":"") ;
				//
				if ( state.missing_dsk && !no_run_reason(state) ) {
					SWEAR(!query) ;                                                      // when query, we cannot miss dsk
					trace("restart_analysis") ;
					SWEAR( !ri.reason , ri.reason ) ;                                    // we should have asked for dep on disk if we had a reason to run
					ri.reason = state.reason ;                                           // record that we must ask for dep on disk
					ri.reset(idx()) ;
					goto RestartAnalysis/*BACKWARD*/ ;
				}
				SWEAR(!( +dep_err && modif && !is_static )) ;                            // if earlier modifs have been seen, we do not want to record errors as they can be washed, unless static
				state.proto.err      = ::max( state.proto.err   , dep_err   ) ;          // |= is forbidden for bit fields
				state.proto.modif    =        state.proto.modif | dep_modif   ;          // .
				proto_seen_critical |= is_critical && (+dep_err||dep_modif)   ;
			}
			if (ri.waiting()                             ) goto Wait ;
			if (sure                                     ) mk_sure() ;                   // improve sure (sure is pessimistic)
			if (+(run_status=ri.state.stamped.err)       ) goto Done ;
			if (no_run_reason(ri.state)==NoRunReason::Dep) goto Done ;
		}
	Run :
		switch (no_run_reason(ri.state)) {
			case NoRunReason::RetryLoop      : trace("fail_loop"       ,ri) ; pre_reason = JobReasonTag::None                                                ;                      break ;
			case NoRunReason::LostLoop       : trace("lost_loop"       ,ri) ; status     = status<Status::Early ? Status::EarlyLostErr : Status::LateLostErr ; report_loop = true ; break ;
			case NoRunReason::RunLoopReq     : trace("run_loop_req"    ,ri) ; status     = Status::RunLoop                                                   ; report_loop = true ; break ;
			case NoRunReason::RunLoopRule    : trace("run_loop_rule"   ,ri) ; status     = Status::RunLoop                                                   ; report_loop = true ; break ;
			case NoRunReason::SubmitLoopReq  : trace("submit_loop_req" ,ri) ; status     = Status::SubmitLoop                                                ; report_loop = true ; break ;
			case NoRunReason::SubmitLoopRule : trace("submit_loop_rule",ri) ; status     = Status::SubmitLoop                                                ; report_loop = true ; break ;
			default :
				report_reason = ri.reason = reason(ri.state) ;                           // ensure we have a reason to report that we would have run if not queried
				trace("run",ri,STR(query),pre_reason,run_status) ;
				if (query) goto Return ;
				if (ri.state.missing_dsk) {                                              // cant run if we are missing some deps on disk, XXX! : rework so that this never fires up
					SWEAR( !is_infinite(special) , special,idx() ) ;                     // Infinite do not process their deps
					ri.reset(idx()) ;
					goto RestartAnalysis/*BACKWARD*/ ;
				}
				if (!is_plain()) {
					//vvvvvvvvvvvvvvvvv
					_submit_special(ri) ;
					//^^^^^^^^^^^^^^^^^
					ri.reason = {} ;                                                     // flash execution
					ri.reset(idx()) ;
				} else {
					JobReasonTag rt = ri.reason.tag ;
					//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
					_submit_plain( ri , dep_pressure ) ;
					//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
					inc_submits( rt , cache_hit_info>=CacheHitInfo::Miss ) ;
					if (ri.waiting()                       ) goto Wait ;
					if (cache_hit_info<CacheHitInfo::Miss) {                             // if cached, there may be new deps, we must re-analyze
						SWEAR(!ri.running()) ;
						make_action = MakeAction::End ;                                  // restart analysis as if called by end() as in case of flash execution, submit has called end()
						ri.inc_wait() ;                                                  // .
						asked_reason = {} ;                                              // .
						ri.reason    = {} ;                                              // .
						trace("restart_analysis",ri) ;
						goto RestartFullAnalysis/*BACKWARD*/ ;
					}
				}
		}
	Done :
		SWEAR( !ri.running() && !ri.waiting() , idx() , ri ) ;
		ri.step(Step::Done,idx()) ;
		ri.reason = {} ;                                                                 // no more reason to run as analysis showed it is ok now
	Wakeup :
		if ( auto it = req->missing_audits.find(idx()) ; it!=req->missing_audits.end() && !req.zombie() ) {
			JobAudit const& ja = it->second ;
			trace("report_missing",ja) ;
			//
			if (ja.report!=JobReport::Hit) req->stats.move(JobReport::Rerun,ja.report,exe_time()) ; // if not Hit, then job was rerun and ja.report is the report that would have been done w/o rerun
			//
			JobReason jr  = reason(ri.state) ;
			::string  pfx =
				status==Status::RunLoop    ? ""
			:	status==Status::SubmitLoop ? ""
			:	ja.report==JobReport::Hit  ? "hit_"
			:	                             "was_"
			;
			if (ja.has_stderr) {
				JobEndRpcReq jerr = idx().job_info(JobInfoKind::End).end ;
				//                                           with_stats
				if (jr.tag>=JobReasonTag::Err) audit_end( ri , true   , pfx , MsgStderr{.msg=reason_str(jr),.stderr=::move(jerr.msg_stderr.stderr)} ) ;
				else                           audit_end( ri , true   , pfx , MsgStderr{.msg=ja.msg        ,.stderr=::move(jerr.msg_stderr.stderr)} ) ;
			} else {
				if (jr.tag>=JobReasonTag::Err) audit_end( ri , true   , pfx , MsgStderr{.msg=reason_str(jr)} ) ;
				else                           audit_end( ri , true   , pfx , MsgStderr{.msg=ja.msg        } ) ;
			}
			req->missing_audits.erase(it) ;
		} else if ( !at_end && report_loop ) {
			audit_end( ri , false/*with_stats*/ ) ;
		}
		trace("wakeup",ri) ;
		if ( ri.done() && wakeup_watchers ) {
			if (special!=Special::Dep) {
				ri.wakeup_watchers() ;
			} else if (!running_reqs()) {
				trace("send_reply",status) ;
				Backends::send_reply(
					asking_job()
				,	JobMngtRpcReply{
						.proc   = JobMngtProc::DepDirect
					,	.seq_id = seq_id()
					,	.fd     = fd    ()
					,	.ok     = No|(status==Status::Ok)
					}
				) ;
				to_pop.job = idx() ; // once reply is sent, we can dispose of ourselves (dont do ToPop assignment to avoid 2 destructions)
				to_pop.req = req   ;
			}
		}
		report_reason = reason(ri.state) ;
		goto Return ;
	Wait :
		trace("wait",ri) ;
	Return :
		return report_reason ;
	}

	void JobData::_propag_speculate(ReqInfo const& cri) const {
		Bool3 proto_speculate = No ;
		Bool3 speculate       = No ;
		for ( Dep const& dep : deps ) {
			if (!dep.parallel) speculate |= proto_speculate ;
			//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			dep->propag_speculate( cri.req , cri.speculate | (speculate&(!dep.dflags[Dflag::Static])) ) ; // static deps are never speculative
			//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			NodeReqInfo const& cdri = dep->c_req_info(cri.req) ;
			if ( !dep.is_crc || cdri.waiting() ) { proto_speculate = Yes ; continue ; }
			Bool3 dep_ok = cdri.done(NodeGoal::Status) ? dep->ok(cdri) : Maybe ;
			switch (dep_ok) {
				case Yes   :                                                                                                               break ;
				case Maybe : if (  dep.dflags[Dflag::Required   ] || dep.dflags[Dflag::Static] ) { proto_speculate |= Maybe ; continue ; } break ;
				case No    : if ( !dep.dflags[Dflag::IgnoreError] || cdri.overwritten          ) { proto_speculate |= Maybe ; continue ; } break ;
			}
			if ( +dep.accesses && !dep.up_to_date() ) proto_speculate = Yes ;
		}
	}

	MsgStderr JobData::special_msg_stderr( Node node , bool short_msg ) const {
		if (is_ok(status)!=No) return {} ;
		Rule      r          = rule()            ;
		MsgStderr msg_stderr ;
		::string& msg        = msg_stderr.msg    ;
		::string& stderr     = msg_stderr.stderr ;
		switch (r->special) {
			case Special::Plain :
				SWEAR(idx().frozen()) ;
				if (+node) return {.msg="frozen file does not exist while not phony : "+node->name()+'\n'} ;
				else       return {.msg="frozen file does not exist while not phony\n"                   } ;
			case Special::InfiniteDep  :
				msg << cat("max dep depth limit (",g_config->max_dep_depth,") reached, consider : lmake.config.max_dep_depth = ",g_config->max_dep_depth+1     ," (or larger)") ;
				goto DoInfinite ;
			case Special::InfinitePath : {
				msg << cat("max path limit ("     ,g_config->path_max     ,") reached, consider : lmake.config.max_path = "     ,(*deps.begin())->name().size()," (or larger)") ;
			DoInfinite :
				if (short_msg) {
					auto gen_dep = [&](::string const& dn) {
						if (dn.size()>111) stderr << dn.substr(0,50)<<"...("<<widen(cat(dn.size()-100),3,true/*right*/)<<")..."<<dn.substr(dn.size()-50) ;
						else               stderr << dn                                                                                                  ;
						stderr <<'\n' ;
					} ;
					::vector_s dns ;
					for( Dep const& d : deps ) dns.push_back(d->name()) ;
					if (dns.size()>23) {
						for(                  NodeIdx i : iota(10) ) gen_dep(dns[              i]) ;
						for( [[maybe_unused]] NodeIdx _ : iota( 3) ) stderr << ".\n.\n.\n"         ;
						for(                  NodeIdx i : iota(10) ) gen_dep(dns[dns.size()-10+i]) ;
					} else {
						for( ::string const& dn : dns ) gen_dep(dn) ;
					}
				} else {
					for( Dep const& d : deps ) stderr << d->name() << '\n' ;
				}
				return msg_stderr ;
			}
			default :
				return {.msg=cat(r->special," error\n")} ;
		}
	}

	void JobData::refresh_codec(Req req) {
		Node file ; for( Dep const& dep : deps ) { SWEAR( !file , idx() ) ; file = dep ; } SWEAR(+file,idx()) ; // there must be a single dep which is the codec file
		//
		Trace trace("refresh_codec",idx(),req) ;
		if (FileInfo(Codec::CodecFile::s_new_codes_file(file->name())).exists()) _submit_codec(req) ;
	}

	static void _create( Codec::CodecFile const& codec_file , ::string const& code_val , bool is_clean , Job job , bool tmp , NfsGuard* nfs_guard ) {
		::string  node_name      = codec_file.name()    ;
		NodeData& nd             = *Node(New,node_name) ;
		::string  disk_node_name ;
		//
		if (tmp) { disk_node_name = codec_file.name(true/*tmp*/) ; SWEAR( is_clean , codec_file ) ;  }                // nothing to clean in tmp space
		else     { disk_node_name = node_name+".tmp"             ; if (!is_clean) unlnk(node_name) ; }
		AcFd( disk_node_name , {.flags=O_WRONLY|O_CREAT|O_TRUNC,.mod=0444,.nfs_guard=nfs_guard} ).write( code_val ) ; // ensure node_name is always correct when it exists as there is no read lock
		//
		nd.set_buildable()                                                          ;
		nd.set_crc_date( Crc(New,code_val,No/*is_lnk*/) , FileSig(disk_node_name) ) ;
		nd.polluted      = {}                                                    ;
		nd.actual_job    = job                                                   ;
		nd.actual_tflags = { Tflag::Incremental , Tflag::Phony , Tflag::Target } ;
		//
		if (!tmp) rename( disk_node_name/*src*/ , node_name/*dst*/ , {.nfs_guard=nfs_guard} ) ;
	} ;
	static void _erase( Codec::CodecFile const& codec_file , NfsGuard* nfs_guard ) {
		::string  node_name = codec_file.name()    ;
		NodeData& nd        = *Node(New,node_name) ;
		//
		unlnk( node_name , {.nfs_guard=nfs_guard} )    ;
		nd.set_buildable()                             ;
		nd.set_crc_date( Crc::None , {FileTag::None} ) ;
		//
		nd.polluted      = {} ;
		nd.actual_job    = {} ;
		nd.actual_tflags = {} ;
	} ;
	void JobData::_submit_codec(Req req) {
		using namespace Codec ;
		// there must be a single dep which is the codec file
		Job      job        = idx()                         ;
		Node     file       ;                                 for( Dep const& dep : deps ) { SWEAR( !file , job ) ; file = dep ; } SWEAR(+file,job) ;
		::string file_name  = file->name()                  ;
		::string manifest   ;
		//
		Trace trace("_submit_codec",job,req) ;
		//
		file->set_buildable() ;
		if (!( file->is_src() && file->crc.is_reg() )) {
			req->audit_job ( Color::Err  , New , "failed" , rule() , file_name                            ) ;
			req->audit_info( Color::Note , "must be a regular source to be used as codec file" , 1/*lvl*/ ) ;
			status = Status::Err ;
			return ;
		}
		//
		Bool3                                   has_new_codes  ;
		::umap_s/*ctx*/<::umap_s/*code*/<Crc> > old_decode_tab = _prepare_old_decode_tab( file_name                        ) ;
		::map_s /*ctx*/<::map_ss/*val ->code*/> encode_tab     = _prepare_encode_tab    ( file_name , /*out*/has_new_codes ) ;
		::map_s /*ctx*/<::map_ss/*code->val */> decode_tab     ;
		::string                                codec_dir_s    = CodecFile::s_dir_s(file_name)                               ;
		//
		if (FileInfo(codec_dir_s).tag()!=FileTag::Dir) {                                                  // if not initialized yet, we create the whole tree in tmp space so as to stay always correct
			::string tmp_codec_dir_s = CodecFile::s_dir_s(file_name,CodecDir::Tmp) ;
			SWEAR( !old_decode_tab , file_name ) ;                                                        // cannot have old codes if not initialized
			mk_dir_s(tmp_codec_dir_s) ;                                                                   // we want a dir to appear initialized, even if empty
			decode_tab = _mk_decode_tab(encode_tab) ;
			for( auto const& [ctx,d_entry] : decode_tab ) {
				manifest << mk_printable(ctx) <<'\n' ;
				for( auto const& [code,val] : d_entry ) {
					NfsGuard nfs_guard  { Engine::g_config->file_sync } ;
					Crc      crc        { New , val                   } ;
					_create( {false/*encode*/,file_name,ctx,code} , val  , true/*is_clean*/ , job , true/*fresh*/ , &nfs_guard ) ;
					_create( {                file_name,ctx,crc } , code , true/*.       */ , job , true/*.    */ , &nfs_guard ) ;
					manifest <<'\t'<< mk_printable(code) <<'\t'<< crc.hex() <<'\n' ;
				}
			}
			rename( tmp_codec_dir_s/*src*/ , codec_dir_s/*dst*/ ) ;                                       // global move
		} else {
			::string       new_codes_file_name = CodecFile::s_new_codes_file(file_name)                 ;
			CodecGuardLock lock                { file_name , {.file_sync=Engine::g_config->file_sync} } ; // if we cannot lock, jobs do not access db, so no need to lock
			//
			_update_old_decode_tab( file_name , new_codes_file_name , /*inout*/old_decode_tab                          ) ;
			_update_encode_tab    (             new_codes_file_name , /*inout*/encode_tab     , /*inout*/has_new_codes ) ;
			unlnk( new_codes_file_name , {.nfs_guard=&lock} ) ;
			decode_tab = _mk_decode_tab(encode_tab) ;
			//
			for( auto const& [ctx,d_entry] : decode_tab ) {
				::umap_s<Crc>&       old_d_entry = old_decode_tab[ctx] ;
				::umap<Crc,::string> old_e_entry ;                       for( auto const& [code,val_crc] : old_d_entry ) old_e_entry.try_emplace(val_crc,code) ;
				manifest << mk_printable(ctx) <<'\n' ;
				for( auto const& [code,val] : d_entry ) {
					lock.keep_alive() ;                                                                                                    // lock have limited liveness, keep it alive regularly
					Crc  crc        { New , val }            ;
					auto dit        = old_d_entry.find(code) ;
					auto eit        = old_e_entry.find(crc ) ;
					bool d_is_clean = dit==old_d_entry.end() ;
					bool e_is_clean = eit==old_e_entry.end() ; //!                                                                    fresh
					if (  d_is_clean || dit->second!=crc  ) _create( {false/*encode*/,file_name,ctx,code} , val  , d_is_clean , job , false , &lock ) ;
					if (  e_is_clean || eit->second!=code ) _create( {                file_name,ctx,crc } , code , e_is_clean , job , false , &lock ) ;
					if ( !d_is_clean                      ) old_d_entry.erase(dit)                                                                    ;
					if ( !e_is_clean                      ) old_e_entry.erase(eit)                                                                    ;
					manifest <<'\t'<< mk_printable(code) <<'\t'<< crc.hex() <<'\n' ;
				}
				for( auto const& [code,_] : old_d_entry ) { lock.keep_alive() ; _erase( {false/*encode*/,file_name,ctx,code} , &lock ) ; } // lock have limited liveness, keep it alive regularly
				for( auto const& [crc ,_] : old_e_entry ) { lock.keep_alive() ; _erase( {                file_name,ctx,crc } , &lock ) ; } // .
			}
		}
		if (has_new_codes==No) {                                                                                                           // codes are strictly increasing and hence no code conflict
			Dep dep { file , Access::Reg , FileInfo(file_name) , false/*err*/ } ;
			dep.acquire_crc()  ;
			deps.assign({dep}) ;
		} else {
			Crc file_crc = _refresh_codec_file( file_name , decode_tab ) ;
			file->set_crc_date( file_crc , FileSig(file_name) ) ;
			deps.assign({Dep( file , Access::Reg , file_crc , false/*err*/ )}) ;
		}
		switch (has_new_codes) {
			case No    : req->audit_job( Color::Note , New , "expand"   , rule() , file_name ) ; break ;
			case Maybe : req->audit_job( Color::Note , New , "reformat" , rule() , file_name ) ; break ;
			case Yes   : req->audit_job( Color::Note , New , "update"   , rule() , file_name ) ; break ;
		}
		AcFd ( _manifest_file(file_name) , {O_WRONLY|O_CREAT|O_TRUNC,0666/*mod*/} ).write( manifest ) ;
		status = Status::Ok ;
	}

	void JobData::_submit_special(ReqInfo& ri) {                   // never report new deps
		Trace trace("_submit_special",idx(),ri) ;
		Req     req     = ri.req          ;
		Special special = rule()->special ;
		bool    frozen_ = idx().frozen()  ;
		//
		if (frozen_) req->frozen_jobs.push(idx()) ;                // record to repeat in summary
		//
		switch (special) {
			case Special::Req          :
			case Special::Dep          : status = Status::Ok  ;                                                                break ;
			case Special::InfiniteDep  :
			case Special::InfinitePath : status = Status::Err ; audit_end_special( req , SpecialStep::Err , No/*modified*/ ) ; break ;
			case Special::Codec        : _submit_codec(req) ;                                                                  break ;
			case Special::Plain : {
				SWEAR(frozen_) ;                                   // only case where we are here without special rule
				SpecialStep special_step = SpecialStep::Steady   ;
				Node        worst_target ;
				Bool3       modified     = No                    ;
				NfsGuard    nfs_guard    { g_config->file_sync } ;
				for( Target t : targets() ) {
					::string    tn = t->name()           ;
					SpecialStep ss = SpecialStep::Steady ;
					if (!( t->crc.valid() && FileSig(tn,{.nfs_guard=&nfs_guard})==t->sig.sig )) {
						FileSig sig ;
						Crc     crc { tn , /*out*/sig } ;
						modified |= crc.match(t->crc) ? No : t->crc.valid() ? Yes : Maybe ;
						Trace trace( "frozen" , t->crc ,"->", crc , t->sig ,"->", sig ) ;
						//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
						t->set_crc_date( crc , {sig,{}} ) ;        // if file disappeared, there is not way to know at which date, we are optimistic here as being pessimistic implies false overwrites
						//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
						if      ( crc!=Crc::None || t.tflags[Tflag::Phony]           ) ss            = SpecialStep::Ok  ;
						else if ( t.tflags[Tflag::Target] && t.tflags[Tflag::Static] ) ss            = SpecialStep::Err ;
						else                                                           t->actual_job = {}               ; // unlink of a star or side target is nothing
					}
					if (ss>special_step) { special_step = ss ; worst_target = t ; }
				}
				status = special_step==SpecialStep::Err ? Status::Err : Status::Ok ;
				audit_end_special( req , special_step , modified , worst_target ) ;
			} break ;
		DF}                                                                                                               // NO_COV
	}

	void JobData::_submit_plain( ReqInfo& ri , CoarseDelay pressure ) {
		using Step = JobStep ;
		Rule            r     = rule() ;
		Req             req   = ri.req ;
		Job             job   = idx()  ;
		Rule::RuleMatch match { job }  ;
		Trace trace(Channel::Cache,"_submit_plain",job,ri,pressure) ;
		SWEAR( !ri.waiting() , ri ) ;
		SWEAR( !ri.running() , ri ) ;
		for( Req rr : running_reqs() ) if (rr!=req) {
			ReqInfo const& cri = c_req_info(rr) ;
			ri.step(cri.step(),job) ;                                                  // Exec or Queued, same as other reqs
			ri.inc_wait() ;
			if (ri.step()==Step::Exec) req->audit_job(Color::Note,"started",job) ;
			SubmitInfo si = {
				.live_out = ri.live_out
			,	.nice     = rr->nice
			,	.pressure = pressure
			} ;
			//                          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			ri.miss_live_out = Backend::s_add_pressure( backend , +job , +req , si ) ; // tell backend of new Req, even if job is started and pressure has become meaningless
			//                          ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			trace("other_req",rr,ri) ;
			SWEAR( cache_hit_info>=CacheHitInfo::Miss , cache_hit_info ) ;             // how can job be running if it is cached ?
			return ;
		}
		//
		for( Node t : targets() ) t->set_buildable() ;                                 // we will need to know if target is a source, possibly in another thread, we'd better call set_buildable here
		// do not generate error if *_ancillary_attrs is not available, as we will not restart job when fixed : do our best by using static info
		::vmap_s<DepDigest>  early_deps             ;
		SubmitAncillaryAttrs submit_ancillary_attrs ;
		try {
			submit_ancillary_attrs = r->submit_ancillary_attrs.eval( job , match , &early_deps ) ; // dont care about dependencies as these attributes have no impact on result
		} catch (MsgStderr const& msg_err) {
			submit_ancillary_attrs = r->submit_ancillary_attrs.spec ;
			req->audit_job   ( Color::Note , "no_dynamic" , job                                                                                                       ) ;
			req->audit_stderr(                              job , { with_nl(r->submit_ancillary_attrs.s_exc_msg(true/*using_static*/))+msg_err.msg , msg_err.stderr } ) ;
		}
		for( auto& [k,dd] : early_deps ) { // suppress sensitiviy to read files as ancillary has no impact on job result nor status, just record deps to trigger building on a best effort basis
			dd.accesses = {} ;
			dd.dflags   = {} ;
		}
		CacheIdx cache_idx1 = 0 ;
		if (!submit_ancillary_attrs.cache_name) { cache_hit_info = CacheHitInfo::NoCache    ; goto CacheDone ; }
		if (!req->cache_method                ) { cache_hit_info = CacheHitInfo::NoDownload ; goto CacheDone ; }
		//
		{	using namespace Cache ;
			::string const&                 cn           = submit_ancillary_attrs.cache_name  ;
			auto                            it           = g_config->cache_idxes.find(cn)     ; if (it==g_config->cache_idxes.end() ) { cache_hit_info = CacheHitInfo::BadCache   ; goto CacheDone ; }
			CacheServerSide&                cache        = CacheServerSide::s_tab[it->second] ; if (!cache                          ) { cache_hit_info = CacheHitInfo::BadCache   ; goto CacheDone ; }
			/**/                            cache_idx1   = it->second+1                       ; if (!has_download(req->cache_method)) { cache_hit_info = CacheHitInfo::NoDownload ; goto CacheDone ; }
			CacheServerSide::DownloadDigest cache_digest ;
			JobInfo&                        job_info     = cache_digest.job_info              ;
			try { //!          vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
				cache_digest = cache.download( job , match , !req->options.flags[ReqFlag::NoIncremental] ) ;
				//             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			} catch (::string const& e) {
				trace("cache_download_throw",e) ;
				req->audit_job ( Color::Warning , "bad_cache_download" , job ) ;
				req->audit_info( Color::Note    , e , 1/*lvl*/               ) ;
				cache_hit_info = CacheHitInfo::BadDownload ;
				goto CacheDone ;
			}
			cache_hit_info = cache_digest.hit_info ;
			trace("hit",cache_hit_info) ;
			if (cache_hit_info<CacheHitInfo::Miss) {
				switch (cache_hit_info) {
					case CacheHitInfo::Hit : {
						//
						if (+cache_digest.file_actions_msg) {
							req->audit_job ( Color::Note , "wash" , job                             ) ;
							req->audit_info( Color::Note , cache_digest.file_actions_msg , 1/*lvl*/ ) ;
							trace("hit_msg",cache_digest.file_actions_msg,ri) ;
						}
						//
						job_info.start.pre_start.job      = +job      ;                                             // repo dependent
						job_info.start.submit_info.reason = ri.reason ;                                             // context dependent
						job_info.end  .end_date           = New       ;                                             // execution dependnt
						//
						JobDigest<Node> digest = job_info.end.digest ;                                              // gather info before being moved
						Job::s_record_thread.emplace(job,::move(job_info.start)) ;
						Job::s_record_thread.emplace(job,::move(job_info.end  )) ;
						//
						ri.step(Step::Hit,job) ;
						JobExec je { job , New } ;                                                                  // job starts and ends, no host
						je.max_stderr_len = job->rule()->start_ancillary_attrs.spec.max_stderr_len ;                // in case it is not dynamic
						if (ri.live_out) je.live_out(ri,job_info.end.stdout) ;
						je.end_analyze(/*inout*/digest) ;
						req->stats.add(JobReport::Hit) ;
						req->missing_audits[job] = { .report=JobReport::Hit , .has_stderr=+job_info.end.msg_stderr.stderr } ;
						::vector<Dep> ds ; ds.reserve(digest.deps.size()) ; for( auto& [d,dd] : digest.deps ) ds.emplace_back( d , dd ) ;
						deps.assign(ds) ;
					}
					break ;
					case CacheHitInfo::Match : {
						status = Status::CacheMatch ;
						req->audit_job( Color::Note , "hit_rerun" , job ) ;
						::vector<Dep> ds ; ds.reserve(job_info.end.digest.deps.size()) ; for( auto& [dn,dd] : job_info.end.digest.deps ) ds.emplace_back( Node(New,dn) , dd ) ;
						deps.assign(ds) ;
					}
					break ;
				DF}                                                                                                 // NO_COV
				for( Req r : reqs() ) if (c_req_info(r).step()==Step::Dep) req_info(r).reset(job,true/*has_run*/) ; // there are new deps and req_info is not reset spontaneously, ...
				return ;                                                                                            // ... so we have to ensure ri.iter is still a legal iterator
			}
		}
	CacheDone :
		SWEAR( cache_hit_info>=CacheHitInfo::Miss , cache_hit_info ) ;
		//
		SubmitRsrcsAttrs submit_rsrcs_attrs ;
		size_t           n_ancillary_deps   = early_deps.size() ;
		try {
			submit_rsrcs_attrs = r->submit_rsrcs_attrs.eval( job , match , &early_deps ) ;
		} catch (MsgStderr const& msg_err) {
			req->audit_job   ( Color::Err , "failed" , job                                                                                                    ) ;
			req->audit_stderr(                         job , { with_nl(r->submit_rsrcs_attrs.s_exc_msg(false/*using_static*/))+msg_err.msg , msg_err.stderr } ) ;
			run_status = RunStatus::Error ;
			trace("no_rsrcs",ri) ;
			return ;
		}
		for( NodeIdx i : iota(n_ancillary_deps,early_deps.size()) ) early_deps[i].second.dflags &= ~Dflag::Full ; // mark new deps as resources only
		for( auto const& [dn,dd] : early_deps ) {
			Node         d   { New , dn }       ;
			NodeReqInfo& dri = d->req_info(req) ;
			d->make(dri,NodeMakeAction::Dsk) ;
			if (dri.waiting()) d->add_watcher(dri,job,ri,pressure) ;
		}
		if (ri.waiting()) {
			trace("waiting_rsrcs") ;
			return ;
		}
		//
		ri.inc_wait() ;                                                                                           // set before calling submit call back as in case of flash execution, we must be clean
		ri.step(Step::Queued,job) ;
		backend = submit_rsrcs_attrs.backend ;
		if (!has_upload(req->cache_method)) cache_idx1 = 0 ;
		try {
			Tokens1 tokens1 = submit_rsrcs_attrs.tokens1() ;
			SubmitInfo si {
				.cache_idx1 =        cache_idx1
			,	.deps       = ::move(early_deps )
			,	.live_out   =        ri.live_out
			,	.nice       =        req->nice
			,	.pressure   =        pressure
			,	.reason     =        ri.reason
			,	.tokens1    =        tokens1
			} ;
			estimate_stats(tokens1) ;                                                                             // refine estimate with best available info just before submitting
			//       vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
			Backend::s_submit( backend , +job , +req , ::move(si) , ::move(submit_rsrcs_attrs.rsrcs) ) ;
			//       ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
			for( Node t : targets() ) t->busy = true ;                                                            // make targets busy once we are sure job is submitted
		} catch (::string const& e) {
			ri.dec_wait() ;                                                                                       // restore n_wait as we prepared to wait
			ri.step(Step::None,job) ;
			status  = Status::EarlyErr ;
			req->audit_job ( Color::Err  , "failed" , job ) ;
			req->audit_info( Color::Note , e , 1/*lvl*/   ) ;
			trace("submit_err",ri) ;
			return ;
		} ;
		trace("submitted",ri) ;
	}

	void JobData::audit_end_special( Req req , SpecialStep step , Bool3 modified , Node node ) const {
		//
		SWEAR( status>Status::Garbage , status ) ;
		Trace trace("audit_end_special",idx(),req,step,modified,status) ;
		//
		bool      frozen_    = idx().frozen()           ;
		MsgStderr msg_stderr = special_msg_stderr(node) ;
		::string  step_str   ;
		switch (step) {
			case SpecialStep::Steady :                                                                             break ;
			case SpecialStep::Ok     : step_str = modified==Yes ? "changed" : modified==Maybe ? "new" : "steady" ; break ;
			case SpecialStep::Err    : step_str = "failed"                                                       ; break ;
		DF}                                                                                                                // NO_COV
		Color color =
			status==Status::Ok && !frozen_ ? Color::HiddenOk
		:	status>=Status::Err            ? Color::Err
		:	                                 Color::Warning
		;
		if (frozen_) {
			if (+step_str) step_str += "_frozen" ;
			else           step_str  = "frozen"  ;
		}
		if (+step_str) {
			/**/                    req->audit_job ( color       , step_str , idx()             ) ;
			if (+msg_stderr.msg   ) req->audit_info( Color::Note , msg_stderr.msg    , 1/*lvl*/ ) ;
			if (+msg_stderr.stderr) req->audit_info( Color::None , msg_stderr.stderr , 1/*lvl*/ ) ;
		}
	}

	bool/*ok*/ JobData::forget( bool targets_ , bool deps_ ) {
		Trace trace("Jforget",idx(),STR(targets_),STR(deps_)) ;
		for( [[maybe_unused]] Req r : running_reqs() ) return false ; // ensure job is not running
		status = Status::New ;
		fence() ;                                                     // once status is New, we are sure target is not up to date, we can safely modify it
		run_status = RunStatus::Ok ;
		if (deps_) {
			::vector<Dep> static_deps ;
			for( Dep const& d : deps )  if (d.dflags[Dflag::Static]) static_deps.push_back(d) ;
			deps.assign(static_deps) ;
		}
		if ( targets_ && is_plain(true/*frozen_ok*/)) _reset_targets() ;
		trace("summary",deps) ;
		return true ;
	}

	bool JobData::running( bool with_zombies , bool hit_ok ) const {
		for( Req r : Req::s_reqs_by_start() ) if ( (with_zombies||!r.zombie()) && c_req_info(r).running(hit_ok) ) return true ;
		return false ;
	}

	::vector<Req> JobData::running_reqs( bool with_zombies , bool hit_ok ) const {                                                   // sorted by start
		::vector<Req> res ; res.reserve(Req::s_n_reqs()) ;                                                                           // pessimistic, so no realloc
		for( Req r : Req::s_reqs_by_start() ) if ( (with_zombies||!r.zombie()) && c_req_info(r).running(hit_ok) ) res.push_back(r) ;
		return res ;
	}

}
