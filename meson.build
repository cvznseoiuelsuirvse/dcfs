project('dcfs', 'c',
  default_options: [
    'warning_level=2',
    'buildtype=debugoptimized',
  ]
)

cc = meson.get_compiler('c')

libcurl_dep = dependency('libcurl')
fuse3_dep = dependency('fuse3')
zlib_dep = dependency('zlib')

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
  dependencies: [libcurl_dep, fuse3_dep, zlib_dep],
  install: true,
)
