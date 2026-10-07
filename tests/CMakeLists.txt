include(FetchContent)
FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

add_executable(opticalnet_unit_tests
  unit/ErrorTests.cpp
  unit/IdTests.cpp
  unit/TransceiverTests.cpp
  unit/SwitchingElementTests.cpp
  unit/NodeTests.cpp
  unit/FiberLinkTests.cpp
  unit/NetworkTests.cpp
  unit/TopologyOperationTests.cpp
  unit/TopologyGraphTests.cpp
  unit/TopologyValidationTests.cpp
  unit/TopologyIoTests.cpp
)
target_compile_definitions(opticalnet_unit_tests PRIVATE
  OPTICALNET_CONFIG_DIR="${PROJECT_SOURCE_DIR}/configs")
target_link_libraries(opticalnet_unit_tests PRIVATE opticalnet::topology opticalnet::core opticalnet_warnings GTest::gtest_main)
gtest_discover_tests(opticalnet_unit_tests)
