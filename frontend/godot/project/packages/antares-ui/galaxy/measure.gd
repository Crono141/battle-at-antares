extends Node2D
## Measure distance (F9, or Options > Distance; owner, 2026-11-20: rebuilt over the native galaxy
## instead of the legacy screen, MAINSCR2::Distance_Between_Stars_Screen): a ring on the star the
## measure starts from, and to the hovered star a moving green line with the distance in parsecs,
## as the legacy screen draws it. Text keeps its size on screen.

const Links := preload("links.gd")
const LINE := Color(0.35, 1.0, 0.35, 1.0)
const REFUSED := Color(1.0, 0.35, 0.3, 1.0)
const RING := Color(0.35, 1.0, 0.35, 0.9)
## Text height in legacy pixels (a little larger than the star names).
const FONT_LEGACY_PIXELS := 8.0

@onready var galaxy: Node2D = get_parent()

var _time := 0.0


func _process(delta: float) -> void:
	if galaxy.measuring or not galaxy.relocation_pick.is_empty():
		_time += delta
		queue_redraw()
	elif _time != 0.0:
		_time = 0.0
		queue_redraw() # the last frame's line goes


func _draw() -> void:
	var pick: Dictionary = galaxy.relocation_pick
	if pick.is_empty() and (not galaxy.measuring or galaxy.measure_from < 0):
		return
	var px: float = galaxy.pixel()
	var from: int = galaxy.measure_from if pick.is_empty() else int(pick.from)
	var at: Vector2 = galaxy.star_position(from)
	var radius: float = maxf(galaxy.star_size(galaxy.stars[from]) * 0.6, 10.0 * px)
	draw_arc(at, radius, 0.0, TAU, 48, RING, 1.5 * px, true)
	var to: int = galaxy.hovered_star()
	if to < 0 or to == from:
		return
	var end: Vector2 = galaxy.star_position(to)
	var parsecs: int = galaxy.parsecs_between(from, to)
	var text := "%d parsec%s" % [parsecs, "" if parsecs == 1 else "s"]
	var color := LINE
	if not pick.is_empty():
		# Relocating (the system pane's menu): the ships' ETA too, or why not, in red.
		var target: Dictionary = pick.stars.get(to, {})
		if target.get("allowed", false):
			var eta := int(target.get("eta", 0))
			text += " · %d turn%s" % [eta, "" if eta == 1 else "s"]
		else:
			color = REFUSED
			text = String(target.get("reason", "Not a relocation target"))
	Links.dashed_line(self, at, end, color, px, _time)
	var font := ThemeDB.fallback_font
	var font_size := int(round(FONT_LEGACY_PIXELS * galaxy.legacy_pixel()))
	# Right of the hovered star, as the legacy screen prints it (left where it would leave the view).
	var view_right: float = galaxy.view_rect().end.x
	var width: float = font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x * px
	var offset: float = galaxy.star_size(galaxy.stars[to]) * 0.5 + 8.0 * px
	var where := Vector2(end.x + offset, end.y + font_size * 0.35 * px)
	if where.x + width > view_right:
		where.x = end.x - offset - width
	draw_set_transform(where, 0.0, Vector2(px, px))
	draw_string_outline(font, Vector2.ZERO, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, 5, Color(0, 0, 0, 0.85))
	draw_string(font, Vector2.ZERO, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, color)
	draw_set_transform(Vector2.ZERO)
