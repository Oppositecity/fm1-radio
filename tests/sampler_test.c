/* SPDX-License-Identifier: MPL-2.0 */
/* app/sampler.c alone: cc -O1 -Ibase/include -o build/sampler_test tests/sampler_test.c && ./build/sampler_test */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "plat.h"
static uint8_t flash[0x100000];
const uint8_t *plat_data(uint32_t off) { return flash + off; }
#include "../app/sampler.c"

static int bad;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "  ok" : "FAIL", what);
    bad += !ok;
}
static int voices_on(void)
{
    int i, n = 0;
    for (i = 0; i < SMP_VOICES; i++)
        n += smv[i].on;
    return n;
}
static int crossings(int n)                       /* upward zero crossings in n output samples */
{
    int i, c = 0;
    float prev = 0;
    for (i = 0; i < n; i++) {
        float s = smp_next();
        c += prev < 0 && s >= 0;
        prev = s;
    }
    return c;
}

int main(void)
{
    int i, worst = 0;
    uint32_t n = (uint32_t)SMP_RATE * 2u;         /* 2 s of 441 Hz in slot 1 */
    for (i = -32768; i < 32768; i += 7) {         /* mu-law: within its step */
        int e = (int)(mu_dec(mu_enc(i)) * 32768.0f) - i;
        e = e < 0 ? -e : e;
        if (e * 32 > (i < 0 ? -i : i) + 256)
            worst++;
    }
    check(worst == 0, "mu-law round trip within ~3%");
    check(mu_enc(0) == 0xFF && mu_dec(0xFF) == 0.0f, "mu-law silence is 0xFF");

    for (i = 0; i < (int)n; i++)
        flash[SMP_OFF[0] + i] = mu_enc((int32_t)(16000.0 * sin(2.0 * 3.14159265 * 441.0 * i / SMP_RATE)));
    smp = SMP_DEFAULT;
    smp.n[0] = n;
    smp.attack = 0;
    smp.release = 0;
    smp_init();
    for (i = 0, worst = 0; i < 1000; i++)
        worst |= smp_next() != 0.0f;
    check(!worst, "silent with no note");

    smp_note_on(SMP_ROOT);
    i = crossings(44100);
    check(i >= 438 && i <= 444, "C4 plays it as recorded (441 Hz)");
    smp_note_off(SMP_ROOT);
    crossings(4410);
    check(voices_on() == 0, "released");

    smp_note_on(SMP_ROOT + 12);
    i = crossings(22050);
    check(i >= 438 && i <= 444, "C5 an octave up (882 Hz)");
    smp_all_off();
    crossings(4410);

    smp_note_on(SMP_ROOT);                        /* one shot: ends with the window (2 s) */
    crossings(44100 * 2 + 4410);
    check(voices_on() == 0, "one shot ends at the slot's end");
    smp.loop = 1;
    smp_note_on(SMP_ROOT);
    crossings(44100 * 5);
    check(voices_on() == 1, "loop goes round");
    smp_all_off();
    crossings(4410);

    smp.loop = 0;                                 /* a window: half from the middle */
    smp.start = 64;
    smp.len = 63;
    smp_update();
    check(fabsf(smp_a - n / 2.0f) < 2.0f && fabsf(smp_b - smp_a - n / 4.0f) < 200.0f, "start / length window");
    smp_note_on(SMP_ROOT);
    crossings(44100 / 2 + 2205);
    check(voices_on() == 0, "the window's one shot is ~0.5 s");

    for (i = 0; i < 4; i++)
        smp_note_on(SMP_ROOT + i);
    smp_note_on(SMP_ROOT + 7);
    check(voices_on() == 4, "4 voices, the oldest stolen");
    {
        float pk = 0;
        for (i = 0; i < 44100; i++)
            pk = fmaxf(pk, fabsf(smp_next()));
        check(pk <= 1.0f, "4 voices stay under full scale");
    }
    smp.n[0] = 0;
    smp_update();
    check(smp_next() == 0.0f, "an empty slot is silent");
    printf(bad ? "sampler: FAILED\n" : "sampler: all ok\n");
    return bad != 0;
}
