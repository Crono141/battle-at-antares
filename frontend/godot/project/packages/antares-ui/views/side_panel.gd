extends PanelContainer
## A panel of the right sidebar's tabs (the empire panels, owner 2026-11-14: "all should appear
## flying out from the right"): open_in(rect) slides it in from the screen's right edge into
## `rect` (right edge against the side tabs, from under the bar to above the map controls);
## close() slides it back out and emits `closed`. Esc and its X close it.

signal closed
signal pin_changed(on: bool)

const SLIDE_TIME := 0.25
const _Themes := preload("../themes.gd")
const PIN_AMBER := Color8(255, 138, 28)

## Pinned (WP3.12, owner 2026-11-25, issue #4): the panel stays open while colonies and systems
## open from it or the map; their panes draw over it until closed. Remembered per panel.
var pinned := false
## Set by the frontend: true while a pane is open over the panel (Esc is theirs then).
var covered := Callable()
var _pin: Button
var _under := false
var _fade: Tween

var _tween: Tween
var _closing := false
var _rect := Rect2()


func _init() -> void:
	minimum_size_changed.connect(_shrink_to_contents)


## Back to its width when its contents need less again: a panel isn't in a container, so Godot
## grows it to fit its contents but never shrinks it (owner 2026-11-25: Colonies stayed wide after
## Planets and back, empty space right of BUY, until a window resize reset it).
func _shrink_to_contents() -> void:
	if not visible or _closing or _rect.size.x <= 0.0:
		return
	if size.x > maxf(_rect.size.x, get_combined_minimum_size().x) + 0.5:
		reset_size()
		if _tween == null or not _tween.is_running():
			position.x = _rect.end.x - size.x


## A pin glyph in the header, before its X (the Colonies and Planets panels).
func add_pin(key: String) -> void:
	var header := get_node_or_null("Box/Header") as Control
	if header == null:
		return
	_pin = Button.new()
	_pin.name = "Pin"
	_pin.toggle_mode = true
	_pin.custom_minimum_size = Vector2(34, 30)
	_pin.size_flags_vertical = Control.SIZE_SHRINK_BEGIN
	_pin.focus_mode = Control.FOCUS_NONE
	_pin.tooltip_text = "Pin: keep this panel open while colonies and systems open over it"
	header.add_child(_pin)
	var close_button := header.get_node_or_null("Close")
	if close_button:
		header.move_child(_pin, close_button.get_index())
	pinned = bool(_Themes.saved_value("panels", key + "_pinned", false))
	_pin.set_pressed_no_signal(pinned)
	_pin.draw.connect(_draw_pin)
	_pin.toggled.connect(func(on: bool):
		pinned = on
		_Themes.save_value("panels", key + "_pinned", on)
		_pin.queue_redraw()
		pin_changed.emit(on))


## Panes cover it (pinned): faded out and out of the pointer's way, still open; back when they go.
func set_under(on: bool) -> void:
	if on == _under:
		return
	_under = on
	if _fade:
		_fade.kill()
	var box := get_node_or_null("Box") as Control
	mouse_filter = Control.MOUSE_FILTER_IGNORE if on else Control.MOUSE_FILTER_STOP
	if not on and box:
		box.show()
	_fade = create_tween()
	_fade.tween_property(self, "modulate:a", 0.0 if on else 1.0, 0.15)
	if on and box:
		_fade.tween_callback(box.hide)


## A push pin: leaning and in the text colour when off, upright and amber when on.
func _draw_pin() -> void:
	var colour := PIN_AMBER if pinned else _pin.get_theme_color("font_color", "Button")
	if not pinned and _pin.is_hovered():
		colour = _pin.get_theme_color("font_hover_color", "Button")
	var s := minf(_pin.size.x, _pin.size.y)
	var centre := _pin.size * 0.5
	_pin.draw_set_transform(centre, 0.0 if pinned else -0.6, Vector2.ONE * s / 30.0)
	_pin.draw_rect(Rect2(-4.5, -11.0, 9.0, 3.0), colour)           # the head
	_pin.draw_rect(Rect2(-3.0, -8.0, 6.0, 8.0), colour)            # the body
	_pin.draw_rect(Rect2(-7.0, 0.0, 14.0, 2.5), colour)            # the collar
	_pin.draw_line(Vector2(0.0, 2.5), Vector2(0.0, 11.0), colour, 1.6) # the needle
	_pin.draw_set_transform(Vector2.ZERO)


## Slides in to `rect` (its height exactly; its width at least the contents').
func open_in(rect: Rect2) -> void:
	_rect = rect
	custom_minimum_size = Vector2(rect.size.x, rect.size.y)
	reset_size()
	var target := rect.end.x - size.x
	position.y = rect.position.y
	if _tween:
		_tween.kill()
	if not visible or _closing:
		position.x = get_viewport_rect().size.x
	_closing = false
	show()
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(self, "position:x", target, SLIDE_TIME)


## The window changed size (owner, 2026-11-14: the panels must stay against the side tabs): it
## moves to the new `area`'s right edge, at the width it wants there.
func reflow(area: Rect2) -> void:
	if not visible or _closing:
		return
	var width := _wanted_width(area)
	open_in(Rect2(area.end.x - width, area.position.y, width, area.size.y))


## How wide the panel is in `area` (a panel may grow or shrink its columns first).
func _wanted_width(area: Rect2) -> float:
	return minf(maxf(_rect.size.x, get_combined_minimum_size().x), area.size.x - 16.0)


## Where it rests when open (the galaxy fits left of this).
func rest_left() -> float:
	return _rect.end.x - maxf(size.x, custom_minimum_size.x)


func close() -> void:
	set_under(false)
	if not visible or _closing:
		return
	if _tween:
		_tween.kill()
	_closing = true
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	_tween.tween_property(self, "position:x", get_viewport_rect().size.x, SLIDE_TIME)
	_tween.tween_callback(func():
		hide()
		_closing = false)
	closed.emit()


## Hidden at once (a legacy screen took over).
func dismiss() -> void:
	if visible:
		if _tween:
			_tween.kill()
		hide()
		_closing = false
		closed.emit()


func _unhandled_key_input(event: InputEvent) -> void:
	var key := event as InputEventKey
	if pinned and covered.is_valid() and covered.call():
		return # a pane over the pinned panel takes Esc first
	if visible and not _closing and key and key.pressed and key.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()
