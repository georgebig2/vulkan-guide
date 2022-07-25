set(CMAKE_CXX_STANDARD 17)

find_package(Vulkan REQUIRED)

add_library(rengine STATIC)
target_include_directories(rengine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
file(GLOB R_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
target_sources(rengine PRIVATE ${R_FILES})

target_include_directories(rengine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(rengine PRIVATE json lz4 vma assetlib spirv_reflect stb_image tracy)

target_link_libraries(rengine PUBLIC Vulkan::Vulkan)


 
target_compile_definitions(rengine PUBLIC TRACY_ENABLE)
target_compile_definitions(rengine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_LEFT_HANDED) 


#both vma and glm and header only libs so we only need the include path
target_include_directories(vma INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/vma" )

target_include_directories(glm INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/glm" )

target_include_directories(json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json" )