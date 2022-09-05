find_package(Vulkan REQUIRED)
include(ExternalProject)

add_library(vkbootstrap STATIC)
#add_library(vma INTERFACE)
add_library(stb_image INTERFACE)
add_library(json INTERFACE)
add_library(tinyobjloader STATIC)
add_library(spirv_reflect STATIC)
add_library(fmt_lib STATIC)
add_library(lz4 STATIC)

#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/libs")
#set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/libs")

add_subdirectory(gltf)

#ExternalProject_Add(nvtools
#SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nvidia-texture-tools"
#BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nvlibs"
#CMAKE_ARGS -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${PROJECT_SOURCE_DIR}/libs -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${PROJECT_SOURCE_DIR}/libs -DCMAKE_CXX_STANDARD=17
#)

add_library(nvtt INTERFACE)
target_include_directories(nvtt INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nvidia-texture-tools/src/nvtt")

#set(nvttfolder "${PROJECT_SOURCE_DIR}/libs/Release")

set(nvttfolder "${PROJECT_SOURCE_DIR}/libs/$<IF:$<CONFIG:Debug>,Debug,Release>")

target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvtt.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/bc6h.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/bc7.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/EtcLib.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvcore.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvimage.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvmath.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvsquish.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvthread.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/posh.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/rg_etc1.lib")
#target_link_libraries(nvtt INTERFACE "${nvttfolder}/squish.lib")

#add_subdirectory(nvidia-texture-tools)

set(TRACY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tracy" CACHE FILEPATH "Path to Tracy")
add_library(tracy OBJECT "${TRACY_DIR}/TracyClient.cpp")
target_include_directories(tracy PUBLIC "${TRACY_DIR}")
target_compile_definitions(tracy PUBLIC TRACY_ENABLE)

#add_subdirectory(vkbootstrap)
target_sources(vkbootstrap PRIVATE 
    
    "${CMAKE_CURRENT_SOURCE_DIR}/vkbootstrap/VkBootstrap.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/vkbootstrap/VkBootstrap.cpp"
)


target_sources(spirv_reflect PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/spv_reflect/spirv_reflect.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/spv_reflect/spirv_reflect.c"
)

target_sources(lz4 PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/lz4/lz4.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/lz4/lz4.c"
)

target_include_directories(lz4 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lz4" )

target_include_directories(spirv_reflect PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/spv_reflect" )
target_include_directories(spirv_reflect PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/spv_reflect/include" )
target_include_directories(vkbootstrap PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/vkbootstrap" )
target_link_libraries(vkbootstrap PRIVATE Vulkan::Vulkan)

# vma
add_library(vma INTERFACE)
target_include_directories(vma INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/vma" )
#set(VMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vma)
#target_sources(vma INTERFACE ${VMA_DIR}/vk_mem_alloc.h)
#target_include_directories(vma INTERFACE ${VMA_DIR})
target_link_libraries(vma INTERFACE Vulkan::Vulkan)

# glm
add_library(glm INTERFACE)
set(GLM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glm)
target_sources(glm INTERFACE ${GLM_DIR}/glm/glm.hpp)
target_include_directories(glm INTERFACE ${GLM_DIR})
target_compile_definitions(glm INTERFACE
    GLM_FORCE_SWIZZLE
    GLM_FORCE_RADIANS
    GLM_FORCE_CTOR_INIT
    GLM_ENABLE_EXPERIMENTAL
    #GLM_FORCE_PLATFORM_UNKNOWN
    #GLM_FORCE_EXPLICIT_CTOR
    #GLM_FORCE_INLINE
    #GLM_FORCE_INTRINSICS
)
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_definitions(glm INTERFACE GLM_FORCE_CXX14)
endif()

target_include_directories(json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json" )

target_sources(tinyobjloader PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyobjloader/tiny_obj_loader.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tinyobjloader/tiny_obj_loader.cc"
)
target_include_directories(tinyobjloader PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/tinyobjloader" )

#"$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
#set(sdl2_DIR "$ENV{VULKAN_SDK}/1.3.216.0/macOS/include/SDL2")
set(sdl2_DIR "/Users/georgijzuravlev/VulkanSDK/1.3.216.0/macOS/include/SDL2")
set(sdl2l_DIR "/Users/georgijzuravlev/VulkanSDK/1.3.216.0/macOS/lib")
add_library(sdl2 INTERFACE)
target_include_directories(sdl2 INTERFACE "${sdl2_DIR}" )
#target_link_directories(sdl2 INTERFACE "${sdl2_DIR}/lib/x64" "${sdl2_image_DIR}/lib/x64")
target_link_directories(sdl2 INTERFACE "${sdl2l_DIR}")
#if (NOT ANDROID)
target_link_libraries(sdl2 INTERFACE SDL2 SDL2main)
#endif()

add_library(imgui STATIC)
target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
target_sources(imgui PUBLIC 
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.h"
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_stdlib.h"
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_stdlib.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_demo.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_draw.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_tables.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_widgets.cpp"
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_impl_vulkan.cpp"
)
if(NOT ANDROID)
target_sources(imgui PRIVATE 
"${CMAKE_CURRENT_SOURCE_DIR}/imgui/imgui_impl_sdl.cpp"
)
endif()
target_link_libraries(imgui PUBLIC Vulkan::Vulkan sdl2)

target_include_directories(stb_image INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/stb_image" )

add_library(assimp INTERFACE)
set(assimp_dir "${CMAKE_CURRENT_SOURCE_DIR}/assimp")
set(assimp_dll "${CMAKE_CURRENT_SOURCE_DIR}/assimp/lib/Release/assimp-vc143-mt.dll")
set(assimp_lib "${CMAKE_CURRENT_SOURCE_DIR}/assimp/lib/Release/assimp-vc143-mt.lib")
target_include_directories(assimp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/assimp/include")
target_link_libraries(assimp INTERFACE "${assimp_lib}" )


## FMT LIB
target_include_directories(fmt_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/fmt_lib")

file(GLOB FMT_FILES "${CMAKE_CURRENT_SOURCE_DIR}/fmt_lib/fmt/*")

target_sources(fmt_lib PRIVATE ${FMT_FILES})



