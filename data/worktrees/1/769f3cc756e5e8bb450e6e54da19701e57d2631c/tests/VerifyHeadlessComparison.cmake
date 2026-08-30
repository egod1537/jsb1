if(NOT DEFINED RUNNER OR NOT DEFINED SCENARIO OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "RUNNER, SCENARIO, and OUTPUT are required")
endif()

file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(
  COMMAND "${RUNNER}"
          --scenario "${SCENARIO}"
          --mode compare
          --output "${OUTPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout_text
  ERROR_VARIABLE stderr_text
)

if(NOT result EQUAL 0)
  message(FATAL_ERROR "comparison runner failed (${result})\n${stdout_text}\n${stderr_text}")
endif()

foreach(artifact IN ITEMS run.json telemetry.mcap scenario.yaml)
  if(NOT EXISTS "${OUTPUT}/${artifact}")
    message(FATAL_ERROR "comparison runner did not produce ${artifact}")
  endif()
endforeach()

file(READ "${OUTPUT}/run.json" manifest_text)
foreach(expected IN ITEMS
    "\"mode\": \"compare\""
    "\"variants\": [\"baseline\", \"primary\"]"
    "\"baseline\": {\"status\": \"completed\"}"
    "\"primary\": {\"status\": \"completed\"}"
    "\"steps\": 10")
  string(FIND "${manifest_text}" "${expected}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "comparison manifest is missing ${expected}:\n${manifest_text}")
  endif()
endforeach()
