# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

include sys_config.mk

VERSION        := 25.12
TAG            := 0
# ubuntu20.04 (focal) is supported through the use of a g++-11 installation, but packages are not available on launchpad.net (because of debian packaging is not recent enough)
DEBIAN_RELEASE := 1
DISTROS        := jammy noble

ifneq ($(shell uname),Linux)
    $(error can only compile under Linux)
endif

# mandatory
MAKEFLAGS := -r -R                                                      # dont use default rules
# user configurable
MAKEFLAGS       += -k                                                   # keep making independent jobs in case of error
N_PARALLEL_JOBS := $(shell nproc||echo 1)
# mandatory
MAKEFLAGS += $(if $(findstring C,$(LMAKE_FLAGS)),,-j$(N_PARALLEL_JOBS)) # no parallel execution if using coverage

.DEFAULT_GOAL := DFLT

REPO_ROOT := $(abspath .)

.PHONY : FORCE
FORCE : ;

sys_config.env : FORCE
	@if [ ! -f $@ ] ; then                                           \
		echo new $@ ;                                                \
		{	echo PATH=\'$$(   echo "$$PATH"   |sed "s:':'\\'':")\' ; \
			echo CXX=\'$$(    echo "$$CXX"    |sed "s:':'\\'':")\' ; \
			echo GCOV=\'$$(   echo "$$GCOV"   |sed "s:':'\\'':")\' ; \
			echo PYTHON2=\'$$(echo "$$PYTHON2"|sed "s:':'\\'':")\' ; \
			echo PYTHON3=\'$$(echo "$$PYTHON" |sed "s:':'\\'':")\' ; \
			echo LMAKE_FLAGS=$$LMAKE_FLAGS                         ; \
		} >$@ ;                                                      \
	fi

sys_config.log : _bin/sys_config sys_config.env
	@echo sys_config
	@# reread sys_config.env in case it has been modified while reading an old sys_config.mk
	@set -a                                    ; \
	PATH=$$(env -i bash -c 'echo $$PATH')      ; \
	unset CXX GCOV PYTHON2 PYTHON3 LMAKE_FLAGS ; \
	. ./sys_config.env                         ; \
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
include Manifest.inc_stamp                                 # Manifest is used in this makefile
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
# - T       : -DTRACE
# - Sa      : -fsanitize address
# - St      : -fsanitize threads
# - P       : -pg
# - C       : coverage (not operational yet)
LTO_FLAGS        := -O3 $(if $(findstring gcc,$(CXX_FLAVOR) ) , -flto=2 , -flto )
EXTRA_LINK_FLAGS :=
EXTRA_LINK_FLAGS := $(if $(findstring O4,$(LMAKE_FLAGS) ) , $(LTO_FLAGS) , $(EXTRA_LINK_FLAGS)     )
EXTRA_LINK_FLAGS := $(if $(findstring O3,$(LMAKE_FLAGS) ) ,              , $(EXTRA_LINK_FLAGS)     )
EXTRA_LINK_FLAGS := $(if $(findstring O2,$(LMAKE_FLAGS) ) ,              , $(EXTRA_LINK_FLAGS)     )
EXTRA_LINK_FLAGS := $(if $(findstring O1,$(LMAKE_FLAGS) ) ,              , $(EXTRA_LINK_FLAGS)     )
EXTRA_LINK_FLAGS := $(if $(findstring O0,$(LMAKE_FLAGS) ) ,              , $(EXTRA_LINK_FLAGS)     )
EXTRA_LINK_FLAGS += $(if $(findstring P, $(LMAKE_FLAGS) ) , -pg                                    )
EXTRA_LINK_FLAGS += $(if $(findstring C, $(LMAKE_FLAGS) ) , --coverage                             )
EXTRA_CC_FLAGS   :=                                         -O3
EXTRA_CC_FLAGS   := $(if $(findstring P, $(LMAKE_FLAGS) ) , -O1          , $(EXTRA_CC_FLAGS)       )
EXTRA_CC_FLAGS   := $(if $(findstring C, $(LMAKE_FLAGS) ) , -O0          , $(EXTRA_CC_FLAGS)       )
EXTRA_CC_FLAGS   := $(if $(findstring O4,$(LMAKE_FLAGS) ) , $(LTO_FLAGS) , $(EXTRA_CC_FLAGS)       )
EXTRA_CC_FLAGS   := $(if $(findstring O3,$(LMAKE_FLAGS) ) , -O3          , $(EXTRA_CC_FLAGS)       )
EXTRA_CC_FLAGS   := $(if $(findstring O2,$(LMAKE_FLAGS) ) , -O2          , $(EXTRA_CC_FLAGS)       )
EXTRA_CC_FLAGS   := $(if $(findstring O1,$(LMAKE_FLAGS) ) , -O1          , $(EXTRA_CC_FLAGS)       )
EXTRA_CC_FLAGS   := $(if $(findstring O0,$(LMAKE_FLAGS) ) , -O0          , $(EXTRA_CC_FLAGS)       )
EXTRA_CC_FLAGS   += $(if $(findstring g, $(LMAKE_FLAGS) ) ,              , -g                      )
EXTRA_CC_FLAGS   += $(if $(findstring d, $(LMAKE_FLAGS) ) , -DNDEBUG                               )
EXTRA_CC_FLAGS   += $(if $(findstring T, $(LMAKE_FLAGS) ) , -DTRACE                                )
EXTRA_CC_FLAGS   += $(if $(findstring P, $(LMAKE_FLAGS) ) , -pg                                    )
EXTRA_CC_FLAGS   += $(if $(findstring C, $(LMAKE_FLAGS) ) , --coverage                             )
HIDDEN_CC_FLAGS  += $(if $(findstring g, $(LMAKE_FLAGS) ) ,              , -fno-omit-frame-pointer )
HIDDEN_CC_FLAGS  += $(if $(findstring P, $(LMAKE_FLAGS) ) , -DPROFILING                            )
HIDDEN_CC_FLAGS  += $(if $(findstring O0,$(LMAKE_FLAGS) ) , -fno-inline                            )
#
SAN_FLAGS := $(if $(findstring Sa,$(LMAKE_FLAGS)),-fsanitize=address -fsanitize=undefined)
SAN_FLAGS += $(if $(findstring St,$(LMAKE_FLAGS)),-fsanitize=thread                      )
# some user codes may have specific (and older) libs, in that case, unless flag l is used, link libstdc++ statically
LIB_STDCPP := $(if $(findstring l,$(LMAKE_FLAGS)),,-static-libstdc++)
#
WARNING_FLAGS := -Wall -Wextra -Wno-cast-function-type -Wno-type-limits -Werror
#
LINK_FLAGS           = $(if $(and $(HAS_32),$(findstring d$(LD_SO_LIB_32)/,$@)),$(LINK_LIB_PATH_32:%=-Wl$(COMMA)-rpath$(COMMA)%),$(LINK_LIB_PATH:%=-Wl$(COMMA)-rpath$(COMMA)%))
SAN                 := $(if $(strip $(SAN_FLAGS)),-san)
LINK                 = PATH=$(CXX_DIR):$$PATH $(CXX) -pthread $(LINK_FLAGS) $(EXTRA_LINK_FLAGS)
LINK_LIB             = -ldl $(if $(and $(HAS_32),$(findstring d$(LD_SO_LIB_32)/,$@)),$(LIB_STACKTRACE_32:%=-l%),$(LIB_STACKTRACE:%=-l%))
CLANG_WARNING_FLAGS := -Wno-misleading-indentation -Wno-unknown-warning-option -Wno-c2x-extensions -Wno-c++2b-extensions
#
ifeq ($(CXX_FLAVOR),clang)
    WARNING_FLAGS += $(CLANG_WARNING_FLAGS)
endif
#
USER_FLAGS := -std=$(CXX_STD) $(EXTRA_CC_FLAGS)
COMPILE1   := PATH=$(CXX_DIR):$$PATH $(CXX) $(USER_FLAGS) $(HIDDEN_CC_FLAGS) -pthread $(WARNING_FLAGS) $(if $(NEED_EXPERIMENTAL_LIBRARY),-fexperimental-library)
LINT       := clang-tidy
LINT_FLAGS := $(USER_FLAGS) $(HIDDEN_CC_FLAGS) $(WARNING_FLAGS) $(CLANG_WARNING_FLAGS)
LINT_CHKS  := --checks=-clang-analyzer-optin.core.EnumCastOutOfRange
LINT_OPTS  := '--header-filter=.*' $(LINT_CHKS)

# On ubuntu, seccomp.h is in /usr/include. On CenOS7, it is in /usr/include/linux, but beware that otherwise, /usr/include must be prefered, hence -idirafter
CC_FLAGS := -iquote ext -iquote src -iquote src/lmake_server -iquote . -idirafter /usr/include/linux

Z_LIB    := $(if $(HAS_ZSTD),-lzstd) $(if $(HAS_ZLIB),-lz)

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
SLURM_CC_FLAGS = $(if $(findstring src/lmake_server/backends/slurm_api-,$<),$(<:src/lmake_server/backends/slurm_api-%.cc=-I ext/slurm/%) -fno-lto)
#
PY_SO    = $(if $(PYTHON2) ,$(if $(findstring 2.so,             $@),-py2          ))
MOD_SO   = $(if $(HAS_32)  ,$(if $(findstring d$(LD_SO_LIB_32)/,$@),-m32          ))
MOD_O    = $(if $(HAS_32)  ,$(if $(findstring -m32,             $@),-m32          ))
PCRE_LIB = $(if $(HAS_PCRE),$(if $(findstring d$(LD_SO_LIB_32)/,$@),    ,-lpcre2-8))

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
	_lib/version.py                     \
	lib/lmake/__init__.py               \
	lib/lmake/auto_sources.py           \
	lib/lmake/config_.py                \
	lib/lmake/import_machinery.py       \
	lib/lmake/py_clmake.py              \
	lib/lmake/rules.py                  \
	lib/lmake/sources.py                \
	lib/lmake/utils.py                  \
	lib/lmake_debug/__init__.py         \
	lib/lmake_debug/default.py          \
	lib/lmake_debug/enter.py            \
	lib/lmake_debug/enter_job.py        \
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
	_bin/find_cc_ld_library_path \
	bin/lautodep                 \
	bin/lcollect                 \
	bin/ldebug                   \
	bin/lforget                  \
	bin/lmake                    \
	bin/lmark                    \
	bin/lmake_repair             \
	bin/ldir_cache_repair        \
	bin/lcache_server            \
	bin/lcache_repair            \
	bin/lmake_server             \
	bin/lrun_cc                  \
	bin/lshow                    \
	bin/xxhsum

MAN_FILES := $(patsubst doc/%.m,%,$(filter-out %/common.1.m,$(filter doc/man/man1/%.1.m,$(SRCS))))

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
	src/process.o \
	src/time.o    \
	src/trace.o   \
	src/utils.o

LMAKE_BASIC_SAN_OBJS := $(LMAKE_BASIC_OBJS:%.o=%$(SAN).o)

LMAKE_FILES        := $(LMAKE_SERVER_FILES) $(LMAKE_REMOTE_FILES)
LMAKE_BIN_FILES    := $(filter bin/%,$(LMAKE_FILES))
LMAKE_DBG_FILES    :=                                # this variable is progressively completed
LMAKE_DBG_FILES_32 :=                                # .

DOCKER_FILES := $(filter docker/%.docker,$(SRCS))

LMAKE_ALL_FILES     := $(LMAKE_FILES) $(LMAKE_DOC_FILES)
LMAKE_ALL_FILES_DBG := $(LMAKE_ALL_FILES) $(LMAKE_BIN_FILES)

LINT : $(patsubst %.cc,%.chk, $(filter-out %.x.cc,$(filter src/%.cc,$(SRCS))) )

align.tok : _bin/align_comments $(filter-out lmake_env/ext_lnk,$(SRCS))
	@echo check comments alignment
	@set -e ;                                                               \
	for f in $(SRCS) ; do                                                   \
		c= ;                                                                \
		case $$f in                                                         \
			museum/*    )                                         ;;        \
			ext/*       )                                         ;;        \
			debian/*    )                                         ;;        \
			.gitignore  )                                         ;;        \
			TO_DO       )                                         ;;        \
			LICENSE     )                                         ;;        \
			*.md        )                                         ;;        \
			doc/*       )                                         ;;        \
			docs/*      )                                         ;;        \
			docker/*    )                                         ;;        \
			*.dir/*     )                                         ;;        \
			lmake_env/* )                                         ;;        \
			*.c         ) c=//                                    ;;        \
			*.h         ) c=//                                    ;;        \
			*.cc        ) c=//                                    ;;        \
			*.hh        ) c=//                                    ;;        \
			*.py        ) c='#'                                   ;;        \
			Makefile    ) c='#'                                   ;;        \
			_bin/*      ) c='#'                                   ;;        \
			unit_tests/*)                                         ;;        \
			*           ) echo unrecognized file $$f >&2 ; exit 1 ;;        \
		esac ;                                                              \
		if [ "$$c" ] && ! align_comments 4 200 $$c <$$f | diff $$f - ; then \
			echo bad comments alignment for $$f >&2 ;                       \
			exit 1 ;                                                        \
		fi ;                                                                \
	done ;                                                                  \
	>$@

ALIGN : align.tok

DFLT_TGT : LMAKE_TGT UNIT_TESTS LMAKE_TEST lmake.tar.gz
DFLT     : DFLT_TGT.SUMMARY

ALL_TGT : DFLT_TGT LINT STORE_TEST ALIGN
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

VERSION_FILES := $(patsubst %,%.v,$(filter-out src/version.cc,$(filter src/%.cc,$(SRCS))) $(filter src/%.hh,$(SRCS)))

# use a stamp to implement a by value update (as make works by date)
%.v.stamp : %
	@sed -n '/\/\/.*START_OF_VERSIONING/,/\/\/.*END_OF_VERSIONING/p' $< >$@
	@# dont touch output if it is steady
	@if cmp -s $@ $(@:%.stamp=%) ; then                        echo steady version info $(@:%.stamp=%) ; \
	else                                cp $@ $(@:%.stamp=%) ; echo new    version info $(@:%.stamp=%) ; \
	fi
%.v : %.v.stamp ;

version.src : $(VERSION_FILES)
	@echo collate version info to $@
	@for f in $^ ; do ! [ -s $$f ] || { echo $${f%.v} : ; cat $$f ; } ; done >$@

version.checked : FORCE
	@[ -e $@ ] || { >$@ ; }

# use a stamp to implement a by value update (while make works by date)
src/version.cc.stamp : _bin/version version.src src/version.hh version.checked
	@echo computing versions to $(@:%.stamp=%)
	@PYTHON=$(PYTHON) VERSION=$(VERSION) TAG=$(TAG) ./$< gen src/version.hh $(@:%.stamp=%) version.src >$@
	@# dont touch output if it is steady
	@if cmp -s $@ $(@:%.stamp=%) ; then                        echo steady version info $(@:%.stamp=%) ; \
	else                                cp $@ $(@:%.stamp=%) ; echo new    version info $(@:%.stamp=%) ; \
	fi
src/version.cc : src/version.cc.stamp ;

_lib/version.py : _bin/version src/version.hh src/version.cc
	@echo convert version to py to $@
	@PYTHON=$(PYTHON) HAS_LD_AUDIT=$(HAS_LD_AUDIT) ./$< cc_to_py src/version.hh src/version.cc >$@

#
# LMAKE
#

# add system configuration to lmake.py :
# Sense git bin dir at install time so as to be independent of it at run time.
# Some python installations require LD_LIBRARY_PATH. Handle this at install time so as to be independent at run time.
lib/%.py _lib/%.py : _lib/%.src.py sys_config.mk _bin/align_comments
	@echo customize $< to $@
	@mkdir -p $(@D)
	@	sed \
			-e 's!\$$BASH!$(BASH)!'                          \
			-e 's!\$$GIT!$(GIT)!'                            \
			-e 's!\$$HAS_LD_AUDIT!$(HAS_LD_AUDIT)!'          \
			-e 's!\$$LD_LIBRARY_PATH!$(PY_LD_LIBRARY_PATH)!' \
			-e 's!\$$PYTHON2!$(PYTHON2)!'                    \
			-e 's!\$$PYTHON!$(PYTHON)!'                      \
			-e 's!\$$STD_PATH!$(STD_PATH)!'                  \
			-e 's!\$$TAG!$(TAG)!'                            \
			-e 's!\$$VERSION!$(VERSION)!'                    \
			$<                                               \
	|	_bin/align_comments 4 200 '#' >$@
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
	src/version$(SAN).o     \
	src/store/unit_test$(SAN).o
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(LINK_LIB)

src/store/unit_test.dir/tok : src/store/unit_test
	@echo store unit test to $@
	@rm -rf   $(@D)
	@mkdir -p $(@D)
	@./$<     $(@D)
	@touch    $@

src/store/big_test.dir/tok : src/store/big_test.py LMAKE
	@echo big test "(2000000)" to $@
	@mkdir -p $(@D)
	@rm -rf   $(@D)/LMAKE
	@PATH=$$PWD/_bin:$$PWD/bin:$$PATH ; ( cd $(@D) ; $(PYTHON) ../big_test.py / 2000000 )
	@touch $@

#
# compilation
#

%.i     : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -E                                      -o $@ $<
%-m32.i : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -E               -m32 -DFORCE_32_BITS=1 -o $@ $<
%-py2.i : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -E                                      -o $@ $<
%-san.i : %.cc ; @echo $(CXX) $(USER_FLAGS) $(SAN_FLAGS) to $@ ; $(COMPILE) -E               $(SAN_FLAGS)           -o $@ $<

%.s     : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -S -fverbose-asm                        -o $@ $<
%-m32.s : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -S -fverbose-asm -m32 -DFORCE_32_BITS=1 -o $@ $<
%-py2.s : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE) -S -fverbose-asm                        -o $@ $<
%-san.s : %.cc ; @echo $(CXX) $(USER_FLAGS) $(SAN_FLAGS) to $@ ; $(COMPILE) -S -fverbose-asm $(SAN_FLAGS)           -o $@ $<

COMPILE_O = $(COMPILE) -c -frtti -fPIC
%.o     : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE_O)                                       -o $@ $<
%-m32.o : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE_O)                -m32 -DFORCE_32_BITS=1 -o $@ $<
%-py2.o : %.cc ; @echo $(CXX) $(USER_FLAGS)              to $@ ; $(COMPILE_O)                                       -o $@ $<
%-san.o : %.cc ; @echo $(CXX) $(USER_FLAGS) $(SAN_FLAGS) to $@ ; $(COMPILE_O)                $(SAN_FLAGS)           -o $@ $<

%.chk   : %.cc
	@echo $(LINT) $(USER_FLAGS) to $@
	@$(LINT) $< $(LINT_OPTS) -- $(LINT_FLAGS) $(PY_CC_FLAGS) $(CC_FLAGS) >$@.err 2>$@.stderr
	@if [ -s $@.err ] ; then echo errors in $@.err ; cat $@.stderr ; else >$@ ; fi

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

SLURM_SRCS := $(patsubst ext/slurm/%/slurm/slurm.h,src/lmake_server/backends/slurm_api-%.cc,$(filter ext/slurm/%/slurm/slurm.h,$(SRCS)))
CACHE_SRCS := $(patsubst %.cc,%-light.cc,$(filter src/caches/%.cc,$(SRCS)))
#
DEP_SRCS   := $(filter-out %.x.cc,$(filter src/%.cc,$(SRCS))) $(SLURM_SRCS) $(CACHE_SRCS)
include $(if $(findstring 1,$(SYS_CONFIG_OK)) , $(patsubst %.cc,%.d, $(DEP_SRCS) ) )

#
# slurm
#

src/lmake_server/backends/slurm_api-%.cc : ext/slurm/%/META
	@echo generate $@
	@# mimic slurm source code to retrieve API version from META, in practice, API_AGE is 0
	@{	awk '/API_CURRENT/ {api_current=$$2} ; /API_AGE/ {api_age=$$2} ; END {printf("#define SLURM_API_VERSION_NUMBER %d\n",api_current-api_age)}' $< ; \
		echo '#include "slurm_api.x.cc"'                                                                                                               ; \
	} >$@

#
# cache
#

src/caches/%-light.cc :
	@echo generate $@
	@{ echo '#define CACHE_LIGHT 1' ; echo '#include "$(@:src/caches/%-light.cc=%.cc)"' ; } >$@

#
# lmake
#

# on CentOS7, gcc looks for libseccomp.so with -lseccomp, but only libseccomp.so.2 exists, and this works everywhere.
SECCOMP_LIB := $(if $(HAS_SECCOMP),-l:libseccomp.so.2)

SERVER_COMMON_SAN_OBJS := \
	$(LMAKE_BASIC_SAN_OBJS)         \
	src/app$(SAN).o                 \
	src/real_path$(SAN).o           \
	src/py$(SAN).o                  \
	src/re$(SAN).o                  \
	src/rpc_job$(SAN).o             \
	src/rpc_job_exec$(SAN).o        \
	src/version$(SAN).o             \
	src/autodep/env$(SAN).o         \
	src/caches/daemon_cache$(SAN).o \
	src/caches/dir_cache$(SAN).o

BACKEND_SAN_OBJS := \
	src/lmake_server/backends/local$(SAN).o  \
	src/lmake_server/backends/slurm$(SAN).o  \
	$(patsubst %.cc,%$(SAN).o,$(SLURM_SRCS)) \
	src/lmake_server/backends/sge$(SAN).o

CLIENT_SAN_OBJS := \
	$(LMAKE_BASIC_SAN_OBJS) \
	src/app$(SAN).o         \
	src/client$(SAN).o      \
	src/rpc_client$(SAN).o  \
	src/version$(SAN).o

SERVER_SAN_OBJS := \
	$(SERVER_COMMON_SAN_OBJS)          \
	src/non_portable$(SAN).o           \
	src/rpc_client$(SAN).o             \
	src/autodep/backdoor$(SAN).o       \
	src/autodep/gather$(SAN).o         \
	src/autodep/ld_server$(SAN).o      \
	src/autodep/ptrace$(SAN).o         \
	src/autodep/record$(SAN).o         \
	src/autodep/syscall_tab$(SAN).o    \
	src/lmake_server/backend$(SAN).o   \
	src/lmake_server/cmd$(SAN).o       \
	src/lmake_server/global$(SAN).o    \
	src/lmake_server/config$(SAN).o    \
	src/lmake_server/job$(SAN).o       \
	src/lmake_server/job_data$(SAN).o  \
	src/lmake_server/makefiles$(SAN).o \
	src/lmake_server/node$(SAN).o      \
	src/lmake_server/req$(SAN).o       \
	src/lmake_server/rule$(SAN).o      \
	src/lmake_server/rule_data$(SAN).o \
	src/lmake_server/store$(SAN).o

bin/lmake_server : \
	$(SERVER_SAN_OBJS)  \
	$(BACKEND_SAN_OBJS) \
	src/lmake_server/main$(SAN).o

bin/lmake_repair : $(SERVER_SAN_OBJS) $(BACKEND_SAN_OBJS) src/lmake_repair$(SAN).o # lmake_repair must be aware of existing backends
_bin/ldump       : $(SERVER_SAN_OBJS)                     src/ldump$(SAN).o
_bin/lkpi        : $(SERVER_SAN_OBJS)                     src/lkpi$(SAN).o

LMAKE_DBG_FILES += bin/lmake_server bin/lmake_repair _bin/ldump _bin/lkpi
bin/lmake_server bin/lmake_repair _bin/ldump _bin/lkpi :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(SECCOMP_LIB) $(Z_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

bin/lcache_server : \
	$(LMAKE_BASIC_SAN_OBJS)                            \
	src/app$(SAN).o                                    \
	src/py$(SAN).o                                     \
	src/re$(SAN).o                                     \
	src/real_path$(SAN).o                              \
	src/rpc_job$(SAN).o                                \
	src/version$(SAN).o                                \
	src/autodep/env$(SAN).o                            \
	src/caches/daemon_cache$(SAN).o                    \
	src/caches/dir_cache$(SAN).o                       \
	src/caches/daemon_cache/engine$(SAN).o             \
	src/caches/daemon_cache/daemon_cache_utils$(SAN).o \
	src/caches/daemon_cache/ldaemon_cache_server$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(Z_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

bin/lcache_repair : \
	$(LMAKE_BASIC_SAN_OBJS)                            \
	src/app$(SAN).o                                    \
	src/py$(SAN).o                                     \
	src/re$(SAN).o                                     \
	src/real_path$(SAN).o                              \
	src/rpc_job$(SAN).o                                \
	src/version.o                                      \
	src/autodep/env$(SAN).o                            \
	src/caches/daemon_cache$(SAN).o                    \
	src/caches/dir_cache$(SAN).o                       \
	src/caches/daemon_cache/engine$(SAN).o             \
	src/caches/daemon_cache/daemon_cache_utils$(SAN).o \
	src/caches/daemon_cache/ldaemon_cache_repair$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(Z_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

bin/ldir_cache_repair : \
	$(SERVER_COMMON_SAN_OBJS) \
	src/version.o             \
	src/ldir_cache_repair$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(Z_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

bin/lcollect : $(CLIENT_SAN_OBJS) src/lcollect$(SAN).o
bin/ldebug   : $(CLIENT_SAN_OBJS) src/ldebug$(SAN).o src/py$(SAN).o
bin/lforget  : $(CLIENT_SAN_OBJS) src/lforget$(SAN).o
bin/lmake    : $(CLIENT_SAN_OBJS) src/lmake$(SAN).o
bin/lmark    : $(CLIENT_SAN_OBJS) src/lmark$(SAN).o
bin/lshow    : $(CLIENT_SAN_OBJS) src/lshow$(SAN).o

LMAKE_DBG_FILES += bin/lcollect bin/lforget bin/lmake bin/lmark bin/lshow
bin/lcollect bin/lforget bin/lmake bin/lmark bin/lshow :
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
_bin/ldump_job : $(SERVER_COMMON_SAN_OBJS) src/ldump_job$(SAN).o
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(Z_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

LMAKE_DBG_FILES += _bin/align_comments
_bin/align_comments : \
	$(LMAKE_BASIC_SAN_OBJS) \
	src/app$(SAN).o         \
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
	src/version.o       \
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
	src/re.o               \
	src/rpc_job_exec.o     \
	src/autodep/backdoor.o \
	src/autodep/env.o      \
	src/autodep/record.o

AUTODEP_OBJS := \
	$(BASIC_REMOTE_OBJS) \
	src/real_path.o      \
	src/version.o        \
	src/autodep/syscall_tab.o
AUTODEP_SAN_OBJS := $(AUTODEP_OBJS:%.o=%$(SAN).o)

REMOTE_OBJS  := \
	$(BASIC_REMOTE_OBJS) \
	src/app.o            \
	src/real_path.o      \
	src/version.o

# XXX! : make job_exec compatible with SAN
#JOB_EXEC_SAN_OBJS := \
#	$(AUTODEP_SAN_OBJS)             \
#	src/app$(SAN).o                 \
#	src/non_portable$(SAN).o        \
#	src/re$(SAN).o                  \
#	src/rpc_job$(SAN).o             \
#	src/autodep/gather$(SAN).o      \
#	src/autodep/ptrace$(SAN).o      \
#	src/autodep/record$(SAN).o      \
#	src/caches/daemon_cache$(SAN).o \
#	src/caches/dir_cache$(SAN).o

#_bin/job_exec : $(JOB_EXEC_SAN_OBJS)                src/job_exec$(SAN).o
#bin/lautodep  : $(JOB_EXEC_SAN_OBJS) src/py$(SAN).o src/autodep/lautodep$(SAN).o

#LMAKE_DBG_FILES += _bin/job_exec bin/lautodep
#_bin/job_exec bin/lautodep :
#	@mkdir -p $(@D)
#	@echo link to $@
#	@$(LINK) $(SAN_FLAGS) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(SECCOMP_LIB) $(Z_LIB) $(LINK_LIB)
#	@$(SPLIT_DBG_CMD)

JOB_EXEC_OBJS := \
	$(AUTODEP_OBJS)                 \
	src/app.o                       \
	src/non_portable.o              \
	src/re.o                        \
	src/rpc_job.o                   \
	src/autodep/gather.o            \
	src/autodep/ptrace.o            \
	src/autodep/record.o            \
	src/caches/daemon_cache-light.o \
	src/caches/dir_cache-light.o

_bin/job_exec : $(JOB_EXEC_OBJS)          src/job_exec.o
bin/lautodep  : $(JOB_EXEC_OBJS) src/py.o src/autodep/lautodep.o

LMAKE_DBG_FILES += _bin/job_exec bin/lautodep
_bin/job_exec bin/lautodep :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(SECCOMP_LIB) $(Z_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

LMAKE_DBG_FILES += bin/ldecode bin/ldepend bin/lencode bin/ltarget bin/lcheck_deps
bin/ldecode     : $(REMOTE_OBJS) src/autodep/job_support.o src/py.o src/autodep/ldecode.o
bin/ldepend     : $(REMOTE_OBJS) src/autodep/job_support.o src/py.o src/autodep/ldepend.o
bin/lencode     : $(REMOTE_OBJS) src/autodep/job_support.o src/py.o src/autodep/lencode.o
bin/ltarget     : $(REMOTE_OBJS) src/autodep/job_support.o src/py.o src/autodep/ltarget.o
bin/lcheck_deps : $(REMOTE_OBJS) src/autodep/job_support.o src/py.o src/autodep/lcheck_deps.o

bin/% :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) $(LIB_STDCPP) -o $@ $^ $(PY_LINK_FLAGS) $(PCRE_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

# remote libs generate errors when -fsanitize=thread # XXX! fix these errors and use $(SAN)

LMAKE_DBG_FILES += lib/clmake.so $(if $(HAS_PY2_DYN),lib/clmake2.so)
lib/clmake.so lib/clmake2.so : SO_FLAGS = $(PY_LINK_FLAGS)
lib/clmake.so                : $(REMOTE_OBJS) src/py.o     src/autodep/job_support.o     src/autodep/clmake.o
lib/clmake2.so               : $(REMOTE_OBJS) src/py-py2.o src/autodep/job_support-py2.o src/autodep/clmake-py2.o

lib/%.so :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) -shared $(LIB_STDCPP) $(MOD_SO) -o $@ $^ $(SO_FLAGS) $(PCRE_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

LMAKE_DBG_FILES    += $(if $(HAS_LD_AUDIT),_d$(LD_SO_LIB)/ld_audit.so   ) _d$(LD_SO_LIB)/ld_preload.so    _d$(LD_SO_LIB)/ld_preload_jemalloc.so
LMAKE_DBG_FILES_32 += $(if $(HAS_LD_AUDIT),_d$(LD_SO_LIB_32)/ld_audit.so) _d$(LD_SO_LIB_32)/ld_preload.so _d$(LD_SO_LIB_32)/ld_preload_jemalloc.so
_d$(LD_SO_LIB)/ld_audit.so               : $(AUTODEP_OBJS)             src/autodep/ld_audit.o
_d$(LD_SO_LIB)/ld_preload.so             : $(AUTODEP_OBJS)             src/autodep/ld_preload.o
_d$(LD_SO_LIB)/ld_preload_jemalloc.so    : $(AUTODEP_OBJS)             src/autodep/ld_preload_jemalloc.o
_d$(LD_SO_LIB_32)/ld_audit.so            : $(AUTODEP_OBJS:%.o=%-m32.o) src/autodep/ld_audit-m32.o
_d$(LD_SO_LIB_32)/ld_preload.so          : $(AUTODEP_OBJS:%.o=%-m32.o) src/autodep/ld_preload-m32.o
_d$(LD_SO_LIB_32)/ld_preload_jemalloc.so : $(AUTODEP_OBJS:%.o=%-m32.o) src/autodep/ld_preload_jemalloc-m32.o

%.so :
	@mkdir -p $(@D)
	@echo link to $@
	@$(LINK) -shared $(LIB_STDCPP) $(MOD_SO) -o $@ $^ $(SO_FLAGS) $(PCRE_LIB) $(LINK_LIB)
	@$(SPLIT_DBG_CMD)

#
# Unit tests
#

UNIT_TESTS : \
	$(patsubst %.py,%.dir/tok,     $(filter unit_tests/%.py,    $(SRCS))) \
	$(patsubst %.dir/run,%.dir/tok,$(filter examples/%.dir/run ,$(SRCS)))

TEST_ENV = \
	export PATH=$(REPO_ROOT)/bin:$(REPO_ROOT)/_bin:$$PATH                                          ; \
	export PYTHONPATH=$(REPO_ROOT)/lib:$(REPO_ROOT)/_lib:$(REPO_ROOT)/unit_tests/base:$$PYTHONPATH ; \
	export CXX=$(CXX)                                                                              ; \
	export LD_LIBRARY_PATH=$(PY3_LIB_DIR)                                                          ; \
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
		trial=$(@:%.dir/tok=%.trial)                                                      ; \
		mkdir -p $$trial ; rm -rf $$trial/*                                               ; \
		ln -s $$PWD/$(@:%.dir/tok=%.dir/skipped) $(@:%.dir/tok=%.trial/skipped)           ; \
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
lmake_env/stamp : lmake_env/Manifest $(patsubst %,lmake_env/%,$(LMAKE_SRCS))
	@touch $@
	@echo init lmake_env
lmake_env/tok : $(LMAKE_ALL_FILES) lmake_env/stamp lmake_env/Lmakefile.py
	@cd lmake_env ;                                       \
	$(REPO_ROOT)/bin/lmake --version >/dev/null 2>&1 || { \
		echo reset lmake_env book-keeping ;               \
		rm -rf LMAKE ../lmake_env-cache   ;               \
	}
	@[ -f lmake_env-cache/LMAKE/size ] || {                    \
		echo init lmake_env-cache                            ; \
		mkdir -p lmake_env-cache/LMAKE                       ; \
		echo 'size=100<<20' >lmake_env-cache/LMAKE/config.py ; \
	}
	@set -e ; cd lmake_env                            ; \
	export CXX=$(CXX)                                 ; \
	$(REPO_ROOT)/bin/lmake lmake.tar.gz -Vn & sleep 1 ; \
	$(REPO_ROOT)/bin/lmake lmake.tar.gz >$(@F).tmp    ; \
	wait $$!                                          ; \
	rm -rf LMAKE.bck                                  ; \
	sleep 2 ; : ensure lmake_server has gone          ; \
	$(REPO_ROOT)/bin/lmake_repair >>$(@F).tmp         ; \
	$(REPO_ROOT)/bin/lmake lmake.tar.gz >$(@F).tmp2   ; \
	grep -q 'was already up to date' $(@F).tmp2 && {    \
		cat $(@F).tmp2 >$(@F).tmp ;                     \
		rm $(@F).tmp2             ;                     \
	} ;                                                 \
	mv $(@F).tmp $(@F)

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

DOC_PY := lib/lmake/config_.py lib/lmake/sources.py lib/lmake/rules.py $(filter unit_tests/%.py,$(SRCS)) $(filter examples/%.dir/Lmakefile.py,$(SRCS))

_bin/mdbook :
	#wget -O- https://github.com/rust-lang/mdBook/releases/download/v0.4.44/mdbook-v0.4.44-x86_64-unknown-linux-gnu.tar.gz  | tar -xz -C_bin # requires recent libraries >=ubuntu22.04
	wget  -O- https://github.com/rust-lang/mdBook/releases/download/v0.4.44/mdbook-v0.4.44-x86_64-unknown-linux-musl.tar.gz | tar -xz -C_bin # static libc

man/man1/%.1 : doc/man/man1/%.1.m doc/man/utils.mh doc/man/man1/common.1.m
	@echo generate man to $@
	@mkdir -p $(@D)
	@m4 doc/man/utils.mh doc/man/man1/common.1.m $< >$@.tmp      # ensure m4 errors are visible (so cannot directly use | sed)
	@sed -e 's:^[\t ]*::' -e 's:-:\\-:g' -e '/^$$/ d' $@.tmp >$@
	@rm $@.tmp

docs/%.html : %.py
	@echo syntax highlight to $@
	@mkdir -p $(@D)
	@pygmentize -f html -o $@ -lpy -O style=default,full,tabsize=4 $<

docs/man/man1/%.html : man/man1/%.1
	@echo format man to $@
	@mkdir -p $(@D)
	@mkdir groff.tmp.$$$$ ; ( cd groff.tmp.$$$$ ; groff -Thtml -man ../$< >../$@ ) ; rm -rf groff.tmp.$$$$

docs/index.html : _bin/mdbook doc/book.toml $(filter doc/src/%.md,$(SRCS))
	echo generate book to $@
	mkdir -p $(@D)
	rm -rf doc/book
	cd doc ; ../_bin/mdbook build
	cd doc/book ; for f in * ; do rm -rf ../../docs/$$f ; done ; mv * ../../docs

# html doc is under git as _bin/mdbook cannot be downloaded in launchpad.net
# also this makes the html doc directly available on github
docs.manifest_stamp : docs/index.html $(MAN_FILES:%.1=docs/%.html) $(DOC_PY:%.py=docs/%.html)
	echo collate
	git ls-files docs                                    | sort > docs.ref_manifest
	find docs -type f '-(' -name '.*.swp' -o -print '-)' | sort > docs.actual_manifest
	diff docs.ref_manifest docs.actual_manifest
	>$@

BOOK : docs.manifest_stamp

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
	@echo LMAKE_FLAGS=gl > $(DEBIAN_DIR)/sys_config.env
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

#
# coverage
#

GCOV : gcov/summary

gcov/summary : FORCE
	GCOV=$(GCOV) $(PYTHON) _bin/gen_gcov.py $@ $(DEP_SRCS)
