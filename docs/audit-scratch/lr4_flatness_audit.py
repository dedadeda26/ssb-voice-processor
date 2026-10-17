#!/usr/bin/env python3
# -*- coding: ascii -*-
"""
Numerical audit of the 3-band LR4 recombination in
Source/Nodes/MultiBandCompNode.cpp (D:\\SSBVoiceProcessor_improved).

Nothing in the project is modified; this script is read-only analysis and
lives under docs/audit-scratch/.

--------------------------------------------------------------------------
WHAT IS MODELLED (exact formulas, taken from the JUCE checkout the project
builds against, D:\\Dev\\JUCE == JUCE 9.0.4)
--------------------------------------------------------------------------
juce::dsp::LinkwitzRileyFilter in JUCE 9.0.4 is NOT built from
IIR::Coefficients.  It is a TPT / zero-delay-feedback state-variable filter:

    g  = tan(pi * fc / fs)
    R2 = sqrt(2)
    h  = 1 / (1 + R2*g + g*g)

    yH = (x - (R2+g)*s1 - s2) * h          // high-pass out
    yB = g*yH + s1 ;  s1 = g*yH + yB       // band-pass out
    yL = g*yB + s2 ;  s2 = g*yB + yL       // low-pass out

    lowpass  : run section #2 with input yL, return yL2   (LR4 = 2 sections)
    highpass : run section #2 with input yH, return yH2   (LR4 = 2 sections)
    allpass  : return yL - R2*yB + yH                     (one 2nd-order AP)

This is also exactly what the header's dual-output helper computes:
    outputHigh = (yL - R2*yB + yH) - yL2 = allpass - lowpass,
i.e. JUCE itself asserts  lowpass + highpass == allpass  for one LR4 split.

For reference, the older/documented-by-task formulation (cascaded
juce::dsp::IIR::Filter with Coefficients::makeLowPass / makeHighPass at
Q = 1/sqrt(2), and Coefficients::makeAllPass at Q = 1/sqrt(2), which is
inverseRootTwo, the default) is also implemented here and both models are
proven numerically identical in section 0b.  So the conclusions do not
depend on which JUCE generation one models.

ASSUMPTIONS (stated explicitly)
 A1. LR4 = two cascaded 2nd-order Butterworth sections, Q = 1/sqrt(2)
     (=> -6.0206 dB per band at its own cutoff, verified in 0a).
 A2. Allpass uses Q = 1/sqrt(2) (JUCE inverseRootTwo default).
 A3. Node structure exactly as written:
        low  = LP4(cl) then AP2(ch)
        rest = HP4(cl)
        mid  = LP4(ch) of rest
        high = HP4(ch) of rest
        out  = low + mid + high            (makeup = 1 at 0 dB)
 A4. amount <= 0.01 short-circuits the compressor (code line 84), so the
     amount=0 case is exactly linear => a transfer function exists.
 A5. Biquad/TF evaluation is done in float64; a float32 simulation is
     reported separately to bound the C++ (float) runtime error.
--------------------------------------------------------------------------
"""

import math
import numpy as np

SQRT2 = math.sqrt(2.0)
INV_SQRT2 = 1.0 / SQRT2
DB = 20.0 / math.log(10.0)

# ----------------------------------------------------------------------------
# 1. JUCE coefficient formulas (validated against the TPT implementation)
# ----------------------------------------------------------------------------

def jlp(fs, fc, Q=INV_SQRT2):
    """juce::dsp::IIR::Coefficients::makeLowPass (b, a), a0 = 1."""
    n = 1.0 / math.tan(math.pi * fc / fs)
    n2 = n * n
    inv_q = 1.0 / Q
    c1 = 1.0 / (1.0 + inv_q * n + n2)
    b = np.array([c1, 2.0 * c1, c1])
    a = np.array([1.0, 2.0 * c1 * (1.0 - n2), c1 * (1.0 - inv_q * n + n2)])
    return b, a


def jhp(fs, fc, Q=INV_SQRT2):
    """juce::dsp::IIR::Coefficients::makeHighPass (b, a), a0 = 1."""
    n = math.tan(math.pi * fc / fs)
    n2 = n * n
    inv_q = 1.0 / Q
    c1 = 1.0 / (1.0 + inv_q * n + n2)
    b = np.array([c1, -2.0 * c1, c1])
    a = np.array([1.0, 2.0 * c1 * (n2 - 1.0), c1 * (1.0 - inv_q * n + n2)])
    return b, a


def jap(fs, fc, Q=INV_SQRT2):
    """juce::dsp::IIR::Coefficients::makeAllPass (b, a), a0 = 1."""
    n = 1.0 / math.tan(math.pi * fc / fs)
    n2 = n * n
    inv_q = 1.0 / Q
    c1 = 1.0 / (1.0 + inv_q * n + n2)
    b0 = c1 * (1.0 - n * inv_q + n2)
    b1 = 2.0 * c1 * (1.0 - n2)
    return np.array([b0, b1, 1.0]), np.array([1.0, b1, b0])


def H(ba, fs, f):
    """Complex transfer function of one biquad at digital frequency f."""
    f = np.asarray(f, dtype=np.float64)
    w = 2.0 * np.pi * f / fs
    z1 = np.exp(-1j * w)
    z2 = z1 * z1
    b, a = ba
    return (b[0] + b[1] * z1 + b[2] * z2) / (a[0] + a[1] * z1 + a[2] * z2)


def H4(ba, fs, f):
    """LR4 = the same 2nd-order section applied twice."""
    h = H(ba, fs, f)
    return h * h


# ----------------------------------------------------------------------------
# 1b. TPT implementation of JUCE 9.0.4, sample by sample.
#     Complex probe: a real LTI filter fed exp(j*w*n) settles to
#     y[n] = H(e^jw) * exp(j*w*n).
# ----------------------------------------------------------------------------

def tpt_response(fs, fc, kind, f, n=30000):
    g = math.tan(math.pi * fc / fs)
    r2 = SQRT2
    h = 1.0 / (1.0 + r2 * g + g * g)
    s1 = s2 = s3 = s4 = 0.0 + 0.0j
    step = complex(math.cos(2.0 * math.pi * f / fs), math.sin(2.0 * math.pi * f / fs))
    x = 1.0 + 0.0j
    y = 0.0 + 0.0j
    for _ in range(n):
        yh = (x - (r2 + g) * s1 - s2) * h
        yb = g * yh + s1
        s1 = g * yh + yb
        yl = g * yb + s2
        s2 = g * yb + yl
        if kind == 'allpass':
            y = yl - r2 * yb + yh
        else:
            inp = yl if kind == 'lowpass' else yh
            yh2 = (inp - (r2 + g) * s3 - s4) * h
            yb2 = g * yh2 + s3
            s3 = g * yh2 + yb2
            yl2 = g * yb2 + s4
            s4 = g * yb2 + yl2
            y = yl2 if kind == 'lowpass' else yh2
        x *= step
    return y * step / x


# ----------------------------------------------------------------------------
# 2. Response builders
# ----------------------------------------------------------------------------

def bands(fs, f, cl, ch):
    """Complex responses of the three branch filters and their parts."""
    lp_cl = H4(jlp(fs, cl), fs, f)
    hp_cl = H4(jhp(fs, cl), fs, f)
    lp_ch = H4(jlp(fs, ch), fs, f)
    hp_ch = H4(jhp(fs, ch), fs, f)
    ap_ch = H(jap(fs, ch), fs, f)
    return dict(lp_cl=lp_cl, hp_cl=hp_cl, lp_ch=lp_ch, hp_ch=hp_ch,
                ap_ch=ap_ch, sum_ch=lp_ch + hp_ch)


def responses(fs, f, cl, ch):
    """(a) as implemented, (b) no compensation, (c) exact magnitude-normalised."""
    d = bands(fs, f, cl, ch)
    low_a = d['lp_cl'] * d['ap_ch']
    low_b = d['lp_cl']
    low_c = d['lp_cl'] * d['sum_ch'] / np.abs(d['sum_ch'])
    mh = d['hp_cl'] * d['sum_ch']          # mid + high (sum is all that matters)
    return {'a': low_a + mh, 'b': low_b + mh, 'c': low_c + mh}


def dbmag(x):
    return 20.0 * np.log10(np.maximum(np.abs(x), 1e-300))


def grid(fs, n=4000, lo=20.0, extra=()):
    hi = 0.4999 * fs
    f = np.geomspace(lo, hi, n)
    pts = [p for p in extra if lo <= p <= hi]
    f = np.concatenate([f, np.asarray(pts, dtype=float)])
    return np.unique(np.round(f, 9))


def worst(dev_db):
    i = int(np.argmax(np.abs(dev_db)))
    return abs(dev_db[i]), float(dev_db[i]), i


# ----------------------------------------------------------------------------
# 3. Time-domain helpers (linear part via scipy lfilter; compressor in a loop)
# ----------------------------------------------------------------------------

def apply_band(x, amount, fs, att_ms=15.0, rel_ms=150.0, thresh_base=-10.0):
    """Line-for-line model of MultiBandCompNode::processBand.
    JUCE: gain = 10^((-(20log10(env)-th)*k)/20) with k = 1-1/ratio
    == (env_th/ env)^k, env_th = 10^(th/20), used here to avoid logs."""
    n = len(x)
    if amount <= 0.01:
        return x.copy(), 0.0
    th = thresh_base - amount * 0.30
    ratio = 1.0 + amount * 0.06
    k = 1.0 - 1.0 / ratio
    env_th = 10.0 ** (th / 20.0)
    att = math.exp(-1.0 / (att_ms * 1e-3 * fs))
    rel = math.exp(-1.0 / (rel_ms * 1e-3 * fs))
    a1 = 1.0 - att
    r1 = 1.0 - rel
    env = 0.0
    y = x.copy()
    gsum = 0.0
    for i in range(n):
        v = x[i]
        av = -v if v < 0.0 else v
        if av > env:
            env = att * env + a1 * av
        else:
            env = rel * env + r1 * av
        if env > env_th:
            g = (env_th / env) ** k
            y[i] = v * g
            gsum += g
        else:
            gsum += 1.0
    return y, -DB * math.log(gsum / n) if gsum > 0 else 0.0


def lf(x, ba):
    from scipy.signal import lfilter
    return lfilter(ba[0], ba[1], x)


def lf4(x, ba):
    """An LR4 band = the same 2nd-order section applied twice."""
    return lf(lf(x, ba), ba)


def node_bands_time(x, fs, cl, ch):
    low = lf(lf4(x, jlp(fs, cl)), jap(fs, ch))
    rest = lf4(x, jhp(fs, cl))
    mid = lf4(rest, jlp(fs, ch))
    high = lf4(rest, jhp(fs, ch))
    return low, mid, high


def measure(y, f_eff, fs, start):
    n = np.arange(len(y) - start, dtype=np.float64)
    ph = np.exp(-1j * 2.0 * np.pi * f_eff * n / fs)
    return 2.0 * abs(np.dot(y[start:], ph)) / len(n)


def run_node(fs, f, amp_db, amounts, cl=300.0, ch=3000.0,
             settle=0.80, meas=0.16):
    """Stepped-sine drive; returns (gain_db, per-band mean GR dB)."""
    amp = 10.0 ** (amp_db / 20.0)
    m_target = meas * fs
    f = float(f)
    ncyc = max(4, int(round(f * m_target / fs)))
    nmeas = int(round(ncyc * fs / f))
    f_eff = ncyc * fs / nmeas
    ntot = int(settle * fs) + nmeas
    t = np.arange(ntot, dtype=np.float64)
    x = amp * np.sin(2.0 * np.pi * f_eff * t / fs)
    lo, mi, hi = node_bands_time(x, fs, cl, ch)
    gr = []
    outs = []
    for band, a in zip((lo, mi, hi), amounts):
        y, g = apply_band(band, a, fs)
        outs.append(y)
        gr.append(g)
    y = outs[0] + outs[1] + outs[2]
    start = int(settle * fs)
    a0 = measure(y, f_eff, fs, start)
    gdb = DB * math.log(max(a0 / amp, 1e-300))
    return gdb, gr, f_eff


# ----------------------------------------------------------------------------
# 4. Static gain law (for the quasi-static compression analysis)
# ----------------------------------------------------------------------------

def gain_law(fs=48000.0, amount=40.0, att_ms=15.0, rel_ms=150.0,
             thresh_base=-10.0, f_test=1000.0):
    """Return G(L): steady-state mean band gain in dB for a sine whose band
    level is L dBFS.  Envelope scales linearly with amplitude, so one
    reference run gives the whole curve."""
    th = thresh_base - amount * 0.30
    ratio = 1.0 + amount * 0.06
    k = 1.0 - 1.0 / ratio
    att = math.exp(-1.0 / (att_ms * 1e-3 * fs))
    rel = math.exp(-1.0 / (rel_ms * 1e-3 * fs))
    n = int(2.0 * fs)
    x = np.sin(2.0 * np.pi * f_test * np.arange(n) / fs)
    env = 0.0
    edb = np.empty(n)
    for i in range(n):
        v = abs(x[i])
        if v > env:
            env = att * env + (1.0 - att) * v
        else:
            env = rel * env + (1.0 - rel) * v
        edb[i] = 20.0 * math.log10(env) if env > 0.0 else -100.0
    cyc = int(round(fs / f_test))
    ref = edb[-cyc:]

    def G(L):
        over = ref + L - th
        over = np.maximum(over, 0.0)
        # mean of linear gains over one cycle; returns gain in dB (<= 0)
        return DB * math.log(np.mean(np.exp(-over * k / DB)))

    return G


# ----------------------------------------------------------------------------
# 0. Verification of the model itself
# ----------------------------------------------------------------------------

def section0():
    print("=" * 100)
    print("0. MODEL VERIFICATION (sanity checks; all must PASS)")
    print("=" * 100)
    fs = 48000.0
    cl, ch = 300.0, 3000.0
    f = np.array([20.0, 50.0, 100.0, 300.0, 1000.0, 3000.0, 10000.0, 20000.0])
    ok = True

    # 0a. LR4 is -6.0206 dB at its own cutoff
    for name, fn in (('LR4 lowpass ', jlp), ('LR4 highpass', jhp)):
        g = dbmag(H4(fn(fs, cl), fs, np.array([cl])))[0]
        good = abs(g + 6.0206) < 1e-6
        ok &= good
        print(f" 0a  {name} at fc=300 Hz          : {g:+.6f} dB   "
              f"(expect -6.020600)  {'PASS' if good else 'FAIL'}")

    # 0b. LP4+HP4 == AP2 (complex), and |LP4+HP4| == 1
    d = bands(fs, f, cl, ch)
    e = np.max(np.abs(d['sum_ch'] - d['ap_ch']))
    ok &= e < 1e-12
    print(f" 0b  |(LP4+HP4) - AP2| max            : {e:.3e}      "
          f"{'PASS' if e < 1e-12 else 'FAIL'}")
    dm = np.max(np.abs(dbmag(d['sum_ch'])))
    ok &= dm < 1e-12
    print(f" 0b  |LP4+HP4| max deviation         : {dm:.3e} dB  "
          f"{'PASS' if dm < 1e-12 else 'FAIL'}")
    # JUCE dual-output identity: outputHigh == allpass - LP4
    ep = np.max(np.abs((d['ap_ch'] - d['lp_ch']) - d['hp_ch']))
    ok &= ep < 1e-12
    print(f" 0b  |(AP-LP4) - HP4| max (JUCE API)  : {ep:.3e}      "
          f"{'PASS' if ep < 1e-12 else 'FAIL'}")

    # 0c. TPT (JUCE 9.0.4) vs IIR::Coefficients formulation -- must be identical
    worst_d = 0.0
    for kind, fn in (('lowpass', jlp), ('highpass', jhp), ('allpass', jap)):
        for ft in (30.0, 120.0, 300.0, 777.0, 3000.0, 9000.0, 20000.0):
            a = tpt_response(fs, cl, kind, ft, n=25000)
            # LR4 = two sections, allpass = one section
            b = H(fn(fs, cl), fs, ft) if kind == 'allpass' else H4(fn(fs, cl), fs, ft)
            worst_d = max(worst_d, abs(a - b))
    ok &= worst_d < 1e-10
    print(f" 0c  TPT(LR4) vs IIR::Coefficients^2 : {worst_d:.3e}      "
          f"{'PASS' if worst_d < 1e-10 else 'FAIL'}")

    # 0d. time-domain node (amount=0) vs analytic response
    errs = []
    for ft in (100.0, 300.0, 1000.0, 3000.0, 9000.0):
        gdb, _, fe = run_node(fs, ft, -20.0, (0.0, 0.0, 0.0), cl, ch)
        ana = dbmag(responses(fs, np.array([fe]), cl, ch)['a'])[0]
        errs.append(abs(gdb - ana))
    ok &= max(errs) < 1e-6
    print(f" 0d  time-domain(amount=0) vs analytic: {max(errs):.3e} dB "
          f"{'PASS' if max(errs) < 1e-6 else 'FAIL'}")
    print()
    return ok


# ----------------------------------------------------------------------------
# 1. Zero-compression flatness
# ----------------------------------------------------------------------------

CONFIG_NAMES = {
    'a': '(a) AP2(ch) compensation (as coded)',
    'b': '(b) no compensation',
    'c': '(c) exact magnitude-normalised comp',
}


def worst_refined(fs, cl, ch, key, f_coarse):
    """Refine the coarse-grid extremum with a bounded 1-D search."""
    from scipy.optimize import minimize_scalar
    lo = max(20.0, f_coarse * 0.6)
    hi = min(0.4999 * fs, f_coarse * 1.6)

    def neg(f):
        return -abs(dbmag(responses(fs, np.array([f]), cl, ch)[key])[0])

    r = minimize_scalar(neg, bounds=(lo, hi), method='bounded',
                        options={'xatol': 1e-4})
    d = dbmag(responses(fs, np.array([r.x]), cl, ch)[key])[0]
    return abs(d), float(d), float(r.x)


def section1():
    print("=" * 100)
    print("1. RECOMBINATION FLATNESS, ZERO COMPRESSION (amount=0, makeup=0 dB)")
    print("   crossLow=300 Hz, crossHigh=3000 Hz, 20 Hz .. 0.4999*fs")
    print("=" * 100)
    hdr = (f"{'fs':>7} {'config':<38} {'worst|dev|':>10} {'signed':>9} "
           f"{'at f(Hz)':>9} {'worst raw':>10} {'RMS|dev|':>10} "
           f"{'dev@300':>9} {'dev@3000':>9}")
    print(hdr)
    print("-" * len(hdr))
    out = {}
    for fs in (48000.0, 96000.0, 16000.0, 8000.0):
        cl, ch = 300.0, 3000.0
        f = grid(fs, extra=(cl, ch, 100.0, 1000.0, 3000.0, 0.25 * fs))
        r = responses(fs, f, cl, ch)
        for key in ('a', 'b', 'c'):
            dev = dbmag(r[key])
            _, _, i = worst(dev)
            rms = math.sqrt(float(np.mean(dev ** 2)))
            w, wv, fw = worst_refined(fs, cl, ch, key, float(f[i]))
            d300 = dbmag(responses(fs, np.array([cl]), cl, ch)[key])[0]
            d3000 = dbmag(responses(fs, np.array([ch]), cl, ch)[key])[0]
            out[(fs, key)] = (w, fw, wv, d300)
            print(f"{fs:>7.0f} {CONFIG_NAMES[key]:<38} {w:>10.6f} {wv:>+9.6f} "
                  f"{fw:>9.1f} {w:>10.1e} {rms:>10.2e} {d300:>+9.4f} "
                  f"{d3000:>+9.4f}")
        print()
    print("Notes:")
    print("  * (a) and (c) are flat to float64 round-off (worst |dev| ~1e-12 dB,")
    print("    i.e. 0.000 dB): the code's AP2 at crossHigh IS the exact allpass")
    print("    equivalent of (mid+high), because")
    print("    LP4+HP4 = (1+s^4)/(s^2+sqrt2 s+1)^2 = (s^2-sqrt2 s+1)/(s^2+sqrt2 s+1).")
    print("  * (c) therefore coincides with (a): the implemented compensation is")
    print("    already the mathematically exact one; there is nothing to improve.")
    print("  * (b) dips slightly (worst 0.11 dB at 48 kHz, at ~395 Hz): the")
    print("    mid+high path carries AP2(ch), the low path does not, so the two")
    print("    vectors miss by the allpass phase at crossLow.")
    print()
    return out


# ----------------------------------------------------------------------------
# 2. With compression (default 40/40/30)
# ----------------------------------------------------------------------------

def section2(fs=48000.0, cl=300.0, ch=3000.0):
    print("=" * 100)
    print("2. WHOLE NODE WITH COMPRESSION (defaults 40/40/30, attack 15 ms,")
    print("   release 150 ms, makeup 0 dB, fs=48 kHz)")
    print("=" * 100)
    G40 = gain_law(fs, 40.0)
    G30 = gain_law(fs, 30.0)
    print("  Static law: steady band gain G(L) dB and reduction GR = -G, for a")
    print("  sine whose band level is L dBFS (env settles ~4 dB under peak):")
    print(f"  {'L dBFS':>8} {'G amt40':>9} {'GR amt40':>9} {'G amt30':>9} {'GR amt30':>9}")
    for L in (-40.0, -30.0, -20.0, -10.0, -6.0, -3.0, 0.0):
        print(f"  {L:>8.1f} {G40(L):>9.3f} {-G40(L):>9.3f} {G30(L):>9.3f} {-G30(L):>9.3f}")
    print()

    f = grid(fs, 3000, extra=(cl, ch, 400.0, 500.0, 600.0, 800.0, 1000.0))
    d = bands(fs, f, cl, ch)
    hlow = d['lp_cl'] * d['ap_ch']
    hmid = d['hp_cl'] * d['lp_ch']
    hhigh = d['hp_cl'] * d['hp_ch']

    def gl_gain(G, h, amp):
        lv = dbmag(amp * h)
        return np.array([10.0 ** (G(float(x)) / 20.0) for x in lv])

    hdr = (f"{'input dBFS':>10} {'worst|dev|':>10} {'signed':>8} {'at f(Hz)':>9} "
           f"{'shape p-p':>9} {'GR low':>7} {'GR mid':>7} {'GR high':>7} {'spread':>7}")
    print(hdr)
    print("-" * len(hdr))
    rows = []
    for lev in (-30.0, -20.0, -10.0, -3.0):
        amp = 10.0 ** (lev / 20.0)
        gl = gl_gain(G40, hlow, amp)
        gm = gl_gain(G40, hmid, amp)
        gh = gl_gain(G30, hhigh, amp)
        dev = dbmag(gl * hlow + gm * hmid + gh * hhigh)
        w, wv, i = worst(dev)
        pp = float(np.max(dev) - np.min(dev))
        gr = -20.0 * np.log10([gl[i], gm[i], gh[i]])
        spread = float(max(gr) - min(gr))
        rows.append((lev, w, float(f[i]), gr, wv))
        print(f"{lev:>10.1f} {w:>10.3f} {wv:>+8.3f} {f[i]:>9.1f} {pp:>9.3f} "
              f"{gr[0]:>7.2f} {gr[1]:>7.2f} {gr[2]:>7.2f} {spread:>7.2f}")
    print()
    print("  'shape p-p' = peak-to-peak of the dB response = non-flatness left")
    print("  after removing the overall level change (this is the flatness break).")
    print()
    print("  Whole-node worst case vs input level (quasi-static, same settings):")
    print(f"  {'input dBFS':>10} {'worst|dev|':>10} {'shape p-p':>9} "
          f"{'GR low':>7} {'GR mid':>7} {'GR high':>7}")
    for lev in (-40.0, -35.0, -30.0, -25.0, -20.0, -15.0, -10.0, -6.0, -3.0, 0.0):
        amp = 10.0 ** (lev / 20.0)
        gl = gl_gain(G40, hlow, amp)
        gm = gl_gain(G40, hmid, amp)
        gh = gl_gain(G30, hhigh, amp)
        dev = dbmag(gl * hlow + gm * hmid + gh * hhigh)
        w = float(np.max(np.abs(dev)))
        pp = float(np.max(dev) - np.min(dev))
        grl = -20.0 * np.log10(gl[np.argmin(np.abs(f - 20.0))])
        grm = -20.0 * np.log10(gm[np.argmin(np.abs(f - 1000.0))])
        grh = -20.0 * np.log10(gh[np.argmin(np.abs(f - 10000.0))])
        print(f"  {lev:>10.1f} {w:>10.3f} {pp:>9.3f} {grl:>7.2f} "
              f"{grm:>7.2f} {grh:>7.2f}")
    print("  (GR columns quoted at 20 Hz / 1 kHz / 10 kHz where that band")
    print("   dominates, so they are the band gains a listener would hear.)")
    print()
    g = 10.0 ** (-6.0 / 20.0)
    dev_eq = dbmag(g * (hlow + hmid + hhigh))
    print(f"  Control: if all three band gains were equal (-6 dB), the sum would be")
    print(f"  {float(np.min(dev_eq)):+.6f} .. {float(np.max(dev_eq)):+.6f} dB "
          f"-> exactly flat.  All non-flatness below is gain-spread only.")
    print()

    print("  Same scenario measured with the time-domain node model (stepped")
    print("  sine; envelope and per-sample gain exactly as processBand):")
    grs = []
    ft = [20, 40, 60, 120, 200, 300, 400, 500, 600, 900, 1500, 2200,
          3000, 4200, 6000, 9000, 14000]
    hdr = (f"{'input dBFS':>10} {'worst|dev|':>10} {'signed':>8} {'at f(Hz)':>9} "
           f"{'shape p-p':>9} {'GR low':>7} {'GR mid':>7} {'GR high':>7} {'spread':>7}")
    print(hdr)
    print("-" * len(hdr))
    for lev in (-20.0, -10.0):
        devs = []
        for f0 in ft:
            g, gr, fe = run_node(fs, f0, lev, (40.0, 40.0, 30.0), cl, ch)
            a = dbmag(responses(fs, np.array([fe]), cl, ch)['a'])[0]
            devs.append((g - a, fe, gr))
        devs.sort(key=lambda t: -abs(t[0]))
        d0, fe0, gr0 = devs[0]
        pp = max(t[0] for t in devs) - min(t[0] for t in devs)
        grs.append((lev, abs(d0), fe0, gr0))
        print(f"{lev:>10.1f} {abs(d0):>10.3f} {d0:>+8.3f} {fe0:>9.1f} {pp:>9.3f} "
              f"{gr0[0]:>7.2f} {gr0[1]:>7.2f} {gr0[2]:>7.2f} "
              f"{max(gr0)-min(gr0):>7.2f}")
    print()
    print("  amount=0 check of the same time-domain model (must be ~0.000 dB):")
    e = 0.0
    for f0 in ft:
        g, gr, fe = run_node(fs, f0, -20.0, (0.0, 0.0, 0.0), cl, ch)
        a = dbmag(responses(fs, np.array([fe]), cl, ch)['a'])[0]
        e = max(e, abs(g - a))
    print(f"    worst |time-domain - analytic| over {len(ft)} freqs: {e:.2e} dB")
    print()
    return rows, grs


# ----------------------------------------------------------------------------
# 3. Is mid+high exactly allpass at crossHigh?
# ----------------------------------------------------------------------------

def section3(fs=48000.0, cl=300.0, ch=3000.0):
    print("=" * 100)
    print("3. IS (mid+high) EXACTLY THE 2nd-ORDER ALLPASS AT crossHigh?")
    print("=" * 100)
    f = grid(fs, 4000, extra=(ch, cl, 100.0, 1000.0))
    d = bands(fs, f, cl, ch)
    hs = d['sum_ch']
    ap = d['ap_ch']
    dev = dbmag(hs)
    w, wv, i = worst(dev)
    ph_err = np.angle(hs / ap, deg=True)
    pe = float(np.max(np.abs(ph_err)))
    print(f"  max | 20log10|mid+high| |                : {w:.3e} dB "
          f"at {f[i]:.1f} Hz")
    print(f"  |mid+high| at crossHigh (3000 Hz)       : "
          f"{dbmag(d['sum_ch'][np.argmin(np.abs(f - ch))]):+.6f} dB")
    print(f"  max |Hsum - AP2| (complex)              : "
          f"{np.max(np.abs(hs - ap)):.3e}")
    print(f"  max phase error vs AP2(Q=1/sqrt2)       : {pe:.3e} deg")
    print(f"  max phase error vs AP2(Q=0.5)           : "
          f"{float(np.max(np.abs(np.angle(hs / H(jap(fs, ch, 0.5), fs, f), deg=True)))):.3f} deg"
          "   (Q=0.5 is NOT what JUCE uses - shown for contrast)")
    print()
    print("  Phase of (mid+high) at spot frequencies (deg, unwrapped), i.e. the")
    print("  rotation the low band must be given:")
    ftv = np.array([20.0, 100.0, 300.0, 500.0, 1000.0, 2000.0, 3000.0,
                    6000.0, 15000.0])
    apv = np.unwrap(np.angle(H(jap(fs, ch), fs, ftv)))
    hsv = np.unwrap(np.angle(bands(fs, ftv, cl, ch)['sum_ch']))
    for ft, a, b in zip(ftv, np.degrees(apv), np.degrees(hsv)):
        print(f"    {ft:>7.0f} Hz : AP2 {a:>+10.4f}   (mid+high)/HP4(cl) "
              f"{b:>+10.4f}   err {b-a:>+12.3e}")
    print("  (AP phase runs 0 -> -360 deg; -180.0000 deg exactly at 3000 Hz)")
    print()
    return w, pe


# ----------------------------------------------------------------------------
# 4. Sample-rate independence / cutoff ordering
# ----------------------------------------------------------------------------

def param_limits(fs, nominal_min, nominal_max):
    maximum = min(nominal_max, 0.45 * fs)
    minimum = min(nominal_min, maximum * 0.5)
    return minimum, maximum


def jlimit(lo, hi, v):
    return max(lo, min(hi, v))


def applied_crossovers(fs, cl_param=None, ch_param=None):
    cl_lo, cl_hi = param_limits(fs, 100.0, 800.0)
    ch_lo, ch_hi = param_limits(fs, 1500.0, 6000.0)
    clv = jlimit(cl_lo, cl_hi, 300.0 if cl_param is None else cl_param)
    chv = jlimit(ch_lo, ch_hi, 3000.0 if ch_param is None else ch_param)
    cl = jlimit(1.0, 0.45 * fs, clv)
    ch = jlimit(cl, 0.45 * fs, chv)
    return (cl_lo, cl_hi, ch_lo, ch_hi, cl, ch)


def section4():
    print("=" * 100)
    print("4. SAMPLE-RATE DEPENDENCE AND CUTOFF ORDERING")
    print("=" * 100)
    hdr = (f"{'fs':>7} {'0.45fs':>7} {'crossLow range':>17} {'crossHigh range':>18} "
           f"{'cl':>7} {'ch':>7} {'worst(a)':>9} {'worst(b)':>9}")
    print(hdr)
    print("-" * len(hdr))
    res = {}
    for fs in (8000.0, 16000.0, 48000.0, 96000.0, 3556.0, 2000.0):
        cl_lo, cl_hi, ch_lo, ch_hi, cl, ch = applied_crossovers(fs)
        f = grid(fs, 3000, extra=(cl, ch, 100.0, 1000.0, 0.25 * fs))
        r = responses(fs, f, cl, ch)
        da = np.max(np.abs(dbmag(r['a'])))
        dbv = np.max(np.abs(dbmag(r['b'])))
        res[fs] = (cl, ch, da, dbv)
        print(f"{fs:>7.0f} {0.45*fs:>7.0f} "
              f"{f'[{cl_lo:.0f},{cl_hi:.0f}]':>17} "
              f"{f'[{ch_lo:.0f},{ch_hi:.0f}]':>18} "
              f"{cl:>7.0f} {ch:>7.0f} {da:>9.2e} {dbv:>9.4f}")
    print()
    print("  8 kHz detail (the realistic low rate):")
    fs = 8000.0
    cl, ch = 300.0, 3000.0
    print(f"    crossLow  range [{param_limits(fs,100,800)[0]:.0f},"
          f"{param_limits(fs,100,800)[1]:.0f}]  crossHigh range "
          f"[{param_limits(fs,1500,6000)[0]:.0f},"
          f"{param_limits(fs,1500,6000)[1]:.0f}]  -> no overlap, ch>=cl always")
    for (a, b) in ((300.0, 3000.0), (800.0, 1500.0), (100.0, 3600.0)):
        _, _, _, _, c1, c2 = applied_crossovers(fs, a, b)
        f = grid(fs, 3000)
        w = np.max(np.abs(dbmag(responses(fs, f, c1, c2)['a'])))
        print(f"    cl={c1:>6.0f} ch={c2:>6.0f}  worst|dev|(a) = {w:.2e} dB   "
              f"bands: LP4 / HP4-LP4 / HP4-HP4")
    print()
    print("  Ranges overlap (user can order crossHigh < crossLow) iff")
    print("  min(1500, 0.225*fs) < min(800, 0.45*fs)  ->  fs < 3555.6 Hz.")
    print("  At fs<=3556 the code's jlimit(cl, ...) silently raises ch to cl, so")
    print("  the DSP crossover differs from the parameter value.  Flatness still")
    print("  holds (the identity needs no ordering), but the split degenerates:")
    fs = 2000.0
    cl_lo, cl_hi, ch_lo, ch_hi, cl, ch = applied_crossovers(fs, 800.0, 450.0)
    print(f"    fs=2000, user crossLow=800, crossHigh=450 -> applied cl={cl:.0f},"
          f" ch={ch:.0f}  (450 coerced to {ch:.0f})")
    f = np.array([100.0, 450.0, 800.0, 1200.0])
    d = bands(fs, f, cl, ch)
    print(f"    {'f(Hz)':>7} {'|low|dB':>9} {'|mid|dB':>9} {'|high|dB':>9} "
          f"{'sum dev dB':>11}")
    for i, ft in enumerate(f):
        low = d['lp_cl'][i] * d['ap_ch'][i]
        mid = d['hp_cl'][i] * d['lp_ch'][i]
        high = d['hp_cl'][i] * d['hp_ch'][i]
        print(f"    {ft:>7.0f} {dbmag(np.array([low]))[0]:>9.2f} "
              f"{dbmag(np.array([mid]))[0]:>9.2f} "
              f"{dbmag(np.array([high]))[0]:>9.2f} "
              f"{dbmag(np.array([low + mid + high]))[0]:>11.2e}")
    print("    -> sum is still flat, but 'mid' is a band around ~800 Hz and")
    print("       'high' is HP8, not the requested 3 kHz split.")
    print()
    return res


# ----------------------------------------------------------------------------
# 5. Float32 (C++ float) reality check
# ----------------------------------------------------------------------------

class TptStage:
    """One juce::dsp::LinkwitzRileyFilter<float> instance, float32, verbatim
    from juce_LinkwitzRileyFilter.cpp::processSample (kind 0=LP4, 1=HP4,
    2=AP2; lowpass/highpass run two internal sections, allpass one)."""

    __slots__ = ('g', 'h', 'r2', 'kind', 's1', 's2', 's3', 's4')

    def __init__(self, g, h, r2, kind):
        self.g, self.h, self.r2, self.kind = g, h, r2, kind
        self.s1 = self.s2 = self.s3 = self.s4 = np.float32(0.0)

    def proc(self, x):
        g, h, r2 = self.g, self.h, self.r2
        s1, s2 = self.s1, self.s2
        yh = (x - (r2 + g) * s1 - s2) * h
        yb = g * yh + s1
        s1 = g * yh + yb
        yl = g * yb + s2
        s2 = g * yb + yl
        self.s1, self.s2 = s1, s2
        if self.kind == 2:
            return yl - r2 * yb + yh
        s3, s4 = self.s3, self.s4
        inp = yl if self.kind == 0 else yh
        yh2 = (inp - (r2 + g) * s3 - s4) * h
        yb2 = g * yh2 + s3
        s3 = g * yh2 + yb2
        yl2 = g * yb2 + s4
        s4 = g * yb2 + yl2
        self.s3, self.s4 = s3, s4
        return yl2 if self.kind == 0 else yh2


def section5(fs=48000.0, cl=300.0, ch=3000.0):
    print("=" * 100)
    print("5. FLOAT32 ARITHMETIC (the node runs in float, not double)")
    print("=" * 100)
    f32 = np.float32
    r2 = f32(math.sqrt(2.0))

    def mk(fc):
        g = f32(math.tan(math.pi * fc / fs))
        h = f32(1.0 / (1.0 + float(r2) * float(g) + float(g) * float(g)))
        return g, h

    gcl, hcl = mk(cl)
    gch, hch = mk(ch)
    m = f32(0.16 * fs)
    hdr = f"{'f(Hz)':>8} {'analytic dB':>12} {'float32 dB':>11} {'err dB':>10}"
    print(hdr)
    print("-" * len(hdr))
    worst_e = 0.0
    for ft in (30.0, 50.0, 300.0, 1000.0, 3000.0, 10000.0, 20000.0):
        ncyc = max(4, int(round(ft * float(m) / fs)))
        nmeas = int(round(ncyc * fs / ft))
        fe = ncyc * fs / nmeas
        ntot = int(0.2 * fs) + nmeas
        w = 2.0 * math.pi * fe / fs
        lp_c = TptStage(gcl, hcl, r2, 0)
        hp_c = TptStage(gcl, hcl, r2, 1)
        lp_h = TptStage(gch, hch, r2, 0)
        hp_h = TptStage(gch, hch, r2, 1)
        ap_h = TptStage(gch, hch, r2, 2)
        y = np.empty(ntot, dtype=f32)
        amp32 = f32(1.0)
        for n in range(ntot):
            x = f32(amp32 * f32(math.sin(w * n)))
            lo = ap_h.proc(lp_c.proc(x))
            rest = hp_c.proc(x)
            mi = lp_h.proc(rest)
            hi = hp_h.proc(rest)
            y[n] = lo + mi + hi
        amp = measure(y.astype(np.float64), fe, fs, int(0.2 * fs))
        g = DB * math.log(amp)
        ana = dbmag(responses(fs, np.array([fe]), cl, ch)['a'])[0]
        e = abs(g - ana)
        worst_e = max(worst_e, e)
        print(f"{ft:>8.0f} {ana:>12.6f} {g:>11.6f} {e:>10.2e}")
    print()
    print(f"  worst float32 deviation from ideal: {worst_e:.2e} dB "
          f"(~{-20*math.log10(max(worst_e,1e-30)):.0f} dB relative)")
    print("  This is the TPT topology JUCE actually runs; a float32 Direct-Form-I")
    print("  biquad would overstate the LF error by ~30 dB (worse conditioning).")
    print("  => the flatness limit in the real build is float round-off, not math.")
    print()
    return worst_e


# ----------------------------------------------------------------------------
# 6. Summary
# ----------------------------------------------------------------------------

def summary(s1, s2q, s2t, s3, s4, s5):
    print("=" * 100)
    print("SUMMARY: WORST-CASE DEVIATION FROM FLAT (amount=0, makeup=0 dB)")
    print("=" * 100)
    hdr = (f"{'scenario':<52} {'fs':>7} {'worst|dev| dB':>13} {'at f(Hz)':>10}")
    print(hdr)
    print("-" * len(hdr))
    for fs in (48000.0, 96000.0, 16000.0, 8000.0):
        for key in ('a', 'b', 'c'):
            w, fw, _, _ = s1[(fs, key)]
            print(f"{CONFIG_NAMES[key]:<52} {fs:>7.0f} {w:>13.3f} {fw:>10.1f}")
        print()
    print(f"{'float32 implementation of (a)':<52} {48000:>7.0f} "
          f"{s5:>13.1e} {'-':>10}")
    print()
    print("COMPRESSION (defaults 40/40/30, fs=48 kHz):")
    for lev, w, fw, gr, _ in s2q:
        print(f"  quasi-static {lev:>5.1f} dBFS: worst|dev| {w:>6.3f} dB at "
              f"{fw:>7.1f} Hz, band GR {gr[0]:.1f}/{gr[1]:.1f}/{gr[2]:.1f} dB")
    for lev, w, fw, gr in s2t:
        print(f"  time-domain   {lev:>5.1f} dBFS: worst|dev| {w:>6.3f} dB at "
              f"{fw:>7.1f} Hz, band GR {gr[0]:.1f}/{gr[1]:.1f}/{gr[2]:.1f} dB")
    print()
    print(f"MID+HIGH at crossHigh: max |mag| dev {s3[0]:.2e} dB, "
          f"max phase error vs AP2 {s3[1]:.2e} deg  (exact allpass)")
    print()


def main():
    print()
    print("=" * 100)
    print("LR4 3-BAND RECOMBINATION AUDIT -- MultiBandCompNode.cpp")
    print("JUCE 9.0.4 TPT LinkwitzRileyFilter + legacy IIR::Coefficients model")
    print("=" * 100)
    print()
    ok = section0()
    if not ok:
        print("!! MODEL VERIFICATION FAILED -- numbers below are not trustworthy")
        print()
    s1 = section1()
    s2q, s2t = section2()
    s3 = section3()
    s4 = section4()
    s5 = section5()
    summary(s1, s2q, s2t, s3, s4, s5)
    print("Model verification:", "PASS" if ok else "FAIL")
    print()


if __name__ == '__main__':
    main()
