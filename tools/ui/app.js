// app.js — PocketOS Installer UI (pywebview edition)
// Uses React.createElement — no JSX / Babel needed.
const { useState, useEffect, useRef } = React;
const { createElement: h } = React;

const VERSION = "v1.0";
const ACCENT_VARS = {
  "--accent-fill": "#d4ccf6",
  "--accent-border": "#9a89dc",
  "--accent": "#7b69cf",
  "--accent-shadow": "#5d4caf",
};

// ── Helpers ────────────────────────────────────────────────────────────────────

function api(method, ...args) {
  if (window.pywebview && window.pywebview.api) {
    return window.pywebview.api[method](...args);
  }
  return Promise.resolve(null);
}

function onPywebviewReady(fn) {
  if (window.pywebview && window.pywebview.api) { fn(); return; }
  window.addEventListener("pywebviewready", fn, { once: true });
}

// ── App ────────────────────────────────────────────────────────────────────────

function App() {
  const [phase, setPhase]       = useState("idle");
  const [card, setCard]         = useState(null);   // { path, name }
  const [drives, setDrives]     = useState([]);
  const [showDrives, setShowDrives] = useState(false);
  const [pct, setPct]           = useState(0);
  const [log, setLog]           = useState([]);
  const [doImport, setDoImport] = useState(false);
  const [romSrc, setRomSrc]     = useState("");
  const [doClean, setDoClean]   = useState(true);
  const [updateInfo, setUpdateInfo] = useState(null);
  const [onionWarn, setOnionWarn]   = useState(false);

  const consoleRef = useRef(null);
  const cardRef    = useRef(null);
  useEffect(() => { cardRef.current = card; }, [card]);

  // ── Global callbacks Python will call via evaluate_js ──
  useEffect(() => {
    window.__pushLog = (text, kind) =>
      setLog(l => [...l, { t: text, kind: kind || "info" }]);
    window.__pushPct = p => setPct(p);
    window.__setPhase = ph => setPhase(ph);
    window.__showUpdate = (tag, url) => setUpdateInfo({ tag, url });
    window.__getSdPath = () => cardRef.current ? cardRef.current.path : null;

    onPywebviewReady(() => {
      api("auto_detect").then(drvs => {
        if (!drvs) return;
        setDrives(drvs);
        if (drvs.length === 1) _selectDrive(drvs[0]);
      });
      api("check_update");
    });
  }, []);

  // Auto-scroll console
  useEffect(() => {
    if (consoleRef.current)
      consoleRef.current.scrollTop = consoleRef.current.scrollHeight;
  }, [log]);

  // ── Drive / path selection ──
  const _selectDrive = async (d) => {
    setShowDrives(false);
    const info = await api("validate_path", d.path);
    setOnionWarn(info ? !info.onion : false);
    setCard(d);
    setPhase("ready");
    setPct(0);
    setLog([{ t: `Selected ${d.path} — SD card detected`, kind: "ok" }]);
  };

  const browseSD = async () => {
    const path = await api("browse_sd");
    if (!path) return;
    const info = await api("validate_path", path);
    if (info && info.valid) {
      _selectDrive({ path, name: info.name });
    } else {
      setLog(l => [...l, { t: "⚠ That folder doesn't look like an SD card root — select the top-level folder.", kind: "warn" }]);
    }
  };

  const browseROMs = async () => {
    const path = await api("browse_roms");
    if (path) setRomSrc(path);
  };

  // ── Install / uninstall ──
  const startInstall = () => {
    if (!card) return;
    setPct(0);
    setLog([{ t: `Starting PocketOS ${VERSION} install on ${card.path}`, kind: "info" }]);
    setPhase("installing");
    api("start_install", card.path, romSrc, doImport, doClean);
  };

  const startUninstall = () => {
    if (!card) return;
    setPct(0);
    setPhase("uninstalling");
    setLog([{ t: `Removing PocketOS from ${card.path}`, kind: "info" }]);
    api("start_uninstall", card.path);
  };

  const doUpdateInstall = () => {
    if (!updateInfo) return;
    setPhase("installing");
    setPct(0);
    setLog([{ t: `Downloading PocketOS ${updateInfo.tag}…`, kind: "info" }]);
    api("download_update", updateInfo.tag, updateInfo.url);
  };

  const reset = () => {
    setPhase("idle"); setCard(null); setPct(0); setLog([]);
    setDoImport(false); setRomSrc(""); setOnionWarn(false); setUpdateInfo(null);
    onPywebviewReady(() => api("auto_detect").then(drvs => setDrives(drvs || [])));
  };

  // ── Derived display state ──
  const busy     = phase === "installing" || phase === "uninstalling";
  const done     = phase === "success" || phase === "removed";
  const segCount = 22;
  const segsOn   = Math.round((pct / 100) * segCount);
  const allDone  = pct >= 100;

  const pill = (() => {
    if (phase === "idle")                        return { cls: "", txt: "",        show: false };
    if (phase === "ready")                       return { cls: "", txt: "READY",   show: true };
    if (busy)                                    return { cls: "busy", txt: pct + "%", show: true };
    if (phase === "success" || phase === "removed") return { cls: "", txt: "DONE", show: true };
    if (phase === "error")                       return { cls: "err", txt: "ERROR", show: true };
    return { cls: "", txt: "", show: false };
  })();

  const footStatus = (() => {
    switch (phase) {
      case "idle":         return { cls: "",      txt: "Select your SD card to get started." };
      case "ready":        return { cls: "ready", txt: `${card.name} ready — click Install to flash PocketOS.` };
      case "installing":   return { cls: "busy",  txt: `Installing PocketOS… ${pct}%` };
      case "uninstalling": return { cls: "busy",  txt: `Removing PocketOS… ${pct}%` };
      case "success":      return { cls: "done",  txt: "PocketOS installed! Eject the card and boot your Miyoo." };
      case "removed":      return { cls: "done",  txt: "PocketOS removed. Your SD card has been restored." };
      case "error":        return { cls: "err",   txt: "Install failed — see log above, then retry." };
      case "confirm":      return { cls: "",      txt: "Confirm removal of PocketOS." };
      default:             return { cls: "",      txt: "" };
    }
  })();

  // ── Render ──
  return h("div", { style: { display: "flex", justifyContent: "center", width: "100%" } },
    h("div", { className: "win", style: ACCENT_VARS },

      // ── Title bar ──
      h("div", { className: "titlebar" },
        h("span", { className: "logo" }, h(Handheld, { scale: 2.4 })),
        h("span", { className: "ttl" }, "PocketOS Installer"),
        h("span", { className: "ver" }, VERSION),
        h("span", { className: "spacer" }),
        pill.show && h("span", { className: "statpill " + pill.cls },
          h("span", { className: "dot" }), pill.txt
        ),
        h("span", { className: "wbtns" },
          h("span", { className: "wbtn" }, "–"),
          h("span", { className: "wbtn close", onClick: reset, title: "Close / reset" }, "✕")
        )
      ),

      // ── Update banner ──
      updateInfo && h("div", { className: "update-banner" },
        h("span", { className: "ub-text" },
          `★ ${updateInfo.tag} available`
        ),
        h("button", { className: "ub-btn", onClick: doUpdateInstall },
          `Download & Install ${updateInfo.tag}`
        )
      ),

      h("div", { className: "body-tex" },

        // ── Hero ──
        h("div", { className: "hero" },
          h("div", { className: "wordmark" }, "Pocket", h("span", { className: "os" }, "OS")),
          h("div", { className: "sub" }, `Installer  ·  ${VERSION}`),
          h("div", { className: "tag" },
            "A minimal launcher for the ", h("b", null, "Miyoo Mini Plus"), h("br"),
            "Built on top of Onion OS"
          )
        ),

        // ── Content ──
        h("div", { className: "content" },

          // SD card
          h("div", { className: "field-label" },
            h("span", { className: "lab" }, "SD Card"),
            h("span", { className: "hint" }, "select the root of your Miyoo SD card")
          ),

          h("div", { className: "sd-row" },
            h("div", { className: "sd-input" },
              h(SdCard, { scale: 4 }),
              h("span", { className: "path " + (card ? "" : "placeholder") },
                card ? card.path : "No card selected…"
              )
            ),
            h("button", {
              className: "btn btn-ghost", disabled: busy,
              onClick: () => drives.length > 0 ? setShowDrives(s => !s) : browseSD()
            }, h(Folder, { scale: 3.5 }), " Browse"),

            showDrives && h("div", { className: "drives" },
              h("div", { className: "dh" }, "Detected drives"),
              drives.map(d =>
                h("div", { key: d.path, className: "drive", onClick: () => _selectDrive(d) },
                  h(SdCard, { scale: 3 }),
                  h("div", { className: "di" },
                    h("div", { className: "dn" }, d.name),
                    h("div", { className: "dd" }, d.path)
                  ),
                  h(Check, { scale: 3 })
                )
              ),
              h("div", { className: "drive", onClick: () => { setShowDrives(false); browseSD(); } },
                h(Folder, { scale: 3 }),
                h("div", { className: "di" },
                  h("div", { className: "dn" }, "Browse…"),
                  h("div", { className: "dd" }, "Choose a different folder")
                )
              )
            )
          ),

          card && h("div", { className: "card-chip" },
            h(SdCard, { scale: 4.5 }),
            h("div", { className: "meta" },
              h("div", { className: "name" }, card.name),
              h("div", { className: "det" }, card.path)
            ),
            h("span", { className: "ok" }, h(Check, { scale: 2.5 }), " DETECTED")
          ),

          onionWarn && h("div", { className: "onion-warn" },
            h("span", null, "⚠️ Onion OS not detected. PocketOS requires Onion OS — install it first, then come back."),
            h("button", { onClick: () => api("open_url", "https://github.com/OnionUI/Onion/releases/latest") },
              "Get Onion OS →"
            )
          ),

          // ROM import
          h("div", { className: "sep" }),
          h("div", { className: "field-label" },
            h("span", { className: "lab" }, "ROM Import"),
            h("span", { className: "hint" }, "optional · unzip ROMs, scan genres, clean duplicates")
          ),
          h("label", { className: "import-chk" },
            h("input", { type: "checkbox", checked: doImport, onChange: e => setDoImport(e.target.checked) }),
            " Import ROMs from a folder of ZIP files"
          ),
          doImport && h("div", { className: "sd-row", style: { marginTop: "8px" } },
            h("div", { className: "sd-input" },
              h(Folder, { scale: 4 }),
              h("span", { className: "path " + (romSrc ? "" : "placeholder") },
                romSrc || "No folder selected…"
              )
            ),
            h("button", { className: "btn btn-ghost", disabled: busy, onClick: browseROMs }, "Browse")
          ),
          doImport && h("label", { className: "import-chk sub-chk" },
            h("input", { type: "checkbox", checked: doClean, onChange: e => setDoClean(e.target.checked) }),
            " Remove duplicate / bad / hacked dumps — keep the best version of each game"
          ),

          // Action buttons
          h("div", { className: "sep" }),

          phase === "confirm"
            ? h("div", { className: "confirm" },
                h(Trash, { scale: 3.5 }),
                h("span", { className: "q" }, `Remove PocketOS from ${card ? card.path : ""}?`),
                h("button", { className: "btn btn-danger btn-sm", onClick: startUninstall }, "Yes, remove"),
                h("button", { className: "btn btn-ghost btn-sm", onClick: () => setPhase("ready") }, "Cancel")
              )
            : busy
            ? h("div", { className: "actions" },
                h("button", { className: "btn btn-ghost", style: { flex: 1 }, onClick: () => {} },
                  "⏳ Working…"
                )
              )
            : done
            ? h("div", { className: "actions" },
                h("button", { className: "btn btn-primary", onClick: reset },
                  h("span", { className: "glyph" }, "A"), " Eject & Finish"
                )
              )
            : h("div", { className: "actions" },
                h("button", { className: "btn btn-primary", disabled: !card || onionWarn, onClick: startInstall },
                  h("span", { className: "glyph" }, "A"),
                  " ", phase === "error" ? "Retry Install" : "Install PocketOS"
                ),
                h("button", { className: "btn btn-danger", disabled: !card, onClick: () => setPhase("confirm") },
                  h(Trash, { scale: 3 }), " Uninstall"
                )
              ),

          // Progress bar
          (busy || done || phase === "error") && h("div", { className: "progress-wrap" },
            h("div", { className: "progress-head" },
              h("span", { className: "lbl" },
                ({ installing: "Installing", success: "Installed", uninstalling: "Removing", removed: "Removed", error: "Failed" })[phase] || "Installing"
              ),
              h("span", { className: "pc" }, pct + "%")
            ),
            h("div", { className: "segbar" },
              Array.from({ length: segCount }, (_, i) =>
                h("div", { key: i, className: "seg" + (i < segsOn ? " on" : "") + (allDone ? " done" : "") })
              )
            )
          ),

          // Console log
          h("div", { className: "console", ref: consoleRef },
            log.length === 0
              ? h("div", { className: "empty" },
                  "PocketOS Installer ready. Output will appear here",
                  h("span", { className: "cursor" })
                )
              : log.map((l, i) =>
                  h("div", { key: i, className: "logline " + l.kind },
                    h("span", { className: "gt" }, ">"),
                    h("span", null,
                      l.t,
                      i === log.length - 1 && busy ? h("span", { className: "cursor" }) : null
                    )
                  )
                )
          )
        ) // content
      ), // body-tex

      // ── Footer ──
      h("div", { className: "footer" },
        h("div", { className: "hints" },
          h("span", { className: "hint" },
            h("span", { className: "glyph-btn a" }, "A"),
            done ? "Finish" : busy ? "—" : "Install"
          ),
          h("span", { className: "hint " + (card ? "" : "off") },
            h("span", { className: "glyph-btn b" }, "B"),
            busy ? "Cancel" : "Back"
          )
        ),
        h("span", { className: "spacer" }),
        h("span", { className: "foot-status " + footStatus.cls },
          h("span", { className: "sdot" }),
          footStatus.txt
        )
      )
    )
  );
}

ReactDOM.createRoot(document.getElementById("root")).render(h(App, null));
