## Spike 3.9, native beams test: draws the recorded beam calls of captured volleys with the Godot
## port (packages/antares-ui/views/beam_draw.gd) beside the captured frames.
##
##   Godot --path project -s res://tests/beam_compare.gd -- <dump dir> <out dir>
##
## For every w_volley<N>_beams.json in the dump dir and each captured frame
## w_volley<N>_frame<K>_at_<X>_<Y>.png (world position X,Y; 4 plane pixels per world pixel), writes
## native_v<N>_f<K>_4x.png (same size and place as the capture) and, around the beam's middle,
## native_v<N>_f<K>_16x_stepped.png and _16x_blended.png (the strands' edges are computed at that
## scale, so this shows "crisp at any zoom").
extends SceneTree

const BeamDraw := preload("res://packages/antares-ui/views/beam_draw.gd")
const ZOOM := 16
const CROP := Vector2i(48, 24) # world pixels shown at 16x


class Painter extends Node2D:
	var beams: Array = [] # [{beam: BeamDraw, ramp: Array, base: int}]

	func _draw() -> void:
		for b: Dictionary in beams:
			b.beam.draw(self, b.ramp, b.base)


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.size() < 2:
		push_error("usage: -- <dump dir> <out dir>")
		quit(1)
		return
	_run.call_deferred(args[0], args[1], args[2].split(",") if args.size() > 2 else PackedStringArray())


func _render(size: Vector2i, origin: Vector2, scale: float, beams: Array, filter: CanvasItem.TextureFilter) -> Image:
	var viewport := SubViewport.new()
	viewport.size = size
	viewport.transparent_bg = true
	viewport.render_target_update_mode = SubViewport.UPDATE_ONCE
	var painter := Painter.new()
	painter.beams = beams
	painter.texture_filter = filter
	painter.material = BeamDraw.edge_material()
	painter.scale = Vector2(scale, scale)
	painter.position = -origin * scale
	viewport.add_child(painter)
	root.add_child(viewport)
	await process_frame
	await process_frame
	await RenderingServer.frame_post_draw
	var image := viewport.get_texture().get_image()
	viewport.queue_free()
	return image


func _run(dump: String, out: String, only: PackedStringArray) -> void:
	DirAccess.make_dir_recursive_absolute(out)
	var files := DirAccess.get_files_at(dump)
	for file in files:
		if not file.ends_with("_beams.json"):
			continue
		var volley := file.trim_prefix("w_volley").trim_suffix("_beams.json")
		if not only.is_empty() and not only.has(volley):
			continue
		var data: Dictionary = JSON.parse_string(FileAccess.get_file_as_string(dump + "/" + file))
		var by_frame := {}
		for call: Dictionary in data.calls:
			var k := int(call.frame)
			if not by_frame.has(k):
				by_frame[k] = []
			by_frame[k].append(call)
		for capture in files:
			if not capture.begins_with("w_volley%s_frame" % volley) or not capture.ends_with(".png"):
				continue
			var parts := capture.trim_suffix(".png").split("_")
			var k := int(parts[2].trim_prefix("frame"))
			if not by_frame.has(k):
				continue
			var at := Vector2(float(parts[4]), float(parts[5]))
			var shot := Image.load_from_file(dump + "/" + capture)
			var beams: Array = []
			var middle := Vector2.ZERO
			for call: Dictionary in by_frame[k]:
				var beam := BeamDraw.new()
				beam.rng.seed = hash([volley, k, call.from, call.to])
				var v := func(a: Array) -> Vector2i: return Vector2i(int(a[0]), int(a[1]))
				beam.draw_beam2(int(call.style), v.call(call.start), v.call(call.end), v.call(call.from), v.call(call.to), int(call.frame_index),
					int(call.palette_base), int(call.sfx_sprite_id), int(call.wide_bolt_size_bonus))
				var ramp: Array = []
				for c: Array in call.ramp:
					ramp.append(Color8(int(c[0]), int(c[1]), int(c[2])))
				beams.append({"beam": beam, "ramp": ramp, "base": int(call.palette_base)})
				middle = (Vector2(v.call(call.from)) + Vector2(v.call(call.to))) * 0.5
			var four := await _render(shot.get_size(), at, 4.0, beams, CanvasItem.TEXTURE_FILTER_NEAREST)
			four.save_png("%s/native_v%s_f%02d_4x.png" % [out, volley, k])
			var crop_origin := middle - Vector2(CROP) * 0.5
			for mode in [["stepped", CanvasItem.TEXTURE_FILTER_NEAREST], ["blended", CanvasItem.TEXTURE_FILTER_LINEAR]]:
				var big := await _render(CROP * ZOOM, crop_origin, ZOOM, beams, mode[1])
				big.save_png("%s/native_v%s_f%02d_16x_%s.png" % [out, volley, k, mode[0]])
			# The capture's same crop, enlarged from its 4x pixels (what the bitmap gives at 16x).
			var cx := int((crop_origin.x - at.x) * 4.0)
			var cy := int((crop_origin.y - at.y) * 4.0)
			var crop := Image.create(CROP.x * 4, CROP.y * 4, false, Image.FORMAT_RGBA8)
			crop.blit_rect(shot, Rect2i(cx, cy, CROP.x * 4, CROP.y * 4), Vector2i.ZERO)
			crop.resize(CROP.x * ZOOM, CROP.y * ZOOM, Image.INTERPOLATE_NEAREST)
			crop.save_png("%s/capture_v%s_f%02d_16x.png" % [out, volley, k])
			print("beam_compare: volley %s frame %d: %d beam call(s) drawn" % [volley, k, beams.size()])
	quit()
