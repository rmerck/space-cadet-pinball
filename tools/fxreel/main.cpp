// Development tool: plays a scripted scene of the light show headless and
// writes the playfield as a numbered run of PNGs, for review GIFs and
// before/after comparisons. No window is opened. Not part of the game.
//
// Usage: pinball_fxreel <scene> <out_prefix> [options]
//
// scene    play       the scripted flipping of pinball_snapshot, from the plunge
//          hits       a ball sent into the bumper nest, then a slingshot and targets
//          multiball  the Hangar staged with two locked, the third ball shot in
//          jackpot    multiball running, a lit jackpot shot made, then the super
//          drain      a ball let go down the middle between still flippers
//          attract    the attract sequence over the scripted game
//          tilt       the table shoved until it tilts
// options  --plain           draw without the light show (the look before it)
//          --reduce-motion   the light show with reduce motion on
//          --every N         a frame every N steps (default 4: 30 a second)
//          --frames N        how many frames (default 90)
//          --skip N          steps before the first frame (default 0)
//          --time            print the mean and worst time to draw a frame
//          --gpu             draw with the platform's own renderer (Metal,
//                            Direct3D, OpenGL) in a hidden window instead of
//                            the software one, and write no frames: each
//                            frame is timed to a one-pixel read back, so the
//                            time includes the GPU's work
//
// Writes <out_prefix>_000.png and on. Each frame is the playfield alone at
// one pixel to the unit (768 x 960).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>

#include "GameTestAccess.hpp"
#include "core/Game.hpp"
#include "core/LightShow.hpp"
#include "core/Scenes.hpp"
#include "core/ScreenLayout.hpp"
#include "platform/Assets.hpp"
#include "platform/TableRenderer.hpp"

namespace {

using pinball::Game;
using pinball::InputState;
using pinball::Vec2;
using Seam = pinball::GameTestAccess;
namespace table = pinball::table;

struct SurfaceDeleter {
  void operator()(SDL_Surface* s) const noexcept { SDL_DestroySurface(s); }
};
struct RendererDeleter {
  void operator()(SDL_Renderer* r) const noexcept { SDL_DestroyRenderer(r); }
};
struct WindowDeleter {
  void operator()(SDL_Window* w) const noexcept { SDL_DestroyWindow(w); }
};

InputState flipping(std::uint32_t step) noexcept {
  InputState input;
  input.plunger = (step % 1200U) >= 20U && (step % 1200U) < 140U;
  input.leftFlipper = (step / 29U) % 2U == 0U;
  input.rightFlipper = (step / 41U) % 2U == 0U;
  return input;
}

struct Options {
  std::string scene;
  std::string prefix;
  bool plain = false;
  bool reduceMotion = false;
  bool time = false;
  bool gpu = false;
  std::uint32_t every = 4;
  std::uint32_t frames = 90;
  std::uint32_t skip = 0;
};

// The scene: what happens at each step. Returns the input for the step and
// may set the table up by hand before it.
class Script {
 public:
  explicit Script(const Options& options) : options_(options) {}

  bool setUp(Game& game) {
    const std::string& s = options_.scene;
    if (s == "multiball") {
      return pinball::stageScene(game, pinball::Scene::Hangar);
    }
    if (s == "jackpot") {
      return pinball::stageScene(game, pinball::Scene::Multiball);
    }
    if (s == "drain") {
      // No ball saver: the ball is lost and the next one served.
      Seam::rules(game).setBallSaveEnabled(false);
    }
    if (s == "hits" || s == "drain" || s == "tilt") {
      // A ball in play first.
      for (std::uint32_t i = 0; i < 400 && !game.rules().ballInPlay(); ++i) {
        game.step(flipping(i));
      }
      return game.rules().ballInPlay();
    }
    return s == "play" || s == "attract";
  }

  InputState before(Game& game, std::uint32_t t) {
    const std::string& s = options_.scene;
    const std::size_t slot = game.ball().id;
    if (s == "play" || s == "attract") {
      return flipping(t);
    }
    if (s == "hits") {
      // Down onto a bumper, onto the middle one, across into a turbo
      // target, then onto the left slingshot's face.
      if (t == 10) {
        Seam::placeBall(game, slot, {262.0F, 165.0F}, {20.0F, 700.0F});
      } else if (t == 110) {
        Seam::placeBall(game, slot, {352.0F, 185.0F}, {0.0F, 700.0F});
      } else if (t == 200) {
        Seam::placeBall(game, slot, {235.0F, 226.0F}, {-800.0F, 0.0F});
      } else if (t == 270) {
        const auto& sling = table::kSlingshots[0].points;
        const Vec2 face = (sling[0] + sling[2]) * 0.5F;
        Seam::placeBall(game, slot, face + Vec2{45.0F, -25.0F}, {-600.0F, 330.0F});
      }
      return InputState{};
    }
    if (s == "multiball" && t == 20) {
      Seam::placeBall(game, slot, {279.0F, 440.0F}, {0.0F, -900.0F});
      Seam::putInPlay(game, slot);
    }
    if (s == "jackpot" && (t == 20 || t == 200)) {
      // The shots the rules light for the jackpot, made through their own
      // events, as Scenes does.
      pinball::Rules& rules = Seam::rules(game);
      for (const pinball::Shot shot : {pinball::Shot::Runway, pinball::Shot::Skyway,
                                       pinball::Shot::Briefing, pinball::Shot::Hangar}) {
        if (!rules.jackpotLit(shot) && !(shot == pinball::Shot::Hangar && rules.superJackpotLit())) {
          continue;
        }
        switch (shot) {
          case pinball::Shot::Runway:
            rules.onHole(table::kRunwayHole, slot);
            break;
          case pinball::Shot::Skyway:
            rules.onHole(table::kSkywayHole, slot);
            break;
          case pinball::Shot::Briefing:
            rules.onHole(table::kScoopHole, slot);
            rules.onEject(table::kScoopHole);
            break;
          default:
            rules.onHole(table::kHangarHole, slot);
            rules.onEject(table::kHangarHole);
            break;
        }
        break;
      }
    }
    if (s == "drain" && t == 10) {
      Seam::placeBall(game, slot, {table::kWidth * 0.5F - 8.0F, 780.0F}, {0.0F, 400.0F});
    }
    if (s == "tilt") {
      InputState input;
      input.nudgeLeft = t % 24 == 0 && t < 200;
      return input;
    }
    return InputState{};
  }

 private:
  const Options& options_;
};

bool parse(int argc, char* argv[], Options& o) {
  if (argc < 3) {
    return false;
  }
  o.scene = argv[1];
  o.prefix = argv[2];
  for (int i = 3; i < argc; ++i) {
    const std::string_view a = argv[i];
    const auto number = [&](std::uint32_t& out) {
      if (i + 1 >= argc) {
        return false;
      }
      out = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
      return true;
    };
    if (a == "--plain") {
      o.plain = true;
    } else if (a == "--reduce-motion") {
      o.reduceMotion = true;
    } else if (a == "--gpu") {
      o.gpu = true;
      o.time = true;
    } else if (a == "--time") {
      o.time = true;
    } else if (a == "--every") {
      if (!number(o.every)) return false;
    } else if (a == "--frames") {
      if (!number(o.frames)) return false;
    } else if (a == "--skip") {
      if (!number(o.skip)) return false;
    } else {
      return false;
    }
  }
  o.every = std::max<std::uint32_t>(o.every, 1);
  return true;
}

int run(const Options& o) {
  namespace screen = pinball::screen;
  const screen::Layout layout =
      screen::computeLayout({1280.0F, 960.0F, 0.0F, 0.0F, 0.0F, 0.0F}, screen::Idiom::Desktop);
  pinball::AssetLoader assets = pinball::AssetLoader::fromExecutableLocation();
  if (!assets.verifyAll()) {
    std::fputs("asset integrity check failed\n", stderr);
    return EXIT_FAILURE;
  }
  const std::unique_ptr<SDL_Surface, SurfaceDeleter> canvas(SDL_CreateSurface(
      static_cast<int>(layout.canvasWidth), static_cast<int>(layout.canvasHeight),
      SDL_PIXELFORMAT_RGBA32));
  const std::unique_ptr<SDL_Surface, SurfaceDeleter> frame(SDL_CreateSurface(
      static_cast<int>(table::kWidth), static_cast<int>(table::kHeight), SDL_PIXELFORMAT_RGBA32));
  if (!canvas || !frame) {
    return EXIT_FAILURE;
  }
  int status = EXIT_SUCCESS;
  {
    std::unique_ptr<SDL_Window, WindowDeleter> window;
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer;
    if (o.gpu) {
      if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return EXIT_FAILURE;
      }
      window.reset(SDL_CreateWindow("fxreel", static_cast<int>(layout.canvasWidth),
                                    static_cast<int>(layout.canvasHeight), SDL_WINDOW_HIDDEN));
      if (window) {
        renderer.reset(SDL_CreateRenderer(window.get(), nullptr));
      }
      if (renderer) {
        SDL_SetRenderLogicalPresentation(renderer.get(), static_cast<int>(layout.canvasWidth),
                                         static_cast<int>(layout.canvasHeight),
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        std::printf("renderer: %s\n", SDL_GetRendererName(renderer.get()));
      }
    } else {
      renderer.reset(SDL_CreateSoftwareRenderer(canvas.get()));
    }
    if (!renderer) {
      std::fprintf(stderr, "no renderer: %s\n", SDL_GetError());
      return EXIT_FAILURE;
    }
    SDL_Renderer* const r = renderer.get();
    const auto drawer =
        pinball::TableRenderer::create(r, assets, pinball::TextureSet::Desktop);
    if (!drawer) {
      return EXIT_FAILURE;
    }
    Game game;
    pinball::LightShow show;
    show.setReduceMotion(o.reduceMotion);
    Script script(o);
    if (!script.setUp(game)) {
      std::fputs("the scene could not be set up\n", stderr);
      return EXIT_FAILURE;
    }
    show.observe(game);
    show.setAttract(o.scene == "attract");
    double total = 0.0;
    double worst = 0.0;
    double cpu = 0.0;  // Processor time: steadier than the clock on a busy machine.
    std::uint32_t t = 0;
    const std::uint32_t steps = o.skip + o.frames * o.every;
    for (std::uint32_t frameIndex = 0; t < steps; ++t) {
      game.step(script.before(game, t));
      show.observe(game);
      if (t < o.skip || (t - o.skip) % o.every != 0) {
        continue;
      }
      const auto start = std::chrono::steady_clock::now();
      const std::clock_t cpuStart = std::clock();
      SDL_SetRenderDrawColor(r, 18, 18, 58, 255);
      SDL_RenderClear(r);
      pinball::DrawOptions options;
      options.lightShow = o.plain ? nullptr : &show;
      drawer->draw(r, game, 1.0F, layout, options);
      SDL_FlushRenderer(r);
      if (o.gpu) {
        // Wait for the GPU: read one pixel back.
        const SDL_Rect one{0, 0, 1, 1};
        SDL_DestroySurface(SDL_RenderReadPixels(r, &one));
      }
      const double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      total += ms;
      cpu += 1000.0 * static_cast<double>(std::clock() - cpuStart) / CLOCKS_PER_SEC;
      worst = std::max(worst, ms);
      if (o.gpu) {
        SDL_RenderPresent(r);
        continue;
      }
      const SDL_Rect field{static_cast<int>(std::lround(layout.playfield.x)),
                           static_cast<int>(std::lround(layout.playfield.y)),
                           static_cast<int>(table::kWidth), static_cast<int>(table::kHeight)};
      SDL_BlitSurface(canvas.get(), &field, frame.get(), nullptr);
      char name[32];
      std::snprintf(name, sizeof name, "_%03u.png", frameIndex++);
      if (!SDL_SavePNG(frame.get(), (o.prefix + name).c_str())) {
        std::fprintf(stderr, "cannot write a frame: %s\n", SDL_GetError());
        status = EXIT_FAILURE;
        break;
      }
    }
    if (o.time) {
      std::printf("%s%s: %u frames, draw mean %.2f ms, worst %.2f ms, processor %.2f ms a frame (%s renderer)\n",
                  o.scene.c_str(), o.plain ? " (plain)" : "", o.frames,
                  total / std::max<double>(o.frames, 1.0), worst,
                  cpu / std::max<double>(o.frames, 1.0), SDL_GetRendererName(r));
    }
    std::printf("%s: score %llu, balls %zu, show bursts %u, large flashes %u (held %u)\n",
                o.scene.c_str(), static_cast<unsigned long long>(game.rules().score()),
                game.ballCount(), show.burstsStarted(), show.largeFlashes(),
                show.largeFlashesHeld());
  }
  return status;
}

}  // namespace

int main(int argc, char* argv[]) {
  Options options;
  if (!parse(argc, argv, options)) {
    std::fputs("usage: pinball_fxreel <play|hits|multiball|jackpot|drain|attract|tilt> <out_prefix>"
               " [--plain] [--reduce-motion] [--every N] [--frames N] [--skip N] [--time] [--gpu]\n",
               stderr);
    return EXIT_FAILURE;
  }
  const int status = run(options);
  SDL_Quit();
  return status;
}
