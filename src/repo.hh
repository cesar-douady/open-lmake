// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023-2025 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "app.hh"
#include "disk.hh"

#include "version.hh"

// must not be touched to fit needs
static constexpr uint8_t NJobGuardBits  = 2 ; // one to define JobTgt, the other to put it in a Crunch vector
static constexpr uint8_t NNodeGuardBits = 1 ; // to be able to make Target

// START_OF_VERSIONING

// NXxxBits are used to dimension address space, and hence max number of objects for each category.
// can be tailored to fit neeeds
static constexpr uint8_t NCacheIdxBits    =  8 ; // used to caches
static constexpr uint8_t NCodecIdxBits    = 32 ; // used to store code <-> value associations in lencode/ldecode
static constexpr uint8_t NDepsIdxBits     = 32 ; // used to index deps
static constexpr uint8_t NJobIdxBits      = 30 ; // 2 guard bits
static constexpr uint8_t NJobNameIdxBits  = 32 ; // used to index Job names
static constexpr uint8_t NJobTgtsIdxBits  = 32 ; // JobTgts are used to store job candidate for each Node, so this Idx is a little bit larget than NodeIdx
static constexpr uint8_t NNodeIdxBits     = 31 ; // 1 guard bit, there are a few targets per job, so this idx is a little bit larger than JobIdx
static constexpr uint8_t NNodeNameIdxBits = 32 ; // used to index Node names
static constexpr uint8_t NPsfxIdxBits     = 32 ; // each rule appears in a few Psfx slots, so this idx is a little bit larger than ruleTgtsIdx
static constexpr uint8_t NReqIdxBits      =  8 ;
static constexpr uint8_t NRuleIdxBits     = 16 ;
static constexpr uint8_t NRuleCrcIdxBits  = 32 ;
static constexpr uint8_t NRuleStrIdxBits  = 32 ; // used to index serialized Rule description
static constexpr uint8_t NRuleTgtsIdxBits = 32 ;
static constexpr uint8_t NTargetsIdxBits  = 32 ; // used to index targets

// END_OF_VERSIONING

//
// derived info
//
// must not be touched to fit needs
using CacheIdx    = Uint<NCacheIdxBits                  > ;
using CodecIdx    = Uint<NCodecIdxBits                  > ;
using DepsIdx     = Uint<NDepsIdxBits                   > ;
using JobIdx      = Uint<NJobIdxBits     +NJobGuardBits > ;
using JobNameIdx  = Uint<NJobNameIdxBits                > ;
using JobTgtsIdx  = Uint<NJobTgtsIdxBits                > ;
using NodeIdx     = Uint<NNodeIdxBits    +NNodeGuardBits> ;
using NodeNameIdx = Uint<NNodeNameIdxBits               > ;
using PsfxIdx     = Uint<NPsfxIdxBits                   > ;
using ReqIdx      = Uint<NReqIdxBits                    > ;
using RuleIdx     = Uint<NRuleIdxBits                   > ;
using RuleStrIdx  = Uint<NRuleStrIdxBits                > ;
using RuleCrcIdx  = Uint<NRuleCrcIdxBits                > ;
using RuleTgtsIdx = Uint<NRuleTgtsIdxBits               > ;
using TargetsIdx  = Uint<NTargetsIdxBits                > ;

// START_OF_VERSIONING

// can be tailored to fit neeeds
using VarIdx = uint8_t ; // used to index stems, targets, deps & rsrcs within a Rule

// ids
// can be tailored to fit neeeds
using SmallId = uint32_t ; // used to identify running jobs, could be uint16_t if we are sure that there cannot be more than 64k jobs running at once
using SeqId   = uint64_t ; // used to distinguish old report when a job is relaunched, may overflow as long as 2 job executions have different values if the 1st is lost

// type to hold the dep depth used to track dep loops
// can be tailored to fit neeeds
using DepDepth = uint16_t ;

// job tokens
// can be tailored to fit neeeds
using Tokens1 = uint8_t ; // store number of tokens-1 (so tokens can go from 1 to 256)

// maximum number of rule generation before a Job/Node clean up is necessary
// can be tailored to fit neeeds
using MatchGen = uint8_t ;

// END_OF_VERSIONING

// if crc's differ on only by that many bits, then we are close to crc clashes. If that happen, we will have to increase CRC size.
// can be tailored to fit neeeds
static constexpr uint8_t NCrcGuardBits = 8 ;

// weight associated to rule when a job completes
// the average value kept in rule is the weighted average between old average value and job value with weiths RuleWeight and 1
// can be tailored to fit neeeds
static constexpr size_t RuleWeight = 100 ;

// number of job traces to keep (indexed by unique id)
// can be tailored to fit neeeds
static constexpr SeqId JobHistorySz = 1000 ;

// backlog of incoming connections from remote jobs (i.e. number of pending connect calls before connections are refused)
// can be tailored to fit neeeds
static constexpr int JobExecBacklog = 4096 ; // max usual value as set in /proc/sys/net/core/somaxconn

//
// derived info
//

// must not be touched to fit needs
using WatcherIdx = Largest<JobIdx,NodeIdx> ;

static constexpr char ServerMrkr[] = ADMIN_DIR_S "server" ;

static void _dflt_app_init_action(AppInitAction& action) {
	if (!action.root_mrkrs) action.root_mrkrs = { "Lmakefile.py" , "Lmakefile/__init__.py" } ;
	if (!action.version   ) action.version    = Version::Repo                                ;
}
inline bool/*read_only*/ repo_app_init   ( AppInitAction&& action={}                            ) { _dflt_app_init_action(action) ; return app_init   ( action         ) ; }
inline void              chk_repo_version( AppInitAction&& action={} , ::string const& dir_s={} ) { _dflt_app_init_action(action) ; return chk_version( action , dir_s ) ; }
inline SearchRootResult  search_repo_root( AppInitAction&& action={}                            ) { _dflt_app_init_action(action) ; return search_root( action         ) ; }
