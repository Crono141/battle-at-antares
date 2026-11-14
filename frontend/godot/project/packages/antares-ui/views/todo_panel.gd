extends "drop_panel.gd"
## The start-of-turn to-do list (WP3.4d, owner 2026-11-13): drops from the bar's TO DO (n). The
## items the turn's reports would have walked the player through one by one (the `todo` query):
## colonies with nothing to build, colony and outpost ships that arrived, a leader's offer, and
## news read once (systems explored, leaders gaining a level, the turn summary). A click opens what
## the item needs; done ones drop off (news once read). END TURN ends the turn; with colonies that
## have nothing to build it asks once first.

signal command(dict: Dictionary)
## An item picked: the frontend opens what it needs (`item` is the query's entry).
signal item_chosen(item: Dictionary)
signal end_turn

@onready var items_box: VBoxContainer = $Fit/Box/Items
@onready var ask: Label = $Fit/Box/Ask
@onready var end_button: Button = $Fit/Box/Buttons/EndTurn
@onready var back_button: Button = $Fit/Box/Buttons/Back

var todo := {}
var _asking := false


func _ready() -> void:
	end_button.pressed.connect(_on_end_turn)
	back_button.pressed.connect(func(): _set_asking(false))


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
	if not _asking:
		_set_asking(false)
	reset_size()


func _on_end_turn() -> void:
	var builds := int(todo.get("builds", 0))
	if builds > 0 and not _asking:
		_set_asking(true)
		ask.text = ("%d colonies have nothing to build. End the turn anyway?" % builds) if builds > 1 \
			else "A colony has nothing to build. End the turn anyway?"
		return
	_set_asking(false)
	close()
	end_turn.emit()


func _set_asking(on: bool) -> void:
	_asking = on
	ask.visible = on
	back_button.visible = on
	end_button.text = "END TURN" if not on else "YES, END IT"
	reset_size()
