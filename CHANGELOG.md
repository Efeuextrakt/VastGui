# Changelog

## 1.4.2 - 2026-11-11

### Keys
- *Create API key* is now a dialog like the vast.ai console (button next to *Refresh*; the list uses the full
  width). Instead of typing JSON you tick permissions in a table:
  - **Standard**: Read / Write per type (User, Instances, Billing/Earning, Machines, Team, Admin) and *Enabled* for
    Miscellaneous. Only types your current key has are offered, and Write is greyed out where your key can only
    read. Write includes Read; unticking Read also removes Write.
  - **2FA** switch per permission, or *Require 2FA for this key* for all of them.
  - **Advanced**: the generated permissions as JSON, editable and kept in sync with the table.
  - Tooltips explain every type, Read, Write and 2FA; *Read more* opens vast.ai's permission docs.
  - Starts with everything your current key may do (except admin), like the console. The name must be 3-50
    characters and at least one permission must be selected.
  The request is identical to the console's (`{"api": {"user_read": {}, "instance_read": {"constraints": {"tfa":
  {"eq": true}}}, ...}}`).

## 1.4.1 - 2026-11-08

### Search
- The raw query field is gone.
- The *Search* button now leads the results line ("Search  17 offers - ..."), so it stays in the same place
  whatever the window size (it no longer moves with the wrapping top bar).
- Presets moved under *Disk Space*, in their own *Presets* section with a new layout: a dropdown to pick a saved
  preset (trash icon to delete; the list keeps the dropdown's width, long names are shortened), then *Save*,
  *Load* and *Reset*. The old buttons at the bottom of the panel are gone.
- Blacklisted machines are no longer "defective": they have their own grey *blacklisted* tag (card: next to the
  CUDA line; table: Status column) and border, and their own *Blacklisted Machines* option in *Machine Options*
  (off = hidden). The results header shows how many are hidden. The *Flag blacklisted machines* setting is gone.

### Instances
- Hover the instance ID or the GPU name (table and cards), or the machine line (cards), for the machine's details:
  GPU, performance, CPU and cores, RAM, board, PCIe, disk, network, CUDA / driver / Ubuntu, reliability, location,
  verification.
- The hourly total in the header only counts what is billed to the account: prepaid (reserved) instances are paid
  from their own prepaid credit and are shown separately ("+ 1 prepaid: $0.028/hr"). The $/hr tooltip shows the
  prepaid credit left.

### Billing
- Shows when the credit runs out at the current instance costs: time left plus the local date and time (red under
  1 day, orange under 3 days). Prepaid instances are added at their normal price once their prepaid credit is used
  up.

### Templates
- The query field is only shown on the *Search* tab.

### UI
- Logs (instance logs, Activity panel and Activity log page) use compact line spacing.
- Dropdowns and menus have an opaque background (text behind them no longer shines through).

## 1.4.0 - 2026-11-05

### Search
- *Defective Machines* is now a normal option in *Machine Options* (ticked: shown with the red *defective* tag and
  border; unticked: hidden), replacing the separate section with its nested *Hide* / *Show them* pair. The choice
  is saved with the filters and presets (older settings are converted). The results header shows how many
  defective offers are hidden.
- New *Machines / Hosts* filter: enter machine and/or host IDs (separated by spaces or commas) to search exactly
  those machines. vast.ai is asked for them directly, so they are found even when they are not in the random
  sample. The header shows when the filter is active.

### Settings
- Settings files from older versions or with invalid values are read value by value: an unknown or invalid
  entry (e.g. theme, server URL, sort options, a damaged filters or template section) falls back to its default
  without resetting the other settings, and the Activity log lists what was reset. Out-of-range numbers are
  clamped.

## 1.3.0 - 2026-11-04

### Search
- Prices now match the vast.ai web console. They were lower before because:
  - the price only included storage for the *Disk Space* filter (16 GB by default); like the console, offers are
    now priced (and filtered) for the larger of the filter and the template's recommended disk, and the filter
    panel says so;
  - interruptible offers showed the bare minimum bid; they now show the console's suggested bid (minimum bid +
    5 % of the GPU price), which is also the default bid in the rent dialog;
  - vast.ai's discounted total (`search.discountedTotalPerHour`), SLA premium and, with *Create new local
    volume*, the new volume's cost are now included.

  Sorting, TFLOPS/$/hr and DLP/$/hr use the same price; the price tooltip shows the breakdown.
- Offers on machines you already rent have a blue border and a disabled *RENTED* button (table: blue row and
  *RENTED*) instead of a *RENTED BY YOU* tag.
- Defective offers shown on request are no longer dimmed; the *defective* tag has no icon (the table shows it in
  the Status column).
- The *INCOMPATIBLE* tag has no icon; in the table the GPU name is red instead.
- Verification tags: unverified is yellow, deverified orange (was red).

### Instances
- New card view (default): one wide card per instance with status, name, GPU, machine, image / template, uptime,
  SSH (copy), ports, GPU / CPU / VRAM / disk usage bars, current cost with breakdown, reserve badge, per-dollar
  metrics, $/hr trend and the action icons. A checkbox per card (plus *Select all*) selects instances for the
  toolbar's Start / Stop / Reboot / Destroy; selected cards have a blue border, outbid ones a red border.
  Right-click a card for the full menu, double-click for the raw JSON. Sort by name, ID, status, GPU, $/hr or
  uptime, up or down (default: name A-Z, unnamed last). Card / table toggle on the toolbar; the default view is in
  Settings > Appearance.
- *$/hr*, the header total and the $/hr sparkline show the current cost like the vast.ai console: the normal price
  while running, only storage while an instance is stopped (the header total now includes stopped instances'
  storage), and the reserved price only while prepaid credit is left on the instance. The tooltip shows the
  breakdown and the normal price.

### Templates
- New card view like the vast.ai console (default): a responsive grid with name, image, tags, description and
  *Select* / *Edit* / *Duplicate* / menu; the current template is marked *SELECTED*. Sort by name or date (up /
  down) on the toolbar. Toggle card / table on the toolbar; the default view for Search and Templates is in
  Settings > Appearance.

### Build
- `build.bat run` / `./build.sh run` builds and then launches the app (combinable: `debug run`, and on Windows
  `msvc run`). It stops before building if the app is already running from the project folder, since the exe
  can't be replaced while it runs.
- `build.sh` accepts several options (e.g. `debug run`) and rejects unknown ones.

## 1.2.0 - 2026-11-03

### Search
- The GPU model picker now uses the same list as the vast.ai web console (`GET /api/v0/gpu_types/?status=active`,
  157 models incl. RTX 4090D and RTX 5090D) instead of a built-in list. Names keep vast.ai's spelling but are
  grouped and sorted by family, model and variant (e.g. RTX 4090 Ti, RTX 4090, RTX 4090D together in
  "RTX 4000 Series"); groups are collapsible and open while searching, with compact checkboxes. The list is cached
  in `VastGui.json` for offline starts.

### UI
- One hover / selection highlight everywhere: dropdown rows, context menus and the GPU picker's groups and models
  use the same rounded highlight; table rows (Instances, Search table, Templates, Keys, Hosting, ...) highlight the
  full row height with rounded corners, and a click anywhere on a row selects it.
- Tables have the same cell margin on the first column as on the others.

### License
- VastGui is now licensed under the MIT License (see `LICENSE.md`), replacing the PolyForm Noncommercial
  License 1.0.0. Commercial use is now permitted. Bundled third-party components keep their own licenses.

### Build
- Windows releases are now built with the MSYS2 **UCRT64** toolchain (MinGW-w64 GCC targeting the Universal C
  Runtime) instead of the deprecated MINGW64 one. `VastGui.exe` imports the system UCRT (`api-ms-win-crt-*.dll`,
  part of Windows 10/11) instead of `msvcrt.dll`, and still needs no redistributable.
- GitHub Actions CI builds and tests every push and pull request on Windows (MinGW-w64 UCRT64 and MSVC) and Linux.
- Fixed the MSVC build (duplicate manifest resource) and the missing executable bit on `build.sh`.

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
