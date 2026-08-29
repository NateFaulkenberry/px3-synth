#pragma once

#include "BusAnalyser.h"
#include "BusInsertTypes.h"
#include "FetCompressor.h"
#include "ParametricEQ.h"

namespace px3
{

// One bus's inserts: EQ then compressor, in that order.
//
// EQ first because the compressor should react to the tone you have decided on
// rather than to the one you are about to remove - a low shelf cut before the
// detector is the difference between a bass note pumping the whole bus and not.
//
// This knows nothing about WHICH bus it is on. Adding the same inserts to a
// source channel or to the master later means constructing another instance
// and giving it parameters; the processors do not change.
class BusInsertChain
{
public:
    void prepare(double sampleRate)
    {
        eq.prepare(sampleRate);
        compressor.prepare(sampleRate);
    }

    void reset()
    {
        eq.reset();
        compressor.reset();
        analyser.reset();
    }

    void setSettings(const EqSettings& eqSettings, const CompressorSettings& compSettings)
    {
        eq.setSettings(eqSettings);
        compressor.setSettings(compSettings);
    }

    void processSample(float& left, float& right)
    {
        // Tapped BEFORE the EQ, so the display shows what is arriving on the
        // bus rather than the result of the curve drawn over it. Post-EQ would
        // draw the same shaping twice - once as the curve, once as the trace -
        // which is exactly when an analyser stops helping you decide a cut.
        analyser.push(left, right);

        eq.processSample(left, right);
        compressor.processSample(left, right);
    }

    const ParametricEQ& getEq() const { return eq; }
    const FetCompressor& getCompressor() const { return compressor; }
    BusAnalyser& getAnalyser() { return analyser; }
    const BusAnalyser& getAnalyser() const { return analyser; }

private:
    ParametricEQ eq;
    FetCompressor compressor;
    BusAnalyser analyser;
};

} // namespace px3
