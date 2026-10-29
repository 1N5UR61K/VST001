#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "beads_engine.h"

class BeadsAudioProcessor : public juce::AudioProcessor
{
public:
    BeadsAudioProcessor();
    ~BeadsAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Beads Textures"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> overwriteReserve { 1.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    beadsvst::PanelState readPanel() const;
    void runEngine (const beadsvst::PanelState&, const float* inL, const float* inR,
                    float* outL, float* outR, int n);

    beadsvst::BeadsEngine engine;
    std::vector<float> inL, inR, outL, outR;
    int lastLatency = -1;
    int midiNotesHeld = 0;

    std::atomic<float>* pTime, *pSize, *pShape, *pPitch,
        *pTimeAmt, *pSizeAmt, *pShapeAmt, *pPitchAmt,
        *pDensity, *pFeedback, *pDryWet, *pReverb,
        *pQuality, *pMono, *pFreeze, *pSeed, *pSeedLock, *pMidiSeed,
        *pInGain, *pOutGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeadsAudioProcessor)
};
