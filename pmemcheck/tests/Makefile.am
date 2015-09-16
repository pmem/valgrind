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

include $(top_srcdir)/Makefile.tool-tests.am

SUBDIRS = . address_specific logging_related small_stacktrace

dist_noinst_SCRIPTS = \
	filter_stderr


noinst_HEADERS = common.h

EXTRA_DIST = \
	const_store.stderr.exp const_store.vgtest \
	tmp_store.stderr.exp tmp_store.vgtest \
	state_machine.stderr.exp state_machine.vgtest \
	state_machine2.stderr.exp state_machine2.vgtest \
	multiple_stores.stderr.exp multiple_stores.vgtest \
	sse_stores.stderr.exp sse_stores.vgtest \
	cas.stderr.exp cas.vgtest \
	flush_check.stderr.exp flush_check.vgtest \
	isa_check.stderr.exp isa_check.vgtest \
	state_no_flush_align.stderr.exp state_no_flush_align.vgtest \
	nt_stores.stderr.exp nt_stores.vgtest \
	set_clean.stderr.exp set_clean.vgtest \
	missed_flush.stderr.exp missed_flush.vgtest\
	trans_impl.stderr.exp trans_impl.vgtest \
	trans_expl.stderr.exp trans_expl.vgtest \
	trans_nest.stderr.exp trans_nest.vgtest \
	trans_impl_nest.stderr.exp trans_impl_nest.vgtest \
	trans_no_pmem.stderr.exp trans_no_pmem.vgtest \
	trans_excl.stderr.exp trans_excl.vgtest \
	trans_only.stderr.exp trans_only.vgtest \
	trans_cache_overl.stderr.exp trans_cache_overl.vgtest \
	trans_cache_flush.stderr.exp trans_cache_flush.vgtest

check_PROGRAMS = \
	const_store \
	tmp_store \
	state_machine \
	state_machine2 \
	multiple_stores \
	sse_stores \
	cas \
	flush_check \
	isa_check\
	state_no_flush_align \
	nt_stores \
	set_clean \
	missed_flush \
	trans_impl \
	trans_expl \
	trans_nest \
	trans_impl_nest \
	trans_no_pmem \
	trans_excl \
	trans_only \
	trans_cache_overl \
	trans_cache_flush
