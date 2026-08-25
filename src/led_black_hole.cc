#include "ftxui/ext/led_black_hole.h"
#include "ftxui/ext/frame_buffer.h"

#include <algorithm>
#include <array>
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ftxui::ext {

namespace {
constexpr float kOrbitalConstant = 0.6f;
constexpr float kInwardDrift = 0.01f;
constexpr float kRadiusJitter = 0.03f;
constexpr int kMinParticles = 60;
constexpr int kMaxParticles = 220;
constexpr float kTwoPi = 6.283185307f;
constexpr float kPi = 3.14159265f;
constexpr float kTickSeconds = 0.08f;

ftxui::Color color_for_disk(float u, float doppler) {
  const float norm_r = std::clamp(u / std::max(doppler, 0.1f), 0.0f, 1.0f);

  if (norm_r < 0.10f) {
    return ftxui::Color::White;
  }
  if (norm_r < 0.32f) {
    return ftxui::Color::RGB(255, 180, 0);
  }
  if (norm_r < 0.60f) {
    return ftxui::Color::RGB(255, 120, 0);
  }
  if (norm_r < 0.85f) {
    return ftxui::Color::RGB(200, 40, 10);
  }
  return ftxui::Color::RGB(110, 10, 20);
}

ftxui::Color color_for_ergosphere_particle(float sparkle_val) {
  if (sparkle_val > 0.88f) {
    return ftxui::Color(ftxui::Color::Palette256(195));
  }
  if (sparkle_val > 0.75f) {
    return ftxui::Color(ftxui::Color::Palette256(189));
  }
  return ftxui::Color::White;
}

float integrate_bend(float b, float mass, float r_horizon) {
  double u = 0.0;
  double v = 1.0 / static_cast<double>(b);
  double phi = 0.0;
  constexpr double kDPhi = 0.0025;
  constexpr int kMaxSteps = 5000;
  const double u_horizon = 1.0 / static_cast<double>(r_horizon);

  const auto dv_dphi = [mass](double uu) { return -uu + 3.0 * mass * uu * uu; };

  bool escaped = false;
  for (int i = 0; i < kMaxSteps; ++i) {
    const double k1u = v;
    const double k1v = dv_dphi(u);
    const double k2u = v + 0.5 * kDPhi * k1v;
    const double k2v = dv_dphi(u + 0.5 * kDPhi * k1u);
    const double k3u = v + 0.5 * kDPhi * k2v;
    const double k3v = dv_dphi(u + 0.5 * kDPhi * k2u);
    const double k4u = v + kDPhi * k3v;
    const double k4v = dv_dphi(u + kDPhi * k3u);

    u += (kDPhi / 6.0) * (k1u + 2 * k2u + 2 * k3u + k4u);
    v += (kDPhi / 6.0) * (k1v + 2 * k2v + 2 * k3v + k4v);
    phi += kDPhi;

    if (u >= u_horizon) return -1.0f;
    if (phi > 0.3 && u <= 0.0) {
      escaped = true;
      break;
    }
  }
  if (!escaped) return -1.0f;

  const float bend = static_cast<float>(phi) - kPi;
  return std::clamp(bend, 0.0f, 3.0f * kPi);
}
}  // namespace

LEDBlackHole::LEDBlackHole(int side, unsigned seed)
    : side_(std::max(side, 1)),
      r_horizon_(side_ * 0.08f),
      r_outer_(side_ * 0.45f),
      mass_(r_horizon_ / 2.0f),
      b_critical_(3.0f * std::sqrt(3.0f) * mass_),
      b_max_(r_outer_ * 1.5f),
      rng_(seed),
      nx_table_(static_cast<std::size_t>(side_) * 2u, 0.0f),
      nx2_table_(static_cast<std::size_t>(side_) * 2u, 0.0f),
      density_(kRadiusBins * kAngleBins, ftxui::Color::Default),
      occupied_(kRadiusBins * kAngleBins, false) {
  build_bend_table();

  const int particle_count = std::clamp(side_ * 3, kMinParticles, kMaxParticles);
  particles_.resize(static_cast<std::size_t>(particle_count));

  std::uniform_real_distribution<float> angle_dist(0.0f, kTwoPi);
  std::uniform_real_distribution<float> radius_dist(r_horizon_, r_outer_);
  std::uniform_real_distribution<float> sparkle_dist(0.7f, 1.0f);
  for (Particle& p : particles_) {
    p.angle = angle_dist(rng_);
    p.radius = radius_dist(rng_);
    p.sparkle = sparkle_dist(rng_);
  }

  const int ergo_count = std::clamp(side_ * 2, 45, 85);
  ergo_particles_.resize(static_cast<std::size_t>(ergo_count));
  for (ErgosphereParticle& ep : ergo_particles_) {
    respawn(ep);
  }
}

void LEDBlackHole::build_bend_table() {
  bend_table_.resize(kBendTableSize);
  const float span = b_max_ - b_critical_;
  for (int i = 0; i < kBendTableSize; ++i) {
    const float t = (i + 0.5f) / kBendTableSize;
    const float b = b_critical_ + span * t * t;
    bend_table_[i] = integrate_bend(b, mass_, r_horizon_);
  }
}

void LEDBlackHole::set_disk_radius(float radius) {
  std::lock_guard<std::mutex> lock(mutex_);
  r_outer_ = std::clamp(radius, r_horizon_ * 1.5f, side_ * 1.5f);
  b_max_ = r_outer_ * 1.5f;
  build_bend_table();
  
  // Respawn particles so they cover the new disk area
  for (Particle& p : particles_) {
    respawn(p);
  }
}

void LEDBlackHole::respawn(Particle& p) {
  std::uniform_real_distribution<float> angle_dist(0.0f, kTwoPi);
  std::uniform_real_distribution<float> radius_dist(r_outer_ * 0.85f, r_outer_);
  std::uniform_real_distribution<float> sparkle_dist(0.7f, 1.0f);
  p.angle = angle_dist(rng_);
  p.radius = radius_dist(rng_);
  p.sparkle = sparkle_dist(rng_);
}

void LEDBlackHole::respawn(ErgosphereParticle& ep) {
  std::uniform_real_distribution<float> angle_dist(0.0f, kTwoPi);
  std::uniform_real_distribution<float> radius_dist(r_horizon_ * 1.05f, r_horizon_ * 1.45f);
  std::uniform_real_distribution<float> vel_dist(0.16f, 0.32f);
  std::uniform_real_distribution<float> sparkle_dist(0.6f, 1.0f);

  ep.angle = angle_dist(rng_);
  ep.radius = radius_dist(rng_);
  ep.angular_velocity = vel_dist(rng_);
  ep.sparkle = sparkle_dist(rng_);
}

int LEDBlackHole::density_index(float radius, float angle) const {
  const float r_frac = (radius - r_horizon_) / (r_outer_ - r_horizon_);
  const int radius_bin = std::clamp(static_cast<int>(r_frac * kRadiusBins), 0, kRadiusBins - 1);

  float a = std::fmod(angle, kTwoPi);
  if (a < 0.0f) a += kTwoPi;
  const int angle_bin = std::clamp(static_cast<int>(a / kTwoPi * kAngleBins), 0, kAngleBins - 1);

  return radius_bin * kAngleBins + angle_bin;
}

void LEDBlackHole::advance(ftxui::animation::Duration elapsed) {
  std::lock_guard<std::mutex> lock(mutex_);
  time_accumulator_ = std::min(time_accumulator_ + std::max(0.0f, elapsed.count()), 0.25f);
  while (time_accumulator_ >= kTickSeconds) {
    tick();
    time_accumulator_ -= kTickSeconds;
  }
}

void LEDBlackHole::tick() {
  phase_ += 0.06f;
  if (phase_ > kTwoPi * 100.0f) phase_ -= kTwoPi * 100.0f;

  std::uniform_real_distribution<float> jitter(-kRadiusJitter, kRadiusJitter);
  for (Particle& p : particles_) {
    p.angle += kOrbitalConstant / p.radius;
    p.radius += jitter(rng_) - kInwardDrift;
    if (p.radius <= r_horizon_) respawn(p);
    p.radius = std::min(p.radius, r_outer_);
  }

  std::uniform_real_distribution<float> ergo_jitter(-0.01f, 0.01f);
  for (ErgosphereParticle& ep : ergo_particles_) {
    ep.angle += ep.angular_velocity;
    ep.radius += ergo_jitter(rng_);
    ep.radius = std::clamp(ep.radius, r_horizon_ * 1.02f, r_horizon_ * 1.50f);
  }

  std::fill(occupied_.begin(), occupied_.end(), false);
  for (const Particle& p : particles_) {
    const float intensity = std::clamp(1.0f - (p.radius - r_horizon_) / (r_outer_ - r_horizon_), 0.0f, 1.0f) * p.sparkle;
    if (intensity < 0.03f) continue;

    const int idx = density_index(p.radius, p.angle);
    density_[idx] = ftxui::Color::White;
    occupied_[idx] = true;
  }
}

void LEDBlackHole::render(TFrameBuffer& matrix) {
  std::lock_guard<std::mutex> lock(mutex_);
  const int sub_side = side_ * 2;
  const float center = side_;

  const float tilt = std::clamp(tilt_squash_.load(), 0.05f, 1.0f);
  const float zoom = std::clamp(zoom_scale_.load(), 0.5f, 2.0f);
  const float rear_scale = std::clamp(rear_scale_.load(), 0.40f, 1.50f);

  const float inv_2_zoom = 1.0f / (2.0f * zoom);
  const float inv_tilt = 1.0f / tilt;
  const float r_outer_rear = r_outer_ * rear_scale;
  const float inv_r_span = 1.0f / (r_outer_ - r_horizon_);
  const float inv_r_span_rear = 1.0f / (r_outer_rear - r_horizon_);
  const float inv_b_rel_span = 1.0f / (b_max_ - b_critical_);
  const float inv_fold_scale = 1.0f / (0.5f * r_horizon_);

  int sx = 0;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
  const float32x4_t v_center = vdupq_n_f32(center);
  const float32x4_t v_inv_2_zoom = vdupq_n_f32(inv_2_zoom);

  for (; sx <= sub_side - 4; sx += 4) {
    const float32x4_t v_sx = {static_cast<float>(sx), static_cast<float>(sx + 1),
                              static_cast<float>(sx + 2), static_cast<float>(sx + 3)};
    const float32x4_t v_nx = vmulq_f32(vsubq_f32(v_sx, v_center), v_inv_2_zoom);
    const float32x4_t v_nx2 = vmulq_f32(v_nx, v_nx);

    vst1q_f32(&nx_table_[sx], v_nx);
    vst1q_f32(&nx2_table_[sx], v_nx2);
  }
#endif
  for (; sx < sub_side; ++sx) {
    const float nx = (sx - center) * inv_2_zoom;
    nx_table_[sx] = nx;
    nx2_table_[sx] = nx * nx;
  }

  const float b_max_sq_margin = b_max_ * b_max_ * 1.05f;

  for (int sy = 0; sy < sub_side; ++sy) {
    const float ny = (sy - center) * inv_2_zoom;
    const float ny2 = ny * ny;
    const float y_front = ny * inv_tilt;
    const float y_front2 = y_front * y_front;

    for (int sx = 0; sx < sub_side; ++sx) {
      const float nx = nx_table_[sx];
      const float nx2 = nx2_table_[sx];
      const float b2 = nx2 + ny2;

      if (b2 > b_max_sq_margin) continue;

      const float b = std::sqrt(b2);

      const float r_front = std::sqrt(nx2 + y_front2);
      const bool is_front_disk = (r_front >= r_horizon_ && r_front <= r_outer_) && (ny >= -0.1f * r_horizon_);

      if (is_front_disk) {
        const float theta = std::atan2(y_front, nx);

        const float omega_front = 1.5f / std::sqrt(r_front / r_horizon_);
        const float theta_swirl_front = theta - phase_ * omega_front;
        const int idx = density_index(r_front, theta_swirl_front);

        const float u_front = (r_front - r_horizon_) * inv_r_span;
        const float radial_norm = 1.0f - u_front;
        const float swirl_wave = 0.5f + 0.5f * std::cos(4.0f * theta_swirl_front + 6.0f * (r_front / r_outer_));
        const float active_norm = radial_norm * (0.65f + 0.35f * swirl_wave);

        if (occupied_[idx] || active_norm > 0.30f) {
          const float doppler = 1.0f - 0.4f * (nx / (r_front + 1e-4f));
          const float dynamic_doppler = doppler * (0.9f + 0.2f * std::sin(theta_swirl_front * 2.0f));
          const float effective_u = std::clamp(u_front + 0.15f * std::sin(theta_swirl_front * 3.0f + 2.0f * u_front), 0.0f, 1.0f);
          ftxui::Color point_color = color_for_disk(effective_u, dynamic_doppler);

          matrix.draw_point(sx, sy, point_color);
          continue;
        }
      }

      if (b <= b_critical_) continue;

      if (b > b_critical_ && b <= b_critical_ * 1.05f) {
        // ftxui::Color::White is a Palette16 enum constant; construct the Color
        // explicitly so draw_point's (int,int,bool) overload is not selected
        // over (int,int,Color).
        matrix.draw_point(sx, sy, ftxui::Color(ftxui::Color::White));
        continue;
      }

      const float b_rel = (b - b_critical_) * inv_b_rel_span;
      const int table_idx = std::clamp(static_cast<int>(std::sqrt(b_rel) * kBendTableSize), 0, kBendTableSize - 1);
      const float bend = bend_table_[table_idx];
      if (bend < 0.0f) continue;

      const float phi_screen = std::atan2(ny, nx);
      const float sin_phi = std::abs(std::sin(phi_screen));

      const float compression = 1.0f + lensing_strength_.load() * bend * (0.4f + 0.6f * sin_phi);
      const float exp_comp = 1.0f / std::max(compression, 0.1f);
      const float r_lensed = r_horizon_ + (r_outer_rear - r_horizon_) * (b_rel <= 0.0f ? 0.0f : std::exp(exp_comp * std::log(b_rel)));

      if (r_lensed >= r_horizon_ && r_lensed <= r_outer_rear) {
        const float x_tanh = ny * inv_fold_scale;
        const float x2_tanh = x_tanh * x_tanh;
        const float transition = std::clamp(x_tanh * (27.0f + x2_tanh) / (27.0f + 9.0f * x2_tanh), -1.0f, 1.0f);
        const float theta_disk = phi_screen - transition * bend;

        const float omega_lensed = 1.5f / std::sqrt(r_lensed / r_horizon_);
        const float theta_swirl_lensed = theta_disk - phase_ * omega_lensed;
        const int idx = density_index(r_lensed, theta_swirl_lensed);

        const float u_lensed = (r_lensed - r_horizon_) * inv_r_span_rear;
        const float radial_norm = 1.0f - u_lensed;
        const float swirl_wave = 0.5f + 0.5f * std::cos(4.0f * theta_swirl_lensed + 6.0f * (r_lensed / r_outer_));
        const float active_norm = radial_norm * (0.65f + 0.35f * swirl_wave);

        const float lensing_boost = 0.8f + 0.4f * sin_phi;
        const float effective_active = active_norm * lensing_boost;

        if (occupied_[idx] || effective_active > 0.30f) {
          const float doppler = 1.0f - 0.35f * (nx / (b + 1e-4f));
          const float dynamic_doppler = doppler * (0.9f + 0.2f * std::sin(theta_swirl_lensed * 2.0f));
          const float effective_u = std::clamp(u_lensed + 0.15f * std::sin(theta_swirl_lensed * 3.0f + 2.0f * u_lensed), 0.0f, 1.0f);
          ftxui::Color point_color = color_for_disk(effective_u, dynamic_doppler);

          matrix.draw_point(sx, sy, point_color);
        }
      }
    }
  }

  for (const ErgosphereParticle& ep : ergo_particles_) {
    const float e_x = center + ep.radius * std::cos(ep.angle) * 2.0f * zoom;
    const float e_y = center + ep.radius * std::sin(ep.angle) * 2.0f * zoom;

    const int sx = static_cast<int>(std::round(e_x));
    const int sy = static_cast<int>(std::round(e_y));

    if (sx >= 0 && sx < sub_side && sy >= 0 && sy < sub_side) {
      matrix.draw_point(sx, sy, color_for_ergosphere_particle(ep.sparkle));
    }
  }
}

}  // namespace ftxui::ext
