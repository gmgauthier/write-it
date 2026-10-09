# Write-It development plan

A gtkmm-3 word processor for LCOS. The window is Word 97. The file is RTF.

Display name: **Write-It**  
Binary / repo / package: `write-it`  
APP_ID: `org.gmgauthier.WriteIt`  
License: The Unlicense (`UNLICENSE`)  
Repos: https://gitea.scriptorium/gmgauthier/write-it (origin), https://github.com/gmgauthier/write-it

## Status (2026-10-09)

**M1, with M2 under way.** Typing, font, size, bold, italic, underline, undo, find and replace, and RTF open and save are in this tree, on the M0 window. Recent files, plain-text import, and Markdown import and export cover headings, paragraphs, bold, and italic. From M2, paragraph indents (left, right, and first line or hanging) are in: Format → Paragraph… sets them, the ruler marks them, and RTF round-trips them as `\li`, `\ri`, and `\fi`. The dialog speaks the measurement units from Tools → Options…, inches (the default) or centimetres. The file keeps twips. Alignment is in too: Format → Align Left, Center, and Align Right, the matching toolbar toggles (which follow the caret), and the Paragraph dialog set it, and RTF writes `\qc` and `\qr` (left is the default and is not written). There is no justified, so `\qj` reads as left. View → Draft is in: the same text without the sheet, its shadow, or the gray pasteboard, wrapped at the page’s text width so lines break where they print, with indents and alignment as on the page. Page stays the default and the view that prints, and the choice is not saved: every launch opens in Page. Bulleted and numbered lists are in too: Format → Bullets and Format → Numbering, and the matching format-toolbar toggles, apply a list to the selected paragraphs or take it off. A list sits on the indents, half an inch per level with the label hanging a quarter inch. Tab and Shift+Tab at the start of an item change its level, Enter on an empty item and Backspace at the start of one end the list there. Numbers restart after a plain paragraph. RTF round-trips lists as `\listtable`, `\listoverridetable`, `\ls`, and `\ilvl`, with a `\pntext` label for readers without lists, and reads Word 6/95’s `\pn` as well. 1.0 is M0 through M5. The packaged release is `v1.0.0` at M5: the `.deb`, the source tarball, and the AppImage. Live with that release before adding a filter. Majors after 1.0 are the roadmap at the end of this file. The suite copy is [../RETRO-OFFICE.md](../RETRO-OFFICE.md).

The 960×700 first-launch mockup is [brand/window.png](brand/window.png). The sample document in that picture is `letter.rtf`.

## 1. Locked decisions

| Decision | Choice |
|---|---|
| Product | Original. The window is Word 97. AbiWord is the feature floor to study |
| Name | Write-It. Binary `write-it`. A bare `write` binary collides with the Unix command |
| Toolkit | C++17, gtkmm-3.0, GTK3 CSS, Meson (`warning_level=2`) |
| Look | One decorated window. Clearlooks-Phenix draws the controls. The window manager draws the title bar |
| File | RTF, a published 1.9 subset covering the feature floor. A template is an RTF starter |
| Fallback | Markdown export and import: CommonMark plus GitHub pipe tables. Save still writes RTF |
| Spell | F7 opens a dialog against a local aspell dictionary |
| Init | No systemd. Config `~/.config/write-it/write-it.ini` |
| License | The Unlicense |
| Versioning | Semantic (`MAJOR.MINOR.PATCH`). When source exists, `meson.build` is the source of truth. The Debian changelog and git tag `vX.Y.Z` match it |

## 2. Place in the suite

Retro-Office is three applications with one window language: Write-It, Count-It (`count-it`), and Show-It (`show-it`). Each is its own repository. The umbrella note lives in the local `lcos-projects` folder as `RETRO-OFFICE.md` and is not part of this repository. This file is the Write-It specification.

LCOS already ships AbiWord and Gnumeric. The gap this suite fills is coherence: a letter, a sheet, and a short deck that feel like one product. Show-It is the presentation peer the ISO does not have. Study AbiWord. Do not fork it, and do not link libabiword as the document. AbiWord is GPL. The house license is the Unlicense. Its weight sits in import filters.

Write-It is the first codebase. Count-It and Show-It wait until this 1.0 has been lived with.

Organized notes stay in the Ephemeris Notepad. Plain text stays with Lunduke-Notepad. Pictures stay with Lunduke Paint and Ristretto. Mail stays with Dispatch. The calculator stays galculator.

## 3. House rules

- Devuan Excalibur / LCOS, XLibre, XFCE, Clearlooks-Phenix
- No systemd, no PackageKit, no custom title bar, no daemon, no online account, no AI
- Local files only. AbiWord’s TCP / XMPP collaboration stays out
- Borrow the LCOS palette. Do not use Bryan’s seal
- Ship `.deb`, source tarball, and AppImage at M5
- Tests headless and offline. Lint covers `src/` only

## 4. Window

One document, one window. The title is `Write-It - letter.rtf`. A dirty document adds a trailing `*`. A new document is `Write-It - Untitled`. An opened file is not dirty until it is edited, as in Word 97, whether it is RTF, Markdown, or plain text: `Write-It - notes.txt` has no `*`, and closing it asks nothing. A Markdown or text file is not RTF, so its Save is Save As, which offers the name with `.rtf` (`notes.rtf`) and leaves the original alone.

Closing a dirty document asks one question. The buttons, in order, are **Save**, **Don’t Save**, **Cancel**. Save is the default. **Close** (Ctrl+W) returns to Untitled. **Exit** (Ctrl+Q) leaves the program. The first launch is 960×700, not maximized. The window remembers its size.

The page sits on a neutral gray pasteboard, `#808080`.

```
+------------------------------------------------------------------+
| File  Edit  View  Insert  Format  Tools  Table  Help             |
+------------------------------------------------------------------+
| [New] [Open] [Save] | [Print] | [Cut] [Copy] [Paste] | [Undo] [Redo]
| [Font ▾] [Size ▾] [B] [I] [U] | [Left] [Center] [Right] | [Style ▾]
+------------------------------------------------------------------+
| ruler                                                            |
|                                                                  |
|   page                                                           |
|   paragraphs, a list, a table, a picture                         |
|                                                                  |
+------------------------------------------------------------------+
| Saved letter.rtf                         Page 1 of 1       100%  |
+------------------------------------------------------------------+
```

Icons come from the desktop icon theme, by freedesktop name: `document-new`, `document-open`, `document-save`, `document-print`, `edit-cut`, `edit-copy`, `edit-paste`, `edit-undo`, `edit-redo`, `format-text-bold`, `format-text-italic`, `format-text-underline`, `format-justify-left`, `format-justify-center`, `format-justify-right`, `format-list-unordered`, `format-list-ordered`. Toolbars are icons. The menu’s words are the tooltip. A missing icon falls back to that short word. A toolbar combo that applies a format returns focus to the document afterward.

Cut, Copy, Paste, Undo, and Redo are insensitive when there is nothing to do. Save stays sensitive. The right-click menu starts with Cut, Copy, Paste, then a separator, then this app’s own items.

### Menus

The menus are File, Edit, View, Insert, Format, Tools, Table, Help. Mnemonics: **F**ile, **E**dit, **V**iew, **I**nsert, F**o**rmat, **T**ools, T**a**ble, **H**elp. A menu item that opens a dialog ends with `…`. Accelerators are visible in the menu.

**File.** Open’s filter lists RTF, Markdown, and plain text. Save writes RTF. Export writes Markdown. Save As adds `.rtf`, and Export adds `.md`, to a typed name unless it already ends in one of theirs, in any case (Export also keeps `.markdown`). Another document extension (`.rtf`, `.md`, `.markdown`, `.txt`) is replaced, not kept, and any other dot is part of the name: `my.report` saves as `my.report.rtf`. The replace question is asked about the file actually written.

| Item | Keys | What it does |
|---|---|---|
| New | Ctrl+N | A blank untitled document |
| New from Template… | | Pick an RTF starter |
| Open… | Ctrl+O | Remember the last directory |
| Open Recent | | Up to eight basename items. The tooltip is the full path. A missing file uses one sentence: “That file is missing.” |
| Save | Ctrl+S | Write the whole RTF file |
| Save As… | | |
| Export… | | Markdown |
| Print… | Ctrl+P | The system print dialog |
| Page Setup… | | Paper, orientation, margins |
| Close | Ctrl+W | Back to Untitled |
| Exit | Ctrl+Q | |

**Edit.**

| Item | Keys |
|---|---|
| Undo | Ctrl+Z |
| Redo | Ctrl+Y, and Ctrl+Shift+Z |
| Cut | Ctrl+X |
| Copy | Ctrl+C |
| Paste | Ctrl+V |
| Delete | Delete. Removes the selection |
| Select All | Ctrl+A |
| Find… | Ctrl+F |
| Replace… | Ctrl+H |

Find and Replace are one modal dialog. Fields, in order: Find, Replace, a Match case check, Next, Replace, Close.

**View.**

| Item | Behaviour |
|---|---|
| Standard Toolbar | Check. On by default |
| Format Toolbar | Check. On by default |
| Side by side | Check. On by default. The two toolbars share one row. Off stacks them, standard above format |
| Status Bar | Check. On by default |
| Zoom | Submenu: 50%, 75%, 100%, 150%, 200%, Fit width |
| Page / Draft | Radio. Page is the default |

**Insert.** Picture…, Table…, Page Break, Footnote.

**Format.** Font…, Bold (Ctrl+B), Italic (Ctrl+I), Underline (Ctrl+U), Align Left, Center, Align Right, then Style…, Bullets, Numbering, Paragraph…, Columns…. Font… is family, size, bold, italic, underline. No colour in v1.

The font list and the size list match the other two apps. Sizes are 8, 9, 10, 11, 12, 14, 16, 18, 24, 36. A new document starts at Sans 11.

**Tools.** Spelling… (F7). Options…: default font family, default size, recent-file count (4, 8, or 12), measurement units (inches or centimetres, default inches), and the dictionary name. The units are what the Paragraph dialog shows and accepts. The file always stores twips, so changing units never changes a document.

**Table.** Insert Table…, Insert Row, Insert Column, Delete Row, Delete Column.

**Help.** About Write-It. The dialog shows the program name, the version, one sentence, the Unlicense, and Close.

### Keyboard

The only keyboard is the Word 97 map in the menus above. WordPerfect 5.1’s function-key template stays out of v1. There is no Options switch for it. Reveal Codes stays out. F10 is the GTK menu key. Alt+F4 closes the window. F1 is Help. F5 is unused here; it starts a Show-It slide show. F7 is spelling.

### Toolbars

The standard toolbar never grows an app-specific button. Groups, left to right: New Open Save, Print, Cut Copy Paste, Undo Redo.

The format toolbar: font, size, bold, italic, underline, align left, align center, align right, then a separator, then the style combo, bullets, and numbering.

View → Side by side is on by default, so the two toolbars share one row. Turning it off stacks them, with the standard toolbar above the format toolbar.

### Status bar

The left side is a message (“Saved letter.rtf”) that stays until the next message. The rightmost cell is the zoom, and it pops the same list as View. The cell to its left is the page, “Page 2 of 4”.

### Config

`~/.config/write-it/write-it.ini`

Keys: `window-width`, `window-height`, `recent`, `recent-count` (4, 8, or 12), `last-dir`, `default-font`, `default-size`, `show-standard-toolbar`, `show-format-toolbar`, `show-statusbar`, `toolbars-side-by-side`, `zoom`, `units` (`in` or `cm`; missing or anything else is `in`).

## 5. Feature floor

AbiWord’s useful surface:

- Character format: font, size, bold, italic, underline
- Paragraph indents, alignment, bulleted and numbered lists
- Named styles
- Tables
- Headers, footers, footnotes
- Images
- Columns and page setup
- Two views of the same document: **page**, and a simpler **draft** with the pagination chrome hidden
- Spell check
- Templates

Page is the view that prints. Draft is the second view. Outline, online layout, and a presentation view stay out. Show-It is the deck.

## 6. Format

The file Write-It saves is **RTF**. One text file of control words, from the published 1.9 specification, covering the feature floor: fonts, paragraphs, lists, named styles, tables, images, headers, footers, footnotes, and columns. A template is an RTF starter, not an `.awt`.

The v1 contract is the RTF Write-It writes, plus straightforward RTF that uses those same control words. A document made of the feature floor opens in Write-It and opens in Word, LibreOffice, and AbiWord. WordPad still shows the words and the bold. Tables, headers, and styles need one of those three.

Word can save RTF that also contains fields, OLE objects, and drawings. Write-It keeps the text and the formatting it understands. `.doc`, `.docx`, `.odt`, `.abw`, HTML, and LaTeX wait. Promising every RTF in the wild is how Write-It stops being lightweight.

**Why this is RTF, and a `.docx` waits.** `.xlsx` and `.pptx` are Office 2007 packages because the period files for those two jobs, `.xls` and `.ppt`, are OLE binaries. The word processor already has a period file that is one text document. Word, LibreOffice, AbiWord, and the WordPad of that desktop open RTF. A `.docx` with this same floor is a zip of `document.xml`, `styles.xml`, `numbering.xml`, headers, footers, footnotes, and a media folder. `.docx` can be a later export, beside `.odt`.

**Markdown is the fallback, and it is lossy.** CommonMark, plus GitHub-style pipe tables. Open and export:

- Headings, paragraphs, bold, italic, lists, simple tables
- Images as paths

Fonts, sizes, underline, alignment, indents, named styles beyond headings, headers, footers, footnotes, columns, and page setup are left out on purpose. Save still writes RTF. Exporting Markdown is a separate command, so the page furniture is not quietly discarded. There is no markdown source view. Pandoc is not the converter. Plain `.txt` still imports as paragraphs.

## 7. Work plan

1.0 is M0 through M5, in this order. Write-It is the first Retro-Office codebase. The next milestone starts when the current one's done line is true. Live with that release before adding a filter. M5 cuts `v1.0.0`.

Each milestone is a branch `feature/mN-short-name` from `master`. A milestone that owns a file format or a document operation brings a headless offline test for that slice. The CHECK harness is the one the other guests use. Lint covers `src/` only.

The sections above are the specification. This section is the order of work. The sample letter in [brand/window.png](brand/window.png) is the chrome target at M0; its paragraphs, list, and table arrive with the milestones that own them.

| Milestone | Done when |
|---|---|
| **M0 — Window** | Menus, both toolbars on one row (View can stack them), ruler stub, empty page, About, status `Page n of m`. Matches the sketch. |
| **M1 — File** | New / Open / Save RTF for paragraphs and character format. Recent files. Plain-text import. Markdown import and export for headings, paragraphs, bold, and italic. |
| **M2 — Paragraph** | Indents, alignment, lists, named styles. Draft view. |
| **M3 — Page** | Tables, headers, footers, footnotes, images, columns, page setup. |
| **M4 — Spell, print, templates** | Spell dialog (aspell). System print. An RTF starter file. Markdown lists and pipe tables, once those objects exist. |
| **M5 — 1.0** | `debian/`, `scripts/release.sh` → `.deb`, tarball, AppImage. Tag `v1.0.0` and publish it. |

### M0 — Window

The Meson tree, the gtkmm window, and `scripts/lint.sh`. No document on disk.

- Menus in order: File, Edit, View, Insert, Format, Tools, Table, Help, with the mnemonics from the window section. Items are visible. Commands that need a document are insensitive. Save stays sensitive.
- The standard toolbar and the format toolbar share one row. View → Side by side is on by default, and turning it off stacks them. The format toolbar runs through alignment, then the style combo, bullets, and numbering. App-specific controls are visible and wait for their milestone.
- Ruler stub. Empty white page on the `#808080` pasteboard. Page view is the selected radio. Draft is present and waits for M2.
- Title `Write-It - Untitled`. First launch asks for 960×700. With the toolbars on one row, the window opens wide enough to show every control on that row. The ini remembers `window-width` and `window-height`.
- Status message, then `Page 1 of 1`, then the zoom. View → Zoom and the zoom cell share one list, including Fit width.
- About Write-It: name, version, one sentence, the Unlicense, Close.
- Close (Ctrl+W) and Exit (Ctrl+Q). The right-click menu starts with Cut, Copy, Paste.

**Done when** the line in the table is true and the window matches [brand/window.png](brand/window.png) with an empty page.

### M1 — File

A document of paragraphs and character runs. This is the first slice that round-trips.

- Type, select, and apply font, size, bold, italic, and underline. A new document starts at Sans 11. Sizes are the locked list. No font colour.
- Undo and Redo for this slice. Find and Replace, one dialog, in the locked field order.
- New, Open, Save, and Save As write RTF for paragraphs and character format. Dirty state is a trailing `*` on the title. Closing a dirty document asks Save, Don’t Save, Cancel, with Save as the default.
- Open Recent, up to eight names, tooltip the full path, and the sentence “That file is missing.” Options… can set the recent-file count to 4, 8, or 12, and the default font family and size.
- Plain `.txt` imports as paragraphs.
- Markdown import and export cover headings, paragraphs, bold, and italic. Lists, tables, and image paths wait until those objects exist.

**Done when** the M1 line in the table is true. A headless test round-trips a paragraph with character format through RTF, round-trips the Markdown subset, and imports a plain-text file as paragraphs.

### M2 — Paragraph

- Indents, alignment, bulleted lists, and numbered lists. The format-toolbar bullets and numbering apply to the selection.
- Named styles. The style combo lists them and applies the chosen style. Style… edits a style.
- Draft view hides the pagination chrome. Page stays the default and is still the view that prints.
- RTF for this slice round-trips. Markdown lists wait until M4.

**Done when** the M2 line in the table is true. A headless test round-trips indents, alignment, one bulleted list, one numbered list, and a named style through RTF.

### M3 — Page

- Tables, and the Table menu: insert a table, insert and delete rows and columns.
- Headers, footers, and footnotes.
- Images. Insert → Picture…. Markdown image paths export and import with this slice.
- Columns and page setup: paper, orientation, margins.
- The ruler reflects the page setup.

**Done when** the M3 line in the table is true. A headless test round-trips a table, a header, a footer, a footnote, an image, and a two-column section through RTF.

### M4 — Spell, print, templates

- Spelling… (F7) is a dialog against a local aspell dictionary. Squiggles and grammar stay out of 1.0. Options… gains the dictionary name.
- Print… opens the system print dialog. Page is the view that prints.
- New from Template… picks an RTF starter shipped with the program.
- Markdown import and export gain lists and GitHub pipe tables.

**Done when** the M4 line in the table is true. A headless test covers the Markdown list and pipe-table slice, and the spelling check against a fixed word list with no network.

### M5 — 1.0

- `debian/`, a desktop file for `org.gmgauthier.WriteIt`, and `scripts/release.sh`.
- The script produces the source tarball, the amd64 `.deb`, and the AppImage. The desktop `Name=` is the AppImage’s name.
- Tag `v1.0.0` after `meson test` and lint are green. Publish the tag and the three artifacts to Gitea and GitHub.

**Done when** `v1.0.0` is tagged and the three artifacts are on both remotes. Live with that release before adding a filter. `.doc`, `.docx`, `.odt`, `.abw`, HTML, and LaTeX stay out of 1.0.

## 8. Traps

- Forking AbiWord, or linking libabiword as the document
- `.doc`, `.docx`, `.odt`, `.abw`, or LaTeX as a v1 deliverable
- Markdown as the file Save writes, or a source view of the markup
- Grammar checking or mail merge in 1.0. Both are later majors. Collaboration that needs an account stays out.
- A presentation view, or an outline that is really Show-It. Drawing is a later major, and it is not a second Show-It.
- WebKit as the page
- A WordPerfect 5.1 function-key map, or Reveal Codes
- A shared framework that has to exist before Write-It can save a file
- Turning the Ephemeris Notepad into a second Write-It

## 9. Roadmap

The end zone is Word 97 as Office 97 Standard shipped it on 16 January 1997. 1.0 is not that. Each row after 1.0 is one major release, tagged `vX.0.0`, and it starts after Pay-It 1.0 has been lived with, in the rotation in the suite note. A patch release does not add the next row. VBA stays out. Save keeps writing RTF.

| Version | Job |
|---|---|
| **1.0** | The work plan above. |
| **2.0** | Font colour, highlight, strikethrough, superscript and subscript, paragraph spacing, borders and shading, tabs on a real ruler, and the format painter. |
| **3.0** | Sections, page numbers, endnotes, captions, a table of contents, an index, bookmarks, and hyperlinks. |
| **4.0** | Comments, track changes, AutoCorrect, AutoText, a thesaurus, a grammar dialog, and spelling as you type. The checkers are local. |
| **5.0** | Mail merge, envelopes, and labels. The data source is a CSV. |
| **6.0** | Text boxes, lines, autoshapes, and wrap around a picture. |
| **7.0** | Open a Word 97 `.doc` and keep the formatting this program understands. |
