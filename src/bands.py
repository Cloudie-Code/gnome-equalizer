# bands.py
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

from gi.repository import Gio, GLib, Gtk

from .presets import BAND_LABELS, CUSTOM, GAIN_MAX, GAIN_MIN, format_gain


class EqualizerBands(Gtk.Box):
    """Six vertical sliders bound to the `bands` key."""

    def __init__(self, settings: Gio.Settings):
        super().__init__(
            orientation=Gtk.Orientation.HORIZONTAL,
            homogeneous=True,
            spacing=6,
            margin_top=12,
            margin_bottom=12,
            margin_start=12,
            margin_end=12,
        )
        self.add_css_class("equalizer-bands")
        self._settings = settings
        self._syncing = False
        self._scales: list[Gtk.Scale] = []
        self._values: list[Gtk.Label] = []

        for label in BAND_LABELS:
            column = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)

            value = Gtk.Label(css_classes=["equalizer-value", "numeric"])
            column.append(value)

            adjustment = Gtk.Adjustment(lower=GAIN_MIN, upper=GAIN_MAX, step_increment=0.5,
                                        page_increment=3)
            scale = Gtk.Scale(
                orientation=Gtk.Orientation.VERTICAL,
                adjustment=adjustment,
                inverted=True,
                draw_value=False,
                has_origin=False,
                height_request=200,
                halign=Gtk.Align.CENTER,
                vexpand=True,
            )
            scale.add_css_class("equalizer-scale")
            scale.add_mark(0, Gtk.PositionType.RIGHT, None)
            # Translators: accessible name of an equalizer slider, %s is a frequency like “1.6k”
            scale.update_property([Gtk.AccessibleProperty.LABEL], [_("%s Hz") % label])
            scale.connect("value-changed", self._on_value_changed)
            column.append(scale)

            column.append(Gtk.Label(label=label, css_classes=["dim-label", "caption", "numeric"]))

            self._scales.append(scale)
            self._values.append(value)
            self.append(column)

        settings.connect("changed::bands", lambda *_: self._sync())
        self._sync()

    def _sync(self) -> None:
        gains = self._settings.get_value("bands").unpack()
        self._syncing = True
        for i, scale in enumerate(self._scales):
            scale.set_value(gains[i] if i < len(gains) else 0.0)
            self._values[i].set_label(format_gain(scale.get_value()))
        self._syncing = False

    def _on_value_changed(self, scale: Gtk.Scale) -> None:
        # Snap to half decibels, the resolution shown in the labels.
        value = round(scale.get_value() * 2) / 2
        if value != scale.get_value():
            scale.set_value(value)
            return
        index = self._scales.index(scale)
        self._values[index].set_label(format_gain(value))
        if self._syncing:
            return
        gains = [s.get_value() for s in self._scales]
        self._settings.set_value("bands", GLib.Variant("ad", gains))
        self._settings.set_string("preset", CUSTOM)
