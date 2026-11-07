extends Node2D
## Fleets on the map (the `fleets` query): one icon per stack in its owner's colours, beside its
## star in the slot the legacy map gives it (the first empire right of the star, the second left,
## more below) or at its place in space; moving fleets the player can see draw a moving dashed line
## to their destination (green for the player's, red for others) and their ETA in turns.

const OWN_LINE := Color(0.35, 0.95, 0.35, 0.9)
const ENEMY_LINE := Color(1.0, 0.3, 0.25, 0.9)
## Icon widths at the legacy zoom levels, closest first (BUFFER0.LBX), by kind.
const ICON_WIDTHS := {
	"empire": [16.0, 12.0, 12.0, 11.0],
	"monster": [18.0, 14.0, 12.0, 11.0],
	"antaran": [16.0, 14.0, 9.0, 7.0],
}
const ETA_LEGACY_PIXELS := 6.0
const Links := preload("res://galaxy/links.gd")

@onready var galaxy: Node2D = get_parent()

var _time := 0.0


func _process(delta: float) -> void:
	_time += delta
	for fleet: Dictionary in galaxy.fleets:
		if fleet.line != "":
			queue_redraw()
			return


func _draw() -> void:
	var px: float = galaxy.pixel()
	var placed := []
	for fleet: Dictionary in galaxy.fleets:
		placed.append(_place(fleet))
	# Lines first, so icons sit on top of them.
	for i in galaxy.fleets.size():
		var fleet: Dictionary = galaxy.fleets[i]
		if fleet.line != "" and int(fleet.destination) >= 0:
			var rect: Rect2 = placed[i]
			Links.dashed_line(self, rect.get_center(), galaxy.star_position(int(fleet.destination)),
				OWN_LINE if fleet.line == "own" else ENEMY_LINE, px, _time)
	var font := ThemeDB.fallback_font
	var font_size := int(round(ETA_LEGACY_PIXELS * galaxy.legacy_pixel()))
	for i in galaxy.fleets.size():
		var fleet: Dictionary = galaxy.fleets[i]
		var rect: Rect2 = placed[i]
		var scale := rect.size.x / _art_width(fleet)
		if not galaxy.art.draw(self, "BUFFER0.LBX", int(fleet.sprite), 0, rect.position, scale):
			draw_rect(rect, galaxy.player_color(int(fleet.color)))
		if int(fleet.eta) >= 0:
			var text := "ETA %d" % int(fleet.eta)
			draw_set_transform(rect.end, 0.0, Vector2(px, px))
			var width := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
			draw_string_outline(font, Vector2(-width, font_size), text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, 4, Color(0, 0, 0, 0.8))
			draw_string(font, Vector2(-width, font_size), text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, galaxy.player_color(int(fleet.color)))
			draw_set_transform(Vector2.ZERO)


## The icon's rectangle in world units (SHIPS::Get_XYs_For_Orbiting_Ships, simplified).
func _place(fleet: Dictionary) -> Rect2:
	var widths: Array = ICON_WIDTHS.get(fleet.kind, ICON_WIDTHS.empire)
	var width: float = galaxy.lod_size(widths)
	var art_w := _art_width(fleet)
	var height: float = width * _art_height(fleet) / art_w
	var star := int(fleet.star)
	if star < 0:
		return Rect2(Vector2(float(fleet.x), float(fleet.y)) - Vector2(width, height) * 0.5, Vector2(width, height))
	var center: Vector2 = galaxy.star_position(star)
	var half: float = galaxy.star_size(galaxy.stars[star]) * 0.5
	var top_left: Vector2
	match int(fleet.slot):
		0:
			top_left = center + Vector2(half - width * 0.5, -half)
		1:
			top_left = center + Vector2(-half - width * 0.5, -half)
		_:
			top_left = center + Vector2((int(fleet.slot) - 3) * width * 1.1 - width * 0.5, half - height * 0.5)
	return Rect2(top_left, Vector2(width, height))


func _art_width(fleet: Dictionary) -> float:
	var sprite: Dictionary = galaxy.art.sprite("BUFFER0.LBX", int(fleet.sprite))
	return float(ICON_WIDTHS.get(fleet.kind, ICON_WIDTHS.empire)[0]) if sprite.is_empty() else float(sprite.size.x)


func _art_height(fleet: Dictionary) -> float:
	var sprite: Dictionary = galaxy.art.sprite("BUFFER0.LBX", int(fleet.sprite))
	return _art_width(fleet) * 0.75 if sprite.is_empty() else float(sprite.size.y)
