# main.py
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

import os
import sys
from gettext import gettext as _

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")

from gi.repository import Adw, Gdk, Gio, Gtk

from .window import EqualizerWindow

APP_ID = "io.github.cloudie_code.equalizer"


class EqualizerApplication(Adw.Application):
    def __init__(self, version: str):
        super().__init__(application_id=APP_ID, flags=Gio.ApplicationFlags.DEFAULT_FLAGS)
        self.version = version
        self.settings = Gio.Settings(schema_id=APP_ID)

        about = Gio.SimpleAction(name="about")
        about.connect("activate", self._on_about)
        self.add_action(about)

        quit_action = Gio.SimpleAction(name="quit")
        quit_action.connect("activate", lambda *_: self.quit())
        self.add_action(quit_action)
        self.set_accels_for_action("app.quit", ["<primary>q"])
        self.set_accels_for_action("window.close", ["<primary>w"])

    def do_startup(self) -> None:
        Adw.Application.do_startup(self)
        css = Gtk.CssProvider()
        css.load_from_path(os.path.join(os.path.dirname(__file__), "style.css"))
        Gtk.StyleContext.add_provider_for_display(
            Gdk.Display.get_default(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION
        )

    def do_activate(self) -> None:
        window = self.props.active_window or EqualizerWindow(self, self.settings)
        window.present()

    def _on_about(self, *_args) -> None:
        about = Adw.AboutDialog(
            application_name=_("Equalizer"),
            application_icon=APP_ID,
            developer_name="cloudie-code",
            version=self.version,
            website="https://github.com/cloudie-code/gnome-equalizer",
            issue_url="https://github.com/cloudie-code/gnome-equalizer/issues",
            license_type=Gtk.License.GPL_3_0,
            # Translators: Replace "translator-credits" with your name/username, and optionally an email or URL.
            translator_credits=_("translator-credits"),
        )
        about.present(self.props.active_window)


def main(version: str) -> int:
    app = EqualizerApplication(version)
    return app.run(sys.argv)
