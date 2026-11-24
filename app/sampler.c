/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal; the sampler fork's changes (notes/SAMPLER-FORK.md) */
/* The sampler for the keys (ARP's page), in the synth's place: 4 voices, each reading the slot's sound from flash in
 * place (plat_data: mu-law, 8 bits, SMP_RATE) at a key's speed, C4 as it was recorded and a semitone a key (varispeed,
 * as an SK-1 or a Mirage: higher is shorter), with linear interpolation, through an attack / release envelope (full
 * sustain while held), then a soft limit (tanh). smp_next() runs in the audio interrupt (radio_render), a sample at a
 * time; the rest from the main loop. The slots' sound is written by radio.c (GRAB, REC: from the FX history).
 * Settings 0..127. Part of the app's unity build; tests/sampler_test.c builds it alone. */
#include <stdint.h>
#include "fastmath.h"

/* G.711 mu-law: the FX history and the slots, 8 bits a sample for 14 */
static uint8_t mu_enc(int32_t x)                  /* x: -32768..32767 */
{
    uint8_t sign = 0;
    int e;
    if (x < 0)
        x = -x, sign = 0x80;
    if (x > 32635)
        x = 32635;
    x += 0x84;
    for (e = 7; e > 0 && !(x & (0x80 << e)); e--)
        ;
    return (uint8_t)~(sign | e << 4 | (x >> (e + 3) & 0x0F));
}
static inline float mu_dec(uint8_t u)             /* -1..1 */
{
    int32_t t;
    u = (uint8_t)~u;
    t = ((((int32_t)u & 0x0F) << 3) + 0x84) << ((u & 0x70) >> 4);
    t -= 0x84;
    return (float)(u & 0x80 ? -t : t) * (1.0f / 32768.0f);
}

#define SMP_VOICES 4
#define SMP_SLOTS 4
#define SMP_DIV 2u                                /* a slot's rate: the history's 22.05 kHz / SMP_DIV */
#define SMP_RATE (22050.0f / (float)SMP_DIV)
#define SMP_ROOT 60                               /* C4: the key that plays a slot as it was recorded */
/* the slots: flash Radio's own data never uses (plat.h's two windows; the places and the store are below 0xE0000) */
static const uint32_t SMP_OFF[SMP_SLOTS] = {PLAT_XDATA1_LO, PLAT_XDATA1_LO + 0x7000u, PLAT_XDATA1_LO + 0xE000u,
                                            PLAT_XDATA0_LO};
static const uint32_t SMP_SIZE[SMP_SLOTS] = {0x7000u, 0x7000u, 0x7000u, 0x4000u};

typedef struct {
    uint8_t start, len, attack, release, slot, loop;
    int8_t octave;
    uint8_t rsv;
    uint32_t n[SMP_SLOTS];                        /* each slot's length, samples (bytes); 0 empty */
} smp_set_t;
static const smp_set_t SMP_DEFAULT = {0, 127, 2, 40, 0, 0, 0, 0, {0, 0, 0, 0}};
static smp_set_t smp = {0, 127, 2, 40, 0, 0, 0, 0, {0, 0, 0, 0}};

typedef struct {
    float pos, inc, env;                          /* read position in the slot (samples), its step; the envelope */
    uint8_t note, gate, on;
    uint32_t age;
} smp_voice_t;
static smp_voice_t smv[SMP_VOICES];
static uint32_t smp_age;
static const uint8_t *volatile smp_base;          /* the slot's sound in flash; 0: none (empty) */
static volatile float smp_a, smp_b;               /* the window played: from, to (samples) */
static float smp_att, smp_rel;

static float smp_attack_ms(void) { return fm_exp2f(10.966f * (float)smp.attack / 127.0f); }          /* 1..2000 */
static float smp_release_ms(void) { return 5.0f * fm_exp2f(9.229f * (float)smp.release / 127.0f); }  /* 5..3000 */

static void smp_update(void)                      /* the window, the envelope's steps, from smp (main loop) */
{
    uint32_t n = smp.n[smp.slot];
    float a, b;
    if (n > SMP_SIZE[smp.slot])
        n = 0;                                    /* (not a length this fork wrote) */
    a = (float)n * (float)smp.start / 128.0f;
    b = a + ((float)n - a) * (float)(smp.len + 1u) / 128.0f;
    if (b > (float)n - 1.0f)
        b = (float)n - 1.0f;
    if (b < a + 32.0f)
        b = a + 32.0f;
    smp_base = 0;                                 /* the interrupt sees none while the window moves */
    smp_a = a;
    smp_b = b;
    smp_att = 1000.0f / (smp_attack_ms() * 44100.0f);
    smp_rel = 1000.0f / (smp_release_ms() * 44100.0f);
    smp_base = n >= 64u ? plat_data(SMP_OFF[smp.slot]) : 0;
}

static void smp_init(void)
{
    uint32_t i;
    for (i = 0; i < SMP_VOICES; i++)
        smv[i].on = smv[i].gate = 0, smv[i].env = 0.0f;
    smp_update();
}

static void smp_note_on(int note)
{
    uint32_t i, v = 0;
    for (i = 0; i < SMP_VOICES; i++)              /* the same note again, or a free voice, or the oldest */
        if (smv[i].on && smv[i].note == note) {
            v = i;
            goto take;
        }
    for (i = 0; i < SMP_VOICES; i++)
        if (!smv[i].on) {
            v = i;
            goto take;
        }
    for (i = 1; i < SMP_VOICES; i++)
        if (smv[i].age < smv[v].age)
            v = i;
take:
    smv[v].on = 0;                                /* (the interrupt leaves it alone while it changes) */
    smv[v].note = (uint8_t)note;
    smv[v].inc = fm_exp2f((float)(note - SMP_ROOT) / 12.0f) * (SMP_RATE / 44100.0f);
    smv[v].pos = smp_a;
    smv[v].env = 0.0f;
    smv[v].age = ++smp_age;
    smv[v].gate = 1;
    smv[v].on = 1;                                /* last: the interrupt sees a whole voice */
}

static void smp_note_off(int note)
{
    uint32_t i;
    for (i = 0; i < SMP_VOICES; i++)
        if (smv[i].gate && smv[i].note == note)
            smv[i].gate = 0;
}

static void smp_all_off(void)
{
    uint32_t i;
    for (i = 0; i < SMP_VOICES; i++)
        smv[i].gate = 0;
}

static void smp_hush(void)                        /* every voice silent now (the slot is being written) */
{
    uint32_t i;
    for (i = 0; i < SMP_VOICES; i++)
        smv[i].on = smv[i].gate = 0;
}

static float smp_next(void)                       /* a sample of the mix (the audio interrupt) */
{
    const uint8_t *s = smp_base;
    float a = smp_a, b = smp_b, sum = 0.0f;
    uint32_t i;
    int any = 0;
    if (!s)
        return 0.0f;
    for (i = 0; i < SMP_VOICES; i++) {
        smp_voice_t *v = &smv[i];
        float p, x;
        uint32_t k;
        if (!v->on)
            continue;
        if (v->gate) {
            v->env += smp_att;
            if (v->env > 1.0f)
                v->env = 1.0f;
        } else {
            v->env -= smp_rel;
            if (v->env <= 0.0f) {
                v->env = 0.0f;
                v->on = 0;
                continue;
            }
        }
        p = v->pos;
        if (p >= b || p < a) {                    /* the window's end: round again, or the voice is done */
            if (!smp.loop || p < a) {
                v->on = 0;
                continue;
            }
            p = a + (p - b);
            if (p >= b)
                p = a;
        }
        k = (uint32_t)p;
        x = mu_dec(s[k]);
        x += (mu_dec(s[k + 1u]) - x) * (p - (float)k);
        v->pos = p + v->inc;
        sum += x * v->env;
        any = 1;
    }
    return any ? fm_tanhf(sum) : 0.0f;            /* 4 voices of a loud station stay under full scale */
}
