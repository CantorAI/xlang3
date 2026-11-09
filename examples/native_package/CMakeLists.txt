add_library(xlang3_demo_package SHARED EXCLUDE_FROM_ALL demo.cpp)
target_link_libraries(xlang3_demo_package PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_demo_package PRIVATE XLANG3_USE_SHARED)
set_target_properties(xlang3_demo_package PROPERTIES
  PREFIX ""
  OUTPUT_NAME "demo.x3pkg"
  LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:xlang3_runtime>/native_package_demo"
  RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:xlang3_runtime>/native_package_demo"
)

add_executable(xlang3_import_demo EXCLUDE_FROM_ALL import_demo.cpp)
target_link_libraries(xlang3_import_demo PRIVATE xlang3_runtime)
target_compile_definitions(xlang3_import_demo PRIVATE XLANG3_USE_SHARED)
set_target_properties(xlang3_import_demo PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:xlang3_runtime>"
)
add_dependencies(xlang3_import_demo xlang3_demo_package xlang_yaml_native_package)
