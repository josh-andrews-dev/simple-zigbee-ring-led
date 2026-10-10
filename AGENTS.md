# simple-zigbee-ring-led

Owner: Personal. Zigbee router firmware for a 16-LED WS2812B/SK6812 ring on a Seeed XIAO ESP32-C6; two devices in the house run it ("Minecraft Light", "Minecraft Lamp" in Home Assistant). Public repo.

## Standards debt (dev-manager review, 2026-10-09)

This repo predates Josh's personal project standard and was deliberately left as built: the
firmware or tool it holds is in use, and it is not worth reworking for hygiene alone (Josh,
2026-10-09). **Before making any functional change here, bring the repo up to the standard
first, in its own PR, and tell Josh that is what you are doing.** The standard is recorded in
the dev-manager repo (`~/Documents/dev-manager/standards/observed.md`). What this repo is
missing today:

- Push-to-main guard hook for both Claude Code and Antigravity.
- `Makefile` with fixed targets wrapping `arduino-cli` (`build`, `flash`, `monitor`).
- A Linux GitHub Actions workflow that compiles the sketch on every PR.
- A `vX.Y.Z` tag with release notes for each build flashed to a device (none exist; two
  devices in the house run an unknown build).
- A Notion tracker (prefix chosen by the agent) instead of none.
- This is a public repo with no LICENSE; Josh chose to leave it that way (2026-10-09).

Rules that apply now, even before that work: every change by pull request into `main`,
squash merge with a Conventional Commits title (the GitHub repo allows nothing else), no direct
pushes to `main`, and never commit secrets.
