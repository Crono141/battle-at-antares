extends PanelContainer
## Native answer to the game's message boxes (HAROLD::User_Box, WP3.1b): "message" and "warning"
## have OK, "confirm" has Yes and No. The game thread waits until answered.

signal answered(id: int, value: int)

var request_id := 0


func _ready() -> void:
	$Box/Buttons/Yes.pressed.connect(func(): _answer(1))
	$Box/Buttons/No.pressed.connect(func(): _answer(0))


func ask(id: int, kind: String, text: String) -> void:
	request_id = id
	$Box/Text.text = text
	$Box/Title.text = "Warning" if kind == "warning" else ("Confirm" if kind == "confirm" else "")
	$Box/Title.visible = $Box/Title.text != ""
	$Box/Buttons/Yes.text = "  Yes  " if kind == "confirm" else "  OK  "
	$Box/Buttons/No.visible = kind == "confirm"
	show()


func _answer(value: int) -> void:
	hide()
	answered.emit(request_id, value)
