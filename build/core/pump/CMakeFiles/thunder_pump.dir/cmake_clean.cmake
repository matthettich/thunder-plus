file(REMOVE_RECURSE
  "libthunder_pump.a"
  "libthunder_pump.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang CXX)
  include(CMakeFiles/thunder_pump.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
