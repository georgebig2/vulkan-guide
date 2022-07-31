# CMakeList.txt : CMake project for vulkan_guide, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("vulkan_guide")


find_package(Vulkan REQUIRED)

if(ANDROID)
    #include_directories(rengine)

#[[    find_package(game-activity REQUIRED CONFIG)
    # Retrieve module passes.
    get_target_property(game-activity-include game-activity::game-activity INTERFACE_INCLUDE_DIRECTORIES)
    add_library(${PROJECT_NAME} SHARED
            android_gradle/app/main.cpp
            #native_app_glue_included.c
            ${game-activity-include}/game-activity/native_app_glue/android_native_app_glue.c
            ${game-activity-include}/game-activity/GameActivity.cpp
            ${game-activity-include}/game-text-input/gametextinput.cpp)

    # Set compiler flags. Those options won't be necessary as prefabs will be updated.
    set_source_files_properties(${game-activity-include}/game-activity/native_app_glue/android_native_app_glue.c PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
    set_source_files_properties(${game-activity-include}/game-text-input/gametextinput.cpp PROPERTIES COMPILE_FLAGS -Wno-return-type)
    set_source_files_properties(${game-activity-include}/game-activity/GameActivity.cpp PROPERTIES COMPILE_FLAGS "-Wno-shadow-field -Wno-return-type -Wno-format")

    # Export GameActivity_onCreate(),
    # Refer to: https://github.com/android-ndk/ndk/issues/381.
    set(CMAKE_SHARED_LINKER_FLAGS
            "${CMAKE_SHARED_LINKER_FLAGS} -u GameActivity_onCreate")

    target_link_libraries(vulkan_guide android log game-activity::game-activity rengine)]]

    add_subdirectory(third_party)
    add_subdirectory(assetlib)
    add_subdirectory(rengine)
    add_subdirectory(android_gradle/app)

else()
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${PROJECT_SOURCE_DIR}/bin/DEBUG")
    set (CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO  "${PROJECT_SOURCE_DIR}/bin/RelWithDebInfo ")
    
    add_subdirectory(third_party)
    add_subdirectory(assetlib)
    add_subdirectory(rengine)
    add_subdirectory(extra-engine)

endif()



#add_subdirectory(asset-baker)

#[[
if (${CMAKE_HOST_SYSTEM_PROCESSOR} STREQUAL "AMD64")
  set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe")
else()
  set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/Bin32/glslangValidator.exe")
endif()  
 

file(GLOB_RECURSE GLSL_SOURCE_FILES
    "${PROJECT_SOURCE_DIR}/shaders/*.frag"  
    "${PROJECT_SOURCE_DIR}/shaders/*.vert"
    "${PROJECT_SOURCE_DIR}/shaders/*.comp"
    )

foreach(GLSL ${GLSL_SOURCE_FILES})
  message(STATUS "BUILDING SHADER")
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_SOURCE_DIR}/shaders/${FILE_NAME}.spv")
  message(STATUS ${GLSL})
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    COMMENT "XXXXXXXXXXXXXXXXXXX"
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    Shaders 
    DEPENDS ${SPIRV_BINARY_FILES}
	SOURCES ${GLSL_SOURCE_FILES}
    )
    ]]
