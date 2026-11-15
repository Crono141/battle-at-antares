extends TextureRect
## A picture from the game's art (an LBX entry, HD when a pack has it). Set `lbx` and `entry` in
## the editor or in code; the view that owns it calls `bind(art)` once the art service is known.
## Until the art arrives the rect stays empty.

@export var lbx := ""
@export var entry := -1:
	set(value):
		entry = value
		_refresh()
@export var frame := 0
## The entry's whole frame at one scale, the picture where the game draws it in its frame (owner,
## 2026-11-15: a Frigate is small in its 52x48 ship frame and a Titan fills it; stretching each
## picture to the rect drew every hull the same size). Off: the picture itself fills the rect.
@export var whole_frame := false:
	set(value):
		whole_frame = value
		_refresh()

var _art: Node


func bind(art: Node) -> void:
	if _art == art:
		return
	_art = art
	if art and not art.loaded.is_connected(_on_loaded):
		art.loaded.connect(_on_loaded)
	_refresh()


func show_sprite(lbx_name: String, entry_number: int) -> void:
	lbx = lbx_name
	entry = entry_number


func _on_loaded(key: String) -> void:
	# Its first picture, or its own entry again (HD art that was still decoding, art.gd).
	if texture == null or key == "%s/%d" % [lbx, entry]:
		_refresh()


func _refresh() -> void:
	if whole_frame:
		texture = null
		queue_redraw()
		return
	if _art == null or lbx == "" or entry < 0:
		texture = null
		return
	texture = _art.texture(lbx, entry, frame)


func _notification(what: int) -> void:
	if what == NOTIFICATION_RESIZED and whole_frame:
		queue_redraw()


func _draw() -> void:
	if not whole_frame or _art == null or lbx == "" or entry < 0:
		return
	var s: Dictionary = _art.sprite(lbx, entry)
	if s.is_empty():
		return
	var frame_size: Vector2 = s.size
	var scale := minf(size.x / frame_size.x, size.y / frame_size.y)
	_art.draw(self, lbx, entry, frame, (size - frame_size * scale) * 0.5, scale)


## Binds every SpriteRect under `root` (views call this when their art is set).
static func bind_all(root: Node, art: Node) -> void:
	for node in root.find_children("*", "TextureRect", true, false):
		if node.has_method("show_sprite"):
			node.bind(art)
