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

if(OPTICALNET_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
