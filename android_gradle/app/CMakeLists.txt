
cmake_minimum_required(VERSION 3.12)
#include(android_package)

#find_package(Vulkan REQUIRED)
find_package(game-activity REQUIRED CONFIG)
#find_library(vulkan_guide android)

# Retrieve module passes.
get_target_property(game-activity-include game-activity::game-activity INTERFACE_INCLUDE_DIRECTORIES)
add_library(${PROJECT_NAME} SHARED
        main.cpp
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

target_link_libraries(vulkan_guide android rengine game-activity::game-activity glm imgui log)

function(android_sync_folder)
    set(options)
    set(oneValueArgs PATH)
    set(multiValueArgs)

    cmake_parse_arguments(PARSE_ARGV 0 TARGET "${options}" "${oneValueArgs}" "${multiValueArgs}")
    get_filename_component(FOLDER_NAME "${TARGET_PATH}" NAME)

    set(SYNC_COMMAND ${CMAKE_COMMAND}
            -DCMAKE_MODULE_PATH=${CMAKE_MODULE_PATH}
            -DFOLDER_DIR=${TARGET_PATH}/.
            -DDEVICE_DIR=/sdcard/com.georgebig2.${PROJECT_NAME}/${FOLDER_NAME}/
            -P "android_sync_folder.cmake")

    add_custom_target(
            sync.${FOLDER_NAME}.stamp
            COMMAND
            ${SYNC_COMMAND}
            COMMENT
            "Update ${FOLDER_NAME} in external storage"
            VERBATIM)

    add_dependencies(${PROJECT_NAME} sync.${FOLDER_NAME}.stamp)
endfunction()

# Sync assets and shaders
#android_sync_folder(PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../assets_export)
#android_sync_folder(PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../shaders)


