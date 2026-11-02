project(
  'nixde',
  'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=3'],
)

add_project_arguments('-DWLR_USE_UNSTABLE', language: 'c')

wlroots = dependency('wlroots-0.20', version: '>=0.20.2', required: false)
if not wlroots.found()
  wlroots = dependency('wlroots', version: ['>=0.20.2', '<0.21'], required: true)
endif

wayland_server = dependency('wayland-server')
xkbcommon = dependency('xkbcommon')
gtk4 = dependency('gtk4')

add_project_arguments(
  '-DNIXDE_DATADIR="' + join_paths(get_option('prefix'), get_option('datadir'), 'nixde') + '"',
  language: 'c',
)

executable(
  'nixde',
  'src/nixde.c',
  dependencies: [wlroots, wayland_server, xkbcommon],
  install: true,
)

executable(
  'nixde-panel',
  [
    'src/panel.c',
    'src/panel/control.c',
    'src/panel/launcher.c',
    'src/panel/tasks.c',
    'src/panel/system.c',
    'src/panel/style.c',
  ],
  dependencies: [gtk4],
  install: true,
)

install_data(
  'data/nixde.desktop',
  install_dir: join_paths(get_option('datadir'), 'wayland-sessions'),
)

install_data(
  'data/nixde-panel.css',
  install_dir: join_paths(get_option('datadir'), 'nixde'),
  rename: 'panel.css',
)

install_data(
  'data/nixde-logo.svg',
  install_dir: join_paths(get_option('datadir'), 'nixde'),
)

install_data(
  'data/nixde-logo.svg',
  install_dir: join_paths(get_option('datadir'), 'icons/hicolor/scalable/apps'),
  rename: 'nixde.svg',
)
