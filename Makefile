# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#
# build configuration
#vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

MAKEFLAGS := -j$(shell nproc||echo 1) -k

BASH     := $(shell        bash    -c 'type -p bash'   )
PYTHON   := $(shell        $(BASH) -c 'type -p python3')
GIT      := $(shell        $(BASH) -c 'type -p git'    )
STD_PATH := $(shell env -i $(BASH) -c 'echo $$PATH'    )
CC       := $(shell        $(BASH) -c 'type -p gcc-12 || type -p gcc-11 || type -p gcc || type -p clang')

#^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#

MAKEFLAGS += -r -R

ifeq ($(strip $(LMAKE_OPT_LVL)),)
OPT_FLAGS   := -O3
else ifeq ($(strip $(LMAKE_OPT_LVL)),0)
OPT_FLAGS   := -O0 -g
else ifeq ($(strip $(LMAKE_OPT_LVL)),1)
OPT_FLAGS   := -O1
else ifeq ($(strip $(LMAKE_OPT_LVL)),2)
OPT_FLAGS   := -O2
else ifeq ($(strip $(LMAKE_OPT_LVL)),3)
OPT_FLAGS   := -O3
else ifeq ($(strip $(LMAKE_OPT_LVL)),4)
OPT_FLAGS := -O3 -DNDEBUG
else ifeq ($(strip $(LMAKE_OPT_LVL)),5)
OPT_FLAGS := -O3 -DNDEBUG -DNO_TRACE
endif

COVERAGE :=
ifneq ($(LMAKE_COVERAGE),)
COVERAGE := --coverage                                                         # XXX : not operational yet
endif

WARNING_FLAGS := -Wall -Wextra -Wno-cast-function-type -Wno-type-limits

LANG := c++20

include sys_config.h.inc_stamp                                                 # sys_config.h is used in this makefile

# python configuration
ifeq ($(PYTHON),)
$(error cannot find python3)
else ifeq ($(shell $(PYTHON) -c 'import sys ; print(sys.version_info.major==3 and sys.version_info.minor>=6)'),False)
$(error python3 version must be at least 3.6)
endif

# CC configuration
ifeq ($(CC),)

$(error cannot find c compiler)

else ifeq ($(findstring gcc,$(CC)),gcc)

ifeq ($(intcmp $(shell $(CC) -dumpversion),11,lt,eq,gt),lt)
$(error gcc version must be at least 11)
endif
# -fsanitize=address and -fsanitize=thread are exclusive of one another
# for an unknown reason, clang is incompatible with -fsanitize
ifeq ($(LMAKE_SAN),A)
SAN_FLAGS := -fsanitize=address -fsanitize=undefined
endif
ifeq ($(LMAKE_SAN),T)
SAN_FLAGS := -fsanitize=thread
endif

else ifeq ($(findstring clang,$(CC)),clang)

ifeq ($(intcmp $(shell $(CC) -dumpversion),15,lt,eq,gt),lt)
$(error clang version must be at least 15)
endif
WARNING_FLAGS += -Wno-misleading-indentation -Wno-unknown-warning-option -Wno-c2x-extensions

endif

# this is the recommanded way to insert a , when calling functions
# /!\ cannot put a comment on the following line or a lot of spaces will be inserted in the variable definition
COMMA         := ,
.DEFAULT_GOAL := DFLT

SAN                 := $(if $(SAN_FLAGS),.san,)
PREPROCESS          := $(CC)             -E                     -ftabstop=4
ASSEMBLE            := $(CC)             -S                     -ftabstop=4
COMPILE             := $(CC) $(COVERAGE) -c -fvisibility=hidden -ftabstop=4
LINK_O              := $(CC) $(COVERAGE) -r
LINK_SO             := $(CC) $(COVERAGE) -pthread -shared-libgcc -shared
LINK_BIN            := $(CC) $(COVERAGE) -pthread
LINK_LIB            := -ldl -lstdc++ -lm
PYTHON_INCLUDE_DIR  := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_path      ("include"  )      )')
PYTHON_LIB_BASE     := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_config_var("LDLIBRARY")[3:-3])') # [3:-3] : transform lib<foo>.so -> <foo>
PYTHON_LINK_OPTIONS := -l$(PYTHON_LIB_BASE)
PYTHON_VERSION      := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_config_var("VERSION"  )      )')
CFLAGS              := $(OPT_FLAGS) -fno-strict-aliasing -pthread -pedantic $(WARNING_FLAGS) -Werror
CXXFLAGS            := $(CFLAGS) -std=$(LANG)
ROOT_DIR            := $(abspath .)
LIB                 := lib
SLIB                := _lib
BIN                 := bin
SBIN                := _bin
DOC                 := doc
SRC                 := src
LMAKE_ENV           := lmake_env
STORE_LIB           := $(SRC)/store

# PYCXX
PYCXX             := ext/pycxx-7.1.7.patched
PYCXX_ROOT        := $(PYCXX)/pycxx-7.1.7
PYCXX_HOME        := $(PYCXX_ROOT)/home
PYCXX_LIB         := $(PYCXX_HOME)/lib
PYCXX_CXX         := $(PYCXX_HOME)/share/python$(PYTHON_VERSION)/CXX
PYCXX_INCLUDE_DIR := $(PYCXX_HOME)/include/python

sys_config.h : sys_config
	CC=$(CC) PYTHON=$(PYTHON) ./$< > $@

HAS_SECCOMP            := $(shell grep -q 'HAS_SECCOMP *1' sys_config.h && echo 1)
HAS_SLURM              := $(shell grep -q 'HAS_SLURM *1'  sys_config.h && echo 1)
PYTHON_LD_LIBRARY_PATH := $(shell awk '$$2=="PYTHON_LD_LIBRARY_PATH" { print substr($$3,2,length($$3)-2) }' sys_config.h)

# Engine
ENGINE_LIB  := $(SRC)/lmakeserver
BACKEND_LIB := $(ENGINE_LIB)/backends

# LMAKE
LMAKE_SERVER_PY_FILES = \
	$(SLIB)/read_makefiles.py        \
	$(SLIB)/serialize.py             \
	$(LIB)/lmake/__init__.py         \
	$(LIB)/lmake/auto_sources.py     \
	$(LIB)/lmake/import_machinery.py \
	$(LIB)/lmake/rules.py            \
	$(LIB)/lmake/sources.py          \
	$(LIB)/lmake/utils.py            \
	$(LIB)/lmake_runtime.py

LMAKE_SERVER_BIN_FILES = \
	$(SBIN)/lmakeserver              \
	$(SBIN)/ldump                    \
	$(SBIN)/ldump_job                \
	$(BIN)/autodep                   \
	$(BIN)/ldebug                    \
	$(BIN)/lforget                   \
	$(BIN)/lmake                     \
	$(BIN)/lmark                     \
	$(BIN)/lshow                     \
	$(BIN)/xxhsum

LMAKE_SERVER_FILES = \
	$(LMAKE_SERVER_PY_FILES)         \
	$(LMAKE_SERVER_BIN_FILES)

LMAKE_REMOTE_FILES = \
	$(SBIN)/job_exec      \
	$(SLIB)/ld_audit.so   \
	$(SLIB)/ld_preload.so \
	$(BIN)/lcheck_deps    \
	$(BIN)/ldecode        \
	$(BIN)/ldepend        \
	$(BIN)/lencode        \
	$(BIN)/ltarget        \
	$(LIB)/clmake.so

LMAKE_BASIC_SAN_OBJS = \
	src/disk$(SAN).o    \
	src/fd$(SAN).o      \
	src/hash$(SAN).o    \
	src/lib$(SAN).o     \
	src/non_portable.o  \
	src/process$(SAN).o \
	src/time$(SAN).o    \
	src/utils$(SAN).o

LMAKE_BASIC_OBJS = \
	src/disk.o         \
	src/fd.o           \
	src/hash.o         \
	src/lib.o          \
	src/non_portable.o \
	src/process.o      \
	src/time.o         \
	src/utils.o

LMAKE_FILES = $(LMAKE_SERVER_FILES) $(LMAKE_REMOTE_FILES)

LMAKE_ALL_FILES = \
	$(LMAKE_FILES)        \
	$(DOC)/lmake_doc.pptx \
	$(DOC)/lmake.html

DFLT : LMAKE UNIT_TESTS LMAKE_TEST lmake.tar.gz

ALL : DFLT STORE_TEST $(DOC)/lmake.html

%.inc_stamp : %                                                                # prepare a stamp to be included, so as to force availability of a file w/o actually including it
	>$@

EXT : $(PYCXX).test.stamp

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
ext/%.patched.stamp : ext/%.dir.stamp ext/%.patch_script
	rm -rf $(@:%.stamp=%)
	cp -r $(@:%.patched.stamp=%.dir) $(@:%.patched.stamp=%.patched)
	cd $(@:%.patched.stamp=%.patched) ; $(ROOT_DIR)/$(@:%.patched.stamp=%.patch_script)
	touch $@
ext/%.patched.h : ext/%.h ext/%.patch_script
	cp $< $@
	cd $(@D) ; $(ROOT_DIR)/$(@:%.patched.h=%.patch_script) $(@F)

.SECONDARY :

%.html : %.texi ; LANGUAGE= LC_ALL= LANG= texi2any --html --no-split --output=$@ $<

#
# LMAKE
#

# add system configuration to lmake.py :
# Sense git bin dir at install time so as to be independent of it at run time.
# Some python installations require LD_LIBRARY_PATH. Handle this at install time so as to be independent at run time.
$(LIB)/%.py : $(SLIB)/%.src.py
	mkdir -p $(@D)
	sed \
		-e 's!\$$BASH!$(BASH)!'                              \
		-e 's!\$$GIT!$(GIT)!'                                \
		-e 's!\$$LD_LIBRARY_PATH!$(PYTHON_LD_LIBRARY_PATH)!' \
		-e 's!\$$STD_PATH!$(STD_PATH)!'                      \
		$< >$@
# for other files, just copy
$(LIB)/% : $(SLIB)/%
	mkdir -p $(@D)
	cp $< $@
# idem for bin
$(BIN)/% : $(SBIN)/%
	mkdir -p $(@D)
	cp $< $@

LMAKE_SERVER : $(LMAKE_SERVER_FILES)
LMAKE_REMOTE : $(LMAKE_REMOTE_FILES)
LMAKE        : LMAKE_SERVER LMAKE_REMOTE

#
# PYCXX
#

$(PYCXX).install.stamp : $(PYCXX).stamp
	rm -rf $(PYCXX_HOME)
	cd $(PYCXX_ROOT) ; $(PYTHON) setup.py install --home=$(ROOT_DIR)/$(PYCXX_HOME)
	mv $(PYCXX_CXX)/cxxextensions.c $(PYCXX_CXX)/cxxextensions.cxx             # painful to provide compilation rules for both .c and .cxx
	ln -s . $(PYCXX_CXX)/Src                                                   # support files include files starting with Src which is not installed
	touch $@
$(PYCXX_LIB)/%$(SAN).o : $(PYCXX).install.stamp
	$(COMPILE) $(CXXFLAGS) $(SAN_FLAGS) -fPIC -I $(PYCXX_INCLUDE_DIR) -I $(PYTHON_INCLUDE_DIR) -o $@ $(@:$(PYCXX_LIB)/%$(SAN).o=$(PYCXX_CXX)/%.cxx)
$(PYCXX).test.stamp : $(PYCXX).stamp
	cd $(@:%.test.stamp=%.patched) ; $(PYTHON) setup_makefile.py linux Makefile
	cd $(@:%.test.stamp=%.patched) ; make clean ; make -j8 test
	touch $@
$(PYCXX_LIB)/pycxx$(SAN).o : $(patsubst %,$(PYCXX_LIB)/%$(SAN).o, cxxsupport cxx_extensions cxx_exceptions cxxextensions IndirectPythonInterface )
	$(LINK_O) $(SAN_FLAGS) -fPIC -o $@ $^

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
	rm -rf   $(@D)
	mkdir -p $(@D)
	./$<     $(@D)
	touch    $@

$(STORE_LIB)/big_test.dir/tok : $(STORE_LIB)/big_test.py LMAKE
	mkdir -p $(@D)
	rm -rf   $(@D)/LMAKE
	PATH=$$PWD/_bin:$$PWD/bin:$$PATH ; ( cd $(@D) ; $(PYTHON) ../big_test.py / 2000000 )
	touch $@

#
# engine
#

SLIB_H    := $(patsubst %, $(SRC)/%.hh         , app client config disk fd hash lib non_portable process pycxx rpc_client rpc_job serialize thread time trace utils    )
AUTODEP_H := $(patsubst %, $(SRC)/autodep/%.hh , env support gather_deps ptrace record                                                                                 )
STORE_H   := $(patsubst %, $(SRC)/store/%.hh   , alloc file prefix red_black side_car struct vector                                                                    )
ENGINE_H  := $(patsubst %, $(ENGINE_LIB)/%.hh  , backend.x cache.x caches/dir_cache cmd.x codec core core.x global.x idxed job.x makefiles node.x req.x rule.x store.x )
BACKEND_H := $(patsubst %, $(BACKEND_LIB)/%.hh , generic                                                                                                               )

ALL_H         := sys_config.h $(PYCXX).install.stamp ext/xxhash.patched.h
ALL_TOP_H     := $(ALL_H) $(SLIB_H) $(AUTODEP_H)
ALL_ENGINE_H  := $(ALL_TOP_H) $(ENGINE_H) $(STORE_H)
ALL_BACKEND_H := $(ALL_TOP_H) $(ENGINE_H) $(BACKEND_H)

# On ubuntu, seccomp.h is in /usr/include. On CenOS7, it is in /usr/include/linux, but beware that otherwise, /usr/include must be prefered, hence -idirafter
INCLUDES := -I ext -I $(PYTHON_INCLUDE_DIR) -I $(PYCXX_INCLUDE_DIR) -I $(SRC) -I $(ENGINE_LIB) -I. -idirafter /usr/include/linux

$(BACKEND_LIB)/%.san.o : $(BACKEND_LIB)/%.cc $(ALL_BACKEND_H) ; $(COMPILE)    $(CXXFLAGS) $(SAN_FLAGS) -frtti -fPIC $(INCLUDES) -o $@ $<
$(BACKEND_LIB)/%.i     : $(BACKEND_LIB)/%.cc $(ALL_BACKEND_H) ; $(PREPROCESS) $(CXXFLAGS)                           $(INCLUDES) -o $@ $<
$(BACKEND_LIB)/%.o     : $(BACKEND_LIB)/%.cc $(ALL_BACKEND_H) ; $(COMPILE)    $(CXXFLAGS)              -frtti -fPIC $(INCLUDES) -o $@ $<

$(ENGINE_LIB)/%.san.o  : $(ENGINE_LIB)/%.cc  $(ALL_ENGINE_H)  ; $(COMPILE)    $(CXXFLAGS) $(SAN_FLAGS) -frtti -fPIC $(INCLUDES) -o $@ $<
$(ENGINE_LIB)/%.i      : $(ENGINE_LIB)/%.cc  $(ALL_ENGINE_H)  ; $(PREPROCESS) $(CXXFLAGS)                           $(INCLUDES) -o $@ $<
$(ENGINE_LIB)/%.o      : $(ENGINE_LIB)/%.cc  $(ALL_ENGINE_H)  ; $(COMPILE)    $(CXXFLAGS)              -frtti -fPIC $(INCLUDES) -o $@ $<

$(SRC)/%.san.o         : $(SRC)/%.cc         $(ALL_TOP_H)     ; $(COMPILE)    $(CXXFLAGS) $(SAN_FLAGS) -frtti -fPIC $(INCLUDES) -o $@ $<
$(SRC)/%.s             : $(SRC)/%.cc         $(ALL_TOP_H)     ; $(ASSEMBLE)   $(CXXFLAGS)                           $(INCLUDES) -o $@ $<
$(SRC)/%.i             : $(SRC)/%.cc         $(ALL_TOP_H)     ; $(PREPROCESS) $(CXXFLAGS)                           $(INCLUDES) -o $@ $<
$(SRC)/%.o             : $(SRC)/%.cc         $(ALL_TOP_H)     ; $(COMPILE)    $(CXXFLAGS)              -frtti -fPIC $(INCLUDES) -o $@ $<

%.san.o                : %.cc                $(ALL_H)         ; $(COMPILE)    $(CXXFLAGS) $(SAN_FLAGS) -frtti -fPIC $(INCLUDES) -o $@ $<
%.i                    : %.cc                $(ALL_H)         ; $(PREPROCESS) $(CXXFLAGS)                           $(INCLUDES) -o $@ $<
%.o                    : %.cc                $(ALL_H)         ; $(COMPILE)    $(CXXFLAGS)              -frtti -fPIC $(INCLUDES) -o $@ $<

#
# lmake
#

# on CentOS7, gcc looks for libseccomp.so with -lseccomp, but only libseccomp.so.2 exists, and this works everywhere.
LIB_SECCOMP := $(if $(HAS_SECCOMP),-l:libseccomp.so.2)

$(SRC)/autodep/ld_preload.o : $(SRC)/autodep/ld.cc $(SRC)/autodep/syscall.cc
$(SRC)/autodep/ld_audit.o   : $(SRC)/autodep/ld.cc $(SRC)/autodep/syscall.cc
$(SRC)/autodep/ptrace.o             :              $(SRC)/autodep/syscall.cc

$(SBIN)/lmakeserver : \
	$(LMAKE_BASIC_SAN_OBJS)                                      \
	$(PYCXX_LIB)/pycxx$(SAN).o                                   \
	$(SRC)/app$(SAN).o                                           \
	$(SRC)/pycxx$(SAN).o                                         \
	$(SRC)/rpc_client$(SAN).o                                    \
	$(SRC)/rpc_job$(SAN).o                                       \
	$(SRC)/trace$(SAN).o                                         \
	$(SRC)/store/file$(SAN).o                                    \
	$(SRC)/autodep/env$(SAN).o                                   \
	$(SRC)/autodep/gather_deps$(SAN).o                           \
	$(SRC)/autodep/ptrace$(SAN).o                                \
	$(SRC)/autodep/record$(SAN).o                                \
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
	mkdir -p $(@D)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PYTHON_LINK_OPTIONS) $(LIB_SECCOMP) $(LINK_LIB)

$(SBIN)/ldump : \
	$(LMAKE_BASIC_SAN_OBJS)                     \
	$(PYCXX_LIB)/pycxx$(SAN).o                  \
	$(SRC)/app$(SAN).o                          \
	$(SRC)/pycxx$(SAN).o                        \
	$(SRC)/rpc_client$(SAN).o                   \
	$(SRC)/rpc_job$(SAN).o                      \
	$(SRC)/trace$(SAN).o                        \
	$(SRC)/autodep/env$(SAN).o                  \
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
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PYTHON_LINK_OPTIONS) $(LINK_LIB)

$(SBIN)/ldump_job : \
	$(LMAKE_BASIC_SAN_OBJS)    \
	$(SRC)/app$(SAN).o         \
	$(SRC)/rpc_job$(SAN).o     \
	$(SRC)/trace$(SAN).o       \
	$(SRC)/autodep/env$(SAN).o \
	$(SRC)/ldump_job$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PYTHON_LINK_OPTIONS) $(LINK_LIB)

$(SBIN)/job_exec : \
	$(LMAKE_BASIC_SAN_OBJS)            \
	$(PYCXX_LIB)/pycxx$(SAN).o         \
	$(SRC)/app$(SAN).o                 \
	$(SRC)/pycxx$(SAN).o               \
	$(SRC)/rpc_job$(SAN).o             \
	$(SRC)/trace$(SAN).o               \
	$(SRC)/autodep/env$(SAN).o         \
	$(SRC)/autodep/gather_deps$(SAN).o \
	$(SRC)/autodep/ptrace$(SAN).o      \
	$(SRC)/autodep/record$(SAN).o      \
	$(SRC)/job_exec$(SAN).o
	mkdir -p $(@D)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PYTHON_LINK_OPTIONS) $(LIB_SECCOMP) $(LINK_LIB)

$(BIN)/lmake : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/lmake$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/ldebug : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/ldebug$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lshow : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/lshow$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lforget : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/lforget$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lmark : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/lmark$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/xxhsum : \
	$(LMAKE_BASIC_SAN_OBJS)   \
	$(SRC)/xxhsum.o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/autodep : \
	$(LMAKE_BASIC_SAN_OBJS)            \
	$(SRC)/app$(SAN).o                 \
	$(SRC)/rpc_job$(SAN).o             \
	$(SRC)/trace$(SAN).o               \
	$(SRC)/autodep/env$(SAN).o         \
	$(SRC)/autodep/gather_deps$(SAN).o \
	$(SRC)/autodep/ptrace$(SAN).o      \
	$(SRC)/autodep/record$(SAN).o      \
	$(SRC)/autodep/autodep$(SAN).o
	mkdir -p $(@D)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LIB_SECCOMP) $(LINK_LIB)

#
# remote
#

# remote executables generate errors when -fsanitize=thread, but are mono-thread, so we don't care

$(BIN)/ldecode : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/support.o \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/ldecode.o
	mkdir -p $(BIN)
	$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/ldepend : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/support.o \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/ldepend.o
	mkdir -p $(BIN)
	$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/lencode : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/support.o \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/lencode.o
	mkdir -p $(BIN)
	$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/ltarget : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/support.o \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/ltarget.o
	mkdir -p $(BIN)
	$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/lcheck_deps : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/app.o             \
	$(SRC)/rpc_job.o         \
	$(SRC)/trace.o           \
	$(SRC)/autodep/support.o \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/lcheck_deps.o
	mkdir -p $(BIN)
	$(LINK_BIN) -o $@ $^ $(LINK_LIB)

# remote libs generate errors when -fsanitize=thread // XXX fix these errors and use $(SAN)

$(SLIB)/ld_preload.so : \
	$(LMAKE_BASIC_OBJS)     \
	$(SRC)/rpc_job.o        \
	$(SRC)/autodep/env.o    \
	$(SRC)/autodep/record.o \
	$(SRC)/autodep/ld_preload.o
	mkdir -p $(@D)
	$(LINK_SO) -o $@ $^ $(LINK_LIB)

$(SLIB)/ld_audit.so : \
	$(LMAKE_BASIC_OBJS)     \
	$(SRC)/rpc_job.o        \
	$(SRC)/autodep/env.o    \
	$(SRC)/autodep/record.o \
	$(SRC)/autodep/ld_audit.o
	mkdir -p $(@D)
	$(LINK_SO) -o $@ $^ $(LINK_LIB)

$(LIB)/clmake.so : \
	$(LMAKE_BASIC_OBJS)      \
	$(SRC)/rpc_job.o         \
	$(SRC)/autodep/support.o \
	$(SRC)/autodep/env.o     \
	$(SRC)/autodep/record.o  \
	$(SRC)/autodep/clmake.o
	mkdir -p $(@D)
	$(LINK_SO) -o $@ $^ $(PYTHON_LINK_OPTIONS) $(LINK_LIB)

#
# Manifest
#
Manifest : .git/index
	git ls-files >$@
include Manifest.inc_stamp                                                     # Manifest is used in this makefile

#
# Unit tests
#

UT_DIR      := unit_tests
UT_BASE_DIR := $(UT_DIR)/base
UT_BASE     := Manifest $(shell grep -x '$(UT_BASE_DIR)/.*' Manifest)

UNIT_TESTS1 : Manifest $(patsubst %.script,%.dir/tok, $(shell grep -x '$(UT_DIR)/.*\.script' Manifest) )
UNIT_TESTS2 : Manifest $(patsubst %.py,%.dir/tok,     $(shell grep -x '$(UT_DIR)/[^/]*\.py'  Manifest) )

UNIT_TESTS : UNIT_TESTS1 UNIT_TESTS2

%.dir/tok : %.script $(LMAKE_FILES) $(UT_BASE) Manifest
	mkdir -p $(@D)
	( cd $(@D) ; git clean -ffdxq || : )                                       # keep $(@D) to ease debugging, ignore rc as old versions of git work but generate an error
	for f in $$(grep '^$(UT_DIR)/base/' Manifest) ; do df=$(@D)/$${f#$(UT_DIR)/base/} ; mkdir -p $$(dirname $$df) ; cp $$f $$df ; done
	cd $(@D) ; find . -type f -printf '%P\n' > Manifest
	( cd $(@D) ; PATH=$(ROOT_DIR)/bin:$(ROOT_DIR)/_bin:$$PATH $(ROOT_DIR)/$< ) > $@ || ( cat $@ ; rm $@ ; exit 1 )

%.dir/tok : %.py $(LMAKE_FILES) _lib/ut.py
	mkdir -p $(@D)
	( cd $(@D) ; git clean -ffdxq || : )                                       # keep $(@D) to ease debugging, ignore rc as old versions of git work but generate an error
	cp $< $(@D)/Lmakefile.py
	( cd $(@D) ; PATH=$(ROOT_DIR)/bin:$(ROOT_DIR)/_bin:$$PATH PYTHONPATH=$(ROOT_DIR)/lib:$(ROOT_DIR)/_lib HOME= $(PYTHON) Lmakefile.py ) >$@ 2>$@.err || ( cat $@ ; cat $@.err ; rm $@ ; exit 1 )
	cat $@.err

#
# lmake env
#

#
# lmake under lmake
#

LMAKE_ENV  : $(LMAKE_ENV)/stamp
LMAKE_TEST : $(LMAKE_ENV)/tok

$(LMAKE_ENV)/Manifest : Manifest
	@mkdir -p $(@D)
	grep -e ^_bin/ -e ^_lib/ -e ^doc/ -e ^ext/ -e ^lib/ -e ^src/ -e ^sys_config\$$ Manifest > $@
	grep ^$(@D)/ Manifest | sed s:$(@D)/::                                                  >>$@
	echo $(@F)                                                                              >>$@
$(LMAKE_ENV)/% : %
	@mkdir -p $(@D)
	cp $< $@
$(LMAKE_ENV)/stamp : $(LMAKE_ALL_FILES) $(LMAKE_ENV)/Manifest $(patsubst %,$(LMAKE_ENV)/%,$(shell grep -e ^_bin/ -e ^_lib/ -e ^doc/ -e ^ext/ -e ^lib/ -e ^src/ -e ^sys_config\$$ Manifest))
	mkdir -p $(LMAKE_ENV)-cache/LMAKE
	echo '300M' > $(LMAKE_ENV)-cache/LMAKE/size
	touch $@
$(LMAKE_ENV)/tok : $(LMAKE_ENV)/stamp $(LMAKE_ENV)/Lmakefile.py
	set -e ; cd $(LMAKE_ENV) ; CC=$(CC) $(ROOT_DIR)/bin/lmake lmake.tar.gz & sleep 1 ; CC=$(CC) $(ROOT_DIR)/bin/lmake lmake.tar.gz >$(@F) ; wait $$! ; touch $(@F)

#
# archive
#
VERSION     := 0.1
ARCHIVE_DIR := open-lmake-$(VERSION)
lmake.tar.gz  : TAR_COMPRESS := z
lmake.tar.bz2 : TAR_COMPRESS := j
lmake.tar.gz lmake.tar.bz2 : $(LMAKE_ALL_FILES)
	rm -rf $(ARCHIVE_DIR)
	for d in $^ ; do mkdir -p $$(dirname $(ARCHIVE_DIR)/$$d) ; cp $$d $(ARCHIVE_DIR)/$$d ; done
	tar c$(TAR_COMPRESS) -f $@ $(ARCHIVE_DIR)

#
# For debian packaging
#
install: $(LMAKE_BINS) $(LMAKE_REMOTE_FILES) $(LMAKE_SERVER_PY_FILES) $(DOC)/lmake.html
	for f in $(LMAKE_SERVER_BIN_FILES); do install -D        $$f $(DESTDIR)/$(prefix)/lib/open-lmake/$$f ; done
	for f in $(LMAKE_REMOTE_FILES)    ; do install -D        $$f $(DESTDIR)/$(prefix)/lib/open-lmake/$$f ; done
	for f in $(LMAKE_SERVER_PY_FILES) ; do install -D -m 644 $$f $(DESTDIR)/$(prefix)/lib/open-lmake/$$f ; done
	install -D $(DOC)/lmake.html $(DESTDIR)/$(prefix)/share/doc/open-lmake/html/lmake.html

# uncoment to automatically cleanup repo before building package
# clean:
# 	git clean -ffdx
