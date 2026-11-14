#include "core/LightShow.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

#include "core/Game.hpp"
#include "core/Physics.hpp"
#include "core/Sound.hpp"

namespace pinball::fx {

namespace {

using table::LampId;

constexpr float kTau = 2.0F * table::kPi;

constexpr std::size_t lampIndex(LampId id) noexcept { return static_cast<std::size_t>(id); }

constexpr Vec2 centreOf(const auto& points) noexcept {
  Vec2 sum{};
  for (const auto& p : points) {
    sum = sum + p;
  }
  return sum * (1.0F / static_cast<float>(points.size()));
}

// The lamps that light together: a sweep runs through one group in order.
struct Group {
  std::array<LampId, 8> ids{};
  std::size_t size = 0;
};
enum : std::uint8_t {
  kGroupRollCall,
  kGroupWing,
  kGroupTurbo,
  kGroupHangar,
  kGroupCourses,
  kGroupArrows,
  kGroupApron,
  kGroupCount
};
constexpr std::array<Group, kGroupCount> kGroups{{
    {{LampId::RollCall1, LampId::RollCall2, LampId::RollCall3}, 3},
    {{LampId::WingW, LampId::WingI, LampId::WingN, LampId::WingG}, 4},
    {{LampId::TurboCharge, LampId::Overdrive, LampId::ArrowTurbine}, 3},
    {{LampId::Lock1, LampId::Lock2, LampId::ArrowHangar}, 3},
    {{LampId::Course1, LampId::Course2, LampId::Course3, LampId::Course4, LampId::Course5,
      LampId::Course6, LampId::Graduation},
     7},
    {{LampId::ArrowCircuitLeft, LampId::ArrowRunway, LampId::ArrowHangar, LampId::ArrowBriefing,
      LampId::ArrowTurbine, LampId::ArrowSkyway, LampId::ArrowCircuitRight},
     7},
    {{LampId::ShootAgain, LampId::ExtraCredit, LampId::ExtraBall}, 3},
}};
// Each lamp of a sweep lights this many steps after the one before it.
constexpr std::uint64_t kSweepStride = 6;
constexpr std::uint64_t kSweepFinale = 60;  // Three pulses at the end.

// The end-of-ball bonus counts down through these, last lit first out.
constexpr std::array<LampId, 18> kBonusLamps{{
    LampId::WingG, LampId::WingN, LampId::WingI, LampId::WingW, LampId::RollCall3,
    LampId::RollCall2, LampId::RollCall1, LampId::TurboCharge, LampId::Overdrive,
    LampId::Graduation, LampId::Course6, LampId::Course5, LampId::Course4, LampId::Course3,
    LampId::Course2, LampId::Course1, LampId::ExtraCredit, LampId::ExtraBall,
}};
constexpr std::uint64_t kBonusStart = 10;
constexpr std::uint64_t kBonusStride = 7;
constexpr std::uint64_t kBonusHold = 30;

// Show lengths, in steps.
constexpr std::uint64_t kBlackoutSteps = 36;
constexpr std::uint64_t kSweepSteps = 84;
constexpr std::uint64_t kMultiballShowSteps = 270;
constexpr std::uint64_t kJackpotShowSteps = 90;
constexpr std::uint64_t kSuperShowSteps = 150;
constexpr std::uint64_t kDrainShowSteps = 300;
constexpr std::uint64_t kPulseSpacing = 45;  // Over kMinLargeFlashSteps: never refused.

int priority(Show show) noexcept {
  switch (show) {
    case Show::None:
      return 0;
    case Show::Drain:
      return 1;
    case Show::Jackpot:
      return 2;
    case Show::MultiballStart:
      return 3;
    case Show::Graduation:
      return 4;
    case Show::SuperJackpot:
      return 5;
    case Show::Tilt:
      return 6;
    case Show::GameOver:
      return 7;
  }
  return 0;
}

std::uint64_t showLength(Show show) noexcept {
  switch (show) {
    case Show::MultiballStart:
    case Show::Graduation:
      return kMultiballShowSteps;
    case Show::Jackpot:
      return kJackpotShowSteps;
    case Show::SuperJackpot:
      return kSuperShowSteps;
    case Show::Drain:
      return kDrainShowSteps;
    case Show::None:
    case Show::Tilt:
    case Show::GameOver:
      break;
  }
  return 0;  // Until the rules let it go.
}

// The lamp an element lights when struck.
constexpr std::array<LampId, table::kHoles.size()> kHoleLamps{
    {LampId::ArrowBriefing, LampId::ArrowHangar, LampId::ArrowRunway, LampId::ArrowSkyway}};
static_assert(table::kScoopHole == 0 && table::kHangarHole == 1 && table::kRunwayHole == 2 &&
              table::kSkywayHole == 3);
constexpr std::array<LampId, table::kRollovers.size()> kRolloverLamps{
    {LampId::RollCall1, LampId::RollCall2, LampId::RollCall3, LampId::WingW, LampId::WingI,
     LampId::WingN, LampId::WingG}};

float clamp01(float v) noexcept { return v > 0.0F ? std::min(v, 1.0F) : 0.0F; }

float bell(float distance, float width) noexcept {
  const float u = distance / width;
  return std::exp(-u * u);
}

}  // namespace

Vec2 flasherPosition(Flasher flasher) noexcept {
  switch (flasher) {
    case Flasher::Hangar:
      return table::kHoles[table::kHangarHole].center;
    case Flasher::Briefing:
      return table::kHoles[table::kScoopHole].center;
    case Flasher::RampLeft:
      return table::kHoles[table::kRunwayHole].center;
    case Flasher::RampRight:
      return table::kHoles[table::kSkywayHole].center;
    case Flasher::Nest: {
      Vec2 sum{};
      for (const table::Bumper& bumper : table::kBumpers) {
        sum = sum + bumper.shape.center;
      }
      return sum * (1.0F / static_cast<float>(table::kBumpers.size()));
    }
    case Flasher::Academy:
      return table::kLamps[lampIndex(LampId::Graduation)].position;
    case Flasher::TopLanes:
      return centreOf(std::array<Vec2, 3>{table::kRollovers[0].zone.center,
                                          table::kRollovers[1].zone.center,
                                          table::kRollovers[2].zone.center});
    case Flasher::Turbine:
      return (table::kSpinner.a + table::kSpinner.b) * 0.5F;
    case Flasher::Apron:
      return table::kLamps[lampIndex(LampId::ShootAgain)].position;
    case Flasher::Table:
    case Flasher::Lights:
    case Flasher::Count:
      break;
  }
  return {table::kWidth * 0.5F, table::kHeight * 0.5F};
}

float lampLevel(LampState state, std::uint64_t step, bool reduceMotion) noexcept {
  const auto blink = [step, reduceMotion](std::uint64_t period, float low, float breathSteps) {
    if (reduceMotion) {
      // A slow breath between low and full: no edge at all.
      const float phase = static_cast<float>(step % static_cast<std::uint64_t>(breathSteps)) /
                          breathSteps;
      return low + (1.0F - low) * (0.5F + 0.5F * std::cos(kTau * phase));
    }
    const std::uint64_t t = step % period;
    const std::uint64_t half = period / 2;
    if (t < half) {
      // A bulb warming: full within three steps.
      return std::min(1.0F, static_cast<float>(t + 1) / 3.0F);
    }
    // And cooling, a little slower.
    return std::max(0.0F, 1.0F - static_cast<float>(t - half + 1) / 4.0F);
  };
  switch (state) {
    case LampState::On:
      return 1.0F;
    case LampState::Blink:
      return blink(kBlinkSteps, 0.45F, 180.0F);
    case LampState::FastBlink:
      return blink(kFastBlinkSteps, 0.70F, 90.0F);
    case LampState::Off:
      break;
  }
  return 0.0F;
}

void LightShow::reset() noexcept {
  // The settings stay; everything seen goes.
  const bool reduce = reduceMotion_;
  const bool attract = attract_;
  *this = LightShow();
  reduceMotion_ = reduce;
  attract_ = attract;
}

void LightShow::setAttract(bool on) noexcept {
  if (on && !attract_) {
    attractStart_ = now_;
  }
  attract_ = on;
}

int LightShow::attractPattern() const noexcept {
  if (!attract_) {
    return 0;
  }
  return static_cast<int>(((now_ - attractStart_) / kAttractPatternSteps) %
                          static_cast<std::uint64_t>(kAttractPatterns));
}

void LightShow::observe(const Game& game) noexcept {
  const std::uint64_t step = game.stepCount();
  if (seen_ && step < now_) {
    reset();
  }
  if (!seen_ && attract_) {
    attractStart_ = step;
  }
  seen_ = true;
  now_ = step;
  readEvents(game);
  readSounds(game);
  updateShow(game);
  updateBursts();
  updateLamps(game);
  updateTrails(game);
}

// --- Hits -------------------------------------------------------------------

void LightShow::readEvents(const Game& game) noexcept {
  const std::uint64_t mark = now_ + 1;
  for (const PhysicsEvent& event : game.events()) {
    const std::size_t i = event.element.index;
    switch (event.type) {
      case PhysicsEvent::Type::Hit:
        if (event.element.kind == table::ElementKind::Target && i < targetHit_.size()) {
          targetHit_[i] = mark;
        } else if (event.element.kind == table::ElementKind::DropTarget && i < doorHit_.size()) {
          doorHit_[i] = mark;
        }
        break;
      case PhysicsEvent::Type::Enter:
        if (event.element.kind == table::ElementKind::Rollover && i < rolloverHit_.size()) {
          rolloverHit_[i] = mark;
        }
        break;
      case PhysicsEvent::Type::Capture:
        if (event.element.kind == table::ElementKind::Hole && i < holeHit_.size()) {
          holeHit_[i] = mark;
        } else if (event.element.kind == table::ElementKind::Kickback) {
          kickbackHit_ = mark;
        }
        break;
      case PhysicsEvent::Type::BallHit:
      case PhysicsEvent::Type::Cross:
        break;
    }
  }
  for (const GameEvent& event : game.gameEvents()) {
    const std::size_t i = event.index;
    switch (event.type) {
      case GameEvent::Type::HoleEject:
        if (i < holeHit_.size()) {
          holeHit_[i] = mark;
        }
        break;
      case GameEvent::Type::BallLocked:
      case GameEvent::Type::LockReleased:
        if (i < bayHit_.size()) {
          bayHit_[i] = mark;
        }
        break;
      case GameEvent::Type::KickbackFired:
      case GameEvent::Type::KickbackReturned:
        kickbackHit_ = mark;
        break;
      case GameEvent::Type::Spin:
        spinHit_ = mark;
        spinEnergy_ = std::min(1.0F, spinEnergy_ + 0.2F);
        break;
      case GameEvent::Type::DoorBankDropped:
      case GameEvent::Type::DoorBankRaised:
        doorHit_.fill(mark);
        break;
      case GameEvent::Type::EjectSave:
        break;
    }
  }
  spinEnergy_ *= 0.985F;
}

void LightShow::readSounds(const Game& game) noexcept {
  const SoundSet sounds = game.sounds();
  const auto has = [sounds](Sound sound) { return (sounds & soundBit(sound)) != 0; };
  const auto hitThisStep = [this](const auto& hits) {
    return std::ranges::any_of(hits, [this](std::uint64_t hit) { return hit == now_ + 1; });
  };
  const bool rm = reduceMotion_;

  if (has(Sound::SetComplete)) {
    // Which set: whatever was struck in this step.
    const auto topLanes = std::span(rolloverHit_).first(3);
    const auto wingLanes = std::span(rolloverHit_).subspan(3);
    if (hitThisStep(topLanes)) {
      sweepGroup(kGroupRollCall);
    }
    if (hitThisStep(wingLanes)) {
      sweepGroup(kGroupWing);
    }
    if (hitThisStep(targetHit_)) {
      sweepGroup(kGroupTurbo);
    }
    if (hitThisStep(doorHit_)) {
      sweepGroup(kGroupHangar);
    }
  }
  if (has(Sound::SkillShot)) {
    sweepGroup(kGroupRollCall);
    burst(Flasher::TopLanes, Colour::Cyan, 0.8F, 90.0F, 40);
  }
  if (has(Sound::Lit)) {
    burst(Flasher::Briefing, Colour::Yellow, 0.5F, 70.0F, 30);
  }
  if (has(Sound::Combo)) {
    sweepGroup(kGroupArrows);
  }
  if (has(Sound::CourseStart)) {
    sweepGroup(kGroupCourses);
    burst(Flasher::Academy, Colour::Violet, 0.7F, 100.0F, 40);
  }
  if (has(Sound::CoursePassed)) {
    sweepGroup(kGroupCourses);
    sweepGroup(kGroupCourses, 70);
  }
  if (has(Sound::RankUp)) {
    burst(Flasher::Academy, Colour::Yellow, 0.9F, 150.0F, 40, 24);
  }
  if (has(Sound::BallLocked)) {
    sweepGroup(kGroupHangar);
    burst(Flasher::Hangar, Colour::Orange, 0.9F, 110.0F, 36);
    if (!rm) {
      burst(Flasher::Hangar, Colour::Orange, 0.9F, 110.0F, 36, 45);
    }
  }
  if (has(Sound::ExtraBall)) {
    sweepGroup(kGroupApron);
    burst(Flasher::Briefing, Colour::Magenta, 0.9F, 110.0F, 40);
    burst(Flasher::Apron, Colour::Magenta, 0.8F, 80.0F, 40, 20);
  }
  if (has(Sound::ExtraCredit)) {
    burst(Flasher::Briefing, Colour::Cyan, 0.8F, 100.0F, 36);
  }
  if (has(Sound::Overdrive)) {
    sweepGroup(kGroupTurbo);
    burst(Flasher::Turbine, Colour::Yellow, 0.8F, 100.0F, 36);
  }
  if (has(Sound::BallSaved)) {
    sweepGroup(kGroupApron);
    burst(Flasher::Apron, Colour::Cyan, 0.7F, 80.0F, 36);
  }
  if (has(Sound::TiltWarning)) {
    burst(Flasher::Lights, Colour::Warm, 0.45F, 0.0F, 24);
  }
  const Vec2 at = game.ball().position;
  if (has(Sound::SuperJackpot)) {
    startShow(Show::SuperJackpot, at);
  } else if (has(Sound::Jackpot)) {
    startShow(Show::Jackpot, at);
  }
  if (has(Sound::MultiballStart)) {
    startShow(game.rules().multiball() == Multiball::Graduation ? Show::Graduation
                                                                : Show::MultiballStart);
  }
  if (has(Sound::Drain)) {
    startShow(Show::Drain);
  }
}

float LightShow::flash(std::uint64_t hitStep, std::uint32_t length) const noexcept {
  if (hitStep == 0 || hitStep > now_ + 1) {
    return 0.0F;
  }
  const std::uint64_t age = now_ + 1 - hitStep;
  if (age >= length) {
    return 0.0F;
  }
  const float f = 1.0F - static_cast<float>(age) / static_cast<float>(length);
  return f * f;
}

float LightShow::doorLevel(std::size_t door) const noexcept {
  return door < doorLevels_.size() ? doorLevels_[door] : 0.0F;
}
float LightShow::doorFlash(std::size_t door) const noexcept {
  return door < doorHit_.size() ? flash(doorHit_[door], 30) : 0.0F;
}
float LightShow::targetFlash(std::size_t target) const noexcept {
  return target < targetHit_.size() ? flash(targetHit_[target], 30) : 0.0F;
}
float LightShow::rolloverFlash(std::size_t rollover) const noexcept {
  return rollover < rolloverHit_.size() ? flash(rolloverHit_[rollover], 24) : 0.0F;
}
float LightShow::holeFlash(std::size_t hole) const noexcept {
  return hole < holeHit_.size() ? flash(holeHit_[hole], 40) : 0.0F;
}
float LightShow::bayFlash(std::size_t bay) const noexcept {
  return bay < bayHit_.size() ? flash(bayHit_[bay], 48) : 0.0F;
}
float LightShow::kickbackFlash() const noexcept { return flash(kickbackHit_, 48); }

float LightShow::spinnerLevel() const noexcept {
  if (reduceMotion_) {
    return spinEnergy_;
  }
  // Each turn strikes the lamps full; between turns they hold at a third of
  // the spin's energy, so a fast spin is a bright flicker and a slow one
  // single blinks.
  return std::max(spinEnergy_ * 0.35F, flash(spinHit_, 4) * std::max(spinEnergy_, 0.4F));
}

// --- Sweeps -----------------------------------------------------------------

void LightShow::sweepGroup(std::uint8_t group, std::uint32_t delay) noexcept {
  Sweep* slot = &sweeps_[0];
  for (Sweep& sweep : sweeps_) {
    if (!sweep.used) {
      slot = &sweep;
      break;
    }
    if (sweep.start < slot->start) {
      slot = &sweep;  // The oldest makes way.
    }
  }
  *slot = Sweep{.start = now_ + delay, .group = group, .used = true};
}

float LightShow::sweepLevel(std::size_t lamp) const noexcept {
  float level = 0.0F;
  for (const Sweep& sweep : sweeps_) {
    if (!sweep.used || sweep.start > now_) {
      continue;
    }
    const Group& group = kGroups[sweep.group];
    const std::uint64_t length = group.size * kSweepStride + kSweepStride + kSweepFinale;
    const std::uint64_t t = now_ - sweep.start;
    if (t >= length) {
      continue;
    }
    for (std::size_t k = 0; k < group.size; ++k) {
      if (lampIndex(group.ids[k]) != lamp) {
        continue;
      }
      if (reduceMotion_) {
        // The group glows up and down together.
        level = std::max(level, std::sin(table::kPi * static_cast<float>(t) /
                                         static_cast<float>(length)));
        continue;
      }
      const std::uint64_t on = k * kSweepStride;
      const std::uint64_t finale = group.size * kSweepStride + kSweepStride;
      if (t >= on && t < on + 2 * kSweepStride + 2) {
        level = std::max(level, 1.0F - static_cast<float>(t - on) / 16.0F);
      } else if (t >= finale && ((t - finale) / 10) % 2 == 0) {
        level = 1.0F;
      }
    }
  }
  return level;
}

// --- Bursts and the limiter -------------------------------------------------

void LightShow::burst(Flasher where, Colour colour, float peak, float radius,
                      std::uint32_t length, std::uint32_t delay) noexcept {
  Burst* slot = nullptr;
  for (Burst& b : bursts_) {
    if (!b.used) {
      slot = &b;
      break;
    }
  }
  if (slot == nullptr) {
    // Full: a burst still waiting makes way, the one asked for last. One
    // that is lit is never cut off (that would be a flash of its own); with
    // none waiting the new one is not started.
    for (Burst& b : bursts_) {
      if (!b.started && (slot == nullptr || b.start > slot->start)) {
        slot = &b;
      }
    }
    if (slot == nullptr) {
      return;
    }
  }
  const bool large = where == Flasher::Table || where == Flasher::Lights || radius >= kLargeRadius;
  if (reduceMotion_) {
    // A slow rise and fall, and softer.
    length = std::max<std::uint32_t>(length * 2, 90);
    peak *= 0.6F;
  }
  *slot = Burst{.where = where,
                .colour = colour,
                .used = true,
                .started = false,
                .large = large,
                .peak = clamp01(peak),
                .radius = radius,
                .start = now_ + delay,
                .length = std::max<std::uint32_t>(length, 4),
                .late = 0};
}

void LightShow::updateBursts() noexcept {
  const std::uint64_t gap = reduceMotion_ ? kMinLargeFadeSteps : kMinLargeFlashSteps;
  // Pending bursts in the order they asked to start.
  for (;;) {
    Burst* next = nullptr;
    for (Burst& b : bursts_) {
      if (b.used && !b.started && b.start <= now_ && (next == nullptr || b.start < next->start)) {
        next = &b;
      }
    }
    if (next == nullptr) {
      break;
    }
    Burst& b = *next;
    if (b.large) {
      const bool sameFlash = lastLarge_ == now_ + 1;  // Lit together: one flash.
      if (!sameFlash && lastLarge_ != 0 && now_ + 1 - lastLarge_ < gap) {
        // Too soon after the last: it waits, and gives up after half a
        // second. It is started (or dropped) at a later step.
        if (b.late == 0) {
          ++largeHeld_;
        }
        ++b.late;
        if (b.late > 60) {
          b.used = false;
        } else {
          b.start = now_ + 1;
        }
        continue;
      }
      if (!sameFlash) {
        ++largeFlashes_;
        lastLarge_ = now_ + 1;
      }
    }
    b.started = true;
    b.start = now_;
    ++burstsStarted_;
  }
  for (Burst& b : bursts_) {
    if (b.used && b.started && now_ >= b.start + b.length) {
      b.used = false;
    }
  }
}

float LightShow::burstLevel(const Burst& b) const noexcept {
  if (!b.used || !b.started || now_ < b.start) {
    return 0.0F;
  }
  const auto age = static_cast<float>(now_ - b.start);
  const float attack = reduceMotion_ ? 24.0F : 3.0F;
  const auto length = static_cast<float>(b.length);
  if (age >= length) {
    return 0.0F;
  }
  if (age < attack) {
    return b.peak * (age + 1.0F) / attack;
  }
  const float fall = 1.0F - (age - attack) / std::max(length - attack, 1.0F);
  return b.peak * fall * std::sqrt(fall);
}

BurstViews LightShow::bursts() const noexcept {
  BurstViews views;
  for (const Burst& b : bursts_) {
    const float level = burstLevel(b);
    if (level > 0.0F && b.where != Flasher::Lights) {
      views.items[views.count++] =
          BurstView{.where = b.where, .colour = b.colour, .level = level, .radius = b.radius};
    }
  }
  return views;
}

float LightShow::largeFlashLevel() const noexcept {
  float level = 1.0F - gi_;
  for (const Burst& b : bursts_) {
    if (b.large && b.where != Flasher::Lights) {
      level = std::max(level, burstLevel(b));
    }
  }
  return clamp01(level);
}

// --- Shows ------------------------------------------------------------------

void LightShow::startShow(Show show, Vec2 at) noexcept {
  const std::uint64_t length = showLength(show_);
  const bool running = show_ != Show::None && (length == 0 || now_ - showStart_ < length);
  if (running && priority(show) < priority(show_)) {
    return;
  }
  show_ = show;
  showStart_ = now_;
  showAt_ = at;
  const bool rm = reduceMotion_;
  switch (show) {
    case Show::MultiballStart:
    case Show::Graduation: {
      const bool gold = show == Show::Graduation;
      // The blackout is the dip of the lights, lifting as the sweep climbs.
      burst(Flasher::Lights, Colour::Warm, 0.85F, 0.0F,
            static_cast<std::uint32_t>(kBlackoutSteps + kSweepSteps));
      const std::array<Colour, 3> pulses =
          gold ? std::array{Colour::Yellow, Colour::White, Colour::Yellow}
               : std::array{Colour::Magenta, Colour::Cyan, Colour::Yellow};
      const auto first = static_cast<std::uint32_t>(kBlackoutSteps + kSweepSteps + 6);
      for (std::size_t k = 0; k < (rm ? 1U : pulses.size()); ++k) {
        const auto delay = first + static_cast<std::uint32_t>(k * kPulseSpacing);
        for (const Flasher dome :
             {Flasher::Hangar, Flasher::Briefing, Flasher::RampLeft, Flasher::RampRight}) {
          burst(dome, pulses[k], 0.9F, 130.0F, 36, delay);
        }
      }
      sweepColour_ = gold ? Colour::Yellow : Colour::Magenta;
      break;
    }
    case Show::Jackpot:
      ringColour_ = Colour::Yellow;
      burst(Flasher::Table, Colour::Yellow, 0.35F, 0.0F, 36);
      for (std::uint32_t k = 0; k < (rm ? 1U : 3U); ++k) {
        burst(k % 2 == 0 ? Flasher::RampLeft : Flasher::RampRight, Colour::Yellow, 0.8F, 90.0F,
              24, k * 18);
      }
      break;
    case Show::SuperJackpot:
      ringColour_ = Colour::Magenta;
      burst(Flasher::Table, Colour::Magenta, 0.45F, 0.0F, 40);
      if (!rm) {
        burst(Flasher::Table, Colour::White, 0.40F, 0.0F, 40, static_cast<std::uint32_t>(kPulseSpacing));
      }
      for (const Flasher dome :
           {Flasher::Hangar, Flasher::Briefing, Flasher::RampLeft, Flasher::RampRight}) {
        burst(dome, Colour::Magenta, 0.9F, 140.0F, 40,
              static_cast<std::uint32_t>(2 * kPulseSpacing));
      }
      break;
    case Show::Drain:
      burst(Flasher::Lights, Colour::Warm, 0.55F, 0.0F, 40);
      if (!rm) {
        burst(Flasher::Lights, Colour::Warm, 0.45F, 0.0F, 40,
              static_cast<std::uint32_t>(kPulseSpacing));
      }
      break;
    case Show::Tilt:
      burst(Flasher::Lights, Colour::Warm, 0.8F, 0.0F, 30);
      if (!rm) {
        burst(Flasher::Lights, Colour::Warm, 0.8F, 0.0F, 30,
              static_cast<std::uint32_t>(kPulseSpacing));
      }
      break;
    case Show::GameOver:
    case Show::None:
      break;
  }
}

void LightShow::updateShow(const Game& game) noexcept {
  const Rules& rules = game.rules();
  // The edges the sounds do not mark.
  if (rules.tilted() && !wasTilted_) {
    startShow(Show::Tilt);
  }
  if (rules.gameOver() && !wasGameOver_) {
    startShow(Show::GameOver);
  }
  if (rules.multiball() == Multiball::Graduation && wasMultiball_ != Multiball::Graduation) {
    startShow(Show::Graduation);
  }
  wasTilted_ = rules.tilted();
  wasGameOver_ = rules.gameOver();
  wasMultiball_ = rules.multiball();

  const std::uint64_t t = now_ - showStart_;
  const std::uint64_t length = showLength(show_);
  if ((length != 0 && t >= length) || (show_ == Show::Tilt && !rules.tilted()) ||
      (show_ == Show::GameOver && !rules.gameOver())) {
    show_ = Show::None;
  }
  const bool rm = reduceMotion_;

  float gi = 1.0F;
  sweepY_ = -1.0F;
  ringRadius_ = -1.0F;
  switch (show_) {
    case Show::MultiballStart:
    case Show::Graduation:
      if (!rm && t >= kBlackoutSteps && t < kBlackoutSteps + kSweepSteps) {
        const float u = static_cast<float>(t - kBlackoutSteps) / static_cast<float>(kSweepSteps);
        sweepY_ = table::kHeight - u * (table::kHeight - 80.0F);
      }
      break;
    case Show::Jackpot:
    case Show::SuperJackpot:
      if (!rm && t < 70) {
        ringCentre_ = showAt_;
        ringRadius_ = static_cast<float>(t) * 11.0F;
      }
      break;
    case Show::Drain: {
      // Down to half for the count, back up at the end.
      const float down = std::min(1.0F, static_cast<float>(t) / 30.0F);
      const float up = t > 240 ? static_cast<float>(t - 240) / 60.0F : 0.0F;
      gi = 1.0F - 0.5F * down * (1.0F - clamp01(up));
      break;
    }
    case Show::Tilt:
      gi = 0.3F + 0.7F * (1.0F - std::min(1.0F, static_cast<float>(t) / 30.0F));
      gi = std::min(gi, t > 60 ? 0.3F : 1.0F);
      break;
    case Show::GameOver:
      gi = 1.0F - 0.45F * std::min(1.0F, static_cast<float>(t) / 120.0F);
      break;
    case Show::None:
      break;
  }
  // The dips.
  for (const Burst& b : bursts_) {
    if (b.where == Flasher::Lights) {
      gi *= 1.0F - burstLevel(b);
    }
  }
  if (attract_) {
    gi = 1.0F;
  }
  gi_ = clamp01(gi);
}

float LightShow::showLevel(std::size_t lamp) const noexcept {
  const std::uint64_t t = now_ - showStart_;
  const Vec2 at = table::kLamps[lamp].position;
  switch (show_) {
    case Show::MultiballStart:
    case Show::Graduation: {
      if (reduceMotion_) {
        if (t >= kBlackoutSteps && t < kBlackoutSteps + 2 * kSweepSteps) {
          return std::sin(table::kPi * static_cast<float>(t - kBlackoutSteps) /
                          static_cast<float>(2 * kSweepSteps));
        }
        return 0.0F;
      }
      float level = sweepY_ >= 0.0F ? bell(at.y - sweepY_, 70.0F) : 0.0F;
      // Every lamp with the pulses.
      for (const Burst& b : bursts_) {
        if (b.large && b.where == Flasher::Hangar) {
          level = std::max(level, burstLevel(b) / std::max(b.peak, 0.01F));
        }
      }
      return level;
    }
    case Show::Jackpot:
    case Show::SuperJackpot:
      if (ringRadius_ >= 0.0F) {
        const Vec2 d = at - ringCentre_;
        return bell(std::sqrt(d.x * d.x + d.y * d.y) - ringRadius_, 45.0F);
      }
      return 0.0F;
    case Show::Drain:
      for (std::size_t k = 0; k < kBonusLamps.size(); ++k) {
        if (lampIndex(kBonusLamps[k]) == lamp) {
          const std::uint64_t out = kBonusStart + kBonusHold + k * kBonusStride;
          if (t >= kBonusStart && t < out) {
            return 1.0F;
          }
          if (t >= out && t < out + 6) {
            return 1.0F - static_cast<float>(t - out) / 6.0F;
          }
        }
      }
      return 0.0F;
    case Show::Tilt:
    case Show::GameOver:
    case Show::None:
      break;
  }
  return 0.0F;
}

// --- Attract ----------------------------------------------------------------

float LightShow::attractLevel(std::size_t lamp) const noexcept {
  const std::uint64_t ta = now_ - attractStart_;
  const std::uint64_t tp = ta % kAttractPatternSteps;
  const Vec2 at = table::kLamps[lamp].position;
  if (reduceMotion_) {
    // One slow wave across the table, ten seconds a crossing.
    const float phase = static_cast<float>(ta % 1200) / 1200.0F + at.x / table::kWidth;
    return 0.3F + 0.45F * (0.5F + 0.5F * std::cos(kTau * phase));
  }
  const auto id = static_cast<LampId>(lamp);
  switch (attractPattern()) {
    case 0: {  // Comets along the arrows; the rest turn slowly.
      if (table::isArrowLamp(id)) {
        const float head = static_cast<float>(tp % 84) / 12.0F;
        return clamp01(1.0F - std::fabs(head - static_cast<float>(lamp)) * 0.6F);
      }
      const std::uint64_t k = (tp / 15 + lamp) % 6;
      return k == 0 ? 1.0F : 0.12F;
    }
    case 1: {  // Sweeps up the table.
      // Over the lamps' own rows, 940 up to 140.
      const float y = 940.0F - static_cast<float>(tp % 120) / 120.0F * 800.0F;
      return std::max(0.1F, bell(at.y - y, 90.0F));
    }
    case 2: {  // The course lamps go round; lanes alternate.
      if (table::isCourseLamp(id)) {
        const std::size_t k = lamp - lampIndex(LampId::Course1);
        const std::size_t head = static_cast<std::size_t>(tp / 8) % 7;
        return k == head ? 1.0F : (k + 1) % 7 == head ? 0.4F : 0.08F;
      }
      return ((tp / 30) + lamp) % 2 == 0 ? 0.9F : 0.1F;
    }
    case 3:  // Left and right halves in turn, once a second.
      return ((tp / 60) % 2 == 0) == (at.x < table::kWidth * 0.5F) ? 1.0F : 0.08F;
    default: {  // Rings out of the bumper nest.
      const Vec2 d = at - flasherPosition(Flasher::Nest);
      const float r = static_cast<float>(tp % 90) * 6.5F;
      return std::max(0.08F, bell(std::sqrt(d.x * d.x + d.y * d.y) - r, 70.0F));
    }
  }
}

// --- Lamps ------------------------------------------------------------------

float LightShow::ambientLevel(std::size_t lamp) const noexcept {
  // Unlit shot arrows and course lamps glimmer in a slow wave, so a quiet
  // table still moves; never more than kAmbientMax, far under a lit lamp.
  const auto id = static_cast<LampId>(lamp);
  float place = 0.0F;
  float period = 0.0F;
  if (table::isArrowLamp(id)) {
    place = static_cast<float>(lamp) / 7.0F;
    period = 330.0F;
  } else if (id >= LampId::Course1 && id <= LampId::Course6) {
    place = static_cast<float>(lamp - lampIndex(LampId::Course1)) / 6.0F;
    period = 420.0F;
  } else {
    return 0.0F;
  }
  if (reduceMotion_) {
    period *= 2.0F;
  }
  const float phase = static_cast<float>(now_ % static_cast<std::uint64_t>(period)) / period;
  const float wave = 0.5F + 0.5F * std::cos(kTau * (phase - place));
  return kAmbientMax * wave * wave * wave;
}

void LightShow::updateLamps(const Game& game) noexcept {
  const Rules& rules = game.rules();
  const bool rm = reduceMotion_;
  for (std::size_t i = 0; i < kLampCount; ++i) {
    states_[i] = rules.lampState(table::kLamps[i].id);
  }
  // The lock lamps follow the bays themselves (as TableLights does).
  states_[lampIndex(LampId::Lock1)] = game.bayTaken(0) ? LampState::On : LampState::Off;
  states_[lampIndex(LampId::Lock2)] = game.bayTaken(1) ? LampState::On : LampState::Off;
  for (std::size_t d = 0; d < doorLevels_.size(); ++d) {
    doorLevels_[d] = fx::lampLevel(rules.doorLampState(d), now_, rm);
  }

  if (attract_) {
    // Attract: a fresh burst on a dome every three quarters of a second.
    const std::uint64_t ta = now_ - attractStart_;
    if (!rm && ta % 90 == 0) {
      constexpr std::array<Flasher, 5> kDomes{Flasher::Hangar, Flasher::RampLeft,
                                              Flasher::Briefing, Flasher::RampRight,
                                              Flasher::Nest};
      constexpr std::array<Colour, 5> kColours{Colour::Orange, Colour::Violet, Colour::Yellow,
                                               Colour::Magenta, Colour::Cyan};
      const std::size_t k = static_cast<std::size_t>(ta / 90) % kDomes.size();
      burst(kDomes[k], kColours[k], 0.6F, 100.0F, 40);
    }
    for (std::size_t i = 0; i < kLampCount; ++i) {
      levels_[i] = clamp01(attractLevel(i));
    }
    return;
  }

  const std::uint64_t t = now_ - showStart_;
  float factor = 1.0F;
  if (rules.tilted()) {
    factor = 0.0F;
  } else if ((show_ == Show::MultiballStart || show_ == Show::Graduation) && t < kBlackoutSteps) {
    factor = rm ? 0.35F : 0.0F;
  } else if (show_ == Show::Drain && t < kBonusStart + kBonusHold +
                                             kBonusLamps.size() * kBonusStride) {
    factor = 0.4F;
  }

  // In a blackout only the show itself lights a lamp.
  const bool blackout = factor < 0.5F && !rules.tilted() && show_ != Show::Drain;
  std::array<float, kLampCount> hits{};
  for (std::size_t k = 0; k < rolloverHit_.size(); ++k) {
    float& level = hits[lampIndex(kRolloverLamps[k])];
    level = std::max(level, rolloverFlash(k));
  }
  for (std::size_t k = 0; k < holeHit_.size(); ++k) {
    float& level = hits[lampIndex(kHoleLamps[k])];
    level = std::max(level, holeFlash(k));
  }
  for (std::size_t k = 0; k < targetHit_.size(); ++k) {
    float& level = hits[lampIndex(LampId::TurboCharge)];
    level = std::max(level, targetFlash(k));
  }
  hits[lampIndex(LampId::Lock1)] = std::max(hits[lampIndex(LampId::Lock1)], bayFlash(0));
  hits[lampIndex(LampId::Lock2)] = std::max(hits[lampIndex(LampId::Lock2)], bayFlash(1));
  hits[lampIndex(LampId::Kickback)] = kickbackFlash();
  hits[lampIndex(LampId::ArrowTurbine)] =
      std::max(hits[lampIndex(LampId::ArrowTurbine)], spinnerLevel() * 0.7F);

  for (std::size_t i = 0; i < kLampCount; ++i) {
    if (states_[i] == LampState::Off && factor > 0.0F) {
      hits[i] = std::max(hits[i], ambientLevel(i));
    }
    const float base = fx::lampLevel(states_[i], now_, rm) * factor;
    const float overlay = rules.tilted() ? 0.0F
                          : blackout     ? showLevel(i)
                                         : std::max({hits[i], sweepLevel(i), showLevel(i)});
    levels_[i] = clamp01(std::max(base, overlay));
  }
}

float LightShow::lampLevel(table::LampId lamp) const noexcept {
  const std::size_t i = lampIndex(lamp);
  return i < levels_.size() ? levels_[i] : 0.0F;
}

LampState LightShow::lampState(table::LampId lamp) const noexcept {
  const std::size_t i = lampIndex(lamp);
  return i < states_.size() ? states_[i] : LampState::Off;
}

ChaseView LightShow::chase(std::size_t arrow) const noexcept {
  ChaseView view;
  if (arrow >= kLampCount || !table::isArrowLamp(table::kLamps[arrow].id) || attract_) {
    return view;
  }
  const LampState state = states_[arrow];
  if (state != LampState::Blink && state != LampState::FastBlink) {
    return view;
  }
  view.active = true;
  view.fast = state == LampState::FastBlink;
  view.steady = reduceMotion_;
  const std::uint64_t period = view.fast ? 24 : 48;
  view.phase = static_cast<float>(now_ % period) / static_cast<float>(period);
  return view;
}

// --- Trails -----------------------------------------------------------------

void LightShow::updateTrails(const Game& game) noexcept {
  std::array<bool, table::kMaxBalls> seen{};
  const BallViews balls = game.balls(1.0F);
  for (const BallView& ball : balls) {
    if (ball.id >= trails_.size()) {
      continue;
    }
    seen[ball.id] = true;
    TrailView& trail = trails_[ball.id];
    if (!ball.visible || ball.locked) {
      trail.count = 0;
      continue;
    }
    if (trail.count > 0) {
      const Vec2 d = ball.position - trail.points[0];
      if (d.x * d.x + d.y * d.y > 80.0F * 80.0F) {
        trail.count = 0;  // Moved by hand: no streak across the table.
      }
    }
    for (std::size_t k = std::min(trail.count, kTrailLength - 1); k > 0; --k) {
      trail.points[k] = trail.points[k - 1];
    }
    trail.points[0] = ball.position;
    trail.count = std::min(trail.count + 1, kTrailLength);
    if (trail.count > 1) {
      const Vec2 d = trail.points[0] - trail.points[trail.count - 1];
      trail.speed = std::sqrt(d.x * d.x + d.y * d.y) / static_cast<float>(trail.count - 1);
    } else {
      trail.speed = 0.0F;
    }
  }
  for (std::size_t slot = 0; slot < trails_.size(); ++slot) {
    if (!seen[slot]) {
      trails_[slot] = TrailView{};
    }
  }
}

const TrailView& LightShow::trail(std::size_t slot) const noexcept {
  static const TrailView kNone{};
  return slot < trails_.size() ? trails_[slot] : kNone;
}

}  // namespace pinball::fx
