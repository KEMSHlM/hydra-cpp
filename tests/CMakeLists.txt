add_executable(hydra-cpp-tests test_main.cpp)

target_include_directories(hydra-cpp-tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(hydra-cpp-tests PRIVATE hydra-cpp-lib)

add_test(NAME hydra-cpp-tests COMMAND hydra-cpp-tests)

add_executable(hydra-c-api-tests test_c_api.c)

target_include_directories(hydra-c-api-tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(hydra-c-api-tests PRIVATE hydra-cpp-lib)

set_target_properties(hydra-c-api-tests PROPERTIES LINKER_LANGUAGE CXX)

add_test(NAME hydra-c-api-tests COMMAND hydra-c-api-tests)

add_executable(hydra-c-integration-tests test_c_integration.c)

target_include_directories(hydra-c-integration-tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

target_link_libraries(hydra-c-integration-tests PRIVATE hydra-cpp-lib)

set_target_properties(hydra-c-integration-tests PROPERTIES LINKER_LANGUAGE CXX)

add_test(NAME hydra-c-integration-tests COMMAND hydra-c-integration-tests)
