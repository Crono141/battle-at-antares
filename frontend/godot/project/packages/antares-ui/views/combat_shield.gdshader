shader_type canvas_item;
// WP3.9b: a combat ship with its shield (mockup B4). The ship's own pixels draw as they are; around
// the hull a green glow grows outwards, as thick as the legacy shield outline (2-10 layers by the
// arc's maximum, cmbtdrw1.cpp) and, per arc, only as far as that arc's strength reaches (a weak
// arc keeps its inner layers, as legacy drops its outer colours). A faint haze marks where the full
// shield was. Arcs are ship-relative and clockwise from the nose (+x in the sprite): 0 front,
// 1 right, 2 rear, 3 left, matching shield_arc_current[].

uniform vec4 arcs = vec4(1.0); // each arc's strength, 0..1 (current / max)
uniform float thickness_px = 0.0; // the full glow, in texture pixels (0: no shield)
uniform float shield_on = 0.0; // 1 shows the glow (hover or "Always show health")
// The hull's outline comes from frame 0 (the ship without its engines' flames), so the glow keeps
// still while the flames animate (owner, 2026-11-15: it pulsed with them). hull_offset: where the
// hull frame's top-left sits in the current frame's pixels; hull_size: its size in pixels.
uniform sampler2D hull_tex : filter_linear_mipmap;
uniform vec2 hull_offset = vec2(0.0);
uniform vec2 hull_size = vec2(1.0);
uniform float use_hull = 0.0;
uniform vec4 inner_color : source_color = vec4(0.92, 1.0, 0.95, 1.0);
uniform vec4 mid_color : source_color = vec4(0.35, 0.94, 0.6, 1.0);
uniform vec4 outer_color : source_color = vec4(0.08, 0.45, 0.24, 1.0);
// The cloak (CMBTDRW1::Plasma_Darken, owner 2026-11-22): a noise map (60x60 frames stacked, values
// 0-13) pushes each pixel down its colour ramp by noise x darkness / 4 (darkness under 5) or
// noise + darkness - 4, at most 15 steps of 16; a pixel pushed past its ramp's end is gone.
// darkness 0 is off; art_scale: texture pixels per 1x pixel (the noise is in 1x pixels).
uniform sampler2D plasma_tex : filter_nearest, repeat_disable;
uniform float darkness = 0.0;
uniform int plasma_frame = 0;
uniform float art_scale = 1.0;
// Mask_Brighten_Bitmap under a shield hit: the ship lit up (0 off).
uniform float brighten = 0.0;

// The sprite's region is padded past its texture (combat_view.gd), so UV runs beyond 0..1 there:
// room for the glow that Godot also knows about (a quad grown in vertex() lost its glow once the
// camera had moved away and back; owner build 2026-11-15).

// Explicit gradients everywhere: inside branches and loops a plain texture() has no defined mip
// level, and when ships are shrunk it picked near-empty mips (no glow, and a faint box round the
// sprite; owner, 2026-11-15).
float alpha_at(sampler2D tex, vec2 uv, vec2 gx, vec2 gy) {
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
		return 0.0;
	}
	return textureGrad(tex, uv, gx, gy).a;
}

// The hull's alpha at a point of the current frame (`uv` in its texture coordinates).
float hull_alpha(sampler2D frame_tex, vec2 uv, vec2 tex_size, vec2 gx, vec2 gy) {
	if (use_hull < 0.5) {
		return alpha_at(frame_tex, uv, gx, gy);
	}
	vec2 k = tex_size / hull_size;
	vec2 h = (uv * tex_size - hull_offset) / hull_size;
	if (h.x < 0.0 || h.y < 0.0 || h.x > 1.0 || h.y > 1.0) {
		return 0.0;
	}
	return textureGrad(hull_tex, h, gx * k, gy * k).a;
}

// The sprite's modulate (fades, retreat, phased-out ships): read in vertex(), since fragment()'s
// COLOR arrives already multiplied by the texture (using it darkened every ship: owner 2026-11-22).
varying vec4 tint;

void vertex() {
	tint = COLOR;
}

vec4 darken(vec4 c, vec2 uv, vec2 tex_size) {
	if (darkness <= 0.0 || c.a <= 0.0) {
		return c;
	}
	ivec2 p = ivec2(floor(uv * tex_size / max(art_scale, 0.001)));
	ivec2 at = ivec2(int(mod(float(p.x), 60.0)), int(mod(float(p.y), 60.0)) + 60 * (plasma_frame % 16));
	float n = floor(texelFetch(plasma_tex, at, 0).r * 255.0 + 0.5);
	float val = darkness < 5.0 ? floor(n * darkness / 4.0) : n + darkness - 4.0;
	val = min(val, 15.0);
	float shade = floor(max(c.r, max(c.g, c.b)) * 15.0 + 0.5);
	float left = shade - val;
	if (left < 0.0) {
		return vec4(0.0);
	}
	c.rgb *= shade > 0.0 ? max(left, 0.5) / shade : 0.0;
	return c;
}

void fragment() {
	vec2 uv = UV;
	vec2 gx = dFdx(uv);
	vec2 gy = dFdy(uv);
	vec4 ship = textureGrad(TEXTURE, clamp(uv, vec2(0.0), vec2(1.0)), gx, gy);
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
		ship = vec4(0.0);
	}
	ship = darken(ship, uv, 1.0 / TEXTURE_PIXEL_SIZE);
	ship.rgb = min(ship.rgb + vec3(brighten * 0.4), vec3(1.0));
	COLOR = ship;
	if (shield_on > 0.5 && thickness_px > 0.0 && ship.a < 0.5) {
		vec2 px = TEXTURE_PIXEL_SIZE;
		vec2 tex_size = 1.0 / px;
		// The distance to the hull, found by stepping outwards in 16 directions.
		float best = 1e6;
		const int DIRS = 24;
		const int STEPS = 10;
		for (int d = 0; d < DIRS; d++) {
			float a = 6.2831853 * float(d) / float(DIRS);
			vec2 dir = vec2(cos(a), sin(a));
			for (int s = 1; s <= STEPS; s++) {
				float r = thickness_px * float(s) / float(STEPS);
				if (r >= best) {
					break;
				}
				if (hull_alpha(TEXTURE, uv + dir * r * px, tex_size, gx, gy) > 0.5) {
					best = r;
					break;
				}
			}
		}
		if (best < 1e5) {
			// Which arc this pixel is in: the angle from the sprite's centre, ship-relative.
			// The angle from the hull's centre, in pixels (square on a non-square sprite).
			vec2 hull_px = use_hull > 0.5 ? hull_size : tex_size;
			vec2 hull_at = use_hull > 0.5 ? hull_offset : vec2(0.0);
			vec2 c = uv * tex_size - hull_at - hull_px / 2.0;
			float ang = atan(c.y, c.x);
			int arc = int(mod(floor((ang + 0.7853982) / 1.5707963), 4.0));
			float strength = arc == 0 ? arcs.x : arc == 1 ? arcs.y : arc == 2 ? arcs.z : arcs.w;
			// A narrow gap where two arcs meet (legacy's dividers).
			float to_edge = abs(mod(ang + 0.7853982, 1.5707963) - 0.7853982);
			float gap = smoothstep(0.66, 0.74, to_edge);
			float t = best / thickness_px; // 0 at the hull, 1 at the full shield's edge
			float reach = strength;
			vec4 glow = vec4(0.0);
			if (t <= reach && reach > 0.0) {
				float u = t / max(reach, 0.001);
				vec3 col = u < 0.5 ? mix(inner_color.rgb, mid_color.rgb, u * 2.0) : mix(mid_color.rgb, outer_color.rgb, (u - 0.5) * 2.0);
				float alpha = mix(0.85, 0.12, smoothstep(0.0, 1.0, u)) * (1.0 - smoothstep(0.8, 1.0, u));
				glow = vec4(col, alpha);
			} else if (t <= 1.0) {
				glow = vec4(mid_color.rgb, 0.12 * (1.0 - t)); // the haze where the full shield was
			}
			glow.a *= 1.0 - gap;
			COLOR = mix(glow, ship, ship.a);
		}
	}
	COLOR *= tint;
}
