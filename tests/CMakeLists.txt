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
  unit/PathTests.cpp
  unit/RoutingBasicTests.cpp
  unit/RoutingConstraintTests.cpp
  unit/RoutingPropertyTests.cpp
  unit/ResourceManagerTests.cpp
  unit/ConnectionManagerTests.cpp
  unit/SimulationPrimitivesTests.cpp
  unit/RequestGeneratorTests.cpp
  unit/SimulationEngineTests.cpp
  unit/ThreadPoolTests.cpp
  unit/StatisticsTests.cpp
  unit/SimulationStudyTests.cpp
  unit/FailureStateTests.cpp
  unit/FailureRoutingTests.cpp
  unit/FailureSimulationTests.cpp
  unit/FailureStudyTests.cpp
  unit/OptimizationTests.cpp
)
target_compile_definitions(opticalnet_unit_tests PRIVATE
  OPTICALNET_CONFIG_DIR="${PROJECT_SOURCE_DIR}/configs")
target_link_libraries(opticalnet_unit_tests PRIVATE opticalnet::simulation opticalnet::resources opticalnet::routing opticalnet::topology opticalnet::core opticalnet_warnings GTest::gtest_main)
gtest_discover_tests(opticalnet_unit_tests)

# Phase 8: the validation suite is its own executable (project-specific invariants, reference models, randomized and
# end-to-end validation), so it can be run and mutation-tested independently of the unit tests.
add_executable(opticalnet_validation_tests
  validation/ValidatorSelfTests.cpp
  validation/TopologyValidationTests.cpp
  validation/RoutingValidationTests.cpp
  validation/ResourceValidationTests.cpp
  validation/ConnectionValidationTests.cpp
  validation/SimulationValidationTests.cpp
  validation/FailureValidationTests.cpp
  validation/DeterminismValidationTests.cpp
  validation/ScenarioValidationTests.cpp
  validation/GoldenFixtureTests.cpp
  validation/NegativeInputTests.cpp
  validation/StressValidationTests.cpp
  validation/GeneratorTests.cpp
)
target_include_directories(opticalnet_validation_tests PRIVATE unit)
target_compile_definitions(opticalnet_validation_tests PRIVATE
  VALIDATION_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/validation/fixtures"
  OPTICALNET_CONFIG_DIR_FOR_VALIDATION="${PROJECT_SOURCE_DIR}/configs")
target_link_libraries(opticalnet_validation_tests PRIVATE opticalnet::validation opticalnet::simulation opticalnet::resources
  opticalnet::routing opticalnet::topology opticalnet::core opticalnet_warnings GTest::gtest_main)
gtest_discover_tests(opticalnet_validation_tests)
