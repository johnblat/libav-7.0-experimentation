cl.exe /DEBUG:FULL /Zi ^
/I "vendor/installed/include" ^
/I "vendor\raylib-5.5_win64_msvc16\include" ^
/Fe:Debug\ffmpeg-test.exe ^
/Fo.\obj\ ^
src\main.cpp ^
src\state.cpp ^
src\pts_frame_conversions.cpp ^
src\texture_ring.cpp ^
src\seek.cpp ^
src\read.cpp ^
src\decode_queue.cpp ^
src\load_texture_from_image_queue.cpp ^
vendor\installed\bin\avcodec.lib ^
vendor\installed\bin\avformat.lib ^
vendor\installed\bin\avutil.lib ^
vendor\installed\bin\swscale.lib ^
vendor\raylib-5.5_win64_msvc16\lib\raylibdll.lib ^


:: SDL2-2.30.8\lib\x64\SDL2.lib ^
:: SDL2-2.30.8\lib\x64\SDL2main.lib ^
