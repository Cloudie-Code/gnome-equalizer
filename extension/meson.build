extension_conf = configuration_data()
extension_conf.set('uuid', extension_uuid)
extension_conf.set('gettext_domain', gettext_package)
extension_conf.set('schema', application_id)

configure_file(
    input: 'metadata.json.in',
    output: 'metadata.json',
    configuration: extension_conf,
    install_dir: extensiondir,
)

install_data(
    'extension.js',
    'presets.js',
    'stylesheet.css',
    'verticalSlider.js',
    install_dir: extensiondir,
)

# The extension carries its own copy of the schema and the symbolic icon so
# it also works when installed on its own (e.g. from a zip).
install_data(
    '../data/io.github.cloudie_code.equalizer.gschema.xml',
    install_dir: extensiondir / 'schemas',
)
custom_target(
    'extension-gschemas',
    input: '../data/io.github.cloudie_code.equalizer.gschema.xml',
    output: 'gschemas.compiled',
    command: [
        find_program('glib-compile-schemas'),
        '--strict',
        '--targetdir=@OUTDIR@',
        meson.project_source_root() / 'data',
    ],
    install: true,
    install_dir: extensiondir / 'schemas',
)
install_data(
    '../data/icons/hicolor/symbolic/apps/io.github.cloudie_code.equalizer-symbolic.svg',
    install_dir: extensiondir / 'icons' / 'hicolor' / 'symbolic' / 'apps',
)
