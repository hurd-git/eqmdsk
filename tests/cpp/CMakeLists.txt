add_executable(eqmdsk_core_tests test_core.cpp)
target_link_libraries(eqmdsk_core_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_core_tests PRIVATE cxx_std_17)
add_test(NAME eqmdsk_core_tests COMMAND eqmdsk_core_tests)

add_executable(eqmdsk_gfile_tests test_gfile.cpp)
target_link_libraries(eqmdsk_gfile_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_gfile_tests PRIVATE cxx_std_17)
target_compile_definitions(eqmdsk_gfile_tests PRIVATE
  EQMDSK_LOCAL_DATA_DIR="${PROJECT_SOURCE_DIR}/../data"
)
add_test(NAME eqmdsk_gfile_tests COMMAND eqmdsk_gfile_tests)

add_executable(eqmdsk_sfile_tests test_sfile.cpp)
target_link_libraries(eqmdsk_sfile_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_sfile_tests PRIVATE cxx_std_17)
add_test(NAME eqmdsk_sfile_tests COMMAND eqmdsk_sfile_tests)
