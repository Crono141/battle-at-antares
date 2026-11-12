extends Control
## The research panel's branching arrow (WP3.3g, as the change-research screen's little arrow):
## a stem down from the field to its applications, with an arrowhead at each one that comes with
## the choice: the chosen one, or all of them in a field that gives every application (creative
## races; owner: "non-creative races only get to pick 1 tech per level").

## The applications' rows (Controls beside this one), and which of them the arrow points at.
var rows: Array[Control] = []
var pointed: Array[bool] = []
var color := Color.WHITE

const WIDTH := 18.0


func _ready() -> void:
	custom_minimum_size.x = WIDTH
	mouse_filter = Control.MOUSE_FILTER_IGNORE


func _draw() -> void:
	if rows.is_empty():
		return
	var last := -1
	for i in pointed.size():
		if pointed[i]:
			last = i
	if last < 0:
		return
	var x := 5.0
	var bottom := _row_mid(last)
	draw_line(Vector2(x, 0), Vector2(x, bottom), color, 2.0)
	for i in rows.size():
		if not pointed[i]:
			continue
		var y := _row_mid(i)
		draw_line(Vector2(x, y), Vector2(WIDTH - 5.0, y), color, 2.0)
		draw_colored_polygon(PackedVector2Array([Vector2(WIDTH, y), Vector2(WIDTH - 7.0, y - 4.0), Vector2(WIDTH - 7.0, y + 4.0)]), color)


func _row_mid(i: int) -> float:
	var r: Control = rows[i]
	return r.position.y + r.size.y * 0.5 - position.y
