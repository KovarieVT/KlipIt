add_executable(klipit_unit_tests test_unit.cpp)
target_link_libraries(klipit_unit_tests PRIVATE klipit_core Qt6::Test)
add_test(NAME unit COMMAND klipit_unit_tests -platform offscreen)

# Website sign-in and clip upload against a stand-in for the Wix API (no internet needed).
add_executable(klipit_web_test test_web.cpp)
target_link_libraries(klipit_web_test PRIVATE klipit_core Qt6::Test)
add_test(NAME unit_web COMMAND klipit_web_test -platform offscreen)

# Real capture engine end to end (needs a display: tests/run_engine_test.sh on Linux).
add_executable(klipit_engine_test test_engine.cpp)
target_link_libraries(klipit_engine_test PRIVATE klipit_core Qt6::Test)

# Renders the app icon and installer artwork from the same drawing code as the app.
add_executable(klipit_render_art render_art.cpp)
target_link_libraries(klipit_render_art PRIVATE klipit_core)
