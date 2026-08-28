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
        for color in ("0x0E, 0x0F, 0x13", "0xFF, 0xAD, 0x33", "0x3E, 0xCF, 0x6E",
                      "0xA7, 0x8B, 0xFA", "0xFF, 0x7A, 0x7A", "0x7F, 0xB0, 0xFF"):
            self.assertIn(color, self.source)
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

    def test_secondary_views_share_the_dark_pocketos_frame(self):
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
        self.assertIn('"bg":            "#F7F6FB"', theme)
        self.assertIn('"bar":           "#FFFFFF"', theme)
        self.assertIn('"sel_border":    "#7B4DCC"', theme)
        self.assertIn('"text":          "#20182E"', theme)

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
        self.assertEqual("#FFFFFF", json.loads((theme_dir / "theme_onion.json").read_text())["bar"])
        self.assertEqual("#20242A", json.loads((theme_dir / "theme_ink.json").read_text())["bar"])
        self.assertEqual("#1D2533", json.loads((theme_dir / "theme_snow.json").read_text())["sel"])

    def test_device_info_uses_onion_runtime_version_sources(self):
        self.assertIn('/.tmp_update/onionVersion/version.txt', self.source)
        self.assertIn('/etc/fw_printenv miyoo_version', self.source)
        self.assertIn('#define ONION_BASE_VERSION "v4.3.1-1"', self.source)


if __name__ == "__main__":
    unittest.main()
