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

SUBDIRS = . address_specific logging_related

dist_noinst_SCRIPTS = \
	filter_stderr


noinst_HEADERS = common.h

EXTRA_DIST = \
	const_store.stderr.exp const_store.stdout.exp const_store.vgtest \
	tmp_store.stderr.exp tmp_store.stdout.exp tmp_store.vgtest \
	state_machine.stderr.exp state_machine.stdout.exp state_machine.vgtest \
	state_machine2.stderr.exp state_machine2.stdout.exp \
		state_machine2.vgtest \
	multiple_stores.stderr.exp multiple_stores.stdout.exp \
		multiple_stores.vgtest \
	sse_stores.stderr.exp sse_stores.stdout.exp sse_stores.vgtest \
	cas.stderr.exp cas.stdout.exp cas.vgtest \
	flush_check.stderr.exp flush_check.stdout.exp flush_check.vgtest \
	state_no_flush_align.stderr.exp state_no_flush_align.stdout.exp \
		state_no_flush_align.vgtest

check_PROGRAMS = \
	const_store \
	tmp_store \
	state_machine \
	state_machine2 \
	multiple_stores \
	sse_stores \
	cas \
	flush_check \
	state_no_flush_align
