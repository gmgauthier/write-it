# Write-It development plan

A gtkmm-3 word processor for LCOS. The window is Word 97. The file is RTF.

Display name: **Write-It**  
Binary / repo / package: `write-it`  
APP_ID: `org.gmgauthier.WriteIt`  
License: The Unlicense (`UNLICENSE`)  
Repos: https://github.com/gmgauthier/write-it is the source of truth, where pull requests and CI run. https://gitea.scriptorium/gmgauthier/write-it and the working copy on plato are downstream mirrors. `master`, milestone branches, and tags are pushed from GitHub to them, never the other way.

## Status

**M5 (1.0) is done and tagged `v1.0.0`.** 1.0 is M0 through M5. The packaged release is `v1.0.0` at M5: the `.deb`, the source tarball, and the AppImage. Live with that release before adding a filter. Majors after 1.0 are the roadmap at the end of this file. What each feature does is in its own section of this file; this table only says where it stands. **`v2.0.0` is 2.0 M1.** Font colour, highlight, strikethrough, superscript, and subscript are in that release. **`v2.1.0` is 2.0 M2.** Space before, space after, and line spacing are in that release. **`v2.2.0` is 2.0 M3.** Borders and shading on a paragraph and on a table cell are in that release. **`v2.2.1`** fixes that release: a body paragraph's borders and shading round-trip, a header, a footer, and a footnote draw theirs, and Insert Row and Insert Column keep each cell's own. **`v2.2.2`** brings a keystroke in a 1000-paragraph document under the 50 ms gate. **`v2.3.0` is 2.0 M4.** A paragraph's own tab stops are set from Format → Tabs… and the ruler, and they round-trip.

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

- Typing slows as the document grows. In a 1000-paragraph document each keystroke takes about 1.4 s of CPU on `de03251`, so 20 keys take about 36 s. Before named styles and the list speed-up merged (`bd885b8`), the same test took about 330 ms. It does not crash; input falls further behind.
- The Format → Style… list shows style names longer than the 32 characters the Named styles item allows.

Known test defects after M2. These tests fail because of the display they run on, not because of the fault they were written to catch:

- `paragraph-dialog` fails under a real window manager. It compares the field’s exact text after GTK has rewritten it (`22` becomes `22"`), and it checks the selection once, before the field is focused again.
- `window-memory` and `status-bar` fail on a screen too small for a 960×700 window.

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
- Each milestone has its own branch, cut from `master`. Every feature PR for that milestone targets the milestone branch, not `master`. M2's branch is `m2`; from M3 on they are named `milestone-N` (`milestone-3`, `milestone-4`, …), so a branch never shares a name with its tag. That is the 1.0 shape, where each of M0–M5 was the branch. From 2.0 on, the branch is the line (`milestone-2`, `milestone-3`, …) and the milestones in section 9 are the releases on it, in that order.
- One Grok session does the work with Greg. It writes the change, runs the tests, opens the pull requests, and pushes the Gitea mirror. It merges only when Greg says to.
- The session reads the code and runs the unit tests. The desktop suite runs locally, under Xvfb or `tests/ui-docker.sh`. Greg tries the change in a live window.
- A pull request into a milestone branch merges once Greg says so and lint passes. Lint is the only required check. The Debian trixie and Devuan Excalibur jobs, and ASan and UBSan on Debian, still compile and run the unit tests on the pull request. The desktop suite stays local. A pull request does not have to be up to date with the milestone branch. If a pull request conflicts, the session fixes the conflict on that pull request.
- A draft pull request from the milestone branch into `master` stays open while the milestone is under way, so CI tests the combined branch after every merge.
- When Greg says a milestone is ready, he does a live pass on that head. Defects go back as small fix pull requests into the milestone branch. Then the branch merges into `master` and the milestone is tagged. The branch stays for the next minor, and the merge does not delete it. It is deleted when the next major is cut.
- `master` is protected: a PR merges only when it is up to date with `master` and lint passes. Lint is the only required check. The two build-and-test jobs still compile and run the unit tests on the pull request. The desktop suite is suite `ui`. Run it on a machine with a display, under Xvfb, or in a local container with `tests/ui-docker.sh`. Milestone branches matching `m[0-9]*` and `milestone-*` are protected too. Their only required check is lint. Creating the branch does not wait on lint, and a pull request into one does not have to be up to date. Nobody bypasses either rule. Force-pushes are blocked on both. `master` also blocks deletion. Branches are updated by merging, not by rebasing.
- Commits, pull request descriptions, and comments carry no role prefix. The git author is Greg Gauthier.
- 1.0's finished milestones got an annotated tag (`m2`, `m3`, …) and no GitHub release. `v1.0.0` is the 1.0 release, with the `.deb`, the AppImage, and the source tarball, and a bug fix on that line is a patch tag (`v1.0.1`, …) with the same three artifacts. From 2.0 on, the first milestone of a major is `vX.0.0` and each milestone after it is the next minor (`v2.1.0`, `v2.2.0`). A fix to a release already out is a patch (`v2.0.1`). Pushing a `v*` tag runs `.gitea/workflows/release.yml` on Gitea Actions. The job builds the three files with `scripts/release.sh` and attaches them to the Gitea release and the GitHub release. It does not run the suite again.

## 4. Window

One document, one window. The title is `Write-It - letter.rtf`. A dirty document adds a trailing `*`. A new document is `Write-It - Untitled`. An opened file is not dirty until it is edited, as in Word 97, whether it is RTF, Markdown, or plain text: `Write-It - notes.txt` has no `*`, and closing it asks nothing. A Markdown or text file is not RTF, so its Save is Save As, which offers the name with `.rtf` (`notes.rtf`) and leaves the original alone.

Closing a dirty document asks one question. The buttons, in order, are **Save**, **Don’t Save**, **Cancel**. Save is the default. **Close** (Ctrl+W) returns to Untitled. **Exit** (Ctrl+Q) leaves the program. The first launch is 960×700, not maximized, at Fit width. The window remembers its size, whether it is maximized, and the zoom, Fit width included. The next launch restores them, no larger than the work area, the window manager's frame included. A frame that has not been reported is not guessed, and the client is still kept within the work area. A maximized window comes back maximized and restores to the size it had before. A missing, empty, or corrupt ini, or a size in it that is not one, opens as the first launch does.

The page sits on a neutral gray pasteboard, `#808080`. Text that fills more than one page is drawn as separate sheets, with that gray between them. The sheets break where the status bar's page count breaks. A page break is kept and written, and the marked paragraph starts the next sheet. Draft draws a short gap there instead of a sheet, and the count is the same in either view. Draft stays one continuous column, with neither the sheet nor the gray. The default page is A4, 21 × 29.7 cm. File → Page Setup… changes the paper, the orientation, and the margins. The pasteboard scrolls up and down, and sideways when the page is wider than the window, to keep the caret in view after every caret movement and every edit (Ctrl+End, the arrows, Page Down and Page Up, typing, Find), in Page and Draft at any zoom, and again when a zoom, a view switch, or a resize moves the page. On the first line it goes right to the top. On the last line it goes right to the bottom when that line is within a screen of the sheet's foot, so the page's edge and the gray beyond it show. A short page's last line stays near the top: pressing Enter there does not jump to the empty foot of the sheet. Page Down and Page Up move the caret one visible height of the pasteboard, keeping its place across the line, and Shift with them extends the selection. The mouse wheel and the scrollbars can take the view away from the caret; its next movement brings it back.

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
| Header and Footer | Check. Off until a file has a header or a footer, which opens with it on. Not saved. Draft hides the areas and keeps the text |

**Insert.** Picture…, Table…, Page Break, Footnote.

**Format.** Font…, Bold (Ctrl+B), Italic (Ctrl+I), Underline (Ctrl+U), Align Left, Center, Align Right, Justify, then Style…, Bullets, Numbering, Paragraph…, Columns…. Font… is family, size, bold, italic, underline, colour, strikethrough, superscript, and subscript. Superscript and subscript do not apply together. The colour list is Word 97's, including Automatic.

The font list and the size list match the other two apps. The size box lists 8, 9, 10, 11, 12, 14, 16, 18, 24, 36 and is editable: a typed size applies on Enter, to the selection. The font and the size apply inside a table cell as well: a selection changes, and with only the caret the next character typed there takes that face or size. Sizes are half points, so 10.5 and 8.5 are accepted and shown, and other fractions such as 10.3 are refused, as in Word 97. Spaces around a typed size are trimmed and leading zeros ignored, so “  12 ” and “00012” are 12. A refused entry shows one of these messages (“This is not a valid number.”, “The number must be between 1 and 1638.” or “Font sizes must be whole numbers or end in .5.”), and the box goes back to the text’s size, selected to type over. Sizes are clamped to 1–1638 pt. A new document starts at Sans 11.

**Tools.** Spelling… (F7). Options…: default font family, default size, recent-file count (4, 8, or 12), measurement units (inches or centimetres, default inches), and the dictionary name. The units are what the Paragraph, Page Setup, Columns, and Table Width dialogs show and accept. The file always stores twips, so changing units never changes a document.

**Table.** Insert Table…, Insert Row, Insert Column, Delete Row, Delete Column, Table Width….

**Help.** About Write-It. The dialog shows the program name, the version, one sentence, the Unlicense, and Close.

### Keyboard

The only keyboard is the Word 97 map in the menus above. WordPerfect 5.1’s function-key template stays out of v1. There is no Options switch for it. Reveal Codes stays out. F10 is the GTK menu key. Alt+F4 closes the window. F1 is Help. F5 is unused here; it starts a Show-It slide show. F7 is spelling.

### Toolbars

The standard toolbar never grows an app-specific button. Groups, left to right: New Open Save, Print, Cut Copy Paste, Undo Redo.

The format toolbar: font, size, bold, italic, underline, font colour, highlight, align left, align center, align right, justify, then a separator, then the style combo, bullets, and numbering. Font colour and highlight sit together after underline, which is where Word 97 puts the highlight next to the font colour.

View → Side by side is on by default, so the two toolbars share one row. Turning it off stacks them, with the standard toolbar above the format toolbar.

### Status bar

The left side is a message (“Saved letter.rtf”) that stays until the next message. The rightmost cell is the zoom, and it pops the same list as View. The cell to its left is the page, “Page 2 of 4”.

The page count follows the page setup, and it is not Word’s pagination. A page is the sheet’s height less its top and bottom margins: 692 px of laid-out text at 100% on the default A4 page. `m` is how many of those the text fills, and `n` is the one the caret’s line starts on. Page view draws one sheet for each of those pages, and the gray between the sheets is not part of the count. A page break is kept and written, and the count includes the sheet it starts. Text and page both scale with the zoom, so neither the zoom nor the view changes the count, short of rounding.

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

1.0 is M0 through M5, in this order. Write-It is the first Retro-Office codebase. The next milestone starts when the current one's done line is true. Live with that release before adding a filter. M5 cuts `v1.0.0`. The majors after 1.0 are milestone plans in section 9. They do not start in this section.

Each milestone is a branch cut from `master`. M2's was `m2`. From M3 on the branch is `milestone-N` (`milestone-3`, `milestone-4`, …). A milestone that owns a file format or a document operation brings a headless offline test for that slice. The CHECK harness is the one the other guests use. Lint covers `src/` only.

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
- Title `Write-It - Untitled`. The first launch opens at 960×700, at Fit width, and the window can be made narrow enough for a 1024 px screen. A toolbar too narrow for its controls ends in an overflow arrow. Its menu lists the controls that do not fit under the words the menus use: Bold, Italic, Underline, Bullets, and Numbering as check items, the alignments as radio items, the font, size, and style boxes as submenus of their entries, and the rest as commands. Later launches restore the user’s size, whether the window was maximized, and the zoom, from `window-width`, `window-height`, `window-maximized`, and `zoom` in the ini, no larger than the work area, the window manager's frame included.
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
- Markdown import and export cover headings, paragraphs, bold, and italic. Image paths arrive with M3. Lists and pipe tables wait until M4.

**Done when** the M1 line in the table is true. A headless test round-trips a paragraph with character format through RTF, round-trips the Markdown subset, and imports a plain-text file as paragraphs.

### M2 — Paragraph

- Indents are left, right, and first line or hanging. Format → Paragraph… sets them, the ruler marks them, and RTF round-trips them as `\li`, `\ri`, and `\fi`. The dialog speaks the measurement units from Tools → Options…, inches (the default) or centimetres; the file keeps twips. Format → Align Left, Center, Align Right, and Justify, the matching toolbar toggles (which follow the caret), and the Paragraph dialog set alignment. Left and right indents, and the By value for a first-line or hanging indent, are 0" to 22" (0 cm to 55.88 cm); a hanging indent may not be larger than the left indent, as in Word 97. The Paragraph dialog refuses a value outside that, or text that is not a number, with a message. It also refuses left and right indents, with the first line when it is indented, that leave less than 0.25" (0.64 cm) of the 6.98" (17.73 cm) text area. The reader clamps a file's indents the same way: `\li31680\ri31680` keeps a quarter inch of that page's text area, with a side that is too wide on its own reduced first.
- A list sits on the indents, half an inch per level with the label hanging a quarter inch. Format → Bullets and Format → Numbering, and the matching format-toolbar toggles, apply a list to the selected paragraphs or take it off. Tab and Shift+Tab at the start of an item change its level. Enter on an empty item and Backspace at the start of one end the list there. RTF writes a `\pntext` label for readers without lists and reads Word 6/95’s `\pn` as well.
- Indents, alignment, bulleted lists, and numbered lists. The format-toolbar bullets and numbering apply to the selection. Justify spreads every line but a paragraph’s last to both indents. RTF writes `\qc`, `\qr`, and `\qj` (left is the default and is not written). `\qd`, East Asian Word’s distributed, reads as left. Numbering follows Word 97: each numbered item belongs to a list, and a list keeps counting past plain paragraphs, bullets, and other lists in between, at every level. A numbered item restarts the levels below it in its own list. A list starts again only where a new list begins: the document’s first, a different list read from a file, or Restart Numbering, which makes the item and the rest of its list a new list. It starts at its level’s start: 1, or the number Word’s `\levelstartat` gives, and each level starts again there under a new parent. A list’s start value is 0 to 32767; negative values read as 0 and larger values as 32767. A level that starts at 0 counts 0, 1, 2, and 0 shows as 0 in any numbering style, since letters and roman numerals have no zero. A left-aligned or justified item’s text stays at the hang when its label ends at least 1 px before it; otherwise, as in Word 97, the text starts at the next default tab stop at least 1 px clear of the label, the stops falling every half inch from the left margin (paragraphs have no tab stops of their own yet). So in Sans 11 at 100% with the standard quarter-inch hang, `1.` through `9.` stay at the hang, and `10.` and wider labels move to the next stop. The document and its file keep the indents. Restart Numbering starts the new list where the old one started, and Format → Numbering starts a new list at 1. Continue Previous List joins them to the nearest numbered list above. Format → Numbering and Enter join the list above. In RTF each list is its own `\listid` and `\ls` (`\ls1` is every bullet), with each level’s `\levelstartat`, so Word and LibreOffice show the same numbers. Reading, an item counts in the `\listid` its `\ls` points at, so two overrides of one list are one list unless one has `\listoverridestartat`. An override with `\listoverridestartat` takes its `\lfolevel`’s `\levelstartat`. Word 6/95’s `\pn` numbers are one list, starting at `\pnstart`. A document holds at most 4000 numbered lists; items of later ones join the last. A one-line centred list item’s label and text are centred as one unit between the first-line indent and the right indent, as in Word 97, to within 1 pixel at any zoom, font, and label width, a label wider than its hang included. GTK centres every line of a paragraph alike, so on screen a centred item’s whole paragraph moves right by d = max(0, f + 0.25″, f + w + s), where f is the first-line indent (negative for a hang), w the label’s width, and s a space in the label’s font; its wrapped lines then sit d/2 right of Word’s, which centres them between the left and right indents. With a hang of 0.25″ or more, d = max(0, w + s − hang), 0 whenever the label and a space fit in the hang: at 100% with the standard 0.25″ hang (16 pixels) and 11-point Sans, wrapped lines move at most 1.5 pixels for labels 1. to 9., 6 for 10. to 99., and 10.5 for 100. to 999. (DejaVu Sans; less with Noto Sans), scaling with the zoom. A narrower hang moves them at least (0.25″ − hang)/2, so 0.125″ (8 pixels at 100%) with no hang at all, and a first-line indent right of the left indent moves them (f + max(0.25″, w + s))/2. The move is screen-only: RTF, undo, the dirty check, and document comparison never see it. Left-aligned, right-aligned, and justified items are not centred this way.
- Named styles. The style combo lists them and applies the chosen style. Style… edits a style. A new document has Normal, Heading 1 to 6, Block Text, and Plain Text, built on the default font and size. The format toolbar’s style combo follows the caret and applies a style to the selected paragraphs; over paragraphs in more than one style it is blank, as in Word 97. Style boxes show at most 32 characters of a name, ellipsized, so a long name (up to 255 bytes) widens neither them, their lists, nor Format → Style…; the full name is the box’s tooltip. Over 11-point Normal, Heading 1 to 6 are 16, 14, 13, 12, 12, and 12 points: bold, then bold italic and italic for Heading 5 and 6. Format → Style… changes a style’s font, size, bold, italic, underline, alignment, indents, outline level, base, and following style, or makes a new one from it. Its size field takes what the size box takes, half points included (“10.5”, and “ 012 ” as 12), and refuses the rest with the box’s messages (“This is not a valid number.”, “The number must be between 1 and 1638.” or “Font sizes must be whole numbers or end in .5.”) in a modal Write-It message box, on OK, on choosing another style, or on New…: the dialog stays open on its style, the field keeps the keyboard with its text selected, and the style is unchanged. Every paragraph in that style follows, and so do the styles based on it. A list item takes its style’s right indent; its list sets the left and first-line indents. A numbered item’s label is formatted as the item’s first character, bold in a heading, as Word 97 formats it from the paragraph mark, and `\pntext` says so. A style moves only what is not direct formatting, so direct formatting stays, even where a style passes through its value: what was set with the toolbar, the Format menu, or the ruler is direct, and so is what a file gives a paragraph or run that differs from its style (a file holds only values, so that is how Word reads it too). So a value applied by hand that equals its style’s own value is not kept through RTF: on reopening it counts as the style’s, and a later change to the style moves it. Applying Heading 1 to a paragraph with a bold word and then Normal leaves the word bold, and a paragraph centred by hand stays centred when its style is made centred and then right-aligned. Enter at the end of a paragraph gives the new one the style’s following style, so a heading is followed by Normal. Each change is one undo step. A style’s size is in half points, so a 10.5 pt style is kept and round-trips as `\fs21` in the stylesheet. Headings show at their style’s size, not a fixed scale. RTF writes `\stylesheet`, `\sN`, `\sbasedon`, and `\snext`, and repeats each paragraph’s format after `\sN` as Word does, because LibreOffice reads `\pard` as flush left. A document that only uses Normal from the default sheet is written without one, as before. A file from before styles opens with its headings (M1’s `\outlinelevel` on body-size text) in Heading 1 to 6, at heading size, so they do not shrink to body text. The reader is defensive: character styles are skipped, a base that is missing or circular is dropped, duplicate names get “ (2)”, a second entry with a number already used is kept under its own name (`\sN` means the first), and the sheet stops at 4096 styles. Markdown headings import and export as Heading 1 to 6. Renaming and deleting a style are out of scope for M2: Style… adds and edits, and a style nobody uses stays in the sheet.
- View → Draft shows the same text without the sheet, its shadow, or the gray pasteboard, wrapped at the page’s text width so lines break where they print, with indents and alignment as on the page. Page stays the default and is still the view that prints. The choice is not saved: every launch opens in Page.
- The size box is editable and applies a typed size on Enter, to the document’s selection, which stays selected while the box has the keyboard. Sizes are in half points, so 10.5 and 8.5 are accepted and shown, and other fractions such as 10.3 are refused, as in Word 97. Spaces around a typed size are trimmed and leading zeros ignored (“  12 ” and “00012” are 12). A refused entry (not a number, empty, out of range, or a fraction other than .5) shows one of these messages: “This is not a valid number.”, “The number must be between 1 and 1638.” or “Font sizes must be whole numbers or end in .5.”. The box then shows the text’s size again, selected. Keys typed in the box (Delete, Backspace, Home, End, Ctrl+A, C, X, V, Z and Y) edit the box, never the document. The box is wide enough to show “1638”, and its list highlights the size shown, or nothing for a typed size it does not list. Sizes are clamped to 1–1638 pt. Empty paragraphs keep their font, size, bold, italic and underline through save and reopen, and typing on an empty line uses that format. Bold, italic and underline apply to selected empty paragraphs, as size and font do. In RTF, `\par` does not reset character formatting (`\plain` or `\pard` do), so the empty paragraph in `\fs40 Big\par\par` stays 20 pt, and a following empty paragraph does too. `\pard\par` has no character format of its own. Known difference from Word: with nothing selected and the caret on an empty line, changing the size affects only what is typed next, not the line itself (Word changes the line).
- Undo and Redo. Each command is one undo step, and so is a run of typing, of Delete, or of Backspace, until a pause of more than a second, a moved caret, an Undo or Redo, or a Save. Typing over a selection starts a run: the word typed over it is one step. Undo puts the caret back where the step began (the start of a selection it replaced); Redo moves the caret to the change and scrolls it into view. A command that changes nothing keeps Redo. Save ends the run, so after typing `ab`, saving, and typing `c`, one Ctrl+Z removes just `c`, lands on the saved state and clears the `*`.
- Any edit shows the `*`; it clears only on save, or when undo or redo lands exactly on the save point. The `*` follows the undo history, not the text (Greg's decision): even a single space shows it, and typing a letter and then Backspace leaves it although the text is as it was saved, while typing a letter and then Undo clears it. Typing or pasting over a selection with the same text is an edit too: it shows the `*` and is its own undo step. An action that inserts, deletes or re-formats nothing adds no undo step and leaves the `*` as it was, such as Backspace at the start, Delete at the end, or bold on text that's already directly bold. Ticking Bold over text that is bold only from its style (a Heading 1) is an edit: the bold becomes direct.
- Text copied and pasted within one window keeps the character format it was copied with, whatever the format at the caret: a plain `one` pasted inside a bold `two` stays plain. Text pasted from another program or another Write-It window arrives as plain text and takes the caret’s format. No character ever carries more than one character format. Paragraph format belongs to the paragraph’s end, as in Word: a pasted paragraph end brings its paragraph’s format (style, indents and list membership), so a paragraph copied whole keeps its format, and the paragraph a paste’s first end closes takes the format of the paragraph it was copied from; the text after a paste’s last end joins the paragraph it lands in and keeps that paragraph’s format. So copying from the middle of a Heading 1 through a Block Text paragraph into the middle of a Normal one gives Heading 1, Block Text, Normal. A word copied from a Heading without its paragraph end keeps the landing paragraph’s style and outline level, so a Heading word pasted into Normal stays Normal. No character ever carries more than one paragraph format.
- Typing speed. Gate: one keystroke in a 1000-paragraph document costs under 50 ms of CPU, measured with `tests/bench_key.cpp` (release build, `plain1000.rtf` and `list1000.rtf`, 50 keys into paragraph 500, median of 3). How to run it is `tests/README.md`. Met in 2.2.2: a Plato release build measured `plain1000` at 19.2 ms and `list1000` at 18.2 ms. Measured again in 2.3.0: `plain1000` at 21.8 ms and `list1000` at 22.6 ms.
- RTF for this slice round-trips. Markdown lists wait until M4.

**Done when** the M2 line in the table is true. A headless test round-trips indents, alignment, one bulleted list, one numbered list, and a named style through RTF.

### M3 — Page

- File → Page Setup… sets the paper, the orientation, and the margins, in the units from Tools → Options…. The paper list is A4 (the default, 21 × 29.7 cm), Letter, Legal, and Custom. Choosing a size while Landscape is selected shows the swapped edges. Turning the orientation swaps the width and height on screen. The file stores the edges as they will print, plus `\landscape` when that is on; a landscape flag does not swap the edges by itself. Width and height must be from 1" to 22" (2.54 cm to 55.88 cm). Margins must leave at least 0.25" (0.64 cm) of the sheet. The dialog refuses anything else with a message and stays open. OK on an untouched dialog changes nothing, because a rounded field keeps the file’s twips. The default page is left out of the file, so a document from before page setup still saves as the same bytes. A hostile file is clamped: paper from 1" to 22", margins inside the sheet with a quarter inch of text, one to twelve columns, and a gap of 0 to 2". One column writes no `\cols` and keeps the half-inch gap. Insert → Page Break puts `\page` on the paragraph after the caret, or on the caret’s own paragraph when the caret is at its start. At the end of the document it appends a paragraph in the following style. The marked paragraph starts the next sheet. Enter in that paragraph leaves the break on the part that had it, and the new paragraph stays on the same sheet. A caret in a table does not take a page break: the command leaves the document as it is, the keyboard stays in that cell, and the status bar says “Cannot break the page inside a table.” A page break already stored on a cell paragraph does not start a sheet. There is no Ctrl+Enter. Format → Columns… sets one to four columns and a gap of 0" to 2" (0 cm to 5.08 cm). A file that already has more than four columns can keep that count; the dialog does not offer a fifth on a document that has fewer. One column stores the half-inch gap. The ruler marks the current paragraph’s indents from the page’s margins, and draws a tick in each column gap when there is more than one column.
- Tables. Insert → Table… and Table → Insert Table… insert a table of 1 to 32 rows and 1 to 16 columns at the caret’s paragraph, or just after the table the caret is already in. The columns share the text width, and none is narrower than 200 twips. Table → Table Width… sets the caret’s column and the whole table. A column stays at least 200 twips. The columns together may extend through the right margin to the page edge, and a table that is already wider can keep that width. Dragging a column’s right border moves that edge: an interior border keeps the table’s width, and the last column’s border changes it. A heading in a cell has the same space above and below as a heading in the body. OK on an untouched dialog changes nothing. Table → Insert Row and Insert Column add a row or a column after the caret’s cell. Delete Row and Delete Column remove that row or column, and the last one leaves an ordinary paragraph. Those four are insensitive when the caret is not in a table. On screen the cells of a row sit side by side, each with a border, and the text wraps inside its cell. The text after a table starts below the table. A click places the caret in that cell. Tab moves to the next cell and Shift+Tab to the previous one. Tab in the last cell adds a row. Enter starts another paragraph in the same cell. A cell may hold more than one paragraph. RTF writes `\trowd`, `\cellx`, and `\cell`. Two tables with no paragraph between them are read back as one table.
- Headers, footers, and footnotes. View → Header and Footer shows the header and the footer on the page. The check is not saved. A file that has either opens with it on, until the user has toggled the item. Draft hides the areas and keeps the text. Insert → Footnote inserts a numbered marker at the caret and an empty note, up to 200 notes, and moves the keyboard to the notes. The marker is a raised number. A caret in a cell gets the marker in that cell. Each note sits at the foot of the sheet that holds its marker, in Page view, whenever the document has any, whether or not the header is showing. Typing in a header, a footer, or the notes replaces that story with plain paragraphs in the font and size at the caret, one paragraph per line. A blank line in the notes separates notes, so a blank line typed inside one note makes two. Until that area is edited, the file’s own paragraphs are kept, pictures included. Ctrl+Z and Ctrl+Y there undo and redo the document. The other Ctrl keys do not format the body while a story has the keyboard.
- Images. Insert → Picture… inserts a PNG or a JPEG, up to 8 MB. Anything else says “Write-It can insert a PNG or JPEG picture.” The stored size is the picture’s pixels at 96 dpi, and no edge is past 22". RTF writes `\pngblip` or `\jpegblip`, with `\picwgoal` and `\pichgoal` when the size is known. On screen a picture wider than 480 px is scaled down; the file keeps the original bytes. The path is not written in the RTF. Markdown image paths export and import with this slice: a path that exists is loaded, and a missing path still round-trips as `![alt](path)`. Deleting a picture and undoing that delete brings the picture and its bytes back, and Save writes them. Redo of that undo removes the picture again.

**Done when** the M3 line in the table is true. A headless test round-trips a table, a header, a footer, a footnote, an image, and a two-column section through RTF.

### M4 — Spell, print, templates

- Spelling… (F7) is a dialog against a local aspell dictionary. It checks the body, including table cells, then the header, the footer, and each note. A word is a run of Unicode letters. An em dash, or any other non-letter, ends it. An apostrophe or an ASCII hyphen stays in the word when a letter follows. Ignore All matches the same letters in any case, so Éclair and éclair are one word. Squiggles and grammar stay out of 1.0. Options… gains the dictionary name.
- Print… opens the system print dialog. Page is the view that prints.
- New from Template… picks an RTF starter shipped with the program.
- Markdown import and export gain lists and GitHub pipe tables. A body cell whose text is only hyphens, with an optional colon at either end, stays a cell. The delimiter row is only the line under the header.

**Done when** the M4 line in the table is true. A headless test covers the Markdown list and pipe-table slice, and the spelling check against a fixed word list with no network.

### M5 — 1.0

- `debian/`, a desktop file for `org.gmgauthier.WriteIt`, and `scripts/release.sh`. The desktop file is `data/write-it.desktop`: `Name=Write-It` is the AppImage’s name, and `Icon=write-it` is the hicolor icon.
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

The end zone is Word 97 as Office 97 Standard shipped it on 16 January 1997. 1.0 is not that. Each row after 1.0 is one major line. Its first milestone is tagged `vX.0.0`, and the line starts after Pay-It 1.0 has been lived with, in the rotation in the suite note. A patch release does not add the next row. VBA stays out. Save keeps writing RTF.

A major is one branch, cut from `master` when that line starts: `milestone-2`, then `milestone-3`, and so on. Its milestones are the order of feature pull requests into that branch. One milestone is one job, with a headless offline test for the slice. Word 97 decides the behaviour the table does not spell out, and the pull request writes that detail back into this file. The first milestone is that line's `.0.0` release, so 2.0 M1 is `v2.0.0` and 3.0 M1 is `v3.0.0`. Each milestone after it is the next minor, so 2.0 M2 is `v2.1.0` and 2.0 M3 is `v2.2.0`. A fix to a release that is already out is a patch, such as `v2.0.1`. Greg tags a milestone when he says it is ready. That tag is the release, with the `.deb`, the source tarball, and the AppImage. The branch stays open for the next minor.

Names below are `2.0 M1`, `3.0 M1`, and so on, so they are not 1.0's M0–M5.

| Version | Job |
|---|---|
| **1.0** | The work plan above. M0 through M5. |
| **2.0** | Format depth, and table rows and columns. Seven milestones. |
| **3.0** | A long document. Eight milestones. |
| **4.0** | Review and the writing tools. Seven milestones. |
| **5.0** | Mail merge, envelopes, and labels. Three milestones. |
| **6.0** | Drawing. Three milestones. |
| **7.0** | Open a Word 97 `.doc`. Six milestones, one for each earlier release. |

### 2.0 — Format depth

The rest of the Format menu a letter uses, and two table jobs 1.0 does not do. 1.0 already has font, size, bold, italic, underline, alignment, indents, lists, and styles. Its table commands act on the caret's one row or one column.

| Milestone | Done when |
|---|---|
| **2.0 M1 — Character** | Font colour, highlight, strikethrough, superscript, and subscript round-trip on a run. |
| **2.0 M2 — Spacing** | Space before, space after, and line spacing round-trip on a paragraph. |
| **2.0 M3 — Borders** | Borders and shading round-trip on a paragraph and on a table cell. |
| **2.0 M4 — Tabs** | The paragraph's own tab stops are set from the ruler and round-trip. |
| **2.0 M5 — Rows and columns** | Several whole rows, or several whole columns, can be selected, copied, and pasted. |
| **2.0 M6 — Row and column colour** | A selected row or column takes one background colour, and that colour round-trips. |
| **2.0 M7 — Painter** | The format painter copies the format of M1–M4, and a cell's shading from M3 and M6. |

**2.0 M1 — Character.** Format → Font… sets the font, the size, bold, italic, underline, the font colour, strikethrough, superscript, and subscript. The dialog shows the format at the start of the selection, or the format typing would use when nothing is selected. Its size box is the same half-point list as the toolbar. The format toolbar's font colour and highlight sit together after underline, which is where Word 97 puts the highlight next to the font colour. The font-colour button starts on Red and the highlight button starts on Yellow. A click uses the colour last chosen from that button, and moving the caret does not change the choice. Superscript and subscript do not apply together: turning one on turns the other off. A run stores one font colour, one highlight, strikethrough on or off, and one of superscript, subscript, or neither.

The lists are Word 97's sixteen colours, in Word's order. The font colour list starts with Automatic. The highlight list starts with None. The sixteen are Black, Blue, Turquoise, Bright Green, Pink, Red, Yellow, White, Dark Blue, Teal, Green, Violet, Dark Red, Dark Yellow, Gray-50%, and Gray-25%. Their red, green, and blue values are the ones in Word's default colour table: 0 0 0, 0 0 255, 0 255 255, 0 255 0, 255 0 255, 255 0 0, 255 255 0, 255 255 255, 0 0 128, 0 128 128, 0 128 0, 128 0 128, 128 0 0, 128 128 0, 128 128 128, and 192 192 192.

A file stores the font colour as `\cfN` into a colour table this program writes, with an empty first entry so 0 is automatic and 1 is black. Highlight is `\highlightN` with that same index, not a colour-table index. Strikethrough is `\strike` and `\strike0`. Superscript and subscript are `\super`, `\sub`, and `\nosupersub`. A colour this program does not list is read as the nearest of the sixteen. A footnote marker is still drawn raised, and the `\super` around it is not stored as superscript. A style carries the same attributes, and a value set on the run stays when the style changes.

**2.0 M2 — Spacing.** Format → Paragraph… gains space before, space after, and line spacing. Line spacing is Word 97's list: single, 1.5, double, at least, exactly, and multiple. Before and after use the units from Tools → Options…. At least and Exactly take a point size, from 0 pt to 1584 pt. Multiple takes a number of lines, from 0 to 132. The screen and the page count follow the spacing. A line cannot be drawn shorter than its font, so Exactly and a multiple below one line are stored and round-trip, and the screen keeps the font's line. RTF stores before and after as `\sb` and `\sa` in twips. Line spacing is `\sl` and `\slmult`: 240, 360, and 480 with `\slmult1` are single, 1.5, and double; another positive `\sl` with `\slmult1` is multiple; a positive `\sl` without it is at least; a negative `\sl` is exactly that many twips.

**2.0 M3 — Borders.** Format → Borders and Shading… sets a paragraph's borders and shading, and a table cell's borders and shading. The dialog has two pages. Borders offers None, Box, Shadow, 3-D, and Custom, Word 97's line styles from single through engrave, the font colours plus Automatic, and widths from 1/4 pt through 6 pt. Shading offers a fill, a pattern (Clear, Solid, and Word's percent steps), and a pattern colour. Apply to is Paragraph, or Cell when a cell is being edited. A cell's borders and shading belong to the whole cell. A paragraph's own borders stay separate, including inside a cell. A cell that has never had borders keeps the 1.0 frame and writes none. Choosing a border, including None, stores it: None writes `\brdrnone` on each edge and the frame is not drawn. A new table has no stored borders until the dialog sets one. The dialog opened on that frame shows a single 1/2 pt box in Automatic. A box is drawn around the paragraph's text. At the start of a page it stays on that sheet: the gap above the page is not part of the box.

A paragraph edge is `\brdrt`, `\brdrl`, `\brdrb`, or `\brdrr`, then the style word, `\brdrw` in twips, `\brdrcf` (0 is automatic and is omitted), `\brsp` when the space is not zero, and `\brdrsh` for a shadow. A side that is none is omitted. `\brdrbtw` and `\brdrbar` are ignored. Decorative shading patterns are not stored. Shading is `\shading` in hundredths of a percent (0 clear, 10000 solid), `\cbpat` for the fill, and `\cfpat` for the pattern colour. It is omitted when the pattern is clear and both colours are automatic. `\pard` restores the style's borders and shading. A style carries both, the way it carries spacing. A cell edge is `\clbrdrt`, `\clbrdrl`, `\clbrdrb`, or `\clbrdrr` before `\cellx`, with the same `\brdr` words. Cell shading is `\clshdng`, `\clcbpat`, and `\clcfpat`. 3-D is inset on the top and left and outset on the bottom and right, which is how the file round-trips.

**2.0 M4 — Tabs.** A paragraph can hold its own tab stops. 1.0's list labels still fall on the default half-inch stops when the paragraph has none. Format → Tabs… and the ruler set the same stops: left, center, right, decimal, and bar, with the leaders Word 97 offers (none, dots, dashes, and underline). The ruler is no longer only a stub. RTF round-trips the stops.

Format → Tabs… lists the paragraph's own stops. The position uses the units from Tools → Options…. Set replaces the stop at that position or adds one. A paragraph holds at most 50 stops. Clear removes the selected stop. Clear All removes every stop on the paragraph. A bar is a vertical line and takes no leader. The dialog also sets the document's default interval. Half an inch, 720 twips, is that default. The ruler stays 18 pixels. Its well, on the left, cycles the alignment a click will add. A click on the ruler adds a stop of that alignment, snapped to an eighth of an inch, and dragging the stop moves it. Dragging it off the ruler removes it. A click on an indent mark does not add a stop. The ruler does not draw the default grid. One drag is one undo. The dialog and the ruler edit the paragraph that has the caret, including a cell, the header, the footer, or a note, and every paragraph in the selection takes that same list.

Custom stops replace the defaults up to the last custom position, measured from the left margin. Past that position the defaults continue on the document's interval, every half inch when the file says nothing. A bar counts toward that last position and does not catch text. Tab inserts a tab character and does not split the paragraph into table cells. A file that already contains tab characters reflows onto these stops. List labels are still drawn, not stored as tab characters, and they still use the default stops when the paragraph has none.

A stop is written as its alignment word, then its leader, then `\txN` in twips from the left margin. Left has no alignment word. Center, right, decimal, and bar are `\tqc`, `\tqr`, `\tqdec`, and `\tb`. Leaders are `\tldot`, `\tlhyph`, and `\tlul`. `\tlth` is read as an underline leader. A bar writes no leader. The default interval is `\deftabN`, and 720 is left out of the file. `\pard` drops a pending alignment or leader and restores the style's stops. A paragraph that names any `\tx` replaces the style's stops entirely. A paragraph that names none uses the style's. A group restores the stops outside it. More than 50 `\tx` words keep the first 50.

RTF has no word for a paragraph with no stops of its own, and this program does not add a private one. Clearing every stop on a paragraph whose style has stops leaves the style's stops when the file is opened again. A style that clears the stops of the style it is based on cannot be written either: a stylesheet entry here does not start with `\pard`, so that style inherits its base's stops. Pango places left, center, right, and decimal. Bar and the leaders are drawn. The default stops are part of the paragraph's tab array, so Tab lands on the interval when the paragraph lists none.

**2.0 M5 — Rows and columns.** A click on the row's left edge selects that row. A click on the column's top edge selects that column. Shift extends the selection along the same edge, so the selection is whole rows or whole columns, never a mix of the two. Copy keeps each selected cell's text, character format, paragraph format, and shading. Paste of rows inserts those rows after the caret's row. Paste of columns inserts those columns after the caret's column. A paste that would pass 1.0's limits, 32 rows or 16 columns, is refused, and the status bar says so. The paste is one undo step. Pasting the same copy into ordinary text inserts the cells' text as paragraphs. RTF round-trips the table after the paste.

**2.0 M6 — Row and column colour.** With rows selected, one background colour paints every cell in those rows. With columns selected, it paints every cell in those columns. The colours are M1's list, and one more choice clears the colour. The colour is that cell's shading, the same property M3 stores, so a later shading on one cell replaces the row colour there. RTF round-trips it as cell shading.

**2.0 M7 — Painter.** The format painter copies character and paragraph format, including M1–M4, from the selection onto the text it is dragged across. Inside a table it also copies the cell's shading, whether that shading came from M3 or from a row or column colour. One click paints once. A double click keeps painting until Escape, as in Word 97. It does not invent a format 2.0 does not store.

**Done when** the seven lines are true. 2.0 M1 is `v2.0.0`. Each later milestone is tagged when Greg says it is ready.

### 3.0 — A long document

1.0 has one section, footnotes, and heading styles. Page numbers, endnotes, captions, a contents, an index, bookmarks, and hyperlinks are this release. Each milestone can use the ones before it.

| Milestone | Done when |
|---|---|
| **3.0 M1 — Sections** | A section break starts a section with its own page setup, columns, and header and footer. |
| **3.0 M2 — Page numbers** | A page number in the header or footer prints that sheet's number, and a section can restart the count. |
| **3.0 M3 — Endnotes** | An endnote marker in the body collects its note at the end of the document. |
| **3.0 M4 — Captions** | A caption is a numbered paragraph tied to a picture or a table. |
| **3.0 M5 — Contents** | A table of contents is built from the heading outline levels and the page numbers. |
| **3.0 M6 — Index** | An index is built from marked entries and the page numbers. |
| **3.0 M7 — Bookmarks** | A bookmark names a place. Insert it, and go to it. |
| **3.0 M8 — Hyperlinks** | A hyperlink jumps to a bookmark in this document, or opens a path. |

**3.0 M1 — Sections.** Insert → Break can start a new section, on the next page or continuous, as in Word 97. The section holds the page setup, the columns, and the header and footer that 1.0 stored for the whole document. A document with one section still saves as 1.0 saved it. RTF round-trips the break and the section's own setup.

**3.0 M2 — Page numbers.** Insert → Page Numbers puts the number in the header or the footer, in the place Word 97 offers. The number on the sheet is the number that prints. A section can restart the count, or keep counting from the section before. The status bar's `Page n of m` stays the count of sheets. RTF round-trips the field.

**3.0 M3 — Endnotes.** Insert → Footnote can insert an endnote. The marker sits in the body, and the note is collected after the last section, not at the foot of the sheet. Footnotes stay what 1.0 made them. Endnotes use the same limit footnotes use, 200. RTF round-trips both.

**3.0 M4 — Captions.** Insert → Caption adds a numbered paragraph under a picture or a table. The number follows the objects of that label. Deleting the object does not leave a number that still counts it. RTF round-trips the caption as that paragraph.

**3.0 M5 — Contents.** Insert → Index and Tables builds a table of contents from Heading 1 to the chosen level, with the page number from M2. Rebuilding replaces the old contents. Editing the heading, or a page break that moves it, shows up on the next rebuild. RTF round-trips the field and the result.

**3.0 M6 — Index.** The same dialog marks an index entry and builds the index from those marks, with the page numbers. A mark that is not shown in the body is still stored. Rebuilding replaces the old index. RTF round-trips the marks and the result.

**3.0 M7 — Bookmarks.** Insert → Bookmark names a selection or a caret. The name is Word 97's: a letter first, then letters, digits, and underscores. Go to that bookmark. A second bookmark of the same name replaces the first. RTF round-trips the names.

**3.0 M8 — Hyperlinks.** Insert → Hyperlink makes a run that jumps to a bookmark in this document, or opens a file path. There is no account and no fetch. The path is stored. RTF round-trips the link.

**Done when** the eight lines are true and `v3.0.0` is tagged.

### 4.0 — Review and writing tools

The checkers are local. Nothing in this release contacts a network. Spelling as you type is the squiggle 1.0 left out. The F7 dialog stays.

| Milestone | Done when |
|---|---|
| **4.0 M1 — Comments** | A comment is anchored to a selection, read and deleted from the review, and round-trips. |
| **4.0 M2 — Track changes** | An insertion and a deletion are marked, accepted, or rejected, and round-trip. |
| **4.0 M3 — AutoCorrect** | A typed replacement from the local list lands as you type, and Undo removes that replacement. |
| **4.0 M4 — AutoText** | A stored entry is inserted by its name, and it keeps the format it was stored with. |
| **4.0 M5 — Thesaurus** | The thesaurus dialog offers local synonyms for the word at the caret and replaces it on OK. |
| **4.0 M6 — Grammar** | The grammar dialog walks the document against a local check and can change the sentence it stopped on. |
| **4.0 M7 — Spelling as you type** | A word the local dictionary does not know is marked on the page, and the mark is not stored in the file. |

**4.0 M1 — Comments.** Insert → Comment anchors a note to the selection. The note is not a footnote. Review shows the comments, and deleting one removes that anchor. RTF round-trips the anchor and the text.

**4.0 M2 — Track changes.** Tools → Track Changes marks an insertion and a deletion from then on. Accept and reject take one change or the whole document. Turning tracking off leaves the marks that are already there. RTF round-trips the marks.

**4.0 M3 — AutoCorrect.** Tools → AutoCorrect… holds a local list of replacements. Typing the from-text and the separator Word 97 uses writes the to-text. One Undo removes that one replacement and leaves the typing before it. The list is the program's, not the document's.

**4.0 M4 — AutoText.** Insert → AutoText stores a selection under a name and inserts it later. The entries live with the program, in the same local store as AutoCorrect, as a separate list. An entry keeps the character and paragraph format it was stored with.

**4.0 M5 — Thesaurus.** Tools → Language → Thesaurus… looks up the word at the caret in a local list. OK replaces that word. A word the list does not know gets the dialog's own sentence, and the document is unchanged. No network.

**4.0 M6 — Grammar.** Tools → Grammar… walks the body, the cells, the header, the footer, and the notes, in the order F7 uses. The check is local, not a service. Change applies the suggestion and moves on. Ignore leaves the sentence. The file does not store the marks.

**4.0 M7 — Spelling as you type.** A word the F7 dictionary does not know is marked on screen while the option is on. The mark follows the same word rules as F7, including Ignore All. The mark is not written into the RTF, and turning the option off clears the marks. Squiggles are this milestone. They stay out of every earlier release.

**Done when** the seven lines are true and `v4.0.0` is tagged.

### 5.0 — Mail merge, envelopes, and labels

The data source is a CSV on disk. There is no database and no account. The merge result is an ordinary document, and Save still writes RTF.

| Milestone | Done when |
|---|---|
| **5.0 M1 — Mail merge** | Merge fields in a document are filled from a CSV into a new document, one copy per row. |
| **5.0 M2 — Envelopes** | An envelope page holds a return address and a delivery address, and prints as that page. |
| **5.0 M3 — Labels** | A label sheet holds one address per label, from the same address block, on a Word 97 label product or a custom grid. |

**5.0 M1 — Mail merge.** Tools → Mail Merge sets the open document as the form, a CSV as the data, and the fields as the columns. A merge field with no such column is left visible. Merging writes a new document, one copy of the form per data row, with the fields replaced. The form itself is unchanged, and it round-trips with the field names still in it. The CSV is not embedded.

**5.0 M2 — Envelopes.** Tools → Envelopes and Labels… builds one envelope. The delivery address and the return address are paragraphs on the envelope page size Word 97 uses for that choice. Printing prints that page. The envelope can take the delivery address from the merge data when a merge is set up, and it can be made with the addresses typed in.

**5.0 M3 — Labels.** The same dialog builds a sheet of labels. One address is repeated, or each label takes the next row of the merge data. The product list is Word 97's, plus a custom label defined by the page, the rows, the columns, and the gaps. The result is an ordinary document.

**Done when** the three lines are true and `v5.0.0` is tagged.

### 6.0 — Drawing

1.0 inserts a picture in the line. This release lets text wrap around that picture, and adds text boxes, lines, and autoshapes. It is not a second Show-It. There is no slide show and no animation.

| Milestone | Done when |
|---|---|
| **6.0 M1 — Wrap** | A picture can leave the line, and the body text wraps around it in the ways Word 97 offers. |
| **6.0 M2 — Text boxes** | A text box holds paragraphs, can be moved and sized, and round-trips. |
| **6.0 M3 — Shapes** | A line or an autoshape can be drawn, moved, and sized, and round-trips. |

**6.0 M1 — Wrap.** A picture still starts in the line, which is 1.0's behaviour. Format → Picture… can set the wrap Word 97 offers for a picture in a word processor: square, tight, through, top and bottom, behind the text, and in front of the text. The file keeps the original bytes, and the wrap is stored with the picture. An in-line picture still round-trips as 1.0 wrote it.

**6.0 M2 — Text boxes.** Insert → Text Box draws a box that holds paragraphs, in the same character and paragraph format as the body. The box can be moved and sized. Text in the body wraps around it by the same choices as M1. RTF round-trips the box and its text.

**6.0 M3 — Shapes.** The drawing tools add a line and the autoshapes on Word 97's drawing bar. A shape can be moved and sized, and given a line colour and a fill from the 2.0 colour list. RTF round-trips the shape. A shape does not hold a document of its own. Text stays in text boxes.

**Done when** the three lines are true and `v6.0.0` is tagged.

### 7.0 — Word 97 `.doc`

Open a Word 97 `.doc` and keep the formatting this program already understands. Save still writes RTF. The reader does not write a `.doc`. Each milestone adds the formatting of one earlier release, and a fixture from that release opens to the same document model as the RTF this program would have saved.

A `.doc` that uses a feature this program does not store is still opened. The unknown part is dropped, and the status bar says so. The parts this milestone understands are kept.

| Milestone | Done when |
|---|---|
| **7.0 M1 — The letter** | A `.doc` of the 1.0 slice opens to the same model as that document saved as RTF. |
| **7.0 M2 — Format depth** | Colour, highlight, strikethrough, superscript, subscript, spacing, borders, shading, tabs, and row and column backgrounds survive the open. |
| **7.0 M3 — Long document** | Sections, page numbers, endnotes, captions, contents, index, bookmarks, and hyperlinks survive the open. |
| **7.0 M4 — Review** | Comments and tracked changes survive the open. AutoCorrect, AutoText, the thesaurus, and the grammar marks are not in the file. |
| **7.0 M5 — Merge fields** | Merge fields survive the open. The CSV stays outside the file. |
| **7.0 M6 — Drawing** | Picture wrap, text boxes, lines, and autoshapes survive the open. |

**7.0 M1 — The letter.** Paragraphs, character format, lists, styles, tables, the header, the footer, footnotes, pictures, columns, and page setup. The picture bytes come across. A headless test opens a fixed `.doc` and compares the model.

**7.0 M2 — Format depth.** The 2.0 run and paragraph format, and a row or column background stored as cell shading, on top of M1. A document that uses only 1.0 formatting still opens as M1 opened it. Copied rows are ordinary cells by the time the file is saved, so the open has nothing separate to keep for the copy itself.

**7.0 M3 — Long document.** The 3.0 structures, on top of M2. A contents or an index keeps its field and the result that was stored in the file.

**7.0 M4 — Review.** Comments and tracked insertions and deletions, on top of M3. Accept and reject work on marks that arrived this way.

**7.0 M5 — Merge fields.** The form's merge fields, on top of M4. Merging still reads the CSV from disk. An envelope or a label sheet that is ordinary paragraphs and page setup already opened under M1.

**7.0 M6 — Drawing.** Wrap, text boxes, lines, and autoshapes, on top of M5. An in-line picture still opens as M1 opened it.

**Done when** the six lines are true and `v7.0.0` is tagged. That tag is the end zone. Save still writes RTF.
