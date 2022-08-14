SetLocal EnableDelayedExpansion

forfiles /m *.vert /c "cmd /c %VULKAN_SDK%\Bin\glslangValidator.exe -V @fname.vert -o @fname.vert.spv"

forfiles /m *.frag /c "cmd /c %VULKAN_SDK%\Bin\glslangValidator.exe -V @fname.frag -o @fname.frag.spv"

forfiles /m *.comp /c "cmd /c %VULKAN_SDK%\Bin\glslangValidator.exe -V @fname.comp -o @fname.comp.spv"

"cmd /c %VULKAN_SDK%\Bin\glslangValidator.exe -V -x -o hud.vert.u32 hud.vert"

pause