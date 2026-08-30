if(NOT DEFINED RUNNER OR NOT DEFINED SCENARIO OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "RUNNER, SCENARIO, and OUTPUT are required")
endif()

file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(
  COMMAND "${RUNNER}"
          --scenario "${SCENARIO}"
          --output "${OUTPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "headless runner failed (${result})\n${stdout_text}\n${stderr_text}")
endif()

set(manifest "${OUTPUT}/run.json")
set(telemetry "${OUTPUT}/telemetry.mcap")
set(scenario_snapshot "${OUTPUT}/scenario.yaml")
if(NOT EXISTS "${manifest}")
  message(FATAL_ERROR "runner did not produce ${manifest}")
endif()
if(NOT EXISTS "${telemetry}")
  message(FATAL_ERROR "runner did not produce ${telemetry}")
endif()
if(NOT EXISTS "${scenario_snapshot}")
  message(FATAL_ERROR "runner did not produce ${scenario_snapshot}")
endif()

file(READ "${manifest}" manifest_text)
if(NOT manifest_text MATCHES "\"status\": \"completed\"")
  message(FATAL_ERROR "manifest is not completed:\n${manifest_text}")
endif()
foreach(expected IN ITEMS
    "\"mode\": \"compare\""
    "\"variants\": [\"baseline\", \"primary\"]"
    "\"baseline\": {\"status\": \"completed\"}"
    "\"primary\": {\"status\": \"completed\"}")
  string(FIND "${manifest_text}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "manifest is missing ${expected}:\n${manifest_text}")
  endif()
endforeach()
if(manifest_text MATCHES "\"autopilot\"" OR manifest_text MATCHES "\"variant\":")
  message(FATAL_ERROR "manifest contains a single-variant selector:\n${manifest_text}")
endif()
if(NOT manifest_text MATCHES "\"steps\": 10([,\n])")
  message(FATAL_ERROR "manifest has unexpected step count:\n${manifest_text}")
endif()
if(NOT manifest_text MATCHES "\"simulation_dt_s\": 0\\.01([,\n])")
  message(FATAL_ERROR "manifest has unexpected dt:\n${manifest_text}")
endif()
