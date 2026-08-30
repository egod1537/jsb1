if(JSB_BUILD_EDITOR)
  add_library(jsb_app STATIC
    Application.cpp
    platform/input/Input.cpp
  )
  target_compile_features(jsb_app PUBLIC cxx_std_20)
  set_target_properties(jsb_app PROPERTIES CXX_SCAN_FOR_MODULES OFF)
  target_include_directories(jsb_app PUBLIC ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(jsb_app
    PUBLIC
      jsb_editor
      jsb_flightgear
    PRIVATE
      jsb_messaging
      jsb_sim_runtime
  )

  add_executable(jsb-flight-console main.cpp)
  target_compile_features(jsb-flight-console PRIVATE cxx_std_20)
  set_target_properties(jsb-flight-console PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}"
  )
  target_link_libraries(jsb-flight-console PRIVATE jsb_app)

  add_custom_command(
    TARGET jsb-flight-console
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:jsb-flight-console>/assets/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
            "$<TARGET_FILE_DIR:jsb-flight-console>/assets/fonts/Inter-Regular.ttf"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/assets/fonts/LICENSE.txt"
            "$<TARGET_FILE_DIR:jsb-flight-console>/assets/fonts/LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:jsb-flight-console>/assets/third_party/unity-editor-icons"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets/third_party/unity-editor-icons"
            "$<TARGET_FILE_DIR:jsb-flight-console>/assets/third_party/unity-editor-icons"
    VERBATIM
  )
endif()
