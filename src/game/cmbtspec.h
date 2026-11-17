#pragma once
#include <cstdint>

namespace CMBTSPEC {
    extern int32_t g_engine_sfx_handle;
    extern uint8_t leader_dies_with_no;
    extern uint8_t compressor_per_size_class_damage;

    struct s_cloak_animation_steps {
        uint8_t normal;
        uint8_t fast;
    };

    struct s_animation {
        s_cloak_animation_steps cloak_steps;
    };

    extern s_animation animation;

    struct s_anti_missile_rocket_accuracy {
        uint8_t base;
        uint8_t range_multiplier;
        uint8_t range_divisor;
    };

    extern s_anti_missile_rocket_accuracy anti_missile_rocket_accuracy;

    /** Draws the cloaking or decloaking effect for a combat ship.
     * @param ship_idx Combat ship index to animate.
     * @param cloak_type Cloak effect variant to display.
     */
    void __cdecl Draw_Cloak(int16_t ship_idx, int16_t cloak_type);

    /** Draws the persistent tractor beam between a ship and its stored target.
     * @param ship_idx Combat ship index emitting the beam.
     */
    void __cdecl Draw_Continuing_Tractor(int16_t ship_idx);

    /** Validates and returns the active tractor-beam target for a combat ship.
     * Invalid or stale tractor links are cleared in place.
     * @param ship_idx Combat ship index emitting the beam.
     * @param target_ship_idx Output target ship index when a valid link exists.
     * @return Nonzero when the ship still has a valid active tractor target.
     */
    bool __cdecl Try_Get_Active_Tractor_Target(int16_t ship_idx, int16_t* target_ship_idx);

    /** Starts the looping ship engine sound effect if it is not already active. */
    void __cdecl Start_Ship_Engine_SFX();

    /** Stops the looping ship engine sound effect. */
    void __cdecl End_Ship_Engine_SFX();

    /** Draws the moving tractor beam using the current dragged-ship screen center.
     * @param ship_idx Combat ship index emitting the beam.
     * @param center_x Current dragged center x position in pixels.
     * @param center_y Current dragged center y position in pixels.
     */
    void __cdecl Draw_Moving_Tractor(int16_t ship_idx, int16_t center_x, int16_t center_y);

    /** Animates the lightning field effect between a ship and a missile target point.
     * @param ship_idx Combat ship index emitting the effect.
     * @param missile_idx Missile index being struck.
     * @param target_x Target x position in pixels.
     * @param target_y Target y position in pixels.
     */
    void __cdecl Lightning_Field(int16_t ship_idx, int16_t missile_idx, int16_t target_x, int16_t target_y);

    /** Plays the ship destruction visual effect at the given combat position.
     * @param ship_idx Combat ship index being destroyed.
     * @param x Ship x position in pixels.
     * @param y Ship y position in pixels.
     * @param explosion_type Explosion effect variant.
     */
    void __cdecl Destroy_Ship_FX(int16_t ship_idx, int16_t x, int16_t y, int16_t explosion_type);
    // WP3.9d (A5): what Destroy_Ship_FX's rules do, without drawing (super-fast, or a native battle).
    void Destroy_Ship_Rules(int16_t ship_idx, int16_t x, int16_t y, int16_t explosion_type);

    /** Draws a stasis ball sprite centered on a combat ship.
     * @param ship_idx Combat ship index to center on.
     * @param scale_percent Scale percentage for the effect sprite.
     * @param off_x Extra x offset in pixels.
     * @param off_y Extra y offset in pixels.
     */
    void __cdecl Draw_Stasis_Ball(int16_t ship_idx, int16_t scale_percent, int16_t off_x, int16_t off_y);

    /** Draws the plasma web overlay for a combat ship.
     * @param ship_idx Combat ship index to overlay.
     * @param x_off Additional x offset in pixels.
     * @param y_off Additional y offset in pixels.
     */
    void __cdecl Draw_Plasma_Web(int16_t ship_idx, int16_t x_off, int16_t y_off);

    /** Plays a spherical area-effect animation and applies its combat damage.
     * @param source_ship_idx Combat ship index at the effect center.
     * @param effect_x Center x position in pixels.
     * @param effect_y Center y position in pixels.
     * @param effect_type Area-effect subtype to use.
     * @param shot_count Damage-strength shot count for the effect.
     */
    void __cdecl Do_Spherical_SFX(int16_t source_ship_idx, int16_t effect_x, int16_t effect_y, int16_t effect_type, int16_t shot_count);

    /** Draws the stasis beam effect between two combat ships.
     * @param source_ship_idx Combat ship index firing the beam.
     * @param target_ship_idx Combat ship index being struck.
     */
    void __cdecl Draw_Stasis_Beam(int16_t source_ship_idx, int16_t target_ship_idx);

    /** Draws anti-missile rockets traveling from a ship toward a missile.
     * @param ship_idx Combat ship index launching the rockets.
     * @param target_x Ship x origin in pixels.
     * @param target_y Ship y origin in pixels.
     * @param missile_idx Missile index being targeted.
     * @param tech_level Anti-missile tech level affecting visuals.
     */
    void __cdecl Draw_Anti_Missile_Rockets(int16_t ship_idx, int16_t target_x, int16_t target_y, int16_t missile_idx, int16_t tech_level);

    /** Draws the small anti-missile rocket marker at a screen position.
     * @param x Screen x position in pixels.
     * @param y Screen y position in pixels.
     */
    void __cdecl Draw_Anti_Missile_Rocket_Picture(int16_t x, int16_t y);

    /** Resolves anti-missile rocket fire against a missile stack.
     * @param ship_idx Combat ship index firing the rockets.
     * @param ship_x Ship x position in pixels.
     * @param ship_y Ship y position in pixels.
     * @param missile_idx Missile index being attacked.
     * @param weapon_slot_idx Weapon slot used for the attack.
     * @param tech_level Anti-missile tech level affecting hit chance.
     */
    void __cdecl Anti_Missile_Rockets(int16_t ship_idx, int16_t ship_x, int16_t ship_y, int16_t missile_idx, int16_t weapon_slot_idx, int16_t tech_level);

    /** Plays the gyro destabilizer effect and applies its structural damage.
     * @param source_idx Combat ship index using the special.
     * @param target_idx Combat ship index being struck.
     * @param attack_count Number of gyro attacks to resolve.
     */
    void __cdecl Gyro_Destablizer(int16_t source_idx, int16_t target_idx, int16_t attack_count);

    /** Draws the gyro destabilizer beam effect between two ships.
     * @param source_ship_idx Combat ship index emitting the effect.
     * @param target_ship_idx Combat ship index being struck.
     */
    void __cdecl Draw_Gyro_Special_Effect(int16_t source_ship_idx, int16_t target_ship_idx);

    /** Resolves a plasma web attack between two combat ships.
     * @param attacker_idx Combat ship index firing the web.
     * @param target_idx Combat ship index being struck.
     * @param weapon_slot_idx Weapon slot used for the attack.
     */
    void __cdecl Plasma_Web(int16_t attacker_idx, int16_t target_idx, int16_t weapon_slot_idx);

    /** Plays the stellar converter beam and applies its damage.
     * @param attacker_idx Combat ship index firing the weapon.
     * @param target_idx Combat ship index being struck.
     * @param num_shots Number of shots being resolved.
     */
    void __cdecl Stellar_Converter(int16_t attacker_idx, int16_t target_idx, int16_t num_shots);

    /** Resolves a tractor beam effect between two combat ships.
     * @param source_ship_idx Combat ship index applying the tractor beam.
     * @param target_ship_idx Combat ship index being tractored.
     */
    void __cdecl Tractor_Beam(int16_t source_ship_idx, int16_t target_ship_idx);

    /** Draws the transporter bomb beam from a ship to a target point.
     * @param ship_idx Combat ship index emitting the beam.
     * @param target_x Target x position in pixels.
     * @param target_y Target y position in pixels.
     */
    void __cdecl Draw_Transporter_Bomb_Beam(int16_t ship_idx, int16_t target_x, int16_t target_y);

    /** Animates a launched missile until impact, retarget, or travel exhaustion.
     * @param missile_idx Missile index being advanced.
     * @param tx Target x position in pixels.
     * @param ty Target y position in pixels.
     * @param is_targeting_missile Nonzero when the target is another missile.
     */
    void __cdecl Missile_Launch_FX(int16_t missile_idx, int16_t tx, int16_t ty, int16_t is_targeting_missile);

    /** Plays a ship explosion sound appropriate for the destroyed ship size.
     * @param size_class Destroyed ship size class.
     */
    void __cdecl Play_Ship_Explosion_SFX(int16_t size_class);

    /** Draws a shrinking collapsing ship frame during spherical destruction effects.
     * @param ship_idx Combat ship index being drawn.
     * @param x Draw x position in pixels.
     * @param y Draw y position in pixels.
     * @param scale Scale percentage for the collapse frame.
     * @param rotation_offset Temporary facing rotation offset.
     */
    void __cdecl Draw_Ship_Collapsing(int16_t ship_idx, int16_t x, int16_t y, int16_t scale, int8_t rotation_offset);
} // namespace CMBTSPEC