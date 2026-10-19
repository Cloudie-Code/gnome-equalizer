moduledir = pkgdatadir / 'gnome_equalizer'

python = import('python')

conf = configuration_data()
conf.set('PYTHON', python.find_installation('python3').full_path())
conf.set('VERSION', meson.project_version())
conf.set('localedir', localedir)
conf.set('pkgdatadir', pkgdatadir)

configure_file(
    input: 'gnome-equalizer.in',
    output: 'gnome-equalizer',
    configuration: conf,
    install: true,
    install_dir: get_option('bindir'),
    install_mode: 'rwxr-xr-x',
)

install_data(
    [
        '__init__.py',
        'bands.py',
        'main.py',
        'output.py',
        'presets.py',
        'style.css',
        'window.py',
    ],
    install_dir: moduledir,
)
