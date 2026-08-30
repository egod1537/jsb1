add_library(jsb_contract STATIC
  telemetry/TelemetryTime.cpp
  telemetry/mcap/McapTelemetryRecorder.cpp
  telemetry/mcap/McapTelemetrySchema.cpp
  telemetry/mcap/McapRecordingReader.cpp
)

target_compile_features(jsb_contract PUBLIC cxx_std_20)
set_target_properties(jsb_contract PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_include_directories(jsb_contract PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(jsb_contract PRIVATE
  JSB_TEST_APPLICATION_VERSION="${PROJECT_VERSION}"
  JSB_TEST_BUILD_TYPE="$<CONFIG>"
  JSB_TEST_GIT_COMMIT="${JSB_GIT_COMMIT}"
  JSB_RUNTIME_BRANCH="${JSB_GIT_BRANCH}"
  JSB_CONTRACT_VERSION="${JSB_CONTRACT_VERSION}"
  JSB_CONTRACT_MAJOR_VERSION=${JSB_CONTRACT_MAJOR_VERSION}
  JSB_TELEMETRY_SCHEMA_VERSION=${JSB_TELEMETRY_SCHEMA_VERSION}
)
target_link_libraries(jsb_contract
  PUBLIC
    jsb_common
    jsb_contract_proto
  PRIVATE mcap_cpp
)
