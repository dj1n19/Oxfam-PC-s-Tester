#!/bin/sh
# Rebuilds every icon file from the master djinn.png (32x32 pixel art, edit it
# in GIMP, Aseprite...). Run by hand after editing the master, then commit the
# results: the build only uses the generated files, so CI needs no ImageMagick.
#
# Why these filters: pixel art must keep sharp pixels.
# - "point" (nearest neighbour) for whole multiples (64, 128, 256): each pixel
#   becomes an exact square block.
# - "box" (average) to go smaller (16, 24) or to a size that is not a multiple
#   (48 = 96 / 2): a few pixels blend, but nothing is smeared.
set -e
cd "$(dirname "$0")"

magick djinn.png -filter box   -resize 16x16   -strip djinn-16.png
magick djinn.png -filter box   -resize 24x24   -strip djinn-24.png
magick djinn.png -filter point -resize 96x96 -filter box -resize 48x48 -strip djinn-48.png
magick djinn.png -filter point -resize 64x64   -strip djinn-64.png
magick djinn.png -filter point -resize 128x128 -strip djinn-128.png
magick djinn.png -filter point -resize 256x256 -strip djinn-256.png   # also the AppImage icon

# Windows .exe icon: one .ico holding every size (Explorer picks the one it needs).
# About 60 KB: fine for a portable program.
magick djinn-16.png djinn-24.png djinn.png djinn-48.png djinn-64.png djinn-128.png djinn-256.png \
       lazy-djin-tester.ico
