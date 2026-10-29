%global appid io.github.cloudie_code.equalizer
%global uuid equalizer@cloudie-code.github.io
%global schema org.gnome.shell.extensions.cloudie-equalizer
Name:           gnome-equalizer
Version:        0.1.4
Release:        1%{?dist}
Summary:        Six band equalizer with loudness equalization and bass boost for GNOME
License:        GPL-3.0-or-later
URL:            https://github.com/cloudie-code/gnome-equalizer
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz
BuildRequires:  gcc
BuildRequires:  meson >= 1.0.0
BuildRequires:  ninja-build
BuildRequires:  gettext
BuildRequires:  desktop-file-utils
BuildRequires:  appstream
BuildRequires:  python3
BuildRequires:  systemd-rpm-macros
BuildRequires:  pkgconfig(glib-2.0) >= 2.80
BuildRequires:  pkgconfig(libpipewire-0.3) >= 1.0.0
BuildRequires:  pkgconfig(gtk4) >= 4.18.0
BuildRequires:  pkgconfig(libadwaita-1) >= 1.7.0
Requires:       python3
Requires:       python3-gobject
Requires:       gtk4 >= 4.18.0
Requires:       libadwaita >= 1.7.0
Requires:       pipewire >= 1.0.0
Requires:       pipewire-pulseaudio
Requires:       wireplumber >= 0.5
Requires:       pulseaudio-utils
Recommends:     gnome-shell

%description
Equalizer shapes the sound of whatever output device you pick in GNOME,
and follows you when you switch from the speakers to headphones: a six band
equalizer with presets, loudness equalization and bass boost, with an
equalizer button next to the volume slider in the system menu.

The audio filter runs as a PipeWire smart filter in the gnome-equalizer.service
systemd user unit, and the GNOME Shell extension %{uuid} adds the popup.

%prep
%autosetup

%build
%meson -Dsystemd-user-unit-dir=%{_userunitdir}
%meson_build

%install
%meson_install
%find_lang %{name}
# The extension's own copy of the catalogs is owned by its directory below.
sed -i '/gnome-shell\/extensions/d' %{name}.lang

%check
%meson_test

%files -f %{name}.lang
%license COPYING
%doc README.md
%{_bindir}/%{name}
%{_libexecdir}/%{name}-daemon
%{_datadir}/%{name}/
%{_datadir}/applications/%{appid}.desktop
%{_datadir}/metainfo/%{appid}.metainfo.xml
%{_datadir}/glib-2.0/schemas/%{schema}.gschema.xml
%{_datadir}/icons/hicolor/scalable/apps/%{appid}.svg
%{_datadir}/icons/hicolor/symbolic/apps/%{appid}-symbolic.svg
%{_datadir}/gnome-shell/extensions/%{uuid}/
%{_userunitdir}/%{name}.service
%{_userunitdir}/pipewire.service.wants/%{name}.service

%changelog
* Thu Oct 29 2026 cloudie-code <344364031+Cloudie-Code@users.noreply.github.com> - 0.1.4-1
- Bound upward gain by perceived loudness to avoid over-amplifying bass-free audio

* Thu Oct 29 2026 cloudie-code <344364031+Cloudie-Code@users.noreply.github.com> - 0.1.3-1
- Raise quiet audio towards the peak ceiling without lowering loud passages

* Fri Oct 23 2026 cloudie-code <344364031+Cloudie-Code@users.noreply.github.com> - 0.1.2-1
- Stabilize loudness equalization and prevent bass-driven volume pumping

* Wed Oct 21 2026 cloudie-code <344364031+Cloudie-Code@users.noreply.github.com> - 0.1.1-1
- Follow the extensions.gnome.org review guidelines

* Mon Oct 19 2026 cloudie-code <344364031+Cloudie-Code@users.noreply.github.com> - 0.1.0-1
- Initial package
