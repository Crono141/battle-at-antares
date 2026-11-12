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


func _on_loaded(_key: String) -> void:
	if texture == null:
		_refresh()


func _refresh() -> void:
	if _art == null or lbx == "" or entry < 0:
		texture = null
		return
	texture = _art.texture(lbx, entry, frame)


## Binds every SpriteRect under `root` (views call this when their art is set).
static func bind_all(root: Node, art: Node) -> void:
	for node in root.find_children("*", "TextureRect", true, false):
		if node.has_method("show_sprite"):
			node.bind(art)
