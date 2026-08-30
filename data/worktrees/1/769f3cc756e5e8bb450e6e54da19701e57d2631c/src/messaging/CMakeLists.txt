add_library(jsb_message_bus STATIC
  MessageBus.cpp
  Subscription.cpp
)
target_compile_features(jsb_message_bus PUBLIC cxx_std_20)
set_target_properties(jsb_message_bus PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_include_directories(jsb_message_bus PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(jsb_message_bus PUBLIC jsb_common)

add_library(jsb_messaging STATIC
  SimulationMessageAdapter.cpp
  SimulationMessageClient.cpp
)
target_compile_features(jsb_messaging PUBLIC cxx_std_20)
set_target_properties(jsb_messaging PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_include_directories(jsb_messaging PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(jsb_messaging
  PUBLIC
    jsb_message_bus
    jsb_sim_runtime
)
if(WIN32)
  target_link_libraries(jsb_messaging PRIVATE shell32)
endif()
