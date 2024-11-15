#! /bin/sh
# Dev Stef - 2022

# install Microsoft Visual Studio Build Tools from https://aka.ms/vs/17/release/vs_BuildTools.exe
# install msys2 https://www.msys2.org/ or download the installer from here https://github.com/msys2/msys2-installer/releases

# @echo off

# From the MSYS2 console
# echo Getting the right packages from msys
# pacman -Syu
# pacman -S make git diffutils nasm yasm pkgconf
# mv /usr/bin/link.exe /usr/bin/link.exe.bak


cd vendor
mkdir sources
mkdir build
cd sources

# Make sure you opened the x64 Native Tools Command Prompt for VS 2022 and then opened MSYS2 shell command
echo Getting libx264...
git clone https://code.videolan.org/videolan/x264.git

cd ../sources/x264
curl "http://git.savannah.gnu.org/gitweb/?p=config.git;a=blob_plain;f=config.guess;hb=HEAD" > config.guess
sed -i 's/host_os = mingw/host_os = msys/' configure

cd ../../build
mkdir x264
cd x264
 
CC=cl ../../sources/x264/configure --prefix=../../installed --enable-static --extra-cflags="-MTd -Zi -Od"
make
make install

echo Compiling ffmpeg...
cd ../../sources
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
# cd ffmpeg
# git checkout b37795688a9bfa6d5a2e9b2535c4b10ebc14ac5d

cd ../build
mkdir ffmpeg
cd ffmpeg
CC=cl  PKG_CONFIG_PATH=../../installed/lib/pkgconfig ../../sources/ffmpeg/configure --prefix=../../installed --toolchain=msvc --target-os=win64 --arch=x86_64 --disable-programs --disable-x86asm --disable-asm --enable-shared --enable-libx264 --disable-protocol=rtmp --disable-protocol=rtmps --disable-protocol=rtmpt --disable-protocol=rtmpts --disable-doc --enable-gpl --enable-version3 --enable-debug --disable-optimizations --optflags="-Od -Zi" --extra-ldflags="-LIBPATH:../../installed/lib" --extra-cflags="-I../../installed/include/ -MTd" --extra-cxxflags="-I../../installed/include/ -MTd"
make
make install