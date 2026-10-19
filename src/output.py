# output.py
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

import json

from gi.repository import Gio, GLib, GObject

SINK_NODE_NAME = "gnome_equalizer_sink"
SERVICE = "gnome-equalizer.service"


class OutputMonitor(GObject.Object):
    """Follows the default output device through `pactl subscribe`."""

    description = GObject.Property(type=str, default="")
    icon_name = GObject.Property(type=str, default="audio-speakers-symbolic")
    filter_running = GObject.Property(type=bool, default=True)

    def __init__(self):
        super().__init__()
        self._refresh_id = 0
        self._subscribe = None
        try:
            self._subscribe = Gio.Subprocess.new(
                ["pactl", "subscribe"], Gio.SubprocessFlags.STDOUT_PIPE
            )
            stream = Gio.DataInputStream.new(self._subscribe.get_stdout_pipe())
            stream.read_line_async(GLib.PRIORITY_DEFAULT, None, self._on_event, stream)
        except GLib.Error:
            pass
        self.refresh()

    def stop(self) -> None:
        if self._subscribe is not None:
            self._subscribe.force_exit()
            self._subscribe = None

    def _on_event(self, stream: Gio.DataInputStream, result: Gio.AsyncResult, _data) -> None:
        try:
            line, _length = stream.read_line_finish_utf8(result)
        except GLib.Error:
            return
        if line is None:
            return
        if "sink" in line or "server" in line:
            # Events arrive in bursts, refresh once they settle.
            if self._refresh_id:
                GLib.source_remove(self._refresh_id)
            self._refresh_id = GLib.timeout_add(150, self._refresh_timeout)
        stream.read_line_async(GLib.PRIORITY_DEFAULT, None, self._on_event, stream)

    def _refresh_timeout(self) -> bool:
        self._refresh_id = 0
        self.refresh()
        return GLib.SOURCE_REMOVE

    def refresh(self) -> None:
        _run(["pactl", "get-default-sink"], self._on_default_sink)

    def _on_default_sink(self, default: str | None) -> None:
        if default is None:
            return
        _run(["pactl", "-f", "json", "list", "sinks"],
             lambda out: self._on_sinks(default.strip(), out))

    def _on_sinks(self, default: str, output: str | None) -> None:
        try:
            sinks = json.loads(output or "[]")
        except json.JSONDecodeError:
            return
        self.filter_running = any(s.get("name") == SINK_NODE_NAME for s in sinks)
        for sink in sinks:
            if sink.get("name") != default:
                continue
            props = sink.get("properties", {})
            self.description = sink.get("description", default)
            self.icon_name = _device_icon(props)
            return


def _device_icon(props: dict) -> str:
    """Same icons as the Sound panel: headphones, displays or speakers."""
    icon = props.get("device.icon_name", "")
    form_factor = props.get("device.form_factor", "")
    if form_factor in ("headphone", "headset") or "headset" in icon or "headphone" in icon:
        return "audio-headphones-symbolic"
    if icon.startswith("video-display"):
        return "video-display-symbolic"
    return "audio-speakers-symbolic"


def start_service(callback) -> None:
    """Enables the filter service so it also starts with every session."""
    _run(["systemctl", "--user", "enable", "--now", SERVICE], lambda _out: callback())


def _run(argv: list[str], callback) -> None:
    try:
        proc = Gio.Subprocess.new(
            argv, Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_SILENCE
        )
    except GLib.Error:
        callback(None)
        return

    def done(p: Gio.Subprocess, result: Gio.AsyncResult) -> None:
        try:
            _ok, stdout, _stderr = p.communicate_utf8_finish(result)
        except GLib.Error:
            stdout = None
        callback(stdout)

    proc.communicate_utf8_async(None, None, done)
