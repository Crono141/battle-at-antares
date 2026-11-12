extends StyleBox
## A piece of the original screens' metal (art/, cut by Moo2Mods tools/themeart.py), drawn the way
## the 3.3g mockups drew it (WP3.3g):
## - `caps` > 0: a button. The art's two end caps keep their proportions at the rect's height and
##   the strip between them stretches; `vertical` lays the button along the rect's height (the
##   side tabs).
## - otherwise a nine-slice with `margins` (texture pixels) drawn at `scale`; `center` false
##   leaves the middle empty (frames).
## `fill` is drawn first across the inside (a panel's tech pattern, darkened by `fill_tint`); `grow`
## (left, top, right, bottom) draws past the rect (a pocket reaching under the screen frame).

var texture: Texture2D
var caps := 0
var margins := PackedInt32Array([0, 0, 0, 0]) # left, top, right, bottom
var scale := 1.0
var center := true
var vertical := false
var tint := Color.WHITE
var fill: Texture2D
var fill_tint := Color(0.75, 0.75, 0.75)
var grow := PackedFloat32Array([0.0, 0.0, 0.0, 0.0])


func _draw(to_canvas_item: RID, rect: Rect2) -> void:
	rect = Rect2(rect.position - Vector2(grow[0], grow[1]), rect.size + Vector2(grow[0] + grow[2], grow[1] + grow[3]))
	if fill != null:
		var inner := rect.grow(-maxf(2.0, margins[0] * scale * 0.5))
		RenderingServer.canvas_item_add_texture_rect(to_canvas_item, inner, fill.get_rid(), false, fill_tint)
	if texture == null:
		return
	var rid := texture.get_rid()
	if vertical:
		# Lay the horizontal art along the height: rotated a quarter turn, its top to the left.
		var t := Transform2D(-PI / 2.0, rect.position + Vector2(0.0, rect.size.y))
		RenderingServer.canvas_item_add_set_transform(to_canvas_item, t)
		_draw_flat(to_canvas_item, rid, Rect2(Vector2.ZERO, Vector2(rect.size.y, rect.size.x)))
		RenderingServer.canvas_item_add_set_transform(to_canvas_item, Transform2D.IDENTITY)
	else:
		_draw_flat(to_canvas_item, rid, rect)


func _draw_flat(ci: RID, rid: RID, rect: Rect2) -> void:
	var tw := float(texture.get_width())
	var th := float(texture.get_height())
	if caps > 0:
		var s := rect.size.y / th
		var cw := minf(caps * s, rect.size.x * 0.5)
		var cap_src := cw / s
		_region(ci, rid, Rect2(rect.position, Vector2(cw, rect.size.y)), Rect2(0, 0, cap_src, th))
		_region(ci, rid, Rect2(rect.position + Vector2(cw, 0), Vector2(rect.size.x - 2.0 * cw, rect.size.y)), Rect2(caps, 0, tw - 2.0 * caps, th))
		_region(ci, rid, Rect2(rect.position + Vector2(rect.size.x - cw, 0), Vector2(cw, rect.size.y)), Rect2(tw - cap_src, 0, cap_src, th))
		return
	var l := margins[0]
	var t := margins[1]
	var r := margins[2]
	var b := margins[3]
	var dl := minf(l * scale, rect.size.x * 0.5)
	var dr := minf(r * scale, rect.size.x * 0.5)
	var dt := minf(t * scale, rect.size.y * 0.5)
	var db := minf(b * scale, rect.size.y * 0.5)
	var xs := [[0.0, l, rect.position.x, dl], [l, tw - r, rect.position.x + dl, rect.size.x - dl - dr], [tw - r, tw, rect.end.x - dr, dr]]
	var ys := [[0.0, t, rect.position.y, dt], [t, th - b, rect.position.y + dt, rect.size.y - dt - db], [th - b, th, rect.end.y - db, db]]
	for ci_x in 3:
		for ci_y in 3:
			if ci_x == 1 and ci_y == 1 and not center:
				continue
			var cx: Array = xs[ci_x]
			var cy: Array = ys[ci_y]
			if cx[3] <= 0.0 or cy[3] <= 0.0:
				continue
			_region(ci, rid, Rect2(cx[2], cy[2], cx[3], cy[3]), Rect2(cx[0], cy[0], cx[1] - cx[0], cy[1] - cy[0]))


func _region(ci: RID, rid: RID, dest: Rect2, src: Rect2) -> void:
	RenderingServer.canvas_item_add_texture_rect_region(ci, dest, rid, src, tint)
