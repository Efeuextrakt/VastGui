# Configuration, API key and security

[← Back to the README](../README.md)

## API key

VastGui never hard-codes, prints or logs the key (it is masked as `****` everywhere). It looks it up in this order:

1. the `VAST_API_KEY` environment variable,
2. `api_key_enc` in `VastGui.json` (Settings -> *Account / API key* -> paste -> **Save key**).

The key in `VastGui.json` is encrypted (XChaCha20-Poly1305, [Monocypher](https://monocypher.org)) with a random
32-byte key stored in `secret.key` in the per-user config folder (`%APPDATA%\VastGui\` or `~/.config/VastGui/`,
owner-only) - also in portable mode, so `VastGui.json` alone (backups, a copy on a share) does not reveal the API
key. Copying `VastGui.json` to another PC therefore does not carry the key along; enter it again there. A plaintext
`api_key` written by older versions is encrypted automatically on the next start. On Linux `VastGui.json`,
`secret.key` and the log file are created with mode `0600`.

Use **Test key** in Settings (for a key that differs from the stored one the button reads **Save & test**).
Create keys at <https://cloud.vast.ai/manage-keys/>. Offer search works without a key.

## vast.ai API

All calls go straight to `https://console.vast.ai/api/v0` (plus `run.vast.ai` for serverless logs / worker
updates) through WinHTTP on Windows and libcurl on Linux, mirroring the requests of the official `vastai` CLI /
SDK. Details:

- The key is sent only over `https://` (plain `http://` is accepted for `localhost` only; Settings > *Connection*
  > *Server URL*) and never forwarded on a redirect to another host or from https to http; request bodies (some
  endpoints expect the key in the body) are never resent to another host either.
- Retries on 429, and on 502/503/504 for GET requests only, so a request that creates something (e.g. renting an
  instance) is never sent twice.
- **Copy files** works between remote locations (`C.<instance>:/path`, `V.<volume>:/path`,
  `<cloud>.<connection>:/path`) via `PUT /commands/copy_direct/`. Copies to or from your PC are not supported
  (the CLI runs a local `rsync` for those) - use the instance's SSH / SCP command instead.
- Settings written by older versions with the CLI backend (`backend`, `vastai_path`, `save_key_to_cli_file`,
  `console_history`, `show_command_buttons`) are ignored and dropped on the next save.

## Files created at runtime

Only configuration files - in `%APPDATA%\VastGui\` (Linux: `~/.config/VastGui/`), or next to the exe in
**portable mode** (create an empty `VastGui.json` beside `VastGui.exe`):

| File | Content |
|------|---------|
| `imgui.ini` | window placement and docking layout |
| `VastGui.json` | settings, filters, sort, presets, column choices, card/table view (Search, Templates, Instances), template and instance card sort, blacklist, current template, encrypted API key, cached GPU model list |
| `VastGui.log` | optional log file (Settings -> Connection -> *Write VastGui.log*) |
| `secret.key` | encryption key for the API key in `VastGui.json`; always in `%APPDATA%\VastGui\` / `~/.config/VastGui/`, also in portable mode |

The app starts with sane defaults when they are missing; a corrupt `VastGui.json` is backed up as
`VastGui.json.bad`.
