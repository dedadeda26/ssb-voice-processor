"""Original-version audit checks, read from the unchanged RAR archive.

This reproduces the original findings, not the repaired project's test suite.
For the repaired project, enable SSB_BUILD_TESTS and run CTest.
Run with: python3 audit/run_checks.py
"""
from pathlib import Path
import cmath
import math
import subprocess
import tempfile

ARCHIVE = Path(__file__).resolve().parents[1] / "SSBVoiceProcessor.rar"


def method(file, signature):
    text = subprocess.check_output(
        ["tar", "-xOf", str(ARCHIVE), f"SSBVoiceProcessor/Source/Nodes/{file}"]
    ).decode("utf-8-sig")
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


shim = r'''
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>
namespace juce {
template<class T> T jmax(T a,T b) { return std::max(a,b); }
template<class T> T jlimit(T a,T b,T v) { return std::max(a,std::min(b,v)); }
struct Decibels {
 static float decibelsToGain(float d) { return d > -100.f ? std::pow(10.f,d/20.f) : 0.f; }
 static float gainToDecibels(float g,float floor) { return g>0 ? std::max(floor,20.f*std::log10(g)) : floor; }
}; }
struct Param { float v; float get() const { return v; } };
class LimiterNode {
public:
 std::atomic<bool> bypassed{false}; double sampleRate=48000.;
 Param thresholdDb{-12.f}, releaseMs{5.f}; float gain=1.f;
 std::atomic<float> reduction{0.f};
 void reset(); void process(float*,int);
};
class NoiseGateNode {
public:
 std::atomic<bool> bypassed{false}; double sampleRate=48000.;
 Param thresholdDb{-50.f}, amountPct{50.f}, attackMs{5.f}, releaseMs{150.f};
 float envelope=0.f, gain=1.f; std::atomic<bool> gateOpen{true};
 void reset(); void process(float*,int);
};
'''
cpp = shim
for file, cls in [("LimiterNode.cpp", "LimiterNode"), ("NoiseGateNode.cpp", "NoiseGateNode")]:
    for name in ["reset", "process"]:
        cpp += "\n" + method(file, f"void {cls}::{name}(")
cpp += r'''
int main() {
 LimiterNode limiter;
 float x[]={1.f,1.f}; limiter.process(x,2);
 float ceiling=juce::Decibels::decibelsToGain(-12.f);
 std::printf("Limiter ceiling=%.9f, sample1=%.9f, sample2=%.9f, excess=%.6f dB\n",
   ceiling,x[0],x[1],20.f*std::log10(x[1]/ceiling));
 if (!(x[1]>ceiling+1e-5f)) return 1;
 NoiseGateNode gate;
 std::vector<float> silence(48000,0.f); gate.process(silence.data(),48000);
 std::vector<float> speech(48000,.2f); gate.process(speech.data(),48000);
 for (int ms : {5,10,50,150})
   std::printf("Gate gain at %d ms after onset: %.6f\n",ms,speech[ms*48-1]/.2f);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="ssb-audit-") as directory:
    source = Path(directory) / "check.cpp"
    binary = Path(directory) / "check"
    source.write_text(cpp)
    subprocess.run(["clang++", "-std=c++17", "-O2", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


def response(frequency, cutoff, high=False, q=.707):
    # JUCE ArrayCoefficients makeLowPass/makeHighPass equations.
    sr = 48000
    n = math.tan(math.pi * cutoff / sr)
    if not high:
        n = 1 / n
    c = 1 / (1 + n / q + n * n)
    b = (c, -2 * c, c) if high else (c, 2 * c, c)
    a = (1, 2 * c * (n*n-1) if high else 2*c*(1-n*n), c*(1-n/q+n*n))
    z = cmath.exp(-2j * math.pi * frequency / sr)
    return sum(v*z**i for i, v in enumerate(b)) / sum(v*z**i for i, v in enumerate(a))


print("Multi-band sum with amounts=0, makeup=0, crossovers=300/3000 Hz:")
for frequency in [1000, 1500, 2000, 2500, 2800, 3000]:
    low = response(frequency, 300, q=1/math.sqrt(2)) ** 2
    rest = response(frequency, 300, high=True, q=1/math.sqrt(2)) ** 2
    summed = low + rest * (response(frequency, 3000) + response(frequency, 3000, high=True))
    db = 20 * math.log10(abs(summed))
    print(f"  {frequency} Hz: {db:.3f} dB")
    if frequency == 3000:
        assert db < -60

fft_size = 1024
print("FFT ingestion per callback pattern:")
for block in [256, 480, 512, 768, 2048]:
    hop = accepted = completed = 0
    for _ in range(100):
        accepted += min(block, fft_size-hop)
        hop += block
        if hop >= fft_size:
            completed += 1
            hop = 0
    print(f"  block={block}: accepted={accepted}/{100*block}, discarded={100*block-accepted}, frames={completed}")

left_widths = [210,10,120,6,140,6,72,4,72,4,72,16,190,16,90,6,76,6,76]
required = sum(left_widths) + 80 + 6 + 80 + 28
print(f"Toolbar minimum requirement (76px mode buttons): {required}px; default=1360px, allowed minimum=1000px")
print("Macro endpoints: compressor 100% => threshold=0 dB, ratio=20, makeup=24 dB;")
print("  de-esser 100% => threshold=0 dB, amount=100% (no reduction for detector envelope below 0 dBFS).")
