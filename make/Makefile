# Copyright 2014 (c) Suneido Software Corp. All rights reserved.
# Licensed under GPLv2.

#===============================================================================
# file: Makefile
# auth: Victor Schappert
# date: 20140814
# desc: GNU makefile for jSuneido debug library (jsdebug) project
#===============================================================================

CONFIG ?=release

#===============================================================================
# DIRECTORIES AND FILES
#===============================================================================

SRCDIR :=../src
OBJDIR :=obj/$(CONFIG)
BINDIR :=bin/$(CONFIG)

TARGET:=jsdebug.so
SOURCES  :=$(wildcard $(SRCDIR)/*.c)
OBJECTS  :=$(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

#===============================================================================
# FLAGS
#===============================================================================

CC:=gcc
CC_INCLUDE_DIRS:="-I$(JAVA_HOME)/include" "-I$(JAVA_HOME)/include/linux"
CC_INCLUDE_DIRS:=$(subst \,/,$(CC_INCLUDE_DIRS))
CC_FLAGS=-O$(OPTLEVEL) $(DEBUGOPTION) $(CC_STDFLAGS) -Wall -fPIC -flto \
         -fmessage-length=0 $(CC_INCLUDE_DIRS)

LD_FLAGS:=-shared -flto

#===============================================================================
# CONFIGURATION-SPECIFIC FLAGS
#===============================================================================

# Note that because conditionals are parsed immediately (in make's first pass),
# you can't make file-scope conditionals depend on target-specific variables.
# So to get different configurations, you have to set the CONFIG variable before
# make is invoked. For example, from the shell: '$ make CONFIG=release'. See
# section 3.7 of the GNU Make manual (entitled "How make Reads a Makefile").

ifeq ($(CONFIG),debug)
OPTLEVEL:=0
DEBUGOPTION:=-g -fstack-check
LD_FLAGS+= -g
else ifeq ($(CONFIG),release)
OPTLEVEL:=3
DEBUGOPTION:=-DNDEBUG
LD_FLAGS+= -s
else ifeq ($(shell echo $(CONFIG) | grep '^[0-3]g\?\(NDEBUG\)\?$$'),$(CONFIG))
OPTLEVEL:=$(shell echo $(CONFIG) | grep -o '^[0-3]')
FLAG_G:=$(shell echo $(CONFIG) | grep -o g)
FLAG_NDEBUG:=$(shell echo $(CONFIG) | grep -o NDEBUG)
ifeq ($(FLAG_G),g)
DEBUGOPTION:=-g -fstack-check
LD_FLAGS+= -g
else
LD_FLAGS+= -s
endif
ifeq ($(FLAG_NDEBUG),NDEBUG)
DEBUGOPTION+= -DNDEBUG
endif
else
$(error Unsupported CONFIG value: '$(CONFIG)')
endif

export CC CC_FLAGS

#===============================================================================
# TARGETS
#===============================================================================

.PHONY: build
build: so

.PHONY: so
so: dirs $(BINDIR)/$(TARGET)

$(BINDIR)/$(TARGET): $(OBJECTS)
	@echo LINKING $@
	@$(LD) -o $@ $(LD_FLAGS) $(OBJECTS)

$(OBJECTS): $(OBJDIR)/%.o : $(SRCDIR)/%.c
	@echo COMPILING $@
	$(CC) $(CC_FLAGS) -c $< -o $@

.PHONY: dirs
dirs: $(OBJDIR) $(BINDIR)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

.PHONY: clean
clean:
	@rm -f $(OBJDIR)/* $(BINDIR)/*
