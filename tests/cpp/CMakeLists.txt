function(eqmdsk_enable_test_assertions target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /UNDEBUG)
  else()
    target_compile_options(${target} PRIVATE -UNDEBUG)
  endif()
endfunction()

add_executable(eqmdsk_core_tests test_core.cpp)
target_link_libraries(eqmdsk_core_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_core_tests PRIVATE cxx_std_17)
eqmdsk_enable_test_assertions(eqmdsk_core_tests)
add_test(NAME eqmdsk_core_tests COMMAND eqmdsk_core_tests)

add_executable(eqmdsk_gfile_tests test_gfile.cpp)
target_link_libraries(eqmdsk_gfile_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_gfile_tests PRIVATE cxx_std_17)
eqmdsk_enable_test_assertions(eqmdsk_gfile_tests)
target_compile_definitions(eqmdsk_gfile_tests PRIVATE
  EQMDSK_LOCAL_DATA_DIR="${PROJECT_SOURCE_DIR}/../data"
)
add_test(NAME eqmdsk_gfile_tests COMMAND eqmdsk_gfile_tests)

add_executable(eqmdsk_afile_tests test_afile.cpp)
target_link_libraries(eqmdsk_afile_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_afile_tests PRIVATE cxx_std_17)
eqmdsk_enable_test_assertions(eqmdsk_afile_tests)
target_compile_definitions(eqmdsk_afile_tests PRIVATE
  EQMDSK_LOCAL_DATA_DIR="${PROJECT_SOURCE_DIR}/../data"
)
add_test(NAME eqmdsk_afile_tests COMMAND eqmdsk_afile_tests)

add_executable(eqmdsk_kfile_tests test_kfile.cpp)
target_link_libraries(eqmdsk_kfile_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_kfile_tests PRIVATE cxx_std_17)
eqmdsk_enable_test_assertions(eqmdsk_kfile_tests)
if(EXISTS "${PROJECT_SOURCE_DIR}/../data/k067590.03300")
  add_test(NAME eqmdsk_kfile_tests COMMAND eqmdsk_kfile_tests
    "${PROJECT_SOURCE_DIR}/../data/k067590.03300"
  )
else()
  add_test(NAME eqmdsk_kfile_tests COMMAND eqmdsk_kfile_tests)
endif()

add_executable(eqmdsk_sfile_tests test_sfile.cpp)
target_link_libraries(eqmdsk_sfile_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_sfile_tests PRIVATE cxx_std_17)
eqmdsk_enable_test_assertions(eqmdsk_sfile_tests)
add_test(NAME eqmdsk_sfile_tests COMMAND eqmdsk_sfile_tests)

add_executable(eqmdsk_fuzz_smoke_tests test_fuzz_smoke.cpp)
target_link_libraries(eqmdsk_fuzz_smoke_tests PRIVATE eqmdsk::eqmdsk)
target_compile_features(eqmdsk_fuzz_smoke_tests PRIVATE cxx_std_17)
eqmdsk_enable_test_assertions(eqmdsk_fuzz_smoke_tests)
add_test(NAME eqmdsk_fuzz_smoke_tests COMMAND eqmdsk_fuzz_smoke_tests)
