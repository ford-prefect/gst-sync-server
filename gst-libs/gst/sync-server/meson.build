gst_sync_server_sources = files([
  'sync-client.c',
  'sync-control-client.c',
  'sync-control-server.c',
  'sync-control-tcp-client.c',
  'sync-control-tcp-server.c',
  'sync-server.c',
  'sync-server-info.c',
])

gst_sync_server_headers = files([
  'sync-client.h',
  'sync-control-client.h',
  'sync-control-server.h',
  'sync-control-tcp-client.h',
  'sync-control-tcp-server.h',
  'sync-server.h',
  'sync-server-info.h',
])

gst_sync_server_dependencies = [
  glib_dep,
  gio_dep,
  gobject_dep,
  gst_dep,
  gst_net_dep,
  json_glib_dep,
]

gstsyncserver = library('gstsyncserver-' + api_version,
  gst_sync_server_sources,
  include_directories : [configinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : gst_sync_server_dependencies,
)

gstsyncserver_dep = declare_dependency(link_with: gstsyncserver,
  include_directories : libsinc,
  dependencies: gst_sync_server_dependencies)

install_headers(gst_sync_server_headers, subdir : 'gstreamer-1.0/gst/sync-server')

pkgconfig_variables = ['exec_prefix=${prefix}',
    'toolsdir=${exec_prefix}/bin',
    'pluginsdir=${libdir}/gstreamer-1.0',
    'datarootdir=${prefix}/share',
    'girdir=${datadir}/gir-1.0',
    'typelibdir=${libdir}/girepository-1.0']

pkgconfig_subdirs = ['gstreamer-1.0']

library_def = {'lib': gstsyncserver}
pkg_name = 'gstreamer-sync-server-1.0'
pkgconfig.generate(gstsyncserver,
  libraries : [glib_dep, gio_dep, gobject_dep, gst_dep, gst_net_dep],
  variables : pkgconfig_variables,
  subdirs : pkgconfig_subdirs,
  name : pkg_name,
  description : 'GStreamer Sync Server',
)
