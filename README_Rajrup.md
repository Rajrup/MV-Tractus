# MV-Tractus

## Installation

-Install FFMpeg

```bash
#install required packages
sudo apt-get -y install autoconf automake build-essential libass-dev libfreetype6-dev \
libsdl1.2-dev libtheora-dev libtool libva-dev libvdpau-dev libvorbis-dev libxcb1-dev libxcb-shm0-dev \
libxcb-xfixes0-dev pkg-config texinfo zlib1g-dev

#install yasm compiler
sudo apt install yasm

#instal h264 lib
sudo apt install libx264-dev

mkdir -p lib && cd lib
wget -O ffmpeg-4.4.5.tar.bz2 https://ffmpeg.org/releases/ffmpeg-4.4.5.tar.bz2
tar -xjvf ffmpeg-4.4.5.tar.bz2
rm -rf ffmpeg-4.4.5.tar.bz2
cd ffmpeg-4.4.5
export INSTALL_BASE_DIR="/main/rajrup/Dropbox/Project/GsplatStream/LivoGstream/src/mv_tractus/lib/ffmpeg-4.4.5"

# Parameters inspired from MeshReduce
./configure \
--prefix="$INSTALL_BASE_DIR/ffmpeg_install" \
--extra-libs=-lpthread \
--extra-libs=-lm \
--enable-gpl \
--enable-libfreetype \
--enable-libx264 \
--enable-nonfree \
--enable-pic \
--disable-static \
--enable-shared
make -j 32
make install

# Add the following to your .bashrc
export FFMPEG_HOME="/main/rajrup/Dropbox/Project/GsplatStream/LivoGstream/src/mv_tractus/lib/ffmpeg-4.4.5"
export LD_LIBRARY_PATH="$FFMPEG_HOME/ffmpeg_install/lib":$LD_LIBRARY_PATH
```

## Build

```bash
cd LivoGstream/src/mv_tractus
mkdir build && cd build
cmake ..
make -j 32
```

## Run

```bash
./extract_mvs ../vid.mp4 ../output

# Check json file for motion vectors in output folder

./extract_mvs_with_frames ../vid_h264.mp4 ../output/vid_h264/
# Check json file and frames for motion vectors in output folder

./extract_mvs_test ../vid.mp4 ../output
# MV extraction is working but overlay is not working
```