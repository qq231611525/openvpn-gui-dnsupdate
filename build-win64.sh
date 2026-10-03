#!/bin/bash
# Cross-compile openvpn-gui for Windows x64 using mingw-w64.
# Run from the repo root. Requires: mingw-w64 (x86_64-w64-mingw32-gcc,
# x86_64-w64-mingw32-windres, x86_64-w64-mingw32-windmc)
set -e
cd "$(dirname "$0")"

CC=x86_64-w64-mingw32-gcc
WINDRES=x86_64-w64-mingw32-windres
WINDMC=x86_64-w64-mingw32-windmc

BUILD=./build-win64
mkdir -p "$BUILD/obj"

# 1. config.h from template (version 11.66, matches upstream HEAD)
sed -e 's/@GUI_VERSION_MAJOR@/11/' -e 's/@GUI_VERSION_MINOR@/66/' \
    config-msvc.h.in > "$BUILD/config.h"

# 2. eventmsg.h from eventmsg.mc
if [ ! -f "$BUILD/eventmsg.h" ]; then
    (cd "$BUILD" && $WINDMC -h . ../eventmsg.mc)
fi
ls "$BUILD"/eventmsg.h

# 3. compile
CFLAGS="-O2 -D_UNICODE -DUNICODE -DWIN32_LEAN_AND_MEAN -DHAVE_CONFIG_H \
        -I$BUILD -I. -Iqrcodegen -Wall -Wno-unused-function -Wno-format"
SRCS="access.c echo.c env_set.c localization.c main.c manage.c misc.c openvpn.c
      openvpn_config.c options.c proxy.c registry.c save_pass.c scripts.c service.c
      tray.c viewlog.c as.c pkcs11.c config_parser.c qr.c qrcodegen/qrcodegen.c
      dnsupdate.c"
OBJS=""
for s in $SRCS; do
    o="$BUILD/obj/$(echo $s | tr '/' '_').o"
    echo "CC $s"
    $CC $CFLAGS -c "$s" -o "$o"
    OBJS="$OBJS $o"
done

# 4. resources
echo "WINDRES res/openvpn-gui-res.rc"
$WINDRES -I. -Ires -I"$BUILD" --codepage=65001 -DHAVE_CONFIG_H \
    res/openvpn-gui-res.rc -o "$BUILD/obj/rsrc.o"

# 5. link
echo "LINK openvpn-gui.exe"
$CC -o "$BUILD/openvpn-gui.exe" $OBJS "$BUILD/obj/rsrc.o" \
    -mwindows -municode \
    -lwtsapi32 -lnetapi32 -lws2_32 -lwinhttp -lsecur32 -lcomctl32 -lcrypt32 \
    -lshlwapi -ladvapi32 -lshell32 -lgdi32 -lcomdlg32 -lole32 -lcryptui \
    -lwininet -liphlpapi -lntdll -ldnsapi

ls -la "$BUILD/openvpn-gui.exe"
echo BUILD_OK
