#!/bin/bash
#
#  Copyright (C) 2023 Ángel Ruiz Fernández
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU Lesser General Public License as
#  published by the Free Software Foundation, version 3.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU Lesser General Public License for more details.
#
#  You should have received a copy of the GNU Lesser General Public
#  License along with this program.  If not, see
#  <http://www.gnu.org/licenses/>
#

if [ "$#" != "1" ]; then
    echo $0: Usage:
    echo "         $0 version"
    exit 1
fi

PKG_VERSION=$1
PKG_ARCH=`dpkg --print-architecture`
PKG_DEPENDS='libsigutils (>= '"$PKG_VERSION"'), libxml2 (>= 2.9.13+dfsg-1), libportaudio2 (>= 19.6.0-1.1), libsoapysdr0.8 (>= 0.8.1-2build1)'
PKG_DEV_DEPENDS='libsigutils-dev (>= '"$PKG_VERSION"'), libxml2-dev (>= 2.9.13+dfsg-1), libsoapysdr-dev (>= 0.8.1-2build1)'

BINDIR=libsuscan_${PKG_VERSION}_${PKG_ARCH}
DEVDIR=libsuscan-dev_${PKG_VERSION}_${PKG_ARCH}
TOOLSDIR=suscan-tools_${PKG_VERSION}_${PKG_ARCH}
############################ Binary package ####################################
# create structure
rm -Rf $BINDIR
mkdir $BINDIR
cd $BINDIR
mkdir -p usr/lib/
mkdir -p usr/share/suscan/config/
mkdir -p DEBIAN/

# create debian thing
rm -f DEBIAN/control
cat <<EOF >>DEBIAN/control
Package: libsuscan
Version: $PKG_VERSION
Section: libs
Priority: optional
Architecture: $PKG_ARCH
Depends: $PKG_DEPENDS
Maintainer: arf20 <aruizfernandez05@gmail.com>
Description: Channel scanner based on sigutils library
EOF

# copy files
cp ../build/libsuscan* usr/lib/
cp ../rsrc/*.yaml usr/share/suscan/config/

# set permissions
cd ..
chmod 755 -R $BINDIR/

# build deb
dpkg-deb -Zgzip --build $BINDIR

############################ Development package ###############################
# create structure
rm -Rf $DEVDIR
mkdir $DEVDIR
cd $DEVDIR
mkdir -p usr/lib/pkgconfig/
mkdir -p usr/include/suscan/analyzer/inspector/
mkdir -p usr/include/suscan/analyzer/correctors/
mkdir -p usr/include/suscan/util/
mkdir -p usr/include/suscan/yaml/
mkdir -p usr/include/suscan/cli/
mkdir -p usr/include/suscan/sgdp4/
mkdir -p DEBIAN/

# create debian thing
rm -f DEBIAN/control
cat <<EOF >>DEBIAN/control
Package: libsuscan-dev
Version: $PKG_VERSION
Section: libdevel
Priority: optional
Architecture: $PKG_ARCH
Depends: libsuscan (= $PKG_VERSION), $PKG_DEV_DEPENDS, pkg-config
Maintainer: arf20 <aruizfernandez05@gmail.com>
Description: Channel scanner based on sigutils library development files
EOF

# copy files
cp ../build/suscan.pc usr/lib/pkgconfig/
cp ../src/*.h usr/include/suscan/
cp ../analyzer/*.h usr/include/suscan/analyzer/
cp ../analyzer/inspector/*.h usr/include/suscan/analyzer/inspector/
cp ../analyzer/correctors/*.h usr/include/suscan/analyzer/correctors/
cp ../util/*.h usr/include/suscan/util/
cp ../yaml/*.h usr/include/suscan/yaml/
cp ../cli/*.h usr/include/suscan/cli/
cp ../sgdp4/*.h usr/include/suscan/sgdp4/

# set permissions
cd ..
chmod 755 -R $DEVDIR

# build deb
dpkg-deb --build $DEVDIR

############################ Tools package ###############################
# create structure
rm -Rf $TOOLSDIR
mkdir $TOOLSDIR
cd $TOOLSDIR
mkdir -p usr/bin/
mkdir -p DEBIAN/

# create debian thing
rm -f DEBIAN/control
cat <<EOF >>DEBIAN/control
Package: suscan-tools
Version: $PKG_VERSION
Section: hamradio
Priority: optional
Architecture: $PKG_ARCH
Depends: libsuscan (= $PKG_VERSION)
Maintainer: arf20 <aruizfernandez05@gmail.com>
Description: Channel scanner based on sigutils library tools
EOF

# copy files
cp ../build/suscan.status usr/bin/
cp ../build/suscli usr/bin/

# set permissions
cd ..
chmod 755 -R $TOOLSDIR

# build deb
dpkg-deb --build $TOOLSDIR
