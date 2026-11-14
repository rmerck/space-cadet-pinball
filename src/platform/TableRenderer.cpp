#include "platform/TableRenderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/AssetManifest.hpp"
#include "core/TableLayout.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/EmbeddedManifest.hpp"
#include "platform/GameViews.hpp"
#include "platform/Hud.hpp"

namespace pinball {

namespace {

constexpr float kRadiansToDegrees = 180.0F / table::kPi;

// The art and the code share their colours and lamp constants through
// generated/ArtPalette.hpp (tools/art/palette.py). It must describe this
// table and this screen.
static_assert(art::kLampColours.size() == table::kLamps.size());
static_assert(art::kLampLitLevels.size() == table::kLamps.size());
static_assert(art::kArrowAims.size() ==
              static_cast<std::size_t>(table::LampId::ArrowCircuitRight) + 1);
static_assert(art::kBackdrop.r == screen::kBackdropRgb[0] &&
              art::kBackdrop.g == screen::kBackdropRgb[1] &&
              art::kBackdrop.b == screen::kBackdropRgb[2]);

// Sprite geometry, in playfield pixels. These mirror tools/art/make_sprites.py.
constexpr float kFlipperSpriteMargin = 9.0F;  // Room for the bat's halo.
constexpr float kFlipperSpriteWidth = kFlipperSpriteMargin * 2.0F + table::kFlipperBaseRadius +
                                      table::kFlipperLength + table::kFlipperTipRadius;
constexpr float kFlipperSpriteHeight =
    kFlipperSpriteMargin * 2.0F + table::kFlipperBaseRadius * 2.0F;
constexpr float kBumperSpriteRadius = 30.0F;  // Sprite is 64 wide; the skirt is 30.
constexpr float kBarSpriteLength = 34.0F;
constexpr float kBarSpriteRadius = 5.0F;
constexpr float kBarSpriteMargin = 2.0F;

Uint8 toByte(float unit) noexcept {
  // Written so that not-a-number takes the first branch: std::clamp would
  // pass it through, and converting it to an integer is undefined.
  if (!(unit > 0.0F)) {
    return 0;
  }
  return static_cast<Uint8>(std::min(unit, 1.0F) * 255.0F + 0.5F);
}

// Where a sprite goes on the table. Nothing in this table moves, so each
// is worked out once instead of every frame.
struct Placed {
  SDL_FRect dst;
  double angleDegrees;
};

constexpr SDL_FRect centred(Vec2 centre, float w, float h) noexcept {
  return {centre.x - w * 0.5F, centre.y - h * 0.5F, w, h};
}

// The turbo targets' bar sprites, stretched to each bar's length.
std::array<Placed, table::kTargets.size()> makeBarSprites() noexcept {
  std::array<Placed, table::kTargets.size()> sprites{};
  for (std::size_t i = 0; i < sprites.size(); ++i) {
    const table::Capsule& shape = table::kTargets[i].shape;
    const Vec2 a = shape.a;
    const Vec2 b = shape.b;
    const float length = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    const float spriteWidth = (kBarSpriteLength + 2.0F * (kBarSpriteRadius + kBarSpriteMargin)) *
                              (length / kBarSpriteLength);
    const float spriteHeight = 2.0F * (kBarSpriteRadius + kBarSpriteMargin);
    sprites[i] = {
        .dst = centred((a + b) * 0.5F, spriteWidth, spriteHeight),
        .angleDegrees =
            static_cast<double>(std::atan2(b.y - a.y, b.x - a.x) * kRadiansToDegrees)};
  }
  return sprites;
}

// door.png: 28 x 16 units, centred on the door's line.
constexpr float kDoorSpriteWidth = 28.0F;
constexpr float kDoorSpriteHeight = 16.0F;
// kickback.png: 28 x 44 units, its plate at the top, a unit below the
// zone's centre (the art's well is drawn for it there).
constexpr float kKickbackSpriteWidth = 28.0F;
constexpr float kKickbackSpriteHeight = 44.0F;
// spinner_0/1.png: 80 x 24 units, centred between the spinner's posts.
constexpr float kSpinnerSpriteWidth = 80.0F;
constexpr float kSpinnerSpriteHeight = 24.0F;
// The marked doors' own light.
constexpr float kDoorGlowRadius = 14.0F;
constexpr float kDoorGlowStrength = 0.30F;
// A struck bumper: the cap grows a little and a light blooms round it.
constexpr float kBumperPop = 0.08F;
constexpr float kBumperGlowPerRadius = 2.2F;
constexpr float kBumperGlowStrength = 0.40F;
// An unlit turbo target is the bar sprite at this level.
constexpr float kTargetLevel = 1.0F;

// A lit lamp: its insert sprite, tinted, and its glow.
struct LampSprite {
  Placed insert;
  bool arrow;
  float glowRadius;
};

std::array<LampSprite, table::kLamps.size()> makeLampSprites() noexcept {
  std::array<LampSprite, table::kLamps.size()> sprites{};
  for (std::size_t i = 0; i < sprites.size(); ++i) {
    const table::Lamp& lamp = table::kLamps[i];
    if (table::isArrowLamp(lamp.id)) {
      const auto [aimX, aimY] = art::kArrowAims[i];
      const float size = art::kInsertArrowUnits * (lamp.id == table::LampId::ArrowBriefing
                                                       ? art::kInsertArrowBriefingScale
                                                       : 1.0F);
      // The sprite points up the screen; turned to point at its aim.
      const double angle = static_cast<double>(
          std::atan2(aimY - lamp.position.y, aimX - lamp.position.x) * kRadiansToDegrees) +
                           90.0;
      sprites[i] = {.insert = {centred(lamp.position, size, size), angle},
                    .arrow = true,
                    .glowRadius = size * art::kArrowGlowRadiusPerSize};
    } else {
      const float diameter = table::isCourseLamp(lamp.id) ? table::kCourseLampDiameter
                                                          : table::kStatusLampDiameter;
      const float size = diameter * art::kInsertRoundPerDiameter;
      sprites[i] = {.insert = {centred(lamp.position, size, size), 0.0},
                    .arrow = false,
                    .glowRadius = diameter * art::kRoundGlowRadiusPerDiameter};
    }
  }
  return sprites;
}

void setTint(SDL_Texture* texture, art::Rgb tint, float level) noexcept {
  const auto channel = [level](std::uint8_t c) noexcept {
    return toByte(static_cast<float>(c) / 255.0F * level);
  };
  SDL_SetTextureColorMod(texture, channel(tint.r), channel(tint.g), channel(tint.b));
}

art::Rgb blendRgb(art::Rgb a, art::Rgb b, float t) noexcept {
  const auto channel = [t](std::uint8_t from, std::uint8_t to) noexcept {
    return toByte((static_cast<float>(from) + (static_cast<float>(to) - static_cast<float>(from)) *
                                                  std::clamp(t, 0.0F, 1.0F)) /
                  255.0F);
  };
  return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

// --- The table drawn from its layout -------------------------------------------------
// Plain colours, no art: what a build shows until the playfield art for the
// layout exists. Everything is turned into triangles once.

constexpr SDL_FColor rgb(int r, int g, int b) noexcept {
  return {static_cast<float>(r) / 255.0F, static_cast<float>(g) / 255.0F,
          static_cast<float>(b) / 255.0F, 1.0F};
}
constexpr SDL_FColor kOutsideColor =  // Off the playfield: the backdrop.
    rgb(screen::kBackdropRgb[0], screen::kBackdropRgb[1], screen::kBackdropRgb[2]);
constexpr SDL_FColor kFieldColor = rgb(36, 27, 74);     // Where the ball rolls.
constexpr SDL_FColor kWallColor = rgb(40, 210, 200);    // Walls and guides: teal.
constexpr SDL_FColor kDeadColor = rgb(176, 176, 190);   // Dead posts and vanes: metal.
constexpr SDL_FColor kSlingColor = rgb(232, 60, 150);   // Slingshots: hot pink.
constexpr SDL_FColor kSwitchColor = rgb(90, 255, 180);  // Rollover strips.
constexpr SDL_FColor kHoleColor = rgb(255, 150, 40);    // Scoop and hangar boxes.
constexpr SDL_FColor kRampColor = rgb(255, 225, 70);    // Ramp mouths and rails.
constexpr SDL_FColor kLampColor = rgb(120, 96, 176);    // Unlit lamps.
constexpr SDL_FColor kMarkColor = rgb(84, 64, 132);     // Spinner, kickback, lock bays.
constexpr SDL_FColor kLitColor = rgb(255, 240, 120);    // A lamp that is on.
constexpr SDL_FColor kBladeColor = rgb(240, 240, 255);  // The spinner's blade.
constexpr SDL_FColor kDoorDownColor = rgb(120, 70, 20); // Where a dropped door lies.

class Mesh {
 public:
  explicit Mesh(std::vector<SDL_Vertex>& out) noexcept : out_(out) {}

  void triangle(Vec2 a, Vec2 b, Vec2 c, SDL_FColor color) {
    for (const Vec2 p : {a, b, c}) {
      out_.push_back({{p.x, p.y}, color, {0.0F, 0.0F}});
    }
  }
  // A filled circle as a fan.
  void disc(Vec2 centre, float radius, SDL_FColor color) {
    constexpr int kSides = 20;
    Vec2 previous{centre.x + radius, centre.y};
    for (int i = 1; i <= kSides; ++i) {
      const float angle = 2.0F * table::kPi * static_cast<float>(i) / static_cast<float>(kSides);
      const Vec2 next{centre.x + radius * std::cos(angle), centre.y + radius * std::sin(angle)};
      triangle(centre, previous, next, color);
      previous = next;
    }
  }
  // A straight line of a width, square ended.
  void line(Vec2 a, Vec2 b, float width, SDL_FColor color) {
    const Vec2 d = b - a;
    const float length = std::sqrt(d.x * d.x + d.y * d.y);
    if (!(length > 0.0F)) {
      return;
    }
    const Vec2 n{-d.y / length * width * 0.5F, d.x / length * width * 0.5F};
    triangle(a + n, b + n, b - n, color);
    triangle(a + n, b - n, a - n, color);
  }
  // A thick line with round ends: a wall as the physics has it.
  void capsule(const table::Capsule& c, SDL_FColor color) {
    line(c.a, c.b, 2.0F * c.radius, color);
    disc(c.a, c.radius, color);
    disc(c.b, c.radius, color);
  }
  void polyline(std::span<const Vec2> points, float width, SDL_FColor color, bool closed) {
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
      line(points[i], points[i + 1], width, color);
      disc(points[i + 1], width * 0.5F, color);
    }
    if (closed && points.size() > 2) {
      line(points.back(), points.front(), width, color);
      disc(points.front(), width * 0.5F, color);
    }
  }
  // The outline of a box.
  void box(Vec2 centre, Vec2 half, float width, SDL_FColor color) {
    const std::array<Vec2, 4> corners{{{centre.x - half.x, centre.y - half.y},
                                       {centre.x + half.x, centre.y - half.y},
                                       {centre.x + half.x, centre.y + half.y},
                                       {centre.x - half.x, centre.y + half.y}}};
    polyline(corners, width, color, true);
  }
  void ring(Vec2 centre, float radius, float width, SDL_FColor color) {
    constexpr int kSides = 20;
    std::array<Vec2, kSides> points{};
    for (int i = 0; i < kSides; ++i) {
      const float angle = 2.0F * table::kPi * static_cast<float>(i) / static_cast<float>(kSides);
      points[static_cast<std::size_t>(i)] = {centre.x + radius * std::cos(angle),
                                             centre.y + radius * std::sin(angle)};
    }
    polyline(points, width, color, true);
  }
  // A simple polygon, filled, by clipping ears. Either winding.
  void polygon(std::span<const Vec2> outline, SDL_FColor color) {
    std::vector<Vec2> points(outline.begin(), outline.end());
    const auto cross = [](Vec2 o, Vec2 a, Vec2 b) {
      return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };
    float doubleArea = 0.0F;
    for (std::size_t i = 0; i < points.size(); ++i) {
      const Vec2 a = points[i];
      const Vec2 b = points[(i + 1) % points.size()];
      doubleArea += a.x * b.y - b.x * a.y;
    }
    const float turn = doubleArea >= 0.0F ? 1.0F : -1.0F;
    while (points.size() > 3) {
      bool clipped = false;
      for (std::size_t i = 0; i < points.size() && !clipped; ++i) {
        const Vec2 a = points[(i + points.size() - 1) % points.size()];
        const Vec2 b = points[i];
        const Vec2 c = points[(i + 1) % points.size()];
        if (cross(a, b, c) * turn <= 0.0F) {
          continue;  // A reflex corner is not an ear.
        }
        bool empty = true;
        for (const Vec2 p : points) {
          const bool corner = (p.x == a.x && p.y == a.y) || (p.x == b.x && p.y == b.y) ||
                              (p.x == c.x && p.y == c.y);
          if (!corner && cross(a, b, p) * turn >= 0.0F && cross(b, c, p) * turn >= 0.0F &&
              cross(c, a, p) * turn >= 0.0F) {
            empty = false;
            break;
          }
        }
        if (empty) {
          triangle(a, b, c, color);
          points.erase(points.begin() + static_cast<std::ptrdiff_t>(i));
          clipped = true;
        }
      }
      if (!clipped) {
        return;  // Not a simple polygon: leave the rest unfilled.
      }
    }
    if (points.size() == 3) {
      triangle(points[0], points[1], points[2], color);
    }
  }

 private:
  std::vector<SDL_Vertex>& out_;
};

std::vector<SDL_Vertex> makePlayfieldMesh() {
  std::vector<SDL_Vertex> vertices;
  Mesh mesh(vertices);
  constexpr float kChainWidth = 4.0F;

  // The field inside the shell, with the lane island cut back out of it.
  mesh.polygon(table::kShell, kFieldColor);
  mesh.polygon(table::kLaneIsland, kOutsideColor);
  mesh.polyline(table::kShell, kChainWidth, kWallColor, true);
  mesh.polyline(table::kLaneIsland, kChainWidth, kWallColor, true);
  // The inner arch without its ghost points; the vanes likewise.
  mesh.polyline(std::span(table::kInnerArch).subspan(1, table::kInnerArch.size() - 2), kChainWidth,
                kWallColor, false);
  for (const auto& vane : {table::kSlingVaneLeft, table::kSlingVaneRight}) {
    mesh.polyline(std::span(vane).subspan(1, vane.size() - 2), 2.0F, kDeadColor, false);
  }

  // Marks under everything that stands on the table: lamps, switches, the
  // boxes that take the ball, the rails.
  for (const table::Lamp& lamp : table::kLamps) {
    const Vec2 at = lamp.position;
    if (table::isArrowLamp(lamp.id)) {
      // An arrow pointing up the table at its shot.
      const float half = table::kArrowLampLength * 0.5F;
      mesh.triangle({at.x, at.y - half}, {at.x + half * 0.7F, at.y + half},
                    {at.x - half * 0.7F, at.y + half}, kLampColor);
    } else {
      const float radius = (table::isCourseLamp(lamp.id) ? table::kCourseLampDiameter
                                                          : table::kStatusLampDiameter) *
                           0.5F;
      mesh.disc(at, radius, kLampColor);
    }
  }
  for (const table::Rollover& rollover : table::kRollovers) {
    mesh.box(rollover.zone.center, table::rolloverHalfSize(rollover), 2.0F, kSwitchColor);
  }
  for (const Vec2 bay : table::kLockBays) {
    mesh.ring(bay, table::kBallRadius, 2.0F, kMarkColor);
  }
  for (const table::Hole& hole : table::kHoles) {
    const bool ramp = table::isRamp(hole.type);
    mesh.box(hole.center, hole.halfSize, 2.0F, ramp ? kRampColor : kHoleColor);
    if (ramp) {
      mesh.polyline(table::rampRail(hole.type), 3.0F, kRampColor, false);
    }
    // Where the ball comes back.
    mesh.ring(hole.exitPosition, 5.0F, 2.0F, ramp ? kRampColor : kHoleColor);
  }
  mesh.line(table::kSpinner.a, table::kSpinner.b, 3.0F, kMarkColor);
  mesh.ring(table::kKickbackZone.center, table::kKickbackZone.radius, 2.0F, kMarkColor);

  for (const table::Capsule& wall : table::kAllWalls) {
    mesh.capsule(wall, kWallColor);
  }
  for (const table::Capsule& post : table::kDeadPosts) {
    mesh.capsule(post, kDeadColor);
  }
  for (const table::Triangle& sling : table::kSlingshots) {
    mesh.triangle(sling.points[0], sling.points[1], sling.points[2], kSlingColor);
    mesh.polyline(sling.points, 2.0F * sling.cornerRadius, kSlingColor, true);
  }
  return vertices;
}

// The layout drawing as a texture of the size the art has (two pixels to a
// table unit), drawn once: a frame then costs one copy, as it does with art,
// and not some nine thousand triangles. A target texture loses what is in
// it when the device is reset or the targets are; the application makes the
// renderer again on either event.
TexturePtr makePlayfieldTexture(SDL_Renderer* r) {
  constexpr int kScale = 2;
  TexturePtr texture(SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET,
                                       static_cast<int>(table::kWidth) * kScale,
                                       static_cast<int>(table::kHeight) * kScale));
  SDL_Texture* const before = SDL_GetRenderTarget(r);
  if (!texture || !SDL_SetRenderTarget(r, texture.get())) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not make the playfield texture: %s",
                 SDL_GetError());
    return nullptr;
  }
  // A render target has its own scale, viewport and clip: nothing of the
  // window's is disturbed.
  const std::vector<SDL_Vertex> mesh = makePlayfieldMesh();
  bool drawn = SDL_SetRenderScale(r, static_cast<float>(kScale), static_cast<float>(kScale));
  drawn = SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE) && drawn;
  drawn = SDL_SetRenderDrawColor(r, screen::kBackdropRgb[0], screen::kBackdropRgb[1],
                                 screen::kBackdropRgb[2], SDL_ALPHA_OPAQUE) &&
          drawn;
  drawn = SDL_RenderClear(r) && drawn;
  drawn = SDL_RenderGeometry(r, nullptr, mesh.data(), static_cast<int>(mesh.size()), nullptr, 0) &&
          drawn;
  drawn = SDL_SetRenderTarget(r, before) && drawn;
  if (!drawn) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not draw the playfield texture: %s",
                 SDL_GetError());
    return nullptr;
  }
  return texture;
}

// The lamps of a light show: each insert at its level in the neon of its
// colour, a halo, and a hot core.
void drawLitLamps(SDL_Renderer* r, SDL_Texture* arrow, SDL_Texture* round, TableEffects& effects,
                  const LightShow& show, std::span<const LampSprite> sprites) {
  for (std::size_t i = 0; i < sprites.size(); ++i) {
    const float level = show.lampLevel(table::kLamps[i].id);
    if (level < 0.01F) {
      continue;
    }
    SDL_Texture* const insert = sprites[i].arrow ? arrow : round;
    setTint(insert, art::kLampNeon[i], art::kLampNeonLevels[i]);
    SDL_SetTextureAlphaMod(insert, toByte(level * 1.15F));
    SDL_RenderTextureRotated(r, insert, nullptr, &sprites[i].insert.dst,
                             sprites[i].insert.angleDegrees, nullptr, SDL_FLIP_NONE);
  }
  SDL_SetTextureAlphaMod(arrow, SDL_ALPHA_OPAQUE);
  SDL_SetTextureAlphaMod(round, SDL_ALPHA_OPAQUE);
  for (std::size_t i = 0; i < sprites.size(); ++i) {
    const float level = show.lampLevel(table::kLamps[i].id);
    if (level < 0.3F) {
      continue;  // A glimmer is the lens alone.
    }
    const Vec2 at = table::kLamps[i].position;
    const art::Rgb neon = art::kLampNeon[i];
    // The halo and the core grow faster than the lens: a glimmer has next to
    // none, a lit lamp all of it.
    const float lit = level * std::sqrt(level);
    effects.addGlow(r, at, sprites[i].glowRadius * 1.45F, neon, art::kNeonGlowStrength * lit);
    effects.addGlow(r, at, sprites[i].glowRadius * 0.45F, mixRgb(neon, art::kFxWhite, 0.55F),
                    0.40F * lit);
  }
  effects.endLayer(r);
}

}  // namespace

std::unique_ptr<TableRenderer> TableRenderer::create(SDL_Renderer* renderer,
                                                     const AssetLoader& assets,
                                                     TextureSet set) {
  if (renderer == nullptr) {
    return nullptr;
  }
  // A missing or damaged asset is fatal: fail at startup, not mid-game.
  Textures t;
  // Art that a build may be without. One the manifest lists must load like
  // any other; one it does not list is simply not there.
  bool failed = false;
  const auto optional = [&](std::string_view name) -> TexturePtr {
    if (findAsset(embeddedAssets(), name) == nullptr) {
      return nullptr;
    }
    TexturePtr texture = assets.loadTexture(renderer, name);
    failed = failed || !texture;
    return texture;
  };
  const auto required = [&](std::string_view name) -> TexturePtr {
    TexturePtr texture = assets.loadTexture(renderer, name);
    failed = failed || !texture;
    return texture;
  };
  // The playfield art belongs to one layout of the table. A build without
  // it draws the table from the layout itself.
  t.background = optional("table/background.png");
  if (!t.background && !failed) {
    t.background = makePlayfieldTexture(renderer);
    failed = !t.background;
    t.backgroundFromLayout = true;
  }
  t.ball = required("sprites/ball.png");
  t.flipper = required("sprites/flipper.png");
  t.bumper = required("sprites/bumper.png");
  t.bumperCap = required("sprites/bumper_cap.png");
  t.glow = required("sprites/glow.png");
  t.flare = required("sprites/flare.png");
  t.ring = required("sprites/ring.png");
  t.bar = required("sprites/bar.png");
  t.door = required("sprites/door.png");
  t.plunger = required("sprites/plunger.png");
  t.panel = required("ui/panel.png");
  if (!t.backgroundFromLayout) {
    // What goes over the art: the layout drawing has its own lamps, blade
    // and kickback.
    t.insertArrow = required("sprites/insert_arrow.png");
    t.insertRound = required("sprites/insert_round.png");
    t.spinner[0] = required("sprites/spinner_0.png");
    t.spinner[1] = required("sprites/spinner_1.png");
    t.kickback = required("sprites/kickback.png");
  }
  t.title = optional("ui/title.png");
  t.dialog = optional("ui/dialog.png");
  if (set == TextureSet::All) {
    t.strip = optional("ui/strip.png");
    t.deck = optional("ui/deck.png");
    t.backdrop = optional("ui/backdrop.png");
    t.button = optional("sprites/button.png");
    // The lights of the button are nothing without the button.
    if (t.button) {
      t.buttonGloss = optional("sprites/button_gloss.png");
    }
  }
  if (failed) {
    return nullptr;
  }
  for (SDL_Texture* opaque :
       {t.background.get(), t.panel.get(), t.strip.get(), t.deck.get(), t.backdrop.get()}) {
    // Opaque: skip blending for the largest draws of the frame.
    if (opaque != nullptr) {
      SDL_SetTextureBlendMode(opaque, SDL_BLENDMODE_NONE);
    }
  }
  // The glow is additive everywhere except the balls' shadows, which switch
  // it for one draw and switch it back.
  SDL_SetTextureBlendMode(t.glow.get(), SDL_BLENDMODE_ADD);
  SDL_SetTextureBlendMode(t.flare.get(), SDL_BLENDMODE_ADD);
  SDL_SetTextureBlendMode(t.ring.get(), SDL_BLENDMODE_ADD);
  return std::unique_ptr<TableRenderer>(new TableRenderer(std::move(t)));
}

TableRenderer::TableRenderer(Textures textures) : textures_(std::move(textures)) {
  effects_.setTextures({.glow = textures_.glow.get(),
                        .flare = textures_.flare.get(),
                        .ring = textures_.ring.get()});
}

OverlayArt TableRenderer::overlayArt() const noexcept {
  return {.dialog = textures_.dialog.get(),
          .title = textures_.title.get(),
          .button = {.face = textures_.button.get(), .gloss = textures_.buttonGloss.get()}};
}

void TableRenderer::drawGlow(SDL_Renderer* r, Vec2 center, float radius, art::Rgb color,
                             float strength) const {
  strength = std::min(strength, art::kMaxGlowStrength);
  if (!(strength > 0.0F)) {
    return;
  }
  SDL_Texture* const glow = textures_.glow.get();
  SDL_SetTextureColorMod(glow, color.r, color.g, color.b);
  SDL_SetTextureAlphaMod(glow, toByte(strength));
  const SDL_FRect dst{center.x - radius, center.y - radius, radius * 2.0F, radius * 2.0F};
  SDL_RenderTexture(r, glow, nullptr, &dst);
}

void TableRenderer::drawBumpers(SDL_Renderer* r, const Game& game, const LightShow* show) const {
  SDL_Texture* const body = textures_.bumper.get();
  SDL_Texture* const cap = textures_.bumperCap.get();
  if (show != nullptr) {
    drawLitBumpers(r, game, *show);
    return;
  }
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Bumper& bumper = table::kBumpers[i];
    const float flash = std::clamp(game.bumperFlash(i), 0.0F, 1.0F);
    // The body takes the thruster's colour, hotter when struck; the chrome
    // cap goes over it as it is.
    const float scale = bumper.shape.radius / kBumperSpriteRadius * (1.0F + kBumperPop * flash);
    const float half = 32.0F * scale;
    const SDL_FRect dst = centred(bumper.shape.center, half * 2.0F, half * 2.0F);
    setTint(body, blendRgb(art::kTintBumper, art::kTintBumperFlash, flash), 1.0F);
    SDL_RenderTexture(r, body, nullptr, &dst);
    SDL_RenderTexture(r, cap, nullptr, &dst);
    drawGlow(r, bumper.shape.center, bumper.shape.radius * kBumperGlowPerRadius,
             art::kTintBumperFlash, flash * kBumperGlowStrength);
  }
}

void TableRenderer::drawLamps(SDL_Renderer* r, const TableLights& lights,
                              const LightShow* show) const {
  static const std::array<LampSprite, table::kLamps.size()> kSprites = makeLampSprites();
  if (show != nullptr) {
    drawLitLamps(r, textures_.insertArrow.get(), textures_.insertRound.get(), effects_, *show,
                 kSprites);
    return;
  }
  // Inserts first, then every glow, so a glow is never cut by the next lamp.
  for (std::size_t i = 0; i < kSprites.size(); ++i) {
    if (!lights.lamps[i]) {
      continue;
    }
    SDL_Texture* const insert =
        kSprites[i].arrow ? textures_.insertArrow.get() : textures_.insertRound.get();
    setTint(insert, art::kLampColours[i], art::kLampLitLevels[i]);
    SDL_RenderTextureRotated(r, insert, nullptr, &kSprites[i].insert.dst,
                             kSprites[i].insert.angleDegrees, nullptr, SDL_FLIP_NONE);
  }
  for (std::size_t i = 0; i < kSprites.size(); ++i) {
    if (lights.lamps[i]) {
      drawGlow(r, table::kLamps[i].position, kSprites[i].glowRadius, art::kLampColours[i],
               art::kLampGlowStrength);
    }
  }
}

void TableRenderer::drawSpinnerAndKickback(SDL_Renderer* r, const Game& game) const {
  // The art's blade hangs edge-on at rest (phase 0 and 0.5) and shows its
  // flat face for the middle half of each half turn.
  const float phase = game.spinnerPhase();
  const float quarter = std::fmod(phase * 2.0F + 0.25F, 1.0F);
  SDL_Texture* const blade = textures_.spinner[quarter < 0.5F ? 0 : 1].get();
  const Vec2 middle = (table::kSpinner.a + table::kSpinner.b) * 0.5F;
  const SDL_FRect spinner = centred(middle, kSpinnerSpriteWidth, kSpinnerSpriteHeight);
  SDL_RenderTexture(r, blade, nullptr, &spinner);

  const SDL_FRect kickback = centred(
      {table::kKickbackZone.center.x, table::kKickbackZone.center.y + 1.0F},
      kKickbackSpriteWidth, kKickbackSpriteHeight);
  SDL_RenderTexture(r, textures_.kickback.get(), nullptr, &kickback);
}

void TableRenderer::drawElements(SDL_Renderer* r, const Game& game,
                                 const TableLights& lights) const {
  elements_.clear();
  Mesh mesh(elements_);
  // Doors that have dropped lie flat: an outline where each stood.
  if (!game.doorBankUp()) {
    for (const table::Capsule& door : table::kDropTargets) {
      mesh.box((door.a + door.b) * 0.5F,
               {(door.b.x - door.a.x) * 0.5F + door.radius, door.radius}, 2.0F, kDoorDownColor);
    }
  }
  // The lamps that are lit (TableLights says which).
  for (std::size_t i = 0; i < table::kLamps.size(); ++i) {
    if (lights.lamps[i]) {
      mesh.disc(table::kLamps[i].position, table::kStatusLampDiameter * 0.5F, kLitColor);
    }
  }
  // The spinner's blade seen from above: full depth when it lies flat, an
  // edge when it stands, twice a turn.
  const float turn = 2.0F * table::kPi * game.spinnerPhase();
  constexpr float kBladeDepth = 16.0F;
  mesh.line(table::kSpinner.a, table::kSpinner.b,
            2.0F + (kBladeDepth - 2.0F) * std::fabs(std::cos(turn)), kBladeColor);
  SDL_RenderGeometry(r, nullptr, elements_.data(), static_cast<int>(elements_.size()), nullptr, 0);
}

void TableRenderer::drawTargets(SDL_Renderer* r, const Game& game,
                                const TableLights& lights) const {
  static const std::array<Placed, table::kTargets.size()> kBars = makeBarSprites();
  SDL_Texture* const bar = textures_.bar.get();
  setTint(bar, art::kTintTurboTarget, kTargetLevel);
  for (const Placed& placed : kBars) {
    SDL_RenderTextureRotated(r, bar, nullptr, &placed.dst, placed.angleDegrees, nullptr,
                             SDL_FLIP_NONE);
  }
  // The bay doors stand while the bank is up: steel, or orange with a light
  // of their own once marked. Down, the art's door track shows.
  if (!game.doorBankUp()) {
    return;
  }
  SDL_Texture* const door = textures_.door.get();
  for (std::size_t i = 0; i < table::kDropTargets.size(); ++i) {
    const table::Capsule& shape = table::kDropTargets[i];
    const Vec2 centre{(shape.a.x + shape.b.x) * 0.5F, shape.a.y};
    const bool marked = lights.doorsMarked[i];
    setTint(door, marked ? art::kTintDoorMarked : art::kTintDoor, 1.0F);
    const SDL_FRect dst = centred(centre, kDoorSpriteWidth, kDoorSpriteHeight);
    SDL_RenderTexture(r, door, nullptr, &dst);
    if (marked) {
      drawGlow(r, centre, kDoorGlowRadius, art::kOrange, kDoorGlowStrength);
    }
  }
}

void TableRenderer::drawSlingshotFlashes(SDL_Renderer* r, const Game& game,
                                         const LightShow* show) const {
  if (show != nullptr) {
    drawLitSlingshots(r, game, *show);
    return;
  }
  for (std::size_t i = 0; i < table::kSlingshots.size(); ++i) {
    // On the middle of the kicking face.
    const table::Triangle& sling = table::kSlingshots[i];
    drawGlow(r, (sling.points[0] + sling.points[2]) * 0.5F, 60.0F, art::kPink,
             game.slingshotFlash(i) * art::kMaxGlowStrength);
  }
}

void TableRenderer::drawLitBumpers(SDL_Renderer* r, const Game& game,
                                   const LightShow& show) const {
  SDL_Texture* const body = textures_.bumper.get();
  SDL_Texture* const cap = textures_.bumperCap.get();
  const float gi = show.giLevel();
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Bumper& bumper = table::kBumpers[i];
    const float flash = std::clamp(game.bumperFlash(i), 0.0F, 1.0F);
    const float radius = bumper.shape.radius;
    // Struck: the body runs through yellow to white-hot and swells; the cap
    // pulses larger still.
    const float scale = radius / kBumperSpriteRadius * (1.0F + 0.12F * flash);
    const SDL_FRect dst = centred(bumper.shape.center, 64.0F * scale, 64.0F * scale);
    const art::Rgb tint = flash < 0.5F
                              ? blendRgb(art::kTintBumper, art::kFxYellow, flash * 2.0F)
                              : blendRgb(art::kFxYellow, art::kFxWhite, (flash - 0.5F) * 1.2F);
    setTint(body, tint, 1.0F);
    SDL_RenderTexture(r, body, nullptr, &dst);
    const float capScale = radius / kBumperSpriteRadius * (1.0F + 0.18F * flash);
    const SDL_FRect capDst = centred(bumper.shape.center, 64.0F * capScale, 64.0F * capScale);
    SDL_RenderTexture(r, cap, nullptr, &capDst);
  }
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Bumper& bumper = table::kBumpers[i];
    const float flash = std::clamp(game.bumperFlash(i), 0.0F, 1.0F);
    const float radius = bumper.shape.radius;
    // A real machine's bumper lamps are always on: a steady warm light that
    // follows the general illumination, then the strike over it.
    effects_.addGlow(r, bumper.shape.center, radius * 1.9F, art::kFxOrange, 0.18F * gi);
    if (flash > 0.0F) {
      effects_.addGlow(r, bumper.shape.center, radius * 2.8F, art::kFxYellow, 0.65F * flash);
      effects_.addGlow(r, bumper.shape.center, radius * 0.7F, art::kFxWhite, 0.45F * flash);
      effects_.addRing(r, bumper.shape.center, radius * (1.05F + 1.3F * (1.0F - flash)),
                       art::kFxOrange, 0.8F * flash);
    }
  }
  effects_.endLayer(r);
}

void TableRenderer::drawLitSlingshots(SDL_Renderer* r, const Game& game,
                                      const LightShow& show) const {
  const float gi = show.giLevel();
  std::array<SDL_Vertex, 3 * table::kSlingshots.size()> faces{};
  int count = 0;
  for (std::size_t i = 0; i < table::kSlingshots.size(); ++i) {
    const table::Triangle& sling = table::kSlingshots[i];
    const float flash = std::clamp(game.slingshotFlash(i), 0.0F, 1.0F);
    const Vec2 face = (sling.points[0] + sling.points[2]) * 0.5F;
    effects_.addGlow(r, face, 46.0F, art::kFxMagenta, 0.10F * gi);
    if (flash > 0.0F) {
      effects_.addGlow(r, face, 72.0F, art::kFxMagenta, 0.65F * flash);
      effects_.addGlow(r, face, 22.0F, art::kFxWhite, 0.40F * flash);
      // The plastic over it lights up whole.
      const art::Rgb c = art::kFxMagenta;
      const SDL_FColor colour{static_cast<float>(c.r) / 255.0F, static_cast<float>(c.g) / 255.0F,
                              static_cast<float>(c.b) / 255.0F, 0.45F * flash};
      for (const Vec2 p : sling.points) {
        faces[static_cast<std::size_t>(count++)] = {{p.x, p.y}, colour, {0.0F, 0.0F}};
      }
    }
  }
  if (count > 0) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_ADD);
    SDL_RenderGeometry(r, nullptr, faces.data(), count, nullptr, 0);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  }
  effects_.endLayer(r);
}

void TableRenderer::drawPlunger(SDL_Renderer* r, const Game& game) const {
  const SDL_FRect plunger{table::kLaneCenterX - table::kPlungerWidth * 0.5F,
                          table::kPlungerRestTopY + game.plungerPull(), table::kPlungerWidth,
                          table::kPlungerHeight};
  SDL_RenderTexture(r, textures_.plunger.get(), nullptr, &plunger);
}

void TableRenderer::drawFlippers(SDL_Renderer* r, const Game& game, float alpha,
                                 bool atRest) const {
  SDL_Texture* const sprite = textures_.flipper.get();
  // The pivot inside the sprite: the centre of the base circle.
  const SDL_FPoint pivotInSprite{kFlipperSpriteMargin + table::kFlipperBaseRadius,
                                 kFlipperSpriteHeight * 0.5F};
  for (std::size_t i = 0; i < table::kFlippers.size(); ++i) {
    FlipperView flipper = game.flipper(i, alpha);
    if (atRest) {
      flipper.angle = table::kFlippers[i].restAngle;
    }
    const SDL_FRect dst{flipper.pivot.x - pivotInSprite.x, flipper.pivot.y - pivotInSprite.y,
                        kFlipperSpriteWidth, kFlipperSpriteHeight};
    // The right flipper points left, so its sprite is mirrored top to bottom
    // to keep the highlight on the upper edge.
    const SDL_FlipMode flip = i == table::kRightFlipperIndex ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE;
    SDL_RenderTextureRotated(r, sprite, nullptr, &dst,
                             static_cast<double>(flipper.angle * kRadiansToDegrees),
                             &pivotInSprite, flip);
  }
}

void TableRenderer::drawBalls(SDL_Renderer* r, std::span<const BallView> balls) const {
  SDL_Texture* const glow = textures_.glow.get();
  // Two rounds, so that a ball up on a rail passes over one on the table.
  // Within a round all the shadows go down first, then the balls. The ball
  // carries its own dark rim, so it needs no halo: nothing lighter than it
  // is drawn after it.
  for (const bool raised : {false, true}) {
    const auto inRound = [raised](const BallView& ball) noexcept {
      return ball.visible && ball.raised == raised;
    };
    if (std::ranges::none_of(balls, inRound)) {
      continue;
    }
    // Soft contact shadows, offset further when the ball is up on a rail.
    // The glow texture is additive everywhere else: switched once for all of
    // them, and back.
    SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(glow, art::kInk.r, art::kInk.g, art::kInk.b);
    SDL_SetTextureAlphaMod(glow, toByte(art::kBallShadowAlpha));
    for (const BallView& ball : balls) {
      if (!inRound(ball)) {
        continue;
      }
      const float radius = table::kBallRadius * ball.scale;
      const float lift = (ball.scale - 1.0F) * 40.0F;
      const float shadowRadius = radius * 1.55F;
      const SDL_FRect shadow = centred(
          {ball.position.x + art::kBallShadowOffsetX + lift,
           ball.position.y + art::kBallShadowOffsetY + lift},
          shadowRadius * 2.0F, shadowRadius * 2.0F);
      SDL_RenderTexture(r, glow, nullptr, &shadow);
    }
    SDL_SetTextureBlendMode(glow, SDL_BLENDMODE_ADD);

    for (const BallView& ball : balls) {
      if (!inRound(ball)) {
        continue;
      }
      // The sprite is larger than the ball: its disc is two radii of the
      // art::kBallSpriteUnits it covers.
      const float size = art::kBallSpriteUnits * ball.scale;
      const SDL_FRect dst = centred(ball.position, size, size);
      SDL_RenderTexture(r, textures_.ball.get(), nullptr, &dst);
    }
  }
}

void TableRenderer::drawBackdrop(SDL_Renderer* r, const screen::Layout& layout) const {
  if (r == nullptr || layout.idiom == screen::Idiom::Desktop) {
    return;
  }
  // What the composition covers: everything outside it is drawn here.
  const screen::Rect& field = layout.playfield;
  const bool stacked = layout.composition == screen::Composition::Stack;
  const float coveredLeft = field.x;
  const float coveredRight = stacked ? field.x + field.w : layout.hud.x + layout.hud.w;
  const float coveredTop = stacked ? layout.hud.y : field.y;
  const float coveredBottom = stacked ? layout.deck.y + layout.deck.h : field.y + field.h;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, screen::kBackdropRgb[0], screen::kBackdropRgb[1],
                         screen::kBackdropRgb[2], SDL_ALPHA_OPAQUE);
  if (SDL_Texture* const tile = textures_.backdrop.get()) {
    // The tile, 512 pixels to 256 units, laid from the canvas origin, in
    // the bands round the composition only: under it the art would be drawn
    // over at once. Each copy is cut to its band by hand, so a software
    // renderer touches no pixel twice.
    constexpr float kTile = 256.0F;
    constexpr float kPixelsPerUnit = 2.0F;
    const std::array<SDL_FRect, 4> bands{{
        {0.0F, 0.0F, layout.canvasWidth, coveredTop},
        {0.0F, coveredBottom, layout.canvasWidth, layout.canvasHeight - coveredBottom},
        {0.0F, coveredTop, coveredLeft, coveredBottom - coveredTop},
        {coveredRight, coveredTop, layout.canvasWidth - coveredRight, coveredBottom - coveredTop},
    }};
    for (const SDL_FRect& band : bands) {
      if (!(band.w > 0.0F) || !(band.h > 0.0F)) {
        continue;
      }
      SDL_RenderFillRect(r, &band);
      const float bandRight = band.x + band.w;
      const float bandBottom = band.y + band.h;
      for (float y = std::floor(band.y / kTile) * kTile; y < bandBottom; y += kTile) {
        for (float x = std::floor(band.x / kTile) * kTile; x < bandRight; x += kTile) {
          const float left = std::max(x, band.x);
          const float top = std::max(y, band.y);
          const float right = std::min(x + kTile, bandRight);
          const float bottom = std::min(y + kTile, bandBottom);
          if (right <= left || bottom <= top) {
            continue;
          }
          const SDL_FRect src{(left - x) * kPixelsPerUnit, (top - y) * kPixelsPerUnit,
                              (right - left) * kPixelsPerUnit, (bottom - top) * kPixelsPerUnit};
          const SDL_FRect dst{left, top, right - left, bottom - top};
          SDL_RenderTexture(r, tile, &src, &dst);
        }
      }
    }
    return;
  }
  // No art: the flat colour under a lattice of dim dots, left out where the
  // composition will cover it anyway.
  const SDL_FRect whole{0.0F, 0.0F, layout.canvasWidth, layout.canvasHeight};
  SDL_RenderFillRect(r, &whole);
  constexpr float kPitch = 32.0F;
  constexpr float kDot = 4.0F;
  SDL_SetRenderDrawColor(r, 38, 16, 78, SDL_ALPHA_OPAQUE);
  // Handed over in batches: one call for hundreds of dots, not one each.
  std::array<SDL_FRect, 256> dots{};
  std::size_t count = 0;
  const auto flush = [&]() {
    if (count > 0) {
      SDL_RenderFillRects(r, dots.data(), static_cast<int>(count));
      count = 0;
    }
  };
  int row = 0;
  for (float y = kPitch * 0.5F; y < layout.canvasHeight; y += kPitch * 0.5F, ++row) {
    const bool rowCovered = y >= coveredTop && y + kDot <= coveredBottom;
    for (float x = row % 2 == 0 ? kPitch * 0.5F : kPitch; x < layout.canvasWidth; x += kPitch) {
      if (rowCovered && x >= coveredLeft && x + kDot <= coveredRight) {
        // Skip to the far side of the composition.
        x += std::floor((coveredRight - x) / kPitch) * kPitch;
        continue;
      }
      dots[count++] = {x, y, kDot, kDot};
      if (count == dots.size()) {
        flush();
      }
    }
  }
  flush();
}

void TableRenderer::draw(SDL_Renderer* r, const Game& game, float alpha,
                         const screen::Layout& layout, const DrawOptions& options,
                         const HudExtras& extras) const {
  if (r == nullptr) {
    return;
  }
  // In the playfield's own coordinates: everything on the table is drawn
  // inside a viewport placed at the layout's playfield.
  const SDL_FRect playfield{0.0F, 0.0F, table::kWidth, table::kHeight};
  const int left = static_cast<int>(std::lround(layout.playfield.x));
  const int top = static_cast<int>(std::lround(layout.playfield.y));

  // The viewport also clips the table so that nothing spills onto the rest
  // of the canvas. A nudge shifts the viewport a few pixels, unless the
  // player has asked for less motion.
  const Vec2 shake = options.reduceMotion ? Vec2{} : game.shake();
  const int shakeX = static_cast<int>(std::lround(std::clamp(shake.x, -12.0F, 12.0F)));
  const int shakeY = static_cast<int>(std::lround(std::clamp(shake.y, -12.0F, 12.0F)));
  const SDL_Rect view{left + shakeX, top + shakeY, static_cast<int>(table::kWidth),
                      static_cast<int>(table::kHeight)};
  SDL_SetRenderViewport(r, &view);
  // The shifted viewport may reach past the playfield's place on the canvas,
  // and at the canvas edge on a window of another shape that is the
  // letterbox. Clip to the playfield's own rectangle; a clip rectangle is
  // relative to the viewport.
  const SDL_Rect clip{-shakeX, -shakeY, view.w, view.h};
  SDL_SetRenderClipRect(r, &clip);

  SDL_RenderTexture(r, textures_.background.get(), nullptr, &playfield);
  const LightShow* const show = options.lightShow;
  if (show != nullptr) {
    effects_.drawUnderlay(r, *show);
  }
  const TableLights lights = tableLights(game);
  if (textures_.backgroundFromLayout) {
    drawElements(r, game, lights);
  } else {
    drawLamps(r, lights, show);
    drawSpinnerAndKickback(r, game);
  }
  drawTargets(r, game, lights);
  if (show != nullptr) {
    effects_.drawElementFlashes(r, game, *show);
  }
  drawBumpers(r, game, show);
  drawSlingshotFlashes(r, game, show);
  drawPlunger(r, game);
  drawFlippers(r, game, alpha, options.flippersAtRest);
  {
    // The shadows follow the same in-between positions as the balls.
    const DrawnBalls balls = ballsToDraw(game, alpha, options.extraBalls);
    if (show != nullptr) {
      effects_.drawShow(r, game, balls.span(), alpha, *show);
    }
    drawBalls(r, balls.span());
    if (show != nullptr) {
      effects_.drawBallGlints(r, game, balls.span(), *show);
    }
  }

  if (options.dimmed) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, screen::kBackdropRgb[0], screen::kBackdropRgb[1],
                           screen::kBackdropRgb[2], 140);
    SDL_RenderFillRect(r, &playfield);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  }
  SDL_SetRenderClipRect(r, nullptr);
  SDL_SetRenderViewport(r, nullptr);

  drawHud(r,
          HudArt{.panel = textures_.panel.get(),
                 .strip = textures_.strip.get(),
                 .title = textures_.title.get()},
          game, layout, extras);
}

}  // namespace pinball
