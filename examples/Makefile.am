noinst_PROGRAMS = test-server

AM_CFLAGS = -I$(top_srcdir)/gst-libs/gst/sync-server
LDADD = $(top_builddir)/gst-libs/gst/sync-server/libgstsyncserver-@GST_API_VERSION@.la

test_server_CFLAGS =    \
        $(AM_CFLAGS)    \
        $(GST_CFLAGS)   \
        $(GLIB_CFLAGS)

test_server_LDADD =     \
        $(LDADD)        \
        $(GST_LIBS)     \
        $(GLIB_LIBS)
