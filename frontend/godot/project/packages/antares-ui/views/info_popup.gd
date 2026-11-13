extends RefCounted
## The right click's help (the game's help popup): a small pane beside what was clicked, with a
## title, the help text and an optional line under it (info_tip.gd: one at a time; a left click
## only closes it, a right click on another item swaps it).

static var _current: Control


## Opens beside `at` (a Control of `owner`'s); returns the pane so it can be closed early.
static func open(owner: Control, at: Control, title: String, text: String, footer := "") -> Control:
	close_current()
	var tip: PanelContainer = preload("info_tip.gd").new()
	tip.theme_type_variation = &"Pane"
	tip.theme = owner.theme if owner.theme else _theme_of(owner)
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	tip.add_child(box)
	var heading := Label.new()
	heading.theme_type_variation = &"Name"
	heading.text = title
	box.add_child(heading)
	var body := Label.new()
	body.text = text if text != "" else "No information."
	body.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
	body.custom_minimum_size.x = 360
	body.add_theme_font_size_override("font_size", 16)
	box.add_child(body)
	if footer != "":
		var line := Label.new()
		line.theme_type_variation = &"Dim"
		line.text = footer
		box.add_child(line)
	# Over everything (the dialogs' overlay too), in the interface's own pixels.
	var layer := CanvasLayer.new()
	layer.layer = 120
	owner.get_tree().root.add_child(layer)
	layer.add_child(tip)
	var wanted := tip.get_combined_minimum_size()
	var rect := at.get_global_rect()
	var screen := owner.get_viewport().get_visible_rect().size
	var place := Vector2(rect.end.x + 8.0, rect.position.y)
	if place.x + wanted.x > screen.x - 8.0:
		place.x = maxf(rect.position.x - wanted.x - 8.0, 8.0)
	place.y = clampf(place.y, 8.0, maxf(screen.y - wanted.y - 8.0, 8.0))
	tip.position = place.floor()
	tip.size = wanted
	_current = tip
	return tip


## The open help pane, if any.
static func is_open() -> bool:
	return is_instance_valid(_current) and not _current.is_queued_for_deletion() and _current.visible


static func close_current() -> void:
	if is_open():
		_current.close()
	_current = null


## The theme a view draws with (themes are set on the views, not inherited by a new layer).
static func _theme_of(node: Node) -> Theme:
	var n := node
	while n:
		if n is Control and (n as Control).theme:
			return (n as Control).theme
		n = n.get_parent()
	return null
