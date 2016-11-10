lib_LTLIBRARIES = libgstsyncserver-@GST_API_VERSION@.la

libgstsyncserver_@GST_API_VERSION@_la_SOURCES = \
        sync-server.c                           \
        sync-client.c                           \
        sync-control-server.c                   \
        sync-control-tcp-server.c               \
        sync-control-tcp-client.c

libgstsyncserver_@GST_API_VERSION@_la_CFLAGS =  \
        -I$(top_srcdir)/gst-libs                \
        -I$(top_builddir)/gst-libs              \
        $(GLIB_CFLAGS)                          \
        $(JSON_GLIB_CFLAGS)                     \
        $(GST_CFLAGS)

libgstsyncserver_@GST_API_VERSION@_la_LIBADD =  \
        $(GLIB_LIBS)                            \
        $(JSON_GLIB_LIBS)                       \
        $(GST_LIBS)

libgstsyncserver_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/sync-server
libgstsyncserver_@GST_API_VERSION@include_HEADERS = \
        sync-server.h                           \
        sync-client.h                           \
        sync-control-server.h                   \
        sync-control-tcp-server.h               \
        sync-control-tcp-client.h
