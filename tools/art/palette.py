# The single source of colours and art constants for Multiball Academy.
# gen_header.py turns the parts the renderer needs into generated/ArtPalette.hpp.

# ---- master palette -----------------------------------------------------------
INK = "#0A0920"        # outlines, bezels, contact shadows; never pure black
NIGHT = "#12123A"      # backdrop base and letterbox colour
DECK_A = "#141A44"     # deck plate, dark square
DECK_B = "#1A2152"     # deck plate, light square
DECK_HI = "#2C3A7C"    # the most a plate may be lifted by the spot light
WELL = "#080A22"       # display glass
WELL_HI = "#1B2466"    # second dither colour in display wells
VIOLET = "#6A35C9"
ORCHID = "#B98CFF"
TEAL = "#00A7A5"
AQUA = "#63E6D8"
PINK = "#FF2E88"
ORANGE = "#FF6B1A"
YELLOW = "#FFD23C"
GREEN = "#2ED16B"
BLUE = "#2F8CFF"
CREAM = "#FFF4D6"
WHITE = "#FFFFFF"
CH_HI = "#F4FAFF"      # chrome highlight
CH_MID = "#9FB4CC"     # chrome body
CH_LO = "#2B3550"      # chrome shade
GOLD_HI = "#FFFBE0"
GOLD = "#FFD23C"
GOLD_LO = "#A85A10"

# ---- shot and lamp colours (one shot, one colour, everywhere) ------------------
SHOT = {"circuit": AQUA, "runway": ORCHID, "hangar": ORANGE, "briefing": YELLOW, "turbine": GREEN, "skyway": PINK}
# LampId order from TableLayout.hpp. Value: colour the renderer tints the insert and its glow with.
LAMPS = [
    ("ArrowCircuitLeft", AQUA), ("ArrowRunway", ORCHID), ("ArrowHangar", ORANGE), ("ArrowBriefing", YELLOW),
    ("ArrowTurbine", GREEN), ("ArrowSkyway", PINK), ("ArrowCircuitRight", AQUA), ("Lock1", ORANGE), ("Lock2", ORANGE),
    ("Course1", ORCHID), ("Course2", ORCHID), ("Course3", ORCHID), ("Course4", ORCHID), ("Course5", ORCHID),
    ("Course6", ORCHID), ("Graduation", YELLOW), ("ExtraCredit", AQUA), ("ExtraBall", PINK), ("Overdrive", YELLOW),
    ("TurboCharge", GREEN), ("Kickback", YELLOW), ("ShootAgain", CREAM), ("RollCall1", CREAM), ("RollCall2", CREAM),
    ("RollCall3", CREAM), ("WingW", AQUA), ("WingI", AQUA), ("WingN", AQUA), ("WingG", AQUA),
]
# Where each shot arrow points (playfield units): the renderer rotates insert_arrow.png from the lamp to this.
LAMP_AIM = {"ArrowCircuitLeft": (118.0, 440.0), "ArrowRunway": (192.0, 425.0), "ArrowHangar": (279.0, 392.0),
            "ArrowBriefing": (356.0, 428.0), "ArrowTurbine": (432.0, 415.0), "ArrowSkyway": (519.0, 425.0),
            "ArrowCircuitRight": (593.0, 440.0)}
INSERT_ARROW_UNITS = 36.0     # drawn size of insert_arrow.png (the arrow itself is 28 long with its bezel)
INSERT_ARROW_BRIEFING = 1.2   # the Briefing arrow is the largest on the table
INSERT_ROUND_PER_DIAMETER = 24.0 / 22.0   # insert_round.png is drawn this many times the lamp's diameter
# Buttons take ink captions, so every face is light enough for 3:1 against INK, pressed (level 0.8) too:
# ORCHID, not VIOLET, for the nudge and menu keys; a light grey-violet for a key that is off or leads nowhere.
TINT = {"flipper": PINK, "bumper": ORANGE, "bumper_flash": YELLOW, "turbo_target": GREEN, "door": CH_MID,
        "door_marked": ORANGE, "button_centre": TEAL, "button_nudge": ORCHID, "button_pause": PINK,
        "button_launch": YELLOW, "button_off": "#8C84AA", "pad": AQUA}

# ---- visibility rule (see STATUS.md, review/visibility.png) ------------------
# Relative luminance (linear, 0..1) ceilings and floors the art and the renderer keep to.
LUM_DECK_MAX = 0.060      # any deck or floor-art pixel under a ball path, before lamps
LUM_STATIC_MAX = 0.45     # baked highlights and flares on static features
LUM_UNLIT_LENS = 0.10     # unlit insert lens, mean (at least 2:1 against the deck)
LUM_LIT_LENS_MAX = 0.75   # lit insert plus its glow, mean over the lens: always under the ball
LUM_BALL_MIN = 0.70       # ball disc, mean
UNLIT_LEVEL = 0.40        # tint multiplier for an unlit insert
GLOW_STRENGTH_MAX = 0.45  # strongest additive glow.png alpha the renderer uses on the playfield
GLOW_STRENGTH_LAMP = 0.30  # glow.png alpha for a lit lamp
GLOW_RADIUS_ARROW = 0.62   # glow radius as a share of the arrow sprite's drawn size
GLOW_RADIUS_ROUND = 1.10   # glow radius as a multiple of a round lamp's diameter
BALL_SPRITE_UNITS = 32.0  # ball.png covers this many units; the chrome disc is 24 of them
BALL_SHADOW_OFFSET = (4.0, 6.0)
BALL_SHADOW_ALPHA = 0.80


LUM_LIT_LENS = 0.42   # a lit lens is tinted no lighter than this, whatever its hue

# ---- light show (owner update: a lit, living machine; src/core/LightShow.*, src/platform/TableEffects.*) ----
# A lit lamp, a flash or a flasher is drawn in the neon of its colour: the same hue as the paint, brighter and
# more saturated, so lit against unlit is obvious. Neon lenses may be lighter than LUM_LIT_LENS but never reach
# the ball's LUM_BALL_MIN.
NEON = {AQUA: "#1EEBFF", ORCHID: "#C45CFF", ORANGE: "#FF8A00", YELLOW: "#FFEA28", GREEN: "#9DFF1E", PINK: "#FF2EC8",
        CREAM: "#FFF2B0"}
LUM_NEON_LENS = 0.60
# The effect colours the renderer uses for bursts, sweeps and the general illumination.
FX = {"cyan": "#1EEBFF", "magenta": "#FF2EC8", "lime": "#9DFF1E", "orange": "#FF8A00", "yellow": "#FFEA28",
      "violet": "#C45CFF", "white": "#FFF8EC", "gi": "#FFB45C", "gi_bulb": "#FFD9A0", "trail": "#9FF4FF"}
GLOW_STRENGTH_NEON = 0.50      # glow.png alpha for a lit lamp at full level
FLASHER_STRENGTH_MAX = 0.70    # strongest flasher burst (flare.png / glow.png); the ball is drawn after it, with its rim
GI_STRENGTH = 0.40             # the warm edge light at full level
FLARE_UNITS = 64.0             # flare.png and ring.png cover this many units at scale 1


def neon_level(h):
    """Colour multiplier for a lit neon lens: 1.0 unless the neon is lighter than LUM_NEON_LENS."""
    return round(min(1.0, (LUM_NEON_LENS / _lum(h)) ** (1 / 2.2)), 2)


def _lum(h):
    c = [int(h.lstrip("#")[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    c = [v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4 for v in c]
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def lit_level(h):
    """Colour multiplier for a lit insert of this colour: 1.0 unless the hue is lighter than LUM_LIT_LENS."""
    return round(min(1.0, (LUM_LIT_LENS / _lum(h)) ** (1 / 2.2)), 2)


def rgb(h):
    h = h.lstrip("#")
    return int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def mix_teal(row):
    """Teal of the mirrored floor squares on a chrome ball, dimmer toward the bottom of the ball."""
    r, g, b = rgb("#27D6CC")
    k = max(0.35, 1.0 - 0.12 * row)
    return "#%02x%02x%02x" % (int(r * k), int(g * k), int(b * k))
