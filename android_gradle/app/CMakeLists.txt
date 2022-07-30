#[[
# CMakeList.txt : CMake project for vulkan_guide, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.8)

project ("vulkan_guide")


find_package(Vulkan REQUIRED)
find_package(game-activity REQUIRED CONFIG)

# Retrieve module passes.
get_target_property(game-activity-include game-activity::game-activity INTERFACE_INCLUDE_DIRECTORIES)
add_library(${PROJECT_NAME} SHARED
        main.cpp
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

target_link_libraries(vulkan_guide android log game-activity::game-activity)

#add_subdirectory(third_party)
#add_subdirectory(assetlib)
#add_subdirectory(asset-baker)
#add_subdirectory("../../rengine")
#add_subdirectory(extra-engine)

]]
