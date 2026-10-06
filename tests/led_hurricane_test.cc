#include "ftxui/ext/led_hurricane.h"
#include "ftxui/ext/frame_buffer.h"

#include <ftxui/screen/screen.hpp>

#include <cmath>
#include <iostream>
#include <vector>

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

void test_holland_vortex_kinematics() {
  TEST_CASE("Holland vortex tangential wind profile and eye calm");

  ftxui::ext::LEDHurricane storm(60);
  storm.set_phase_override(2); // Category 5

  const float v_max = storm.max_wind_speed_ms();
  TEST_ASSERT(v_max > 60.0f); // Category 5 winds > 60 m/s (~135+ mph)
  std::cout << "  Category 5 peak wind: " << v_max << " m/s (" << v_max * 2.23694f << " mph)" << std::endl;

  // Eye center calm: r -> 0 has v_theta -> 0
  const float v_center = storm.tangential_velocity_at(0.05f);
  TEST_ASSERT(v_center < 0.25f * v_max);
  TEST_ASSERT(v_center >= 0.0f);

  // Peak wind at R_max (norm ~0.95)
  const float v_eyewall = storm.tangential_velocity_at(0.95f);
  TEST_ASSERT(std::abs(v_eyewall - v_max) < 1.0f);

  // Outer vortex decay: v_theta decays monotonically as r increases beyond eyewall
  const float v_r2 = storm.tangential_velocity_at(2.0f);
  const float v_r3 = storm.tangential_velocity_at(3.5f);
  TEST_ASSERT(v_r2 < v_eyewall);
  TEST_ASSERT(v_r3 < v_r2);
  std::cout << "  Wind profile: center " << v_center << " m/s, eyewall " << v_eyewall
            << " m/s, r=2: " << v_r2 << " m/s, r=3.5: " << v_r3 << " m/s" << std::endl;
}

void test_lifecycle_pressure_and_intensity() {
  TEST_CASE("Lifecycle thermodynamic pressure drop across phases");

  ftxui::ext::LEDHurricane storm(60);

  // Phase 0: Inflow / Environmental depression
  storm.set_phase_override(0);
  const float p_inflow = storm.central_pressure_hpa();
  TEST_ASSERT(p_inflow > 990.0f);
  TEST_ASSERT(storm.current_phase() == ftxui::ext::HurricanePhase::Inflow);

  // Phase 1: Cyclogenesis
  storm.set_phase_override(1);
  const float p_genesis = storm.central_pressure_hpa();
  TEST_ASSERT(p_genesis < p_inflow);
  TEST_ASSERT(storm.current_phase() == ftxui::ext::HurricanePhase::Genesis);

  // Phase 2: Category 5 Mature
  storm.set_phase_override(2);
  const float p_cat5 = storm.central_pressure_hpa();
  TEST_ASSERT(p_cat5 <= 915.0f); // Deep low ~905 hPa
  TEST_ASSERT(storm.current_phase() == ftxui::ext::HurricanePhase::Category5);

  // Phase 3: Dissipation
  storm.set_phase_override(3);
  const float p_decay = storm.central_pressure_hpa();
  TEST_ASSERT(p_decay > p_cat5);
  TEST_ASSERT(storm.current_phase() == ftxui::ext::HurricanePhase::Dissipation);

  std::cout << "  Pressure evolution: Inflow " << p_inflow << " hPa -> Genesis " << p_genesis
            << " hPa -> Cat5 " << p_cat5 << " hPa -> Dissipation " << p_decay << " hPa" << std::endl;
}

void test_stadium_eye_geometry_and_clearance() {
  TEST_CASE("Stadium eyewall flare and eye clearance");

  ftxui::ext::LEDHurricane storm(60);
  storm.set_phase_override(2); // Category 5

  // 1. Center of the eye at sea level has significantly lower cloud density than the eyewall
  const float dens_eye_center = storm.cloud_density_at(0.0f, 0.0f, 0.3f);
  const float dens_eyewall = storm.cloud_density_at(0.95f, 0.0f, 0.3f);
  TEST_ASSERT(dens_eyewall > dens_eye_center * 2.5f);
  std::cout << "  Eye center density: " << dens_eye_center << " vs eyewall: " << dens_eyewall << std::endl;

  // 2. Stadium slope: Eyewall peak radius flares outward with height
  // Sample along x at z = 0.2 and z = 0.8 to find peak density radius
  auto find_peak_r = [&](float z) {
    float max_r = 0.5f;
    float max_d = 0.0f;
    for (float r = 0.5f; r <= 2.2f; r += 0.05f) {
      const float d = storm.cloud_density_at(r, 0.0f, z);
      if (d > max_d) {
        max_d = d;
        max_r = r;
      }
    }
    return max_r;
  };

  const float r_peak_low = find_peak_r(0.2f);
  const float r_peak_high = find_peak_r(0.8f);
  TEST_ASSERT(r_peak_high > r_peak_low);
  std::cout << "  Eyewall peak radius: low (z=0.2): " << r_peak_low << " -> high (z=0.8): "
            << r_peak_high << " (outward stadium flare)" << std::endl;
}

void test_lightning_electrodynamics() {
  TEST_CASE("Eyewall lightning strike and exponential dissipation");

  ftxui::ext::LEDHurricane storm(60);
  storm.set_phase_override(2); // Category 5

  // Initially inactive
  storm.set_lightning_frequency(0.0f); // pause auto-strikes
  storm.advance(std::chrono::milliseconds(500));
  TEST_ASSERT(!storm.is_lightning_active());

  // Trigger strike
  storm.trigger_lightning();
  TEST_ASSERT(storm.is_lightning_active());
  std::cout << "  Lightning strike triggered successfully." << std::endl;

  // Advance simulation to test exponential decay
  storm.advance(std::chrono::milliseconds(300));
  TEST_ASSERT(!storm.is_lightning_active());
  std::cout << "  Lightning flash dissipated cleanly." << std::endl;
}

void test_camera_drag_and_orbit() {
  TEST_CASE("Camera controls: tilt, orbit, zoom matching ragatui conventions");

  ftxui::ext::LEDHurricane storm(60);

  storm.set_tilt(0.25f);
  TEST_ASSERT(std::abs(storm.tilt() - 0.25f) < 1e-4f);

  storm.set_tilt(1.5f); // Clamps to 1.0
  TEST_ASSERT(storm.tilt() <= 1.0f);

  storm.set_orbit(1.57f);
  TEST_ASSERT(std::abs(storm.orbit() - 1.57f) < 1e-2f);

  storm.set_orbit(7.0f); // Wraps within [-pi, pi]
  TEST_ASSERT(storm.orbit() >= -3.15f && storm.orbit() <= 3.15f);

  storm.set_zoom(1.8f);
  TEST_ASSERT(std::abs(storm.zoom() - 1.8f) < 1e-4f);
}

void test_render_and_advance() {
  TEST_CASE("Advance and render frame buffer execution");

  ftxui::ext::LEDHurricane storm(32);
  storm.set_phase_override(2);

  storm.advance(std::chrono::milliseconds(100));

  ftxui::ext::TFrameBuffer fb(32);
  storm.render(fb);

  auto elem = fb.render();
  TEST_ASSERT(elem != nullptr);
  std::cout << "  Rendered 3D hurricane to FTXUI frame buffer successfully." << std::endl;
}

void test_no_hard_edges_in_render() {
  TEST_CASE("Continuous volumetric rendering without edge seams");

  ftxui::ext::LEDHurricane storm(40);
  storm.set_tilt(0.35f);
  storm.set_phase_override(2);
  storm.advance(std::chrono::milliseconds(200));

  ftxui::ext::TFrameBuffer fb(40, ftxui::Color::Black, ftxui::ext::TFrameBuffer::DrawMode::Block);
  storm.render(fb);

  ftxui::Screen screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80), ftxui::Dimension::Fixed(80));
  ftxui::Render(screen, fb.render());

  std::vector<int> col_changes(screen.dimx(), 0);
  std::vector<int> row_changes(screen.dimy(), 0);
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x + 1 < screen.dimx(); ++x) {
      if (screen.PixelAt(x, y).foreground_color != screen.PixelAt(x + 1, y).foreground_color) {
        col_changes[x]++;
      }
    }
  }
  for (int x = 0; x < screen.dimx(); ++x) {
    for (int y = 0; y + 1 < screen.dimy(); ++y) {
      if (screen.PixelAt(x, y).foreground_color != screen.PixelAt(x, y + 1).foreground_color) {
        row_changes[y]++;
      }
    }
  }

  auto spike = [](const std::vector<int>& v) {
    int worst = 0;
    for (std::size_t i = 1; i + 1 < v.size(); ++i) {
      const int side = std::max(v[i - 1], v[i + 1]);
      worst = std::max(worst, v[i] - side);
    }
    return worst;
  };

  const int col_spike = spike(col_changes);
  const int row_spike = spike(row_changes);
  std::cout << "  Local line spike: column " << col_spike << ", row " << row_spike << std::endl;
  TEST_ASSERT(col_spike < 35);
  TEST_ASSERT(row_spike < 35);
}

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "Running LEDHurricane Physics & Render Tests..." << std::endl;
  std::cout << "========================================" << std::endl;

  test_holland_vortex_kinematics();
  test_lifecycle_pressure_and_intensity();
  test_stadium_eye_geometry_and_clearance();
  test_lightning_electrodynamics();
  test_camera_drag_and_orbit();
  test_render_and_advance();
  test_no_hard_edges_in_render();

  std::cout << "\nResults: " << g_passed << " passed, " << g_failed << " failed." << std::endl;
  return g_failed == 0 ? 0 : 1;
}
