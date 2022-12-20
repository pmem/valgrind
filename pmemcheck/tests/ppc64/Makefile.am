include $(top_srcdir)/Makefile.tool-tests.am

SUBDIRS = .

dist_noinst_SCRIPTS = \
	filter_stderr

check_PROGRAMS = \
		 missed_flush \
		 flush_check

EXTRA_DIST = \
	     missed_flush.stderr.exp missed_flush.vgtest \
	     flush_check.stderr.exp flush_check.vgtest
