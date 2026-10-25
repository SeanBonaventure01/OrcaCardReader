#include "orca/fx/audio.hpp"

#include <algorithm>
#include <cmath>

#include "nlohmann/json.hpp"

namespace orca::fx {

namespace {

std::uint32_t le32(const std::uint8_t* p) {
  return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}
std::uint16_t le16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }

}  // namespace

WavResult parse_wav_header(const std::uint8_t* d, std::size_t len, std::size_t file_size) {
  WavResult r;
  if (len < 12 || std::string(reinterpret_cast<const char*>(d), 4) != "RIFF" ||
      std::string(reinterpret_cast<const char*>(d + 8), 4) != "WAVE") {
    r.error = "not a RIFF/WAVE file";
    return r;
  }
  WavInfo info;
  bool have_fmt = false;
  std::size_t pos = 12;
  while (pos + 8 <= len) {
    const std::string id(reinterpret_cast<const char*>(d + pos), 4);
    const std::uint32_t size = le32(d + pos + 4);
    const std::size_t body = pos + 8;
    if (id == "fmt ") {
      if (size < 16 || body + 16 > len) {
        r.error = "truncated fmt chunk";
        return r;
      }
      const std::uint16_t format = le16(d + body);
      info.channels = le16(d + body + 2);
      info.sample_rate = le32(d + body + 4);
      info.bits = le16(d + body + 14);
      if (format != 1) {
        r.error = "not PCM (format " + std::to_string(format) + ")";
        return r;
      }
      have_fmt = true;
    } else if (id == "data") {
      if (!have_fmt) {
        r.error = "data chunk before fmt chunk";
        return r;
      }
      info.data_offset = body;
      info.data_size = std::min<std::size_t>(size, file_size > body ? file_size - body : 0);
      if (info.channels != 1) {
        r.error = "must be mono (has " + std::to_string(info.channels) + " channels)";
        return r;
      }
      if (info.bits != 16) {
        r.error = "must be 16-bit (is " + std::to_string(info.bits) + "-bit)";
        return r;
      }
      if (info.sample_rate < 8000 || info.sample_rate > 48000) {
        r.error = "sample rate must be 8-48 kHz";
        return r;
      }
      if (info.data_size == 0) {
        r.error = "no audio data";
        return r;
      }
      r.info = info;
      return r;
    }
    pos = body + size + (size & 1);  // chunks are word-aligned
  }
  r.error = have_fmt ? "no data chunk in header" : "no fmt chunk";
  return r;
}

void mix(const std::int16_t* a, std::size_t na, int va, const std::int16_t* b, std::size_t nb, int vb,
         std::int16_t* out, std::size_t n) {
  va = std::clamp(va, 0, 100);
  vb = std::clamp(vb, 0, 100);
  for (std::size_t i = 0; i < n; ++i) {
    const std::int32_t sa = i < na ? a[i] * va : 0;
    const std::int32_t sb = i < nb ? b[i] * vb : 0;
    out[i] = static_cast<std::int16_t>(std::clamp<std::int32_t>((sa + sb) / 100, -32768, 32767));
  }
}

std::vector<std::int16_t> synth_tone(std::uint32_t rate, double freq, std::uint32_t ms, int amplitude, bool square) {
  const std::size_t n = static_cast<std::size_t>(rate) * ms / 1000;
  const std::size_t ramp = std::min<std::size_t>(rate * 5 / 1000, n / 2);
  std::vector<std::int16_t> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double phase = std::fmod(i * freq / rate, 1.0);
    double v = square ? (phase < 0.5 ? 1.0 : -1.0) : std::sin(2 * 3.14159265358979323846 * phase);
    double env = 1.0;
    if (ramp > 0 && i < ramp) env = static_cast<double>(i) / ramp;
    if (ramp > 0 && i >= n - ramp) env = static_cast<double>(n - 1 - i) / ramp;
    out[i] = static_cast<std::int16_t>(std::lround(v * env * amplitude));
  }
  return out;
}

const std::vector<Partial> kOrcaBong = {
    {605.5, 0.0, 0.020, -58.0},    {1210.1, -18.1, 0.037, -121.4}, {1815.7, -5.2, 0.029, -163.1},
    {2415.3, -20.8, 0.023, -153.4}, {3017.8, -14.1, 0.019, -152.9}, {3623.3, -25.5, 0.019, -107.4},
    {4230.3, -18.6, 0.033, -277.2},
};

std::vector<std::int16_t> synth_partials(std::uint32_t rate, const std::vector<Partial>& partials, double pitch,
                                         std::uint32_t ms, int peak) {
  constexpr double kTwoPi = 2 * 3.14159265358979323846;
  const std::size_t n = static_cast<std::size_t>(rate) * ms / 1000;
  const std::size_t fade = std::min<std::size_t>(rate / 10, n / 4);
  std::vector<double> acc(n, 0.0);
  for (const auto& p : partials) {
    const double amp = std::pow(10.0, p.level_db / 20);
    const double attack = std::max(p.attack_s, 0.012);  // shorter attacks click
    const std::size_t attack_n = static_cast<std::size_t>(attack * rate);
    const double decay = std::pow(10.0, p.decay_db_per_s / 20 / rate);  // per-sample factor
    // Recurrences instead of sin()/pow() per sample: the ESP32-S3 has no double-precision FPU.
    const double w = kTwoPi * p.freq_hz * pitch / rate, cw = std::cos(w), sw = std::sin(w);
    double s = 0, c = 1, env = 1;
    for (std::size_t i = 0; i < n; ++i) {
      double e;
      if (i < attack_n) {
        e = 0.5 - 0.5 * std::cos(3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(attack_n));
      } else {
        e = env;
        env *= decay;
      }
      acc[i] += amp * e * s;
      const double ns = s * cw + c * sw;
      c = c * cw - s * sw;
      s = ns;
    }
  }
  for (std::size_t i = 0; i < fade; ++i) acc[n - 1 - i] *= static_cast<double>(i) / fade;
  double max = 1e-9;
  for (double v : acc) max = std::max(max, std::abs(v));
  std::vector<std::int16_t> out(n);
  for (std::size_t i = 0; i < n; ++i) out[i] = static_cast<std::int16_t>(std::lround(acc[i] / max * peak));
  return out;
}

std::vector<std::int16_t> synth_beep(const std::string& id, std::uint32_t rate) {
  auto gap = [&](std::uint32_t ms) { return std::vector<std::int16_t>(rate * ms / 1000, 0); };
  auto cat = [](std::vector<std::int16_t> a, const std::vector<std::int16_t>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
  };
  auto bong = [&](double pitch, std::uint32_t ms) { return synth_partials(rate, kOrcaBong, pitch, ms); };
  // Musical intervals relative to the reader's own pitch.
  constexpr double kFourthDown = 0.75, kThird = 1.26, kFourth = 4.0 / 3, kFifth = 1.5;
  if (id == "accept") return bong(1.0, 750);
  if (id == "reject") return cat(cat(bong(kFourthDown, 200), gap(60)), bong(kFourthDown, 200));
  if (id == "bonus") return cat(bong(1.0, 140), bong(kFourth, 650));
  if (id == "start" || id == "event_start") return cat(cat(bong(1.0, 140), bong(kThird, 140)), bong(kFifth, 700));
  if (id == "end" || id == "event_end") return cat(cat(bong(kFifth, 140), bong(kThird, 140)), bong(1.0, 700));
  return {};
}

SoundMap parse_sound_map(const std::string& text) {
  SoundMap m;
  auto j = nlohmann::json::parse(text, nullptr, false, true);
  if (j.is_discarded() || !j.is_object()) {
    m.warnings.push_back("sounds.json is not a JSON object");
    return m;
  }
  for (auto it = j.begin(); it != j.end(); ++it) {
    if (!it->is_string() || it->get<std::string>().empty()) {
      m.warnings.push_back("sound '" + it.key() + "' must map to a file path");
      continue;
    }
    const std::string path = it->get<std::string>();
    if (path.find("..") != std::string::npos || path.front() == '/') {
      m.warnings.push_back("sound '" + it.key() + "' path must be relative to the SD card");
      continue;
    }
    m.files[it.key()] = path;
  }
  return m;
}

}  // namespace orca::fx
