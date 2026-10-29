{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  gettext,
  glib,
  desktop-file-utils,
  appstream,
  wrapGAppsHook4,
  gobject-introspection,
  python3,
  pipewire,
  gtk4,
  libadwaita,
  pulseaudio,
}:

let
  python = python3.withPackages (ps: [ ps.pygobject3 ]);
in
stdenv.mkDerivation (finalAttrs: {
  pname = "gnome-equalizer";
  version = "0.1.3";

  src = lib.cleanSource ../..;

  nativeBuildInputs = [
    python
    meson
    ninja
    pkg-config
    gettext
    glib
    desktop-file-utils
    appstream
    wrapGAppsHook4
    gobject-introspection
  ];

  buildInputs = [
    python
    glib
    pipewire
    gtk4
    libadwaita
  ];

  # NixOS looks for user units in lib/systemd/user (systemd.packages).
  mesonFlags = [ "-Dsystemd-user-unit-dir=lib/systemd/user" ];

  doCheck = true;

  preFixup = ''
    gappsWrapperArgs+=(--prefix PATH : ${lib.makeBinPath [ pulseaudio ]})
  '';

  passthru.extensionUuid = "equalizer@cloudie-code.github.io";

  meta = {
    description = "Six band equalizer with loudness equalization and bass boost for GNOME";
    homepage = "https://github.com/cloudie-code/gnome-equalizer";
    license = lib.licenses.gpl3Plus;
    platforms = lib.platforms.linux;
    mainProgram = "gnome-equalizer";
  };
})
