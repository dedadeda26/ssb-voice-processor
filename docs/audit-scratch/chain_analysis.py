"""Numeric checks of the SSB Voice Processor default signal chain.

All biquads use the same RBJ cookbook formulas as JUCE
(juce::dsp::IIR::ArrayCoefficients::makeLowPass / makeHighPass), so the
magnitude numbers below describe the shipped code, not an approximation of it.
"""
import numpy as np
from scipy import signal

SR = 48000.0


def biquad(kind, f, q=0.7071067811865476, fs=SR):
    w0 = 2.0 * np.pi * f / fs
    cw = np.cos(w0)
    alpha = np.sin(w0) / (2.0 * q)
    if kind == "lp":
        b = [(1 - cw) / 2, 1 - cw, (1 - cw) / 2]
        a = [1 + alpha, -2 * cw, 1 - alpha]
    elif kind == "hp":
        b = [(1 + cw) / 2, -(1 + cw), (1 + cw) / 2]
        a = [1 + alpha, -2 * cw, 1 - alpha]
    else:
        raise ValueError(kind)
    b = np.array(b) / a[0]
    a = np.array(a) / a[0]
    return b, a


def cascade_response(sections, freqs, fs=SR):
    """Complex response of a cascade of (b, a) sections."""
    w = 2 * np.pi * np.asarray(freqs) / fs
    z = np.exp(1j * w)
    h = np.ones_like(z, dtype=complex)
    for b, a in sections:
        num = b[0] + b[1] / z + b[2] / z ** 2
        den = a[0] + a[1] / z + a[2] / z ** 2
        h *= num / den
    return h


def butter_sections(kind, f, order, fs=SR):
    """order-N Butterworth as a cascade of order/2 sections with the right Qs."""
    qs = [1.0 / (2.0 * np.cos(np.pi * (2 * k - 1) / (2 * order)))
          for k in range(1, order // 2 + 1)]
    return [biquad(kind, f, q, fs) for q in qs]


def db(x):
    return 20 * np.log10(np.maximum(np.abs(x), 1e-12))


print("=== 1. Default chain linear response (all dynamics at unity) ===")
print("chain: InputGain(0dB) -> HighPass 90Hz Butterworth -> "
      "NoiseGate(open) -> EQ(flat) -> MBC(unity) -> Comp(below thr) -> "
      "Saturation(linear part) -> DeEsser(unity) -> BandLimit 90..3100 -> Limiter")
sec = [biquad("hp", 90.0), biquad("hp", 90.0), biquad("lp", 3100.0)]
probe = [100, 200, 300, 500, 1000, 2000, 2700, 3100, 4000, 5000, 6200, 8000, 10000, 15000, 20000]
h = cascade_response(sec, probe)
print(f"{'f(Hz)':>8} {'gain(dB)':>9}")
for f, g in zip(probe, db(h)):
    print(f"{f:>8} {g:>9.2f}")

print()
print("=== 2. How steep is the shipped band limit? (attenuation at 2x / 3x cutoff) ===")
print("Shipped: 2nd-order LP at 3100 Hz (12 dB/oct).")
for order in (2, 4, 6, 8):
    hs = cascade_response(butter_sections("lp", 3100.0, order), [3100, 6200, 9300, 12400, 20000])
    print(f" order {order} ({6*order:>2} dB/oct): " +
          "  ".join(f"{f/1000:.1f}k={g:6.1f}dB" for f, g in zip([3100, 6200, 9300, 12400, 20000], db(hs))))

print()
print("=== 3. Saturation aliasing: direct 48 kHz tanh vs 8x oversampled reference ===")


def chain_after_sat(x, fs):
    """BandLimit HP90 + LP3100 that follows the saturator in the real chain."""
    out = x
    for kind, f in (("hp", 90.0), ("lp", 3100.0)):
        b, a = biquad(kind, f, fs=fs)
        out = signal.lfilter(b, a, out)
    return out


def make_input(freq, amp, n, two_tone):
    t = np.arange(n) / SR
    if two_tone:
        return amp * (np.sin(2 * np.pi * freq * t) + np.sin(2 * np.pi * (freq + 300) * t)) / 2
    return amp * np.sin(2 * np.pi * freq * t)


def rms(x):
    return float(np.sqrt(np.mean(x ** 2)))


def alias_error(freq, amp, drive_db, two_tone=False):
    """Alias distortion = shipped 48 kHz nonlinearity vs an 8x oversampled one."""
    n = int(SR * 1.0)
    x = make_input(freq, amp, n, two_tone)
    drive = 10 ** (drive_db / 20.0)
    comp = 1.0 / np.sqrt(drive)

    yA = chain_after_sat(np.tanh(x * drive) * comp, SR)          # shipped
    up = signal.resample_poly(x, 8, 1)
    yB = chain_after_sat(signal.resample_poly(np.tanh(up * drive) * comp, 1, 8), SR)

    err = yA - yB
    return 20 * np.log10(max(rms(err), 1e-12) / max(rms(yB), 1e-12))


# sanity: with the nonlinearity replaced by a plain gain both paths must agree
n = int(SR * 1.0)
_sanity = make_input(2900.0, 0.25, n, False)
_a = _sanity
_b = signal.resample_poly(signal.resample_poly(_sanity, 8, 1), 1, 8)
print(f"   (resampler-only residual: {20*np.log10(max(rms(_a-_b),1e-12)/rms(_b)):.1f} dB "
      "-> the test below therefore uses a direct spur search instead)")

print()
print("=== 3b. Direct alias-spur search: 2.9 kHz tone, LP applied, spur in 1.0-1.6 kHz ===")
print("   17th harmonic of 2.9 kHz = 49.3 kHz -> folds to 1.3 kHz at 48 kHz; the 2nd-order")
print("   low-pass cannot remove it because it lands inside the transmitted band.")
for amp, label in ((0.06, "-24 dBFS"), (0.25, "-12 dBFS"), (0.5, "-6 dBFS")):
    row = []
    for drive_db in (0, 6, 12, 18, 24):
        n = int(SR * 2.0)
        t = np.arange(n) / SR
        x = amp * np.sin(2 * np.pi * 2900.0 * t)
        drive = 10 ** (drive_db / 20.0)
        y = chain_after_sat(np.tanh(x * drive) / np.sqrt(drive), SR)
        w = np.hanning(n)
        s = np.abs(np.fft.rfft(y * w)) / (np.sum(w) / 2)
        f = np.fft.rfftfreq(n, 1 / SR)
        fund = db(s[np.argmin(np.abs(f - 2900.0))])
        band = (f > 1000) & (f < 1600)
        spur = db(s[band]).max() - fund
        row.append(f"{drive_db:>2}dB={spur:>6.1f}dBc")
    print(f" {label:>10}: " + "  ".join(row))

print()
print("=== 4. Out-of-band energy the radio actually sees (IMD/splatter) ===")
print("Two-tone 2.7k+3.0k, -12 dBFS, default drive +6 dB, measured after the chain's band limit")
amp = 0.25
for drive_db in (0, 6, 12, 24):
    n = int(SR * 1.0)
    t = np.arange(n) / SR
    x = amp * (np.sin(2 * np.pi * 2700 * t) + np.sin(2 * np.pi * 3000 * t)) / 2
    drive = 10 ** (drive_db / 20.0)
    y = np.tanh(x * drive) / np.sqrt(drive)
    y = chain_after_sat(y, SR)
    w = np.hanning(n)
    s = np.abs(np.fft.rfft(y * w)) / (np.sum(w) / 2)
    f = np.fft.rfftfreq(n, 1 / SR)
    fund = db(s[np.argmin(np.abs(f - 2700))])
    for band_lo, band_hi in ((3100, 6200), (6200, 12000)):
        m = (f >= band_lo) & (f < band_hi)
        peak = db(s[m]).max()
        print(f" drive {drive_db:>2}dB: worst {band_lo}-{band_hi}Hz component = {peak:>6.1f} dBFS "
              f"({peak - fund:>6.1f} dBc)")
