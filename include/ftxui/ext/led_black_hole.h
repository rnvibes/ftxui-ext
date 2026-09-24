#pragma once

#include <ftxui/component/animation.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <random>
#include <vector>

namespace ftxui::ext {

class TFrameBuffer;

enum class DrawMode {
  Braille,
  Block,
  Mixed,
};

enum class ColorMode {
  Monochrome,
  Color,
};

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

  // 0 = flat geometric disk (no far-side arch), 1 = full geodesic lensing.
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

  // Blend of smooth ("smeared") sheet (0.0) and advected particles (1.0).
  void set_disk_mix(float m) { disk_mix_.store(std::clamp(m, 0.0f, 1.0f)); }
  float disk_mix() const { return disk_mix_.load(); }
  void set_particles(bool on) { set_disk_mix(on ? 0.5f : 0.0f); }
  bool particles() const { return disk_mix() > 0.05f; }

  // Pulsating accretion flow: traveling hot spots plus a global ignition beat.
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

  void set_draw_mode(DrawMode mode) { draw_mode_.store(mode); }
  DrawMode draw_mode() const { return draw_mode_.load(); }

  void set_color_mode(ColorMode mode) { color_mode_.store(mode); }
  ColorMode color_mode() const { return color_mode_.load(); }

 private:
  // Precomputed per-pixel screen geometry. Depends only on the render size,
  // zoom and tilt, so it is rebuilt when one of those changes -- never per
  // frame. The per-pixel loop then does table lookups and arithmetic only.
  struct ScreenPixel {
    float nx = 0.0f;
    float b = -1.0f;   // impact parameter (screen radius); <0 => outside field
    float phi = 0.0f;  // screen azimuth atan2(ny, nx)
    float r_front = 0.0f;  // radius where the line of sight meets the disk
    float theta = 0.0f;    // azimuth of that intersection
    float ny = 0.0f;       // vertical screen coordinate (near/far side test)
  };

  static constexpr int kBendTableSize = 256;
  static constexpr int kShadowBins = 128;
  static constexpr int kDiskRadiusBins = 128;
  static constexpr int kColorLutSize = 256;

  void tick();
  float r_isco() const;
  void build_bend_tables();   // bend + perihelion per impact parameter (spin)
  void build_shadow_table();  // Bardeen critical curve (spin + tilt)
  void build_disk_tables();   // Omega, u^t, temperature, blackbody LUT (spin)
  void rebuild_geometry();    // per-pixel screen geometry (size/zoom/tilt)

  const int side_;
  const float r_horizon_;
  float r_outer_;
  const float mass_;
  float b_max_;

  float phase_{0.0f};
  float pulse_phase_{0.0f};

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
  std::mutex mutex_;

  // Geodesic tables (rebuilt when the spin changes).
  std::vector<float> bend_pro_;
  std::vector<float> bend_ret_;
  std::vector<float> rmin_pro_;
  std::vector<float> rmin_ret_;
  std::vector<float> rfar_pro_;  // emission radius of the far-side image
  std::vector<float> rfar_ret_;
  std::vector<float> shadow_r_;  // critical-curve radius vs screen azimuth

  // Disk emission tables (rebuilt when the spin changes).
  std::vector<float> omega_lut_;
  std::vector<float> ut_lut_;
  std::vector<float> temp_lut_;
  std::vector<float> emis_lut_;
  std::vector<std::array<std::uint8_t, 3>> color_lut_;
  float color_tmin_ = 1000.0f;
  float color_tmax_ = 12000.0f;

  // Per-pixel screen geometry (rebuilt on size/zoom/tilt change).
  std::vector<ScreenPixel> geom_;
  int geom_sub_side_ = 0;
  float geom_zoom_ = -1.0f;
  float geom_tilt_ = -1.0f;
  float geom_orbit_ = 0.0f;
  bool view_dirty_ = true;

  float time_accumulator_ = 0.0f;
};

}  // namespace ftxui::ext
