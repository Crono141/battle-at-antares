extends "drop_panel.gd"
## Command points (WP3.1b; a drop-down from the bar's Command figure, WP3.3g): the summary the
## legacy text box shows, from the "command_points" query. Opened from the bar it stays on the
## galaxy; opened as the game's screen (SCREEN_SHOW_COMMAND_POINTS, from elsewhere) closing it
## sends "close", back to the screen that opened it.

signal command(dict: Dictionary)

## Opened as the game's screen, not from the bar.
var as_screen := false


func _ready() -> void:
	super._ready()
	$Fit/Box/Header/Close.pressed.connect(close)
	closed.connect(func():
		if as_screen:
			as_screen = false
			command.emit({"verb": "close"}))


func show_result(result: Dictionary) -> void:
	$Fit/Box/Header/Title.text = result.get("title", "Command points")
	$Fit/Box/Text.text = result.get("text", "")
	$Fit/Box/Total.text = "%d of %d command points used" % [
		int(result.get("command_points_used", 0)), int(result.get("command_points", 0))]
