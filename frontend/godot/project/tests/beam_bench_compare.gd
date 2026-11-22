## WP3.9 stage 1: the native beam volleys (packages/antares-ui/views/beam_volley.gd) against the
## game's own, tick by tick. The trace comes from the reference bench (src/antares/beam_bench.cpp:
## ANTARES_BEAM_BENCH=<dir> in a legacy battle writes <dir>/beam_bench.jsonl).
##
##   Godot --headless --path project -s res://tests/beam_bench_compare.gd -- <beam_bench.jsonl> [case ...]
##
## Each case's inputs that the game draws at random or computes from ship data (the aim offset
## and the fire points of each run) are taken from the game's fire calls on the run's first frame;
## the rest is the port's. Compared per tick: segments (frame, max frames, ship-hit flag, ends),
## muzzle bursts, hit explosions, shield-hit states, sounds (beam, hull hit), recipe ramps.
## Ticks the game spent only on the damage numbers (the frontend shows its own) don't count.
extends SceneTree

const BeamVolley := preload("res://packages/antares-ui/views/beam_volley.gd")


func _initialize() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		push_error("usage: -- <beam_bench.jsonl> [case ...]")
		quit(1)
		return
	var only := {}
	for k in range(1, args.size()):
		only[int(args[k])] = true
	var file := FileAccess.open(args[0], FileAccess.READ)
	if file == null:
		push_error("can't read %s" % args[0])
		quit(1)
		return
	var cases: Array = []
	var current: Dictionary = {}
	while not file.eof_reached():
		var line := file.get_line()
		if line.is_empty():
			continue
		var d: Dictionary = JSON.parse_string(line)
		if d.has("case"):
			current = {"head": d, "lines": []}
		elif d.has("end"):
			current["ticks"] = int(d.ticks)
			cases.append(current)
		elif d.has("t") and not current.is_empty():
			current.lines.append(d)
	var passed := 0
	var failed := 0
	var by_weapon := {}
	for c: Dictionary in cases:
		var n := int(c.head.case)
		if not only.is_empty() and not only.has(n):
			continue
		var problems := _compare(c)
		var key := ",".join(c.head.entries.map(func(e): return "%d" % int(e.weapon)))
		if problems.is_empty():
			passed += 1
		else:
			failed += 1
			if failed <= 40:
				print("case %d (weapons %s, specials %s, result %s/%s): %s" % [n, key, ",".join(c.head.entries.map(func(e): return "%x" % int(e.specials))),
					c.head.entries[0].result, c.head.entries[0].special, "; ".join(problems.slice(0, 4))])
		var w: Array = by_weapon.get(key, [0, 0])
		w[0 if problems.is_empty() else 1] += 1
		by_weapon[key] = w
	for key in by_weapon:
		print("weapons %s: %d match, %d differ" % [key, by_weapon[key][0], by_weapon[key][1]])
	print("beam bench: %d cases match, %d differ" % [passed, failed])
	var missile_ok := _missile_volley()
	quit(0 if failed == 0 and missile_ok else 1)


## A volley at a missile stack in the engine event's shape (combat_native.cpp Beam: the target is
## ship -1 with only x, y and center): the bench has no such case, so this only checks that it
## plans, draws segments that end at the stack, and ends.
func _missile_volley() -> bool:
	var volley := {"entries": [{"weapon": 9, "count": 2, "result": 0, "special": 0, "hits": 2, "specials": 0, "type": 0,
		"fire_points": [[0, 0]]}], "missile": true, "speedx2": false, "shield_frames": 10,
		"attacker": {"ship": 5, "x": 400, "y": 300, "center": [420, 320], "grid": [20, 15], "size": 2, "facing": 0, "owner": 0},
		"target": {"ship": -1, "x": 560, "y": 340, "center": [560, 340]}}
	var ticks: Array = BeamVolley.new().plan(volley)
	var segs := 0
	var shields := 0
	for ops: Array in ticks:
		for op: Dictionary in ops:
			segs += 1 if String(op.op) == "seg" else 0
			shields += 1 if String(op.op) == "shield" else 0
	var ok := ticks.size() > 2 and ticks.size() < 60 and segs > 0 and shields == 0
	print("missile volley: %d ticks, %d segments, %d shield ops: %s" % [ticks.size(), segs, shields, "ok" if ok else "FAILED"])
	return ok


func _v(a: Array, i := 0) -> Vector2i:
	return Vector2i(int(a[i]), int(a[i + 1]))


func _compare(c: Dictionary) -> Array:
	var head: Dictionary = c.head
	var lines: Array = c.lines
	var volley := {"entries": [], "attacker": head.attacker, "target": head.target, "missile": false, "speedx2": int(head.speedx2) != 0}
	for e: Dictionary in head.entries:
		volley.entries.append(e.duplicate())
	var planner := BeamVolley.new()
	# The runs' inputs: fire calls on a run's first frame, by channel, in tick order.
	var t_center := _v(head.target.center)
	var a_center := _v(head.attacker.center)
	var runs := {} # "tick/slot" -> {offset, points}
	var order: Array = []
	for l: Dictionary in lines:
		if l.has("fire") and int(l.frame) == 0:
			var key := "%d/%d" % [int(l.t), int(l.slot)]
			if not runs.has(key):
				runs[key] = {"offset": _v(l.fire, 2) - t_center, "points": [], "slot": int(l.slot)}
				order.append(key)
			var from := _v(l.fire, 0) - a_center
			runs[key].points.append([from.x, from.y])
	for key in order:
		planner.runs_given[int(runs[key].slot)].append(runs[key])
	var ticks: Array = planner.plan(volley)

	# The game's ticks, as comparable lists.
	var game: Array = []
	for t in int(c.get("ticks", 0)):
		game.append({"seg": [], "burst": [], "explosion": [], "shield": [], "sound": [], "ramp": []})
	for l: Dictionary in lines:
		var t := int(l.t)
		if t < 0 or t >= game.size():
			continue
		var g: Dictionary = game[t]
		if l.has("seg"):
			g.seg.append("s%d f%d max%d hit%d %d,%d-%d,%d" % [int(l.slot), int(l.frame), int(l.max), int(l.hit), int(l.seg[0]), int(l.seg[1]), int(l.seg[2]), int(l.seg[3])])
		elif l.has("burst"):
			g.burst.append("b%d %d,%d d%d v%d f%d" % [int(l.slot), int(l.burst[0]), int(l.burst[1]), int(l.dir), int(l.variant), int(l.frame)])
		elif l.has("explosion"):
			g.explosion.append("x%d %d,%d t%d f%d" % [int(l.slot), int(l.explosion[0]), int(l.explosion[1]), int(l.type), int(l.frame)])
		elif l.has("shield") and int(l.status) != 0:
			g.shield.append("st%d f%d r%d sp%d h%d" % [int(l.status), int(l.frame), int(l.rotation), int(l.special), int(l.heavy)])
		elif l.has("sound") and int(l.sound) != 38 and int(l.sound) != 42:
			g.sound.append(int(l.sound))
		elif l.has("beam2"):
			g.ramp.append(str((l.ramp as Array).map(func(c): return [int(c[0]), int(c[1]), int(c[2])])))
	var mine: Array = []
	for ops: Array in ticks:
		var m := {"seg": [], "burst": [], "explosion": [], "shield": [], "sound": [], "ramp": []}
		for op: Dictionary in ops:
			match String(op.op):
				"seg":
					m.seg.append("s%d f%d max%d hit%d %d,%d-%d,%d" % [op.slot, op.frame, op.max, op.hit, op.from.x, op.from.y, op.to.x, op.to.y])
					if op.has("recipe"):
						m.ramp.append(str(BeamVolley.recipe_ramp(op.recipe)))
				"burst":
					m.burst.append("b%d %d,%d d%d v%d f%d" % [op.slot, op.at.x, op.at.y, op.dir, op.variant, op.frame])
				"explosion":
					m.explosion.append("x%d %d,%d t%d f%d" % [op.slot, op.at.x, op.at.y, op.type, op.frame])
				"shield":
					m.shield.append("st%d f%d r%d sp%d h%d" % [op.status, op.frame, op.rotation, op.special, op.heavy])
				"sound":
					m.sound.append(int(op.id))
		mine.append(m)

	var problems: Array = []
	var last_game := -1
	for t in game.size():
		var g: Dictionary = game[t]
		if not (g.seg.is_empty() and g.burst.is_empty() and g.explosion.is_empty() and g.shield.is_empty()):
			last_game = t
	var last_mine := -1
	for t in mine.size():
		var m: Dictionary = mine[t]
		if not (m.seg.is_empty() and m.burst.is_empty() and m.explosion.is_empty() and m.shield.is_empty()):
			last_mine = t
	if last_game != last_mine:
		problems.append("drawing ends on tick %d, game %d" % [last_mine, last_game])
	for t in maxi(last_game, last_mine) + 1:
		var g: Dictionary = game[t] if t < game.size() else {"seg": [], "burst": [], "explosion": [], "shield": [], "sound": [], "ramp": []}
		var m: Dictionary = mine[t] if t < mine.size() else {"seg": [], "burst": [], "explosion": [], "shield": [], "sound": [], "ramp": []}
		for kind in ["seg", "burst", "explosion", "shield", "sound", "ramp"]:
			var a: Array = g[kind].duplicate()
			var b: Array = m[kind].duplicate()
			a.sort()
			b.sort()
			if kind == "ramp":
				# The game sets a recipe ramp after drawing; compare the distinct ramps only.
				a = _unique(a)
				b = _unique(b)
			if a != b:
				problems.append("t%d %s: game %s, port %s" % [t, kind, str(a).left(160), str(b).left(160)])
	return problems


func _unique(a: Array) -> Array:
	var out: Array = []
	for x in a:
		if not out.has(x):
			out.append(x)
	return out
