include $(top_srcdir)/Makefile.tool-tests.am

dist_noinst_SCRIPTS = \
		      filter_stderr

check_PROGRAMS = sse_stores \
		 isa_check\
		 store_split \
		 floats \
		 flush_check \
		 missed_flush \
		 nt_stores

EXTRA_DIST = sse_stores.stderr.exp sse_stores.vgtest \
	     isa_check.stderr.exp isa_check.vgtest \
	     flush_check.stderr.exp flush_check.vgtest \
	     missed_flush.stderr.exp missed_flush.vgtest \
	     nt_stores.stderr.exp nt_stores.vgtest \
	     store_split.stderr.exp store_split.vgtest \
	     floats.stderr.exp floats.vgtest
