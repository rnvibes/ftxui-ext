#include "ftxui/ext/led_black_hole.h"
#include "ftxui/ext/frame_buffer.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace {

int g_passed = 0;
int g_failed = 0;

#define TEST_ASSERT(cond)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                     \
                << "]: " #cond << std::endl;                                   \
      ++g_failed;                                                              \
    } else {                                                                   \
      ++g_passed;                                                              \
    }                                                                          \
  } while (0)

#define TEST_CASE(name)                                                        \
  std::cout << "Running test: " << name << "..." << std::endl;

}  // namespace

void test_kerr_horizons_and_ergosphere() {
  TEST_CASE("Kerr horizons and ergosphere limits");

  // Side = 100 -> r_horizon_ = 8.0, mass = 4.0
  ftxui::ext::LEDBlackHole bh(100);

  // Default spin is 0.85
  TEST_ASSERT(std::abs(bh.spin() - 0.85f) < 1e-4f);

  // Set non-rotating (Schwarzschild limit: a -> 0)
  bh.set_spin(0.0f);
  float r_plus_0 = bh.r_outer_horizon();
  float r_minus_0 = bh.r_inner_horizon();
  float r_ergo_0 = bh.r_ergosphere(0.0f);

  // For a = 0, r_+ = 2M, r_- = 0
  // r_+ = M + M = 2M = 8.0f
  TEST_ASSERT(std::abs(r_plus_0 - 8.0f) < 1e-3f);
  TEST_ASSERT(std::abs(r_minus_0 - 0.0f) < 1e-3f);
  TEST_ASSERT(std::abs(r_ergo_0 - 8.0f) < 1e-3f);

  // Critical impact parameters at a = 0: both prograde and retrograde must equal 3*sqrt(3)*M
  float b_pro_0 = bh.b_critical_prograde();
  float b_ret_0 = bh.b_critical_retrograde();
  float b_schwarzschild = 3.0f * std::sqrt(3.0f) * 4.0f; // ~20.7846
  TEST_ASSERT(std::abs(b_pro_0 - b_schwarzschild) < 1e-2f);
  TEST_ASSERT(std::abs(b_ret_0 - b_schwarzschild) < 1e-2f);

  // Spin up to a = 0.8 * M
  bh.set_spin(0.80f);
  // M = 4.0, a = 0.8 * 4.0 = 3.2
  // r_+ = M + sqrt(M^2 - a^2) = 4.0 + sqrt(16.0 - 10.24) = 4.0 + sqrt(5.76) = 4.0 + 2.4 = 6.4
  // r_- = M - sqrt(M^2 - a^2) = 4.0 - 2.4 = 1.6
  TEST_ASSERT(std::abs(bh.r_outer_horizon() - 6.4f) < 1e-3f);
  TEST_ASSERT(std::abs(bh.r_inner_horizon() - 1.6f) < 1e-3f);

  // Horizon product relation r_+ * r_- = a^2
  float prod = bh.r_outer_horizon() * bh.r_inner_horizon();
  float a_dim = 0.80f * 4.0f;
  TEST_ASSERT(std::abs(prod - a_dim * a_dim) < 1e-3f);

  // Horizon sum relation r_+ + r_- = 2*M
  float sum = bh.r_outer_horizon() + bh.r_inner_horizon();
  TEST_ASSERT(std::abs(sum - 8.0f) < 1e-3f);

  // Ergosphere:
  // At pole (theta = 0): cos(theta) = 1, r_E(0) = M + sqrt(M^2 - a^2) = r_+ = 6.4
  TEST_ASSERT(std::abs(bh.r_ergosphere(0.0f) - 6.4f) < 1e-3f);
  // At equator (theta = pi/2): cos(theta) = 0, r_E(pi/2) = 2*M = 8.0
  constexpr float kHalfPi = 1.57079632679f;
  TEST_ASSERT(std::abs(bh.r_ergosphere(kHalfPi) - 8.0f) < 1e-3f);
}

void test_kerr_critical_impact_parameters() {
  TEST_CASE("Kerr prograde vs retrograde critical impact parameters");

  ftxui::ext::LEDBlackHole bh(100);
  bh.set_spin(0.95f);

  float b_pro = bh.b_critical_prograde();
  float b_ret = bh.b_critical_retrograde();

  // For a spinning black hole:
  // Prograde photons can approach much closer before capture (smaller b_crit)
  // Retrograde photons are repelled / captured earlier (larger b_crit)
  // b_pro < b_ret
  TEST_ASSERT(b_pro < b_ret);
  std::cout << "  Spin 0.95: b_prograde = " << b_pro << ", b_retrograde = " << b_ret << std::endl;
  TEST_ASSERT(b_pro > 0.0f);
  TEST_ASSERT(b_ret > 0.0f);
}

void test_render_and_advance() {
  TEST_CASE("Advance and render frame buffer execution");

  ftxui::ext::LEDBlackHole bh(30);
  bh.set_spin(0.90f);

  // Step the simulation through multiple ticks
  bh.advance(std::chrono::milliseconds(200));

  ftxui::ext::TFrameBuffer fb(30);
  bh.render(fb);

  // Element render should succeed and produce a valid DOM element
  auto elem = fb.render();
  TEST_ASSERT(elem != nullptr);
  std::cout << "  Rendered frame buffer to FTXUI element successfully." << std::endl;
}

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "Running Kerr Black Hole physics tests..." << std::endl;
  std::cout << "========================================" << std::endl;

  test_kerr_horizons_and_ergosphere();
  test_kerr_critical_impact_parameters();
  test_render_and_advance();

  std::cout << "\nResults: " << g_passed << " passed, " << g_failed << " failed." << std::endl;
  return g_failed == 0 ? 0 : 1;
}
