# Nectar
Getting the sweetest details from your test encodes. 

An video analysis app using SDL2 and ffmpeg 4.0 to analyze multiple encodes of the same video source to select the best one. Can do frame-by-frame seeking.

## Concept
You will give Nectar a "source" video file and multiple "test" video files. You can compare the test video files against the source by selecting which test you want to compare against the source. You can seek frame-by-frame to compare the details of the source and the test video files.

## How to use
Use left and right key buttons to seek frames.
Use up and down key buttons to select the test encode to use against the source.
Use space key to toggle between the source and the selected test encode.

# Ffmpeg notes
### Git checout specific branch
git clone --depth 1 https://git.ffmpeg.org/ffmpeg.git ffmpeg
git fetch origin release/5.0:refs/remotes/origin/release/5.0
git checkout origin/release/5.0

rm -r vendor/build/ffmpeg/*
rm -r vendor/build/x264/*
rm -r vendor/installed/bin/*
rm -r vendor/installed/include/*
rm -r vendor/installed/lib/*
rm -r vendor/installed/share/*
rm -r vendor/sources/ffmpeg
