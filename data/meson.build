desktop_conf = configuration_data()
desktop_conf.set('bindir', prefix / get_option('bindir'))
desktop_in = configure_file(
          input: 'io.github.cloudie_code.equalizer.desktop.in',
         output: 'io.github.cloudie_code.equalizer.desktop.in',
  configuration: desktop_conf,
)

desktop_file = i18n.merge_file(
        input: desktop_in,
       output: 'io.github.cloudie_code.equalizer.desktop',
         type: 'desktop',
       po_dir: '../po',
      install: true,
  install_dir: get_option('datadir') / 'applications'
)

desktop_utils = find_program('desktop-file-validate', required: false)
if desktop_utils.found()
  test('Validate desktop file', desktop_utils, args: [desktop_file])
endif

appstream_file = i18n.merge_file(
        input: 'io.github.cloudie_code.equalizer.metainfo.xml.in',
       output: 'io.github.cloudie_code.equalizer.metainfo.xml',
       po_dir: '../po',
      install: true,
  install_dir: get_option('datadir') / 'metainfo'
)

appstreamcli = find_program('appstreamcli', required: false, disabler: true)
test('Validate appstream file', appstreamcli,
     args: ['validate', '--no-net', '--explain', appstream_file])

install_data(schema_id + '.gschema.xml',
  install_dir: schemadir
)

compile_schemas = find_program('glib-compile-schemas', required: false, disabler: true)
test('Validate schema file',
     compile_schemas,
     args: ['--strict', '--dry-run', meson.current_source_dir()])

service_conf = configuration_data()
service_conf.set('libexecdir', prefix / get_option('libexecdir'))
configure_file(
  input: 'gnome-equalizer.service.in',
  output: 'gnome-equalizer.service',
  configuration: service_conf,
  install_dir: systemd_user_unit_dir,
)

# Start with PipeWire for every user, without a `systemctl --user enable`.
install_symlink(
  'gnome-equalizer.service',
  pointing_to: '../gnome-equalizer.service',
  install_dir: systemd_user_unit_dir / 'pipewire.service.wants',
)

subdir('icons')
