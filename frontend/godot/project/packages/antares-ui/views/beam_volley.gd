## Native beams (WP3.9 stage 1): the original's beam volley as a list of ticks to draw, ported from
## beams.cpp so the frontend draws what the legacy screen drew, tick for tick:
##   Beam_SFX         the two channels taking turns through the volley's entries, one tick each
##                    55 ms (Release_Time(1)), the 0x80 modifier's three plays, the shield's
##                    display states;
##   Fire_Weapon      each weapon's look (the recipe table for the "Steve stuff" weapons, the bolt
##                    parameters and palette fragment for the others), modifiers;
##   Draw_Generic_Beam the segment on each frame (head, tail, the stop at a holding shield), the
##                    muzzle burst, the hit explosion, ship-hit and shield-hit flags;
##   Set_Beam_Colors  a recipe weapon's 16-colour ramp.
## Compared with the game itself by tests/beam_bench_compare.gd (src/antares/beam_bench.cpp).
## The reflected beam (the reflection field) is not ported yet: its entries are skipped.
extends RefCounted

const TICK := 0.055 # seconds: Release_Time(1)
const SHIELD_DIAMETERS := [25, 46, 63, 76, 117, 155, 193, 215]
const SHIP_TO_SHIELD_SIZE := [0, 1, 1, 2, 3, 3]
const MAX_PLAYERS := 8

const MASS_DRIVER := 1
const GAUSS_CANNON := 2
const LASER_CANNON := 3
const PARTICLE_BEAM := 4
const FUSION_BEAM := 5
const ION_PULSE_CANNON := 6
const GRAVITON_BEAM := 7
const NEUTRON_BLASTER := 8
const PHASOR := 9
const DISRUPTER := 10
const DEATH_RAY := 11
const PLASMA_CANNON := 12
const SPATIAL_COMPRESSOR := 13
const MAULER_DEVICE := 27
const DRAGON_BREATH := 40
const PHASOR_EYE := 41
const CRYSTAL_RAY := 42
const PLASMA_BREATH := 43
const STELLAR_BEAM := 46 # weapon 46 (0x2e): drawn as a beam whatever its type

const RECIPE_WEAPONS := [MASS_DRIVER, GAUSS_CANNON, LASER_CANNON, PARTICLE_BEAM, FUSION_BEAM, ION_PULSE_CANNON, GRAVITON_BEAM,
	NEUTRON_BLASTER, PHASOR, DISRUPTER, DEATH_RAY, PLASMA_CANNON, PHASOR_EYE, CRYSTAL_RAY]

# Set_Beam_Colors' tables (beams.cpp).
const LOW_RAMP := [7, 25, 35, 45, 55, 70, 89, 100]
const PRIMARY_CROSS := [[80, 60, 40, 20], [90, 80, 60, 40], [95, 85, 70, 50]]
const SATURATED := [[110, 125, 155, 250], [115, 140, 200, 250], [150, 200, 300, 300]]


## Fire_Weapon's switch: the bolt parameters per weapon (only `burst`, `multi` and `sound` reach
## the drawing for recipe weapons; their look is recipe()).
## Keys: frag (palette fragment), dur (segment length), step, burst, ball (mauler ball), jc, jr
## (colour jitter chance and range), sparkle_phase, caps, point_end, tip, full, sparkles,
## suppress, reverse, mode (0 beam, 1 bolt, 2 wide), core_max, core_min, outer_max, outer_min,
## sound, multi (three draws a tick).
static func weapon_params(weapon: int) -> Dictionary:
	var p := {"frag": 0, "dur": 0, "step": 1, "burst": 0, "ball": 0, "jc": 0, "jr": 0, "sparkle_phase": 0, "caps": 0, "point_end": 0,
		"tip": 0, "full": 0, "sparkles": 0, "suppress": 0, "reverse": 0, "mode": 0, "core_max": 0, "core_min": 0, "outer_max": 0,
		"outer_min": 0, "sound": 0, "multi": 0}
	match weapon:
		MASS_DRIVER:
			p.merge({"frag": 7, "dur": 1, "burst": 1, "step": 45, "core_max": 10, "sparkle_phase": 1, "outer_max": 7, "caps": 1,
				"point_end": 1, "tip": 1, "mode": 1, "sound": 12, "multi": 1}, true)
		GAUSS_CANNON:
			p.merge({"frag": 2, "mode": 1, "jc": 40, "jr": 3, "step": 45, "dur": 2, "core_max": 10, "outer_max": 7, "burst": 1,
				"sparkle_phase": 1, "caps": 1, "tip": 1, "full": 1, "sound": 15, "multi": 1}, true)
		LASER_CANNON:
			p.merge({"frag": 0, "jc": 10, "dur": 90, "jr": 1, "step": 55, "core_min": 7, "sparkle_phase": 1, "core_max": 10,
				"mode": 0, "sparkles": 1, "sound": 1}, true)
		PARTICLE_BEAM:
			p.merge({"dur": 60, "frag": 1, "sparkle_phase": 1, "jr": 3, "jc": 50, "step": 45, "core_max": 15, "core_min": 7,
				"outer_max": 3, "caps": 1, "point_end": 1, "tip": 1, "mode": 1, "sound": 23, "full": 1}, true)
		FUSION_BEAM:
			p.merge({"frag": 8, "jc": 40, "dur": 640, "jr": 5, "core_max": 15, "core_min": 10, "outer_max": 7, "step": 40,
				"caps": 1, "point_end": 1, "tip": 1, "mode": 2, "sound": 9, "full": 1, "sparkles": 1, "reverse": 1}, true)
		ION_PULSE_CANNON:
			p.merge({"dur": 40, "frag": 3, "jr": 3, "mode": 1, "jc": 50, "step": 45, "sparkle_phase": 1, "core_max": 15,
				"core_min": 7, "outer_max": 3, "caps": 1, "point_end": 1, "tip": 1, "sound": 14, "full": 1, "sparkles": 1}, true)
		GRAVITON_BEAM:
			p.merge({"frag": 6, "sparkle_phase": 1, "jc": 30, "dur": 640, "core_max": 7, "jr": 5, "step": 35, "outer_max": 3,
				"core_min": 7, "caps": 1, "tip": 1, "mode": 2, "sound": 11}, true)
		NEUTRON_BLASTER:
			p.merge({"frag": 2, "jr": 3, "dur": 90, "mode": 1, "jc": 30, "step": 45, "core_max": 15, "core_min": 7,
				"sparkle_phase": 1, "outer_max": 3, "caps": 1, "point_end": 1, "tip": 1, "sound": 22, "full": 1}, true)
		PHASOR, PHASOR_EYE:
			p.merge({"frag": 0, "dur": 90, "jc": 15, "mode": 1, "jr": 3, "step": 50, "core_min": 7, "outer_max": 6,
				"outer_min": 2, "sparkle_phase": 1, "core_max": 15, "caps": 1, "tip": 1, "sound": 19, "sparkles": 1}, true)
		DISRUPTER:
			p.merge({"frag": 0, "dur": 40, "jr": 3, "jc": 40, "mode": 1, "step": 45, "core_max": 15, "core_min": 7,
				"sparkle_phase": 1, "outer_max": 3, "caps": 1, "point_end": 1, "tip": 1, "sound": 2, "full": 1}, true)
		DEATH_RAY, CRYSTAL_RAY:
			p.merge({"frag": 6, "jr": 3, "dur": 90, "sparkle_phase": 1, "jc": 40, "step": 35, "core_max": 15, "core_min": 7,
				"outer_max": 3, "caps": 1, "point_end": 1, "tip": 1, "mode": 2, "sound": 21, "full": 1}, true)
		PLASMA_CANNON:
			p.merge({"frag": 0, "dur": 60, "jc": 50, "sound": 13, "jr": 5, "core_min": 10, "outer_max": 8, "step": 50,
				"sparkle_phase": 1, "core_max": 13, "caps": 1, "point_end": 1, "tip": 1, "mode": 2, "full": 1, "sparkles": 1}, true)
		SPATIAL_COMPRESSOR:
			p.merge({"mode": 2, "sparkle_phase": 1, "jc": 30, "jr": 3, "step": 55, "frag": 2, "core_min": 10, "core_max": 15,
				"dur": 40, "outer_max": 10, "caps": 1, "point_end": 1, "tip": 1, "full": 1, "sparkles": 1, "sound": 1}, true)
		MAULER_DEVICE, DRAGON_BREATH, PLASMA_BREATH:
			p.merge({"dur": 20, "frag": 5, "jr": 5, "mode": 1, "jc": 30, "step": 40, "sparkle_phase": 1, "core_max": 15,
				"core_min": 10, "outer_max": 7, "caps": 1, "point_end": 1, "tip": 1, "ball": 1, "full": 1, "sparkles": 1,
				"multi": 1, "sound": 5}, true)
		STELLAR_BEAM:
			p.merge({"frag": 6, "jc": 10, "dur": 40, "sparkle_phase": 1, "jr": 3, "step": 50, "core_max": 15, "outer_min": 7,
				"core_min": 10, "outer_max": 10, "caps": 1, "point_end": 1, "tip": 1, "mode": 2, "sound": 18, "full": 1,
				"sparkles": 1, "suppress": 1}, true)
		_:
			return {}
	return p


## Build_Steve_Stuff_Struct_For_Weapon: a recipe weapon's look. Keys: c1, c2 (the two colours,
## 8-bit), thickness (the ramp's intensity, 0-2), style (Draw_Beam2: 0 beam, 1 bolt, 2 arc,
## 3 wide beam), length (the segment's length; the game's sfx_sprite_id), step (pixels a frame;
## the game's duration), bonus (wide bolt size bonus). Empty for other weapons.
static func recipe(weapon: int, specials: int) -> Dictionary:
	var arc := specials & 0x10 != 0
	var heavy := specials & 0x100 != 0
	match weapon:
		MASS_DRIVER:
			return _recipe([0x59, 0x6b, 0xc3], [0xda, 0xfc, 0xff], 1, 1, 4, 20, 0)
		GAUSS_CANNON:
			return _recipe([0x59, 0x67, 0x6c], [0xb2, 0xc8, 0xc8], 1, 1, 6, 20, 0)
		LASER_CANNON:
			return _recipe([0xc8, 0x14, 0x20], [0xff, 0x50, 0x0a], 1, 2 if arc else 0, 200, 50, 0)
		PARTICLE_BEAM:
			return _recipe([0x5e, 0x70, 0xcd], [0xba, 0xdd, 0xff], 0, 2 if arc else 0, 200, 50, 0)
		FUSION_BEAM:
			if arc and heavy:
				return _recipe([0xd2, 0x46, 0x00], [0xff, 0xa2, 0x00], 1, 3, 100, 25, 1)
			if arc:
				return _recipe([0xd2, 0x46, 0x00], [0xff, 0xa2, 0x00], 1, 2, 120, 30, 0)
			if heavy:
				return _recipe([0xd2, 0x46, 0x00], [0xff, 0xa2, 0x00], 1, 1, 100, 25, 1)
			return _recipe([0xd2, 0x46, 0x00], [0xff, 0xa2, 0x00], 1, 1, 120, 30, 0)
		ION_PULSE_CANNON:
			return _recipe([0x94, 0x9b, 0x49], [0xfe, 0xfe, 0x94], 2, 2 if arc else 1, 200, 50, 0)
		GRAVITON_BEAM:
			return _recipe([0x44, 0x98, 0x8f], [0x93, 0xff, 0xff], 2, 3, 200, 50, 0)
		NEUTRON_BLASTER:
			return _recipe([0x92, 0x53, 0xba], [0xff, 0xb8, 0xff], 1, 2 if arc else 1, 200, 50, 0)
		PHASOR, PHASOR_EYE:
			return _recipe([0xa9, 0x2b, 0x00], [0xff, 0xa6, 0x60], 1, 2 if arc else 0, 200, 50, 0)
		DISRUPTER:
			return _recipe([0x6b, 0xa2, 0x16], [0xf2, 0xff, 0x35], 1, 1, 40, 30, 0)
		DEATH_RAY, CRYSTAL_RAY:
			return _recipe([0xc9, 0x53, 0xc4], [0xff, 0xb8, 0xff], 1, 3, 200, 50, 0)
		PLASMA_CANNON:
			return _recipe([0xd2, 0x53, 0x00], [0xff, 0xff, 0x52], 2, 3 if arc else 1, 100, 30, 1)
	return {}


static func _recipe(c1: Array, c2: Array, thickness: int, style: int, length: int, step: int, bonus: int) -> Dictionary:
	return {"c1": c1, "c2": c2, "thickness": thickness, "style": style, "length": length, "step": step, "bonus": bonus}


## Set_Beam_Colors: a recipe weapon's 16 colours (8-bit; the game keeps them 6-bit, value / 4).
static func recipe_ramp(fx: Dictionary) -> Array:
	var c1: Array = fx.c1
	var c2: Array = fx.c2
	var intensity := clampi(int(fx.thickness), 0, 2)
	var rgb: Array = []
	for i in 7:
		rgb.append([c1[0] * LOW_RAMP[i] / 100, c1[1] * LOW_RAMP[i] / 100, c1[2] * LOW_RAMP[i] / 100])
	rgb.append([c1[0], c1[1], c1[2]])
	var cross := 115
	for i in 4:
		var w: int = PRIMARY_CROSS[intensity][i]
		var scaled := w * cross
		var c := []
		for k in 3:
			c.append(mini((int(c1[k]) * scaled / 100 + int(c2[k]) * (100 - w)) / 100, 255))
		rgb.append(c)
		cross += 15
	rgb.append([c2[0], c2[1], c2[2]])
	for i in 3:
		var s: int = SATURATED[intensity][i]
		rgb.append([mini(int(c2[0]) * s / 100, 255), mini(int(c2[1]) * s / 100, 255), mini(int(c2[2]) * s / 100, 255)])
	var six := []
	for c: Array in rgb:
		six.append([int(c[0]) / 4, int(c[1]) / 4, int(c[2]) / 4])
	return six


## Six-bit game colours to Godot colours (the game's DAC: value * 255 / 63).
static func to_colors(six: Array) -> Array:
	var out := []
	for c: Array in six:
		out.append(Color8(int(c[0]) * 255 / 63, int(c[1]) * 255 / 63, int(c[2]) * 255 / 63))
	return out


## angle.cpp Get_Angle (beam_draw.gd has the port).
static func _angle(dx: int, dy: int) -> int:
	return preload("res://packages/antares-ui/views/beam_draw.gd").get_angle(dx, dy)


## Rob_Interpolate_Line: `p` moved `step` (the larger axis) toward `target`.
static func interpolate(p: Vector2i, target: Vector2i, step: int) -> Vector2i:
	var dx := target.x - p.x
	var dy := target.y - p.y
	if step < 1:
		return p
	var max_dist := maxi(absi(dx), absi(dy))
	if max_dist <= step:
		return target
	if dx == 0:
		return Vector2i(p.x, p.y - step if dy <= 0 else p.y + step)
	if dy == 0:
		return Vector2i(p.x - step if dx <= 0 else p.x + step, p.y)
	var low_x := 0
	var low_y := 0
	var high_x := dx
	var high_y := dy
	var cur_x := dx >> 1
	var cur_y := dy >> 1
	while true:
		var d := maxi(absi(cur_x), absi(cur_y))
		if d == step:
			break
		if d < step:
			low_x = cur_x
			low_y = cur_y
			cur_x += _cdiv(high_x - cur_x, 2)
			cur_y += _cdiv(high_y - cur_y, 2)
		else:
			high_x = cur_x
			high_y = cur_y
			cur_x += _cdiv(low_x - cur_x, 2)
			cur_y += _cdiv(low_y - cur_y, 2)
	return Vector2i(p.x + cur_x, p.y + cur_y)


## C integer division (toward zero) and remainder (sign of the dividend).
static func _cdiv(a: int, b: int) -> int:
	var q := absi(a) / absi(b)
	return q if (a < 0) == (b < 0) else -q


static func _cmod(a: int, b: int) -> int:
	return a - _cdiv(a, b) * b


static func shield_size(size_class: int) -> int:
	return SHIP_TO_SHIELD_SIZE[clampi(size_class, 0, SHIP_TO_SHIELD_SIZE.size() - 1)]


# ---- The volley ----

var volley: Dictionary
var rng := RandomNumberGenerator.new()
## Per channel, the runs' inputs in order (the bench comparison gives the game's): each
## {offset: Vector2i (the aim's offset from the target's centre), points: [[dx, dy]] (fire points)}.
## Else the offset is drawn here as the game draws it and the points are the entry's.
var runs_given := [[], []]

# Beam_SFX's state.
var _max_frames := 0
var _ship_hit_now := [0, 0]
var _firing_now := [0, 0]
var _shield_hit_now := 0
var _pierced := 0
var _shield_status := 0
var _shield_frame := 0
var _shield_frames := 10 # Shield_Anims(0, 0)'s frame count (BEAMS.LBX 67); the volley may say
var _ops: Array = []
var _fire_sounds := {} # slot * 5 + fire point -> true while its firing sound plays (g_beam_fire_sfx_handle)
var _shield_sound := false # g_shield_sfx_handle
const SHIELD_SOUND_TAG := 10


## The strands of one "seg" op, as the game draws them (Draw_Generic_Beam's bolt modes for the
## other weapons, Draw_Beam2's styles for recipe weapons), on a BeamDraw. Palette base 144 + 16 *
## slot. Compared with the game's own strands by tests/beam_bench_compare.gd.
static func strands(op: Dictionary) -> RefCounted:
	var beam = preload("res://packages/antares-ui/views/beam_draw.gd").new()
	var base := 144 + 16 * int(op.slot)
	var a: Vector2i = op.from
	var b: Vector2i = op.to
	if op.has("recipe"):
		var fx: Dictionary = op.recipe
		beam.draw_beam2(int(fx.style), op.start, op.end, a, b, int(op.frame), base, int(fx.length), int(fx.bonus))
	elif a != b:
		var p: Dictionary = op.params
		match int(p.mode):
			1:
				beam.bolt_line(a.x, a.y, b.x, b.y, base, 15, p.jc, p.jr, p.sparkle_phase, p.caps, p.tip, p.full, p.sparkles, p.suppress,
					p.core_max, p.core_min, p.outer_max, p.outer_min)
			2:
				beam.wide_bolt_line(a.x, a.y, b.x, b.y, base, 15, p.jc, p.jr, p.sparkle_phase, p.point_end, p.tip, p.full, p.sparkles,
					p.core_max, p.core_min, (int(p.outer_max) + int(p.core_max)) / 2, (int(p.outer_min) + int(p.core_min)) / 2,
					p.outer_max, p.outer_min)
			_:
				beam.bolt_line(a.x, a.y, b.x, b.y, base, 15, p.jc, p.jr, p.sparkle_phase, p.caps, p.tip, p.full, p.sparkles, p.suppress,
					0, 0, maxi(int(p.outer_max), int(p.core_max)), maxi(int(p.outer_min), int(p.core_min)))
	return beam


## Plans `v`: {entries: [{weapon, count, result, special, hits, specials, fire_points: [[dx, dy]]}],
## attacker: {ship, x, y, center: [x, y], size, facing, owner}, target: {ship, x, y, center, size,
## facing}, missile (bool), speedx2 (bool), shield_frames}. Returns the ticks, each an Array of ops:
##   {op: "seg", slot, frame, max, hit, from, to, start, end, weapon, specials, recipe (or params)}
##   {op: "burst", at, dir, variant, frame, slot}   BEAMS.LBX 1 + variant*32 + slot*16 + dir
##   {op: "explosion", at, type, frame, slot}      BEAMS.LBX 65 + slot
##   {op: "ball", at}                              the Mauler's ball, BEAMS.LBX 88
##   {op: "shield", status, frame, rotation, special, heavy}
##   {op: "sound", id, tag, pitch}                 a game sound; `tag` names it for a fade
##   {op: "fade", tag}                             the tagged sound fades (over 10 ticks)
func plan(v: Dictionary) -> Array:
	volley = v
	_shield_frames = int(v.get("shield_frames", 10))
	var entries: Array = v.get("entries", [])
	var ticks: Array = []
	if entries.is_empty():
		return ticks
	var w0 := int(entries[0].weapon)
	if w0 != STELLAR_BEAM and int(entries[0].get("type", 0)) != 0:
		return ticks # not a beam weapon: not drawn by this loop
	var attacker: Dictionary = v.attacker
	var target: Dictionary = v.target
	var missile := bool(v.get("missile", false))
	var a_pos := Vector2i(int(attacker.x), int(attacker.y))
	var t_pos := Vector2i(int(target.x), int(target.y))
	var a_center := Vector2i(int(attacker.center[0]), int(attacker.center[1]))
	var t_center := Vector2i(int(target.center[0]), int(target.center[1])) if not missile else t_pos
	var ship_range := maxi(absi(t_pos.x - a_pos.x), absi(t_pos.y - a_pos.y))
	var current := [0, -1]
	var frame_idx := [0, 0]
	var special_timer := [0, 0]
	var offsets := [Vector2i.ZERO, Vector2i.ZERO]
	var run_points := [[[0, 0]], [[0, 0]]]
	var fx_idx := 0
	if int(entries[0].specials) & 0x80 != 0:
		special_timer[0] = 3
	_pierced = 0
	_fire_sounds = {}
	_shield_sound = false
	_shield_status = 0
	_shield_frame = 0
	_shield_hit_now = 0
	var speedx2 := bool(v.get("speedx2", false))
	var guard := 0
	while guard < 2000:
		guard += 1
		_ops = []
		for i in 2:
			var cur: int = current[i]
			if cur >= 0:
				var e: Dictionary = entries[cur]
				if frame_idx[i] == 0:
					var given: Array = runs_given[i]
					if not given.is_empty():
						var run: Dictionary = given.pop_front()
						offsets[i] = run.offset
						run_points[i] = run.points
					else:
						offsets[i] = _aim_offset(e, ship_range, missile, int(target.get("size", 0)))
						run_points[i] = e.get("fire_points", [[0, 0]])
				var points: Array = run_points[i]
				for fp in points.size():
					var p: Array = points[fp]
					_fire_weapon(a_center + Vector2i(int(p[0]), int(p[1])), t_center + offsets[i], e, frame_idx[i], i, fp)
			var skip := false
			if cur <= -1:
				var other: int = current[1 - i]
				if other != -1 and other < 16 and other >= 0:
					if int(entries[other].weapon) <= 0 or frame_idx[1 - i] >= 9:
						skip = true
			elif frame_idx[i] >= _max_frames:
				skip = true
			if skip:
				frame_idx[i] = 0
				special_timer[i] -= 1
				if special_timer[i] <= 0:
					special_timer[i] = 0
					var next := fx_idx + 1
					if next >= entries.size() or next >= 16 or int(entries[next].weapon) <= 0:
						current[i] = -1
					elif not bool(entries[next].get("damage_entry", false)):
						fx_idx = next
						current[i] = fx_idx
						if int(entries[fx_idx].specials) & 0x80 != 0:
							special_timer[i] = 3
					else:
						current[i] = -1 # the reflected beam: not ported yet
			else:
				if speedx2:
					frame_idx[i] += 2 if frame_idx[i] + 2 < _max_frames else 1
				else:
					frame_idx[i] += 1
		if current[0] <= -1 and current[1] <= -1:
			_shield_hit_now = 0
		var done: bool = current[0] <= -1 and current[1] <= -1 and _shield_status == 0
		if not missile:
			_adjust_shield()
			if _shield_status != 0:
				var special := 0
				if current[0] > -1:
					special = int(entries[current[0]].special)
				if current[1] > -1:
					special += int(entries[1 if current[0] == 0 else 0].special) # the game's own index ([current_fx_idx[0] == 0])
				if _pierced != 0:
					special = 1
				var heavy := 0
				if current[0] > -1:
					heavy = int(entries[current[0]].specials) & 0x100
				_ops.append({"op": "shield", "status": _shield_status, "frame": _shield_frame, "rotation": _shield_rotation(),
					"special": 1 if special != 0 else 0, "heavy": heavy})
				# Draw_Shield_Hit's sound: 38 on the first frame, lower for bigger ships, faded on the last.
				if _shield_frame == 0 and not _shield_sound:
					_shield_sound = true
					_ops.append({"op": "sound", "id": 38, "tag": SHIELD_SOUND_TAG, "pitch": -int(target.get("size", 0)) * 2500})
				elif _shield_frames - 1 <= _shield_frame and _shield_sound:
					_shield_sound = false
					_ops.append({"op": "fade", "tag": SHIELD_SOUND_TAG})
			for i in 2:
				var cur: int = current[i]
				if cur > -1 and _ship_hit_now[i] != 0:
					var e: Dictionary = entries[cur]
					if int(e.hits) > 0 and not e.get("_hit_shown", false):
						e["_hit_shown"] = true
						_ops.append({"op": "sound", "id": 0x1d})
						_ops.append({"op": "damage", "entry": cur})
		ticks.append(_ops)
		if done:
			break
	for e: Dictionary in entries:
		e.erase("_hit_shown")
	return ticks


## The aim point's random offset from the target's centre (Beam_SFX, a channel's first frame).
func _aim_offset(e: Dictionary, ship_range: int, missile: bool, target_size: int) -> Vector2i:
	var target_ship := int(volley.target.get("ship", 1))
	var result := int(e.result)
	var special := int(e.special)
	var hits := int(e.hits)
	var apply_random := false
	if result == 0 and special == 0:
		if target_ship != 0:
			if not missile:
				var max_radius := ship_range / 2
				var shield_radius: int = SHIELD_DIAMETERS[shield_size(target_size)] / 2
				if shield_radius <= max_radius:
					max_radius = shield_radius
				return Vector2i(_random(max_radius + 1) - max_radius / 2, _random(max_radius + 1) - max_radius / 2)
			apply_random = true
		elif hits == 0:
			return _ring_offset(ship_range)
		else:
			apply_random = true
	elif target_ship == 0 and hits == 0:
		return _ring_offset(ship_range)
	else:
		apply_random = true
	if apply_random:
		if not missile or hits != 0:
			return Vector2i.ZERO
		var shield_radius: int = mini(SHIELD_DIAMETERS[0] / 2, ship_range / 2)
		var radius := shield_radius / 4 + 1
		var base := shield_radius / 2
		return Vector2i((base + _random(radius)) * (_random(2) - 1), (base + _random(radius)) * (_random(2) - 1))
	return Vector2i.ZERO


func _ring_offset(ship_range: int) -> Vector2i:
	var max_radius := mini(ship_range / 2, 30)
	var radius := max_radius / 4 + 1
	var base := max_radius / 2
	return Vector2i((base + _random(radius)) * (_random(2) - 1), (base + _random(radius)) * (_random(2) - 1))


func _random(n: int) -> int:
	return rng.randi_range(0, maxi(n, 1) - 1)


## Adjust_Shield_Display_Status.
func _adjust_shield() -> void:
	match _shield_status:
		0:
			if _shield_hit_now != 0:
				_shield_status = 1
				_shield_frame = 0
		1:
			_shield_frame += 1
			if _shield_frame >= 4:
				_shield_status = 2
		2:
			_shield_frame += 1
			if _shield_frame >= 7:
				if _shield_hit_now != 0:
					_shield_frame = 4
				else:
					_shield_status = 3
		3:
			_shield_frame += 1
			if _shield_frame >= _shield_frames:
				_shield_status = 0


## Get_SFX_Draw_Shield_Facing2(attacker, target): the shield hit's direction (0-15).
func _shield_rotation() -> int:
	var a: Dictionary = volley.attacker
	var t: Dictionary = volley.target
	var a_off := Vector2i(int(a.center[0]) - int(a.x), int(a.center[1]) - int(a.y))
	var t_off := Vector2i(int(t.center[0]) - int(t.x), int(t.center[1]) - int(t.y))
	var x1 := int(a.grid[0]) * 20 + a_off.x
	var y1 := int(a.grid[1]) * 20 + a_off.y
	var x2 := int(t.grid[0]) * 20 + t_off.x
	var y2 := int(t.grid[1]) * 20 + t_off.y
	return (_angle(x1 - x2, y2 - y1) / 22) % 16


## Fire_Weapon: one fire point of one channel on one tick.
func _fire_weapon(from: Vector2i, to: Vector2i, e: Dictionary, frame: int, slot: int, fire_point: int) -> void:
	var weapon := int(e.weapon)
	var specials := int(e.specials)
	var p := weapon_params(weapon)
	if p.is_empty():
		return
	var fx := recipe(weapon, specials)
	if fx.is_empty():
		if specials & 2 != 0:
			p.mode = mini(int(p.mode) + 1, 2)
		if specials & 0x80 != 0:
			p.step = int(p.step) * 2
			p.dur = int(p.dur) / 2
		if specials & 4 != 0:
			p.mode = maxi(int(p.mode) - 1, 0)
		if specials & 8 != 0:
			p.tip = 1
		if specials & 0x10 != 0 and int(p.dur) != 640:
			p.dur = int(p.dur) * 2
		if specials & 0x20 != 0:
			p.full = 0
		if specials & 0x40 != 0:
			p.tip = 1
	var attacker_size := int(volley.attacker.get("size", 0))
	var loops := 3 if int(p.multi) != 0 and attacker_size != -1 else 1
	for k in loops:
		_generic_beam(from, to, e, p, fx, frame + k, slot)
	# The firing sound: on the first frame (and when the fire point's number equals the frame, a
	# quirk), faded once the ship stops firing or on the last frame.
	var tag := slot * 5 + fire_point
	if fire_point == frame or frame == 0:
		_ops.append({"op": "sound", "id": int(p.sound), "slot": slot, "tag": tag})
		_fire_sounds[tag] = true
	elif (_firing_now[slot] == 0 or _max_frames - 1 <= frame) and _fire_sounds.get(tag, false):
		_ops.append({"op": "fade", "tag": tag})
		_fire_sounds.erase(tag)


## Draw_Generic_Beam.
func _generic_beam(a: Vector2i, t: Vector2i, e: Dictionary, p: Dictionary, fx: Dictionary, frame: int, slot: int) -> void:
	var angle := _angle(a.x - t.x, t.y - a.y)
	_ship_hit_now[0] = 0
	_ship_hit_now[1] = 0
	var beam_dir := absi(_cmod(angle / 22 + 8, 16))
	var ship_range := maxi(absi(t.x - a.x), absi(t.y - a.y))
	var attacker_size := int(volley.attacker.get("size", 0))
	var target_size := int(volley.target.get("size", 0))
	var duration := int(p.dur)
	var step := int(p.step)
	var mode := int(p.mode)
	var burst_variant := int(p.burst)
	if attacker_size == -1:
		mode = 0
		duration = mini(duration, 20)
		burst_variant = 1
		step = 5
	if not fx.is_empty():
		duration = int(fx.length)
		step = int(fx.step)
	var eff := maxi(duration, 60)
	if ship_range < eff:
		eff = ship_range
	var min_frames := 2 if attacker_size == -1 else 3
	var frames := maxi((ship_range + eff) / step, min_frames)
	if frames > 6:
		step *= 2
	frames = maxi((ship_range + eff) / step, min_frames)
	_max_frames = frames
	if _max_frames <= frame:
		return
	if frame == 0 and attacker_size != -1:
		_ops.append({"op": "burst", "at": a, "dir": beam_dir, "variant": burst_variant, "frame": 0, "slot": slot})
		return
	var target_len := step * frame
	var attacker_len := target_len - duration
	var style := int(fx.get("style", -1))
	if style == 2:
		target_len = ship_range
		attacker_len = 0
	target_len = clampi(target_len, 0, ship_range)
	var head := interpolate(a, t, target_len)
	_ship_hit_now[slot] = 1 if head == t or _max_frames - 2 <= frame else 0
	var result := int(e.result)
	var special := int(e.special)
	var reduce := 0
	if result == 0 or special != 0 or _pierced != 0 or attacker_size == -1:
		_pierced = 1
		reduce = 0
	else:
		reduce = SHIELD_DIAMETERS[shield_size(target_size)] / 3
	var cap := mini(duration - 1, reduce)
	var b2 := interpolate(a, t, target_len - cap)
	attacker_len = clampi(attacker_len, 0, ship_range)
	if style == 2:
		attacker_len = 0
	var b1 := a
	if attacker_len != 0:
		b1 = interpolate(a, t, attacker_len)
	var reverse := int(p.reverse) != 0 and fx.is_empty()
	if reverse:
		var tmp := b1
		b1 = b2
		b2 = tmp
	var seg := {"op": "seg", "slot": slot, "frame": frame, "max": _max_frames, "hit": _ship_hit_now[slot], "from": b1, "to": b2,
		"start": a, "end": t, "weapon": int(e.weapon), "specials": int(e.specials), "step": step, "len": duration}
	if not fx.is_empty():
		seg["recipe"] = fx
	else:
		seg["params"] = p.duplicate()
		seg["params"]["mode"] = mode
	_ops.append(seg)
	if fx.is_empty() and b1 != b2 and int(p.ball) != 0 and _ship_hit_now[slot] == 0:
		_ops.append({"op": "ball", "at": b2})
	if reverse:
		var tmp2 := b1
		b1 = b2
		b2 = tmp2
	if frame < 3 or a == b1:
		if attacker_size != -1:
			_ops.append({"op": "burst", "at": a, "dir": beam_dir, "variant": burst_variant, "frame": frame, "slot": slot})
		_firing_now[slot] = 1
	else:
		_firing_now[slot] = 0
	if _ship_hit_now[slot] != 0 and attacker_size != -1:
		var exp_frame: int
		if frame < _max_frames - 1:
			exp_frame = _cmod(frame - 1 + _cdiv(-10 - ship_range, step), 5)
		else:
			exp_frame = frame - _max_frames + 7
		exp_frame = absi(exp_frame)
		if special != 0 or result != 0:
			var facing := 0 if bool(volley.get("missile", false)) else int(volley.target.get("facing", 0))
			_ops.append({"op": "explosion", "at": t, "type": facing, "frame": exp_frame, "slot": slot})
	var radius: int = SHIELD_DIAMETERS[shield_size(target_size)] >> 1
	_shield_hit_now = 1 if result != 0 and absi(b2.x - t.x) <= radius and absi(b2.y - t.y) <= radius else 0
