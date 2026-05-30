// pocketOS.c
// List-based launcher for Miyoo Mini (Onion OS)
// Replaces MainUI with a two-panel Pocket OS style interface.
//
// Screens:
//   HOME    - vertical list: Games / Recents / Settings / Sleep
//   SYSTEMS - two-panel: system list (left) | game list (right), navigate systems
//   GAMES   - same two-panel, focus moves to game list
//
// On game select: writes /tmp/cmd_to_run.sh and exits — Onion runtime.sh
// picks it up and launches the emulator exactly like the normal flow.

#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <dirent.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <zlib.h>

#ifdef POCKETOS_ENABLE_AUDIO
typedef struct Mix_Chunk Mix_Chunk;
typedef struct _Mix_Music Mix_Music;
extern int Mix_OpenAudio(int frequency, Uint16 format, int channels, int chunksize);
extern void Mix_CloseAudio(void);
extern Mix_Chunk *Mix_LoadWAV_RW(SDL_RWops *src, int freesrc);
extern void Mix_FreeChunk(Mix_Chunk *chunk);
extern int Mix_PlayChannelTimed(int channel, Mix_Chunk *chunk, int loops, int ticks);
extern int Mix_HaltMusic(void);
extern int Mix_HaltChannel(int channel);
extern int Mix_VolumeChunk(Mix_Chunk *chunk, int volume);
extern int Mix_VolumeMusic(int volume);
extern Mix_Music *Mix_LoadMUS(const char *file);
extern void Mix_FreeMusic(Mix_Music *music);
extern int Mix_PlayMusic(Mix_Music *music, int loops);
extern void Mix_ChannelFinished(void (*channel_finished)(int channel));
#define MIX_MAX_VOLUME 128
#endif

// ── Screen & layout ──────────────────────────────────────────────────────────

#define SCREEN_W   640
#define SCREEN_H   480
#define BPP        32

#define STATUS_H    44   // 22 logical × 2
#define HINT_H      44   // 22 logical × 2
#define CONTENT_H   (SCREEN_H - STATUS_H - HINT_H)
#define CONTENT_Y   STATUS_H

#define LEFT_W      256   // 128 logical × 2  (systems panel)
#define PANEL_HDR_H  24   // 12 logical × 2
#define ITEM_H       56   // 28 logical × 2  (systems list)
#define PANEL_ROWS  ((CONTENT_H - PANEL_HDR_H) / ITEM_H)

#define GAME_ITEM_H  90   // comfortable 2-line game titles at 26pt
#define GAME_ROWS   ((CONTENT_H - PANEL_HDR_H) / GAME_ITEM_H)
#define GAME_LINE_GAP 30  // px between title line 1 and line 2 (26pt font)

#define HOME_VISIBLE 4                          // rows shown at once
#define HOME_ITEM_H  (CONTENT_H / HOME_VISIBLE) // fills content area exactly (98px)
#define HOME_ITEM_X  40
#define HOME_ITEM_W  (SCREEN_W - 80)
#define HEADER_H     36   /* settings section header row height */

// ── Device paths ─────────────────────────────────────────────────────────────

#ifndef POCKETOS_ROOT
#define POCKETOS_ROOT "/mnt/SDCARD"
#endif

#ifndef EMU_ROOT
#define EMU_ROOT    POCKETOS_ROOT "/Emu"
#endif
#ifndef ROMS_ROOT
#define ROMS_ROOT   POCKETOS_ROOT "/Roms"
#endif
#ifndef CMD_PATH
#define CMD_PATH    "/tmp/cmd_to_run.sh"
#endif
#ifndef SYSDIR
#define SYSDIR      POCKETOS_ROOT "/.tmp_update"
#endif
#ifndef ASSET_ROOT
#define ASSET_ROOT  SYSDIR "/res/pocketos"
#endif
#ifndef LOG_PATH
#define LOG_PATH    SYSDIR "/logs/pocketos_debug.log"
#endif
#ifndef FONT_PATH
#define FONT_PATH   POCKETOS_ROOT "/miyoo/app/Exo-2-Bold-Italic_Universal.ttf"
#endif
#ifndef FONT_ALT
#define FONT_ALT    POCKETOS_ROOT "/miyoo/app/wqy-microhei.ttc"
#endif
#ifndef FONT_PRIMARY
#define FONT_PRIMARY POCKETOS_ROOT "/miyoo/app/BPreplayBold.otf"
#endif

// ── Button mappings (from Onion keymap_sw.h) ─────────────────────────────────

#define BTN_UP     SDLK_UP
#define BTN_DOWN   SDLK_DOWN
#define BTN_LEFT   SDLK_LEFT
#define BTN_RIGHT  SDLK_RIGHT
#define BTN_A      SDLK_SPACE
#define BTN_B      SDLK_LCTRL
#define BTN_X      SDLK_LSHIFT
#define BTN_Y      SDLK_LALT
#define BTN_L1     SDLK_e
#define BTN_R1     SDLK_t
#define BTN_L2     SDLK_TAB
#define BTN_R2     SDLK_BACKSPACE
#define BTN_SELECT SDLK_RCTRL
#define BTN_START  SDLK_RETURN
#define BTN_MENU   SDLK_ESCAPE

// ── Max items ────────────────────────────────────────────────────────────────

#define MAX_SYSTEMS  64
#define MAX_GAMES   1500

// ── Colors (static helpers use COL macro after screen is init'd) ──────────────

#define RGBA(r,g,b) SDL_MapRGB(screen->format,(r),(g),(b))

// Forward declarations for functions defined later in the file
static void scan_fonts(void);
static void apply_font_index(int idx);
static void save_theme_font(int idx);
static void draw_font_picker(void);
static void on_font_picker_key(SDLKey k);
static void scan_themes(void);
static void load_browse_data(void);
static void draw_info_panel(void);
static void on_info_panel_key(SDLKey k);
static void apply_theme_index(int idx);
static void draw_theme_picker(void);
static void on_theme_picker_key(SDLKey k);
static void load_theme(char *font_out, int font_outlen);

// Named color constants (resolved at runtime)
static Uint32 C_BG, C_BAR, C_SEP, C_SEL, C_PANEL_HDR;
static Uint32 C_DIVIDER, C_CARD, C_CARD_BORDER;
static Uint32 C_SEL_HI, C_SEL_BORDER, C_PANEL_HI;

// Retro cream/navy palette — see pocket_os_design_guide.md
static SDL_Color SC_TEXT  = { 13,  28,  51, 255};  // dark navy #0D1C33
static SDL_Color SC_WHITE = {244, 247, 251, 255};  // soft white #F4F7FB (bar text)
static SDL_Color SC_DIM   = { 95, 102, 128, 255};  // muted #5F6680
__attribute__((unused)) static SDL_Color SC_ARROW = {141, 126, 214, 255};  // lavender
static SDL_Color SC_HDR   = { 13,  28,  51, 255};  // same as text for headers

// ── State machine ────────────────────────────────────────────────────────────

typedef enum {
    STATE_HOME,
    STATE_SYSTEMS,
    STATE_GAMES,
    STATE_RECENT,
    STATE_FAVORITES,
    STATE_APPS,
    STATE_SETTINGS,
    STATE_FONT_PICKER,
    STATE_THEME_PICKER,
    STATE_BROWSE_CATS,
    STATE_BROWSE_GAMES,
    STATE_INFO_PANEL,
    STATE_GAME_OPTIONS
} State;

static State state = STATE_HOME;
static int   info_panel_about = 0;  /* 0 = device/miyoo info, 1 = pocket OS about */
static int   home_sel    = 0;
static int   home_offset = 0;

// ── Data structures ───────────────────────────────────────────────────────────

typedef struct {
    char label[48];
    char emu_dir[256];   // /mnt/SDCARD/Emu/GBA
    char rom_dir[256];   // /mnt/SDCARD/Roms/GBA
    char extlist[128];   // "gba|bin|zip|7z"
} System;

typedef struct {
    char name[240];
    char path[512];
} Game;

typedef struct {
    char label[240];
    char rompath[512];
    char launch[512];
    char system[48];
} PlayEntry;

typedef struct {
    const char *label;
    const char *icon;
    const char *cmd;
} AppEntry;

// ── Browse-by-genre data ──────────────────────────────────────────────────────

#define BROWSE_GENRE_MAX   72
#define BROWSE_GENRE_LEN   48
#define BROWSE_GAME_MAX  2048

typedef struct {
    char  title[240];
    char  path[512];
    char  system[24];
    char  genre[BROWSE_GENRE_LEN];
} BrowseGame;

typedef struct {
    char label[BROWSE_GENRE_LEN];
    int  start;   // first index into browse_game_pool (sorted by genre)
    int  count;
} BrowseGenre;

static BrowseGame  browse_game_pool[BROWSE_GAME_MAX];
static int         browse_game_count   = 0;
static BrowseGenre browse_genres[BROWSE_GENRE_MAX];
static int         browse_genre_count  = 0;
static int         browse_genre_sel    = 0;
static int         browse_genre_off    = 0;
static int         browse_game_sel     = 0;
static int         browse_game_off     = 0;

static System systems[MAX_SYSTEMS];
static int    sys_count  = 0;
static int    sys_sel    = 0;
static int    sys_offset = 0;

static Game games[MAX_GAMES];
static int  game_count  = 0;
static int  game_sel    = 0;
static int  game_offset = 0;
static int  game_opts_sel  = 0;   /* selected row in Game Options panel */
static int  game_opts_mode = 0;   /* 0=menu, 1=rom_info, 2=save_info */
static State game_opts_back = STATE_GAMES; /* which state to return to */
static char game_opts_name[240];
static char game_opts_path[512];
static char game_opts_launch[512];
static char game_opts_system[48];

static PlayEntry recent_entries[MAX_GAMES];
static int recent_count = 0;
static int recent_sel = 0;
static int recent_offset = 0;

static PlayEntry favorite_entries[MAX_GAMES];
static int favorite_count = 0;
static int favorite_sel = 0;
static int favorite_offset = 0;

static int app_sel    = 0;
static int app_offset = 0;

/* Font picker */
#define FONT_LIST_MAX 64
static char font_list_path[FONT_LIST_MAX][512];
static char font_list_name[FONT_LIST_MAX][64];
static int  font_list_count = 0;
static int  font_pick_sel    = 0;
static int  font_pick_offset = 0;
static int  font_pick_prev   = 0;  /* index of font active when picker opened */

/* Theme picker */
#define THEME_LIST_MAX 64
static char theme_list_path[THEME_LIST_MAX][512];
static char theme_list_name[THEME_LIST_MAX][64];
static int  theme_list_count = 0;
static int  theme_pick_sel   = 0;
static int  theme_pick_offset = 0;

typedef struct {
    const char *label;
    const char *icon;
    const char *kind;
    const char *cmd;
    int is_header;   // 1 = non-selectable section label row
} SettingsEntry;

#define HDR(label) { label, NULL, NULL, NULL, 1 }

static SettingsEntry SETTINGS_ENTRIES[] = {
    HDR("DISPLAY"),
    { "Brightness",       "icon_brightness.png",   "brightness",   NULL, 0 },
    { "Luminance",        "icon_luminance.png",    "lumination",   NULL, 0 },
    { "Saturation",       "icon_saturation.png",   "saturation",   NULL, 0 },
    { "Hue",              "icon_hue.png",          "hue",          NULL, 0 },
    { "Contrast",         "icon_contrast.png",     "contrast",     NULL, 0 },
    { "Blue Light",       "icon_bluelight.png",    "bluelightlvl", NULL, 0 },
    { "PWM Frequency",    "icon_pwmfreq.png",      "pwmfreq",      NULL, 0 },
    { "Font",             "icon_font.png",         "font",         NULL, 0 },
    { "Theme",            "icon_theme.png",        "theme",        NULL, 0 },
    HDR("AUDIO"),
    { "Volume",           "icon_volume.png",       "audio",        NULL, 0 },
    { "Mute",             "icon_mute.png",         "mute",         NULL, 0 },
    { "Audio Fix",        "icon_audiofix.png",     "audiofix",     NULL, 0 },
    { "Vibration",        "icon_vibration.png",    "vibration",    NULL, 0 },
    HDR("SYSTEM"),
    { "UTC Offset",       "icon_clock.png",        "utcoffset",    NULL, 0 },
    { "Sleep Timer",      "icon_sleeptimer.png",   "sleeptimer",   NULL, 0 },
    { "Auto-Resume",      "icon_autoresume.png",   "autoresume",   NULL, 0 },
    { "Disable Standby",  "icon_standby.png",      "standby",      NULL, 0 },
    { "Low Batt Warn",    "icon_battwarn.png",     "battwarn",     NULL, 0 },
    { "Low Batt Save",    "icon_battsave.png",     "battsave",     NULL, 0 },
    { "Wi-Fi",            "icon_wifi.png",         "network",      NULL, 0 },
    HDR("CONTROLS"),
    { "Controls",         "icon_controls.png",     "controls",     NULL, 0 },
    HDR("INFO"),
    { "Device",           "icon_device.png",       "system",       NULL, 0 },
    { "About",            "icon_about.png",        "about",        NULL, 0 },
    HDR("POWER"),
    { "Power Off",        "icon_power.png",        "power",        "shutdown", 0 }
};
#define SETTINGS_COUNT ((int)(sizeof(SETTINGS_ENTRIES) / sizeof(SETTINGS_ENTRIES[0])))
static int settings_sel       = 0;
static int settings_scroll_px = 0;  /* pixel offset for variable-height scroll */

// ── SDL globals ───────────────────────────────────────────────────────────────

static SDL_Surface *video  = NULL;
static SDL_Surface *screen = NULL;
static TTF_Font    *font_body  = NULL;   // 21pt — panels, bars
static TTF_Font    *font_game  = NULL;   // 20pt — two-line game titles
static TTF_Font    *font_large = NULL;   // 26pt — home rows, settings rows
static TTF_Font    *font_small = NULL;   // 14pt — labels, hints, values
static int          running = 1;

#ifdef POCKETOS_ENABLE_AUDIO
static int audio_ready = 0;
static Mix_Chunk *sfx_move = NULL;
static Mix_Chunk *sfx_select = NULL;
static Mix_Chunk *sfx_back = NULL;
static Mix_Chunk *sfx_launch = NULL;
static Mix_Chunk *sfx_start = NULL;
static Mix_Music *bg_music = NULL;
static volatile int music_pending = 0;
#endif

typedef struct {
    char name[64];
    SDL_Surface *surface;
} AssetCache;

static AssetCache asset_cache[128];
static int asset_cache_count = 0;

static int screenshot_combo_held = 0;
static int screenshot_toast_frames = 0;  /* > 0 = show "Saved" toast */

// ── Home menu ────────────────────────────────────────────────────────────────

static const char *HOME_LABELS[] = {
    "Favorites", "Recent", "Library", "Browse", "Apps", "Settings", "Sleep"
};
static const char *HOME_ICONS[] = {
    "favorites.png", "recent.png", "library.png", "browse.png",
    "apps.png",      "settings.png", "sleep.png"
};
#define HOME_COUNT 7

static AppEntry APP_ENTRIES[] = {
    { "Advanced Menu",   "tools.png",        "cd /mnt/SDCARD/App/AdvanceMENU; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Battery",         "app_battery.png",  "cd /mnt/SDCARD/App/BatteryMonitorUI; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Boot Logo",       "icon_theme.png",   "cd /mnt/SDCARD/App/EasyLogoTweak; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Calibration",     "app_display.png",  "cd /mnt/SDCARD/App/240pSuite; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Clock",           "app_clock.png",    "cd /mnt/SDCARD/App/Clock; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Expert Mode",     "app_expert.png",   "cd /mnt/SDCARD/App/Expert_Mode; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Filter",          "app_search.png",   "cd /mnt/SDCARD/App/Filter; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Game Switcher",   "app_switcher.png", "cd /mnt/SDCARD/App/StartGameSwitcher; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Green Mode",      "icon_bluelight.png","cd /mnt/SDCARD/App/Green; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Guest Mode",      "app_expert.png",   "cd /mnt/SDCARD/App/Guest_Mode; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Internet Archive","downloads.png",    "/mnt/SDCARD/.tmp_update/bin/romdl" },
    { "Music",           "music.png",        "cd /mnt/SDCARD/App/Gmu; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Packages",        "app_packages.png", "cd /mnt/SDCARD/App/PackageManager; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Play Activity",   "app_activity.png", "cd /mnt/SDCARD/App/PlayActivity; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Quick Guide",     "app_manual.png",   "cd /mnt/SDCARD/App/Onion_Manual; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Random Game",     "app_random.png",   "cd /mnt/SDCARD/App/RandomGamePicker; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Reader",          "reader.png",       "cd /mnt/SDCARD/App/PixelReader; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "RetroArch",       "app_retroarch.png","cd /mnt/SDCARD/App/RetroArch; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "ROM Scripts",     "app_random.png",   "cd /mnt/SDCARD/App/romscripts; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Screenshots",     "screenshots.png",  "cd /mnt/SDCARD/App/Screenshots_Viewer; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Search",          "app_search.png",   "cd /mnt/SDCARD/App/Search; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Settings",        "settings.png",     "internal-settings" },
    { "Terminal",        "app_terminal.png", "cd /mnt/SDCARD/App/Terminal; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Themes",          "themes.png",       "cd /mnt/SDCARD/App/ThemeSwitcher; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Tools",           "tools.png",        "cd /mnt/SDCARD/App/Commander_Italic; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Tweaks",          "app_tweaks.png",   "cd /mnt/SDCARD/App/Tweaks; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Updates",         "app_update.png",   "cd /mnt/SDCARD/App/OtaUpdate; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Video",           "video.png",        "cd /mnt/SDCARD/App/FFplay; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Wi-Fi",           "wifi.png",         "internal-network" },
    { "Power Off",       "icon_power.png",   "shutdown" }
};
#define APP_COUNT ((int)(sizeof(APP_ENTRIES) / sizeof(APP_ENTRIES[0])))

// ── Utility: simple path resolver ────────────────────────────────────────────
// Handles the Emu config "../../Roms/GBA" pattern.
// Strips leading "../" sequences and prepends POCKETOS_ROOT.

static void resolve_sdcard_path(const char *rel, char *out, int outlen) {
    if (rel[0] == '/') {
        strncpy(out, rel, outlen - 1);
        out[outlen - 1] = '\0';
        return;
    }
    const char *p = rel;
    while (strncmp(p, "../", 3) == 0) p += 3;
    snprintf(out, outlen, "%s/%s", POCKETOS_ROOT, p);
}

// ── Utility: check if file matches extlist ────────────────────────────────────

static int ext_match(const char *filename, const char *extlist) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) return 0;
    const char *ext = dot + 1;

    char buf[128];
    strncpy(buf, extlist, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, "|");
    while (tok) {
        if (strcasecmp(ext, tok) == 0) return 1;
        tok = strtok(NULL, "|");
    }
    return 0;
}

// ── Utility: strip file extension for display ─────────────────────────────────

static void strip_ext(const char *filename, char *out, int outlen) {
    strncpy(out, filename, outlen - 1);
    out[outlen - 1] = '\0';
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

// ── Utility: tiny JSON string reader (no external dependency) ─────────────────
// Reads the first value of "key" from a flat JSON file.

static int json_str(const char *filepath, const char *key, char *out, int outlen) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;

    char buf[4096];
    int n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    // Find "key"
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *p = strstr(buf, search);
    if (!p) return 0;
    p += strlen(search);

    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;

    int i = 0;
    while (*p && *p != '"' && i < outlen - 1) {
        if (*p == '\\') { p++; }  // skip escape char, copy next char literally
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static int json_str_from_buf(const char *buf, const char *key, char *out, int outlen) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = strstr(buf, search);
    if (!p) return 0;
    p += strlen(search);

    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p != '"') return 0;
    p++;

    int i = 0;
    while (*p && *p != '"' && i < outlen - 1) {
        if (*p == '\\') p++;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

static void log_kv(const char *key, const char *value);
static void log_int(const char *key, int value);
static void log_errno_msg(const char *context, const char *path);

static int json_int_file(const char *filepath, const char *key, int fallback) {
    FILE *f = fopen(filepath, "r");
    if (!f) return fallback;

    char buf[4096];
    int n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';

    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *p = strstr(buf, search);
    if (!p) return fallback;
    p += strlen(search);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (*p == '"') p++;
    return atoi(p);
}

static int set_json_int_file(const char *filepath, const char *key, int value) {
    FILE *f = fopen(filepath, "r");
    if (!f) {
        log_errno_msg("settings open failed", filepath);
        return 0;
    }

    char buf[8192];
    int n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n <= 0) return 0;
    buf[n] = '\0';

    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *p = strstr(buf, search);
    if (!p) {
        log_kv("settings key missing", key);
        return 0;
    }

    char *v = strchr(p, ':');
    if (!v) return 0;
    v++;
    while (*v == ' ' || *v == '\t') v++;
    char *end = v;
    if (*end == '"') {
        end++;
        while (*end && *end != '"') end++;
        if (*end == '"') end++;
    } else {
        while (*end == '-' || (*end >= '0' && *end <= '9')) end++;
    }

    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s/.tmp_update/logs/pocketos_system.tmp", POCKETOS_ROOT);
    mkdir(POCKETOS_ROOT "/.tmp_update/logs", 0755);
    FILE *out = fopen(tmp, "w");
    if (!out) {
        log_errno_msg("settings tmp open failed", tmp);
        return 0;
    }
    fwrite(buf, 1, (size_t)(v - buf), out);
    fprintf(out, "%d", value);
    fputs(end, out);
    fclose(out);

    if (rename(tmp, filepath) != 0) {
        log_errno_msg("settings rename failed", filepath);
        unlink(tmp);
        return 0;
    }
    log_kv("settings updated", key);
    return 1;
}

static int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

// Writes all four CSC values to mi_disp in one shot.
// Reads current system.json for any field not being changed.
static void apply_display_csc(void) {
    int lum = json_int_file(POCKETOS_ROOT "/system.json", "lumination", 7);
    int hue = json_int_file(POCKETOS_ROOT "/system.json", "hue",        10);
    int sat = json_int_file(POCKETOS_ROOT "/system.json", "saturation", 10);
    int con = json_int_file(POCKETOS_ROOT "/system.json", "contrast",   10);

    int luma_proc = lum * 2 + 17 * 2;   // matches disp_csc_reset.sh: lumination + FACTOR*2
    int sat_proc  = sat * 5;
    int hue_proc  = hue * 5;
    int con_proc  = con + 40;

    FILE *f = fopen("/proc/mi_modules/mi_disp/mi_disp0", "w");
    if (f) {
        fprintf(f, "csc 0 3 %d %d %d %d 0 0\n", con_proc, hue_proc, luma_proc, sat_proc);
        fclose(f);
    } else {
        log_errno_msg("csc apply failed", "mi_disp0");
    }
}

static void apply_brightness(int brightness) {
    brightness = clampi(brightness, 0, 10);
    set_json_int_file(POCKETOS_ROOT "/system.json", "brightness", brightness);
    int raw = (int)round(3.0 * exp(0.350656 * brightness));
    FILE *f = fopen("/sys/devices/soc0/soc/1f003400.pwm/pwm/pwmchip0/pwm0/duty_cycle", "w");
    if (f) {
        fprintf(f, "%d", raw);
        fclose(f);
    } else {
        log_errno_msg("brightness apply failed", "pwm0/duty_cycle");
    }
}

static void apply_volume(int vol, int mute) {
    vol = clampi(vol, 0, 20);
    mute = mute ? 1 : 0;
    set_json_int_file(POCKETOS_ROOT "/system.json", "vol", vol);
    set_json_int_file(POCKETOS_ROOT "/system.json", "mute", mute);
    int raw = vol == 0 ? -60 : (int)round(48.0 * log10(1.0 + vol)) - 60;
    FILE *f = fopen("/proc/mi_modules/mi_ao/mi_ao0", "w");
    if (f) {
        fprintf(f, "set_ao_volume 0 %ddB\n", raw);
        fprintf(f, "set_ao_volume 1 %ddB\n", raw);
        fprintf(f, "set_ao_mute %d\n", mute);
        fclose(f);
    } else {
        log_errno_msg("volume apply failed", "mi_ao0");
    }
}

static void apply_blue_light(int enabled) {
    const char *script = SYSDIR "/script/blue_light.sh";
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "%s %s", script, enabled ? "enable" : "disable");
    int rc = system(cmd);
    if (rc != 0) log_int("blue_light rc", rc);
    // persist state flag
    const char *flag = SYSDIR "/config/.blfOn";
    if (enabled) { FILE *f = fopen(flag, "w"); if (f) fclose(f); }
    else remove(flag);
}

static void apply_blue_light_level(int level) {
    level = clampi(level, 0, 6);
    const char *path = SYSDIR "/config/display/blueLightLevel";
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", level); fclose(f); }
    else log_errno_msg("blue light level write failed", path);
    /* level 0 = off, 1-6 = on at that intensity */
    apply_blue_light(level > 0);
}

static void apply_config_flag(const char *flagname, int enabled) {
    char path[256];
    snprintf(path, sizeof(path), SYSDIR "/config/%s", flagname);
    if (enabled) { FILE *f = fopen(path, "w"); if (f) fclose(f); }
    else remove(path);
}

static int read_config_flag(const char *flagname) {
    char path[256];
    snprintf(path, sizeof(path), SYSDIR "/config/%s", flagname);
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

static int read_config_int(const char *relpath, int def) {
    char path[256];
    snprintf(path, sizeof(path), SYSDIR "/config/%s", relpath);
    FILE *f = fopen(path, "r");
    if (!f) return def;
    int v = def;
    if (fscanf(f, "%d", &v) != 1) v = def;
    fclose(f);
    return v;
}

static void write_config_int(const char *relpath, int val) {
    char path[256];
    snprintf(path, sizeof(path), SYSDIR "/config/%s", relpath);
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", val); fclose(f); }
    else log_errno_msg("config write failed", relpath);
}

static void apply_wifi(int enabled) {
    enabled = enabled ? 1 : 0;
    set_json_int_file(POCKETOS_ROOT "/system.json", "wifi", enabled);
    int rc = 0;
    if (enabled) rc = system("/customer/app/axp_test wifion >/dev/null 2>&1 &");
    else         rc = system("/customer/app/axp_test wifioff >/dev/null 2>&1 &");
    if (rc != 0) log_int("wifi command rc", rc);
    rc = system("touch /tmp/network_changed");
    if (rc != 0) log_int("network_changed touch rc", rc);
}

static void system_from_launch(const char *launch, char *out, int outlen) {
    const char *p = strstr(launch, "/Emu/");
    if (!p) {
        strncpy(out, "Game", outlen - 1);
        out[outlen - 1] = '\0';
        return;
    }
    p += 5;
    int i = 0;
    while (*p && *p != '/' && i < outlen - 1) out[i++] = *p++;
    out[i] = '\0';
}

// ── Battery level ─────────────────────────────────────────────────────────────

static int read_battery(void) {
    const char *tmp_paths[] = {
        "/tmp/percBat",
        "/tmp/.percBat",
        NULL
    };
    for (int i = 0; tmp_paths[i]; i++) {
        FILE *f = fopen(tmp_paths[i], "r");
        if (f) {
            int v = -1;
            if (fscanf(f, "%d", &v) == 1) {
                fclose(f);
                if (v == 500) return 100;
                if (v >= 0 && v <= 100) return v;
            } else {
                fclose(f);
            }
        }
    }

    FILE *axp = fopen("/tmp/.axp_result", "r");
    if (axp) {
        char buf[128];
        int v = -1;
        int n = (int)fread(buf, 1, sizeof(buf) - 1, axp);
        fclose(axp);
        buf[n] = '\0';
        if (sscanf(buf, "{\"battery\":%d", &v) == 1) {
            if (v == 500) return 100;
            if (v >= 0 && v <= 100) return v;
        }
    }

    const char *paths[] = {
        "/sys/class/power_supply/axp20x-battery/capacity",
        "/sys/class/power_supply/battery/capacity",
        NULL
    };
    for (int i = 0; paths[i]; i++) {
        FILE *f = fopen(paths[i], "r");
        if (f) {
            int v = -1;
            int ok = fscanf(f, "%d", &v);
            fclose(f);
            if (ok == 1 && v >= 0 && v <= 100) return v;
        }
    }
    return -1;
}

static void format_storage_value(char *out, int outlen) {
    struct statvfs st;
    if (statvfs(POCKETOS_ROOT, &st) != 0 || st.f_blocks == 0) {
        snprintf(out, outlen, "--");
        return;
    }
    unsigned long long free_mb = ((unsigned long long)st.f_bavail * st.f_frsize) / (1024ULL * 1024ULL);
    unsigned long long total_mb = ((unsigned long long)st.f_blocks * st.f_frsize) / (1024ULL * 1024ULL);
    if (total_mb >= 1024) {
        snprintf(out, outlen, "%lluG free", free_mb / 1024ULL);
    } else {
        snprintf(out, outlen, "%lluM free", free_mb);
    }
}

static void settings_value(const SettingsEntry *entry, char *out, int outlen) {
    const char *k = entry->kind;
    if (strcmp(k, "brightness") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "brightness", -1);
        if (v >= 0) snprintf(out, outlen, "%d / 10", v);
        else snprintf(out, outlen, "--");
    } else if (strcmp(k, "lumination") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "lumination", -1);
        if (v >= 0) snprintf(out, outlen, "%d / 20", v);
        else snprintf(out, outlen, "--");
    } else if (strcmp(k, "saturation") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "saturation", -1);
        if (v >= 0) snprintf(out, outlen, "%d / 20", v);
        else snprintf(out, outlen, "--");
    } else if (strcmp(k, "hue") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "hue", -1);
        if (v >= 0) snprintf(out, outlen, "%d / 20", v);
        else snprintf(out, outlen, "--");
    } else if (strcmp(k, "contrast") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "contrast", -1);
        if (v >= 0) snprintf(out, outlen, "%d / 20", v);
        else snprintf(out, outlen, "--");
    } else if (strcmp(k, "audio") == 0) {
        int vol = json_int_file(POCKETOS_ROOT "/system.json", "vol", -1);
        int mute = json_int_file(POCKETOS_ROOT "/system.json", "mute", 0);
        if (mute) snprintf(out, outlen, "Muted");
        else if (vol >= 0) snprintf(out, outlen, "%d / 20", vol);
        else snprintf(out, outlen, "--");
    } else if (strcmp(k, "mute") == 0) {
        int mute = json_int_file(POCKETOS_ROOT "/system.json", "mute", 0);
        snprintf(out, outlen, "%s", mute ? "ON" : "OFF");
    } else if (strcmp(k, "audiofix") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "audiofix", 1);
        snprintf(out, outlen, "%s", v ? "ON" : "OFF");
    } else if (strcmp(k, "vibration") == 0) {
        int v = read_config_int("vibration", 2);
        snprintf(out, outlen, "%d / 4", v);
    } else if (strcmp(k, "bluelightlvl") == 0) {
        int v = read_config_int("display/blueLightLevel", 0);
        if (v == 0) snprintf(out, outlen, "Off");
        else        snprintf(out, outlen, "%d / 6", v);
    } else if (strcmp(k, "pwmfreq") == 0) {
        int v = read_config_int("pwmfrequency", 7);
        snprintf(out, outlen, "%d / 10", v);
    } else if (strcmp(k, "utcoffset") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "utcoffset", 0);
        if (v == 0)      snprintf(out, outlen, "UTC");
        else if (v > 0)  snprintf(out, outlen, "UTC+%d", v);
        else             snprintf(out, outlen, "UTC%d", v);
    } else if (strcmp(k, "sleeptimer") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "hibernate", 5);
        if (v == 0) snprintf(out, outlen, "Off");
        else        snprintf(out, outlen, "%d min", v);
    } else if (strcmp(k, "autoresume") == 0) {
        int disabled = read_config_flag(".noAutoStart");
        snprintf(out, outlen, "%s", disabled ? "OFF" : "ON");
    } else if (strcmp(k, "standby") == 0) {
        int dis = read_config_flag(".disableStandby");
        snprintf(out, outlen, "%s", dis ? "ON" : "OFF");
    } else if (strcmp(k, "battwarn") == 0) {
        int v = read_config_int("battery/warnAt", 10);
        snprintf(out, outlen, "%d%%", v);
    } else if (strcmp(k, "battsave") == 0) {
        int v = read_config_int("battery/exitAt", 4);
        snprintf(out, outlen, "%d%%", v);
    } else if (strcmp(k, "controls") == 0) {
        snprintf(out, outlen, "Default");
    } else if (strcmp(k, "storage") == 0) {
        format_storage_value(out, outlen);
    } else if (strcmp(k, "network") == 0) {
        int wifi = json_int_file(POCKETOS_ROOT "/system.json", "wifi", 0);
        snprintf(out, outlen, "%s", wifi ? "ON" : "OFF");
    } else if (strcmp(k, "system") == 0) {
        FILE *f = fopen("/tmp/deviceModel", "r");
        int model = 0;
        if (f) { if (fscanf(f, "%d", &model) != 1) model = 0; fclose(f); }
        snprintf(out, outlen, model == 354 ? "Mini Plus" : model == 283 ? "Mini" : "Device");
    } else if (strcmp(k, "font") == 0) {
        if (font_pick_sel >= 0 && font_pick_sel < font_list_count) {
            char label[64];
            strncpy(label, font_list_name[font_pick_sel], sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
            char *dot = strrchr(label, '.');
            if (dot) *dot = '\0';
            snprintf(out, outlen, "%s", label);
        } else {
            snprintf(out, outlen, "Default");
        }
    } else if (strcmp(k, "theme") == 0) {
        if (theme_pick_sel >= 0 && theme_pick_sel < theme_list_count) {
            char label[64];
            strncpy(label, theme_list_name[theme_pick_sel], sizeof(label) - 1);
            label[sizeof(label) - 1] = '\0';
            char *dot = strrchr(label, '.');
            if (dot) *dot = '\0';
            /* strip "theme_" prefix if present */
            char *p = label;
            if (strncmp(p, "theme_", 6) == 0) p += 6;
            /* capitalize first letter */
            if (*p >= 'a' && *p <= 'z') *p -= 32;
            snprintf(out, outlen, "%s", p);
        } else {
            snprintf(out, outlen, "Default");
        }
    } else if (strcmp(k, "about") == 0) {
        snprintf(out, outlen, "Pocket OS");
    } else if (strcmp(k, "power") == 0) {
        snprintf(out, outlen, "Shutdown");
    } else {
        snprintf(out, outlen, "--");
    }
}

// ── Sort helpers ──────────────────────────────────────────────────────────────

static int cmp_sys(const void *a, const void *b) {
    return strcasecmp(((System *)a)->label, ((System *)b)->label);
}

static int cmp_game(const void *a, const void *b) {
    return strcasecmp(((Game *)a)->name, ((Game *)b)->name);
}

static int cmp_play_entry(const void *a, const void *b) {
    return strcasecmp(((PlayEntry *)a)->label, ((PlayEntry *)b)->label);
}

// -- Debug logging ------------------------------------------------------------

// ── Logger ────────────────────────────────────────────────────────────────────
// Single persistent file handle; opened once at startup, flushed after every
// write so the last line in the file is always the last thing that ran.

#define LOG_MAX_BYTES (512 * 1024)   /* rotate when log exceeds 512 KB */

static FILE *g_log_fp = NULL;

static void log_timestamp(FILE *f) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(f, "[%s] ", ts);
}

static void log_msg(const char *msg) {
    if (!g_log_fp) return;
    log_timestamp(g_log_fp);
    fprintf(g_log_fp, "%s\n", msg);
    fflush(g_log_fp);
}

static void log_kv(const char *key, const char *value) {
    if (!g_log_fp) return;
    log_timestamp(g_log_fp);
    fprintf(g_log_fp, "%s: %s\n", key, value ? value : "(null)");
    fflush(g_log_fp);
}

static void log_int(const char *key, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    log_kv(key, buf);
}

static void log_errno_msg(const char *context, const char *path) {
    if (!g_log_fp) return;
    log_timestamp(g_log_fp);
    fprintf(g_log_fp, "ERROR %s: path=%s  errno=%d (%s)\n",
            context, path ? path : "", errno, strerror(errno));
    fflush(g_log_fp);
}

static void log_file_state(const char *label, const char *path) {
    if (!g_log_fp) return;
    struct stat st;
    log_timestamp(g_log_fp);
    if (stat(path, &st) == 0)
        fprintf(g_log_fp, "%s: %s  [exists size=%ld mode=%04o]\n",
                label, path, (long)st.st_size, (unsigned)(st.st_mode & 0777));
    else
        fprintf(g_log_fp, "%s: %s  [MISSING errno=%d %s]\n",
                label, path, errno, strerror(errno));
    fflush(g_log_fp);
}

static void log_sdl_error(const char *context) {
    if (!g_log_fp) return;
    log_timestamp(g_log_fp);
    fprintf(g_log_fp, "SDL ERROR %s: %s\n", context, SDL_GetError());
    fflush(g_log_fp);
}

/* Signal handler — logs the signal then re-raises so the OS still gets it */
static const char *g_log_path_static = LOG_PATH;
static void sig_handler(int sig) {
    /* async-signal-safe: use write() not fprintf */
    FILE *f = fopen(g_log_path_static, "a");
    if (f) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char ts[32];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
        const char *signame =
            sig == SIGSEGV ? "SIGSEGV (segfault)" :
            sig == SIGABRT ? "SIGABRT (abort)"    :
            sig == SIGFPE  ? "SIGFPE (fpe)"       :
            sig == SIGBUS  ? "SIGBUS (bus error)"  :
            sig == SIGILL  ? "SIGILL (illegal op)" : "UNKNOWN";
        fprintf(f, "[%s] *** CRASH signal=%d (%s) ***\n", ts, sig, signame);
        fclose(f);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void log_open(void) {
    /* Rotate if log is too large */
    struct stat st;
    if (stat(LOG_PATH, &st) == 0 && st.st_size > LOG_MAX_BYTES) {
        char old[256];
        snprintf(old, sizeof(old), "%s.old", LOG_PATH);
        rename(LOG_PATH, old);
    }

    /* Ensure log directory exists */
    char dir[256];
    snprintf(dir, sizeof(dir), "%s", LOG_PATH);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir(dir, 0755); }

    g_log_fp = fopen(LOG_PATH, "a");
    if (!g_log_fp) return;

    /* Session header */
    fprintf(g_log_fp, "\n");
    fprintf(g_log_fp, "========================================\n");
    log_timestamp(g_log_fp);
    fprintf(g_log_fp, "PocketOS v1.0  started\n");
    fprintf(g_log_fp, "========================================\n");
    fflush(g_log_fp);

    /* Register crash signal handlers */
    signal(SIGSEGV, sig_handler);
    signal(SIGABRT, sig_handler);
    signal(SIGFPE,  sig_handler);
    signal(SIGBUS,  sig_handler);
    signal(SIGILL,  sig_handler);
}

static void log_close(void) {
    if (!g_log_fp) return;
    log_msg("PocketOS exiting normally");
    fclose(g_log_fp);
    g_log_fp = NULL;
}

/* Elapsed-time timer — uses CLOCK_MONOTONIC for ms precision */
typedef struct { struct timespec t0; const char *label; } LogTimer;

static LogTimer log_timer_begin(const char *label) {
    LogTimer lt;
    lt.label = label;
    clock_gettime(CLOCK_MONOTONIC, &lt.t0);
    log_kv("TIMER begin", label);
    return lt;
}

static void log_timer_end(LogTimer lt) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec  - lt.t0.tv_sec)  * 1000L
            + (t1.tv_nsec - lt.t0.tv_nsec) / 1000000L;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s  %ldms", lt.label, ms);
    log_kv("TIMER end", buf);
}

/* State names for readable state-transition logging */
static const char *state_name(int s) {
    switch (s) {
        case 0:  return "HOME";
        case 1:  return "FAVORITES";
        case 2:  return "RECENT";
        case 3:  return "SYSTEMS";
        case 4:  return "GAMES";
        case 5:  return "APPS";
        case 6:  return "SETTINGS";
        case 7:  return "BROWSE_CATS";
        case 8:  return "BROWSE_GAMES";
        case 9:  return "INFO_PANEL";
        case 10: return "GAME_OPTIONS";
        default: return "UNKNOWN";
    }
}

static int g_prev_state = -1;
static void log_state_if_changed(int cur) {
    if (cur == g_prev_state) return;
    if (!g_log_fp) return;
    log_timestamp(g_log_fp);
    fprintf(g_log_fp, "state: %s → %s\n",
            g_prev_state >= 0 ? state_name(g_prev_state) : "START",
            state_name(cur));
    fflush(g_log_fp);
    g_prev_state = cur;
}

static void fill_rect(int x, int y, int w, int h, Uint32 color);
static void fill_rect_alpha(int x, int y, int w, int h, Uint8 alpha);
static void fill_rrect(int x, int y, int w, int h, int r, Uint32 col);

// -- Audio helpers ------------------------------------------------------------

#ifdef POCKETOS_ENABLE_AUDIO
static Mix_Chunk *load_sfx(const char *name, int volume) {
    char path[512];
    snprintf(path, sizeof(path), "%s/sound/%s", ASSET_ROOT, name);
    SDL_RWops *rw = SDL_RWFromFile(path, "rb");
    if (!rw) {
        log_kv("sfx missing", path);
        return NULL;
    }
    Mix_Chunk *chunk = Mix_LoadWAV_RW(rw, 1);
    if (!chunk) {
        log_kv("sfx load failed", path);
        return NULL;
    }
    Mix_VolumeChunk(chunk, volume);
    return chunk;
}

static void play_sfx(Mix_Chunk *chunk);

static void on_channel_done(int channel) {
    (void)channel;
    music_pending = 1;
}

static void init_audio(void) {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        log_kv("SDL audio init failed", SDL_GetError());
        return;
    }
    if (Mix_OpenAudio(44100, AUDIO_S16SYS, 2, 1024) != 0) {
        log_kv("Mix_OpenAudio failed", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return;
    }

    audio_ready = 1;
    sfx_move = load_sfx("ui_move.wav", 42);
    sfx_select = load_sfx("ui_select.wav", 54);
    sfx_back = load_sfx("ui_back.wav", 46);
    sfx_launch = load_sfx("ui_launch.wav", 60);
    sfx_start = load_sfx("startup_coin.wav", 38);

    char music_path[512];
    snprintf(music_path, sizeof(music_path), "%s/sound/menu-music.mp3", ASSET_ROOT);
    bg_music = Mix_LoadMUS(music_path);
    if (!bg_music) log_kv("bg music load failed", music_path);

    Mix_ChannelFinished(on_channel_done);
    play_sfx(sfx_start);
}

static void play_sfx(Mix_Chunk *chunk) {
    if (audio_ready && chunk) Mix_PlayChannelTimed(-1, chunk, 0, -1);
}

static void play_move(void) { play_sfx(sfx_move); }
static void play_select(void) { play_sfx(sfx_select); }
static void play_back(void) { play_sfx(sfx_back); }
static void play_launch(void) { play_sfx(sfx_launch ? sfx_launch : sfx_select); }

static void stop_audio(void) {
    if (audio_ready) Mix_HaltMusic();
}

static void shutdown_audio(void) {
    if (!audio_ready) return;
    Mix_HaltMusic();
    Mix_HaltChannel(-1);
    if (sfx_move) Mix_FreeChunk(sfx_move);
    if (sfx_select) Mix_FreeChunk(sfx_select);
    if (sfx_back) Mix_FreeChunk(sfx_back);
    if (sfx_launch) Mix_FreeChunk(sfx_launch);
    if (sfx_start) Mix_FreeChunk(sfx_start);
    if (bg_music) { Mix_FreeMusic(bg_music); bg_music = NULL; }
    Mix_CloseAudio();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_ready = 0;
}
#else
static void init_audio(void) {}
static void play_move(void) {}
static void play_select(void) {}
static void play_back(void) {}
static void play_launch(void) {}
static void stop_audio(void) {}
static void shutdown_audio(void) {}
#endif

// -- Image helpers ------------------------------------------------------------

static SDL_Surface *load_asset(const char *name) {
    for (int i = 0; i < asset_cache_count; i++) {
        if (strcmp(asset_cache[i].name, name) == 0) return asset_cache[i].surface;
    }
    if (asset_cache_count >= (int)(sizeof(asset_cache) / sizeof(asset_cache[0]))) return NULL;

    char path[512];
    snprintf(path, sizeof(path), "%s/%s", ASSET_ROOT, name);
    SDL_Surface *img = IMG_Load(path);
    if (!img) {
        char bmp_path[512];
        strncpy(bmp_path, path, sizeof(bmp_path) - 1);
        bmp_path[sizeof(bmp_path) - 1] = '\0';
        char *dot = strrchr(bmp_path, '.');
        if (dot) strcpy(dot, ".bmp");
        else strncat(bmp_path, ".bmp", sizeof(bmp_path) - strlen(bmp_path) - 1);
        img = SDL_LoadBMP(bmp_path);
        if (!img) {
            log_kv("asset load failed", path);
            strncpy(asset_cache[asset_cache_count].name, name, sizeof(asset_cache[asset_cache_count].name) - 1);
            asset_cache[asset_cache_count].name[sizeof(asset_cache[asset_cache_count].name) - 1] = '\0';
            asset_cache[asset_cache_count].surface = NULL;
            asset_cache_count++;
            return NULL;
        }
    }
    /* Convert palette/non-32bpp surfaces so draw_asset can read pixels uniformly */
    if (img->format->BytesPerPixel != 4) {
        SDL_Surface *conv = SDL_DisplayFormatAlpha(img);
        SDL_FreeSurface(img);
        img = conv;
        if (!img) return NULL;
    }
    strncpy(asset_cache[asset_cache_count].name, name, sizeof(asset_cache[asset_cache_count].name) - 1);
    asset_cache[asset_cache_count].name[sizeof(asset_cache[asset_cache_count].name) - 1] = '\0';
    asset_cache[asset_cache_count].surface = img;
    asset_cache_count++;
    return img;
}

static Uint32 surface_pixel(SDL_Surface *s, int x, int y) {
    Uint8 *p = (Uint8 *)s->pixels + y * s->pitch + x * s->format->BytesPerPixel;
    switch (s->format->BytesPerPixel) {
    case 1: return *p;
    case 2: return *(Uint16 *)p;
    case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN) return p[0] << 16 | p[1] << 8 | p[2];
        return p[0] | p[1] << 8 | p[2] << 16;
    default: return *(Uint32 *)p;
    }
}

static void put_screen_pixel(int x, int y, Uint32 pixel) {
    if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) return;
    Uint8 *p = (Uint8 *)screen->pixels + y * screen->pitch + x * screen->format->BytesPerPixel;
    switch (screen->format->BytesPerPixel) {
    case 1: *p = (Uint8)pixel; break;
    case 2: *(Uint16 *)p = (Uint16)pixel; break;
    case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (pixel >> 16) & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = pixel & 0xff;
        } else {
            p[0] = pixel & 0xff;
            p[1] = (pixel >> 8) & 0xff;
            p[2] = (pixel >> 16) & 0xff;
        }
        break;
    default: *(Uint32 *)p = pixel; break;
    }
}

static int is_keyed_icon_bg(const char *name, Uint8 r, Uint8 g, Uint8 b) {
    if (strncmp(name, "ui_", 3) == 0) return 0;
    int spread = abs((int)r - (int)g) + abs((int)r - (int)b) + abs((int)g - (int)b);
    if (r > 246 && g > 246 && b > 246) return 1;
    if (r > 214 && g > 214 && b > 214 && spread < 18) return 1;
    return 0;
}

static int draw_asset(const char *name, int x, int y, int w, int h) {
    SDL_Surface *img = load_asset(name);
    if (!img || w <= 0 || h <= 0) return 0;

    int has_alpha = (img->format->Amask != 0);

    SDL_Rect clip;
    SDL_GetClipRect(screen, &clip);
    int cx1 = clip.x, cy1 = clip.y;
    int cx2 = clip.x + clip.w, cy2 = clip.y + clip.h;

    SDL_LockSurface(img);
    SDL_LockSurface(screen);
    for (int dy = 0; dy < h; dy++) {
        int sy = dy + y;
        if (sy < cy1 || sy >= cy2) continue;
        int isy = (dy * img->h) / h;
        for (int dx = 0; dx < w; dx++) {
            int sx = dx + x;
            if (sx < cx1 || sx >= cx2) continue;
            int isx = (dx * img->w) / w;
            Uint32 raw = surface_pixel(img, isx, isy);
            Uint8 r, g, b, a = 255;
            SDL_GetRGB(raw, img->format, &r, &g, &b);
            if (has_alpha) SDL_GetRGBA(raw, img->format, &r, &g, &b, &a);
            if (a < 8) continue;
            if (is_keyed_icon_bg(name, r, g, b)) continue;

            /* Defringe: icons composited against white have edge pixels
             * blended toward white. Recover the true colour before blending.
             * Formula: orig = (composite - white*(1-a)) / a
             * Done in integer: orig = (c*255 - 255*(255-a)) / a  */
            if (has_alpha && a < 250) {
                int inv = 255 - a;
                int rr = ((int)r * 255 - 255 * inv) / a;
                int rg = ((int)g * 255 - 255 * inv) / a;
                int rb = ((int)b * 255 - 255 * inv) / a;
                r = (Uint8)(rr < 0 ? 0 : rr > 255 ? 255 : rr);
                g = (Uint8)(rg < 0 ? 0 : rg > 255 ? 255 : rg);
                b = (Uint8)(rb < 0 ? 0 : rb > 255 ? 255 : rb);
            }

            if (a >= 250) {
                put_screen_pixel(sx, sy, SDL_MapRGB(screen->format, r, g, b));
            } else {
                /* Alpha blend with whatever is already on screen */
                Uint32 bg_px = surface_pixel(screen, sx, sy);
                Uint8 br, bg, bb;
                SDL_GetRGB(bg_px, screen->format, &br, &bg, &bb);
                Uint8 or_ = (Uint8)(((int)r * a + (int)br * (255 - a)) >> 8);
                Uint8 og  = (Uint8)(((int)g * a + (int)bg * (255 - a)) >> 8);
                Uint8 ob  = (Uint8)(((int)b * a + (int)bb * (255 - a)) >> 8);
                put_screen_pixel(sx, sy, SDL_MapRGB(screen->format, or_, og, ob));
            }
        }
    }
    SDL_UnlockSurface(screen);
    SDL_UnlockSurface(img);
    return 1;
}

static void draw_panel_asset(int x, int y, int w, int h) {
    // Cream panel with 1px warm border
    fill_rrect(x, y, w, h, 4, C_CARD_BORDER);
    fill_rrect(x + 1, y + 1, w - 2, h - 2, 3, C_CARD);
}

static void draw_select_asset(int x, int y, int w, int h) {
    /* Drop shadow: soften 3px below/right before drawing */
    fill_rect_alpha(x + 3, y + h,     w, 2, 55);
    fill_rect_alpha(x + 3, y + h + 2, w, 1, 28);
    fill_rect_alpha(x + w, y + 3,     3, h, 40);

    /* Selection body: rounded border, then two-tone inner fill with rrect so
       corners are actually visible (fill_rect would overwrite the corner curve) */
    int r = 6;
    fill_rrect(x, y, w, h, r, C_SEL_BORDER);
    fill_rrect(x + 1, y + 1,         w - 2, h / 2,          r - 1, C_SEL_HI);
    fill_rect( x + 1, y + 1 + h / 2, w - 2, h - h / 2 - 2,         C_SEL);

    /* 1px top shine — lighten SEL_HI toward white */
    Uint8 sr, sg, sb;
    SDL_GetRGB(C_SEL_HI, screen->format, &sr, &sg, &sb);
    Uint8 lr = sr + (255 - sr) * 2 / 3;
    Uint8 lg = sg + (255 - sg) * 2 / 3;
    Uint8 lb = sb + (255 - sb) * 2 / 3;
    fill_rect(x + r, y + 1, w - 2*r, 1, RGBA(lr, lg, lb));

    /* 1px bottom shadow — darken SEL toward black */
    Uint8 dr, dg, db;
    SDL_GetRGB(C_SEL, screen->format, &dr, &dg, &db);
    fill_rect(x + r, y + h - 2, w - 2*r, 1,
              RGBA(dr * 2 / 3, dg * 2 / 3, db * 2 / 3));
}

static const char *system_full_name(const char *label) {
    if (strcasecmp(label, "GB")      == 0) return "Game Boy";
    if (strcasecmp(label, "GBC")     == 0) return "Game Boy Color";
    if (strcasecmp(label, "GBA")     == 0) return "Game Boy Advance";
    if (strcasecmp(label, "NDS")     == 0) return "Nintendo DS";
    if (strcasecmp(label, "FC")      == 0) return "Famicom";
    if (strcasecmp(label, "NES")     == 0) return "Nintendo NES";
    if (strcasecmp(label, "SFC")     == 0) return "Super Famicom";
    if (strcasecmp(label, "SNES")    == 0) return "Super Nintendo";
    if (strcasecmp(label, "N64")     == 0) return "Nintendo 64";
    if (strcasecmp(label, "VBOY")    == 0) return "Virtual Boy";
    if (strcasecmp(label, "MD")      == 0) return "Mega Drive";
    if (strcasecmp(label, "GEN")     == 0 ||
        strcasecmp(label, "GENESIS") == 0) return "Genesis";
    if (strcasecmp(label, "SMS")     == 0) return "Master System";
    if (strcasecmp(label, "GG")      == 0) return "Game Gear";
    if (strcasecmp(label, "SATURN")  == 0) return "Saturn";
    if (strcasecmp(label, "SCD")     == 0) return "Sega CD";
    if (strcasecmp(label, "32X")     == 0) return "Sega 32X";
    if (strcasecmp(label, "PS")      == 0 ||
        strcasecmp(label, "PSX")     == 0 ||
        strcasecmp(label, "PS1")     == 0) return "PlayStation";
    if (strcasecmp(label, "PSP")     == 0) return "PlayStation Portable";
    if (strcasecmp(label, "PCE")     == 0) return "PC Engine";
    if (strcasecmp(label, "PCECD")   == 0) return "PC Engine CD";
    if (strcasecmp(label, "PCFX")    == 0) return "PC-FX";
    if (strcasecmp(label, "SGFX")    == 0) return "SuperGrafx";
    if (strcasecmp(label, "NEOGEO")  == 0 ||
        strcasecmp(label, "NGP")     == 0) return "Neo Geo Pocket";
    if (strcasecmp(label, "NGPC")    == 0) return "Neo Geo Pocket Color";
    if (strcasecmp(label, "LYNX")    == 0) return "Atari Lynx";
    if (strcasecmp(label, "JAGUAR")  == 0) return "Atari Jaguar";
    if (strcasecmp(label, "2600")    == 0 ||
        strcasecmp(label, "ATARI2600") == 0) return "Atari 2600";
    if (strcasecmp(label, "WSWAN")   == 0) return "WonderSwan";
    if (strcasecmp(label, "WSWANC")  == 0) return "WonderSwan Color";
    if (strcasecmp(label, "COLECO")  == 0) return "ColecoVision";
    if (strcasecmp(label, "VECTREX") == 0) return "Vectrex";
    if (strcasecmp(label, "ADVMAME") == 0) return "MAME";
    return label;  /* fall back to folder name if unknown */
}

static const char *system_icon(const char *label) {
    if (strcasecmp(label, "GBA") == 0) return "gba.png";
    if (strcasecmp(label, "GB") == 0) return "gb.png";
    if (strcasecmp(label, "GBC") == 0) return "gbc.png";
    if (strcasecmp(label, "NES") == 0 || strcasecmp(label, "FC") == 0) return "nes.png";
    if (strcasecmp(label, "SNES") == 0 || strcasecmp(label, "SFC") == 0) return "snes.png";
    if (strcasecmp(label, "Genesis") == 0 || strcasecmp(label, "MD") == 0) return "games.png";
    if (strcasecmp(label, "Game Gear") == 0 || strcasecmp(label, "GG") == 0) return "gbc.png";
    if (strcasecmp(label, "PSX") == 0 || strcasecmp(label, "PS") == 0) return "games.png";
    return "games.png";
}

__attribute__((unused)) static const char *cart_icon(const char *label) {
    if (strcasecmp(label, "GBA") == 0) return "gba_cart.png";
    if (strcasecmp(label, "GB") == 0) return "gb_cart.png";
    if (strcasecmp(label, "GBC") == 0) return "gbc_cart.png";
    if (strcasecmp(label, "NES") == 0 || strcasecmp(label, "FC") == 0) return "nes_cart.png";
    if (strcasecmp(label, "SNES") == 0 || strcasecmp(label, "SFC") == 0) return "snes_cart.png";
    return system_icon(label);
}

// ── Load systems ──────────────────────────────────────────────────────────────

static void load_systems(void) {
    sys_count = 0;
    log_msg("load_systems begin");
    log_file_state("emu_root", EMU_ROOT);
    log_file_state("roms_root", ROMS_ROOT);

    DIR *d = opendir(EMU_ROOT);
    if (!d) {
        log_errno_msg("opendir failed", EMU_ROOT);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) && sys_count < MAX_SYSTEMS) {
        if (ent->d_name[0] == '.') continue;

        char emu_dir[256];
        snprintf(emu_dir, sizeof(emu_dir), "%s/%s", EMU_ROOT, ent->d_name);

        struct stat st;
        if (stat(emu_dir, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        char config[512];
        snprintf(config, sizeof(config), "%s/config.json", emu_dir);
        if (access(config, F_OK) != 0) {
            log_kv("skip emu missing config", config);
            continue;
        }

        char label[48]   = "";
        char rompath[128] = "";
        char extlist[128] = "";

        json_str(config, "label",   label,   sizeof(label));
        json_str(config, "rompath", rompath, sizeof(rompath));
        json_str(config, "extlist", extlist, sizeof(extlist));

        if (label[0] == '\0') {
            log_kv("skip emu empty label", config);
            continue;
        }

        char abs_rom[256];
        if (rompath[0] != '\0') {
            resolve_sdcard_path(rompath, abs_rom, sizeof(abs_rom));
        } else {
            // Guess: /mnt/SDCARD/Roms/<dirname>
            snprintf(abs_rom, sizeof(abs_rom), "%s/%s", ROMS_ROOT, ent->d_name);
        }

        // Only include if roms directory exists
        if (stat(abs_rom, &st) != 0 || !S_ISDIR(st.st_mode)) {
            log_errno_msg("skip system missing rom dir", abs_rom);
            continue;
        }

        System *sys = &systems[sys_count++];
        strncpy(sys->label,   label,   sizeof(sys->label)   - 1);
        strncpy(sys->emu_dir, emu_dir, sizeof(sys->emu_dir) - 1);
        strncpy(sys->rom_dir, abs_rom, sizeof(sys->rom_dir) - 1);
        strncpy(sys->extlist, extlist, sizeof(sys->extlist) - 1);
    }

    closedir(d);
    qsort(systems, sys_count, sizeof(System), cmp_sys);

    char msg[64];
    snprintf(msg, sizeof(msg), "load_systems count=%d", sys_count);
    log_msg(msg);
}

// ── Load games for selected system ───────────────────────────────────────────

static void load_games(int idx) {
    game_count  = 0;
    game_sel    = 0;
    game_offset = 0;

    if (idx < 0 || idx >= sys_count) return;
    System *sys = &systems[idx];
    log_kv("load_games system", sys->label);
    log_kv("load_games rom_dir", sys->rom_dir);
    log_kv("load_games extlist", sys->extlist);
    log_file_state("load_games rom_dir_state", sys->rom_dir);

    DIR *d = opendir(sys->rom_dir);
    if (!d) {
        log_errno_msg("opendir failed", sys->rom_dir);
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) && game_count < MAX_GAMES) {
        if (ent->d_name[0] == '.') continue;

        // Skip directories
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", sys->rom_dir, ent->d_name);
        struct stat st;
        if (stat(fullpath, &st) != 0 || S_ISDIR(st.st_mode)) continue;

        // Extension filter (skip if extlist set and file doesn't match)
        if (sys->extlist[0] != '\0' && !ext_match(ent->d_name, sys->extlist)) continue;

        char display[240];
        strip_ext(ent->d_name, display, sizeof(display));

        Game *g = &games[game_count++];
        strncpy(g->name, display,  sizeof(g->name) - 1);
        strncpy(g->path, fullpath, sizeof(g->path) - 1);
    }

    closedir(d);
    qsort(games, game_count, sizeof(Game), cmp_game);

    char msg[64];
    snprintf(msg, sizeof(msg), "load_games count=%d", game_count);
    log_msg(msg);
}

static void load_play_entries(const char *path, PlayEntry *entries, int *count, int sort_entries) {
    *count = 0;
    log_file_state("load_play_entries source", path);
    FILE *f = fopen(path, "r");
    if (!f) {
        log_errno_msg("play list open failed", path);
        return;
    }

    char line[2048];
    while (fgets(line, sizeof(line), f) && *count < MAX_GAMES) {
        PlayEntry *e = &entries[*count];
        memset(e, 0, sizeof(*e));
        if (!json_str_from_buf(line, "label", e->label, sizeof(e->label))) continue;
        if (!json_str_from_buf(line, "rompath", e->rompath, sizeof(e->rompath))) continue;
        if (!json_str_from_buf(line, "launch", e->launch, sizeof(e->launch))) continue;
        system_from_launch(e->launch, e->system, sizeof(e->system));
        (*count)++;
    }
    fclose(f);

    if (sort_entries) qsort(entries, *count, sizeof(PlayEntry), cmp_play_entry);
    log_kv("load_play_entries path", path);
    log_int("load_play_entries count", *count);
}

static void load_recent(void) {
    LogTimer _t = log_timer_begin("load_recent");
    load_play_entries(POCKETOS_ROOT "/Roms/recentlist.json",
                      recent_entries, &recent_count, 0);
    if (recent_count == 0) {
        load_play_entries(POCKETOS_ROOT "/Roms/recentlist-hidden.json",
                          recent_entries, &recent_count, 0);
    }
    recent_sel = 0;
    recent_offset = 0;
    log_timer_end(_t);
}

static void load_favorites(void) {
    LogTimer _t = log_timer_begin("load_favorites");
    load_play_entries(POCKETOS_ROOT "/Roms/favourite.json",
                      favorite_entries, &favorite_count, 1);
    favorite_sel = 0;
    favorite_offset = 0;
    log_timer_end(_t);
}

#define FAV_PATH POCKETOS_ROOT "/Roms/favourite.json"

static int is_favorite(const char *rompath) {
    for (int i = 0; i < favorite_count; i++)
        if (strcmp(favorite_entries[i].rompath, rompath) == 0) return 1;
    return 0;
}

static void toggle_favorite(const char *label, const char *rompath, const char *launch) {
    int found = -1;
    for (int i = 0; i < favorite_count; i++) {
        if (strcmp(favorite_entries[i].rompath, rompath) == 0) { found = i; break; }
    }

    if (found >= 0) {
        /* Remove: shift entries down */
        for (int i = found; i < favorite_count - 1; i++)
            favorite_entries[i] = favorite_entries[i + 1];
        favorite_count--;
    } else {
        /* Add */
        if (favorite_count < MAX_GAMES) {
            PlayEntry *e = &favorite_entries[favorite_count++];
            memset(e, 0, sizeof(*e));
            strncpy(e->label,   label,   sizeof(e->label)   - 1);
            strncpy(e->rompath, rompath, sizeof(e->rompath) - 1);
            strncpy(e->launch,  launch,  sizeof(e->launch)  - 1);
        }
    }

    /* Re-sort and write back */
    qsort(favorite_entries, favorite_count, sizeof(PlayEntry), cmp_play_entry);

    FILE *f = fopen(FAV_PATH, "w");
    if (!f) { log_errno_msg("toggle_favorite write failed", FAV_PATH); return; }
    for (int i = 0; i < favorite_count; i++) {
        fprintf(f, "{\"label\":\"%s\",\"rompath\":\"%s\",\"launch\":\"%s\"}\n",
                favorite_entries[i].label,
                favorite_entries[i].rompath,
                favorite_entries[i].launch);
    }
    fclose(f);
}

// ── Launch a game ─────────────────────────────────────────────────────────────

static void launch_game(int sys_idx, int game_idx) {
    if (sys_idx < 0 || sys_idx >= sys_count) {
        log_int("launch_game invalid sys_idx", sys_idx);
        log_int("launch_game sys_count", sys_count);
        return;
    }
    if (game_idx < 0 || game_idx >= game_count) {
        log_int("launch_game invalid game_idx", game_idx);
        log_int("launch_game game_count", game_count);
        return;
    }

    System *sys = &systems[sys_idx];
    Game *game = &games[game_idx];

    log_kv("launch system", sys->label);
    log_kv("launch emu_dir", sys->emu_dir);
    log_kv("launch rom", game->path);
    log_file_state("launch emu_dir_state", sys->emu_dir);
    char launch_sh[320];
    snprintf(launch_sh, sizeof(launch_sh), "%s/launch.sh", sys->emu_dir);
    log_file_state("launch launch_sh", launch_sh);
    log_file_state("launch rom_state", game->path);

    FILE *f = fopen(CMD_PATH, "w");
    if (!f) {
        log_errno_msg("cmd open failed", CMD_PATH);
        return;
    }

    fprintf(f,
            "LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so "
            "\"%s/launch.sh\" \"%s\"",
            sys->emu_dir, game->path);
    fclose(f);

    if (chmod(CMD_PATH, 0755) != 0) log_errno_msg("cmd chmod failed", CMD_PATH);
    log_kv("cmd written", CMD_PATH);
    log_file_state("cmd file", CMD_PATH);
    play_launch();
    stop_audio();
    running = 0;
}

static void launch_entry(PlayEntry *entry) {
    log_kv("launch entry", entry->label);
    log_kv("launch entry rom", entry->rompath);
    log_kv("launch entry launch", entry->launch);
    log_file_state("launch entry rom_state", entry->rompath);
    log_file_state("launch entry launch_state", entry->launch);

    FILE *f = fopen(CMD_PATH, "w");
    if (!f) {
        log_errno_msg("cmd open failed", CMD_PATH);
        return;
    }
    fprintf(f,
            "LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so "
            "\"%s\" \"%s\"",
            entry->launch, entry->rompath);
    fclose(f);
    if (chmod(CMD_PATH, 0755) != 0) log_errno_msg("cmd chmod failed", CMD_PATH);
    log_file_state("cmd file", CMD_PATH);
    play_launch();
    stop_audio();
    running = 0;
}

static void launch_app_cmd(const char *cmd) {
    log_kv("launch app cmd", cmd);
    FILE *f = fopen(CMD_PATH, "w");
    if (!f) {
        log_errno_msg("cmd open failed", CMD_PATH);
        return;
    }
    fprintf(f, "#!/bin/sh\n%s\n", cmd);
    fclose(f);
    if (chmod(CMD_PATH, 0755) != 0) log_errno_msg("cmd chmod failed", CMD_PATH);
    log_file_state("cmd file", CMD_PATH);
    play_launch();
    stop_audio();
    running = 0;
}

/* Sleep and power-off must NOT write cmd_to_run.sh — that file persists across
   boots and would create a shutdown loop. Execute directly via system() instead. */
static void exec_power_cmd(const char *cmd) {
    log_kv("exec power cmd", cmd);
    stop_audio();
    system(cmd);
    running = 0;
}

static void run_settings_action(const char *cmd) {
    if (!cmd) return;
    if (strcmp(cmd, "restart") == 0) {
        play_launch();
        stop_audio();
        running = 0;
        return;
    }
    launch_app_cmd(cmd);
}

/* Height of a single row by index. */
static int settings_row_h(int i) {
    return SETTINGS_ENTRIES[i].is_header ? HEADER_H : HOME_ITEM_H;
}

/* Pixel Y (relative to CONTENT_Y) of the top of row i. */
static int settings_row_top(int i) {
    int y = 0;
    for (int r = 0; r < i; r++) y += settings_row_h(r);
    return y;
}

/* Total height of all settings rows in pixels. */
static int total_settings_height(void) {
    return settings_row_top(SETTINGS_COUNT);
}

static void open_settings_kind(const char *kind) {
    if (strcmp(kind, "display") == 0) kind = "brightness";
    settings_sel = 1;  /* default: first real entry after DISPLAY header */
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        if (!SETTINGS_ENTRIES[i].is_header && SETTINGS_ENTRIES[i].kind &&
            strcmp(SETTINGS_ENTRIES[i].kind, kind) == 0) {
            settings_sel = i;
            break;
        }
    }
    /* scroll so selected row is visible */
    int row_y = settings_row_top(settings_sel);
    settings_scroll_px = row_y;
    if (settings_scroll_px < 0) settings_scroll_px = 0;
    int max_scroll = total_settings_height() - CONTENT_H;
    if (max_scroll < 0) max_scroll = 0;
    if (settings_scroll_px > max_scroll) settings_scroll_px = max_scroll;
    state = STATE_SETTINGS;
}

static void adjust_csc_field(const char *field, int min, int max, int def, int delta) {
    int v = json_int_file(POCKETOS_ROOT "/system.json", field, def);
    if (delta == 0) delta = 1;
    v = clampi(v + delta, min, max);
    set_json_int_file(POCKETOS_ROOT "/system.json", field, v);
    apply_display_csc();
}

static void adjust_setting(int delta) {
    if (SETTINGS_ENTRIES[settings_sel].is_header) return;
    const char *k = SETTINGS_ENTRIES[settings_sel].kind;
    if (strcmp(k, "brightness") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "brightness", 7);
        if (delta == 0) delta = 1;
        apply_brightness(v + delta);
        play_select();
    } else if (strcmp(k, "lumination") == 0) {
        adjust_csc_field("lumination", 0, 20, 7, delta == 0 ? 1 : delta);
        play_select();
    } else if (strcmp(k, "saturation") == 0) {
        adjust_csc_field("saturation", 0, 20, 10, delta == 0 ? 1 : delta);
        play_select();
    } else if (strcmp(k, "hue") == 0) {
        adjust_csc_field("hue", 0, 20, 10, delta == 0 ? 1 : delta);
        play_select();
    } else if (strcmp(k, "contrast") == 0) {
        adjust_csc_field("contrast", 0, 20, 10, delta == 0 ? 1 : delta);
        play_select();
    } else if (strcmp(k, "audio") == 0) {
        int vol = json_int_file(POCKETOS_ROOT "/system.json", "vol", 15);
        int mute = json_int_file(POCKETOS_ROOT "/system.json", "mute", 0);
        if (delta == 0) apply_volume(vol, !mute);
        else apply_volume(vol + delta, mute);
        play_select();
    } else if (strcmp(k, "mute") == 0) {
        int vol  = json_int_file(POCKETOS_ROOT "/system.json", "vol",  15);
        int mute = json_int_file(POCKETOS_ROOT "/system.json", "mute",  0);
        apply_volume(vol, !mute);
        play_select();
    } else if (strcmp(k, "audiofix") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "audiofix", 1);
        set_json_int_file(POCKETOS_ROOT "/system.json", "audiofix", !v);
        play_select();
    } else if (strcmp(k, "vibration") == 0) {
        int v = read_config_int("vibration", 2);
        if (delta == 0) delta = 1;
        write_config_int("vibration", clampi(v + delta, 0, 4));
        play_select();
    } else if (strcmp(k, "bluelightlvl") == 0) {
        int v = read_config_int("display/blueLightLevel", 0);
        if (delta == 0) delta = 1;
        apply_blue_light_level(v + delta);
        play_select();
    } else if (strcmp(k, "pwmfreq") == 0) {
        int v = read_config_int("pwmfrequency", 7);
        if (delta == 0) delta = 1;
        write_config_int("pwmfrequency", clampi(v + delta, 0, 10));
        play_select();
    } else if (strcmp(k, "utcoffset") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "utcoffset", 0);
        if (delta == 0) delta = 1;
        set_json_int_file(POCKETOS_ROOT "/system.json", "utcoffset", clampi(v + delta, -12, 14));
        play_select();
    } else if (strcmp(k, "sleeptimer") == 0) {
        int v = json_int_file(POCKETOS_ROOT "/system.json", "hibernate", 5);
        if (delta == 0) delta = 1;
        // steps: 0,1,2,3,5,10,15,30 minutes
        static const int steps[] = {0,1,2,3,5,10,15,30};
        int n = (int)(sizeof(steps)/sizeof(steps[0]));
        int idx = 0;
        for (int i = 0; i < n; i++) if (steps[i] <= v) idx = i;
        idx = clampi(idx + delta, 0, n - 1);
        set_json_int_file(POCKETOS_ROOT "/system.json", "hibernate", steps[idx]);
        play_select();
    } else if (strcmp(k, "autoresume") == 0) {
        int disabled = read_config_flag(".noAutoStart");
        apply_config_flag(".noAutoStart", !disabled);
        play_select();
    } else if (strcmp(k, "standby") == 0) {
        int dis = read_config_flag(".disableStandby");
        apply_config_flag(".disableStandby", !dis);
        play_select();
    } else if (strcmp(k, "battwarn") == 0) {
        int v = read_config_int("battery/warnAt", 10);
        if (delta == 0) delta = 1;
        write_config_int("battery/warnAt", clampi(v + delta * 5, 0, 50));
        play_select();
    } else if (strcmp(k, "battsave") == 0) {
        int v = read_config_int("battery/exitAt", 4);
        if (delta == 0) delta = 1;
        write_config_int("battery/exitAt", clampi(v + delta * 2, 0, 20));
        play_select();
    } else if (strcmp(k, "network") == 0) {
        int wifi = json_int_file(POCKETOS_ROOT "/system.json", "wifi", 0);
        apply_wifi(!wifi);
        play_select();
    } else if (strcmp(k, "power") == 0 && delta == 0) {
        run_settings_action(SETTINGS_ENTRIES[settings_sel].cmd);
    } else {
        play_select();
    }
}

// ── Drawing helpers ───────────────────────────────────────────────────────────

static void fill_rect(int x, int y, int w, int h, Uint32 color) {
    SDL_Rect r = { x, y, w, h };
    SDL_FillRect(screen, &r, color);
}

/* Darken existing pixels in a region by alpha/255 (0=none, 255=black). */
static void fill_rect_alpha(int x, int y, int w, int h, Uint8 alpha) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0 || alpha == 0) return;
    if (SDL_LockSurface(screen) < 0) return;
    Uint32 *pix = (Uint32 *)screen->pixels;
    int stride = screen->pitch >> 2;
    int inv = 255 - alpha;
    for (int row = y; row < y + h; row++) {
        Uint32 *line = pix + row * stride;
        for (int col = x; col < x + w; col++) {
            Uint8 r, g, b;
            SDL_GetRGB(line[col], screen->format, &r, &g, &b);
            line[col] = SDL_MapRGB(screen->format,
                (Uint8)(r * inv >> 8),
                (Uint8)(g * inv >> 8),
                (Uint8)(b * inv >> 8));
        }
    }
    SDL_UnlockSurface(screen);
}

// Warm parchment texture — three close cream shades, diagonal grain.
// Used for home and settings backgrounds instead of a flat fill.
static void draw_textured_bg(int x, int y, int w, int h) {
    Uint32 c0 = C_BG;
    Uint8 br, bg, bb;
    SDL_GetRGB(c0, screen->format, &br, &bg, &bb);
    /* derive grain from theme bg: +12 lighter, -10 darker */
    Uint8 l = (br > 243) ? 255 : br + 12;
    Uint8 d = (br < 10)  ? 0   : br - 10;
    float ratio = (bb > br) ? 1.2f : 1.0f;   /* tint toward dominant channel */
    Uint8 lg = (bg > 243) ? 255 : (Uint8)(bg + (int)(12 * ratio));
    Uint8 lb = (bb > 243) ? 255 : (Uint8)(bb + (int)(12 * ratio));
    Uint8 dg = (bg < 10)  ? 0   : (Uint8)(bg - 10);
    Uint8 db = (bb < 10)  ? 0   : (Uint8)(bb - 10);
    Uint32 c1 = RGBA(l, lg, lb);   /* highlight grain */
    Uint32 c2 = RGBA(d, dg, db);   /* shadow grain    */

    if (SDL_LockSurface(screen) < 0) { fill_rect(x, y, w, h, c0); return; }
    Uint32 *pix = (Uint32 *)screen->pixels;
    int stride = screen->pitch >> 2;
    int x1 = x + w, y1 = y + h;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;

    for (int row = y; row < y1; row++) {
        Uint32 *line = pix + row * stride;
        for (int col = x; col < x1; col++) {
            // Diagonal grain: hash maps (col,row) → 0..63.
            // ~5% lighter, ~11% darker, ~84% base — reads as paper grain.
            int v = (col * 5 + row * 11) & 63;
            line[col] = (v < 3) ? c1 : (v < 10) ? c2 : c0;
        }
    }
    SDL_UnlockSurface(screen);
}

// 5-bar signal-style level indicator. Bars grow taller left→right, filled in lavender.
__attribute__((unused))
static void draw_level_bars(int x, int y, int val, int max) {
    if (max <= 0) return;
    int bars = 5, bw = 5, gap = 3;
    int bh_min = 5, bh_max = 20;
    for (int b = 0; b < bars; b++) {
        int bh = bh_min + (b * (bh_max - bh_min)) / (bars - 1);
        int filled = (val * (bars - 1) >= b * max);
        Uint32 c = filled ? C_SEL_BORDER : C_SEP;
        fill_rect(x + b * (bw + gap), y + bh_max - bh, bw, bh, c);
    }
}

// Returns max value for numeric settings, 0 for toggles/text.
static int setting_max_val(const char *k) {
    if (strcmp(k, "brightness")  == 0) return 10;
    if (strcmp(k, "lumination")  == 0) return 20;
    if (strcmp(k, "saturation")  == 0) return 20;
    if (strcmp(k, "hue")         == 0) return 20;
    if (strcmp(k, "contrast")    == 0) return 20;
    if (strcmp(k, "bluelightlvl")== 0) return 6;
    if (strcmp(k, "pwmfreq")     == 0) return 10;
    if (strcmp(k, "audio")       == 0) return 20;
    if (strcmp(k, "vibration")   == 0) return 4;
    return 0;
}

// Returns current raw value for numeric settings.
static int setting_cur_val(const char *k) {
    if (strcmp(k, "brightness")  == 0) return json_int_file(POCKETOS_ROOT "/system.json", "brightness",  7);
    if (strcmp(k, "lumination")  == 0) return json_int_file(POCKETOS_ROOT "/system.json", "lumination",  7);
    if (strcmp(k, "saturation")  == 0) return json_int_file(POCKETOS_ROOT "/system.json", "saturation", 10);
    if (strcmp(k, "hue")         == 0) return json_int_file(POCKETOS_ROOT "/system.json", "hue",        10);
    if (strcmp(k, "contrast")    == 0) return json_int_file(POCKETOS_ROOT "/system.json", "contrast",   10);
    if (strcmp(k, "bluelightlvl")== 0) return read_config_int("display/blueLightLevel", 0);
    if (strcmp(k, "pwmfreq")     == 0) return read_config_int("pwmfrequency", 7);
    if (strcmp(k, "audio")       == 0) return json_int_file(POCKETOS_ROOT "/system.json", "vol",        15);
    if (strcmp(k, "vibration")   == 0) return read_config_int("vibration", 2);
    return 0;
}

static void draw_text(TTF_Font *font, const char *text, int x, int y, SDL_Color col) {
    if (!text || text[0] == '\0') return;
    SDL_Surface *s = TTF_RenderUTF8_Blended(font, text, col);
    if (!s) return;
    SDL_Rect dst = { x, y, 0, 0 };
    SDL_BlitSurface(s, NULL, screen, &dst);
    SDL_FreeSurface(s);
}

static int text_w(TTF_Font *font, const char *text) {
    int w = 0;
    TTF_SizeUTF8(font, text, &w, NULL);
    return w;
}

static void draw_text_center(TTF_Font *font, const char *text,
                              int area_x, int area_w, int y, SDL_Color col) {
    int tw = text_w(font, text);
    draw_text(font, text, area_x + (area_w - tw) / 2, y, col);
}

// Rounded rectangle via filled rects (SDL1.2 has no primitives)
static void fill_rrect(int x, int y, int w, int h, int r, Uint32 col) {
    if (r < 1) { fill_rect(x, y, w, h, col); return; }
    if (r > h / 2) r = h / 2;
    fill_rect(x + r, y,         w - 2*r, h,     col);
    fill_rect(x,     y + r,     r,       h-2*r, col);
    fill_rect(x+w-r, y + r,     r,       h-2*r, col);
    // soften corners with small squares
    fill_rect(x + r/2, y,         r - r/2, r,       col);
    fill_rect(x + w - r, y,       r - r/2, r,       col);
    fill_rect(x + r/2, y+h-r,    r - r/2, r,       col);
    fill_rect(x + w - r, y+h-r,  r - r/2, r,       col);
}

// Right-pointing pixel-art chevron. x,y = left edge vertical center.
// arm = px per side, thick = stroke width (try arm=8 thick=2 for rows).
static void draw_chevron(int x, int y, int arm, int thick, Uint32 col) {
    for (int i = 0; i < arm; i++) {
        int s = arm - 1 - i; // spread from centre
        fill_rect(x + i, y - s, thick, thick, col);
        fill_rect(x + i, y + s, thick, thick, col);
    }
}

// Rectangular button pip with border — design guide btn-hint style.
// pw x ph with 2px rounded corners, colored bg, lighter border, white letter.
static void draw_btn_pill(int x, int y, int pw, int ph, Uint32 border, Uint32 bg, const char *letter) {
    fill_rrect(x, y, pw, ph, 2, border);
    fill_rrect(x + 1, y + 1, pw - 2, ph - 2, 2, bg);
    int lw = text_w(font_small, letter);
    draw_text(font_small, letter, x + (pw - lw) / 2, y + (ph - 14) / 2, SC_WHITE);
}

// Shoulder-button pip — same style, wider shape for L/R.
static void draw_shoulder_pill(int x, int y, int w, int h, Uint32 border, Uint32 bg, const char *letter) {
    fill_rrect(x, y, w, h, 2, border);
    fill_rrect(x + 1, y + 1, w - 2, h - 2, 2, bg);
    int lw = text_w(font_small, letter);
    draw_text(font_small, letter, x + (w - lw) / 2, y + (h - 14) / 2, SC_WHITE);
}

/* Minimal PNG writer — no external process, just zlib.
   Writes 24-bit RGB PNG from the current SDL screen surface. */
static void png_put_u32be(uint8_t *b, uint32_t v) {
    b[0]=v>>24; b[1]=v>>16; b[2]=v>>8; b[3]=v;
}
static void png_write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t hdr[4];
    png_put_u32be(hdr, len);
    fwrite(hdr, 4, 1, f);
    fwrite(type, 4, 1, f);
    uint32_t crc = crc32(crc32(0, (const Bytef*)type, 4), data ? data : (const Bytef*)"", len);
    if (data && len) fwrite(data, len, 1, f);
    png_put_u32be(hdr, crc);
    fwrite(hdr, 4, 1, f);
}
static void take_screenshot(void) {
    const char *dir = "/mnt/SDCARD/Screenshots";
    mkdir(dir, 0755);

    char out[256];
    int n;
    for (n = 0; n < 1000; n++) {
        snprintf(out, sizeof(out), "%s/Screenshot_%03d.png", dir, n);
        if (access(out, F_OK) != 0) break;
    }

    FILE *f = fopen(out, "wb");
    if (!f) { log_errno_msg("screenshot fopen failed", out); return; }

    int w = screen->w, h = screen->h;

    /* PNG signature */
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 8, 1, f);

    /* IHDR */
    uint8_t ihdr[13] = {0};
    png_put_u32be(ihdr,     (uint32_t)w);
    png_put_u32be(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;   /* bit depth */
    ihdr[9] = 2;   /* colour type: RGB */
    png_write_chunk(f, "IHDR", ihdr, 13);

    /* Build filter-byte-prefixed raw rows */
    int rowbytes = 1 + w * 3;
    uint8_t *raw = malloc((size_t)h * rowbytes);
    SDL_LockSurface(screen);
    for (int y = 0; y < h; y++) {
        raw[y * rowbytes] = 0;  /* filter: None */
        for (int x = 0; x < w; x++) {
            uint32_t px = ((uint32_t*)((uint8_t*)screen->pixels + y * screen->pitch))[x];
            uint8_t r, g, b, a;
            SDL_GetRGBA(px, screen->format, &r, &g, &b, &a);
            raw[y * rowbytes + 1 + x*3]     = r;
            raw[y * rowbytes + 1 + x*3 + 1] = g;
            raw[y * rowbytes + 1 + x*3 + 2] = b;
        }
    }
    SDL_UnlockSurface(screen);

    /* Compress and write IDAT */
    uLongf comp_len = compressBound((uLong)h * rowbytes);
    uint8_t *comp = malloc(comp_len);
    compress2(comp, &comp_len, raw, (uLong)h * rowbytes, 6);
    free(raw);
    png_write_chunk(f, "IDAT", comp, (uint32_t)comp_len);
    free(comp);

    png_write_chunk(f, "IEND", NULL, 0);
    fclose(f);

    screenshot_toast_frames = 90;
    log_kv("screenshot saved", out);
}

static void draw_screenshot_toast(void) {
    if (screenshot_toast_frames <= 0) return;
    screenshot_toast_frames--;
    const char *msg = "Screenshot saved";
    int tw = text_w(font_body, msg);
    int pw = tw + 24, ph = 36;
    int px = (SCREEN_W - pw) / 2;
    int py = SCREEN_H - ph - 12;
    fill_rrect(px, py, pw, ph, 6, C_SEL_BORDER);
    fill_rrect(px + 1, py + 1, pw - 2, ph - 2, 5, C_SEL_HI);
    draw_text(font_body, msg, px + 12, py + (ph - 22) / 2, SC_TEXT);
}

// Scroll position indicator: thin track + proportional thumb on the right edge.
// Only draws when total > visible (content exceeds viewport).
static void draw_scrollbar(int x, int y, int h, int total, int visible, int offset) {
    if (total <= visible || total <= 0) return;
    fill_rect(x, y, 4, h, C_SEP);
    int thumb_h = (h * visible) / total;
    if (thumb_h < 14) thumb_h = 14;
    if (thumb_h > h) thumb_h = h;
    int max_travel = h - thumb_h;
    int thumb_y = y + (max_travel > 0 ? (max_travel * offset) / (total - visible) : 0);
    fill_rrect(x, thumb_y, 4, thumb_h, 2, C_SEL_BORDER);
}

// Unified dark navy for all icons — silhouette-first per design guide.
// Per-category rainbow colors conflicted with the retro handheld aesthetic.
static Uint32 icon_accent(const char *name) {
    (void)name;
    return RGBA(0x0D, 0x1C, 0x33);  // #0D1C33 dark navy — same as SC_TEXT
}

static void draw_builtin_icon(const char *name, int x, int y, int w, int h, int selected) {
    // Retro silhouette: no colored bg square, just the symbol on the surface.
    // On cream: per-category accent color. On lavender (selected): dark navy.
    // bg punches "holes" (ring cutouts etc.) using the current surface bg.
    Uint32 sym = selected ? RGBA(0x0D, 0x1C, 0x33) : icon_accent(name);
    Uint32 bg  = selected ? C_SEL : C_BG;

    // Symbol drawn in the inner 60% of the cell (no pad for bg square)
    int sp = w / 5;
    int cx = x + w / 2;
    int sy = y + sp;
    int sh = h - 2 * sp;
    int sw = w - 2 * sp;
    int scx = cx;

    if (strstr(name, "favorites")) {
        // Star: two crossing rectangles + center diamond
        fill_rect(scx - sw/8, sy, sw/4, sh, sym);
        fill_rect(x + sp, sy + sh/3, sw, sh/3, sym);
        fill_rect(scx - sw/5, sy + sh/5, sw*2/5, sh*3/5, sym);
    } else if (strstr(name, "recent")) {
        // Clock face: ring + hands
        int cr = sw / 2;
        fill_rrect(scx - cr, sy + sh/2 - cr, cr*2, cr*2, cr, sym);
        fill_rrect(scx - cr + 3, sy + sh/2 - cr + 3, cr*2 - 6, cr*2 - 6, cr - 3, bg);
        fill_rect(scx - 1, sy + sh/2 - cr + 4, 2, cr - 3, sym);
        fill_rect(scx, sy + sh/2 - 2, cr - 4, 3, sym);
    } else if (strstr(name, "library")) {
        // Three books side by side
        int bkw = sw / 4;
        fill_rect(x + sp,           sy + 2, bkw, sh - 2, sym);
        fill_rect(x + sp + bkw + 2, sy,     bkw, sh,     sym);
        fill_rect(x + sp + bkw*2+4, sy + 3, bkw, sh - 5, sym);
        fill_rect(x + sp - 1, sy + sh - 3, sw + 2, 3, sym);
    } else if (strstr(name, "apps")) {
        // 2×2 grid of rounded tiles
        int ts = sw / 2 - 2;
        fill_rrect(x + sp,        sy,        ts, ts, 3, sym);
        fill_rrect(x + sp + ts+4, sy,        ts, ts, 3, sym);
        fill_rrect(x + sp,        sy + ts+4, ts, ts, 3, sym);
        fill_rrect(x + sp + ts+4, sy + ts+4, ts, ts, 3, sym);
    } else if (strstr(name, "settings")) {
        // Gear: cross + center circle
        fill_rect(scx - sw/5, sy,         sw*2/5, sh, sym);
        fill_rect(x + sp,     sy + sh*2/5, sw,    sh/5, sym);
        fill_rrect(scx - sw/4, sy + sh/4,  sw/2, sh/2, sw/6, bg);
    } else if (strstr(name, "download")) {
        // Down-arrow + tray
        int aw = sw * 2 / 3;
        fill_rect(scx - sw/8, sy, sw/4, sh*2/3, sym);                // shaft
        fill_rect(scx - aw/2, sy + sh/3, aw, sh/6, sym);             // arrowhead wide
        fill_rect(scx - aw/2 + aw/4, sy + sh/3 + sh/6, aw/2, sh/8, sym); // arrowhead tip
        fill_rect(x + sp, sy + sh*4/5, sw, sh/6, sym);               // tray
    } else if (strstr(name, "power")) {
        // Power ring + vertical line
        int pr = sh * 3/8;
        fill_rrect(scx - pr, sy + sh/4, pr*2, pr*2, pr, sym);
        fill_rrect(scx - pr + 3, sy + sh/4 + 3, pr*2 - 6, pr*2 - 6, pr - 3, bg);
        fill_rect(scx - 2, sy, 4, sh/2 + 4, sym);
        fill_rect(scx - 2, sy, 4, sh/4, bg);     // gap in ring at top (erase)
        fill_rect(scx - 2, sy, 4, sh/5, sym);    // line redrawn above gap
    } else if (strstr(name, "music")) {
        // Eighth notes
        fill_rect(scx - sw/4, sy + sh/5, sw/5, sh*3/5, sym);
        fill_rect(scx + sw/8, sy, sw/5, sh*2/3, sym);
        fill_rect(scx - sw/4, sy + sh/5 - 2, sw*5/8, sh/6, sym);
        fill_rrect(scx - sw/4 - 2, sy + sh*3/4, sw/3, sh/5, sw/8, sym);
        fill_rrect(scx + sw/8,     sy + sh*2/3, sw/3, sh/5, sw/8, sym);
    } else if (strstr(name, "wifi")) {
        // WiFi arcs: 3 concentric arcs approximated as rectangles
        fill_rect(scx - 2, sy + sh*2/3, 4, sh/4, sym);
        fill_rect(scx - sw/4, sy + sh/3, sw/2, sh/6, sym);
        fill_rect(scx - sw*2/5, sy, sw*4/5, sh/6, sym);
    } else if (strstr(name, "video")) {
        // Play triangle (approximated)
        int th = sh * 2 / 3;
        fill_rect(x + sp + 2, sy + sh/6, sw/3, th, sym);
        for (int i = 0; i < th; i++) {
            int tw2 = (sw * 2 / 3) * i / th;
            fill_rect(x + sp + sw/3, sy + sh/6 + (th/2 - tw2/2), tw2, 1, sym);
        }
    } else if (strstr(name, "reader")) {
        // Open book
        fill_rect(scx - sw/2, sy + sh/6, sw/2, sh*2/3, sym);
        fill_rect(scx, sy + sh/6, sw/2, sh*2/3, sym);
        fill_rect(scx - 1, sy + sh/6, 2, sh*2/3, bg);
        fill_rect(scx - sw/2, sy + sh*4/5, sw, sh/7, sym);
    } else if (strstr(name, "theme")) {
        // Palette: circle with color dots
        fill_rrect(scx - sw/2, sy + sh/6, sw, sh*2/3, sw/4, sym);
        fill_rrect(scx - sw/3, sy + sh/3, sw*2/3, sh/3, sw/6, bg);
    } else if (strstr(name, "screenshot")) {
        // Camera outline
        fill_rrect(x + sp, sy + sh/4, sw, sh*3/4, 3, sym);
        fill_rrect(x + sp + 2, sy + sh/4 + 2, sw - 4, sh*3/4 - 4, 2, bg);
        fill_rrect(scx - sw/4, sy + sh*3/8, sw/2, sh*3/8, sw/6, sym);
        fill_rect(scx - sw/6, sy + sh/8, sw/3, sh/5, sym);
    } else if (strstr(name, "tool")) {
        // Wrench silhouette
        fill_rect(scx - sw/6, sy + sh/4, sw/3, sh/2, sym);
        fill_rrect(scx - sw/3, sy, sw*2/3, sh/3, sw/5, sym);
        fill_rrect(scx - sw/4, sy + sh*2/3, sw/2, sh/3, sw/6, sym);
    } else if (strstr(name, "sleep")) {
        // Crescent moon — full circle with overlapping bg circle
        int mr = sw * 2 / 5;
        fill_rrect(scx - mr, sy + sh/2 - mr, mr*2, mr*2, mr, sym);
        fill_rrect(scx - mr/2, sy + sh/2 - mr, mr*3/2+2, mr*2, mr, bg);
    } else {
        // Generic cartridge: outline rect with label stripe
        fill_rrect(x + sp, sy, sw, sh, 3, sym);
        fill_rect(x + sp + 2, sy + 2, sw - 4, sh / 3, bg);
        fill_rect(x + sp + sw/4, sy + sh - sh/4, sw/2, sh/5, bg);
    }
}

__attribute__((unused))
static void draw_button_hint(const char *asset, const char *label, const char *text, int x, int y) {
    if (!draw_asset(asset, x, y, 30, 30)) {
        fill_rrect(x + 2, y + 2, 26, 26, 5, RGBA(79, 70, 150));
        draw_text(font_small, label, x + 10, y + 6, SC_WHITE);
    }
    draw_text(font_small, text, x + 38, y + 7, SC_TEXT);
}

// Truncate a string to fit inside max_px pixels using the given font
static void truncate_to_fit(TTF_Font *font, const char *src, char *dst, int dstlen, int max_px) {
    strncpy(dst, src, dstlen - 1);
    dst[dstlen - 1] = '\0';
    while (strlen(dst) > 3 && text_w(font, dst) > max_px) {
        int l = strlen(dst);
        dst[l-1] = '\0';
        dst[l-2] = '.';
        dst[l-3] = '.';
        dst[l-4] = '.';
    }
}

// Split a long string into two lines that each fit within max_px.
// Finds the last space in src where line 1 fits, puts remainder in out2.
// Falls back to hard truncation of line 1 if no good split is found.
static void wrap_text(TTF_Font *font, const char *src,
                      char *out1, int buf1, char *out2, int buf2, int max_px) {
    if (text_w(font, src) <= max_px) {
        strncpy(out1, src, buf1 - 1); out1[buf1 - 1] = '\0';
        out2[0] = '\0';
        return;
    }
    strncpy(out1, src, buf1 - 1); out1[buf1 - 1] = '\0';
    int split = -1;
    int len = (int)strlen(out1);
    for (int i = len - 1; i > 0; i--) {
        if (out1[i] == ' ') {
            out1[i] = '\0';
            if (text_w(font, out1) <= max_px) { split = i; break; }
            out1[i] = ' ';
        }
    }
    if (split < 0) {
        strncpy(out1, src, buf1 - 1); out1[buf1 - 1] = '\0';
        truncate_to_fit(font, out1, out1, buf1, max_px);
        out2[0] = '\0';
        return;
    }
    strncpy(out2, src + split + 1, buf2 - 1); out2[buf2 - 1] = '\0';
    if (text_w(font, out2) > max_px)
        truncate_to_fit(font, out2, out2, buf2, max_px);
}

// ── Status bar ────────────────────────────────────────────────────────────────

static void draw_status(void) {
    /* Theme-aware gradient: C_BAR top → slightly lighter bottom → accent border */
    Uint8 r0, g0, b0;
    SDL_GetRGB(C_BAR, screen->format, &r0, &g0, &b0);
    Uint8 r1 = (Uint8)(r0 + (255-r0)*12/255);
    Uint8 g1 = (Uint8)(g0 + (255-g0)*12/255);
    Uint8 b1 = (Uint8)(b0 + (255-b0)*12/255);
    Uint8 r2 = (Uint8)(r0 + (255-r0)*28/255);
    Uint8 g2 = (Uint8)(g0 + (255-g0)*28/255);
    Uint8 b2 = (Uint8)(b0 + (255-b0)*28/255);
    fill_rect(0, 0,            SCREEN_W, STATUS_H / 2,           C_BAR);
    fill_rect(0, STATUS_H / 2, SCREEN_W, STATUS_H - STATUS_H/2, RGBA(r1,g1,b1));
    fill_rect(0, STATUS_H - 1, SCREEN_W, 1,                      RGBA(r2,g2,b2));
    /* Soft shadow below bar — drawn over content bg for depth */
    fill_rect_alpha(0, STATUS_H,     SCREEN_W, 2, 50);
    fill_rect_alpha(0, STATUS_H + 2, SCREEN_W, 2, 28);
    fill_rect_alpha(0, STATUS_H + 4, SCREEN_W, 1, 12);

    // Three-column layout: [POCKET OS left] [time center] [battery% right]
    int mid_y = (STATUS_H - 20) / 2;  // vertically center 20px-tall text

    // Left: "POCKET OS" in soft white
    draw_text(font_body, "POCKET OS", 12, mid_y, SC_WHITE);

    // Center: clock — localtime() uses TZ env set from Onion's config/.tz at startup
    time_t t = time(NULL);
    int utc_off = json_int_file(POCKETOS_ROOT "/system.json", "utcoffset", 0);
    if (utc_off) t += utc_off * 3600;  // manual fine-tune on top of auto TZ
    struct tm *tm = localtime(&t);
    char clk[16];
    snprintf(clk, sizeof(clk), "%02d:%02d", tm->tm_hour, tm->tm_min);
    draw_text_center(font_body, clk, 0, SCREEN_W, mid_y, SC_WHITE);

    // Right: battery% in green, then battery bar
    int batt = read_battery();
    char bstr[16];
    if (batt >= 0) snprintf(bstr, sizeof(bstr), "%d%%", batt);
    else           snprintf(bstr, sizeof(bstr), "--%%");
    SDL_Color batt_col = {0x8F, 0xD4, 0x6A, 255};  // #8FD46A battery green
    int bw_str = text_w(font_body, bstr);
    draw_text(font_body, bstr, SCREEN_W - bw_str - 10, mid_y, batt_col);
}

// ── Hint bar ──────────────────────────────────────────────────────────────────

// Hint bar background — navy gradient (reverse of status bar)
static void draw_hint_base(void) {
    int y = SCREEN_H - HINT_H;
    // Gradient: lighter navy at top, deeper navy at bottom
    fill_rect(0, y,            SCREEN_W, HINT_H / 2, RGBA(0x10, 0x28, 0x46));
    fill_rect(0, y + HINT_H/2, SCREEN_W, HINT_H - HINT_H/2, RGBA(0x07, 0x1A, 0x33));
    fill_rect(0, y, SCREEN_W, 1, RGBA(0x28, 0x41, 0x5F));  // top border
}

// Draws button hints across the footer bar.
// Design guide: small rectangular pills, A=green, B=red, L/R=gray.
static void draw_hints_row(const char *al, const char *bl,
                           const char *l_lbl, const char *r_lbl,
                           const char *lr_text, const char *yl,
                           const char *xl) {
    int hy = SCREEN_H - HINT_H;
    // Pill dimensions: 26w x 18h (13x9 logical)
    int pw = 26, ph = 18;
    int py  = hy + (HINT_H - ph) / 2;
    int ty  = hy + (HINT_H - 16) / 2;
    // A: green #4FA85E, border #BFE6B9
    Uint32 ca_bg  = RGBA(0x4F, 0xA8, 0x5E), ca_bd = RGBA(0xBF, 0xE6, 0xB9);
    // B: red-maroon #B84E54, border #E6B2B2
    Uint32 cb_bg  = RGBA(0xB8, 0x4E, 0x54), cb_bd = RGBA(0xE6, 0xB2, 0xB2);
    // L/R: gray #4B5568, border #AEB7C5
    Uint32 clr_bg = RGBA(0x4B, 0x55, 0x68), clr_bd = RGBA(0xAE, 0xB7, 0xC5);

    int x = 16;
    int icon_h = HINT_H - 8;
    if (al) {
        int adv;
        if (load_asset("prompt_a.png")) {
            draw_asset("prompt_a.png", x, hy + 4, icon_h, icon_h);
            adv = icon_h;
        } else {
            draw_btn_pill(x, py, pw, ph, ca_bd, ca_bg, "A");
            adv = pw;
        }
        draw_text(font_small, al, x + adv + 5, ty, SC_WHITE);
        x += adv + 5 + text_w(font_small, al) + 14;
    }
    if (bl) {
        int adv;
        if (load_asset("prompt_b.png")) {
            draw_asset("prompt_b.png", x, hy + 4, icon_h, icon_h);
            adv = icon_h;
        } else {
            draw_btn_pill(x, py, pw, ph, cb_bd, cb_bg, "B");
            adv = pw;
        }
        draw_text(font_small, bl, x + adv + 5, ty, SC_WHITE);
        x += adv + 5 + text_w(font_small, bl) + 14;
    }
    if (yl) {
        /* Y button: Xbox yellow */
        Uint32 cy_bg = RGBA(0xC4, 0x9E, 0x1B), cy_bd = RGBA(0xF0, 0xD8, 0x80);
        int adv;
        if (load_asset("xbox_button_color_y.png")) {
            draw_asset("xbox_button_color_y.png", x, hy + 4, icon_h, icon_h);
            adv = icon_h;
        } else {
            draw_btn_pill(x, py, pw, ph, cy_bd, cy_bg, "Y");
            adv = pw;
        }
        draw_text(font_small, yl, x + adv + 5, ty, SC_WHITE);
        x += adv + 5 + text_w(font_small, yl) + 14;
    }
    if (xl) {
        /* X button: Xbox blue */
        Uint32 cx_bg = RGBA(0x1A, 0x6B, 0xC4), cx_bd = RGBA(0x80, 0xBE, 0xF0);
        int adv;
        if (load_asset("xbox_button_color_x.png")) {
            draw_asset("xbox_button_color_x.png", x, hy + 4, icon_h, icon_h);
            adv = icon_h;
        } else {
            draw_btn_pill(x, py, pw, ph, cx_bd, cx_bg, "X");
            adv = pw;
        }
        draw_text(font_small, xl, x + adv + 5, ty, SC_WHITE);
        x += adv + 5 + text_w(font_small, xl) + 14;
    }
    if (l_lbl && r_lbl && lr_text) {
        int adv_l, adv_r;
        if (load_asset("prompt_l.png")) {
            draw_asset("prompt_l.png", x, hy + 4, icon_h, icon_h);
            adv_l = icon_h + 3;
        } else {
            int sw = 24, sh = 16;
            draw_shoulder_pill(x, hy + (HINT_H - sh) / 2, sw, sh, clr_bd, clr_bg, l_lbl);
            adv_l = sw + 3;
        }
        if (load_asset("prompt_r.png")) {
            draw_asset("prompt_r.png", x + adv_l, hy + 4, icon_h, icon_h);
            adv_r = icon_h + 3;
        } else {
            int sw = 24, sh = 16;
            draw_shoulder_pill(x + adv_l, hy + (HINT_H - sh) / 2, sw, sh, clr_bd, clr_bg, r_lbl);
            adv_r = sw + 3;
        }
        draw_text(font_small, lr_text, x + adv_l + adv_r, ty, SC_WHITE);
    }
}

__attribute__((unused))
static void draw_hints(const char *text) {
    draw_hint_base();
    (void)text;
    draw_hints_row("Select", "Back", "L", "R", "Page", NULL, NULL);
}

static void draw_home_hints(void) {
    draw_hint_base();
    draw_hints_row("Select", "Back", NULL, NULL, NULL, NULL, NULL);
    // Right side: d-pad up/down hint using PNG asset if available
    int hy = SCREEN_H - HINT_H;
    int ty = hy + (HINT_H - 16) / 2;
    int ax = SCREEN_W - 160;
    int icon_sz = HINT_H - 8;
    if (!draw_asset("xbox_dpad.png", ax, hy + 4, icon_sz, icon_sz)) {
        // Fallback: pixel-art up/down arrows
        Uint32 c_lr = RGBA(0x4B, 0x55, 0x68);
        Uint32 c_bd = RGBA(0xAE, 0xB7, 0xC5);
        int ay = hy + (HINT_H - 22) / 2;
        draw_shoulder_pill(ax, ay,      22, 11, c_bd, c_lr, "U");
        draw_shoulder_pill(ax, ay + 12, 22, 11, c_bd, c_lr, "D");
    }
    draw_text(font_small, "Navigate", ax + icon_sz + 6, ty, SC_WHITE);
}

// ── Home screen ───────────────────────────────────────────────────────────────

static void draw_home(void) {
    draw_textured_bg(0, CONTENT_Y, SCREEN_W, CONTENT_H);
    draw_status();
    draw_home_hints();

    int visible = HOME_VISIBLE;

    for (int row = 0; row < visible && home_offset + row < HOME_COUNT; row++) {
        int i   = home_offset + row;
        int iy  = CONTENT_Y + row * HOME_ITEM_H;
        int sel = (i == home_sel);

        if (sel) {
            draw_select_asset(6, iy + 3, SCREEN_W - 20, HOME_ITEM_H - 6);
        }
        if (!sel) {
            fill_rect(12, iy + HOME_ITEM_H - 1, SCREEN_W - 24, 1, C_SEP);
        }

        if (!draw_asset(HOME_ICONS[i], 14, iy + (HOME_ITEM_H - 82) / 2, 82, 82)) {
            draw_builtin_icon(HOME_ICONS[i], 14, iy + (HOME_ITEM_H - 82) / 2, 82, 82, sel);
        }

        draw_text(font_large, HOME_LABELS[i], 110, iy + (HOME_ITEM_H - 30) / 2, SC_TEXT);

        Uint32 chev_col = sel ? RGBA(0x3D, 0x2C, 0x88) : RGBA(0x5F, 0x66, 0x80);
        draw_chevron(SCREEN_W - 32, iy + HOME_ITEM_H / 2, 8, 2, chev_col);
    }

    draw_scrollbar(SCREEN_W - 10, CONTENT_Y + 8, CONTENT_H - 16,
                   HOME_COUNT, visible, home_offset);
}

// ── Two-panel (systems + games) ───────────────────────────────────────────────

static void draw_panel(void) {
    fill_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
    draw_status();
    draw_hint_base();
    if (state == STATE_GAMES)
        draw_hints_row("Launch", "Back", "L", "R", "Page", "Favorite", "Options");
    else
        draw_hints_row("Select", "Back", "L", "R", "Page", NULL, NULL);

    // ── Left: systems ──
    draw_panel_asset(6, CONTENT_Y + 6, LEFT_W - 10, CONTENT_H - 12);
    draw_panel_asset(LEFT_W + 4, CONTENT_Y + 6, SCREEN_W - LEFT_W - 10, CONTENT_H - 12);

    // Panel header — lavender gradient (#E8E3F6 → #D6CFEE), border #A99EDE
    fill_rect(10, CONTENT_Y + 10, LEFT_W - 18, PANEL_HDR_H / 2, C_PANEL_HI);
    fill_rect(10, CONTENT_Y + 10 + PANEL_HDR_H/2, LEFT_W - 18, PANEL_HDR_H - PANEL_HDR_H/2, C_PANEL_HDR);
    fill_rect(10, CONTENT_Y + 10 + PANEL_HDR_H - 1, LEFT_W - 18, 1, C_DIVIDER);
    draw_text(font_small, "SYSTEMS", 18, CONTENT_Y + 12, SC_HDR);

    // Clamp scroll
    if (sys_sel < sys_offset) sys_offset = sys_sel;
    if (sys_sel >= sys_offset + PANEL_ROWS) sys_offset = sys_sel - PANEL_ROWS + 1;

    int sy0 = CONTENT_Y + PANEL_HDR_H + 12;

    for (int i = 0; i < PANEL_ROWS && sys_offset + i < sys_count; i++) {
        int si  = sys_offset + i;
        int iy  = sy0 + i * ITEM_H;
        int sel = (si == sys_sel);

        if (sel) draw_select_asset(10, iy + 4, LEFT_W - 18, ITEM_H - 8);
        else fill_rect(10, iy + ITEM_H - 1, LEFT_W - 18, 1, C_SEP);

        // Both cream and lavender are light — always use dark navy text
        SDL_Color tc = SC_TEXT;

        // Truncate full system name to fit left panel (no icon)
        const char *full_name = system_full_name(systems[si].label);
        char label[48];
        truncate_to_fit(font_body, full_name, label, sizeof(label), LEFT_W - 50);
        draw_text(font_body, label, 18, iy + (ITEM_H - 22) / 2, tc);
        Uint32 chev_sys = sel ? RGBA(0x3D, 0x2C, 0x88) : RGBA(0x5F, 0x66, 0x80);
        draw_chevron(LEFT_W - 22, iy + ITEM_H / 2, 7, 2, chev_sys);
    }
    draw_scrollbar(LEFT_W - 12, sy0, PANEL_ROWS * ITEM_H, sys_count, PANEL_ROWS, sys_offset);

    // ── Divider ──
    fill_rect(LEFT_W, CONTENT_Y + 6, 2, CONTENT_H - 12, C_DIVIDER);

    // ── Right: games ──

    int rx = LEFT_W + 1;
    int rw = SCREEN_W - rx;

    // Right panel header — same lavender gradient
    fill_rect(rx + 8, CONTENT_Y + 10, rw - 16, PANEL_HDR_H / 2, C_PANEL_HI);
    fill_rect(rx + 8, CONTENT_Y + 10 + PANEL_HDR_H/2, rw - 16, PANEL_HDR_H - PANEL_HDR_H/2, C_PANEL_HDR);
    fill_rect(rx + 8, CONTENT_Y + 10 + PANEL_HDR_H - 1, rw - 16, 1, C_DIVIDER);
    char hdr[80];
    if (sys_count > 0)
        snprintf(hdr, sizeof(hdr), "GAMES — %s  (%d)", systems[sys_sel].label, game_count);
    else
        snprintf(hdr, sizeof(hdr), "GAMES");
    draw_text(font_small, hdr, rx + 16, CONTENT_Y + 12, SC_HDR);

    // Clamp scroll
    if (game_sel < game_offset) game_offset = game_sel;
    if (game_sel >= game_offset + GAME_ROWS) game_offset = game_sel - GAME_ROWS + 1;

    int gy0 = CONTENT_Y + PANEL_HDR_H + 12;

    if (game_count == 0) {
        draw_text(font_body, "No games found.", rx + 20, gy0 + 24, SC_DIM);
        draw_text(font_body, "Check that your ROMs are in the", rx + 20, gy0 + 52, SC_DIM);
        draw_text(font_body, "correct folder for this system.", rx + 20, gy0 + 74, SC_DIM);
    } else {
        for (int i = 0; i < GAME_ROWS && game_offset + i < game_count; i++) {
            int gi  = game_offset + i;
            int iy  = gy0 + i * GAME_ITEM_H;
            int sel = (state == STATE_GAMES) && (gi == game_sel);

            if (sel) draw_select_asset(rx + 10, iy + 4, rw - 20, GAME_ITEM_H - 8);
            else fill_rect(rx + 10, iy + GAME_ITEM_H - 1, rw - 20, 1, C_SEP);

            int fav = is_favorite(games[gi].path);
            int star_w = fav ? text_w(font_game, "\xe2\x98\x85") + 4 : 0;
            char line1[240], line2[240];
            wrap_text(font_game, games[gi].name,
                      line1, sizeof(line1), line2, sizeof(line2), rw - 32 - star_w);
            int ty = line2[0] ? iy + (GAME_ITEM_H - GAME_LINE_GAP - 22) / 2
                              : iy + (GAME_ITEM_H - 22) / 2;
            if (fav) {
                SDL_Color star_col = sel ? (SDL_Color){0xFF, 0xD7, 0x00, 0xFF}
                                        : (SDL_Color){0xC4, 0x9E, 0x1B, 0xFF};
                draw_text(font_game, "\xe2\x98\x85", rx + 14, ty, star_col);
            }
            draw_text(font_game, line1, rx + 14 + star_w, ty, SC_TEXT);
            if (line2[0])
                draw_text(font_game, line2, rx + 14 + star_w, ty + GAME_LINE_GAP, SC_TEXT);
        }
        draw_scrollbar(SCREEN_W - 14, gy0, GAME_ROWS * GAME_ITEM_H, game_count, GAME_ROWS, game_offset);
    }
}

static void draw_entry_list(const char *title, PlayEntry *entries, int count,
                            int *sel, int *offset, int show_star) {
    fill_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
    draw_status();
    draw_hint_base();
    draw_hints_row("Select", "Back", "L", "R", "Tabs", NULL,
                   count > 0 ? "Options" : NULL);

    draw_panel_asset(6, CONTENT_Y + 6, SCREEN_W - 12, CONTENT_H - 12);
    fill_rect(10, CONTENT_Y + 10, SCREEN_W - 20, PANEL_HDR_H, C_PANEL_HDR);
    draw_text(font_small, title, 22, CONTENT_Y + 15, SC_HDR);

    int rows = GAME_ROWS;
    if (*sel < *offset) *offset = *sel;
    if (*sel >= *offset + rows) *offset = *sel - rows + 1;

    int y0 = CONTENT_Y + PANEL_HDR_H + 12;
    if (count == 0) {
        const char *hint1 = NULL, *hint2 = NULL;
        if (strcmp(title, "Favorites") == 0) {
            hint1 = "No favorites yet.";
            hint2 = "Press Y on any game in the Library to add one.";
        } else if (strcmp(title, "Recent") == 0) {
            hint1 = "No recent games yet.";
            hint2 = "Play a game and it will appear here.";
        } else {
            hint1 = "Nothing here yet.";
        }
        draw_text(font_body, hint1, 24, y0 + 28, SC_DIM);
        if (hint2) draw_text(font_body, hint2, 24, y0 + 58, SC_DIM);
        return;
    }

    for (int i = 0; i < rows && *offset + i < count; i++) {
        int idx = *offset + i;
        int iy = y0 + i * GAME_ITEM_H;
        int is_sel = idx == *sel;
        if (is_sel) draw_select_asset(10, iy + 4, SCREEN_W - 20, GAME_ITEM_H - 8);
        else fill_rect(10, iy + GAME_ITEM_H - 1, SCREEN_W - 20, 1, C_SEP);

        // System tag on right, 2-line title on left
        draw_text(font_small, entries[idx].system, SCREEN_W - 54, iy + (GAME_ITEM_H - 14) / 2, SC_DIM);
        Uint32 chev_ent = is_sel ? RGBA(0x3D, 0x2C, 0x88) : RGBA(0x5F, 0x66, 0x80);
        draw_chevron(SCREEN_W - 22, iy + GAME_ITEM_H / 2, 7, 2, chev_ent);

        char line1[240], line2[240];
        int star_w = show_star ? text_w(font_game, "\xe2\x98\x85 ") : 0;  // UTF-8 ★
        int avail_w = SCREEN_W - 70 - star_w;
        wrap_text(font_game, entries[idx].label,
                  line1, sizeof(line1), line2, sizeof(line2), avail_w);
        int ty = line2[0] ? iy + (GAME_ITEM_H - GAME_LINE_GAP - 28) / 2
                          : iy + (GAME_ITEM_H - 28) / 2;
        int tx = 14;
        if (show_star) {
            SDL_Color star_col; SDL_GetRGB(C_SEL_BORDER, screen->format, &star_col.r, &star_col.g, &star_col.b); star_col.unused = 255;
            draw_text(font_game, "\xe2\x98\x85", tx, ty, star_col);
            tx += star_w;
        }
        draw_text(font_game, line1, tx, ty, SC_TEXT);
        if (line2[0])
            draw_text(font_game, line2, tx, ty + GAME_LINE_GAP, SC_TEXT);
    }
    draw_scrollbar(SCREEN_W - 14, y0, rows * GAME_ITEM_H, count, rows, *offset);
}

// ── Browse by genre ───────────────────────────────────────────────────────────

static void normalize_genre(const char *raw, char *out, int outlen) {
    if (!raw || !raw[0] || strcmp(raw, "Unsorted") == 0) {
        strncpy(out, "Unsorted", outlen - 1); out[outlen-1] = '\0'; return;
    }
    char buf[128];
    strncpy(buf, raw, sizeof(buf) - 1); buf[sizeof(buf)-1] = '\0';
    char *second = strchr(buf, ',');
    if (second) { *second = '\0'; second++; while (*second == ' ') second++; }
    const char *first = buf;

    if (strcmp(first, "Action") == 0 && second) {
        if (strstr(second, "Platformer"))  { strncpy(out, "Platformer",   outlen-1); goto done; }
        if (strstr(second, "Beat"))        { strncpy(out, "Beat 'em Up",  outlen-1); goto done; }
        if (strstr(second, "Fight"))       { strncpy(out, "Fighting",     outlen-1); goto done; }
        if (strstr(second, "Shoot"))       { strncpy(out, "Shooter",      outlen-1); goto done; }
    }
    if (strncmp(first, "Action Adventure", 16) == 0) { strncpy(out, "Action/Adventure", outlen-1); goto done; }
    if (strcmp(first, "Action") == 0)                { strncpy(out, "Action",           outlen-1); goto done; }
    if (strcmp(first, "Role-Playing") == 0 && second) {
        if (strstr(second, "Action"))  { strncpy(out, "Action RPG", outlen-1); goto done; }
        strncpy(out, "RPG", outlen-1); goto done;
    }
    if (strcmp(first, "Strategy") == 0)    { strncpy(out, "Strategy",   outlen-1); goto done; }
    if (strcmp(first, "Sports") == 0)      { strncpy(out, "Sports",     outlen-1); goto done; }
    if (strcmp(first, "Driving") == 0)     { strncpy(out, "Racing",     outlen-1); goto done; }
    if (strcmp(first, "Simulation") == 0)  { strncpy(out, "Simulation", outlen-1); goto done; }
    if (strcmp(first, "Miscellaneous") == 0 && second) {
        if (strstr(second, "Puzzle"))  { strncpy(out, "Puzzle",      outlen-1); goto done; }
        if (strstr(second, "Rhythm"))  { strncpy(out, "Rhythm",      outlen-1); goto done; }
        if (strstr(second, "Card") || strstr(second, "Board"))
                                       { strncpy(out, "Card & Board",outlen-1); goto done; }
        if (strstr(second, "Compil"))  { strncpy(out, "Compilation", outlen-1); goto done; }
        strncpy(out, "Misc", outlen-1); goto done;
    }
    strncpy(out, first, outlen-1);
done:
    out[outlen-1] = '\0';
}

static int browse_game_cmp(const void *a, const void *b) {
    const BrowseGame *ga = (const BrowseGame *)a;
    const BrowseGame *gb = (const BrowseGame *)b;
    int gc = strcmp(ga->genre, gb->genre);
    if (gc) return gc;
    return strcasecmp(ga->title, gb->title);
}

static void parse_miyoogamelist(const char *xml_path, const char *sys_folder) {
    FILE *f = fopen(xml_path, "r");
    if (!f) return;
    char line[1024];
    char cur_path[512] = {0};
    char cur_name[240] = {0};
    while (fgets(line, sizeof(line), f)) {
        char *p, *end;
        if ((p = strstr(line, "<path>"))) {
            p += 6; end = strstr(p, "</path>");
            if (end) { int n = (int)(end-p); if (n > 511) n = 511; strncpy(cur_path, p, n); cur_path[n] = 0; }
        }
        if ((p = strstr(line, "<name>"))) {
            p += 6; end = strstr(p, "</name>");
            if (end) { int n = (int)(end-p); if (n > 239) n = 239; strncpy(cur_name, p, n); cur_name[n] = 0; }
        }
        if ((p = strstr(line, "<genre>"))) {
            p += 7; end = strstr(p, "</genre>");
            if (!end || browse_game_count >= BROWSE_GAME_MAX) { cur_path[0] = cur_name[0] = 0; continue; }
            char raw[128] = {0};
            int n = (int)(end-p); if (n > 127) n = 127;
            strncpy(raw, p, n);
            BrowseGame *g = &browse_game_pool[browse_game_count++];
            strncpy(g->title,  cur_name[0] ? cur_name : cur_path, 239);
            strncpy(g->system, sys_folder, 23);
            const char *fname = cur_path;
            if (fname[0] == '.' && fname[1] == '/') fname += 2;
            snprintf(g->path, sizeof(g->path), ROMS_ROOT "/%s/%s", sys_folder, fname);
            normalize_genre(raw, g->genre, BROWSE_GENRE_LEN);
            cur_path[0] = cur_name[0] = 0;
        }
    }
    fclose(f);
}

static void load_browse_data(void) {
    browse_game_count  = 0;
    browse_genre_count = 0;

    DIR *d = opendir(ROMS_ROOT);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (ent->d_name[0] == '.') continue;
        char xml[512];
        snprintf(xml, sizeof(xml), ROMS_ROOT "/%s/miyoogamelist.xml", ent->d_name);
        parse_miyoogamelist(xml, ent->d_name);
    }
    closedir(d);

    if (browse_game_count == 0) return;

    qsort(browse_game_pool, browse_game_count, sizeof(BrowseGame), browse_game_cmp);

    // Build genre index
    const char *prev = "";
    for (int i = 0; i < browse_game_count; i++) {
        if (strcmp(browse_game_pool[i].genre, prev) != 0) {
            if (browse_genre_count >= BROWSE_GENRE_MAX) break;
            BrowseGenre *bg = &browse_genres[browse_genre_count++];
            strncpy(bg->label, browse_game_pool[i].genre, BROWSE_GENRE_LEN-1);
            bg->label[BROWSE_GENRE_LEN-1] = '\0';
            bg->start = i;
            bg->count = 1;
            prev = browse_game_pool[i].genre;
        } else {
            browse_genres[browse_genre_count-1].count++;
        }
    }
}

static void draw_browse(void) {
    fill_rect(0, CONTENT_Y, SCREEN_W, CONTENT_H, C_BG);
    draw_status();
    draw_hint_base();
    if (state == STATE_BROWSE_CATS)
        draw_hints_row("Select", "Back", "L", "R", "Page", NULL, NULL);
    else
        draw_hints_row("Launch", "Back", "L", "R", "Page", NULL, NULL);

    if (browse_genre_count == 0) {
        draw_text(font_body, "No genre data found.", 20, CONTENT_Y + 36, SC_DIM);
        draw_text(font_body, "Run the PocketOS Genre Scanner on your computer,", 20, CONTENT_Y + 64, SC_DIM);
        draw_text(font_body, "then point it at this SD card.", 20, CONTENT_Y + 88, SC_DIM);
        return;
    }

    draw_panel_asset(6,          CONTENT_Y + 6, LEFT_W - 10,              CONTENT_H - 12);
    draw_panel_asset(LEFT_W + 4, CONTENT_Y + 6, SCREEN_W - LEFT_W - 10,  CONTENT_H - 12);

    /* Left panel header */
    fill_rect(10, CONTENT_Y + 10, LEFT_W - 18, PANEL_HDR_H / 2, C_PANEL_HI);
    fill_rect(10, CONTENT_Y + 10 + PANEL_HDR_H/2, LEFT_W - 18, PANEL_HDR_H - PANEL_HDR_H/2, C_PANEL_HDR);
    fill_rect(10, CONTENT_Y + 10 + PANEL_HDR_H - 1, LEFT_W - 18, 1, C_DIVIDER);
    draw_text(font_small, "GENRES", 18, CONTENT_Y + 12, SC_HDR);

    if (browse_genre_sel < browse_genre_off) browse_genre_off = browse_genre_sel;
    if (browse_genre_sel >= browse_genre_off + PANEL_ROWS) browse_genre_off = browse_genre_sel - PANEL_ROWS + 1;

    int sy0 = CONTENT_Y + PANEL_HDR_H + 12;
    for (int i = 0; i < PANEL_ROWS && browse_genre_off + i < browse_genre_count; i++) {
        int gi  = browse_genre_off + i;
        int iy  = sy0 + i * ITEM_H;
        int sel = (gi == browse_genre_sel);
        if (sel && state == STATE_BROWSE_CATS)
            draw_select_asset(10, iy + 4, LEFT_W - 18, ITEM_H - 8);
        else if (sel)
            fill_rect(10, iy + 4, LEFT_W - 18, ITEM_H - 8, C_SEL);
        else
            fill_rect(10, iy + ITEM_H - 1, LEFT_W - 18, 1, C_SEP);
        char label[48];
        truncate_to_fit(font_body, browse_genres[gi].label, label, sizeof(label), LEFT_W - 50);
        draw_text(font_body, label, 18, iy + (ITEM_H - 22) / 2, SC_TEXT);
        char cnt[10];
        snprintf(cnt, sizeof(cnt), "%d", browse_genres[gi].count);
        draw_text(font_small, cnt, LEFT_W - 28 - text_w(font_small, cnt),
                  iy + (ITEM_H - 14) / 2, SC_DIM);
        Uint32 chev = (sel && state == STATE_BROWSE_CATS) ? C_SEL_BORDER : C_SEP;
        draw_chevron(LEFT_W - 22, iy + ITEM_H / 2, 7, 2, chev);
    }
    draw_scrollbar(LEFT_W - 12, sy0, PANEL_ROWS * ITEM_H,
                   browse_genre_count, PANEL_ROWS, browse_genre_off);

    fill_rect(LEFT_W, CONTENT_Y + 6, 2, CONTENT_H - 12, C_DIVIDER);

    /* Right panel */
    int rx = LEFT_W + 1;
    int rw = SCREEN_W - rx;

    fill_rect(rx + 8, CONTENT_Y + 10, rw - 16, PANEL_HDR_H / 2, C_PANEL_HI);
    fill_rect(rx + 8, CONTENT_Y + 10 + PANEL_HDR_H/2, rw - 16, PANEL_HDR_H - PANEL_HDR_H/2, C_PANEL_HDR);
    fill_rect(rx + 8, CONTENT_Y + 10 + PANEL_HDR_H - 1, rw - 16, 1, C_DIVIDER);
    char hdr[80];
    snprintf(hdr, sizeof(hdr), "%s  (%d)",
             browse_genres[browse_genre_sel].label,
             browse_genres[browse_genre_sel].count);
    draw_text(font_small, hdr, rx + 16, CONTENT_Y + 12, SC_HDR);

    BrowseGenre *bg = &browse_genres[browse_genre_sel];
    if (browse_game_sel < browse_game_off) browse_game_off = browse_game_sel;
    if (browse_game_sel >= browse_game_off + GAME_ROWS) browse_game_off = browse_game_sel - GAME_ROWS + 1;

    int gy0 = CONTENT_Y + PANEL_HDR_H + 12;
    for (int row = 0; row < GAME_ROWS && browse_game_off + row < bg->count; row++) {
        int gi  = bg->start + browse_game_off + row;
        int iy  = gy0 + row * GAME_ITEM_H;
        int sel = (state == STATE_BROWSE_GAMES) && (browse_game_off + row == browse_game_sel);
        if (sel) draw_select_asset(rx + 10, iy + 4, rw - 20, GAME_ITEM_H - 8);
        else     fill_rect(rx + 10, iy + GAME_ITEM_H - 1, rw - 20, 1, C_SEP);
        char line1[240], line2[240];
        wrap_text(font_game, browse_game_pool[gi].title,
                  line1, sizeof(line1), line2, sizeof(line2), rw - 32);
        int title_block = line2[0] ? GAME_LINE_GAP + 28 + 4 + 14 : 28 + 4 + 14;
        int ty = iy + (GAME_ITEM_H - title_block) / 2;
        draw_text(font_game, line1, rx + 14, ty, SC_TEXT);
        if (line2[0])
            draw_text(font_game, line2, rx + 14, ty + GAME_LINE_GAP, SC_TEXT);
        int sys_y = ty + (line2[0] ? GAME_LINE_GAP + 28 + 4 : 28 + 4);
        draw_text(font_small, browse_game_pool[gi].system, rx + 14, sys_y, SC_DIM);
    }
    if (bg->count > 0)
        draw_scrollbar(SCREEN_W - 14, gy0, GAME_ROWS * GAME_ITEM_H,
                       bg->count, GAME_ROWS, browse_game_off);
}

/* Keep these as thin wrappers so render dispatch still compiles */
static void draw_browse_cats(void)  { draw_browse(); }
static void draw_browse_games(void) { draw_browse(); }

static void on_browse_cats_key(SDLKey k) {
    int before = browse_genre_sel;
    if (k == BTN_UP   && browse_genre_sel > 0)                    browse_genre_sel--;
    if (k == BTN_DOWN && browse_genre_sel < browse_genre_count-1) browse_genre_sel++;
    if (k == BTN_L1)  browse_genre_sel = (browse_genre_sel - PANEL_ROWS < 0) ? 0 : browse_genre_sel - PANEL_ROWS;
    if (k == BTN_R1)  browse_genre_sel = (browse_genre_sel + PANEL_ROWS >= browse_genre_count) ? browse_genre_count-1 : browse_genre_sel + PANEL_ROWS;
    if (browse_genre_sel != before) { play_move(); browse_game_sel = 0; browse_game_off = 0; }
    if (k == BTN_A || k == BTN_RIGHT) {
        play_select();
        browse_game_sel = 0; browse_game_off = 0;
        state = STATE_BROWSE_GAMES;
    }
    if (k == BTN_B || k == BTN_LEFT || k == BTN_MENU) {
        play_back(); state = STATE_HOME;
    }
}

static void on_browse_games_key(SDLKey k) {
    if (browse_genre_sel < 0 || browse_genre_sel >= browse_genre_count) { state = STATE_BROWSE_CATS; return; }
    BrowseGenre *bg = &browse_genres[browse_genre_sel];
    int before = browse_game_sel;
    if (k == BTN_UP   && browse_game_sel > 0)            browse_game_sel--;
    if (k == BTN_DOWN && browse_game_sel < bg->count-1)  browse_game_sel++;
    if (k == BTN_L1)  browse_game_sel = (browse_game_sel - GAME_ROWS < 0) ? 0 : browse_game_sel - GAME_ROWS;
    if (k == BTN_R1)  browse_game_sel = (browse_game_sel + GAME_ROWS >= bg->count) ? bg->count-1 : browse_game_sel + GAME_ROWS;
    if (browse_game_sel < browse_game_off) browse_game_off = browse_game_sel;
    if (browse_game_sel >= browse_game_off + GAME_ROWS)  browse_game_off = browse_game_sel - GAME_ROWS + 1;
    if (browse_game_sel != before) play_move();
    if (k == BTN_A || k == BTN_RIGHT) {
        int gi = bg->start + browse_game_sel;
        BrowseGame *g = &browse_game_pool[gi];
        /* Find the matching system by ROM folder name, not display label */
        System *sys = NULL;
        for (int si = 0; si < sys_count; si++) {
            const char *rbase = strrchr(systems[si].rom_dir, '/');
            rbase = rbase ? rbase + 1 : systems[si].rom_dir;
            if (strcasecmp(rbase, g->system) == 0) {
                sys = &systems[si]; break;
            }
        }
        if (!sys) { log_kv("browse launch: no system match for", g->system); return; }
        PlayEntry pe = {0};
        strncpy(pe.label,   g->title,  sizeof(pe.label)-1);
        strncpy(pe.rompath, g->path,   sizeof(pe.rompath)-1);
        strncpy(pe.system,  g->system, sizeof(pe.system)-1);
        snprintf(pe.launch, sizeof(pe.launch), "%s/launch.sh", sys->emu_dir);
        launch_entry(&pe);
    }
    if (k == BTN_B || k == BTN_LEFT || k == BTN_MENU) {
        play_back(); state = STATE_BROWSE_CATS;
    }
}

static void draw_apps(void) {
    draw_textured_bg(0, CONTENT_Y, SCREEN_W, CONTENT_H);
    draw_status();
    draw_hint_base();
    draw_hints_row("Open", "Back", NULL, NULL, NULL, NULL, NULL);

    /* clamp scroll */
    if (app_sel < app_offset) app_offset = app_sel;
    if (app_sel >= app_offset + HOME_VISIBLE) app_offset = app_sel - HOME_VISIBLE + 1;

    for (int row = 0; row < HOME_VISIBLE && app_offset + row < APP_COUNT; row++) {
        int i   = app_offset + row;
        int iy  = CONTENT_Y + row * HOME_ITEM_H;
        int sel = (i == app_sel);

        if (sel)
            draw_select_asset(6, iy + 3, SCREEN_W - 20, HOME_ITEM_H - 6);
        else
            fill_rect(12, iy + HOME_ITEM_H - 1, SCREEN_W - 24, 1, C_SEP);

        if (!draw_asset(APP_ENTRIES[i].icon, 14, iy + (HOME_ITEM_H - 82) / 2, 82, 82))
            draw_builtin_icon(APP_ENTRIES[i].icon, 14, iy + (HOME_ITEM_H - 82) / 2, 82, 82, sel);

        draw_text(font_large, APP_ENTRIES[i].label, 110, iy + (HOME_ITEM_H - 30) / 2, SC_TEXT);

        Uint32 chev_col = sel ? RGBA(0x3D, 0x2C, 0x88) : RGBA(0x5F, 0x66, 0x80);
        draw_chevron(SCREEN_W - 32, iy + HOME_ITEM_H / 2, 8, 2, chev_col);
    }

    draw_scrollbar(SCREEN_W - 10, CONTENT_Y + 8, CONTENT_H - 16,
                   APP_COUNT, HOME_VISIBLE, app_offset);
}

// ── Info panel (Device / About) ───────────────────────────────────────────────

static void read_first_line(const char *path, char *out, int outlen) {
    out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, outlen, f)) {
        int n = strlen(out);
        while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' || out[n-1] == ' '))
            out[--n] = '\0';
    }
    fclose(f);
}

static void draw_info_row(int x, int y, int w, const char *label, const char *value) {
    draw_text(font_body, label, x, y, SC_DIM);
    int vw = text_w(font_body, value);
    draw_text(font_body, value, x + w - vw, y, SC_TEXT);
}

// ── Game Options panel ─────────────────────────────────────────────────────────

#define GOPTS_LAUNCH    0
#define GOPTS_FAVORITE  1
#define GOPTS_ROM_INFO  2
#define GOPTS_SAVE_INFO 3
#define GOPTS_COUNT     4

static void enter_game_options(const char *name, const char *path,
                               const char *launch, const char *system,
                               State back_state) {
    strncpy(game_opts_name,   name,   sizeof(game_opts_name)   - 1);
    strncpy(game_opts_path,   path,   sizeof(game_opts_path)   - 1);
    strncpy(game_opts_launch, launch, sizeof(game_opts_launch) - 1);
    strncpy(game_opts_system, system, sizeof(game_opts_system) - 1);
    game_opts_name[sizeof(game_opts_name)-1]     = '\0';
    game_opts_path[sizeof(game_opts_path)-1]     = '\0';
    game_opts_launch[sizeof(game_opts_launch)-1] = '\0';
    game_opts_system[sizeof(game_opts_system)-1] = '\0';
    game_opts_sel  = 0;
    game_opts_mode = 0;
    game_opts_back = back_state;
    state = STATE_GAME_OPTIONS;
    play_select();
}

static void draw_game_options(void) {
    /* Render background state behind the overlay */
    switch (game_opts_back) {
    case STATE_GAMES:
        draw_panel();
        break;
    case STATE_FAVORITES:
        draw_entry_list("Favorites", favorite_entries, favorite_count,
                        &favorite_sel, &favorite_offset, 1);
        break;
    case STATE_RECENT:
        draw_entry_list("Recent", recent_entries, recent_count,
                        &recent_sel, &recent_offset, 0);
        break;
    default:
        draw_panel();
        break;
    }

    /* Dim content behind the card */
    fill_rect_alpha(0, 0, SCREEN_W, SCREEN_H, 140);

    int cw  = 520;
    int ch  = (game_opts_mode == 0) ? 300 : 340;
    int cx  = (SCREEN_W - cw) / 2;
    int cy  = (SCREEN_H - ch) / 2;
    int pad = 20;
    int inner_x = cx + pad;
    int inner_w = cw - pad * 2;

    draw_panel_asset(cx, cy, cw, ch);

    /* Header band with game title */
    fill_rrect(cx, cy, cw, 52, 4, C_SEL_BORDER);
    char hdr[80];
    strncpy(hdr, game_opts_name, sizeof(hdr) - 1);
    hdr[sizeof(hdr)-1] = '\0';
    while (strlen(hdr) > 4 && text_w(font_body, hdr) > inner_w - 8) {
        hdr[strlen(hdr)-1] = '\0';
    }
    if (strlen(hdr) < strlen(game_opts_name) && strlen(hdr) > 3) {
        hdr[strlen(hdr)-3] = '\0';
        strcat(hdr, "...");
    }
    draw_text_center(font_body, hdr, cx, cw, cy + 16, SC_WHITE);

    fill_rect(cx + 12, cy + 56, cw - 24, 1, C_SEP);

    if (game_opts_mode == 0) {
        /* ── Action menu ── */
        draw_hint_base();
        draw_hints_row("Select", "Back", NULL, NULL, NULL, NULL, NULL);

        const char *items[GOPTS_COUNT];
        items[GOPTS_LAUNCH]   = "Launch";
        items[GOPTS_FAVORITE] = is_favorite(game_opts_path)
                                ? "\xe2\x98\x85  Unfavorite"
                                : "\xe2\x98\x86  Add to Favorites";
        items[GOPTS_ROM_INFO]  = "ROM Info";
        items[GOPTS_SAVE_INFO] = "Save Info";

        int row_h = 52;
        int ry = cy + 68;
        for (int i = 0; i < GOPTS_COUNT; i++) {
            int sel = (i == game_opts_sel);
            if (sel) {
                fill_rrect(inner_x - 6, ry + 4, inner_w + 12, row_h - 8,
                           3, C_SEL_BORDER);
                fill_rrect(inner_x - 5, ry + 5, inner_w + 10, row_h - 10,
                           2, C_SEL_HI);
            }
            SDL_Color col = sel ? SC_WHITE : SC_TEXT;
            draw_text(font_body, items[i], inner_x + 8, ry + (row_h - 22) / 2, col);
            ry += row_h;
        }

    } else if (game_opts_mode == 1) {
        /* ── ROM Info ── */
        draw_hint_base();
        draw_hints_row(NULL, "Back", NULL, NULL, NULL, NULL, NULL);

        int row_h = 40;
        int ry = cy + 68;

        const char *slash = strrchr(game_opts_path, '/');
        const char *fname = slash ? slash + 1 : game_opts_path;
        draw_info_row(inner_x, ry, inner_w, "File", fname);
        ry += row_h;

        struct stat st;
        if (stat(game_opts_path, &st) == 0) {
            char sizebuf[32];
            if (st.st_size >= 1024 * 1024)
                snprintf(sizebuf, sizeof(sizebuf), "%.1f MB",
                         (double)st.st_size / (1024.0 * 1024.0));
            else
                snprintf(sizebuf, sizeof(sizebuf), "%ld KB",
                         (long)st.st_size / 1024);
            draw_info_row(inner_x, ry, inner_w, "Size", sizebuf);
            ry += row_h;

            char tmbuf[32];
            struct tm *tm_info = localtime(&st.st_mtime);
            strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d", tm_info);
            draw_info_row(inner_x, ry, inner_w, "Modified", tmbuf);
            ry += row_h;
        } else {
            draw_info_row(inner_x, ry, inner_w, "Status", "File not found");
            ry += row_h;
        }

        /* Truncated path */
        char pathbuf[64];
        int plen = strlen(game_opts_path);
        if (plen > 48)
            snprintf(pathbuf, sizeof(pathbuf), "...%s", game_opts_path + plen - 45);
        else {
            strncpy(pathbuf, game_opts_path, sizeof(pathbuf) - 1);
            pathbuf[sizeof(pathbuf)-1] = '\0';
        }
        draw_info_row(inner_x, ry, inner_w, "Path", pathbuf);

    } else {
        /* ── Save Info ── */
        draw_hint_base();
        draw_hints_row(NULL, "Back", NULL, NULL, NULL, NULL, NULL);

        int row_h = 40;
        int ry = cy + 68;

        char saves_dir[512];
        snprintf(saves_dir, sizeof(saves_dir),
                 POCKETOS_ROOT "/Saves/%s", game_opts_system);

        const char *sl = strrchr(game_opts_path, '/');
        char rombase[240];
        strncpy(rombase, sl ? sl + 1 : game_opts_path, sizeof(rombase) - 1);
        rombase[sizeof(rombase)-1] = '\0';
        char *dot = strrchr(rombase, '.');
        if (dot) *dot = '\0';

        DIR *dp = opendir(saves_dir);
        int found = 0;
        if (dp) {
            struct dirent *ent;
            while ((ent = readdir(dp)) != NULL && found < 4) {
                int rlen = strlen(rombase);
                if (strncmp(ent->d_name, rombase, rlen) == 0 &&
                    ent->d_name[rlen] != '\0' &&
                    ent->d_name[0] != '.') {
                    char save_path[600];
                    snprintf(save_path, sizeof(save_path),
                             "%s/%s", saves_dir, ent->d_name);
                    struct stat st;
                    if (stat(save_path, &st) == 0) {
                        char tmbuf[32];
                        struct tm *tm_info = localtime(&st.st_mtime);
                        strftime(tmbuf, sizeof(tmbuf), "%Y-%m-%d %H:%M", tm_info);
                        draw_info_row(inner_x, ry, inner_w, ent->d_name, tmbuf);
                        ry += row_h;
                        found++;
                    }
                }
            }
            closedir(dp);
        }
        if (!found) {
            draw_text_center(font_body, "No saves found.", cx, cw,
                             ry + 10, SC_DIM);
        }
    }
}

static void on_game_options_key(SDLKey k) {
    if (game_opts_mode != 0) {
        if (k == BTN_B || k == BTN_MENU) {
            game_opts_mode = 0;
            play_back();
        }
        return;
    }
    if (k == BTN_UP   && game_opts_sel > 0)             game_opts_sel--;
    if (k == BTN_DOWN && game_opts_sel < GOPTS_COUNT-1) game_opts_sel++;
    if (k == BTN_A) {
        switch (game_opts_sel) {
        case GOPTS_LAUNCH:
            if (game_opts_back == STATE_GAMES) {
                launch_game(sys_sel, game_sel);
            } else {
                PlayEntry tmp;
                memset(&tmp, 0, sizeof(tmp));
                strncpy(tmp.label,   game_opts_name,   sizeof(tmp.label)-1);
                strncpy(tmp.rompath, game_opts_path,   sizeof(tmp.rompath)-1);
                strncpy(tmp.launch,  game_opts_launch, sizeof(tmp.launch)-1);
                strncpy(tmp.system,  game_opts_system, sizeof(tmp.system)-1);
                launch_entry(&tmp);
            }
            break;
        case GOPTS_FAVORITE:
            toggle_favorite(game_opts_name, game_opts_path, game_opts_launch);
            play_select();
            break;
        case GOPTS_ROM_INFO:
            game_opts_mode = 1;
            play_select();
            break;
        case GOPTS_SAVE_INFO:
            game_opts_mode = 2;
            play_select();
            break;
        }
    }
    if (k == BTN_B || k == BTN_MENU) {
        play_back();
        state = game_opts_back;
        game_opts_mode = 0;
    }
}

static void draw_info_panel(void) {
    /* Dim the settings list behind the panel */
    fill_rect_alpha(0, 0, SCREEN_W, SCREEN_H, 140);
    draw_status();

    /* Card geometry */
    int cw = 520, ch = 300;
    int cx = (SCREEN_W - cw) / 2;
    int cy = (SCREEN_H - ch) / 2;
    int pad = 20;
    int inner_x = cx + pad;
    int inner_w = cw - pad * 2;
    int row_h   = 38;

    /* Card background */
    draw_panel_asset(cx, cy, cw, ch);

    /* Header band */
    fill_rrect(cx, cy, cw, 52, 4, C_SEL_BORDER);
    const char *title = info_panel_about ? "POCKET OS" : "DEVICE INFO";
    draw_text_center(font_large, title, cx, cw, cy + 12, SC_WHITE);

    /* Separator */
    fill_rect(cx + 12, cy + 56, cw - 24, 1, C_SEP);

    int ry = cy + 68;

    if (!info_panel_about) {
        /* ── Device / Miyoo info ── */
        char model_s[32], fw[64], onion[64], kernel[128];

        /* Model */
        FILE *f = fopen("/tmp/deviceModel", "r");
        int model = 0;
        if (f) { if (fscanf(f, "%d", &model) != 1) model = 0; fclose(f); }
        snprintf(model_s, sizeof(model_s), "%s",
                 model == 354 ? "Miyoo Mini Plus" :
                 model == 283 ? "Miyoo Mini" : "Miyoo (unknown)");

        /* Firmware */
        read_first_line("/tmp/firmwareVersion", fw, sizeof(fw));
        if (!fw[0]) read_first_line(POCKETOS_ROOT "/miyoo/app/version", fw, sizeof(fw));
        if (!fw[0]) snprintf(fw, sizeof(fw), "Unknown");

        /* Onion OS version */
        read_first_line(POCKETOS_ROOT "/.tmp_update/onion-version", onion, sizeof(onion));
        if (!onion[0]) read_first_line(POCKETOS_ROOT "/onion-version", onion, sizeof(onion));
        if (!onion[0]) snprintf(onion, sizeof(onion), "Unknown");

        /* Kernel (first word after "version" in /proc/version) */
        char proc_ver[256];
        read_first_line("/proc/version", proc_ver, sizeof(proc_ver));
        kernel[0] = '\0';
        /* extract version token: "Linux version X.Y.Z-..." */
        char *vp = strstr(proc_ver, "version ");
        if (vp) {
            vp += 8;
            int ki = 0;
            while (*vp && *vp != ' ' && ki < (int)sizeof(kernel) - 1)
                kernel[ki++] = *vp++;
            kernel[ki] = '\0';
        }
        if (!kernel[0]) snprintf(kernel, sizeof(kernel), "Unknown");

        draw_info_row(inner_x, ry,            inner_w, "Model",     model_s);
        draw_info_row(inner_x, ry + row_h,    inner_w, "Firmware",  fw);
        draw_info_row(inner_x, ry + row_h*2,  inner_w, "Onion OS",  onion);
        draw_info_row(inner_x, ry + row_h*3,  inner_w, "Kernel",    kernel);

    } else {
        /* ── Pocket OS about ── */
        char theme_label[64] = "Default";
        char font_label[64]  = "Default";

        if (theme_pick_sel >= 0 && theme_pick_sel < theme_list_count) {
            strncpy(theme_label, theme_list_name[theme_pick_sel], sizeof(theme_label) - 1);
            theme_label[sizeof(theme_label)-1] = '\0';
            char *dot = strrchr(theme_label, '.');
            if (dot) *dot = '\0';
            char *p = theme_label;
            if (strncmp(p, "theme_", 6) == 0) p += 6;
            if (*p >= 'a' && *p <= 'z') *p -= 32;
            memmove(theme_label, p, strlen(p) + 1);
        }
        if (font_pick_sel >= 0 && font_pick_sel < font_list_count) {
            strncpy(font_label, font_list_name[font_pick_sel], sizeof(font_label) - 1);
            font_label[sizeof(font_label)-1] = '\0';
            char *dot = strrchr(font_label, '.');
            if (dot) *dot = '\0';
        }

        draw_info_row(inner_x, ry,           inner_w, "Version",  "1.0");
        draw_info_row(inner_x, ry + row_h,   inner_w, "Platform", "Miyoo Mini+");
        draw_info_row(inner_x, ry + row_h*2, inner_w, "Theme",    theme_label);
        draw_info_row(inner_x, ry + row_h*3, inner_w, "Font",     font_label);
    }

    /* Bottom separator + hint */
    fill_rect(cx + 12, cy + ch - 40, cw - 24, 1, C_SEP);
    draw_text_center(font_small, "B  Back", cx, cw, cy + ch - 26, SC_DIM);
}

static void on_info_panel_key(SDLKey k) {
    if (k == BTN_B || k == BTN_MENU || k == BTN_A) {
        play_back();
        state = STATE_SETTINGS;
    }
}

static void draw_settings(void) {
    draw_textured_bg(0, CONTENT_Y, SCREEN_W, CONTENT_H);
    draw_status();
    draw_hint_base();
    draw_hints_row("Change", "Back", "L", "R", "Adjust", NULL, NULL);

    int clip_top    = CONTENT_Y;
    int clip_bottom = CONTENT_Y + CONTENT_H;

    /* Clip all item drawing to the content region so icons/text
     * never bleed into the status bar or hint bar when scrolling. */
    SDL_Rect content_clip = { 0, CONTENT_Y, SCREEN_W, CONTENT_H };
    SDL_SetClipRect(screen, &content_clip);

    /* Walk all rows; skip those fully outside the viewport */
    for (int i = 0; i < SETTINGS_COUNT; i++) {
        int rh  = settings_row_h(i);
        int ry  = CONTENT_Y + settings_row_top(i) - settings_scroll_px;

        if (ry + rh <= clip_top)    continue;  /* above viewport */
        if (ry      >= clip_bottom) break;     /* below viewport */

        if (SETTINGS_ENTRIES[i].is_header) {
            /* Section header: lavender band + small-caps label */
            int band_y = ry;
            int band_h = rh;
            if (band_y < clip_top)            { band_h -= clip_top - band_y; band_y = clip_top; }
            if (band_y + band_h > clip_bottom) band_h = clip_bottom - band_y;
            fill_rect(0, band_y, SCREEN_W, band_h, C_PANEL_HI);
            fill_rect(0, band_y + band_h - 1, SCREEN_W, 1, C_DIVIDER);
            /* Draw label only if top of text is visible */
            int lbl_y = ry + (rh - 14) / 2;
            if (lbl_y >= clip_top && lbl_y + 14 <= clip_bottom)
                draw_text(font_small, SETTINGS_ENTRIES[i].label, 14, lbl_y, SC_HDR);
        } else {
            int is_sel = (i == settings_sel);
            if (is_sel)
                draw_select_asset(6, ry + 3, SCREEN_W - 20, HOME_ITEM_H - 6);
            else
                fill_rect(12, ry + HOME_ITEM_H - 1, SCREEN_W - 24, 1, C_SEP);

            int icon_y = ry + (HOME_ITEM_H - 72) / 2;
            if (SETTINGS_ENTRIES[i].icon) {
                if (!draw_asset(SETTINGS_ENTRIES[i].icon, 14, icon_y, 72, 72))
                    draw_builtin_icon(SETTINGS_ENTRIES[i].icon, 14, icon_y, 72, 72, is_sel);
            }

            draw_text(font_large, SETTINGS_ENTRIES[i].label,
                      110, ry + (HOME_ITEM_H - 30) / 2, SC_TEXT);

            char value[64];
            settings_value(&SETTINGS_ENTRIES[i], value, sizeof(value));
            SDL_Color value_col = is_sel ? SC_TEXT : SC_DIM;

            /* Right panel: x=450 to x=600 (150px), chevron at 608 */
            int rp_x = 450;
            int rp_w = SCREEN_W - rp_x - 40;   /* 150px content width */
            int mid  = ry + HOME_ITEM_H / 2;

            int mv = setting_max_val(SETTINGS_ENTRIES[i].kind);
            if (mv > 0) {
                /* Numeric: value text (font_body) above, wide progress bar below */
                int cv  = setting_cur_val(SETTINGS_ENTRIES[i].kind);
                int vw  = text_w(font_body, value);
                int vy  = mid - 26;   /* upper half — text sits above center */
                int by  = mid + 6;    /* lower half — bar sits below center */
                int bh  = 10;

                draw_text(font_body, value, rp_x + rp_w - vw, vy, value_col);

                /* Track: thin flat line */
                int mid_y = by + bh / 2;
                fill_rrect(rp_x, mid_y - 2, rp_w, 4, 2, C_SEP);
                /* Fill: wedge via scanlines — smooth diagonal edges, no staircase.
                 * Wedge left tip = wmin px tall, right edge = bh px tall.
                 * For each row, find the x where the diagonal edge begins. */
                int fw = (rp_w * cv) / mv;
                if (fw > 0) {
                    int wmin = 3;
                    int half_max = bh / 2;
                    int half_min = wmin / 2;
                    for (int row = 0; row < bh; row++) {
                        int dy = row - half_max;                /* signed dist from centre */
                        int ady = dy < 0 ? -dy : dy;
                        /* x where this row enters the wedge (left diagonal edge) */
                        int x0 = 0;
                        if (ady > half_min) {
                            /* linear interp: at x=0 half-height=half_min, at x=fw half-height=half_max */
                            x0 = (fw * (ady - half_min)) / (half_max - half_min + 1);
                        }
                        if (x0 >= fw) continue;
                        fill_rect(rp_x + x0, mid_y - half_max + row, fw - x0, 1, C_SEL_BORDER);
                    }
                }
            } else {
                /* Toggle / text: single large value, vertically centred */
                int vw = text_w(font_body, value);
                draw_text(font_body, value, rp_x + rp_w - vw,
                          mid - 11, value_col);
            }

            Uint32 chev_col = is_sel ? RGBA(0x3D, 0x2C, 0x88) : RGBA(0x5F, 0x66, 0x80);
            draw_chevron(SCREEN_W - 32, mid, 8, 2, chev_col);
        }
    }

    SDL_SetClipRect(screen, NULL);  /* restore full-screen clip */

    /* Scrollbar: thumb position based on pixel scroll */
    int total_h = total_settings_height();
    int max_scroll = total_h - CONTENT_H;
    if (max_scroll > 0) {
        int track_h = CONTENT_H - 16;
        int thumb_h = (track_h * CONTENT_H) / total_h;
        if (thumb_h < 14) thumb_h = 14;
        int travel = track_h - thumb_h;
        int thumb_y = CONTENT_Y + 8 + (travel * settings_scroll_px) / max_scroll;
        fill_rect(SCREEN_W - 10, CONTENT_Y + 8, 4, track_h, C_SEP);
        fill_rrect(SCREEN_W - 10, thumb_y, 4, thumb_h, 2, C_SEL_BORDER);
    }
}

// ── Render ────────────────────────────────────────────────────────────────────

static void render(void) {
    switch (state) {
    case STATE_HOME:
        draw_home();
        break;
    case STATE_SYSTEMS:
    case STATE_GAMES:
        draw_panel();
        break;
    case STATE_RECENT:
        draw_entry_list("Recent", recent_entries, recent_count, &recent_sel, &recent_offset, 0);
        break;
    case STATE_FAVORITES:
        draw_entry_list("Favorites", favorite_entries, favorite_count, &favorite_sel, &favorite_offset, 1);
        break;
    case STATE_APPS:
        draw_apps();
        break;
    case STATE_SETTINGS:
        draw_settings();
        break;
    case STATE_FONT_PICKER:
        draw_font_picker();
        break;
    case STATE_THEME_PICKER:
        draw_theme_picker();
        break;
    case STATE_BROWSE_CATS:
        draw_browse_cats();
        break;
    case STATE_BROWSE_GAMES:
        draw_browse_games();
        break;
    case STATE_INFO_PANEL:
        draw_settings();   /* settings list behind the card */
        draw_info_panel();
        break;
    case STATE_GAME_OPTIONS:
        draw_game_options();
        break;
    }
    draw_screenshot_toast();
    SDL_BlitSurface(screen, NULL, video, NULL);
    SDL_Flip(video);
#ifdef POCKETOS_SCREENSHOT
    { static int _saved = 0; if (!_saved) { SDL_SaveBMP(screen, POCKETOS_SCREENSHOT); _saved = 1; } }
#endif
}

// ── Input ─────────────────────────────────────────────────────────────────────

static void on_home_key(SDLKey k) {
    int before = home_sel;
    if ((k == BTN_UP   || k == BTN_L1) && home_sel > 0)              home_sel--;
    if ((k == BTN_DOWN || k == BTN_R1) && home_sel < HOME_COUNT - 1) home_sel++;
    // keep selection visible
    if (home_sel < home_offset) home_offset = home_sel;
    if (home_sel >= home_offset + HOME_VISIBLE) home_offset = home_sel - HOME_VISIBLE + 1;
    if (home_sel != before) play_move();
    if (k == BTN_A || k == BTN_RIGHT) {
        play_select();
        switch (home_sel) {
        case 0: // Favorites
            load_favorites();
            state = STATE_FAVORITES;
            break;
        case 1: // Recent
            load_recent();
            state = STATE_RECENT;
            break;
        case 2: // Library
            state = STATE_SYSTEMS;
            break;
        case 3: // Browse
            if (browse_genre_count == 0) {
                LogTimer _t = log_timer_begin("load_browse_data");
                load_browse_data();
                log_timer_end(_t);
            }
            browse_genre_sel = 0; browse_genre_off = 0;
            state = STATE_BROWSE_CATS;
            break;
        case 4: // Apps
            state = STATE_APPS;
            break;
        case 5: // Settings
            open_settings_kind("display");
            break;
        case 6: // Sleep
            exec_power_cmd("echo mem > /sys/power/state");
            break;
        }
    }
}

static void on_entry_key(SDLKey k, PlayEntry *entries, int count, int *sel,
                         int *offset, State back_state) {
    if (count <= 0) {
        if (k == BTN_B || k == BTN_LEFT || k == BTN_MENU) {
            play_back();
            state = STATE_HOME;
        }
        return;
    }
    int before = *sel;
    if (k == BTN_UP) *sel = (*sel - 1 + count) % count;
    if (k == BTN_DOWN) *sel = (*sel + 1) % count;
    if (k == BTN_L1) *sel = (*sel - GAME_ROWS < 0) ? 0 : *sel - GAME_ROWS;
    if (k == BTN_R1) *sel = (*sel + GAME_ROWS >= count) ? count - 1 : *sel + GAME_ROWS;
    if (*sel != before) play_move();
    if (k == BTN_A || k == BTN_RIGHT) launch_entry(&entries[*sel]);
    if (k == BTN_X) {
        PlayEntry *e = &entries[*sel];
        enter_game_options(e->label, e->rompath, e->launch, e->system, back_state);
    }
    if (k == BTN_B || k == BTN_LEFT || k == BTN_MENU) {
        play_back();
        state = STATE_HOME;
    }
    (void)offset;
}

static void on_apps_key(SDLKey k) {
    int before = app_sel;
    if (k == BTN_UP   && app_sel > 0)              app_sel--;
    if (k == BTN_DOWN && app_sel < APP_COUNT - 1)  app_sel++;
    if (app_sel != before) play_move();
    if (k == BTN_A) {
        if (strcmp(APP_ENTRIES[app_sel].label, "Settings") == 0 ||
            strcmp(APP_ENTRIES[app_sel].label, "Wi-Fi") == 0) {
            play_select();
            open_settings_kind(strcmp(APP_ENTRIES[app_sel].label, "Wi-Fi") == 0 ? "network" : "display");
        } else if (strcmp(APP_ENTRIES[app_sel].cmd, "shutdown") == 0) {
            exec_power_cmd("shutdown");
        } else {
            launch_app_cmd(APP_ENTRIES[app_sel].cmd);
        }
    }
    if (k == BTN_B || k == BTN_MENU) {
        play_back();
        state = STATE_HOME;
    }
}

static void draw_font_picker(void) {
    draw_textured_bg(0, CONTENT_Y, SCREEN_W, CONTENT_H);
    draw_status();
    draw_hint_base();
    draw_hints_row("Select", "Cancel", NULL, NULL, NULL, NULL, NULL);

    if (font_pick_sel < font_pick_offset) font_pick_offset = font_pick_sel;
    if (font_pick_sel >= font_pick_offset + HOME_VISIBLE) font_pick_offset = font_pick_sel - HOME_VISIBLE + 1;

    for (int row = 0; row < HOME_VISIBLE && font_pick_offset + row < font_list_count; row++) {
        int i   = font_pick_offset + row;
        int iy  = CONTENT_Y + row * HOME_ITEM_H;
        int sel = (i == font_pick_sel);

        if (sel)
            draw_select_asset(6, iy + 3, SCREEN_W - 20, HOME_ITEM_H - 6);
        else
            fill_rect(12, iy + HOME_ITEM_H - 1, SCREEN_W - 24, 1, C_SEP);

        /* Strip extension for display */
        char label[64];
        strncpy(label, font_list_name[i], sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        char *dot = strrchr(label, '.');
        if (dot) *dot = '\0';

        draw_text(font_large, label, 20, iy + (HOME_ITEM_H - 30) / 2, SC_TEXT);

        Uint32 chev_col = sel ? RGBA(0x3D, 0x2C, 0x88) : RGBA(0x5F, 0x66, 0x80);
        draw_chevron(SCREEN_W - 32, iy + HOME_ITEM_H / 2, 8, 2, chev_col);
    }

    draw_scrollbar(SCREEN_W - 10, CONTENT_Y + 8, CONTENT_H - 16,
                   font_list_count, HOME_VISIBLE, font_pick_offset);
}

static void on_font_picker_key(SDLKey k) {
    int before = font_pick_sel;
    if (k == BTN_UP   && font_pick_sel > 0)                    font_pick_sel--;
    if (k == BTN_DOWN && font_pick_sel < font_list_count - 1)  font_pick_sel++;
    if (font_pick_sel != before) {
        play_move();
        apply_font_index(font_pick_sel);
    }
    if (k == BTN_A) {
        play_select();
        save_theme_font(font_pick_sel);
        state = STATE_SETTINGS;
    }
    if (k == BTN_B || k == BTN_MENU) {
        play_back();
        apply_font_index(font_pick_prev);  /* revert */
        state = STATE_SETTINGS;
    }
}

static void scan_themes(void) {
    theme_list_count = 0;
    DIR *d = opendir(ASSET_ROOT);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) && theme_list_count < THEME_LIST_MAX) {
        const char *n = ent->d_name;
        if (strncmp(n, "theme_", 6) != 0) continue;
        const char *ext = strrchr(n, '.');
        if (!ext || strcmp(ext, ".json") != 0) continue;
        snprintf(theme_list_path[theme_list_count], 512, "%s/%s", ASSET_ROOT, n);
        strncpy(theme_list_name[theme_list_count], n, 63);
        theme_list_name[theme_list_count][63] = '\0';
        theme_list_count++;
    }
    closedir(d);
}

static void apply_theme_index(int idx) {
    if (idx < 0 || idx >= theme_list_count) return;
    /* Copy preset to active theme.json */
    char dst[512];
    snprintf(dst, sizeof(dst), "%s/theme.json", ASSET_ROOT);
    FILE *src = fopen(theme_list_path[idx], "r");
    if (!src) return;
    char buf[4096] = {0};
    int n = (int)fread(buf, 1, sizeof(buf) - 1, src);
    fclose(src);
    FILE *out = fopen(dst, "w");
    if (!out) return;
    fwrite(buf, 1, n, out);
    fclose(out);
    /* Reload colors */
    char dummy[8];
    load_theme(dummy, sizeof(dummy));
}

static void draw_theme_picker(void) {
    draw_textured_bg(0, CONTENT_Y, SCREEN_W, CONTENT_H);
    draw_status();
    draw_hint_base();
    draw_hints_row("Select", "Cancel", NULL, NULL, NULL, NULL, NULL);

    if (theme_pick_sel < theme_pick_offset) theme_pick_offset = theme_pick_sel;
    if (theme_pick_sel >= theme_pick_offset + HOME_VISIBLE)
        theme_pick_offset = theme_pick_sel - HOME_VISIBLE + 1;

    for (int row = 0; row < HOME_VISIBLE && theme_pick_offset + row < theme_list_count; row++) {
        int i   = theme_pick_offset + row;
        int iy  = CONTENT_Y + row * HOME_ITEM_H;
        int sel = (i == theme_pick_sel);

        if (sel)
            draw_select_asset(6, iy + 3, SCREEN_W - 20, HOME_ITEM_H - 6);
        else
            fill_rect(12, iy + HOME_ITEM_H - 1, SCREEN_W - 24, 1, C_SEP);

        char label[64];
        strncpy(label, theme_list_name[i], sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        char *dot = strrchr(label, '.');
        if (dot) *dot = '\0';
        char *p = label;
        if (strncmp(p, "theme_", 6) == 0) p += 6;
        if (*p >= 'a' && *p <= 'z') *p -= 32;

        draw_text(font_large, p, 20, iy + (HOME_ITEM_H - 30) / 2, SC_TEXT);

        Uint32 chev_col = sel ? RGBA(0x3D, 0x2C, 0x88) : RGBA(0x5F, 0x66, 0x80);
        draw_chevron(SCREEN_W - 32, iy + HOME_ITEM_H / 2, 8, 2, chev_col);
    }

    draw_scrollbar(SCREEN_W - 10, CONTENT_Y + 8, CONTENT_H - 16,
                   theme_list_count, HOME_VISIBLE, theme_pick_offset);
}

static void on_theme_picker_key(SDLKey k) {
    int before = theme_pick_sel;
    if (k == BTN_UP   && theme_pick_sel > 0)                     theme_pick_sel--;
    if (k == BTN_DOWN && theme_pick_sel < theme_list_count - 1)  theme_pick_sel++;
    if (theme_pick_sel != before) {
        play_move();
        apply_theme_index(theme_pick_sel);
    }
    if (k == BTN_A) {
        play_select();
        apply_theme_index(theme_pick_sel);
        state = STATE_SETTINGS;
    }
    if (k == BTN_B || k == BTN_MENU) {
        play_back();
        state = STATE_SETTINGS;
    }
}

static void on_settings_key(SDLKey k) {
    int before = settings_sel;

    if (k == BTN_UP && settings_sel > 0) {
        settings_sel--;
        /* skip headers when moving up */
        while (settings_sel > 0 && SETTINGS_ENTRIES[settings_sel].is_header)
            settings_sel--;
        /* if we landed on a header at index 0, restore */
        if (SETTINGS_ENTRIES[settings_sel].is_header)
            settings_sel = before;
    }
    if (k == BTN_DOWN && settings_sel < SETTINGS_COUNT - 1) {
        settings_sel++;
        /* skip headers when moving down */
        while (settings_sel < SETTINGS_COUNT - 1 && SETTINGS_ENTRIES[settings_sel].is_header)
            settings_sel++;
        if (SETTINGS_ENTRIES[settings_sel].is_header)
            settings_sel = before;
    }

    if (settings_sel != before) {
        play_move();
        /* scroll so selected row is fully visible */
        int row_y = settings_row_top(settings_sel);
        int row_h = settings_row_h(settings_sel);
        int max_scroll = total_settings_height() - CONTENT_H;
        if (max_scroll < 0) max_scroll = 0;
        if (row_y < settings_scroll_px)
            settings_scroll_px = row_y;
        if (row_y + row_h > settings_scroll_px + CONTENT_H)
            settings_scroll_px = row_y + row_h - CONTENT_H;
        if (settings_scroll_px < 0) settings_scroll_px = 0;
        if (settings_scroll_px > max_scroll) settings_scroll_px = max_scroll;
    }

    if (k == BTN_A) {
        const char *kind = SETTINGS_ENTRIES[settings_sel].kind;
        if (kind && strcmp(kind, "font") == 0) {
            play_select();
            if (font_list_count == 0) {
                LogTimer _t = log_timer_begin("scan_fonts (lazy)");
                scan_fonts();
                log_timer_end(_t);
            }
            font_pick_prev = font_pick_sel;
            state = STATE_FONT_PICKER;
            return;
        }
        if (kind && strcmp(kind, "theme") == 0) {
            play_select();
            if (theme_list_count == 0) {
                LogTimer _t = log_timer_begin("scan_themes (lazy)");
                scan_themes();
                log_timer_end(_t);
            }
            state = STATE_THEME_PICKER;
            return;
        }
        if (kind && strcmp(kind, "system") == 0) {
            play_select();
            info_panel_about = 0;
            state = STATE_INFO_PANEL;
            return;
        }
        if (kind && strcmp(kind, "about") == 0) {
            play_select();
            info_panel_about = 1;
            state = STATE_INFO_PANEL;
            return;
        }
        adjust_setting(0);
    }
    if (k == BTN_LEFT || k == BTN_L1)             adjust_setting(-1);
    if (k == BTN_RIGHT || k == BTN_R1)            adjust_setting(1);
    if (k == BTN_B || k == BTN_MENU) {
        play_back();
        state = STATE_HOME;
    }
}

static void on_systems_key(SDLKey k) {
    if (!sys_count) return;

    int before = sys_sel;
    if (k == BTN_UP) {
        sys_sel = (sys_sel - 1 + sys_count) % sys_count;
        load_games(sys_sel);
    }
    if (k == BTN_DOWN) {
        sys_sel = (sys_sel + 1) % sys_count;
        load_games(sys_sel);
    }
    if (k == BTN_L1) {
        sys_sel = (sys_sel - PANEL_ROWS < 0) ? 0 : sys_sel - PANEL_ROWS;
        load_games(sys_sel);
    }
    if (k == BTN_R1) {
        sys_sel = (sys_sel + PANEL_ROWS >= sys_count) ? sys_count - 1 : sys_sel + PANEL_ROWS;
        load_games(sys_sel);
    }
    if (sys_sel != before) play_move();
    if ((k == BTN_A || k == BTN_RIGHT) && game_count > 0) {
        play_select();
        state = STATE_GAMES;
    }
    if (k == BTN_B || k == BTN_LEFT || k == BTN_MENU) {
        play_back();
        state = STATE_HOME;
    }
}

static void on_games_key(SDLKey k) {
    int before = game_sel;
    if (k == BTN_UP) {
        game_sel = (game_sel - 1 + game_count) % game_count;
    }
    if (k == BTN_DOWN) {
        game_sel = (game_sel + 1) % game_count;
    }
    if (k == BTN_L1) {
        game_sel = (game_sel - GAME_ROWS < 0) ? 0 : game_sel - GAME_ROWS;
    }
    if (k == BTN_R1) {
        game_sel = (game_sel + GAME_ROWS >= game_count)
                       ? game_count - 1
                       : game_sel + GAME_ROWS;
    }
    if (game_sel != before) play_move();
    if (k == BTN_A) {
        launch_game(sys_sel, game_sel);
    }
    if (k == BTN_Y && game_count > 0) {
        Game *g = &games[game_sel];
        System *sys = &systems[sys_sel];
        char launch[512];
        snprintf(launch, sizeof(launch), "%s/launch.sh", sys->emu_dir);
        toggle_favorite(g->name, g->path, launch);
        play_select();
    }
    if (k == BTN_X && game_count > 0) {
        Game *g = &games[game_sel];
        System *sys = &systems[sys_sel];
        char launch[512];
        snprintf(launch, sizeof(launch), "%s/launch.sh", sys->emu_dir);
        enter_game_options(g->name, g->path, launch, sys->label, STATE_GAMES);
    }
    if (k == BTN_B || k == BTN_LEFT) {
        play_back();
        state = STATE_SYSTEMS;
    }
    if (k == BTN_MENU) {
        play_back();
        state = STATE_HOME;
    }
}

// ── Font picker helpers ───────────────────────────────────────────────────────

static void scan_fonts(void) {
    font_list_count = 0;
    const char *dirs[] = {
        POCKETOS_ROOT "/miyoo/app",
        ASSET_ROOT
    };
    for (int d = 0; d < 2 && font_list_count < FONT_LIST_MAX; d++) {
        DIR *dp = opendir(dirs[d]);
        if (!dp) continue;
        struct dirent *ent;
        while ((ent = readdir(dp)) && font_list_count < FONT_LIST_MAX) {
            const char *n = ent->d_name;
            int len = strlen(n);
            int is_font = (len > 4) &&
                (strcasecmp(n + len - 4, ".otf") == 0 ||
                 strcasecmp(n + len - 4, ".ttf") == 0 ||
                 strcasecmp(n + len - 4, ".ttc") == 0);
            if (!is_font) continue;
            if (strncasecmp(n, "AdobeBlank", 10) == 0) continue;
            char testpath[512];
            snprintf(testpath, sizeof(testpath), "%s/%s", dirs[d], n);
            /* Verify the font actually renders before listing it */
            TTF_Font *probe = TTF_OpenFont(testpath, 26);
            if (!probe) continue;
            TTF_CloseFont(probe);
            strncpy(font_list_path[font_list_count], testpath, 511);
            font_list_path[font_list_count][511] = '\0';
            strncpy(font_list_name[font_list_count], n, 63);
            font_list_name[font_list_count][63] = '\0';
            font_list_count++;
        }
        closedir(dp);
    }
}

/* Find the index in font_list matching the currently active FONT_PRIMARY. */
static int current_font_index(void) {
    for (int i = 0; i < font_list_count; i++)
        if (strstr(font_list_path[i], "BPreplayBold") ||
            strstr(font_list_path[i], FONT_PRIMARY + (int)(strrchr(FONT_PRIMARY,'/') - FONT_PRIMARY + 1)))
            return i;
    return 0;
}

static void apply_font_index(int idx) {
    if (idx < 0 || idx >= font_list_count) return;
    const char *fp = font_list_path[idx];
    TTF_Font *nb = TTF_OpenFont(fp, 21);
    TTF_Font *ng = TTF_OpenFont(fp, 26);
    TTF_Font *nl = TTF_OpenFont(fp, 29);
    TTF_Font *ns = TTF_OpenFont(fp, 14);
    if (!nb || !ng || !nl || !ns) {
        if (nb) TTF_CloseFont(nb);
        if (ng) TTF_CloseFont(ng);
        if (nl) TTF_CloseFont(nl);
        if (ns) TTF_CloseFont(ns);
        return;
    }
    TTF_CloseFont(font_body);  TTF_CloseFont(font_game);
    TTF_CloseFont(font_large); TTF_CloseFont(font_small);
    font_body = nb; font_game = ng; font_large = nl; font_small = ns;
}

static void save_theme_font(int idx) {
    if (idx < 0 || idx >= font_list_count) return;
    char theme_path[512];
    snprintf(theme_path, sizeof(theme_path), "%s/theme.json", ASSET_ROOT);

    /* Read existing theme.json if present, otherwise start fresh */
    char buf[4096] = {0};
    FILE *f = fopen(theme_path, "r");
    if (f) { fread(buf, 1, sizeof(buf) - 1, f); fclose(f); }

    /* Build updated JSON: replace or inject "font" key */
    char new_buf[4096];
    const char *fn = font_list_name[idx];
    char *fp_tag = strstr(buf, "\"font\"");
    if (fp_tag && buf[0]) {
        /* Replace existing font value in-place */
        char before[4096] = {0}, after[4096] = {0};
        int blen = (int)(fp_tag - buf);
        strncpy(before, buf, blen);
        /* Skip past the old value */
        const char *p = fp_tag + 6;
        while (*p == ' ' || *p == ':' || *p == '\t') p++;
        if (*p == '"') { p++; while (*p && *p != '"') p++; if (*p) p++; }
        strncpy(after, p, sizeof(after) - 1);
        snprintf(new_buf, sizeof(new_buf), "%s\"font\": \"%s\"%s", before, fn, after);
    } else {
        /* No existing theme.json or no font key — write minimal file */
        snprintf(new_buf, sizeof(new_buf), "{\n  \"font\": \"%s\"\n}\n", fn);
    }

    f = fopen(theme_path, "w");
    if (f) { fputs(new_buf, f); fclose(f); }
}

// ── Theme loader ─────────────────────────────────────────────────────────────

static void hex_to_rgb(const char *hex, Uint8 *r, Uint8 *g, Uint8 *b) {
    if (!hex || hex[0] != '#' || strlen(hex) < 7) return;
    unsigned int v = 0;
    sscanf(hex + 1, "%06x", &v);
    *r = (v >> 16) & 0xFF;
    *g = (v >>  8) & 0xFF;
    *b =  v        & 0xFF;
}

/* Load theme.json and apply color/font overrides. Call after defaults are set.
   font_out: if non-NULL, receives full path to theme font (empty = use default). */
static void load_theme(char *font_out, int font_outlen) {
    char path[512];
    snprintf(path, sizeof(path), "%s/theme.json", ASSET_ROOT);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char buf[4096] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    char val[128];
    Uint8 r, g, b;

#define TH_C(key, var) \
    if (json_str_from_buf(buf, key, val, sizeof(val))) { \
        r = 0; g = 0; b = 0; hex_to_rgb(val, &r, &g, &b); \
        var = RGBA(r, g, b); \
    }
#define TH_SC(key, var) \
    if (json_str_from_buf(buf, key, val, sizeof(val))) { \
        r = 0; g = 0; b = 0; hex_to_rgb(val, &r, &g, &b); \
        var.r = r; var.g = g; var.b = b; \
    }

    TH_C("bg",          C_BG)
    TH_C("bar",         C_BAR)
    TH_C("sep",         C_SEP)
    TH_C("sel",         C_SEL)
    TH_C("sel_hi",      C_SEL_HI)
    TH_C("sel_border",  C_SEL_BORDER)
    TH_C("panel_hdr",   C_PANEL_HDR)
    TH_C("panel_hi",    C_PANEL_HI)
    TH_C("divider",     C_DIVIDER)
    TH_C("card",        C_CARD)
    TH_C("card_border", C_CARD_BORDER)
    TH_SC("text",       SC_TEXT)
    TH_SC("white",      SC_WHITE)
    TH_SC("dim",        SC_DIM)
    TH_SC("hdr",        SC_HDR)

#undef TH_C
#undef TH_SC

    if (font_out && json_str_from_buf(buf, "font", val, sizeof(val))) {
        char p1[512], p2[512];
        snprintf(p1, sizeof(p1), "%s/miyoo/app/%s", POCKETOS_ROOT, val);
        snprintf(p2, sizeof(p2), "%s/%s", ASSET_ROOT, val);
        FILE *tf = fopen(p1, "r");
        if (tf) { fclose(tf); strncpy(font_out, p1, font_outlen - 1); return; }
        tf = fopen(p2, "r");
        if (tf) { fclose(tf); strncpy(font_out, p2, font_outlen - 1); }
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    log_open();
    log_msg("pocketOS main start");
    LogTimer _t_startup = log_timer_begin("total startup");
    const char *autotest_env = getenv("POCKETOS_AUTOTEST_FRAMES");
    int autotest_frames = autotest_env ? atoi(autotest_env) : 0;
    int frames = 0;

    { LogTimer _t = log_timer_begin("SDL_Init");
      if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log_sdl_error("SDL_Init");
        log_close();
        return 1;
      }
      log_timer_end(_t); }
    log_msg("SDL_Init OK");
    SDL_ShowCursor(SDL_DISABLE);
    SDL_EnableKeyRepeat(280, 60);

    { LogTimer _t = log_timer_begin("TTF+IMG+audio init");
      if (TTF_Init() != 0) {
          log_sdl_error("TTF_Init");
      } else {
          log_msg("TTF_Init OK");
      }
      int img_flags = IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
      log_kv("IMG_Init flags", (img_flags & IMG_INIT_PNG) ? "PNG+JPG" : "partial");
      init_audio();
      log_timer_end(_t); }

    video  = SDL_SetVideoMode(SCREEN_W, SCREEN_H, BPP, SDL_HWSURFACE | SDL_DOUBLEBUF);
    screen = SDL_CreateRGBSurface(SDL_HWSURFACE, SCREEN_W, SCREEN_H, BPP, 0, 0, 0, 0);
    if (!video || !screen) {
        log_sdl_error("video init");
        log_close();
        return 1;
    }
    log_msg("video surface OK");

    // Apply Onion's timezone so localtime() returns correct local time
    {
        FILE *tzf = fopen(SYSDIR "/config/.tz", "r");
        if (tzf) {
            char tz[64] = {0};
            if (fgets(tz, sizeof(tz), tzf)) {
                char *nl = strchr(tz, '\n'); if (nl) *nl = '\0';
                char *cr = strchr(tz, '\r'); if (cr) *cr = '\0';
                if (tz[0]) { setenv("TZ", tz, 1); tzset(); }
            }
            fclose(tzf);
        }
    }

    // Check theme.json for a font override before loading fonts
    char theme_font[512] = "";
    { LogTimer _t = log_timer_begin("load_theme (font pass)");
      load_theme(theme_font, sizeof(theme_font));  // first pass: font only (colors need defaults first)
      log_timer_end(_t); }

    // Load fonts: theme font → BPreplayBold → Exo → CJK fallback
    { LogTimer _t = log_timer_begin("font load");
      const char *fp = theme_font[0] ? theme_font : FONT_PRIMARY;
      font_body  = TTF_OpenFont(fp, 21);
      font_game  = TTF_OpenFont(fp, 26);
      font_large = TTF_OpenFont(fp, 29);
      font_small = TTF_OpenFont(fp, 14);
      if (!font_body)  font_body  = TTF_OpenFont(FONT_PRIMARY, 21);
      if (!font_game)  font_game  = TTF_OpenFont(FONT_PRIMARY, 20);
      if (!font_large) font_large = TTF_OpenFont(FONT_PRIMARY, 26);
      if (!font_small) font_small = TTF_OpenFont(FONT_PRIMARY, 14);
      if (!font_body)  font_body  = TTF_OpenFont(FONT_PATH, 21);
      if (!font_game)  font_game  = TTF_OpenFont(FONT_PATH, 20);
      if (!font_large) font_large = TTF_OpenFont(FONT_PATH, 26);
      if (!font_small) font_small = TTF_OpenFont(FONT_PATH, 14);
      if (!font_body)  font_body  = TTF_OpenFont(FONT_ALT, 21);
      if (!font_game)  font_game  = TTF_OpenFont(FONT_ALT, 20);
      if (!font_large) font_large = TTF_OpenFont(FONT_ALT, 26);
      if (!font_small) font_small = TTF_OpenFont(FONT_ALT, 14);
      log_timer_end(_t); }
    if (!font_body || !font_game || !font_large || !font_small) {
        log_msg("ERROR: font load failed — no usable font found");
        log_file_state("font_path", FONT_PATH);
        log_file_state("font_primary", FONT_PRIMARY);
        log_file_state("font_alt", FONT_ALT);
        log_close();
        return 1;
    }
    log_msg("fonts loaded OK");

    // Resolve palette defaults — retro cream/navy/lavender per design guide
    C_BG          = RGBA(0xF1, 0xEB, 0xDD);
    C_BAR         = RGBA(0x07, 0x1A, 0x33);
    C_SEP         = RGBA(0xD2, 0xCE, 0xC3);
    C_SEL         = RGBA(0xC4, 0xB9, 0xEF);
    C_SEL_HI      = RGBA(0xE2, 0xDD, 0xFA);
    C_SEL_BORDER  = RGBA(0x8D, 0x7E, 0xD6);
    C_PANEL_HDR   = RGBA(0xD6, 0xCF, 0xEE);
    C_PANEL_HI    = RGBA(0xE8, 0xE3, 0xF6);
    C_DIVIDER     = RGBA(0xA9, 0x9E, 0xDE);
    C_CARD        = RGBA(0xF8, 0xF1, 0xE6);
    C_CARD_BORDER = RGBA(0xD2, 0xCE, 0xC3);

    // Apply theme color overrides (second pass — defaults are now set)
    load_theme(NULL, 0);

    { LogTimer _t = log_timer_begin("load_systems");
      load_systems();
      log_timer_end(_t); }
    if (sys_count > 0) {
        LogTimer _t = log_timer_begin("load_games(0)");
        load_games(0);
        log_timer_end(_t);
    }
    { LogTimer _t = log_timer_begin("scan_fonts");
      scan_fonts();
      log_timer_end(_t); }

    log_timer_end(_t_startup);
    log_msg("entering main loop");

    SDL_Event ev;
    while (running) {
        log_state_if_changed((int)state);

        /* Screenshot combo: L1 + L2 + R1 + R2 all held */
        {
            Uint8 *ks = SDL_GetKeyState(NULL);
            int combo = ks[BTN_L1] && ks[BTN_L2] && ks[BTN_R1] && ks[BTN_R2];
            if (combo && !screenshot_combo_held) take_screenshot();
            screenshot_combo_held = combo;
        }

        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN) {
                SDLKey k = ev.key.keysym.sym;
                switch (state) {
                case STATE_HOME:    on_home_key(k);    break;
                case STATE_SYSTEMS: on_systems_key(k); break;
                case STATE_GAMES:   on_games_key(k);   break;
                case STATE_RECENT:
                    on_entry_key(k, recent_entries, recent_count, &recent_sel, &recent_offset, STATE_RECENT);
                    break;
                case STATE_FAVORITES:
                    on_entry_key(k, favorite_entries, favorite_count, &favorite_sel, &favorite_offset, STATE_FAVORITES);
                    break;
                case STATE_APPS:         on_apps_key(k);         break;
                case STATE_SETTINGS:     on_settings_key(k);     break;
                case STATE_FONT_PICKER:  on_font_picker_key(k);  break;
                case STATE_THEME_PICKER: on_theme_picker_key(k); break;
                case STATE_BROWSE_CATS:   on_browse_cats_key(k);   break;
                case STATE_BROWSE_GAMES:  on_browse_games_key(k);  break;
                case STATE_INFO_PANEL:    on_info_panel_key(k);    break;
                case STATE_GAME_OPTIONS:  on_game_options_key(k);  break;
                }
            }
        }
        render();
        SDL_Delay(16);

#ifdef POCKETOS_ENABLE_AUDIO
        if (music_pending && audio_ready && bg_music) {
            music_pending = 0;
            Mix_VolumeMusic(MIX_MAX_VOLUME * 55 / 100);
            Mix_PlayMusic(bg_music, -1);
        }
#endif

        if (autotest_frames > 0 && ++frames >= autotest_frames) {
            running = 0;
        }
    }

    TTF_CloseFont(font_body);
    TTF_CloseFont(font_game);
    TTF_CloseFont(font_large);
    TTF_CloseFont(font_small);
    for (int i = 0; i < asset_cache_count; i++) {
        SDL_FreeSurface(asset_cache[i].surface);
    }
    // Persist settings so they survive reboot (mirrors runtime.sh save_settings)
    {
        char sn[64] = {0};
        FILE *snf = fopen("/tmp/deviceSN", "r");
        if (snf) { fgets(sn, sizeof(sn), snf); fclose(snf);
            char *nl = strchr(sn,'\n'); if (nl) *nl='\0'; }
        if (sn[0]) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd),
                "cp -f " POCKETOS_ROOT "/system.json "
                SYSDIR "/config/system/%s.json", sn);
            system(cmd);
        }
    }

    shutdown_audio();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    log_close();
    return 0;
}
