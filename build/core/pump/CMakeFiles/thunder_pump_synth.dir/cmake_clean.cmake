file(REMOVE_RECURSE
  "libthunder_pump_synth.a"
  "libthunder_pump_synth.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang CXX)
  include(CMakeFiles/thunder_pump_synth.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
