# Write-It development plan

A gtkmm-3 word processor for LCOS. The window is Word 97. The file is RTF.

Display name: **Write-It**  
Binary / repo / package: `write-it`  
APP_ID: `org.gmgauthier.WriteIt`  
License: The Unlicense (`UNLICENSE`)  
Repos: https://gitea.scriptorium/gmgauthier/write-it (origin), https://github.com/gmgauthier/write-it

## Status (2026-10-07)

**Specification.** This repository holds the plan. Source begins at M0. v1 is M0 through M5. Live with that set before adding a filter. Tag `v0.1.0` at M5.

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

Write-It is the first codebase. Count-It and Show-It wait until this v1 has been lived with.

Organized notes stay in the Ephemeris Notepad. Plain text stays with Lunduke-Notepad. Pictures stay with Lunduke Paint and Ristretto. Mail stays with Dispatch. The calculator stays galculator.

## 3. House rules

- Devuan Excalibur / LCOS, XLibre, XFCE, Clearlooks-Phenix
- No systemd, no PackageKit, no custom title bar, no daemon, no online account, no AI
- Local files only. AbiWord’s TCP / XMPP collaboration stays out
- Borrow the LCOS palette. Do not use Bryan’s seal
- Ship `.deb`, source tarball, and AppImage at M5
- Tests headless and offline. Lint covers `src/` only

## 4. Window

One document, one window. The title is `Write-It - letter.rtf`. A dirty document adds a trailing `*`. A new document is `Write-It - Untitled`.

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

Icons come from the desktop icon theme, by freedesktop name: `document-new`, `document-open`, `document-save`, `document-print`, `edit-cut`, `edit-copy`, `edit-paste`, `edit-undo`, `edit-redo`, `format-text-bold`, `format-text-italic`, `format-text-underline`, `format-justify-left`, `format-justify-center`, `format-justify-right`. Toolbars are icons. The menu’s words are the tooltip. A missing icon falls back to that short word. A toolbar combo that applies a format returns focus to the document afterward.

Cut, Copy, Paste, Undo, and Redo are insensitive when there is nothing to do. Save stays sensitive. The right-click menu starts with Cut, Copy, Paste, then a separator, then this app’s own items.

### Menus

The menus are File, Edit, View, Insert, Format, Tools, Table, Help. Mnemonics: **F**ile, **E**dit, **V**iew, **I**nsert, F**o**rmat, **T**ools, T**a**ble, **H**elp. A menu item that opens a dialog ends with `…`. Accelerators are visible in the menu.

**File.** Open’s filter lists RTF, Markdown, and plain text. Save writes RTF. Export writes Markdown.

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
| Status Bar | Check. On by default |
| Zoom | Submenu: 50%, 75%, 100%, 150%, 200%, Fit width |
| Page / Draft | Radio. Page is the default |

**Insert.** Picture…, Table…, Page Break, Footnote.

**Format.** Font…, Bold (Ctrl+B), Italic (Ctrl+I), Underline (Ctrl+U), Align Left, Center, Align Right, then Style…, Bullets, Numbering, Paragraph…, Columns…. Font… is family, size, bold, italic, underline. No colour in v1.

The font list and the size list match the other two apps. Sizes are 8, 9, 10, 11, 12, 14, 16, 18, 24, 36. A new document starts at Sans 11.

**Tools.** Spelling… (F7). Options…: default font family, default size, recent-file count (4, 8, or 12), and the dictionary name.

**Table.** Insert Table…, Insert Row, Insert Column, Delete Row, Delete Column.

**Help.** About Write-It. The dialog shows the program name, the version, one sentence, the Unlicense, and Close.

### Keyboard

The only keyboard is the Word 97 map in the menus above. WordPerfect 5.1’s function-key template stays out of v1. There is no Options switch for it. Reveal Codes stays out. F10 is the GTK menu key. Alt+F4 closes the window. F1 is Help. F5 is unused here; it starts a Show-It slide show. F7 is spelling.

### Toolbars

The standard toolbar never grows an app-specific button. Groups, left to right: New Open Save, Print, Cut Copy Paste, Undo Redo.

The format toolbar: font, size, bold, italic, underline, align left, align center, align right, then a separator, then the style combo, bullets, and numbering.

### Status bar

The left side is a message (“Saved letter.rtf”) that stays until the next message. The rightmost cell is the zoom, and it pops the same list as View. The cell to its left is the page, “Page 2 of 4”.

### Config

`~/.config/write-it/write-it.ini`

Keys: `window-width`, `window-height`, `recent`, `last-dir`, `default-font`, `default-size`, `show-standard-toolbar`, `show-format-toolbar`, `show-statusbar`, `zoom`.

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

| Milestone | Done when |
|---|---|
| **M0 — Window** | Menus, both toolbars, ruler stub, empty page, About, status `Page n of m`. Matches the sketch. |
| **M1 — File** | New / Open / Save RTF for paragraphs and character format. Recent files. Plain-text import. Markdown import and export for headings, paragraphs, bold, and italic. |
| **M2 — Paragraph** | Indents, alignment, lists, named styles. Draft view. |
| **M3 — Page** | Tables, headers, footers, footnotes, images, columns, page setup. |
| **M4 — Spell, print, templates** | Spell dialog (aspell). System print. An RTF starter file. Markdown lists and pipe tables, once those objects exist. |
| **M5 — Package** | `debian/`, `scripts/release.sh` → `.deb`, tarball, AppImage. Tag `v0.1.0`. |

## 8. Traps

- Forking AbiWord, or linking libabiword as the document
- `.doc`, `.docx`, `.odt`, `.abw`, or LaTeX as a v1 deliverable
- Markdown as the file Save writes, or a source view of the markup
- Collaboration, grammar checking, mail merge
- A presentation view, a drawing canvas, or an outline that is really Show-It
- WebKit as the page
- A WordPerfect 5.1 function-key map, or Reveal Codes
- A shared framework that has to exist before Write-It can save a file
- Turning the Ephemeris Notepad into a second Write-It
