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

float integrate_bend_kerr(float b_in, float mass, float a_spin, float r_plus) {
  // In the equatorial plane (theta = pi/2), null geodesics in Kerr metric satisfy:
  // (dr/dlambda)^2 = R(r) = (r^2 + a^2 - a*b)^2 - Delta*(b - a)^2
  // dphi/dlambda = -(a - b) + a*(r^2 + a^2 - a*b)/Delta = (b - a) + a*(2*M*r - a*(b - a))/Delta
  // With u = 1/r, du/dphi = (du/dlambda) / (dphi/dlambda) = -(1/r^2)(dr/dlambda) / (dphi/dlambda)
  // We integrate the second-order ODE d^2u / dphi^2 using standard 4th-order Runge-Kutta.
  // For stability and smoothness across prograde and retrograde impact parameters:
  const double M = static_cast<double>(mass);
  const double a = static_cast<double>(a_spin);
  const double b = static_cast<double>(b_in);
  const double u_horizon = 1.0 / static_cast<double>(r_plus);

  // Initial conditions at r -> infinity (u -> 0):
  // Impact parameter b = L/E. As r -> infinity, du/dphi = 1/b.
  double u = 0.0;
  double v = 1.0 / b;
  double phi = 0.0;
  constexpr double kDPhi = 0.0025;
  constexpr int kMaxSteps = 6000;

  // dv/dphi = -u + 3*M*u^2 + Kerr spin corrections:
  // High-order expansion in u of the equatorial Kerr null geodesic equation:
  // d^2u/dphi^2 = -u + 3*M*u^2 - 2*a*M*u^3/b - 3*a^2*u^3 + 5*M*a^2*u^4 ...
  // Full algebraic form for equatorial Kerr:
  const auto dv_dphi = [M, a, b](double uu) -> double {
    const double uu2 = uu * uu;
    // Leading Schwarzschild term:
    double res = -uu + 3.0 * M * uu2;
    // Frame-dragging / spin interaction term (depends on sign of b relative to a):
    const double inv_b = (std::abs(b) > 1e-6) ? (1.0 / b) : 0.0;
    res += -2.0 * a * M * uu2 * uu * inv_b - 3.0 * (a * a) * uu2 * uu + 5.0 * M * (a * a) * uu2 * uu2;
    return res;
  };

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

    if (u >= u_horizon) return -1.0f;  // Captured by event horizon
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
  std::uniform_real_distribution<float> radius_dist(r_outer_horizon(), r_outer_);
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

float LEDBlackHole::r_outer_horizon() const {
  const float a_dim = std::clamp(spin_.load(), -0.99f, 0.99f) * mass_;
  const float disc = std::max(0.0f, mass_ * mass_ - a_dim * a_dim);
  return mass_ + std::sqrt(disc);
}

float LEDBlackHole::r_inner_horizon() const {
  const float a_dim = std::clamp(spin_.load(), -0.99f, 0.99f) * mass_;
  const float disc = std::max(0.0f, mass_ * mass_ - a_dim * a_dim);
  return mass_ - std::sqrt(disc);
}

float LEDBlackHole::r_ergosphere(float theta) const {
  const float a_dim = std::clamp(spin_.load(), -0.99f, 0.99f) * mass_;
  const float cos_th = std::cos(theta);
  const float disc = std::max(0.0f, mass_ * mass_ - a_dim * a_dim * cos_th * cos_th);
  return mass_ + std::sqrt(disc);
}

float LEDBlackHole::b_critical_prograde() const {
  // Prograde equatorial critical impact parameter
  const float a_star = std::clamp(spin_.load(), -0.99f, 0.99f);
  // r_ph_prograde = 2 * M * (1 + cos(2/3 * acos(-a_star)))
  const float r_ph = 2.0f * mass_ * (1.0f + std::cos((2.0f / 3.0f) * std::acos(-a_star)));
  const float a_dim = a_star * mass_;
  // b_crit = (-(r_ph^3) + 3*M*r_ph^2 - a^2*(r_ph + M)) / (a*(r_ph - M))
  // Equivalently, b_crit = -(r_ph^2 + a^2) / a + (2*M*r_ph) / a when simplified:
  if (std::abs(a_star) < 1e-4f) {
    return 3.0f * std::sqrt(3.0f) * mass_;
  }
  const float b_crit = -((r_ph * r_ph * r_ph - 3.0f * mass_ * r_ph * r_ph + a_dim * a_dim * (r_ph + mass_)) /
                         (a_dim * (r_ph - mass_)));
  return std::abs(b_crit);
}

float LEDBlackHole::b_critical_retrograde() const {
  // Retrograde equatorial critical impact parameter
  const float a_star = std::clamp(spin_.load(), -0.99f, 0.99f);
  // r_ph_retro = 2 * M * (1 + cos(2/3 * acos(a_star)))
  const float r_ph = 2.0f * mass_ * (1.0f + std::cos((2.0f / 3.0f) * std::acos(a_star)));
  const float a_dim = a_star * mass_;
  if (std::abs(a_star) < 1e-4f) {
    return 3.0f * std::sqrt(3.0f) * mass_;
  }
  const float b_crit = -((r_ph * r_ph * r_ph - 3.0f * mass_ * r_ph * r_ph + a_dim * a_dim * (r_ph + mass_)) /
                         (a_dim * (r_ph - mass_)));
  return std::abs(b_crit);
}

void LEDBlackHole::set_spin(float spin) {
  std::lock_guard<std::mutex> lock(mutex_);
  spin_.store(std::clamp(spin, -0.99f, 0.99f));
  build_bend_table();
}

void LEDBlackHole::build_bend_table() {
  bend_table_prograde_.resize(kBendTableSize);
  bend_table_retrograde_.resize(kBendTableSize);

  const float r_plus = r_outer_horizon();
  const float a_dim = spin_.load() * mass_;
  const float b_crit_pro = b_critical_prograde();
  const float b_crit_ret = b_critical_retrograde();

  const float span_pro = b_max_ - b_crit_pro;
  const float span_ret = b_max_ - b_crit_ret;

  for (int i = 0; i < kBendTableSize; ++i) {
    const float t = (i + 0.5f) / kBendTableSize;
    // Prograde: b > 0
    const float b_pro = b_crit_pro + span_pro * t * t;
    bend_table_prograde_[i] = integrate_bend_kerr(b_pro, mass_, a_dim, r_plus);

    // Retrograde: b < 0
    const float b_ret = b_crit_ret + span_ret * t * t;
    bend_table_retrograde_[i] = integrate_bend_kerr(-b_ret, mass_, a_dim, r_plus);
  }
}

void LEDBlackHole::set_disk_radius(float radius) {
  std::lock_guard<std::mutex> lock(mutex_);
  r_outer_ = std::clamp(radius, r_outer_horizon() * 1.5f, side_ * 1.5f);
  b_max_ = r_outer_ * 1.5f;
  build_bend_table();
  
  // Respawn particles so they cover the new disk area
  for (Particle& p : particles_) {
    respawn(p);
  }
}

void LEDBlackHole::respawn(Particle& p) {
  const float r_plus = r_outer_horizon();
  std::uniform_real_distribution<float> angle_dist(0.0f, kTwoPi);
  std::uniform_real_distribution<float> radius_dist(std::max(r_plus, r_outer_ * 0.85f), r_outer_);
  std::uniform_real_distribution<float> sparkle_dist(0.7f, 1.0f);
  p.angle = angle_dist(rng_);
  p.radius = radius_dist(rng_);
  p.sparkle = sparkle_dist(rng_);
}

void LEDBlackHole::respawn(ErgosphereParticle& ep) {
  const float r_plus = r_outer_horizon();
  const float r_ergo = r_ergosphere(kPi * 0.5f);  // Equatorial ergosphere radius = 2*M
  const float r_min = r_plus * 1.01f;
  const float r_max = std::max(r_min + 0.01f * mass_, r_ergo * 1.05f);

  std::uniform_real_distribution<float> angle_dist(0.0f, kTwoPi);
  std::uniform_real_distribution<float> radius_dist(r_min, r_max);
  std::uniform_real_distribution<float> sparkle_dist(0.6f, 1.0f);

  ep.angle = angle_dist(rng_);
  ep.radius = radius_dist(rng_);
  // Frame-dragging angular velocity: omega = 2*M*a / (r^3 + a^2*r + 2*M*a^2)
  const float a_dim = spin_.load() * mass_;
  const float r = ep.radius;
  const float denom = r * r * r + a_dim * a_dim * r + 2.0f * mass_ * a_dim * a_dim;
  const float omega_drag = (denom > 1e-4f) ? (2.0f * mass_ * a_dim / denom) : 0.0f;
  // Combine frame-dragging with small thermal perturbation:
  std::uniform_real_distribution<float> vel_jitter(0.9f, 1.1f);
  ep.angular_velocity = omega_drag * vel_jitter(rng_) * 0.8f + 0.10f;
  ep.sparkle = sparkle_dist(rng_);
}

int LEDBlackHole::density_index(float radius, float angle) const {
  const float r_plus = r_outer_horizon();
  const float r_frac = (radius - r_plus) / std::max(r_outer_ - r_plus, 1e-3f);
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

  const float r_plus = r_outer_horizon();
  const float a_dim = spin_.load() * mass_;

  std::uniform_real_distribution<float> jitter(-kRadiusJitter, kRadiusJitter);
  for (Particle& p : particles_) {
    // Relativistic Keplerian frequency in Kerr metric: Omega = 1 / (r^(3/2)/sqrt(M) + a)
    const float r = std::max(p.radius, r_plus);
    const float omega_kerr = 1.0f / (std::pow(r, 1.5f) / std::sqrt(mass_) + a_dim + 1e-3f);
    p.angle += omega_kerr * 0.4f + kOrbitalConstant / r;
    p.radius += jitter(rng_) - kInwardDrift;
    if (p.radius <= r_plus) respawn(p);
    p.radius = std::min(p.radius, r_outer_);
  }

  const float r_ergo = r_ergosphere(kPi * 0.5f);
  std::uniform_real_distribution<float> ergo_jitter(-0.01f, 0.01f);
  for (ErgosphereParticle& ep : ergo_particles_) {
    const float r = ep.radius;
    const float denom = r * r * r + a_dim * a_dim * r + 2.0f * mass_ * a_dim * a_dim;
    const float omega_drag = (denom > 1e-4f) ? (2.0f * mass_ * a_dim / denom) : 0.0f;
    ep.angular_velocity = omega_drag * 0.8f + 0.10f;
    ep.angle += ep.angular_velocity;
    ep.radius += ergo_jitter(rng_);
    ep.radius = std::clamp(ep.radius, r_plus * 1.01f, std::max(r_plus * 1.02f, r_ergo * 1.05f));
  }

  std::fill(occupied_.begin(), occupied_.end(), false);
  for (const Particle& p : particles_) {
    const float intensity = std::clamp(1.0f - (p.radius - r_plus) / std::max(r_outer_ - r_plus, 1e-3f), 0.0f, 1.0f) * p.sparkle;
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
  const float r_plus = r_outer_horizon();
  const float r_outer_rear = r_outer_ * rear_scale;
  const float inv_r_span = 1.0f / std::max(r_outer_ - r_plus, 1e-3f);
  const float inv_r_span_rear = 1.0f / std::max(r_outer_rear - r_plus, 1e-3f);
  const float inv_fold_scale = 1.0f / (0.5f * r_plus);

  const float b_crit_pro = b_critical_prograde();
  const float b_crit_ret = b_critical_retrograde();
  const float inv_b_span_pro = 1.0f / std::max(b_max_ - b_crit_pro, 1e-3f);
  const float inv_b_span_ret = 1.0f / std::max(b_max_ - b_crit_ret, 1e-3f);

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
      const bool is_front_disk = (r_front >= r_plus && r_front <= r_outer_) && (ny >= -0.1f * r_plus);

      if (is_front_disk) {
        const float theta = std::atan2(y_front, nx);

        const float a_dim = spin_.load() * mass_;
        const float omega_front = 1.0f / (std::pow(r_front, 1.5f) / std::sqrt(mass_) + a_dim + 1e-3f);
        const float theta_swirl_front = theta - phase_ * omega_front * 1.5f;
        const int idx = density_index(r_front, theta_swirl_front);

        const float u_front = (r_front - r_plus) * inv_r_span;
        const float radial_norm = 1.0f - u_front;
        const float swirl_wave = 0.5f + 0.5f * std::cos(4.0f * theta_swirl_front + 6.0f * (r_front / r_outer_));
        const float active_norm = radial_norm * (0.65f + 0.35f * swirl_wave);

        if (occupied_[idx] || active_norm > 0.30f) {
          const float doppler = 1.0f - 0.45f * (nx / (r_front + 1e-4f));
          const float dynamic_doppler = doppler * (0.9f + 0.2f * std::sin(theta_swirl_front * 2.0f));
          const float effective_u = std::clamp(u_front + 0.15f * std::sin(theta_swirl_front * 3.0f + 2.0f * u_front), 0.0f, 1.0f);
          ftxui::Color point_color = color_for_disk(effective_u, dynamic_doppler);

          matrix.draw_point(sx, sy, point_color);
          continue;
        }
      }

      // In Kerr spacetime, prograde vs retrograde critical impact parameters create the asymmetric D-shaped shadow.
      // nx > 0 corresponds to prograde photon orbit side, nx < 0 to retrograde.
      const bool is_prograde = (nx >= 0.0f);
      const float b_critical = is_prograde ? b_crit_pro : b_crit_ret;

      if (b <= b_critical) continue;

      if (b > b_critical && b <= b_critical * 1.05f) {
        matrix.draw_point(sx, sy, ftxui::Color(ftxui::Color::White));
        continue;
      }

      const float inv_b_rel_span = is_prograde ? inv_b_span_pro : inv_b_span_ret;
      const float b_rel = (b - b_critical) * inv_b_rel_span;
      const int table_idx = std::clamp(static_cast<int>(std::sqrt(std::max(0.0f, b_rel)) * kBendTableSize), 0, kBendTableSize - 1);
      const float bend = is_prograde ? bend_table_prograde_[table_idx] : bend_table_retrograde_[table_idx];
      if (bend < 0.0f) continue;

      const float phi_screen = std::atan2(ny, nx);
      const float sin_phi = std::abs(std::sin(phi_screen));

      const float compression = 1.0f + lensing_strength_.load() * bend * (0.4f + 0.6f * sin_phi);
      const float exp_comp = 1.0f / std::max(compression, 0.1f);
      const float r_lensed = r_plus + (r_outer_rear - r_plus) * (b_rel <= 0.0f ? 0.0f : std::exp(exp_comp * std::log(b_rel)));

      if (r_lensed >= r_plus && r_lensed <= r_outer_rear) {
        const float x_tanh = ny * inv_fold_scale;
        const float x2_tanh = x_tanh * x_tanh;
        const float transition = std::clamp(x_tanh * (27.0f + x2_tanh) / (27.0f + 9.0f * x2_tanh), -1.0f, 1.0f);
        const float theta_disk = phi_screen - transition * bend;

        const float a_dim = spin_.load() * mass_;
        const float omega_lensed = 1.0f / (std::pow(r_lensed, 1.5f) / std::sqrt(mass_) + a_dim + 1e-3f);
        const float theta_swirl_lensed = theta_disk - phase_ * omega_lensed * 1.5f;
        const int idx = density_index(r_lensed, theta_swirl_lensed);

        const float u_lensed = (r_lensed - r_plus) * inv_r_span_rear;
        const float radial_norm = 1.0f - u_lensed;
        const float swirl_wave = 0.5f + 0.5f * std::cos(4.0f * theta_swirl_lensed + 6.0f * (r_lensed / r_outer_));
        const float active_norm = radial_norm * (0.65f + 0.35f * swirl_wave);

        const float lensing_boost = 0.8f + 0.4f * sin_phi;
        const float effective_active = active_norm * lensing_boost;

        if (occupied_[idx] || effective_active > 0.30f) {
          const float doppler = 1.0f - 0.40f * (nx / (b + 1e-4f));
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
