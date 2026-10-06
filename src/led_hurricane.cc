#include "ftxui/ext/led_hurricane.h"
#include "ftxui/ext/frame_buffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace ftxui::ext {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kTickSeconds = 0.05f;
constexpr float kAdvectPeriod = 120.0f; // Cross-fade period for sheared cloud turbulence

// Fast 3D vector math
struct V3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) {
  const float l = length(a);
  return l > 1e-6f ? a * (1.0f / l) : V3{0.0f, 0.0f, 1.0f};
}
inline V3 cross(V3 a, V3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// ---------------------------------------------------------------------------
// Hash & Procedural 3D Noise (Deterministic and Fast)
// ---------------------------------------------------------------------------
inline float fract(float v) { return v - std::floor(v); }

inline float hash13(float x, float y, float z) {
  x = fract(x * 0.1031f);
  y = fract(y * 0.1031f);
  z = fract(z * 0.1031f);
  const float d = x * (z + 31.32f) + y * (y + 31.32f) + z * (x + 31.32f);
  x += d;
  y += d;
  z += d;
  return fract((x + y) * z);
}

inline float vnoise(float x, float y, float z) {
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

inline float fast_fbm2(float x, float y, float z) {
  return 0.62f * vnoise(x, y, z) + 0.38f * vnoise(x * 2.05f + 1.23f, y * 2.05f + 5.67f, z * 2.05f + 2.89f);
}

inline float smoothstep(float edge0, float edge1, float x) {
  const float t = std::clamp((x - edge0) / std::max(edge1 - edge0, 1e-6f), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// Henyey-Greenstein forward-scattering phase function
inline float henyey_greenstein(float cos_theta, float g) {
  const float g2 = g * g;
  const float denom = 1.0f + g2 - 2.0f * g * cos_theta;
  return (1.0f - g2) / (4.0f * kPi * std::pow(std::max(denom, 1e-4f), 1.5f));
}

// ---------------------------------------------------------------------------
// Physical Hurricane Parameters & Geometry
// ---------------------------------------------------------------------------
constexpr float kHollandB = 1.55f;
constexpr float kRmaxNorm = 0.95f;   // Eyewall radius at sea level
constexpr float kStadiumSlope = 0.65f; // Eyewall outward flare with height
constexpr float kH_tropo = 1.0f;     // Troposphere scale height (norm)
constexpr float kR_domain = 4.8f;    // Bounding cylinder radius

// Fast Macro Density Evaluator (Eyewall + Spiral Rainbands + Inflow + Cirrus)
inline float eval_macro_density(float z, float r, float phi, float t,
                                HurricanePhase phase, float p_norm, float storm_height,
                                float max_radius, float inflow_spd) {
  float maturity = 1.0f;
  float eye_clarity = 1.0f;
  if (phase == HurricanePhase::Inflow) {
    maturity = 0.25f + 0.35f * smoothstep(0.0f, 0.25f, p_norm);
    eye_clarity = 0.10f;
  } else if (phase == HurricanePhase::Genesis) {
    maturity = 0.60f + 0.40f * smoothstep(0.25f, 0.50f, p_norm);
    eye_clarity = smoothstep(0.25f, 0.50f, p_norm);
  } else if (phase == HurricanePhase::Category5) {
    maturity = 1.0f;
    eye_clarity = 1.0f;
  } else {
    maturity = 1.0f - 0.70f * smoothstep(0.75f, 1.00f, p_norm);
    eye_clarity = 1.0f - 0.90f * smoothstep(0.75f, 1.00f, p_norm);
  }

  const float r_eyewall = kRmaxNorm * (1.0f + kStadiumSlope * z);

  // 1. Eyewall Ring density
  const float dr_eye = (r - r_eyewall) * 2.38f;
  const float eyewall_gauss = std::exp(-dr_eye * dr_eye);
  const float vertical_shape = std::sin(std::clamp(z / storm_height, 0.0f, 1.0f) * kPi);
  float dens_eyewall = eyewall_gauss * vertical_shape * (0.8f + 0.4f * maturity);

  // Clear eye core
  if (r < r_eyewall) {
    const float clear_factor = smoothstep(0.0f, r_eyewall * 0.85f, r);
    dens_eyewall *= (1.0f - eye_clarity * (1.0f - clear_factor));
  }

  // 2. Inflow feeder current & Logarithmic Spiral Rainbands
  const float time_val = t * inflow_spd;
  constexpr int kNumBands = 3;
  constexpr float kInvPitch = 1.0f / 0.32f;

  float spiral_sum = 0.0f;
  const float log_r = std::log(std::max(r / kRmaxNorm, 0.15f));
  const float rot_speed = 0.8f / std::sqrt(std::max(r, 0.5f));
  const float base_phase = phi + kInvPitch * log_r - rot_speed * time_val;

  for (int k = 0; k < kNumBands; ++k) {
    const float k_offset = static_cast<float>(k) * (kTwoPi / kNumBands);
    const float band_phase = base_phase + k_offset;
    const float band_val = 0.5f + 0.5f * std::cos(band_phase);
    const float band_profile = band_val * band_val * band_val * band_val;
    const float rad_env = smoothstep(r_eyewall * 0.9f, r_eyewall * 1.8f, r) *
                          smoothstep(max_radius, max_radius * 0.45f, r);
    spiral_sum += band_profile * rad_env;
  }

  const float dz_band = (z - 0.28f) * 4.545f;
  const float rainband_z = std::exp(-dz_band * dz_band);
  const float dens_bands = spiral_sum * rainband_z * 0.65f;

  // 3. Environmental Inflow Blanket
  const float inflow_inward = smoothstep(max_radius, kRmaxNorm * 1.2f, r);
  const float inflow_z = smoothstep(0.25f, 0.02f, z);
  const float dens_inflow = inflow_inward * inflow_z * 0.35f * (1.1f - 0.5f * maturity);

  // 4. Upper Outflow Cirrus Canopy
  const float cirrus_z = smoothstep(0.65f, 0.95f, z);
  const float cirrus_r = smoothstep(max_radius * 1.1f, kRmaxNorm * 0.8f, r);
  const float dens_cirrus = cirrus_z * cirrus_r * 0.45f * maturity;

  return dens_eyewall + dens_bands + dens_inflow + dens_cirrus;
}

}  // namespace

LEDHurricane::LEDHurricane(int side, unsigned seed)
    : side_(std::max(side, 1)),
      max_radius_(kR_domain),
      storm_height_(kH_tropo),
      rng_(seed) {
  seed_offset_ = std::uniform_real_distribution<float>(0.0f, 1000.0f)(rng_);
}

void LEDHurricane::set_orbit(float radians) {
  float a = std::fmod(radians, kTwoPi);
  if (a > kPi) a -= kTwoPi;
  if (a < -kPi) a += kTwoPi;
  orbit_.store(a);
}

HurricanePhase LEDHurricane::current_phase() const {
  const int override_p = phase_override_.load();
  if (override_p >= 0 && override_p <= 3) {
    return static_cast<HurricanePhase>(override_p);
  }
  const float p = life_phase_.load();
  if (p < 0.25f) return HurricanePhase::Inflow;
  if (p < 0.50f) return HurricanePhase::Genesis;
  if (p < 0.75f) return HurricanePhase::Category5;
  return HurricanePhase::Dissipation;
}

float LEDHurricane::central_pressure_hpa() const {
  // Pressure drops as cyclonic intensity builds up
  const float p = life_phase_.load();
  float cat_intensity = 0.0f;
  const int override_p = phase_override_.load();
  if (override_p >= 0) {
    if (override_p == 0) cat_intensity = 0.15f;
    else if (override_p == 1) cat_intensity = 0.60f;
    else if (override_p == 2) cat_intensity = 1.00f;
    else cat_intensity = 0.30f;
  } else {
    if (p < 0.25f) {
      cat_intensity = 0.10f + 0.30f * smoothstep(0.0f, 0.25f, p);
    } else if (p < 0.50f) {
      cat_intensity = 0.40f + 0.60f * smoothstep(0.25f, 0.50f, p);
    } else if (p < 0.75f) {
      cat_intensity = 1.00f;
    } else {
      cat_intensity = 1.00f - 0.90f * smoothstep(0.75f, 1.00f, p);
    }
  }
  cat_intensity *= intensity_.load();
  // Environmental pressure 1012 hPa -> Category 5 low 905 hPa
  return 1012.0f - cat_intensity * 107.0f;
}

float LEDHurricane::max_wind_speed_ms() const {
  // Holland peak wind relation v_max = sqrt(B * delta_p / (rho * e))
  const float delta_p_pa = (1012.0f - central_pressure_hpa()) * 100.0f;
  constexpr float rho_air = 1.15f;
  constexpr float e_base = 2.7182818f;
  return std::sqrt(std::max(0.0f, (kHollandB * delta_p_pa) / (rho_air * e_base)));
}

float LEDHurricane::eye_radius_km() const {
  // Normalized 1.0 = ~32 km eye radius
  return kRmaxNorm * 32.0f;
}

float LEDHurricane::tangential_velocity_at(float r_norm) const {
  const float r = std::max(r_norm, 1e-4f);
  const float v_max = max_wind_speed_ms();
  const float rm_over_r = kRmaxNorm / r;
  const float term = std::pow(rm_over_r, kHollandB) * std::exp(1.0f - std::pow(rm_over_r, kHollandB));
  return v_max * std::sqrt(std::max(0.0f, term));
}

bool LEDHurricane::is_lightning_active() const {
  return lightning_flash_intensity_ > 0.05f || !active_bolts_.empty();
}

void LEDHurricane::trigger_lightning() {
  std::lock_guard<std::mutex> lock(mutex_);
  trigger_lightning_locked();
}

void LEDHurricane::trigger_lightning_locked() {
  // Place bolt inside the convective eyewall ring
  std::uniform_real_distribution<float> angle_dist(0.0f, kTwoPi);
  std::uniform_real_distribution<float> jitter(-0.15f, 0.15f);
  const float ang = angle_dist(rng_);
  const float r_start = kRmaxNorm * 1.1f;

  detail::LightningBranch bolt;
  bolt.intensity = 1.0f;

  // Generate fractal branching path from cloud top to cloud base / ocean
  constexpr int kSegments = 7;
  float cur_x = r_start * std::cos(ang);
  float cur_y = r_start * std::sin(ang);
  float cur_z = 0.85f;

  bolt.nodes.push_back({cur_x, cur_y, cur_z});
  for (int i = 1; i <= kSegments; ++i) {
    const float t = static_cast<float>(i) / kSegments;
    cur_z = 0.85f * (1.0f - t) + 0.08f;
    cur_x += jitter(rng_) * 0.25f;
    cur_y += jitter(rng_) * 0.25f;
    bolt.nodes.push_back({cur_x, cur_y, cur_z});
  }

  active_bolts_.push_back(std::move(bolt));
  lightning_flash_intensity_ = 1.0f;
  lightning_flash_pos_ = {cur_x, cur_y, 0.5f};
}

void LEDHurricane::advance(ftxui::animation::Duration elapsed) {
  std::lock_guard<std::mutex> lock(mutex_);
  time_accumulator_ = std::min(time_accumulator_ + std::max(0.0f, elapsed.count()), 0.25f);
  while (time_accumulator_ >= kTickSeconds) {
    tick(kTickSeconds);
    time_accumulator_ -= kTickSeconds;
  }
}

void LEDHurricane::tick(float dt) {
  time_sec_ += dt;
  update_lifecycle(dt);
  update_lightning(dt);
}

void LEDHurricane::update_lifecycle(float dt) {
  const float speed = lifecycle_speed_.load();
  if (speed > 0.0f) {
    constexpr float kBaseCycleDuration = 50.0f; // 50 seconds for full 4-phase cycle
    float p = life_phase_.load() + (dt * speed) / kBaseCycleDuration;
    if (p >= 1.0f) p = std::fmod(p, 1.0f);
    life_phase_.store(p);
  }
}

void LEDHurricane::update_lightning(float dt) {
  // Decay global flash intensity
  if (lightning_flash_intensity_ > 0.0f) {
    lightning_flash_intensity_ = std::max(0.0f, lightning_flash_intensity_ - dt * 6.5f);
  }

  // Decay active bolts
  for (auto it = active_bolts_.begin(); it != active_bolts_.end();) {
    it->intensity -= dt * 7.5f;
    if (it->intensity <= 0.0f) {
      it = active_bolts_.erase(it);
    } else {
      ++it;
    }
  }

  // Automatic lightning strikes during Category 5 phase
  const HurricanePhase phase = current_phase();
  if (phase == HurricanePhase::Category5) {
    time_since_last_strike_ += dt;
    const float freq = lightning_freq_.load();
    if (freq > 0.0f) {
      const float mean_interval = 1.0f / freq;
      std::uniform_real_distribution<float> prob(0.0f, 1.0f);
      if (time_since_last_strike_ > 0.20f && prob(rng_) < (dt / mean_interval)) {
        trigger_lightning_locked();
        time_since_last_strike_ = 0.0f;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 3D Volumetric Cloud Density Evaluator
// ---------------------------------------------------------------------------
float LEDHurricane::cloud_density_at(float x, float y, float z) const {
  if (z <= 0.0f || z >= storm_height_) return 0.0f;
  const float r2 = x * x + y * y;
  if (r2 >= max_radius_ * max_radius_) return 0.0f;
  const float r = std::sqrt(r2);

  const HurricanePhase phase = current_phase();
  const float p_norm = life_phase_.load();
  const float phi = std::atan2(y, x);

  const float macro = eval_macro_density(z, r, phi, time_sec_, phase, p_norm,
                                         storm_height_, max_radius_, inflow_speed_.load());
  if (macro <= 0.005f) return 0.0f;

  const float t = time_sec_ * inflow_speed_.load();
  const float fa = fract(t / kAdvectPeriod);
  const float fb = fract(t / kAdvectPeriod + 0.5f);
  const float om = 0.75f / (r * std::sqrt(r) + 0.5f);
  const float aa = phi - om * fa * kAdvectPeriod;
  const float ab = phi - om * fb * kAdvectPeriod;

  const float na = fast_fbm2(std::cos(aa) * 2.8f, std::sin(aa) * 2.8f, r * 2.1f + z * 3.5f + seed_offset_);
  const float nb = fast_fbm2(std::cos(ab) * 2.8f, std::sin(ab) * 2.8f, r * 2.1f + z * 3.5f + seed_offset_ + 19.3f);
  const float wa = 1.0f - std::abs(2.0f * fa - 1.0f);
  const float wb = 1.0f - wa;
  const float turb = (wa * na + wb * nb) * (1.0f / std::sqrt(wa * wa + wb * wb));

  return std::clamp(macro * (0.50f + 0.80f * turb), 0.0f, 1.8f);
}

// ---------------------------------------------------------------------------
// High-Performance Volumetric Ray Marcher
// ---------------------------------------------------------------------------
void LEDHurricane::render(TFrameBuffer& matrix) {
  std::lock_guard<std::mutex> lock(mutex_);
  const int n = side_ * 2;
  const float tilt = tilt_.load();
  const float zoom = zoom_.load();
  const float orbit = orbit_.load();
  const bool mono = color_mode_.load() == ColorMode::Monochrome;

  const std::array<float, 8> key{time_sec_, life_phase_.load(), lightning_flash_intensity_,
                                 tilt, orbit, zoom, mono ? 1.0f : 0.0f, seed_offset_};
  if (frame_.empty() || key != render_key_ || frame_.size() != static_cast<std::size_t>(n) * n) {
    render_key_ = key;
    retrace(1);
  }

  // Draw into FTXUI matrix
  for (int y = 0; y < n; ++y) {
    for (int x = 0; x < n; ++x) {
      const auto& px = frame_[static_cast<std::size_t>(y) * n + x];
      if (px[3]) {
        matrix.draw_point(x, y, ftxui::Color::RGB(px[0], px[1], px[2]));
      }
    }
  }
}

void LEDHurricane::retrace(int /*ss*/) {
  const int n = side_ * 2;
  const float tilt = tilt_.load();
  const float zoom = zoom_.load();
  const float orbit = orbit_.load();
  const bool mono = color_mode_.load() == ColorMode::Monochrome;
  const float flash_mag = lightning_flash_intensity_;
  const V3 flash_pos = {lightning_flash_pos_[0], lightning_flash_pos_[1], lightning_flash_pos_[2]};
  const HurricanePhase phase = current_phase();
  const float p_norm = life_phase_.load();
  const float inflow_spd = inflow_speed_.load();
  const float cur_time = time_sec_;

  // 3D Camera Setup
  const float elev = std::asin(std::clamp(tilt, 0.05f, 1.0f));
  const float ce = std::cos(elev);
  const float se = std::sin(elev);
  const float cam_dist = 7.5f / zoom;

  const V3 target = {0.0f, 0.0f, 0.35f};
  const V3 cam_pos = {
      target.x + cam_dist * ce * std::sin(orbit),
      target.y - cam_dist * ce * std::cos(orbit),
      target.z + cam_dist * se
  };

  const V3 fwd = normalize(target - cam_pos);
  const V3 right = normalize(cross(fwd, V3{0.0f, 0.0f, 1.0f}));
  const V3 up = cross(right, fwd);
  const float fov_scale = 0.55f / zoom;

  // Sun directional light (Low-angle golden sunlight)
  constexpr V3 kSunDir = {-0.65f, 0.45f, 0.60f};
  const V3 sun_norm = normalize(kSunDir);

  if (frame_.size() != static_cast<std::size_t>(n) * n) {
    frame_.assign(static_cast<std::size_t>(n) * n, {0, 0, 0, 0});
  }

  auto process_rows = [&](int y_start, int y_end) {
    for (int y = y_start; y < y_end; ++y) {
      const float v = 1.0f - (y + 0.5f) / n * 2.0f;
      for (int x = 0; x < n; ++x) {
        const float u = (x + 0.5f) / n * 2.0f - 1.0f;
        const V3 ray_dir = normalize(fwd + right * (u * fov_scale) + up * (v * fov_scale));

        float t_min = 0.0f;
        float t_max = 50.0f;

        // Bounding z-planes
        if (std::abs(ray_dir.z) > 1e-5f) {
          const float t_z0 = (0.0f - cam_pos.z) / ray_dir.z;
          const float t_z1 = (storm_height_ - cam_pos.z) / ray_dir.z;
          t_min = std::max(t_min, std::min(t_z0, t_z1));
          t_max = std::min(t_max, std::max(t_z0, t_z1));
        }

        // Ray-cylinder intersection
        const float a_cyl = ray_dir.x * ray_dir.x + ray_dir.y * ray_dir.y;
        const float b_cyl = 2.0f * (cam_pos.x * ray_dir.x + cam_pos.y * ray_dir.y);
        const float c_cyl = cam_pos.x * cam_pos.x + cam_pos.y * cam_pos.y - max_radius_ * max_radius_;
        const float disc = b_cyl * b_cyl - 4.0f * a_cyl * c_cyl;
        if (disc >= 0.0f && a_cyl > 1e-6f) {
          const float sqrt_d = std::sqrt(disc);
          const float t_cyl0 = (-b_cyl - sqrt_d) / (2.0f * a_cyl);
          const float t_cyl1 = (-b_cyl + sqrt_d) / (2.0f * a_cyl);
          t_min = std::max(t_min, std::min(t_cyl0, t_cyl1));
          t_max = std::min(t_max, std::max(t_cyl0, t_cyl1));
        } else {
          t_min = 1.0f;
          t_max = -1.0f;
        }

        std::array<std::uint8_t, 4>& out = frame_[static_cast<std::size_t>(y) * n + x];
        if (t_max <= t_min || t_max < 0.0f) {
          out = {0, 0, 0, 0};
          continue;
        }

        t_min = std::max(0.0f, t_min);

        // Ray march integration
        constexpr int kSteps = 22;
        const float dt_step = (t_max - t_min) / kSteps;
        float transmittance = 1.0f;
        float col[3] = {0.0f, 0.0f, 0.0f};

        const float cos_sun = dot(ray_dir, sun_norm);
        const float hg_phase = henyey_greenstein(cos_sun, 0.72f);

        for (int i = 0; i < kSteps && transmittance > 0.02f; ++i) {
          const float dist = t_min + (i + 0.5f) * dt_step;
          const V3 p = cam_pos + ray_dir * dist;
          if (p.z <= 0.0f || p.z >= storm_height_) continue;

          const float r2 = p.x * p.x + p.y * p.y;
          if (r2 >= max_radius_ * max_radius_) continue;
          const float r = std::sqrt(r2);
          const float phi = std::atan2(p.y, p.x);

          const float macro = eval_macro_density(p.z, r, phi, cur_time, phase,
                                                 p_norm, storm_height_, max_radius_, inflow_spd);
          if (macro <= 0.005f) continue;

          // Fast 2-octave turbulence
          const float t = cur_time * inflow_spd;
          const float fa = fract(t / kAdvectPeriod);
          const float fb = fract(t / kAdvectPeriod + 0.5f);
          const float om = 0.75f / (r * std::sqrt(r) + 0.5f);
          const float aa = phi - om * fa * kAdvectPeriod;
          const float ab = phi - om * fb * kAdvectPeriod;

          const float na = fast_fbm2(std::cos(aa) * 2.8f, std::sin(aa) * 2.8f, r * 2.1f + p.z * 3.5f + seed_offset_);
          const float nb = fast_fbm2(std::cos(ab) * 2.8f, std::sin(ab) * 2.8f, r * 2.1f + p.z * 3.5f + seed_offset_ + 19.3f);
          const float wa = 1.0f - std::abs(2.0f * fa - 1.0f);
          const float wb = 1.0f - wa;
          const float turb = (wa * na + wb * nb) * (1.0f / std::sqrt(wa * wa + wb * wb));

          const float rho = std::clamp(macro * (0.50f + 0.80f * turb), 0.0f, 1.8f);

          // Extinction along step
          const float sigma_t = 1.8f * rho;
          const float step_tau = sigma_t * dt_step;
          const float step_trans = std::exp(-step_tau);

          // Fast directional sun shadow (single macro sample toward sun)
          float sun_vis = 1.0f;
          const V3 sp = p + sun_norm * 0.38f;
          if (sp.z > 0.0f && sp.z < storm_height_) {
            const float sr2 = sp.x * sp.x + sp.y * sp.y;
            if (sr2 < max_radius_ * max_radius_) {
              const float sr = std::sqrt(sr2);
              const float s_phi = std::atan2(sp.y, sp.x);
              const float s_macro = eval_macro_density(sp.z, sr, s_phi, cur_time,
                                                       phase, p_norm, storm_height_, max_radius_, inflow_spd);
              sun_vis = std::exp(-s_macro * 0.80f);
            }
          }

          // Ambient lighting: cool sky dome + warm bounce
          const float amb = 0.25f + 0.20f * (p.z / storm_height_);
          const float direct = sun_vis * hg_phase * 3.8f;

          // Volumetric internal lightning illumination
          float lightning_glow = 0.0f;
          if (flash_mag > 0.01f) {
            const V3 delta = p - flash_pos;
            const float flash_dist_sq = dot(delta, delta);
            lightning_glow = (flash_mag * 5.5f) / (flash_dist_sq + 0.35f);
          }

          const float lum = (amb * 0.45f + direct + lightning_glow);

          float r_scat = lum * (0.92f + 0.08f * p.z);
          float g_scat = lum * (0.94f + 0.06f * p.z);
          float b_scat = lum * (1.02f);

          if (lightning_glow > 0.05f) {
            r_scat += lightning_glow * 0.60f;
            g_scat += lightning_glow * 0.85f;
            b_scat += lightning_glow * 1.35f;
          }

          const float absorb = (1.0f - step_trans);
          col[0] += transmittance * r_scat * absorb;
          col[1] += transmittance * g_scat * absorb;
          col[2] += transmittance * b_scat * absorb;
          transmittance *= step_trans;
        }

        // Ocean Floor Surface Interaction (z = 0)
        if (transmittance > 0.02f && cam_pos.z > 0.0f && ray_dir.z < -1e-4f) {
          const float t_sea = -cam_pos.z / ray_dir.z;
          if (t_sea > 0.0f) {
            const V3 sea_p = cam_pos + ray_dir * t_sea;
            const float r_sea2 = sea_p.x * sea_p.x + sea_p.y * sea_p.y;
            if (r_sea2 <= max_radius_ * max_radius_) {
              const float r_sea = std::sqrt(r_sea2);
              const float sea_phi = std::atan2(sea_p.y, sea_p.x);
              const float sea_macro = eval_macro_density(0.35f, r_sea, sea_phi,
                                                         cur_time, phase, p_norm, storm_height_,
                                                         max_radius_, inflow_spd);
              const float ocean_shadow = std::exp(-sea_macro * 0.9f);
              col[0] += transmittance * (0.04f * ocean_shadow);
              col[1] += transmittance * (0.09f * ocean_shadow);
              col[2] += transmittance * (0.18f * ocean_shadow);
              transmittance = 0.0f;
            }
          }
        }

        // Tone Mapping & Color Packaging (ACES filmic curve + sRGB gamma)
        float lin[3] = {col[0] * 1.6f, col[1] * 1.6f, col[2] * 1.6f};
        if (mono) {
          const float gray = 0.2126f * lin[0] + 0.7152f * lin[1] + 0.0722f * lin[2];
          lin[0] = lin[1] = lin[2] = gray;
        }

        int peak = 0;
        for (int ch = 0; ch < 3; ++ch) {
          const float v_val = lin[ch];
          const float tm = (v_val * (2.51f * v_val + 0.03f)) / (v_val * (2.43f * v_val + 0.59f) + 0.14f);
          const int q = static_cast<int>(std::lround(255.0f * std::pow(std::clamp(tm, 0.0f, 1.0f), 1.0f / 2.2f)));
          out[ch] = static_cast<std::uint8_t>(q);
          peak = std::max(peak, q);
        }
        out[3] = (peak >= 12 && (1.0f - transmittance) > 0.05f) ? 1 : 0;
      }
    }
  };

  const unsigned hw = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
  const int workers = (n < 64) ? 1 : static_cast<int>(hw);
  if (workers == 1) {
    process_rows(0, n);
  } else {
    std::vector<std::thread> pool;
    pool.reserve(workers);
    for (int i = 0; i < workers; ++i) {
      pool.emplace_back(process_rows, n * i / workers, n * (i + 1) / workers);
    }
    for (std::thread& t : pool) t.join();
  }

  trace_ss_ = 1;
  trace_zoom_ = zoom;
  trace_tilt_ = tilt;
  trace_orbit_ = orbit;
}

}  // namespace ftxui::ext
