extends Control
## The starfield behind the galaxy: the legacy map's three background layers (STARBG.LBX 0-2,
## far to near), tiled across the window at the legacy map's pixel size and sliding at different
## speeds as the camera moves, as the legacy map's parallax does.

## How far each layer slides per screen pixel the camera moves, far to near.
const SPEEDS := [0.04, 0.08, 0.16]

@export var galaxy_path: NodePath
@onready var galaxy: Node2D = get_node(galaxy_path)


func _process(_delta: float) -> void:
	queue_redraw()


func _draw() -> void:
	draw_rect(Rect2(Vector2.ZERO, size), Color.BLACK)
	if galaxy.art == null:
		return
	var legacy: float = galaxy.legacy_pixel()
	var camera_screen: Vector2 = galaxy.camera.position * galaxy.camera.zoom.x
	for layer in 3:
		var tex: Texture2D = galaxy.art.texture("STARBG.LBX", layer)
		if tex == null:
			continue
		var tile := Vector2(640.0, 480.0) * legacy
		var offset: Vector2 = -camera_screen * float(SPEEDS[layer])
		offset = Vector2(fposmod(offset.x, tile.x), fposmod(offset.y, tile.y)) - tile
		var y: float = offset.y
		while y < size.y:
			var x: float = offset.x
			while x < size.x:
				draw_texture_rect(tex, Rect2(Vector2(x, y), tile), false)
				x += tile.x
			y += tile.y
