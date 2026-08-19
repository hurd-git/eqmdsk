add_executable(eqmdsk_core_tests test_core.cpp)
target_link_libraries(eqmdsk_core_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_core_tests PRIVATE cxx_std_17)
add_test(NAME eqmdsk_core_tests COMMAND eqmdsk_core_tests)

