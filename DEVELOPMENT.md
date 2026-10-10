# Write-It development plan

A gtkmm-3 word processor for LCOS. The window is Word 97. The file is RTF.

Display name: **Write-It**  
Binary / repo / package: `write-it`  
APP_ID: `org.gmgauthier.WriteIt`  
License: The Unlicense (`UNLICENSE`)  
Repos: https://github.com/gmgauthier/write-it is the source of truth, where PRs, reviews, and CI run. https://gitea.scriptorium/gmgauthier/write-it and the working copy on plato are downstream mirrors: SysAdmin pushes `master`, milestone branches, and tags from GitHub to them, never the other way.

## Status

**M2 (Paragraph) is done and tagged `m2`. M3 is next.** 1.0 is M0 through M5. The packaged release is `v1.0.0` at M5: the `.deb`, the source tarball, and the AppImage. Live with that release before adding a filter. Majors after 1.0 are the roadmap at the end of this file. What each feature does is in its own section of this file; this table only says where it stands.

| M2 item | State |
|---|---|
| Paragraph indents, and inches or centimetres in Tools → Options… | Merged |
| Align left, center, and right | Merged |
| Justify | Merged |
| Draft view | Merged |
| Bulleted and numbered lists, numbering that keeps counting as in Word 97 | Merged |
| List labels on centred and right-aligned items | Merged |
| List start values (`\levelstartat`) | Merged |
| Named styles | Merged |
| Size box shows the real size | Merged |
| Size box: editable half points; empty paragraphs keep format | Merged |
| Status bar: `Page n of m` and the zoom cell | Merged |
| The caret stays in view | Merged |
| Toolbars overflow, so the window fits a 1024 px screen | Merged |
| Open files from the command line and the desktop file | Merged |
| An imported `.txt` or `.md` opens unmodified | Merged |
| Window size, maximised state, and zoom remembered | Merged |

Known issues after M2, to be fixed in a later milestone:

- Typing slows as the document grows, because every keystroke re-reads the whole document.
- A word copied from a Heading 1 and pasted into a Normal paragraph saves with `\outlinelevel0` but no `\s1`, so the paragraph is marked as a heading while its style is still Normal.
- The Format → Style… list shows style names longer than the 32 characters the Named styles item allows.

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

### How changes land

- When a design choice is in doubt, Word 97 decides. If Word 97 still leaves it open, ask Greg.
- Where this file makes a claim, the app has to keep it. A mismatch is a bug, not a design question.
- One PR per feature. The PR updates the lines of this file that describe that feature, in the section the feature belongs to. The Status section is updated separately.
- Failing tests come first, in their own commit, then the fix.
- Everything in `src/` except `main.cpp` builds once as the `writeit_core` static library, which the app and every test link. A new `src/*.cpp` file goes in that one list in `meson.build`, never in a test target's own sources.
- A test that opens a window is marked `is_parallel: false` in `meson.build`, so window tests never share the X display at the same time.
- Every suite ends with `done(name, n)`, where `n` is the exact number of checks it runs. A suite that runs more or fewer fails.
- Each milestone has its own branch, cut from `master`. Every feature PR for that milestone targets the milestone branch, not `master`. M2's branch is `m2`; from M3 on they are named `milestone-N` (`milestone-3`, `milestone-4`, …), so a branch never shares a name with its tag.
- Bug Basher reviews every PR on GitHub with a comment that starts `Bug Basher: <commit>: approved.` and names the commit. The project has one GitHub account, so a comment review is the gate. An approval covers only the commit it names and never carries over: every new head, including a clean merge, needs its own review naming that commit before it merges. He reads the code, runs its suites under ASan and UBSan, and tries whatever the PR adds in a live window for a few minutes.
- Bug Basher does not change product code. Defects go back to the PR's owner as a task with the failing case, and the owner fixes them on the same PR.
- SysAdmin merges a PR into the milestone branch once its head is approved and all four CI checks pass: the Devuan Excalibur build and tests, the Debian trixie build and tests, lint, and "ASan and UBSan (debian:trixie)", which runs every suite under the sanitisers. A PR does not have to be up to date with the milestone branch, so one merge does not send the others back for review. If a PR conflicts, its owner fixes the conflict, and Bug Basher's review of the new head looks at the conflicted files.
- A draft PR from the milestone branch into `master` stays open while the milestone is under way, so CI tests the combined branch after every merge.
- Coordinator owns the queue: the order PRs merge in and who holds each one. If nothing moves for 15 minutes, Watcher asks the room for a status, and goes to Greg only if the same PR is still stuck after a second ask.
- When every feature is in, Bug Basher does one full live pass on the milestone branch head. Defects go back as small fix PRs into the milestone branch. Then SysAdmin merges the milestone branch into `master` once, deletes the branch, and tags.
- `master` is protected: a PR merges only when it is up to date with `master` and all four CI checks pass. Milestone branches are protected too: all four checks must pass, but being up to date is not required. Nobody bypasses either rule, and force-pushes are blocked. Branches are updated by merging, not by rebasing.
- Every commit, PR description, and GitHub comment starts with its author's name, for example `Coordinator:` or `Scribe:`.
- Each finished milestone gets an annotated tag (`m2`, `m3`, …) and no GitHub release. The only release is `v1.0.0`, with the `.deb`, the AppImage, and the source tarball.

## 4. Window

One document, one window. The title is `Write-It - letter.rtf`. A dirty document adds a trailing `*`. A new document is `Write-It - Untitled`. An opened file is not dirty until it is edited, as in Word 97, whether it is RTF, Markdown, or plain text: `Write-It - notes.txt` has no `*`, and closing it asks nothing. A Markdown or text file is not RTF, so its Save is Save As, which offers the name with `.rtf` (`notes.rtf`) and leaves the original alone.

Closing a dirty document asks one question. The buttons, in order, are **Save**, **Don’t Save**, **Cancel**. Save is the default. **Close** (Ctrl+W) returns to Untitled. **Exit** (Ctrl+Q) leaves the program. The first launch is 960×700, not maximized, at Fit width. The window remembers its size, whether it is maximized, and the zoom, Fit width included. The next launch restores them, no larger than the screen it opens on. A maximized window comes back maximized and restores to the size it had before. A missing, empty, or corrupt ini, or a size in it that is not one, opens as the first launch does.

The page sits on a neutral gray pasteboard, `#808080`. Until M3’s Page Setup, the page is A4, 21 × 29.7 cm. The pasteboard scrolls up and down, and sideways when the page is wider than the window, to keep the caret in view after every caret movement and every edit (Ctrl+End, the arrows, Page Down and Page Up, typing, Find), in Page and Draft at any zoom, and again when a zoom, a view switch, or a resize moves the page. On the first line it goes right to the top, and on the last line right to the bottom. Page Down and Page Up move the caret one visible height of the pasteboard, keeping its place across the line, and Shift with them extends the selection. The mouse wheel and the scrollbars can take the view away from the caret; its next movement brings it back.

```
+------------------------------------------------------------------+
| File  Edit  View  Insert  Format  Tools  Table  Help             |
+------------------------------------------------------------------+
| [New] [Open] [Save] | [Print] | [Cut] [Copy] [Paste] | [Undo] [Redo]
| [Font ▾] [Size ▾] [B] [I] [U] | [Left] [Center] [Right] [Justify] | [Style ▾]
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

Icons come from the desktop icon theme, by freedesktop name: `document-new`, `document-open`, `document-save`, `document-print`, `edit-cut`, `edit-copy`, `edit-paste`, `edit-undo`, `edit-redo`, `format-text-bold`, `format-text-italic`, `format-text-underline`, `format-justify-left`, `format-justify-center`, `format-justify-right`, `format-justify-fill`, `format-list-unordered`, `format-list-ordered`. Toolbars are icons. The menu’s words are the tooltip. A missing icon falls back to that short word. A toolbar combo that applies a format returns focus to the document afterward.

Cut, Copy, Paste, Undo, and Redo are insensitive when there is nothing to do. Save stays sensitive. The right-click menu starts with Cut, Copy, Paste, then a separator, then this app’s own items. On a numbered list item those are Restart Numbering and Continue Previous List, each insensitive when it would change nothing.

### Menus

The menus are File, Edit, View, Insert, Format, Tools, Table, Help. Mnemonics: **F**ile, **E**dit, **V**iew, **I**nsert, F**o**rmat, **T**ools, T**a**ble, **H**elp. A menu item that opens a dialog ends with `…`. Accelerators are visible in the menu.

**File.** Open’s filter lists RTF, Markdown, and plain text. Save writes RTF. Export writes Markdown. Save As adds `.rtf`, and Export adds `.md`, to a typed name unless it already ends in one of theirs, in any case (Export also keeps `.markdown`). Another document extension (`.rtf`, `.md`, `.markdown`, `.txt`) is replaced, not kept, and any other dot is part of the name: `my.report` saves as `my.report.rtf`. The replace question is asked about the file actually written. Every way of opening a file (Open, Open Recent, the command line, the desktop’s Open and Open With) says the same sentences, naming the file: “Could not find the file “letter.rtf”.” and “Could not open the file “letter.rtf”.”. A file with no local path is refused with “Write-It can only open files on this computer:” and its URI. Files given on the command line or by the desktop (`Exec=write-it %F`) open one window each. An untouched Untitled window takes the first; a file already open, saved to or imported from, brings its window forward, under any name (a symlink, a hard link, a `..` path) and from Open and Open Recent as well; a window holding a document is never reloaded. A second launch hands its files to the running Write-It.

| Item | Keys | What it does |
|---|---|---|
| New | Ctrl+N | A blank untitled document |
| New from Template… | | Pick an RTF starter |
| Open… | Ctrl+O | Remember the last directory |
| Open Recent | | Up to eight basename items. The tooltip is the full path. A missing file uses one sentence that names it: “Could not find the file “letter.rtf”.” |
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
| Zoom | Submenu: 50%, 75%, 100%, 150%, 200%, Fit width. Fit width is the default: the page fills the visible width and follows the window as it is resized. The choice is remembered |
| Page / Draft | Radio. Page is the default |

**Insert.** Picture…, Table…, Page Break, Footnote.

**Format.** Font…, Bold (Ctrl+B), Italic (Ctrl+I), Underline (Ctrl+U), Align Left, Center, Align Right, Justify, then Style…, Bullets, Numbering, Paragraph…, Columns…. Font… is family, size, bold, italic, underline. No colour in v1.

The font list and the size list match the other two apps. The size box lists 8, 9, 10, 11, 12, 14, 16, 18, 24, 36 and is editable: a typed size applies on Enter, to the selection. Sizes are half points, so 10.5 and 8.5 are accepted and shown, and other fractions such as 10.3 are refused, as in Word 97. Spaces around a typed size are trimmed and leading zeros ignored, so “  12 ” and “00012” are 12. A refused entry shows one of these messages (“This is not a valid number.”, “The number must be between 1 and 1638.” or “Font sizes must be whole numbers or end in .5.”), and the box goes back to the text’s size, selected to type over. Sizes are clamped to 1–1638 pt. A new document starts at Sans 11.

**Tools.** Spelling… (F7). Options…: default font family, default size, recent-file count (4, 8, or 12), measurement units (inches or centimetres, default inches), and the dictionary name. The units are what the Paragraph dialog shows and accepts. The file always stores twips, so changing units never changes a document.

**Table.** Insert Table…, Insert Row, Insert Column, Delete Row, Delete Column.

**Help.** About Write-It. The dialog shows the program name, the version, one sentence, the Unlicense, and Close.

### Keyboard

The only keyboard is the Word 97 map in the menus above. WordPerfect 5.1’s function-key template stays out of v1. There is no Options switch for it. Reveal Codes stays out. F10 is the GTK menu key. Alt+F4 closes the window. F1 is Help. F5 is unused here; it starts a Show-It slide show. F7 is spelling.

### Toolbars

The standard toolbar never grows an app-specific button. Groups, left to right: New Open Save, Print, Cut Copy Paste, Undo Redo.

The format toolbar: font, size, bold, italic, underline, align left, align center, align right, justify, then a separator, then the style combo, bullets, and numbering.

View → Side by side is on by default, so the two toolbars share one row. Turning it off stacks them, with the standard toolbar above the format toolbar.

### Status bar

The left side is a message (“Saved letter.rtf”) that stays until the next message. The rightmost cell is the zoom, and it pops the same list as View. The cell to its left is the page, “Page 2 of 4”.

Until M3 lays out real pages the page count is approximate: a page is the A4 sheet’s height (16838 twips) on the screen page’s scale, less the page view’s top and bottom insets (692 px of laid-out text at 100%), `m` is how many of those the text fills, and `n` is the one the caret’s line starts on. Both scale with the zoom, so neither the zoom nor the view changes the count.

### Config

`~/.config/write-it/write-it.ini`

Keys: `window-width`, `window-height`, `window-maximized`, `recent`, `recent-count` (4, 8, or 12), `last-dir`, `default-font`, `default-size`, `show-standard-toolbar`, `show-format-toolbar`, `show-statusbar`, `toolbars-side-by-side`, `zoom` (`50` through `200`, or `fit-width`; missing or anything else is `fit-width`), `units` (`in` or `cm`; missing or anything else is `in`).

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
- Title `Write-It - Untitled`. The first launch opens at 960×700, at Fit width, and the window can be made narrow enough for a 1024 px screen. A toolbar too narrow for its controls ends in an overflow arrow. Its menu lists the controls that do not fit under the words the menus use: Bold, Italic, Underline, Bullets, and Numbering as check items, the alignments as radio items, the font, size, and style boxes as submenus of their entries, and the rest as commands. Later launches restore the user’s size, whether the window was maximized, and the zoom, from `window-width`, `window-height`, `window-maximized`, and `zoom` in the ini, no larger than the screen.
- Status message, then `Page 1 of 1`, then the zoom. View → Zoom and the zoom cell share one list, including Fit width.
- About Write-It: name, version, one sentence, the Unlicense, Close.
- Close (Ctrl+W) and Exit (Ctrl+Q). The right-click menu starts with Cut, Copy, Paste.

**Done when** the line in the table is true and the window matches [brand/window.png](brand/window.png) with an empty page.

### M1 — File

A document of paragraphs and character runs. This is the first slice that round-trips.

- Type, select, and apply font, size, bold, italic, and underline. A new document starts at Sans 11. The size box lists the usual sizes and accepts half points. No font colour.
- Undo and Redo for this slice. Find and Replace, one dialog, in the locked field order.
- New, Open, Save, and Save As write RTF for paragraphs and character format. Dirty state is a trailing `*` on the title. Closing a dirty document asks Save, Don’t Save, Cancel, with Save as the default.
- Open Recent, up to eight names, tooltip the full path, and the sentence “Could not find the file “letter.rtf”.”, naming the file Options… can set the recent-file count to 4, 8, or 12, and the default font family and size.
- Plain `.txt` imports as paragraphs.
- Markdown import and export cover headings, paragraphs, bold, and italic. Lists, tables, and image paths wait until those objects exist.

**Done when** the M1 line in the table is true. A headless test round-trips a paragraph with character format through RTF, round-trips the Markdown subset, and imports a plain-text file as paragraphs.

### M2 — Paragraph

- Indents are left, right, and first line or hanging. Format → Paragraph… sets them, the ruler marks them, and RTF round-trips them as `\li`, `\ri`, and `\fi`. The dialog speaks the measurement units from Tools → Options…, inches (the default) or centimetres; the file keeps twips. Format → Align Left, Center, Align Right, and Justify, the matching toolbar toggles (which follow the caret), and the Paragraph dialog set alignment. Left and right indents, and the By value for a first-line or hanging indent, are 0" to 22" (0 cm to 55.88 cm); a hanging indent may not be larger than the left indent, as in Word 97. The Paragraph dialog refuses a value outside that, or text that is not a number, with a message. It also refuses left and right indents, with the first line when it is indented, that leave less than 0.25" (0.64 cm) of the 6.98" (17.73 cm) text area. Known gap for M2: the RTF reader still accepts such indents from a file (`\li31680\ri31680`, say), and that paragraph is then laid out off the page.
- A list sits on the indents, half an inch per level with the label hanging a quarter inch. Format → Bullets and Format → Numbering, and the matching format-toolbar toggles, apply a list to the selected paragraphs or take it off. Tab and Shift+Tab at the start of an item change its level. Enter on an empty item and Backspace at the start of one end the list there. RTF writes a `\pntext` label for readers without lists and reads Word 6/95’s `\pn` as well.
- Indents, alignment, bulleted lists, and numbered lists. The format-toolbar bullets and numbering apply to the selection. Justify spreads every line but a paragraph’s last to both indents. RTF writes `\qc`, `\qr`, and `\qj` (left is the default and is not written). `\qd`, East Asian Word’s distributed, reads as left. Numbering follows Word 97: each numbered item belongs to a list, and a list keeps counting past plain paragraphs, bullets, and other lists in between, at every level. A numbered item restarts the levels below it in its own list. A list starts again only where a new list begins: the document’s first, a different list read from a file, or Restart Numbering, which makes the item and the rest of its list a new list. It starts at its level’s start: 1, or the number Word’s `\levelstartat` gives, and each level starts again there under a new parent. A list’s start value is 0 to 32767; negative values read as 0 and larger values as 32767. A level that starts at 0 counts 0, 1, 2, and 0 shows as 0 in any numbering style, since letters and roman numerals have no zero. A left-aligned or justified item’s text stays at the hang when its label ends at least 1 px before it; otherwise, as in Word 97, the text starts at the next default tab stop at least 1 px clear of the label, the stops falling every half inch from the left margin (paragraphs have no tab stops of their own yet). So in Sans 11 at 100% with the standard quarter-inch hang, `1.` through `9.` stay at the hang, and `10.` and wider labels move to the next stop. The document and its file keep the indents. Restart Numbering starts the new list where the old one started, and Format → Numbering starts a new list at 1. Continue Previous List joins them to the nearest numbered list above. Format → Numbering and Enter join the list above. In RTF each list is its own `\listid` and `\ls` (`\ls1` is every bullet), with each level’s `\levelstartat`, so Word and LibreOffice show the same numbers. Reading, an item counts in the `\listid` its `\ls` points at, so two overrides of one list are one list unless one has `\listoverridestartat`. An override with `\listoverridestartat` takes its `\lfolevel`’s `\levelstartat`. Word 6/95’s `\pn` numbers are one list, starting at `\pnstart`. A document holds at most 4000 numbered lists; items of later ones join the last. A one-line centred list item’s label and text are centred as one unit between the first-line indent and the right indent, as in Word 97, to within 1 pixel at any zoom, font, and label width, a label wider than its hang included. GTK centres every line of a paragraph alike, so on screen a centred item’s whole paragraph moves right by d = max(0, f + 0.25″, f + w + s), where f is the first-line indent (negative for a hang), w the label’s width, and s a space in the label’s font; its wrapped lines then sit d/2 right of Word’s, which centres them between the left and right indents. With a hang of 0.25″ or more, d = max(0, w + s − hang), 0 whenever the label and a space fit in the hang: at 100% with the standard 0.25″ hang (16 pixels) and 11-point Sans, wrapped lines move at most 1.5 pixels for labels 1. to 9., 6 for 10. to 99., and 10.5 for 100. to 999. (DejaVu Sans; less with Noto Sans), scaling with the zoom. A narrower hang moves them at least (0.25″ − hang)/2, so 0.125″ (8 pixels at 100%) with no hang at all, and a first-line indent right of the left indent moves them (f + max(0.25″, w + s))/2. The move is screen-only: RTF, undo, the dirty check, and document comparison never see it. Left-aligned, right-aligned, and justified items are not centred this way.
- Named styles. The style combo lists them and applies the chosen style. Style… edits a style. A new document has Normal, Heading 1 to 6, Block Text, and Plain Text, built on the default font and size. The format toolbar’s style combo follows the caret and applies a style to the selected paragraphs; over paragraphs in more than one style it is blank, as in Word 97. Style boxes show at most 32 characters of a name, ellipsized, so a long name (up to 255 bytes) widens neither them, their lists, nor Format → Style…; the full name is the box’s tooltip. Over 11-point Normal, Heading 1 to 6 are 16, 14, 13, 12, 12, and 12 points: bold, then bold italic and italic for Heading 5 and 6. Format → Style… changes a style’s font, size, bold, italic, underline, alignment, indents, outline level, base, and following style, or makes a new one from it. Every paragraph in that style follows, and so do the styles based on it. A list item takes its style’s right indent; its list sets the left and first-line indents. A numbered item’s label is formatted as the item’s first character, bold in a heading, as Word 97 formats it from the paragraph mark, and `\pntext` says so. A style moves only what is not direct formatting, so direct formatting stays, even where a style passes through its value: what was set with the toolbar, the Format menu, or the ruler is direct, and so is what a file gives a paragraph or run that differs from its style (a file holds only values, so that is how Word reads it too). So a value applied by hand that equals its style’s own value is not kept through RTF: on reopening it counts as the style’s, and a later change to the style moves it. Applying Heading 1 to a paragraph with a bold word and then Normal leaves the word bold, and a paragraph centred by hand stays centred when its style is made centred and then right-aligned. Enter at the end of a paragraph gives the new one the style’s following style, so a heading is followed by Normal. Each change is one undo step. A style’s size is in half points, so a 10.5 pt style is kept and round-trips as `\fs21` in the stylesheet. Headings show at their style’s size, not a fixed scale. RTF writes `\stylesheet`, `\sN`, `\sbasedon`, and `\snext`, and repeats each paragraph’s format after `\sN` as Word does, because LibreOffice reads `\pard` as flush left. A document that only uses Normal from the default sheet is written without one, as before. A file from before styles opens with its headings (M1’s `\outlinelevel` on body-size text) in Heading 1 to 6, at heading size, so they do not shrink to body text. The reader is defensive: character styles are skipped, a base that is missing or circular is dropped, duplicate names get “ (2)”, a second entry with a number already used is kept under its own name (`\sN` means the first), and the sheet stops at 4096 styles. Markdown headings import and export as Heading 1 to 6. Renaming and deleting a style are out of scope for M2: Style… adds and edits, and a style nobody uses stays in the sheet.
- View → Draft shows the same text without the sheet, its shadow, or the gray pasteboard, wrapped at the page’s text width so lines break where they print, with indents and alignment as on the page. Page stays the default and is still the view that prints. The choice is not saved: every launch opens in Page.
- The size box is editable and applies a typed size on Enter, to the document’s selection, which stays selected while the box has the keyboard. Sizes are in half points, so 10.5 and 8.5 are accepted and shown, and other fractions such as 10.3 are refused, as in Word 97. Spaces around a typed size are trimmed and leading zeros ignored (“  12 ” and “00012” are 12). A refused entry (not a number, empty, out of range, or a fraction other than .5) shows one of these messages: “This is not a valid number.”, “The number must be between 1 and 1638.” or “Font sizes must be whole numbers or end in .5.”. The box then shows the text’s size again, selected. Keys typed in the box (Delete, Backspace, Home, End, Ctrl+A, C, X, V, Z and Y) edit the box, never the document. The box is wide enough to show “1638”, and its list highlights the size shown, or nothing for a typed size it does not list. Sizes are clamped to 1–1638 pt. Empty paragraphs keep their font, size, bold, italic and underline through save and reopen, and typing on an empty line uses that format. Bold, italic and underline apply to selected empty paragraphs, as size and font do. Known defect, deferred past M2: in RTF, `\par` does not reset character formatting (only `\plain` or `\pard` do), so Word 97 shows the empty paragraph in `\fs40 Big\par\par` at 20 pt; Write-It currently gives it the default size. Write-It gives an empty paragraph its own size only when the file sets a character format inside that paragraph. Known difference from Word: with nothing selected and the caret on an empty line, changing the size affects only what is typed next, not the line itself (Word changes the line).
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
