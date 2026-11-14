#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Game.hpp"
#include "core/LightShow.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

// What the table's lights show: which lamps are lit and which bay doors are
// marked. The drawing asks nothing else about lamps, and asks it only here.
// No SDL: unit tested in core_tests.
namespace pinball {

struct TableLights {
  // In LampId order (table::kLamps).
  std::array<bool, table::kLamps.size()> lamps{};
  // In table::kDropTargets order.
  std::array<bool, table::kDropTargets.size()> doorsMarked{};

  [[nodiscard]] constexpr bool lit(table::LampId id) const noexcept {
    const auto index = static_cast<std::size_t>(id);
    return index < lamps.size() && lamps[index];
  }
  constexpr void light(table::LampId id, bool on) noexcept {
    const auto index = static_cast<std::size_t>(id);
    if (index < lamps.size()) {
      lamps[index] = on;
    }
  }
};

// Blinking lamps, in steps of the game's clock: on for half of each period.
// The same clock as the light show (core/LightShow.hpp).
inline constexpr std::uint64_t kBlinkSteps = fx::kBlinkSteps;          // Twice a second.
inline constexpr std::uint64_t kFastBlinkSteps = fx::kFastBlinkSteps;  // Five times a second.

// Whether a lamp in this state is lit at this step of the game.
[[nodiscard]] constexpr bool lampLit(LampState state, std::uint64_t step) noexcept {
  switch (state) {
    case LampState::On:
      return true;
    case LampState::Blink:
      return step % kBlinkSteps < kBlinkSteps / 2;
    case LampState::FastBlink:
      return step % kFastBlinkSteps < kFastBlinkSteps / 2;
    case LampState::Off:
      break;
  }
  return false;
}

// The rules say what every lamp does (Rules::lampState, one answer for each
// LampId); the lock lamps follow the bays themselves, which empty one after
// the other as a multiball starts; a door's lamp is Rules::doorLampState.
[[nodiscard]] inline TableLights tableLights(const Game& game) noexcept {
  using table::LampId;
  TableLights lights;
  const Rules& rules = game.rules();
  const std::uint64_t step = game.stepCount();
  for (const table::Lamp& lamp : table::kLamps) {
    lights.light(lamp.id, lampLit(rules.lampState(lamp.id), step));
  }
  lights.light(LampId::Lock1, game.bayTaken(0));
  lights.light(LampId::Lock2, game.bayTaken(1));
  for (std::size_t i = 0; i < lights.doorsMarked.size(); ++i) {
    lights.doorsMarked[i] = lampLit(rules.doorLampState(i), step);
  }
  return lights;
}

}  // namespace pinball
