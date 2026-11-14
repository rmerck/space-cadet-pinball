#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/Game.hpp"
#include "core/LightShow.hpp"
#include "core/Vec2.hpp"
#include "generated/ArtPalette.hpp"

// Draws the light show (core/LightShow.hpp) over the table: the general
// illumination, the hit flashes, the flasher domes and washes, the sweeps
// and chases, the balls' trails and glints. Called by TableRenderer at its
// layers, in the playfield's own coordinates. Reads the game and the show,
// writes nothing back.
//
// Every additive light of a layer is put in one batch and handed to the
// renderer in one SDL_RenderGeometry call per texture; the batches are fixed
// arrays, so a frame allocates nothing.
namespace pinball {

// The neon of a light show colour.
[[nodiscard]] art::Rgb fxColour(fx::Colour colour) noexcept;
// From a to b, t from 0 to 1.
[[nodiscard]] art::Rgb mixRgb(art::Rgb a, art::Rgb b, float t) noexcept;

// Quads of one texture, additive, in a fixed array.
class GlowBatch {
 public:
  static constexpr std::size_t kMaxQuads = 160;

  GlowBatch() noexcept;
  // A quad centred on the point, of the given half size, tinted, at the
  // strength given (0 to 1, the texture's alpha is multiplied by it). A
  // full batch is drawn and emptied first, so nothing is lost.
  void add(SDL_Renderer* r, SDL_Texture* texture, Vec2 centre, Vec2 half, art::Rgb colour,
           float strength);
  void add(SDL_Renderer* r, SDL_Texture* texture, Vec2 centre, float radius, art::Rgb colour,
           float strength) {
    add(r, texture, centre, {radius, radius}, colour, strength);
  }
  // Draws what is queued, if anything.
  void flush(SDL_Renderer* r);
  [[nodiscard]] std::size_t queued() const noexcept { return quads_; }
  // Quads drawn and draw calls made since the counters were last cleared.
  [[nodiscard]] std::size_t drawnQuads() const noexcept { return drawnQuads_; }
  [[nodiscard]] std::size_t drawCalls() const noexcept { return drawCalls_; }
  void clearCounters() noexcept { drawnQuads_ = drawCalls_ = 0; }

 private:
  std::array<SDL_Vertex, kMaxQuads * 4> vertices_{};
  std::array<int, kMaxQuads * 6> indices_{};
  std::size_t quads_ = 0;
  SDL_Texture* texture_ = nullptr;
  // Which renderer: learnt at the first flush.
  enum class Software : std::uint8_t { Unknown, Yes, No };
  Software software_ = Software::Unknown;
  std::size_t drawnQuads_ = 0;
  std::size_t drawCalls_ = 0;
};

class TableEffects {
 public:
  struct Textures {
    SDL_Texture* glow = nullptr;   // Additive (glow.png).
    SDL_Texture* flare = nullptr;  // Additive (flare.png).
    SDL_Texture* ring = nullptr;   // Additive (ring.png).
  };

  void setTextures(Textures textures) noexcept { textures_ = textures; }

  // Over the playfield art, under everything else: the general
  // illumination, dimmed by the show.
  void drawUnderlay(SDL_Renderer* r, const LightShow& show);
  // Over the lamps and the standing elements: what was struck flashes, the
  // spinner strobes, the chases run toward lit shots.
  void drawElementFlashes(SDL_Renderer* r, const Game& game, const LightShow& show);
  // Over the flippers, under the balls: flasher domes, washes, the sweep,
  // the jackpot ring, the flippers' highlight, the balls' trails.
  void drawShow(SDL_Renderer* r, const Game& game, std::span<const BallView> balls, float alpha,
                const LightShow& show);
  // Over the balls: a moving specular glint and the colour of the nearest
  // light on the chrome.
  void drawBallGlints(SDL_Renderer* r, const Game& game, std::span<const BallView> balls,
                      const LightShow& show);

  // A light of the glow texture in the current layer, and the end of the
  // layer: what the renderer's own lit lamps and bumpers add.
  void addGlow(SDL_Renderer* r, Vec2 centre, float radius, art::Rgb colour, float strength) {
    glow_.add(r, textures_.glow, centre, radius, colour, strength);
  }
  void addRing(SDL_Renderer* r, Vec2 centre, float radius, art::Rgb colour, float strength) {
    ring_.add(r, textures_.ring, centre, radius, colour, strength);
  }
  void endLayer(SDL_Renderer* r) { flushAll(r); }

  // For the frame-time measurement: quads and draw calls this frame.
  [[nodiscard]] std::size_t quads() const noexcept;
  [[nodiscard]] std::size_t drawCalls() const noexcept;
  void clearCounters() noexcept;

 private:
  void flushAll(SDL_Renderer* r);

  Textures textures_;
  GlowBatch glow_;
  GlowBatch flare_;
  GlowBatch ring_;
};

}  // namespace pinball
