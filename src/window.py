# window.py
#
# Copyright 2026 cloudie-code
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later

from gettext import gettext as _

from gi.repository import Adw, Gio, GLib, GObject, Gtk

from .bands import EqualizerBands
from .output import OutputMonitor, start_service
from .presets import CUSTOM, presets


class EqualizerWindow(Adw.ApplicationWindow):
    """Laid out like the Sound panel of GNOME Settings."""

    def __init__(self, application: Adw.Application, settings: Gio.Settings):
        super().__init__(
            application=application,
            title=_("Equalizer"),
            default_width=720,
            default_height=760,
            width_request=360,
            height_request=294,
        )
        self._settings = settings
        self._presets = presets()
        self._syncing_preset = False

        self._monitor = OutputMonitor()
        self.connect("close-request", lambda *_: self._monitor.stop() or False)

        toolbar = Adw.ToolbarView()
        header = Adw.HeaderBar()
        menu = Gio.Menu()
        menu.append(_("_About Equalizer"), "app.about")
        header.pack_end(Gtk.MenuButton(icon_name="open-menu-symbolic", menu_model=menu,
                                       primary=True, tooltip_text=_("Main Menu")))
        toolbar.add_top_bar(header)

        self._banner = Adw.Banner(
            title=_("The equalizer service is not running"),
            button_label=_("_Start"),
        )
        self._banner.connect("button-clicked", self._on_start_service)
        self._monitor.bind_property(
            "filter-running", self._banner, "revealed",
            GObject.BindingFlags.SYNC_CREATE | GObject.BindingFlags.INVERT_BOOLEAN,
        )
        toolbar.add_top_bar(self._banner)

        page = Adw.PreferencesPage()
        page.add(self._build_output_group())
        page.add(self._build_equalizer_group())
        page.add(self._build_enhancements_group())
        toolbar.set_content(page)
        self.set_content(toolbar)

    def _build_output_group(self) -> Adw.PreferencesGroup:
        group = Adw.PreferencesGroup(title=_("Output"))

        device_row = Adw.ActionRow(title=_("Output Device"))
        device_box = Gtk.Box(spacing=6, valign=Gtk.Align.CENTER)
        icon = Gtk.Image()
        self._monitor.bind_property("icon-name", icon, "icon-name",
                                    GObject.BindingFlags.SYNC_CREATE)
        label = Gtk.Label(ellipsize=3, css_classes=["dim-label"])
        self._monitor.bind_property("description", label, "label",
                                    GObject.BindingFlags.SYNC_CREATE)
        device_box.append(icon)
        device_box.append(label)
        device_row.add_suffix(device_box)
        group.add(device_row)

        enabled_row = Adw.SwitchRow(
            title=_("Equalizer"),
            subtitle=_("Applies to the output device chosen in the system menu"),
        )
        self._settings.bind("enabled", enabled_row, "active", Gio.SettingsBindFlags.DEFAULT)
        group.add(enabled_row)
        return group

    def _build_equalizer_group(self) -> Adw.PreferencesGroup:
        group = Adw.PreferencesGroup(title=_("Equalizer"))

        reset = Gtk.Button(label=_("_Reset"), use_underline=True, valign=Gtk.Align.CENTER,
                           css_classes=["flat"])
        reset.connect("clicked", lambda *_: self._apply_preset(0))
        group.set_header_suffix(reset)

        names = [name for _id, name, _gains in self._presets] + [_("Custom")]
        self._preset_row = Adw.ComboRow(title=_("Preset"), model=Gtk.StringList.new(names))
        self._preset_row.connect("notify::selected", self._on_preset_selected)
        group.add(self._preset_row)

        bands_row = Gtk.ListBoxRow(activatable=False, selectable=False)
        bands = EqualizerBands(self._settings)
        self._settings.bind("enabled", bands, "sensitive", Gio.SettingsBindFlags.GET)
        bands_row.set_child(bands)
        group.add(bands_row)

        self._settings.connect("changed::preset", lambda *_: self._sync_preset())
        self._sync_preset()
        return group

    def _build_enhancements_group(self) -> Adw.PreferencesGroup:
        group = Adw.PreferencesGroup(title=_("Sound Enhancements"))

        loudness = Adw.SwitchRow(
            title=_("Loudness Equalization"),
            subtitle=_("Evens out the volume, so quiet passages are raised and loud ones lowered"),
        )
        self._settings.bind("loudness-equalization", loudness, "active",
                            Gio.SettingsBindFlags.DEFAULT)
        group.add(loudness)

        bass = Adw.ExpanderRow(
            title=_("Bass Boost"),
            subtitle=_("Boosts the lowest frequencies"),
            show_enable_switch=True,
        )
        self._settings.bind("bass-boost", bass, "enable-expansion", Gio.SettingsBindFlags.DEFAULT)

        level_row = Adw.ActionRow(title=_("Level"))
        adjustment = Gtk.Adjustment(lower=1, upper=12, step_increment=1, page_increment=3)
        scale = Gtk.Scale(adjustment=adjustment, digits=0, draw_value=False, hexpand=True,
                          valign=Gtk.Align.CENTER, width_request=200)
        scale.update_property([Gtk.AccessibleProperty.LABEL], [_("Level")])
        for mark in (1, 6, 12):
            scale.add_mark(mark, Gtk.PositionType.BOTTOM, None)
        value = Gtk.Label(width_chars=6, xalign=1, css_classes=["numeric"])
        adjustment.connect("value-changed", lambda adj: adj.set_value(round(adj.get_value())))
        adjustment.connect(
            "value-changed",
            lambda adj: value.set_label(_("+%d dB") % round(adj.get_value())),
        )
        self._settings.bind("bass-boost-level", adjustment, "value", Gio.SettingsBindFlags.DEFAULT)
        value.set_label(_("+%d dB") % round(adjustment.get_value()))
        level_row.add_suffix(scale)
        level_row.add_suffix(value)
        bass.add_row(level_row)
        group.add(bass)
        return group

    def _sync_preset(self) -> None:
        current = self._settings.get_string("preset")
        ids = [preset_id for preset_id, _name, _gains in self._presets]
        index = ids.index(current) if current in ids else len(ids)
        self._syncing_preset = True
        self._preset_row.set_selected(index)
        self._syncing_preset = False

    def _on_preset_selected(self, row: Adw.ComboRow, _pspec) -> None:
        if self._syncing_preset:
            return
        index = row.get_selected()
        if index < len(self._presets):
            self._apply_preset(index)
        else:
            self._settings.set_string("preset", CUSTOM)

    def _apply_preset(self, index: int) -> None:
        preset_id, _name, gains = self._presets[index]
        self._settings.set_value("bands", GLib.Variant("ad", [float(g) for g in gains]))
        self._settings.set_string("preset", preset_id)

    def _on_start_service(self, _banner: Adw.Banner) -> None:
        start_service(lambda: GLib.timeout_add(500, self._refresh_once))

    def _refresh_once(self) -> bool:
        self._monitor.refresh()
        return GLib.SOURCE_REMOVE
