executable('ibus-engine-sunpinyin',
  'debug.cpp',
  'engine.cpp',
  'ibus_portable.cpp',
  'imi_ibus_win.cpp',
  'main.cpp',
  'sunpinyin_config.cpp',
  'sunpinyin_conversion.cpp',
  'sunpinyin_engine.cpp',
  'sunpinyin_engine_proxy.cpp',
  'sunpinyin_lookup_table.cpp',
  'sunpinyin_property.cpp',
  dependencies: [ibus_dep, sunpinyin_dep, opencc_dep, intl_dep],
  install: true,
  install_dir: package_libexecdir)

conversion_test = executable('test-opencc-conversion',
  '../tests/opencc-conversion.cpp',
  'sunpinyin_conversion.cpp',
  include_directories: include_directories('.'),
  dependencies: [opencc_dep, dependency('glib-2.0')])
test('opencc-conversion', conversion_test)
