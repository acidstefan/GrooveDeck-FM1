/* Build.ps1 prueft, dass diese nativen Kernroutinen der Bibliothek im Abbild und im SRAM liegen.
 * GrooveDeck nutzt eigene DSP; die Verweise halten sie nur gegen --gc-sections. */
#include <stdint.h>
extern char groove_sequence_render[];
extern char groove_sequence_clock[];
extern char groove_sequence_position[];
extern char groove_sequence_transport[];
extern char fm1_sea_decode_groups[];
extern char fm1_codec_sea_mono_s16_run[];
extern char synth_dx7_render[];
extern char synth_dx7_midi[];
extern char synth_dx7_key[];
extern char fm1_dx7_panic[];
extern char fm_exp2_fixed[];
extern char bass303_bank_render[];
extern char bass303_bank_key[];
extern char bass303_bank_set[];
extern char drum808_kit_render[];
extern char drum808_kit_trigger[];
extern char psg_render[];
extern char psg_key[];
extern char psg_midi[];
extern char psg_panic[];
extern char psg_set[];
extern char psg_get[];
extern char pluck_render[];
extern char pluck_key[];
extern char pluck_midi[];
extern char pluck_panic[];
extern char pluck_set[];
extern char pluck_get[];
extern char fm1_mix128[];
extern char fm1_mix_mono_stereo128[];
extern char fm1_bus_output128[];
extern char fm1_master_mix128[];
extern char fm1_master_gain[];
extern char fm1_declick128[];
extern char fm1_render_begin[];
extern char fm1_render_end[];
extern char synth_dx7_active[];
extern char bass303_bank_active[];
extern char drum808_kit_active[];
extern char psg_active[];
extern char pluck_active[];
extern char sampledrum_active[];
extern char synth_dx7_limit[];
extern char psg_limit[];
extern char pluck_limit[];
extern char sampledrum_limit[];
extern char bass303_advance_idle[];
extern char fm1_send128[];
extern char fm1_fx_render[];
extern char fm1_fx_clear[];
extern char fm1_fx_active[];
extern char sampledrum_render[];
extern char sampledrum_trigger[];
extern char sampledrum_panic[];
extern char psg_active_chips[];
extern char bass303_render[];
extern char drum808_kit_render_st[];
extern char fm1_fx_set[];
extern char dx7_compute_pure_store_64_asm[];
extern char dx7_compute_mod_add_64_asm[];
extern char psg_mix_asm[];
extern char psg_dc_asm[];
extern char drum808_q24_add32_asm[];
extern char bass303_mix128_asm[];
extern char sampledrum_lpf20_asm[];
extern char sampledrum_mix_asm[];
extern char groove_effects_prepare128_asm[];
extern char psg_noise_mix_asm[];
extern char fm1_master_sum256_asm[];
extern char fm1_master_scale256_asm[];
extern char fm1_master_nominal256_asm[];
__attribute__((used)) const void *const volatile gd_keep_native[] = {
    groove_sequence_render,
    groove_sequence_clock,
    groove_sequence_position,
    groove_sequence_transport,
    fm1_sea_decode_groups,
    fm1_codec_sea_mono_s16_run,
    synth_dx7_render,
    synth_dx7_midi,
    synth_dx7_key,
    fm1_dx7_panic,
    fm_exp2_fixed,
    bass303_bank_render,
    bass303_bank_key,
    bass303_bank_set,
    drum808_kit_render,
    drum808_kit_trigger,
    psg_render,
    psg_key,
    psg_midi,
    psg_panic,
    psg_set,
    psg_get,
    pluck_render,
    pluck_key,
    pluck_midi,
    pluck_panic,
    pluck_set,
    pluck_get,
    fm1_mix128,
    fm1_mix_mono_stereo128,
    fm1_bus_output128,
    fm1_master_mix128,
    fm1_master_gain,
    fm1_declick128,
    fm1_render_begin,
    fm1_render_end,
    synth_dx7_active,
    bass303_bank_active,
    drum808_kit_active,
    psg_active,
    pluck_active,
    sampledrum_active,
    synth_dx7_limit,
    psg_limit,
    pluck_limit,
    sampledrum_limit,
    bass303_advance_idle,
    fm1_send128,
    fm1_fx_render,
    fm1_fx_clear,
    fm1_fx_active,
    sampledrum_render,
    sampledrum_trigger,
    sampledrum_panic,
    psg_active_chips,
    bass303_render,
    drum808_kit_render_st,
    fm1_fx_set,
    dx7_compute_pure_store_64_asm,
    dx7_compute_mod_add_64_asm,
    psg_mix_asm,
    psg_dc_asm,
    drum808_q24_add32_asm,
    bass303_mix128_asm,
    sampledrum_lpf20_asm,
    sampledrum_mix_asm,
    groove_effects_prepare128_asm,
    psg_noise_mix_asm,
    fm1_master_sum256_asm,
    fm1_master_scale256_asm,
    fm1_master_nominal256_asm,
};
