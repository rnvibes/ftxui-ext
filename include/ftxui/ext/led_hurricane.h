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

#ifndef FTXUI_EXT_COLOR_MODE_DEFINED
#define FTXUI_EXT_COLOR_MODE_DEFINED
enum class ColorMode {
  Monochrome,
  Color,
};
#endif

enum class HurricanePhase {
  Inflow = 0,     // Phase 1: Environmental inflow, organizing convergence
  Genesis = 1,    // Phase 2: Rapid cyclogenesis, eye formation
  Category5 = 2,  // Phase 3: Peak intensity, eyewall stadium, lightning strikes
  Dissipation = 3 // Phase 4: Landfall / decay, eyewall collapse
};

namespace detail {

struct LightningNode {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct LightningBranch {
  std::vector<LightningNode> nodes;
  float intensity = 0.0f;
};

}  // namespace detail

class LEDHurricane {
 public:
  explicit LEDHurricane(int side, unsigned seed = std::random_device{}());

  void advance(ftxui::animation::Duration elapsed);
  void render(TFrameBuffer& matrix);

  // -------------------------------------------------------------------------
  // Camera Controls (Matches LEDBlackHole & ragatui / DropPane mouse drag)
  // -------------------------------------------------------------------------
  // Tilt controls elevation above the storm disk:
  //   ~0.10: low-altitude oblique / profile view looking across the cloud deck
  //   ~0.40: high-angle orbital view revealing the 3D stadium eye
  //    1.00: top-down polar / satellite view
  void set_tilt(float tilt) { tilt_.store(std::clamp(tilt, 0.05f, 1.0f)); }
  float tilt() const { return tilt_.load(); }

  // Orbit controls camera azimuth in radians around the storm center
  void set_orbit(float radians);
  float orbit() const { return orbit_.load(); }

  // Zoom scale: 0.5 (wide satellite view) to 2.5 (close-up eyewall inspection)
  void set_zoom(float scale) { zoom_.store(std::clamp(scale, 0.5f, 2.5f)); }
  float zoom() const { return zoom_.load(); }

  // -------------------------------------------------------------------------
  // Physical & Cinematic Controls
  // -------------------------------------------------------------------------
  // Storm intensity scale: 0.0 (tropical depression) to 1.0 (Category 5 hurricane)
  void set_intensity(float scale) { intensity_.store(std::clamp(scale, 0.0f, 1.0f)); }
  float intensity() const { return intensity_.load(); }

  // Cloud inflow current speed multiplier
  void set_inflow_speed(float speed) { inflow_speed_.store(std::clamp(speed, 0.1f, 5.0f)); }
  float inflow_speed() const { return inflow_speed_.load(); }

  // Rate of the cinematic lifecycle loop (1.0 = ~45-60s full cycle)
  void set_lifecycle_speed(float speed) { lifecycle_speed_.store(std::clamp(speed, 0.0f, 5.0f)); }
  float lifecycle_speed() const { return lifecycle_speed_.load(); }

  // Force/lock a specific lifecycle phase, or -1 for automatic continuous progression
  void set_phase_override(int phase) { phase_override_.store(phase); }
  int phase_override() const { return phase_override_.load(); }

  // Current lifecycle phase (0: Inflow, 1: Genesis, 2: Category5, 3: Dissipation)
  HurricanePhase current_phase() const;
  float lifecycle_progress() const { return life_phase_.load(); }

  // Lightning controls: strikes per second in Phase 3
  void set_lightning_frequency(float freq) { lightning_freq_.store(std::clamp(freq, 0.0f, 10.0f)); }
  float lightning_frequency() const { return lightning_freq_.load(); }

  // Manually trigger an immediate lightning bolt in the eyewall
  void trigger_lightning();

  // Color mode: Full-color cinematic vs monochrome terminal
  void set_color_mode(ColorMode mode) { color_mode_.store(mode); }
  ColorMode color_mode() const { return color_mode_.load(); }

  // -------------------------------------------------------------------------
  // Scientific Diagnostics & Probes (Physics verification & unit tests)
  // -------------------------------------------------------------------------
  float central_pressure_hpa() const;
  float max_wind_speed_ms() const;
  float eye_radius_km() const;
  float tangential_velocity_at(float r_norm) const;
  float cloud_density_at(float x, float y, float z) const;
  bool is_lightning_active() const;

 private:
  void tick(float dt);
  void update_lifecycle(float dt);
  void update_lightning(float dt);
  void trigger_lightning_locked();
  void retrace(int ss);

  const int side_;
  const float max_radius_;     // Physical domain radius
  const float storm_height_;    // Tropospheric scale height

  std::mt19937 rng_;
  float seed_offset_ = 0.0f;
  std::mutex mutex_;

  // Camera settings
  std::atomic<float> tilt_{0.38f};
  std::atomic<float> orbit_{0.0f};
  std::atomic<float> zoom_{1.0f};

  // Simulation settings
  std::atomic<float> intensity_{1.0f};
  std::atomic<float> inflow_speed_{1.0f};
  std::atomic<float> lifecycle_speed_{1.0f};
  std::atomic<int> phase_override_{-1};
  std::atomic<float> lightning_freq_{1.4f};
  std::atomic<ColorMode> color_mode_{ColorMode::Color};

  // State variables
  std::atomic<float> life_phase_{0.15f}; // Progress through [0.0, 1.0)
  float time_sec_{0.0f};
  float time_accumulator_{0.0f};

  // Lightning state
  std::vector<detail::LightningBranch> active_bolts_;
  float lightning_flash_intensity_{0.0f};
  std::array<float, 3> lightning_flash_pos_{0.0f, 0.0f, 0.0f};
  float time_since_last_strike_{0.0f};

  // Rendering & progressive cache
  std::vector<std::array<std::uint8_t, 4>> frame_;
  std::array<float, 8> render_key_{};
  int trace_ss_ = 0;
  float trace_zoom_ = -1.0f;
  float trace_tilt_ = -1.0f;
  float trace_orbit_ = -999.0f;
  bool view_dirty_ = true;
  bool trace_coarse_ = false;
  std::chrono::steady_clock::time_point view_changed_at_{};
};

}  // namespace ftxui::ext
