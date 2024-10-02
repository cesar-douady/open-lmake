# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

VERSION := 24.09

#
# user configurable
MAKEFLAGS := -j$(shell nproc||echo 1) -k
# mandatory
MAKEFLAGS += -r -R

.DEFAULT_GOAL := DFLT

$(shell { echo CXX=$$CXX ; echo PYTHON2=$$PYTHON2 ; echo PYTHON=$$PYTHON ; echo SLURM=$$SLURM ; echo LMAKE_FLAGS=$$LMAKE_FLAGS ; } >sys_config.env.tmp )
$(shell cmp sys_config.env sys_config.env.tmp 2>/dev/null || { cp sys_config.env.tmp sys_config.env ; echo new env : >&2 ; cat sys_config.env >&2 ; }  )
$(shell rm -f sys_config.env.tmp                                                                                                                       )

sys_config.log : _bin/sys_config sys_config.env
	set -a ; . ./sys_config.env ; ./$< $(@:%.log=%.mk) $(@:%.log=%.h) 2>$@ || cat $@ # reread sys_config.env in case it has been modified while reading an old sys_config.mk
sys_config.mk : sys_config.log ;+@[ -f $@ ] || { echo "cannot find $@" ; exit 1 ; }
sys_config.h  : sys_config.log ;+@[ -f $@ ] || { echo "cannot find $@" ; exit 1 ; }

# defines HAS_SECCOMP, HAS_SGE and HAS_SLURM
include sys_config.mk

# this is the recommanded way to insert a , when calling functions
# /!\ cannot put a comment on the following line or a lot of spaces will be inserted in the variable definition
COMMA := ,

HIDDEN_FLAGS := -ftabstop=4 -ftemplate-backtrace-limit=0 -pedantic -fvisibility=hidden
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
WARNING_FLAGS := -Wall -Wextra -Wno-cast-function-type -Wno-type-limits -Werror
#
CXX_EXE := $(shell bash -c 'type -p $(CXX)')
CXX_DIR := $(shell dirname $(CXX_EXE))
#
LINK_OPTS           := $(patsubst %,-Wl$(COMMA)-rpath=%,$(LINK_LIB_PATH)) -pthread # e.g. : -Wl,-rpath=/a/b -Wl,-rpath=/c -pthread
SAN                 := $(if $(strip $(SAN_FLAGS)),-san)
LINK                := PATH=$(CXX_DIR):$$PATH $(CXX_EXE) $(COVERAGE) $(LINK_OPTS)
LINK_LIB             = -ldl $(if $(and $(LD_SO_LIB_32),$(findstring $(LD_SO_LIB_32),$@)),$(LIB_STACKTRACE_32:%=-l%),$(LIB_STACKTRACE:%=-l%))
CLANG_WARNING_FLAGS := -Wno-misleading-indentation -Wno-unknown-warning-option -Wno-c2x-extensions -Wno-c++2b-extensions
#
ifeq ($(CXX_FLAVOR),clang)
    WARNING_FLAGS += $(CLANG_WARNING_FLAGS)
endif
#
USER_FLAGS := -std=$(CXX_STD) $(OPT_FLAGS) $(EXTRA_FLAGS)
COMPILE    := PATH=$(CXX_DIR):$$PATH $(CXX_EXE) $(COVERAGE) $(USER_FLAGS) $(HIDDEN_FLAGS) -fno-strict-aliasing -pthread $(WARNING_FLAGS)
LINT       := clang-tidy
LINT_OPTS  := $(USER_FLAGS) $(HIDDEN_FLAGS) $(WARNING_FLAGS) $(CLANG_WARNING_FLAGS)
ROOT_DIR   := $(abspath .)
LIB        := lib
SLIB       := _lib
BIN        := bin
SBIN       := _bin
DOC        := doc
SRC        := src
LMAKE_ENV  := lmake_env
STORE_LIB  := $(SRC)/store

PY2_INC_DIRS  := $(if $(PYTHON2),$(filter-out $(STD_INC_DIRS),$(PY2_INCLUDEDIR) $(PY2_INCLUDEPY))) # for some reasons, compilation breaks if standard inc dirs are given with -isystem
PY2_CC_OPTS   := $(if $(PYTHON2),$(patsubst %,-isystem %,$(PY2_INC_DIRS)) -Wno-register)
PY2_LINK_OPTS := $(if $(PYTHON2),$(patsubst %,-L%,$(PY2_LIB_DIR)) $(patsubst %,-Wl$(COMMA)-rpath=%,$(PY2_LIB_DIR)) -l:$(PY2_LIB_BASE))
PY3_INC_DIRS  := $(filter-out $(STD_INC_DIRS),$(PY3_INCLUDEDIR) $(PY3_INCLUDEPY))                  # for some reasons, compilation does not work if standard inc dirs are given with -isystem
PY3_CC_OPTS   := $(patsubst %,-isystem %,$(PY3_INC_DIRS)) -Wno-register
PY3_LINK_OPTS := $(patsubst %,-L%,$(PY3_LIB_DIR))  $(patsubst %,-Wl$(COMMA)-rpath=%,$(PY3_LIB_DIR)) -l:$(PY3_LIB_BASE)
FUSE_CC_OPTS  := $(if $(HAS_FUSE),$(shell pkg-config fuse3 --cflags))
FUSE_LIB      := $(if $(HAS_FUSE),$(shell pkg-config fuse3 --libs  ))
PCRE_LIB      := $(if $(HAS_PCRE),-lpcre2-8)

PY_CC_OPTS   = $(if $(and $(PYTHON2)     ,$(findstring -py2,           $@)),$(PY2_CC_OPTS)  ,$(PY3_CC_OPTS)  )
PY_LINK_OPTS = $(if $(and $(LD_SO_LIB_32),$(findstring 2.so,           $@)),$(PY2_LINK_OPTS),$(PY3_LINK_OPTS))
PY_SO        = $(if $(and $(PYTHON2)     ,$(findstring 2.so,           $@)),-py2)
MOD_SO       = $(if $(and $(LD_SO_LIB_32),$(findstring $(LD_SO_LIB_32),$@)),-m32)
MOD_O        = $(if $(and $(LD_SO_LIB_32),$(findstring -m32,           $@)),-m32)

# Engine
SRC_ENGINE := $(SRC)/lmakeserver

# LMAKE
LMAKE_SERVER_PY_FILES := \
	$(SLIB)/read_makefiles.py              \
	$(SLIB)/serialize.py                   \
	$(LIB)/lmake/__init__.py               \
	$(LIB)/lmake/auto_sources.py           \
	$(LIB)/lmake/import_machinery.py       \
	$(LIB)/lmake/custom_importer.py        \
	$(LIB)/lmake/rules.py                  \
	$(LIB)/lmake/sources.py                \
	$(LIB)/lmake/utils.py                  \
	$(LIB)/lmake_debug/__init__.py         \
	$(LIB)/lmake_debug/default.py          \
	$(LIB)/lmake_debug/enter.py            \
	$(LIB)/lmake_debug/gdb.py              \
	$(LIB)/lmake_debug/none.py             \
	$(LIB)/lmake_debug/pudb.py             \
	$(LIB)/lmake_debug/vscode.py           \
	$(LIB)/lmake_debug/utils.py            \
	$(LIB)/lmake_debug/runtime/__init__.py \
	$(LIB)/lmake_debug/runtime/pdb_.py     \
	$(LIB)/lmake_debug/runtime/pudb_.py    \
	$(LIB)/lmake_debug/runtime/vscode.py   \
	$(LIB)/lmake_debug/runtime/utils.py    \
	$(LIB)/lmake_runtime.py

LMAKE_SERVER_BIN_FILES := \
	$(SBIN)/lmakeserver            \
	$(SBIN)/ldump                  \
	$(SBIN)/ldump_job              \
	$(SBIN)/align_comments         \
	$(BIN)/autodep                 \
	$(BIN)/ldebug                  \
	$(BIN)/lforget                 \
	$(BIN)/lmake                   \
	$(BIN)/lmark                   \
	$(BIN)/lrepair                 \
	$(BIN)/lshow                   \
	$(BIN)/find_cc_ld_library_path \
	$(BIN)/xxhsum

LMAKE_SERVER_FILES := \
	$(LMAKE_SERVER_PY_FILES)  \
	$(LMAKE_SERVER_BIN_FILES)

LMAKE_REMOTE_SLIBS := ld_audit.so ld_preload.so ld_preload_jemalloc.so
LMAKE_REMOTE_FILES := \
	$(if $(LD_SO_LIB_32),$(patsubst %,_$(LD_SO_LIB_32)/%,$(LMAKE_REMOTE_SLIBS))) \
	$(patsubst %,_$(LD_SO_LIB)/%,$(LMAKE_REMOTE_SLIBS))                          \
	$(LIB)/clmake.so                                                             \
	$(if $(PYTHON2),$(LIB)/clmake2.so)                                           \
	$(SBIN)/job_exec                                                             \
	$(BIN)/lcheck_deps                                                           \
	$(BIN)/ldecode                                                               \
	$(BIN)/lencode                                                               \
	$(BIN)/ldepend                                                               \
	$(BIN)/ltarget

LMAKE_BASIC_OBJS_ := \
	src/disk.o    \
	src/fd.o      \
	src/hash.o    \
	src/lib.o     \
	src/process.o \
	src/time.o    \
	src/utils.o

LMAKE_BASIC_OBJS     := $(LMAKE_BASIC_OBJS_)               src/non_portable.o
LMAKE_BASIC_SAN_OBJS := $(LMAKE_BASIC_OBJS_:%.o=%$(SAN).o) src/non_portable.o

LMAKE_FILES := $(LMAKE_SERVER_FILES) $(LMAKE_REMOTE_FILES)

LMAKE_ALL_FILES := \
	$(LMAKE_FILES)        \
	$(DOC)/lmake_doc.pptx \
	$(DOC)/lmake.html

LINT : $(patsubst %.cc,%.chk, $(filter-out %.x.cc,$(filter %.cc,$(shell git ls-files))) )

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
	@echo "@set VERSION       $(VERSION)"                  >  $(@D)/info.texi
	@echo "@set UPDATED       $$(env -i date '+%d %B %Y')" >> $(@D)/info.texi
	@echo "@set UPDATED-MONTH $$(env -i date '+%B %Y'   )" >> $(@D)/info.texi
	cd $(@D) ; LANGUAGE= LC_ALL= LANG= texi2any --html --no-split --output=$(@F) $(<F)

#
# Manifest
#
Manifest : .git/index
	@git ls-files | uniq >$@.new
	@if cmp -s $@.new $@ ; then rm $@.new    ; echo steady Manifest ; \
	else                        mv $@.new $@ ; echo new    Manifest ; \
	fi
include Manifest.inc_stamp # Manifest is used in this makefile

#
# versioning
#

SOURCES     := $(shell cat Manifest)
CPP_SOURCES := $(filter %.cc,$(SOURCES)) $(filter %.hh,$(SOURCES))

# use a stamp to implement a by value update (while make works by date)
version.hh.stamp : _bin/version Manifest $(CPP_SOURCES)
	@./$< $(CPP_SOURCES) >$@
	@# dont touch output if it is steady
	@if cmp -s $@ $(@:%.stamp=%) ; then                        echo steady version ; \
	else                                mv $@ $(@:%.stamp=%) ; echo new    version ; \
	fi
version.hh : version.hh.stamp ;

#
# LMAKE
#

# add system configuration to lmake.py :
# Sense git bin dir at install time so as to be independent of it at run time.
# Some python installations require LD_LIBRARY_PATH. Handle this at install time so as to be independent at run time.
$(LIB)/%.py : $(SLIB)/%.src.py sys_config.mk
	mkdir -p $(@D)
	sed \
		-e 's!\$$BASH!$(BASH)!'                          \
		-e 's!\$$GIT!$(GIT)!'                            \
		-e 's!\$$LD_LIBRARY_PATH!$(PY_LD_LIBRARY_PATH)!' \
		-e 's!\$$PYTHON2!$(PYTHON2)!'                    \
		-e 's!\$$PYTHON!$(PYTHON)!'                      \
		-e 's!\$$STD_PATH!$(STD_PATH)!'                  \
		-e 's!\$$VERSION!$(VERSION)!'                    \
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
	$(LINK) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(STORE_LIB)/unit_test.dir/tok : $(STORE_LIB)/unit_test
	@rm -rf   $(@D)
	@mkdir -p $(@D)
	./$<      $(@D)
	@touch      $@

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
CPP_OPTS := -iquote ext -iquote $(SRC) -iquote $(SRC_ENGINE) -iquote . $(FUSE_CC_OPTS) -idirafter /usr/include/linux

%.i     : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS)              to $@ ; $(COMPILE) -E                           $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%-m32.i : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS)              to $@ ; $(COMPILE) -E -m32                      $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%-py2.i : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS)              to $@ ; $(COMPILE) -E                           $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%.s     : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS)              to $@ ; $(COMPILE) -S                           $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%-m32.s : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS)              to $@ ; $(COMPILE) -S -m32                      $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%-py2.s : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS)              to $@ ; $(COMPILE) -S                           $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%.o     : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS)              to $@ ; $(COMPILE) -c              -frtti -fPIC $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%-m32.o : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS)              to $@ ; $(COMPILE) -c -m32         -frtti -fPIC $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%-py2.o : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS)              to $@ ; $(COMPILE) -c              -frtti -fPIC $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%-san.o : %.cc $(ALL_H) ; @echo $(CXX)  $(USER_FLAGS) $(SAN_FLAGS) to $@ ; $(COMPILE) -c $(SAN_FLAGS) -frtti -fPIC $(PY_CC_OPTS) $(CPP_OPTS) -o $@ $<
%.chk   : %.cc $(ALL_H) ; @echo $(LINT) $(USER_FLAGS)              to $@ ; $(LINT)    $< -- $(LINT_OPTS)           $(PY_CC_OPTS) $(CPP_OPTS) >$@ ; [ ! -s $@ ]

%.d : %.cc $(ALL_H)
	@$(COMPILE) \
		-MM                                                  \
		-MF $@                                               \
		$(PY_CC_OPTS) $(CPP_OPTS)                            \
		-MT '$(@:%.d=%.i) $(@:%.d=%-m32.i) $(@:%-py2.d=%.i)' \
		-MT '$(@:%.d=%.s) $(@:%.d=%-m32.s) $(@:%.d=%-py2.s)' \
		-MT '$(@:%.d=%.o) $(@:%.d=%-m32.o) $(@:%.d=%-py2.o)' \
		-MT '$(@:%.d=%-san.o)'                               \
		-MT '$(@:%.d=%.chk)'                                 \
		-MT '$@'                                             \
		$< 2>/dev/null || :

include $(patsubst %.cc,%.d, $(filter-out %.x.cc,$(filter %.cc,$(shell git ls-files))) )

#
# lmake
#

# on CentOS7, gcc looks for libseccomp.so with -lseccomp, but only libseccomp.so.2 exists, and this works everywhere.
LIB_SECCOMP := $(if $(HAS_SECCOMP),-l:libseccomp.so.2)

$(SRC)/autodep/ld_audit.o            : $(SRC)/autodep/ld_common.x.cc
$(SRC)/autodep/ld_preload.o          : $(SRC)/autodep/ld_common.x.cc $(SRC)/autodep/ld.x.cc
$(SRC)/autodep/ld_preload_jemalloc.o : $(SRC)/autodep/ld_common.x.cc $(SRC)/autodep/ld.x.cc
$(SRC)/autodep/ld_server$(SAN).o     : $(SRC)/autodep/ld_common.x.cc $(SRC)/autodep/ld.x.cc
$(SRC_ENGINE)/backends/slurm$(SAN).o : CPP_OPTS += $(if $(SLURM_INC_DIR),-isystem $(SLURM_INC_DIR))

CLIENT_SAN_OBJS := \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o

SERVER_SAN_OBJS := \
	$(LMAKE_BASIC_SAN_OBJS)                \
	$(SRC)/app$(SAN).o                     \
	$(SRC)/py$(SAN).o                      \
	$(SRC)/rpc_client$(SAN).o              \
	$(SRC)/rpc_job$(SAN).o                 \
	$(SRC)/rpc_job_exec$(SAN).o            \
	$(SRC)/fuse$(SAN).o                    \
	$(SRC)/trace$(SAN).o                   \
	$(SRC)/autodep/backdoor$(SAN).o        \
	$(SRC)/autodep/env$(SAN).o             \
	$(SRC)/autodep/ld_server$(SAN).o       \
	$(SRC)/autodep/record$(SAN).o          \
	$(SRC)/autodep/syscall_tab$(SAN).o     \
	$(SRC)/store/file$(SAN).o              \
	$(SRC_ENGINE)/backend$(SAN).o          \
	$(SRC_ENGINE)/cache$(SAN).o            \
	$(SRC_ENGINE)/caches/dir_cache$(SAN).o \
	$(SRC_ENGINE)/codec$(SAN).o            \
	$(SRC_ENGINE)/global$(SAN).o           \
	$(SRC_ENGINE)/job$(SAN).o              \
	$(SRC_ENGINE)/node$(SAN).o             \
	$(SRC_ENGINE)/req$(SAN).o              \
	$(SRC_ENGINE)/rule$(SAN).o             \
	$(SRC_ENGINE)/store$(SAN).o

$(SBIN)/lmakeserver : \
	$(SERVER_SAN_OBJS)                                      \
	$(SRC)/autodep/gather$(SAN).o                           \
	$(SRC)/autodep/ptrace$(SAN).o                           \
	                  $(SRC_ENGINE)/backends/local$(SAN).o  \
	$(if $(HAS_SLURM),$(SRC_ENGINE)/backends/slurm$(SAN).o) \
	$(if $(HAS_SGE)  ,$(SRC_ENGINE)/backends/sge$(SAN).o  ) \
	$(SRC_ENGINE)/cmd$(SAN).o                               \
	$(SRC_ENGINE)/makefiles$(SAN).o                         \
	$(SRC_ENGINE)/main$(SAN).o

$(BIN)/lrepair : \
	$(SERVER_SAN_OBJS)                                      \
	$(SRC)/autodep/gather$(SAN).o                           \
	$(SRC)/autodep/ptrace$(SAN).o                           \
	                  $(SRC_ENGINE)/backends/local$(SAN).o  \
	$(if $(HAS_SLURM),$(SRC_ENGINE)/backends/slurm$(SAN).o) \
	$(if $(HAS_SGE)  ,$(SRC_ENGINE)/backends/sge$(SAN).o  ) \
	$(SRC_ENGINE)/makefiles$(SAN).o                         \
	$(SRC)/lrepair$(SAN).o

$(SBIN)/ldump : \
	$(SERVER_SAN_OBJS)   \
	$(SRC)/ldump$(SAN).o

$(SBIN)/lmakeserver $(BIN)/lrepair $(SBIN)/ldump :
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_OPTS) $(PCRE_LIB) $(FUSE_LIB) $(LIB_SECCOMP) $(LINK_LIB)


$(BIN)/lmake   : $(CLIENT_SAN_OBJS)               $(SRC)/lmake$(SAN).o
$(BIN)/lshow   : $(CLIENT_SAN_OBJS)               $(SRC)/lshow$(SAN).o
$(BIN)/lforget : $(CLIENT_SAN_OBJS)               $(SRC)/lforget$(SAN).o
$(BIN)/lmark   : $(CLIENT_SAN_OBJS)               $(SRC)/lmark$(SAN).o
$(BIN)/ldebug  : $(CLIENT_SAN_OBJS:%$(SAN).o=%.o) $(SRC)/ldebug.o        $(SRC)/py.o

$(BIN)/lmake $(BIN)/lshow $(BIN)/lforget $(BIN)/lmark :
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

# XXX : why ldebug does not support sanitize thread ?
$(BIN)/ldebug :
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK) -o $@ $^ $(PY_LINK_OPTS) $(LINK_LIB)

$(SBIN)/ldump_job : \
	$(LMAKE_BASIC_SAN_OBJS)    \
	$(SRC)/app$(SAN).o         \
	$(SRC)/rpc_job$(SAN).o     \
	$(SRC)/fuse$(SAN).o        \
	$(SRC)/trace$(SAN).o       \
	$(SRC)/autodep/env$(SAN).o \
	$(SRC)/ldump_job$(SAN).o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_OPTS) $(FUSE_LIB) $(LINK_LIB)

$(SBIN)/align_comments : \
	$(LMAKE_BASIC_SAN_OBJS) \
	$(SRC)/align_comments$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

# XXX : why xxhsum does not support sanitize thread ?
$(BIN)/xxhsum : \
	$(LMAKE_BASIC_OBJS) \
	$(SRC)/xxhsum.o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/fuse_test : \
	$(LMAKE_BASIC_OBJS) \
	$(SRC)/fuse.o       \
	$(SRC)/fuse_test.o
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK) -o $@ $^ $(FUSE_LIB) $(LINK_LIB)

#
# remote
#

# remote executables generate errors when -fsanitize=thread, but are mono-thread, so we don't care

BASIC_REMOTE_OBJS := \
	$(LMAKE_BASIC_OBJS)       \
	$(SRC)/rpc_job_exec.o     \
	$(SRC)/autodep/backdoor.o \
	$(SRC)/autodep/env.o      \
	$(SRC)/autodep/record.o

AUTODEP_OBJS := $(BASIC_REMOTE_OBJS) $(SRC)/autodep/syscall_tab.o
REMOTE_OBJS  := $(BASIC_REMOTE_OBJS) $(SRC)/autodep/job_support.o

JOB_EXEC_OBJS := \
	$(AUTODEP_OBJS)         \
	$(SRC)/app.o            \
	$(SRC)/py.o             \
	$(SRC)/rpc_job.o        \
	$(SRC)/fuse.o           \
	$(SRC)/trace.o          \
	$(SRC)/autodep/gather.o \
	$(SRC)/autodep/ptrace.o

$(SBIN)/job_exec : $(JOB_EXEC_OBJS) $(SRC)/job_exec.o
$(BIN)/autodep   : $(JOB_EXEC_OBJS) $(SRC)/autodep/autodep.o

# XXX : why job_exec and autodep do not support sanitize thread ?
$(SBIN)/job_exec $(BIN)/autodep :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) -o $@ $^ $(PY_LINK_OPTS) $(PCRE_LIB) $(FUSE_LIB) $(LIB_SECCOMP) $(LINK_LIB)

$(BIN)/ldecode     : $(REMOTE_OBJS) $(SRC)/autodep/ldecode.o
$(BIN)/ldepend     : $(REMOTE_OBJS) $(SRC)/autodep/ldepend.o
$(BIN)/lencode     : $(REMOTE_OBJS) $(SRC)/autodep/lencode.o
$(BIN)/ltarget     : $(REMOTE_OBJS) $(SRC)/autodep/ltarget.o
$(BIN)/lcheck_deps : $(REMOTE_OBJS) $(SRC)/autodep/lcheck_deps.o

$(BIN)/% :
	@mkdir -p $(BIN)
	@echo link to $@
	@$(LINK) -o $@ $^ $(LINK_LIB)

# remote libs generate errors when -fsanitize=thread // XXX fix these errors and use $(SAN)

_$(LD_SO_LIB)/ld_audit.so               : $(AUTODEP_OBJS)             $(SRC)/autodep/ld_audit.o
_$(LD_SO_LIB)/ld_preload.so             : $(AUTODEP_OBJS)             $(SRC)/autodep/ld_preload.o
_$(LD_SO_LIB)/ld_preload_jemalloc.so    : $(AUTODEP_OBJS)             $(SRC)/autodep/ld_preload_jemalloc.o
_$(LD_SO_LIB_32)/ld_audit.so            : $(AUTODEP_OBJS:%.o=%-m32.o) $(SRC)/autodep/ld_audit-m32.o
_$(LD_SO_LIB_32)/ld_preload.so          : $(AUTODEP_OBJS:%.o=%-m32.o) $(SRC)/autodep/ld_preload-m32.o
_$(LD_SO_LIB_32)/ld_preload_jemalloc.so : $(AUTODEP_OBJS:%.o=%-m32.o) $(SRC)/autodep/ld_preload_jemalloc-m32.o

$(LIB)/clmake.so $(LIB)/clmake2.so : SO_OPTS = $(PY_LINK_OPTS)
$(LIB)/clmake.so                   : $(REMOTE_OBJS) $(SRC)/py.o     $(SRC)/autodep/clmake.o
$(LIB)/clmake2.so                  : $(REMOTE_OBJS) $(SRC)/py-py2.o $(SRC)/autodep/clmake-py2.o

%.so :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) -shared -static-libstdc++ $(MOD_SO) -o $@ $^ $(SO_OPTS) $(LINK_LIB) # some user codes may have specific (and older) libs, avoid dependencies

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
	@	(	cd $(@D) ; \
				PATH=$(ROOT_DIR)/bin:$(ROOT_DIR)/_bin:$$PATH                                                  \
				CXX=$(CXX_EXE)                                                                                \
				HAS_32BITS=$(if $(LD_SO_LIB_32),1)                                                            \
			$(ROOT_DIR)/$<                                                                                    \
		) </dev/null >$@.out 2>$@.err                                                                         \
	&&	( if [ ! -f $(@D)/skipped ] ; then mv $@.out $@ ; else echo skipped $@ : $$(cat $(@D)/skipped) ; fi ) \
	||	( cat $@.out $@.err ; exit 1 )

%.dir/tok : %.py $(LMAKE_FILES) _lib/ut.py
	@echo py test to $@
	@mkdir -p $(@D)
	@( cd $(@D) ; git clean -ffdxq >/dev/null 2>/dev/null ) ; : # keep $(@D) to ease debugging, ignore rc as old versions of git work but generate an error
	@cp $< $(@D)/Lmakefile.py
	@	(	cd $(@D) ;                                                                                        \
				PATH=$(ROOT_DIR)/bin:$(ROOT_DIR)/_bin:$$PATH                                                  \
				PYTHONPATH=$(ROOT_DIR)/lib:$(ROOT_DIR)/_lib:$$PYTHONPATH                                      \
				CXX=$(CXX_EXE)                                                                                \
				LD_LIBRARY_PATH=$(PY_LIB_DIR)                                                                 \
				HAS_32BITS=$(if $(LD_SO_LIB_32),1)                                                            \
			$(PYTHON)                                                                                         \
				Lmakefile.py                                                                                  \
		) </dev/null >$@.out 2>$@.err                                                                         \
	&&	( if [ ! -f $(@D)/skipped ] ; then mv $@.out $@ ; else echo skipped $@ : $$(cat $(@D)/skipped) ; fi ) \
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
	@grep ^$(@D)/ Manifest | sed s:$(@D)/::      >>$@
	@echo $(@F)                                  >>$@
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
	@set -e ;                                                               \
	cd $(LMAKE_ENV) ;                                                       \
	export CXX=$(CXX_EXE) ;                                                 \
	rc=0 ;                                                                  \
	$(ROOT_DIR)/bin/lmake lmake.tar.gz -Vn & sleep 1 ;                      \
	$(ROOT_DIR)/bin/lmake lmake.tar.gz >$(@F) || { rm -f $(@F) ; rc=1 ; } ; \
	wait $$!                                  || { rm -f $(@F) ; rc=1 ; } ; \
	exit $$rc

#
# archive
#

ARCHIVE_DIR := open-lmake-$(VERSION)
lmake.tar.gz  : TAR_COMPRESS := z
lmake.tar.bz2 : TAR_COMPRESS := j
lmake.tar.gz lmake.tar.bz2 : $(LMAKE_ALL_FILES)
	@rm -rf $(ARCHIVE_DIR)
	for d in $^ ; do mkdir -p $$(dirname $(ARCHIVE_DIR)/$$d) ; cp $$d $(ARCHIVE_DIR)/$$d ; done
	tar c$(TAR_COMPRESS) -f $@ $(ARCHIVE_DIR)

#
# /!\ this rule is necessary for debian packaging to work, it is not primarily made to be executed by user
#
install : $(LMAKE_BINS) $(LMAKE_REMOTE_FILES) $(LMAKE_SERVER_PY_FILES) $(DOC)/lmake.html
	for f in $(LMAKE_SERVER_BIN_FILES); do install -D        $$f $(DESTDIR)/$(prefix)/lib/open-lmake/$$f ; done
	for f in $(LMAKE_REMOTE_FILES)    ; do install -D        $$f $(DESTDIR)/$(prefix)/lib/open-lmake/$$f ; done
	for f in $(LMAKE_SERVER_PY_FILES) ; do install -D -m 644 $$f $(DESTDIR)/$(prefix)/lib/open-lmake/$$f ; done
	install -D $(DOC)/lmake.html $(DESTDIR)/$(prefix)/share/doc/open-lmake/html/lmake.html

#
# Install debian packages needed to build open-lmake package
# /!\ use : sudo make debdeps, as you need privileges for these commands
#
debdeps :
	apt install dh-make devscripts debhelper equivs
	mk-build-deps --install debian/control

#
# Build debian package (then install it using: apt install <pkg>)
#
deb :
	sed \
		-e 's!\$$VERSION!$(VERSION)!' \
		-e 's!\$$DATE!'"$$(date -R)!" \
		debian/changelog.src >debian/changelog
	debuild -b -us -uc

# uncoment to automatically cleanup repo before building package
# clean:
# 	git clean -ffdx
