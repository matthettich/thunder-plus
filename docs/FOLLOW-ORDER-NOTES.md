# FOLLOW above PHRASE ON STOP and ARRANGE ON STOP (build 656, owner 19 Nov)

Owner: "move the FOLLOW setting to above PHRASE ON STOP and ARRANGE ON STOP, in the View menu and any other menus that have all three."

Line numbers are `index.html` at build 647 (before the change).

| List | Where | Order before | Order now | What was done |
|---|---|---|---|---|
| Project screen rows (`projRows()`), the Screen section | :27173 FOLLOW, :27175 ARRANGE LOOK-AHEAD, :27177 TAB, :27179 RESUME, :27181 PHRASE ON STOP, :27183 ARRANGE ON STOP | FOLLOW, ARRANGE LOOK-AHEAD, TAB, RESUME, PHRASE ON STOP, ARRANGE ON STOP | ARRANGE LOOK-AHEAD, TAB, RESUME, FOLLOW, PHRASE ON STOP, ARRANGE ON STOP | FOLLOW moved down (the owner's words); the other rows keep their order. None of the three has `sec:` or `gap:`. |
| View menu (desktop) and Settings > Screen (phone and desktop): `buildScreen()` Display part, `SHEET_QUICK` | :36781, :36996-36997 | SIMPLE PHRASE, FOLLOW, COMMANDS, COMMAND BAR, then the other Screen rows in `projRows()` order (PHRASE ON STOP, ARRANGE ON STOP among them) | SIMPLE PHRASE, FOLLOW, PHRASE ON STOP, ARRANGE ON STOP, COMMANDS, COMMAND BAR, then the rest | The two ON STOP rows moved up to FOLLOW (FOLLOW stays a quick row beside SIMPLE PHRASE); the later loop skips `SHEET_QUICK` rows so none shows twice. |

## Lists with only some of the three (left alone)

- Input sheet (`SHEETS.input`, Editing group, :36767): FOLLOW, TAB only. The two ON STOP rows are not in any sheet.
- Settings > Project tab (`buildProj`): skips the Screen section entirely.
- Help (keys): only "Ctrl/Cmd+Shift+F or Scroll Lock: Follow off / on". No list of the three.
- Electron (`electron/main.js`): no native menu rows for these settings. The desktop app shows the same in-page View menu.
- No command palette lists them. FOLLOW is also a bar-row name in the FX bar (`P_('follow', 'FOLLOW', ...)`, unrelated: a Flex parameter).
- FOLLOW is missing from no menu that has the two ON STOP rows.

## Row numbers

`cs.project.y` (the Project screen cursor) is a runtime index only (not saved, not remembered across sessions), so nothing needed fixing. No test addressed these rows by number.
