find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(JSB_CONTRACT_ROOT "${PROJECT_SOURCE_DIR}/contract")
file(STRINGS "${JSB_CONTRACT_ROOT}/VERSION" JSB_CONTRACT_VERSION LIMIT_COUNT 1)
if(NOT JSB_CONTRACT_VERSION MATCHES "^([1-9][0-9]*)\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "contract/VERSION must be a semantic version")
endif()
set(JSB_CONTRACT_MAJOR_VERSION "${CMAKE_MATCH_1}")
set(JSB_TELEMETRY_SCHEMA_VERSION 1)

execute_process(
  COMMAND git rev-parse HEAD
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  RESULT_VARIABLE JSB_GIT_COMMIT_RESULT
  OUTPUT_VARIABLE JSB_GIT_COMMIT
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT JSB_GIT_COMMIT_RESULT EQUAL 0 OR JSB_GIT_COMMIT STREQUAL "")
  set(JSB_GIT_COMMIT "unknown")
endif()
execute_process(
  COMMAND git branch --show-current
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  RESULT_VARIABLE JSB_GIT_BRANCH_RESULT
  OUTPUT_VARIABLE JSB_GIT_BRANCH
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT JSB_GIT_BRANCH_RESULT EQUAL 0 OR JSB_GIT_BRANCH STREQUAL "")
  set(JSB_GIT_BRANCH "unknown")
endif()

set(JSB_CONTRACT_GENERATED_ROOT "${PROJECT_BINARY_DIR}/generated/contract")
set(JSB_CONTRACT_CPP_ROOT "${JSB_CONTRACT_GENERATED_ROOT}/cpp")
set(JSB_CONTRACT_PYTHON_ROOT "${PROJECT_BINARY_DIR}/generated/python")
set(JSB_CONTRACT_DESCRIPTOR
  "${JSB_CONTRACT_GENERATED_ROOT}/jsb_telemetry_v1.pb"
)

set(JSB_CONTRACT_PROTO_FILES
  telemetry/common.proto
  telemetry/aircraft_state.proto
  telemetry/control.proto
  telemetry/simulation.proto
)
set(JSB_CONTRACT_PROTO_PATHS)
foreach(proto IN LISTS JSB_CONTRACT_PROTO_FILES)
  list(APPEND JSB_CONTRACT_PROTO_PATHS "${JSB_CONTRACT_ROOT}/${proto}")
endforeach()

set(JSB_CONTRACT_CPP_SOURCES)
set(JSB_CONTRACT_CPP_HEADERS)
foreach(proto IN LISTS JSB_CONTRACT_PROTO_FILES)
  string(REGEX REPLACE "\\.proto$" ".pb.cc" generated_source "${proto}")
  string(REGEX REPLACE "\\.proto$" ".pb.h" generated_header "${proto}")
  list(APPEND JSB_CONTRACT_CPP_SOURCES
    "${JSB_CONTRACT_CPP_ROOT}/${generated_source}"
  )
  list(APPEND JSB_CONTRACT_CPP_HEADERS
    "${JSB_CONTRACT_CPP_ROOT}/${generated_header}"
  )
endforeach()

add_custom_command(
  OUTPUT
    ${JSB_CONTRACT_CPP_SOURCES}
    ${JSB_CONTRACT_CPP_HEADERS}
    "${JSB_CONTRACT_DESCRIPTOR}"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${JSB_CONTRACT_CPP_ROOT}"
  COMMAND protobuf::protoc
    --proto_path=${JSB_CONTRACT_ROOT}
    --cpp_out=${JSB_CONTRACT_CPP_ROOT}
    --include_imports
    --descriptor_set_out=${JSB_CONTRACT_DESCRIPTOR}
    ${JSB_CONTRACT_PROTO_FILES}
  DEPENDS
    ${JSB_CONTRACT_PROTO_PATHS}
    protobuf::protoc
  WORKING_DIRECTORY "${JSB_CONTRACT_ROOT}"
  COMMENT "Generating JSB0 telemetry contract C++ and descriptor set"
  VERBATIM
)

add_library(jsb_contract_proto STATIC
  ${JSB_CONTRACT_CPP_SOURCES}
  ${JSB_CONTRACT_CPP_HEADERS}
)
target_compile_features(jsb_contract_proto PUBLIC cxx_std_20)
target_include_directories(jsb_contract_proto PUBLIC
  "${JSB_CONTRACT_CPP_ROOT}"
)
target_link_libraries(jsb_contract_proto PUBLIC
  protobuf::libprotobuf
)
set_target_properties(jsb_contract_proto PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

add_custom_target(contract_generate_python
  COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/scripts/contract_tool.py"
    generate-python
    --root "${PROJECT_SOURCE_DIR}"
    --protoc "$<TARGET_FILE:protobuf::protoc>"
    --output "${JSB_CONTRACT_PYTHON_ROOT}"
  DEPENDS
    protobuf::protoc
    ${JSB_CONTRACT_PROTO_PATHS}
  COMMENT "Generating JSB0 telemetry contract Python types"
  VERBATIM
)

add_custom_target(contract_validate
  COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/scripts/contract_tool.py"
    validate
    --root "${PROJECT_SOURCE_DIR}"
    --protoc "$<TARGET_FILE:protobuf::protoc>"
    --output "${JSB_CONTRACT_GENERATED_ROOT}/validation"
  DEPENDS
    protobuf::protoc
    ${JSB_CONTRACT_PROTO_PATHS}
  COMMENT "Validating JSB0 Runtime contract"
  VERBATIM
)

add_custom_target(contract_export
  COMMAND ${Python3_EXECUTABLE}
    "${PROJECT_SOURCE_DIR}/scripts/contract_tool.py"
    export
    --root "${PROJECT_SOURCE_DIR}"
    --output "${PROJECT_BINARY_DIR}/dist/contract"
  COMMENT "Exporting JSB0 Runtime contract artifact"
  VERBATIM
)
