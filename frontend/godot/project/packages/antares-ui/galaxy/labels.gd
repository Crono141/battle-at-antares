extends Node2D
## Star names under the stars, as the legacy map labels them (the `galaxy` query's `label`): hidden
## until known, in the owner's colour once their colony is known, outposts dimmer, and shared
## systems coloured in parts by how many colonies each empire has there. Text keeps its size on
## screen.

## Text height in legacy pixels (the map's small font).
const FONT_LEGACY_PIXELS := 6.5
const OUTPOST_DIM := 0.7

@onready var galaxy: Node2D = get_parent()


func _draw() -> void:
	var font := ThemeDB.fallback_font
	var px: float = galaxy.pixel()
	var font_size := int(round(FONT_LEGACY_PIXELS * galaxy.legacy_pixel()))
	for star: Dictionary in galaxy.stars:
		var text: String = star.label
		if text.is_empty():
			continue
		var color: Color = galaxy.player_color(_owner_color(int(star.label_owner)))
		if star.outpost:
			color = color.darkened(1.0 - OUTPOST_DIM)
		var below := Vector2(float(star.x), float(star.y) + galaxy.star_size(star) * 0.5)
		draw_set_transform(below, 0.0, Vector2(px, px))
		var width := font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
		var at := Vector2(-width * 0.5, font_size + 1.0)
		var shares: Array = star.get("label_shares", [])
		if shares.is_empty():
			draw_string_outline(font, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, 4, Color(0, 0, 0, 0.8))
			draw_string(font, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, color)
		else:
			_draw_shared(font, at, text, font_size, shares)
	draw_set_transform(Vector2.ZERO)


## A player id's colour index (the label names an owner by player id).
func _owner_color(player: int) -> int:
	if player < 0:
		return -1
	for p: Dictionary in galaxy.data.get("players", []):
		if int(p.id) == player:
			return int(p.color)
	return -1


## Several empires' colonies in one system: the name in parts, each empire's share of the letters.
func _draw_shared(font: Font, at: Vector2, text: String, font_size: int, shares: Array) -> void:
	var total := 0
	for share: Dictionary in shares:
		total += int(share.colonies)
	draw_string_outline(font, at, text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, 4, Color(0, 0, 0, 0.8))
	var start := 0
	var x := at.x
	for i in shares.size():
		var share: Dictionary = shares[i]
		var count := text.length() - start if i == shares.size() - 1 else int(round(float(text.length()) * int(share.colonies) / total))
		var part := text.substr(start, count)
		draw_string(font, Vector2(x, at.y), part, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, galaxy.player_color(_owner_color(int(share.player))))
		x += font.get_string_size(part, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
		start += count
