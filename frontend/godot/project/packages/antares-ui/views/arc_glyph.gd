extends Control
## A firing arc as the mockup's glyph (the original's arc pictures, DESIGN.LBX 33-37): a dial with
## the ship's arrow and the arc lit; 0 forward, 1 forward extended, 2 back extended, 3 back, 4 360.
## The Ship Design panel's weapon rows and the parts list's arc buttons and dial.

var arc := 0:
	set(value):
		arc = value
		queue_redraw()

const SPANS := [[-0.75, -0.25], [-1.125, 0.125], [-0.125, 1.125], [0.25, 0.75], [0.0, 2.0]]


func _draw() -> void:
	var r := minf(size.x, size.y) * 0.5 - 1.0
	var c := size * 0.5
	draw_circle(c, r, Color(0.03, 0.04, 0.08))
	var span: Array = SPANS[clampi(arc, 0, 4)]
	var points := PackedVector2Array()
	if arc != 4:
		points.append(c)
	var steps := 24
	for i in steps + 1:
		var a := PI * (float(span[0]) + (float(span[1]) - float(span[0])) * float(i) / float(steps))
		points.append(c + Vector2(cos(a), sin(a)) * r)
	draw_colored_polygon(points, Color(0.16, 0.36, 0.84))
	draw_arc(c, r, 0.0, TAU, 32, Color(0.22, 0.27, 0.37), 1.0, true)
	var s := r * 0.45
	draw_colored_polygon(PackedVector2Array([c + Vector2(-s * 0.55, s * 0.6), c + Vector2(0, -s), c + Vector2(s * 0.55, s * 0.6)]), Color(0.79, 0.82, 0.86))
