// Offline test: runs the engine on a synthetic input and writes WAV files.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "beads_engine.h"

static void WriteWav(const std::string& path, const std::vector<float>& l,
                     const std::vector<float>& r, int sr) {
  FILE* f = fopen(path.c_str(), "wb");
  uint32_t n = l.size(), data = n * 4, riff = 36 + data, fmt = 16, rate = sr,
           byte_rate = sr * 4;
  uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&rate, 4, 1, f); fwrite(&byte_rate, 4, 1, f); fwrite(&align, 2, 1, f);
  fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
  for (uint32_t i = 0; i < n; ++i) {
    for (const std::vector<float>* v : {&l, &r}) {
      float x = std::fmax(-1.f, std::fmin(1.f, (*v)[i]));
      int16_t s = int16_t(x * 32767.f);
      fwrite(&s, 2, 1, f);
    }
  }
  fclose(f);
}

int main(int argc, char** argv) {
  std::string outdir = argc > 1 ? argv[1] : ".";
  const double rates[] = {44100.0, 48000.0, 96000.0};
  const int block_sizes[] = {64, 512, 37};
  int failures = 0;
  for (double sr : rates) {
    for (int quality = 0; quality < 4; ++quality) {
      beadsvst::BeadsEngine e;
      e.Prepare(sr);
      beadsvst::PanelState p;
      p.quality = quality; p.seed_lock = true;
      p.dry_wet = 0.8f; p.density = 0.7f; p.size = 0.4f; p.reverb = 0.3f;
      p.feedback = 0.3f; p.time = 0.3f;
      p.input_gain = 0.5f;
      size_t total = size_t(sr * 6);
      std::vector<float> in(total), ol(total), orr(total), il(total);
      for (size_t i = 0; i < total; ++i) {
        double t = i / sr;
        double note = 220.0 * std::pow(2.0, (int(t * 4) % 5) * 3 / 12.0);
        double env = std::exp(-fmod(t * 4, 1.0) * 4.0);
        in[i] = float(0.6 * env * std::sin(2 * M_PI * note * t));
      }
      size_t pos = 0, bi = 0;
      while (pos < total) {
        size_t n = std::min<size_t>(block_sizes[bi++ % 3], total - pos);
        if (pos > sr * 3) p.freeze = true;
        e.Process(p, &in[pos], &in[pos], &ol[pos], &orr[pos], n);
        pos += n;
      }
      double peak = 0, sum = 0, tail = 0; bool bad = false;
      for (size_t i = 0; i < total; ++i) {
        double v = std::fabs(ol[i]);
        if (!std::isfinite(ol[i])) bad = true;
        peak = std::fmax(peak, v); sum += v * v;
        if (i > total / 2) tail += v * v;
      }
      double rms = std::sqrt(sum / total);
      printf("sr=%6.0f quality=%d latency=%4d peak=%.3f rms=%.4f tail_rms=%.4f %s\n",
             sr, quality, e.LatencySamples(), peak, rms,
             std::sqrt(tail / (total / 2)), bad ? "NAN!" : "ok");
      if (bad || rms < 1e-4 || peak > 4.0) ++failures;
      char name[256];
      snprintf(name, sizeof name, "%s/beads_sr%d_q%d.wav", outdir.c_str(), int(sr), quality);
      WriteWav(name, ol, orr, int(sr));
    }
  }
  printf("failures: %d\n", failures);
  return failures;
}
