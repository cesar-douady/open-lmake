# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

VERSION        := 25.04
TAG            := 32
# ubuntu20.04 (focal) is supported through the use of a g++-11 installation, but packages are not available on launchpad.net
DEBIAN_RELEASE := 1
DISTROS        := jammy noble

ifneq ($(shell uname),Linux)
    $(error can only compile under Linux)
endif

MAKEFLAGS := -r -R                       # mandatory
MAKEFLAGS += -k -j$(shell nproc||echo 1) # user configurable

.DEFAULT_GOAL := DFLT

REPO_ROOT := $(abspath .)

.PHONY : FORCE
FORCE : ;
sys_config.env : FORCE
	@if [ ! -f $@ ] ; then                                           \
		echo new $@ ;                                                \
		{	echo PATH=\'$$(   echo "$$PATH"   |sed "s:':'\\'':")\' ; \
			echo CXX=\'$$(    echo "$$CXX"    |sed "s:':'\\'':")\' ; \
			echo PYTHON2=\'$$(echo "$$PYTHON2"|sed "s:':'\\'':")\' ; \
			echo PYTHON3=\'$$(echo "$$PYTHON" |sed "s:':'\\'':")\' ; \
			echo LMAKE_FLAGS=$$LMAKE_FLAGS                         ; \
		} >$@ ;                                                      \
	fi

sys_config.log : _bin/sys_config sys_config.env
	@echo sys_config
	@# reread sys_config.env in case it has been modified while reading an old sys_config.mk
	@set -a                               ; \
	PATH=$$(env -i bash -c 'echo $$PATH') ; \
	unset CXX PYTHON2 PYTHON3 LMAKE_FLAGS ; \
	. ./sys_config.env                    ; \
	./$< $(@:%.log=%.mk) $(@:%.log=%.h) $(@:%.log=%.sum) $(@:%.log=%.err) 2>$@ ||:
sys_config.mk  : sys_config.log ;+@[ -f $@ ] || { echo "cannot find $@" ; exit 1 ; }
sys_config.h   : sys_config.log ;+@[ -f $@ ] || { echo "cannot find $@" ; exit 1 ; }
sys_config.sum : sys_config.log ;+@[ -f $@ ] || { echo "cannot find $@" ; exit 1 ; }
sys_config.err : sys_config.log ;+@[ -f $@ ] || { echo "cannot find $@" ; exit 1 ; }

%.SUMMARY : %
	@echo '***********'
	@echo '* SUMMARY *'
	@echo '***********'
	@echo '*'
	@echo '* provided env :'
	@echo '*'
	@cat sys_config.env
	@echo '*'
	@echo '* sys_config summary :'
	@echo '*'
	@cat sys_config.sum

# defines HAS_SECCOMP
include sys_config.mk

ifeq ($(SYS_CONFIG_OK),0)
$(shell echo '*********'        >&2 )
$(shell echo '* ERROR *'        >&2 )
$(shell echo '*********'        >&2 )
$(shell echo '*'                >&2 )
$(shell echo '* provided env :' >&2 )
$(shell echo '*'                >&2 )
$(shell cat sys_config.env      >&2 )
$(shell echo '*'                >&2 )
$(shell echo '* error :'        >&2 )
$(shell echo '*'                >&2 )
$(shell cat sys_config.err >&2 )
$(error )
endif

#
# Manifest
#
Manifest : .git/index
	@git ls-files | uniq >$@.new
	@if cmp -s $@.new $@ ; then rm $@.new    ; echo steady Manifest ; \
	else                        mv $@.new $@ ; echo new    Manifest ; \
	fi
include Manifest.inc_stamp # Manifest is used in this makefile
EXCLUDES := $(if $(HAS_LD_AUDIT),,src/autodep/ld_audit.cc)
SRCS     := $(filter-out $(EXCLUDES),$(shell cat Manifest 2>/dev/null))

# this is the recommanded way to insert a , when calling functions
# /!\ cannot put a comment on the following line or a lot of spaces will be inserted in the variable definition
COMMA := ,

# XXX! : add -fdebug_prefix-map=$(REPO_ROOT)=??? when we know a sound value (e.g. the dir in which sources will be installed)
HIDDEN_CC_FLAGS := -ftabstop=4 -ftemplate-backtrace-limit=0 -pedantic -fvisibility=hidden
# syntax for LMAKE_FLAGS : (O[01234])?g?d?t?(S[AT])?P?C?
# - O[0123] : compiler optimization level (4 means -O3 -flto), defaults to 1 if profiling else 3
# - g       : dont ease debugging
# - d       : -DNDEBUG
# - t       : -DNO_TRACE
# - SA      : -fsanitize address
# - ST      : -fsanitize threads
# - P       : -pg
# - C       : coverage (not operational yet)
LTO_FLAGS        := -O3 $(if $(findstring gcc,$(CXX_FLAVOR) ),-flto=2     ,-flto                  )
COVERAGE         :=     $(if $(findstring C,  $(LMAKE_FLAGS)),--coverage                          )
PROFILE          :=     $(if $(findstring P,  $(LMAKE_FLAGS)),-pg                                 )
EXTRA_LINK_FLAGS :=     $(if $(findstring P,  $(LMAKE_FLAGS)),            ,-O3                    )
EXTRA_LINK_FLAGS :=     $(if $(findstring O4, $(LMAKE_FLAGS)),$(LTO_FLAGS),$(EXTRA_LINK_FLAGS)    )
EXTRA_LINK_FLAGS :=     $(if $(findstring O3, $(LMAKE_FLAGS)),            ,$(EXTRA_LINK_FLAGS)    )
EXTRA_LINK_FLAGS :=     $(if $(findstring O2, $(LMAKE_FLAGS)),            ,$(EXTRA_LINK_FLAGS)    )
EXTRA_LINK_FLAGS :=     $(if $(findstring O1, $(LMAKE_FLAGS)),            ,$(EXTRA_LINK_FLAGS)    )
EXTRA_LINK_FLAGS :=     $(if $(findstring O0, $(LMAKE_FLAGS)),            ,$(EXTRA_LINK_FLAGS)    )
EXTRA_CC_FLAGS   :=     $(if $(findstring P,  $(LMAKE_FLAGS)),-O1         ,-O3                    )
EXTRA_CC_FLAGS   :=     $(if $(findstring O4, $(LMAKE_FLAGS)),$(LTO_FLAGS),$(EXTRA_CC_FLAGS)      )
EXTRA_CC_FLAGS   :=     $(if $(findstring O3, $(LMAKE_FLAGS)),-O3         ,$(EXTRA_CC_FLAGS)      )
EXTRA_CC_FLAGS   :=     $(if $(findstring O2, $(LMAKE_FLAGS)),-O2         ,$(EXTRA_CC_FLAGS)      )
EXTRA_CC_FLAGS   :=     $(if $(findstring O1, $(LMAKE_FLAGS)),-O1         ,$(EXTRA_CC_FLAGS)      )
EXTRA_CC_FLAGS   :=     $(if $(findstring O0, $(LMAKE_FLAGS)),-O0         ,$(EXTRA_CC_FLAGS)      )
EXTRA_CC_FLAGS   +=     $(if $(findstring g,  $(LMAKE_FLAGS)),            ,-g                     )
EXTRA_CC_FLAGS   +=     $(if $(findstring d,  $(LMAKE_FLAGS)),-DNDEBUG                            )
EXTRA_CC_FLAGS   +=     $(if $(findstring t,  $(LMAKE_FLAGS)),-DNO_TRACE                          )
HIDDEN_CC_FLAGS  +=     $(if $(findstring g,  $(LMAKE_FLAGS)),            ,-fno-omit-frame-pointer)
HIDDEN_CC_FLAGS  +=     $(if $(findstring P,  $(LMAKE_FLAGS)),-DPROFILING                         )
HIDDEN_CC_FLAGS  +=     $(if $(findstring O0, $(LMAKE_FLAGS)),-fno-inline                         )
#
SAN_FLAGS := $(if $(findstring SA,$(LMAKE_FLAGS)),-fsanitize=address -fsanitize=undefined)
SAN_FLAGS += $(if $(findstring ST,$(LMAKE_FLAGS)),-fsanitize=thread                      )
# some user codes may have specific (and older) libs, in that case, unless flag l is used, link libstdc++ statically
LIB_STDCPP := $(if $(findstring l,$(LMAKE_FLAGS)),,-static-libstdc++)
#
WARNING_FLAGS := -Wall -Wextra -Wno-cast-function-type -Wno-type-limits -Werror
#
LINK_FLAGS           = $(if $(and $(HAS_32),$(findstring d$(LD_SO_LIB_32)/,$@)),$(LINK_LIB_PATH_32:%=-Wl$(COMMA)-rpath$(COMMA)%),$(LINK_LIB_PATH:%=-Wl$(COMMA)-rpath$(COMMA)%))
SAN                 := $(if $(strip $(SAN_FLAGS)),-san)
LINK                 = PATH=$(CXX_DIR):$$PATH $(CXX) $(COVERAGE) $(PROFILE) -pthread $(LINK_FLAGS) $(EXTRA_LINK_FLAGS)
LINK_LIB             = -ldl $(if $(and $(HAS_32),$(findstring d$(LD_SO_LIB_32)/,$@)),$(LIB_STACKTRACE_32:%=-l%),$(LIB_STACKTRACE:%=-l%))
CLANG_WARNING_FLAGS := -Wno-misleading-indentation -Wno-unknown-warning-option -Wno-c2x-extensions -Wno-c++2b-extensions
#
ifeq ($(CXX_FLAVOR),clang)
    WARNING_FLAGS += $(CLANG_WARNING_FLAGS)
endif
#
# XXX : suppress -fno-strict-aliasing when proven correct
USER_FLAGS := -std=$(CXX_STD) $(EXTRA_CC_FLAGS) $(COVERAGE) $(PROFILE)
COMPILE1   := PATH=$(CXX_DIR):$$PATH $(CXX) $(USER_FLAGS) $(HIDDEN_CC_FLAGS) -pthread $(WARNING_FLAGS) $(if $(NEED_EXPERIMENTAL_LIBRARY),-fexperimental-library)
LINT       := clang-tidy
LINT_FLAGS := $(USER_FLAGS) $(HIDDEN_CC_FLAGS) $(WARNING_FLAGS) $(CLANG_WARNING_FLAGS)
LINT_CHKS  := -checks=-clang-analyzer-optin.core.EnumCastOutOfRange
LINT_OPTS  := '-header-filter=.*' $(LINT_CHKS)

# On ubuntu, seccomp.h is in /usr/include. On CenOS7, it is in /usr/include/linux, but beware that otherwise, /usr/include must be prefered, hence -idirafter
CC_FLAGS := -iquote ext -iquote src -iquote src/lmakeserver -iquote . -idirafter /usr/include/linux

PCRE_LIB := $(if $(HAS_PCRE),-lpcre2-8)
Z_LIB    := $(if $(HAS_ZSTD),-lzstd,$(if $(HAS_ZLIB),-lz))

PY2_INC_DIRS := $(if $(PYTHON2),$(filter-out $(STD_INC_DIRS),$(PY2_INCLUDEDIR) $(PY2_INCLUDEPY))) # for some reasons, compilation breaks if standard inc dirs are given with -I
PY3_INC_DIRS :=                 $(filter-out $(STD_INC_DIRS),$(PY3_INCLUDEDIR) $(PY3_INCLUDEPY))  # .
PY2_CC_FLAGS := $(if $(PYTHON2),$(patsubst %,-I %,$(PY2_INC_DIRS)) -Wno-register)
PY3_CC_FLAGS :=                 $(patsubst %,-I %,$(PY3_INC_DIRS)) -Wno-register
#
PY2_LINK_FLAGS := $(if $(PYTHON2),$(if $(PY2_LIB_DIR),$(PY2_LIB_DIR)/$(PY2_LIB_BASE),-l:$(PY2_LIB_BASE)) $(patsubst %,-Wl$(COMMA)-rpath$(COMMA)%,$(PY2_LIB_DIR)))
PY3_LINK_FLAGS :=                 $(if $(PY3_LIB_DIR),$(PY3_LIB_DIR)/$(PY3_LIB_BASE),-l:$(PY3_LIB_BASE)) $(patsubst %,-Wl$(COMMA)-rpath$(COMMA)%,$(PY3_LIB_DIR))

PY_CC_FLAGS    = $(if $(and $(PYTHON2),$(findstring -py2,$@)),$(PY2_CC_FLAGS)  ,$(PY3_CC_FLAGS)  )
PY_LINK_FLAGS  = $(if $(and $(PYTHON2),$(findstring 2.so,$@)),$(PY2_LINK_FLAGS),$(PY3_LINK_FLAGS))
#
# /!\ LTO is incompatible with multiple definitions, even in different translation units
SLURM_CC_FLAGS = $(if $(findstring src/lmakeserver/backends/slurm_api-,$<),$(<:src/lmakeserver/backends/slurm_api-%.cc=-I ext/slurm/%) -fno-lto)
#
PY_SO          = $(if $(and $(PYTHON2),$(findstring 2.so,             $@)),-py2)
MOD_SO         = $(if $(and $(HAS_32) ,$(findstring d$(LD_SO_LIB_32)/,$@)),-m32)
MOD_O          = $(if $(and $(HAS_32) ,$(findstring -m32,             $@)),-m32)

COMPILE = $(COMPILE1) $(PY_CC_FLAGS) $(SLURM_CC_FLAGS) $(CC_FLAGS)

# XXX> : use split debug info when stacktrace supports it
SPLIT_DBG_CMD = \
	$(if $(if $(and $(HAS_32),$(findstring d$(LD_SO_LIB_32)/,$@)),$(SPLIT_DBG_32),$(SPLIT_DBG)) , \
		( \
			cd $(@D)                                                 ; \
			$(OBJCOPY) --only-keep-debug             $(@F) $(@F).dbg ; \
			$(OBJCOPY) --strip-debug                 $(@F)           ; \
			$(OBJCOPY) --add-gnu-debuglink=$(@F).dbg $(@F)             \
		) \
	)

#
# LMAKE
#

LMAKE_SERVER_PY_FILES := \
	_lib/read_makefiles.py              \
	_lib/fmt_rule.py                    \
	_lib/serialize.py                   \
	lib/lmake/__init__.py               \
	lib/lmake/auto_sources.py           \
	lib/lmake/config.py                 \
	lib/lmake/import_machinery.py       \
	lib/lmake/py_clmake.py              \
	lib/lmake/rules.py                  \
	lib/lmake/sources.py                \
	lib/lmake/utils.py                  \
	lib/lmake_debug/__init__.py         \
	lib/lmake_debug/default.py          \
	lib/lmake_debug/enter.py            \
	lib/lmake_debug/gdb.py              \
	lib/lmake_debug/none.py             \
	lib/lmake_debug/pudb.py             \
	lib/lmake_debug/vscode.py           \
	lib/lmake_debug/utils.py            \
	lib/lmake_debug/runtime/__init__.py \
	lib/lmake_debug/runtime/pdb_.py     \
	lib/lmake_debug/runtime/pudb_.py    \
	lib/lmake_debug/runtime/vscode.py   \
	lib/lmake_debug/runtime/utils.py

LMAKE_SERVER_BIN_FILES := \
	_bin/align_comments          \
	_bin/ldump                   \
	_bin/ldump_job               \
	_bin/lkpi                    \
	_bin/lmakeserver             \
	_bin/find_cc_ld_library_path \
	bin/lautodep                 \
	bin/ldebug                   \
	bin/lforget                  \
	bin/lmake                    \
	bin/lmark                    \
	bin/lrepair                  \
	bin/lrun_cc                  \
	bin/lshow                    \
	bin/xxhsum

MAN_FILES := $(patsubst %.m,%,$(filter-out %/common.1.m,$(filter doc/man/man1/%.1.m,$(SRCS))))

LMAKE_SERVER_FILES := \
	$(LMAKE_SERVER_PY_FILES)  \
	$(LMAKE_SERVER_BIN_FILES)

LMAKE_REMOTE_SLIBS := $(if $(HAS_LD_AUDIT),ld_audit.so) ld_preload.so ld_preload_jemalloc.so
LMAKE_REMOTE_FILES := \
	$(if $(HAS_32),$(patsubst %,_d$(LD_SO_LIB_32)/%,$(LMAKE_REMOTE_SLIBS))) \
	$(patsubst %,_d$(LD_SO_LIB)/%,$(LMAKE_REMOTE_SLIBS))                    \
	$(if $(HAS_PY3_DYN),lib/clmake.so)                                      \
	$(if $(HAS_PY2_DYN),lib/clmake2.so)                                     \
	_bin/job_exec                                                           \
	bin/lcheck_deps                                                         \
	bin/ldecode                                                             \
	bin/lencode                                                             \
	bin/ldepend                                                             \
	bin/ltarget

LMAKE_DOC_FILES := \
	doc/lmake_doc.pptx \
	$(MAN_FILES)

LMAKE_BASIC_OBJS := \
	src/disk.o    \
	src/fd.o      \
	src/hash.o    \
	src/lib.o     \
	src/process.o \
	src/time.o    \
	src/utils.o

LMAKE_BASIC_SAN_OBJS := $(LMAKE_BASIC_OBJS:%.o=%$(SAN).o)

LMAKE_FILES        := $(LMAKE_SERVER_FILES) $(LMAKE_REMOTE_FILES)
LMAKE_BIN_FILES    := $(filter bin/%,$(LMAKE_FILES))
LMAKE_DBG_FILES    :=                                # this variable is progressively completed
LMAKE_DBG_FILES_32 :=                                # .

DOCKER_FILES := $(filter docker/%.docker,$(SRCS))

LMAKE_ALL_FILES     := $(LMAKE_FILES) $(LMAKE_DOC_FILES)
LMAKE_ALL_FILES_DBG := $(LMAKE_ALL_FILES) $(LMAKE_BIN_FILES)

LINT : $(patsubst %.cc,%.chk, $(filter-out %.x.cc,$(filter %.cc,$(SRCS))) )

DFLT_TGT : LMAKE_TGT UNIT_TESTS LMAKE_TEST lmake.tar.gz
DFLT     : DFLT_TGT.SUMMARY

ALL_TGT : DFLT_TGT LINT STORE_TEST
ALL     : ALL_TGT.SUMMARY

%.inc_stamp : % # prepare a stamp to be included, so as to force availability of a file w/o actually including it
	>$@

ext/%.dir.stamp : ext/%.tar.gz
	@rm -rf $(@:%.stamp=%)
	@mkdir -p $(@:%.stamp=%)
	tar -zxf $< -C $(@:%.stamp=%)
	touch $@
ext/%.dir.stamp : ext/%.zip
	@rm -rf $(@:%.stamp=%)
	@mkdir -p $(@:%.stamp=%)
	unzip -d $(@:%.stamp=%) $<
	touch $@

.SECONDARY :

#
# versioning
#

VERSION_SRCS := $(filter src/%.cc,$(SRCS)) $(filter src/%.hh,$(SRCS))

# use a stamp to implement a by value update (while make works by date)
version.hh.stamp : _bin/version Manifest $(VERSION_SRCS)
	@VERSION=$(VERSION) TAG=$(TAG) ./$< $(VERSION_SRCS) >$@
	@# dont touch output if it is steady
	@if cmp -s $@ $(@:%.stamp=%) ; then                        echo steady version ; \
	else                                cp $@ $(@:%.stamp=%) ; echo new    version ; \
	fi
version.hh : version.hh.stamp ;

#
# LMAKE
#

# add system configuration to lmake.py :
# Sense git bin dir at install time so as to be independent of it at run time.
# Some python installations require LD_LIBRARY_PATH. Handle this at install time so as to be independent at run time.
lib/%.py _lib/%.py : _lib/%.src.py sys_config.mk
	@echo customize $< to $@
	@mkdir -p $(@D)
	@sed \
		-e 's!\$$BASH!$(BASH)!'                          \
		-e 's!\$$GIT!$(GIT)!'                            \
		-e 's!\$$HAS_LD_AUDIT!$(HAS_LD_AUDIT)!'          \
		-e 's!\$$LD_LIBRARY_PATH!$(PY_LD_LIBRARY_PATH)!' \
		-e 's!\$$PYTHON2!$(PYTHON2)!'                    \
		-e 's!\$$PYTHON!$(PYTHON)!'                      \
		-e 's!\$$STD_PATH!$(STD_PATH)!'                  \
		-e 's!\$$TAG!$(TAG)!'                            \
		-e 's!\$$VERSION!$(VERSION)!'                    \
		$< >$@
# for other files, just copy
lib/% : _lib/%
	@mkdir -p $(@D)
	cp $< $@
# idem for bin
bin/% : _bin/%
	@mkdir -p $(@D)
	cp $< $@

LMAKE_DOC    : $(LMAKE_DOC_FILES)
LMAKE_SERVER : $(LMAKE_SERVER_FILES)
LMAKE_REMOTE : $(LMAKE_REMOTE_FILES)
LMAKE_TGT    : LMAKE_DOC LMAKE_SERVER LMAKE_REMOTE
LMAKE        : LMAKE_TGT.SUMMARY

#
# store
#

STORE_TEST : src/store/unit_test.dir/tok src/store/big_test.dir/tok

src/store/unit_test : \
	$(LMAKE_BASIC_SAN_OBJS) \
	src/app$(SAN).o         \
	src/trace$(SAN).o       \
	src/store/unit_test$(SAN).o
	$(LINK) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

src/store/unit_test.dir/tok : src/store/unit_test
	@rm -rf   $(@D)
	@mkdir -p $(@D)
	./$<      $(@D)
	@touch      $@

src/store/big_test.dir/tok : src/store/big_test.py LMAKE
	@mkdir -p $(@D)
	@rm -rf   $(@D)/LMAKE
	PATH=$$PWD/_bin:$$PWD/bin:$$PATH ; ( cd $(@D) ; $(PYTHON) ../big_test.py / 2000000 )
	@touch $@

#
# compilation
#

%.i     : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -E              -o $@ $<
%-m32.i : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -E -m32         -o $@ $<
%-py2.i : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -E              -o $@ $<
%-san.i : %.cc ; @echo $(CXX) $(USER_FLAGS) $(SAN_FLAGS) to $@ ; $(COMPILE) -E $(SAN_FLAGS) -o $@ $<

%.s     : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -S -fverbose-asm              -o $@ $<
%-m32.s : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -S -fverbose-asm -m32         -o $@ $<
%-py2.s : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -S -fverbose-asm              -o $@ $<
%-san.s : %.cc ; @echo $(CXX) $(USER_FLAGS) $(SAN_FLAGS) to $@ ; $(COMPILE) -S -fverbose-asm $(SAN_FLAGS) -o $@ $<

COMPILE_O = $(COMPILE) -c -frtti -fPIC
%.o     : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE_O)              -o $@ $<
%-m32.o : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE_O) -m32         -o $@ $<
%-py2.o : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE_O)              -o $@ $<
%-san.o : %.cc ; @echo $(CXX) $(USER_FLAGS) $(SAN_FLAGS) to $@ ; $(COMPILE_O) $(SAN_FLAGS) -o $@ $<

%.chk   : %.cc ; @echo $(LINT) $(USER_FLAGS) to $@ ; $(LINT) $< $(LINT_OPTS) -- $(LINT_FLAGS) $(PY_CC_FLAGS) $(CC_FLAGS) >$@ ; [ ! -s $@ ]

%.d : %.cc
	@$(COMPILE) \
		-MM -MG                                              \
		-MF $@                                               \
		-MT '$(@:%.d=%.i) $(@:%.d=%-m32.i) $(@:%-py2.d=%.i)' \
		-MT '$(@:%.d=%.s) $(@:%.d=%-m32.s) $(@:%.d=%-py2.s)' \
		-MT '$(@:%.d=%.o) $(@:%.d=%-m32.o) $(@:%.d=%-py2.o)' \
		-MT '$(@:%.d=%-san.o)'                               \
		-MT '$(@:%.d=%.chk)'                                 \
		-MT '$@'                                             \
		$< 2>/dev/null || :

SLURM_SRCS := $(patsubst ext/slurm/%/slurm/slurm.h,src/lmakeserver/backends/slurm_api-%.cc,$(filter ext/slurm/%/slurm/slurm.h,$(SRCS)))
DEP_SRCS := $(filter-out %.x.cc,$(filter src/%.cc,$(SRCS))) $(patsubst %.cc,%.d,$(SLURM_SRCS))
include $(if $(findstring 1,$(SYS_CONFIG_OK)) , $(patsubst %.cc,%.d, $(DEP_SRCS) ) )

#
# slurm
#

src/lmakeserver/backends/slurm_api-%.cc :
	@echo generate $@
	@(	echo '#define SLURM_VERSION   "$*"'                  \
	;	echo '#define SLURM_NAMESPACE Slurm_$(subst .,_,$*)' \
	;	echo '#include "slurm_api.x.cc"'                     \
	) >$@

#
# lmake
#

# on CentOS7, gcc looks for libseccomp.so with -lseccomp, but only libseccomp.so.2 exists, and this works everywhere.
SECCOMP_LIB := $(if $(HAS_SECCOMP),-l:libseccomp.so.2)

RPC_JOB_SAN_OBJS := \
	src/rpc_job$(SAN).o          \
	src/caches/dir_cache$(SAN).o

CLIENT_SAN_OBJS := \
	$(LMAKE_BASIC_SAN_OBJS)   \
	src/app$(SAN).o        \
	src/client$(SAN).o     \
	src/rpc_client$(SAN).o \
	src/trace$(SAN).o

SERVER_SAN_OBJS := \
	$(LMAKE_BASIC_SAN_OBJS)         \
	$(RPC_JOB_SAN_OBJS)             \
	src/app$(SAN).o                 \
	src/py$(SAN).o                  \
	src/re$(SAN).o                  \
	src/rpc_client$(SAN).o          \
	src/rpc_job_exec$(SAN).o        \
	src/trace$(SAN).o               \
	src/autodep/backdoor$(SAN).o    \
	src/autodep/env$(SAN).o         \
	src/autodep/ld_server$(SAN).o   \
	src/autodep/record$(SAN).o      \
	src/autodep/syscall_tab$(SAN).o \
	src/lmakeserver/backend$(SAN).o \
	src/lmakeserver/codec$(SAN).o   \
	src/lmakeserver/global$(SAN).o  \
	src/lmakeserver/config$(SAN).o  \
	src/lmakeserver/job$(SAN).o     \
	src/lmakeserver/node$(SAN).o    \
	src/lmakeserver/req$(SAN).o     \
	src/lmakeserver/rule$(SAN).o    \
	src/lmakeserver/store$(SAN).o

_bin/lmakeserver : \
	$(SERVER_SAN_OBJS)                       \
	src/non_portable$(SAN).o                 \
	src/autodep/gather$(SAN).o               \
	src/autodep/ptrace$(SAN).o               \
	src/lmakeserver/cmd$(SAN).o              \
	src/lmakeserver/makefiles$(SAN).o        \
	src/lmakeserver/backends/local$(SAN).o   \
	src/lmakeserver/backends/slurm$(SAN).o   \
	$(patsubst %.cc,%$(SAN).o,$(SLURM_SRCS)) \
	src/lmakeserver/backends/sge$(SAN).o     \
	src/lmakeserver/main$(SAN).o

bin/lrepair : \
	$(SERVER_SAN_OBJS)                     \
	src/non_portable$(SAN).o               \
	src/autodep/gather$(SAN).o             \
	src/autodep/ptrace$(SAN).o             \
	src/lmakeserver/makefiles$(SAN).o      \
	src/lmakeserver/backends/local$(SAN).o \
	src/lmakeserver/backends/slurm$(SAN).o \
	src/lmakeserver/backends/sge$(SAN).o   \
	src/lrepair$(SAN).o

_bin/ldump : \
	$(SERVER_SAN_OBJS) \
	src/ldump$(SAN).o

_bin/lkpi : \
	$(SERVER_SAN_OBJS) \
	src/lkpi$(SAN).o

LMAKE_DBG_FILES += _bin/lmakeserver bin/lrepair _bin/ldump _bin/lkpi
_bin/lmakeserver bin/lrepair _bin/ldump _bin/lkpi :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(SECCOMP_LIB) $(Z_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

bin/lmake   : $(CLIENT_SAN_OBJS) src/lmake$(SAN).o
bin/lshow   : $(CLIENT_SAN_OBJS) src/lshow$(SAN).o
bin/lforget : $(CLIENT_SAN_OBJS) src/lforget$(SAN).o
bin/lmark   : $(CLIENT_SAN_OBJS) src/lmark$(SAN).o
bin/ldebug  : $(CLIENT_SAN_OBJS) src/ldebug$(SAN).o src/py$(SAN).o

LMAKE_DBG_FILES += bin/lmake bin/lshow bin/lforget bin/lmark
bin/lmake bin/lshow bin/lforget bin/lmark :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

LMAKE_DBG_FILES += bin/ldebug
bin/ldebug :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

LMAKE_DBG_FILES += _bin/ldump_job
_bin/ldump_job : \
	$(LMAKE_BASIC_SAN_OBJS) \
	$(RPC_JOB_SAN_OBJS)     \
	src/app$(SAN).o         \
	src/trace$(SAN).o       \
	src/autodep/env$(SAN).o \
	src/ldump_job$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(Z_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

LMAKE_DBG_FILES += _bin/align_comments
_bin/align_comments : \
	$(LMAKE_BASIC_SAN_OBJS) \
	src/align_comments$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

LMAKE_DBG_FILES += bin/xxhsum
# xxhsum may be used in jobs and is thus incompatible with sanitize
bin/xxhsum : \
	$(LMAKE_BASIC_OBJS) \
	src/app.o           \
	src/trace.o         \
	src/xxhsum.o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) -o $@ $^ $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

#
# remote
#

# remote executables generate errors when -fsanitize=thread, but are mono-thread, so we don't care

BASIC_REMOTE_OBJS := \
	$(LMAKE_BASIC_OBJS)    \
	src/rpc_job_exec.o     \
	src/autodep/backdoor.o \
	src/autodep/env.o      \
	src/autodep/record.o

AUTODEP_OBJS := \
	$(BASIC_REMOTE_OBJS) \
	src/autodep/syscall_tab.o
REMOTE_OBJS  := \
	$(BASIC_REMOTE_OBJS)   \
	src/app.o              \
	src/trace.o            \
	src/autodep/job_support.o

JOB_EXEC_SAN_OBJS := \
	$(AUTODEP_OBJS:%.o=%$(SAN).o) \
	$(RPC_JOB_SAN_OBJS)           \
	src/app$(SAN).o               \
	src/non_portable$(SAN).o      \
	src/re$(SAN).o                \
	src/trace$(SAN).o             \
	src/autodep/gather$(SAN).o    \
	src/autodep/ptrace$(SAN).o    \
	src/autodep/record$(SAN).o

_bin/job_exec : $(JOB_EXEC_SAN_OBJS) $(CACHE_SAN_OBJS) src/job_exec$(SAN).o
bin/lautodep  : $(JOB_EXEC_SAN_OBJS) src/py.o          src/autodep/lautodep$(SAN).o

LMAKE_DBG_FILES += _bin/job_exec bin/lautodep
_bin/job_exec bin/lautodep :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(SECCOMP_LIB) $(Z_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

LMAKE_DBG_FILES += bin/ldecode bin/ldepend bin/lencode bin/ltarget bin/lcheck_deps
bin/ldecode     : $(REMOTE_OBJS) src/autodep/ldecode.o
bin/ldepend     : $(REMOTE_OBJS) src/autodep/ldepend.o
bin/lencode     : $(REMOTE_OBJS) src/autodep/lencode.o
bin/ltarget     : $(REMOTE_OBJS) src/autodep/ltarget.o
bin/lcheck_deps : $(REMOTE_OBJS) src/autodep/lcheck_deps.o

bin/% :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) -o $@ $^ $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

# remote libs generate errors when -fsanitize=thread # XXX! fix these errors and use $(SAN)

LMAKE_DBG_FILES    += $(if $(HAS_LD_AUDIT),_d$(LD_SO_LIB)/ld_audit.so   ) _d$(LD_SO_LIB)/ld_preload.so    _d$(LD_SO_LIB)/ld_preload_jemalloc.so
LMAKE_DBG_FILES_32 += $(if $(HAS_LD_AUDIT),_d$(LD_SO_LIB_32)/ld_audit.so) _d$(LD_SO_LIB_32)/ld_preload.so _d$(LD_SO_LIB_32)/ld_preload_jemalloc.so
_d$(LD_SO_LIB)/ld_audit.so               : $(AUTODEP_OBJS)             src/autodep/ld_audit.o
_d$(LD_SO_LIB)/ld_preload.so             : $(AUTODEP_OBJS)             src/autodep/ld_preload.o
_d$(LD_SO_LIB)/ld_preload_jemalloc.so    : $(AUTODEP_OBJS)             src/autodep/ld_preload_jemalloc.o
_d$(LD_SO_LIB_32)/ld_audit.so            : $(AUTODEP_OBJS:%.o=%-m32.o) src/autodep/ld_audit-m32.o
_d$(LD_SO_LIB_32)/ld_preload.so          : $(AUTODEP_OBJS:%.o=%-m32.o) src/autodep/ld_preload-m32.o
_d$(LD_SO_LIB_32)/ld_preload_jemalloc.so : $(AUTODEP_OBJS:%.o=%-m32.o) src/autodep/ld_preload_jemalloc-m32.o

LMAKE_DBG_FILES += lib/clmake.so $(if $(HAS_PY2_DYN),lib/clmake2.so)
lib/clmake.so lib/clmake2.so : SO_FLAGS = $(PY_LINK_FLAGS)
lib/clmake.so                : $(REMOTE_OBJS) src/py.o     src/autodep/clmake.o
lib/clmake2.so               : $(REMOTE_OBJS) src/py-py2.o src/autodep/clmake-py2.o

%.so :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) -shared $(LIB_STDCPP) $(MOD_SO) -o $@ $^ $(SO_FLAGS) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

#
# Unit tests
#

UT_BASE := $(filter     unit_tests/base/%,$(SRCS))
UT_SRCS := $(filter-out unit_tests/base/%,$(SRCS))

UNIT_TESTS : \
	$(patsubst %.py,%.dir/tok,     $(filter unit_tests/%.py,    $(UT_SRCS))) \
	$(patsubst %.script,%.dir/tok, $(filter unit_tests/%.script,$(UT_SRCS))) \
	$(patsubst %.dir/run,%.dir/tok,$(filter examples/%.dir/run ,$(UT_SRCS)))

TEST_ENV = \
	export PATH=$(REPO_ROOT)/bin:$(REPO_ROOT)/_bin:$$PATH                                          ; \
	export PYTHONPATH=$(REPO_ROOT)/lib:$(REPO_ROOT)/_lib:$(REPO_ROOT)/unit_tests/base:$$PYTHONPATH ; \
	export CXX=$(CXX)                                                                              ; \
	export LD_LIBRARY_PATH=$(PY_LIB_DIR)                                                           ; \
	export HAS_32=$(HAS_32)                                                                        ; \
	export PYTHON2=$(PYTHON2)                                                                      ; \
	exec </dev/null >$@.out 2>$@.err

# keep $(@D) to ease debugging, ignore git rc as old versions work but generate errors
TEST_PRELUDE = \
	mkdir -p $(@D) ; \
	( cd $(@D) ; git clean -ffdxq >/dev/null 2>/dev/null ; : ; ) ;

TEST_POSTLUDE = \
	if [ $$? = 0 ] ;                                                                                             \
	then ( if [ ! -f $(@D)/skipped ] ; then mv $@.out $@ ; else echo skipped $@ : $$(cat $(@D)/skipped) ; fi ) ; \
	else ( cat $@.out $@.err ; exit 1                                                                        ) ; \
	fi ;

%.dir/tok : %.script $(LMAKE_FILES) $(UT_BASE) _bin/ut_launch
	@echo script test to $@
	@$(TEST_PRELUDE)
	@for f in $(UT_BASE) ; do df=$(@D)/$${f#unit_tests/base/} ; mkdir -p $$(dirname $$df) ; cp $$f $$df ; done
	@cd $(@D) ; find . -type f -printf '%P\n' > Manifest
	@( $(TEST_ENV) ; cd $(@D) ; $(REPO_ROOT)/$< ) ; $(TEST_POSTLUDE)

%.dir/tok : %.py $(LMAKE_FILES) _lib/ut.py
	@echo py test to $@
	@$(TEST_PRELUDE)
	@cp $< $(@D)/Lmakefile.py
	@( $(TEST_ENV) ; cd $(@D) ; $(PYTHON) Lmakefile.py ) ; $(TEST_POSTLUDE)

# examples can alter their source to show what happens to the user, so copy in a trial dir before execution
%.dir/tok : %.dir/Lmakefile.py %.dir/run $(LMAKE_FILES) _lib/ut.py
	@echo run example to $@
	@$(TEST_PRELUDE)
	@(                                                                                      \
		trial=$(@:%.dir/tok=%.trial) ;                                                      \
		mkdir -p $$trial ;                                                                  \
		rm -rf $$trial/* ;                                                                  \
		tar -c -C$(@D) $(patsubst $(@D)/%,%,$(filter $(@D)/%,$(SRCS))) | tar -x -C$$trial ; \
		$(TEST_ENV) ; cd $(@:%.dir/tok=%.trial) ; ./run                                     \
	) ; $(TEST_POSTLUDE)

#
# lmake under lmake
#

LMAKE_ENV  : lmake_env/stamp
LMAKE_TEST : lmake_env/tok

LMAKE_SRCS := \
	$(filter _bin/%,$(SRCS)) \
	$(filter _lib/%,$(SRCS)) \
	$(filter doc/%, $(SRCS)) \
	$(filter ext/%, $(SRCS)) \
	$(filter src/%, $(SRCS))
LMAKE_ENV_SRCS := $(filter lmake_env/%,$(SRCS))

lmake_env/Manifest : Manifest
	@mkdir -p $(@D)
	@for f in $(LMAKE_SRCS)     ; do echo $$f              ; done > $@
	@for f in $(LMAKE_ENV_SRCS) ; do echo $${f#lmake_env/} ; done >>$@
	@echo $(@F)                                                   >>$@
	@echo generate $@
lmake_env/% : %
	@mkdir -p $(@D)
	cp $< $@
lmake_env/stamp : $(LMAKE_ALL_FILES) lmake_env/Manifest $(patsubst %,lmake_env/%,$(LMAKE_SRCS))
	@mkdir -p lmake_env-cache/LMAKE
	echo '300M' > lmake_env-cache/LMAKE/size
	@touch $@
	@echo init lmake_env-cache
lmake_env/tok : lmake_env/stamp lmake_env/Lmakefile.py
	@set -e ;                                                                \
	cd lmake_env ;                                                           \
	export CXX=$(CXX) ;                                                      \
	rc=0 ;                                                                   \
	$(REPO_ROOT)/bin/lmake lmake.tar.gz -Vn & sleep 1 ;                      \
	$(REPO_ROOT)/bin/lmake lmake.tar.gz >$(@F) || { rm -f $(@F) ; rc=1 ; } ; \
	wait $$!                                   || { rm -f $(@F) ; rc=1 ; } ; \
	exit $$rc

#
# dockers
#

DOCKER : $(DOCKER_FILES)
	@for df in $^ ; do                            \
		d=$${df%.docker}                        ; \
		d=$${d##*/}                             ; \
		echo                                    ; \
		echo '*'                                ; \
		echo '*' build docker $$d from $$df     ; \
		echo '*'                                ; \
		sudo docker build -f $$df -t $$d docker ; \
	done

#
# doc
#

_bin/mdbook :
	#wget -O- https://github.com/rust-lang/mdBook/releases/download/v0.4.44/mdbook-v0.4.44-x86_64-unknown-linux-gnu.tar.gz  | tar -xz -C_bin # requires recent libraries >=ubuntu22.04
	wget  -O- https://github.com/rust-lang/mdBook/releases/download/v0.4.44/mdbook-v0.4.44-x86_64-unknown-linux-musl.tar.gz | tar -xz -C_bin # static libc

doc/man/man1/%.1 : doc/man/man1/%.1.m doc/man/utils.mh doc/man/man1/common.1.m
	@echo generate man to $@
	@m4  doc/man/utils.mh doc/man/man1/common.1.m $< | sed -e 's:^[\t ]*::' -e 's:-:\\-:g' -e '/^$$/ d' >$@

# html doc is under git as _bin/mdbook cannot be downloaded in launchpad.net
# also this makes the html doc directly available on github
BOOK : _bin/mdbook doc/book.toml $(filter doc/src/%.md,$(SRCS)) $(MAN_FILES)
	@echo generate book to $@
	@rm -rf docs doc/book
	@cd doc ; ../_bin/mdbook build
	@mkdir -p doc/book/man/man1
	@cd doc ; for f in $(patsubst doc/man/man1/%.1,%,$(MAN_FILES)) ; do groff -Thtml -man man/man1/$$f.1 > book/man/man1/$$f.html ; done
	@rm doc/grohtml-*.png
	@rm -rf docs ; mv doc/book docs
	@find docs -name '.*.swp' -exec rm {} \;
	@git ls-files docs | sort > doc/book.ref_manifest
	@find docs -type f | sort > doc/book.actual_manifest
	@diff doc/book.ref_manifest doc/book.actual_manifest

#
# packaging
#

# as of now, stacktrace is incompatible with split debug info
LMAKE_DBG_FILES_ALL := $(patsubst %,%.dbg,$(if $(SPLIT_DBG),$(LMAKE_DBG_FILES)) $(if $(and $(HAS_32),$(SPLIT_DBG_32)),$(LMAKE_DBG_FILES_32)))

ARCHIVE_DIR := open-lmake-$(VERSION)
lmake.tar.gz  : TAR_COMPRESS := z
lmake.tar.bz2 : TAR_COMPRESS := j
lmake.tar.gz lmake.tar.bz2 : $(LMAKE_ALL_FILES)
	@rm -rf $(ARCHIVE_DIR)
	@mkdir -p $(ARCHIVE_DIR)
	@tar -c $(LMAKE_ALL_FILES) $(LMAKE_DBG_FILES_ALL) $$(find docs -type f)| tar -x -C$(ARCHIVE_DIR)
	tar c$(TAR_COMPRESS) -f $@ $(ARCHIVE_DIR)

VERSION_TAG := $(VERSION).$(TAG)

#
# Build debian package
# to install on one of $(DISTROS) :
# - sudo apt-add-repository ppa:cdouady/open-lmake
# - sudo apt update
# - sudo apt install open-lmake
#

DEBIAN_DIR := open-lmake-$(VERSION_TAG)
DEBIAN_TAG := open-lmake_$(VERSION_TAG)

EXAMPLE_FILES := $(filter examples/%,$(SRCS))

# Install debian packages needed to build open-lmake package
DEBIAN_DEPS :
	sudo -k apt install dh-make devscripts debhelper equivs
	sudo -k mk-build-deps --install debian/control

#
# /!\ these rules are necessary for debian packaging to work, they are not primarily made to be executed by user
#
install : $(LMAKE_ALL_FILES) $(EXAMPLE_FILES)
	@echo -n installing ...
	@set -e ; for f in $(LMAKE_SERVER_BIN_FILES) $(LMAKE_REMOTE_FILES) ; do install -D        $$f              $(DESTDIR)/usr/lib/open-lmake/$$f       ; done
	@set -e ; for f in $(LMAKE_DBG_FILES_ALL) $(LMAKE_SERVER_PY_FILES) ; do install -D -m 644 $$f              $(DESTDIR)/usr/lib/open-lmake/$$f       ; done
	@set -e ; for f in $(EXAMPLE_FILES)                                ; do install -D -m 644 $$f              $(DESTDIR)/usr/share/doc/open-lmake/$$f ; done
	@set -e ; for f in $$(find docs -type f)                           ; do install -D -m 644 $$f              $(DESTDIR)/usr/share/doc/open-lmake/$$f ; done
	@set -e ;                                                               install -D -m 644 apparmor-profile $(DESTDIR)/etc/apparmor.d/open-lmake
	@echo '' done

clean :
	@echo -n cleaning ...
	@rm -rf Manifest.inc_stamp sys_config.log sys_config.trial sys_config.mk sys_config.h sys_config.sum sys_config.err
	@find . -name '*.d' -type f | xargs rm -f
	@rm -f $(SLURM_SRCS)
	@echo '' done

DEBIAN_SRC : $(patsubst %,$(DEBIAN_TAG)-$(DEBIAN_RELEASE)~%_source.changes , $(DISTROS) )
DEBIAN_BIN : $(DEBIAN_TAG).bin_stamp
DEBIAN     : DEBIAN_BIN DEBIAN_SRC

DEBIAN_SRCS   := $(filter-out debian/%,$(filter-out unit_tests/%,$(filter-out lmake_env/%,$(SRCS))))
DEBIAN_DEBIAN := $(filter debian/%,$(SRCS))
DEBIAN_COPY   := $(filter-out %.src,$(DEBIAN_DEBIAN))

# as of now, stacktrace is incompatible with split debug info
$(DEBIAN_TAG).orig.tar.gz : $(DEBIAN_SRCS)
	@echo generate debian dir $(DEBIAN_DIR)
	@rm -rf $(DEBIAN_DIR) ; mkdir -p $(DEBIAN_DIR)
	@tar -c $(DEBIAN_SRCS) Manifest | tar -x -C$(DEBIAN_DIR)
	@echo LMAKE_FLAGS=gtl > $(DEBIAN_DIR)/sys_config.env
	@tar -cz -C$(DEBIAN_DIR) -f $@ .
	@tar -c $(DEBIAN_COPY) | tar -x -C$(DEBIAN_DIR)
	@{ for f in                   $(LMAKE_BIN_FILES)  ; do echo /usr/lib/open-lmake/$$f       /usr/$$f                   ; done ; } > $(DEBIAN_DIR)/debian/open-lmake.links
	@{ for f in $(if $(SPLIT_DBG),$(LMAKE_BIN_FILES)) ; do echo /usr/lib/open-lmake/$$f.dbg   /usr/lib/debug/usr/$$f.dbg ; done ; } >>$(DEBIAN_DIR)/debian/open-lmake.links
	@{ for f in                   doc docs examples   ; do echo /usr/share/doc/open-lmake/$$f /usr/lib/open-lmake/$$f    ; done ; } >>$(DEBIAN_DIR)/debian/open-lmake.links
	@{ for f in                   $(MAN_FILES)        ; do echo $$f                                                      ; done ; } > $(DEBIAN_DIR)/debian/open-lmake.manpages

$(DEBIAN_TAG)-%_source.changes : $(DEBIAN_TAG).orig.tar.gz $(DEBIAN_DEBIAN)
	@rm -rf $(DEBIAN_DIR)-$*
	@cp -a $(DEBIAN_DIR) $(DEBIAN_DIR)-$*
	@RELEASE='$*' ;                                \
	sed                                            \
		-e 's!\$$VERSION_TAG!$(VERSION_TAG)!g'     \
		-e 's!\$$DEBIAN_RELEASE!'"$$RELEASE"'!g'   \
		-e 's!\$$OS_CODENAME!'"$${RELEASE#*~}"'!g' \
		-e 's!\$$DATE!'"$$(date -R)"'!g'           \
		debian/changelog.src >$(DEBIAN_DIR)-$$RELEASE/debian/changelog
	@RELEASE='$*'                                                           ; \
	KEY=$$(echo $$(gpg --list-keys|grep -x ' *[0-9A-Z]\+') )                ; \
	echo generate source package in $(DEBIAN_DIR)-$$RELEASE using key $$KEY ; \
	( cd $(DEBIAN_DIR)-$$RELEASE ; MAKEFLAGS= MAKELEVEL= debuild -S -us -k$$KEY ) >$@.log
	@echo upload command : dput ppa:cdouady/open-lmake $@

# ensure bin and src package constructions are serialized
$(DEBIAN_TAG).bin_stamp : $(DEBIAN_TAG).orig.tar.gz $(DEBIAN_DEBIAN)
	@echo generate bin package in $(DEBIAN_DIR)-bin
	@rm -rf $(DEBIAN_DIR)-bin
	@cp -a $(DEBIAN_DIR) $(DEBIAN_DIR)-bin
	@. /etc/os-release ;                               \
	sed                                                \
		-e 's!\$$VERSION_TAG!$(VERSION_TAG)!g'         \
		-e 's!\$$DEBIAN_RELEASE!$(DEBIAN_RELEASE)!g'   \
		-e 's!\$$OS_CODENAME!'"$$VERSION_CODENAME"'!g' \
		-e 's!\$$DATE!'"$$(date -R)"'!g'               \
		debian/changelog.src >$(DEBIAN_DIR)-bin/debian/changelog
	# work around a lintian bug that reports elf-error warnings for debug symbol files # XXX! : find a way to filter out these lines more cleanly
	@cd $(DEBIAN_DIR)-bin                                                                                                                         ; \
	{ MAKEFLAGS= MAKELEVEL= debuild -b -us -uc ; rc=$$? ; } | grep -vx 'W:.*\<elf-error\>.* Unable to find program interpreter name .*\[.*.dbg\]' ; \
	exit $$rc
	@touch $@
