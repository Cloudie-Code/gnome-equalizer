project(
    'gnome-equalizer',
    'c',
    version: '0.1.1',
    license: 'GPL-3.0-or-later',
    meson_version: '>= 1.0.0',
    default_options: [
        'c_std=gnu17',
        'warning_level=2',
        'werror=false',
    ],
)

i18n = import('i18n')
gnome = import('gnome')

application_id = 'io.github.cloudie_code.equalizer'
# extensions.gnome.org requires the schema under org.gnome.shell.extensions.
schema_id = 'org.gnome.shell.extensions.cloudie-equalizer'
extension_uuid = 'equalizer@cloudie-code.github.io'
gettext_package = meson.project_name()

prefix = get_option('prefix')
localedir = prefix / get_option('localedir')
pkgdatadir = prefix / get_option('datadir') / meson.project_name()
schemadir = get_option('datadir') / 'glib-2.0' / 'schemas'
extensiondir = get_option('datadir') / 'gnome-shell' / 'extensions' / extension_uuid

systemd_user_unit_dir = get_option('systemd-user-unit-dir')
if systemd_user_unit_dir == ''
    if prefix == '/usr'
        systemd_user_unit_dir = '/usr/lib/systemd/user'
    else
        # Searched by systemd as $XDG_DATA_HOME/systemd/user for ~/.local installs.
        systemd_user_unit_dir = get_option('datadir') / 'systemd' / 'user'
    endif
endif

dependency('gtk4', version: '>= 4.18.0')
dependency('libadwaita-1', version: '>= 1.7.0')

find_program('glib-compile-schemas', required: true)
find_program('gtk-update-icon-cache', required: false)
find_program('update-desktop-database', required: false)

subdir('daemon')
subdir('data')
subdir('src')
subdir('extension')
subdir('po')

gnome.post_install(
    glib_compile_schemas: true,
    gtk_update_icon_cache: true,
    update_desktop_database: true,
)
