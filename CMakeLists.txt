cmake_minimum_required(VERSION 3.20)
project(Frost VERSION 0.1.0 LANGUAGES C)
add_library(frost_dsp STATIC
  engine/chain.c
  engine/preset.c
  engine/effect.c
  effects/gain/gain.c
  effects/noise_gate/noise_gate.c
  effects/eq/eq.c
  effects/cabinet_ir/ir.c
  effects/overdrive/overdrive.c
  effects/delay/delay.c
  effects/modulation/modulation.c
  effects/chorus/chorus.c
  effects/flanger/flanger.c
  effects/reverb/reverb.c
  effects/reverb/reverb16.c
  effects/common/frost_dsp.c
  effects/drives/drives.c
  effects/dynamics/dynamics.c
  effects/modulators/modulators.c
  effects/octave/octave.c
  effects/drop_tune/drop_tune.c
  effects/delays/delays.c
  effects/reverbs/reverbs.c
  effects/reverbs/reverbs16.c
  effects/ns1x/ns1x.c
  effects/multiband/multiband.c
  effects/highpass/highpass.c
  effects/lowpass/lowpass.c
  effects/autowah/autowah.c
  effects/treble_boost/treble_boost.c
  effects/foxx/foxx.c
  effects/tape/tape.c
  effects/vintage_fuzz/vintage_fuzz.c
  effects/acoustic/acoustic.c
  effects/amp_heads/amp_heads.c
  effects/fcm/fcm_effect.c
  effects/fcm/fcm_transfer.c
)
target_compile_features(frost_dsp PUBLIC c_std_99)
target_include_directories(frost_dsp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT WIN32)
  target_link_libraries(frost_dsp PUBLIC m)
endif()
add_library(frost_control STATIC firmware/protocol/slots.c firmware/protocol/preset_transfer.c firmware/protocol/preset_read.c firmware/protocol/policy.c)
target_compile_features(frost_control PUBLIC c_std_99)
target_link_libraries(frost_control PUBLIC frost_dsp)
if(MSVC)
  target_compile_options(frost_dsp PRIVATE /W4 /fp:strict)
  target_compile_options(frost_control PRIVATE /W4 /fp:strict)
else()
  target_compile_options(frost_dsp PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
  target_compile_options(frost_control PRIVATE -Wall -Wextra -Werror -ffp-contract=off)
endif()
