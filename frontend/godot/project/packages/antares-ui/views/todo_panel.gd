extends "drop_panel.gd"
## The start-of-turn to-do list (WP3.4d, owner 2026-11-13): drops from the bar's TO DO (n). The
## items the turn's reports would have walked the player through one by one (the `todo` query):
## colonies with nothing to build, colony and outpost ships that arrived, a leader's offer, and
## news (systems explored, leaders gaining a level, the turn summary). A click opens what the item
## needs; done ones drop off (news once read). Nothing may be ignored (owner, 2026-11-13): the game
## refuses `end_turn` while any is open (cancelling a colony ship's prompt is an answer). Owner,
## 2026-11-14: just a short list, opened by itself when there is something to do and hidden once
## everything is answered (frontend.gd); TURN / TO DO (n) on the bar still opens and closes it.

signal command(dict: Dictionary)
## An item picked: the frontend opens what it needs (`item` is the query's entry).
signal item_chosen(item: Dictionary)

@onready var items_box: VBoxContainer = $Fit/Box/Items

var todo := {}


## The `todo` answer: one row per item not yet done, news after the things to do.
func show_todo(result: Dictionary) -> void:
	todo = result
	for child in items_box.get_children():
		items_box.remove_child(child)
		child.queue_free()
	var shown := 0
	for news in [false, true]:
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
				row.add_theme_color_override("font_color", get_theme_color("font_color", "Label").darkened(0.15))
			var picked := item
			row.pressed.connect(func(): item_chosen.emit(picked))
			items_box.add_child(row)
			shown += 1
	if shown == 0:
		var none := Label.new()
		none.text = "Nothing waiting this turn."
		items_box.add_child(none)
	reset_size()
