#include "ftxui/ext/led_black_hole.h"
#include "ftxui/ext/frame_buffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace ftxui::ext {

namespace {

constexpr float kTwoPi = 6.283185307f;
constexpr float kPi = 3.14159265f;
constexpr float kTickSeconds = 0.08f;
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

// ---------------------------------------------------------------------------
// Kerr null geodesics in Kerr-Schild Cartesian coordinates, one per pixel.
//
// Units are G = c = M = 1 with the spin axis along +z. KS coordinates are
// Cartesian and regular everywhere outside the ring singularity: no polar axis
// to wrap around, no coordinate singularity at the horizon, and no per-region
// branch. Every pixel runs the same code, which is what makes the image
// continuous -- the shadow, photon ring, the far side of the disk lensed over
// the top, and the warped star field all come out of the one integration.
//
// The metric is g = eta + f l (x) l, so g^-1 = eta - f l (x) l, with
//   r^4 - (rho^2 - a^2) r^2 - a^2 z^2 = 0
//   f = 2 r^3 / (r^4 + a^2 z^2)
//   l = (1, (r x + a y)/(r^2 + a^2), (r y - a x)/(r^2 + a^2), z/r)
// and a photon obeys Hamilton's equations for H = g^{mu nu} p_mu p_nu / 2.
//
// Rays are traced backwards from the camera. A past-directed ray cannot cross
// the future horizon in these ingoing coordinates -- it piles up against it --
// so the ray is traced future-directed in the time-reversed spacetime, which is
// Kerr with the spin flipped. The two agree exactly on r and theta along the
// path, which is all the disk and the sky depend on.
//
// This mirrors lucia's src/lib/blackhole/geodesic.ts and its GLSL shader.
// ---------------------------------------------------------------------------
constexpr float kStepK = 0.07f;       // RK4 step as a fraction of r
constexpr int kMaxSteps = 400;
constexpr float kCamDist = 38.0f;     // M
constexpr float kTimeScale = 10.0f;   // M of animation per second
constexpr float kTimePeriod = 6000.0f;  // clock wrap; everything periodic divides it
constexpr float kGasPeriod = 150.0f;  // crossfade period of the sheared gas layers
constexpr float kPeakKelvin = 6600.0f;
// A terminal has no bloom to lift the dim receding side, and its cells sit on
// a dark but not black background, so expose brighter than the web view.
constexpr float kExposure = 2.0f;
constexpr float kLutTMin = 1000.0f;
constexpr float kLutTMax = 30000.0f;

struct V3 {
  float x, y, z;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

float ks_radius(V3 x, float a) {
  const float w = dot(x, x) - a * a;
  return std::sqrt(0.5f * (w + std::sqrt(w * w + 4.0f * a * a * x.z * x.z)));
}

// Hamilton's equations for a future-directed photon (p_t = -1) around spin a.
// `lens` scales f, the metric's deviation from flat space.
void derivs(V3 x, V3 p, float a, float lens, V3& dx, V3& dp) {
  const float a2 = a * a;
  const float z2 = x.z * x.z;
  const float w = dot(x, x) - a2;
  const float S = std::sqrt(w * w + 4.0f * a2 * z2);
  const float r2 = 0.5f * (w + S);
  const float r = std::sqrt(r2);
  const float sd = r2 + a2;

  const V3 l{(r * x.x + a * x.y) / sd, (r * x.y - a * x.x) / sd, x.z / r};
  const float Dn = r2 * r2 + a2 * z2;
  const float f = lens * 2.0f * r2 * r / Dn;
  const float L = 1.0f + dot(l, p);

  // dr/dx_i, from differentiating the quartic that defines r.
  const V3 rg{x.x * r / S, x.y * r / S, x.z * sd / (r * S)};
  const float c = lens * 2.0f * r2 / (Dn * Dn);
  const float k = 3.0f * a2 * z2 - r2 * r2;
  const V3 fg{c * rg.x * k, c * rg.y * k, c * (rg.z * k - 2.0f * a2 * r * x.z)};

  const float A = x.x * p.x + x.y * p.y;
  const float B = x.y * p.x - x.x * p.y;
  const float dLdr = (A * sd - 2.0f * r * (r * A + a * B)) / (sd * sd) - x.z * p.z / r2;
  const V3 Lg{(r * p.x - a * p.y) / sd + dLdr * rg.x, (r * p.y + a * p.x) / sd + dLdr * rg.y,
              p.z / r + dLdr * rg.z};

  const float fL = f * L;
  const float hL2 = 0.5f * L * L;
  dx = p + l * (-fL);
  dp = fg * hL2 + Lg * fL;
}

// Scale a unit direction so the momentum is null at x.
V3 null_momentum(V3 x, V3 d, float a, float lens) {
  const float r = ks_radius(x, a);
  const float sd = r * r + a * a;
  const V3 l{(r * x.x + a * x.y) / sd, (r * x.y - a * x.x) / sd, x.z / r};
  const float f = lens * 2.0f * r * r * r / (r * r * r * r + a * a * x.z * x.z);
  const float c = dot(l, d);
  const float q = 1.0f - f * c * c;
  return d * ((f * c + std::sqrt(f * f * c * c + q * (1.0f + f))) / q);
}

// RK4 along one ray from `x` towards `dir`, recording disk-plane crossings
// inside `r_disk` (in M). `spin` is the real hole's; integration runs at -spin.
detail::BlackHoleRay trace_ray(V3 x, V3 dir, float spin, float lens, float r_disk, int max_steps) {
  detail::BlackHoleRay out;
  const float a = -spin;
  const float r_stop = (1.0f + std::sqrt(std::max(0.0f, 1.0f - spin * spin))) * 1.01f;
  const float r_esc = std::sqrt(dot(x, x)) * 1.5f;
  V3 p = null_momentum(x, dir, a, lens);
  out.lz = -(x.x * p.y - x.y * p.x);  // conserved; the real photon's

  for (int i = 0; i < max_steps; ++i) {
    const float r = ks_radius(x, a);
    if (r < r_stop) return out;
    if (r > r_esc && dot(x, p) > 0.0f) {
      const float n = 1.0f / std::sqrt(dot(p, p));
      out.escaped = true;
      out.sky = {p.x * n, p.y * n, p.z * n};
      return out;
    }

    const float h = kStepK * r;
    V3 k1x, k1p, k2x, k2p, k3x, k3p, k4x, k4p;
    derivs(x, p, a, lens, k1x, k1p);
    derivs(x + k1x * (0.5f * h), p + k1p * (0.5f * h), a, lens, k2x, k2p);
    derivs(x + k2x * (0.5f * h), p + k2p * (0.5f * h), a, lens, k3x, k3p);
    derivs(x + k3x * h, p + k3p * h, a, lens, k4x, k4p);
    const V3 xn = x + (k1x + k2x * 2.0f + k3x * 2.0f + k4x) * (h / 6.0f);
    const V3 pn = p + (k1p + k2p * 2.0f + k3p * 2.0f + k4p) * (h / 6.0f);

    if (x.z * xn.z < 0.0f) {
      const float t = x.z / (x.z - xn.z);
      const float hx = x.x + t * (xn.x - x.x);
      const float hy = x.y + t * (xn.y - x.y);
      const float rh2 = hx * hx + hy * hy - a * a;
      if (rh2 > 0.0f) {
        const float rh = std::sqrt(rh2);
        if (rh < r_disk) {
          out.hit_r[out.hits] = rh;
          out.hit_phi[out.hits] = std::atan2(hy, hx);
          if (++out.hits == detail::BlackHoleRay::kMaxHits) return out;
        }
      }
    }
    x = xn;
    p = pn;
  }
  return out;
}

// ------------------------------------------------------------------- noise

float fract(float v) { return v - std::floor(v); }

float hash13(float x, float y, float z) {
  x = fract(x * 0.1031f);
  y = fract(y * 0.1031f);
  z = fract(z * 0.1031f);
  const float d = x * (z + 31.32f) + y * (y + 31.32f) + z * (x + 31.32f);
  x += d;
  y += d;
  z += d;
  return fract((x + y) * z);
}

std::array<float, 4> hash42(float px, float py) {
  float a = fract(px * 0.1031f);
  float b = fract(py * 0.1030f);
  float c = fract(px * 0.0973f);
  float d = fract(py * 0.1099f);
  const float s = a * (d + 33.33f) + b * (c + 33.33f) + c * (a + 33.33f) + d * (b + 33.33f);
  a += s;
  b += s;
  c += s;
  d += s;
  return {fract((a + b) * c), fract((a + c) * b), fract((b + c) * d), fract((c + d) * a)};
}

float vnoise(float x, float y, float z) {
  const float ix = std::floor(x), iy = std::floor(y), iz = std::floor(z);
  float fx = x - ix, fy = y - iy, fz = z - iz;
  fx = fx * fx * (3.0f - 2.0f * fx);
  fy = fy * fy * (3.0f - 2.0f * fy);
  fz = fz * fz * (3.0f - 2.0f * fz);
  auto lerp = [](float u, float v, float t) { return u + (v - u) * t; };
  auto h = [&](float dx, float dy, float dz) { return hash13(ix + dx, iy + dy, iz + dz); };
  return lerp(lerp(lerp(h(0, 0, 0), h(1, 0, 0), fx), lerp(h(0, 1, 0), h(1, 1, 0), fx), fy),
              lerp(lerp(h(0, 0, 1), h(1, 0, 1), fx), lerp(h(0, 1, 1), h(1, 1, 1), fx), fy), fz);
}

float fbm(float x, float y, float z) {
  float v = 0.0f;
  float amp = 0.5f;
  for (int i = 0; i < 4; ++i) {
    v += amp * vnoise(x, y, z);
    x = x * 2.03f + 1.7f;
    y = y * 2.03f + 9.2f;
    z = z * 2.03f + 3.1f;
    amp *= 0.5f;
  }
  return v;
}

// ------------------------------------------------------------------- disk

float omega_k(float r, float a) { return 1.0f / (r * std::sqrt(r) + a); }

// g = nu_obs / nu_emit for a prograde circular equatorial orbit, from the
// conserved E = 1 and Lz, so coordinate independent.
float disk_redshift(float r, float lz, float a, bool beaming) {
  const float sr = std::sqrt(r);
  const float ut = (r * sr + a) /
                   (std::sqrt(r * sr) * std::sqrt(std::max(r * sr - 3.0f * sr + 2.0f * a, 1e-4f)));
  return beaming ? 1.0f / (ut * (1.0f - omega_k(r, a) * lz)) : 1.0f / ut;
}

// Gas: noise advected at the Keplerian rate. Differential rotation shears any
// pattern without bound, so two layers restart half a period apart and
// crossfade -- each is only ever kGasPeriod old.
float gas(float r, float phi, float a, float t, float seed) {
  const float fa = fract(t / kGasPeriod);
  const float fb = fract(t / kGasPeriod + 0.5f);
  const float om = omega_k(r, a);
  const float aa = phi - om * fa * kGasPeriod;
  const float ab = phi - om * fb * kGasPeriod;
  const float na = fbm(std::cos(aa) * 3.0f, std::sin(aa) * 3.0f, r * 2.2f + seed);
  const float nb = fbm(std::cos(ab) * 3.0f, std::sin(ab) * 3.0f, r * 2.2f + seed + 17.0f);
  const float wa = 1.0f - std::abs(2.0f * fa - 1.0f);
  const float wb = 1.0f - wa;
  // Renormalise so contrast does not dip mid-fade.
  return 0.5f + (wa * (na - 0.5f) + wb * (nb - 0.5f)) / std::sqrt(wa * wa + wb * wb);
}

// Particles: bright clumps on circular orbits, at most one per cell of a polar
// grid that turns ring by ring. Each ring's rate is rounded to a whole number
// of turns per kTimePeriod, so the clock wrap is seamless. A clump stays well
// inside its ring radially; its trail reaches into the cell behind, so each
// sample also checks the cell ahead of it. Nothing is cut at a cell boundary.
float particles(float r, float phi, float a, float t, float seed) {
  constexpr float kDr = 0.5f;
  const float ring = std::floor(r / kDr);
  const float rc = (ring + 0.5f) * kDr;
  const float turns = std::floor(omega_k(rc, a) * kTimePeriod / kTwoPi + 0.5f);
  const float ang = phi - kTwoPi * turns * (t / kTimePeriod);
  const float cells = std::max(8.0f, std::floor(kTwoPi * rc / 1.4f));
  const float cell_a = kTwoPi / cells;
  const float u = ang / cell_a;
  const float base = std::floor(u);
  float sum = 0.0f;
  for (int k = 0; k < 2; ++k) {
    const float c = base + static_cast<float>(k);
    const auto h = hash42(ring + seed, c - cells * std::floor(c / cells));
    if (h[0] > 0.4f) continue;
    const float size = 0.6f + 0.8f * h[3];
    const float dr = r - (rc + (h[1] - 0.5f) * 0.2f);
    // Arc distance from the clump; positive is ahead along the orbit.
    const float da = (u - c - 0.5f - (h[2] - 0.5f) * 0.3f) * cell_a * rc;
    const float along = da / ((da > 0.0f ? 0.06f : 0.3f) * size);  // short head, long trail
    const float across = dr / (0.045f * size);
    sum += (0.3f + 2.2f * h[3] * h[3] * h[3]) * std::exp(-(along * along + across * across));
  }
  return sum;
}

}  // namespace

LEDBlackHole::LEDBlackHole(int side, unsigned seed)
    : side_(std::max(side, 1)),
      r_horizon_(side_ * 0.08f),
      r_outer_(side_ * 0.45f),
      mass_(r_horizon_ / 2.0f),
      rng_(seed) {
  seed_offset_ = std::uniform_real_distribution<float>(0.0f, 1000.0f)(rng_);
  build_color_lut();
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
  view_dirty_ = true;
}

bool LEDBlackHole::ray_captured(float b_impact) const {
  // The real photon moves along -y, so x0 < 0 gives Lz > 0: prograde.
  const float b = b_impact / mass_;
  const detail::BlackHoleRay ray =
      trace_ray({-b, -400.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, std::clamp(spin_.load(), -0.99f, 0.99f),
                1.0f, 0.0f, 20000);
  return !ray.escaped;
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

void LEDBlackHole::build_color_lut() {
  // Blackbody ramp over [kLutTMin, kLutTMax], linearised from the sRGB-ish
  // Helland fit so shading and tone mapping happen in linear light.
  color_lut_.resize(kColorLutSize);
  for (int i = 0; i < kColorLutSize; ++i) {
    const float k = kLutTMin + (kLutTMax - kLutTMin) * i / (kColorLutSize - 1);
    std::uint8_t r = 0, g = 0, b = 0;
    kelvin_to_rgb(k, r, g, b);
    color_lut_[i] = {std::pow(r / 255.0f, 2.2f), std::pow(g / 255.0f, 2.2f),
                     std::pow(b / 255.0f, 2.2f)};
  }
}

void LEDBlackHole::set_disk_radius(float radius) {
  std::lock_guard<std::mutex> lock(mutex_);
  r_outer_ = std::clamp(radius, r_outer_horizon() * 1.5f, side_ * 1.5f);
  view_dirty_ = true;
}

void LEDBlackHole::retrace(int ss) {
  const int n = side_ * 2;
  const int w = n * ss;
  rays_.assign(static_cast<std::size_t>(w) * w, detail::BlackHoleRay{});

  const float spin = std::clamp(spin_.load(), -0.99f, 0.99f);
  const float lens = std::clamp(lensing_strength_.load(), 0.0f, 1.0f);
  const float tilt = std::clamp(tilt_squash_.load(), 0.05f, 1.0f);
  const float zoom = std::clamp(zoom_scale_.load(), 0.5f, 2.0f);
  const float orbit = orbit_.load();
  const float r_disk = r_outer_ / mass_;

  // The camera sits in the y-z plane, `tilt` = sin(elevation) above the disk.
  // +x is always screen right, so there is no degenerate up vector face-on.
  const float elev = std::asin(tilt);
  const float ce = std::cos(elev);
  const float se = std::sin(elev);
  const V3 cam{0.0f, -kCamDist * ce, kCamDist * se};
  const V3 fwd{0.0f, ce, -se};
  const V3 right{1.0f, 0.0f, 0.0f};
  const V3 up{0.0f, se, ce};
  const float tan_half = 0.36f / zoom;
  const float co = std::cos(orbit);
  const float so = std::sin(orbit);

  auto rows = [&](int y0, int y1) {
    for (int y = y0; y < y1; ++y) {
      for (int x = 0; x < w; ++x) {
        const float u0 = (x + 0.5f) / w * 2.0f - 1.0f;
        const float v0 = 1.0f - (y + 0.5f) / w * 2.0f;  // screen y is down
        // Camera roll about the viewing axis.
        const float u = co * u0 - so * v0;
        const float v = so * u0 + co * v0;
        V3 d = fwd + (right * u + up * v) * tan_half;
        d = d * (1.0f / std::sqrt(dot(d, d)));
        rays_[static_cast<std::size_t>(y) * w + x] = trace_ray(cam, d, spin, lens, r_disk, kMaxSteps);
      }
    }
  };

  // Rows are independent, so split them across cores: a retrace happens on
  // every drag step and should not stall the UI.
  const unsigned hw = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
  const int workers = w * w < 1024 ? 1 : static_cast<int>(hw);
  if (workers == 1) {
    rows(0, w);
  } else {
    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (int i = 0; i < workers; ++i) {
      pool.emplace_back(rows, w * i / workers, w * (i + 1) / workers);
    }
    for (std::thread& t : pool) t.join();
  }

  trace_ss_ = ss;
  trace_zoom_ = zoom;
  trace_tilt_ = tilt;
  trace_orbit_ = orbit;
  trace_lensing_ = lens;
  trace_r_outer_ = r_outer_;
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
  const float rate = pulse_rate_.load();
  time_m_ = std::fmod(time_m_ + kTickSeconds * kTimeScale * rate, kTimePeriod);
  beat_phase_ = std::fmod(beat_phase_ + kTickSeconds * rate * 0.9f, kTwoPi);
}

void LEDBlackHole::render(TFrameBuffer& matrix) {
  std::lock_guard<std::mutex> lock(mutex_);
  const int n = side_ * 2;
  const float tilt = std::clamp(tilt_squash_.load(), 0.05f, 1.0f);
  const float zoom = std::clamp(zoom_scale_.load(), 0.5f, 2.0f);
  const float orbit = orbit_.load();
  const float lens = std::clamp(lensing_strength_.load(), 0.0f, 1.0f);
  // Supersample small (terminal-sized) views 2x2; large ones have pixels
  // small enough already, and the trace cost is per ray.
  const int full_ss = n <= 128 ? 2 : 1;
  const auto now = std::chrono::steady_clock::now();
  bool retraced = false;
  if (rays_.empty() || trace_zoom_ != zoom || trace_tilt_ != tilt || trace_orbit_ != orbit ||
      trace_lensing_ != lens || trace_r_outer_ != r_outer_ || view_dirty_) {
    // The first frame is traced in full; later changes are a drag in progress.
    const bool first = rays_.empty();
    retrace(first ? full_ss : 1);
    trace_coarse_ = !first && full_ss > 1;
    view_changed_at_ = now;
    build_shadow_table();
    view_dirty_ = false;
    retraced = true;
  } else if (trace_coarse_ && now - view_changed_at_ > std::chrono::milliseconds(120)) {
    retrace(full_ss);
    trace_coarse_ = false;
    retraced = true;
  }

  const float spin = std::clamp(spin_.load(), -0.99f, 0.99f);
  const bool beaming = rel_beam_.load();
  const float mix = disk_mix_.load();
  const bool pulsing = pulsing_.load();
  const bool override_on = color_on_.load();
  const bool mono = color_mode_.load() == ColorMode::Monochrome;
  const std::array<float, 10> key{time_m_,
                                  beaming ? 1.0f : 0.0f,
                                  mix,
                                  pulsing ? beat_phase_ : -1.0f,
                                  override_on ? 1.0f : 0.0f,
                                  static_cast<float>(color_r_.load()),
                                  static_cast<float>(color_g_.load()),
                                  static_cast<float>(color_b_.load()),
                                  mono ? 1.0f : 0.0f,
                                  seed_offset_};

  if (retraced || key != shade_key_ || frame_.size() != static_cast<std::size_t>(n) * n) {
    shade_key_ = key;
    frame_.assign(static_cast<std::size_t>(n) * n, {0, 0, 0, 0});

    const float r_in = r_isco() / mass_;
    const float r_out = r_outer_ / mass_;
    // Page-Thorne flux peaks at r = (49/36) r_in; normalise that to 1. It is
    // zero at the ISCO, so the inner edge fades rather than cuts.
    const float flux_norm = 7.0f * std::pow(49.0f / 36.0f, 3.0f) * r_in * r_in * r_in;
    const float t = time_m_;
    const float seed = seed_offset_;
    const float particle_gain = 2.0f * mix;
    const float beat = pulsing ? 0.88f + 0.12f * std::sin(beat_phase_) : 1.0f;
    const std::array<float, 3> tint{std::pow(color_r_.load() / 255.0f, 2.2f),
                                    std::pow(color_g_.load() / 255.0f, 2.2f),
                                    std::pow(color_b_.load() / 255.0f, 2.2f)};

    // Stars: about one grid cell per 3.5 pixels of sky, so each star is
    // resolved rather than aliased.
    const float pixel_angle = 2.0f * 0.36f / zoom / n;
    const float star_scale = 1.0f / (3.5f * pixel_angle);

    auto shade_disk = [&](float r, float phi, float lz, float rgb[3]) -> float {
      const float edge = [&] {
        auto ss = [](float e0, float e1, float v) {
          const float x = std::clamp((v - e0) / (e1 - e0), 0.0f, 1.0f);
          return x * x * (3.0f - 2.0f * x);
        };
        return ss(r_in * 0.98f, r_in * 1.12f, r) * ss(r_out, r_out * 0.72f, r);
      }();
      if (edge <= 0.0f) return 0.0f;
      const float flux = std::max(flux_norm * (1.0f - std::sqrt(r_in / r)) / (r * r * r), 0.0f);
      const float g = std::clamp(disk_redshift(r, lz, spin, beaming), 0.05f, 4.0f);
      const float dens = gas(r, phi, spin, t, seed);
      const float clump = particle_gain > 0.0f ? particle_gain * ext::particles(r, phi, spin, t, seed) : 0.0f;

      const float kelvin = kPeakKelvin * std::pow(flux, 0.25f) * g * (1.0f + 0.35f * clump);
      const float lu = std::clamp((kelvin - kLutTMin) / (kLutTMax - kLutTMin), 0.0f, 1.0f);
      const float fi = lu * (kColorLutSize - 1);
      const int i0 = std::min(static_cast<int>(fi), kColorLutSize - 2);
      const float fr = fi - i0;
      const auto& c0 = color_lut_[i0];
      const auto& c1 = color_lut_[i0 + 1];

      const float lum = flux * g * g * g * (0.25f + 1.4f * dens * dens + 3.5f * clump) * beat;
      const float cover = edge * std::clamp(0.35f + 0.75f * dens + clump, 0.0f, 0.96f);
      for (int ch = 0; ch < 3; ++ch) {
        const float bb = override_on ? tint[ch] : c0[ch] + fr * (c1[ch] - c0[ch]);
        rgb[ch] = bb * lum * cover * 2.2f;
      }
      return cover;
    };

    auto stars = [&](const std::array<float, 3>& d, float rgb[3]) {
      const float qx = d[0] * star_scale, qy = d[1] * star_scale, qz = d[2] * star_scale;
      const float ix = std::floor(qx), iy = std::floor(qy), iz = std::floor(qz);
      const float h = hash13(ix + seed, iy, iz);
      if (h < 0.93f) return;
      const float ox = hash13(ix + 1.3f, iy + seed, iz) - 0.5f;
      const float oy = hash13(ix, iy + 2.7f, iz + seed) - 0.5f;
      const float oz = hash13(ix + seed, iy, iz + 5.1f) - 0.5f;
      const float fx = qx - ix - 0.5f - ox * 0.5f;
      const float fy = qy - iy - 0.5f - oy * 0.5f;
      const float fz = qz - iz - 0.5f - oz * 0.5f;
      const float b = std::pow((h - 0.93f) / 0.07f, 3.0f) * 0.45f *
                      std::exp(-(fx * fx + fy * fy + fz * fz) * 18.0f);
      const float warm = hash13(ix, iy + 9.0f, iz);
      rgb[0] += b * (1.0f - 0.28f * warm);
      rgb[1] += b * (0.84f);
      rgb[2] += b * (0.62f + 0.38f * warm);
    };

    const int ss = trace_ss_;
    const int w = n * ss;
    const float inv_samples = 1.0f / (ss * ss);
    for (int y = 0; y < n; ++y) {
      for (int x = 0; x < n; ++x) {
        float acc[3] = {0.0f, 0.0f, 0.0f};
        for (int sy = 0; sy < ss; ++sy) {
          for (int sx = 0; sx < ss; ++sx) {
            const detail::BlackHoleRay& ray =
                rays_[static_cast<std::size_t>(y * ss + sy) * w + (x * ss + sx)];
            // Front-to-back: each crossing covers part of what lies behind.
            float col[3] = {0.0f, 0.0f, 0.0f};
            float cover = 0.0f;
            for (int k = 0; k < ray.hits && cover < 0.985f; ++k) {
              float e[3];
              const float a = shade_disk(ray.hit_r[k], ray.hit_phi[k], ray.lz, e);
              if (a <= 0.0f) continue;
              for (int ch = 0; ch < 3; ++ch) col[ch] += (1.0f - cover) * e[ch];
              cover += (1.0f - cover) * a;
            }
            if (ray.escaped && cover < 0.985f) {
              float s[3] = {0.0f, 0.0f, 0.0f};
              stars(ray.sky, s);
              for (int ch = 0; ch < 3; ++ch) col[ch] += (1.0f - cover) * s[ch];
            }
            for (int ch = 0; ch < 3; ++ch) acc[ch] += col[ch];
          }
        }

        float lin[3];
        for (int ch = 0; ch < 3; ++ch) lin[ch] = acc[ch] * inv_samples * kExposure;
        if (mono) {
          const float yl = 0.2126f * lin[0] + 0.7152f * lin[1] + 0.0722f * lin[2];
          lin[0] = lin[1] = lin[2] = yl;
        }
        // ACES fit, then display gamma.
        std::array<std::uint8_t, 4>& out = frame_[static_cast<std::size_t>(y) * n + x];
        int peak = 0;
        for (int ch = 0; ch < 3; ++ch) {
          const float v = lin[ch];
          const float tm = (v * (2.51f * v + 0.03f)) / (v * (2.43f * v + 0.59f) + 0.14f);
          const int q = static_cast<int>(
              std::lround(255.0f * std::pow(std::clamp(tm, 0.0f, 1.0f), 1.0f / 2.2f)));
          out[ch] = static_cast<std::uint8_t>(q);
          peak = std::max(peak, q);
        }
        // Near-black stays undrawn so the terminal background shows through,
        // and a Braille cell is not tinted by its darkest dot.
        out[3] = peak >= 14 ? 1 : 0;
      }
    }
  }

  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      const auto& px = frame_[static_cast<std::size_t>(y) * n + x];
      if (px[3]) matrix.draw_point(x, y, ftxui::Color::RGB(px[0], px[1], px[2]));
    }
  }
}

}  // namespace ftxui::ext
