add_library(implot STATIC
  ${implot_SOURCE_DIR}/implot.cpp
  ${implot_SOURCE_DIR}/implot_items.cpp
)

target_include_directories(implot PUBLIC
  ${implot_SOURCE_DIR}
)

target_link_libraries(implot PUBLIC
  imgui
)
