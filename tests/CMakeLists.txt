add_executable(arctra_api_smoke_test api_smoke_test.cpp)
target_link_libraries(arctra_api_smoke_test PRIVATE arctra::core)
arctra_set_project_warnings(arctra_api_smoke_test)
add_test(NAME api_smoke_test COMMAND arctra_api_smoke_test)

add_executable(arctra_cpf_test cpf_test.cpp)
target_link_libraries(arctra_cpf_test PRIVATE arctra::core)
target_compile_definitions(
    arctra_cpf_test
    PRIVATE ARCTRA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
arctra_set_project_warnings(arctra_cpf_test)
add_test(NAME cpf_parser COMMAND arctra_cpf_test)

add_executable(arctra_cpf_acquisition_test cpf_acquisition_test.cpp)
target_link_libraries(arctra_cpf_acquisition_test PRIVATE arctra::core)
target_compile_definitions(
    arctra_cpf_acquisition_test
    PRIVATE ARCTRA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
arctra_set_project_warnings(arctra_cpf_acquisition_test)
add_test(NAME cpf_acquisition_header COMMAND arctra_cpf_acquisition_test)

add_executable(arctra_satellite_file_test satellite_file_test.cpp)
target_link_libraries(arctra_satellite_file_test PRIVATE arctra::core)
target_compile_definitions(
    arctra_satellite_file_test
    PRIVATE ARCTRA_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
arctra_set_project_warnings(arctra_satellite_file_test)
add_test(NAME satellite_file COMMAND arctra_satellite_file_test)

add_executable(arctra_coordinates_test coordinates_test.cpp)
target_link_libraries(arctra_coordinates_test PRIVATE arctra::core)
arctra_set_project_warnings(arctra_coordinates_test)
add_test(NAME coordinates COMMAND arctra_coordinates_test)

add_executable(arctra_pass_finder_test pass_finder_test.cpp)
target_link_libraries(arctra_pass_finder_test PRIVATE arctra::core)
arctra_set_project_warnings(arctra_pass_finder_test)
add_test(NAME pass_finder COMMAND arctra_pass_finder_test)
