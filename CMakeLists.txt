cmake_minimum_required(VERSION 3.20)
project(OpticalNet VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(OPTICALNET_BUILD_TESTS "Build the OpticalNet test suite" ON)
option(OPTICALNET_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

add_library(opticalnet_warnings INTERFACE)
if(MSVC)
  target_compile_options(opticalnet_warnings INTERFACE /W4 /permissive- /Zc:__cplusplus)
  if(OPTICALNET_WARNINGS_AS_ERRORS)
    target_compile_options(opticalnet_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(opticalnet_warnings INTERFACE -Wall -Wextra -Wpedantic)
  if(OPTICALNET_WARNINGS_AS_ERRORS)
    target_compile_options(opticalnet_warnings INTERFACE -Werror)
  endif()
endif()

add_library(opticalnet_core STATIC
  src/core/Error.cpp
  src/core/Transceiver.cpp
  src/core/SwitchingElement.cpp
  src/core/Node.cpp
  src/core/FiberLink.cpp
  src/core/Network.cpp
)
add_library(opticalnet::core ALIAS opticalnet_core)
target_include_directories(opticalnet_core PUBLIC include)
target_compile_features(opticalnet_core PUBLIC cxx_std_20)
target_link_libraries(opticalnet_core PRIVATE opticalnet_warnings)

include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

add_library(opticalnet_topology STATIC
  src/topology/LinkCost.cpp
  src/topology/Topology.cpp
  src/topology/TopologyIo.cpp
)
add_library(opticalnet::topology ALIAS opticalnet_topology)
target_link_libraries(opticalnet_topology
  PUBLIC opticalnet::core
  PRIVATE opticalnet_warnings nlohmann_json::nlohmann_json)

add_library(opticalnet_routing STATIC
  src/routing/Path.cpp
  src/routing/DijkstraRouter.cpp
  src/routing/RoutingEngine.cpp
)
add_library(opticalnet::routing ALIAS opticalnet_routing)
target_link_libraries(opticalnet_routing
  PUBLIC opticalnet::topology
  PRIVATE opticalnet_warnings)

add_library(opticalnet_resources STATIC
  src/resources/ResourceManager.cpp
  src/resources/ConnectionManager.cpp
)
add_library(opticalnet::resources ALIAS opticalnet_resources)
target_link_libraries(opticalnet_resources
  PUBLIC opticalnet::routing
  PRIVATE opticalnet_warnings)

add_library(opticalnet_simulation STATIC
  src/simulation/Rng.cpp
  src/simulation/EventQueue.cpp
  src/simulation/SimulationConfig.cpp
  src/simulation/RequestGenerator.cpp
  src/simulation/SimulationEngine.cpp
)
add_library(opticalnet::simulation ALIAS opticalnet_simulation)
target_link_libraries(opticalnet_simulation
  PUBLIC opticalnet::resources
  PRIVATE opticalnet_warnings)

if(OPTICALNET_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
