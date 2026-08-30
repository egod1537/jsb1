option(JSB_ENABLE_UNITY_BUILD
  "Enable CMake unity builds for the application library"
  OFF
)

option(JSB_BUILD_EDITOR
  "Build the interactive FlightUI editor"
  ON
)

if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
  find_program(CCACHE_EXECUTABLE ccache)
  if(CCACHE_EXECUTABLE)
    set(CMAKE_CXX_COMPILER_LAUNCHER
      "${CMAKE_COMMAND};-E;env;CCACHE_SLOPPINESS=pch_defines,time_macros;${CCACHE_EXECUTABLE}"
      CACHE STRING "C++ compiler launcher"
    )
    message(STATUS "Using ccache: ${CCACHE_EXECUTABLE}")
  endif()
endif()

mark_as_advanced(CCACHE_EXECUTABLE)
