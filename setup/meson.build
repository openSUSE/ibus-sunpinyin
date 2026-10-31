configure_file(
  input: 'config.py.in',
  output: 'config.py',
  configuration: configuration,
  install_dir: package_datadir / 'setup')
configure_file(
  input: 'ibus-setup-sunpinyin.in',
  output: 'ibus-setup-sunpinyin',
  configuration: configuration,
  install_dir: package_libexecdir,
  install_mode: 'rwxr-xr-x')
install_data('main.py', 'setup.ui',
  install_dir: package_datadir / 'setup',
  install_mode: 'rw-r--r--')
