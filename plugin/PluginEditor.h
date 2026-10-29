#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class BeadsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BeadsEditor (BeadsAudioProcessor&);
    ~BeadsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { repaint (reserveBar); }

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };

    void addKnob (Knob& k, const juce::String& paramId, const juce::String& text, bool small);

    BeadsAudioProcessor& proc;

    Knob time, size, shape, pitch;
    Knob timeAmt, sizeAmt, shapeAmt, pitchAmt;
    Knob density, feedback, dryWet, reverb;
    Knob inGain, outGain;

    juce::ToggleButton freeze { "FREEZE" }, seedLock { "SEED LOCK" }, midiSeed { "MIDI = SEED GATE" },
                       mono { "MONO" };
    juce::TextButton seedHold { "SEED (hold)" };
    juce::ComboBox quality;
    juce::Label qualityLabel;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> aFreeze, aSeedLock, aMidi, aMono;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aQuality;

    juce::Rectangle<int> reserveBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeadsEditor)
};
