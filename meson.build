project('ibus-sunpinyin', 'cpp',
  version: '2.0.3',
  meson_version: '>=0.59.0',
  license: ['LGPL-2.1-only', 'CDDL-1.0'],
  default_options: ['cpp_std=c++17', 'warning_level=1'])

ibus_dep = dependency('ibus-1.0', version: '>=1.2', method: 'pkg-config')
sunpinyin_dep = dependency('sunpinyin-2.0', version: '>=2.0', method: 'pkg-config')
opencc_dep = dependency('opencc', version: '>=1.0', method: 'pkg-config')
intl_dep = dependency('intl')
python = import('python').find_installation('python3')

package_datadir = get_option('datadir') / meson.project_name()
package_libexecdir = get_option('libexecdir') / meson.project_name()
prefix = get_option('prefix')
icon_dir = prefix / package_datadir / 'icons'
locale_dir = prefix / get_option('localedir')

configuration = configuration_data()
configuration.set('INSTALL_PREFIX', prefix)
configuration.set('DATADIR', prefix / get_option('datadir'))
configuration.set('LOCALEDIR', locale_dir)
configuration.set('DATA_DIR', prefix / package_datadir)
configuration.set('LIBEXEC_DIR', prefix / package_libexecdir)
configuration.set('ICON_DIR', icon_dir)
configuration.set('VERSION', meson.project_version())
configuration.set('PYTHON', python.full_path())

add_project_arguments(
  '-DGETTEXT_PACKAGE="' + meson.project_name() + '"',
  '-DIBUS_SUNPINYIN_LOCALEDIR="' + locale_dir + '"',
  '-DIBUS_SUNPINYIN_ICON_DIR="' + icon_dir + '"',
  '-DLIBEXECDIR="' + prefix / package_libexecdir + '"',
  language: 'cpp')

subdir('src')
subdir('setup')
subdir('data')
subdir('icons')
subdir('po')
