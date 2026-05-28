cmake_minimum_required(VERSION 3.20)
project(c_model CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Locate elaborated headers from spec_validate
set(SPEC_VALIDATE_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/../spec_validate/include")
if(NOT EXISTS "${SPEC_VALIDATE_INCLUDE}/ni_flit_constants.h")
  message(FATAL_ERROR "spec_validate include dir not found: ${SPEC_VALIDATE_INCLUDE}")
endif()

# Drift gate: refuse to build if codegen output is stale.
add_custom_target(codegen_check
  COMMAND py -3 ${CMAKE_CURRENT_SOURCE_DIR}/../spec_validate/tools/codegen.py --check
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../spec_validate
  COMMENT "Verifying codegen artifacts are not stale"
)

include_directories(${SPEC_VALIDATE_INCLUDE})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# Windows MSVC compatibility
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_subdirectory(tests)
