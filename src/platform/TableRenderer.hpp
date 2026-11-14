#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/Game.hpp"
#include "core/ScreenLayout.hpp"
#include "core/TouchControls.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/Assets.hpp"
#include "platform/Hud.hpp"
#include "platform/TableEffects.hpp"
#include "platform/TableLights.hpp"
#include "platform/TouchOverlay.hpp"

namespace pinball {

struct DrawOptions {
  // Darkens the playfield, used while paused.
  bool dimmed = false;
  // The system asks for less motion: the table does not jolt on a nudge.
  bool reduceMotion = false;
  // The flippers drawn down whatever the physics holds: a paused game has
  // let go of every button, and a bat held up when it stopped drops as soon
  // as it goes on, so the picture under the pause shows it down.
  bool flippersAtRest = false;
  // For the review tool: balls drawn as well as the game's own, so a table
  // with several can be looked at before the game can be played into one.
  std::span<const BallView> extraBalls{};
  // The light show to draw (core/LightShow.hpp), fed every step by the
  // caller. Null: the lamps alone, lit or not, as the rules say.
  const LightShow* lightShow = nullptr;
};

// Which textures to load. Desktop: what the Classic composition draws with,
// the title (ui/title.png) and the dialog card (ui/dialog.png) included.
// All: the art of the touch layouts as well (ui/strip.png, ui/deck.png,
// ui/backdrop.png, sprites/button.png, sprites/button_gloss.png). Each of
// those is used if the build's asset manifest lists it; where it does not,
// plain windows, buttons and lettering are drawn in its place.
enum class TextureSet : std::uint8_t { Desktop, All };

// Draws the game onto the logical canvas. Owns every texture.
//
// The playfield itself is art (table/background.png) when the build has it.
// When the asset manifest does not list that file, the table is drawn from
// src/core/TableLayout.hpp instead, in plain colours: every wall, lane,
// element outline and lamp where the physics has it, once, into a texture
// that each frame then copies. That is what a build
// shows while the art for a new layout is still to be drawn, so that the
// picture always matches the table being played. Used by the
// game window and by the offscreen snapshot tool, so both show the same thing.
class TableRenderer {
 public:
  // Loads the textures. Returns nullptr, after logging, if one the manifest
  // lists cannot be loaded.
  [[nodiscard]] static std::unique_ptr<TableRenderer> create(SDL_Renderer* renderer,
                                                             const AssetLoader& assets,
                                                             TextureSet set);

  // Fills what the composition leaves of the canvas: the gutters, the bands
  // outside the safe area, the margin round Classic on a touch screen. Call
  // after clearing and before draw. The desktop's composition is the whole
  // canvas, so there it draws nothing. Without backdrop art it is the flat
  // screen::kBackdropRgb under a lattice of dim dots.
  void drawBackdrop(SDL_Renderer* renderer, const screen::Layout& layout) const;

  // Draws one frame: the table at layout.playfield and the status display at
  // layout.hud. Does not clear or present. alpha is the fraction of a step
  // since the latest one (FixedTimestep::alpha()): the ball and the flippers
  // are drawn that far between the previous step and the latest, so motion
  // is smooth when the display does not refresh in step with the
  // simulation. 1 draws the latest step exactly. Everything else shows the
  // latest step.
  void draw(SDL_Renderer* renderer, const Game& game, float alpha, const screen::Layout& layout,
            const DrawOptions& options, const HudExtras& extras = {}) const;

  // Draws the touch controls over the frame, held ones pressed in: the
  // Stack's deck with its button row and flipper pads, the Classic panel's
  // row, and the pause button. Call after draw and before any dialog. Draws
  // nothing for the desktop idiom.
  void drawTouchOverlay(SDL_Renderer* renderer, const screen::Layout& layout,
                        const TouchControls::View& view) const;

  // The art the dialogs (pause, About, initials) draw with. Valid for as
  // long as this renderer.
  [[nodiscard]] OverlayArt overlayArt() const noexcept;

 private:
  struct Textures {
    TexturePtr background;  // The art, or the table drawn from its layout.
    // The background is the layout drawing, so the table's moving parts are
    // drawn the same plain way over it.
    bool backgroundFromLayout = false;
    TexturePtr ball;
    TexturePtr flipper;
    TexturePtr bumper;
    TexturePtr bumperCap;
    TexturePtr glow;
    TexturePtr flare;  // The light show's flasher bursts.
    TexturePtr ring;   // And its shock rings.
    TexturePtr bar;
    TexturePtr door;
    TexturePtr plunger;
    TexturePtr panel;
    // Over the table art only: the layout drawing has its own.
    TexturePtr insertArrow;
    TexturePtr insertRound;
    std::array<TexturePtr, 2> spinner;  // Edge-on, flat.
    TexturePtr kickback;
    // The rest may be null: see TextureSet.
    TexturePtr title;
    TexturePtr dialog;
    TexturePtr strip;
    TexturePtr deck;
    TexturePtr backdrop;
    TexturePtr button;
    TexturePtr buttonGloss;
  };

  explicit TableRenderer(Textures textures);

  // A soft additive light of the given colour and strength (0 to 1, held
  // to art::kMaxGlowStrength so that no light outshines the ball).
  void drawGlow(SDL_Renderer* renderer, Vec2 center, float radius, art::Rgb color,
                float strength) const;
  // The lit lamps over the art's unlit inserts, each with its glow. With a
  // light show, each at its own level, in neon.
  void drawLamps(SDL_Renderer* renderer, const TableLights& lights, const LightShow* show) const;
  // The spinner's blade by its phase, and the kickback's spring.
  void drawSpinnerAndKickback(SDL_Renderer* renderer, const Game& game) const;
  void drawBumpers(SDL_Renderer* renderer, const Game& game, const LightShow* show) const;
  void drawTargets(SDL_Renderer* renderer, const Game& game, const TableLights& lights) const;
  // What moves or lights among the table's own elements, for the layout
  // drawing: doors lying down, the lock and kickback lamps, the spinner.
  void drawElements(SDL_Renderer* renderer, const Game& game, const TableLights& lights) const;
  void drawSlingshotFlashes(SDL_Renderer* renderer, const Game& game, const LightShow* show) const;
  // With a light show: the bumpers lit, flashing white-hot and pulsing
  // when struck; the slingshots' kicking faces flashing.
  void drawLitBumpers(SDL_Renderer* renderer, const Game& game, const LightShow& show) const;
  void drawLitSlingshots(SDL_Renderer* renderer, const Game& game, const LightShow& show) const;
  void drawPlunger(SDL_Renderer* renderer, const Game& game) const;
  void drawFlippers(SDL_Renderer* renderer, const Game& game, float alpha, bool atRest) const;
  // Every ball that is in sight, those on the table first and those riding
  // a ramp's rail over them.
  void drawBalls(SDL_Renderer* renderer, std::span<const BallView> balls) const;

  Textures textures_;
  // The light show's drawing, with its fixed batches.
  mutable TableEffects effects_;
  // Triangles for drawElements, kept between frames so that drawing one
  // allocates nothing.
  mutable std::vector<SDL_Vertex> elements_;
};

}  // namespace pinball
