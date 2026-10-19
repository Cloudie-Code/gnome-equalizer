// SPDX-License-Identifier: GPL-3.0-or-later

import Atk from 'gi://Atk';
import Clutter from 'gi://Clutter';
import GObject from 'gi://GObject';
import St from 'gi://St';

const SCROLL_STEP = 0.5;
const KEY_STEP = 1;
const SNAP_THRESHOLD = 0.6;

/**
 * A vertical slider centred on zero, drawn with the theme's slider style
 * so it matches the volume slider (accent colour, handle, track).
 */
export const VerticalSlider = GObject.registerClass({
    Properties: {
        'value': GObject.ParamSpec.double(
            'value', null, null,
            GObject.ParamFlags.READWRITE,
            -100, 100, 0),
    },
    Signals: {
        'drag-begin': {},
        'drag-end': {},
    },
}, class VerticalSlider extends St.DrawingArea {
    _init({min, max, step}) {
        this._min = min;
        this._max = max;
        this._step = step;
        this._value = 0;
        this._barWidth = 0;
        this._handleRadius = 0;

        super._init({
            style_class: 'slider equalizer-slider',
            can_focus: true,
            reactive: true,
            track_hover: true,
            accessible_role: Atk.Role.SLIDER,
            y_expand: true,
            x_align: Clutter.ActorAlign.CENTER,
        });

        this._accessible = St.GenericAccessible.new_for_actor(this);
        this._accessible.connect('get-current-value', () => this._value);
        this._accessible.connect('get-minimum-value', () => this._min);
        this._accessible.connect('get-maximum-value', () => this._max);
        this._accessible.connect('get-minimum-increment', () => this._step);
        this._accessible.connect('set-current-value', (_a, value) => (this.value = value));
        this.set_accessible(this._accessible);

        this._panGesture = new Clutter.PanGesture();
        this._panGesture.set_begin_threshold(0);
        this._panGesture.connect('recognize', () => {
            this._grab = global.stage.grab(this);
            this.emit('drag-begin');
            this._moveHandle(this._panGesture.get_centroid());
            return Clutter.EVENT_STOP;
        });
        this._panGesture.connect('pan-update',
            () => this._moveHandle(this._panGesture.get_centroid()));
        this._panGesture.connect('end', () => {
            this._grab?.dismiss();
            this._grab = null;
            this.emit('drag-end');
        });
        this.add_action(this._panGesture);

        const scroll = new Clutter.ScrollController({
            flags: Clutter.ScrollControllerFlags.DISCRETE |
                Clutter.ScrollControllerFlags.SCROLL_VERTICAL,
        });
        scroll.connect('scroll', (_c, _sprite, _source, _dx, dy) => {
            this.value = this._value - dy * SCROLL_STEP;
        });
        this.add_action(scroll);
    }

    get value() {
        return this._value;
    }

    set value(value) {
        value = Math.clamp(Math.round(value / this._step) * this._step, this._min, this._max);
        if (this._value === value)
            return;

        this._value = value;
        this.notify('value');
        this._accessible.notify('accessible-value');
        this.queue_repaint();
    }

    vfunc_key_press_event(event) {
        const key = event.get_key_symbol();
        if (key === Clutter.KEY_Up || key === Clutter.KEY_Right) {
            this.value = this._value + KEY_STEP;
            return Clutter.EVENT_STOP;
        }
        if (key === Clutter.KEY_Down || key === Clutter.KEY_Left) {
            this.value = this._value - KEY_STEP;
            return Clutter.EVENT_STOP;
        }
        return super.vfunc_key_press_event(event);
    }

    vfunc_style_changed() {
        const node = this.get_theme_node();
        this._barWidth = node.get_length('-barlevel-height');
        this._handleRadius = Math.round(2 * node.get_length('-slider-handle-radius')) / 2;
        this._trackColor = node.get_color('-barlevel-background-color');
        this._activeColor = node.get_color('-barlevel-active-background-color');
        super.vfunc_style_changed();
    }

    vfunc_get_preferred_width(_forHeight) {
        const width = Math.max(this._barWidth, 2 * this._handleRadius);
        return this.get_theme_node().adjust_preferred_width(width, width);
    }

    vfunc_get_preferred_height(_forWidth) {
        const height = 4 * this._handleRadius;
        return this.get_theme_node().adjust_preferred_height(height, height);
    }

    _valueToY(value, height) {
        const r = this._handleRadius;
        const progress = (value - this._min) / (this._max - this._min);
        return r + (height - 2 * r) * (1 - progress);
    }

    vfunc_repaint() {
        const cr = this.get_context();
        const [width, height] = this.get_surface_size();
        const x = width / 2;
        const half = this._barWidth / 2;
        const zeroY = this._valueToY(0, height);
        const valueY = this._valueToY(this._value, height);

        const roundedBar = (top, bottom) => {
            cr.newSubPath();
            cr.arc(x, top + half, half, Math.PI, 0);
            cr.arc(x, bottom - half, half, 0, Math.PI);
            cr.closePath();
        };

        roundedBar(this._handleRadius - half, height - this._handleRadius + half);
        cr.setSourceColor(this._trackColor);
        cr.fill();

        if (this._value !== 0) {
            roundedBar(Math.min(zeroY, valueY) - half, Math.max(zeroY, valueY) + half);
            cr.setSourceColor(this._activeColor);
            cr.fill();
        }

        cr.setSourceColor(this.get_theme_node().get_foreground_color());
        cr.arc(x, valueY, this._handleRadius, 0, 2 * Math.PI);
        cr.fill();
        cr.$dispose();
    }

    _moveHandle(coords) {
        const height = this.allocation.get_height();
        const r = this._handleRadius;
        const progress = 1 - Math.clamp((coords.y - r) / (height - 2 * r), 0, 1);
        let value = this._min + progress * (this._max - this._min);
        if (Math.abs(value) < SNAP_THRESHOLD)
            value = 0;
        this.value = value;
    }
});
