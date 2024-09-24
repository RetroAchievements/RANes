#!/bin/bash

echo ' MacOS Build !!! '
id
pwd
uname -a
sw_vers
env

SCRIPT_DIR=$( cd $(dirname $BASH_SOURCE[0]); pwd );

QT_MAJOR=5;
QT_PKGNAME=qt$QT_MAJOR;
FCEUX_VERSION_MAJOR=`perl $SCRIPT_DIR/../scripts/fceuVersion.pl -major`;
FCEUX_VERSION_MINOR=`perl $SCRIPT_DIR/../scripts/fceuVersion.pl -minor`;
FCEUX_VERSION_PATCH=`perl $SCRIPT_DIR/../scripts/fceuVersion.pl -patch`;
FCEUX_VERSION="$FCEUX_VERSION_MAJOR.$FCEUX_VERSION_MINOR.$FCEUX_VERSION_PATCH";
SDL2_VERSION=2.0.20

echo "Building Version: $FCEUX_VERSION";

NPROC=`getconf _NPROCESSORS_ONLN`;
echo "Number of Processors: $NPROC";

INSTALL_PREFIX=/tmp/fceux

# Clean build and packaging directories
sudo rm -rf build;
sudo rm -rf $INSTALL_PREFIX/fceux.app;

# Remove windows specific DLLs so that they don't get installed
find output -name *.dll -exec rm -f {} \;

gcc --version

echo '****************************************'
echo "APPVEYOR_SSH_KEY=$APPVEYOR_SSH_KEY";
echo "APPVEYOR_SSH_BLOCK=$APPVEYOR_SSH_BLOCK";
echo '****************************************'

if [ ! -z $FCEU_RELEASE_VERSION ]; then
	APPVEYOR_CMAKE_FLAGS=" -DPUBLIC_RELEASE=1 ";
fi

echo '****************************************'
echo 'Install Dependency sdl2'
echo '****************************************'
brew  install  sdl2
BREW_SDL2=$?
echo "brew install sdl2 Return: $BREW_SDL2";
#sleep 10
if [ $BREW_SDL2 != 0 ]; then
   echo "brew install sdl2 FAILED: Attempted to build from $SDL2_VERSION release source";
   curl -o SDL2-$SDL2_VERSION.tar.gz  https://www.libsdl.org/release/SDL2-$SDL2_VERSION.tar.gz
   tar -xvf SDL2-$SDL2_VERSION.tar.gz
   cd SDL2-$SDL2_VERSION;
   ./configure  --without-x --enable-hidapi
   make -j $NPROC
   make install
   cd ..
fi

echo '****************************************'
echo "Install Dependency $QT_PKGNAME"
echo '****************************************'
brew  install  $QT_PKGNAME

echo '****************************************'
echo 'Install Dependency minizip'
echo '****************************************'
brew  install  minizip

echo '****************************************'
echo 'Install Optional Dependency libarchive'
echo '****************************************'
brew  install  libarchive
LIBARCHIVE_PATH=`brew --prefix libarchive`;

echo '****************************************'
echo 'Install Optional Dependency x264'
echo '****************************************'
brew  install  x264

echo '****************************************'
echo 'Install Optional Dependency x265'
echo '****************************************'
brew  install  x265

echo '****************************************'
echo 'Install Optional Dependency ffmpeg'
echo '****************************************'
brew  install  ffmpeg

#brew  install  zlib  # Already installed in appveyor macOS

export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/usr/local/lib/pkgconfig:$LIBARCHIVE_PATH/lib/pkgconfig:

ls -ltr $HOME/Qt;

#find $HOME/Qt -name Qt5Config.cmake
#find $HOME/Qt -name Qt6Config.cmake

#export Qt6_DIR=$HOME/Qt/6.0/clang_64;
#export Qt5_DIR=$HOME/Qt/5.15/clang_64;

if [ $QT_MAJOR == 6 ]; then
export Qt6_DIR=`brew --prefix qt6`;
echo "Qt6_DIR=$Qt6_DIR";
Qt_DIR=$Qt6_DIR;
USE_QT6=1;
else
export Qt5_DIR=`brew --prefix qt5`;
echo "Qt5_DIR=$Qt5_DIR";
Qt_DIR=$Qt5_DIR;
USE_QT6=0;
fi
#ls $Qt_DIR;

PATH=$PATH:$Qt_DIR/bin
echo '**************************'
echo '***  Building Project  ***'
echo '**************************'
./scripts/unix_make_docs.sh;
mkdir build;
cd build;
cmake \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX \
	-DCMAKE_VERBOSE_MAKEFILE:BOOL=ON \
   -DCMAKE_PREFIX_PATH=$Qt_DIR \
   -DCMAKE_PROJECT_VERSION_MAJOR=$FCEUX_VERSION_MAJOR \
   -DCMAKE_PROJECT_VERSION_MINOR=$FCEUX_VERSION_MINOR \
   -DCMAKE_PROJECT_VERSION_PATCH=$FCEUX_VERSION_PATCH \
   -DCPACK_PACKAGE_VERSION_MAJOR=$FCEUX_VERSION_MAJOR \
   -DCPACK_PACKAGE_VERSION_MINOR=$FCEUX_VERSION_MINOR \
   -DCPACK_PACKAGE_VERSION_PATCH=$FCEUX_VERSION_PATCH \
   -DQT6=$USE_QT6 \
   $APPVEYOR_CMAKE_FLAGS \
	.. || exit 1
make -j $NPROC || exit 1
#sudo make install || exit 1 # make install is already run by cpack
sudo cpack -G DragNDrop || exit 1

if [ ! -z $APPVEYOR ]; then
	echo 'Pushing DMG Package to Build Artifacts'
	if [ -z $FCEU_RELEASE_VERSION ]; then
		cp fceux-*.dmg fceux-Darwin.dmg
		appveyor PushArtifact fceux-Darwin.dmg
		appveyor SetVariable  -Name  MACOS_ARTIFACT  -Value  fceux-Darwin.dmg
	else
		appveyor PushArtifact fceux-*.dmg
		appveyor SetVariable  -Name  MACOS_ARTIFACT  -Value  `ls fceux-*.dmg`
	fi
fi

# Debug via ssh if necessary
if [ ! -z $APPVEYOR_SSH_BLOCK ]; then
   curl -sflL 'https://raw.githubusercontent.com/appveyor/ci/master/scripts/enable-ssh.sh' | bash -e -
fi

