AM_CPPFLAGS = -I$(top_srcdir)/include
libexec_PROGRAMS = tsdfx-copier
tsdfx_copier_SOURCES = copier.c
tsdfx_copier_LDADD = $(CRYPTO_LIBS) $(top_builddir)/lib/libtsd/libtsd.la
dist_man8_MANS = tsdfx-copier.8
