// Plain C++ wrapper around the Mutable Instruments Beads DSP (MIT licensed,
// Copyright Emilie Gillet). No JUCE types in here, so it can be reused anywhere.
//
// Responsibilities:
//  * run the Beads Processor at its native sample rate (24k/32k/48k depending
//    on the quality mode) while the host runs at any sample rate,
//  * feed the Processor in the small fixed blocks it was designed for,
//  * translate "panel" values (knob positions) into Processor parameters.

#ifndef BEADS_ENGINE_H_
#define BEADS_ENGINE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace beadsvst {

struct PanelState {
  // All knobs 0..1 unless noted (same as the hardware panel).
  float time = 0.5f;
  float size = 0.5f;
  float shape = 0.5f;
  float pitch = 0.5f;            // 0.5 = centre = no transposition
  float time_amount = 0.5f;      // attenuverter knobs, 0.5 = centre (no effect)
  float size_amount = 0.5f;
  float shape_amount = 0.5f;
  float pitch_amount = 0.5f;
  float density = 0.5f;
  float feedback = 0.0f;
  float dry_wet = 0.5f;
  float reverb = 0.0f;
  int quality = 0;               // 0 = 48k/16bit, 1 = 32k/12bit (Clouds),
                                 // 2 = 24k/12bit hi-fi dry, 3 = 24k µ-law tape
  bool mono = false;             // mono recording = twice the recording time
  bool freeze = false;
  bool seed_button = false;      // momentary
  bool seed_lock = false;        // latched seed
  bool external_seed_active = false;  // use external_seed_gate instead
  bool external_seed_gate = false;
  float input_gain = 1.0f;       // linear, applied before Beads
  float output_gain = 1.0f;      // linear, applied after Beads
};

class BeadsEngine {
 public:
  BeadsEngine();
  ~BeadsEngine();

  void Prepare(double host_sample_rate);
  void Reset();

  // Processes a block of any size. in/out are separate buffers (may be the
  // same size as `n`). Stereo, planar.
  void Process(const PanelState& panel,
               const float* in_l, const float* in_r,
               float* out_l, float* out_r,
               size_t n);

  // Latency in host samples introduced by resampling and block alignment.
  int LatencySamples() const { return latency_; }

  // 0..1: how much recording space is left before the oldest audio gets
  // overwritten while frozen (1 = plenty).
  float overwrite_reserve() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  int latency_ = 0;
};

}  // namespace beadsvst

#endif  // BEADS_ENGINE_H_
