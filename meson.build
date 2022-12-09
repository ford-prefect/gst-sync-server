project('gst-sync-server', 'c',
  version: '0.0.1',
  meson_version : '>= 0.62',
  default_options : ['warning_level=1', 'buildtype=debugoptimized'])

sync_server_version = meson.project_version()
version_arr = sync_server_version.split('.')
version_major = version_arr[0].to_int()
version_minor = version_arr[1].to_int()
version_micro = version_arr[2].to_int()
if version_arr.length() == 4
  version_nano = version_arr[3]
else
  version_nano = 0
endif

# maintain compatibility with the previous libtool versioning
# libversion has 3 parts A.B.C
# A is the ABI version, change it if the ABI is broken, changing it resets B and C to 0. It matches soversion
# B is the ABI age, change it on new APIs that don't break existing ones, changing it resets C to 0
# C is the revision, change on new updates that don't change APIs
api_version = '1.0'
soversion = 0
# maintaining compatibility with the previous libtool versioning
# current = minor * 100 + micro
curversion = version_minor * 100 + version_micro
libversion = '@0@.@1@.0'.format(soversion, curversion)
osxversion = curversion + 1

static_build = get_option('default_library') == 'static'

# picking some old enough versions for now
glib_req = '>= 2.40'
json_glib_req = '>= 1.2.0'
gst_req = '>= 1.0.0'

cc = meson.get_compiler('c')
pkgconfig = import('pkgconfig')

syslibs = []

cdata = configuration_data()

cdata.set_quoted('PACKAGE_STRING', meson.project_name())
cdata.set_quoted('PACKAGE_NAME', meson.project_name())
cdata.set_quoted('PACKAGE', meson.project_name())
cdata.set_quoted('VERSION', meson.project_version())

configinc = include_directories('.')
libsinc = include_directories('gst-libs')

# GLib
glib_dep = dependency('glib-2.0', version: glib_req,
  fallback : ['glib', 'libglib_dep', ])
gobject_dep = dependency('gobject-2.0', version: glib_req,
  fallback : ['glib', 'libgobject_dep', ])
gio_dep = dependency('gio-2.0', version: glib_req,
  fallback : ['glib', 'libgio_dep', ])
json_glib_dep = dependency('json-glib-1.0', version: json_glib_req,
  fallback : ['json-glib', 'json_glib(dep', ])

# GStreamer
gst_dep = dependency('gstreamer-1.0', version: gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gst_net_dep = dependency('gstreamer-net-1.0', version: gst_req,
  fallback : ['gstreamer', 'gst_net_dep'])

subdir('gst-libs')
subdir('examples')

configure_file(output : 'config.h', configuration : cdata)
