AM_CPPFLAGS = -I$(top_srcdir)/include
libexec_PROGRAMS = tsdfx-scanner
tsdfx_scanner_SOURCES = scanner.c
tsdfx_scanner_LDADD = $(CRYPTO_LIBS) $(top_builddir)/lib/libtsd/libtsd.la
dist_man8_MANS = tsdfx-scanner.8
