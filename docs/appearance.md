# Appearance and layout

RelayDock looks like part of OBS by default. You can change how it looks and how the dock is arranged. Every setting applies at once and changes RelayDock's own windows only. OBS keeps its theme.

## Appearance

Open Settings, Appearance.

| Setting | What it does |
| --- | --- |
| Theme | Follow OBS uses the colours of your OBS theme. Light and Dark are RelayDock's own. Custom lets you pick the background colour, and RelayDock picks light or dark text to match. |
| Accent colour | The colour of the main buttons and the selected page. Works with every theme. Use default goes back to the theme's own. |
| Background colour | The window background, with the Custom theme. |
| Background image | A picture behind the dock. PNG, JPEG, WebP or BMP. RelayDock reads it from where it is and does not copy it. |
| Image placement | Fill, Fit, Stretch, Tile or Centre. |
| Image opacity | How strongly the image shows. |
| Card opacity | How solid the cards are. Lower it to let a background image show through. |
| Corner radius | 0 to 16 pixels. |
| Text size | 75 to 150 percent of the OBS text size. |
| Spacing | Comfortable or Compact. Compact fits more on a small dock. |
| Animate status changes | The status label breathes while a destination connects or reconnects. That is the only animation RelayDock has. |
| Reduce motion | Switches the animation off. |

Reset appearance puts everything on this page back to the default.

### Text stays readable

Whatever colours you pick, RelayDock checks every text colour against its background and corrects it until it reaches a contrast ratio of 4.5 to 1, the WCAG AA level. Status colours (green for live, amber for connecting, red for failed) are adjusted the same way. A status never relies on colour alone. It always has a word next to it.

### Potato Mode

Potato Mode switches the animation off and slows the dock's measurements to every two seconds, whatever this page says.

## Layout

Open Settings, Layout.

The layout in use decides:

- whether the buttons sit above or below the cards,
- whether cards are Expanded with details or Compact,
- whether cards form a list or a grid that puts cards side by side on a wide dock,
- which of the Suggestions, Performance and Network sections show,
- the order of the sections.

The two toggle buttons in the dock's toolbar switch card details and the grid without opening the settings.

### Saved layouts

A saved layout remembers all of the above under a name.

| Button | What it does |
| --- | --- |
| Save as new | Saves the current arrangement under a new name and switches to it. |
| Load | Switches to the selected layout. |
| Duplicate | Copies the selected layout. |
| Rename | Gives it another name. |
| Delete | Removes it. The last layout cannot be deleted. |
| Reset | Puts the layout in use back to RelayDock's default arrangement. |

Changes you make while a layout is in use are saved into that layout.

## Where the dock sits

The dock is a normal OBS dock. Drag it by its title to any edge of the OBS window, onto another dock to make tabs, or out of the window to float. View, Docks, Lock Docks in OBS applies to it as well.

## Keyboard

- Tab moves through the dock's controls.
- On a focused card, Enter opens the editor, Alt+Up and Alt+Down move the card.
- In the vertical layout editor, arrow keys move the selected item by one pixel, and by ten with Shift.
- Icon buttons have a name for screen readers and show it as a tooltip.
