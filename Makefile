# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#
# user configurable
MAKEFLAGS := -j$(shell nproc||echo 1) -k
# mandatory
MAKEFLAGS += -r -R

.DEFAULT_GOAL := DFLT

$(shell { echo CXX=$$CXX ; echo PYTHON2=$$PYTHON2 ; echo PYTHON=$$PYTHON ; } >sys_config_env.tmp                                                     )
$(shell cmp sys_config_env sys_config_env.tmp 2>/dev/null || { cp sys_config_env.tmp sys_config_env ; echo new env : >&2 ; cat sys_config_env >&2 ; })
$(shell rm -f sys_config_env.tmp                                                                                                                     )

sys_config.log : _bin/sys_config sys_config_env
	. ./sys_config_env ; ./$< $(@:%.log=%.mk) $(@:%.log=%.h) 2>$@ || cat $@
sys_config.mk : sys_config.log ;+@[ -f $@ ] || { echo "cannot find $@" ; exit 1 ; }
sys_config.h  : sys_config.log ;+@[ -f $@ ] || { echo "cannot find $@" ; exit 1 ; }

# defines  HAS_SECCOMP & HAS_SLURM
include sys_config.mk

# this is the recommanded way to insert a , when calling functions
# /!\ cannot put a comment on the following line or a lot of spaces will be inserted in the variable definition
COMMA := ,

# syntax for LMAKE_FLAGS : O[0123]G?D?T?S[AT]C?
# - O[0123] : compiler optimization level, defaults to 3
# - G       : -g
# - D       : -DNDEBUG
# - T       : -DNO_TRACE
# - SA      : -fsanitize address
# - ST      : -fsanitize threads
# - C       : coverage (not operational yet)
OPT_FLAGS    := -O3
OPT_FLAGS    := $(if $(findstring O2,$(LMAKE_FLAGS)),-O2,$(OPT_FLAGS))
OPT_FLAGS    := $(if $(findstring O1,$(LMAKE_FLAGS)),-O1,$(OPT_FLAGS))
OPT_FLAGS    := $(if $(findstring O0,$(LMAKE_FLAGS)),-O0,$(OPT_FLAGS))
EXTRA_FLAGS  += $(if $(findstring G, $(LMAKE_FLAGS)),-g)
HIDDEN_FLAGS += $(if $(findstring G, $(LMAKE_FLAGS)),-fno-omit-frame-pointer)
EXTRA_FLAGS  += $(if $(findstring d, $(LMAKE_FLAGS)),-DNDEBUG)
EXTRA_FLAGS  += $(if $(findstring t, $(LMAKE_FLAGS)),-DNO_TRACE)
SAN_FLAGS    += $(if $(findstring SA,$(LMAKE_FLAGS)),-fsanitize=address -fsanitize=undefined)
SAN_FLAGS    += $(if $(findstring ST,$(LMAKE_FLAGS)),-fsanitize=thread)
COVERAGE     += $(if $(findstring C, $(LMAKE_FLAGS)),--coverage)
#
WARNING_FLAGS := -Wall -Wextra -Wno-cast-function-type -Wno-type-limits
#
LANG := c++20
#
SAN       := $(if $(strip $(SAN_FLAGS)),.san,)
LINK_OPTS := $(patsubst %,-Wl$(COMMA)-rpath=%,$(LINK_LIB_PATH)) -pthread # e.g. : -Wl,-rpath=/a/b -Wl,-rpath=/c -pthread
LINK_O    := $(CXX) $(COVERAGE) -r
LINK_SO   := $(CXX) $(COVERAGE) $(LINK_OPTS) -shared                     # some usage may have specific libs, avoid dependencies
LINK_BIN  := $(CXX) $(COVERAGE) $(LINK_OPTS)
LINK_LIB  := -ldl
ifneq ($(HAS_PCRE),)
    LINK_LIB += -lpcre2-8
endif
#
ifeq ($(CXX_FLAVOR),clang)
    WARNING_FLAGS += -Wno-misleading-indentation -Wno-unknown-warning-option -Wno-c2x-extensions -Wno-c++2b-extensions
endif
#
USER_FLAGS := $(OPT_FLAGS) $(EXTRA_FLAGS) -std=$(LANG)
COMPILE    := $(CXX) -ftabstop=4 $(COVERAGE) $(USER_FLAGS) $(HIDDEN_FLAGS) -fvisibility=hidden -ftemplate-backtrace-limit=0 -fno-strict-aliasing -pthread -pedantic $(WARNING_FLAGS) -Werror
ROOT_DIR   := $(abspath .)
LIB        := lib
SLIB       := _lib
BIN        := bin
SBIN       := _bin
DOC        := doc
SRC        := src
LMAKE_ENV  := lmake_env
STORE_LIB  := $(SRC)/store

ifneq ($(PYTHON2),)
    PY2_INC_DIRS  := $(filter-out $(STD_INC_DIRS),$(PY2_INCLUDEDIR) $(PY2_INCLUDEPY)) # for some reasons, compilation breaks if standard inc dirs are given with -isystem
    PY2_CC_OPTS   := $(patsubst %,-isystem %,$(PY2_INC_DIRS)) -Wno-register
    PY2_LINK_OPTS := $(patsubst %,-L%,$(PY2_LIB_DIR)) $(patsubst %,-Wl$(COMMA)-rpath=%,$(PY2_LIB_DIR)) -l:$(PY2_LIB_BASE)
endif
PY_INC_DIRS  := $(filter-out $(STD_INC_DIRS),$(PY_INCLUDEDIR) $(PY_INCLUDEPY))        # for some reasons, compilation does not work if standard inc dirs are given with -isystem
PY_CC_OPTS   := $(patsubst %,-isystem %,$(PY_INC_DIRS)) -Wno-register
PY_LINK_OPTS := $(patsubst %,-L%,$(PY_LIB_DIR))  $(patsubst %,-Wl$(COMMA)-rpath=%,$(PY_LIB_DIR))  -l:$(PY_LIB_BASE)

# Engine
SRC_ENGINE  := $(SRC)/lmakeserver
SRC_BACKEND := $(SRC_ENGINE)/backends

# LMAKE
LMAKE_SERVER_PY_FILES := \
	$(SLIB)/read_makefiles.py        \
	$(SLIB)/serialize.py             \
	$(LIB)/lmake/__init__.py         \
	$(LIB)/lmake/auto_sources.py     \
	$(LIB)/lmake/import_machinery.py \
	$(LIB)/lmake/custom_importer.py  \
	$(LIB)/lmake/rules.py            \
	$(LIB)/lmake/sources.py          \
	$(LIB)/lmake/utils.py            \
	$(LIB)/lmake_dbg.py              \
	$(LIB)/lmake_runtime.py

LMAKE_SERVER_BIN_FILES := \
	$(SBIN)/lmakeserver    \
	$(SBIN)/ldump          \
	$(SBIN)/ldump_job      \
	$(SBIN)/align_comments \
	$(BIN)/autodep         \
	$(BIN)/ldebug          \
	$(BIN)/lforget         \
	$(BIN)/lmake           \
	$(BIN)/lmark           \
	$(BIN)/lrepair         \
	$(BIN)/lshow           \
	$(BIN)/xxhsum

LMAKE_SERVER_FILES := \
	$(LMAKE_SERVER_PY_FILES)  \
	$(LMAKE_SERVER_BIN_FILES)

LMAKE_REMOTE_FILES := \
	$(SBIN)/job_exec               \
	$(SLIB)/ld_audit.so            \
	$(SLIB)/ld_preload.so          \
	$(SLIB)/ld_preload_jemalloc.so \
	$(BIN)/lcheck_deps             \
	$(BIN)/ldecode                 \
	$(BIN)/lencode                 \
	$(BIN)/ldepend                 \
	$(BIN)/ltarget                 \
	$(LIB)/clmake.so
ifneq ($(PYTHON2),)
    LMAKE_REMOTE_FILES := $(LMAKE_REMOTE_FILES) $(LIB)/clmake2.so
endif

LMAKE_BASIC_SAN_OBJS := \
	src/disk$(SAN).o    \
	src/fd$(SAN).o      \
	src/hash$(SAN).o    \
	src/lib$(SAN).o     \
	src/non_portable.o  \
	src/process$(SAN).o \
	src/time$(SAN).o    \
	src/utils$(SAN).o

LMAKE_BASIC_OBJS := \
	src/disk.o         \
	src/fd.o           \
	src/hash.o         \
	src/lib.o          \
	src/non_portable.o \
	src/process.o      \
	src/time.o         \
	src/utils.o

LMAKE_FILES := $(LMAKE_SERVER_FILES) $(LMAKE_REMOTE_FILES)

LMAKE_ALL_FILES := \
	$(LMAKE_FILES)        \
	$(DOC)/lmake_doc.pptx \
	$(DOC)/lmake.html

DFLT : LMAKE UNIT_TESTS LMAKE_TEST lmake.tar.gz

ALL : DFLT STORE_TEST $(DOC)/lmake.html

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

%.html : %.texi
	LANGUAGE= LC_ALL= LANG= texi2any --html --no-split --output=$@ $<

#
# Manifest
#
Manifest : .git/index
	git ls-files >$@
include Manifest.inc_stamp # Manifest is used in this makefile

#
# versioning
#

SOURCES     := $(shell cat Manifest)
CPP_SOURCES := $(filter %.cc,$(SOURCES)) $(filter %.hh,$(SOURCES))

# use a stamp to implement a by value update (while make works by date)
version.hh.stamp : _bin/version Manifest $(CPP_SOURCES)
	@./$< $(CPP_SOURCES) > $@
	@# dont touch output if it is steady
	@if cmp -s $@ $(@:%.stamp=%) ; then                        echo steady version ; \
	else                                mv $@ $(@:%.stamp=%) ; echo new version    ; \
	fi
version.hh : version.hh.stamp ;

#
# LMAKE
#

# add system configuration to lmake.py :
# Sense git bin dir at install time so as to be independent of it at run time.
# Some python installations require LD_LIBRARY_PATH. Handle this at install time so as to be independent at run time.
$(LIB)/%.py : $(SLIB)/%.src.py
	@mkdir -p $(@D)
	sed \
		-e 's!\$$BASH!$(BASH)!'                          \
		-e 's!\$$PYTHON2!$(PYTHON2)!'                    \
		-e 's!\$$PYTHON!$(PYTHON)!'                      \
		-e 's!\$$GIT!$(GIT)!'                            \
		-e 's!\$$LD_LIBRARY_PATH!$(PY_LD_LIBRARY_PATH)!' \
		-e 's!\$$STD_PATH!$(STD_PATH)!'                  \
		$< >$@
# for other files, just copy
$(LIB)/% : $(SLIB)/%
	@mkdir -p $(@D)
	cp $< $@
# idem for bin
$(BIN)/% : $(SBIN)/%
	@mkdir -p $(@D)
	cp $< $@

LMAKE_SERVER : $(LMAKE_SERVER_FILES)
LMAKE_REMOTE : $(LMAKE_REMOTE_FILES)
LMAKE        : LMAKE_SERVER LMAKE_REMOTE

#
# store
#

STORE_TEST : $(STORE_LIB)/unit_test.dir/tok $(STORE_LIB)/big_test.dir/tok

$(STORE_LIB)/unit_test : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(STORE_LIB)/file$(SAN).o \
	$(SRC)/app$(SAN).o        \
	$(SRC)/trace$(SAN).o      \
	$(STORE_LIB)/unit_test$(SAN).o
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(STORE_LIB)/unit_test.dir/tok : $(STORE_LIB)/unit_test
	@rm -rf   $(@D)
	@mkdir -p $(@D)
	./$<     $(@D)
	@touch    $@

$(STORE_LIB)/big_test.dir/tok : $(STORE_LIB)/big_test.py LMAKE
	@mkdir -p $(@D)
	@rm -rf   $(@D)/LMAKE
	PATH=$$PWD/_bin:$$PWD/bin:$$PATH ; ( cd $(@D) ; $(PYTHON) ../big_test.py / 2000000 )
	@touch $@

#
# engine
#

ALL_H := version.hh sys_config.h ext/xxhash.h

# On ubuntu, seccomp.h is in /usr/include. On CenOS7, it is in /usr/include/linux, but beware that otherwise, /usr/include must be prefered, hence -idirafter
CPP_OPTS := -iquote ext -iquote $(SRC) -iquote $(SRC_ENGINE) -iquote . -idirafter /usr/include/linux

%_py2.san.o : %.cc $(ALL_H) ; @echo $(CXX) -c $(USER_FLAGS) $(SAN_FLAGS) to $@ ; $(COMPILE) -c $(SAN_FLAGS) -frtti -fPIC $(PY2_CC_OPTS) $(CPP_OPTS) -o $@ $<
%_py2.i     : %.cc $(ALL_H) ; @echo $(CXX) -E $(USER_FLAGS)              to $@ ; $(COMPILE) -E                           $(PY2_CC_OPTS) $(CPP_OPTS) -o $@ $<
%_py2.s     : %.cc $(ALL_H) ; @echo $(CXX) -S $(USER_FLAGS)              to $@ ; $(COMPILE) -S                           $(PY2_CC_OPTS) $(CPP_OPTS) -o $@ $<
%_py2.o     : %.cc $(ALL_H) ; @echo $(CXX) -c $(USER_FLAGS)              to $@ ; $(COMPILE) -c              -frtti -fPIC $(PY2_CC_OPTS) $(CPP_OPTS) -o $@ $<

%.san.o     : %.cc $(ALL_H) ; @echo $(CXX) -c $(USER_FLAGS) $(SAN_FLAGS) to $@ ; $(COMPILE) -c $(SAN_FLAGS) -frtti -fPIC $(PY_CC_OPTS)  $(CPP_OPTS) -o $@ $<
%.i         : %.cc $(ALL_H) ; @echo $(CXX) -E $(USER_FLAGS)              to $@ ; $(COMPILE) -E                           $(PY_CC_OPTS)  $(CPP_OPTS) -o $@ $<
%.s         : %.cc $(ALL_H) ; @echo $(CXX) -S $(USER_FLAGS)              to $@ ; $(COMPILE) -S                           $(PY_CC_OPTS)  $(CPP_OPTS) -o $@ $<
%.o         : %.cc $(ALL_H) ; @echo $(CXX) -c $(USER_FLAGS)              to $@ ; $(COMPILE) -c              -frtti -fPIC $(PY_CC_OPTS)  $(CPP_OPTS) -o $@ $<

%_py2.d : %.cc $(ALL_H) ; @$(COMPILE) -MM -MT '$(@:%.d=%.i) $(@:%.d=%.s) $(@:%.d=%.o) $(@:%.d=%.san.o) $@ ' -MF $@ $(PY2_CC_OPTS) $(CPP_OPTS) $< 2>/dev/null || :
%.d     : %.cc $(ALL_H) ; @$(COMPILE) -MM -MT '$(@:%.d=%.i) $(@:%.d=%.s) $(@:%.d=%.o) $(@:%.d=%.san.o) $@ ' -MF $@ $(PY_CC_OPTS)  $(CPP_OPTS) $< 2>/dev/null || :

include $(patsubst %.cc,%.d, $(filter-out %.x.cc,$(filter %.cc,$(shell git ls-files))) )
include src/py_py2.d src/autodep/clmake_py2.d

#
# lmake
#

# on CentOS7, gcc looks for libseccomp.so with -lseccomp, but only libseccomp.so.2 exists, and this works everywhere.
LIB_SECCOMP := $(if $(HAS_SECCOMP),-l:libseccomp.so.2)

$(SRC)/autodep/ld_preload.o          : $(SRC)/autodep/ld_common.x.cc $(SRC)/autodep/ld.x.cc
$(SRC)/autodep/ld_preload_jemalloc.o : $(SRC)/autodep/ld_common.x.cc $(SRC)/autodep/ld.x.cc
$(SRC)/autodep/ld_server$(SAN).o     : $(SRC)/autodep/ld_common.x.cc $(SRC)/autodep/ld.x.cc
$(SRC)/autodep/ld_audit.o            : $(SRC)/autodep/ld_common.x.cc
$(SRC)/ldump$(SAN).o                 : $(ALL_ENGINE_H)
$(SRC)/ldump_job$(SAN).o             : $(ALL_ENGINE_H)
$(SRC)/lrepair$(SAN).o               : $(ALL_ENGINE_H)

$(SBIN)/lmakeserver : \
	$(LMAKE_BASIC_SAN_OBJS)                                      \
	$(SRC)/app$(SAN).o                                           \
	$(SRC)/py$(SAN).o                                            \
	$(SRC)/rpc_client$(SAN).o                                    \
	$(SRC)/rpc_job$(SAN).o                                       \
	$(SRC)/trace$(SAN).o                                         \
	$(SRC)/store/file$(SAN).o                                    \
	$(SRC)/autodep/env$(SAN).o                                   \
	$(SRC)/autodep/gather$(SAN).o                                \
	$(SRC)/autodep/ld_server$(SAN).o                             \
	$(SRC)/autodep/ptrace$(SAN).o                                \
	$(SRC)/autodep/record$(SAN).o                                \
	$(SRC)/autodep/syscall_tab$(SAN).o                           \
	$(SRC)/lmakeserver/backend$(SAN).o                           \
	                  $(SRC)/lmakeserver/backends/local$(SAN).o  \
	$(if $(HAS_SLURM),$(SRC)/lmakeserver/backends/slurm$(SAN).o) \
	$(SRC)/lmakeserver/cache$(SAN).o                             \
	$(SRC)/lmakeserver/caches/dir_cache$(SAN).o                  \
	$(SRC)/lmakeserver/cmd$(SAN).o                               \
	$(SRC)/lmakeserver/codec$(SAN).o                             \
	$(SRC)/lmakeserver/global$(SAN).o                            \
	$(SRC)/lmakeserver/job$(SAN).o                               \
	$(SRC)/lmakeserver/makefiles$(SAN).o                         \
	$(SRC)/lmakeserver/node$(SAN).o                              \
	$(SRC)/lmakeserver/req$(SAN).o                               \
	$(SRC)/lmakeserver/rule$(SAN).o                              \
	$(SRC)/lmakeserver/store$(SAN).o                             \
	$(SRC)/lmakeserver/main$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_OPTS) $(LIB_SECCOMP) $(LINK_LIB)

$(BIN)/lrepair : \
	$(LMAKE_BASIC_SAN_OBJS)                                      \
	$(SRC)/app$(SAN).o                                           \
	$(SRC)/py$(SAN).o                                            \
	$(SRC)/rpc_client$(SAN).o                                    \
	$(SRC)/rpc_job$(SAN).o                                       \
	$(SRC)/trace$(SAN).o                                         \
	$(SRC)/autodep/env$(SAN).o                                   \
	$(SRC)/autodep/gather$(SAN).o                                \
	$(SRC)/autodep/ld_server$(SAN).o                             \
	$(SRC)/autodep/ptrace$(SAN).o                                \
	$(SRC)/autodep/record$(SAN).o                                \
	$(SRC)/autodep/syscall_tab$(SAN).o                           \
	$(SRC)/store/file$(SAN).o                                    \
	$(SRC)/lmakeserver/backend$(SAN).o                           \
	                  $(SRC)/lmakeserver/backends/local$(SAN).o  \
	$(if $(HAS_SLURM),$(SRC)/lmakeserver/backends/slurm$(SAN).o) \
	$(SRC)/lmakeserver/cache$(SAN).o                             \
	$(SRC)/lmakeserver/caches/dir_cache$(SAN).o                  \
	$(SRC)/lmakeserver/codec$(SAN).o                             \
	$(SRC)/lmakeserver/global$(SAN).o                            \
	$(SRC)/lmakeserver/job$(SAN).o                               \
	$(SRC)/lmakeserver/makefiles$(SAN).o                         \
	$(SRC)/lmakeserver/node$(SAN).o                              \
	$(SRC)/lmakeserver/req$(SAN).o                               \
	$(SRC)/lmakeserver/rule$(SAN).o                              \
	$(SRC)/lmakeserver/store$(SAN).o                             \
	$(SRC)/lrepair$(SAN).o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_OPTS) $(LIB_SECCOMP) $(LINK_LIB)

$(SBIN)/ldump : \
	$(LMAKE_BASIC_SAN_OBJS)                     \
	$(SRC)/app$(SAN).o                          \
	$(SRC)/py$(SAN).o                           \
	$(SRC)/rpc_client$(SAN).o                   \
	$(SRC)/rpc_job$(SAN).o                      \
	$(SRC)/trace$(SAN).o                        \
	$(SRC)/autodep/env$(SAN).o                  \
	$(SRC)/autodep/ld_server$(SAN).o            \
	$(SRC)/autodep/record$(SAN).o               \
	$(SRC)/autodep/syscall_tab$(SAN).o          \
	$(SRC)/store/file$(SAN).o                   \
	$(SRC)/lmakeserver/backend$(SAN).o          \
	$(SRC)/lmakeserver/cache$(SAN).o            \
	$(SRC)/lmakeserver/caches/dir_cache$(SAN).o \
	$(SRC)/lmakeserver/codec$(SAN).o            \
	$(SRC)/lmakeserver/global$(SAN).o           \
	$(SRC)/lmakeserver/job$(SAN).o              \
	$(SRC)/lmakeserver/node$(SAN).o             \
	$(SRC)/lmakeserver/req$(SAN).o              \
	$(SRC)/lmakeserver/rule$(SAN).o             \
	$(SRC)/lmakeserver/store$(SAN).o            \
	$(SRC)/ldump$(SAN).o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_OPTS) $(LINK_LIB)

$(SBIN)/ldump_job : \
	$(LMAKE_BASIC_SAN_OBJS)    \
	$(SRC)/app$(SAN).o         \
	$(SRC)/rpc_job$(SAN).o     \
	$(SRC)/trace$(SAN).o       \
	$(SRC)/autodep/env$(SAN).o \
	$(SRC)/ldump_job$(SAN).o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_OPTS) $(LINK_LIB)

# XXX : why job_exec does not support sanitize thread ?
$(SBIN)/job_exec : \
	$(LMAKE_BASIC_OBJS)          \
	$(SRC)/app.o                 \
	$(SRC)/py.o                  \
	$(SRC)/rpc_job.o             \
	$(SRC)/trace.o               \
	$(SRC)/autodep/env.o         \
	$(SRC)/autodep/gather.o      \
	$(SRC)/autodep/ptrace.o      \
	$(SRC)/autodep/record.o      \
	$(SRC)/autodep/syscall_tab.o \
	$(SRC)/job_exec.o
	@mkdir -p $(@D)
	@echo link to $@
	@@$(LINK_BIN) -o $@ $^ $(PY_LINK_OPTS) $(LIB_SECCOMP) $(LINK_LIB)

$(SBIN)/align_comments : \
	$(LMAKE_BASIC_SAN_OBJS) \
	$(SRC)/align_comments$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lmake : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/lmake$(SAN).o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/ldebug : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/ldebug$(SAN).o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lshow : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/lshow$(SAN).o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lforget : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/lforget$(SAN).o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lmark : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/lmark$(SAN).o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

# XXX : why xxhsum does not support sanitize thread ?
$(BIN)/xxhsum : \
	$(LMAKE_BASIC_OBJS) \
	$(SRC)/xxhsum.o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/autodep : \
	$(LMAKE_BASIC_SAN_OBJS)            \
	$(SRC)/app$(SAN).o                 \
	$(SRC)/rpc_job$(SAN).o             \
	$(SRC)/trace$(SAN).o               \
	$(SRC)/autodep/env$(SAN).o         \
	$(SRC)/autodep/gather$(SAN).o      \
	$(SRC)/autodep/ptrace$(SAN).o      \
	$(SRC)/autodep/record$(SAN).o      \
	$(SRC)/autodep/syscall_tab$(SAN).o \
	$(SRC)/autodep/autodep$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LIB_SECCOMP) $(LINK_LIB)

#
# remote
#

# remote executables generate errors when -fsanitize=thread, but are mono-thread, so we don't care

$(BIN)/ldecode : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/ldecode.o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/ldepend : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/ldepend.o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/lencode : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/lencode.o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/ltarget : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/ltarget.o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/lcheck_deps : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/lcheck_deps.o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK_BIN) -o $@ $^ $(LINK_LIB)

# remote libs generate errors when -fsanitize=thread // XXX fix these errors and use $(SAN)

$(SLIB)/ld_preload.so : \
	$(LMAKE_BASIC_OBJS)          \
	$(SRC)/rpc_job.o             \
	$(SRC)/autodep/env.o         \
	$(SRC)/autodep/record.o      \
	$(SRC)/autodep/syscall_tab.o \
	$(SRC)/autodep/ld_preload.o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK_SO) -o $@ $^ $(LINK_LIB)

$(SLIB)/ld_preload_jemalloc.so : \
	$(LMAKE_BASIC_OBJS)          \
	$(SRC)/rpc_job.o             \
	$(SRC)/autodep/env.o         \
	$(SRC)/autodep/record.o      \
	$(SRC)/autodep/syscall_tab.o \
	$(SRC)/autodep/ld_preload_jemalloc.o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK_SO) -o $@ $^ $(LINK_LIB)

$(SLIB)/ld_audit.so : \
	$(LMAKE_BASIC_OBJS)          \
	$(SRC)/rpc_job.o             \
	$(SRC)/autodep/env.o         \
	$(SRC)/autodep/record.o      \
	$(SRC)/autodep/syscall_tab.o \
	$(SRC)/autodep/ld_audit.o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK_SO) -o $@ $^ $(LINK_LIB)

ifneq ($(PYTHON2),)
$(LIB)/clmake2.so : \
	$(LMAKE_BASIC_OBJS)     \
	$(SRC)/py_py2.o         \
	$(SRC)/rpc_job.o        \
	$(SRC)/autodep/env.o    \
	$(SRC)/autodep/record.o \
	$(SRC)/autodep/clmake_py2.o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK_SO) -o $@ $^ $(PY2_LINK_OPTS) $(LINK_LIB)
endif

$(LIB)/clmake.so : \
	$(LMAKE_BASIC_OBJS)     \
	$(SRC)/py.o             \
	$(SRC)/rpc_job.o        \
	$(SRC)/autodep/env.o    \
	$(SRC)/autodep/record.o \
	$(SRC)/autodep/clmake.o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK_SO) -o $@ $^ $(PY_LINK_OPTS) $(LINK_LIB)

#
# Unit tests
#

UT_DIR      := unit_tests
UT_BASE_DIR := $(UT_DIR)/base
UT_BASE     := Manifest $(shell grep -x '$(UT_BASE_DIR)/.*' Manifest)

UNIT_TESTS1 : Manifest $(patsubst %.py,%.dir/tok,     $(shell grep -x '$(UT_DIR)/[^/]*\.py'  Manifest) )
UNIT_TESTS2 : Manifest $(patsubst %.script,%.dir/tok, $(shell grep -x '$(UT_DIR)/.*\.script' Manifest) )

UNIT_TESTS : UNIT_TESTS1 UNIT_TESTS2

%.dir/tok : %.script $(LMAKE_FILES) $(UT_BASE) Manifest
	@echo script test to $@
	@mkdir -p $(@D)
	@( cd $(@D) ; git clean -ffdxq >/dev/null 2>/dev/null ) ; : # keep $(@D) to ease debugging, ignore rc as old versions of git work but generate an error
	@for f in $$(grep '^$(UT_DIR)/base/' Manifest) ; do df=$(@D)/$${f#$(UT_DIR)/base/} ; mkdir -p $$(dirname $$df) ; cp $$f $$df ; done
	@cd $(@D) ; find . -type f -printf '%P\n' > Manifest
	@	( cd $(@D) ; PATH=$(ROOT_DIR)/bin:$(ROOT_DIR)/_bin:$$PATH CXX=$(CXX) $(ROOT_DIR)/$< ) >$@.out 2>$@.err \
	&&	mv $@.out $@                                                                                           \
	||	( cat $@.out $@.err ; exit 1 )

%.dir/tok : %.py $(LMAKE_FILES) _lib/ut.py
	@echo py test to $@
	@mkdir -p $(@D)
	@( cd $(@D) ; git clean -ffdxq >/dev/null 2>/dev/null ) ; : # keep $(@D) to ease debugging, ignore rc as old versions of git work but generate an error
	@cp $< $(@D)/Lmakefile.py
	@	( cd $(@D) ; PATH=$(ROOT_DIR)/bin:$(ROOT_DIR)/_bin:$$PATH PYTHONPATH=$(ROOT_DIR)/lib:$(ROOT_DIR)/_lib HOME= CXX=$(CXX) $(PYTHON) Lmakefile.py ) >$@.out 2>$@.err \
	&&	mv $@.out $@                                                                                                                                                     \
	||	( cat $@.out $@.err ; exit 1 )

#
# lmake env
#

#
# lmake under lmake
#

LMAKE_ENV  : $(LMAKE_ENV)/stamp
LMAKE_TEST : $(LMAKE_ENV)/tok

LMAKE_SRCS := $(shell grep -e ^_bin/ -e ^_lib/ -e ^doc/ -e ^ext/ -e ^lib/ -e ^src/ Manifest)
$(LMAKE_ENV)/Manifest : Manifest
	@mkdir -p $(@D)
	@for f in $(LMAKE_SRCS) ; do echo $$f ; done > $@
	@grep ^$(@D)/ Manifest | sed s:$(@D)/::       >>$@
	@echo $(@F)                                   >>$@
	@echo generate $@
$(LMAKE_ENV)/% : %
	@mkdir -p $(@D)
	cp $< $@
$(LMAKE_ENV)/stamp : $(LMAKE_ALL_FILES) $(LMAKE_ENV)/Manifest $(patsubst %,$(LMAKE_ENV)/%,$(LMAKE_SRCS))
	@mkdir -p $(LMAKE_ENV)-cache/LMAKE
	echo '300M' > $(LMAKE_ENV)-cache/LMAKE/size
	@touch $@
	@echo init $(LMAKE_ENV)-cache
$(LMAKE_ENV)/tok : $(LMAKE_ENV)/stamp $(LMAKE_ENV)/Lmakefile.py
	@set -e ; cd $(LMAKE_ENV) ; export CXX=$(CXX) ; $(ROOT_DIR)/bin/lmake lmake.tar.gz -Vn & sleep 1 ; $(ROOT_DIR)/bin/lmake lmake.tar.gz >$(@F) || rm -f $(@F) ; wait $$! || rm -f $(@F)

#
# archive
#
VERSION     := 0.1
ARCHIVE_DIR := open-lmake-$(VERSION)
lmake.tar.gz  : TAR_COMPRESS := z
lmake.tar.bz2 : TAR_COMPRESS := j
lmake.tar.gz lmake.tar.bz2 : $(LMAKE_ALL_FILES)
	@rm -rf $(ARCHIVE_DIR)
	for d in $^ ; do mkdir -p $$(dirname $(ARCHIVE_DIR)/$$d) ; cp $$d $(ARCHIVE_DIR)/$$d ; done
	tar c$(TAR_COMPRESS) -f $@ $(ARCHIVE_DIR)

#
# For debian packaging
#
install : $(LMAKE_BINS) $(LMAKE_REMOTE_FILES) $(LMAKE_SERVER_PY_FILES) $(DOC)/lmake.html
	for f in $(LMAKE_SERVER_BIN_FILES); do install -D        $$f $(DESTDIR)/$(prefix)/lib/open-lmake/$$f ; done
	for f in $(LMAKE_REMOTE_FILES)    ; do install -D        $$f $(DESTDIR)/$(prefix)/lib/open-lmake/$$f ; done
	for f in $(LMAKE_SERVER_PY_FILES) ; do install -D -m 644 $$f $(DESTDIR)/$(prefix)/lib/open-lmake/$$f ; done
	install -D $(DOC)/lmake.html $(DESTDIR)/$(prefix)/share/doc/open-lmake/html/lmake.html

#
# Install debian packges needed to build open-lmake package
#
debdeps:
	sudo apt install dh-make devscripts debhelper equivs
	sudo mk-build-deps --install debian/control

#
# Build debian package (then install it using: apt install <pkg>)
#
deb:
	debuild -b -us -uc

# uncoment to automatically cleanup repo before building package
# clean:
# 	git clean -ffdx
