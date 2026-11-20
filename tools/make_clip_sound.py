#!/usr/bin/env python3
"""Builds resources/sounds/clip.wav: KlipIt's clip-saved sound.

One short, dark beep: a low pipe-organ tone (D4 with a soft sub-octave and
organ harmonics) and a faint cathedral echo. 48 kHz, 16-bit stereo.
Run: python3 tools/make_clip_sound.py
"""
import os
import wave

import numpy as np

RATE = 48000
F0 = 293.66          # D4
BEEP = 0.17          # seconds the tone holds before it fades
LENGTH = 0.75        # total length, including the echo


def tone(t):
    # organ-like stack: fundamental, sub-octave, octave, twelfth; two voices a hair apart for depth
    out = np.zeros_like(t)
    for detune in (-1.5, 1.5):  # cents
        f = F0 * 2 ** (detune / 1200)
        out += (np.sin(2 * np.pi * f * t)
                + 0.30 * np.sin(2 * np.pi * f / 2 * t)
                + 0.28 * np.sin(2 * np.pi * f * 2 * t)
                + 0.10 * np.sin(2 * np.pi * f * 3 * t))
    return out / 2


def envelope(t):
    attack, release = 0.006, 0.09
    env = np.clip(t / attack, 0, 1)
    tail = t > BEEP
    env[tail] *= np.exp(-(t[tail] - BEEP) / release * 3)
    return env


def echo(x, seed):
    # small Schroeder reverb: parallel combs, then allpasses
    rng = np.random.default_rng(seed)
    combs = [int(RATE * d) for d in (0.0297, 0.0371, 0.0411, 0.0437)]
    combs = [c + int(rng.integers(0, 40)) for c in combs]
    wet = np.zeros_like(x)
    for d in combs:
        y = np.zeros_like(x)
        for i in range(len(x)):
            y[i] = x[i] + (0.72 * y[i - d] if i >= d else 0)
        wet += y
    wet /= len(combs)
    for d, g in ((int(RATE * 0.005), 0.7), (int(RATE * 0.0017), 0.7)):
        y = np.zeros_like(wet)
        for i in range(len(wet)):
            xd = wet[i - d] if i >= d else 0
            yd = y[i - d] if i >= d else 0
            y[i] = -g * wet[i] + xd + g * yd
        wet = y
    return wet


def main():
    t = np.arange(int(RATE * LENGTH)) / RATE
    dry = tone(t) * envelope(t)
    # gentle low-pass so it stays dark rather than piercing
    dry = np.convolve(dry, np.ones(6) / 6, mode="same")
    left = dry + 0.22 * echo(dry, 1)
    right = dry + 0.22 * echo(dry, 2)
    st = np.stack([left, right], axis=1)
    fade = np.ones(len(t))
    n = int(RATE * 0.08)
    fade[-n:] = np.linspace(1, 0, n)
    st *= fade[:, None]
    st *= 10 ** (-7 / 20) / np.max(np.abs(st))  # peak at -7 dBFS, about as loud as the old chime
    pcm = (st * 32767).astype("<i2")
    out = os.path.join(os.path.dirname(__file__), "..", "resources", "sounds", "clip.wav")
    with wave.open(out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(pcm.tobytes())
    print("wrote", os.path.normpath(out), f"{LENGTH:.2f}s")


if __name__ == "__main__":
    main()
