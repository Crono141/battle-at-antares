## Native beams (spike 3.9, the owner's direction of 2026-11-21): the original's beam drawing
## (bolt.cpp: Bolt_Segment, Bolt_Line, Wide_Bolt_Line; beams.cpp: Draw_Beam2's styles) ported, so
## a beam is generated here from its weapon's recipe and drawn as strands, crisp at any zoom.
## A strand is a 1x-pixel-wide line with one colour per pixel along it, from the weapon's 16-colour
## ramp; the HD look is each strand as a smooth line through its end pixels' centres (the twins'
## stroke, D21). Random jitter is Godot's own (close, not the game's numbers).
extends RefCounted

const ATAN := [0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 8, 9, 9, 10, 10, 11, 11, 11, 12, 12, 13, 13, 14, 14, 14, 15, 15, 16, 16, 16, 17, 17, 18, 18, 18, 19, 19, 20, 20, 20, 21, 21, 22, 22, 22, 23, 23, 24, 24, 24, 25, 25, 25, 26, 26, 26, 27, 27, 27, 28, 28, 29, 29, 29, 30, 30, 30, 31, 31, 31, 32, 32, 32, 32, 33, 33, 33, 34, 34, 34, 35, 35, 35, 36, 36, 36, 36, 37, 37, 37, 38, 38, 38, 38, 39, 39, 39, 39, 40, 40, 40, 40, 41, 41, 41, 41, 42, 42, 42, 42, 43, 43, 43, 43, 44, 44, 44, 44, 45]
const HEAD_WIDE := [0, -1, 0, 1, -1, -1, -1, 1, -1, -2, -1, 2, -1, -1, 0, 1, -1, 0, -1, 1, -2, -1, -1, 2, -1, 0, 1, 0, -1, 0, 0, 1, 1, 1, -1, -1, -1, 0, 1, 1, -1, 1, 0, 1, -2, 1, 1, 2, -1, 0, 0, 1, -1, 1, 1, 1, -2, 1, 2, 1, 1, 0, -1, 1, 1, 1, 0, 1, 2, 1, -1, 2, 1, 0, -1, 0, 1, 0, 0, 1, -1, 1, 1, -1, 1, -1, 0, 1, 1, 0, 1, 1, 2, -1, 1, 2, 0, -1, 0, 1, 1, -1, 1, 1, 1, -2, 1, 2, 1, 1, 0, -1, 1, 0, 1, -1, 2, 1, 1, -2, 1, 0, -1, 0, 1, 0, 0, -1, -1, -1, 1, 1, 1, 0, -1, -1, 1, -1, 0, -1, 2, -1, -1, -2, 1, 0, -1, -1, -1, -1, 1, -1, 2, -1, -1, -2, -1, 0, 1, -1, -1, -1, 0, -1, -2, -1, 1, -2, -1, 0, 1, 0, -1, 0, 0, -1, 1, -1, -1, 1, -1, 1, 0, -1, -1, 0, -1, -1, -2, 1, -1, -2]
const TAIL_WIDE := [0, 1, 0, -1, 1, -1, 1, 1, 1, 2, 1, -2, 0, -1, 1, 1, 1, 0, 1, -1, 1, -2, 2, 1, -1, 0, 1, 0, 1, 0, 0, -1, 1, 1, -1, -1, -1, -1, 1, 0, 1, -1, 0, -1, -1, -2, 2, -1, -1, -1, 1, 0, -1, -1, 1, -1, -1, -2, 2, -1, 1, -1, -1, 0, -1, -1, 0, -1, 1, -2, -2, -1, 1, 0, -1, 0, -1, 0, 0, -1, -1, 1, 1, -1, 0, -1, -1, 1, -1, 0, -1, -1, -1, -2, -2, 1, 0, 1, 0, -1, -1, -1, -1, 1, -1, 2, -1, -2, 0, 1, -1, -1, -1, 0, -1, 1, -1, 2, -2, -1, 1, 0, -1, 0, -1, 0, 0, 1, -1, -1, 1, 1, 1, 1, -1, 0, -1, 1, 0, 1, 1, 2, -2, 1, 0, 1, -1, 0, -1, 1, 1, 1, 2, 1, -2, 1, -1, 1, 1, 0, 1, 1, 0, 1, -1, 2, 2, 1, -1, 0, 1, 0, 1, 0, 0, 1, 1, -1, -1, 1, 0, 1, 1, -1, 1, 0, 1, 1, 1, 2, 2, -1]

## The strands' edge: faded over one screen pixel whatever the zoom (the Compatibility renderer
## has no 2D MSAA), measured from the quad's UV with screen-space derivatives.
const EDGE_SHADER := """
shader_type canvas_item;
void fragment() {
	vec4 c = COLOR; // already the strand's texture times its vertex colour
	float across = min(UV.y, 1.0 - UV.y) / max(fwidth(UV.y), 1e-5);
	float along = min(UV.x, 1.0 - UV.x) / max(fwidth(UV.x), 1e-5);
	COLOR = vec4(c.rgb, c.a * clamp(across + 0.5, 0.0, 1.0) * clamp(along + 0.5, 0.0, 1.0));
}
"""


## The material a CanvasItem drawing beams should carry (antialiased strand edges).
static func edge_material() -> ShaderMaterial:
	var shader := Shader.new()
	shader.code = EDGE_SHADER
	var material := ShaderMaterial.new()
	material.shader = shader
	return material


const STROKE_HALF_WIDTH := 0.5625 # world (1x) pixels
# Colours blend along a strand (owner, 2026-11-21: "blended looks better").
const TEXTURE_FILTER := CanvasItem.TEXTURE_FILTER_LINEAR

var strands: Array = [] # {a: Vector2i, b: Vector2i, colors: PackedInt32Array} (palette indices, in Plasma_Line order)
var dots: Array = [] # {at: Vector2i, color: int}
var rng := RandomNumberGenerator.new()
var _textures: Array = [] # kept while drawn: a canvas command doesn't keep its texture alive


## angle.cpp Get_Angle: whole degrees, 0 along +x, counter to the screen's y.
static func get_angle(dx: int, dy: int) -> int:
	if dx == 0:
		return 270 if dy < 0 else (90 if dy > 0 else 0)
	if dy == 0:
		return 180 if dx < 0 else 0
	var nx: int
	var ny: int
	var quadrant: int
	if dx < 0:
		if dy < 0:
			nx = -dx; ny = -dy; quadrant = 180
		else:
			nx = dy; ny = -dx; quadrant = 90
	else:
		if dy > 0:
			nx = dx; ny = dy; quadrant = 0
		else:
			nx = -dy; ny = dx; quadrant = 270
	var angle: int
	if ny == nx:
		angle = 45
	elif ny > nx:
		angle = 90 - ATAN[(nx << 7) / ny]
	else:
		angle = ATAN[(ny << 7) / nx]
	angle += quadrant
	return 0 if angle == 360 else angle


## The game's Random(n), as Godot's: 0 .. n-1 (close enough for jitter).
func _random(n: int) -> int:
	return rng.randi_range(0, maxi(n, 1) - 1)


func bolt_segment(x1: int, y1: int, x2: int, y2: int, color_base: int, color_range: int, jitter_chance: int, jitter_range: int,
		min_color: int, max_color: int, apply_sparkles: int, sparkle_phase: int, tip_highlight: int) -> void:
	var range2 := jitter_range * 2 + 1
	var max_d := maxi(absi(x2 - x1), absi(y2 - y1))
	var length := max_d + 1
	var step := (color_range << 16) / length
	var accum := 32000 / length
	var rand_val := 0
	var buf := PackedInt32Array()
	buf.resize(length + 2)
	if y2 < y1:
		for i in length + 1:
			buf[length - i + 1] = ((accum >> 16) + rand_val + color_base) & 0xFF
			accum += step
			rand_val = _random(range2) - jitter_range if _random(100) < jitter_chance else 0
		buf[length] = color_base & 0xFF
		buf[0] = (color_base + color_range - 1) & 0xFF
	else:
		for i in length + 1:
			buf[i + 1] = ((accum >> 16) + rand_val + color_base) & 0xFF
			accum += step
			rand_val = _random(range2) - jitter_range if _random(100) < jitter_chance else 0
		buf[0] = color_base & 0xFF
		buf[length] = (color_base + color_range - 1) & 0xFF
	if apply_sparkles == 1:
		var i := sparkle_phase % 8
		while i < length + 2:
			buf[i] = (buf[i] + 3) & 0xFF
			i += 8
	for i in length + 2:
		buf[i] = clampi(buf[i], min_color, max_color)
	if tip_highlight != 0:
		var fade := 3 / tip_highlight
		if y2 < y1:
			for i in 6:
				buf[i + 1] = (buf[i + 1] + fade) & 0xFF
		elif length > 5:
			for i in 6:
				buf[length - i - 1] = (buf[length - i - 1] + fade) & 0xFF
	for i in length + 2:
		buf[i] = mini(buf[i], color_base + 15)
	strands.append({"a": Vector2i(x1, y1), "b": Vector2i(x2, y2), "colors": buf})


func _dot(x: int, y: int, color: int) -> void:
	dots.append({"at": Vector2i(x, y), "color": color})


func bolt_line(x1: int, y1: int, x2: int, y2: int, palette_base: int, palette_span: int, jitter_chance: int, jitter_range: int,
		sparkle_phase: int, draw_caps: int, tip_highlight: int, full_core_gradient: int, apply_sparkles: int, suppress_side: int,
		core_max: int, core_min: int, side_max: int, side_min: int) -> void:
	if draw_caps == 1:
		var cross := palette_base + 1
		var diag := palette_base
		if tip_highlight == 1:
			cross = palette_base + 4
			diag = palette_base + 1
		var centre := core_max + palette_base
		for end in ([Vector2i(x2, y2)] if full_core_gradient != 0 else [Vector2i(x2, y2), Vector2i(x1, y1)]):
			_dot(end.x, end.y, centre)
			for o in [Vector2i(0, -1), Vector2i(0, 1), Vector2i(-1, 0), Vector2i(1, 0)]:
				_dot(end.x + o.x, end.y + o.y, cross)
			for o in [Vector2i(-1, -1), Vector2i(-1, 1), Vector2i(1, -1), Vector2i(1, 1)]:
				_dot(end.x + o.x, end.y + o.y, diag)
	var x_off := 0
	var y_off := 0
	if absi(y2 - y1) >= absi(x2 - x1):
		x_off = 1
	else:
		y_off = 1
	var flank_range := side_max - side_min + (3 if tip_highlight == 1 else 0)
	if suppress_side == 0:
		bolt_segment(x1 - x_off, y1 - y_off, x2 - x_off, y2 - y_off, side_min + palette_base, flank_range, jitter_chance, jitter_range / 2,
			palette_base, palette_span + palette_base - 1, apply_sparkles, sparkle_phase, 0)
		bolt_segment(x1 + x_off, y1 + y_off, x2 + x_off, y2 + y_off, side_min + palette_base, flank_range, jitter_chance, jitter_range / 2,
			palette_base, palette_span + palette_base - 1, apply_sparkles, sparkle_phase, 0)
	var core_range := core_max - core_min
	var core_base := core_min + palette_base
	if full_core_gradient == 0:
		core_base += (core_range * 3) / 4
		core_range = 0
	bolt_segment(x1, y1, x2, y2, core_base, core_range, jitter_chance, jitter_range, palette_base, palette_span + palette_base - 1,
		apply_sparkles, sparkle_phase, tip_highlight)


func wide_bolt_line(x1: int, y1: int, x2: int, y2: int, palette_base: int, palette_span: int, jitter_chance: int, jitter_range: int,
		sparkle_phase: int, point_source_end: int, tip_highlight: int, full_core_gradient: int, apply_sparkles: int,
		core_max: int, core_min: int, mid_max: int, mid_min: int, outer_max: int, outer_min: int) -> void:
	var angle := get_angle(x2 - x1, y2 - y1)
	var d := (((371 - angle) % 360) * 2) / 45
	var max_color := palette_base + palette_span - 1
	var side_range := jitter_range / 2
	# Strands 8-9 and 10-11 of the direction's 12 offsets: the outer pair; 0-7: the four mid strands.
	for pair in [[8, outer_min, outer_max, 0], [10, outer_min, outer_max, 0], [0, mid_min, mid_max, tip_highlight],
			[2, mid_min, mid_max, tip_highlight], [4, mid_min, mid_max, tip_highlight], [6, mid_min, mid_max, tip_highlight]]:
		var k: int = pair[0]
		var sx := x1
		var sy := y1
		if point_source_end != 1:
			sx = TAIL_WIDE[d * 12 + k] + x1
			sy = TAIL_WIDE[d * 12 + k + 1] + y1
		var ex: int = HEAD_WIDE[d * 12 + k] + x2
		var ey: int = HEAD_WIDE[d * 12 + k + 1] + y2
		bolt_segment(sx, sy, ex, ey, palette_base + int(pair[1]), int(pair[2]) - int(pair[1]), jitter_chance, side_range,
			palette_base, max_color, apply_sparkles, sparkle_phase, int(pair[3]))
	var core_base := core_min + palette_base
	var core_range := core_max - core_min
	if full_core_gradient == 0:
		core_base += (core_range * 3) / 4
		core_range = 0
	bolt_segment(x1, y1, x2, y2, core_base, core_range, jitter_chance, jitter_range, palette_base, max_color, apply_sparkles,
		sparkle_phase, tip_highlight)


## beams.cpp Draw_Beam2: one frame of a recipe weapon, by its style (0 beam, 1 bolt, 2 arc, 3 wide beam).
## `start`/`end`: shooter and target; `from`/`to`: the segment this frame.
func draw_beam2(style: int, start: Vector2i, end: Vector2i, from: Vector2i, to: Vector2i, frame_index: int, palette_base: int,
		sfx_sprite_id: int, wide_bolt_size_bonus: int) -> void:
	if from == to or style >= 4:
		return
	var mod_thick := frame_index % 2
	match style:
		0:
			var a16 := 12
			var a17 := 3
			var a := from
			var b := to
			if frame_index == 0:
				a = to
				b = from
				a16 = 10
			elif frame_index == 1:
				a17 = 6
				a16 = 10
			bolt_line(a.x, a.y, b.x, b.y, palette_base, 12, 30, 3, frame_index, 0, 1, 1, 0, 0, a16, a17, 4, 0)
		1:
			var size := wide_bolt_size_bonus * 2 + 7
			wide_bolt_line(from.x, from.y, to.x, to.y, palette_base, 12, 50, 3, frame_index, 1, 1 if sfx_sprite_id >= 10 else 0, 1, 0,
				12, 3, size, 1, 2, 0)
		2:
			var div := mod_thick + 1
			var rx := _random(5) + end.x - 3
			var ry := _random(5) + end.y - 3
			bolt_line(start.x, start.y, rx, ry, palette_base, 12, 100, 4, frame_index * 4, 0, 0, 0, 1, 0, 12 / div, 8 / div, 4 / div, 0)
		3:
			var a20 := 2 / (mod_thick + 1)
			var a18 := 5 / (mod_thick * 2 + 1) + 1
			var a16 := 21 / (mod_thick + 2)
			wide_bolt_line(start.x, start.y, end.x, end.y, palette_base, 16, 100, 3, frame_index * 2, 0, 0, 0, 1,
				a16, a16, a18, a18, a20, a20)


## Draws the strands and dots on `item` (world units = the game's 1x pixels): each strand a quad one
## pixel wide through its end pixels' centres (extended half a pixel at each end), coloured along
## its length from `ramp` (Color per palette offset from `palette_base`). The item's texture_filter
## should be TEXTURE_FILTER (blended along the strand: the owner's choice, 2026-11-21); NEAREST
## would show the original's per-pixel colour steps.
func draw(item: CanvasItem, ramp: Array, palette_base: int) -> void:
	for s: Dictionary in strands:
		var a: Vector2i = s.a
		var b: Vector2i = s.b
		# Plasma_Line's order: from the end with the smaller y (from x1 when level).
		var first := a
		var last := b
		if a.y > b.y:
			first = b
			last = a
		var count := maxi(absi(b.x - a.x), absi(b.y - a.y)) + 1
		var colors: PackedInt32Array = s.colors
		var image := Image.create(count, 1, false, Image.FORMAT_RGBA8)
		for k in count:
			image.set_pixel(k, 0, ramp[clampi(colors[k] - palette_base, 0, ramp.size() - 1)])
		var texture := ImageTexture.create_from_image(image)
		_textures.append(texture)
		var p0 := Vector2(first) + Vector2(0.5, 0.5)
		var p1 := Vector2(last) + Vector2(0.5, 0.5)
		var dir := (p1 - p0).normalized() if p1 != p0 else Vector2.RIGHT
		# The twins' stroke is fully covered to half a pixel from its line and fades out by 0.625
		# (S = 4: half + 0.5 sub-pixel), so neighbouring strands overlap instead of leaving seams:
		# the quad's edge (where the shader's fade is half) sits at 0.5625.
		var n := Vector2(-dir.y, dir.x) * STROKE_HALF_WIDTH
		var s0 := p0 - dir * STROKE_HALF_WIDTH
		var s1 := p1 + dir * STROKE_HALF_WIDTH
		item.draw_polygon(PackedVector2Array([s0 - n, s1 - n, s1 + n, s0 + n]), PackedColorArray([Color.WHITE, Color.WHITE, Color.WHITE, Color.WHITE]),
			PackedVector2Array([Vector2(0, 0), Vector2(1, 0), Vector2(1, 1), Vector2(0, 1)]), texture)
	for d: Dictionary in dots:
		var at: Vector2i = d.at
		item.draw_rect(Rect2(Vector2(at), Vector2.ONE), ramp[clampi(int(d.color) - palette_base, 0, ramp.size() - 1)])
