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
]

if build_machine.system() == 'darwin'
  deps += dependency('CoreFoundation')
endif


add_project_arguments('-DMAX_FILESIZE=' + get_option('max_filesize').to_string(), language: 'c')

src_files = files(
  'src/dcfs.c',
  'src/fs.c',
  'src/request.c',
  'src/util.c',
  'src/discord/discord.c',
  'src/json/json.c',
  'src/json/reader.c',
)

inc_dirs = include_directories('src')

executable('dcfs',
  src_files,
  include_directories: inc_dirs,
  dependencies: deps,
  c_args: ['-Wno-unused-parameter'],
  install: true,
)
