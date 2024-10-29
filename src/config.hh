// This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
// Copyright (c) 2023 Doliam
// This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
// This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#pragma once

#include "utils.hh"

// XXX : to support Idx's above 32 bits, we must :
// - define NXxxIdxes
// - define XxxIdx=Uint<NXxxidxes> +/- guard bits (1 for Node, 2 for Job), reserved values (a few for Rule)
// - allocate address space in store/file.hh after this number rather than from the index type
// - ensure that all allocations check boundaries

// START_OF_VERSIONING

// idxs
using CodecIdx    = uint32_t ; // used to store code <-> value associations in lencode/ldecode
using DepsIdx     = uint32_t ; // used to index deps
using FileNameIdx = uint16_t ; // 64k for a file name is already ridiculously long
using JobIdx      = uint32_t ; // 2 guard bits
using JobTgtsIdx  = uint32_t ; // JobTgts are used to store job candidate for each Node, so this Idx is a little bit larget than NodeIdx
using NameIdx     = uint32_t ; // used to index Rule & Job names
using NodeIdx     = uint32_t ; // 1 guard bit, there are a few targets per job, so this idx is a little bit larger than JobIdx
using PsfxIdx     = uint32_t ; // each rule appears in a few Psfx slots, so this idx is a little bit larger than ruleTgtsIdx
using ReqIdx      = uint8_t  ;
using RuleIdx     = uint16_t ;
using RuleStrIdx  = uint32_t ; // used to index serialized Rule description
using RuleTgtsIdx = uint32_t ;
using TargetsIdx  = uint32_t ; // used to index targets
using VarIdx      = uint8_t  ; // used to index stems, targets, deps & rsrcs within a Rule

// ids
using SmallId = uint32_t ; // used to identify running jobs, could be uint16_t if we are sure that there cannot be more than 64k jobs running at once
using SeqId   = uint64_t ; // used to distinguish old report when a job is relaunched, may overflow as long as 2 job executions have different values if the 1st is lost

// type to hold the dep depth used to track dep loops
using DepDepth = uint16_t ;

// rule matching priority
using Prio = double ;

// job tokens
using Tokens1 = uint8_t ; // store number of tokens-1 (so tokens can go from 1 to 256)

// if crc's differ on only by that many bits, then we are close to crc clashes. If that happen, we will have to increase CRC size.
static constexpr uint8_t NCrcGuardBits = 8 ;

// maximum number of rule generation before a Job/Node clean up is necessary
// minimum is 1 : values range from 0 (bad) to NMatchGen
// Job's and Node's store a generation, so this must not be too high
static constexpr size_t NMatchGen = 255 ; // maximum value for 8 bits
static_assert(NMatchGen>=1) ;

// maximum number of cmd/rsrcs generation before a Job/Node clean up is necessary
// minimum is 3 : we need a value for bad cmd, bad rsrcs and ok
// Job's store a generation, so this must not be too high
static constexpr size_t NExecGen = 255 ;
static_assert(NExecGen>=3) ;

// max number of bits a code may have for lencode/ldecode
static constexpr uint8_t MaxCodecBits = 32 ; // if more than 32 bits, then we need a stronger Crc as we are subject to the anniversary paradox here

// max number of Req's
static constexpr ReqIdx NReqs = ::numeric_limits<ReqIdx>::max() ;

//
// Directories
//

#define ADMIN_DIR_S            "LMAKE/"
#define PRIVATE_ADMIN_SUBDIR_S "lmake/"
#define PRIVATE_ADMIN_DIR_S    ADMIN_DIR_S PRIVATE_ADMIN_SUBDIR_S
static constexpr char AdminDirS       [] = ADMIN_DIR_S         ;
static constexpr char PrivateAdminDirS[] = PRIVATE_ADMIN_DIR_S ;

// END_OF_VERSIONING

// weight associated to rule when a job completes
// the average value kept in rule is the weighted average between old average value and job value with weiths RuleWeight and 1
static constexpr JobIdx RuleWeight = 100 ;

// number of job traces to keep (indexed by unique id)
static constexpr SeqId JobHistorySz = 1000 ;

// backlog of incoming connections from remote jobs (i.e. number of pending connect calls before connections are refused)
static constexpr int JobExecBacklog = 4096 ; // max usual value as set in /proc/sys/net/core/somaxconn

//
// derived info
//

using WatcherIdx = Largest<JobIdx,NodeIdx> ;

static constexpr uint8_t NMatchGenBits = n_bits(NMatchGen+1) ;
using MatchGen = Uint<NMatchGenBits> ;

static constexpr uint8_t NExecGenBits = n_bits(NExecGen+1) ;
using ExecGen = Uint<NExecGenBits> ;

using Tokens = uint16_t ; // tokens can go from 1 to 256
