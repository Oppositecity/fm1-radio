#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The sampler fork's audio tests: a host build with RADIO_LOCAL_TEST (the built-in MP3 as the station: no network),
# then tests/local/*.host, their WAVs and screens into app/build/local. Needs ffmpeg (the test MP3) and clang.
#   sh tests/run_local.sh
set -e
cd "$(dirname "$0")/.."
GEN=app/build/gen
mkdir -p "$GEN" app/build/local
if [ ! -f "$GEN/test_mp3.h" ]; then           # 4 s: a plucked arpeggio in minor thirds, a noise click each half second
    ffmpeg -y -loglevel error -f lavfi -i "aevalsrc='0.5*sin(2*PI*t*(220*pow(2,(floor(mod(t*8,8))*3)/12)))*exp(-mod(t,0.125)*18)+0.3*(random(0)-0.5)*exp(-mod(t,0.5)*60)|0.5*sin(2*PI*t*(220*pow(2,(floor(mod(t*8,8))*3)/12))*1.003)*exp(-mod(t,0.125)*18)+0.3*(random(1)-0.5)*exp(-mod(t,0.5)*60)':s=44100:d=4" \
        -b:a 128k app/build/local/test.mp3
    python3 -c "import sys; d=open('app/build/local/test.mp3','rb').read(); open('$GEN/test_mp3.h','w').write('static const uint8_t test_mp3[%d] = {%s};\n' % (len(d), ','.join(map(str, d))))"
fi
printf '#!/bin/sh\nexec %s -DRADIO_LOCAL_TEST "$@"\n' "${CC:-clang}" > app/build/local/cc
chmod +x app/build/local/cc
CC="$PWD/app/build/local/cc" ./build.sh --host >/dev/null
for s in tests/local/*.host; do
    echo "== $s"
    app/build/host/radio_host "$s" app/build/local
done
