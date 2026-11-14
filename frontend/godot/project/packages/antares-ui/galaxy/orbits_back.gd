extends Node2D
## The open system's back layer, under the stars (owner, 2026-11-14): the orbit rings and the
## planets on the far half of their orbits, so the star hides them. Orbits (orbits.gd) draws it and
## owns the fade; this node follows its alpha.

@onready var orbits: Node2D = get_parent().get_node("Orbits")


func _process(_delta: float) -> void:
	if modulate.a != orbits.modulate.a:
		modulate.a = orbits.modulate.a
	if not orbits.galaxy.system.is_empty() or modulate.a > 0.0:
		queue_redraw()


func _draw() -> void:
	orbits.draw_layer(self, true)
