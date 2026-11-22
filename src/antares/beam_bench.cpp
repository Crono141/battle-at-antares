#include "pch.h"
#include "antares/beam_bench.h"
#include "antares/combat_native.h"
#include "antares/identity.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// The reference bench for the native beams (beam_bench.h). Debug only: nothing here runs unless
// ANTARES_BEAM_BENCH names a folder.

namespace antares::beam_bench {
    namespace {
        FILE* s_out = nullptr;
        bool s_done = false;
        bool s_tracing = false;
        int32_t s_case = -1;
        int32_t s_tick = -1;

        struct s_case_entry {
            int16_t weapon;
            int16_t count;
            uint16_t result;
            uint16_t special;
            int16_t hits;
            uint16_t specials;
        };

        struct s_bench_case {
            std::vector<s_case_entry> entries;
        };

        // Recipe ("Steve stuff") weapons take only 0x10 and 0x100 from the specials in their
        // look, and 0x80 in the scheduler; the others take the whole specials block.
        const int16_t k_recipe[] = {WEAPON_MASS_DRIVER, WEAPON_GAUSS_CANNON, WEAPON_LASER_CANNON, WEAPON_PARTICLE_BEAM, WEAPON_FUSION_BEAM, WEAPON_ION_PULSE_CANNON, WEAPON_GRAVITON_BEAM, WEAPON_NEUTRON_BLASTER, WEAPON_PHASOR, WEAPON_DISRUPTER, WEAPON_DEATH_RAY, WEAPON_PLASMA_CANNON, WEAPON_PHASOR_EYE, WEAPON_CRYSTAL_RAY};
        const uint16_t k_recipe_specials[] = {0, 0x10, 0x100, 0x110, 0x80};
        const int16_t k_other[] = {WEAPON_SPATIAL_COMPRESSOR, WEAPON_MAULER_DEVICE, WEAPON_DRAGON_BREATH, WEAPON_PLASMA_BREATH, 46};
        const uint16_t k_other_specials[] = {0, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};

        std::vector<s_bench_case> Cases() {
            std::vector<s_bench_case> cases;
            // Hit kinds: on the hull (no shield in the way), held by the shield, through it.
            const s_case_entry kinds[] = {{0, 1, 0, 0, 5, 0}, {0, 1, 1, 0, 0, 0}, {0, 1, 1, 1, 5, 0}};
            auto add = [&](int16_t weapon, const uint16_t* specials, size_t n) {
                for (size_t s = 0; s < n; ++s) {
                    for (const s_case_entry& kind : kinds) {
                        s_case_entry e = kind;
                        e.weapon = weapon;
                        e.specials = specials[s];
                        cases.push_back({{e}});
                    }
                }
                // Several shots of one weapon (several fire points).
                cases.push_back({{{weapon, 3, 0, 0, 5, 0}}});
            };
            for (int16_t w : k_recipe) {
                add(w, k_recipe_specials, sizeof(k_recipe_specials) / sizeof(k_recipe_specials[0]));
            }
            for (int16_t w : k_other) {
                add(w, k_other_specials, sizeof(k_other_specials) / sizeof(k_other_specials[0]));
            }
            // Volleys of several weapons: the two channels take turns.
            cases.push_back({{{WEAPON_GRAVITON_BEAM, 2, 0, 0, 5, 0}, {WEAPON_DISRUPTER, 1, 0, 0, 5, 0}, {WEAPON_LASER_CANNON, 4, 0, 0, 5, 0}}});
            cases.push_back({{{WEAPON_FUSION_BEAM, 2, 1, 0, 0, 0}, {WEAPON_FUSION_BEAM, 2, 1, 0, 0, 0x100}}});
            cases.push_back({{{WEAPON_MASS_DRIVER, 4, 0, 0, 5, 0}, {WEAPON_SPATIAL_COMPRESSOR, 1, 1, 1, 5, 0}, {WEAPON_PHASOR, 2, 1, 0, 0, 0x80}}});
            return cases;
        }

        void Ship_Json(std::string* out, const char* name, int16_t ship, int16_t x, int16_t y) {
            int16_t cx = 0, cy = 0;
            COMBINIT::Ship_Center_Offsets(ship, &cx, &cy);
            const s_combat_data& d = MOX::combat_data[ship];
            char buf[256];
            snprintf(buf, sizeof(buf), "\"%s\":{\"ship\":%d,\"x\":%d,\"y\":%d,\"center\":[%d,%d],\"grid\":[%d,%d],\"facing\":%d,\"size\":%d,\"owner\":%d,\"picture\":%d}", name, ship, x, y, x + cx, y + cy, d.x, d.y, d.facing_dir, d.size_class, d.owner, d.picture_num);
            *out += buf;
        }
    } // namespace

    bool Tracing() {
        return s_tracing && s_out != nullptr;
    }

    void Run(int16_t attacker, int16_t attacker_x, int16_t attacker_y, int16_t target, int16_t target_x, int16_t target_y, int16_t non_ship_target) {
        if (s_done || s_tracing) {
            return;
        }
        const char* dir = antares::Get_Env("BEAM_BENCH");
        if (dir == nullptr || !antares::combat::Legacy_Draws() || non_ship_target != 0 || target <= 0 || attacker <= 0) {
            return;
        }
        s_done = true;
        const std::string path = std::string(dir) + "/beam_bench.jsonl";
        s_out = fopen(path.c_str(), "w");
        if (s_out == nullptr) {
            return;
        }
        timer::Handle_Animation_Speed_Key(key::KEY_PAGE_UP); // no waiting between ticks
        // ANTARES_BEAM_BENCH_X2=1: the x2 speed (the scheduler steps two frames a tick).
        if (const char* x2 = antares::Get_Env("BEAM_BENCH_X2"); x2 != nullptr && strcmp(x2, "1") == 0) {
            MOX::speedx2_flag = 1;
        }

        // The non-recipe weapons' 16-colour fragments (BEAMS.LBX entry 0's palette animation;
        // Fire_Weapon copies fragment N into the slot's 16 colours), 6-bit as the game keeps them.
        std::string fragments = "{\"fragments\":[";
        for (int16_t f = 0; f < 10; ++f) {
            fragments += f ? ",[" : "[";
            for (int16_t i = 0; i < 16; ++i) {
                uint8_t r = 0, g = 0, b = 0;
                remap::Get_Palette_Animation_Color(BEAMS::l_anims, f * 16 + i, &r, &g, &b);
                char rgb[24];
                snprintf(rgb, sizeof(rgb), "%s[%d,%d,%d]", i ? "," : "", r, g, b);
                fragments += rgb;
            }
            fragments += "]";
        }
        fragments += "],\"shield_diameters\":[";
        for (int i = 0; i < 8; ++i) {
            fragments += (i ? "," : "") + std::to_string(BEAMS::shield_diameters[i]);
        }
        fragments += "],\"reflection_diameters\":[";
        for (int i = 0; i < 4; ++i) {
            fragments += (i ? "," : "") + std::to_string(BEAMS::reflection_field_diameters[i]);
        }
        fragments += "]}\n";
        fputs(fragments.c_str(), s_out);

        const std::vector<s_bench_case> cases = Cases();
        for (size_t c = 0; c < cases.size(); ++c) {
            s_beam_fx_entry entries[16];
            memset(entries, 0, sizeof(entries));
            std::string line = "{\"case\":" + std::to_string(c) + ",\"entries\":[";
            for (size_t e = 0; e < cases[c].entries.size(); ++e) {
                const s_case_entry& ce = cases[c].entries[e];
                entries[e].weapon_id = ce.weapon;
                entries[e].shot_count = ce.count;
                entries[e].result_flags = ce.result;
                entries[e].special_flag = ce.special;
                entries[e].accum_hit_x = ce.hits;
                entries[e].accum_hit_y = 0;
                entries[e].specials = ce.specials;
                char buf[160];
                snprintf(buf, sizeof(buf), "%s{\"weapon\":%d,\"count\":%d,\"result\":%d,\"special\":%d,\"hits\":%d,\"specials\":%d,\"type\":%d}", e ? "," : "", ce.weapon, ce.count, ce.result, ce.special, ce.hits, ce.specials, TECHDATA::weapons[ce.weapon].type);
                line += buf;
            }
            line += "],";
            Ship_Json(&line, "attacker", attacker, attacker_x, attacker_y);
            line += ",";
            Ship_Json(&line, "target", target, target_x, target_y);
            char tail[96];
            snprintf(tail, sizeof(tail), ",\"bearing\":%d,\"speedx2\":%d}\n", CMBTFIRE::Facing_Shield(CMBTDRW1::Relative_Bearing(attacker, target)), MOX::speedx2_flag | MOX::net_flag);
            line += tail;
            fputs(line.c_str(), s_out);

            s_case = (int32_t)c;
            s_tick = -1;
            s_tracing = true;
            BEAMS::Beam_SFX(attacker, attacker_x, attacker_y, target, target_x, target_y, entries, 0);
            s_tracing = false;
            fprintf(s_out, "{\"end\":%d,\"ticks\":%d}\n", (int)c, (int)(s_tick + 1));
        }
        fclose(s_out);
        s_out = nullptr;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "beam bench: %zu cases written to %s", cases.size(), path.c_str());
        std::_Exit(0);
    }

    void Tick() {
        if (Tracing()) {
            ++s_tick;
        }
    }

    void Fire(int16_t from_x, int16_t from_y, int16_t to_x, int16_t to_y, int16_t weapon, int16_t frame, int16_t slot, int16_t fire_point, int16_t attacker_facing, int16_t target_facing) {
        if (Tracing()) {
            fprintf(s_out, "{\"t\":%d,\"fire\":[%d,%d,%d,%d],\"weapon\":%d,\"frame\":%d,\"slot\":%d,\"fp\":%d,\"facings\":[%d,%d]}\n", s_tick, from_x, from_y, to_x, to_y, weapon, frame, slot, fire_point, attacker_facing, target_facing);
        }
    }

    void Segment(int16_t frame, int16_t max_frames, int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t hit, int16_t slot, int16_t step, int16_t length) {
        if (Tracing()) {
            fprintf(s_out, "{\"t\":%d,\"seg\":[%d,%d,%d,%d],\"frame\":%d,\"max\":%d,\"hit\":%d,\"slot\":%d,\"step\":%d,\"len\":%d}\n", s_tick, x1, y1, x2, y2, frame, max_frames, hit, slot, step, length);
        }
    }

    void Burst(int16_t x, int16_t y, int16_t dir, int16_t variant, int16_t frame, int16_t slot) {
        if (Tracing()) {
            fprintf(s_out, "{\"t\":%d,\"burst\":[%d,%d],\"dir\":%d,\"variant\":%d,\"frame\":%d,\"slot\":%d}\n", s_tick, x, y, dir, variant, frame, slot);
        }
    }

    void Explosion(int16_t x, int16_t y, int16_t type, int16_t frame, int16_t size) {
        if (Tracing()) {
            fprintf(s_out, "{\"t\":%d,\"explosion\":[%d,%d],\"type\":%d,\"frame\":%d,\"slot\":%d}\n", s_tick, x, y, type, frame, size);
        }
    }

    void Shield(int16_t x, int16_t y, int16_t ship, int16_t special, int16_t heavy, int16_t rotation, int16_t frame, int16_t status) {
        if (Tracing()) {
            fprintf(s_out, "{\"t\":%d,\"shield\":[%d,%d],\"ship\":%d,\"special\":%d,\"heavy\":%d,\"rotation\":%d,\"frame\":%d,\"status\":%d}\n", s_tick, x, y, ship, special, heavy, rotation, frame, status);
        }
    }

    void Beam2(int32_t start_x, int32_t start_y, int32_t end_x, int32_t end_y, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t frame, int32_t palette_base, int32_t style) {
        if (Tracing()) {
            std::string ramp;
            for (int32_t i = 0; i < 16; ++i) {
                const auto& c = video::current_palette[(palette_base + i) & 0xFF];
                char rgb[24];
                snprintf(rgb, sizeof(rgb), "%s[%d,%d,%d]", i ? "," : "", c.r, c.g, c.b);
                ramp += rgb;
            }
            fprintf(s_out, "{\"t\":%d,\"beam2\":[%d,%d,%d,%d,%d,%d,%d,%d],\"frame\":%d,\"base\":%d,\"style\":%d,\"ramp\":[%s]}\n", s_tick, start_x, start_y, end_x, end_y, x1, y1, x2, y2, frame, palette_base, style, ramp.c_str());
        }
    }

    void Strand(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const uint8_t* colors, int32_t count) {
        if (Tracing()) {
            std::string list;
            for (int32_t i = 0; i < count; ++i) {
                list += (i ? "," : "") + std::to_string(colors[i]);
            }
            fprintf(s_out, "{\"t\":%d,\"strand\":[%d,%d,%d,%d],\"colors\":[%s]}\n", s_tick, x1, y1, x2, y2, list.c_str());
        }
    }

    void Sound(int16_t sound_id) {
        if (Tracing()) {
            fprintf(s_out, "{\"t\":%d,\"sound\":%d}\n", s_tick, sound_id);
        }
    }
} // namespace antares::beam_bench
