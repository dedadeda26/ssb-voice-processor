"""Static transfer curves of the shipped dynamics nodes, reproduced from source.

MultiBandCompNode::processBand  (Source/Nodes/MultiBandCompNode.cpp:92-119)
    threshold = -10 - amount*0.30        (dBFS, hard-coded base!)
    ratio     = 1 + amount*0.06
    gainDb    = -over * (1 - 1/ratio)    for over > 0
    output    = band * gain * makeup     (makeup default 0 dB)

CompressorNode::process (Source/Nodes/CompressorNode.cpp:30-44)
    threshold -18 dB, ratio 3, knee 6 dB, makeup 0 dB by default
    over < knee:  reduced = over*(1-1/ratio)*smoothstep(over/knee)
"""
import numpy as np


def mbc_gr(level_db, amount):
    threshold = -10.0 - amount * 0.30
    ratio = 1.0 + amount * 0.06
    over = level_db - threshold
    return 0.0 if over <= 0 else over * (1.0 - 1.0 / ratio)


def comp_gr(level_db, thresh=-18.0, ratio=3.0, knee=6.0):
    over = level_db - thresh
    if over <= 0:
        return 0.0
    k = 1.0 - 1.0 / ratio
    if over < knee:
        t = over / knee
        return over * k * t * t * (3.0 - 2.0 * t)
    return over * k


print("Gain reduction (dB, positive = attenuated) at 48 kHz, defaults as shipped")
print()
levels = [-40, -34, -30, -26, -22, -18, -14, -10, -6, -3]
print(f"{'band level':>10} | {'MBC amt 40':>10} {'MBC amt 70':>10} {'MBC amt 100':>11} | "
      f"{'Comp default':>12} {'Comp ratio 8':>12}")
for l in levels:
    print(f"{l:>7} dBFS | {mbc_gr(l,40):>10.1f} {mbc_gr(l,70):>10.1f} {mbc_gr(l,100):>11.1f} | "
          f"{comp_gr(l):>12.1f} {comp_gr(l, ratio=8):>12.1f}")

print()
print("Thresholds implied by the three 'Amount' knobs (no user control exists):")
for a in (0, 20, 40, 60, 80, 100):
    print(f"  amount {a:>3}: threshold {-10 - a*0.30:>6.1f} dBFS, ratio {1 + a*0.06:>4.2f}:1")

print()
print("Default chain loudness budget for a -20 dBFS speech peak (MBC all bands + comp):")
sp = -20.0
mbc = mbc_gr(sp, 40)
after = sp - mbc
print(f"  in {sp:.1f} -> MBC (3 bands, amt 40) {after:.1f} dBFS "
      f"(-{mbc:.1f} dB, makeup 0) -> Comp default {after - comp_gr(after):.1f} dBFS "
      f"(-{comp_gr(after):.1f} dB, makeup 0)")
print(f"  total static loss from the two dynamics nodes at this level: {mbc + comp_gr(after):.1f} dB")
