# presets.py
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

# Keep in sync with extension/presets.js.
BAND_LABELS = ("60", "180", "550", "1.6k", "5k", "15k")
GAIN_MIN = -12.0
GAIN_MAX = 12.0
CUSTOM = "custom"


def presets() -> list[tuple[str, str, tuple[float, ...]]]:
    """(id, translated name, gains in dB) of every built-in preset."""
    return [
        ("flat", _("Flat"), (0, 0, 0, 0, 0, 0)),
        ("bass", _("Bass"), (6, 4, 1, 0, 0, 0)),
        ("treble", _("Treble"), (0, 0, 0, 1, 4, 6)),
        ("vocal", _("Vocal"), (-2, -1, 2, 4, 2, 0)),
        ("rock", _("Rock"), (5, 3, -1, 1, 3, 4)),
        ("pop", _("Pop"), (-1, 2, 4, 3, 1, -1)),
        ("jazz", _("Jazz"), (3, 2, -1, 1, 2, 3)),
        ("classical", _("Classical"), (4, 2, 0, 0, 2, 3)),
        ("electronic", _("Electronic"), (5, 3, 0, -1, 3, 5)),
    ]


def format_gain(value: float) -> str:
    if value == 0:
        return "0"
    magnitude = f"{abs(value):g}"
    return f"+{magnitude}" if value > 0 else f"−{magnitude}"
