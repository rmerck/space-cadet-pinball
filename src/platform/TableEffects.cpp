#include "platform/TableEffects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

#include "core/TableLayout.hpp"

namespace pinball {

namespace {

using table::LampId;

float length(Vec2 v) noexcept { return std::sqrt(v.x * v.x + v.y * v.y); }

Vec2 unit(Vec2 v) noexcept {
  const float l = length(v);
  return l > 0.0F ? v * (1.0F / l) : Vec2{0.0F, -1.0F};
}

// The colour of each hole's shot (generated/ArtPalette.hpp), in kHoles order.
art::Rgb holeColour(std::size_t hole) noexcept {
  switch (hole) {
    case table::kScoopHole:
      return art::kFxYellow;
    case table::kHangarHole:
      return art::kFxOrange;
    case table::kRunwayHole:
      return art::kFxViolet;
    default:
      return art::kFxMagenta;
  }
}

// The general illumination: bulbs under the playfield's edge, every fourth
// point of the shell, a little inside it. Worked out once.
struct GiBulbs {
  std::array<Vec2, table::kShell.size() / 4 + 1> at{};
  std::size_t count = 0;
};
GiBulbs makeGiBulbs() noexcept {
  GiBulbs bulbs;
  const Vec2 middle{table::kWidth * 0.5F, table::kHeight * 0.55F};
  for (std::size_t i = 0; i < table::kShell.size(); i += 4) {
    const Vec2 p = table::kShell[i];
    bulbs.at[bulbs.count++] = p + unit(middle - p) * 34.0F;
  }
  return bulbs;
}

}  // namespace

art::Rgb mixRgb(art::Rgb a, art::Rgb b, float t) noexcept {
  t = std::clamp(t, 0.0F, 1.0F);
  const auto channel = [t](std::uint8_t from, std::uint8_t to) {
    return static_cast<std::uint8_t>(std::lround(
        static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) * t));
  };
  return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

art::Rgb fxColour(fx::Colour colour) noexcept {
  switch (colour) {
    case fx::Colour::Cyan:
      return art::kFxCyan;
    case fx::Colour::Magenta:
      return art::kFxMagenta;
    case fx::Colour::Lime:
      return art::kFxLime;
    case fx::Colour::Orange:
      return art::kFxOrange;
    case fx::Colour::Yellow:
      return art::kFxYellow;
    case fx::Colour::Violet:
      return art::kFxViolet;
    case fx::Colour::White:
      return art::kFxWhite;
    case fx::Colour::Warm:
      return art::kFxGi;
  }
  return art::kFxWhite;
}

// --- GlowBatch --------------------------------------------------------------

GlowBatch::GlowBatch() noexcept {
  for (std::size_t q = 0; q < kMaxQuads; ++q) {
    const int v = static_cast<int>(q * 4);
    const std::size_t i = q * 6;
    indices_[i + 0] = v;
    indices_[i + 1] = v + 1;
    indices_[i + 2] = v + 2;
    indices_[i + 3] = v;
    indices_[i + 4] = v + 2;
    indices_[i + 5] = v + 3;
  }
}

void GlowBatch::add(SDL_Renderer* r, SDL_Texture* texture, Vec2 centre, Vec2 half,
                    art::Rgb colour, float strength) {
  if (texture == nullptr || !(strength > 0.004F) || !(half.x > 0.0F) || !(half.y > 0.0F)) {
    return;
  }
  if (quads_ == kMaxQuads || (quads_ > 0 && texture != texture_)) {
    flush(r);
  }
  texture_ = texture;
  const SDL_FColor c{static_cast<float>(colour.r) / 255.0F, static_cast<float>(colour.g) / 255.0F,
                     static_cast<float>(colour.b) / 255.0F, std::min(strength, 1.0F)};
  SDL_Vertex* v = &vertices_[quads_ * 4];
  v[0] = {{centre.x - half.x, centre.y - half.y}, c, {0.0F, 0.0F}};
  v[1] = {{centre.x + half.x, centre.y - half.y}, c, {1.0F, 0.0F}};
  v[2] = {{centre.x + half.x, centre.y + half.y}, c, {1.0F, 1.0F}};
  v[3] = {{centre.x - half.x, centre.y + half.y}, c, {0.0F, 1.0F}};
  ++quads_;
}

void GlowBatch::flush(SDL_Renderer* r) {
  if (quads_ == 0 || texture_ == nullptr) {
    quads_ = 0;
    return;
  }
  // The vertex colours carry the tint and strength: the texture's own
  // modulation (set by other draws of it) must not.
  if (software_ == Software::Unknown) {
    const char* const name = SDL_GetRendererName(r);
    software_ = name != nullptr && std::string_view(name) == SDL_SOFTWARE_RENDERER ? Software::Yes
                                                                                   : Software::No;
  }
  if (software_ == Software::Yes) {
    // The software renderer fills a textured triangle a pixel at a time,
    // but copies a rectangle with its fast blitter: each quad as a copy.
    for (std::size_t q = 0; q < quads_; ++q) {
      const SDL_Vertex* v = &vertices_[q * 4];
      const SDL_FRect dst{v[0].position.x, v[0].position.y, v[2].position.x - v[0].position.x,
                          v[2].position.y - v[0].position.y};
      SDL_SetTextureColorModFloat(texture_, v[0].color.r, v[0].color.g, v[0].color.b);
      SDL_SetTextureAlphaModFloat(texture_, v[0].color.a);
      SDL_RenderTexture(r, texture_, nullptr, &dst);
    }
    SDL_SetTextureColorMod(texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture_, 255);
    drawCalls_ += quads_;
  } else {
    SDL_SetTextureColorMod(texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture_, 255);
    SDL_RenderGeometry(r, texture_, vertices_.data(), static_cast<int>(quads_ * 4),
                       indices_.data(), static_cast<int>(quads_ * 6));
    ++drawCalls_;
  }
  drawnQuads_ += quads_;
  quads_ = 0;
}

// --- TableEffects -----------------------------------------------------------

void TableEffects::flushAll(SDL_Renderer* r) {
  glow_.flush(r);
  flare_.flush(r);
  ring_.flush(r);
}

std::size_t TableEffects::quads() const noexcept {
  return glow_.drawnQuads() + flare_.drawnQuads() + ring_.drawnQuads();
}
std::size_t TableEffects::drawCalls() const noexcept {
  return glow_.drawCalls() + flare_.drawCalls() + ring_.drawCalls();
}
void TableEffects::clearCounters() noexcept {
  glow_.clearCounters();
  flare_.clearCounters();
  ring_.clearCounters();
}

void TableEffects::drawUnderlay(SDL_Renderer* r, const LightShow& show) {
  static const GiBulbs kBulbs = makeGiBulbs();
  const float gi = show.giLevel();
  if (gi < 0.995F) {
    // The floor darkens with the lights; lit lamps go over it untouched.
    const SDL_FRect field{0.0F, 0.0F, table::kWidth, table::kHeight};
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, art::kInk.r, art::kInk.g, art::kInk.b,
                           static_cast<Uint8>(std::lround((1.0F - gi) * 0.62F * 255.0F)));
    SDL_RenderFillRect(r, &field);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  }
  for (std::size_t i = 0; i < kBulbs.count; ++i) {
    glow_.add(r, textures_.glow, kBulbs.at[i], 54.0F, art::kFxGiBulb, art::kGiStrength * gi);
  }
  flushAll(r);
}

void TableEffects::drawElementFlashes(SDL_Renderer* r, const Game& game, const LightShow& show) {
  SDL_Texture* const glow = textures_.glow;
  SDL_Texture* const ring = textures_.ring;
  // Turbo targets.
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    const float f = show.targetFlash(i);
    if (f > 0.0F) {
      const table::Capsule& shape = table::kTargets[i].shape;
      const Vec2 mid = (shape.a + shape.b) * 0.5F;
      glow_.add(r, glow, mid, 44.0F, art::kFxLime, 0.6F * f);
      glow_.add(r, glow, mid, 16.0F, art::kFxWhite, 0.5F * f);
    }
  }
  // Bay doors: the face lamp, and a flash when struck or when the bank moves.
  for (std::size_t i = 0; i < table::kDropTargets.size(); ++i) {
    const table::Capsule& shape = table::kDropTargets[i];
    const Vec2 mid{(shape.a.x + shape.b.x) * 0.5F, shape.a.y};
    if (game.doorBankUp()) {
      glow_.add(r, glow, mid, 13.0F, art::kFxOrange, 0.55F * show.doorLevel(i));
    }
    const float f = show.doorFlash(i);
    if (f > 0.0F) {
      glow_.add(r, glow, mid, 34.0F, art::kFxOrange, 0.6F * f);
      glow_.add(r, glow, mid, 12.0F, art::kFxWhite, 0.45F * f);
    }
  }
  // Holes, on entry and on kickout.
  for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
    const float f = show.holeFlash(i);
    if (f > 0.0F) {
      const table::Hole& hole = table::kHoles[i];
      glow_.add(r, glow, hole.center, 56.0F, holeColour(i), 0.65F * f);
      glow_.add(r, glow, hole.exitPosition, 26.0F, holeColour(i), 0.5F * f);
      ring_.add(r, ring, hole.center, 18.0F + 40.0F * (1.0F - f), holeColour(i), 0.7F * f);
    }
  }
  // The lock bays.
  for (std::size_t i = 0; i < table::kLockBays.size(); ++i) {
    const float f = show.bayFlash(i);
    if (f > 0.0F) {
      glow_.add(r, glow, table::kLockBays[i], 32.0F, art::kFxOrange, 0.65F * f);
      ring_.add(r, ring, table::kLockBays[i], 10.0F + 26.0F * (1.0F - f), art::kFxYellow, 0.7F * f);
    }
  }
  // The kickback.
  if (const float f = show.kickbackFlash(); f > 0.0F) {
    glow_.add(r, glow, table::kKickbackZone.center, 44.0F, art::kFxYellow, 0.65F * f);
    ring_.add(r, ring, table::kKickbackZone.center, 12.0F + 34.0F * (1.0F - f), art::kFxWhite,
              0.6F * f);
  }
  // Rollovers.
  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    const float f = show.rolloverFlash(i);
    if (f > 0.0F) {
      const bool top = table::kRollovers[i].group == table::RolloverGroup::TopLane;
      glow_.add(r, glow, table::kRollovers[i].zone.center, 24.0F,
                top ? art::kFxYellow : art::kFxCyan, 0.55F * f);
    }
  }
  // The spinner strobes with its turns.
  if (const float s = show.spinnerLevel(); s > 0.0F) {
    const Vec2 mid = (table::kSpinner.a + table::kSpinner.b) * 0.5F;
    glow_.add(r, glow, mid, {56.0F, 30.0F}, art::kFxLime, 0.6F * s);
    glow_.add(r, glow, mid, {30.0F, 10.0F}, art::kFxWhite, 0.45F * s);
  }
  // Chases: three chevrons behind each blinking arrow, running toward it.
  for (std::size_t i = 0; i < table::kLamps.size(); ++i) {
    const fx::ChaseView chase = show.chase(i);
    if (!chase.active) {
      continue;
    }
    const Vec2 at = table::kLamps[i].position;
    const auto [aimX, aimY] = art::kArrowAims[i];
    const Vec2 u = unit(Vec2{aimX, aimY} - at);
    const art::Rgb colour = art::kLampNeon[i];
    const float head = chase.phase * 4.0F;  // Runs 0 (farthest), 1, 2, then the arrow.
    for (int k = 0; k < 3; ++k) {
      const float slot = static_cast<float>(2 - k);
      const float level = chase.steady ? 0.55F : std::max(0.0F, 1.0F - std::fabs(head - slot) * 1.2F);
      if (level <= 0.0F) {
        continue;
      }
      const Vec2 p = at - u * (24.0F + 15.0F * static_cast<float>(k));
      glow_.add(r, glow, p, 11.0F, colour, 0.65F * level);
      glow_.add(r, glow, p, 4.5F, art::kFxWhite, 0.5F * level);
    }
  }
  flushAll(r);
}

void TableEffects::drawShow(SDL_Renderer* r, const Game& game, std::span<const BallView> balls,
                            float alpha, const LightShow& show) {
  SDL_Texture* const glow = textures_.glow;
  // Washes over the whole table first.
  const fx::BurstViews bursts = show.bursts();
  for (const fx::BurstView& b : bursts.span()) {
    if (b.where != fx::Flasher::Table) {
      continue;
    }
    // A soft wash from the middle out, fading before the cabinet's edge.
    glow_.add(r, glow, {table::kWidth * 0.5F, table::kHeight * 0.5F},
              {table::kWidth * 0.62F, table::kHeight * 0.62F}, fxColour(b.colour), 0.6F * b.level);
  }
  // The flasher domes.
  for (const fx::BurstView& b : bursts.span()) {
    if (b.where == fx::Flasher::Table) {
      continue;
    }
    const Vec2 at = fx::flasherPosition(b.where);
    const art::Rgb c = fxColour(b.colour);
    const float s = b.level * art::kFlasherStrengthMax;
    // flare.png carries its own bloom: a hot core over it is all it needs.
    glow_.add(r, glow, at, b.radius * 0.30F, mixRgb(c, art::kFxWhite, 0.6F), s);
    flare_.add(r, textures_.flare, at, b.radius * 1.15F, c, s);
  }
  // The multiball sweep climbing the table.
  if (const float y = show.sweepY(); y >= 0.0F) {
    const art::Rgb c = fxColour(show.sweepColour());
    glow_.add(r, glow, {table::kWidth * 0.5F, y}, {table::kWidth * 0.75F, 80.0F}, c, 0.55F);
    glow_.add(r, glow, {table::kWidth * 0.5F, y}, {table::kWidth * 0.6F, 18.0F}, art::kFxWhite, 0.45F);
  }
  // The jackpot's ring.
  if (const float radius = show.ringRadius(); radius >= 0.0F) {
    const float fade = std::max(0.0F, 1.0F - radius / 770.0F);
    // ring.png is brightest at 0.78 of its half size.
    ring_.add(r, textures_.ring, show.ringCentre(), radius / 0.78F + 8.0F, fxColour(show.ringColour()),
              0.75F * fade);
  }
  // The flippers: a soft highlight while raised.
  for (std::size_t i = 0; i < table::kFlippers.size(); ++i) {
    const table::Flipper& rest = table::kFlippers[i];
    const FlipperView f = game.flipper(i, alpha);
    const float span = rest.activeAngle - rest.restAngle;
    const float raised = std::clamp((f.angle - rest.restAngle) / span, 0.0F, 1.0F);
    if (raised > 0.02F) {
      const Vec2 dir{std::cos(f.angle), std::sin(f.angle)};
      for (const float along : {0.35F, 0.75F}) {
        glow_.add(r, glow, f.pivot + dir * (table::kFlipperLength * along), 26.0F, art::kFxMagenta,
                  0.22F * raised);
      }
    }
  }
  // Each ball's trail at speed: the motion reads as a streak.
  const float trailStrength = show.reduceMotion() ? 0.15F : 0.30F;
  for (const BallView& ball : balls) {
    if (!ball.visible || ball.locked) {
      continue;
    }
    const fx::TrailView& trail = show.trail(ball.id);
    const float fast = std::clamp((trail.speed - 4.0F) / 10.0F, 0.0F, 1.0F);
    if (trail.count < 3 || fast <= 0.0F) {
      continue;
    }
    const float radius = table::kBallRadius * ball.scale;
    for (std::size_t k = 1; k < trail.count; ++k) {
      const float age = static_cast<float>(k) / static_cast<float>(trail.count);
      glow_.add(r, glow, trail.points[k], radius * (1.25F - 0.6F * age), art::kFxTrail,
                trailStrength * fast * (1.0F - age));
    }
  }
  flushAll(r);
}

void TableEffects::drawBallGlints(SDL_Renderer* r, const Game& game,
                                  std::span<const BallView> balls, const LightShow& show) {
  SDL_Texture* const glow = textures_.glow;
  // A light hanging over the bumper nest, as the backbox's would: the
  // highlight slides round the chrome as the ball rolls under it.
  const Vec2 lamp{table::kWidth * 0.5F, 240.0F};
  const fx::BurstViews bursts = show.bursts();
  for (const BallView& ball : balls) {
    if (!ball.visible) {
      continue;
    }
    const float radius = table::kBallRadius * ball.scale;
    const Vec2 toLamp = unit(lamp - ball.position);
    glow_.add(r, glow, ball.position + toLamp * (radius * 0.45F), radius * 0.42F, art::kFxWhite, 0.5F);
    // The nearest flasher or struck bumper shows on the chrome in its colour.
    float best = 0.0F;
    Vec2 from{};
    art::Rgb colour = art::kFxWhite;
    for (const fx::BurstView& b : bursts.span()) {
      if (b.where == fx::Flasher::Table) {
        continue;
      }
      const Vec2 at = fx::flasherPosition(b.where);
      const float reach = std::max(b.radius * 2.0F, 160.0F);
      const float strength = b.level * std::max(0.0F, 1.0F - length(at - ball.position) / reach);
      if (strength > best) {
        best = strength;
        from = at;
        colour = fxColour(b.colour);
      }
    }
    for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
      const Vec2 at = table::kBumpers[i].shape.center;
      const float strength =
          game.bumperFlash(i) * std::max(0.0F, 1.0F - length(at - ball.position) / 110.0F);
      if (strength > best) {
        best = strength;
        from = at;
        colour = art::kFxYellow;
      }
    }
    if (best > 0.02F) {
      glow_.add(r, glow, ball.position + unit(from - ball.position) * (radius * 0.55F), radius * 0.55F,
                colour, 0.8F * best);
    }
  }
  flushAll(r);
}

}  // namespace pinball
