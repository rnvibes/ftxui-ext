#include "ftxui/ext/led_black_hole.h"
#include "ftxui/ext/frame_buffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace ftxui::ext {

namespace {

constexpr float kTwoPi = 6.283185307f;
constexpr float kPi = 3.14159265f;
constexpr float kTickSeconds = 0.08f;
constexpr float kInnerKelvin = 12000.0f;  // blue-white core at the ISCO
constexpr float kOuterKelvin = 2000.0f;   // orange-red rim
constexpr float kBigBend = 6.0f * kPi;

// Blackbody colour (Tanner Helland approximation). Evaluated only while the
// colour LUT is built, never per pixel.
void kelvin_to_rgb(float kelvin, std::uint8_t& out_r, std::uint8_t& out_g, std::uint8_t& out_b) {
  const float t = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
  float r = 255.0f;
  float g = 255.0f;
  float b = 255.0f;
  if (t <= 66.0f) {
    r = 255.0f;
    g = 99.4708025861f * std::log(std::max(t, 1.0f)) - 161.1195681661f;
    b = (t <= 19.0f) ? 0.0f
                     : 138.5177312231f * std::log(std::max(t - 10.0f, 1.0f)) - 305.0447927307f;
  } else {
    r = 329.698727446f * std::pow(std::max(t - 60.0f, 1.0f), -0.1332047592f);
    g = 288.1221695283f * std::pow(std::max(t - 60.0f, 1.0f), -0.0755148492f);
    b = 255.0f;
  }
  out_r = static_cast<std::uint8_t>(std::clamp(r, 0.0f, 255.0f));
  out_g = static_cast<std::uint8_t>(std::clamp(g, 0.0f, 255.0f));
  out_b = static_cast<std::uint8_t>(std::clamp(b, 0.0f, 255.0f));
}

// ---------------------------------------------------------------------------
// Exact equatorial Kerr null geodesic, integrated in an affine parameter.
//
// With u = 1/r and phi the Boyer-Lindquist azimuth, and
//   Q(u) = 1 + (a^2 - b^2) u^2 + 2 M (b - a)^2 u^3
//   D(u) = 1 - 2 M u + a^2 u^2
//   K(u) = b (1 - 2 M u) + 2 M a u
// the orbit obeys (du/dphi)^2 = Q D^2 / K^2. Integrating instead with
//   du/dtau = v,   dv/dtau = (Q D^2)'/2,   dphi/dtau = K
// removes the K -> 0 pole and lets v pass smoothly through the perihelion.
// For a = 0 this reduces to (du/dphi)^2 = 1/b^2 - u^2 + 2 M u^3, whose
// weak-field deflection is 4M/b and whose capture threshold is 3*sqrt(3) M.
// ---------------------------------------------------------------------------
struct RayTrace {
  bool captured = true;
  float bend = kBigBend;  // |phi_out| - pi for flyby rays
  float r_min = 0.0f;     // perihelion radius
  float r_far = -1.0f;    // radius at the far-side crossing (|phi| = pi)
};

RayTrace integrate_ray(float b, float mass, float a, float r_plus) {
  RayTrace out;
  const double M = mass;
  const double A = a;
  const double B = b;
  const double u_h = 1.0 / static_cast<double>(r_plus);
  auto accel = [&](double u, double& v_dot, double& phi_dot) {
    const double Q = 1.0 + (A * A - B * B) * u * u + 2.0 * M * (B - A) * (B - A) * u * u * u;
    const double D = 1.0 - 2.0 * M * u + A * A * u * u;
    const double Qp = 2.0 * (A * A - B * B) * u + 6.0 * M * (B - A) * (B - A) * u * u;
    const double Dp = -2.0 * M + 2.0 * A * A * u;
    v_dot = 0.5 * (Qp * D * D + 2.0 * Q * D * Dp);
    phi_dot = B * (1.0 - 2.0 * M * u) + 2.0 * M * A * u;
  };
  const double dt = 0.010 / std::max(1.0, std::abs(B));
  constexpr int kMaxSteps = 200000;
  constexpr double kPhiCap = 4.0 * kPi;
  double u = 0.0;
  double v = 1.0;
  double phi = 0.0;
  double u_max = 0.0;
  for (int i = 0; i < kMaxSteps; ++i) {
    double k1v = 0.0, k1p = 0.0;
    accel(u, k1v, k1p);
    const double k1u = v;
    double k2v = 0.0, k2p = 0.0;
    accel(u + 0.5 * dt * k1u, k2v, k2p);
    const double k2u = v + 0.5 * dt * k1v;
    double k3v = 0.0, k3p = 0.0;
    accel(u + 0.5 * dt * k2u, k3v, k3p);
    const double k3u = v + 0.5 * dt * k2v;
    double k4v = 0.0, k4p = 0.0;
    accel(u + dt * k3u, k4v, k4p);
    const double k4u = v + dt * k3v;

    const double u_prev = u;
    const double phi_prev = phi;
    const double du = dt / 6.0 * (k1u + 2.0 * k2u + 2.0 * k3u + k4u);
    const double dphi = dt / 6.0 * (k1p + 2.0 * k2p + 2.0 * k3p + k4p);
    u += du;
    v += dt / 6.0 * (k1v + 2.0 * k2v + 2.0 * k3v + k4v);
    phi += dphi;
    u_max = std::max(u_max, u);
    if (out.r_far < 0.0f) {
      const double ap = std::abs(phi_prev);
      const double an = std::abs(phi);
      if (ap < kPi && an >= kPi) {
        const double f = (kPi - ap) / std::max(an - ap, 1e-12);
        const double u_far = u_prev + f * (u - u_prev);
        out.r_far = static_cast<float>(1.0 / std::max(u_far, 1e-9));
      }
    }

    const double D = 1.0 - 2.0 * M * u + A * A * u * u;
    if (u >= u_h || D <= 1e-4) {
      out.captured = true;
      return out;
    }
    if (u < 0.0) {
      // Interpolate the crossing back to u = 0 to remove the last-step bias.
      const double frac = (u_prev > 0.0) ? std::clamp(u_prev / (u_prev - u), 0.0, 1.0) : 0.0;
      const double phi_out = phi_prev + frac * dphi;
      out.captured = false;
      out.bend = static_cast<float>(std::max(0.0, std::abs(phi_out) - kPi));
      out.r_min = static_cast<float>(1.0 / std::max(u_max, 1e-9));
      return out;
    }
    if (std::abs(phi) > kPhiCap) {
      break;  // ~3 windings: inside the photon ring
    }
  }
  out.captured = true;
  return out;
}

}  // namespace

LEDBlackHole::LEDBlackHole(int side, unsigned seed)
    : side_(std::max(side, 1)),
      r_horizon_(side_ * 0.08f),
      r_outer_(side_ * 0.45f),
      mass_(r_horizon_ / 2.0f),
      b_max_(r_outer_ * 1.5f),
      rng_(seed) {
  build_bend_tables();
  build_disk_tables();
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
  // Prograde equatorial critical impact parameter.
  const float a_star = std::clamp(spin_.load(), -0.99f, 0.99f);
  // r_ph_prograde = 2 * M * (1 + cos(2/3 * acos(-a_star)))
  const float r_ph = 2.0f * mass_ * (1.0f + std::cos((2.0f / 3.0f) * std::acos(-a_star)));
  const float a_dim = a_star * mass_;
  if (std::abs(a_star) < 1e-4f) {
    return 3.0f * std::sqrt(3.0f) * mass_;
  }
  const float b_crit = -((r_ph * r_ph * r_ph - 3.0f * mass_ * r_ph * r_ph + a_dim * a_dim * (r_ph + mass_)) /
                         (a_dim * (r_ph - mass_)));
  return std::abs(b_crit);
}

float LEDBlackHole::b_critical_retrograde() const {
  // Retrograde equatorial critical impact parameter.
  const float a_star = std::clamp(spin_.load(), -0.99f, 0.99f);
  const float r_ph = 2.0f * mass_ * (1.0f + std::cos((2.0f / 3.0f) * std::acos(a_star)));
  const float a_dim = a_star * mass_;
  if (std::abs(a_star) < 1e-4f) {
    return 3.0f * std::sqrt(3.0f) * mass_;
  }
  const float b_crit = -((r_ph * r_ph * r_ph - 3.0f * mass_ * r_ph * r_ph + a_dim * a_dim * (r_ph + mass_)) /
                         (a_dim * (r_ph - mass_)));
  return std::abs(b_crit);
}

// Innermost stable circular orbit (Bardeen-Press-Teukolsky). a = 0 -> 6M.
float LEDBlackHole::r_isco() const {
  const float a = std::clamp(spin_.load(), -0.99f, 0.99f);
  const float a2 = a * a;
  const float z1 =
      1.0f + std::cbrt(1.0f - a2) * (std::cbrt(1.0f + a) + std::cbrt(1.0f - a));
  const float z2 = std::sqrt(3.0f * a2 + z1 * z1);
  const float inner = std::max(0.0f, (3.0f - z1) * (3.0f + z1 + 2.0f * z2));
  return mass_ * (3.0f + z2 - std::sqrt(inner));
}

float LEDBlackHole::deflection(float b_impact) const {
  const float r_plus = r_outer_horizon();
  return integrate_ray(b_impact, mass_, spin_.load() * mass_, r_plus).bend;
}

float LEDBlackHole::isco_radius() const { return r_isco(); }

float LEDBlackHole::shadow_radius(float azimuth) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (shadow_r_.empty()) {
    build_shadow_table();
  }
  float psi = std::fmod(azimuth, kTwoPi);
  if (psi < 0.0f) psi += kTwoPi;
  const int si = std::clamp(static_cast<int>(psi * (kShadowBins / kTwoPi)), 0, kShadowBins - 1);
  return shadow_r_[si];
}

float LEDBlackHole::redshift_factor(float r, float b_impact) const {
  const float M = mass_;
  const float a = spin_.load() * mass_;
  const float sqrt_m = std::sqrt(M);
  const float omega = sqrt_m / (r * std::sqrt(r) + a * sqrt_m);
  const float arg = 1.0f - 2.0f * M / r + 4.0f * M * a * omega / r -
                    omega * omega * (r * r + a * a + 2.0f * M * a * a / r);
  const float ut = 1.0f / std::sqrt(std::max(arg, 1e-3f));
  if (!rel_beam_.load()) {
    return 1.0f / ut;  // no Doppler beaming
  }
  float denom = 1.0f - omega * b_impact;
  if (std::abs(denom) < 1e-3f) denom = (denom < 0.0f) ? -1e-3f : 1e-3f;
  return 1.0f / (ut * denom);
}

void LEDBlackHole::set_disk_color(int r, int g, int b) {
  color_r_.store(std::clamp(r, 0, 255));
  color_g_.store(std::clamp(g, 0, 255));
  color_b_.store(std::clamp(b, 0, 255));
  color_on_.store(true);
}

void LEDBlackHole::set_orbit(float radians) {
  float a = std::fmod(radians, kTwoPi);
  if (a > kPi) a -= kTwoPi;
  if (a < -kPi) a += kTwoPi;
  orbit_.store(a);
}

void LEDBlackHole::set_spin(float spin) {
  std::lock_guard<std::mutex> lock(mutex_);
  spin_.store(std::clamp(spin, -0.99f, 0.99f));
  build_bend_tables();
  build_disk_tables();
  view_dirty_ = true;
}

void LEDBlackHole::build_bend_tables() {
  bend_pro_.resize(kBendTableSize);
  bend_ret_.resize(kBendTableSize);
  rmin_pro_.resize(kBendTableSize);
  rmin_ret_.resize(kBendTableSize);
  rfar_pro_.resize(kBendTableSize);
  rfar_ret_.resize(kBendTableSize);

  const float r_plus = r_outer_horizon();
  const float a_dim = spin_.load() * mass_;
  const float b_crit_pro = b_critical_prograde();
  const float b_crit_ret = b_critical_retrograde();
  const float span_pro = std::max(b_max_ - b_crit_pro, 1e-3f);
  const float span_ret = std::max(b_max_ - b_crit_ret, 1e-3f);

  for (int i = 0; i < kBendTableSize; ++i) {
    const float t = (i + 0.5f) / kBendTableSize;
    const float b_pro = b_crit_pro + span_pro * t * t;
    const RayTrace rp = integrate_ray(b_pro, mass_, a_dim, r_plus);
    bend_pro_[i] = rp.captured ? kBigBend : rp.bend;
    rmin_pro_[i] = rp.captured ? r_plus : rp.r_min;
    rfar_pro_[i] = rp.captured ? -1.0f : rp.r_far;

    const float b_ret = b_crit_ret + span_ret * t * t;
    const RayTrace rr = integrate_ray(-b_ret, mass_, a_dim, r_plus);
    bend_ret_[i] = rr.captured ? kBigBend : rr.bend;
    rmin_ret_[i] = rr.captured ? r_plus : rr.r_min;
    rfar_ret_[i] = rr.captured ? -1.0f : rr.r_far;
  }
}

// Kerr shadow boundary in the observer's sky. For a spherical photon orbit at
// radius r the critical curve is (Bardeen 1973; alpha = -xi/sin i):
//   xi  = -(r^3 - 3Mr^2 + a^2 r + a^2 M) / (a (r - M))
//   eta = -r^3 (r^3 - 6Mr^2 + 9M^2 r - 4a^2 M) / (a^2 (r - M)^2)
//   alpha = -xi / sin i,  beta = +-sqrt(eta + a^2 cos^2 i - xi^2 cot^2 i)
// swept over r in [r_ph(retro), r_ph(pro)]. The curve is star-shaped about
// the origin, so a radius-vs-azimuth table is enough for a point test.
void LEDBlackHole::build_shadow_table() {
  const float M = mass_;
  const float a = spin_.load() * mass_;
  const float a_star = std::clamp(spin_.load(), -0.99f, 0.99f);
  const float schwarzschild = 3.0f * std::sqrt(3.0f) * M;
  shadow_r_.assign(kShadowBins, schwarzschild);

  const float tilt = std::clamp(tilt_squash_.load(), 0.05f, 1.0f);
  const float cos_i = tilt;
  const float sin_i = std::sqrt(std::max(1.0f - cos_i * cos_i, 0.0025f));
  const float cot_i = cos_i / sin_i;
  if (std::abs(a_star) < 0.02f) {
    return;  // Schwarzschild: circle of radius 3 sqrt(3) M
  }

  const float r_pro = 2.0f * M * (1.0f + std::cos((2.0f / 3.0f) * std::acos(std::clamp(-a_star, -1.0f, 1.0f))));
  const float r_ret = 2.0f * M * (1.0f + std::cos((2.0f / 3.0f) * std::acos(std::clamp(a_star, -1.0f, 1.0f))));

  const int kSamples = 1024;
  std::vector<float> th;
  std::vector<float> rad;
  th.reserve(kSamples + 1);
  rad.reserve(kSamples + 1);
  for (int i = 0; i <= kSamples; ++i) {
    const float r = r_ret + (r_pro - r_ret) * static_cast<float>(i) / kSamples;
    if (std::abs(r - M) < 1e-5f) continue;
    const float rm = r - M;
    const float xi = -((r * r * r - 3.0f * M * r * r + a * a * r + a * a * M) / (a * rm));
    const float eta = -(r * r * r) * (r * r * r - 6.0f * M * r * r + 9.0f * M * M * r - 4.0f * a * a * M) /
                      (a * a * rm * rm);
    const float arg = eta + a * a * cos_i * cos_i - xi * xi * cot_i * cot_i;
    if (arg < 0.0f) continue;  // this spherical orbit is not on the silhouette
    const float beta = std::sqrt(arg);
    const float alpha = -xi / sin_i;
    float theta = std::atan2(beta, alpha);
    if (theta < 0.0f) theta += kPi;
    if (!th.empty() && theta < th.back()) theta = th.back() + 1e-5f;  // guard monotonicity
    th.push_back(theta);
    rad.push_back(std::sqrt(alpha * alpha + beta * beta));
  }
  if (th.size() < 2) return;

  auto radius_at = [&](float t) {
    if (t <= th.front()) return rad.front();
    if (t >= th.back()) return rad.back();
    for (std::size_t j = 1; j < th.size(); ++j) {
      if (t <= th[j]) {
        const float f = (t - th[j - 1]) / std::max(th[j] - th[j - 1], 1e-6f);
        return rad[j - 1] + f * (rad[j] - rad[j - 1]);
      }
    }
    return rad.back();
  };

  for (int k = 0; k < kShadowBins; ++k) {
    const float psi = kTwoPi * (k + 0.5f) / kShadowBins;
    const float t = (psi <= kPi) ? psi : (kTwoPi - psi);  // beta -> -beta symmetry
    shadow_r_[k] = radius_at(t);
  }
}

void LEDBlackHole::build_disk_tables() {
  omega_lut_.resize(kDiskRadiusBins);
  ut_lut_.resize(kDiskRadiusBins);
  temp_lut_.resize(kDiskRadiusBins);
  emis_lut_.resize(kDiskRadiusBins);

  const float M = mass_;
  const float a = spin_.load() * mass_;
  const float sqrt_m = std::sqrt(M);
  const float r_in = r_isco();
  const float r_out = std::max(r_outer_, r_in + 1e-3f);

  for (int j = 0; j < kDiskRadiusBins; ++j) {
    const float r = r_in + (r_out - r_in) * (j + 0.5f) / kDiskRadiusBins;
    // Exact Kerr equatorial Keplerian frequency.
    const float omega = sqrt_m / (r * std::sqrt(r) + a * sqrt_m);
    // u^t for the circular orbit: 1/sqrt(-(g_tt + 2 Omega g_tphi + Omega^2 g_phiphi)).
    const float arg = 1.0f - 2.0f * M / r + 4.0f * M * a * omega / r -
                      omega * omega * (r * r + a * a + 2.0f * M * a * a / r);
    omega_lut_[j] = omega;
    ut_lut_[j] = 1.0f / std::sqrt(std::max(arg, 1e-3f));
    // Shakura-Sunyaev: T ~ r^(-3/4), peak at the ISCO.
    const float ratio = r_in / r;
    // Bright, warm display profile so the outer disk stays above the
    // terminal's visibility floor instead of fading to black.
    // Fiery natural palette mapped across the disk: blue-white core -> yellow
    // -> orange -> deep red rim.
    const float u = std::clamp((r - r_in) / std::max(r_out - r_in, 1e-3f), 0.0f, 1.0f);
    temp_lut_[j] = kInnerKelvin + (kOuterKelvin - kInnerKelvin) *
                                      std::exp(0.7f * std::log(u + 0.03f));
    emis_lut_[j] = 0.40f + 0.60f * ratio;
  }

  color_lut_.resize(kColorLutSize);
  color_tmin_ = temp_lut_[kDiskRadiusBins - 1];
  color_tmax_ = temp_lut_[0];
  for (int i = 0; i < kColorLutSize; ++i) {
    const float temp = color_tmin_ + (color_tmax_ - color_tmin_) * (i + 0.5f) / kColorLutSize;
    std::uint8_t r = 0, g = 0, b = 0;
    kelvin_to_rgb(temp, r, g, b);
    color_lut_[i] = {r, g, b};
  }
}

void LEDBlackHole::set_disk_radius(float radius) {
  std::lock_guard<std::mutex> lock(mutex_);
  r_outer_ = std::clamp(radius, r_outer_horizon() * 1.5f, side_ * 1.5f);
  b_max_ = r_outer_ * 1.5f;
  build_bend_tables();
  build_disk_tables();
  view_dirty_ = true;
}

void LEDBlackHole::rebuild_geometry() {
  const int sub_side = side_ * 2;
  const float center = side_;
  const float tilt = std::clamp(tilt_squash_.load(), 0.05f, 1.0f);
  const float zoom = std::clamp(zoom_scale_.load(), 0.5f, 2.0f);
  const float inv_2_zoom = 1.0f / (2.0f * zoom);
  const float inv_tilt = 1.0f / tilt;
  const float orbit = orbit_.load();
  const float cos_o = std::cos(orbit);
  const float sin_o = std::sin(orbit);

  geom_.assign(static_cast<std::size_t>(sub_side) * sub_side, ScreenPixel{});
  const float b_max_sq_margin = b_max_ * b_max_ * 1.05f;

  for (int sy = 0; sy < sub_side; ++sy) {
    const float ny0 = (sy - center) * inv_2_zoom;
    for (int sx = 0; sx < sub_side; ++sx) {
      const float nx0 = (sx - center) * inv_2_zoom;
      // Camera roll: rotate the screen plane so a horizontal drag spins the view.
      const float nx = cos_o * nx0 - sin_o * ny0;
      const float ny = sin_o * nx0 + cos_o * ny0;
      const float y_front = ny * inv_tilt;
      const float b2 = nx * nx + ny * ny;
      ScreenPixel& p = geom_[static_cast<std::size_t>(sy) * sub_side + sx];
      if (b2 > b_max_sq_margin) continue;
      p.nx = nx;
      p.b = std::sqrt(b2);
      p.phi = std::atan2(ny, nx);
      p.r_front = std::sqrt(nx * nx + y_front * y_front);
      p.theta = std::atan2(y_front, nx);
      p.ny = ny;
    }
  }
  geom_sub_side_ = sub_side;
  geom_zoom_ = zoom;
  geom_tilt_ = tilt;
  geom_orbit_ = orbit;
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

  pulse_phase_ += kTickSeconds * pulse_rate_.load() * kTwoPi;
  if (pulse_phase_ > kTwoPi * 1000.0f) pulse_phase_ -= kTwoPi * 1000.0f;
}

void LEDBlackHole::render(TFrameBuffer& matrix) {
  std::lock_guard<std::mutex> lock(mutex_);
  const int sub_side = side_ * 2;
  const float tilt = std::clamp(tilt_squash_.load(), 0.05f, 1.0f);
  const float zoom = std::clamp(zoom_scale_.load(), 0.5f, 2.0f);
  const float orbit = orbit_.load();
  if (geom_.size() != static_cast<std::size_t>(sub_side) * sub_side || geom_zoom_ != zoom ||
      geom_tilt_ != tilt || geom_orbit_ != orbit || view_dirty_) {
    rebuild_geometry();
    build_shadow_table();
    view_dirty_ = false;
  }

  const float r_in = r_isco();
  const float b_crit_pro = b_critical_prograde();
  const float b_crit_ret = b_critical_retrograde();
  const float span_pro = std::max(b_max_ - b_crit_pro, 1e-3f);
  const float span_ret = std::max(b_max_ - b_crit_ret, 1e-3f);
  const float inv_dr = 1.0f / std::max(r_outer_ - r_in, 1e-3f);
  const float inv_bins = static_cast<float>(kDiskRadiusBins);
  const float color_span = std::max(color_tmax_ - color_tmin_, 1e-3f);
  const bool rel_beam = rel_beam_.load();
  const float lens = std::clamp(lensing_strength_.load(), 0.0f, 1.0f);

  // Linear interpolation of the far-side transfer table (smoother than the
  // nearest-entry lookup the index alone would give).
  auto sample_rfar = [&](bool pro, float b) {
    const float bc = pro ? b_crit_pro : b_crit_ret;
    const float sp = pro ? span_pro : span_ret;
    const float br = std::clamp((b - bc) / sp, 0.0f, 1.0f);
    const float f = std::sqrt(br) * (kBendTableSize - 1);
    const int i0 = std::clamp(static_cast<int>(f), 0, kBendTableSize - 1);
    const int i1 = std::min(i0 + 1, kBendTableSize - 1);
    const float fr = f - static_cast<float>(i0);
    const float* tbl = pro ? rfar_pro_.data() : rfar_ret_.data();
    return tbl[i0] + fr * (tbl[i1] - tbl[i0]);
  };
  // Soft rim: fade over a couple of logical pixels instead of a hard cut.
  const float edge_w = std::max(0.10f * (r_outer_ - r_in), 1.0f);
  auto edge_at = [&](float r) {
    return std::min(std::clamp((r - r_in) / edge_w, 0.0f, 1.0f),
                    std::clamp((r_outer_ - r) / edge_w, 0.0f, 1.0f));
  };

  auto emit = [&](int x, int y, float r, float b_signed, float boost, float theta, bool stipple) {
    const int j = std::clamp(static_cast<int>((r - r_in) * inv_dr * inv_bins), 0, kDiskRadiusBins - 1);
    const float ut = ut_lut_[j];
    float g = 0.0f;
    if (rel_beam) {
      float denom = 1.0f - omega_lut_[j] * b_signed;
      if (std::abs(denom) < 1e-3f) denom = (denom < 0.0f) ? -1e-3f : 1e-3f;
      g = 1.0f / (ut * denom);  // relativistic redshift + Doppler beaming
    } else {
      g = 1.0f / ut;  // gravitational/transverse redshift only
    }
    g = std::clamp(g, 0.05f, 20.0f);

    // Pulsating accretion: spiral hot spots advected with the flow, riding a
    // slow global "ignition" beat. pulse ~ [0.2, 1.3].
    float pulse = 1.0f;
    if (pulsing_) {
      const float lump = 0.5f + 0.5f * std::sin(pulse_phase_ - 4.0f * theta + (r - r_in) * 1.6f);
      const float beat = 0.75f + 0.25f * std::sin(pulse_phase_ * 0.7f);
      pulse = (0.45f + 0.85f * lump) * beat;
    }

    const float temp = temp_lut_[j] * g * (pulsing_ ? (0.90f + 0.20f * pulse) : 1.0f);
    const int ci = std::clamp(
        static_cast<int>((temp - color_tmin_) / color_span * (kColorLutSize - 1)), 0, kColorLutSize - 1);
    const float g2 = g * g;
    const float intensity = emis_lut_[j] * g2 * g2 * boost * pulse;  // beaming g^4 * flux * pulse
    const float s = std::sqrt(std::sqrt(std::clamp(intensity, 0.0f, 1.0f)));
    // Mix of the smooth ("smeared") sheet and advected particles: the sheet is
    // drawn at (1 - mix) and the hash adds bright particles on top, so both are
    // visible together. mix = 0 -> pure smear, 1 -> pure particles.
    float sparkle = 1.0f;
    if (stipple) {
      const float mix = disk_mix_.load();
      if (mix > 0.0f) {
        sparkle = 1.0f - 0.45f * mix;  // keep the fiery sheet visible
        const float aphi = theta + phase_ * (0.5f + 18.0f * omega_lut_[j]);
        const int gi = static_cast<int>(std::floor(r * 2.5f));
        const int gj = static_cast<int>(std::floor(aphi * 6.0f));
        std::uint32_t hsh = static_cast<std::uint32_t>(gi) * 73856093u ^
                            static_cast<std::uint32_t>(gj) * 19349663u;
        hsh ^= hsh >> 13;
        hsh *= 0x85ebca6bu;
        hsh ^= hsh >> 16;
        if ((hsh & 3u) == 0u) {  // ~25% of cells carry a particle
          sparkle += mix * 1.8f * (0.55f + static_cast<float>(hsh % 100u) * 0.009f);
        }
      }
    }
    const std::array<std::uint8_t, 3>& c = color_lut_[ci];
    // Clamp before the cast: an out-of-range float -> uint8_t conversion is UB
    // (that is what produced stray green/cyan sparkles).
    const auto to8 = [](float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f)); };
    std::uint8_t rr = to8(c[0] * s * sparkle);
    std::uint8_t gg = to8(c[1] * s * sparkle);
    std::uint8_t bb = to8(c[2] * s * sparkle);
    if (color_on_.load()) {
      const float sb = std::min(s * sparkle * 1.5f, 1.0f);
      rr = to8(static_cast<float>(color_r_.load()) * sb);
      gg = to8(static_cast<float>(color_g_.load()) * sb);
      bb = to8(static_cast<float>(color_b_.load()) * sb);
    }
    matrix.draw_point(x, y, ftxui::Color::RGB(rr, gg, bb));
  };

  for (int sy = 0; sy < sub_side; ++sy) {
    for (int sx = 0; sx < sub_side; ++sx) {
      const ScreenPixel& p = geom_[static_cast<std::size_t>(sy) * sub_side + sx];
      if (p.b < 0.0f) continue;

      float psi = p.phi;
      if (psi < 0.0f) psi += kTwoPi;
      const int si = std::clamp(static_cast<int>(psi * (kShadowBins / kTwoPi)), 0, kShadowBins - 1);
      const float shadow = shadow_r_[si];
      const bool prograde = p.nx < 0.0f;  // xi > 0 side of the critical curve
      const float b_signed = prograde ? p.b : -p.b;

      // Photon ring: a thin band just OUTSIDE the shadow (never fill the
      // shadow interior -- it must stay black).
      if (p.b > shadow && p.b <= shadow * 1.04f) {
        emit(sx, sy, r_in, b_signed, 2.4f, p.theta, false);
        continue;
      }

      // Near side of the disk passes IN FRONT of the hole, so it is drawn even
      // across the shadow: this is the horizontal band.
      if (p.ny >= 0.0f && p.r_front >= r_in && p.r_front <= r_outer_) {
        emit(sx, sy, p.r_front, b_signed, 1.15f * edge_at(p.r_front), p.theta, true);
        continue;
      }

      // Everything else is behind the hole and occluded by the shadow.
      if (p.b <= shadow) continue;

      // Far side, lensed over the top. Its emission radius is the geodesic
      // transfer r(|phi| = pi): the radius where the bent ray crosses the disk
      // plane on the far side. This is what produces the arc/swoop.
      if (p.ny < 0.0f) {
        // Blend the prograde/retrograde branches across the screen axis so the
        // arc has no seam, and fade its rim instead of cutting it.
        const float blend = std::clamp(0.5f - p.nx * 0.5f, 0.0f, 1.0f);
        const float r_lensed =
            blend * sample_rfar(true, p.b) + (1.0f - blend) * sample_rfar(false, p.b);
        // lensing strength: blend the tucked-in flat image with the geodesic arch
        const float r_far = lens * r_lensed + (1.0f - lens) * p.r_front;
        if (r_far >= r_in && r_far <= r_outer_) {
          emit(sx, sy, r_far, b_signed, 1.35f * edge_at(r_far), p.theta + kPi, true);
        }
      }
    }
  }

}

}  // namespace ftxui::ext
