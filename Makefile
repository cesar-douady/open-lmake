# This file is part of the lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

#
# build configuration
#vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

MAKEFLAGS := -j$(shell nproc||echo 1) -k

PYTHON := $(shell bash -c 'type -p python3')

CC := $(shell bash -c 'type -p gcc-12 || type -p gcc-11 || type -p gcc')
#CC := $(shell bash -c 'type -p clang')

OPT_FLAGS := -O3
#OPT_FLAGS := -O3 -DNDEBUG                                                     # better, as there are numerous asserts that have a perf impact, but too early for now

GIT := $(shell bash -c 'type -p git')

#^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
#

MAKEFLAGS += -r -R

ifneq ($(LMAKE_DEBUG),)
OPT_FLAGS := -O0 -g
endif
COVERAGE :=
ifneq ($(LMAKE_COVERAGE),)
COVERAGE := --coverage                                                         # XXX : not operational yet
endif

WARNING_FLAGS := -Wall -Wextra -Wno-cast-function-type -Wno-type-limits
ifeq ($(strip $(CC)),clang)
endif

LANG := c++20

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
ASAN_FLAGS := -fsanitize=address -fsanitize=undefined
endif
ifeq ($(LMAKE_SAN),T)
TSAN_FLAGS := -fsanitize=thread
endif

else ifeq ($(findstring clang,$(CC)),clang)

ifeq ($(intcmp $(shell $(CC) -dumpversion),15,lt,eq,gt),lt)
$(error clang version must be at least 15)
endif
WARNING_FLAGS += -Wno-misleading-indentation -Wno-unknown-warning-option -Wno-c2x-extensions

endif

SAN_FLAGS           := $(strip $(ASAN_FLAGS) $(TSAN_FLAGS))
SAN                 := $(if $(SAN_FLAGS),.san,)
PREPROCESS          := $(CC)             -E                     -ftabstop=4
COMPILE             := $(CC) $(COVERAGE) -c -fvisibility=hidden -ftabstop=4
LINK_LIB_PATH       := $(shell $(CC) -v -E /dev/null 2>&1 | grep LIBRARY_PATH= | cut -d= -f2 | sed 's/:/ /g' | xargs realpath | uniq | sed s/^/-Wl,-rpath=/)
LINK_O              := $(CC) $(COVERAGE) -r
LINK_SO             := $(CC) $(COVERAGE) $(LINK_LIB_PATH) -pthread -shared-libgcc -shared
LINK_BIN            := $(CC) $(COVERAGE) $(LINK_LIB_PATH) -pthread
LINK_LIB            := -ldl -lstdc++ -lm
PYTHON_INCLUDE_DIR  := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_path      ("include"  )      )')
PYTHON_LIB_BASE     := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_config_var("LDLIBRARY")[3:-3])') # [3:-3] : transform lib<foo>.so -> <foo>
PYTHON_LIB_DIR      := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_config_var("LIBDIR"   )      )')
PYTHON_LINK_OPTIONS := -L$(PYTHON_LIB_DIR) -Wl,-rpath=$(PYTHON_LIB_DIR) -l$(PYTHON_LIB_BASE)
PYTHON_VERSION      := $(shell $(PYTHON) -c 'import sysconfig ; print(sysconfig.get_config_var("VERSION"  )      )')
CFLAGS              := $(OPT_FLAGS) -fno-strict-aliasing -pthread -pedantic $(WARNING_FLAGS) -Werror
CXXFLAGS            := $(CFLAGS) -std=$(LANG)
ROOT                := $(shell pwd)
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

# SECCOMP
SECCOMP             := ext/libseccomp-2.1.0.dir
SECCOMP_ROOT        := $(SECCOMP)/libseccomp-2.1.0
SECCOMP_INCLUDE_DIR := $(SECCOMP_ROOT)/include
SECCOMP_LIB_DIR     := $(SECCOMP_ROOT)/src

# Engine
ENGINE_LIB := $(SRC)/lmakeserver

# LMAKE
LMAKE_FILES = \
	$(DOC)/lmake_doc.pptx         \
	$(DOC)/lmake.html             \
	unit_tests/base/Lmakefile.py  \
	$(SLIB)/autodep_ld_audit.so   \
	$(SLIB)/autodep_ld_preload.so \
	$(SLIB)/read_makefiles.py     \
	$(SLIB)/serialize.py          \
	$(SBIN)/job_exec              \
	$(SBIN)/lmakeserver           \
	$(SBIN)/ldump                 \
	$(SBIN)/ldump_job             \
	$(LIB)/lmake.py               \
	$(LIB)/clmake.so              \
	$(BIN)/autodep                \
	$(BIN)/lcheck_deps            \
	$(BIN)/ldepend                \
	$(BIN)/lforget                \
	$(BIN)/lfreeze                \
	$(BIN)/lmake                  \
	$(BIN)/lshow                  \
	$(BIN)/ltarget                \
	$(BIN)/xxhsum

DFLT : LMAKE UNIT_TESTS LMAKE_TEST lmake.tar.gz

ALL : DFLT STORE_TEST $(DOC)/lmake.html

sys_config.h : sys_config
	CC=$(CC) PYTHON=$(PYTHON) ./$< > $@

lmake.tar.gz : $(LMAKE_FILES)
	tar -cz -f $@ $^

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
	cd $(@:%.patched.stamp=%.patched) ; $(ROOT)/$(@:%.patched.stamp=%.patch_script)
	touch $@
ext/%.patched.h : ext/%.h ext/%.patch_script
	cp $< $@
	cd $(@D) ; $(ROOT)/$(@:%.patched.h=%.patch_script) $(@F)

.SECONDARY :

%.html : %.texi ; texi2any --html --no-split --output=$@ $<

#
# LMAKE
#

# add system configuration to lmake.py :
# Sense git bin dir at install time so as to be independent of it at run time.
# Some python installations require LD_LIBRARY_PATH. Handle this at install time so as to be independent at run time.
$(LIB)/lmake.py : $(SLIB)/lmake.src.py
	mkdir -p $(@D)
	cp $<    $@
	echo "_git = '$(GIT)'" >>$@
	[ '$(LD_LIBRARY_PATH)' = '' ] || echo "if _reading_makefiles : Rule.environ_cmd.LD_LIBRARY_PATH = '$(LD_LIBRARY_PATH)'" >>$@

LMAKE : $(LMAKE_FILES)

#
# PYCXX
#

$(PYCXX).install.stamp : $(PYCXX).stamp
	rm -rf $(PYCXX_HOME)
	cd $(PYCXX_ROOT) ; $(PYTHON) setup.py install --home=$(ROOT)/$(PYCXX_HOME)
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
# SECCOMP
#

$(SECCOMP).install.stamp : $(SECCOMP).stamp
	cd $(SECCOMP_ROOT) ; ./configure ; MAKEFLAGS= make
	touch $@

#
# store
#

STORE_TEST : $(STORE_LIB)/unit_test.dir/tok $(STORE_LIB)/big_test.dir/tok

$(STORE_LIB)/unit_test : \
	$(STORE_LIB)/file$(SAN).o \
	$(SRC)/app$(SAN).o        \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
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

INCLUDE_SECCOMP := -I $(SECCOMP_INCLUDE_DIR)
INSTALL_SECCOMP := $(SECCOMP).install.stamp

SLIB_H    := $(patsubst %, $(SRC)/%.hh         , app client config disk hash lib non_portable pycxx rpc_client rpc_job serialize time trace utils          )
AUTODEP_H := $(patsubst %, $(SRC)/autodep/%.hh , autodep_ld autodep_support gather_deps ptrace record                                                      )
STORE_H   := $(patsubst %, $(SRC)/store/%.hh   , alloc file prefix red_black side_car struct vector                                                        )
ENGINE_H  := $(patsubst %, $(ENGINE_LIB)/%.hh  , backend.x cache.x caches/dir_cache cmd.x core core.x global.x job.x makefiles node.x req.x rule.x store.x )

ALL_TOP_H    := sys_config.h $(SLIB_H) $(AUTODEP_H) $(PYCXX).install.stamp $(INSTALL_SECCOMP) ext/xxhash.patched.h
ALL_ENGINE_H := $(ALL_TOP_H) $(ENGINE_H) $(STORE_H)

INCLUDES := -I $(SRC) -I $(ENGINE_LIB) -I ext -I $(PYTHON_INCLUDE_DIR) -I $(PYCXX_INCLUDE_DIR) $(INCLUDE_SECCOMP) -I.
%.san.i : %.cc $(ALL_ENGINE_H) ; $(PREPROCESS) $(CXXFLAGS) $(SAN_FLAGS)              $(INCLUDES) -o $@ $<
%.san.o : %.cc $(ALL_ENGINE_H) ; $(COMPILE)    $(CXXFLAGS) $(SAN_FLAGS) -frtti -fPIC $(INCLUDES) -o $@ $<
%.i     : %.cc $(ALL_ENGINE_H) ; $(PREPROCESS) $(CXXFLAGS)                           $(INCLUDES) -o $@ $<
%.o     : %.cc $(ALL_ENGINE_H) ; $(COMPILE)    $(CXXFLAGS)              -frtti -fPIC $(INCLUDES) -o $@ $<

#
# lmake
#

LIB_SECCOMP := -L $(SECCOMP_LIB_DIR) -lseccomp

$(SBIN)/lmakeserver : \
	$(PYCXX_LIB)/pycxx$(SAN).o                  \
	$(SRC)/app$(SAN).o                          \
	$(SRC)/disk$(SAN).o                         \
	$(SRC)/hash$(SAN).o                         \
	$(SRC)/lib$(SAN).o                          \
	$(SRC)/non_portable.o                       \
	$(SRC)/pycxx$(SAN).o                        \
	$(SRC)/rpc_client$(SAN).o                   \
	$(SRC)/rpc_job$(SAN).o                      \
	$(SRC)/time$(SAN).o                         \
	$(SRC)/trace$(SAN).o                        \
	$(SRC)/utils$(SAN).o                        \
	$(SRC)/store/file$(SAN).o                   \
	$(SRC)/autodep/gather_deps$(SAN).o          \
	$(SRC)/autodep/ptrace$(SAN).o               \
	$(SRC)/autodep/record$(SAN).o               \
	$(SRC)/lmakeserver/backend$(SAN).o          \
	$(SRC)/lmakeserver/backends/local$(SAN).o   \
	$(SRC)/lmakeserver/cache$(SAN).o            \
	$(SRC)/lmakeserver/caches/dir_cache$(SAN).o \
	$(SRC)/lmakeserver/cmd$(SAN).o              \
	$(SRC)/lmakeserver/global$(SAN).o           \
	$(SRC)/lmakeserver/job$(SAN).o              \
	$(SRC)/lmakeserver/makefiles$(SAN).o        \
	$(SRC)/lmakeserver/node$(SAN).o             \
	$(SRC)/lmakeserver/req$(SAN).o              \
	$(SRC)/lmakeserver/rule$(SAN).o             \
	$(SRC)/lmakeserver/store$(SAN).o            \
	$(SRC)/lmakeserver/main$(SAN).o
	mkdir -p $(@D)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PYTHON_LINK_OPTIONS) $(LIB_SECCOMP) $(LINK_LIB)

$(SBIN)/ldump : \
	$(PYCXX_LIB)/pycxx$(SAN).o                  \
	$(SRC)/app$(SAN).o                          \
	$(SRC)/disk$(SAN).o                         \
	$(SRC)/hash$(SAN).o                         \
	$(SRC)/lib$(SAN).o                          \
	$(SRC)/non_portable.o                       \
	$(SRC)/pycxx$(SAN).o                        \
	$(SRC)/rpc_client$(SAN).o                   \
	$(SRC)/rpc_job$(SAN).o                      \
	$(SRC)/time$(SAN).o                         \
	$(SRC)/trace$(SAN).o                        \
	$(SRC)/utils$(SAN).o                        \
	$(SRC)/store/file$(SAN).o                   \
	$(SRC)/lmakeserver/backend$(SAN).o          \
	$(SRC)/lmakeserver/cache$(SAN).o            \
	$(SRC)/lmakeserver/caches/dir_cache$(SAN).o \
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
	$(SRC)/app$(SAN).o     \
	$(SRC)/disk$(SAN).o    \
	$(SRC)/hash$(SAN).o    \
	$(SRC)/lib$(SAN).o     \
	$(SRC)/non_portable.o  \
	$(SRC)/rpc_job$(SAN).o \
	$(SRC)/time$(SAN).o    \
	$(SRC)/trace$(SAN).o   \
	$(SRC)/utils$(SAN).o   \
	$(SRC)/ldump_job$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PYTHON_LINK_OPTIONS) $(LINK_LIB)

$(SBIN)/job_exec : \
	$(PYCXX_LIB)/pycxx$(SAN).o         \
	$(SRC)/app$(SAN).o                 \
	$(SRC)/disk$(SAN).o                \
	$(SRC)/hash$(SAN).o                \
	$(SRC)/lib$(SAN).o                 \
	$(SRC)/non_portable.o              \
	$(SRC)/pycxx$(SAN).o               \
	$(SRC)/rpc_job$(SAN).o             \
	$(SRC)/time$(SAN).o                \
	$(SRC)/trace$(SAN).o               \
	$(SRC)/utils$(SAN).o               \
	$(SRC)/autodep/gather_deps$(SAN).o \
	$(SRC)/autodep/ptrace$(SAN).o      \
	$(SRC)/autodep/record$(SAN).o      \
	$(SRC)/job_exec$(SAN).o
	mkdir -p $(@D)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(PYTHON_LINK_OPTIONS) $(LIB_SECCOMP) $(LINK_LIB)

$(BIN)/lmake : \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
	$(SRC)/lmake$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lshow : \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
	$(SRC)/lshow$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lforget : \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
	$(SRC)/lforget$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/lfreeze : \
	$(SRC)/app$(SAN).o        \
	$(SRC)/client$(SAN).o     \
	$(SRC)/disk$(SAN).o       \
	$(SRC)/lib$(SAN).o        \
	$(SRC)/non_portable.o     \
	$(SRC)/rpc_client$(SAN).o \
	$(SRC)/time$(SAN).o       \
	$(SRC)/trace$(SAN).o      \
	$(SRC)/utils$(SAN).o      \
	$(SRC)/lfreeze$(SAN).o
	mkdir -p $(BIN)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

$(BIN)/xxhsum : \
	$(SRC)/disk.o         \
	$(SRC)/hash.o         \
	$(SRC)/lib.o          \
	$(SRC)/non_portable.o \
	$(SRC)/time.o         \
	$(SRC)/utils.o        \
	$(SRC)/xxhsum.o
	mkdir -p $(BIN)
	$(LINK_BIN) -o $@ $^ $(LINK_LIB)

#
# job_exec
#

# ldepend generates error when -fsanitize=thread, but is mono-thread, so we don't care
$(BIN)/ldepend : \
	$(SRC)/app.o                     \
	$(SRC)/disk.o                    \
	$(SRC)/hash.o                    \
	$(SRC)/lib.o                     \
	$(SRC)/non_portable.o            \
	$(SRC)/rpc_job.o                 \
	$(SRC)/time.o                    \
	$(SRC)/trace.o                   \
	$(SRC)/utils.o                   \
	$(SRC)/autodep/autodep_support.o \
	$(SRC)/autodep/record.o          \
	$(SRC)/autodep/ldepend.o
	mkdir -p $(BIN)
	$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/ltarget : \
	$(SRC)/app.o                     \
	$(SRC)/disk.o                    \
	$(SRC)/hash.o                    \
	$(SRC)/lib.o                     \
	$(SRC)/non_portable.o            \
	$(SRC)/rpc_job.o                 \
	$(SRC)/time.o                    \
	$(SRC)/trace.o                   \
	$(SRC)/utils.o                   \
	$(SRC)/autodep/autodep_support.o \
	$(SRC)/autodep/record.o          \
	$(SRC)/autodep/ltarget.o
	mkdir -p $(BIN)
	$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/lcheck_deps : \
	$(SRC)/app.o                     \
	$(SRC)/disk.o                    \
	$(SRC)/hash.o                    \
	$(SRC)/lib.o                     \
	$(SRC)/non_portable.o            \
	$(SRC)/rpc_job.o                 \
	$(SRC)/time.o                    \
	$(SRC)/trace.o                   \
	$(SRC)/utils.o                   \
	$(SRC)/autodep/autodep_support.o \
	$(SRC)/autodep/record.o          \
	$(SRC)/autodep/lcheck_deps.o
	mkdir -p $(BIN)
	$(LINK_BIN) -o $@ $^ $(LINK_LIB)

$(BIN)/autodep : \
	$(SRC)/app$(SAN).o                 \
	$(SRC)/disk$(SAN).o                \
	$(SRC)/hash$(SAN).o                \
	$(SRC)/lib$(SAN).o                 \
	$(SRC)/non_portable.o              \
	$(SRC)/rpc_job$(SAN).o             \
	$(SRC)/time$(SAN).o                \
	$(SRC)/trace$(SAN).o               \
	$(SRC)/utils$(SAN).o               \
	$(SRC)/autodep/gather_deps$(SAN).o \
	$(SRC)/autodep/ptrace$(SAN).o      \
	$(SRC)/autodep/record$(SAN).o      \
	$(SRC)/autodep/autodep$(SAN).o
	mkdir -p $(@D)
	$(LINK_BIN) $(SAN_FLAGS) -o $@ $^ $(LIB_SECCOMP) $(LINK_LIB)

$(SRC)/autodep/autodep_ld_preload.o : $(SRC)/autodep/autodep_ld.cc
$(SLIB)/autodep_ld_preload.so : \
	$(SRC)/disk.o           \
	$(SRC)/hash.o           \
	$(SRC)/lib.o            \
	$(SRC)/non_portable.o   \
	$(SRC)/rpc_job.o        \
	$(SRC)/time.o           \
	$(SRC)/utils.o          \
	$(SRC)/autodep/record.o \
	$(SRC)/autodep/autodep_ld_preload.o
	mkdir -p $(@D)
	$(LINK_SO) -o $@ $^ $(LIB_SECCOMP) $(LINK_LIB)

$(SRC)/autodep/autodep_ld_audit.o : $(SRC)/autodep/autodep_ld.cc
$(SLIB)/autodep_ld_audit.so : \
	$(SRC)/disk.o               \
	$(SRC)/hash.o               \
	$(SRC)/lib.o                \
	$(SRC)/non_portable.o       \
	$(SRC)/rpc_job.o            \
	$(SRC)/time.o               \
	$(SRC)/utils.o              \
	$(SRC)/autodep/record.o     \
	$(SRC)/autodep/autodep_ld_audit.o
	mkdir -p $(@D)
	$(LINK_SO) -o $@ $^ $(LIB_SECCOMP) $(LINK_LIB)

$(LIB)/clmake.so : \
	$(SRC)/disk.o                    \
	$(SRC)/hash.o                    \
	$(SRC)/lib.o                     \
	$(SRC)/non_portable.o            \
	$(SRC)/rpc_job.o                 \
	$(SRC)/time.o                    \
	$(SRC)/utils.o                   \
	$(SRC)/autodep/autodep_support.o \
	$(SRC)/autodep/record.o          \
	$(SRC)/autodep/clmake.o
	mkdir -p $(@D)
	$(LINK_SO) -o $@ $^ $(PYTHON_LINK_OPTIONS) $(LINK_LIB)

#
# Manifest
#
Manifest : .git/index
	git ls-files >$@
Manifest.stamp : Manifest
	>$@
include Manifest.stamp                                                         # force make to remake Manifest without reading it

#
# Unit tests
#

UT_DIR      := unit_tests
UT_BASE_DIR := $(UT_DIR)/base

UNIT_TESTS1 : Manifest $(patsubst %.script,%.dir/tok, $(shell grep -x '$(UT_DIR)/.*\.script' Manifest) )
UNIT_TESTS2 : Manifest $(patsubst %.py,%.dir/tok,     $(shell grep -x '$(UT_DIR)/[^/]*\.py'  Manifest) )

UNIT_TESTS : UNIT_TESTS1 UNIT_TESTS2

%.dir/tok : %.script $(LMAKE_FILES) $(UT_BASE) Manifest
	mkdir -p $(@D)
	( cd $(@D) ; git clean -ffdxq || : )                                       # keep $(@D) to ease debugging, ignore rc as old versions of git work but generate an error
	for f in $$(grep '^$(UT_DIR)/base/' Manifest) ; do df=$(@D)/$${f#$(UT_DIR)/base/} ; mkdir -p $$(dirname $$df) ; cp $$f $$df ; done
	cd $(@D) ; find . -type f -printf '%P\n' > Manifest
	( cd $(@D) ; PATH=$(ROOT)/bin:$(ROOT)/_bin:$$PATH $(ROOT)/$< ) > $@ || ( cat $@ ; rm $@ ; exit 1 )

%.dir/tok : %.py $(LMAKE_FILES) _lib/ut.py
	mkdir -p $(@D)
	( cd $(@D) ; git clean -ffdxq || : )                                       # keep $(@D) to ease debugging, ignore rc as old versions of git work but generate an error
	cp $< $(@D)/Lmakefile.py
	( cd $(@D) ; PATH=$(ROOT)/bin:$(ROOT)/_bin:$$PATH PYTHONPATH=$(ROOT)/lib:$(ROOT)/_lib HOME= $(PYTHON) Lmakefile.py ) > $@ || ( cat $@ ; rm $@ ; exit 1 )

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
$(LMAKE_ENV)/stamp : $(LMAKE_FILES) $(LMAKE_ENV)/Manifest $(patsubst %,$(LMAKE_ENV)/%,$(shell grep -e ^_bin/ -e ^_lib/ -e ^doc/ -e ^ext/ -e ^lib/ -e ^src/ -e ^sys_config\$$ Manifest))
	mkdir -p $(LMAKE_ENV)-cache
	touch $@
$(LMAKE_ENV)/tok : $(LMAKE_ENV)/stamp $(LMAKE_ENV)/Lmakefile.py
	set -e ; cd $(LMAKE_ENV) ; CC=$(CC) $(ROOT)/bin/lmake lmake.tar.gz & sleep 1 ; CC=$(CC) $(ROOT)/bin/lmake lmake.tar.gz >$(@F) ; wait $$! ; touch $(@F)
