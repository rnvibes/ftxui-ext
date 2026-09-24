#include "ftxui/ext/led_black_hole.h"
#include "ftxui/ext/frame_buffer.h"

#include <ftxui/screen/screen.hpp>

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

void test_isco_radius() {
  TEST_CASE("ISCO inner disk edge");

  // side = 100 -> M = 4
  ftxui::ext::LEDBlackHole bh(100);
  const float M = 4.0f;

  bh.set_spin(0.0f);
  TEST_ASSERT(std::abs(bh.isco_radius() - 6.0f * M) < 1e-2f);

  bh.set_spin(0.90f);
  const float r_isco = bh.isco_radius();
  TEST_ASSERT(r_isco > bh.r_outer_horizon());
  TEST_ASSERT(r_isco < 6.0f * M);
  std::cout << "  spin 0.90: r_isco = " << r_isco << " (r+ = " << bh.r_outer_horizon()
            << ", 6M = " << 6.0f * M << ")" << std::endl;
}

void test_geodesic_weak_field() {
  TEST_CASE("Schwarzschild weak-field deflection dphi ~ 4M/b");

  ftxui::ext::LEDBlackHole bh(100);
  const float M = 4.0f;
  bh.set_spin(0.0f);
  const float b = 50.0f * M;
  const float bend = bh.deflection(b);
  const float expected = 4.0f * M / b;
  TEST_ASSERT(std::abs(bend - expected) / expected < 0.10f);
  std::cout << "  deflection(" << b << ") = " << bend << ", 4M/b = " << expected << std::endl;
}

void test_deflection_diverges_at_bcrit() {
  TEST_CASE("Deflection grows without bound as b -> b_crit");

  ftxui::ext::LEDBlackHole bh(100);
  bh.set_spin(0.90f);
  const float bc = bh.b_critical_prograde();
  const float far = bh.deflection(bc * 1.50f);
  const float near = bh.deflection(bc * 1.01f);
  TEST_ASSERT(std::isfinite(near));
  TEST_ASSERT(near > 2.0f * far);
  std::cout << "  bend(b_crit*1.5) = " << far << ", bend(b_crit*1.01) = " << near << std::endl;
}

void test_shadow_boundary_matches_bcrit() {
  TEST_CASE("Kerr shadow D-shape: edges match b_crit_pro/retro");

  // side = 100 -> M = 4
  ftxui::ext::LEDBlackHole bh(100);
  bh.set_spin(0.90f);
  bh.set_tilt(0.05f);  // near edge-on, sin(i) ~ 1
  const float b_pro = bh.b_critical_prograde();
  const float b_ret = bh.b_critical_retrograde();

  const float retro_edge = bh.shadow_radius(0.0f);        // +x: retrograde
  const float pro_edge = bh.shadow_radius(3.14159265f);   // -x: prograde
  TEST_ASSERT(std::abs(retro_edge - b_ret) < 0.05f * b_ret);
  TEST_ASSERT(std::abs(pro_edge - b_pro) < 0.05f * b_pro);
  TEST_ASSERT(pro_edge < retro_edge);  // the D-shape asymmetry
  std::cout << "  shadow(-x) = " << pro_edge << " vs b_pro = " << b_pro
            << " ; shadow(+x) = " << retro_edge << " vs b_ret = " << b_ret << std::endl;
}

void test_shadow_face_on() {
  TEST_CASE("Face-on Kerr shadow is a circle of intermediate radius");

  ftxui::ext::LEDBlackHole bh(100);
  bh.set_spin(0.90f);
  bh.set_tilt(1.0f);  // face-on
  const float r0 = bh.shadow_radius(0.0f);
  const float r90 = bh.shadow_radius(1.5707963f);
  const float r180 = bh.shadow_radius(3.14159265f);
  const float r270 = bh.shadow_radius(4.7123889f);
  const float mean = 0.25f * (r0 + r90 + r180 + r270);
  TEST_ASSERT(std::abs(r0 - mean) / mean < 0.35f);
  TEST_ASSERT(std::abs(r90 - mean) / mean < 0.35f);
  TEST_ASSERT(mean > bh.b_critical_prograde());
  TEST_ASSERT(mean < bh.b_critical_retrograde());
  std::cout << "  face-on shadow radii: " << r0 << ", " << r90 << ", " << r180 << ", " << r270
            << " (b_pro = " << bh.b_critical_prograde() << ", b_ret = " << bh.b_critical_retrograde()
            << ")" << std::endl;
}

void test_relativistic_redshift_factor() {
  TEST_CASE("Relativistic Doppler/redshift factor g");

  ftxui::ext::LEDBlackHole bh(100);
  bh.set_spin(0.90f);
  const float r = 40.0f;
  const float b = 8.0f;
  const float g_approach = bh.redshift_factor(r, b);
  const float g_recede = bh.redshift_factor(r, -b);
  TEST_ASSERT(g_approach > g_recede * 1.10f);  // beaming asymmetry

  // g -> 1 far from the hole (weak field, slow orbit)
  const float g_far = bh.redshift_factor(2000.0f, b);
  TEST_ASSERT(std::abs(g_far - 1.0f) < 0.02f);
  std::cout << "  g(+b) = " << g_approach << ", g(-b) = " << g_recede
            << ", g(far) = " << g_far << std::endl;
}

ftxui::Screen render_black_hole(float spin, float tilt) {
  ftxui::ext::LEDBlackHole bh(44);
  bh.set_spin(spin);
  bh.set_tilt(tilt);
  bh.set_particles(false);  // measure the smooth sheet here
  bh.advance(std::chrono::milliseconds(300));
  ftxui::ext::TFrameBuffer fb(44, ftxui::Color::White,
                              ftxui::ext::TFrameBuffer::DrawMode::Block);
  bh.render(fb);
  ftxui::Screen screen =
      ftxui::Screen::Create(ftxui::Dimension::Fixed(200), ftxui::Dimension::Fixed(120));
  ftxui::Render(screen, fb.render());
  return screen;
}

void test_render_responds_to_spin() {
  TEST_CASE("Rendered image depends on spin (disk/shadow really drawn)");

  const ftxui::Screen a = render_black_hole(0.90f, 0.20f);
  const ftxui::Screen b = render_black_hole(0.00f, 0.20f);
  int colored_diff = 0;
  for (int y = 0; y < a.dimy(); ++y) {
    for (int x = 0; x < a.dimx(); ++x) {
      if (a.PixelAt(x, y).foreground_color != b.PixelAt(x, y).foreground_color) {
        ++colored_diff;
      }
    }
  }
  std::cout << "  cells differing between spin 0.9 and 0.0: " << colored_diff << std::endl;
  TEST_ASSERT(colored_diff > 1000);
}

void test_relativistic_beaming_toggle() {
  TEST_CASE("bh.rel_beam toggles the Doppler asymmetry");

  ftxui::ext::LEDBlackHole bh(100);
  bh.set_spin(0.90f);
  const float r = 40.0f;
  const float b = 8.0f;

  bh.set_relativistic_beaming(true);
  const float on_p = bh.redshift_factor(r, b);
  const float on_m = bh.redshift_factor(r, -b);
  TEST_ASSERT(on_p > on_m * 1.10f);

  bh.set_relativistic_beaming(false);
  const float off_p = bh.redshift_factor(r, b);
  const float off_m = bh.redshift_factor(r, -b);
  TEST_ASSERT(std::abs(off_p - off_m) < 1e-6f);
  TEST_ASSERT(!bh.relativistic_beaming());
  std::cout << "  g(+b) " << on_p << " vs g(-b) " << on_m << " (on);  " << off_p << " vs " << off_m
            << " (off)" << std::endl;
}

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "Running Kerr Black Hole physics tests..." << std::endl;
  std::cout << "========================================" << std::endl;

  test_kerr_horizons_and_ergosphere();
  test_kerr_critical_impact_parameters();
  test_isco_radius();
  test_geodesic_weak_field();
  test_deflection_diverges_at_bcrit();
  test_shadow_boundary_matches_bcrit();
  test_shadow_face_on();
  test_relativistic_redshift_factor();
  test_relativistic_beaming_toggle();
  test_render_and_advance();
  test_render_responds_to_spin();

  std::cout << "\nResults: " << g_passed << " passed, " << g_failed << " failed." << std::endl;
  return g_failed == 0 ? 0 : 1;
}
