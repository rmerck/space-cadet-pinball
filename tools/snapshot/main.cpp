// Development tool: runs the game headless with scripted input and writes one
// rendered frame to a PNG in the current directory. No window is opened.
// Used to review art and layout on any window shape, and by ctest as a
// rendering smoke test.
//
// Usage: pinball_snapshot <steps> <name.png> [state] [options]
//
// state    play (the default), launch, pause, interrupted, confirm, about,
//          about-info, licences, initials, gameover, longest (the course
//          window at its fullest, the longest lines there can be),
//          the store's staged scenes (core/Scenes.hpp: table, multiball,
//          hangar, course, graduation; the steps run after the scene, with
//          no buttons pressed),
//          locked (a ball locked in the Hangar's first bay and the next on
//          the plunger), bankdown (the bay doors dropped), spinning (the
//          spinner's blade a quarter turn round, on its edge),
//          or check: draw the layout's screens one after another and fail
//          unless every control, window and dialog is really drawn (see
//          checkLayout below); the picture written is the play frame
// options  --layout <preset | WxH[@scale][+top,bottom,left,right]>
//          --surface WxH  --insets top,bottom,left,right
//          --idiom desktop|touch
//          --page N            which licence page, from 1
//          --message TEXT      shown in the message window
//          --pressed LIST      of left,right,centre,nl,nr,pause
//          --zones             outline the safe area, zones and hit rectangles
//
// Sizes and insets are in points. Without a scale the picture is the
// layout's canvas, one pixel to the unit; with one it is the window at that
// many pixels to the point, as a device would show it.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "GameTestAccess.hpp"
#include "core/Args.hpp"
#include "core/DisplayText.hpp"
#include "core/Game.hpp"
#include "core/HighScores.hpp"
#include "core/InitialsEntry.hpp"
#include "core/OverlayLayout.hpp"
#include "core/PanelLayout.hpp"
#include "core/Scenes.hpp"
#include "core/ScreenLayout.hpp"
#include "core/SurfaceSpec.hpp"
#include "core/TouchControls.hpp"
#include "platform/About.hpp"
#include "platform/Assets.hpp"
#include "platform/GameViews.hpp"
#include "platform/Hud.hpp"
#include "platform/InitialsScreen.hpp"
#include "platform/PauseMenu.hpp"
#include "platform/SafeFile.hpp"
#include "platform/TableRenderer.hpp"

namespace {

struct SurfaceDeleter {
  void operator()(SDL_Surface* s) const noexcept { SDL_DestroySurface(s); }
};
struct RendererDeleter {
  void operator()(SDL_Renderer* r) const noexcept { SDL_DestroyRenderer(r); }
};
struct IoCloser {
  void operator()(SDL_IOStream* io) const noexcept { SDL_CloseIO(io); }
};

// The output is always a plain file name in the current directory: lower-case
// letters, digits and underscores, ending in ".png". No path separators.
bool isPlainPngName(std::string_view name) noexcept {
  constexpr std::string_view kExtension = ".png";
  if (name.size() <= kExtension.size() || name.size() > 64 || !name.ends_with(kExtension)) {
    return false;
  }
  return std::ranges::all_of(name.substr(0, name.size() - kExtension.size()), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
  });
}

// A fixed script that exercises the table: launch, then work the flippers.
pinball::InputState scriptedInput(std::uint32_t step) noexcept {
  pinball::InputState input;
  input.plunger = (step % 1200U) >= 20U && (step % 1200U) < 140U;
  input.leftFlipper = (step / 29U) % 2U == 0U;
  input.rightFlipper = (step / 41U) % 2U == 0U;
  return input;
}

// The same launches with the flippers left alone, so every ball drains.
pinball::InputState losingInput(std::uint32_t step) noexcept {
  pinball::InputState input;
  input.plunger = (step % 1200U) >= 20U && (step % 1200U) < 140U;
  return input;
}

// True if the playfield part of the frame holds a real picture: many
// different colours, not a flat fill or a few lines. The rest is ignored,
// because the panel is filled even when nothing else draws.
bool hasContent(SDL_Surface* surface, const pinball::screen::Rect& playfield) {
  constexpr std::size_t kNeeded = 16;
  if (!SDL_LockSurface(surface)) {
    return false;
  }
  std::array<std::uint32_t, kNeeded> seen{};
  std::size_t distinct = 0;
  const auto* pixels = static_cast<const std::uint8_t*>(surface->pixels);
  // The playfield's part of the surface, kept inside it.
  const int left = std::clamp(static_cast<int>(playfield.x), 0, surface->w);
  const int top = std::clamp(static_cast<int>(playfield.y), 0, surface->h);
  const int right = std::clamp(static_cast<int>(playfield.x + playfield.w), left, surface->w);
  const int bottom = std::clamp(static_cast<int>(playfield.y + playfield.h), top, surface->h);
  // Sample every fourth pixel of every fourth row: plenty, and quick.
  for (int y = top; y < bottom && distinct < kNeeded; y += 4) {
    const std::uint8_t* row =
        pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch);
    for (int x = left; x < right && distinct < kNeeded; x += 4) {
      std::uint32_t value = 0;
      std::memcpy(&value, row + static_cast<std::size_t>(x) * 4U, sizeof value);
      const auto known = std::span(seen).first(distinct);
      if (std::ranges::find(known, value) == known.end()) {
        seen[distinct++] = value;
      }
    }
  }
  SDL_UnlockSurface(surface);
  return distinct >= kNeeded;
}

enum class State : std::uint8_t {
  Play,
  Launch,
  Pause,
  Interrupted,
  Confirm,
  About,
  AboutInfo,
  Licences,
  Initials,
  GameOver,
  Longest,
  Table,
  Course,
  Multiball,
  Hangar,
  Graduation,
  Locked,
  BankDown,
  Spinning,
  Check,
};

struct StateName {
  std::string_view name;
  State state;
};
constexpr std::array<StateName, 20> kStates{{
    {"play", State::Play},
    {"launch", State::Launch},
    {"pause", State::Pause},
    {"interrupted", State::Interrupted},
    {"confirm", State::Confirm},
    {"about", State::About},
    {"about-info", State::AboutInfo},
    {"licences", State::Licences},
    {"initials", State::Initials},
    {"gameover", State::GameOver},
    {"longest", State::Longest},
    {"table", State::Table},
    {"course", State::Course},
    {"multiball", State::Multiball},
    {"hangar", State::Hangar},
    {"graduation", State::Graduation},
    {"locked", State::Locked},
    {"bankdown", State::BankDown},
    {"spinning", State::Spinning},
    {"check", State::Check},
}};

// What the command line asked for.
struct Request {
  std::uint32_t steps = 0;
  std::string output;
  State state = State::Play;
  pinball::screen::Surface surface{};
  pinball::screen::Idiom idiom = pinball::screen::Idiom::Desktop;
  int scale = 0;  // Pixels per point; 0 for one pixel per canvas unit.
  std::size_t licencePage = 0;
  std::string_view message;
  pinball::TouchControls::View pressed{};
  bool zones = false;
};

// The largest picture drawn, on either side, in canvas units and in pixels.
constexpr float kMaxPictureSide = 8192.0F;

constexpr const char* kUsage =
    "Usage: pinball_snapshot <steps 1..1000000> <name.png> [state] [options]\n"
    "  state: play launch pause interrupted confirm about about-info licences\n"
    "         initials gameover longest table course multiball hangar graduation\n"
    "         locked bankdown spinning check\n"
    "  --layout <preset | WxH[@scale][+top,bottom,left,right]>   points, 100..4096; scale 1..4,\n"
    "           and no more than 8192 pixels a side at that scale\n"
    "  --surface WxH   --insets top,bottom,left,right\n"
    "  --idiom desktop|touch\n"
    "  --page <1..99>   --message <TEXT>   --zones\n"
    "  --pressed <left,right,centre,nl,nr,pause>\n";

// "left,right,..." into the view. False if a word is unknown or repeated.
bool parsePressed(std::string_view text, pinball::TouchControls::View& view) noexcept {
  struct Word {
    std::string_view name;
    bool pinball::TouchControls::View::* flag;
  };
  using View = pinball::TouchControls::View;
  constexpr std::array<Word, 6> kWords{{{"left", &View::left},
                                        {"right", &View::right},
                                        {"centre", &View::centre},
                                        {"nl", &View::nudgeLeft},
                                        {"nr", &View::nudgeRight},
                                        {"pause", &View::pause}}};
  while (true) {
    const std::size_t comma = text.find(',');
    const std::string_view word = text.substr(0, comma);
    const auto found = std::ranges::find(kWords, word, &Word::name);
    if (found == kWords.end() || view.*(found->flag)) {
      return false;
    }
    view.*(found->flag) = true;
    if (comma == std::string_view::npos) {
      return true;
    }
    text.remove_prefix(comma + 1);
  }
}

// Reads the whole command line. Nothing is guessed: an unknown word, a
// repeated or half-given option, or a number out of range is refused.
std::optional<Request> parseArguments(std::span<char* const> args) {
  namespace screen = pinball::screen;
  if (args.size() < 3) {
    return std::nullopt;
  }
  Request request;
  const auto steps = pinball::parseFrameLimit(args[1]);
  const std::string_view name = args[2];
  if (!steps || !isPlainPngName(name)) {
    return std::nullopt;
  }
  request.steps = *steps;
  request.output = std::string(name);

  std::size_t next = 3;
  if (next < args.size() && !std::string_view(args[next]).starts_with("--")) {
    const auto found = std::ranges::find(kStates, std::string_view(args[next]), &StateName::name);
    if (found == kStates.end()) {
      return std::nullopt;
    }
    request.state = found->state;
    ++next;
  }

  std::optional<screen::SurfaceSpec> layout;
  std::optional<screen::SurfaceSpec> surface;
  std::optional<std::string_view> insets;
  std::optional<screen::Idiom> idiom;
  bool page = false;
  bool message = false;
  bool pressed = false;
  for (; next < args.size(); ++next) {
    const std::string_view option = args[next];
    if (option == "--zones") {
      if (request.zones) {
        return std::nullopt;
      }
      request.zones = true;
      continue;
    }
    // Every other option takes one value.
    if (next + 1 >= args.size()) {
      return std::nullopt;
    }
    const std::string_view value = args[++next];
    if (option == "--layout" && !layout) {
      layout = screen::parseSurfaceSpec(value, screen::presets());
      if (!layout) {
        return std::nullopt;
      }
    } else if (option == "--surface" && !surface) {
      // A bare size: no preset, scale or insets here.
      surface = screen::parseSurfaceSpec(value, {});
      if (!surface || value.find_first_of("@+") != std::string_view::npos) {
        return std::nullopt;
      }
    } else if (option == "--insets" && !insets) {
      insets = value;
    } else if (option == "--idiom" && !idiom) {
      if (value == "desktop") {
        idiom = screen::Idiom::Desktop;
      } else if (value == "touch") {
        idiom = screen::Idiom::Touch;
      } else {
        return std::nullopt;
      }
    } else if (option == "--page" && !page) {
      const auto number = screen::parseSpecNumber(value, 1, 99);
      if (!number) {
        return std::nullopt;
      }
      request.licencePage = *number - 1;
      page = true;
    } else if (option == "--message" && !message) {
      // Only what the rules themselves could say.
      if (!pinball::display::fits(value, pinball::display::kMaxMessageLength)) {
        return std::nullopt;
      }
      request.message = value;
      message = true;
    } else if (option == "--pressed" && !pressed) {
      if (!parsePressed(value, request.pressed)) {
        return std::nullopt;
      }
      pressed = true;
    } else {
      return std::nullopt;
    }
  }
  // One way of naming the window at a time, and insets only with a size.
  if ((layout && surface) || (insets && !surface)) {
    return std::nullopt;
  }
  if (surface) {
    if (insets && !screen::parseSpecInsets(*insets, surface->surface)) {
      return std::nullopt;
    }
    layout = surface;
  }
  if (layout) {
    request.surface = layout->surface;
    request.idiom = layout->idiom;
    request.scale = layout->scale;
  } else {
    const screen::Preset* const desktop = screen::findPreset("desktop");
    if (desktop == nullptr) {
      return std::nullopt;
    }
    request.surface = desktop->surface;
    request.idiom = desktop->idiom;
  }
  if (idiom) {
    request.idiom = *idiom;
  }
  // A scale that would make the picture larger than is ever drawn is a
  // mistake in the command line, found before anything is loaded.
  const auto scale = static_cast<float>(request.scale);
  if (request.surface.width * scale > kMaxPictureSide ||
      request.surface.height * scale > kMaxPictureSide) {
    return std::nullopt;
  }
  return request;
}

void outline(SDL_Renderer* r, const pinball::screen::Rect& rect, SDL_Color color) {
  if (rect.w <= 0.0F || rect.h <= 0.0F) {
    return;
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 40);
  const SDL_FRect area{rect.x, rect.y, rect.w, rect.h};
  SDL_RenderFillRect(r, &area);
  SDL_SetRenderDrawColor(r, color.r, color.g, color.b, 255);
  for (int i = 0; i < 2; ++i) {
    const auto inset = static_cast<float>(i);
    const SDL_FRect line{rect.x + inset, rect.y + inset, rect.w - 2.0F * inset,
                         rect.h - 2.0F * inset};
    SDL_RenderRect(r, &line);
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

// The geometry a touch lands in, over the picture.
void drawZones(SDL_Renderer* r, const pinball::screen::Layout& layout) {
  outline(r, layout.flipperLeftZone, {70, 235, 255, 255});
  outline(r, layout.flipperRightZone, {255, 60, 170, 255});
  outline(r, layout.safe, {255, 255, 255, 255});
  for (const pinball::screen::Rect& hit :
       {layout.pauseButton, layout.nudgeLeftButton, layout.centreButton, layout.nudgeRightButton}) {
    outline(r, hit, {255, 225, 70, 255});
  }
}

// With a sample table, so the layout of a full list can be checked.
pinball::HighScores sampleScores() {
  pinball::HighScores sample;
  sample.submit({12'345'678, 8, {'A', 'C', 'E'}});
  sample.submit({3'250'000, 4, {'S', 'K', 'Y'}});
  sample.submit({987'500, 2, {'J', 'E', 'T'}});
  sample.submit({400'000, 1});
  sample.submit({182'500, 0, {'W', 'M', 'W'}});
  return sample;
}

// One picture: what to draw over the game.
struct Scene {
  State state = State::Play;
  pinball::TouchControls::View pressed{};
  std::size_t licencePage = 0;
  std::string_view message{};
  bool zones = false;
  // Leave out the touch controls, or the dialog of the state (the table is
  // still dimmed for a pause): only the checks ask for these.
  bool withoutControls = false;
  bool withoutDialog = false;
};

// Draws the scene for the game onto the renderer's canvas.
void drawScene(SDL_Renderer* r, const pinball::TableRenderer& table,
               const pinball::screen::Layout& layout, const pinball::Game& game,
               const Scene& scene) {
  // What a scripted game cannot be steered into is shown by asking.
  pinball::HudPreview preview;
  preview.message = scene.message;
  preview.course = scene.state == State::Longest;
  pinball::DrawOptions options;
  // The lights as they stand at this step (a single look: the flashes of
  // earlier steps are not seen; tools/fxreel feeds every step).
  pinball::LightShow lights;
  lights.observe(game);
  options.lightShow = &lights;
  options.dimmed = scene.state == State::Pause || scene.state == State::Interrupted ||
                   scene.state == State::Confirm;
  options.flippersAtRest = options.dimmed;

  SDL_SetRenderDrawColor(r, pinball::screen::kBackdropRgb[0], pinball::screen::kBackdropRgb[1],
                         pinball::screen::kBackdropRgb[2], SDL_ALPHA_OPAQUE);
  SDL_RenderClear(r);
  // Alpha 1: the latest step exactly, so the picture depends only on the
  // step count.
  table.drawBackdrop(r, layout);
  table.draw(r, game, 1.0F, layout, options,
             pinball::HudExtras{.bestScore = 0, .newHighScore = false, .preview = &preview});
  if (!scene.withoutControls) {
    pinball::TouchControls::View view = scene.pressed;
    view.centreRole = pinball::TouchControls().view(pinball::touchContext(game)).centreRole;
    table.drawTouchOverlay(r, layout, view);
  }
  if (scene.zones) {
    drawZones(r, layout);
  }

  const pinball::OverlayArt art = table.overlayArt();
  switch (scene.withoutDialog ? State::Play : scene.state) {
    case State::Pause:
    case State::Interrupted:
    case State::Confirm:
      pinball::drawPauseMenu(
          r, layout,
          pinball::MenuState{.muted = false,
                             .musicOn = true,
                             .confirmNewGame = scene.state == State::Confirm,
                             .afterInterruption = scene.state == State::Interrupted},
          art);
      break;
    case State::About:
    case State::AboutInfo:
    case State::Licences: {
      const pinball::AboutPage page = scene.state == State::About
                                          ? pinball::AboutPage::Scores
                                          : scene.state == State::AboutInfo
                                                ? pinball::AboutPage::About
                                                : pinball::AboutPage::Licences;
      pinball::drawAbout(r, sampleScores(), layout,
                         pinball::AboutState{.page = page, .licencePage = scene.licencePage},
                         art);
      break;
    }
    case State::Initials: {
      pinball::InitialsEntry entry(pinball::Initials{'W', 'M', 'A'});
      entry.right();
      pinball::drawInitialsEntry(r, entry, 12'345'678, true, layout, art);
      break;
    }
    case State::Play:
    case State::Launch:
    case State::GameOver:
    case State::Longest:
    case State::Table:
    case State::Course:
    case State::Multiball:
    case State::Hangar:
    case State::Graduation:
    case State::Locked:
    case State::BankDown:
    case State::Spinning:
    case State::Check:
      break;
  }
  SDL_RenderPresent(r);
}

// --- The check: a drawer that draws nothing must not pass.
//
// Every picture below is drawn for the same layout and compared with
// another in the places where the two must differ: a control with the
// picture that leaves the controls out, a pressed control with the same one
// at rest, a dialog with the game behind it, one page with the next. A
// window of the status display must hold text over its face, and must
// change when what it shows changes. A caption, an arrow or a line of body
// text is looked for by its own colour, which nothing behind it has.

using Pixels = std::vector<std::uint32_t>;

struct Checker {
  SDL_Surface* surface;
  SDL_Renderer* renderer;
  const pinball::TableRenderer& table;
  const pinball::screen::Layout& layout;
  int failures = 0;

  // Relative luminance of a pixel, 0 to 1 (sRGB, as WCAG has it).
  [[nodiscard]] double luminance(std::uint32_t pixel) const {
    const SDL_PixelFormatDetails* const format = SDL_GetPixelFormatDetails(surface->format);
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    SDL_GetRGB(pixel, format, nullptr, &r, &g, &b);
    const auto linear = [](Uint8 c) {
      const double v = static_cast<double>(c) / 255.0;
      return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(r) + 0.7152 * linear(g) + 0.0722 * linear(b);
  }

  [[nodiscard]] Pixels picture(const pinball::Game& game, const Scene& scene) const {
    drawScene(renderer, table, layout, game, scene);
    Pixels pixels(static_cast<std::size_t>(surface->w) * static_cast<std::size_t>(surface->h));
    if (SDL_LockSurface(surface)) {
      const auto* bytes = static_cast<const std::uint8_t*>(surface->pixels);
      for (int y = 0; y < surface->h; ++y) {
        std::memcpy(pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->w),
                    bytes + static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->pitch),
                    static_cast<std::size_t>(surface->w) * sizeof(std::uint32_t));
      }
      SDL_UnlockSurface(surface);
    }
    return pixels;
  }

  // The rectangle's pixels, kept inside the picture: left, top, right, bottom.
  [[nodiscard]] std::array<int, 4> bounds(const pinball::screen::Rect& rect) const noexcept {
    const int left = std::clamp(static_cast<int>(std::ceil(rect.x)), 0, surface->w);
    const int top = std::clamp(static_cast<int>(std::ceil(rect.y)), 0, surface->h);
    return {left, top,
            std::clamp(static_cast<int>(std::floor(rect.x + rect.w)), left, surface->w),
            std::clamp(static_cast<int>(std::floor(rect.y + rect.h)), top, surface->h)};
  }

  // The share of the rectangle's pixels that differ between two pictures.
  [[nodiscard]] double changed(const Pixels& a, const Pixels& b,
                               const pinball::screen::Rect& rect) const noexcept {
    const auto [left, top, right, bottom] = bounds(rect);
    std::size_t different = 0;
    for (int y = top; y < bottom; ++y) {
      const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->w);
      for (int x = left; x < right; ++x) {
        const std::size_t at = row + static_cast<std::size_t>(x);
        different += a[at] != b[at] ? 1U : 0U;
      }
    }
    const double area = static_cast<double>(right - left) * static_cast<double>(bottom - top);
    return area > 0.0 ? static_cast<double>(different) / area : 0.0;
  }

  // The share of the rectangle in the colours captions, arrows and body
  // text are drawn in: light on a plain face and in body text, ink on a
  // button of art (platform/TouchOverlay.hpp).
  [[nodiscard]] double captionInk(const Pixels& a, const pinball::screen::Rect& rect) const {
    const SDL_PixelFormatDetails* const format = SDL_GetPixelFormatDetails(surface->format);
    const auto mapped = [format](SDL_Color c) {
      return SDL_MapRGBA(format, nullptr, c.r, c.g, c.b, c.a);
    };
    const std::uint32_t light = mapped(pinball::kCaptionPlain);
    const std::uint32_t ink = mapped(pinball::kCaptionOnArt);
    const auto [left, top, right, bottom] = bounds(rect);
    std::size_t count = 0;
    for (int y = top; y < bottom; ++y) {
      const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->w);
      for (int x = left; x < right; ++x) {
        const std::uint32_t pixel = a[row + static_cast<std::size_t>(x)];
        count += pixel == light || pixel == ink ? 1U : 0U;
      }
    }
    const double area = static_cast<double>(right - left) * static_cast<double>(bottom - top);
    return area > 0.0 ? static_cast<double>(count) / area : 0.0;
  }

  void expect(bool ok, const char* what) {
    if (!ok) {
      std::fprintf(stderr, "check failed: %s\n", what);
      ++failures;
    }
  }
  // At least this share of a rectangle must differ, or be lettering.
  static constexpr double kSome = 0.01;
  void differs(const Pixels& a, const Pixels& b, const pinball::screen::Rect& rect,
               const char* what) {
    expect(changed(a, b, rect) >= kSome, what);
  }
  void same(const Pixels& a, const Pixels& b, const pinball::screen::Rect& rect,
            const char* what) {
    expect(changed(a, b, rect) == 0.0, what);
  }
  // Lettered in one of the given colours. Art behind the text has many
  // colours of its own, so lettering is told from it by colour.
  void letteredIn(const Pixels& a, const pinball::screen::Rect& rect,
                  std::span<const SDL_Color> colors, const char* what) {
    const SDL_PixelFormatDetails* const format = SDL_GetPixelFormatDetails(surface->format);
    std::vector<std::uint32_t> wanted;
    for (const SDL_Color c : colors) {
      wanted.push_back(SDL_MapRGBA(format, nullptr, c.r, c.g, c.b, c.a));
    }
    const auto [left, top, right, bottom] = bounds(rect);
    std::size_t count = 0;
    for (int y = top; y < bottom; ++y) {
      const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->w);
      for (int x = left; x < right; ++x) {
        count += std::ranges::find(wanted, a[row + static_cast<std::size_t>(x)]) != wanted.end()
                     ? 1U
                     : 0U;
      }
    }
    const double area = static_cast<double>(right - left) * static_cast<double>(bottom - top);
    expect(area > 0.0 && static_cast<double>(count) / area >= kSome, what);
  }
  void captioned(const Pixels& a, const pinball::screen::Rect& rect, const char* what) {
    expect(captionInk(a, rect) >= kSome, what);
  }
  // A button: its caption, and, on a key of art, the face under it. The ink
  // caption must stand at least 3:1 against what is round it (the median of
  // the pixels two away from each ink pixel that are not ink), which a
  // caption over the card or the deck with no face drawn does not.
  void buttonCaptioned(const Pixels& a, const pinball::screen::Rect& rect, const char* what) {
    captioned(a, rect, what);
    if (table.overlayArt().button.face == nullptr) {
      return;
    }
    const SDL_PixelFormatDetails* const format = SDL_GetPixelFormatDetails(surface->format);
    const SDL_Color c = pinball::kCaptionOnArt;
    const std::uint32_t ink = SDL_MapRGBA(format, nullptr, c.r, c.g, c.b, c.a);
    const auto [left, top, right, bottom] = bounds(rect);
    const auto at = [&](int x, int y) {
      return a[static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->w) +
               static_cast<std::size_t>(x)];
    };
    std::vector<double> around;
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        if (at(x, y) != ink) {
          continue;
        }
        for (const auto& [dx, dy] : {std::pair{2, 0}, std::pair{-2, 0}, std::pair{0, 2},
                                    std::pair{0, -2}}) {
          const int nx = x + dx;
          const int ny = y + dy;
          if (nx >= left && nx < right && ny >= top && ny < bottom && at(nx, ny) != ink) {
            around.push_back(luminance(at(nx, ny)));
          }
        }
      }
    }
    if (around.empty()) {
      expect(false, what);
      return;
    }
    std::ranges::nth_element(around,
                             around.begin() + static_cast<std::ptrdiff_t>(around.size() / 2));
    const double face = around[around.size() / 2];
    const double contrast = (face + 0.05) / (luminance(ink) + 0.05);
    if (contrast < 3.0) {
      std::fprintf(stderr, "  %s: caption contrast %.2f\n", what, contrast);
    }
    expect(contrast >= 3.0, what);
  }
  // A dialog on the card of art: its chrome lip along the top edge, which
  // nothing else drawn there is as light as.
  void carded(const Pixels& a, const pinball::screen::Rect& box, const char* what) {
    if (table.overlayArt().dialog == nullptr) {
      return;
    }
    const auto [left, top, right, bottom] =
        bounds({box.x + 48.0F, box.y + 2.0F, box.w - 96.0F, 5.0F});
    std::size_t light = 0;
    for (int y = top; y < bottom; ++y) {
      for (int x = left; x < right; ++x) {
        light += luminance(a[static_cast<std::size_t>(y) * static_cast<std::size_t>(surface->w) +
                             static_cast<std::size_t>(x)]) >= 0.25
                     ? 1U
                     : 0U;
      }
    }
    const double area = static_cast<double>(right - left) * static_cast<double>(bottom - top);
    expect(area > 0.0 && static_cast<double>(light) / area >= 0.5, what);
  }
};

constexpr pinball::screen::Rect placed(const pinball::screen::Rect& r, float dx,
                                       float dy) noexcept {
  return {r.x + dx, r.y + dy, r.w, r.h};
}

// Runs every check for the layout. game is some way into play; fresh has
// not started. Returns the number of failures.
int checkLayout(Checker& check, const pinball::Game& game, const pinball::Game& fresh,
                const pinball::Game& multiball, const pinball::Game& graduation) {
  namespace screen = pinball::screen;
  const screen::Layout& layout = check.layout;
  const bool touch = layout.idiom == screen::Idiom::Touch;
  const bool stack = layout.composition == screen::Composition::Stack;
  using View = pinball::TouchControls::View;

  const Pixels play = check.picture(game, {});
  const Pixels again = check.picture(game, {});
  check.same(play, again, {0.0F, 0.0F, layout.canvasWidth, layout.canvasHeight},
             "the same scene draws the same picture twice");

  // --- The status display: each window lettered, and following the game.
  const float hx = stack ? layout.hud.x : layout.hud.x - pinball::panel::kLeft;
  const float hy = layout.hud.y;
  const screen::Rect score = placed(stack ? pinball::strip::kScoreWell : pinball::panel::kScoreWell, hx, hy);
  const screen::Rect status = placed(stack ? pinball::strip::kStatusWell : pinball::panel::kStatusWell, hx, hy);
  const screen::Rect rank = placed(stack ? pinball::strip::kRankWell : pinball::panel::kRankWell, hx, hy);
  const screen::Rect course = placed(stack ? pinball::strip::kCourseWell : pinball::panel::kCourseWell, hx, hy);
  const screen::Rect meter = placed(stack ? pinball::strip::kMeterWell : pinball::panel::kMeterWell, hx, hy);
  const screen::Rect message = placed(stack ? pinball::strip::kMessageWell : pinball::panel::kMessageWell, hx, hy);
  // The course lamps: a window of their own on the strip, the foot of the
  // course window on the panel.
  const screen::Rect lamps =
      stack ? placed(pinball::strip::kLampWell, hx, hy)
            : screen::Rect{course.x, course.y + course.h - 50.0F, course.w, 50.0F};
  check.letteredIn(play, score, pinball::kHudLettering, "score window has lettering");
  check.letteredIn(play, status, pinball::kHudLettering, "status window has lettering");
  check.letteredIn(play, rank, pinball::kHudLettering, "rank window has lettering");
  check.letteredIn(play, course, pinball::kHudLettering, "course window has lettering");
  const Pixels start = check.picture(fresh, {});
  check.differs(play, start, score, "score window follows the score");
  check.differs(play, start, message, "message window follows the message");
  const Pixels running = check.picture(game, {.state = State::Longest, .message = "CLASS IN SESSION"});
  check.differs(play, running, course, "course window shows a running course");
  check.letteredIn(running, message, pinball::kHudLettering, "message window shows a message");
  const Pixels many = check.picture(multiball, {});
  for (const pinball::BallView& ball : multiball.balls()) {
    check.differs(start, many,
                  {layout.playfield.x + ball.position.x - 12.0F,
                   layout.playfield.y + ball.position.y - 12.0F, 24.0F, 24.0F},
                  "each ball of a multiball is drawn on the table");
  }
  const Pixels graduating = check.picture(graduation, {});
  check.differs(play, graduating, meter, "meter window follows the doors and the turbo meter");
  check.differs(start, graduating, lamps, "course lamps show the courses passed");
  check.differs(start, graduating, rank, "rank window follows the rank");

  // --- The touch controls.
  if (touch) {
    const Pixels bare = check.picture(game, {.withoutControls = true});
    View all{};
    all.left = all.right = all.centre = all.nudgeLeft = all.nudgeRight = all.pause = true;
    const Pixels held = check.picture(game, {.pressed = all});
    // What is drawn of each control lies inside what a touch may land in.
    const std::array<std::pair<screen::Rect, const char*>, 4> buttons{{
        {layout.pauseButton, "pause button"},
        {layout.nudgeLeftButton, "left nudge button"},
        {layout.centreButton, "centre button"},
        {layout.nudgeRightButton, "right nudge button"},
    }};
    for (const auto& [rect, name] : buttons) {
      std::string what = std::string(name) + " is drawn";
      check.differs(bare, play, rect, what.c_str());
      what = std::string(name) + " has its caption or mark";
      check.buttonCaptioned(play, rect, what.c_str());
      what = std::string(name) + " looks pressed when held";
      check.differs(play, held, rect, what.c_str());
    }
    check.differs(play, start, layout.centreButton, "centre button says what it will do");
    if (stack) {
      const screen::Rect left = placed(pinball::deck::kLeftCorner, layout.deck.x, layout.deck.y);
      const screen::Rect right = placed(pinball::deck::kRightCorner, layout.deck.x, layout.deck.y);
      check.differs(bare, play, layout.deck, "deck is drawn");
      check.differs(play, held, left, "left flipper pad lights when held");
      check.differs(play, held, right, "right flipper pad lights when held");
      // The corners are their own: not part of a button's face.
      View leftOnly{};
      leftOnly.left = true;
      const Pixels one = check.picture(game, {.pressed = leftOnly});
      check.same(play, one, layout.nudgeLeftButton, "a held flipper leaves the nudge button alone");
      check.same(play, one, right, "the left flipper leaves the right pad alone");
    } else {
      const screen::Rect corner =
          placed(pinball::panel::kFlipperCorner, layout.playfield.x, layout.playfield.y);
      check.differs(bare, play, corner, "flipper corner is marked");
      check.differs(play, held, corner, "flipper corner lights when held");
      check.same(bare, held, layout.playfield, "nothing is drawn over the table");
    }
  }

  // --- Pause. The table is dimmed either way; the menu or the word must
  // be there on top of that.
  const Pixels dimmed = check.picture(game, {.state = State::Pause, .withoutDialog = true});
  const Pixels paused = check.picture(game, {.state = State::Pause});
  check.differs(play, dimmed, layout.playfield, "the paused table is dimmed");
  if (touch) {
    const screen::MenuLayout menu = screen::menuLayout(layout);
    check.differs(dimmed, paused, menu.box, "pause menu is drawn");
    check.carded(paused, menu.box, "pause menu is on its card");
    const std::array<std::pair<screen::Rect, const char*>, 5> rows{{
        {menu.resume, "menu: resume"},
        {menu.newGame, "menu: new game"},
        {menu.sound, "menu: sound"},
        {menu.music, "menu: music"},
        {menu.about, "menu: scores and about"},
    }};
    for (const auto& [rect, name] : rows) {
      check.differs(dimmed, paused, rect, name);
      check.buttonCaptioned(paused, rect, name);
    }
    check.differs(dimmed, paused, layout.hud, "pausing dims the status display too");
    const Pixels confirm = check.picture(game, {.state = State::Confirm});
    check.differs(paused, confirm, menu.newGame, "menu: new game asks to confirm");
    check.same(paused, confirm, menu.resume, "menu: confirming changes only its own button");
    const Pixels interrupted = check.picture(game, {.state = State::Interrupted});
    check.differs(paused, interrupted, menu.box, "after an interruption there is no menu");
    check.differs(dimmed, interrupted, layout.playfield, "after an interruption the table says so");
  } else {
    check.differs(dimmed, paused, {layout.playfield.x + 150.0F, layout.playfield.y + 420.0F,
                                   468.0F, 130.0F}, "PAUSED is drawn over the table");
  }

  // --- About: every page its own, and the licences more than one.
  const screen::AboutLayout about = screen::aboutLayout(layout);
  const Pixels scores = check.picture(game, {.state = State::About});
  const Pixels info = check.picture(game, {.state = State::AboutInfo});
  const Pixels licences = check.picture(game, {.state = State::Licences});
  check.differs(play, scores, about.box, "About is drawn");
  check.carded(scores, about.box, "About is on its card");
  check.differs(scores, info, about.box, "About: scores and about pages differ");
  check.differs(info, licences, about.box, "About: about and licence pages differ");
  check.differs(scores, licences, about.box, "About: scores and licence pages differ");
  // The licence text itself, where every layout has it.
  check.captioned(licences, about.body, "About: the licence page has its text");
  const std::size_t pages = pinball::licencePageCount(layout);
  check.expect(pages >= 2, "the licences take more than one page");
  const Pixels last = check.picture(game, {.state = State::Licences, .licencePage = pages - 1});
  check.differs(licences, last, about.body, "About: first and last licence pages differ");
  check.captioned(last, about.body, "About: the last licence page has its text");
  const Pixels beyond = check.picture(game, {.state = State::Licences, .licencePage = pages + 7});
  check.same(last, beyond, about.box, "About: a page past the last is the last");
  if (touch) {
    for (const screen::Rect& tab : about.tabs) {
      check.buttonCaptioned(scores, tab, "About: tab has a caption");
    }
    check.differs(scores, info, about.tabs[0], "About: the tab of the page shown is marked");
    check.buttonCaptioned(scores, about.close, "About: close button");
    check.buttonCaptioned(licences, about.previous, "About: previous page button");
    check.buttonCaptioned(licences, about.next, "About: next page button");
    check.differs(licences, last, about.next, "About: next is greyed on the last page");
    check.differs(licences, last, about.previous, "About: previous is greyed on the first page");
    check.same(scores, info, about.close, "About: close stays put");
  }

  // --- Initials.
  const screen::InitialsLayout initials = screen::initialsLayout(layout);
  const Pixels entry = check.picture(game, {.state = State::Initials});
  check.differs(play, entry, initials.box, "initials prompt is drawn");
  check.carded(entry, initials.box, "initials prompt is on its card");
  for (const screen::Rect& slot : initials.slot) {
    check.letteredIn(entry, slot, pinball::kInitialsLettering, "initials: a letter in each slot");
  }
  if (touch) {
    for (std::size_t i = 0; i < initials.up.size(); ++i) {
      check.buttonCaptioned(entry, initials.up[i], "initials: up arrow");
      check.buttonCaptioned(entry, initials.down[i], "initials: down arrow");
    }
    check.buttonCaptioned(entry, initials.ok, "initials: OK button");
  }

  // Leave the play frame on the canvas: it is the picture written.
  (void)check.picture(game, {});
  return check.failures;
}

int run(const Request& request) {
  namespace screen = pinball::screen;
  const screen::Layout layout = screen::computeLayout(request.surface, request.idiom);
  const bool touch = layout.idiom == screen::Idiom::Touch;
  // Refused before anything is loaded or drawn.
  if (layout.canvasWidth > kMaxPictureSide || layout.canvasHeight > kMaxPictureSide) {
    std::fputs("that window needs a larger canvas than this tool draws\n", stderr);
    return EXIT_FAILURE;
  }
  const float scaledWidth = request.surface.width * static_cast<float>(request.scale);
  const float scaledHeight = request.surface.height * static_cast<float>(request.scale);
  if (scaledWidth > kMaxPictureSide || scaledHeight > kMaxPictureSide) {
    std::fputs("that window is too large to draw at that scale\n", stderr);
    return EXIT_FAILURE;
  }

  pinball::AssetLoader assets = pinball::AssetLoader::fromExecutableLocation();
  if (!assets.verifyAll()) {
    std::fputs("asset integrity check failed\n", stderr);
    return EXIT_FAILURE;
  }

  // The frame is the layout's canvas, unit for pixel.
  const std::unique_ptr<SDL_Surface, SurfaceDeleter> surface(
      SDL_CreateSurface(static_cast<int>(std::lround(layout.canvasWidth)),
                        static_cast<int>(std::lround(layout.canvasHeight)),
                        SDL_PIXELFORMAT_RGBA32));
  if (!surface) {
    std::fprintf(stderr, "SDL_CreateSurface failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }

  {
    // Declared after the surface so it is destroyed first.
    const std::unique_ptr<SDL_Renderer, RendererDeleter> renderer(
        SDL_CreateSoftwareRenderer(surface.get()));
    if (!renderer) {
      std::fprintf(stderr, "SDL_CreateSoftwareRenderer failed: %s\n", SDL_GetError());
      return EXIT_FAILURE;
    }
    SDL_Renderer* const r = renderer.get();
    const std::unique_ptr<pinball::TableRenderer> table = pinball::TableRenderer::create(
        r, assets, touch ? pinball::TextureSet::All : pinball::TextureSet::Desktop);
    if (!table) {
      return EXIT_FAILURE;
    }

    pinball::Game game;
    std::uint32_t step = 0;
    if (request.state == State::GameOver) {
      // Lose every ball first; the steps asked for come after that.
      constexpr std::uint32_t kGiveUp = 400'000;
      while (!game.gameOver() && step < kGiveUp) {
        game.step(losingInput(step++));
      }
      if (!game.gameOver()) {
        std::fputs("the scripted game did not end\n", stderr);
        return EXIT_FAILURE;
      }
    }
    // The store's scenes are set up first; the steps asked for follow,
    // with nothing pressed.
    std::optional<pinball::Scene> staged;
    switch (request.state) {
      case State::Table:
        staged = pinball::Scene::Table;
        break;
      case State::Course:
        staged = pinball::Scene::Course;
        break;
      case State::Multiball:
        staged = pinball::Scene::Multiball;
        break;
      case State::Hangar:
        staged = pinball::Scene::Hangar;
        break;
      case State::Graduation:
        staged = pinball::Scene::Graduation;
        break;
      default:
        break;
    }
    if (staged && !pinball::stageScene(game, *staged)) {
      std::fputs("the scene could not be set up\n", stderr);
      return EXIT_FAILURE;
    }
    const bool idle =
        request.state == State::GameOver || request.state == State::Launch || staged.has_value();
    for (std::uint32_t i = 0; i < request.steps; ++i) {
      game.step(idle ? pinball::InputState{} : scriptedInput(step++));
    }

    if (request.state == State::Locked || request.state == State::BankDown) {
      // What scripted play does not reach by itself is set up by hand,
      // through the seam the tests use: the three doors are marked, as
      // three hits would, and for the lock a ball is sent into the Hangar.
      using Seam = pinball::GameTestAccess;
      for (std::size_t door = 0; door < pinball::table::kDropTargets.size(); ++door) {
        Seam::rules(game).onDropTarget(door);
      }
      game.step(pinball::InputState{});
      if (request.state == State::Locked) {
        const std::size_t slot = game.ball().id;
        Seam::placeBall(game, slot, {279.0F, 440.0F}, {0.0F, -900.0F});
        Seam::putInPlay(game, slot);
        for (int i = 0; i < 240 && game.lockedBalls() == 0; ++i) {
          game.step(pinball::InputState{});
        }
      }
      for (int i = 0; i < 30; ++i) {
        game.step(pinball::InputState{});
      }
      const bool reached = request.state == State::Locked
                               ? game.lockedBalls() == 1
                               : !game.doorBankUp();
      if (!reached) {
        std::fputs("the scene could not be set up\n", stderr);
        return EXIT_FAILURE;
      }
    }

    if (request.state == State::Spinning) {
      // A ball sent fast up the Turbine lane, through the spinner, and the
      // blade caught a quarter turn round, when it stands on its edge.
      using Seam = pinball::GameTestAccess;
      const std::size_t slot = game.ball().id;
      const pinball::Vec2 lane = (pinball::table::kSpinner.a + pinball::table::kSpinner.b) * 0.5F;
      Seam::placeBall(game, slot, {lane.x, lane.y + 30.0F}, {0.0F, -1500.0F});
      Seam::putInPlay(game, slot);
      for (int i = 0; i < 240 && (game.spinCount() == 0 || game.spinnerPhase() != 0.25F); ++i) {
        game.step(pinball::InputState{});
      }
      if (game.spinnerPhase() != 0.25F) {
        std::fputs("the scene could not be set up\n", stderr);
        return EXIT_FAILURE;
      }
    }

    if (request.state == State::Check) {
      const pinball::Game fresh;
      if (game.rules().score() == fresh.rules().score() || !game.rules().ballInPlay()) {
        std::fputs("check needs enough steps for a ball in play and a score: try 1200\n", stderr);
        return EXIT_FAILURE;
      }
      Checker checker{surface.get(), r, *table, layout};
      pinball::Game multiball;
      pinball::Game graduation;
      if (!pinball::stageScene(multiball, pinball::Scene::Multiball) ||
          !pinball::stageScene(graduation, pinball::Scene::Graduation)) {
        std::fputs("the scenes could not be set up\n", stderr);
        return EXIT_FAILURE;
      }
      if (const int failures = checkLayout(checker, game, fresh, multiball, graduation);
          failures > 0) {
        std::fprintf(stderr, "%d check(s) failed for this layout\n", failures);
        return EXIT_FAILURE;
      }
    } else {
      drawScene(r, *table, layout, game,
                Scene{.state = request.state,
                      .pressed = request.pressed,
                      .licencePage = request.licencePage,
                      .message = request.message,
                      .zones = request.zones});
    }
  }

  if (!hasContent(surface.get(), layout.playfield)) {
    std::fputs("rendered frame is blank\n", stderr);
    return EXIT_FAILURE;
  }
  // At a device's scale the picture is the window, that many pixels to the
  // point.
  std::unique_ptr<SDL_Surface, SurfaceDeleter> scaled;
  SDL_Surface* picture = surface.get();
  if (request.scale > 0) {
    scaled.reset(SDL_CreateSurface(static_cast<int>(std::lround(scaledWidth)),
                                   static_cast<int>(std::lround(scaledHeight)),
                                   SDL_PIXELFORMAT_RGBA32));
    if (!scaled || !SDL_BlitSurfaceScaled(surface.get(), nullptr, scaled.get(), nullptr,
                                          SDL_SCALEMODE_LINEAR)) {
      std::fprintf(stderr, "scaling the picture failed: %s\n", SDL_GetError());
      return EXIT_FAILURE;
    }
    picture = scaled.get();
  }
  // Never write through whatever already has this name, be it a file, a
  // link to one or a link that points nowhere: the picture is encoded in
  // memory and put in place by replaceFile, which writes a new file beside
  // the name and renames it over. (Asking first whether the name is taken
  // would follow a link, and call a dangling one free.)
  const std::unique_ptr<SDL_IOStream, IoCloser> encoded(SDL_IOFromDynamicMem());
  if (!encoded || !SDL_SavePNG_IO(picture, encoded.get(), false)) {
    std::fprintf(stderr, "SDL_SavePNG_IO failed: %s\n", SDL_GetError());
    return EXIT_FAILURE;
  }
  const auto* const data = static_cast<const char*>(SDL_GetPointerProperty(
      SDL_GetIOProperties(encoded.get()), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr));
  const Sint64 size = SDL_GetIOSize(encoded.get());
  if (data == nullptr || size <= 0) {
    std::fputs("encoding the PNG produced nothing\n", stderr);
    return EXIT_FAILURE;
  }
  // A picture is for opening and attaching: ordinary permissions, not the
  // score file's owner-only ones.
  if (!pinball::replaceFile(request.output, std::string_view(data, static_cast<std::size_t>(size)),
                            pinball::FileAccess::Shared)) {
    std::fprintf(stderr, "cannot write %s\n", request.output.c_str());
    return EXIT_FAILURE;
  }
  std::printf("wrote %s after %u steps (%d x %d)\n", request.output.c_str(),
              static_cast<unsigned>(request.steps), picture->w, picture->h);
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char* argv[]) {
  const auto request =
      parseArguments(std::span<char* const>(argv, static_cast<std::size_t>(std::max(argc, 0))));
  if (!request) {
    std::fputs(kUsage, stderr);
    return EXIT_FAILURE;
  }

  int exitCode = EXIT_FAILURE;
  try {
    exitCode = run(*request);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "Fatal: %s\n", e.what());
  } catch (...) {
    std::fputs("Fatal: unknown error\n", stderr);
  }
  SDL_Quit();
  return exitCode;
}
