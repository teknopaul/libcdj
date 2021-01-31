#!/bin/bash
#
# Build a binary .deb package
#
set -e
test $(id -u) == "0" || (echo "run as root" && exit 1) # requires -e

test -d target/ || (echo "run make first" && exit 1)

#
# The package name
#
name=libcdj
arch=$(uname -m)

if [ $arch == armv6l ] ; then
  lib_dir=/usr/lib/x86_64-linux-gnu
elif [ $arch == x86_64 ] ; then
  lib_dir=/usr/lib
fi


cd $(dirname $0)/..
project_root=$PWD

#
# Create a temporary build directory
#
tmp_dir=/tmp/$name-debbuild
rm -r $tmp_dir ||:
mkdir -p \
  $tmp_dir/DEBIAN \
  $tmp_dir/usr/bin \
  $tmp_dir/$lib_dir \
  $tmp_dir/usr/include/cdj

#
# copy in the files
#

# bins
cp --archive \
  target/cdj-mon \
  target/vdj \
  target/vdj-debug \
    $tmp_dir/usr/bin

# libs
cp --archive target/*.so \
    $tmp_dir/$lib_dir

# hdrs
cp --archive src/c/*.h \
    $tmp_dir/usr/include/cdj

size=$(du -sk $tmp_dir | cut -f 1)

. ./version
sed -e "s/@PACKAGE_VERSION@/$VERSION/" $project_root/deploy/DEBIAN/control.in > $tmp_dir/DEBIAN/control
sed -i -e "s/@SIZE@/$size/" $tmp_dir/DEBIAN/control

cp --archive -R $project_root/deploy/DEBIAN/p* $tmp_dir/DEBIAN

#
# setup conffiles (none for now)
#
#(
#  cd $tmp_dir/
#  find etc -type f | sed 's.^./.' > DEBIAN/conffiles
#)

#
# perms
#
chown root.root $tmp_dir/usr/bin/* $tmp_dir/$lib_dir/*
chmod 755 $tmp_dir/usr/bin/* $tmp_dir/$lib_dir
chmod 644 $tmp_dir/$lib_dir/*

#
# Build the .deb
#
dpkg-deb --build $tmp_dir target/$name-$VERSION-1.$arch.deb

test -f target/$name-$VERSION-1.$arch.deb

echo "built target/$name-$VERSION-1.$arch.deb"

if [ -n "$SUDO_USER" ]
then
  chown $SUDO_USER target/ target/$name-$VERSION-1.$arch.deb
fi

# deletes on reboot anyway
# test -d $tmp_dir && rm -rf $tmp_dir
