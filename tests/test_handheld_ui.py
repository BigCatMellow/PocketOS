import unittest
import json
import colorsys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "pocketOS" / "pocketOS.c"


class HandheldUiContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = SOURCE.read_text(encoding="utf-8")

    def test_five_primary_categories_are_present(self):
        for label in ("MOST PLAYED", "BROWSE", "LIBRARY", "FAVORITES", "SETTINGS"):
            self.assertIn(f'"{label}"', self.source)
        self.assertIn("(category + 5) % 5", self.source)

    def test_category_navigation_and_paging_use_separate_shoulders(self):
        self.assertIn("cycle_browser_category(-1)", self.source)
        self.assertIn("cycle_browser_category(1)", self.source)
        self.assertIn("k == BTN_L2", self.source)
        self.assertIn("k == BTN_R2", self.source)

    def test_primary_views_use_fixed_low_cost_palette(self):
        for color in ("0xF8, 0xF1, 0xE6", "0xFF, 0xFD, 0xF8", "0x25, 0x19, 0x34",
                      "0x7D, 0x3C, 0xFF", "0x9B, 0x6B, 0xFF", "0x5C, 0x1F, 0xE0"):
            self.assertIn(color, self.source)
        self.assertIn("browser_palette.dark_text = SC_WHITE", self.source)
        primary_shell = self.source[
            self.source.index("draw_most_played_shell"):
            self.source.index("__attribute__((unused)) static void draw_browse")
        ]
        self.assertNotIn("SDL_SetAlpha", primary_shell)

    def test_release_version_is_1_2_8(self):
        self.assertIn('#define POCKETOS_VERSION "1.2.8"', self.source)

    def test_library_system_rows_do_not_repeat_names_in_badges(self):
        library = self.source[
            self.source.index("static void draw_library_shell"):
            self.source.index("static void draw_favorites_shell")
        ]
        self.assertNotIn("draw_browser_badge", library)
        self.assertIn("draw_text(font_body, label, 18", library)

    def test_browse_and_library_share_two_panel_geometry_and_font_roles(self):
        browse = self.source[
            self.source.index("static void draw_browse_shell"):
            self.source.index("static void draw_library_shell")
        ]
        library = self.source[
            self.source.index("static void draw_library_shell"):
            self.source.index("static void draw_favorites_shell")
        ]
        self.assertIn("const int left_w = 260", browse)
        self.assertIn("const int left_w = 260", library)
        self.assertIn("TWO_PANEL_LEFT_ROWS", browse)
        self.assertIn("TWO_PANEL_LEFT_ROWS", library)
        self.assertIn("left_y0 + row * 54", browse)
        self.assertIn("left_y0 + row * 54", library)
        self.assertIn("draw_text(font_body, label, 18", browse)
        self.assertIn("draw_text(font_body, label, 18", library)
        self.assertIn("truncate_to_fit(font_body, browse_game_pool[idx].title", browse)
        self.assertIn("draw_text(font_body, title, title_x, y + 20", browse)
        self.assertNotIn("font_game", browse)
        self.assertIn("truncate_to_fit(font_body, games[idx].name", library)
        self.assertIn("draw_text(font_body, title, title_x, y + 20", library)
        self.assertNotIn("font_game", library)

    def test_most_played_and_favorites_match_settings_body_text(self):
        most_played = self.source[
            self.source.index("static void draw_most_played_shell"):
            self.source.index("static void draw_browse_shell")
        ]
        favorites = self.source[
            self.source.index("static void draw_favorites_shell"):
            self.source.index("static const char *SETTINGS_HUB_LABELS")
        ]
        for view in (most_played, favorites):
            self.assertIn("truncate_to_fit(font_body", view)
            self.assertIn("draw_text(font_body, title", view)
            self.assertNotIn("font_game", view)

    def test_library_uses_familiar_compact_system_names(self):
        for compact in ("NES", "SNES", "GB", "GBC", "GBA", "Genesis", "PS1"):
            self.assertIn(f'return "{compact}";', self.source)
        self.assertIn('strcasecmp(label, "MD")      == 0) return "Genesis"', self.source)

    def test_secondary_views_share_the_light_pocketos_frame(self):
        for function in (
            "draw_apps", "draw_settings", "draw_font_picker", "draw_theme_picker",
            "draw_info_panel", "draw_game_options", "draw_entry_list",
        ):
            start = self.source.rindex(f"static void {function}")
            body_start = self.source.index("{", start)
            next_function = self.source.find("\nstatic ", body_start)
            body = self.source[body_start:next_function]
            self.assertTrue(
                "draw_secondary_frame" in body or function == "draw_game_options",
                f"{function} does not use the PocketOS secondary frame",
            )
            self.assertNotIn("draw_textured_bg", body)
            self.assertNotIn("draw_select_asset", body)

    def test_host_renderer_covers_secondary_views(self):
        renderer = (ROOT / "tools" / "render_handheld_ui.py").read_text(encoding="utf-8")
        for screen in (
            "apps", "settings-list", "font", "theme", "device", "about",
            "appearance", "recent", "options", "rom-info", "save-info", "test-center",
        ):
            self.assertIn(f'"{screen}"', renderer)

    def test_legacy_themes_feed_the_redesigned_palette(self):
        self.assertIn("static void refresh_browser_palette(void)", self.source)
        self.assertIn("theme_sel = mapped_color(C_SEL)", self.source)
        self.assertIn("mix_color(theme_sel, theme_border, 120)", self.source)
        self.assertIn("theme_pick_sel = current_theme_index()", self.source)
        self.assertIn('"Appearance"', self.source)
        self.assertIn('"pocketosAppearance"', self.source)
        self.assertIn("apply_appearance_mode();", self.source)
        self.assertIn("browser_palette.is_light", self.source)
        self.assertIn("color_luma(bg) >= 145", self.source)
        self.assertIn("if (is_light)", self.source)
        renderer = (ROOT / "tools" / "render_handheld_ui.py").read_text(encoding="utf-8")
        self.assertIn("THEME_VARIANTS", renderer)

    def test_light_onion_theme_is_available(self):
        theme = json.loads(
            (ROOT / "assets" / "res" / "pocketos" / "theme_onion.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertIn("C_BG          = RGBA(0xF8, 0xF1, 0xE6)", self.source)
        self.assertIn("C_BAR         = RGBA(0xFF, 0xFD, 0xF8)", self.source)
        self.assertIn("C_SEL         = RGBA(0x7D, 0x3C, 0xFF)", self.source)
        self.assertNotIn("C_BAR         = RGBA(0x07, 0x1A, 0x33)", self.source)
        self.assertEqual("#F8F1E6", theme["bg"])
        self.assertEqual("#FFFDF8", theme["bar"])
        self.assertEqual("#7D3CFF", theme["sel"])
        self.assertEqual("#5C1FE0", theme["sel_border"])
        self.assertEqual("#251934", theme["text"])

    def test_light_theme_presets_cover_distinct_surfaces(self):
        theme_dir = ROOT / "assets" / "res" / "pocketos"
        light_names = ("onion", "ink", "porcelain", "snow")
        palettes = {}
        for name in light_names:
            path = theme_dir / f"theme_{name}.json"
            self.assertTrue(path.is_file(), f"missing light theme {name}")
            data = json.loads(path.read_text(encoding="utf-8"))
            palettes[name] = (data["bg"], data["bar"], data["sel"], data["sel_border"])
        self.assertEqual(len(light_names), len(set(palettes.values())))
        for name in light_names:
            data = json.loads((theme_dir / f"theme_{name}.json").read_text())
            bg = data["bg"].lstrip("#")
            r, g, b = (int(bg[i:i + 2], 16) for i in (0, 2, 4))
            self.assertGreaterEqual((299 * r + 587 * g + 114 * b) // 1000, 235)
        self.assertEqual("#7D3CFF", json.loads((theme_dir / "theme_onion.json").read_text())["sel"])
        self.assertEqual("#7834F8", json.loads((theme_dir / "theme_ink.json").read_text())["sel"])
        self.assertEqual("#6F2DFF", json.loads((theme_dir / "theme_snow.json").read_text())["sel"])

    def test_theme_presets_cover_light_and_dark_palettes(self):
        theme_dir = ROOT / "assets" / "res" / "pocketos"
        dark_names = {
            "ayu_dark", "catppuccin_mocha", "dracula_dark", "everforest_dark",
            "gruvbox_dark", "kanagawa", "monokai", "nord_dark", "one_dark",
            "rose_pine", "solarized_dark", "tokyo_night",
        }
        for path in sorted(theme_dir.glob("theme_*.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            name = path.stem.removeprefix("theme_")
            for key in ("bg", "bar", "card"):
                color = data[key].lstrip("#")
                r, g, b = (int(color[i:i + 2], 16) for i in (0, 2, 4))
                luma = (299 * r + 587 * g + 114 * b) // 1000
                if name in dark_names:
                    self.assertLessEqual(luma, 100, f"{path.name} {key} is too light")
                else:
                    self.assertGreaterEqual(luma, 225, f"{path.name} {key} is too dark")
            self.assertGreaterEqual(
                (299 * int(data["text"][1:3], 16) + 587 * int(data["text"][3:5], 16)
                 + 114 * int(data["text"][5:7], 16)) // 1000,
                160 if name in dark_names else 10,
                f"{path.name} text lacks usable contrast",
            )
            self.assertEqual("#FFFFFF", data["white"], f"{path.name} selected text is not white")

    def test_dark_mode_is_a_separate_appearance_setting(self):
        self.assertIn('read_config_int("pocketosAppearance", 0)', self.source)
        self.assertIn('write_config_int("pocketosAppearance", !dark)', self.source)
        self.assertIn('snprintf(out, outlen, "%s", dark ? "Dark" : "Light")', self.source)
        self.assertIn("C_BG          = mapped_pixel(mix_color(base, accent_border, 18))", self.source)

    def test_right_dpad_never_launches_a_game(self):
        self.assertNotIn("k == BTN_A || k == BTN_RIGHT) launch_entry", self.source)
        self.assertIn("if (k == BTN_A) launch_entry(&entries[*sel]);", self.source)
        self.assertIn("if (k == BTN_A) launch_entry(entry);", self.source)
        self.assertIn("if (k == BTN_A) launch_entry(&entry);", self.source)

    def test_health_monitor_is_opt_in_and_bounded(self):
        self.assertIn('#define HEALTH_LOG_PATH SYSDIR "/logs/pocketos_health.csv"', self.source)
        self.assertIn("if (stat(HEALTH_LOG_PATH, &st) != 0) return", self.source)
        self.assertIn("#define HEALTH_LOG_MAX_BYTES (512 * 1024)", self.source)
        self.assertIn('health_log_sample("minute");', self.source)

    def test_health_monitor_parses_proc_files_line_by_line(self):
        start = self.source.index("static long proc_kb_line_value")
        end = self.source.index("static void health_log_sample", start)
        parser = self.source[start:end]
        self.assertIn("fgets(line, sizeof(line), f)", parser)
        self.assertIn("proc_kb_line_value(line, label)", parser)
        self.assertIn("strtol(p, &end, 10)", parser)
        self.assertNotIn("fscanf", parser)

    def test_terminal_stress_test_is_explicit_and_timed(self):
        self.assertIn('getenv("POCKETOS_STRESS_TEST")', self.source)
        self.assertIn('getenv("POCKETOS_STRESS_TEST_SECONDS")', self.source)
        self.assertIn("static void run_stress_step(int step)", self.source)
        runner = (ROOT / "tools" / "pocketos_stress_test.sh").read_text(encoding="utf-8")
        self.assertIn("pocketos_stress_test_seconds", runner)
        self.assertIn("Press MENU once to close Terminal", runner)

    def test_test_center_offers_dpad_presets_without_terminal_typing(self):
        self.assertIn("STATE_TEST_CENTER", self.source)
        self.assertIn('"PocketOS active test - 15 min"', self.source)
        self.assertIn('"PocketOS active test - 30 min"', self.source)
        self.assertIn('"PocketOS active test - 60 min"', self.source)
        self.assertIn('"PocketOS idle baseline - 15 min"', self.source)
        self.assertIn('"PocketOS idle baseline - 30 min"', self.source)
        self.assertIn('"PocketOS idle baseline - 60 min"', self.source)
        self.assertIn('"Onion idle baseline - 15 min"', self.source)
        self.assertIn('"Onion idle baseline - 30 min"', self.source)
        self.assertIn('"Onion idle baseline - 60 min"', self.source)
        self.assertIn('"/tmp/pocketos_failed"', self.source)
        self.assertIn('launcher-comparison-monitor.sh start %s %d', self.source)
        app_root = ROOT / "assets" / "App" / "PocketOS Test Center"
        self.assertTrue((app_root / "config.json").is_file())
        self.assertIn("POCKETOS_START_SCREEN=test-center", (app_root / "launch.sh").read_text())
        self.assertIn('"Test Center",     "app_activity.png"', self.source)

    def test_legacy_onion_baseline_monitor_delegates_to_shared_collector(self):
        runner = (ROOT / "tools" / "onion_baseline_monitor.sh").read_text(encoding="utf-8")
        self.assertIn('launcher-comparison-monitor.sh', runner)
        self.assertIn('exec sh "$MONITOR" start onion "$2"', runner)
        self.assertNotIn("find_mainui_pid", runner)
        self.assertNotIn("sleep 60", runner)

    def test_paired_comparison_monitor_uses_the_same_schema_for_both_launchers(self):
        runner = (ROOT / "tools" / "launcher_comparison_monitor.sh").read_text(encoding="utf-8")
        self.assertIn("pocketos_comparison_health.csv", runner)
        self.assertIn("onion_comparison_health.csv", runner)
        self.assertIn("pocketos:pocketOS", runner)
        self.assertIn("onion:MainUI", runner)
        self.assertIn("sleep 60", runner)
        self.assertIn("KEEP_AWAKE=/tmp/stay_awake", runner)
        self.assertIn("trap cleanup 0", runner)

    def test_theme_accents_have_visible_range(self):
        theme_dir = ROOT / "assets" / "res" / "pocketos"
        hues = []
        accents = set()
        for path in sorted(theme_dir.glob("theme_*.json")):
            data = json.loads(path.read_text(encoding="utf-8"))
            color = data["sel"].lstrip("#")
            r, g, b = (int(color[i:i + 2], 16) for i in (0, 2, 4))
            hue, saturation, _value = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            hues.append(int(hue * 12))
            accents.add(data["sel"])
            self.assertGreaterEqual(saturation, 0.25, f"{path.name} accent is too gray")
        self.assertGreaterEqual(len(accents), 24)
        self.assertGreaterEqual(len(set(hues)), 8)

    def test_device_info_uses_onion_runtime_version_sources(self):
        self.assertIn('/.tmp_update/onionVersion/version.txt', self.source)
        self.assertIn('/etc/fw_printenv miyoo_version', self.source)
        self.assertIn('#define ONION_BASE_VERSION "v4.3.1-1"', self.source)


if __name__ == "__main__":
    unittest.main()
