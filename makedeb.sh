#!/bin/bash

ver=$1

# libsuscan
# create structure
mkdir libsuscan_${ver}_amd64
cd libsuscan_${ver}_amd64
mkdir -p usr/lib/
mkdir -p usr/share/suscan/config/
mkdir -p DEBIAN/

# create debian thing
rm DEBIAN/control
cat <<EOF >>DEBIAN/control
Package: libsuscan
Version: $ver
Section: base
Priority: optional
Architecture: amd64
Depends: libsigutils (>= 0.3.0-1), libsndfile1 (>= 1.0.31-2), libfftw3-3 (>= 3.3.8-2), libxml2 (>= 2.9.10+dfsg-6.7+deb11u3), libsoapysdr0.7 (>= 0.7.2-2)
Maintainer: arf20 <aruizfernandez05@gmail.com>
Description: Channel scanner based on sigutils library
EOF

# copy files
cp ../build/libsuscan* usr/lib/
cp ../rsrc/*.yaml usr/share/suscan/config/

# set permissions
cd ..
chmod 755 -R libsuscan_${ver}_amd64/

# build deb
dpkg-deb --build libsuscan_${ver}_amd64


# libsuscan-dev
# create structure
mkdir libsuscan-dev_${ver}_amd64
cd libsuscan-dev_${ver}_amd64
mkdir -p usr/lib/pkgconfig/
mkdir -p usr/include/suscan/analyzer/inspector/
mkdir -p usr/include/suscan/analyzer/correctors/
mkdir -p usr/include/suscan/util/
mkdir -p usr/include/suscan/yaml/
mkdir -p usr/include/suscan/cli/
mkdir -p usr/include/suscan/sgdp4/
mkdir -p DEBIAN/

# create debian thing
rm DEBIAN/control
cat <<EOF >>DEBIAN/control
Package: libsuscan-dev
Version: $ver
Section: base
Priority: optional
Architecture: amd64
Depends: libsigutils-dev (>= 0.3.0-1), libsndfile1 (>= 1.0.31-2), libfftw3-3 (>= 3.3.8-2), libxml2 (>= 2.9.10+dfsg-6.7+deb11u3), libsoapysdr0.7 (>= 0.7.2-2)
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
chmod 755 -R libsuscan-dev_${ver}_amd64/

# build deb
dpkg-deb --build libsuscan-dev_${ver}_amd64


# suscan-tools
# create structure
mkdir suscan-tools_${ver}_amd64
cd suscan-tools_${ver}_amd64
mkdir -p usr/bin/
mkdir -p DEBIAN/

# create debian thing
rm DEBIAN/control
cat <<EOF >>DEBIAN/control
Package: suscan-tools
Version: $ver
Section: base
Priority: optional
Architecture: amd64
Depends: libsuscan (>= 0.3.0-1)
Maintainer: arf20 <aruizfernandez05@gmail.com>
Description: Channel scanner based on sigutils library tools
EOF

# copy files
cp ../build/suscan.status usr/bin/
cp ../build/suscli usr/bin/

# set permissions
cd ..
chmod 755 -R suscan-tools_${ver}_amd64/

# build deb
dpkg-deb --build suscan-tools_${ver}_amd64
