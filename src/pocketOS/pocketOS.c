// pocketOS.c
// List-based launcher for Miyoo Mini (Onion OS)
// Replaces MainUI with a two-panel Pocket OS style interface.
//
// Screens:
//   HOME    - vertical list: Games / Favorites / Settings / Sleep
//   SYSTEMS - two-panel: system list (left) | game list (right), navigate systems
//   GAMES   - same two-panel, focus moves to game list
//
// On game select: writes /tmp/cmd_to_run.sh and exits — Onion runtime.sh
// picks it up and launches the emulator exactly like the normal flow.

#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <SDL/SDL_ttf.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <zlib.h>
#if __has_include(<sqlite3.h>)
#include <sqlite3.h>
#elif __has_include(<sqlite3/sqlite3.h>)
#include <sqlite3/sqlite3.h>
#else
typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;
#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_OPEN_READONLY 0x00000001
extern int sqlite3_open_v2(const char *filename, sqlite3 **ppDb, int flags, const char *zVfs);
extern int sqlite3_close(sqlite3 *);
extern const char *sqlite3_errmsg(sqlite3 *);
extern int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
extern int sqlite3_step(sqlite3_stmt *);
extern const unsigned char *sqlite3_column_text(sqlite3_stmt *, int iCol);
extern long long sqlite3_column_int64(sqlite3_stmt *, int iCol);
extern int sqlite3_finalize(sqlite3_stmt *pStmt);
#endif

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
#define HOME_TAB_H   40   /* section tab strip height at top of home content */

/* Primary game-browser shell from the handheld UI redesign. */
#define BROWSER_HEADER_H 52
#define BROWSER_FOOTER_H 40
#define BROWSER_BODY_H   (SCREEN_H - BROWSER_HEADER_H - BROWSER_FOOTER_H)
#define BROWSER_ROWS     5
#define LIBRARY_SYS_ROWS 6
#define SETTINGS_SECTION_H 24
#define SETTINGS_ROW_H     56

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
#define HEALTH_LOG_PATH SYSDIR "/logs/pocketos_health.csv"
#endif
#ifndef FIRMWARE_VERSION_PATH
#define FIRMWARE_VERSION_PATH "/tmp/firmwareVersion"
#endif
#define MOST_PLAYED_DB \
    POCKETOS_ROOT "/Saves/CurrentProfile/play_activity/play_activity_db.sqlite"
#ifndef FONT_PATH
#define FONT_PATH   POCKETOS_ROOT "/miyoo/app/Exo-2-Bold-Italic_Universal.ttf"
#endif
#ifndef FONT_ALT
#define FONT_ALT    POCKETOS_ROOT "/miyoo/app/wqy-microhei.ttc"
#endif
#ifndef FONT_PRIMARY
#define FONT_PRIMARY POCKETOS_ROOT "/miyoo/app/BPreplayBold.otf"
#endif
#define POCKETOS_VERSION "1.2.4"
#define ONION_BASE_VERSION "v4.3.1-1"

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

#define MAX_SYSTEMS    64
#define MAX_GAMES    1500
#define MAX_RECENT    200
#define MAX_FAVORITES 200

// ── Colors (static helpers use COL macro after screen is init'd) ──────────────

#define RGBA(r,g,b) SDL_MapRGB(screen->format,(r),(g),(b))

// Forward declarations for functions defined later in the file
static void scan_fonts(void);
static int current_font_index(void);
static void apply_font_index(int idx);
static void save_theme_font(int idx);
static void draw_font_picker(void);
static void on_font_picker_key(SDLKey k);
static void scan_themes(void);
static int current_theme_index(void);
static void load_browse_data(void);
static void draw_info_panel(void);
static void on_info_panel_key(SDLKey k);
static void apply_theme_index(int idx);
static void preview_theme_index(int idx);
static void draw_theme_picker(void);
static void on_theme_picker_key(SDLKey k);
static void load_theme(char *font_out, int font_outlen);
static void load_theme_file(const char *path, char *font_out, int font_outlen);
static void set_palette_defaults(void);
static void apply_appearance_mode(void);
static void reload_theme_palette(void);
static void clear_text_cache(void);
static void clear_text_cache(void);
static SDL_Color browser_text(void);
static SDL_Color browser_secondary(void);
static SDL_Color browser_dim(void);
static SDL_Color browser_dark_text(void);
static Uint32 browser_rgb(Uint8 r, Uint8 g, Uint8 b);
static Uint32 browser_accent(int category);
static SDL_Color browser_accent_text(int category);
static void draw_browser_badge(int x, int y, int w, const char *system, int selected);
static void draw_browser_more(int x, int y, int w, int total, int offset,
                              int visible, int category);
static void format_playtime_compact(int secs, char *out, int outlen);
static void draw_secondary_frame(const char *parent, const char *title,
                                 const char *meta);
static void draw_secondary_footer(int mode);

// Named color constants (resolved at runtime)
static Uint32 C_BG, C_BAR, C_SEP, C_SEL, C_PANEL_HDR;
static Uint32 C_DIVIDER, C_CARD, C_CARD_BORDER;
static Uint32 C_SEL_HI, C_SEL_BORDER, C_PANEL_HI;

typedef struct {
    Uint32 bg, bar, sep, sel, panel_hdr;
    Uint32 divider, card, card_border;
    Uint32 sel_hi, sel_border, panel_hi;
    SDL_Color text, white, dim, sub_sel, hdr;
} ThemePalette;

static ThemePalette theme_preview_original;
static int theme_preview_active = 0;

// Off-white/atomic purple palette — see pocket_os_design_guide.md
static SDL_Color SC_TEXT  = { 37,  25,  52, 255};  // deep plum #251934
static SDL_Color SC_WHITE = {255, 255, 255, 255};  // white text on purple selections
static SDL_Color SC_DIM     = {102,  88, 112, 255};  // muted plum #665870
static SDL_Color SC_SUB_SEL = {239, 227, 255, 255};  // pale lavender on selection bg
__attribute__((unused)) static SDL_Color SC_ARROW = {125,  60, 255, 255};  // atomic purple
static SDL_Color SC_HDR   = { 37,  25,  52, 255};  // same as text for headers

// ── State machine ────────────────────────────────────────────────────────────

typedef enum {
    STATE_HOME,
    STATE_SYSTEMS,
    STATE_GAMES,
    STATE_RECENT,
    STATE_FAVORITES,
    STATE_MOST_PLAYED,
    STATE_APPS,
    STATE_SETTINGS,
    STATE_FONT_PICKER,
    STATE_THEME_PICKER,
    STATE_BROWSE_CATS,
    STATE_BROWSE_GAMES,
    STATE_INFO_PANEL,
    STATE_GAME_OPTIONS,
} State;

static void enter_game_options(const char *name, const char *path,
                               const char *launch, const char *system,
                               State back_state);

static State state = STATE_MOST_PLAYED;
static int   info_panel_about = 0;  /* 0 = device/miyoo info, 1 = pocket OS about */
static State info_panel_back = STATE_SETTINGS;
static int home_section    = 0;       /* 0=BROWSE(default)  1=PLAY(incl. Library)  2=SYSTEM */
static int home_sel_sec[3] = {0,0,0}; /* per-section cursor */
static int browser_category = 0;      /* Most Played, Browse, Library, Favorites, Settings */

// ── Data structures ───────────────────────────────────────────────────────────

typedef struct {
    char    label[48];
    char    emu_dir[256];   // /mnt/SDCARD/Emu/GBA
    char    rom_dir[256];   // /mnt/SDCARD/Roms/GBA
    char    extlist[128];   // "gba|bin|zip|7z"
    time_t  rom_dir_mtime;  // mtime when game list was last loaded
    int     rom_count;      // matching files in rom_dir, shown in Library
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
    int  play_secs;   // total play time in seconds; 0 = unknown/not applicable
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
static int  game_count    = 0;
static int  game_sel      = 0;
static int  game_offset   = 0;
static int  games_sys_idx = -1;  /* which system's games are currently loaded */
static int  game_opts_sel  = 0;   /* selected row in Game Options panel */
static int  game_opts_mode = 0;   /* 0=menu, 1=rom_info, 2=save_info */
static State game_opts_back = STATE_GAMES; /* which state to return to */
static char game_opts_name[240];
static char game_opts_path[512];
static char game_opts_launch[512];
static char game_opts_system[48];

static PlayEntry recent_entries[MAX_RECENT];
static int recent_count = 0;
static int recent_sel = 0;
static int recent_offset = 0;

static PlayEntry favorite_entries[MAX_FAVORITES];
static int favorite_count = 0;
static int favorite_sel = 0;
static int favorite_offset = 0;

static PlayEntry most_played_entries[MAX_GAMES];
static int most_played_count  = 0;
static int most_played_sel    = 0;
static int most_played_offset = 0;

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
    { "Appearance",       "icon_theme.png",        "appearance",   NULL, 0 },
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

/* Settings value cache — populated on entry to settings, invalidated on write */
static char settings_val_cache[SETTINGS_COUNT][64];
static int  settings_num_cache[SETTINGS_COUNT];   /* numeric value for bar rendering */
static int  settings_val_valid = 0;  /* 0 = stale, rebuild on next draw */

// ── SDL globals ───────────────────────────────────────────────────────────────

static SDL_Surface *video  = NULL;
static SDL_Surface *screen = NULL;
static TTF_Font    *font_body  = NULL;   // 21pt — panels, bars
static TTF_Font    *font_game  = NULL;   // 20pt — two-line game titles
static TTF_Font    *font_large = NULL;   // 26pt — home rows, settings rows
static TTF_Font    *font_small = NULL;   // 14pt — labels, hints, values
static char         active_font_path[512] = "";
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

/* Text surface cache — avoids TTF_RenderUTF8_Blended alloc/free per draw_text call.
   Key: font pointer + text string + packed color. LRU eviction at capacity. */
#define TEXT_CACHE_MAX 128
typedef struct {
    TTF_Font    *font;
    Uint32       color_key;   /* (r<<16)|(g<<8)|b */
    char         text[128];
    SDL_Surface *surface;
    unsigned int last_use;    /* frame counter for LRU */
} TextCache;
static TextCache  text_cache[TEXT_CACHE_MAX];
static int        text_cache_count = 0;
static unsigned int text_cache_frame = 0;

static int screenshot_combo_held = 0;
static int screenshot_toast_frames = 0;  /* > 0 = show "Saved" toast */

static int    g_dirty          = 1;   /* render needed this frame */
static time_t g_last_input     = 0;   /* time() of last button press */
static int    g_last_clock_min = -1;  /* last rendered clock minute */
static int    g_batt_last      = -1;  /* last rendered battery level */
static int    g_idle_dimmed       = 0;   /* 1 = backlight auto-dimmed, restore on input */
static int    g_pre_dim_brightness = -1; /* brightness level saved before auto-dim */

static SDL_Surface *g_dim_overlay = NULL;  /* pre-built 60% darkened full-screen surface */

// ── Home menu ────────────────────────────────────────────────────────────────
// Three sections: [BROWSE(0, default, leftmost)] [PLAY(1, incl. Library)] [SYSTEM(2)]
// L1/R1 cycle left/right through sections; Up/Down navigate within section.

typedef struct { const char *label; const char *icon; int action; } HomeItem;

/* action numbers match the original switch cases */
static const HomeItem HOME_SEC0[] = {  /* BROWSE (special-cased, see draw_home_browse) */
    { "Browse",    "browse.png",    3 },
};
static const HomeItem HOME_SEC1[] = {  /* PLAY */
    { "Most Played", "app_activity.png", 7 },
    { "Library",     "library.png",      2 },
    { "Favorites",   "favorites.png",    1 },
};
static const HomeItem HOME_SEC2[] = {  /* SYSTEM */
    { "Apps",      "apps.png",     4 },
    { "Settings",  "settings.png", 5 },
    { "Sleep",     "sleep.png",    6 },
};

static const HomeItem *HOME_SECTIONS[3] = { HOME_SEC0, HOME_SEC1, HOME_SEC2 };
static const int       HOME_SEC_COUNT[3] = { 1, 3, 3 };
static const char     *HOME_SEC_NAME[3]  = { "BROWSE", "PLAY", "SYSTEM" };

static AppEntry APP_ENTRIES[] = {
    { "Advanced Menu",   "tools.png",        "cd /mnt/SDCARD/App/AdvanceMENU; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Battery",         "app_battery.png",  "cd /mnt/SDCARD/App/BatteryMonitorUI; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Boot Logo",       "icon_theme.png",   "cd /mnt/SDCARD/App/EasyLogoTweak; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Calibration",     "app_display.png",  "cd /mnt/SDCARD/App/240pSuite; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
    { "Clock",           "app_clock.png",    "cd /mnt/SDCARD/App/Clock; chmod a+x ./launch.sh; LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ./launch.sh" },
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

static void copy_truncated(char *dst, size_t dstlen, const char *src) {
    if (dstlen == 0) return;
    size_t len = strlen(src);
    if (len >= dstlen) len = dstlen - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static int path_join(char *out, size_t outlen, const char *dir, const char *name) {
    size_t dirlen = strlen(dir);
    size_t namelen = strlen(name);
    int needs_sep = dirlen > 0 && dir[dirlen - 1] != '/';
    if (dirlen + (size_t)needs_sep + namelen + 1 > outlen) return 0;

    memcpy(out, dir, dirlen);
    if (needs_sep) out[dirlen++] = '/';
    memcpy(out + dirlen, name, namelen);
    out[dirlen + namelen] = '\0';
    return 1;
}

static int resolve_sdcard_path(const char *rel, char *out, int outlen) {
    if (rel[0] == '/') {
        if (strlen(rel) >= (size_t)outlen) return 0;
        copy_truncated(out, (size_t)outlen, rel);
        return 1;
    }
    const char *p = rel;
    while (strncmp(p, "../", 3) == 0) p += 3;
    return path_join(out, (size_t)outlen, POCKETOS_ROOT, p);
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

static int count_roms_in_dir(const char *rom_dir, const char *extlist) {
    DIR *d = opendir(rom_dir);
    if (!d) return 0;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (extlist[0] != '\0' && !ext_match(ent->d_name, extlist)) continue;

        char fullpath[512];
        struct stat st;
        if (!path_join(fullpath, sizeof(fullpath), rom_dir, ent->d_name)) continue;
        if (stat(fullpath, &st) == 0 && S_ISREG(st.st_mode)) count++;
    }
    closedir(d);
    return count;
}

// ── Utility: strip file extension for display ─────────────────────────────────

static void strip_ext(const char *filename, char *out, int outlen) {
    copy_truncated(out, (size_t)outlen, filename);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

// ── Utility: tiny JSON string reader (no external dependency) ─────────────────
// Reads the first value of "key" from a flat JSON file.

static int json_copy_string(const char *p, char *out, int outlen) {
    int i = 0;
    while (*p && *p != '"' && i < outlen - 1) {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '\\' && *p) {
            ch = (unsigned char)*p++;
            if (ch == 'n') ch = '\n';
            else if (ch == 'r') ch = '\r';
            else if (ch == 't') ch = '\t';
            else if (ch == 'b') ch = '\b';
            else if (ch == 'f') ch = '\f';
        }
        out[i++] = (char)ch;
    }
    out[i] = '\0';
    return i > 0;
}

static int json_str(const char *filepath, const char *key, char *out, int outlen) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;

    char buf[8192];
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

    return json_copy_string(p, out, outlen);
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

    return json_copy_string(p, out, outlen);
}

static void log_kv(const char *key, const char *value);
static void log_int(const char *key, int value);
static void log_errno_msg(const char *context, const char *path);

static FILE *open_atomic_file(const char *path, char *tmp, size_t tmp_len) {
    if (snprintf(tmp, tmp_len, "%s.tmp", path) >= (int)tmp_len) return NULL;
    unlink(tmp);
    return fopen(tmp, "w");
}

static int commit_atomic_file(FILE *f, const char *tmp, const char *path) {
    int ok = 1;
    if (fflush(f) != 0 || ferror(f)) ok = 0;
    if (ok && fsync(fileno(f)) != 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (ok && rename(tmp, path) == 0) return 1;
    log_errno_msg("atomic write failed", path);
    unlink(tmp);
    return 0;
}

static int json_write_string(FILE *f, const char *value) {
    if (fputc('"', f) == EOF) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        switch (*p) {
        case '"': if (fputs("\\\"", f) == EOF) return 0; break;
        case '\\': if (fputs("\\\\", f) == EOF) return 0; break;
        case '\b': if (fputs("\\b", f) == EOF) return 0; break;
        case '\f': if (fputs("\\f", f) == EOF) return 0; break;
        case '\n': if (fputs("\\n", f) == EOF) return 0; break;
        case '\r': if (fputs("\\r", f) == EOF) return 0; break;
        case '\t': if (fputs("\\t", f) == EOF) return 0; break;
        default:
            if (*p < 0x20) {
                if (fprintf(f, "\\u%04x", *p) < 0) return 0;
            } else if (fputc(*p, f) == EOF) {
                return 0;
            }
        }
    }
    return fputc('"', f) != EOF;
}

/* Onion's runtime parses the ROM from the separator between two double-quoted
   arguments. Keep this format aligned with common/system/state.h. */
static int onion_write_quoted_arg(FILE *f, const char *value) {
    if (fputc('"', f) == EOF) return 0;
    for (const char *p = value; *p; p++) {
        if (*p == '"' || *p == '\n' || *p == '\r' || *p == '`') return 0;
        if (fputc(*p, f) == EOF) return 0;
    }
    return fputc('"', f) != EOF;
}

static int write_onion_game_command(FILE *f, const char *launch, const char *rompath) {
    if (fputs("LD_PRELOAD=/mnt/SDCARD/miyoo/app/../lib/libpadsp.so ", f) == EOF)
        return 0;
    if (!onion_write_quoted_arg(f, launch) || fputc(' ', f) == EOF)
        return 0;
    if (!onion_write_quoted_arg(f, rompath) || fputc('\n', f) == EOF)
        return 0;
    return 1;
}

static int json_int_file(const char *filepath, const char *key, int fallback) {
    FILE *f = fopen(filepath, "r");
    if (!f) return fallback;

    char buf[8192];
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
    int lum = json_int_file(POCKETOS_ROOT "/system.json", "lumination", 5);
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
    /* Pre-computed lookup — avoids expf() glibc version dependency */
    static const int brt_lut[11] = {3,4,6,9,12,17,25,35,50,70,100};
    int raw = brt_lut[brightness];
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

static System *find_system_for_rompath(const char *rompath) {
    for (int i = 0; i < sys_count; i++) {
        size_t root_len = strlen(systems[i].rom_dir);
        if (strncmp(rompath, systems[i].rom_dir, root_len) == 0 &&
            rompath[root_len] == '/') {
            char launch[320];
            snprintf(launch, sizeof(launch), "%s/launch.sh", systems[i].emu_dir);
            if (access(launch, R_OK) == 0) return &systems[i];
        }
    }
    return NULL;
}

// ── Battery level ─────────────────────────────────────────────────────────────

static int read_battery(void) {
    static int    cached    = -1;
    static time_t cached_ts = 0;
    time_t now = time(NULL);
    if (cached >= 0 && now - cached_ts < 5) return cached;

    int result = -1;

    const char *tmp_paths[] = {
        "/tmp/percBat",
        "/tmp/.percBat",
        NULL
    };
    for (int i = 0; tmp_paths[i] && result < 0; i++) {
        FILE *f = fopen(tmp_paths[i], "r");
        if (f) {
            int v = -1;
            if (fscanf(f, "%d", &v) == 1) {
                if (v == 500) result = 100;
                else if (v >= 0 && v <= 100) result = v;
            }
            fclose(f);
        }
    }

    if (result < 0) {
        FILE *axp = fopen("/tmp/.axp_result", "r");
        if (axp) {
            char buf[128];
            int v = -1;
            int n = (int)fread(buf, 1, sizeof(buf) - 1, axp);
            fclose(axp);
            buf[n] = '\0';
            if (sscanf(buf, "{\"battery\":%d", &v) == 1) {
                if (v == 500) result = 100;
                else if (v >= 0 && v <= 100) result = v;
            }
        }
    }

    if (result < 0) {
        const char *paths[] = {
            "/sys/class/power_supply/axp20x-battery/capacity",
            "/sys/class/power_supply/battery/capacity",
            NULL
        };
        for (int i = 0; paths[i] && result < 0; i++) {
            FILE *f = fopen(paths[i], "r");
            if (f) {
                int v = -1;
                if (fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 100) result = v;
                fclose(f);
            }
        }
    }

    if (result >= 0) { cached = result; cached_ts = now; }
    return result;
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
    } else if (strcmp(k, "appearance") == 0) {
        int dark = read_config_int("pocketosAppearance", 0);
        snprintf(out, outlen, "%s", dark ? "Dark" : "Light");
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
#define HEALTH_LOG_MAX_BYTES (512 * 1024)

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
}

static void log_kv(const char *key, const char *value) {
    if (!g_log_fp) return;
    log_timestamp(g_log_fp);
    fprintf(g_log_fp, "%s: %s\n", key, value ? value : "(null)");
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
    fflush(g_log_fp);  /* flush errors immediately so they survive a crash */
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
}

static void log_sdl_error(const char *context) {
    if (!g_log_fp) return;
    log_timestamp(g_log_fp);
    fprintf(g_log_fp, "SDL ERROR %s: %s\n", context, SDL_GetError());
    fflush(g_log_fp);  /* flush errors immediately so they survive a crash */
}

/* Signal handler — logs a fixed message then re-raises the signal. */
static const char *g_log_path_static = LOG_PATH;
static void sig_handler(int sig) {
    const char *msg = sig == SIGSEGV ? "*** POCKETOS CRASH: SIGSEGV ***\n" :
                      sig == SIGABRT ? "*** POCKETOS CRASH: SIGABRT ***\n" :
                      sig == SIGFPE  ? "*** POCKETOS CRASH: SIGFPE ***\n"  :
                      sig == SIGBUS  ? "*** POCKETOS CRASH: SIGBUS ***\n"  :
                      sig == SIGILL  ? "*** POCKETOS CRASH: SIGILL ***\n"  :
                                       "*** POCKETOS CRASH: SIGNAL ***\n";
    int fd = open(g_log_path_static, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd >= 0) {
        size_t len = 0;
        while (msg[len]) len++;
        ssize_t written = write(fd, msg, len);
        (void)written;
        close(fd);
    }
    signal(sig, SIG_DFL);
    kill(getpid(), sig);
}

static void log_open(void) {
    /* Logging is opt-in: only activate if the log file already exists.
       To enable: touch /mnt/SDCARD/.tmp_update/logs/pocketos_debug.log
       To disable: delete that file. */
    struct stat st;
    if (stat(LOG_PATH, &st) != 0) return;

    /* Rotate if log is too large */
    if (st.st_size > LOG_MAX_BYTES) {
        char old[256];
        snprintf(old, sizeof(old), "%s.old", LOG_PATH);
        rename(LOG_PATH, old);
    }

    g_log_fp = fopen(LOG_PATH, "a");
    if (!g_log_fp) return;

    /* Session header */
    fprintf(g_log_fp, "\n");
    fprintf(g_log_fp, "========================================\n");
    log_timestamp(g_log_fp);
    fprintf(g_log_fp, "PocketOS v%s  started\n", POCKETOS_VERSION);
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
        case STATE_HOME:         return "HOME";
        case STATE_SYSTEMS:      return "SYSTEMS";
        case STATE_GAMES:        return "GAMES";
        case STATE_RECENT:       return "RECENT";
        case STATE_FAVORITES:    return "FAVORITES";
        case STATE_MOST_PLAYED:  return "MOST_PLAYED";
        case STATE_APPS:         return "APPS";
        case STATE_SETTINGS:     return "SETTINGS";
        case STATE_FONT_PICKER:  return "FONT_PICKER";
        case STATE_THEME_PICKER: return "THEME_PICKER";
        case STATE_BROWSE_CATS:  return "BROWSE_CATS";
        case STATE_BROWSE_GAMES: return "BROWSE_GAMES";
        case STATE_INFO_PANEL:   return "INFO_PANEL";
        case STATE_GAME_OPTIONS: return "GAME_OPTIONS";
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

/* ── Opt-in health monitor ──────────────────────────────────────────────────
 * Enable by creating HEALTH_LOG_PATH.  The monitor records only launcher
 * health data (never ROM names or other library data), once per minute plus
 * launch/exit.  A bounded CSV keeps SD-card writes and storage use small. */
static long proc_kb_value(const char *path, const char *label) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char key[64];
    long value;
    while (fscanf(f, "%63s %ld", key, &value) == 2) {
        if (strcmp(key, label) == 0) {
            fclose(f);
            return value;
        }
    }
    fclose(f);
    return -1;
}

static void health_log_sample(const char *event) {
    struct stat st;
    if (stat(HEALTH_LOG_PATH, &st) != 0) return;  /* monitoring is opt-in */

    int write_header = st.st_size == 0;
    if (st.st_size > HEALTH_LOG_MAX_BYTES) {
        char previous[512];
        snprintf(previous, sizeof(previous), "%s.prev", HEALTH_LOG_PATH);
        rename(HEALTH_LOG_PATH, previous);
        write_header = 1;
    }

    FILE *f = fopen(HEALTH_LOG_PATH, "a");
    if (!f) return;
    if (write_header)
        fputs("timestamp,event,screen,rss_kb,mem_available_kb,battery_percent,brightness\n", f);
    fprintf(f, "%ld,%s,%s,%ld,%ld,%d,%d\n", (long)time(NULL), event,
            state_name(state), proc_kb_value("/proc/self/status", "VmRSS:"),
            proc_kb_value("/proc/meminfo", "MemAvailable:"), read_battery(),
            json_int_file(POCKETOS_ROOT "/system.json", "brightness", -1));
    fclose(f);
}

/* Exercise rendering, list selection, font replacement, and theme preview
 * without launching a game or writing user appearance settings.  This runs
 * only when POCKETOS_STRESS_TEST is explicitly set by the Terminal runner. */
static void run_stress_step(int step) {
    switch (step % 6) {
        case 0:
            browser_category = 0;
            if (most_played_count) most_played_sel = step % most_played_count;
            state = STATE_MOST_PLAYED;
            break;
        case 1:
            browser_category = 1;
            if (browse_genre_count) browse_genre_sel = step % browse_genre_count;
            state = browse_genre_count ? STATE_BROWSE_CATS : STATE_HOME;
            break;
        case 2:
            browser_category = 2;
            if (sys_count) sys_sel = step % sys_count;
            state = sys_count ? STATE_SYSTEMS : STATE_HOME;
            break;
        case 3:
            browser_category = 3;
            if (favorite_count) favorite_sel = step % favorite_count;
            state = STATE_FAVORITES;
            break;
        case 4:
            browser_category = 4;
            state = STATE_SETTINGS;
            break;
        default:
            browser_category = 4;
            home_section = 2;
            state = STATE_HOME;
            break;
    }
    if (font_list_count > 1 && step % 5 == 0)
        apply_font_index((step / 5) % font_list_count);
    if (theme_list_count > 1 && step % 12 == 0)
        preview_theme_index((step / 12) % theme_list_count);
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
    if (Mix_OpenAudio(22050, AUDIO_S16SYS, 1, 1024) != 0) {
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
    if (strcasecmp(label, "MD")      == 0) return "Genesis";
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

static const char *library_system_name(const char *label) {
    if (strcasecmp(label, "FC") == 0 || strcasecmp(label, "NES") == 0) return "NES";
    if (strcasecmp(label, "SFC") == 0 || strcasecmp(label, "SNES") == 0) return "SNES";
    if (strcasecmp(label, "GB") == 0) return "GB";
    if (strcasecmp(label, "GBC") == 0) return "GBC";
    if (strcasecmp(label, "GBA") == 0) return "GBA";
    if (strcasecmp(label, "MD") == 0 || strcasecmp(label, "GEN") == 0 ||
        strcasecmp(label, "GENESIS") == 0) return "Genesis";
    if (strcasecmp(label, "PS") == 0 || strcasecmp(label, "PSX") == 0 ||
        strcasecmp(label, "PS1") == 0) return "PS1";
    if (strcasecmp(label, "ARCADE") == 0 || strcasecmp(label, "ADVMAME") == 0 ||
        strcasecmp(label, "MAME") == 0) return "Arcade";
    return system_full_name(label);
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
        if (!path_join(emu_dir, sizeof(emu_dir), EMU_ROOT, ent->d_name)) {
            log_kv("skip overlong emu path", ent->d_name);
            continue;
        }

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
            if (!resolve_sdcard_path(rompath, abs_rom, sizeof(abs_rom))) {
                log_kv("skip overlong rom path", rompath);
                continue;
            }
        } else {
            // Guess: /mnt/SDCARD/Roms/<dirname>
            if (!path_join(abs_rom, sizeof(abs_rom), ROMS_ROOT, ent->d_name)) {
                log_kv("skip overlong rom path", ent->d_name);
                continue;
            }
        }

        // Only include if roms directory exists
        if (stat(abs_rom, &st) != 0 || !S_ISDIR(st.st_mode)) {
            log_errno_msg("skip system missing rom dir", abs_rom);
            continue;
        }

        System *sys = &systems[sys_count++];
        copy_truncated(sys->label,   sizeof(sys->label),   label);
        copy_truncated(sys->emu_dir, sizeof(sys->emu_dir), emu_dir);
        copy_truncated(sys->rom_dir, sizeof(sys->rom_dir), abs_rom);
        copy_truncated(sys->extlist, sizeof(sys->extlist), extlist);
        sys->rom_count = count_roms_in_dir(sys->rom_dir, sys->extlist);
    }

    closedir(d);
    qsort(systems, sys_count, sizeof(System), cmp_sys);

    char msg[64];
    snprintf(msg, sizeof(msg), "load_systems count=%d", sys_count);
    log_msg(msg);
}

// ── Load games for selected system ───────────────────────────────────────────

static void load_games(int idx) {
    if (idx < 0 || idx >= sys_count) {
        game_count = 0; game_sel = 0; game_offset = 0;
        games_sys_idx = -1;
        return;
    }

    System *sys = &systems[idx];

    /* Skip reload if same system and ROM directory hasn't changed */
    struct stat rom_st;
    time_t cur_mtime = (stat(sys->rom_dir, &rom_st) == 0) ? rom_st.st_mtime : 0;
    if (idx == games_sys_idx && cur_mtime == sys->rom_dir_mtime) {
        game_sel    = 0;
        game_offset = 0;
        return;
    }

    game_count  = 0;
    game_sel    = 0;
    game_offset = 0;

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
        if (!path_join(fullpath, sizeof(fullpath), sys->rom_dir, ent->d_name)) {
            log_kv("skip overlong game path", ent->d_name);
            continue;
        }
        struct stat st;
        if (stat(fullpath, &st) != 0 || S_ISDIR(st.st_mode)) continue;

        // Extension filter (skip if extlist set and file doesn't match)
        if (sys->extlist[0] != '\0' && !ext_match(ent->d_name, sys->extlist)) continue;

        char display[240];
        strip_ext(ent->d_name, display, sizeof(display));

        Game *g = &games[game_count++];
        copy_truncated(g->name, sizeof(g->name), display);
        copy_truncated(g->path, sizeof(g->path), fullpath);
    }

    closedir(d);
    qsort(games, game_count, sizeof(Game), cmp_game);

    sys->rom_dir_mtime = cur_mtime;
    sys->rom_count = game_count;
    games_sys_idx = idx;

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
    int max_count = (entries == recent_entries)   ? MAX_RECENT    :
                    (entries == favorite_entries) ? MAX_FAVORITES : MAX_GAMES;
    while (fgets(line, sizeof(line), f) && *count < max_count) {
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

__attribute__((unused)) static void load_recent(void) {
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

static void load_most_played(void) {
    most_played_count = 0;
    most_played_sel = 0;
    most_played_offset = 0;

    if (access(MOST_PLAYED_DB, R_OK) != 0) {
        log_kv("most_played db not found", MOST_PLAYED_DB);
        return;
    }

    sqlite3 *db = NULL;
    if (sqlite3_open_v2(MOST_PLAYED_DB, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
        log_kv("most_played open failed", db ? sqlite3_errmsg(db) : MOST_PLAYED_DB);
        if (db) sqlite3_close(db);
        return;
    }

    const char *sql =
        "SELECT rom.name, rom.file_path, "
        "SUM(play_activity.play_time) AS total_secs "
        "FROM rom JOIN play_activity ON rom.id = play_activity.rom_id "
        "WHERE play_activity.play_time IS NOT NULL "
        "GROUP BY rom.id HAVING total_secs > 60 "
        "ORDER BY total_secs DESC LIMIT 500;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        log_kv("most_played prepare failed", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW && most_played_count < MAX_GAMES) {
        const char *name  = (const char *)sqlite3_column_text(stmt, 0);
        const char *fpath = (const char *)sqlite3_column_text(stmt, 1);
        long long   secs  = sqlite3_column_int64(stmt, 2);
        if (!name || !fpath) continue;

        PlayEntry *e = &most_played_entries[most_played_count];
        memset(e, 0, sizeof(*e));
        e->play_secs = (int)secs;

        strncpy(e->label, name, sizeof(e->label) - 1);

        if (fpath[0] == '/')
            snprintf(e->rompath, sizeof(e->rompath), "%s", fpath);
        else
            snprintf(e->rompath, sizeof(e->rompath), ROMS_ROOT "/%s", fpath);

        System *entry_system = find_system_for_rompath(e->rompath);
        if (!entry_system) {
            log_kv("most_played no launcher for", e->rompath);
            continue;
        }
        snprintf(e->launch, sizeof(e->launch), "%s/launch.sh", entry_system->emu_dir);
        system_from_launch(e->launch, e->system, sizeof(e->system));

        most_played_count++;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    log_int("most_played loaded", most_played_count);
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
        if (favorite_count < MAX_FAVORITES) {
            PlayEntry *e = &favorite_entries[favorite_count++];
            memset(e, 0, sizeof(*e));
            strncpy(e->label,   label,   sizeof(e->label)   - 1);
            strncpy(e->rompath, rompath, sizeof(e->rompath) - 1);
            strncpy(e->launch,  launch,  sizeof(e->launch)  - 1);
            system_from_launch(e->launch, e->system, sizeof(e->system));
        }
    }

    /* Re-sort and write back */
    qsort(favorite_entries, favorite_count, sizeof(PlayEntry), cmp_play_entry);
    if (favorite_count == 0) {
        favorite_sel = 0;
        favorite_offset = 0;
    } else {
        if (favorite_sel >= favorite_count) favorite_sel = favorite_count - 1;
        if (favorite_offset > favorite_sel) favorite_offset = favorite_sel;
    }

    char tmp[sizeof(FAV_PATH) + 8];
    FILE *f = open_atomic_file(FAV_PATH, tmp, sizeof(tmp));
    if (!f) {
        log_errno_msg("toggle_favorite write failed", FAV_PATH);
        load_favorites();
        return;
    }
    for (int i = 0; i < favorite_count; i++) {
        fputs("{\"label\":", f);
        json_write_string(f, favorite_entries[i].label);
        fputs(",\"rompath\":", f);
        json_write_string(f, favorite_entries[i].rompath);
        fputs(",\"launch\":", f);
        json_write_string(f, favorite_entries[i].launch);
        fputs("}\n", f);
    }
    if (!commit_atomic_file(f, tmp, FAV_PATH)) load_favorites();
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

    char tmp[sizeof(CMD_PATH) + 8];
    FILE *f = open_atomic_file(CMD_PATH, tmp, sizeof(tmp));
    if (!f) {
        log_errno_msg("cmd open failed", CMD_PATH);
        return;
    }
    char launch[sizeof(sys->emu_dir) + 16];
    snprintf(launch, sizeof(launch), "%s/launch.sh", sys->emu_dir);
    if (!write_onion_game_command(f, launch, game->path)) {
        fclose(f);
        unlink(tmp);
        log_kv("unsupported launch path", game->path);
        return;
    }
    if (!commit_atomic_file(f, tmp, CMD_PATH)) return;

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

    char tmp[sizeof(CMD_PATH) + 8];
    FILE *f = open_atomic_file(CMD_PATH, tmp, sizeof(tmp));
    if (!f) {
        log_errno_msg("cmd open failed", CMD_PATH);
        return;
    }
    if (!write_onion_game_command(f, entry->launch, entry->rompath)) {
        fclose(f);
        unlink(tmp);
        log_kv("unsupported launch path", entry->rompath);
        return;
    }
    if (!commit_atomic_file(f, tmp, CMD_PATH)) return;
    if (chmod(CMD_PATH, 0755) != 0) log_errno_msg("cmd chmod failed", CMD_PATH);
    log_file_state("cmd file", CMD_PATH);
    play_launch();
    stop_audio();
    running = 0;
}

static void launch_app_cmd(const char *cmd) {
    log_kv("launch app cmd", cmd);
    char tmp[sizeof(CMD_PATH) + 8];
    FILE *f = open_atomic_file(CMD_PATH, tmp, sizeof(tmp));
    if (!f) {
        log_errno_msg("cmd open failed", CMD_PATH);
        return;
    }
    fprintf(f, "#!/bin/sh\n%s\n", cmd);
    if (!commit_atomic_file(f, tmp, CMD_PATH)) return;
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
    int rc = system(cmd);
    if (rc != 0) log_int("exec power cmd rc", rc);
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
    return SETTINGS_ENTRIES[i].is_header ? SETTINGS_SECTION_H : SETTINGS_ROW_H;
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
    int max_scroll = total_settings_height() - BROWSER_BODY_H;
    if (max_scroll < 0) max_scroll = 0;
    if (settings_scroll_px > max_scroll) settings_scroll_px = max_scroll;
    settings_val_valid = 0;
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
        int v = json_int_file(POCKETOS_ROOT "/system.json", "brightness", 6);
        if (delta == 0) delta = 1;
        apply_brightness(v + delta);
        play_select();
    } else if (strcmp(k, "lumination") == 0) {
        adjust_csc_field("lumination", 0, 20, 5, delta == 0 ? 1 : delta);
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
    } else if (strcmp(k, "appearance") == 0) {
        int dark = read_config_int("pocketosAppearance", 0);
        write_config_int("pocketosAppearance", !dark);
        reload_theme_palette();
        clear_text_cache();
        play_select();
    } else if (strcmp(k, "power") == 0 && delta == 0) {
        run_settings_action(SETTINGS_ENTRIES[settings_sel].cmd);
    } else {
        play_select();
    }
    settings_val_valid = 0;  /* any adjustment invalidates cached display values */
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
    if (strcmp(k, "brightness")  == 0) return json_int_file(POCKETOS_ROOT "/system.json", "brightness",  6);
    if (strcmp(k, "lumination")  == 0) return json_int_file(POCKETOS_ROOT "/system.json", "lumination",  5);
    if (strcmp(k, "saturation")  == 0) return json_int_file(POCKETOS_ROOT "/system.json", "saturation", 10);
    if (strcmp(k, "hue")         == 0) return json_int_file(POCKETOS_ROOT "/system.json", "hue",        10);
    if (strcmp(k, "contrast")    == 0) return json_int_file(POCKETOS_ROOT "/system.json", "contrast",   10);
    if (strcmp(k, "bluelightlvl")== 0) return read_config_int("display/blueLightLevel", 0);
    if (strcmp(k, "pwmfreq")     == 0) return read_config_int("pwmfrequency", 7);
    if (strcmp(k, "audio")       == 0) return json_int_file(POCKETOS_ROOT "/system.json", "vol",        15);
    if (strcmp(k, "vibration")   == 0) return read_config_int("vibration", 2);
    return 0;
}

static void clear_text_cache(void) {
    for (int i = 0; i < text_cache_count; i++) {
        SDL_FreeSurface(text_cache[i].surface);
        text_cache[i].surface = NULL;
    }
    text_cache_count = 0;
    text_cache_frame = 0;
}

static void draw_text(TTF_Font *font, const char *text, int x, int y, SDL_Color col) {
    if (!text || text[0] == '\0') return;

    text_cache_frame++;
    Uint32 ck = ((Uint32)col.r << 16) | ((Uint32)col.g << 8) | col.b;

    /* Look for a cached surface */
    SDL_Surface *s = NULL;
    int slot = -1;
    for (int i = 0; i < text_cache_count; i++) {
        if (text_cache[i].font == font &&
            text_cache[i].color_key == ck &&
            strncmp(text_cache[i].text, text, sizeof(text_cache[i].text) - 1) == 0) {
            text_cache[i].last_use = text_cache_frame;
            s = text_cache[i].surface;
            slot = i;
            break;
        }
    }

    if (!s) {
        /* Miss — render and cache */
        s = TTF_RenderUTF8_Blended(font, text, col);
        if (!s) return;

        if (text_cache_count < TEXT_CACHE_MAX) {
            slot = text_cache_count++;
        } else {
            /* Evict LRU slot */
            unsigned int oldest = text_cache[0].last_use;
            slot = 0;
            for (int i = 1; i < TEXT_CACHE_MAX; i++) {
                if (text_cache[i].last_use < oldest) {
                    oldest = text_cache[i].last_use;
                    slot = i;
                }
            }
            SDL_FreeSurface(text_cache[slot].surface);
        }
        text_cache[slot].font = font;
        text_cache[slot].color_key = ck;
        strncpy(text_cache[slot].text, text, sizeof(text_cache[slot].text) - 1);
        text_cache[slot].text[sizeof(text_cache[slot].text) - 1] = '\0';
        text_cache[slot].surface  = s;
        text_cache[slot].last_use = text_cache_frame;
    }

    SDL_Rect dst = { x, y, 0, 0 };
    SDL_BlitSurface(s, NULL, screen, &dst);
}

static int text_w(TTF_Font *font, const char *text) {
    int w = 0;
    TTF_SizeUTF8(font, text, &w, NULL);
    return w;
}

/* Formats seconds as "12h 34m" / "34m" / "<1m" into out. */
__attribute__((unused)) static void format_playtime(int secs, char *out, int outlen) {
    if (secs < 60) { snprintf(out, outlen, "<1m"); return; }
    int mins = secs / 60;
    int hrs  = mins / 60;
    mins %= 60;
    if (hrs > 0) snprintf(out, outlen, "%dh %dm", hrs, mins);
    else         snprintf(out, outlen, "%dm", mins);
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
    if (n == 1000) {
        log_msg("screenshot limit reached");
        return;
    }

    int w = screen->w, h = screen->h;
    int rowbytes = 1 + w * 3;
    uint8_t *raw = malloc((size_t)h * rowbytes);
    if (!raw) {
        log_msg("screenshot raw allocation failed");
        return;
    }
    if (SDL_LockSurface(screen) != 0) {
        free(raw);
        log_sdl_error("screenshot lock");
        return;
    }
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
    if (!comp) {
        free(raw);
        log_msg("screenshot compression allocation failed");
        return;
    }
    int zresult = compress2(comp, &comp_len, raw, (uLong)h * rowbytes, 6);
    free(raw);
    if (zresult != Z_OK) {
        free(comp);
        log_int("screenshot compression failed", zresult);
        return;
    }

    FILE *f = fopen(out, "wb");
    if (!f) {
        free(comp);
        log_errno_msg("screenshot fopen failed", out);
        return;
    }

    /* PNG signature and header */
    static const uint8_t sig[8] = {137,80,78,71,13,10,26,10};
    fwrite(sig, 8, 1, f);
    uint8_t ihdr[13] = {0};
    png_put_u32be(ihdr,     (uint32_t)w);
    png_put_u32be(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = 2;
    png_write_chunk(f, "IHDR", ihdr, 13);
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

// Icons follow SC_TEXT so they adapt to any theme automatically.
static Uint32 icon_accent(const char *name) {
    (void)name;
    return RGBA(SC_TEXT.r, SC_TEXT.g, SC_TEXT.b);
}

static void draw_builtin_icon(const char *name, int x, int y, int w, int h, int selected) {
    // Retro silhouette: no colored bg square, just the symbol on the surface.
    // On cream: per-category accent color. On lavender (selected): dark navy.
    // bg punches "holes" (ring cutouts etc.) using the current surface bg.
    Uint32 sym = selected ? RGBA(SC_WHITE.r, SC_WHITE.g, SC_WHITE.b) : icon_accent(name);
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
        fill_rrect(x + 2, y + 2, 26, 26, 5, C_SEL);
        draw_text(font_small, label, x + 10, y + 6, SC_WHITE);
    }
    draw_text(font_small, text, x + 38, y + 7, SC_TEXT);
}

// Truncate a string to fit inside max_px pixels using the given font
static void truncate_to_fit(TTF_Font *font, const char *src, char *dst, int dstlen, int max_px) {
    if (src != dst) copy_truncated(dst, (size_t)dstlen, src);
    else if (dstlen > 0) dst[dstlen - 1] = '\0';
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
        copy_truncated(out1, (size_t)buf1, src);
        out2[0] = '\0';
        return;
    }
    copy_truncated(out1, (size_t)buf1, src);
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
        copy_truncated(out1, (size_t)buf1, src);
        truncate_to_fit(font, out1, out1, buf1, max_px);
        out2[0] = '\0';
        return;
    }
    copy_truncated(out2, (size_t)buf2, src + split + 1);
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
    static int utc_off_cache = 0;
    static time_t utc_off_last = 0;
    if (t - utc_off_last > 30) { utc_off_cache = json_int_file(POCKETOS_ROOT "/system.json", "utcoffset", 0); utc_off_last = t; }
    if (utc_off_cache) t += utc_off_cache * 3600;  // manual fine-tune on top of auto TZ
    struct tm *tm = localtime(&t);
    char clk[16];
    snprintf(clk, sizeof(clk), "%02d:%02d", tm->tm_hour, tm->tm_min);
    draw_text_center(font_body, clk, 0, SCREEN_W, mid_y, SC_WHITE);

    // Right: battery% in green, then battery bar
    int batt = read_battery();
    char bstr[16];
    if (batt >= 0) snprintf(bstr, sizeof(bstr), "%d%%", batt);
    else           snprintf(bstr, sizeof(bstr), "--%%");
    SDL_Color batt_col;
    if      (batt < 0)   batt_col = SC_DIM;                                         // unknown
    else if (batt > 60)  batt_col = (SDL_Color){0x47, 0x83, 0x3c, 255};            // green
    else if (batt > 30)  batt_col = (SDL_Color){0x97, 0x82, 0x29, 255};            // yellow
    else                 batt_col = (SDL_Color){0x82, 0x27, 0x27, 255};            // red
    int bw_str = text_w(font_body, bstr);
    draw_text(font_body, bstr, SCREEN_W - bw_str - 10, mid_y, batt_col);
}

// ── Hint bar ──────────────────────────────────────────────────────────────────

// Hint bar background — derives from C_BAR so any theme auto-adapts.
static void draw_hint_base(void) {
    int y = SCREEN_H - HINT_H;
    Uint8 r0, g0, b0;
    SDL_GetRGB(C_BAR, screen->format, &r0, &g0, &b0);
    // Slightly lighter at top, base at bottom, accent border line
    Uint8 r1 = (Uint8)(r0 + (255-r0)*18/255);
    Uint8 g1 = (Uint8)(g0 + (255-g0)*18/255);
    Uint8 b1 = (Uint8)(b0 + (255-b0)*18/255);
    Uint8 r2 = (Uint8)(r0 + (255-r0)*38/255);
    Uint8 g2 = (Uint8)(g0 + (255-g0)*38/255);
    Uint8 b2 = (Uint8)(b0 + (255-b0)*38/255);
    fill_rect(0, y,            SCREEN_W, HINT_H / 2, RGBA(r1, g1, b1));
    fill_rect(0, y + HINT_H/2, SCREEN_W, HINT_H - HINT_H/2, C_BAR);
    fill_rect(0, y, SCREEN_W, 1, RGBA(r2, g2, b2));  // top border
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
    // L/R: follow selection theme colors
    Uint32 clr_bg = C_SEL, clr_bd = C_SEL_BORDER;

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
    /* L/R bumpers cycle sections; show the neighbour names as labels */
    const char *l_sec = HOME_SEC_NAME[(home_section + 2) % 3];
    const char *r_sec = HOME_SEC_NAME[(home_section + 1) % 3];
    draw_hints_row("Select", NULL, l_sec, r_sec, "Section", NULL, NULL);
    /* Right side: d-pad up/down hint */
    int hy = SCREEN_H - HINT_H;
    int ty = hy + (HINT_H - 16) / 2;
    int ax = SCREEN_W - 160;
    int icon_sz = HINT_H - 8;
    if (!draw_asset("xbox_dpad.png", ax, hy + 4, icon_sz, icon_sz)) {
        Uint32 c_lr = C_SEL;
        Uint32 c_bd = C_SEL_BORDER;
        int ay = hy + (HINT_H - 22) / 2;
        draw_shoulder_pill(ax, ay,      22, 11, c_bd, c_lr, "U");
        draw_shoulder_pill(ax, ay + 12, 22, 11, c_bd, c_lr, "D");
    }
    draw_text(font_small, "Navigate", ax + icon_sz + 6, ty, SC_WHITE);
}

// ── Home browse (section 0 — full-width genre list) ──────────────────────────

static void draw_home_browse(int list_y, int avail) {
    if (!browse_genre_count) {
        draw_text(font_body, "No genre data found.", 24, list_y + 24, SC_DIM);
        draw_text(font_body, "Run the PocketOS Genre Scanner on your computer,", 24, list_y + 52, SC_DIM);
        draw_text(font_body, "then point it at this SD card.", 24, list_y + 76, SC_DIM);
        return;
    }

    int rows = avail / ITEM_H;

    /* Clamp scroll */
    if (browse_genre_sel < browse_genre_off) browse_genre_off = browse_genre_sel;
    if (browse_genre_sel >= browse_genre_off + rows) browse_genre_off = browse_genre_sel - rows + 1;

    for (int i = 0; i < rows && browse_genre_off + i < browse_genre_count; i++) {
        int gi     = browse_genre_off + i;
        int iy     = list_y + i * ITEM_H;
        int is_sel = (gi == browse_genre_sel);

        if (is_sel) {
            draw_select_asset(6, iy + 3, SCREEN_W - 20, ITEM_H - 6);
            fill_rect(6, iy + 9, 4, ITEM_H - 18, C_SEL_BORDER);
        } else {
            fill_rect(24, iy + ITEM_H - 1, SCREEN_W - 48, 1, C_SEP);
        }

        SDL_Color tc  = is_sel ? SC_WHITE   : SC_TEXT;
        SDL_Color lbc = is_sel ? SC_SUB_SEL : SC_DIM;

        draw_text(font_body, browse_genres[gi].label,
                  24, iy + (ITEM_H - 22) / 2, tc);

        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d", browse_genres[gi].count);
        int lw = text_w(font_small, cnt);
        draw_text(font_small, cnt,
                  SCREEN_W - 50 - lw, iy + (ITEM_H - 18) / 2, lbc);

        Uint32 chev = is_sel ? RGBA(SC_WHITE.r, SC_WHITE.g, SC_WHITE.b) : C_SEP;
        draw_chevron(SCREEN_W - 32, iy + ITEM_H / 2, 8, 2, chev);
    }

    draw_scrollbar(SCREEN_W - 14, list_y, rows * ITEM_H, browse_genre_count, rows, browse_genre_off);
}

// ── Home screen ───────────────────────────────────────────────────────────────

static const char *home_item_subtitle(int action) {
    static char buf[64];
    switch (action) {
    case 1:
        if (favorite_count == 0) return "no favorites yet";
        snprintf(buf, sizeof(buf), "%d favorite%s", favorite_count, favorite_count == 1 ? "" : "s");
        return buf;
    case 2:
        snprintf(buf, sizeof(buf), "%d system%s", sys_count, sys_count == 1 ? "" : "s");
        return buf;
    case 3:
        return "explore by genre";
    case 7:
        if (most_played_count == 0) return "no play data yet";
        snprintf(buf, sizeof(buf), "%d game%s", most_played_count, most_played_count == 1 ? "" : "s");
        return buf;
    default:
        return NULL;
    }
}

__attribute__((unused)) static void draw_home(void) {
    draw_textured_bg(0, CONTENT_Y, SCREEN_W, CONTENT_H);
    draw_status();
    draw_home_hints();

    // ── Section tab strip ─────────────────────────────────────────────────────
    {
        int tw = SCREEN_W / 3;
        for (int i = 0; i < 3; i++) {
            int tx     = i * tw;
            int active = (i == home_section);
            if (active) {
                fill_rect(tx, CONTENT_Y, tw, HOME_TAB_H, C_PANEL_HI);
                fill_rect(tx + 2, CONTENT_Y + HOME_TAB_H - 3, tw - 4, 3, C_SEL);
            }
            SDL_Color tc = active ? SC_HDR : SC_DIM;
            draw_text_center(font_small, HOME_SEC_NAME[i],
                             tx, tw, CONTENT_Y + (HOME_TAB_H - 18) / 2, tc);
        }
        fill_rect(0, CONTENT_Y + HOME_TAB_H - 1, SCREEN_W, 1, C_DIVIDER);
    }

    // ── Section content ───────────────────────────────────────────────────────
    int avail_c = CONTENT_H - HOME_TAB_H;
    int list_y  = CONTENT_Y + HOME_TAB_H;

    if (home_section == 0) {
        draw_home_browse(list_y, avail_c);
        return;
    }

    // ── Item list — height fills the section, centered when fewer items ────────
    const HomeItem *items  = HOME_SECTIONS[home_section];
    int             count  = HOME_SEC_COUNT[home_section];
    int             sel    = home_sel_sec[home_section];
    int             item_h = count > 0 ? avail_c / count : avail_c;
    if (item_h > 160) item_h = 160;
    int             total  = item_h * count;
    int             base_y = list_y + (avail_c - total) / 2;

    /* Icon and text metrics derived from row height */
    int icon_sz = item_h - 24;
    if (icon_sz > 110) icon_sz = 110;
    int text_x  = 18 + icon_sz + 8;

    for (int row = 0; row < count; row++) {
        int iy     = base_y + row * item_h;
        int is_sel = (row == sel);

        if (is_sel) {
            draw_select_asset(6, iy + 3, SCREEN_W - 20, item_h - 6);
            /* Left accent bar — drawn over the selection highlight's straight left edge */
            fill_rect(6, iy + 9, 4, item_h - 18, C_SEL_BORDER);
        } else {
            fill_rect(text_x, iy + item_h - 1, SCREEN_W - text_x - 20, 1, C_SEP);
        }

        int icon_y = iy + (item_h - icon_sz) / 2;
        if (!draw_asset(items[row].icon, 14, icon_y, icon_sz, icon_sz))
            draw_builtin_icon(items[row].icon, 14, icon_y, icon_sz, icon_sz, is_sel);

        const char *sub     = home_item_subtitle(items[row].action);
        int         label_h = 30;
        int         sub_h   = sub ? 20 : 0;
        int         gap     = sub ? 4  : 0;
        int         block_h = label_h + gap + sub_h;
        int         block_y = iy + (item_h - block_h) / 2;

        draw_text(font_large, items[row].label, text_x, block_y,
                  is_sel ? SC_WHITE : SC_TEXT);
        if (sub)
            draw_text(font_small, sub, text_x + 2, block_y + label_h + gap,
                      is_sel ? SC_SUB_SEL : SC_DIM);

        Uint32 chev_col = is_sel ? RGBA(SC_WHITE.r, SC_WHITE.g, SC_WHITE.b) : C_SEP;
        draw_chevron(SCREEN_W - 32, iy + item_h / 2, 10, 2, chev_col);
    }

    /* Dead space below items (1-item sections): could add content here */
}

// ── Two-panel (systems + games) ───────────────────────────────────────────────

__attribute__((unused)) static void draw_panel(void) {
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

        SDL_Color tc = sel ? SC_WHITE : SC_TEXT;

        // Truncate full system name to fit left panel (no icon)
        const char *full_name = system_full_name(systems[si].label);
        char label[48];
        truncate_to_fit(font_body, full_name, label, sizeof(label), LEFT_W - 50);
        draw_text(font_body, label, 18, iy + (ITEM_H - 22) / 2, tc);
        Uint32 chev_sys = sel ? RGBA(SC_WHITE.r, SC_WHITE.g, SC_WHITE.b) : C_SEP;
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
        snprintf(hdr, sizeof(hdr), "GAMES — %s (%d)", system_full_name(systems[sys_sel].label), game_count);
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
            SDL_Color gtc = sel ? SC_WHITE : SC_TEXT;
            draw_text(font_game, line1, rx + 14 + star_w, ty, gtc);
            if (line2[0])
                draw_text(font_game, line2, rx + 14 + star_w, ty + GAME_LINE_GAP, gtc);
        }
        draw_scrollbar(SCREEN_W - 14, gy0, GAME_ROWS * GAME_ITEM_H, game_count, GAME_ROWS, game_offset);
    }
}

static void draw_entry_list(const char *title, PlayEntry *entries, int count,
                            int *sel, int *offset, int show_star) {
    char meta[24];
    snprintf(meta, sizeof(meta), "%d GAMES", count);
    draw_secondary_frame("LIBRARY", title, meta);
    draw_secondary_footer(4);

    int rows = BROWSER_ROWS;
    if (*sel < *offset) *offset = *sel;
    if (*sel >= *offset + rows) *offset = *sel - rows + 1;

    int y0 = 82;
    if (count == 0) {
        const char *hint1 = NULL, *hint2 = NULL;
        if (strcmp(title, "Favorites") == 0) {
            hint1 = "No favorites yet.";
            hint2 = "Press Y on any game in the Library to add one.";
        } else if (strcmp(title, "Recent") == 0) {
            hint1 = "No recent games yet.";
            hint2 = "Play a game and it will appear here.";
        } else if (strcmp(title, "Most Played") == 0) {
            hint1 = "No play data yet.";
            hint2 = "Play Activity data will appear here.";
        } else {
            hint1 = "Nothing here yet.";
        }
        draw_text_center(font_body, hint1, 0, SCREEN_W, 206, browser_secondary());
        if (hint2) draw_text_center(font_small, hint2, 0, SCREEN_W, 240, browser_dim());
        return;
    }

    for (int i = 0; i < rows && *offset + i < count; i++) {
        int idx = *offset + i;
        int iy = y0 + i * 64;
        int is_sel = idx == *sel;
        if (is_sel)
            fill_rect(10, iy + 3, SCREEN_W - 20, 58, browser_accent(4));
        else
            fill_rect(18, iy + 63, SCREEN_W - 36, 1, browser_rgb(0x1E, 0x21, 0x28));

        char label[240];
        truncate_to_fit(font_game, entries[idx].label, label, sizeof(label), 430);
        int tx = show_star ? 46 : 20;
        if (show_star)
            draw_text(font_body, "*", 24, iy + 20,
                      is_sel ? browser_dark_text() : browser_accent_text(0));
        draw_text(font_game, label, tx, iy + 16,
                  is_sel ? browser_dark_text() : browser_text());
        draw_browser_badge(492, iy + 20, 54, entries[idx].system, is_sel);
        if (entries[idx].play_secs > 0) {
            char pt[24];
            format_playtime_compact(entries[idx].play_secs, pt, sizeof(pt));
            int pt_w = text_w(font_small, pt);
            draw_text(font_small, pt, 616 - pt_w, iy + 23,
                      is_sel ? browser_dark_text() : browser_secondary());
        }
    }
    draw_browser_more(0, 414, SCREEN_W, count, *offset, rows, 4);
}

// ── Browse by genre ───────────────────────────────────────────────────────────

/* Returns 1 if needle appears as a whole word in hay (not inside a longer word). */
static int word_match(const char *hay, const char *needle) {
    const char *p = hay;
    size_t nlen = strlen(needle);
    while ((p = strstr(p, needle))) {
        int pre_ok  = (p == hay) || !isalpha((unsigned char)p[-1]);
        int post_ok = !isalpha((unsigned char)p[nlen]);
        if (pre_ok && post_ok) return 1;
        p++;
    }
    return 0;
}

/* Title-based franchise detection — runs after normalize_genre() and overrides
   the genre bucket so games with many entries get their own browseable section
   instead of being diluted across RPG / Platformer / etc. */
static void franchise_override(const char *title, char *genre, int genrelen) {
    static const struct { const char *kw; const char *cat; } FT[] = {
        { "pokemon",       "Pokemon"       },
        { "pok\xc3\xa9mon","Pokemon"       },  /* UTF-8 é */
        { "zelda",         "Zelda"         },
        { "mario",         "Mario"         },
        { "wario",         "Mario"         },
        { "sonic",         "Sonic"         },
        { "mega man",      "Mega Man"      },
        { "megaman",       "Mega Man"      },
        { "rockman",       "Mega Man"      },
        { "metroid",       "Metroid"       },
        { "castlevania",   "Castlevania"   },
        { "final fantasy", "Final Fantasy" },
        { "dragon quest",  "Dragon Quest"  },
        { "dragon ball",   "Dragon Ball"   },
        { "fire emblem",   "Fire Emblem"   },
        { "kirby",         "Kirby"         },
        { "donkey kong",   "Donkey Kong"   },
        { NULL, NULL }
    };
    char lower[256];
    int i = 0;
    for (; title[i] && i < 255; i++) lower[i] = (char)tolower((unsigned char)title[i]);
    lower[i] = '\0';
    for (int j = 0; FT[j].kw; j++) {
        if (word_match(lower, FT[j].kw)) {
            strncpy(genre, FT[j].cat, genrelen - 1);
            genre[genrelen - 1] = '\0';
            return;
        }
    }
}

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
    copy_truncated(out, (size_t)outlen, first);
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

static void xml_unescape(char *s) {
    static const struct { const char *ent; char ch; } map[] = {
        {"&amp;",  '&'}, {"&apos;", '\''}, {"&quot;", '"'},
        {"&lt;",   '<'}, {"&gt;",   '>'},  {NULL, 0}
    };
    char *w = s;
    while (*s) {
        if (*s == '&') {
            int replaced = 0;
            for (int i = 0; map[i].ent; i++) {
                size_t l = strlen(map[i].ent);
                if (strncmp(s, map[i].ent, l) == 0) {
                    *w++ = map[i].ch;
                    s += l;
                    replaced = 1;
                    break;
                }
            }
            if (!replaced) *w++ = *s++;
        } else {
            *w++ = *s++;
        }
    }
    *w = '\0';
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
            if (end) { int n = (int)(end-p); if (n > 239) n = 239; strncpy(cur_name, p, n); cur_name[n] = 0; xml_unescape(cur_name); }
        }
        if ((p = strstr(line, "<genre>"))) {
            p += 7; end = strstr(p, "</genre>");
            if (!end || browse_game_count >= BROWSE_GAME_MAX) { cur_path[0] = cur_name[0] = 0; continue; }
            char raw[128] = {0};
            int n = (int)(end-p); if (n > 127) n = 127;
            strncpy(raw, p, n);
            BrowseGame *g = &browse_game_pool[browse_game_count++];
            copy_truncated(g->title, sizeof(g->title),
                           cur_name[0] ? cur_name : cur_path);
            copy_truncated(g->system, sizeof(g->system), sys_folder);
            const char *fname = cur_path;
            if (fname[0] == '.' && fname[1] == '/') fname += 2;
            char system_rom_dir[256];
            if (!path_join(system_rom_dir, sizeof(system_rom_dir), ROMS_ROOT, sys_folder) ||
                !path_join(g->path, sizeof(g->path), system_rom_dir, fname)) {
                browse_game_count--;
                cur_path[0] = cur_name[0] = 0;
                continue;
            }
            normalize_genre(raw, g->genre, BROWSE_GENRE_LEN);
            franchise_override(g->title, g->genre, BROWSE_GENRE_LEN);
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

// ── Handheld redesign: primary game-browser shell ───────────────────────────

typedef struct {
    Uint32 bg, surface, line, raised, muted;
    int is_light;
    SDL_Color text, secondary, dim, dark_text;
    Uint32 accent[5];
    SDL_Color accent_text[5];
} BrowserPalette;

static BrowserPalette browser_palette;
static int browser_palette_ready = 0;

static int color_luma(SDL_Color color) {
    return (299 * color.r + 587 * color.g + 114 * color.b) / 1000;
}

static SDL_Color mix_color(SDL_Color a, SDL_Color b, int b_weight) {
    if (b_weight < 0) b_weight = 0;
    if (b_weight > 255) b_weight = 255;
    int a_weight = 255 - b_weight;
    return (SDL_Color){
        (Uint8)((a.r * a_weight + b.r * b_weight) / 255),
        (Uint8)((a.g * a_weight + b.g * b_weight) / 255),
        (Uint8)((a.b * a_weight + b.b * b_weight) / 255),
        0xFF
    };
}

static SDL_Color mapped_color(Uint32 pixel) {
    SDL_Color color = {0, 0, 0, 0xFF};
    SDL_GetRGB(pixel, screen->format, &color.r, &color.g, &color.b);
    return color;
}

static Uint32 mapped_pixel(SDL_Color color) {
    return SDL_MapRGB(screen->format, color.r, color.g, color.b);
}

static void refresh_browser_palette(void) {
    SDL_Color bg = mapped_color(C_BG);
    SDL_Color bar = mapped_color(C_BAR);
    SDL_Color sep = mapped_color(C_SEP);
    SDL_Color card = mapped_color(C_CARD);
    SDL_Color card_border = mapped_color(C_CARD_BORDER);
    const SDL_Color black = {0, 0, 0, 0xFF};
    const SDL_Color white = {255, 255, 255, 0xFF};
    int is_light = color_luma(bg) >= 145;

    SDL_Color base, surface, line, raised, muted;
    if (is_light) {
        base = mix_color(bg, white, 16);
        surface = mix_color(card, white, 8);
        line = mix_color(sep, card_border, 96);
        raised = mix_color(bar, white, color_luma(bar) < 120 ? 96 : 24);
        muted = mix_color(SC_DIM, bg, 78);
        browser_palette.text = color_luma(SC_TEXT) < 150 ? SC_TEXT : mix_color(SC_TEXT, black, 160);
        browser_palette.secondary = mix_color(browser_palette.text, bg, 92);
        browser_palette.dim = mix_color(browser_palette.text, bg, 145);
        browser_palette.dark_text = SC_WHITE;
    } else {
        SDL_Color candidates[] = {bg, bar, card, SC_TEXT, SC_WHITE};
        SDL_Color darkest = candidates[0];
        SDL_Color lightest = candidates[0];
        for (int i = 1; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
            if (color_luma(candidates[i]) < color_luma(darkest)) darkest = candidates[i];
            if (color_luma(candidates[i]) > color_luma(lightest)) lightest = candidates[i];
        }
        base = mix_color(darkest, black, 56);
        surface = mix_color(base, lightest, 12);
        line = mix_color(base, lightest, 25);
        raised = mix_color(base, lightest, 40);
        muted = mix_color(base, lightest, 76);
        browser_palette.text = lightest;
        browser_palette.secondary = mix_color(lightest, base, 105);
        browser_palette.dim = mix_color(lightest, base, 158);
        browser_palette.dark_text = base;
    }

    browser_palette.bg = mapped_pixel(base);
    browser_palette.surface = mapped_pixel(surface);
    browser_palette.line = mapped_pixel(line);
    browser_palette.raised = mapped_pixel(raised);
    browser_palette.muted = mapped_pixel(muted);
    browser_palette.is_light = is_light;

    SDL_Color theme_sel = mapped_color(C_SEL);
    SDL_Color theme_hi = mapped_color(C_SEL_HI);
    SDL_Color theme_border = mapped_color(C_SEL_BORDER);
    const SDL_Color semantic[5] = {
        theme_sel,
        mix_color(theme_sel, theme_hi, 112),
        theme_hi,
        mix_color(theme_sel, theme_border, 120),
        theme_border
    };
    for (int i = 0; i < 5; i++) {
        SDL_Color accent = semantic[i];
        if (is_light) {
            for (int step = 0; step < 5 && color_luma(accent) > 132; step++)
                accent = mix_color(accent, black, 34);
        } else {
            for (int step = 0; step < 5 && color_luma(accent) < 145; step++)
                accent = mix_color(accent, browser_palette.text, 36);
        }
        browser_palette.accent[i] = mapped_pixel(accent);
        browser_palette.accent_text[i] = accent;
    }
    browser_palette_ready = 1;
}

static SDL_Color browser_text(void) {
    return browser_palette_ready ? browser_palette.text
                                 : (SDL_Color){0x25, 0x19, 0x34, 0xFF};
}
static SDL_Color browser_secondary(void) {
    return browser_palette_ready ? browser_palette.secondary
                                 : (SDL_Color){0x66, 0x58, 0x70, 0xFF};
}
static SDL_Color browser_dim(void) {
    return browser_palette_ready ? browser_palette.dim
                                 : (SDL_Color){0x7B, 0x6C, 0x80, 0xFF};
}
static SDL_Color browser_dark_text(void) {
    return browser_palette_ready ? browser_palette.dark_text
                                 : (SDL_Color){0xFF, 0xFF, 0xFF, 0xFF};
}

static Uint32 browser_rgb(Uint8 r, Uint8 g, Uint8 b) {
    if (browser_palette_ready) {
        if (r == 0x0E && g == 0x0F && b == 0x13) return browser_palette.bg;
        if (r == 0x12 && g == 0x14 && b == 0x1A) return browser_palette.surface;
        if (r == 0x1E && g == 0x21 && b == 0x28) return browser_palette.line;
        if (r == 0x2B && g == 0x30 && b == 0x3A) return browser_palette.raised;
        if (r == 0x56 && g == 0x5A && b == 0x64) return browser_palette.muted;
        if ((r == 0x14 && g == 0x10 && b == 0x08) ||
            (r == 0x08 && g == 0x14 && b == 0x0C) ||
            (r == 0x0F && g == 0x0A && b == 0x18) ||
            (r == 0x14 && g == 0x08 && b == 0x08) ||
            (r == 0x08 && g == 0x0D && b == 0x16))
            return browser_palette.bg;
    }
    return RGBA(r, g, b);
}

static Uint32 browser_accent(int category) {
    if (category < 0 || category > 4) category = 0;
    if (browser_palette_ready) return browser_palette.accent[category];
    if (category == 1) return RGBA(0x8D, 0x5B, 0xFF);
    if (category == 2) return RGBA(0x6D, 0x2B, 0xFF);
    if (category == 3) return RGBA(0xA4, 0x7A, 0xFF);
    if (category == 4) return RGBA(0x5C, 0x1F, 0xE0);
    return RGBA(0x7D, 0x3C, 0xFF);
}

static SDL_Color browser_accent_text(int category) {
    if (category < 0 || category > 4) category = 0;
    if (browser_palette_ready) return browser_palette.accent_text[category];
    if (category == 1) return (SDL_Color){0x8D, 0x5B, 0xFF, 0xFF};
    if (category == 2) return (SDL_Color){0x6D, 0x2B, 0xFF, 0xFF};
    if (category == 3) return (SDL_Color){0xA4, 0x7A, 0xFF, 0xFF};
    if (category == 4) return (SDL_Color){0x5C, 0x1F, 0xE0, 0xFF};
    return (SDL_Color){0x7D, 0x3C, 0xFF, 0xFF};
}

static Uint32 browser_accent_tint(int category) {
    SDL_Color tint = mix_color(browser_dark_text(), browser_accent_text(category), 44);
    return mapped_pixel(tint);
}

static const char *browser_badge_label(const char *label) {
    if (!label || !label[0]) return "--";
    if (strcasecmp(label, "NES") == 0) return "FC";
    if (strcasecmp(label, "SNES") == 0) return "SFC";
    if (strcasecmp(label, "GENESIS") == 0 || strcasecmp(label, "GEN") == 0) return "MD";
    if (strcasecmp(label, "MAME") == 0 || strcasecmp(label, "ADVMAME") == 0 ||
        strcasecmp(label, "ARCADE") == 0) return "ARC";
    return label;
}

static SDL_Color browser_system_color(const char *label) {
    const char *tag = browser_badge_label(label);
    if (strcasecmp(tag, "FC") == 0)  return (SDL_Color){0xFF, 0x7A, 0x7A, 0xFF};
    if (strcasecmp(tag, "SFC") == 0) return (SDL_Color){0xC9, 0xA7, 0xFF, 0xFF};
    if (strcasecmp(tag, "GB") == 0 || strcasecmp(tag, "GBC") == 0)
        return (SDL_Color){0x9E, 0xE0, 0x7A, 0xFF};
    if (strcasecmp(tag, "GBA") == 0) return (SDL_Color){0x7F, 0xB0, 0xFF, 0xFF};
    if (strcasecmp(tag, "MD") == 0)  return (SDL_Color){0x57, 0xD0, 0x8C, 0xFF};
    if (strcasecmp(tag, "ARC") == 0) return (SDL_Color){0xFF, 0x8A, 0xC2, 0xFF};
    return (SDL_Color){0xB4, 0xB8, 0xC2, 0xFF};
}

static void draw_browser_badge(int x, int y, int w, const char *system, int selected) {
    SDL_Color col = browser_system_color(system);
    Uint32 border = browser_rgb(col.r, col.g, col.b);
    Uint32 inside = selected ? browser_rgb(0x14, 0x10, 0x08)
                             : browser_rgb(0x12, 0x14, 0x1A);
    fill_rrect(x, y, w, 22, 2, border);
    fill_rrect(x + 1, y + 1, w - 2, 20, 1, inside);

    char tag[8];
    copy_truncated(tag, sizeof(tag), browser_badge_label(system));
    draw_text_center(font_small, tag, x, w, y + 4, col);
}

static void draw_browser_header(int category) {
    static const char *tabs[] = {"MOST PLAYED", "BROWSE", "LIBRARY", "FAVORITES", "SETTINGS"};
    static const int tab_x[] = {32, 137, 209, 283, 381};
    static const int tab_w[] = {105, 72, 74, 98, 92};

    fill_rect(0, 0, SCREEN_W, BROWSER_HEADER_H, browser_rgb(0x12, 0x14, 0x1A));
    fill_rect(0, BROWSER_HEADER_H - 1, SCREEN_W, 1, browser_rgb(0x1E, 0x21, 0x28));

    fill_rrect(5, 17, 22, 18, 2, browser_rgb(0x1E, 0x21, 0x28));
    draw_text_center(font_small, "L", 5, 22, 19, browser_secondary());
    fill_rrect(478, 17, 22, 18, 2, browser_rgb(0x1E, 0x21, 0x28));
    draw_text_center(font_small, "R", 478, 22, 19, browser_secondary());

    for (int i = 0; i < 5; i++) {
        SDL_Color col = i == category ? browser_accent_text(category) : browser_dim();
        draw_text_center(font_small, tabs[i], tab_x[i], tab_w[i], 19, col);
        if (i == category)
            fill_rect(tab_x[i] + 8, BROWSER_HEADER_H - 3, tab_w[i] - 16, 3,
                      browser_accent(category));
    }

    int batt = read_battery();
    char bstr[12];
    if (batt >= 0) snprintf(bstr, sizeof(bstr), "%d%%", batt);
    else snprintf(bstr, sizeof(bstr), "--%%");
    SDL_Color batt_col = batt >= 0 && batt <= 25
                         ? (SDL_Color){0xFF, 0x7A, 0x7A, 0xFF}
                         : browser_secondary();
    int bw = text_w(font_small, bstr);
    draw_text(font_small, bstr, 584 - bw, 19, batt_col);
    fill_rect(592, 18, 32, 16, browser_rgb(0x56, 0x5A, 0x64));
    fill_rect(594, 20, 28, 12, browser_rgb(0x12, 0x14, 0x1A));
    if (batt > 0) {
        int level = (26 * (batt > 100 ? 100 : batt)) / 100;
        if (level < 1) level = 1;
        fill_rect(595, 21, level, 10, browser_rgb(batt_col.r, batt_col.g, batt_col.b));
    }
    fill_rect(624, 22, 3, 8, browser_rgb(0x56, 0x5A, 0x64));
}

static int draw_browser_hint(int x, const char *key, const char *label, Uint32 key_col) {
    int key_w = text_w(font_small, key) + 8;
    if (key_w < 20) key_w = 20;
    int y = SCREEN_H - BROWSER_FOOTER_H + 11;
    fill_rrect(x, y, key_w, 18, 2, key_col);
    fill_rrect(x + 1, y + 1, key_w - 2, 16, 1, browser_rgb(0x12, 0x14, 0x1A));
    draw_text_center(font_small, key, x, key_w, y + 2, browser_text());
    draw_text(font_small, label, x + key_w + 5, y + 2, browser_secondary());
    return key_w + 5 + text_w(font_small, label) + 16;
}

static void draw_browser_footer(int mode, int category) {
    int y = SCREEN_H - BROWSER_FOOTER_H;
    fill_rect(0, y, SCREEN_W, BROWSER_FOOTER_H, browser_rgb(0x12, 0x14, 0x1A));
    fill_rect(0, y, SCREEN_W, 1, browser_rgb(0x1E, 0x21, 0x28));

    int x = 14;
    x += draw_browser_hint(x, "A", mode == 1 || mode == 3 || mode == 6 ? "OPEN" : "PLAY",
                           browser_rgb(0x3E, 0xCF, 0x6E));
    if (mode == 2 || mode == 4)
        x += draw_browser_hint(x, "B", "BACK", browser_rgb(0xFF, 0x7A, 0x7A));
    if (mode == 2 || mode == 4 || mode == 5)
        x += draw_browser_hint(x, "Y", "FAV", browser_rgb(0xFF, 0xAD, 0x33));
    if (mode == 0 || mode == 2 || mode == 4 || mode == 5)
        draw_browser_hint(x, "X", "OPTS", browser_rgb(0x7F, 0xB0, 0xFF));
    if (mode == 3)
        draw_browser_hint(x, "START", "RANDOM", browser_rgb(0xA7, 0x8B, 0xFA));

    draw_browser_hint(505, "L/R", "CATEGORY", browser_accent(category));
}

static void draw_browser_more(int x, int y, int w, int total, int offset,
                              int visible, int category) {
    int remaining = total - offset - visible;
    char text[24];
    if (remaining > 0) snprintf(text, sizeof(text), "%d MORE", remaining);
    else snprintf(text, sizeof(text), "END");
    draw_text_center(font_small, text, x, w, y, remaining > 0
                     ? browser_accent_text(category) : browser_dim());
}

static void draw_genre_glyph(const char *label, int x, int y, Uint32 color) {
    unsigned int hash = 0;
    for (const unsigned char *p = (const unsigned char *)label; *p; p++)
        hash = hash * 33u + *p;
    if ((hash % 3u) == 0) {
        fill_rect(x + 4, y, 4, 12, color);
        fill_rect(x, y + 4, 12, 4, color);
    } else if ((hash % 3u) == 1) {
        fill_rect(x + 2, y, 8, 2, color);
        fill_rect(x, y + 2, 12, 8, color);
        fill_rect(x + 2, y + 10, 8, 2, color);
        fill_rect(x + 4, y + 4, 4, 4, browser_rgb(0x12, 0x14, 0x1A));
    } else {
        fill_rect(x + 5, y, 2, 12, color);
        fill_rect(x, y + 5, 12, 2, color);
        fill_rect(x + 2, y + 2, 8, 8, color);
    }
}

static void format_playtime_compact(int secs, char *out, int outlen) {
    int mins = secs / 60;
    if (mins >= 60) snprintf(out, outlen, "%dH", mins / 60);
    else if (mins > 0) snprintf(out, outlen, "%dM", mins);
    else snprintf(out, outlen, "<1M");
}

static int library_total_games(void) {
    int total = 0;
    for (int i = 0; i < sys_count; i++) total += systems[i].rom_count;
    return total;
}

static void draw_most_played_shell(void) {
    int category = 0;
    fill_rect(0, 0, SCREEN_W, SCREEN_H, browser_rgb(0x0E, 0x0F, 0x13));
    draw_browser_header(category);
    draw_browser_footer(0, category);

    if (most_played_sel < most_played_offset) most_played_offset = most_played_sel;
    if (most_played_sel >= most_played_offset + BROWSER_ROWS)
        most_played_offset = most_played_sel - BROWSER_ROWS + 1;

    draw_text(font_small, "PLAY TIME", 18, 61, browser_dim());
    char count_text[24];
    snprintf(count_text, sizeof(count_text), "%d GAMES", most_played_count);
    draw_text(font_small, count_text, SCREEN_W - 18 - text_w(font_small, count_text),
              61, browser_dim());

    if (most_played_count == 0) {
        draw_text_center(font_body, "NO PLAY HISTORY YET", 0, SCREEN_W, 212,
                         browser_secondary());
        draw_text_center(font_small, "PLAY ACTIVITY WILL APPEAR HERE", 0, SCREEN_W,
                         244, browser_dim());
        return;
    }

    const int y0 = 82;
    const int row_h = 64;
    for (int row = 0; row < BROWSER_ROWS && most_played_offset + row < most_played_count; row++) {
        int idx = most_played_offset + row;
        int y = y0 + row * row_h;
        int selected = idx == most_played_sel;
        if (selected)
            fill_rect(10, y + 3, SCREEN_W - 20, row_h - 6, browser_accent(category));
        else
            fill_rect(18, y + row_h - 1, SCREEN_W - 36, 1, browser_rgb(0x1E, 0x21, 0x28));

        char rank[16];
        snprintf(rank, sizeof(rank), "%02d", idx + 1);
        draw_text(font_body, rank, 20, y + 20,
                  selected ? browser_dark_text() : browser_dim());

        char title[240];
        truncate_to_fit(font_game, most_played_entries[idx].label, title, sizeof(title), 400);
        draw_text(font_game, title, 64, y + 16,
                  selected ? browser_dark_text() : browser_text());
        draw_browser_badge(486, y + 20, 50, most_played_entries[idx].system, selected);

        char play[16];
        format_playtime_compact(most_played_entries[idx].play_secs, play, sizeof(play));
        int pw = text_w(font_body, play);
        draw_text(font_body, play, 618 - pw, y + 20,
                  selected ? browser_dark_text() : browser_secondary());
    }
    draw_browser_more(0, 414, SCREEN_W, most_played_count, most_played_offset,
                      BROWSER_ROWS, category);
}

static void draw_browse_shell(void) {
    int category = 1;
    const int left_w = 224;
    fill_rect(0, 0, SCREEN_W, SCREEN_H, browser_rgb(0x0E, 0x0F, 0x13));
    draw_browser_header(category);
    draw_browser_footer(state == STATE_BROWSE_GAMES ? 2 : 1, category);
    fill_rect(left_w, BROWSER_HEADER_H, 1, BROWSER_BODY_H,
              browser_rgb(0x1E, 0x21, 0x28));

    draw_text(font_small, "GENRES", 18, 65, browser_dim());
    draw_text(font_small, "GAMES", left_w + 18, 65, browser_dim());

    if (browse_genre_count == 0) {
        draw_text_center(font_body, "NO GENRE DATA FOUND", 0, SCREEN_W, 212,
                         browser_secondary());
        draw_text_center(font_small, "RUN THE POCKETOS GENRE SCANNER", 0, SCREEN_W,
                         244, browser_dim());
        return;
    }

    if (browse_genre_sel < browse_genre_off) browse_genre_off = browse_genre_sel;
    if (browse_genre_sel >= browse_genre_off + BROWSER_ROWS)
        browse_genre_off = browse_genre_sel - BROWSER_ROWS + 1;

    const int left_y0 = 92;
    for (int row = 0; row < BROWSER_ROWS && browse_genre_off + row < browse_genre_count; row++) {
        int idx = browse_genre_off + row;
        int y = left_y0 + row * 56;
        int selected = idx == browse_genre_sel;
        int focused = selected && state == STATE_BROWSE_CATS;
        if (focused)
            fill_rect(8, y + 3, left_w - 16, 50, browser_accent(category));
        else if (selected)
            fill_rect(8, y + 3, left_w - 16, 50, browser_accent_tint(category));
        else
            fill_rect(16, y + 55, left_w - 32, 1, browser_rgb(0x1E, 0x21, 0x28));

        Uint32 glyph = focused ? browser_rgb(0x08, 0x14, 0x0C) : browser_accent(category);
        draw_genre_glyph(browse_genres[idx].label, 18, y + 22, glyph);
        char label[48];
        truncate_to_fit(font_body, browse_genres[idx].label, label, sizeof(label), 132);
        draw_text(font_body, label, 42, y + 18,
                  focused ? browser_dark_text()
                          : selected ? browser_accent_text(category) : browser_text());
        char count[12];
        snprintf(count, sizeof(count), "%d", browse_genres[idx].count);
        draw_text(font_small, count, 205 - text_w(font_small, count), y + 20,
                  focused ? browser_dark_text() : browser_dim());
    }
    draw_browser_more(0, 402, left_w, browse_genre_count, browse_genre_off,
                      BROWSER_ROWS, category);

    BrowseGenre *genre = &browse_genres[browse_genre_sel];
    char genre_header[80];
    snprintf(genre_header, sizeof(genre_header), "%s / %d", genre->label, genre->count);
    char header_fit[80];
    truncate_to_fit(font_small, genre_header, header_fit, sizeof(header_fit), SCREEN_W - left_w - 104);
    draw_text(font_small, header_fit, left_w + 72, 65, browser_secondary());

    if (browse_game_sel < browse_game_off) browse_game_off = browse_game_sel;
    if (browse_game_sel >= browse_game_off + BROWSER_ROWS)
        browse_game_off = browse_game_sel - BROWSER_ROWS + 1;

    const int right_y0 = 92;
    for (int row = 0; row < BROWSER_ROWS && browse_game_off + row < genre->count; row++) {
        int local_idx = browse_game_off + row;
        int idx = genre->start + local_idx;
        int y = right_y0 + row * 62;
        int selected = state == STATE_BROWSE_GAMES && local_idx == browse_game_sel;
        if (selected)
            fill_rect(left_w + 8, y + 3, SCREEN_W - left_w - 16, 56, browser_accent(category));
        else
            fill_rect(left_w + 16, y + 61, SCREEN_W - left_w - 32, 1,
                      browser_rgb(0x1E, 0x21, 0x28));

        int title_x = left_w + 18;
        if (is_favorite(browse_game_pool[idx].path)) {
            fill_rect(title_x, y + 27, 6, 6,
                      selected ? browser_rgb(0x08, 0x14, 0x0C) : browser_accent(category));
            title_x += 14;
        }
        char title[240];
        truncate_to_fit(font_game, browse_game_pool[idx].title, title, sizeof(title),
                        568 - title_x);
        draw_text(font_game, title, title_x, y + 15,
                  selected ? browser_dark_text() : browser_text());
        draw_browser_badge(580, y + 20, 44, browse_game_pool[idx].system, selected);
    }
    draw_browser_more(left_w, 414, SCREEN_W - left_w, genre->count,
                      browse_game_off, BROWSER_ROWS, category);
}

static void draw_library_shell(void) {
    int category = 2;
    const int left_w = 260;
    fill_rect(0, 0, SCREEN_W, SCREEN_H, browser_rgb(0x0E, 0x0F, 0x13));
    draw_browser_header(category);
    draw_browser_footer(state == STATE_GAMES ? 4 : 3, category);
    fill_rect(left_w, BROWSER_HEADER_H, 1, BROWSER_BODY_H,
              browser_rgb(0x1E, 0x21, 0x28));

    draw_text(font_small, "SYSTEMS", 18, 65, browser_dim());
    char total[32];
    snprintf(total, sizeof(total), "%d GAMES", library_total_games());
    draw_text(font_small, total, left_w - 18 - text_w(font_small, total), 65, browser_dim());
    draw_text(font_small, sys_count > 0 ? system_full_name(systems[sys_sel].label) : "GAMES",
              left_w + 18, 65, browser_secondary());

    if (sys_sel < sys_offset) sys_offset = sys_sel;
    if (sys_sel >= sys_offset + LIBRARY_SYS_ROWS)
        sys_offset = sys_sel - LIBRARY_SYS_ROWS + 1;

    const int left_y0 = 92;
    for (int row = 0; row < LIBRARY_SYS_ROWS && sys_offset + row < sys_count; row++) {
        int idx = sys_offset + row;
        int y = left_y0 + row * 54;
        int selected = idx == sys_sel;
        int focused = selected && state == STATE_SYSTEMS;
        if (focused)
            fill_rect(8, y + 3, left_w - 16, 48, browser_accent(category));
        else if (selected)
            fill_rect(8, y + 3, left_w - 16, 48, browser_accent_tint(category));
        else
            fill_rect(16, y + 53, left_w - 32, 1, browser_rgb(0x1E, 0x21, 0x28));

        char label[64];
        truncate_to_fit(font_body, library_system_name(systems[idx].label), label,
                        sizeof(label), 190);
        draw_text(font_body, label, 18, y + 16,
                  focused ? browser_dark_text()
                          : selected ? browser_accent_text(category) : browser_text());
        char count[12];
        snprintf(count, sizeof(count), "%d", systems[idx].rom_count);
        draw_text(font_small, count, 242 - text_w(font_small, count), y + 18,
                  focused ? browser_dark_text() : browser_dim());
    }
    if (sys_count > LIBRARY_SYS_ROWS)
        draw_browser_more(0, 421, left_w, sys_count, sys_offset,
                          LIBRARY_SYS_ROWS, category);

    if (game_sel < game_offset) game_offset = game_sel;
    if (game_sel >= game_offset + BROWSER_ROWS)
        game_offset = game_sel - BROWSER_ROWS + 1;

    if (game_count == 0) {
        draw_text_center(font_body, "NO GAMES FOUND", left_w, SCREEN_W - left_w,
                         218, browser_secondary());
        draw_text_center(font_small, "CHECK THIS SYSTEM'S ROM FOLDER", left_w,
                         SCREEN_W - left_w, 250, browser_dim());
        return;
    }

    const int right_y0 = 92;
    for (int row = 0; row < BROWSER_ROWS && game_offset + row < game_count; row++) {
        int idx = game_offset + row;
        int y = right_y0 + row * 62;
        int selected = state == STATE_GAMES && idx == game_sel;
        if (selected)
            fill_rect(left_w + 8, y + 3, SCREEN_W - left_w - 16, 56, browser_accent(category));
        else
            fill_rect(left_w + 16, y + 61, SCREEN_W - left_w - 32, 1,
                      browser_rgb(0x1E, 0x21, 0x28));

        int title_x = left_w + 18;
        if (is_favorite(games[idx].path)) {
            fill_rect(title_x, y + 27, 6, 6,
                      selected ? browser_rgb(0x0F, 0x0A, 0x18) : browser_accent(category));
            title_x += 14;
        }
        char title[240];
        truncate_to_fit(font_game, games[idx].name, title, sizeof(title),
                        SCREEN_W - title_x - 18);
        draw_text(font_game, title, title_x, y + 15,
                  selected ? browser_dark_text() : browser_text());
    }
    draw_browser_more(left_w, 414, SCREEN_W - left_w, game_count, game_offset,
                      BROWSER_ROWS, category);
}

static void draw_favorites_shell(void) {
    int category = 3;
    fill_rect(0, 0, SCREEN_W, SCREEN_H, browser_rgb(0x0E, 0x0F, 0x13));
    draw_browser_header(category);
    draw_browser_footer(5, category);

    if (favorite_sel < favorite_offset) favorite_offset = favorite_sel;
    if (favorite_sel >= favorite_offset + BROWSER_ROWS)
        favorite_offset = favorite_sel - BROWSER_ROWS + 1;

    draw_text(font_small, "SAVED GAMES", 18, 61, browser_dim());
    char count_text[24];
    snprintf(count_text, sizeof(count_text), "%d FAVORITES", favorite_count);
    draw_text(font_small, count_text, SCREEN_W - 18 - text_w(font_small, count_text),
              61, browser_dim());

    if (favorite_count == 0) {
        draw_text_center(font_body, "NO FAVORITES YET", 0, SCREEN_W, 212,
                         browser_secondary());
        draw_text_center(font_small, "PRESS Y ON A GAME TO ADD IT", 0, SCREEN_W,
                         244, browser_dim());
        return;
    }

    const int y0 = 82;
    const int row_h = 64;
    for (int row = 0; row < BROWSER_ROWS && favorite_offset + row < favorite_count; row++) {
        int idx = favorite_offset + row;
        int y = y0 + row * row_h;
        int selected = idx == favorite_sel;
        if (selected)
            fill_rect(10, y + 3, SCREEN_W - 20, row_h - 6, browser_accent(category));
        else
            fill_rect(18, y + row_h - 1, SCREEN_W - 36, 1, browser_rgb(0x1E, 0x21, 0x28));

        fill_rect(22, y + 27, 8, 8,
                  selected ? browser_rgb(0x14, 0x08, 0x08) : browser_accent(category));
        char title[240];
        truncate_to_fit(font_game, favorite_entries[idx].label, title, sizeof(title), 414);
        draw_text(font_game, title, 48, y + 16,
                  selected ? browser_dark_text() : browser_text());
        draw_browser_badge(548, y + 20, 56, favorite_entries[idx].system, selected);
    }
    draw_browser_more(0, 414, SCREEN_W, favorite_count, favorite_offset,
                      BROWSER_ROWS, category);
}

static const char *SETTINGS_HUB_LABELS[] = {
    "Apps", "Settings", "Device Info", "About PocketOS", "Sleep"
};
static const char *SETTINGS_HUB_SUBTITLES[] = {
    "TOOLS AND UTILITIES", "DISPLAY, AUDIO, CONTROLS", "HARDWARE AND STORAGE",
    "VERSION AND CREDITS", "SUSPEND THE DEVICE"
};
static const char *SETTINGS_HUB_TAGS[] = {"AP", "ST", "DV", "OS", "ZZ"};
#define SETTINGS_HUB_COUNT 5

static void draw_settings_hub_shell(void) {
    int category = 4;
    int selected = home_sel_sec[2];
    fill_rect(0, 0, SCREEN_W, SCREEN_H, browser_rgb(0x0E, 0x0F, 0x13));
    draw_browser_header(category);
    draw_browser_footer(6, category);
    draw_text(font_small, "SYSTEM", 18, 61, browser_dim());
    draw_text(font_small, "POCKETOS " POCKETOS_VERSION,
              SCREEN_W - 18 - text_w(font_small, "POCKETOS " POCKETOS_VERSION),
              61, browser_dim());

    const int y0 = 82;
    const int row_h = 64;
    for (int row = 0; row < SETTINGS_HUB_COUNT; row++) {
        int y = y0 + row * row_h;
        int is_selected = row == selected;
        if (is_selected)
            fill_rect(10, y + 3, SCREEN_W - 20, row_h - 6, browser_accent(category));
        else
            fill_rect(18, y + row_h - 1, SCREEN_W - 36, 1, browser_rgb(0x1E, 0x21, 0x28));

        Uint32 tag_bg = is_selected ? browser_rgb(0x08, 0x0D, 0x16)
                                    : browser_rgb(0x1E, 0x21, 0x28);
        fill_rrect(20, y + 16, 40, 28, 2, tag_bg);
        draw_text_center(font_small, SETTINGS_HUB_TAGS[row], 20, 40, y + 23,
                         is_selected ? browser_text() : browser_accent_text(category));
        draw_text(font_body, SETTINGS_HUB_LABELS[row], 78, y + 10,
                  is_selected ? browser_dark_text() : browser_text());
        draw_text(font_small, SETTINGS_HUB_SUBTITLES[row], 80, y + 37,
                  is_selected ? browser_dark_text() : browser_dim());
        draw_chevron(610, y + 32, 7, 2,
                      is_selected ? browser_rgb(0x08, 0x0D, 0x16)
                                  : browser_rgb(0x56, 0x5A, 0x64));
    }
}

__attribute__((unused)) static void draw_browse(void) {
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
        if (sel)
            draw_select_asset(10, iy + 4, LEFT_W - 18, ITEM_H - 8);
        else
            fill_rect(10, iy + ITEM_H - 1, LEFT_W - 18, 1, C_SEP);
        int lsel = sel;
        char label[48];
        truncate_to_fit(font_body, browse_genres[gi].label, label, sizeof(label), LEFT_W - 50);
        draw_text(font_body, label, 18, iy + (ITEM_H - 22) / 2, lsel ? SC_WHITE : SC_TEXT);
        char cnt[10];
        snprintf(cnt, sizeof(cnt), "%d", browse_genres[gi].count);
        draw_text(font_small, cnt, LEFT_W - 28 - text_w(font_small, cnt),
                  iy + (ITEM_H - 14) / 2, lsel ? SC_WHITE : SC_DIM);
        Uint32 chev = lsel ? RGBA(SC_WHITE.r, SC_WHITE.g, SC_WHITE.b) : C_SEP;
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
    snprintf(hdr, sizeof(hdr), "%s (%d)",
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
        SDL_Color btc = sel ? SC_WHITE : SC_TEXT;
        draw_text(font_game, line1, rx + 14, ty, btc);
        if (line2[0])
            draw_text(font_game, line2, rx + 14, ty + GAME_LINE_GAP, btc);
        int sys_y = ty + (line2[0] ? GAME_LINE_GAP + 28 + 4 : 28 + 4);
        draw_text(font_small, browse_game_pool[gi].system, rx + 14, sys_y, SC_DIM);
    }
    if (bg->count > 0)
        draw_scrollbar(SCREEN_W - 14, gy0, GAME_ROWS * GAME_ITEM_H,
                       bg->count, GAME_ROWS, browse_game_off);
}

/* Keep state-specific wrappers for render dispatch and modal backdrops. */
static void draw_browse_cats(void)  { draw_browse_shell(); }
static void draw_browse_games(void) { draw_browse_shell(); }

static void open_browser_category(int category) {
    browser_category = (category + 5) % 5;
    switch (browser_category) {
    case 0:
        load_most_played();
        state = STATE_MOST_PLAYED;
        break;
    case 1:
        if (browse_genre_count == 0) load_browse_data();
        state = STATE_BROWSE_CATS;
        break;
    case 2:
        if (sys_count > 0) load_games(sys_sel);
        state = STATE_SYSTEMS;
        break;
    case 3:
        load_favorites();
        state = STATE_FAVORITES;
        break;
    default:
        home_section = 2;
        state = STATE_HOME;
        break;
    }
}

static void cycle_browser_category(int delta) {
    play_move();
    open_browser_category(browser_category + delta);
}

static int selected_browse_entry(PlayEntry *entry) {
    if (!entry || browse_genre_sel < 0 || browse_genre_sel >= browse_genre_count)
        return 0;
    BrowseGenre *genre = &browse_genres[browse_genre_sel];
    if (browse_game_sel < 0 || browse_game_sel >= genre->count) return 0;
    BrowseGame *game = &browse_game_pool[genre->start + browse_game_sel];
    System *sys = find_system_for_rompath(game->path);
    if (!sys) {
        log_kv("browse entry: no system match for", game->path);
        return 0;
    }

    memset(entry, 0, sizeof(*entry));
    copy_truncated(entry->label, sizeof(entry->label), game->title);
    copy_truncated(entry->rompath, sizeof(entry->rompath), game->path);
    snprintf(entry->launch, sizeof(entry->launch), "%s/launch.sh", sys->emu_dir);
    system_from_launch(entry->launch, entry->system, sizeof(entry->system));
    return 1;
}

static void on_browse_cats_key(SDLKey k) {
    if (k == BTN_L1) { cycle_browser_category(-1); return; }
    if (k == BTN_R1) { cycle_browser_category(1); return; }
    if (k == BTN_MENU) { open_browser_category(4); return; }
    if (browse_genre_count <= 0) return;

    int before = browse_genre_sel;
    if (k == BTN_UP   && browse_genre_sel > 0)                    browse_genre_sel--;
    if (k == BTN_DOWN && browse_genre_sel < browse_genre_count-1) browse_genre_sel++;
    if (k == BTN_L2)  browse_genre_sel = (browse_genre_sel - BROWSER_ROWS < 0) ? 0 : browse_genre_sel - BROWSER_ROWS;
    if (k == BTN_R2)  browse_genre_sel = (browse_genre_sel + BROWSER_ROWS >= browse_genre_count) ? browse_genre_count-1 : browse_genre_sel + BROWSER_ROWS;
    if (browse_genre_sel != before) { play_move(); browse_game_sel = 0; browse_game_off = 0; }
    if (k == BTN_A || k == BTN_RIGHT) {
        play_select();
        browse_game_sel = 0; browse_game_off = 0;
        state = STATE_BROWSE_GAMES;
    }
    if (k == BTN_B || k == BTN_LEFT) {
        play_back(); open_browser_category(0);
    }
}

static void on_browse_games_key(SDLKey k) {
    if (k == BTN_L1) { cycle_browser_category(-1); return; }
    if (k == BTN_R1) { cycle_browser_category(1); return; }
    if (k == BTN_MENU) { open_browser_category(4); return; }
    if (browse_genre_sel < 0 || browse_genre_sel >= browse_genre_count) { state = STATE_BROWSE_CATS; return; }
    BrowseGenre *bg = &browse_genres[browse_genre_sel];
    if (bg->count <= 0) { state = STATE_BROWSE_CATS; return; }
    int before = browse_game_sel;
    if (k == BTN_UP   && browse_game_sel > 0)            browse_game_sel--;
    if (k == BTN_DOWN && browse_game_sel < bg->count-1)  browse_game_sel++;
    if (k == BTN_L2)  browse_game_sel = (browse_game_sel - BROWSER_ROWS < 0) ? 0 : browse_game_sel - BROWSER_ROWS;
    if (k == BTN_R2)  browse_game_sel = (browse_game_sel + BROWSER_ROWS >= bg->count) ? bg->count-1 : browse_game_sel + BROWSER_ROWS;
    if (browse_game_sel < browse_game_off) browse_game_off = browse_game_sel;
    if (browse_game_sel >= browse_game_off + GAME_ROWS)  browse_game_off = browse_game_sel - GAME_ROWS + 1;
    if (browse_game_sel != before) play_move();
    if (k == BTN_A) {
        PlayEntry entry;
        if (selected_browse_entry(&entry)) launch_entry(&entry);
    }
    if (k == BTN_Y) {
        PlayEntry entry;
        if (selected_browse_entry(&entry)) {
            toggle_favorite(entry.label, entry.rompath, entry.launch);
            play_select();
        }
    }
    if (k == BTN_X) {
        PlayEntry entry;
        if (selected_browse_entry(&entry))
            enter_game_options(entry.label, entry.rompath, entry.launch,
                               entry.system, STATE_BROWSE_GAMES);
    }
    if (k == BTN_B || k == BTN_LEFT) {
        play_back(); state = STATE_BROWSE_CATS;
    }
}

static void draw_secondary_header(const char *parent, const char *title) {
    fill_rect(0, 0, SCREEN_W, BROWSER_HEADER_H, browser_rgb(0x12, 0x14, 0x1A));
    fill_rect(0, BROWSER_HEADER_H - 1, SCREEN_W, 1, browser_rgb(0x1E, 0x21, 0x28));

    fill_rrect(14, 17, 22, 18, 2, browser_rgb(0x2B, 0x30, 0x3A));
    draw_text_center(font_small, "B", 14, 22, 19, browser_secondary());
    draw_text(font_small, parent, 46, 19, browser_dim());
    int parent_end = 46 + text_w(font_small, parent);
    fill_rect(parent_end + 12, 17, 1, 18, browser_rgb(0x2B, 0x30, 0x3A));
    draw_text(font_small, title, parent_end + 26, 19, browser_text());

    int batt = read_battery();
    char bstr[12];
    if (batt >= 0) snprintf(bstr, sizeof(bstr), "%d%%", batt);
    else snprintf(bstr, sizeof(bstr), "--%%");
    SDL_Color batt_col = batt >= 0 && batt <= 25
                         ? (SDL_Color){0xFF, 0x7A, 0x7A, 0xFF}
                         : browser_secondary();
    int bw = text_w(font_small, bstr);
    draw_text(font_small, bstr, 584 - bw, 19, batt_col);
    fill_rect(592, 18, 32, 16, browser_rgb(0x56, 0x5A, 0x64));
    fill_rect(594, 20, 28, 12, browser_rgb(0x12, 0x14, 0x1A));
    if (batt > 0) {
        int level = (26 * (batt > 100 ? 100 : batt)) / 100;
        if (level < 1) level = 1;
        fill_rect(595, 21, level, 10, browser_rgb(batt_col.r, batt_col.g, batt_col.b));
    }
    fill_rect(624, 22, 3, 8, browser_rgb(0x56, 0x5A, 0x64));
}

static void draw_secondary_frame(const char *parent, const char *title,
                                 const char *meta) {
    fill_rect(0, 0, SCREEN_W, SCREEN_H, browser_rgb(0x0E, 0x0F, 0x13));
    draw_secondary_header(parent, title);
    if (meta && meta[0]) {
        draw_text(font_small, meta, 18, 64, browser_dim());
        fill_rect(18, 81, SCREEN_W - 36, 1, browser_rgb(0x1E, 0x21, 0x28));
    }
}

/* 0=open, 1=change/adjust, 2=apply, 3=back only, 4=select/options. */
static void draw_secondary_footer(int mode) {
    int y = SCREEN_H - BROWSER_FOOTER_H;
    fill_rect(0, y, SCREEN_W, BROWSER_FOOTER_H, browser_rgb(0x12, 0x14, 0x1A));
    fill_rect(0, y, SCREEN_W, 1, browser_rgb(0x1E, 0x21, 0x28));

    int x = 14;
    if (mode != 3) {
        const char *label = mode == 0 ? "OPEN" : mode == 1 ? "CHANGE" :
                            mode == 2 ? "APPLY" : "SELECT";
        x += draw_browser_hint(x, "A", label, browser_rgb(0x3E, 0xCF, 0x6E));
    }
    x += draw_browser_hint(x, "B", "BACK", browser_rgb(0xFF, 0x7A, 0x7A));
    if (mode == 1)
        draw_browser_hint(x, "L/R", "ADJUST", browser_accent(4));
    if (mode == 4)
        draw_browser_hint(x, "X", "OPTS", browser_accent(4));
    if (mode == 0 || mode == 2)
        draw_browser_hint(x, "L2/R2", "PAGE", browser_accent(4));
}

static void app_initials(const char *label, char out[3]) {
    int n = 0;
    int at_word = 1;
    for (const unsigned char *p = (const unsigned char *)label; *p && n < 2; p++) {
        if (isalnum(*p) && at_word) {
            out[n++] = (char)toupper(*p);
            at_word = 0;
        } else if (!isalnum(*p)) {
            at_word = 1;
        }
    }
    if (n == 1) {
        for (const unsigned char *p = (const unsigned char *)label + 1; *p; p++) {
            if (isalnum(*p)) { out[n++] = (char)toupper(*p); break; }
        }
    }
    out[n] = '\0';
}

static void draw_apps(void) {
    char meta[24];
    snprintf(meta, sizeof(meta), "%d INSTALLED", APP_COUNT);
    draw_secondary_frame("SETTINGS", "APPS", meta);
    draw_secondary_footer(0);

    if (app_sel < app_offset) app_offset = app_sel;
    if (app_sel >= app_offset + BROWSER_ROWS) app_offset = app_sel - BROWSER_ROWS + 1;

    for (int row = 0; row < BROWSER_ROWS && app_offset + row < APP_COUNT; row++) {
        int i   = app_offset + row;
        int iy  = 82 + row * 64;
        int sel = (i == app_sel);

        if (sel)
            fill_rect(10, iy + 3, SCREEN_W - 20, 58, browser_accent(4));
        else
            fill_rect(18, iy + 63, SCREEN_W - 36, 1, browser_rgb(0x1E, 0x21, 0x28));

        char initials[3];
        app_initials(APP_ENTRIES[i].label, initials);
        Uint32 icon_bg = sel ? browser_rgb(0x0E, 0x0F, 0x13)
                             : browser_rgb(0x2B, 0x30, 0x3A);
        fill_rrect(20, iy + 14, 40, 36, 3, icon_bg);
        draw_text_center(font_small, initials, 20, 40, iy + 24,
                         sel ? browser_accent_text(4) : browser_secondary());

        char label[128];
        truncate_to_fit(font_game, APP_ENTRIES[i].label, label, sizeof(label), 500);
        draw_text(font_game, label, 78, iy + 16,
                  sel ? browser_dark_text() : browser_text());

        Uint32 chev_col = sel ? browser_rgb(0x0E, 0x0F, 0x13)
                              : browser_rgb(0x56, 0x5A, 0x64);
        draw_chevron(SCREEN_W - 28, iy + 32, 7, 2, chev_col);
    }
    draw_browser_more(0, 414, SCREEN_W, APP_COUNT, app_offset, BROWSER_ROWS, 4);
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

static void read_command_line(const char *command, char *out, int outlen) {
    out[0] = '\0';
    FILE *pipe = popen(command, "r");
    if (!pipe) return;
    if (fgets(out, outlen, pipe)) {
        int n = strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' ||
                         out[n - 1] == ' ' || out[n - 1] == '\t'))
            out[--n] = '\0';
    }
    pclose(pipe);
}

static void draw_info_row(int x, int y, int w, const char *label, const char *value) {
    char label_fit[96], value_fit[160];
    truncate_to_fit(font_body, label, label_fit, sizeof(label_fit), w / 2 - 12);
    truncate_to_fit(font_body, value, value_fit, sizeof(value_fit), w / 2 + 12);
    draw_text(font_body, label_fit, x, y, browser_secondary());
    int vw = text_w(font_body, value_fit);
    draw_text(font_body, value_fit, x + w - vw, y, browser_text());
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
    int category = 2;
    switch (game_opts_back) {
    case STATE_GAMES:
        draw_library_shell();
        category = 2;
        break;
    case STATE_BROWSE_GAMES:
        draw_browse_shell();
        category = 1;
        break;
    case STATE_FAVORITES:
        draw_favorites_shell();
        category = 3;
        break;
    case STATE_RECENT:
        draw_entry_list("Recent", recent_entries, recent_count,
                        &recent_sel, &recent_offset, 0);
        category = 4;
        break;
    case STATE_MOST_PLAYED:
        draw_most_played_shell();
        category = 0;
        break;
    default:
        draw_library_shell();
        break;
    }

    if (g_dim_overlay)
        SDL_BlitSurface(g_dim_overlay, NULL, screen, NULL);
    else
        fill_rect_alpha(0, 0, SCREEN_W, SCREEN_H, 140);

    const int cx = 60;
    const int cy = 66;
    const int cw = 520;
    const int ch = 350;
    const int inner_x = cx + 20;
    const int inner_w = cw - 40;
    fill_rrect(cx, cy, cw, ch, 5, browser_rgb(0x2B, 0x30, 0x3A));
    fill_rrect(cx + 1, cy + 1, cw - 2, ch - 2, 4, browser_rgb(0x12, 0x14, 0x1A));
    fill_rect(cx + 1, cy + 1, cw - 2, 3, browser_accent(category));

    const char *panel_title = game_opts_mode == 0 ? "GAME OPTIONS" :
                              game_opts_mode == 1 ? "ROM INFO" : "SAVE INFO";
    draw_text(font_small, panel_title, inner_x, cy + 18, browser_accent_text(category));
    char hdr[120];
    truncate_to_fit(font_game, game_opts_name, hdr, sizeof(hdr), inner_w - 76);
    draw_text(font_game, hdr, inner_x, cy + 42, browser_text());
    draw_browser_badge(cx + cw - 70, cy + 38, 50, game_opts_system, 0);
    fill_rect(inner_x, cy + 76, inner_w, 1, browser_rgb(0x2B, 0x30, 0x3A));

    if (game_opts_mode == 0) {
        const char *items[GOPTS_COUNT];
        items[GOPTS_LAUNCH]   = "Launch Game";
        items[GOPTS_FAVORITE] = is_favorite(game_opts_path)
                                ? "Remove from Favorites"
                                : "Add to Favorites";
        items[GOPTS_ROM_INFO]  = "ROM Info";
        items[GOPTS_SAVE_INFO] = "Save Info";

        const int row_h = 58;
        int ry = cy + 88;
        for (int i = 0; i < GOPTS_COUNT; i++) {
            int sel = (i == game_opts_sel);
            if (sel)
                fill_rect(inner_x, ry + 3, inner_w, row_h - 6, browser_accent(category));
            else
                fill_rect(inner_x + 8, ry + row_h - 1, inner_w - 16, 1,
                          browser_rgb(0x1E, 0x21, 0x28));
            draw_text(font_body, items[i], inner_x + 14, ry + 18,
                      sel ? browser_dark_text() : browser_text());
            draw_chevron(inner_x + inner_w - 18, ry + row_h / 2, 7, 2,
                         sel ? browser_rgb(0x0E, 0x0F, 0x13)
                             : browser_rgb(0x56, 0x5A, 0x64));
            ry += row_h;
        }
    } else if (game_opts_mode == 1) {
        const int row_h = 54;
        int ry = cy + 104;

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

        char pathbuf[96];
        int plen = strlen(game_opts_path);
        if (plen > 72)
            snprintf(pathbuf, sizeof(pathbuf), "...%s", game_opts_path + plen - 69);
        else {
            strncpy(pathbuf, game_opts_path, sizeof(pathbuf) - 1);
            pathbuf[sizeof(pathbuf)-1] = '\0';
        }
        draw_info_row(inner_x, ry, inner_w, "Path", pathbuf);

    } else {
        const int row_h = 54;
        int ry = cy + 104;

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
                    if (!path_join(save_path, sizeof(save_path), saves_dir, ent->d_name))
                        continue;
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
            draw_text_center(font_body, "NO SAVES FOUND", cx, cw,
                             ry + 60, browser_secondary());
        }
    }
    draw_secondary_footer(game_opts_mode == 0 ? 5 : 3);
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
                snprintf(tmp.label,   sizeof(tmp.label),   "%s", game_opts_name);
                snprintf(tmp.rompath, sizeof(tmp.rompath), "%s", game_opts_path);
                snprintf(tmp.launch,  sizeof(tmp.launch),  "%s", game_opts_launch);
                snprintf(tmp.system,  sizeof(tmp.system),  "%s", game_opts_system);
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
    const char *labels[4];
    char values[4][160];
    memset(values, 0, sizeof(values));
    if (!info_panel_about) {
        char model_s[32], fw[64], onion[64], kernel[128];

        FILE *f = fopen("/tmp/deviceModel", "r");
        int model = 0;
        if (f) { if (fscanf(f, "%d", &model) != 1) model = 0; fclose(f); }
        snprintf(model_s, sizeof(model_s), "%s",
                 model == 354 ? "Miyoo Mini Plus" :
                 model == 283 ? "Miyoo Mini" : "Miyoo (unknown)");

        read_first_line(FIRMWARE_VERSION_PATH, fw, sizeof(fw));
        if (!fw[0])
            read_command_line("/etc/fw_printenv miyoo_version 2>/dev/null",
                              fw, sizeof(fw));
        const char *fw_prefix = "miyoo_version=";
        if (strncmp(fw, fw_prefix, strlen(fw_prefix)) == 0)
            memmove(fw, fw + strlen(fw_prefix),
                    strlen(fw + strlen(fw_prefix)) + 1);
        if (!fw[0]) snprintf(fw, sizeof(fw), "Not reported");

        read_first_line(POCKETOS_ROOT "/.tmp_update/onionVersion/version.txt",
                        onion, sizeof(onion));
        if (!onion[0]) snprintf(onion, sizeof(onion), "%s", ONION_BASE_VERSION);

        char proc_ver[256];
        read_first_line("/proc/version", proc_ver, sizeof(proc_ver));
        kernel[0] = '\0';
        char *vp = strstr(proc_ver, "version ");
        if (vp) {
            vp += 8;
            int ki = 0;
            while (*vp && *vp != ' ' && ki < (int)sizeof(kernel) - 1)
                kernel[ki++] = *vp++;
            kernel[ki] = '\0';
        }
        if (!kernel[0]) snprintf(kernel, sizeof(kernel), "Unknown");

        labels[0] = "MODEL";
        labels[1] = "FIRMWARE";
        labels[2] = "ONION BASE";
        labels[3] = "KERNEL";
        snprintf(values[0], sizeof(values[0]), "%s", model_s);
        snprintf(values[1], sizeof(values[1]), "%s", fw);
        snprintf(values[2], sizeof(values[2]), "%s", onion);
        snprintf(values[3], sizeof(values[3]), "%s", kernel);
    } else {
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

        labels[0] = "VERSION";
        labels[1] = "PLATFORM";
        labels[2] = "THEME";
        labels[3] = "FONT";
        snprintf(values[0], sizeof(values[0]), "%s", POCKETOS_VERSION);
        snprintf(values[1], sizeof(values[1]), "Miyoo Mini+");
        snprintf(values[2], sizeof(values[2]), "%s", theme_label);
        snprintf(values[3], sizeof(values[3]), "%s", font_label);
    }

    draw_secondary_frame("SETTINGS", info_panel_about ? "ABOUT" : "DEVICE INFO",
                         info_panel_about ? "POCKETOS" : "SYSTEM INFORMATION");
    draw_secondary_footer(3);
    for (int i = 0; i < 4; i++) {
        int y = 90 + i * 78;
        draw_text(font_small, labels[i], 20, y + 8, browser_dim());
        char value[160];
        truncate_to_fit(font_game, values[i], value, sizeof(value), SCREEN_W - 72);
        draw_text(font_game, value, 20, y + 31, browser_text());
        fill_rect(20, y + 76, SCREEN_W - 40, 1, browser_rgb(0x1E, 0x21, 0x28));
    }
}

static void on_info_panel_key(SDLKey k) {
    if (k == BTN_B || k == BTN_MENU || k == BTN_A) {
        play_back();
        state = info_panel_back;
    }
}

static void draw_settings(void) {
    if (!settings_val_valid) {
        for (int i = 0; i < SETTINGS_COUNT; i++) {
            if (!SETTINGS_ENTRIES[i].is_header) {
                settings_value(&SETTINGS_ENTRIES[i],
                               settings_val_cache[i], sizeof(settings_val_cache[i]));
                settings_num_cache[i] = setting_cur_val(SETTINGS_ENTRIES[i].kind);
            }
        }
        settings_val_valid = 1;
    }

    draw_secondary_frame("SETTINGS", "PREFERENCES", NULL);
    draw_secondary_footer(1);

    const int clip_top = BROWSER_HEADER_H;
    const int clip_bottom = SCREEN_H - BROWSER_FOOTER_H;

    SDL_Rect content_clip = {0, clip_top, SCREEN_W, BROWSER_BODY_H};
    SDL_SetClipRect(screen, &content_clip);

    for (int i = 0; i < SETTINGS_COUNT; i++) {
        int rh  = settings_row_h(i);
        int ry  = clip_top + settings_row_top(i) - settings_scroll_px;

        if (ry + rh <= clip_top) continue;
        if (ry >= clip_bottom) break;

        if (SETTINGS_ENTRIES[i].is_header) {
            int band_y = ry;
            int band_h = rh;
            if (band_y < clip_top) { band_h -= clip_top - band_y; band_y = clip_top; }
            if (band_y + band_h > clip_bottom) band_h = clip_bottom - band_y;
            fill_rect(0, band_y, SCREEN_W, band_h, browser_rgb(0x12, 0x14, 0x1A));
            fill_rect(0, band_y + band_h - 1, SCREEN_W, 1,
                      browser_rgb(0x1E, 0x21, 0x28));
            int lbl_y = ry + (rh - 14) / 2;
            if (lbl_y >= clip_top && lbl_y + 14 <= clip_bottom)
                draw_text(font_small, SETTINGS_ENTRIES[i].label, 18, lbl_y,
                          browser_accent_text(4));
        } else {
            int is_sel = (i == settings_sel);
            if (is_sel)
                fill_rect(8, ry + 2, SCREEN_W - 16, rh - 4, browser_accent(4));
            else
                fill_rect(18, ry + rh - 1, SCREEN_W - 36, 1,
                          browser_rgb(0x1E, 0x21, 0x28));

            char setting_label[96];
            truncate_to_fit(font_body, SETTINGS_ENTRIES[i].label, setting_label,
                            sizeof(setting_label), 330);
            draw_text(font_body, setting_label, 20, ry + 17,
                      is_sel ? browser_dark_text() : browser_text());

            const char *value = settings_val_cache[i];
            SDL_Color value_col = is_sel ? browser_dark_text() : browser_secondary();
            const int rp_x = 420;
            const int rp_w = 170;
            const int mid = ry + rh / 2;

            int mv = setting_max_val(SETTINGS_ENTRIES[i].kind);
            if (mv > 0) {
                int cv  = settings_num_cache[i];
                int vw  = text_w(font_body, value);
                draw_text(font_body, value, rp_x + rp_w - vw, ry + 7, value_col);
                fill_rrect(rp_x, ry + 38, rp_w, 4, 2,
                           browser_rgb(0x2B, 0x30, 0x3A));
                int fw = (rp_w * cv) / mv;
                if (fw > 0)
                    fill_rrect(rp_x, ry + 37, fw, 6, 2,
                               is_sel ? browser_rgb(0x0E, 0x0F, 0x13)
                                      : browser_accent(4));
            } else {
                char value_fit[96];
                truncate_to_fit(font_body, value, value_fit, sizeof(value_fit), rp_w);
                int vw = text_w(font_body, value_fit);
                draw_text(font_body, value_fit, rp_x + rp_w - vw, mid - 11, value_col);
            }

            draw_chevron(SCREEN_W - 24, mid, 7, 2,
                         is_sel ? browser_rgb(0x0E, 0x0F, 0x13)
                                : browser_rgb(0x56, 0x5A, 0x64));
        }
    }

    SDL_SetClipRect(screen, NULL);

    int total_h = total_settings_height();
    int max_scroll = total_h - BROWSER_BODY_H;
    if (max_scroll > 0) {
        int track_h = BROWSER_BODY_H - 16;
        int thumb_h = (track_h * BROWSER_BODY_H) / total_h;
        if (thumb_h < 14) thumb_h = 14;
        int travel = track_h - thumb_h;
        int thumb_y = BROWSER_HEADER_H + 8 +
                      (travel * settings_scroll_px) / max_scroll;
        fill_rect(SCREEN_W - 6, BROWSER_HEADER_H + 8, 2, track_h,
                  browser_rgb(0x2B, 0x30, 0x3A));
        fill_rrect(SCREEN_W - 7, thumb_y, 4, thumb_h, 2, browser_accent(4));
    }
}

// ── Render ────────────────────────────────────────────────────────────────────

static void render(void) {
    switch (state) {
    case STATE_HOME:
        draw_settings_hub_shell();
        break;
    case STATE_SYSTEMS:
    case STATE_GAMES:
        draw_library_shell();
        break;
    case STATE_RECENT:
        draw_entry_list("Recent", recent_entries, recent_count, &recent_sel, &recent_offset, 0);
        break;
    case STATE_FAVORITES:
        draw_favorites_shell();
        break;
    case STATE_MOST_PLAYED:
        draw_most_played_shell();
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
        if (info_panel_back == STATE_HOME) draw_settings_hub_shell();
        else draw_settings();
        draw_info_panel();
        break;
    case STATE_GAME_OPTIONS:
        draw_game_options();
        break;
    }
    draw_screenshot_toast();
    SDL_BlitSurface(screen, NULL, video, NULL);
    SDL_Flip(video);
    {
        static int screenshot_saved = 0;
        if (!screenshot_saved) {
            const char *path = getenv("POCKETOS_SCREENSHOT_PATH");
#ifdef POCKETOS_SCREENSHOT
            if (!path || !path[0]) path = POCKETOS_SCREENSHOT;
#endif
            if (path && path[0]) {
                SDL_SaveBMP(screen, path);
                screenshot_saved = 1;
            }
        }
    }
}

// ── Input ─────────────────────────────────────────────────────────────────────

static void on_home_key(SDLKey k) {
    if (k == BTN_L1) { cycle_browser_category(-1); return; }
    if (k == BTN_R1) { cycle_browser_category(1); return; }

    int *sel = &home_sel_sec[2];
    int before = *sel;
    if (k == BTN_UP)   *sel = (*sel - 1 + SETTINGS_HUB_COUNT) % SETTINGS_HUB_COUNT;
    if (k == BTN_DOWN) *sel = (*sel + 1) % SETTINGS_HUB_COUNT;
    if (*sel != before) play_move();

    if (k != BTN_A && k != BTN_RIGHT) return;
    play_select();
    switch (*sel) {
    case 0:
        state = STATE_APPS;
        break;
    case 1:
        open_settings_kind("display");
        break;
    case 2:
        info_panel_about = 0;
        info_panel_back = STATE_HOME;
        state = STATE_INFO_PANEL;
        break;
    case 3:
        info_panel_about = 1;
        info_panel_back = STATE_HOME;
        state = STATE_INFO_PANEL;
        break;
    default:
        exec_power_cmd("echo mem > /sys/power/state");
        break;
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
    if (k == BTN_A) launch_entry(&entries[*sel]);
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

static void on_most_played_key(SDLKey k) {
    if (k == BTN_L1) { cycle_browser_category(-1); return; }
    if (k == BTN_R1) { cycle_browser_category(1); return; }
    if (k == BTN_MENU) { open_browser_category(4); return; }
    if (most_played_count <= 0) return;

    int before = most_played_sel;
    if (k == BTN_UP)   most_played_sel = (most_played_sel - 1 + most_played_count) % most_played_count;
    if (k == BTN_DOWN) most_played_sel = (most_played_sel + 1) % most_played_count;
    if (k == BTN_L2)
        most_played_sel = most_played_sel - BROWSER_ROWS < 0 ? 0 : most_played_sel - BROWSER_ROWS;
    if (k == BTN_R2)
        most_played_sel = most_played_sel + BROWSER_ROWS >= most_played_count
                          ? most_played_count - 1 : most_played_sel + BROWSER_ROWS;
    if (most_played_sel != before) play_move();

    PlayEntry *entry = &most_played_entries[most_played_sel];
    if (k == BTN_A) launch_entry(entry);
    if (k == BTN_Y) {
        toggle_favorite(entry->label, entry->rompath, entry->launch);
        play_select();
    }
    if (k == BTN_X)
        enter_game_options(entry->label, entry->rompath, entry->launch,
                           entry->system, STATE_MOST_PLAYED);
}

static void on_favorites_key(SDLKey k) {
    if (k == BTN_L1) { cycle_browser_category(-1); return; }
    if (k == BTN_R1) { cycle_browser_category(1); return; }
    if (k == BTN_MENU) { open_browser_category(4); return; }
    if (favorite_count <= 0) return;

    int before = favorite_sel;
    if (k == BTN_UP)   favorite_sel = (favorite_sel - 1 + favorite_count) % favorite_count;
    if (k == BTN_DOWN) favorite_sel = (favorite_sel + 1) % favorite_count;
    if (k == BTN_L2)
        favorite_sel = favorite_sel - BROWSER_ROWS < 0 ? 0 : favorite_sel - BROWSER_ROWS;
    if (k == BTN_R2)
        favorite_sel = favorite_sel + BROWSER_ROWS >= favorite_count
                       ? favorite_count - 1 : favorite_sel + BROWSER_ROWS;
    if (favorite_sel != before) play_move();

    PlayEntry entry = favorite_entries[favorite_sel];
    if (k == BTN_A) launch_entry(&entry);
    if (k == BTN_Y) {
        toggle_favorite(entry.label, entry.rompath, entry.launch);
        play_select();
    }
    if (k == BTN_X)
        enter_game_options(entry.label, entry.rompath, entry.launch,
                           entry.system, STATE_FAVORITES);
}

static void on_apps_key(SDLKey k) {
    int before = app_sel;
    if (k == BTN_UP   && app_sel > 0)              app_sel--;
    if (k == BTN_DOWN && app_sel < APP_COUNT - 1)  app_sel++;
    if (k == BTN_L2) app_sel = app_sel - BROWSER_ROWS < 0
                                  ? 0 : app_sel - BROWSER_ROWS;
    if (k == BTN_R2) app_sel = app_sel + BROWSER_ROWS >= APP_COUNT
                                  ? APP_COUNT - 1 : app_sel + BROWSER_ROWS;
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
    char meta[24];
    snprintf(meta, sizeof(meta), "%d AVAILABLE", font_list_count);
    draw_secondary_frame("SETTINGS", "FONT", meta);
    draw_secondary_footer(2);

    if (font_pick_sel < font_pick_offset) font_pick_offset = font_pick_sel;
    if (font_pick_sel >= font_pick_offset + BROWSER_ROWS)
        font_pick_offset = font_pick_sel - BROWSER_ROWS + 1;

    if (font_list_count == 0) {
        draw_text_center(font_body, "NO FONTS FOUND", 0, SCREEN_W, 216,
                         browser_secondary());
        return;
    }

    for (int row = 0; row < BROWSER_ROWS && font_pick_offset + row < font_list_count; row++) {
        int i   = font_pick_offset + row;
        int iy  = 82 + row * 64;
        int sel = (i == font_pick_sel);

        if (sel)
            fill_rect(10, iy + 3, SCREEN_W - 20, 58, browser_accent(4));
        else
            fill_rect(18, iy + 63, SCREEN_W - 36, 1, browser_rgb(0x1E, 0x21, 0x28));

        char label[64];
        strncpy(label, font_list_name[i], sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        char *dot = strrchr(label, '.');
        if (dot) *dot = '\0';

        char label_fit[64];
        truncate_to_fit(font_game, label, label_fit, sizeof(label_fit), 520);
        draw_text(font_game, label_fit, 20, iy + 16,
                  sel ? browser_dark_text() : browser_text());

        draw_chevron(SCREEN_W - 28, iy + 32, 7, 2,
                     sel ? browser_rgb(0x0E, 0x0F, 0x13)
                         : browser_rgb(0x56, 0x5A, 0x64));
    }
    draw_browser_more(0, 414, SCREEN_W, font_list_count, font_pick_offset,
                      BROWSER_ROWS, 4);
}

static void on_font_picker_key(SDLKey k) {
    int before = font_pick_sel;
    if (k == BTN_UP   && font_pick_sel > 0)                    font_pick_sel--;
    if (k == BTN_DOWN && font_pick_sel < font_list_count - 1)  font_pick_sel++;
    if (k == BTN_L2) font_pick_sel = font_pick_sel - BROWSER_ROWS < 0
                                         ? 0 : font_pick_sel - BROWSER_ROWS;
    if (k == BTN_R2 && font_list_count > 0)
        font_pick_sel = font_pick_sel + BROWSER_ROWS >= font_list_count
                        ? font_list_count - 1 : font_pick_sel + BROWSER_ROWS;
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
        copy_truncated(theme_list_name[theme_list_count],
                       sizeof(theme_list_name[0]), n);
        theme_list_count++;
    }
    closedir(d);

    for (int i = 1; i < theme_list_count; i++) {
        for (int j = i; j > 0 && strcasecmp(theme_list_name[j - 1],
                                             theme_list_name[j]) > 0; j--) {
            char name[64], path[512];
            memcpy(name, theme_list_name[j - 1], sizeof(name));
            memcpy(path, theme_list_path[j - 1], sizeof(path));
            memcpy(theme_list_name[j - 1], theme_list_name[j], sizeof(name));
            memcpy(theme_list_path[j - 1], theme_list_path[j], sizeof(path));
            memcpy(theme_list_name[j], name, sizeof(name));
            memcpy(theme_list_path[j], path, sizeof(path));
        }
    }
}

static int theme_palette_matches(const char *a_path, const char *b_path) {
    static const char *keys[] = {"bg", "bar", "sel", "sel_border", "text", "white"};
    for (int i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++) {
        char a[24], b[24];
        if (!json_str(a_path, keys[i], a, sizeof(a)) ||
            !json_str(b_path, keys[i], b, sizeof(b)) ||
            strcasecmp(a, b) != 0)
            return 0;
    }
    return 1;
}

static int current_theme_index(void) {
    char current[512];
    snprintf(current, sizeof(current), "%s/theme.json", ASSET_ROOT);
    if (access(current, R_OK) == 0) {
        for (int i = 0; i < theme_list_count; i++) {
            if (theme_palette_matches(current, theme_list_path[i])) return i;
        }
    }
    for (int i = 0; i < theme_list_count; i++) {
        if (strcasecmp(theme_list_name[i], "theme_cream.json") == 0) return i;
    }
    return theme_list_count > 0 ? 0 : -1;
}

static ThemePalette capture_theme_palette(void) {
    ThemePalette p = {
        C_BG, C_BAR, C_SEP, C_SEL, C_PANEL_HDR,
        C_DIVIDER, C_CARD, C_CARD_BORDER,
        C_SEL_HI, C_SEL_BORDER, C_PANEL_HI,
        SC_TEXT, SC_WHITE, SC_DIM, SC_SUB_SEL, SC_HDR
    };
    return p;
}

static void restore_theme_palette(const ThemePalette *p) {
    C_BG = p->bg; C_BAR = p->bar; C_SEP = p->sep; C_SEL = p->sel;
    C_PANEL_HDR = p->panel_hdr; C_DIVIDER = p->divider; C_CARD = p->card;
    C_CARD_BORDER = p->card_border; C_SEL_HI = p->sel_hi;
    C_SEL_BORDER = p->sel_border; C_PANEL_HI = p->panel_hi;
    SC_TEXT = p->text; SC_WHITE = p->white; SC_DIM = p->dim;
    SC_SUB_SEL = p->sub_sel; SC_HDR = p->hdr;
    refresh_browser_palette();
}

static void preview_theme_index(int idx) {
    if (idx < 0 || idx >= theme_list_count) return;
    if (theme_preview_active) restore_theme_palette(&theme_preview_original);
    load_theme_file(theme_list_path[idx], NULL, 0);
    apply_appearance_mode();
    refresh_browser_palette();
}

static void apply_theme_index(int idx) {
    if (idx < 0 || idx >= theme_list_count) return;
    char dst[512];
    snprintf(dst, sizeof(dst), "%s/theme.json", ASSET_ROOT);
    FILE *src = fopen(theme_list_path[idx], "r");
    if (!src) {
        log_errno_msg("theme preset open failed", theme_list_path[idx]);
        return;
    }
    char buf[4096] = {0};
    int n = (int)fread(buf, 1, sizeof(buf) - 1, src);
    int read_ok = !ferror(src) && feof(src);
    fclose(src);
    if (!read_ok) {
        log_kv("theme preset too large or unreadable", theme_list_path[idx]);
        return;
    }
    char tmp[sizeof(dst) + 8];
    FILE *out = open_atomic_file(dst, tmp, sizeof(tmp));
    if (!out) {
        log_errno_msg("theme write failed", dst);
        return;
    }
    fwrite(buf, 1, n, out);
    if (!commit_atomic_file(out, tmp, dst)) return;
    preview_theme_index(idx);
}

static void draw_theme_picker(void) {
    char meta[24];
    snprintf(meta, sizeof(meta), "%d AVAILABLE", theme_list_count);
    draw_secondary_frame("SETTINGS", "THEME", meta);
    draw_secondary_footer(2);

    if (theme_pick_sel < theme_pick_offset) theme_pick_offset = theme_pick_sel;
    if (theme_pick_sel >= theme_pick_offset + BROWSER_ROWS)
        theme_pick_offset = theme_pick_sel - BROWSER_ROWS + 1;

    if (theme_list_count == 0) {
        draw_text_center(font_body, "NO THEMES FOUND", 0, SCREEN_W, 216,
                         browser_secondary());
        return;
    }

    for (int row = 0; row < BROWSER_ROWS && theme_pick_offset + row < theme_list_count; row++) {
        int i   = theme_pick_offset + row;
        int iy  = 82 + row * 64;
        int sel = (i == theme_pick_sel);

        if (sel)
            fill_rect(10, iy + 3, SCREEN_W - 20, 58, browser_accent(4));
        else
            fill_rect(18, iy + 63, SCREEN_W - 36, 1, browser_rgb(0x1E, 0x21, 0x28));

        char label[64];
        strncpy(label, theme_list_name[i], sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        char *dot = strrchr(label, '.');
        if (dot) *dot = '\0';
        char *p = label;
        if (strncmp(p, "theme_", 6) == 0) p += 6;
        if (*p >= 'a' && *p <= 'z') *p -= 32;

        char label_fit[64];
        truncate_to_fit(font_game, p, label_fit, sizeof(label_fit), 440);
        draw_text(font_game, label_fit, 20, iy + 16,
                  sel ? browser_dark_text() : browser_text());

        if (sel) {
            Uint32 border = browser_rgb(0x0E, 0x0F, 0x13);
            fill_rrect(504, iy + 19, 24, 24, 2, border);
            fill_rect(506, iy + 21, 20, 20, C_BAR);
            fill_rrect(536, iy + 19, 24, 24, 2, border);
            fill_rect(538, iy + 21, 20, 20, C_SEL_BORDER);
            fill_rrect(568, iy + 19, 24, 24, 2, border);
            fill_rect(570, iy + 21, 20, 20, C_BG);
        }
    }
    draw_browser_more(0, 414, SCREEN_W, theme_list_count, theme_pick_offset,
                      BROWSER_ROWS, 4);
}

static void on_theme_picker_key(SDLKey k) {
    int before = theme_pick_sel;
    if (k == BTN_UP   && theme_pick_sel > 0)                     theme_pick_sel--;
    if (k == BTN_DOWN && theme_pick_sel < theme_list_count - 1)  theme_pick_sel++;
    if (k == BTN_L2) theme_pick_sel = theme_pick_sel - BROWSER_ROWS < 0
                                          ? 0 : theme_pick_sel - BROWSER_ROWS;
    if (k == BTN_R2 && theme_list_count > 0)
        theme_pick_sel = theme_pick_sel + BROWSER_ROWS >= theme_list_count
                         ? theme_list_count - 1 : theme_pick_sel + BROWSER_ROWS;
    if (theme_pick_sel != before) {
        play_move();
        preview_theme_index(theme_pick_sel);
    }
    if (k == BTN_A) {
        play_select();
        apply_theme_index(theme_pick_sel);
        theme_preview_active = 0;
        state = STATE_SETTINGS;
    }
    if (k == BTN_B || k == BTN_MENU) {
        play_back();
        if (theme_preview_active) restore_theme_palette(&theme_preview_original);
        theme_preview_active = 0;
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
        int max_scroll = total_settings_height() - BROWSER_BODY_H;
        if (max_scroll < 0) max_scroll = 0;
        if (row_y < settings_scroll_px)
            settings_scroll_px = row_y;
        if (row_y + row_h > settings_scroll_px + BROWSER_BODY_H)
            settings_scroll_px = row_y + row_h - BROWSER_BODY_H;
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
            font_pick_sel = current_font_index();
            font_pick_prev = font_pick_sel;
            font_pick_offset = font_pick_sel >= BROWSER_ROWS
                               ? font_pick_sel - BROWSER_ROWS + 1 : 0;
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
            theme_pick_sel = current_theme_index();
            if (theme_pick_sel < 0) theme_pick_sel = 0;
            theme_pick_offset = theme_pick_sel >= BROWSER_ROWS
                                ? theme_pick_sel - BROWSER_ROWS + 1 : 0;
            theme_preview_original = capture_theme_palette();
            theme_preview_active = 1;
            state = STATE_THEME_PICKER;
            return;
        }
        if (kind && strcmp(kind, "system") == 0) {
            play_select();
            info_panel_about = 0;
            info_panel_back = STATE_SETTINGS;
            state = STATE_INFO_PANEL;
            return;
        }
        if (kind && strcmp(kind, "about") == 0) {
            play_select();
            info_panel_about = 1;
            info_panel_back = STATE_SETTINGS;
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
    if (k == BTN_L1) { cycle_browser_category(-1); return; }
    if (k == BTN_R1) { cycle_browser_category(1); return; }
    if (k == BTN_MENU) { open_browser_category(4); return; }
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
    if (k == BTN_L2) {
        sys_sel = (sys_sel - LIBRARY_SYS_ROWS < 0) ? 0 : sys_sel - LIBRARY_SYS_ROWS;
        load_games(sys_sel);
    }
    if (k == BTN_R2) {
        sys_sel = (sys_sel + LIBRARY_SYS_ROWS >= sys_count) ? sys_count - 1 : sys_sel + LIBRARY_SYS_ROWS;
        load_games(sys_sel);
    }
    if (sys_sel != before) play_move();
    if ((k == BTN_A || k == BTN_RIGHT) && game_count > 0) {
        play_select();
        state = STATE_GAMES;
    }
    if (k == BTN_START && game_count > 0) {
        game_sel = rand() % game_count;
        launch_game(sys_sel, game_sel);
    }
}

static void on_games_key(SDLKey k) {
    if (k == BTN_L1) { cycle_browser_category(-1); return; }
    if (k == BTN_R1) { cycle_browser_category(1); return; }
    if (k == BTN_MENU) { open_browser_category(4); return; }
    if (game_count <= 0) {
        if (k == BTN_B || k == BTN_LEFT) state = STATE_SYSTEMS;
        return;
    }

    int before = game_sel;
    if (k == BTN_UP) {
        game_sel = (game_sel - 1 + game_count) % game_count;
    }
    if (k == BTN_DOWN) {
        game_sel = (game_sel + 1) % game_count;
    }
    if (k == BTN_L2) {
        game_sel = (game_sel - BROWSER_ROWS < 0) ? 0 : game_sel - BROWSER_ROWS;
    }
    if (k == BTN_R2) {
        game_sel = (game_sel + BROWSER_ROWS >= game_count)
                       ? game_count - 1
                       : game_sel + BROWSER_ROWS;
    }
    if (game_sel != before) play_move();
    if (k == BTN_A) {
        launch_game(sys_sel, game_sel);
    }
    if (k == BTN_START) {
        game_sel = rand() % game_count;
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
        const char *sysbase = strrchr(sys->rom_dir, '/');
        sysbase = sysbase ? sysbase + 1 : sys->rom_dir;
        enter_game_options(g->name, g->path, launch, sysbase, STATE_GAMES);
    }
    if (k == BTN_B || k == BTN_LEFT) {
        play_back();
        state = STATE_SYSTEMS;
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
            snprintf(font_list_path[font_list_count], sizeof(font_list_path[0]), "%s", testpath);
            copy_truncated(font_list_name[font_list_count],
                           sizeof(font_list_name[0]), n);
            font_list_count++;
        }
        closedir(dp);
    }
}

/* Find the picker index that matches the font currently in use. */
static int current_font_index(void) {
    for (int i = 0; i < font_list_count; i++) {
        if (strcmp(font_list_path[i], active_font_path) == 0) return i;
    }
    const char *active_name = strrchr(active_font_path, '/');
    active_name = active_name ? active_name + 1 : active_font_path;
    for (int i = 0; i < font_list_count; i++) {
        if (active_name[0] && strcmp(font_list_name[i], active_name) == 0) return i;
    }
    return 0;
}

static int open_font_set(const char *path, TTF_Font **body, TTF_Font **game,
                         TTF_Font **large, TTF_Font **small) {
    *body = TTF_OpenFont(path, 21);
    *game = TTF_OpenFont(path, 26);
    *large = TTF_OpenFont(path, 29);
    *small = TTF_OpenFont(path, 14);
    if (*body && *game && *large && *small) return 1;
    if (*body) TTF_CloseFont(*body);
    if (*game) TTF_CloseFont(*game);
    if (*large) TTF_CloseFont(*large);
    if (*small) TTF_CloseFont(*small);
    *body = *game = *large = *small = NULL;
    return 0;
}

static void apply_font_index(int idx) {
    if (idx < 0 || idx >= font_list_count) return;
    const char *fp = font_list_path[idx];
    TTF_Font *nb, *ng, *nl, *ns;
    if (!open_font_set(fp, &nb, &ng, &nl, &ns)) return;
    clear_text_cache();
    TTF_CloseFont(font_body);  TTF_CloseFont(font_game);
    TTF_CloseFont(font_large); TTF_CloseFont(font_small);
    font_body = nb; font_game = ng; font_large = nl; font_small = ns;
    snprintf(active_font_path, sizeof(active_font_path), "%s", fp);
}

static void save_theme_font(int idx) {
    if (idx < 0 || idx >= font_list_count) return;
    char theme_path[512];
    snprintf(theme_path, sizeof(theme_path), "%s/theme.json", ASSET_ROOT);

    /* Read existing theme.json if present, otherwise start fresh */
    char buf[4096] = {0};
    FILE *f = fopen(theme_path, "r");
    if (f) {
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        int valid = !ferror(f) && (feof(f) || n < sizeof(buf) - 1);
        fclose(f);
        if (!valid) {
            log_kv("theme file too large or unreadable", theme_path);
            return;
        }
    }

    const char *fn = font_list_name[idx];
    char *fp_tag = strstr(buf, "\"font\"");
    char tmp[sizeof(theme_path) + 8];
    f = open_atomic_file(theme_path, tmp, sizeof(tmp));
    if (!f) {
        log_errno_msg("font theme write failed", theme_path);
        return;
    }
    if (fp_tag && buf[0]) {
        fwrite(buf, 1, (size_t)(fp_tag - buf), f);
        const char *p = fp_tag + 6;
        while (*p == ' ' || *p == ':' || *p == '\t') p++;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                p++;
            }
            if (*p) p++;
        }
        fputs("\"font\": ", f);
        json_write_string(f, fn);
        fputs(p, f);
    } else {
        fputs("{\n  \"font\": ", f);
        json_write_string(f, fn);
        fputs("\n}\n", f);
    }
    commit_atomic_file(f, tmp, theme_path);
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
    load_theme_file(path, font_out, font_outlen);
}

static void set_palette_defaults(void) {
    C_BG          = RGBA(0xF8, 0xF1, 0xE6);
    C_BAR         = RGBA(0xFF, 0xFD, 0xF8);
    C_SEP         = RGBA(0xE4, 0xD8, 0xC7);
    C_SEL         = RGBA(0x7D, 0x3C, 0xFF);
    C_SEL_HI      = RGBA(0x9B, 0x6B, 0xFF);
    C_SEL_BORDER  = RGBA(0x5C, 0x1F, 0xE0);
    C_PANEL_HDR   = RGBA(0xEF, 0xE3, 0xD4);
    C_PANEL_HI    = RGBA(0xF4, 0xEA, 0xDB);
    C_DIVIDER     = RGBA(0xD5, 0xC5, 0xB0);
    C_CARD        = RGBA(0xFF, 0xFC, 0xF6);
    C_CARD_BORDER = RGBA(0xE6, 0xD8, 0xC3);

    SC_TEXT    = (SDL_Color){ 37,  25,  52, 255};
    SC_WHITE   = (SDL_Color){255, 255, 255, 255};
    SC_DIM     = (SDL_Color){102,  88, 112, 255};
    SC_SUB_SEL = (SDL_Color){239, 227, 255, 255};
    SC_ARROW   = (SDL_Color){125,  60, 255, 255};
    SC_HDR     = (SDL_Color){ 37,  25,  52, 255};
}

static void apply_appearance_mode(void) {
    int dark = read_config_int("pocketosAppearance", 0) ? 1 : 0;
    if (!dark) {
        SC_WHITE = (SDL_Color){255, 255, 255, 255};
        return;
    }

    SDL_Color accent = mapped_color(C_SEL);
    SDL_Color accent_hi = mapped_color(C_SEL_HI);
    SDL_Color accent_border = mapped_color(C_SEL_BORDER);
    SDL_Color base = {0x0E, 0x0F, 0x13, 0xFF};
    SDL_Color plum = {0x22, 0x16, 0x32, 0xFF};
    SDL_Color white = {0xF6, 0xF1, 0xFF, 0xFF};

    C_BG          = mapped_pixel(mix_color(base, accent_border, 18));
    C_BAR         = mapped_pixel(mix_color(base, accent, 12));
    C_SEP         = mapped_pixel(mix_color(plum, accent_hi, 48));
    C_CARD        = mapped_pixel(mix_color(base, white, 12));
    C_CARD_BORDER = mapped_pixel(mix_color(plum, accent_border, 72));
    C_PANEL_HDR   = mapped_pixel(mix_color(base, accent, 24));
    C_PANEL_HI    = mapped_pixel(mix_color(plum, accent_hi, 44));
    C_DIVIDER     = mapped_pixel(mix_color(plum, accent_border, 88));

    SC_TEXT    = white;
    SC_WHITE   = (SDL_Color){255, 255, 255, 255};
    SC_DIM     = mix_color(white, base, 116);
    SC_SUB_SEL = mix_color(white, accent, 38);
    SC_HDR     = white;
}

static void reload_theme_palette(void) {
    set_palette_defaults();
    load_theme(NULL, 0);
    apply_appearance_mode();
    refresh_browser_palette();
}

static void load_theme_file(const char *path, char *font_out, int font_outlen) {
    if (font_out && font_outlen > 0) font_out[0] = '\0';
    FILE *f = fopen(path, "r");
    if (!f) return;
    char buf[4096] = {0};
    size_t nread = fread(buf, 1, sizeof(buf) - 1, f);
    buf[nread] = '\0';
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
        if (tf) {
            fclose(tf);
            snprintf(font_out, font_outlen, "%s", p1);
            return;
        }
        tf = fopen(p2, "r");
        if (tf) {
            fclose(tf);
            snprintf(font_out, font_outlen, "%s", p2);
        }
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    srand((unsigned int)time(NULL));
    log_open();
    log_msg("pocketOS main start");
    LogTimer _t_startup = log_timer_begin("total startup");
    const char *autotest_env = getenv("POCKETOS_AUTOTEST_FRAMES");
    int autotest_frames = autotest_env ? atoi(autotest_env) : 0;
    int frames = 0;
    int stress_test = getenv("POCKETOS_STRESS_TEST") != NULL;
    int stress_frames = 0;
    const char *stress_seconds_env = getenv("POCKETOS_STRESS_TEST_SECONDS");
    int stress_seconds = stress_seconds_env ? atoi(stress_seconds_env) : 0;
    time_t stress_end = stress_test && stress_seconds > 0
                      ? time(NULL) + stress_seconds : 0;

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

    /* Build a reusable semi-transparent black overlay for modal dimming.
       SDL_SetAlpha on a plain black surface lets SDL composite it without
       the per-pixel CPU loop that fill_rect_alpha uses. */
    g_dim_overlay = SDL_CreateRGBSurface(SDL_SWSURFACE, SCREEN_W, SCREEN_H,
                                          BPP, 0, 0, 0, 0);
    if (g_dim_overlay) {
        SDL_FillRect(g_dim_overlay, NULL,
                     SDL_MapRGB(g_dim_overlay->format, 0, 0, 0));
        SDL_SetAlpha(g_dim_overlay, SDL_SRCALPHA, 140);
    }

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

    // Load one complete font set, falling back as a unit if any size fails.
    { LogTimer _t = log_timer_begin("font load");
      const char *font_candidates[] = {theme_font, FONT_PRIMARY, FONT_PATH, FONT_ALT};
      for (int i = 0; i < 4 && !font_body; i++) {
          const char *fp = font_candidates[i];
          if (!fp[0]) continue;
          if (open_font_set(fp, &font_body, &font_game, &font_large, &font_small))
              snprintf(active_font_path, sizeof(active_font_path), "%s", fp);
      }
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

    // Resolve palette defaults — off-white surfaces with atomic purple accents
    set_palette_defaults();

    // Apply theme color overrides (second pass — defaults are now set)
    load_theme(NULL, 0);
    apply_appearance_mode();
    refresh_browser_palette();

    { LogTimer _t = log_timer_begin("load_systems");
      load_systems();
      log_timer_end(_t); }
    load_favorites();
    load_most_played();
    if (sys_count > 0) {
        LogTimer _t = log_timer_begin("load_games(0)");
        load_games(0);
        log_timer_end(_t);
    }
    { LogTimer _t = log_timer_begin("load_browse_data");
      load_browse_data();
      log_timer_end(_t); }
    { LogTimer _t = log_timer_begin("scan_fonts");
      scan_fonts();
      log_timer_end(_t); }
    { LogTimer _t = log_timer_begin("scan_themes");
      scan_themes();
      theme_pick_sel = current_theme_index();
      if (theme_pick_sel < 0) theme_pick_sel = 0;
      log_timer_end(_t); }

    /* Host smoke tests can select a view without injecting key events. */
    const char *start_screen = getenv("POCKETOS_START_SCREEN");
    if (start_screen && strcmp(start_screen, "browse") == 0) {
        browser_category = 1; state = STATE_BROWSE_CATS;
    } else if (start_screen && strcmp(start_screen, "library") == 0) {
        browser_category = 2; state = STATE_SYSTEMS;
    } else if (start_screen && strcmp(start_screen, "favorites") == 0) {
        browser_category = 3; state = STATE_FAVORITES;
    } else if (start_screen && strcmp(start_screen, "settings") == 0) {
        browser_category = 4; home_section = 2; state = STATE_HOME;
    } else if (start_screen && strcmp(start_screen, "apps") == 0) {
        browser_category = 4; state = STATE_APPS;
    } else if (start_screen && strcmp(start_screen, "settings-list") == 0) {
        browser_category = 4; open_settings_kind("brightness");
    } else if (start_screen && strcmp(start_screen, "appearance") == 0) {
        browser_category = 4; open_settings_kind("appearance");
    } else if (start_screen && strcmp(start_screen, "font") == 0) {
        browser_category = 4;
        font_pick_sel = current_font_index();
        font_pick_prev = font_pick_sel;
        state = STATE_FONT_PICKER;
    } else if (start_screen && strcmp(start_screen, "theme") == 0) {
        browser_category = 4;
        scan_themes();
        theme_pick_sel = current_theme_index();
        if (theme_pick_sel < 0) theme_pick_sel = 0;
        theme_preview_original = capture_theme_palette();
        theme_preview_active = 1;
        if (theme_list_count > 0) preview_theme_index(theme_pick_sel);
        state = STATE_THEME_PICKER;
    } else if (start_screen && strcmp(start_screen, "device") == 0) {
        browser_category = 4; info_panel_about = 0;
        info_panel_back = STATE_HOME; state = STATE_INFO_PANEL;
    } else if (start_screen && strcmp(start_screen, "about") == 0) {
        browser_category = 4; info_panel_about = 1;
        info_panel_back = STATE_HOME; state = STATE_INFO_PANEL;
    } else if (start_screen && strcmp(start_screen, "recent") == 0) {
        browser_category = 4; load_recent(); state = STATE_RECENT;
    } else if (start_screen &&
               (strcmp(start_screen, "options") == 0 ||
                strcmp(start_screen, "rom-info") == 0 ||
                strcmp(start_screen, "save-info") == 0) &&
               sys_count > 0 && game_count > 0) {
        char launch[512];
        snprintf(launch, sizeof(launch), "%s/launch.sh", systems[sys_sel].emu_dir);
        enter_game_options(games[game_sel].name, games[game_sel].path, launch,
                           systems[sys_sel].label, STATE_GAMES);
        if (strcmp(start_screen, "rom-info") == 0) game_opts_mode = 1;
        if (strcmp(start_screen, "save-info") == 0) game_opts_mode = 2;
    } else {
        browser_category = 0; state = STATE_MOST_PLAYED;
    }

    log_timer_end(_t_startup);
    log_msg("entering main loop");

    /* Use minimum CPU frequency in the launcher — runtime.sh bumps to
       performance before game launch and resets to ondemand after return. */
    {
        FILE *gov = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "w");
        if (gov) { fputs("powersave", gov); fclose(gov); }
    }

    g_last_input = time(NULL);  /* start idle timer from launch, not epoch */
    health_log_sample("launch");

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
                g_dirty = 1;
                g_last_input = time(NULL);
                if (g_idle_dimmed) {
                    apply_brightness(g_pre_dim_brightness);
                    g_idle_dimmed = 0;
                }
                SDLKey k = ev.key.keysym.sym;
                switch (state) {
                case STATE_HOME:    on_home_key(k);    break;
                case STATE_SYSTEMS: on_systems_key(k); break;
                case STATE_GAMES:   on_games_key(k);   break;
                case STATE_RECENT:
                    on_entry_key(k, recent_entries, recent_count, &recent_sel, &recent_offset, STATE_RECENT);
                    break;
                case STATE_FAVORITES:
                    on_favorites_key(k);
                    break;
                case STATE_MOST_PLAYED:
                    on_most_played_key(k);
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

        if (stress_test && ++stress_frames % 60 == 0) {
            run_stress_step(stress_frames / 60);
            g_last_input = time(NULL);  /* keep the stress test awake */
            if (g_idle_dimmed) {
                apply_brightness(g_pre_dim_brightness);
                g_idle_dimmed = 0;
            }
            g_dirty = 1;
        }

        /* Idle backlight dim: after 30s no input, drop brightness 3 steps */
        {
            time_t now = time(NULL);
            if (!g_idle_dimmed && g_last_input > 0 && (now - g_last_input) >= 30) {
                g_pre_dim_brightness = json_int_file(POCKETOS_ROOT "/system.json", "brightness", 5);
                apply_brightness(clampi(g_pre_dim_brightness - 3, 0, 10));
                g_idle_dimmed = 1;
            }
        }

        /* Per-minute tasks: clock tick + battery change + auto-sleep */
        {
            time_t now = time(NULL);
            int _utc = json_int_file(POCKETOS_ROOT "/system.json", "utcoffset", 0);
            time_t disp = now + _utc * 3600;
            struct tm *tm = localtime(&disp);
            if (tm->tm_min != g_last_clock_min) {
                g_last_clock_min = tm->tm_min;
                g_dirty = 1;
                health_log_sample("minute");

                /* Battery change detection (cached, so cheap) */
                int batt = read_battery();
                if (batt != g_batt_last) { g_batt_last = batt; g_dirty = 1; }

                /* Auto-sleep idle timer */
                int hibernate_min = json_int_file(POCKETOS_ROOT "/system.json", "hibernate", 0);
                if (hibernate_min > 0 && (now - g_last_input) >= hibernate_min * 60)
                    exec_power_cmd("echo mem > /sys/power/state");
            }
        }

        /* Screenshot toast counts down — keep rendering until it clears */
        if (screenshot_toast_frames > 0) g_dirty = 1;
        if (g_dirty) {
            g_dirty = 0;
            render();
        }
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
        if (stress_end && time(NULL) >= stress_end) running = 0;
    }

    health_log_sample("exit");
    clear_text_cache();
    TTF_CloseFont(font_body);
    TTF_CloseFont(font_game);
    TTF_CloseFont(font_large);
    TTF_CloseFont(font_small);
    for (int i = 0; i < asset_cache_count; i++) {
        SDL_FreeSurface(asset_cache[i].surface);
    }
    if (g_dim_overlay) { SDL_FreeSurface(g_dim_overlay); g_dim_overlay = NULL; }
    // Persist settings so they survive reboot (mirrors runtime.sh save_settings)
    {
        char sn[64] = {0};
        FILE *snf = fopen("/tmp/deviceSN", "r");
        if (snf) { if (!fgets(sn, sizeof(sn), snf)) sn[0] = '\0'; fclose(snf);
            char *nl = strchr(sn,'\n'); if (nl) *nl='\0'; }
        if (sn[0]) {
            char cmd[512];
            snprintf(cmd, sizeof(cmd),
                "cp -f " POCKETOS_ROOT "/system.json "
                SYSDIR "/config/system/%s.json", sn);
            int rc = system(cmd);
            if (rc != 0) log_int("persist settings rc", rc);
        }
    }

    shutdown_audio();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    log_close();
    return 0;
}
