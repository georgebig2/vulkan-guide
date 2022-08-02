cmake_minimum_required (VERSION 3.8)
set(CMAKE_CXX_STANDARD 17)

if(ANDROID)
    add_compile_definitions(VK_USE_PLATFORM_ANDROID_KHR)
    add_compile_definitions(VULKAN_HPP_TYPESAFE_CONVERSION=1)
    #target_compile_definitions(rengine INTERFACE VK_USE_PLATFORM_ANDROID_KHR)
elseif(WIN32)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR)
#    target_compile_definitions(rengine INTERFACE VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    add_compile_definitions(VK_USE_PLATFORM_METAL_EXT)
	#target_compile_definitions(rengine INTERFACE VK_USE_PLATFORM_METAL_EXT)
endif()

find_package(Vulkan REQUIRED)

add_library(rengine STATIC)
target_include_directories(rengine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
file(GLOB R_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
target_sources(rengine PRIVATE ${R_FILES})



target_include_directories(rengine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(rengine PUBLIC Vulkan::Vulkan vkbootstrap vma)
target_link_libraries(rengine PRIVATE assetlib spirv_reflect tracy glm imgui fmt_lib)



 
target_compile_definitions(rengine PRIVATE TRACY_ENABLE)
target_compile_definitions(rengine PRIVATE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_LEFT_HANDED) 

#both vma and glm and header only libs so we only need the include path
#target_include_directories(vma INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/vma" )

#target_include_directories(glm INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/glm" )

#target_include_directories(json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json" )