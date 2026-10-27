# pybind11 extension module `opticalnet._opticalnet` (Phase 9). Only configured when OPTICALNET_BUILD_PYTHON=ON.
find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)

include(FetchContent)
FetchContent_Declare(pybind11
  URL https://github.com/pybind/pybind11/archive/refs/tags/v2.13.6.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(pybind11)

pybind11_add_module(_opticalnet bindings.cpp)
target_link_libraries(_opticalnet PRIVATE opticalnet::validation opticalnet::simulation opticalnet_warnings)

# The module is placed inside the Python package so `import opticalnet` works from python/ without installation.
set(OPTICALNET_PYTHON_OUTPUT_DIR "${PROJECT_SOURCE_DIR}/python/opticalnet" CACHE PATH "Where the built extension module is written")
set_target_properties(_opticalnet PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${OPTICALNET_PYTHON_OUTPUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY "${OPTICALNET_PYTHON_OUTPUT_DIR}")
foreach(cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set_target_properties(_opticalnet PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY_${cfg} "${OPTICALNET_PYTHON_OUTPUT_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_${cfg} "${OPTICALNET_PYTHON_OUTPUT_DIR}")
endforeach()

# A custom output directory (e.g. a separate Debug build) also needs the package's Python sources next to the module.
get_filename_component(_opticalnet_pkg_parent "${OPTICALNET_PYTHON_OUTPUT_DIR}/.." ABSOLUTE)
if(NOT "${OPTICALNET_PYTHON_OUTPUT_DIR}" STREQUAL "${PROJECT_SOURCE_DIR}/python/opticalnet")
  add_custom_command(TARGET _opticalnet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/python/opticalnet/__init__.py" "${OPTICALNET_PYTHON_OUTPUT_DIR}/__init__.py")
endif()

if(OPTICALNET_BUILD_TESTS)
  add_test(NAME python_api_tests
    COMMAND ${Python_EXECUTABLE} -m pytest "${PROJECT_SOURCE_DIR}/tests/api" -q -p no:cacheprovider
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
  set_tests_properties(python_api_tests PROPERTIES ENVIRONMENT "OPTICALNET_PYTHON_DIR=${_opticalnet_pkg_parent}")
endif()
