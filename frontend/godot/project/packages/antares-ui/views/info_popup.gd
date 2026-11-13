extends RefCounted
## The right click's help (the game's help popup): a small pane beside what was clicked, with a
## title, the help text and an optional line under it. Gone with the next click anywhere.


## Opens beside `at` (a Control of `owner`'s); returns the popup so it can be closed early.
static func open(owner: Control, at: Control, title: String, text: String, footer := "") -> Popup:
	var popup := Popup.new()
	popup.transparent_bg = true
	popup.transparent = true
	var panel := PanelContainer.new()
	panel.theme_type_variation = &"Pane"
	var box := VBoxContainer.new()
	box.add_theme_constant_override("separation", 6)
	panel.add_child(box)
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
	popup.add_child(panel)
	owner.add_child(popup)
	# In the interface's own pixels (the window's content scale applies to the popup too): right of
	# what was clicked, or left of it when there is no room, and kept above the bottom edge.
	var wanted := panel.get_combined_minimum_size()
	var rect := at.get_global_rect()
	var screen := owner.get_viewport().get_visible_rect().size
	var place := Vector2(rect.end.x + 8.0, rect.position.y)
	if place.x + wanted.x > screen.x - 8.0:
		place.x = maxf(rect.position.x - wanted.x - 8.0, 8.0)
	place.y = clampf(place.y, 8.0, maxf(screen.y - wanted.y - 8.0, 8.0))
	popup.popup(Rect2i(Vector2i(place), Vector2i(wanted)))
	return popup
