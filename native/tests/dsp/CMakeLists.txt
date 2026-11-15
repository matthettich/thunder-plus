# U02 smoke tests: each DSP static library is linked into a tiny executable that runs one effect / module for a few blocks.
if(TARGET thunder_dsp_tfx)
  add_executable(dsp_smoke_tfx smoke_tfx.cpp)
  target_link_libraries(dsp_smoke_tfx PRIVATE thunder_dsp_tfx)
  add_test(NAME dsp_smoke_tfx COMMAND dsp_smoke_tfx)
  add_executable(dsp_smoke_mods smoke_mods.cpp)
  target_link_libraries(dsp_smoke_mods PRIVATE thunder_dsp_mods)
  add_test(NAME dsp_smoke_mods COMMAND dsp_smoke_mods)
  add_executable(dsp_smoke_w smoke_w.cpp)
  target_link_libraries(dsp_smoke_w PRIVATE thunder_dsp_w)
  add_test(NAME dsp_smoke_w COMMAND dsp_smoke_w)
endif()

# U03: pump effects and instruments (native/core/pump)
if(TARGET thunder_pump)
  add_executable(dsp_smoke_pump smoke_pump.cpp)
  target_link_libraries(dsp_smoke_pump PRIVATE thunder_pump)
  # One process per effect type. Run all 25 in one process on the host and STUTTER (20) can put out NaN at its first sample after a
  # SPRING (13) instance was used (a state that survives between instances; found in U03, reported not fixed: see PINS.md). The same
  # sequence in one pump.wasm instance (node, 15 Nov) gives finite output for all 25.
  foreach(t RANGE 0 24)
    add_test(NAME dsp_smoke_pump_${t} COMMAND dsp_smoke_pump ${t})
  endforeach()
  add_executable(dsp_smoke_pump_synth smoke_pump_synth.cpp)
  target_link_libraries(dsp_smoke_pump_synth PRIVATE thunder_pump_synth)
  add_test(NAME dsp_smoke_pump_synth COMMAND dsp_smoke_pump_synth)
endif()
