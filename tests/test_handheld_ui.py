import unittest
import json
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

    def test_release_version_is_1_2_0(self):
        self.assertIn('#define POCKETOS_VERSION "1.2.0"', self.source)

    def test_library_system_rows_do_not_repeat_names_in_badges(self):
        library = self.source[
            self.source.index("static void draw_library_shell"):
            self.source.index("static void draw_favorites_shell")
        ]
        self.assertNotIn("draw_browser_badge", library)
        self.assertIn("draw_text(font_body, label, 18", library)

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
            "recent", "options", "rom-info", "save-info",
        ):
            self.assertIn(f'"{screen}"', renderer)

    def test_legacy_themes_feed_the_redesigned_palette(self):
        self.assertIn("static void refresh_browser_palette(void)", self.source)
        self.assertIn("theme_accent = mapped_color(C_SEL_BORDER)", self.source)
        self.assertIn("theme_pick_sel = current_theme_index()", self.source)
        self.assertIn("browser_palette.is_light", self.source)
        self.assertIn("color_luma(bg) >= 145", self.source)
        self.assertIn("if (is_light)", self.source)
        renderer = (ROOT / "tools" / "render_handheld_ui.py").read_text(encoding="utf-8")
        self.assertIn("THEME_VARIANTS", renderer)

    def test_light_onion_theme_is_available(self):
        theme = (ROOT / "assets" / "res" / "pocketos" / "theme_onion.json").read_text(
            encoding="utf-8"
        )
        self.assertIn("C_BG          = RGBA(0xF8, 0xF1, 0xE6)", self.source)
        self.assertIn("C_BAR         = RGBA(0xFF, 0xFD, 0xF8)", self.source)
        self.assertIn("C_SEL         = RGBA(0x7D, 0x3C, 0xFF)", self.source)
        self.assertNotIn("C_BAR         = RGBA(0x07, 0x1A, 0x33)", self.source)
        self.assertIn('"bg":            "#F8F1E6"', theme)
        self.assertIn('"bar":           "#FFFDF8"', theme)
        self.assertIn('"sel":           "#7D3CFF"', theme)
        self.assertIn('"sel_border":    "#5C1FE0"', theme)
        self.assertIn('"text":          "#251934"', theme)

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
            self.assertTrue(data["sel"].startswith(("#6", "#7", "#8")))
        self.assertEqual("#7D3CFF", json.loads((theme_dir / "theme_onion.json").read_text())["sel"])
        self.assertEqual("#7834F8", json.loads((theme_dir / "theme_ink.json").read_text())["sel"])
        self.assertEqual("#6F2DFF", json.loads((theme_dir / "theme_snow.json").read_text())["sel"])

    def test_device_info_uses_onion_runtime_version_sources(self):
        self.assertIn('/.tmp_update/onionVersion/version.txt', self.source)
        self.assertIn('/etc/fw_printenv miyoo_version', self.source)
        self.assertIn('#define ONION_BASE_VERSION "v4.3.1-1"', self.source)


if __name__ == "__main__":
    unittest.main()
