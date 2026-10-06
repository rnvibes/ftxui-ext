#pragma once

#include <ftxui/component/animation.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <vector>

namespace ftxui::ext {

class TFrameBuffer;

namespace detail {

// One camera ray's Kerr geodesic, traced once per view and shaded every tick.
// The path depends only on the camera and the spin; time only moves the gas
// along it.
struct BlackHoleRay {
  static constexpr int kMaxHits = 4;
  std::array<float, kMaxHits> hit_r{};    // disk-plane crossings, in M
  std::array<float, kMaxHits> hit_phi{};  // azimuth of each crossing
  int hits = 0;
  bool escaped = false;
  float lz = 0.0f;                        // photon axial angular momentum
  std::array<float, 3> sky{};             // escape direction, for the stars
};

}  // namespace detail

enum class DrawMode {
  Braille,
  Block,
  Mixed,
};

#ifndef FTXUI_EXT_COLOR_MODE_DEFINED
#define FTXUI_EXT_COLOR_MODE_DEFINED
enum class ColorMode {
  Monochrome,
  Color,
};
#endif

class LEDBlackHole {
 public:
  explicit LEDBlackHole(int side, unsigned seed = std::random_device{}());

  void advance(ftxui::animation::Duration elapsed);
  void render(TFrameBuffer& matrix);

  void set_tilt(float squash) { tilt_squash_.store(std::clamp(squash, 0.05f, 1.0f)); }
  float tilt() const { return tilt_squash_.load(); }

  void set_zoom(float scale) { zoom_scale_.store(std::clamp(scale, 0.5f, 2.0f)); }
  float zoom() const { return zoom_scale_.load(); }

  void set_rear_scale(float scale) { rear_scale_.store(std::clamp(scale, 0.40f, 1.50f)); }
  float rear_scale() const { return rear_scale_.load(); }

  // Scales the metric's deviation from flat space: 0 = straight rays (a flat
  // disk behind an opaque sphere), 1 = full Kerr lensing. Continuous between.
  void set_lensing(float strength) { lensing_strength_.store(std::clamp(strength, 0.0f, 1.0f)); }
  float lensing() const { return lensing_strength_.load(); }

  void set_disk_radius(float radius);

  // Relativistic beaming (the (1 - Omega b) Doppler factor plus its g^4
  // intensity asymmetry). Disabling it leaves only the gravitational/transverse
  // redshift and gives the left/right-symmetric "cinematic" disk.
  void set_relativistic_beaming(bool on) { rel_beam_.store(on); }
  bool relativistic_beaming() const { return rel_beam_.load(); }

  // Camera roll about the viewing axis, in radians ("spin it in place").
  void set_orbit(float radians);
  float orbit() const { return orbit_.load(); }

  // Artificial single-colour override for the disk (r,g,b in 0..255).
  void set_disk_color(int r, int g, int b);
  void clear_disk_color() { color_on_.store(false); }
  bool disk_color_active() const { return color_on_.load(); }

  // Blend of smooth ("smeared") sheet (0.0) and orbiting particles (1.0).
  void set_disk_mix(float m) { disk_mix_.store(std::clamp(m, 0.0f, 1.0f)); }
  float disk_mix() const { return disk_mix_.load(); }
  void set_particles(bool on) { set_disk_mix(on ? 0.5f : 0.0f); }
  bool particles() const { return disk_mix() > 0.05f; }

  // Pulsating accretion flow: a slow global ignition beat. The rate also scales
  // how fast the gas and particles orbit.
  void set_pulsing(bool on) { pulsing_.store(on); }
  bool pulsing() const { return pulsing_.load(); }
  void set_pulse_rate(float rate) { pulse_rate_.store(std::clamp(rate, 0.1f, 8.0f)); }
  float pulse_rate() const { return pulse_rate_.load(); }

  void set_spin(float spin);
  float spin() const { return spin_.load(); }

  float r_outer_horizon() const;
  float r_inner_horizon() const;
  float r_ergosphere(float theta = 1.57079632679f) const;
  float b_critical_prograde() const;
  float b_critical_retrograde() const;

  // Physics probes (used by the regression tests and by tooling).
  float deflection(float b_impact) const;   // total bend of a flyby ray, radians
  float isco_radius() const;                // Bardeen-Press-Teukolsky ISCO
  float shadow_radius(float azimuth);       // critical-curve radius vs screen angle
  float redshift_factor(float r, float b_impact) const;  // g = nu_obs / nu_emit
  // Whether an equatorial ray at signed impact parameter b (positive =
  // prograde) falls in, by the same 3D Kerr-Schild tracer the renderer uses.
  bool ray_captured(float b_impact) const;

  void set_draw_mode(DrawMode mode) { draw_mode_.store(mode); }
  DrawMode draw_mode() const { return draw_mode_.load(); }

  void set_color_mode(ColorMode mode) { color_mode_.store(mode); }
  ColorMode color_mode() const { return color_mode_.load(); }

 private:
  static constexpr int kShadowBins = 128;
  static constexpr int kColorLutSize = 256;

  void tick();
  float r_isco() const;
  void build_shadow_table();  // Bardeen critical curve (spin + tilt)
  void build_color_lut();     // blackbody ramp, linear light
  void retrace(int ss);       // per-pixel geodesics (size/zoom/tilt/orbit/spin/lensing)

  const int side_;
  const float r_horizon_;
  float r_outer_;
  const float mass_;

  float time_m_{0.0f};  // animation clock, in M, wrapped to a seamless period
  float beat_phase_{0.0f};

  std::atomic<float> spin_{0.85f};
  std::atomic<float> tilt_squash_{0.20f};
  std::atomic<float> zoom_scale_{0.85f};
  std::atomic<float> rear_scale_{0.85f};
  std::atomic<float> lensing_strength_{1.0f};
  std::atomic<bool> rel_beam_{true};
  std::atomic<float> orbit_{0.0f};
  std::atomic<bool> color_on_{false};
  std::atomic<int> color_r_{255};
  std::atomic<int> color_g_{140};
  std::atomic<int> color_b_{40};
  std::atomic<float> disk_mix_{0.5f};
  std::atomic<bool> pulsing_{true};
  std::atomic<float> pulse_rate_{1.0f};
  std::atomic<DrawMode> draw_mode_{DrawMode::Mixed};
  std::atomic<ColorMode> color_mode_{ColorMode::Color};

  std::mt19937 rng_;
  float seed_offset_ = 0.0f;  // shifts the noise so each seed is a new universe
  std::mutex mutex_;

  std::vector<float> shadow_r_;  // critical-curve radius vs screen azimuth
  std::vector<std::array<float, 3>> color_lut_;

  // Traced rays, ss x ss per logical pixel (rebuilt on a view change only).
  std::vector<detail::BlackHoleRay> rays_;
  int trace_ss_ = 0;
  float trace_zoom_ = -1.0f;
  float trace_tilt_ = -1.0f;
  float trace_orbit_ = 0.0f;
  float trace_lensing_ = -1.0f;
  float trace_r_outer_ = -1.0f;
  bool view_dirty_ = true;
  // While the view is moving, trace one ray per pixel; refine to ss x ss once
  // it has been still for a moment.
  bool trace_coarse_ = false;
  std::chrono::steady_clock::time_point view_changed_at_{};

  // Last shaded frame, RGBA (A = lit). Shading only changes when the clock
  // ticks or a look setting moves, so repeated renders just redraw it.
  std::vector<std::array<std::uint8_t, 4>> frame_;
  std::array<float, 10> shade_key_{};

  float time_accumulator_ = 0.0f;
};

}  // namespace ftxui::ext
