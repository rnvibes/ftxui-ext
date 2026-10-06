#include "ftxui/ext/led_hurricane.h"
#include "ftxui/ext/frame_buffer.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>

using namespace ftxui;

int main(int argc, char* argv[]) {
  bool snapshot_mode = false;
  int target_side = 44;
  int force_phase = -1;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--snapshot" || arg == "--demo") {
      snapshot_mode = true;
    } else if (arg == "--side" && i + 1 < argc) {
      target_side = std::clamp(std::stoi(argv[++i]), 20, 100);
    } else if (arg == "--cat5" || arg == "-3") {
      force_phase = 2;
    } else if (arg == "--inflow" || arg == "-1") {
      force_phase = 0;
    } else if (arg == "--genesis" || arg == "-2") {
      force_phase = 1;
    } else if (arg == "--dissipation" || arg == "-4") {
      force_phase = 3;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: hurricane [options]\n"
                << "Options:\n"
                << "  --snapshot, --demo   Render a single frame to stdout and exit\n"
                << "  --side <N>           Grid side resolution (default 44)\n"
                << "  --cat5, -3           Start locked in Category 5 stage\n"
                << "  --inflow, -1         Start in Perpetual Inflow stage\n"
                << "  --genesis, -2        Start in Cyclogenesis stage\n"
                << "  --dissipation, -4    Start in Dissipation stage\n"
                << "  --help, -h           Show this help message\n\n"
                << "Interactive Controls:\n"
                << "  Mouse Drag           Tilt (elevation) and Orbit (azimuth)\n"
                << "  Mouse Wheel / + / -  Zoom in / out\n"
                << "  Space                Trigger lightning bolt in eyewall\n"
                << "  1, 2, 3, 4           Lock Phase: Inflow, Genesis, Cat 5, Decay\n"
                << "  0 or A               Automatic continuous lifecycle progression\n"
                << "  M                    Toggle Monochrome / Full-Color mode\n"
                << "  B                    Toggle Braille / Block drawing mode\n"
                << "  Q or Esc             Exit application\n";
      return 0;
    }
  }

  // Detect non-interactive stdout / pipe
  if ((!isatty(fileno(stdin)) || !isatty(fileno(stdout))) && !snapshot_mode) {
    snapshot_mode = true;
  }

  ftxui::ext::LEDHurricane storm(target_side);
  if (force_phase >= 0) {
    storm.set_phase_override(force_phase);
  }

  // Framebuffer container
  ftxui::ext::TFrameBuffer fb(target_side, Color::White, ftxui::ext::TFrameBuffer::DrawMode::Braille);
  bool braille_mode = true;

  auto build_ui = [&](float fps) -> Element {
    fb.clear();
    storm.render(fb);

    // Diagnostics
    const float p_hpa = storm.central_pressure_hpa();
    const float v_max = storm.max_wind_speed_ms();
    const float v_mph = v_max * 2.23694f;
    const float tilt_deg = std::asin(storm.tilt()) * (180.0f / 3.14159265f);
    float orbit_deg = storm.orbit() * (180.0f / 3.14159265f);
    if (orbit_deg < 0.0f) orbit_deg += 360.0f;
    const float zoom_val = storm.zoom();
    const bool lightning_active = storm.is_lightning_active();
    const auto phase = storm.current_phase();

    // Phase Badge
    Element phase_badge;
    if (phase == ftxui::ext::HurricanePhase::Inflow) {
      phase_badge = text(" [1: INFLOW - PERPETUAL STREAMLINES] ") | bold | color(Color::Cyan) | bgcolor(Color::RGB(10, 35, 55));
    } else if (phase == ftxui::ext::HurricanePhase::Genesis) {
      phase_badge = text(" [2: CYCLOGENESIS - EYE ORGANIZING] ") | bold | color(Color::Yellow) | bgcolor(Color::RGB(55, 45, 10));
    } else if (phase == ftxui::ext::HurricanePhase::Category5) {
      phase_badge = text(" [3: CATEGORY 5 - MATURE STADIUM] ") | bold | color(Color::RGB(255, 60, 60)) | bgcolor(Color::RGB(65, 10, 10));
    } else {
      phase_badge = text(" [4: DISSIPATION - SHEAR & DECAY] ") | bold | color(Color::RGB(160, 160, 180)) | bgcolor(Color::RGB(35, 35, 45));
    }

    // Lightning indicator
    Element lightning_badge = lightning_active
        ? (text(" ⚡ LIGHTNING STRIKE ") | bold | color(Color::RGB(255, 255, 120)) | bgcolor(Color::RGB(110, 80, 240)))
        : (text("   EYEWALL CALM   ") | dim | color(Color::GrayDark));

    // Telemetry Panel
    std::ostringstream p_str, v_str, tilt_str, orbit_str, zoom_str, fps_str;
    p_str << std::fixed << std::setprecision(1) << p_hpa << " hPa";
    v_str << std::fixed << std::setprecision(1) << v_max << " m/s (" << static_cast<int>(v_mph) << " mph)";
    tilt_str << std::fixed << std::setprecision(0) << tilt_deg << "° (" << (storm.tilt() > 0.85f ? "Satellite" : "Oblique") << ")";
    orbit_str << std::fixed << std::setprecision(0) << orbit_deg << "°";
    zoom_str << std::fixed << std::setprecision(1) << zoom_val << "x";
    fps_str << std::fixed << std::setprecision(1) << fps << " FPS";

    auto meter_bar = [&](float frac, Color c) {
      const int w = 18;
      int filled = std::clamp(static_cast<int>(frac * w), 0, w);
      std::string bar(filled, '#');
      std::string empty(w - filled, '-');
      return hbox({
          text("["),
          text(bar) | bold | color(c),
          text(empty) | dim,
          text("]"),
      });
    };

    float p_norm = std::clamp((1012.0f - p_hpa) / 107.0f, 0.0f, 1.0f);
    float v_norm = std::clamp(v_max / 75.0f, 0.0f, 1.0f);

    auto telemetry_box = vbox({
        text("── METEOROLOGICAL TELEMETRY ──") | bold | color(Color::White) | hcenter,
        separator(),
        hbox({text("Central Pressure: ") | dim, text(p_str.str()) | bold | color(Color::RGB(255, 140, 40))}),
        meter_bar(p_norm, Color::RGB(255, 100, 30)),
        separatorEmpty(),
        hbox({text("Max Wind Speed:   ") | dim, text(v_str.str()) | bold | color(Color::RGB(255, 80, 80))}),
        meter_bar(v_norm, Color::RGB(255, 60, 60)),
        separatorEmpty(),
        hbox({text("Eye Radius:       ") | dim, text("32 km (Stadium flare: +65%)") | bold | color(Color::Cyan)}),
        hbox({text("Troposphere Top:  ") | dim, text("16.0 km (Cirrus Outflow)") | color(Color::White)}),
        separator(),
        text("── 3D CAMERA ORIENTATION ──") | bold | color(Color::White) | hcenter,
        hbox({text("Tilt (Elevation): ") | dim, text(tilt_str.str()) | color(Color::Yellow)}),
        hbox({text("Orbit (Azimuth):  ") | dim, text(orbit_str.str()) | color(Color::Yellow)}),
        hbox({text("Zoom Level:       ") | dim, text(zoom_str.str()) | color(Color::Yellow)}),
        separator(),
        hbox({text("Renderer:         ") | dim, text(braille_mode ? "Braille (Hi-Res)" : "Block (Dense)") | color(Color::White)}),
        hbox({text("Frame Rate:       ") | dim, text(fps_str.str()) | color(Color::Green)}),
        separatorEmpty(),
        lightning_badge | hcenter,
    }) | border | size(WIDTH, GREATER_THAN, 38);

    auto viewport = fb.render() | center;

    auto header = hbox({
        text(" 🌀 LEDHurricane: 3D Tropical Cyclone Simulation ") | bold | color(Color::White) | bgcolor(Color::RGB(20, 40, 70)),
        filler(),
        phase_badge,
    });

    auto footer = hbox({
        text(" [Drag: Tilt/Orbit] ") | color(Color::Cyan),
        text(" [Wheel/+/-: Zoom] ") | color(Color::Cyan),
        text(" [Space: Lightning] ") | bold | color(Color::Yellow),
        text(" [1-4: Phase Lock] ") | color(Color::White),
        text(" [0: Auto Cycle] ") | color(Color::White),
        text(" [B: Mode] ") | color(Color::White),
        text(" [M: Mono] ") | color(Color::White),
        text(" [Q: Exit] ") | color(Color::RGB(255, 90, 90)),
    }) | bgcolor(Color::RGB(15, 18, 24));

    return vbox({
        header,
        separator(),
        hbox({
            viewport | flex,
            separator(),
            telemetry_box,
        }) | flex,
        separator(),
        footer,
    });
  };

  // Snapshot / Non-interactive CLI mode
  if (snapshot_mode) {
    storm.advance(std::chrono::milliseconds(400));
    if (force_phase == 2 || force_phase < 0) {
      storm.trigger_lightning();
    }

    auto screen = Screen::Create(Dimension::Fixed(100), Dimension::Fixed(target_side + 4));
    Render(screen, build_ui(30.0f));
    screen.Print();
    std::cout << std::endl;
    return 0;
  }

  // Interactive TUI Application
  auto screen = ScreenInteractive::Fullscreen();
  std::atomic<bool> running{true};
  std::atomic<float> display_fps{0.0f};

  // Mouse interaction state
  bool dragging = false;
  int drag_start_x = 0;
  int drag_start_y = 0;
  float drag_start_tilt = storm.tilt();
  float drag_start_orbit = storm.orbit();

  // Animation update thread (~30 FPS)
  std::thread ticker([&]() {
    auto last_time = std::chrono::steady_clock::now();
    auto fps_start = last_time;
    int frames = 0;

    while (running.load()) {
      auto now = std::chrono::steady_clock::now();
      float dt = std::chrono::duration<float>(now - last_time).count();
      last_time = now;

      storm.advance(ftxui::animation::Duration(dt));
      screen.PostEvent(Event::Custom);

      ++frames;
      float fps_dur = std::chrono::duration<float>(now - fps_start).count();
      if (fps_dur >= 0.5f) {
        display_fps.store(frames / fps_dur);
        frames = 0;
        fps_start = now;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(32));
    }
  });

  // FTXUI Component definition
  auto renderer = Renderer([&]() {
    return build_ui(display_fps.load());
  });

  // Event handler for keyboard and mouse
  auto component = CatchEvent(renderer, [&](Event event) {
    if (event == Event::Character('q') || event == Event::Character('Q') || event == Event::Escape) {
      screen.ExitLoopClosure()();
      return true;
    }

    // Keyboard navigation
    if (event == Event::ArrowLeft || event == Event::Character('h')) {
      storm.set_orbit(storm.orbit() - 0.10f);
      return true;
    }
    if (event == Event::ArrowRight || event == Event::Character('l')) {
      storm.set_orbit(storm.orbit() + 0.10f);
      return true;
    }
    if (event == Event::ArrowUp || event == Event::Character('k')) {
      storm.set_tilt(storm.tilt() + 0.05f);
      return true;
    }
    if (event == Event::ArrowDown || event == Event::Character('j')) {
      storm.set_tilt(storm.tilt() - 0.05f);
      return true;
    }
    if (event == Event::Character('+') || event == Event::Character('=')) {
      storm.set_zoom(storm.zoom() + 0.10f);
      return true;
    }
    if (event == Event::Character('-') || event == Event::Character('_')) {
      storm.set_zoom(storm.zoom() - 0.10f);
      return true;
    }

    // Action triggers
    if (event == Event::Character(' ')) {
      storm.trigger_lightning();
      return true;
    }

    // Phase selection
    if (event == Event::Character('1')) {
      storm.set_phase_override(0);
      return true;
    }
    if (event == Event::Character('2')) {
      storm.set_phase_override(1);
      return true;
    }
    if (event == Event::Character('3')) {
      storm.set_phase_override(2);
      return true;
    }
    if (event == Event::Character('4')) {
      storm.set_phase_override(3);
      return true;
    }
    if (event == Event::Character('0') || event == Event::Character('a') || event == Event::Character('A')) {
      storm.set_phase_override(-1); // Free-running
      return true;
    }

    // Drawing mode & Color
    if (event == Event::Character('b') || event == Event::Character('B')) {
      braille_mode = !braille_mode;
      fb.set_draw_mode(braille_mode ? ftxui::ext::TFrameBuffer::DrawMode::Braille
                                    : ftxui::ext::TFrameBuffer::DrawMode::Block);
      return true;
    }
    if (event == Event::Character('m') || event == Event::Character('M')) {
      auto cur = storm.color_mode();
      storm.set_color_mode(cur == ftxui::ext::ColorMode::Color
                               ? ftxui::ext::ColorMode::Monochrome
                               : ftxui::ext::ColorMode::Color);
      return true;
    }

    // Mouse drag handling (Matching ragatui / DropPane)
    if (event.is_mouse()) {
      auto mouse = event.mouse();
      if (mouse.button == Mouse::Left) {
        if (mouse.motion == Mouse::Pressed) {
          dragging = true;
          drag_start_x = mouse.x;
          drag_start_y = mouse.y;
          drag_start_tilt = storm.tilt();
          drag_start_orbit = storm.orbit();
          return true;
        } else if (mouse.motion == Mouse::Moved && dragging) {
          float delta_y = static_cast<float>(mouse.y - drag_start_y);
          float delta_x = static_cast<float>(mouse.x - drag_start_x);
          // Vertical drag tilts towards face-on (satellite); horizontal drag spins view
          storm.set_tilt(std::clamp(drag_start_tilt + delta_y * 0.02f, 0.05f, 1.0f));
          storm.set_orbit(drag_start_orbit + delta_x * 0.012f);
          return true;
        } else if (mouse.motion == Mouse::Released) {
          dragging = false;
          return true;
        }
      }
      if (mouse.button == Mouse::WheelUp) {
        storm.set_zoom(storm.zoom() + 0.08f);
        return true;
      }
      if (mouse.button == Mouse::WheelDown) {
        storm.set_zoom(storm.zoom() - 0.08f);
        return true;
      }
    }

    return false;
  });

  screen.Loop(component);

  running.store(false);
  if (ticker.joinable()) {
    ticker.join();
  }

  return 0;
}
