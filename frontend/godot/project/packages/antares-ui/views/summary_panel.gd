extends VBoxContainer
## The turn's summary as a notification area (owner, 2026-11-14): bottom left, a SUMMARY (n)
## button that expands and collapses the list of the turn summary's messages (the `turn_summary`
## query: TURNSUM's order, serious ones marked). It doesn't hold the turn; the to-do list keeps
## only what must be answered. Hidden when the End of Turn Summary option is off or there is
## nothing to say. A message about one of the player's colonies opens that colony.

## A message picked (the query's entry; `colony` >= 0 when it has one to open).
signal item_chosen(item: Dictionary)

const LIST_WIDTH := 420.0

var summary := {}
var on_galaxy := false

@onready var list_panel: PanelContainer = $List
@onready var items_box: VBoxContainer = $List/Scroll/Items
@onready var button: Button = $Toggle


func _ready() -> void:
	button.pressed.connect(func(): list_panel.visible = not list_panel.visible)
	list_panel.hide()
	_restyle()
	_update_shown()


func _notification(what: int) -> void:
	if what == NOTIFICATION_THEME_CHANGED and is_node_ready():
		_restyle()


## In the screen frame's bottom left corner, growing upwards.
func _restyle() -> void:
	var inset := get_theme_constant("inset", "Screen")
	var gap := get_theme_constant("pane_gap", "Screen")
	offset_left = inset + gap + 6.0
	offset_bottom = -(inset + gap + 6.0)


## The `turn_summary` answer: the list rebuilt; a new turn's messages start collapsed.
func show_summary(result: Dictionary) -> void:
	if int(result.get("stardate", -1)) != int(summary.get("stardate", -1)):
		list_panel.hide()
	summary = result
	for child in items_box.get_children():
		items_box.remove_child(child)
		child.queue_free()
	var items: Array = result.get("items", [])
	for item: Dictionary in items:
		var row := Button.new()
		row.theme_type_variation = &"ListItem"
		row.focus_mode = Control.FOCUS_NONE
		row.alignment = HORIZONTAL_ALIGNMENT_LEFT
		row.autowrap_mode = TextServer.AUTOWRAP_WORD_SMART
		row.custom_minimum_size = Vector2(LIST_WIDTH - 24.0, 28)
		row.text = ("!  " if bool(item.get("serious", false)) else "•  ") + String(item.get("text", ""))
		if bool(item.get("serious", false)):
			row.add_theme_color_override("font_color", Color(1.0, 0.55, 0.45))
		if int(item.get("colony", -1)) < 0:
			row.tooltip_text = "No colony to open for this message"
		var picked := item
		row.pressed.connect(func(): item_chosen.emit(picked))
		items_box.add_child(row)
	button.text = "SUMMARY (%d)" % items.size()
	_update_shown()


## Shown over the native galaxy only.
func set_on_galaxy(on: bool) -> void:
	on_galaxy = on
	_update_shown()


func _update_shown() -> void:
	visible = on_galaxy and bool(summary.get("on", false)) and not summary.get("items", []).is_empty()
	list_panel.custom_minimum_size.x = LIST_WIDTH
	var scroll: ScrollContainer = $List/Scroll
	scroll.custom_minimum_size.y = minf(items_box.get_combined_minimum_size().y, 360.0)
	reset_size() # anchored bottom left, it grows upwards
