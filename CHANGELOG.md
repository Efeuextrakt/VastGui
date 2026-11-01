# Changelog

## 1.1.0 - 2026-11-02

### Changed
- The `vastai` CLI backend is gone: every action uses the vast.ai REST API directly (no Python, no child
  processes). Removed with it: the backend selector and *vastai binary* setting, the raw command input of the CLI
  Console (the page remains as the full **Activity log** viewer), *Query ssh-url via vastai* (the SSH / SCP
  commands are computed locally) and the Search *Copy as CLI command* buttons.
- Ported to REST: **Cloud copy** (`POST /commands/rclone/`), **Update workers** (`POST run.vast.ai/update_workers/`)
  and **Copy files** between remote locations (`PUT /commands/copy_direct/`). Copies to or from this PC are no
  longer offered (the CLI ran a local rsync for them).
- The activity log shows the API request of each action (`> PUT /api/v0/...`) instead of a CLI command line;
  request bodies are never logged.
- The vastai CLI key file (`~/.config/vastai/vast_api_key`) is no longer read or written; the key comes from
  `VAST_API_KEY` or VastGui.json. Linux builds now require libcurl.

### License
- VastGui is now licensed under the PolyForm Noncommercial License 1.0.0: free for personal, hobby, study,
  research and non-profit use, no commercial use (see `LICENSE.md`). Bundled third-party components keep
  their own licenses; full texts are in `THIRD_PARTY_LICENSES.txt`.

### Security
- The API key in `VastGui.json` is now stored encrypted (`api_key_enc`, XChaCha20-Poly1305 via the vendored
  Monocypher) with a random key in `secret.key` in the per-user config folder (also in portable mode). Existing
  plaintext `api_key` entries are encrypted on the next start.
- The REST backend sends the key only over `https://` (`http://` for localhost only) and never forwards the
  `Authorization` header or a request body on a redirect to another host, nor from https to http.
- No more automatic retries of POST/PUT/DELETE after 502/503/504 (could rent an instance twice).
- Linux: settings, key and log files are created owner-only (`0600`).
- Previous API keys stay masked in the log after the key is changed; *Test key* with an unsaved key is now labelled
  *Save & test*; HTTP responses are size-capped.

### Fixed
- Instance status now follows the vast.ai web console exactly (port of its status logic): interruptible
  instances show *outbid* when the console does (also when stopped), plus *scheduling*, *inactive*,
  *unavailable*, *starting*, *expired*, *insufficient credits* with the console's explanation as tooltip.
- Possible crash on exit: ImGui saved `imgui.ini` from a dangling filename pointer after the app was destroyed.

## 1.0.0 - 2026-11-01

First release of VastGui, a native desktop GUI for vast.ai: a single static `VastGui.exe` (C++20, Dear ImGui,
DirectX 11) with no DLLs or external assets. A Linux build (GLFW + OpenGL 3) is also supported.

### Search
- Offer search like the vast.ai console: #GPUs, rental type (on-demand / interruptible / reserved), GPU models,
  region, sort, card or table view, filter panel with 20 range sliders, raw query, presets.
- More complete and stable results: vast.ai returns a random sample per request, so every search sends several
  identical requests (default 5, limit 1000 each; both configurable) and merges them by machine + GPU count.
- TFLOPS/$/hr and DLP/$/hr metrics, colour-coded green to red within the results.
- Defective-machine filter (low reliability, missing/zero key values, performance far below the model median,
  blacklisted machines) with a counter and a "show greyed out" option. Deverified machines are only listed on request.
- Incompatible offers (failing the template's CUDA / extra-filter requirements) get an INCOMPATIBLE badge.
- Offers on machines you already rent are hidden by default (optional RENTED BY YOU badge).
- Rent dialog: disk, label, bid, volume; template summary with *Change...* (pick a template for this rental only)
  and a collapsed *Template settings* section (image, launch mode, env, ports, docker options, on-start).

### Instances
- Status table with outbid detection, CPU/GPU/VRAM/disk usage, TFLOPS/$/hr and DLP/$/hr, $/hr sparkline,
  bulk start/stop/reboot/destroy and all CLI actions (logs, SSH/SCP, Jupyter, copy, cloud copy, snapshot,
  change bid, execute, templates from instances, recycle, update, machine blacklist).
- Logs window with Instance / Daemon tabs, line numbers, auto-refresh, tail 1000 (+/- 1000, Max 20000), save to file.
- Add a Reserved Discount: Save NN% badge, 1 / 3 / 6 month plans or custom amount, two-step purchase.

### Other pages
- Templates (tabs: My templates, Recommended, Search; create / edit / delete), Storage, Serverless, Billing and
  Earnings (charts, CSV export), Members / Team, Audit Logs, Keys (SSH, API keys, env vars), Hosting, CLI Console.

### General
- Two backends: the official `vastai` CLI (hidden console, timeouts) or the native REST API (WinHTTP / libcurl);
  auto-detected by default. Nothing blocks the UI: all calls run on a worker pool.
- API key from `VAST_API_KEY`, `VastGui.json` or the vastai key file; never logged (masked as `****`). Optional
  saving to the vastai CLI key file.
- Every dialog has a close button and closes on a click outside it; floating windows close on an outside click
  unless docked. Commands are only shown in the Activity log / CLI Console (optional Search buttons).
- Window size, position and docking layout are restored after a restart. Dark/light theme, accent colour,
  font size / UI scale (applied with *Apply*), per-monitor DPI.
- Config files only: `imgui.ini` and `VastGui.json` in `%APPDATA%\VastGui` (or next to the exe in portable mode).
