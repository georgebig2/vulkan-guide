set(CMAKE_CXX_STANDARD 17)


file(GLOB ENGINE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_executable (extra ${ENGINE_FILES})


set_property(TARGET extra PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:extra>")

target_include_directories(extra PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

 
target_compile_definitions(extra PUBLIC TRACY_ENABLE)
target_compile_definitions(extra PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_LEFT_HANDED) 


target_precompile_headers(extra PUBLIC "<unordered_map>" "<vector>" "<iostream>" "<fstream>" "<string>" )
target_link_libraries(extra glm tinyobjloader imgui stb_image spirv_reflect)

target_link_libraries(extra rengine sdl2 assetlib tracy fmt_lib vkbootstrap vma)