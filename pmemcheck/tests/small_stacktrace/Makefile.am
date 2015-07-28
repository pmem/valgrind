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

SUBDIRS = .

dist_noinst_SCRIPTS = \
	filter_stderr

EXTRA_DIST = \
	trans_mt.stderr.exp trans_mt.vgtest \
	trans_mt_cross.stderr.exp trans_mt_cross.vgtest

check_PROGRAMS = \
	trans_mt \
	trans_mt_cross

trans_mt_LDADD 	= -lpthread
trans_mt_cross_LDADD 	= -lpthread
