default:
    @just --list

# Create the development virtualenv (meson and ninja)
dev-setup:
    python3 -m venv .venv
    .venv/bin/pip install -q meson ninja

# Build natively into build/
build-native:
    .venv/bin/meson setup build --prefix="{{justfile_directory()}}/_native" --reconfigure 2>/dev/null || .venv/bin/meson setup build --prefix="{{justfile_directory()}}/_native"
    .venv/bin/meson compile -C build

# Install natively into _native/
install-native: build-native
    .venv/bin/meson install -C build

# Run the installed settings app
run-native:
    GSETTINGS_SCHEMA_DIR="{{justfile_directory()}}/_native/share/glib-2.0/schemas" "{{justfile_directory()}}/_native/bin/gnome-equalizer"

# Run the installed settings app in Brazilian Portuguese
run-native-pt:
    LANGUAGE=pt_BR GSETTINGS_SCHEMA_DIR="{{justfile_directory()}}/_native/share/glib-2.0/schemas" "{{justfile_directory()}}/_native/bin/gnome-equalizer"

# Run the audio filter in the foreground (stop the service first)
run-daemon: build-native
    GSETTINGS_SCHEMA_DIR="{{justfile_directory()}}/_native/share/glib-2.0/schemas" "{{justfile_directory()}}/build/daemon/gnome-equalizer-daemon"

# Run the test suite (DSP, desktop, metainfo and schema validation)
test: build-native
    .venv/bin/meson test -C build --print-errorlogs

# Regenerate the translation template and merge it into the catalogs
update-po: build-native
    .venv/bin/meson compile -C build gnome-equalizer-update-po

# Clean build directories
clean:
    rm -rf _build build _native _pack dist

# Install for the current user into ~/.local and start the filter
install-user:
    .venv/bin/meson setup _build --prefix="$HOME/.local" --reconfigure 2>/dev/null || .venv/bin/meson setup _build --prefix="$HOME/.local"
    .venv/bin/meson install -C _build
    systemctl --user daemon-reload
    systemctl --user restart gnome-equalizer.service
    gnome-extensions enable equalizer@cloudie-code.github.io || echo "Log out and back in, then run: gnome-extensions enable equalizer@cloudie-code.github.io"

# Remove the ~/.local installation
uninstall-user:
    -gnome-extensions disable equalizer@cloudie-code.github.io
    -systemctl --user stop gnome-equalizer.service
    ninja -C _build uninstall
    rm -rf "$HOME/.local/share/gnome-shell/extensions/equalizer@cloudie-code.github.io"
    systemctl --user daemon-reload

# Pack the GNOME Shell extension for extensions.gnome.org into dist/
pack-extension:
    rm -rf _pack dist/extension
    .venv/bin/meson setup _pack --prefix=/usr
    DESTDIR="{{justfile_directory()}}/dist/extension" .venv/bin/meson install -C _pack --quiet
    mkdir -p dist
    rm -f "dist/equalizer@cloudie-code.github.io.shell-extension.zip"
    cd "dist/extension/usr/share/gnome-shell/extensions/equalizer@cloudie-code.github.io" && rm -f schemas/gschemas.compiled && zip -qr "{{justfile_directory()}}/dist/equalizer@cloudie-code.github.io.shell-extension.zip" .
    rm -rf _pack dist/extension
    @echo "dist/equalizer@cloudie-code.github.io.shell-extension.zip"
