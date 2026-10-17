#include "MultiBandCompNode.h"

MultiBandCompNode::MultiBandCompNode() : DspNode("Multi-Band") {}

void MultiBandCompNode::prepare(double sr, int bs)
{
    DspNode::prepare(sr, bs);
    limitFrequency(crossLowHz, 100.0f, 800.0f);
    limitFrequency(crossHighHz, 1500.0f, 6000.0f);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 1 };

    lowBandFilter.prepare(spec);
    restBandFilter.prepare(spec);
    lowBandFilter.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    restBandFilter.setType(juce::dsp::LinkwitzRileyFilterType::highpass);

    midLowpass.prepare(spec);
    highHighpass.prepare(spec);
    lowPhaseCompensation.prepare(spec);
    midLowpass.setType(juce::dsp::LinkwitzRileyFilterType::lowpass);
    highHighpass.setType(juce::dsp::LinkwitzRileyFilterType::highpass);
    lowPhaseCompensation.setType(juce::dsp::LinkwitzRileyFilterType::allpass);

    lowBuf.setSize(1, blockSize, false, true, true);
    midBuf.setSize(1, blockSize, false, true, true);
    highBuf.setSize(1, blockSize, false, true, true);
    tmpBuf.setSize(1, blockSize, false, true, true);

    lastCrossLow = -1.0;
    lastCrossHigh = -1.0;
    makeup.prepare(sr);
    makeup.snapTo(juce::Decibels::decibelsToGain(makeupDb.get()));
    autoMakeupDb = 0.0f;
    bandGrDb.fill(0.0f);
    updateSplitters();
}

void MultiBandCompNode::reset()
{
    lowBandFilter.reset();
    restBandFilter.reset();
    midLowpass.reset();
    highHighpass.reset();
    lowPhaseCompensation.reset();
    lowEnv = midEnv = highEnv = 0.0f;
    lastCrossLow = lastCrossHigh = -1.0;
    makeup.snapTo(juce::Decibels::decibelsToGain(makeupDb.get()));
    autoMakeupDb = 0.0f;
    bandGrDb.fill(0.0f);
    for (auto& m : bandMeters) m.store(0.0f);
}

void MultiBandCompNode::updateSplitters()
{
    const float maxFrequency = static_cast<float>(sampleRate * 0.45);
    float cl = juce::jlimit(1.0f, maxFrequency, crossLowHz.get());
    if (std::abs(cl - lastCrossLow) > 0.5)
    {
        lastCrossLow = cl;
        lowBandFilter.setCutoffFrequency(cl);
        restBandFilter.setCutoffFrequency(cl);
    }

    float ch = juce::jlimit(cl, maxFrequency, crossHighHz.get());
    if (std::abs(ch - lastCrossHigh) > 0.5)
    {
        lastCrossHigh = ch;
        midLowpass.setCutoffFrequency(ch);
        highHighpass.setCutoffFrequency(ch);
        lowPhaseCompensation.setCutoffFrequency(ch);
    }
}

void MultiBandCompNode::updateBandMeters(int meterBase, float peakIn, float peakOut, float gr)
{
    auto smooth = [](float oldV, float newV) {
        return newV > oldV ? newV : oldV * 0.85f + newV * 0.15f;
    };
    int base = juce::jlimit(0, 6, meterBase);
    bandMeters[(size_t)(base + 0)].store(
        smooth(bandMeters[(size_t)(base + 0)].load(), juce::jlimit(0.0f, 1.0f, peakIn)));
    bandMeters[(size_t)(base + 1)].store(
        smooth(bandMeters[(size_t)(base + 1)].load(), juce::jlimit(0.0f, 1.0f, peakOut)));
    bandMeters[(size_t)(base + 2)].store(
        smooth(bandMeters[(size_t)(base + 2)].load(), juce::jlimit(0.0f, 1.0f, gr)));
}

void MultiBandCompNode::processBand(float* buf, int n, float amount, float threshold,
                                    float& env, int meterBase, float& grDb)
{
    grDb = 0.0f;
    const int bandIndex = juce::jlimit(0, 2, meterBase / 3);

    if (amount <= 0.01f)
    {
        float peak = 0.0f;
        double power = 0.0;
        for (int i = 0; i < n; ++i)
        {
            peak = juce::jmax(peak, std::abs(buf[i]));
            power += (double) buf[i] * buf[i];
        }
        bandInPower[(size_t) bandIndex] = power;
        bandOutPower[(size_t) bandIndex] = power;
        updateBandMeters(meterBase, peak, peak, 0.0f);
        return;
    }

    const float ratio = 1.0f + amount * 0.06f;
    const float attMs = juce::jlimit(0.5f, 100.0f, attackMs.get());
    const float relMs = juce::jlimit(20.0f, 1000.0f, releaseMs.get());
    const float attCoef = std::exp(-1.0f / (attMs * 0.001f * (float) sampleRate));
    const float relCoef = std::exp(-1.0f / (relMs * 0.001f * (float) sampleRate));

    float peakIn = 0.0f, peakOut = 0.0f, maxGr = 0.0f;
    double sumIn = 0.0, sumOut = 0.0;

    for (int i = 0; i < n; ++i)
    {
        const float x = buf[i];
        peakIn = juce::jmax(peakIn, std::abs(x));

        const float absX = std::abs(x);
        if (absX > env) env = attCoef * env + (1.0f - attCoef) * absX;
        else            env = relCoef * env + (1.0f - relCoef) * absX;
        const float envDb = juce::Decibels::gainToDecibels(env, -100.0f);
        const float over = envDb - threshold;
        float gainDb = 0.0f;
        if (over > 0.0f) gainDb = -over * (1.0f - 1.0f / ratio);
        const float gain = juce::Decibels::decibelsToGain(gainDb);
        const float y = x * gain;
        buf[i] = y;

        sumIn += (double) x * x;
        sumOut += (double) y * y;
        peakOut = juce::jmax(peakOut, std::abs(y));
        if (gain < 1.0f) maxGr = juce::jmax(maxGr, 1.0f - gain);
    }

    if (sumIn > 1e-20)
        grDb = juce::jlimit(-36.0f, 0.0f, (float) (10.0 * std::log10(juce::jmax(1e-20, sumOut) / sumIn)));

    bandInPower[(size_t) bandIndex] = sumIn;
    bandOutPower[(size_t) bandIndex] = sumOut;
    updateBandMeters(meterBase, peakIn, peakOut, maxGr);
}

void MultiBandCompNode::process(float* buffer, int numSamples)
{
    updateSplitters();

    // Reuse the prepared buffers for larger host blocks, without reallocating.
    const int capacity = lowBuf.getNumSamples();
    if (capacity <= 0) return;
    if (numSamples > capacity)
    {
        for (int pos = 0; pos < numSamples; pos += capacity)
            process(buffer + pos, juce::jmin(capacity, numSamples - pos));
        return;
    }

    juce::FloatVectorOperations::copy(lowBuf.getWritePointer(0), buffer, numSamples);
    juce::FloatVectorOperations::copy(tmpBuf.getWritePointer(0), buffer, numSamples);

    // 低频：lowBandFilter 固定 lowpass
    {
        juce::dsp::AudioBlock<float> lowBlock(lowBuf.getArrayOfWritePointers(), 1, (size_t) numSamples);
        lowBandFilter.process(juce::dsp::ProcessContextReplacing<float>(lowBlock));
        lowPhaseCompensation.process(juce::dsp::ProcessContextReplacing<float>(lowBlock));
    }
    // 剩余（中+高）：restBandFilter 固定 highpass
    {
        juce::dsp::AudioBlock<float> restBlock(tmpBuf.getArrayOfWritePointers(), 1, (size_t) numSamples);
        restBandFilter.process(juce::dsp::ProcessContextReplacing<float>(restBlock));
    }

    // 拆中/高
    {
        juce::dsp::AudioBlock<float> midBlock(midBuf.getArrayOfWritePointers(), 1, (size_t) numSamples);
        juce::FloatVectorOperations::copy(midBuf.getWritePointer(0), tmpBuf.getReadPointer(0), numSamples);
        midLowpass.process(juce::dsp::ProcessContextReplacing<float>(midBlock));
    }
    {
        juce::dsp::AudioBlock<float> highBlock(highBuf.getArrayOfWritePointers(), 1, (size_t) numSamples);
        juce::FloatVectorOperations::copy(highBuf.getWritePointer(0), tmpBuf.getReadPointer(0), numSamples);
        highHighpass.process(juce::dsp::ProcessContextReplacing<float>(highBlock));
    }

    // One explicit threshold for all three bands (the old code derived it from
    // the Amount knobs, which turned them into volume controls).
    const float threshold = juce::jlimit(-60.0f, 0.0f, threshDb.get());
    processBand(lowBuf.getWritePointer(0),  numSamples, lowAmount.get(),  threshold, lowEnv,  0, bandGrDb[0]);
    processBand(midBuf.getWritePointer(0),  numSamples, midAmount.get(),  threshold, midEnv,  3, bandGrDb[1]);
    processBand(highBuf.getWritePointer(0), numSamples, highAmount.get(), threshold, highEnv, 6, bandGrDb[2]);

    // Automatic make-up: give back what the bands collectively took away, so
    // raising an Amount knob no longer acts as a volume control. Weighting by the
    // actual band energy matters: the band carrying the signal is the one whose
    // loss has to be compensated, not the average of three bands.
    const double totalIn = bandInPower[0] + bandInPower[1] + bandInPower[2];
    const double totalOut = bandOutPower[0] + bandOutPower[1] + bandOutPower[2];
    float achievedGrDb = 0.0f;
    if (totalIn > 1e-20)
        achievedGrDb = (float) (10.0 * std::log10(juce::jmax(1e-20, totalOut) / totalIn));
    achievedGrDb = juce::jlimit(-24.0f, 0.0f, achievedGrDb);
    const float targetAuto = juce::jlimit(0.0f, 18.0f, -achievedGrDb);
    autoMakeupDb += (targetAuto - autoMakeupDb) * 0.15f;
    makeup.setTarget(juce::Decibels::decibelsToGain(autoMakeupDb + makeupDb.get()));

    auto* lo = lowBuf.getReadPointer(0);
    auto* mi = midBuf.getReadPointer(0);
    auto* hi = highBuf.getReadPointer(0);
    for (int i = 0; i < numSamples; ++i)
        buffer[i] = (lo[i] + mi[i] + hi[i]) * makeup.next();
}

std::vector<DspNode::ParamDescriptor> MultiBandCompNode::getParameters()
{
    return {
        { "lowAmount",  "param.lowAmount",  "", 0.0f, 100.0f, 40.0f, 0, true,  &lowAmount },
        { "midAmount",  "param.midAmount",  "", 0.0f, 100.0f, 40.0f, 0, true,  &midAmount },
        { "highAmount", "param.highAmount", "", 0.0f, 100.0f, 30.0f, 0, true,  &highAmount },
        frequencyParameter("crossLow", "param.crossLow", crossLowHz, false),
        frequencyParameter("crossHigh", "param.crossHigh", crossHighHz, false),
        { "thresh",     "param.mbThresh",   "dB", -45.0f, -6.0f, -22.0f, 1, false, &threshDb },
        { "attack",     "param.attack",     "ms", 0.5f, 100.0f, 15.0f, 1, false, &attackMs },
        { "release",    "param.release",    "ms", 20.0f, 1000.0f, 150.0f, 0, false, &releaseMs },
        { "makeup",     "param.makeup",     "dB", 0.0f, 12.0f, 0.0f, 1, false, &makeupDb },
    };
}
