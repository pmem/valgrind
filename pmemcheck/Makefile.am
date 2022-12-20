# Persistent memory checker.
# Copyright (c) 2014-2015, Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms and conditions of the GNU General Public License,
# version 2, or (at your option) any later version, as published
# by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.

include $(top_srcdir)/Makefile.tool.am

EXTRA_DIST = docs/pmc-manual.xml

#----------------------------------------------------------------------------
# Headers
#----------------------------------------------------------------------------

pkginclude_HEADERS = pmemcheck.h

noinst_HEADERS = pmc_include.h

#----------------------------------------------------------------------------
# pmemcheck-<platform>
#----------------------------------------------------------------------------

noinst_PROGRAMS  = pmemcheck-@VGCONF_ARCH_PRI@-@VGCONF_OS@
if VGCONF_HAVE_PLATFORM_SEC
noinst_PROGRAMS += pmemcheck-@VGCONF_ARCH_SEC@-@VGCONF_OS@
endif

PMEMCHECK_SOURCES_COMMON = \
	pmc_main.c \
	pmc_tx.c \
	pmc_common.c

pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_SOURCES      = \
	$(PMEMCHECK_SOURCES_COMMON)
pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_PRI_CAPS@) -O2
pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_PRI_CAPS@)
pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_PRI_CAPS@)
pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_PRI@ \
	$(LINK) \
	$(pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_CFLAGS) \
	$(pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_LDFLAGS)

if VGCONF_HAVE_PLATFORM_SEC
pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_SOURCES      = \
	$(PMEMCHECK_SOURCES_COMMON)
pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS       = \
	$(AM_CFLAGS_@VGCONF_PLATFORM_SEC_CAPS@) -O2
pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_DEPENDENCIES = \
	$(TOOL_DEPENDENCIES_@VGCONF_PLATFORM_SEC_CAPS@)
pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDADD        = \
	$(TOOL_LDADD_@VGCONF_PLATFORM_SEC_CAPS@)
pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS      = \
	$(TOOL_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LINK = \
	$(top_builddir)/coregrind/link_tool_exe_@VGCONF_OS@ \
	@VALT_LOAD_ADDRESS_SEC@ \
	$(LINK) \
	$(pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_CFLAGS) \
	$(pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_LDFLAGS)
endif

# pmc_main.c contains the helper function for pmemcheck that get called
# all the time. To maximise performance compile with -fomit-frame-pointer
# Primary beneficiary is x86.
pmc_main.o: CFLAGS += -fomit-frame-pointer

#----------------------------------------------------------------------------
# vgpreload_pmemcheck-<platform>.so
#----------------------------------------------------------------------------
# vgpreload_pmemcheck-<platform>.so
#----------------------------------------------------------------------------

if VGCONF_ARCHS_INCLUDE_PPC64
noinst_PROGRAMS += vgpreload_pmemcheck-@VGCONF_ARCH_PRI@-@VGCONF_OS@.so
if VGCONF_HAVE_PLATFORM_SEC
noinst_PROGRAMS += vgpreload_pmemcheck-@VGCONF_ARCH_SEC@-@VGCONF_OS@.so
endif

# pmc_replace_memcmp.c runs on the simulated CPU, and is built with
# AM_CFLAGS_PSO_* (see $(top_srcdir)/Makefile.all.am).
VGPRELOAD_PMEMCHECK_SOURCES_COMMON = pmc_replace_memcmp.c

vgpreload_pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_so_SOURCES      = \
	$(VGPRELOAD_PMEMCHECK_SOURCES_COMMON)
vgpreload_pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_so_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)
vgpreload_pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_so_CFLAGS       = \
	$(AM_CFLAGS_PSO_@VGCONF_PLATFORM_PRI_CAPS@) -O2
vgpreload_pmemcheck_@VGCONF_ARCH_PRI@_@VGCONF_OS@_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_@VGCONF_PLATFORM_PRI_CAPS@)

if VGCONF_HAVE_PLATFORM_SEC
vgpreload_pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_so_SOURCES      = \
	$(VGPRELOAD_PMEMCHECK_SOURCES_COMMON)
vgpreload_pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_so_CPPFLAGS     = \
	$(AM_CPPFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
vgpreload_pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_so_CFLAGS       = \
	$(AM_CFLAGS_PSO_@VGCONF_PLATFORM_SEC_CAPS@) -O2
vgpreload_pmemcheck_@VGCONF_ARCH_SEC@_@VGCONF_OS@_so_LDFLAGS      = \
	$(PRELOAD_LDFLAGS_@VGCONF_PLATFORM_SEC_CAPS@)
endif
endif
