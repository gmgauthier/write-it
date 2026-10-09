project(
  'write-it',
  'cpp',
  version: '0.1.0',
  license: 'Unlicense',
  meson_version: '>=0.57',
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

# Every app source except main.cpp, compiled once. The app and every test
# link this library; no target lists src/*.cpp files of its own.
#
# A new file in src/ goes in this list and nowhere else. Do not add src/*.cpp
# to a test target: link writeit_dep instead, so a source added by one PR can
# never leave another target with an undefined reference.
writeit_core_sources = files(
  'src/about_dialog.cpp',
  'src/application.cpp',
  'src/document.cpp',
  'src/editor.cpp',
  'src/filename.cpp',
  'src/font_sizes.cpp',
  'src/main_window.cpp',
  'src/narrow_combo.cpp',
  'src/open_plan.cpp',
  'src/page_text.cpp',
  'src/rtf.cpp',
  'src/settings.cpp',
  'src/units.cpp',
  'src/view.cpp',
)

writeit_core = static_library(
  'writeit_core',
  writeit_core_sources,
  include_directories: inc,
  dependencies: [gtkmm, glibmm],
)

writeit_dep = declare_dependency(
  link_with: writeit_core,
  include_directories: inc,
  dependencies: [gtkmm, glibmm],
)

write_it_exe = executable(
  'write-it',
  'src/main.cpp',
  dependencies: [writeit_dep],
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

write_it_open_tests = executable(
  'write-it-open-tests',
  'tests/test_open.cpp',
  dependencies: [writeit_dep],
)
test('open', write_it_open_tests)

write_it_filename_tests = executable(
  'write-it-filename-tests',
  'tests/test_filename.cpp',
  dependencies: [writeit_dep],
)
test('filename', write_it_filename_tests)

write_it_settings_tests = executable(
  'write-it-settings-tests',
  'tests/test_settings.cpp',
  dependencies: [writeit_dep],
)
test('settings', write_it_settings_tests)

# Opening a .txt or .md in the real window, under a display: unmodified until
# edited, and Save still goes through Save As.
write_it_import_clean_tests = executable(
  'write-it-import-clean-tests',
  'tests/test_import_clean.cpp',
  dependencies: [writeit_dep],
)
test('import-clean', write_it_import_clean_tests, timeout: 60)

# check.hpp's guard. A suite that runs one check too few or too many, or
# names no count, fails; done() without a count does not compile.
check_guard = executable('check-guard', 'tests/check_guard.cpp', include_directories: inc)
test('guard-too-few-fails', check_guard, args: ['early'], should_fail: true)
test('guard-too-many-fails', check_guard, args: ['too-many'], should_fail: true)
test('guard-no-count-fails', check_guard, args: ['no-count'], should_fail: true)
test('guard-exact-passes', check_guard, args: ['exact'])
# The header's text goes into the probe itself, so editing check.hpp
# reconfigures and the probe is never answered from meson's cache.
done_without_count = meson.get_compiler('cpp').compiles(
  import('fs').read('tests/check.hpp') + '\nint main() { return suite_test::done("x"); }\n',
  args: ['-std=c++17'],
  name: 'suite_test::done() without a count',
)
# The control: with a count it compiles, so the NO above is about the
# missing count and nothing else.
assert(
  meson.get_compiler('cpp').compiles(
    import('fs').read('tests/check.hpp') + '\nint main() { return suite_test::done("x", 1); }\n',
    args: ['-std=c++17'],
    name: 'suite_test::done() with a count',
  ),
  'check.hpp does not compile with a count either',
)
test(
  'guard-count-required',
  find_program(done_without_count ? 'false' : 'true'),
)

write_it_view_tests = executable(
  'write-it-view-tests',
  'tests/test_view.cpp',
  dependencies: [writeit_dep],
)
test('view', write_it_view_tests)

write_it_font_sizes_tests = executable(
  'write-it-font-sizes-tests',
  'tests/test_font_sizes.cpp',
  dependencies: [writeit_dep],
)
test('font-sizes', write_it_font_sizes_tests)

write_it_document_tests = executable(
  'write-it-document-tests',
  'tests/test_document.cpp',
  dependencies: [writeit_dep],
)
test('document', write_it_document_tests)

write_it_heading_paragraph_tests = executable(
  'write-it-heading-paragraph-tests',
  'tests/test_heading_paragraphs.cpp',
  dependencies: [writeit_dep],
)
test('heading-paragraphs', write_it_heading_paragraph_tests)

write_it_indents_tests = executable(
  'write-it-indents-tests',
  'tests/test_indents.cpp',
  dependencies: [writeit_dep],
)
test('indents', write_it_indents_tests)

write_it_alignment_tests = executable(
  'write-it-alignment-tests',
  'tests/test_alignment.cpp',
  dependencies: [writeit_dep],
)
test('alignment', write_it_alignment_tests)

# Justify in the real window, under a display: the toolbar button, the menu
# item and GTK's JUSTIFY_FILL.
write_it_justify_ui_tests = executable(
  'write-it-justify-ui-tests',
  'tests/test_justify_ui.cpp',
  dependencies: [writeit_dep],
)
test('justify-ui', write_it_justify_ui_tests, timeout: 60)

write_it_lists_tests = executable(
  'write-it-lists-tests',
  'tests/test_lists.cpp',
  dependencies: [writeit_dep],
)
test('lists', write_it_lists_tests)

write_it_units_tests = executable(
  'write-it-units-tests',
  'tests/test_units.cpp',
  dependencies: [writeit_dep],
)
test('units', write_it_units_tests)

write_it_rtf_unicode_tests = executable(
  'write-it-rtf-unicode-tests',
  'tests/test_rtf_unicode.cpp',
  dependencies: [writeit_dep, dependency('glib-2.0')],
)
# The 200,000-round fuzz, apart so rtf-unicode's own checks (below) stay quick. About 20 s
# under ASan and UBSan, against meson's 30 s default, hence its own timeout.
# `meson test --no-suite slow` leaves it out locally; CI runs everything.
test(
  'rtf-unicode-fuzz',
  write_it_rtf_unicode_tests,
  args: ['fuzz'],
  timeout: 120,
  suite: 'slow',
)
test('rtf-unicode', write_it_rtf_unicode_tests)

write_it_page_count_tests = executable(
  'write-it-page-count-tests',
  'tests/test_page_count.cpp',
  dependencies: [writeit_dep],
)
test('page-count', write_it_page_count_tests)

# The real window under a display: the status bar's page cell and zoom cell.
write_it_status_bar_tests = executable(
  'write-it-status-bar-tests',
  'tests/test_status_bar.cpp',
  dependencies: [writeit_dep],
)
test('status-bar', write_it_status_bar_tests, timeout: 120)

# The real window under a display: View > Zoom and View > Page / Draft, and
# the width GTK allocates and wraps at after each change.
write_it_page_width_tests = executable(
  'write-it-page-width-tests',
  'tests/test_page_width.cpp',
  dependencies: [writeit_dep],
)
test('page-width', write_it_page_width_tests, timeout: 120)

# The real window under a display: every way the caret moves (Ctrl+End,
# arrows, typing, a placed caret) in Page, Draft and 200%, and whether the
# pasteboard scrolls to keep it in view.
write_it_caret_scroll_tests = executable(
  'write-it-caret-scroll-tests',
  'tests/test_caret_scroll.cpp',
  dependencies: [writeit_dep],
)
test('caret-scroll', write_it_caret_scroll_tests, timeout: 120)

# The real window under a display: it fits a 1024 px screen.
write_it_window_width_tests = executable(
  'write-it-window-width-tests',
  'tests/test_window_width.cpp',
  dependencies: [writeit_dep],
)
test('window-width', write_it_window_width_tests, timeout: 60)

# The real window under a display: a closed window leaves nothing on the
# clipboard, which outlives it (run under ASan to see a use after free).
write_it_window_lifetime_tests = executable(
  'write-it-window-lifetime-tests',
  'tests/test_window_lifetime.cpp',
  dependencies: [writeit_dep],
)
test('window-lifetime', write_it_window_lifetime_tests, timeout: 60)

# The real window under a display: what overflows the toolbars at 960 px and
# at the narrowest reaches the overflow menus with its label, and does there
# what its button or box does.
write_it_toolbar_overflow_tests = executable(
  'write-it-toolbar-overflow-tests',
  'tests/test_toolbar_overflow.cpp',
  dependencies: [writeit_dep],
)
test('toolbar-overflow', write_it_toolbar_overflow_tests, timeout: 120)

# The real window under a display: a list label wider than its hang moves a
# left-aligned or justified item's text to the next default tab stop.
write_it_list_tabs_tests = executable(
  'write-it-list-tabs-tests',
  'tests/test_list_tabs.cpp',
  dependencies: [writeit_dep],
)
test('list-tabs', write_it_list_tabs_tests, timeout: 120)

# The real application under a display: a file already open, asked for by a
# symlink, a hard link or a ".." path, from Application::open or File > Open.
write_it_open_identity_tests = executable(
  'write-it-open-identity-tests',
  'tests/test_open_identity.cpp',
  dependencies: [writeit_dep],
)
test('open-identity', write_it_open_identity_tests, timeout: 60)

# Launches the program with files, on its own Xvfb and private D-Bus; see the script.
test(
  'smoke-open',
  find_program('tests/smoke_open.sh'),
  args: [write_it_exe.full_path()],
  depends: write_it_exe,
  timeout: 180,
  is_parallel: false,
)

test(
  'desktop-file',
  find_program('tests/desktop_file.sh'),
  args: [
    meson.current_source_dir() / 'data' / 'write-it.desktop',
    meson.current_source_dir() / 'src' / 'editor.cpp',
  ],
)
