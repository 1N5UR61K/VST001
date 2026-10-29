#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      (0xff15171c);
    const juce::Colour kPanel   (0xff1f232b);
    const juce::Colour kAccent  (0xffe8a33d);
    const juce::Colour kText    (0xffd8dce3);
}

void BeadsEditor::addKnob (Knob& k, const juce::String& id, const juce::String& text, bool small)
{
    k.slider.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
    k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xff3a404c));
    k.slider.setColour (juce::Slider::thumbColourId, kText);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (
                                                   proc.apvts.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (k.slider);
    k.label.setText (text, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setColour (juce::Label::textColourId, small ? kText.withAlpha (0.7f) : kText);
    k.label.setFont (juce::FontOptions (small ? 11.0f : 14.0f, small ? juce::Font::plain : juce::Font::bold));
    addAndMakeVisible (k.label);
    k.attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
}

BeadsEditor::BeadsEditor (BeadsAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    addKnob (time,  "time",  "TIME",  false);  addKnob (timeAmt,  "time_amt",  "random / cv amount", true);
    addKnob (size,  "size",  "SIZE",  false);  addKnob (sizeAmt,  "size_amt",  "random / cv amount", true);
    addKnob (shape, "shape", "SHAPE", false);  addKnob (shapeAmt, "shape_amt", "random / cv amount", true);
    addKnob (pitch, "pitch", "PITCH", false);  addKnob (pitchAmt, "pitch_amt", "random / cv amount", true);
    addKnob (density,  "density",  "DENSITY",  false);
    addKnob (feedback, "feedback", "FEEDBACK", false);
    addKnob (dryWet,   "dry_wet",  "DRY / WET", false);
    addKnob (reverb,   "reverb",   "REVERB",   false);
    addKnob (inGain,  "in_gain",  "input gain dB",  true);
    addKnob (outGain, "out_gain", "output gain dB", true);

    for (auto* b : { &freeze, &seedLock, &midiSeed, &mono })
    {
        b->setColour (juce::ToggleButton::textColourId, kText);
        b->setColour (juce::ToggleButton::tickColourId, kAccent);
        addAndMakeVisible (*b);
    }
    aFreeze   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "freeze", freeze);
    aSeedLock = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "seed_lock", seedLock);
    aMidi     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "midi_seed", midiSeed);
    aMono     = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "mono", mono);

    // Momentary seed button: parameter is "on" only while the mouse is held.
    seedHold.onStateChange = [this]
    {
        if (auto* prm = proc.apvts.getParameter ("seed"))
        {
            const bool down = seedHold.isDown();
            if (down != (prm->getValue() > 0.5f))
                prm->setValueNotifyingHost (down ? 1.0f : 0.0f);
        }
    };
    addAndMakeVisible (seedHold);

    quality.addItemList (proc.apvts.getParameter ("quality")->getAllValueStrings(), 1);
    addAndMakeVisible (quality);
    aQuality = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "quality", quality);
    qualityLabel.setText ("QUALITY", juce::dontSendNotification);
    qualityLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (qualityLabel);

    setSize (720, 600);
    startTimerHz (15);
}

BeadsEditor::~BeadsEditor() { stopTimer(); }

void BeadsEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("BEADS TEXTURES", 20, 8, 400, 30, juce::Justification::left);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (kText.withAlpha (0.6f));
    g.drawText ("prototype port of the open-source Mutable Instruments Beads DSP (MIT)",
                20, 34, 600, 18, juce::Justification::left);

    g.setColour (kPanel);
    g.fillRoundedRectangle (10.0f, 60.0f, 700.0f, 270.0f, 8.0f);
    g.fillRoundedRectangle (10.0f, 340.0f, 700.0f, 130.0f, 8.0f);

    // recording-space indicator (how much room is left while frozen)
    g.setColour (kText.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("freeze buffer room", reserveBar.getX(), reserveBar.getY() - 14, reserveBar.getWidth(), 12,
                juce::Justification::left);
    g.setColour (juce::Colour (0xff3a404c));
    g.fillRoundedRectangle (reserveBar.toFloat(), 3.0f);
    g.setColour (kAccent);
    g.fillRoundedRectangle (reserveBar.toFloat().withWidth (reserveBar.getWidth() * proc.overwriteReserve.load()), 3.0f);
}

void BeadsEditor::resized()
{
    const int colW = 700 / 4;
    Knob* top[4]    = { &time, &size, &shape, &pitch };
    Knob* topAmt[4] = { &timeAmt, &sizeAmt, &shapeAmt, &pitchAmt };
    Knob* mid[4]    = { &density, &feedback, &dryWet, &reverb };

    for (int i = 0; i < 4; ++i)
    {
        const int x = 10 + i * colW;
        top[i]->label.setBounds (x, 66, colW, 18);
        top[i]->slider.setBounds (x + (colW - 120) / 2, 84, 120, 120);
        topAmt[i]->slider.setBounds (x + (colW - 64) / 2, 208, 64, 64);
        topAmt[i]->label.setBounds (x, 274, colW, 16);
    }
    for (int i = 0; i < 4; ++i)
    {
        const int x = 10 + i * colW;
        mid[i]->label.setBounds (x, 344, colW, 18);
        mid[i]->slider.setBounds (x + (colW - 90) / 2, 362, 90, 90);
    }

    // Row of buttons
    int x = 20, y = 482, h = 28;
    freeze.setBounds (x, y, 90, h);          x += 100;
    seedHold.setBounds (x, y, 110, h);       x += 120;
    seedLock.setBounds (x, y, 110, h);       x += 120;
    midiSeed.setBounds (x, y, 160, h);       x += 170;
    mono.setBounds (x, y, 80, h);

    qualityLabel.setBounds (20, 522, 70, 24);
    quality.setBounds (90, 522, 210, 26);

    inGain.slider.setBounds (330, 512, 48, 48);   inGain.label.setBounds (310, 560, 90, 16);
    outGain.slider.setBounds (420, 512, 48, 48);  outGain.label.setBounds (400, 560, 90, 16);

    reserveBar = { 520, 540, 180, 8 };
}
