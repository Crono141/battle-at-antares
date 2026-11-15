shader_type canvas_item;
// WP3.9b: a combat ship with its shield (mockup B4). The ship's own pixels draw as they are; around
// the hull a green glow grows outwards, as thick as the legacy shield outline (2-10 layers by the
// arc's maximum, cmbtdrw1.cpp) and, per arc, only as far as that arc's strength reaches (a weak
// arc keeps its inner layers, as legacy drops its outer colours). A faint haze marks where the full
// shield was. Arcs are ship-relative and clockwise from the nose (+x in the sprite): 0 front,
// 1 right, 2 rear, 3 left, matching shield_arc_current[].

uniform vec4 arcs = vec4(1.0); // each arc's strength, 0..1 (current / max)
uniform float thickness_px = 0.0; // the full glow, in texture pixels (0: no shield)
uniform float margin = 0.0; // how far the quad grows past the texture, as a fraction of its size
uniform float shield_on = 0.0; // 1 shows the glow (hover or "Always show health")
uniform vec4 inner_color : source_color = vec4(0.92, 1.0, 0.95, 1.0);
uniform vec4 mid_color : source_color = vec4(0.35, 0.94, 0.6, 1.0);
uniform vec4 outer_color : source_color = vec4(0.08, 0.45, 0.24, 1.0);

varying vec2 local_uv;

void vertex() {
	// The quad grows by `margin` on every side so the glow has room past the sprite's edges.
	float k = 1.0 + 2.0 * margin;
	VERTEX *= k;
	local_uv = (UV - 0.5) * k + 0.5;
}

float alpha_at(sampler2D tex, vec2 uv) {
	if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) {
		return 0.0;
	}
	return texture(tex, uv).a;
}

void fragment() {
	vec2 uv = local_uv;
	vec4 ship = (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) ? vec4(0.0) : texture(TEXTURE, uv);
	COLOR = ship;
	if (shield_on > 0.5 && thickness_px > 0.0 && ship.a < 0.5) {
		vec2 px = TEXTURE_PIXEL_SIZE;
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
				if (alpha_at(TEXTURE, uv + dir * r * px) > 0.5) {
					best = r;
					break;
				}
			}
		}
		if (best < 1e5) {
			// Which arc this pixel is in: the angle from the sprite's centre, ship-relative.
			vec2 c = uv - vec2(0.5);
			c.x *= px.y / px.x; // in pixels: width / height, so the arcs are square on a non-square sprite
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
}
