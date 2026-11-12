extends PanelContainer
## A panel that drops down from under the top bar (WP3.3g: the bar's command points and research,
## owner: "slide down from the top and stay in native"): open_at() slides it down with its left edge
## at x (kept on screen), close() slides it back up and emits `closed`. Esc and its X close it.

signal closed

const SLIDE_TIME := 0.22

var _tween: Tween
var _closing := false


## Opens below the bar, centred on `center_x` (kept inside the screen).
func open_at(center_x: float) -> void:
	if visible and not _closing:
		return
	reset_size()
	var width := get_parent_area_size().x
	var inset := float(get_theme_constant("inset", "Screen"))
	position.x = clampf(center_x - size.x * 0.5, inset + 8.0, width - size.x - inset - 48.0)
	if _tween:
		_tween.kill()
	if not visible:
		position.y = -size.y
	_closing = false
	show()
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_OUT)
	_tween.tween_property(self, "position:y", float(get_theme_constant("content_top", "Screen")), SLIDE_TIME)


func close() -> void:
	if not visible or _closing:
		return
	if _tween:
		_tween.kill()
	_closing = true
	_tween = create_tween().set_trans(Tween.TRANS_CUBIC).set_ease(Tween.EASE_IN)
	_tween.tween_property(self, "position:y", -size.y, SLIDE_TIME)
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
	if visible and not _closing and key and key.pressed and key.keycode == KEY_ESCAPE:
		close()
		get_viewport().set_input_as_handled()
