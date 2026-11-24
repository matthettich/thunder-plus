file(REMOVE_RECURSE
  "libthunder_pump_pffft.a"
  "libthunder_pump_pffft.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/thunder_pump_pffft.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
