add_library(jsb_flightgear STATIC
  FlightGearSender.cpp
  FlightGearSystem.cpp
)
target_compile_features(jsb_flightgear PUBLIC cxx_std_20)
set_target_properties(jsb_flightgear PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_include_directories(jsb_flightgear PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(jsb_flightgear PUBLIC jsb_sim_runtime)
if(WIN32)
  target_link_libraries(jsb_flightgear PRIVATE ws2_32)
endif()
