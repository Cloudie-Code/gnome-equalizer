daemon_conf = configuration_data()
daemon_conf.set_quoted('GETTEXT_PACKAGE', gettext_package)
daemon_conf.set_quoted('LOCALEDIR', localedir)
configure_file(output: 'config.h', configuration: daemon_conf)

cc = meson.get_compiler('c')

executable(
    'gnome-equalizer-daemon',
    ['main.c', 'dsp.c'],
    dependencies: [
        dependency('gio-2.0', version: '>= 2.80'),
        dependency('gio-unix-2.0'),
        dependency('libpipewire-0.3', version: '>= 1.0.0'),
        cc.find_library('m'),
    ],
    c_args: ['-Wno-unused-parameter'],
    install: true,
    install_dir: get_option('libexecdir'),
)

test(
    'dsp',
    executable(
        'test-dsp',
        ['../tests/test-dsp.c', 'dsp.c'],
        include_directories: include_directories('.'),
        dependencies: cc.find_library('m'),
        c_args: ['-Wno-unused-parameter'],
    ),
)
