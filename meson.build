project(
  'write-it',
  'cpp',
  version: '0.1.0',
  license: 'Unlicense',
  meson_version: '>=0.56',
  default_options: [
    'cpp_std=c++17',
    'warning_level=2',
    'prefix=/usr',
  ],
)

gtkmm = dependency('gtkmm-3.0')
glibmm = dependency('glibmm-2.4')

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('APP_ID', 'org.gmgauthier.WriteIt')
conf.set_quoted('DATADIR', get_option('prefix') / get_option('datadir') / 'write-it')
conf.set_quoted('SOURCE_ROOT', meson.project_source_root())

configure_file(
  output: 'config.hpp',
  configuration: conf,
)

inc = include_directories('.', 'src')

executable(
  'write-it',
  'src/main.cpp',
  'src/application.cpp',
  'src/main_window.cpp',
  'src/editor.cpp',
  'src/about_dialog.cpp',
  'src/settings.cpp',
  'src/document.cpp',
  'src/rtf.cpp',
  'src/units.cpp',
  include_directories: inc,
  dependencies: [gtkmm],
  install: true,
  install_dir: get_option('bindir'),
)

install_data(
  'data/write-it.desktop',
  install_dir: get_option('datadir') / 'applications',
)

install_data(
  'data/write-it.css',
  install_dir: get_option('datadir') / 'write-it',
)

write_it_settings_tests = executable(
  'write-it-settings-tests',
  'tests/test_settings.cpp',
  'src/settings.cpp',
  'src/units.cpp',
  include_directories: inc,
  dependencies: [glibmm],
)
test('settings', write_it_settings_tests)

write_it_document_tests = executable(
  'write-it-document-tests',
  'tests/test_document.cpp',
  'src/document.cpp',
  'src/rtf.cpp',
  include_directories: inc,
)
test('document', write_it_document_tests)

write_it_indents_tests = executable(
  'write-it-indents-tests',
  'tests/test_indents.cpp',
  'src/document.cpp',
  'src/rtf.cpp',
  'src/units.cpp',
  include_directories: inc,
)
test('indents', write_it_indents_tests)

write_it_units_tests = executable(
  'write-it-units-tests',
  'tests/test_units.cpp',
  'src/units.cpp',
  include_directories: inc,
)
test('units', write_it_units_tests)

write_it_alignment_tests = executable(
  'write-it-alignment-tests',
  'tests/test_alignment.cpp',
  'src/document.cpp',
  'src/rtf.cpp',
  include_directories: inc,
)
test('alignment', write_it_alignment_tests)
