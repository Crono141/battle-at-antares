extends Control
## The research panel's branching arrow (WP3.3g, as the change-research screen's little arrow):
## a stem down from the field to its applications, with an arrowhead at each one that comes with
## the choice: the chosen one, or all of them in a field that gives every application (creative
## races; owner: "non-creative races only get to pick 1 tech per level").

## The applications' rows (Controls beside this one), and which of them the arrow points at.
var rows: Array[Control] = []
var pointed: Array[bool] = []
var color := Color.WHITE
## Every application comes with the field (the arrow points at all of them whichever is picked).
var gives_all := false
var _chosen: Array[bool] = []


## The arrow as the game has it (the field's current choice, or none).
func set_chosen(chosen: Array[bool]) -> void:
	_chosen = chosen.duplicate()
	pointed = chosen.duplicate()
	queue_redraw()


## The pointer is on row `i` (-1: off the rows): the arrow shows what picking it would give.
func hover(i: int) -> void:
	if i < 0:
		pointed = _chosen.duplicate()
	else:
		pointed = []
		for k in rows.size():
			pointed.append(gives_all or k == i)
	queue_redraw()

const WIDTH := 18.0


## The arrow pulses slowly, as the game's does (owner, 2026-11-13).
const PULSE_TIME := 0.6 # 50% faster than first tried (owner)
const PULSE_DIM := 0.35


func _ready() -> void:
	custom_minimum_size.x = WIDTH
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	var pulse := create_tween().set_loops().set_trans(Tween.TRANS_SINE).set_ease(Tween.EASE_IN_OUT)
	pulse.tween_property(self, "modulate:a", PULSE_DIM, PULSE_TIME)
	pulse.tween_property(self, "modulate:a", 1.0, PULSE_TIME)


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
