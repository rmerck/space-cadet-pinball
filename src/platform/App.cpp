#include "platform/App.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

#include "core/LoopPolicy.hpp"
#include "core/OverlayLayout.hpp"
#if !defined(NDEBUG) || defined(PINBALL_IOS_TEST_EXIT)
#include "core/Scenes.hpp"
#endif
#include "core/Version.hpp"
#include "platform/About.hpp"
#include "platform/Assets.hpp"
#include "platform/InitialsScreen.hpp"
#include "platform/MacMenu.hpp"
#include "platform/PauseMenu.hpp"
#include "platform/ios/IosSystem.hpp"

namespace pinball {

namespace {

// Frame period used when presenting does not block (no vsync, hidden window).
constexpr std::uint64_t kUnsyncedFrameNs = 1'000'000'000ULL / 120ULL;
// Hard floor on frame time that applies even when vsync is reported. 1000 FPS
// is above any display refresh rate, so it never costs a vblank, and it still
// stops a driver that ignores vsync from spinning a core flat out.
constexpr std::uint64_t kMinFrameNs = 1'000'000'000ULL / 1000ULL;
// Half period of the blinking cursor in the initials prompt.
constexpr Uint64 kCursorBlinkMs = 400;
// The window a touch layout opens in where the system does not give it the
// whole screen (a desktop, for trying the touch controls): a phone's, in
// points.
constexpr int kTouchWindowWidth = 393;
constexpr int kTouchWindowHeight = 852;
// The least time between two attempts to open an audio device, and how many
// frames are shown before the first one when it is made in the background.
constexpr Uint64 kAudioRetryMs = 5000;
constexpr std::uint32_t kFramesBeforeAudio = 3;

// A window of the given size, in points, of which the system says the safe
// rectangle is clear of notches, bars and corners.
constexpr screen::Surface surfaceFor(int width, int height, const SDL_Rect& safe) noexcept {
  const auto inset = [](std::int64_t amount) noexcept {
    return static_cast<float>(std::max<std::int64_t>(amount, 0));
  };
  // 64-bit sums: a rectangle from the system is believed only so far.
  const std::int64_t right = std::int64_t{width} - (std::int64_t{safe.x} + safe.w);
  const std::int64_t bottom = std::int64_t{height} - (std::int64_t{safe.y} + safe.h);
  return {static_cast<float>(width), static_cast<float>(height), inset(safe.y),
          inset(bottom),             inset(safe.x),              inset(right)};
}
// A phone in landscape, as measured in the simulator: 62 points each side
// and 20 at the bottom. A safe rectangle that is the whole window has no
// insets, and one that sticks out of the window has none either.
static_assert(surfaceFor(874, 402, {62, 0, 750, 382}).safeLeft == 62.0F);
static_assert(surfaceFor(874, 402, {62, 0, 750, 382}).safeRight == 62.0F);
static_assert(surfaceFor(874, 402, {62, 0, 750, 382}).safeTop == 0.0F);
static_assert(surfaceFor(874, 402, {62, 0, 750, 382}).safeBottom == 20.0F);
static_assert(surfaceFor(1032, 1376, {0, 32, 1032, 1324}).safeTop == 32.0F);
static_assert(surfaceFor(1032, 1376, {0, 32, 1032, 1324}).safeBottom == 20.0F);
static_assert(surfaceFor(400, 800, {0, 0, 400, 800}).safeBottom == 0.0F);
static_assert(surfaceFor(400, 800, {-10, -10, 2147483647, 2147483647}).safeRight == 0.0F);
static_assert(surfaceFor(400, 800, {-10, -10, 2147483647, 2147483647}).safeLeft == 0.0F);

// The touch layout for the window as it is now. From the window, never the
// screen: on a tablet the window can be any part of it.
screen::Layout touchLayout(SDL_Window* window) {
  int width = 0;
  int height = 0;
  SDL_GetWindowSize(window, &width, &height);
  SDL_Rect safe{0, 0, width, height};
  if (!SDL_GetWindowSafeArea(window, &safe)) {
    safe = {0, 0, width, height};
  }
  return screen::computeLayout(surfaceFor(width, height, safe), screen::Idiom::Touch);
}

// Makes the renderer's logical canvas the layout's. The desktop's fixed
// canvas keeps its shape inside any window, with bars. A touch canvas is
// the window itself, in other units: its size is rounded to whole units
// here, so it is stretched the fraction of a unit that differs and no bar
// can appear at an edge.
bool applyPresentation(SDL_Renderer* renderer, const screen::Layout& layout) {
  const bool touch = layout.idiom == screen::Idiom::Touch;
  return SDL_SetRenderLogicalPresentation(
      renderer, static_cast<int>(std::lround(layout.canvasWidth)),
      static_cast<int>(std::lround(layout.canvasHeight)),
      touch ? SDL_LOGICAL_PRESENTATION_STRETCH : SDL_LOGICAL_PRESENTATION_LETTERBOX);
}

// Settings SDL reads when the subsystems start and the window is made.
void applyHints(bool touch) {
  if (touch) {
    // A finger is a finger and not also a mouse; a mouse or trackpad click
    // is a touch, so the on-screen controls work with a pointer too.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");
  }
#if defined(PINBALL_IOS)
  // Sound effects of a game: silent when the ring switch is, and mixed with
  // whatever the player is listening to, which is neither stopped nor
  // turned down. SDL's own choice without this is Playback with ducking,
  // whatever its documentation says.
  SDL_SetHint(SDL_HINT_AUDIO_CATEGORY, "ambient");
  // The flipper thumbs rest at the bottom edge: the home indicator is
  // dimmed and the first swipe from an edge only brings it back.
  SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
  // As Info.plist declares: a phone is held upright, a tablet any way up.
  SDL_SetHint(SDL_HINT_ORIENTATIONS,
              SDL_IsTablet() ? "Portrait PortraitUpsideDown LandscapeLeft LandscapeRight"
                             : "Portrait");
#endif
}

// Pairs SDL_InitSubSystem with SDL_QuitSubSystem on every exit path of create().
// The subsystem calls are reference counted, unlike SDL_Init/SDL_Quit.
class VideoSession {
 public:
  VideoSession() : ok_(SDL_InitSubSystem(SDL_INIT_VIDEO)) {}
  ~VideoSession() {
    if (ok_ && !released_) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
  }
  VideoSession(const VideoSession&) = delete;
  VideoSession& operator=(const VideoSession&) = delete;
  bool ok() const noexcept { return ok_; }
  // Hands responsibility for SDL_QuitSubSystem to the App.
  void release() noexcept { released_ = true; }

 private:
  bool ok_;
  bool released_ = false;
};

}  // namespace

std::unique_ptr<App> App::create(const AppOptions& options) {
  SDL_SetAppMetadata(kGameTitle, kGameVersion, kGameIdentifier);
  SDL_SetAppMetadataProperty(SDL_PROP_APP_METADATA_URL_STRING, "https://github.com/rmerck");

  // Refuse to start unless every asset matches the build. Done first, before
  // any window exists.
  AssetLoader assets = AssetLoader::fromExecutableLocation();
  if (!assets.verifyAll()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Asset integrity check failed; reinstall or rebuild the game");
    return nullptr;
  }

  const bool touch = options.idiom == screen::Idiom::Touch;
  applyHints(touch);

  VideoSession session;
  if (!session.ok()) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL video init failed: %s", SDL_GetError());
    return nullptr;
  }

  // The desktop's canvas is fixed, and the window opens at its size. A
  // touch layout follows the window, whatever size the system makes it.
  screen::Layout layout = screen::desktopLayout();
  const int windowWidth =
      touch ? kTouchWindowWidth : static_cast<int>(std::lround(layout.canvasWidth));
  const int windowHeight =
      touch ? kTouchWindowHeight : static_cast<int>(std::lround(layout.canvasHeight));
  SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(PINBALL_IOS)
  // Fullscreen is what hides the status bar and defers the edge gestures.
  windowFlags |= SDL_WINDOW_FULLSCREEN;
#endif

  std::unique_ptr<SDL_Window, WindowDeleter> window(
      SDL_CreateWindow(kGameTitle, windowWidth, windowHeight, windowFlags));
  if (!window) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
    return nullptr;
  }
  if (touch) {
    layout = touchLayout(window.get());
  }

  std::unique_ptr<SDL_Renderer, RendererDeleter> renderer(
      SDL_CreateRenderer(window.get(), nullptr));
  if (!renderer) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer failed: %s", SDL_GetError());
    return nullptr;
  }

  // VSync and letterboxing are best-effort: the game still runs without them.
  const bool vsync = SDL_SetRenderVSync(renderer.get(), 1);
  if (!vsync) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "VSync refused, pacing manually: %s",
                SDL_GetError());
  }
  if (!applyPresentation(renderer.get(), layout)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Logical presentation failed: %s", SDL_GetError());
  }
  SDL_SetWindowMinimumSize(window.get(), windowWidth / 4, windowHeight / 4);

  // (The Game inside App can throw if the physics world cannot be created;
  // main reports that.)
  std::unique_ptr<TableRenderer> table = TableRenderer::create(
      renderer.get(), assets, touch ? TextureSet::All : TextureSet::Desktop);
  if (!table) {
    return nullptr;
  }

  std::unique_ptr<App> app(
      new App(options, layout, window.release(), renderer.release(), vsync, std::move(table)));
  session.release();
  return app;
}

App::App(const AppOptions& options, const screen::Layout& layout, SDL_Window* window,
         SDL_Renderer* renderer, bool vsync, std::unique_ptr<TableRenderer> table)
    : options_(options),
      layout_(layout),
      window_(window),
      renderer_(renderer),
      table_(std::move(table)),
      previousNs_(SDL_GetTicksNS()),
      vsync_(vsync),
      audio_(options.backgroundAudioOpen ? nullptr : AudioOutput::open()),
      // A timed automated run leaves the player's score file alone: it is
      // neither read (which can also set a bad file aside) nor written.
      highScores_(options.frameLimit || options.scene || !options.keepScores ? HighScores{}
                                                                             : scoreStore_.load()),
      aboutEvent_(SDL_RegisterEvents(1)) {
  const bool routed = routeAboutMenu(aboutEvent_);
  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "About menu item routed to the game: %s",
               routed ? "yes" : "no");

  // A staged scene, for screenshots. Only builds that read --scene can stage
  // one (src/main.cpp, src/main_ios.cpp): elsewhere the staging is not even
  // linked, and a scene given here only keeps the score file out of the run.
#if !defined(NDEBUG) || defined(PINBALL_IOS_TEST_EXIT)
  if (options.scene) {
    const std::string_view name = sceneName(*options.scene);
    if (stageScene(game_, *options.scene)) {
      SDL_Log("Scene %.*s staged", static_cast<int>(name.size()), name.data());
    } else {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Scene %.*s could not be staged",
                  static_cast<int>(name.size()), name.data());
    }
  }
#endif

  systemCorners_ = options.systemCorners.value_or(SDL_IsTablet());
  reduceMotion_ = ios::reduceMotionEnabled();
  // The player's own music is playing: the game's tune stays out of its
  // way until the player turns it on.
  if (ios::otherAudioPlaying()) {
    musicOn_ = false;
  }
  audioRouteChangesSeen_ = ios::audioRouteChanges();
  if (options_.backgroundAudioOpen) {
    // Not started here: the first open waits until the game is on screen
    // (serviceAudio), so nothing about audio is on the way to the first
    // frame.
    audioOpener_.emplace();
    audioRetryWanted_ = true;
  }
}

App::~App() {
  table_.reset();
  renderer_.reset();
  window_.reset();
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

bool App::needsManualPacing() const {
  if (!vsync_ || !presented_) {
    return true;
  }
  const SDL_WindowFlags hidden =
      SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED;
  return (SDL_GetWindowFlags(window_.get()) & hidden) != 0;
}

namespace {
// How long a halted loop sleeps between redraws.
constexpr Sint32 kHaltedWaitMs = 100;
}  // namespace

int App::run() {
  previousNs_ = SDL_GetTicksNS();

  FrameResult result = FrameResult::Continue;
  while (result == FrameResult::Continue) {
    // While nothing is moving or sounding, sleep until something happens
    // instead of redrawing a still picture at full rate. A sound still
    // ringing keeps the loop at frame rate so its tail plays without gaps.
    // It sleeps anyway after a while, in case the audio device has stopped
    // taking sound and a tail can never finish. With no audio device nothing
    // drains the mixer, so its voices never end and do not count.
    haltedFrames_ = halted() ? haltedFrames_ + 1 : 0;
    SDL_Event event;
    if (sleepsWhileHalted(halted(), options_.frameLimit.has_value(),
                          audio_ != nullptr && mixer_.busy(), haltedFrames_) &&
        SDL_WaitEventTimeout(&event, kHaltedWaitMs)) {
      handleEvent(event);
    }

    result = iterate(false);

    // Presenting normally blocks until vblank. When it will not (no vsync,
    // hidden window) pace at the simulation rate. Otherwise still enforce a
    // floor, because a driver can accept vsync and then ignore it.
    const std::uint64_t minFrameNs = needsManualPacing() ? kUnsyncedFrameNs : kMinFrameNs;
    const std::uint64_t spent = SDL_GetTicksNS() - frameStartNs_;
    if (spent < minFrameNs) {
      SDL_DelayPrecise(minFrameNs - spent);
    }
  }
  return finish();
}

FrameResult App::frame() { return iterate(true); }

FrameResult App::iterate(bool restWhenStill) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    handleEvent(event);
  }

  frameStartNs_ = SDL_GetTicksNS();
  const double frameSeconds =
      options_.fixedFrameSeconds
          ? std::clamp(*options_.fixedFrameSeconds, 0.0, 1.0)
          : static_cast<double>(frameStartNs_ - previousNs_) / 1e9;
  previousNs_ = frameStartNs_;

  serviceAudio();
  readInput();
  repeatHeldArrow();

  // While halted the clock keeps moving but nothing accumulates, and the
  // first frame after a halt adds no time either (see PauseClock).
  const bool isHalted = halted();
  const double simSeconds = clock_.simulated(frameSeconds, isHalted);
  if (!isHalted) {
    const int steps = timestep_.advance(simSeconds);
    // Sounds from every step of the frame start together, once each.
    SoundSet sounds = 0;
    for (int i = 0; i < steps; ++i) {
      game_.step(input_);
      lightShow_.observe(game_);
      sounds |= game_.sounds();
      // Checked per step, so a game that ends and restarts within one
      // frame is still recorded.
      recordFinishedGame();
      if (halted()) {
        // The initials prompt opened. Nothing more runs behind it, so a
        // key held this frame cannot start a new game under the prompt.
        break;
      }
    }
    mixer_.trigger(sounds);
    if (steps > 0) {
      // The simulation has seen every latched press at least once.
      latched_ = InputState{};
    }
  }

  // The initials prompt may have opened under the player's fingers.
  syncScreen();

  // The tune plays while the game runs and rests while it stands still.
  mixer_.setMusicPlaying(musicOn_ && !isHalted);
  if (audio_) {
    audio_->pump(mixer_);
  }
  updateIdleTimer();

  // A standing picture changes only when an event has touched something or
  // the prompt's cursor blinks. Where the loop cannot sleep it is not drawn
  // again until then. Nothing at all is drawn in the background, where the
  // system forbids it.
  const bool cursorLit = (SDL_GetTicks() / kCursorBlinkMs) % 2U == 0U;
  const bool blinked = entry_ && !aboutOpen_ && cursorLit != cursorWasLit_;
  const bool still = isHalted && halted() && redrawsDue_ == 0 && !blinked;
  if (!backgrounded_ && (!restWhenStill || options_.frameLimit || !still)) {
    // The accumulator is frozen while halted, so alpha is stable and nothing snaps.
    render(timestep_.alpha(), cursorLit);
    // Only a frame that reached the screen counts.
    if (presented_ && redrawsDue_ > 0) {
      --redrawsDue_;
    }
    cursorWasLit_ = cursorLit;
    ++drawnFrames_;
  }

  ++frames_;
  if (options_.frameLimit && frames_ >= *options_.frameLimit) {
    running_ = false;
  }
  return running_ ? FrameResult::Continue : FrameResult::Quit;
}

int App::finish() {
  // Quitting at the initials prompt keeps the score, under the letters shown.
  commitInitials();

  if (deviceLost_) {
    return 1;
  }

  // Self-check for automated runs: a long enough run must have simulated.
  if (options_.frameLimit && *options_.frameLimit >= 30 && game_.stepCount() == 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "No simulation steps ran in %u frames",
                 static_cast<unsigned>(frames_));
    return 1;
  }
  return 0;
}

void App::handleEvent(const SDL_Event& event) {
  // Whatever it is, the picture may not be the same after it.
  redrawsDue_ = kRedrawsAfterEvent;
  switch (event.type) {
    case SDL_EVENT_QUIT:
      running_ = false;
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_RESTORED:
      handleFocusChange(event.type);
      break;
    case SDL_EVENT_RENDER_DEVICE_RESET:
    case SDL_EVENT_RENDER_TARGETS_RESET:
      // Every texture died with the old device; or only what was drawn
      // into the render targets did, and the playfield may be one (it is
      // drawn into a texture when there is no art for it).
      reloadTextures();
      break;
    case SDL_EVENT_RENDER_DEVICE_LOST:
      // Nothing can be drawn again. Leave through the normal exit, so a score
      // waiting at the initials prompt is still kept.
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The graphics device was lost; exiting");
      deviceLost_ = true;
      running_ = false;
      break;
    case SDL_EVENT_KEY_DOWN:
      handleKeyDown(event.key);
      break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
      handleTouch(event);
      break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
      handleLayoutChange();
      break;
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
    case SDL_EVENT_DID_ENTER_FOREGROUND:
    case SDL_EVENT_TERMINATING:
    case SDL_EVENT_LOW_MEMORY:
      handleLifecycle(event.type);
      break;
    default:
      // The About item in the system menu bar. It only ever opens the screen.
      // Not over the initials prompt, which must be answered first, as with F1.
      if (aboutEvent_ != 0 && event.type == aboutEvent_ && !aboutOpen_ && !entry_) {
        aboutOpen_ = true;
        aboutPage_ = AboutState{};
        latched_ = InputState{};
      }
      break;
  }
  syncScreen();
}

void App::handleLayoutChange() {
  if (layout_.idiom != screen::Idiom::Touch) {
    // The desktop's canvas is fixed; SDL fits it to the window.
    return;
  }
  const screen::Layout next = touchLayout(window_.get());
  // The controls are somewhere else now. A finger that was holding one
  // must not find itself on another.
  touch_.cancelAll();
  overlayTaps_.cancel();
  latched_ = InputState{};
  const bool recomposed = next.composition != layout_.composition;
  layout_ = next;
  if (!applyPresentation(renderer_.get(), layout_)) {
    noteRenderFailure("SDL_SetRenderLogicalPresentation");
  }
  aboutPage_.licencePage = std::min(aboutPage_.licencePage, licencePageCount(layout_) - 1);
  // The table has moved from under the player's hands (a turned tablet):
  // the ball does not run on while they find the controls again.
  if (recomposed && !halted() && !game_.gameOver()) {
    interrupt();
  }
}

void App::serviceAudio() {
  if (!audioOpener_) {
    return;
  }
  const Uint64 now = SDL_GetTicks();
  if (audioOpening_ && !audioOpener_->busy()) {
    // The wait before another attempt runs from the end of this one: an
    // open that took half a minute to fail is not followed at once by the
    // next.
    audioOpening_ = false;
    audioAttemptMs_ = now;
  }
  if (std::unique_ptr<AudioOutput> opened = audioOpener_->take()) {
    audio_ = std::move(opened);
    // It may have finished opening after the application left the front.
    if (audioSuspended_) {
      audio_->suspend();
    }
  }
  // A device that came or went (headphones, a speaker) is a reason to try
  // again where there was no device before.
  const unsigned routeChanges = ios::audioRouteChanges();
  if (routeChanges != audioRouteChangesSeen_) {
    audioRouteChangesSeen_ = routeChanges;
    audioRetryWanted_ = true;
  }
  if (audio_) {
    audioRetryWanted_ = false;
    return;
  }
  // The first open once a few frames have been shown; later ones no more
  // often than every few seconds.
  const bool due = audioAttempted_ ? now - audioAttemptMs_ >= kAudioRetryMs
                                   : drawnFrames_ >= kFramesBeforeAudio;
  if (audioRetryWanted_ && !backgrounded_ && !audioOpener_->busy() && due) {
    audioRetryWanted_ = false;
    audioAttempted_ = true;
    audioOpening_ = true;
    audioAttemptMs_ = now;
    audioOpener_->start();
  }
}

void App::updateIdleTimer() {
  if (layout_.idiom != screen::Idiom::Touch) {
    return;
  }
  // A ball cradled on a held flipper sends no events, and the screen must
  // not dim over it. A paused or finished game lets the device sleep.
  const bool awake = !halted() && !game_.gameOver();
  if (keepingAwake_ != awake) {
    keepingAwake_ = awake;
    if (!(awake ? SDL_DisableScreenSaver() : SDL_EnableScreenSaver())) {
      SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Idle timer not changed: %s", SDL_GetError());
    }
  }
}

App::Status App::status() const {
  Status status{};
  status.paused = paused_;
  status.autoPaused = autoPaused_;
  status.interrupted = interrupted_;
  status.aboutOpen = aboutOpen_;
  status.entryOpen = entry_.has_value();
  status.backgrounded = backgrounded_;
  status.muted = mixer_.muted();
  status.musicOn = musicOn_;
  status.confirmNewGame = confirmNewGame_;
  status.audioOpen = audio_ != nullptr;
  status.audioSuspended = audio_ != nullptr && audio_->suspended();
  status.gameOver = game_.gameOver();
  status.about = aboutPage_;
  status.initials = entry_ ? entry_->letters() : Initials{'A', 'A', 'A'};
  status.cursor = entry_ ? entry_->cursor() : 0;
  status.frames = frames_;
  status.drawnFrames = drawnFrames_;
  status.steps = game_.stepCount();
  status.input = input_;
  status.layout = layout_;
  status.touch = touch_.view(touchContext());
  return status;
}

void App::commitInitials() {
  if (!entry_) {
    return;
  }
  pending_.initials = entry_->letters();
  lastInitials_ = pending_.initials;
  entry_.reset();
  latched_ = InputState{};
  newHighScore_ = highScores_.submit(pending_) < HighScores::kCapacity;
  if (newHighScore_ && options_.keepScores && !options_.scene) {
    scoreStore_.save(highScores_);
  }
}

void App::recordFinishedGame() {
  const bool over = game_.gameOver();
  if (over && !wasGameOver_) {
    // The game has just ended: enter it in the table if it made it.
    const HighScore entry{game_.rules().score(),
                          static_cast<std::uint8_t>(game_.rules().rank())};
    if (!highScores_.qualifies(entry.score)) {
      newHighScore_ = false;
    } else if (options_.frameLimit) {
      // A timed automated run has no one to type: enter it without initials,
      // for this run only. Nothing is saved.
      newHighScore_ = highScores_.submit(entry) < HighScores::kCapacity;
    } else {
      // Ask for initials. The entry is made when they are confirmed.
      pending_ = entry;
      entry_.emplace(lastInitials_);
      entryOpenedMs_ = SDL_GetTicks();
      latched_ = InputState{};
    }
  } else if (!over) {
    newHighScore_ = false;
  }
  wasGameOver_ = over;
}

void App::toggleFullscreen() {
#if defined(PINBALL_IOS)
  // Always the whole screen, or the window the system gives.
  return;
#else
  // Ask the window, not a cached flag: the user can also change fullscreen
  // through the window manager.
  const bool isFullscreen = (SDL_GetWindowFlags(window_.get()) & SDL_WINDOW_FULLSCREEN) != 0;
  if (!SDL_SetWindowFullscreen(window_.get(), !isFullscreen)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Fullscreen toggle failed: %s", SDL_GetError());
  }
#endif
}

void App::noteRenderFailure(const char* call) {
  // Once: a renderer that fails tends to fail every frame. The game keeps
  // running, since a device reset or a restored window can bring it back.
  if (!renderFailureLogged_) {
    renderFailureLogged_ = true;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s failed: %s", call, SDL_GetError());
  }
}

void App::reloadTextures() {
  // Reading, checking and decoding every asset takes many frames' worth of
  // time, inside event handling. Count it as a halt, so the frame that
  // follows adds no simulation time instead of a burst of catch-up steps.
  // Whether or not the reload works: the time is spent either way.
  (void)clock_.simulated(0.0, true);
  // Read and verified again from disk: the bytes checked at start were freed.
  AssetLoader assets = AssetLoader::fromExecutableLocation();
  std::unique_ptr<TableRenderer> table;
  if (assets.verifyAll()) {
    table = TableRenderer::create(
        renderer_.get(), assets,
        layout_.idiom == screen::Idiom::Touch ? TextureSet::All : TextureSet::Desktop);
  }
  if (!table) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                 "Could not reload the textures after a graphics device reset");
    return;
  }
  table_ = std::move(table);
}

void App::render(double alpha, bool cursorLit) {
  SDL_Renderer* r = renderer_.get();
  const bool touch = layout_.idiom == screen::Idiom::Touch;

  // The backdrop's own colour everywhere: the desktop's letterbox bars and
  // what shows at the table's edge when a nudge jolts it, and on a touch
  // screen anything the canvas leaves uncovered at an edge.
  SDL_SetRenderDrawColor(r, screen::kBackdropRgb[0], screen::kBackdropRgb[1],
                         screen::kBackdropRgb[2], SDL_ALPHA_OPAQUE);
  if (!SDL_RenderClear(r)) {
    noteRenderFailure("SDL_RenderClear");
  }

  const bool showPaused = paused_ || autoPaused_;
  // The ball and flippers are drawn part-way between the last two steps.
  table_->drawBackdrop(r, layout_);
  lightShow_.setReduceMotion(reduceMotion_);
  table_->draw(r, game_, static_cast<float>(alpha), layout_,
               DrawOptions{.dimmed = showPaused,
                           .reduceMotion = reduceMotion_,
                           .flippersAtRest = showPaused,
                           .lightShow = &lightShow_},
               HudExtras{highScores_.best(), newHighScore_});
  if (touch) {
    table_->drawTouchOverlay(r, layout_, touch_.view(touchContext()));
  }
  const OverlayArt art = table_->overlayArt();
  if (showPaused && !aboutOpen_ && !entry_) {
    drawPauseMenu(r, layout_,
                  MenuState{.muted = mixer_.muted(),
                            .musicOn = musicOn_,
                            .confirmNewGame = confirmNewGame_,
                            .afterInterruption = touch ? interrupted_ : !paused_},
                  art);
  }
  if (aboutOpen_) {
    drawAbout(r, highScores_, layout_, aboutPage_, art);
  } else if (entry_) {
    drawInitialsEntry(r, *entry_, pending_.score, cursorLit, layout_, art);
  }

  presented_ = SDL_RenderPresent(r);
  if (!presented_) {
    noteRenderFailure("SDL_RenderPresent");
  }
}

}  // namespace pinball
