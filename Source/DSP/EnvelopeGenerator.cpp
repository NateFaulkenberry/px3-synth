#include "EnvelopeGenerator.h"

void EnvelopeGenerator::setSettings(const EnvelopeSettings& settings)
{
    envelopeSettings = settings;

    adsrParameters.attack = envelopeSettings.attackSeconds;
    adsrParameters.decay = envelopeSettings.decaySeconds;
    adsrParameters.sustain = envelopeSettings.sustainLevel;
    adsrParameters.release = envelopeSettings.releaseSeconds;

    adsr.setParameters(adsrParameters);
}

void EnvelopeGenerator::noteOn()
{
    adsr.noteOn();
}

void EnvelopeGenerator::noteOff()
{
    adsr.noteOff();
}

void EnvelopeGenerator::reset()
{
    adsr.reset();
}

bool EnvelopeGenerator::isActive() const
{
    return adsr.isActive();
}

float EnvelopeGenerator::getNextSample()
{
    return adsr.getNextSample();
}
