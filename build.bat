cl.exe /DEBUG:FULL /Zi ^
/I "vendor/installed/include" ^
/I "SDL-release-2.30.8/include" ^
/Fe:Debug\ffmpeg-test.exe main.cpp ^
vendor\installed\bin\avcodec.lib ^
vendor\installed\bin\avformat.lib ^
vendor\installed\bin\avutil.lib ^
vendor\installed\bin\swscale.lib ^
SDL-release-2.30.8\VisualC\x64\Debug\SDL2main.lib ^
SDL-release-2.30.8\VisualC\x64\Debug\SDL2.lib ^
/link /SUBSYSTEM:WINDOWS Shell32.lib

:: SDL2-2.30.8\lib\x64\SDL2.lib ^
:: SDL2-2.30.8\lib\x64\SDL2main.lib ^

