i18n.gettext(gettext_package, preset: 'glib')

# GNOME Shell only looks for an extension's catalogs inside the extension
# itself or in the system locale dir, so the extension gets its own copy.
meson.add_install_script(
    '../build-aux/install-extension-locale.sh',
    get_option('localedir'),
    extensiondir / 'locale',
    gettext_package,
)
