extends "drop_panel.gd"
## The tax rate (owner, 2026-11-13: a click on the treasury drops this down, as command points and
## research do; it was a menu opened by hovering, which got in the way of the treasury's help):
## the game's six rates, the one in force armed; a click sets it (`set_tax_rate`) and closes.

signal command(dict: Dictionary)
## A right click's help (the tax rate popup's record), shown by the frontend.
signal help_requested(topic: int, at: Control)

const Themes := preload("../themes.gd")
const HELP_TAX_RATE := 560

@onready var rates: VBoxContainer = $Fit/Box/Rates


func _ready() -> void:
	super._ready()
	var group := ButtonGroup.new()
	for rate in [0, 10, 20, 30, 40, 50]:
		var b := Button.new()
		b.text = "Tax %d%%" % rate
		b.toggle_mode = true
		b.button_group = group
		b.focus_mode = Control.FOCUS_NONE
		b.custom_minimum_size.y = 32
		b.set_meta("rate", rate)
		b.pressed.connect(func():
			command.emit({"verb": "set_tax_rate", "rate": rate})
			close())
		Themes.glowing(b)
		rates.add_child(b)
	gui_input.connect(_on_right_click)
	for b in rates.get_children():
		(b as Control).gui_input.connect(_on_right_click)


func _on_right_click(event: InputEvent) -> void:
	var mb := event as InputEventMouseButton
	if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_RIGHT:
		help_requested.emit(HELP_TAX_RATE, self)
		accept_event()


## The rate in force, armed.
func show_rate(rate: int) -> void:
	for b: Button in rates.get_children():
		b.set_pressed_no_signal(int(b.get_meta("rate")) == rate)
