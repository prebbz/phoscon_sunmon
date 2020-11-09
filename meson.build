project('phoscon-sunmon', 'c')
# Project dependencies
deps = []
deps += dependency('gio-2.0')
deps += dependency('gio-unix-2.0')
deps += dependency('glib-2.0')
deps += dependency('jansson')
deps += dependency('libcurl')
deps += dependency('systemd')
extra_cflags = ['-W', '-Wformat=2', '-Wpointer-arith', '-Winline', \
                '-Wstrict-prototypes', '-Wmissing-prototypes', \
                '-Wdisabled-optimization', '-Wfloat-equal', '-Wall', \
                '-Wno-unused-parameter', '-Wno-pointer-arith', '-g']
include_dirs = include_directories('.')
# Project source files
main_sources = files([
      'main.c', 'phoscon_client.c', 'sun_client.c', 'util.c', 'cfg.c'
])
executable('phoscon-sunmon',
  sources: main_sources,
  dependencies : deps,
  c_args : extra_cflags,
  install_mode : [ 'rwxr-xr-x', 'nobody', 'nobody'],
  install : true)
