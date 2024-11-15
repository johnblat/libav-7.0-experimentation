#!/bin/sh

# remove any libav dlls and pdbs in build/
rm Debug/avcodec-*.dll
rm Debug/avformat-*.dll
rm Debug/avutil-*.dll
rm Debug/swscale-*.dll
rm Debug/swresample-*.dll
rm Debug/avfilter-*.dll
rm Debug/libx264-*.dll

rm Debug/avcodec-*.pdb
rm Debug/avformat-*.pdb
rm Debug/avutil-*.pdb
rm Debug/swscale-*.pdb
rm Debug/swresample-*.pdb
rm Debug/avfilter-*.pdb


# # .dll files with wildcard version numbers
# cp vendor/build/ffmpeg/libavcodec/avcodec-*.dll build/
# cp vendor/build/ffmpeg/libavformat/avformat-*.dll build/
# cp vendor/build/ffmpeg/libavutil/avutil-*.dll build/
# cp vendor/build/ffmpeg/libswscale/swscale-*.dll build/
# cp vendor/build/ffmpeg/libswresample/swresample-*.dll build/
# cp vendor/build/ffmpeg/libavfilter/avfilter-*.dll build/
# cp vendor/build/x264/libx264-*.dll build/

# installed at vendor/installed/bin
# cp .dll files like above except from this path
cp vendor/installed/bin/avcodec-*.dll Debug/
cp vendor/installed/bin/avformat-*.dll Debug/
cp vendor/installed/bin/avutil-*.dll Debug/
cp vendor/installed/bin/swscale-*.dll Debug/
cp vendor/installed/bin/swresample-*.dll Debug/
cp vendor/installed/bin/avfilter-*.dll Debug/
cp vendor/installed/bin/libx264-*.dll Debug/

# # .pdb files with wildcard version numbers
# under vendor/build

# # .pdb files with wildcard version numbers
# cp vendor/build/ffmpeg/libavcodec/avcodec-*.pdb build/
# cp vendor/build/ffmpeg/libavformat/avformat-*.pdb build/
# cp vendor/build/ffmpeg/libavutil/avutil-*.pdb build/
# cp vendor/build/ffmpeg/libswscale/swscale-*.pdb build/
# cp vendor/build/ffmpeg/libswresample/swresample-*.pdb build/
# cp vendor/build/ffmpeg/libavfilter/avfilter-*.pdb build/

# x264-164.dll
# cp vendor/build/x264/libx264-*.dll build/libx264-*.dll