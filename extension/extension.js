// SPDX-License-Identifier: GPL-3.0-or-later

import Clutter from 'gi://Clutter';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import St from 'gi://St';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import {Extension, gettext as _} from 'resource:///org/gnome/shell/extensions/extension.js';

import {BAND_LABELS, GAIN_MAX, GAIN_MIN, getPresets} from './presets.js';
import {VerticalSlider} from './verticalSlider.js';

const APP_DESKTOP_ID = 'io.github.cloudie_code.equalizer.desktop';
const SINK_NODE_NAME = 'gnome_equalizer_sink';
const ICON_FILE = 'icons/hicolor/symbolic/apps/io.github.cloudie_code.equalizer-symbolic.svg';

function formatGain(value) {
    if (value === 0)
        return '0';
    const text = Number.isInteger(value) ? `${Math.abs(value)}` : Math.abs(value).toFixed(1);
    return value > 0 ? `+${text}` : `−${text}`;
}

const EqualizerBands = GObject.registerClass(
class EqualizerBands extends St.BoxLayout {
    _init(settings) {
        super._init({
            style_class: 'equalizer-bands',
            x_expand: true,
        });

        this._settings = settings;
        this._sliders = [];
        this._valueLabels = [];
        this._dragging = false;

        BAND_LABELS.forEach((label, i) => {
            const band = new St.BoxLayout({
                style_class: 'equalizer-band',
                orientation: Clutter.Orientation.VERTICAL,
                x_expand: true,
            });

            const valueLabel = new St.Label({
                style_class: 'equalizer-band-value',
                x_align: Clutter.ActorAlign.CENTER,
            });
            band.add_child(valueLabel);

            const slider = new VerticalSlider({min: GAIN_MIN, max: GAIN_MAX, step: 0.5});
            /* Translators: accessible name of an equalizer slider, %s is a frequency like “1.6k” */
            slider.accessible_name = _('%s Hz').format(label);
            slider.connect('notify::value', () => this._onSliderChanged(i));
            slider.connect('drag-begin', () => (this._dragging = true));
            slider.connect('drag-end', () => (this._dragging = false));
            band.add_child(slider);

            band.add_child(new St.Label({
                style_class: 'equalizer-band-label',
                text: label,
                opacity: 160,
                x_align: Clutter.ActorAlign.CENTER,
            }));

            this._sliders.push(slider);
            this._valueLabels.push(valueLabel);
            this.add_child(band);
        });

        this._settings.connectObject('changed::bands', () => this._sync(), this);
        this._sync();
    }

    _sync() {
        const gains = this._settings.get_value('bands').deepUnpack();
        this._syncing = true;
        this._sliders.forEach((slider, i) => {
            slider.value = gains[i] ?? 0;
            this._valueLabels[i].text = formatGain(slider.value);
        });
        this._syncing = false;
    }

    _onSliderChanged(index) {
        const slider = this._sliders[index];
        this._valueLabels[index].text = formatGain(slider.value);
        if (this._syncing)
            return;

        const gains = this._sliders.map(s => s.value);
        this._settings.set_value('bands', new GLib.Variant('ad', gains));
        this._settings.set_string('preset', 'custom');
    }
});

export default class EqualizerExtension extends Extension {
    enable() {
        this._settings = this.getSettings();
        this._presets = getPresets(_);
        this._icon = Gio.FileIcon.new(this.dir.resolve_relative_path(ICON_FILE));

        const quickSettings = Main.panel.statusArea.quickSettings;
        if (quickSettings._volumeOutput) {
            this._setup(quickSettings);
        } else {
            // The system indicators are created asynchronously at startup.
            this._idleId = GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, () => {
                if (!quickSettings._volumeOutput)
                    return GLib.SOURCE_CONTINUE;
                this._idleId = 0;
                this._setup(quickSettings);
                return GLib.SOURCE_REMOVE;
            });
        }
    }

    disable() {
        if (this._idleId) {
            GLib.source_remove(this._idleId);
            this._idleId = 0;
        }

        this._restoreDeviceList();

        this._menu?.close({animate: false});
        this._menu?.destroy();
        this._menu = null;
        this._menuManager = null;
        this._button?.destroy();
        this._button = null;
        this._presetItems = null;

        this._settings?.disconnectObject(this);
        this._settings = null;
        this._icon = null;
        this._output = null;
        this._quickSettings = null;
    }

    _setup(quickSettings) {
        this._quickSettings = quickSettings;
        this._output = quickSettings._volumeOutput._output;

        this._createButton();
        this._createMenu();
        this._hideOwnSink();

        this._settings.connectObject(
            'changed::enabled', () => this._syncEnabled(),
            'changed::preset', () => this._syncPreset(),
            'changed::loudness-equalization', () => this._syncToggles(),
            'changed::bass-boost', () => this._syncToggles(),
            this);
        this._syncEnabled();
        this._syncPreset();
        this._syncToggles();
    }

    _createButton() {
        this._button = new St.Button({
            child: new St.Icon({gicon: this._icon}),
            style_class: 'icon-button flat',
            can_focus: true,
            x_expand: false,
            y_expand: true,
            accessible_name: _('Open equalizer'),
        });
        this._button.connect('clicked', () => this._menu.toggle());

        // Place it between the slider and the output device arrow.
        const box = this._output.child;
        box.insert_child_below(this._button, this._output._menuButton);
    }

    _createMenu() {
        // QuickToggleMenu is not exported; reuse the class of the output menu
        // so the popup looks and animates like every other quick settings menu.
        const QuickToggleMenu = this._output.menu.constructor;
        this._menu = new QuickToggleMenu(this._output);
        this._menu.actor.hide();
        this._menu.setHeader(this._icon, _('Equalizer'));

        this._switch = new PopupMenu.Switch(this._settings.get_boolean('enabled'));
        const switchButton = new St.Button({
            child: this._switch,
            can_focus: true,
            y_align: Clutter.ActorAlign.CENTER,
            accessible_name: _('Apply the equalizer'),
        });
        switchButton.connect('clicked', () =>
            this._settings.set_boolean('enabled', !this._settings.get_boolean('enabled')));
        // Right edge of the header, after the spacer that follows the title.
        this._menu._header.layout_manager.attach(switchButton, 3, 0, 1, 2);

        const bandsItem = new PopupMenu.PopupBaseMenuItem({
            reactive: false,
            can_focus: false,
            style_class: 'equalizer-bands-item',
        });
        this._bands = new EqualizerBands(this._settings);
        bandsItem.add_child(this._bands);
        this._menu.addMenuItem(bandsItem);

        this._presetMenu = new PopupMenu.PopupSubMenuMenuItem(_('Preset'));
        this._presetItems = new Map();
        for (const preset of this._presets) {
            const item = this._presetMenu.menu.addAction(preset.name, () => {
                this._settings.set_value('bands', new GLib.Variant('ad', preset.gains));
                this._settings.set_string('preset', preset.id);
            });
            this._presetItems.set(preset.id, item);
        }
        this._menu.addMenuItem(this._presetMenu);

        this._menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());

        this._loudnessItem = new PopupMenu.PopupSwitchMenuItem(_('Loudness Equalization'), false);
        this._loudnessItem.connect('toggled', (_item, state) =>
            this._settings.set_boolean('loudness-equalization', state));
        this._menu.addMenuItem(this._loudnessItem);

        this._bassItem = new PopupMenu.PopupSwitchMenuItem(_('Bass Boost'), false);
        this._bassItem.connect('toggled', (_item, state) =>
            this._settings.set_boolean('bass-boost', state));
        this._menu.addMenuItem(this._bassItem);

        this._menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._menu.addSettingsAction(_('Equalizer Settings'), APP_DESKTOP_ID);

        const qsMenu = this._quickSettings.menu;
        qsMenu._overlay.add_child(this._menu.actor);
        this._menu.connect('open-state-changed', (_m, isOpen) => {
            qsMenu._setDimmed(isOpen);
            qsMenu._activeMenu = isOpen ? this._menu : null;
            if (isOpen)
                this._button.add_style_pseudo_class('checked');
            else
                this._button.remove_style_pseudo_class('checked');
        });

        this._menuManager = new PopupMenu.PopupMenuManager(this._output);
        this._menuManager.addMenu(this._menu);
    }

    _syncEnabled() {
        const enabled = this._settings.get_boolean('enabled');
        this._switch.state = enabled;
        this._bands.opacity = enabled ? 255 : 128;
        if (enabled)
            this._menu._headerIcon.add_style_class_name('active');
        else
            this._menu._headerIcon.remove_style_class_name('active');
    }

    _syncPreset() {
        const id = this._settings.get_string('preset');
        const preset = this._presets.find(p => p.id === id);
        const name = preset ? preset.name : _('Custom');

        this._menu.setHeader(this._icon, _('Equalizer'), name);
        for (const [presetId, item] of this._presetItems) {
            item.setOrnament(presetId === id
                ? PopupMenu.Ornament.CHECK
                : PopupMenu.Ornament.NONE);
        }
    }

    _syncToggles() {
        this._loudnessItem.setToggleState(this._settings.get_boolean('loudness-equalization'));
        this._bassItem.setToggleState(this._settings.get_boolean('bass-boost'));
    }

    _isOwnSink(id) {
        const control = this._output._control;
        const device = control.lookup_output_id(id);
        const stream = device ? control.lookup_stream_id(device.get_stream_id()) : null;
        return stream?.get_name() === SINK_NODE_NAME;
    }

    // The equalizer sink is an implementation detail: it always follows the
    // device picked by the user, so it must not be listed as an output.
    _hideOwnSink() {
        const output = this._output;
        const addDevice = Object.getPrototypeOf(output)._addDevice;
        output._addDevice = id => {
            if (!this._isOwnSink(id))
                addDevice.call(output, id);
        };
        for (const id of [...output._deviceItems.keys()]) {
            if (this._isOwnSink(id))
                output._removeDevice(id);
        }
    }

    _restoreDeviceList() {
        const output = this._output;
        if (!output || !Object.hasOwn(output, '_addDevice'))
            return;

        delete output._addDevice;
        const control = output._control;
        for (const stream of control.get_sinks()) {
            if (stream.get_name() !== SINK_NODE_NAME)
                continue;
            const device = control.lookup_device_from_stream(stream);
            if (device)
                output._addDevice(device.get_id());
        }
    }
}
