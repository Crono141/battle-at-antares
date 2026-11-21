extends "drop_panel.gd"
## The turn's to-do list (WP3.4d, owner 2026-11-13; one list since 2026-11-21): drops from the
## bar's TO DO (n). What asks for a choice first (colonies with nothing to build, colony and
## outpost ships that arrived, a leader's offer): these hold the turn. Then the news, which
## doesn't (systems explored, leaders gaining a level, the turn summary's messages, serious ones
## marked): read ones drop off. END TURN at the bottom ends the turn once nothing holds it. The
## frontend opens the list by itself at a turn's start when it has anything, and hides it once
## it is empty.

signal command(dict: Dictionary)
## An item picked: the frontend opens what it needs (`item` is the query's entry).
signal item_chosen(item: Dictionary)
## END TURN pressed (only enabled when nothing holds the turn).
signal end_turn_requested

@onready var items_box: VBoxContainer = $Fit/Box/Items

var todo := {}


## The `todo` answer: one row per item not yet done, the choices before the news.
func show_todo(result: Dictionary) -> void:
	todo = result
	for child in items_box.get_children():
		items_box.remove_child(child)
		child.queue_free()
	var shown := 0
	for news in [false, true]:
		if news and shown > 0:
			items_box.add_child(HSeparator.new())
		var before := shown
		for item: Dictionary in result.get("items", []):
			if bool(item.get("done", false)) or bool(item.get("news", false)) != news:
				continue
			var row := Button.new()
			row.theme_type_variation = &"ListItem"
			row.focus_mode = Control.FOCUS_NONE
			row.alignment = HORIZONTAL_ALIGNMENT_LEFT
			row.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
			row.custom_minimum_size = Vector2(380, 30)
			row.text = ("•  " if news else "▸  ") + String(item.get("text", ""))
			if news:
				var color := get_theme_color("font_color", "Label").darkened(0.15)
				if bool(item.get("serious", false)):
					color = Color(1.0, 0.55, 0.45) # a serious summary message
				row.add_theme_color_override("font_color", color)
			var picked := item
			row.pressed.connect(func(): item_chosen.emit(picked))
			items_box.add_child(row)
			shown += 1
		if news and shown == before and items_box.get_child_count() > 0 and items_box.get_child(-1) is HSeparator:
			items_box.get_child(-1).queue_free()
	if shown == 0:
		var none := Label.new()
		none.text = "Nothing waiting this turn."
		items_box.add_child(none)
	_show_end_turn(int(result.get("open", 0)))
	reset_size()


var _end_turn: Button


## END TURN under the list: disabled while choices hold the turn (owner, 2026-11-21).
func _show_end_turn(open: int) -> void:
	if _end_turn == null:
		_end_turn = Button.new()
		_end_turn.focus_mode = Control.FOCUS_NONE
		_end_turn.custom_minimum_size = Vector2(160, 36)
		_end_turn.size_flags_horizontal = Control.SIZE_SHRINK_CENTER
		_end_turn.pressed.connect(func(): end_turn_requested.emit())
		$Fit/Box.add_child(_end_turn)
	_end_turn.text = "END TURN"
	_end_turn.disabled = open > 0
	_end_turn.tooltip_text = "%d %s still to answer" % [open, "item" if open == 1 else "items"] if open > 0 else "End the turn"
