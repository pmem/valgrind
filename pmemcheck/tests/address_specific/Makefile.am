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
	region_manipulation.stderr.exp region_manipulation.vgtest \
	remove_regions.stderr.exp remove_regions.vgtest \
	remove_aligned_region.stderr.exp remove_aligned_region.vgtest \
	flush_align.stderr.exp flush_align.vgtest \
	region_coalescing.stderr.exp region_coalescing.vgtest

check_PROGRAMS = \
	region_manipulation \
	remove_regions \
	remove_aligned_region \
	flush_align \
	region_coalescing
