#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/Rules.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

// The table's light show: what every lamp, flash, flasher and the general
// illumination are doing, worked out from the game's state alone. No SDL:
// unit tested in core_tests; src/platform/TableEffects.* draws it.
//
// It only reads the game. Call observe(game) after every Game::step (every
// step, not every frame: a hit inside a frame of two steps still flashes),
// then ask what to show. Everything it answers is a function of the steps
// it has observed, so a replay of the same game gives the same lights, step
// for step, and nothing it does can change the game.
//
// Photosensitivity (WCAG 2.3.1): anything that lights or darkens a large
// part of the table (the general illumination, a wash over the whole table,
// a flasher of a large radius) is a "large flash", and no two of those start
// within kMinLargeFlashSteps of each other: never more than three in any one
// second. With reduce motion on, there is no strobe or flicker at all: lamps
// that blink breathe instead, bursts rise and fall slowly, sweeps and chases
// are steady glows, and large flashes come at most once a second.
namespace pinball {

class Game;

namespace fx {

// Blinking lamps, in steps of the game's clock (120 a second).
inline constexpr std::uint64_t kBlinkSteps = 60;      // Twice a second.
inline constexpr std::uint64_t kFastBlinkSteps = 24;  // Five times a second: hurry up.
// The shortest gap between the starts of two large flashes: 42 steps is
// 2.86 a second, under the three of WCAG 2.3.1.
inline constexpr std::uint64_t kMinLargeFlashSteps = 42;
// With reduce motion: well under once a second.
inline constexpr std::uint64_t kMinLargeFadeSteps = 150;
// Steps a ball's trail remembers.
inline constexpr std::size_t kTrailLength = 8;
// The most an unlit lamp glimmers in the quiet table's slow wave: a lit
// lamp is 1, and at least four times this.
inline constexpr float kAmbientMax = 0.22F;
// Bursts in flight at once (pending or lit).
inline constexpr std::size_t kMaxBursts = 24;

// The colours the light show works in; the renderer gives each its neon
// (generated/ArtPalette.hpp, kFx*).
enum class Colour : std::uint8_t { Cyan, Magenta, Lime, Orange, Yellow, Violet, White, Warm };

// Where a flasher dome sits: each over an element of the table
// (TableLayout.hpp), so it follows the element if the layout moves it.
// Table is the wash over the whole playfield; Lights is the general
// illumination itself (a dip in it).
enum class Flasher : std::uint8_t {
  Hangar,     // kHoles[kHangarHole]
  Briefing,   // kHoles[kScoopHole]
  RampLeft,   // kHoles[kRunwayHole]
  RampRight,  // kHoles[kSkywayHole]
  Nest,       // the middle of kBumpers
  Academy,    // the Graduation lamp, in the middle of the course lamps
  TopLanes,   // the middle of the roll call rollovers
  Turbine,    // the spinner
  Apron,      // the Shoot Again lamp
  Table,      // the whole playfield
  Lights,     // the general illumination dips
  Count,
};
[[nodiscard]] Vec2 flasherPosition(Flasher flasher) noexcept;

// A lamp in this state at this step, 0 to 1. Blinks are square with a short
// rise and fall, as a bulb warms and cools; with reduce motion they breathe.
[[nodiscard]] float lampLevel(LampState state, std::uint64_t step, bool reduceMotion) noexcept;

// The big moments, each with its own show.
enum class Show : std::uint8_t {
  None,
  MultiballStart,  // Blackout, a sweep up the table, then three pulses.
  Graduation,      // The same in gold.
  Jackpot,         // A ring of light out from the ball and a wash.
  SuperJackpot,    // Two rings and two washes.
  Drain,           // The lights dim and flicker; the bonus counts down.
  Tilt,            // The lights flicker and go dark until the ball drains.
  GameOver,        // The lights fade down.
};

// A flasher burst as it is to be drawn now.
struct BurstView {
  Flasher where = Flasher::Nest;
  Colour colour = Colour::White;
  float level = 0.0F;   // 0 to 1, times the burst's peak already.
  float radius = 0.0F;  // Playfield units; 0 for Table and Lights.
};

// Fixed capacity, no allocation.
struct BurstViews {
  std::array<BurstView, kMaxBursts> items{};
  std::size_t count = 0;
  [[nodiscard]] std::span<const BurstView> span() const& noexcept {
    return std::span<const BurstView>(items).first(count);
  }
  std::span<const BurstView> span() const&& = delete;
};

// The trail behind one ball: where it was at the latest steps, newest first.
struct TrailView {
  std::array<Vec2, kTrailLength> points{};
  std::size_t count = 0;
  float speed = 0.0F;  // Playfield units per step over the trail.
};

// A shot arrow's chase: three chevrons running toward the shot while its
// arrow blinks.
struct ChaseView {
  bool active = false;
  float phase = 0.0F;   // 0 to 1, the leading chevron's place.
  bool steady = false;  // Reduce motion: all three lit, no running.
  bool fast = false;    // The arrow fast blinks (hurry up): runs twice as fast.
};

class LightShow {
 public:
  static constexpr std::size_t kLampCount = table::kLamps.size();


  // --- Settings -------------------------------------------------------------
  // The one setter for reduce motion (the OS setting or the game's own).
  void setReduceMotion(bool on) noexcept { reduceMotion_ = on; }
  [[nodiscard]] bool reduceMotion() const noexcept { return reduceMotion_; }

  // Attract mode, for the title screen while the table plays itself: the
  // lamps run cycling chases and sweeps of their own, whatever the rules
  // say, and the flashers fire now and then. Its clock is the steps
  // observed since it was turned on.
  void setAttract(bool on) noexcept;
  [[nodiscard]] bool attract() const noexcept { return attract_; }
  // Which of the attract patterns is running, 0 to kAttractPatterns - 1.
  static constexpr int kAttractPatterns = 5;
  static constexpr std::uint64_t kAttractPatternSteps = 720;  // Six seconds each.
  [[nodiscard]] int attractPattern() const noexcept;

  // Forget everything seen: for a new Game object.
  void reset() noexcept;

  // --- Feeding it -----------------------------------------------------------
  // Reads the step the game has just made. A game whose step count went
  // backwards is a new one: the show starts afresh.
  void observe(const Game& game) noexcept;

  // --- What to show ---------------------------------------------------------
  [[nodiscard]] std::uint64_t step() const noexcept { return now_; }
  // Each lamp's brightness, 0 to 1, in LampId order.
  [[nodiscard]] float lampLevel(table::LampId lamp) const noexcept;
  [[nodiscard]] const std::array<float, kLampCount>& lampLevels() const noexcept {
    return levels_;
  }
  // The state the rules gave each lamp at the latest step.
  [[nodiscard]] LampState lampState(table::LampId lamp) const noexcept;
  // A bay door's face lamp, 0 to 1, and its flash when struck.
  [[nodiscard]] float doorLevel(std::size_t door) const noexcept;
  [[nodiscard]] float doorFlash(std::size_t door) const noexcept;
  // Flashes on a hit, 1 at the hit and fading: turbo targets, rollovers,
  // holes (on entry and on kickout), lock bays, the kickback.
  [[nodiscard]] float targetFlash(std::size_t target) const noexcept;
  [[nodiscard]] float rolloverFlash(std::size_t rollover) const noexcept;
  [[nodiscard]] float holeFlash(std::size_t hole) const noexcept;
  [[nodiscard]] float bayFlash(std::size_t bay) const noexcept;
  [[nodiscard]] float kickbackFlash() const noexcept;
  // The spinner's lamps: brighter the faster it spins, strobing with each
  // turn (steady with reduce motion).
  [[nodiscard]] float spinnerLevel() const noexcept;
  // The general illumination, 0 (dark) to 1 (full).
  [[nodiscard]] float giLevel() const noexcept { return gi_; }
  // The arrow lamps' chases, in LampId order (the first seven lamps).
  [[nodiscard]] ChaseView chase(std::size_t arrow) const noexcept;
  // The running show and how far into it, in steps.
  [[nodiscard]] Show show() const noexcept { return show_; }
  [[nodiscard]] std::uint64_t showSteps() const noexcept { return now_ - showStart_; }
  // The multiball sweep: a band of light climbing the table. Negative when
  // there is none.
  [[nodiscard]] float sweepY() const noexcept { return sweepY_; }
  [[nodiscard]] Colour sweepColour() const noexcept { return sweepColour_; }
  // A jackpot's ring of light: its centre and radius, radius negative when
  // there is none.
  [[nodiscard]] Vec2 ringCentre() const noexcept { return ringCentre_; }
  [[nodiscard]] float ringRadius() const noexcept { return ringRadius_; }
  [[nodiscard]] Colour ringColour() const noexcept { return ringColour_; }
  // Every burst lit now.
  [[nodiscard]] BurstViews bursts() const noexcept;
  // A ball's trail, by slot.
  [[nodiscard]] const TrailView& trail(std::size_t slot) const noexcept;

  // --- For the tests ----------------------------------------------------------
  // How bright the large flashes are now, 0 to 1: the wash, the dips of the
  // general illumination, the large flashers. What the 3 Hz rule counts.
  [[nodiscard]] float largeFlashLevel() const noexcept;
  // Large flashes started, and those refused or dropped by the limiter.
  [[nodiscard]] std::uint32_t largeFlashes() const noexcept { return largeFlashes_; }
  [[nodiscard]] std::uint32_t largeFlashesHeld() const noexcept { return largeHeld_; }
  // Bursts started, of any size.
  [[nodiscard]] std::uint32_t burstsStarted() const noexcept { return burstsStarted_; }

  // Starts a burst (Table and Lights are always large; a dome is large if
  // its radius is kLargeRadius or more). delay: steps from now. Public so
  // that the tests can drive the limiter directly.
  static constexpr float kLargeRadius = 120.0F;
  void burst(Flasher where, Colour colour, float peak, float radius, std::uint32_t length,
             std::uint32_t delay = 0) noexcept;
  // Starts a show now, unless one that matters more is running.
  void startShow(Show show, Vec2 at = {}) noexcept;

 private:
  struct Burst {
    Flasher where = Flasher::Nest;
    Colour colour = Colour::White;
    bool used = false;
    bool started = false;
    bool large = false;
    float peak = 0.0F;
    float radius = 0.0F;
    std::uint64_t start = 0;  // The step it starts, or started.
    std::uint32_t length = 0;
    std::uint32_t late = 0;  // Steps it has waited for the limiter.
  };
  // A set of lamps lighting in turn.
  struct Sweep {
    std::uint64_t start = 0;
    std::uint8_t group = 0;
    bool used = false;
  };
  static constexpr std::size_t kMaxSweeps = 6;

  void readEvents(const Game& game) noexcept;
  void readSounds(const Game& game) noexcept;
  void updateBursts() noexcept;
  void updateShow(const Game& game) noexcept;
  void updateLamps(const Game& game) noexcept;
  void updateTrails(const Game& game) noexcept;
  void sweepGroup(std::uint8_t group, std::uint32_t delay = 0) noexcept;
  [[nodiscard]] float burstLevel(const Burst& b) const noexcept;
  [[nodiscard]] float flash(std::uint64_t hitStep, std::uint32_t length) const noexcept;
  [[nodiscard]] float attractLevel(std::size_t lamp) const noexcept;
  [[nodiscard]] float showLevel(std::size_t lamp) const noexcept;
  [[nodiscard]] float sweepLevel(std::size_t lamp) const noexcept;
  [[nodiscard]] float ambientLevel(std::size_t lamp) const noexcept;

  bool reduceMotion_ = false;
  bool attract_ = false;
  bool seen_ = false;
  std::uint64_t attractStart_ = 0;
  std::uint64_t now_ = 0;

  std::array<LampState, kLampCount> states_{};
  std::array<float, kLampCount> levels_{};
  std::array<float, table::kDropTargets.size()> doorLevels_{};
  // One more than the step of the latest hit; 0 for never.
  std::array<std::uint64_t, table::kDropTargets.size()> doorHit_{};
  std::array<std::uint64_t, table::kTargets.size()> targetHit_{};
  std::array<std::uint64_t, table::kRollovers.size()> rolloverHit_{};
  std::array<std::uint64_t, table::kHoles.size()> holeHit_{};
  std::array<std::uint64_t, table::kLockBays.size()> bayHit_{};
  std::uint64_t kickbackHit_ = 0;
  std::uint64_t spinHit_ = 0;
  float spinEnergy_ = 0.0F;

  std::array<Burst, kMaxBursts> bursts_{};
  std::array<Sweep, kMaxSweeps> sweeps_{};
  std::uint64_t lastLarge_ = 0;  // One more than the step of the latest large flash.
  std::uint32_t largeFlashes_ = 0;
  std::uint32_t largeHeld_ = 0;
  std::uint32_t burstsStarted_ = 0;

  Show show_ = Show::None;
  std::uint64_t showStart_ = 0;
  Vec2 showAt_{};
  float gi_ = 1.0F;
  float sweepY_ = -1.0F;
  Colour sweepColour_ = Colour::Magenta;
  Vec2 ringCentre_{};
  float ringRadius_ = -1.0F;
  Colour ringColour_ = Colour::Yellow;

  // What the rules said at the step before, for the edges.
  bool wasTilted_ = false;
  bool wasGameOver_ = false;
  Multiball wasMultiball_ = Multiball::None;

  std::array<TrailView, table::kMaxBalls> trails_{};
};

}  // namespace fx

using fx::LightShow;

}  // namespace pinball
