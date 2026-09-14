#pragma once

#include <ftxui/component/animation.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
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

  void set_lensing(float strength) { lensing_strength_.store(std::clamp(strength, 0.1f, 10.0f)); }
  float lensing() const { return lensing_strength_.load(); }

  void set_disk_radius(float radius);

  void set_spin(float spin);
  float spin() const { return spin_.load(); }

  float r_outer_horizon() const;
  float r_inner_horizon() const;
  float r_ergosphere(float theta = 1.57079632679f) const;
  float b_critical_prograde() const;
  float b_critical_retrograde() const;

  void set_draw_mode(DrawMode mode) { draw_mode_.store(mode); }
  DrawMode draw_mode() const { return draw_mode_.load(); }

  void set_color_mode(ColorMode mode) { color_mode_.store(mode); }
  ColorMode color_mode() const { return color_mode_.load(); }

 private:
  struct Particle {
    float angle;
    float radius;
    float sparkle;
  };

  struct ErgosphereParticle {
    float angle;
    float radius;
    float angular_velocity;
    float sparkle;
  };

  static constexpr int kBendTableSize = 256;
  static constexpr int kRadiusBins = 64;
  static constexpr int kAngleBins = 128;

  void respawn(Particle& p);
  void respawn(ErgosphereParticle& ep);
  void tick();
  void build_bend_table();
  int density_index(float radius, float angle) const;

  const int side_;
  const float r_horizon_;
  float r_outer_;
  const float mass_;
  float b_max_;

  float phase_{0.0f};

  std::atomic<float> spin_{0.85f};
  std::atomic<float> tilt_squash_{0.20f};
  std::atomic<float> zoom_scale_{0.85f};
  std::atomic<float> rear_scale_{0.85f};
  std::atomic<float> lensing_strength_{0.35f};
  std::atomic<DrawMode> draw_mode_{DrawMode::Mixed};
  std::atomic<ColorMode> color_mode_{ColorMode::Color};

  std::mt19937 rng_;
  std::mutex mutex_;
  std::vector<Particle> particles_;
  std::vector<ErgosphereParticle> ergo_particles_;

  std::vector<float> bend_table_prograde_;
  std::vector<float> bend_table_retrograde_;
  std::vector<float> nx_table_;
  std::vector<float> nx2_table_;
  std::vector<ftxui::Color> density_;
  std::vector<bool> occupied_;
  float time_accumulator_ = 0.0f;
};

}  // namespace ftxui::ext
