// SPDX-License-Identifier: GPL-3.0-or-later

// Keep in sync with src/presets.py.
export const BAND_LABELS = ['60', '180', '550', '1.6k', '5k', '15k'];
export const GAIN_MIN = -12;
export const GAIN_MAX = 12;

/**
 * @param {Function} _ gettext function of the caller
 * @returns {Array<{id: string, name: string, gains: number[]}>}
 */
export function getPresets(_) {
    return [
        {id: 'flat', name: _('Flat'), gains: [0, 0, 0, 0, 0, 0]},
        {id: 'bass', name: _('Bass'), gains: [6, 4, 1, 0, 0, 0]},
        {id: 'treble', name: _('Treble'), gains: [0, 0, 0, 1, 4, 6]},
        {id: 'vocal', name: _('Vocal'), gains: [-2, -1, 2, 4, 2, 0]},
        {id: 'rock', name: _('Rock'), gains: [5, 3, -1, 1, 3, 4]},
        {id: 'pop', name: _('Pop'), gains: [-1, 2, 4, 3, 1, -1]},
        {id: 'jazz', name: _('Jazz'), gains: [3, 2, -1, 1, 2, 3]},
        {id: 'classical', name: _('Classical'), gains: [4, 2, 0, 0, 2, 3]},
        {id: 'electronic', name: _('Electronic'), gains: [5, 3, 0, -1, 3, 5]},
    ];
}
