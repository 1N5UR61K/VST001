#include "beads_engine.h"

#define _USE_MATH_DEFINES
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "beads/dsp/processor.h"
#include "stmlib/utils/gate_flags.h"
#include "stmlib/utils/random.h"

namespace beadsvst {

namespace {

const size_t kChunk = 12;                 // frames per Processor call
const size_t kDelayBytes = 768 * 1024;    // same layout as the module's RAM
const size_t kReverbWords = 32768;

// ---------------------------------------------------------------------------
// Streaming windowed-sinc resampler (one channel).
// ---------------------------------------------------------------------------
class Resampler {
 public:
  void Init(double in_rate, double out_rate) {
    step_ = in_rate / out_rate;
    const double fc = std::min(1.0, out_rate / in_rate) * 0.96;
    half_ = static_cast<int>(std::ceil(16.0 / fc));
    taps_ = 2 * half_;
    table_.assign((kPhases + 1) * taps_, 0.0f);
    for (int p = 0; p <= kPhases; ++p) {
      const double f = double(p) / kPhases;
      double sum = 0.0;
      std::vector<double> row(taps_);
      for (int k = 0; k < taps_; ++k) {
        const double d = (k - half_ + 1) - f;   // distance in input samples
        const double x = d / half_;
        double w = 0.0;
        if (std::fabs(x) < 1.0) {
          w = 0.42 + 0.5 * std::cos(M_PI * x) + 0.08 * std::cos(2.0 * M_PI * x);
        }
        const double a = M_PI * fc * d;
        const double sinc = std::fabs(a) < 1e-9 ? 1.0 : std::sin(a) / a;
        row[k] = fc * sinc * w;
        sum += row[k];
      }
      for (int k = 0; k < taps_; ++k) {
        table_[p * taps_ + k] = static_cast<float>(row[k] / sum);
      }
    }
    Reset();
  }

  void Reset() {
    buf_.assign(half_, 0.0f);
    pos_ = double(half_ - 1);
  }

  // Group delay in input samples.
  int delay_in() const { return half_; }

  void Push(const float* x, size_t n) { buf_.insert(buf_.end(), x, x + n); }

  void Pull(std::vector<float>* out) {
    while (true) {
      const long i = static_cast<long>(pos_);
      if (i + half_ >= static_cast<long>(buf_.size())) break;
      const double frac = pos_ - double(i);
      const double fp = frac * kPhases;
      const int p = static_cast<int>(fp);
      const float t = static_cast<float>(fp - p);
      const float* a = &table_[p * taps_];
      const float* b = a + taps_;
      const float* x = &buf_[i - half_ + 1];
      float acc = 0.0f;
      for (int k = 0; k < taps_; ++k) {
        acc += x[k] * (a[k] + t * (b[k] - a[k]));
      }
      out->push_back(acc);
      pos_ += step_;
    }
    const long drop = static_cast<long>(pos_) - half_;
    if (drop > 0) {
      buf_.erase(buf_.begin(), buf_.begin() + drop);
      pos_ -= double(drop);
    }
  }

 private:
  static const int kPhases = 256;
  double step_ = 1.0;
  int half_ = 16;
  int taps_ = 32;
  double pos_ = 0.0;
  std::vector<float> table_;
  std::vector<float> buf_;
};

// Same knob "laws" as the hardware (cv_reader.h).
float AttenuverterLaw(float r) {
  float v = r - 0.5f;
  return (2.0f / 0.47f) * v * std::max(std::fabs(v) - 0.03f, 0.0f);
}

float DetuneLaw(float r) {
  float detune = 2.0f * r - 1.0f;
  const float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 6.0f;
  int integral = static_cast<int>(detune);
  if (integral > 6) integral = 6;
  float fractional = detune - float(integral);
  static const float intervals[8] = {0, 1, 3, 4, 7, 12, 24, 24};
  fractional = std::max((fractional - 0.33333333f) * 1.5f, 0.0f);
  const float t = fractional * fractional * (3.0f - 2.0f * fractional);
  const float a = intervals[integral];
  const float b = intervals[integral + 1];
  return (a + (b - a) * t) * sign;
}

inline float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }

}  // namespace

struct BeadsEngine::Impl {
  Impl()
      : processor(new beads::Processor),
        delay(new uint8_t[kDelayBytes]),
        reverb(new uint16_t[kReverbWords]) {
    std::memset(delay.get(), 0, kDelayBytes);
    std::memset(reverb.get(), 0, kReverbWords * sizeof(uint16_t));
    uint8_t* buffers[3] = {
        delay.get(), delay.get() + 256 * 1024, delay.get() + 512 * 1024};
    processor->Init(buffers, reverb.get());
    stmlib::Random::Seed(0x1234567);
  }

  std::unique_ptr<beads::Processor> processor;
  std::unique_ptr<uint8_t[]> delay;
  std::unique_ptr<uint16_t[]> reverb;

  double host_rate = 48000.0;
  double proc_rate = 48000.0;
  bool bypass_src = true;
  int media = -1;     // beads::Processor::Media, -1 = not configured yet
  bool mono = false;
  bool prepared = false;

  Resampler down[2], up[2];
  std::vector<float> in_q[2];     // at processor rate
  std::vector<float> out_q[2];    // at processor rate
  std::vector<float> host_q[2];   // at host rate
  std::vector<float> scratch[2];

  // smoothed knobs
  float s_time = 0.5f, s_size = 0.5f, s_shape = 0.5f, s_pitch = 0.0f;
  float s_ta = 0.0f, s_sa = 0.0f, s_sha = 0.0f, s_pa = 0.0f;
  float s_density = 0.5f, s_fb = 0.0f, s_dw = 0.5f, s_rev = 0.0f;
  bool first = true;
  stmlib::GateFlags prev_seed = stmlib::GATE_FLAG_LOW;
  int priming = 0;

  void ConfigureIfNeeded(const PanelState& p, int* latency) {
    static const int kQualityToMedia[4] = {
        beads::Processor::MEDIA_DIGITAL,
        beads::Processor::MEDIA_VINTAGE_DIGITAL,
        beads::Processor::MEDIA_CLEAN_TAPE,
        beads::Processor::MEDIA_DIRTY_TAPE};
    const int q = std::min(3, std::max(0, p.quality));
    const int m = kQualityToMedia[q];
    if (m == media && p.mono == mono) return;
    media = m;
    mono = p.mono;
    processor->Configure(static_cast<beads::Processor::Media>(m), mono);
    proc_rate = processor->sample_rate();
    bypass_src = std::fabs(proc_rate - host_rate) < 0.5;
    for (int c = 0; c < 2; ++c) {
      in_q[c].clear();
      out_q[c].clear();
      host_q[c].clear();
      if (!bypass_src) {
        down[c].Init(host_rate, proc_rate);
        up[c].Init(proc_rate, host_rate);
      }
    }
    // Pre-fill the output queue so we never run dry.
    double lat = double(kChunk) * host_rate / proc_rate + 2.0;
    if (!bypass_src) {
      lat += down[0].delay_in();
      lat += up[0].delay_in() * host_rate / proc_rate;
    }
    priming = static_cast<int>(std::ceil(lat)) + 2;
    for (int c = 0; c < 2; ++c) host_q[c].assign(priming, 0.0f);
    *latency = priming;
  }
};

BeadsEngine::BeadsEngine() : impl_(new Impl) {}
BeadsEngine::~BeadsEngine() {}

void BeadsEngine::Prepare(double host_sample_rate) {
  impl_->host_rate = host_sample_rate;
  impl_->media = -1;    // force re-configure on next Process
  impl_->prepared = true;
}

void BeadsEngine::Reset() {
  impl_->media = -1;
}

float BeadsEngine::overwrite_reserve() const {
  return impl_->processor->overwrite_reserve();
}

void BeadsEngine::Process(const PanelState& panel,
                          const float* in_l, const float* in_r,
                          float* out_l, float* out_r, size_t n) {
  Impl& m = *impl_;
  m.ConfigureIfNeeded(panel, &latency_);

  const float* in[2] = {in_l, in_r};
  float* out[2] = {out_l, out_r};

  // 1. Host rate -> processor rate.
  for (int c = 0; c < 2; ++c) {
    m.scratch[c].resize(n);
    for (size_t i = 0; i < n; ++i) {
      float v = in[c][i] * panel.input_gain;
      m.scratch[c][i] = std::isfinite(v) ? v : 0.0f;
    }
    if (m.bypass_src) {
      m.in_q[c].insert(m.in_q[c].end(), m.scratch[c].begin(), m.scratch[c].end());
    } else {
      m.down[c].Push(m.scratch[c].data(), n);
      m.down[c].Pull(&m.in_q[c]);
    }
  }

  // 2. Run Beads in fixed chunks.
  size_t consumed = 0;
  const size_t available = std::min(m.in_q[0].size(), m.in_q[1].size());
  while (available - consumed >= kChunk) {
    // Knob smoothing (the module low-passes its pots once per block, too).
    const float k = 0.05f;
    auto smooth = [&](float& s, float target) {
      if (m.first) s = target; else s += k * (target - s);
    };
    smooth(m.s_time, panel.time);
    smooth(m.s_size, panel.size);
    smooth(m.s_shape, panel.shape);
    smooth(m.s_pitch, DetuneLaw(panel.pitch));
    smooth(m.s_ta, AttenuverterLaw(panel.time_amount));
    smooth(m.s_sa, AttenuverterLaw(panel.size_amount));
    smooth(m.s_sha, AttenuverterLaw(panel.shape_amount));
    smooth(m.s_pa, AttenuverterLaw(panel.pitch_amount));
    smooth(m.s_density, panel.density);
    smooth(m.s_fb, panel.feedback);
    smooth(m.s_dw, panel.dry_wet);
    smooth(m.s_rev, panel.reverb);
    m.first = false;

    beads::Parameters p;
    p.time.base = m.s_time;   p.time.attenuverter = m.s_ta;
    p.time.cv = 0.0f;         p.time.has_cv = false;
    p.size.base = m.s_size;   p.size.attenuverter = m.s_sa;
    p.size.cv = 0.0f;         p.size.has_cv = false;
    p.shape.base = m.s_shape; p.shape.attenuverter = m.s_sha;
    p.shape.cv = 0.0f;        p.shape.has_cv = false;
    p.pitch.base = m.s_pitch; p.pitch.attenuverter = m.s_pa;
    p.pitch.cv = 0.0f;        p.pitch.has_cv = false;
    p.monophonic_mode = false;

    beads::ModulatedParameter d;
    d.base = m.s_density;
    d.cv = 0.0f;

    beads::Processor::MixParameters mix;
    mix.feedback = Clamp01(m.s_fb * 1.005f);
    mix.dry_wet = Clamp01(m.s_dw * 1.005f);
    mix.reverb_amount = Clamp01(m.s_rev * 1.005f);
    mix.output_gate = false;

    bool freeze[kChunk];
    stmlib::GateFlags seed[kChunk];
    for (size_t i = 0; i < kChunk; ++i) {
      freeze[i] = panel.freeze;
      m.prev_seed = seed[i] = stmlib::ExtractGateFlags(
          m.prev_seed, panel.external_seed_gate);
    }

    beads::SeedButtonState sb = panel.seed_lock
        ? beads::SEED_BUTTON_STATE_LOCKED
        : (panel.seed_button ? beads::SEED_BUTTON_STATE_ON
                             : beads::SEED_BUTTON_STATE_OFF);

    beads::FloatFrame fin[kChunk], fout[kChunk];
    for (size_t i = 0; i < kChunk; ++i) {
      fin[i].l = m.in_q[0][consumed + i];
      fin[i].r = m.in_q[1][consumed + i];
      if (panel.mono) fin[i].r = fin[i].l = 0.5f * (fin[i].l + fin[i].r);
    }

    m.processor->Process(
        sb, panel.external_seed_active ? seed : NULL, freeze, d, p, mix,
        fin, fout, kChunk);

    for (size_t i = 0; i < kChunk; ++i) {
      m.out_q[0].push_back(fout[i].l);
      m.out_q[1].push_back(fout[i].r);
    }
    consumed += kChunk;
  }
  for (int c = 0; c < 2; ++c) {
    m.in_q[c].erase(m.in_q[c].begin(), m.in_q[c].begin() + consumed);
  }

  // 3. Processor rate -> host rate.
  for (int c = 0; c < 2; ++c) {
    if (m.bypass_src) {
      m.host_q[c].insert(m.host_q[c].end(), m.out_q[c].begin(), m.out_q[c].end());
    } else {
      m.up[c].Push(m.out_q[c].data(), m.out_q[c].size());
      m.up[c].Pull(&m.host_q[c]);
    }
    m.out_q[c].clear();
  }

  // 4. Deliver exactly n samples.
  for (int c = 0; c < 2; ++c) {
    const size_t have = std::min(n, m.host_q[c].size());
    for (size_t i = 0; i < have; ++i) {
      float v = m.host_q[c][i] * panel.output_gain;
      out[c][i] = std::isfinite(v) ? v : 0.0f;
    }
    for (size_t i = have; i < n; ++i) out[c][i] = 0.0f;  // underrun guard
    m.host_q[c].erase(m.host_q[c].begin(), m.host_q[c].begin() + have);
  }
}

}  // namespace beadsvst
