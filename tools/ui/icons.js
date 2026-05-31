// icons.js — PixelArt renderer + chunky pixel-art icons for PocketOS Installer.
const { createElement: h } = React;

function PixelArt({ grid, palette, scale = 4, style }) {
  const rows = grid.length;
  const cols = Math.max(...grid.map((r) => r.length));
  const rects = [];
  for (let y = 0; y < rows; y++) {
    const row = grid[y];
    for (let x = 0; x < row.length; x++) {
      const ch = row[x];
      const fill = palette[ch];
      if (!fill || fill === "transparent") continue;
      rects.push(h("rect", { key: x + "-" + y, x, y, width: 1.02, height: 1.02, fill }));
    }
  }
  return h("svg", {
    width: cols * scale, height: rows * scale,
    viewBox: `0 0 ${cols} ${rows}`,
    shapeRendering: "crispEdges",
    style: { display: "block", ...style },
  }, rects);
}

function SdCard({ scale = 5 }) {
  return h(PixelArt, {
    scale,
    palette: { ".": "transparent", "k": "#101426", "d": "#5d4caf", "p": "#8675d6", "l": "#d4ccf6", "w": "#f3f0ff" },
    grid: ["..kkkkkk", ".kwlllpk", "kkdldldk", "kpdldldk", "kplllllk", "kplllllk", "kpllwllk", "kplllllk", "kppppppk", ".kkkkkkk"],
  });
}

function Install({ scale = 5 }) {
  return h(PixelArt, {
    scale,
    palette: { ".": "transparent", "k": "#0d1a2e", "g": "#4fb733", "l": "#a6f088", "w": "#ffffff" },
    grid: ["...kk...", "...gk...", "...gk...", ".k.gk.k.", ".kggggk.", "..kggk..", "...kk...", "kk....kk", "kggggggk", "kkkkkkkk"],
  });
}

function Trash({ scale = 5 }) {
  return h(PixelArt, {
    scale,
    palette: { ".": "transparent", "k": "#b6392a", "r": "#ea5440", "w": "#ffd9d2" },
    grid: ["..kkkk..", ".kk..kk.", "kkkkkkkk", ".krwrwrk", ".krwrwrk", ".krwrwrk", ".krwrwrk", ".kkrrrkk", "..kkkk.."],
  });
}

function Check({ scale = 4, color = "#4fb733" }) {
  return h(PixelArt, {
    scale,
    palette: { ".": "transparent", "g": color },
    grid: ["......gg", ".....gg.", "g...gg..", "gg.gg...", ".ggg....", "..g....."],
  });
}

function Folder({ scale = 4 }) {
  return h(PixelArt, {
    scale,
    palette: { ".": "transparent", "k": "#13243f", "y": "#f0c552", "o": "#d99b2e", "w": "#fbe6a6" },
    grid: [".kkk....", "kywwk...", "kyyyykkk", "kwyyyyyk", "kyyyyyyk", "koyyyyok", "kkkkkkkk"],
  });
}

function Handheld({ scale = 5 }) {
  return h(PixelArt, {
    scale,
    palette: { ".": "transparent", "k": "#0d1a2e", "g": "#2b3a55", "s": "#82ea5f", "c": "#cfe9ff", "p": "#d4ccf6", "r": "#ea5440" },
    grid: [".kkkkkkkkkk.", "kkggggggggkk", "kgkcccccckgk", "kgkcssssckgk", "kgkcssssckgk", "kgkcccccckgk", "kgggggggggk", "kgpgggggrgk", "kgggggggggk", ".kkkkkkkkkk."],
  });
}
