# Output Layout

Use one folder per ruleset:

- `outputs/<ruleset>/states/` for simulation state files (`.json`)
- `outputs/<ruleset>/renders/` for rendered images (`.png`)

Current default ruleset:

- `outputs/default/states/`
- `outputs/default/renders/`

Suggested naming pattern:

- State: `state_<moves>.json`
- Render: `board_<moves>_<window>.png`