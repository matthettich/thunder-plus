# U34a: offline song render C API (WIP notes, being completed)

Header `native/core/include/thunder/render.h`, implementation `native/core/src/render/render_api.cpp`, tests `native/tests/test_render_api*.cpp`, `test_render_c.c`,
CLI `native/tools/thunder-render/main.cpp` re-based on the API (byte-identical WAVs before and after for ELECTRO plain / --fx / --master, page and core timing).
Final numbers, choices and the unverified list follow in the last commit of this unit.
