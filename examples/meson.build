examples = [
  'test-client',
  'test-server',
]

foreach ex : examples
  executable(ex, '@0@.c'.format(ex),
    include_directories: libsinc,
    dependencies: gstsyncserver_dep,
    install: false)
endforeach
