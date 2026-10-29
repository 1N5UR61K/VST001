#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;

juce::AudioProcessorValueTreeState::ParameterLayout BeadsAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto knob = [&] (const char* id, const char* name, float def)
    {
        p.push_back (std::make_unique<APF> (juce::ParameterID { id, 1 }, name,
                                            juce::NormalisableRange<float> (0.0f, 1.0f, 0.0001f), def));
    };
    knob ("time",   "Time",   0.5f);
    knob ("size",   "Size",   0.5f);
    knob ("shape",  "Shape",  0.5f);
    knob ("pitch",  "Pitch",  0.5f);
    knob ("time_amt",  "Time Amount",  0.5f);
    knob ("size_amt",  "Size Amount",  0.5f);
    knob ("shape_amt", "Shape Amount", 0.5f);
    knob ("pitch_amt", "Pitch Amount", 0.5f);
    knob ("density",  "Density",  0.5f);
    knob ("feedback", "Feedback", 0.0f);
    knob ("dry_wet",  "Dry/Wet",  0.5f);
    knob ("reverb",   "Reverb",   0.0f);

    p.push_back (std::make_unique<APC> (juce::ParameterID { "quality", 1 }, "Quality",
        juce::StringArray { "48k 16-bit", "32k 12-bit (Clouds)", "24k 12-bit hi-fi dry", "24k tape (cassette)" }, 0));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "mono", 1 }, "Mono (2x record time)", false));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "freeze", 1 }, "Freeze", false));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "seed", 1 }, "Seed (hold)", false));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "seed_lock", 1 }, "Seed Lock", true));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "midi_seed", 1 }, "MIDI Notes = Seed Gate", false));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "in_gain", 1 }, "Input Gain",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "out_gain", 1 }, "Output Gain",
        juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f));
    return { p.begin(), p.end() };
}

BeadsAudioProcessor::BeadsAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BEADS", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    pTime = get ("time"); pSize = get ("size"); pShape = get ("shape"); pPitch = get ("pitch");
    pTimeAmt = get ("time_amt"); pSizeAmt = get ("size_amt");
    pShapeAmt = get ("shape_amt"); pPitchAmt = get ("pitch_amt");
    pDensity = get ("density"); pFeedback = get ("feedback");
    pDryWet = get ("dry_wet"); pReverb = get ("reverb");
    pQuality = get ("quality"); pMono = get ("mono"); pFreeze = get ("freeze");
    pSeed = get ("seed"); pSeedLock = get ("seed_lock"); pMidiSeed = get ("midi_seed");
    pInGain = get ("in_gain"); pOutGain = get ("out_gain");
}

bool BeadsAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    const auto in  = l.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void BeadsAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.Prepare (sampleRate);
    const auto n = (size_t) juce::jmax (1, samplesPerBlock);
    inL.assign (n, 0.0f); inR.assign (n, 0.0f);
    outL.assign (n, 0.0f); outR.assign (n, 0.0f);
    lastLatency = -1;
    midiNotesHeld = 0;
}

beadsvst::PanelState BeadsAudioProcessor::readPanel() const
{
    beadsvst::PanelState s;
    s.time = pTime->load(); s.size = pSize->load();
    s.shape = pShape->load(); s.pitch = pPitch->load();
    s.time_amount = pTimeAmt->load(); s.size_amount = pSizeAmt->load();
    s.shape_amount = pShapeAmt->load(); s.pitch_amount = pPitchAmt->load();
    s.density = pDensity->load(); s.feedback = pFeedback->load();
    s.dry_wet = pDryWet->load(); s.reverb = pReverb->load();
    s.quality = (int) pQuality->load();
    s.mono = pMono->load() > 0.5f;
    s.freeze = pFreeze->load() > 0.5f;
    s.seed_button = pSeed->load() > 0.5f;
    s.seed_lock = pSeedLock->load() > 0.5f;
    s.input_gain = juce::Decibels::decibelsToGain (pInGain->load());
    s.output_gain = juce::Decibels::decibelsToGain (pOutGain->load());
    return s;
}

void BeadsAudioProcessor::runEngine (const beadsvst::PanelState& s, const float* l, const float* r,
                                     float* ol, float* orr, int n)
{
    engine.Process (s, l, r, ol, orr, (size_t) n);
}

void BeadsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    if ((int) inL.size() < numSamples)
    {
        // Host gave us a bigger block than announced: grow (rare).
        inL.resize ((size_t) numSamples); inR.resize ((size_t) numSamples);
        outL.resize ((size_t) numSamples); outR.resize ((size_t) numSamples);
    }

    // Copy input (mono input is duplicated to both sides).
    const float* srcL = numIn > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* srcR = numIn > 1 ? buffer.getReadPointer (1) : srcL;
    for (int i = 0; i < numSamples; ++i)
    {
        inL[(size_t) i] = srcL ? srcL[i] : 0.0f;
        inR[(size_t) i] = srcR ? srcR[i] : inL[(size_t) i];
    }

    auto panel = readPanel();
    const bool midiSeed = pMidiSeed->load() > 0.5f;
    panel.external_seed_active = midiSeed;

    // Split the block at MIDI events so note on/off is sample accurate.
    int pos = 0;
    auto renderTo = [&] (int end)
    {
        if (end > pos)
        {
            panel.external_seed_gate = midiNotesHeld > 0;
            runEngine (panel, &inL[(size_t) pos], &inR[(size_t) pos],
                       &outL[(size_t) pos], &outR[(size_t) pos], end - pos);
            pos = end;
        }
    };
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        renderTo (juce::jlimit (0, numSamples, meta.samplePosition));
        if (msg.isNoteOn()) ++midiNotesHeld;
        else if (msg.isNoteOff()) midiNotesHeld = juce::jmax (0, midiNotesHeld - 1);
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) midiNotesHeld = 0;
    }
    renderTo (numSamples);
    midi.clear();

    if (numOut > 0) juce::FloatVectorOperations::copy (buffer.getWritePointer (0), outL.data(), numSamples);
    if (numOut > 1) juce::FloatVectorOperations::copy (buffer.getWritePointer (1), outR.data(), numSamples);
    if (numOut == 1 && numOut > 0)
    {
        auto* o = buffer.getWritePointer (0);
        for (int i = 0; i < numSamples; ++i) o[i] = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);
    }

    overwriteReserve.store (engine.overwrite_reserve());
    const int lat = engine.LatencySamples();
    if (lat != lastLatency)
    {
        lastLatency = lat;
        setLatencySamples (lat);
    }
}

juce::AudioProcessorEditor* BeadsAudioProcessor::createEditor()
{
    return new BeadsEditor (*this);
}

void BeadsAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void BeadsAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BeadsAudioProcessor();
}
