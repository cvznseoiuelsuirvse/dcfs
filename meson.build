project('dcfs', 'c',
  meson_version: '>=1.1',
  default_options: [
    'warning_level=2',
    'buildtype=debugoptimized',
  ]
)

cc = meson.get_compiler('c')

deps = [
  dependency('libcurl'), 
  dependency('fuse3'), 
  dependency('zlib')
]

if build_machine.system() == 'darwin'
  deps += dependency('CoreFoundation')
endif


add_project_arguments('-DMAX_FILESIZE=' + get_option('max_filesize').to_string(), language: 'c')

src_files = files(
  'src/main.c',
  'src/request.c',
  'src/util.c',
)

lib_files = files(
  'lib/discord/discord.c',
  'lib/discord/api.c',
  'lib/json/json.c',
  'lib/json/reader.c',
)

inc_dirs = include_directories('lib', 'src')

executable('dcfs',
  src_files,
  lib_files,
  include_directories: inc_dirs,
  dependencies: deps,
  c_args: ['-Wno-unused-parameter'],
  install: true,
)
