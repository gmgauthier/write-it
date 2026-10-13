# Bug backlog

Reviewed 2026-10-11 against the v1.0.0 sources (`cc05f40`). The specification is `DEVELOPMENT.md`. A claim there that the program breaks is a bug. Font colour, highlight, strikethrough, superscript, sections, page numbers, grammar, mail merge, and `.doc` / `.docx` / `.odt` / `.abw` / HTML / LaTeX import are later majors. They are not listed here.

Reviewed again on 2026-10-12 against the v1.0.12 sources (`aef5e4c`). This pass did not re-run the suite. It read the sources against the specification. The typing-speed item stays open: `tests/README.md` records a 1.0.8 release build that met the gate, and this pass did not remeasure 1.0.12. `DEVELOPMENT.md` still says the gate is not met.

Measured again on 2026-10-13 against 1.0.16 (`f53ad21`), release build, with `tests/bench_key.cpp`. Both gate lines were over 100 ms. The standing item in Open owns the periodic rerun.

Checked the same day, under Xvfb, on a short document: Insert → Page Break in ordinary text wrote `\page` and left the text on the same sheet. That is fixed in 1.0.17. Insert → Page Break in a table cell is fixed in 1.0.18. A heading on a document that holds a table or a picture bounced the page, and opening a toolbar list jumped to the first page. That is fixed in 1.0.19. The typing-speed item is first in Open.

`meson test` runs the file and document suites (`document`, `rtf-unicode`, `rtf-unicode-fuzz`, `import-clean`, `page`, `lists`, `styles`, `indents`, `alignment`, `spelling`, `open`, `filename`, `settings`, `units`, `font-sizes`, `view`, `page-count`), the window suites (`page-ui`, `justify-ui`, `status-bar`, `page-width`, `page-sheet`, `list-labels`, `list-tabs`, `list-draw`, `caret-scroll`, `window-width`, `window-memory`, `style-direct`, `style-ui`, `style-size-ui`, `font-size-ui`, `paragraph-dialog`, `capture`, `capture-count`, `paste-format`, `clipboard-paste`, `dirty-state`, `fast-typing`, `toolbar-overflow`, `print`, `template`, `undo`, `undo-fuzz`, `heading-paragraphs`, `open-identity`, `window-lifetime`), `smoke-open`, `desktop-file`, and the `guard-*` checks. The three `guard-*-fails` targets are expected to fail. The package build of this tag reported 50 passed, 3 expected failures, and 0 failures. This review did not re-run the suite. It read the 1.0 sources against the specification.

Checked and left out of Open: a short page stays still on Enter (`MainWindow::scroll_to_caret`). Style boxes ellipsize at 32 characters (`narrow_name_box` in `src/styles.cpp`), so the status note that Format → Style… widens for a long name is stale. Save of an imported `.txt` or `.md` goes through Save As and leaves the original alone. Open’s missing-file and unreadable-file sentences name the file.

## Open

### Typing in a long document misses the 50 ms gate

- Severity: incorrect
- Confidence: medium
- Where: `src/editor.cpp` `MainWindow::on_user_end`; `DEVELOPMENT.md` M2 typing-speed gate
- Trigger: Type in paragraph 500 of a 1000-paragraph document. The gate is under 50 ms of CPU per keystroke, median of three, on a release build, for `plain1000.rtf` and `list1000.rtf`.
- Outcome: Each keystroke still normalises the touched paragraphs, updates the title and the actions, applies the page size, syncs the format controls, and queues list layout. `DEVELOPMENT.md` says the gate is not met, and it records about 1.4 s per keystroke on commit `de03251`. The measurement is `tests/bench_key.cpp`, described in `tests/README.md`. A 1.0.8 release build met 50 ms; those numbers are in `tests/README.md`. A 1.0.16 release build on 2026-10-13 did not: `plain1000` 128.7 ms and `list1000` 145.1 ms. The standing item below owns the periodic rerun.

### Rerun the typing-speed bench, and optimize above 100 ms

- Severity: incorrect
- Confidence: high
- Where: `tests/bench_key.cpp` (`write-it-bench-key`). How to build and run it is `tests/README.md`.
- Trigger: Periodically, on a release build, run `xvfb-run -a ./build-release/write-it-bench-key`. Read the `plain1000` and `list1000` gate lines. Each line is the median of three runs of 50 keys at the end of paragraph 500, process CPU, refresh included. This item stays open. A run at or under 100 ms is recorded here and does not close it.
- Outcome: The specification in `DEVELOPMENT.md` stays under 50 ms. This item is the later check. When either gate line is over 100 ms, optimize the key path, rerun the bench, and record the new pair here. A pair at or under 100 ms needs no code change. Last run, 2026-10-13, release build of 1.0.16 (`f53ad21`) on Plato: `plain1000` 128.7 ms, `list1000` 145.1 ms. The handlers were 39.4 ms and 42.0 ms. Both gate lines are over 100 ms, so the next pass optimizes. The 1.0.8 pair in `tests/README.md` (4.35 ms and 6.93 ms on this machine, 4.63 ms and 7.31 ms on the 4-vCPU LCOS guest) still stands for that release.

## Test defects

These fail because of the display they run on. The package build of v1.0.0 did not report them as failures. They were not re-run for this review.

### `paragraph-dialog` compares the field’s text after GTK rewrites it

- Severity: incorrect
- Confidence: medium
- Where: `tests/test_paragraph_dialog.cpp`
- Trigger: Run the suite under a real window manager.
- Outcome: The suite expects the typed text to stay exactly `22` (and the same for `99`, `30`, and `1e3`) after the warning. GTK can rewrite the entry to `22"`. The check runs once, before the field is focused again. The dialog’s own refusal of those values is the product behaviour the suite was written to lock.

### `window-memory` and `status-bar` need a screen that holds 960×700

- Severity: incorrect
- Confidence: medium
- Where: `tests/test_window_memory.cpp`, `tests/test_status_bar.cpp`
- Trigger: Run either suite on a screen whose work area is shorter or narrower than the first-launch window.
- Outcome: `window-memory` requires the first launch to measure `960x700`. `status-bar` lays out the message, the page cell, and the zoom cell in that window. A smaller screen clamps the window and those checks fail. The window itself is fitted, frame included, in 1.0.6. These checks still require a screen that holds 960×700.

## Closed

### A heading bounces the page, and a toolbar list jumps to the first page

- Fixed in 1.0.19.
- Severity: incorrect
- Confidence: high
- Where: `src/styles.cpp` `MainWindow::commit_document`, `MainWindow::retag_paragraphs`; `src/main_window.cpp` `MainWindow::hold_view`, `MainWindow::apply_page_size`
- Trigger: Scroll to a line near the bottom of a document that already holds a table or a picture, and choose a heading from the toolbar. Or place the caret near the bottom and open and close a toolbar list.
- Outcome: The page stays where it was. The heading is set. The caret is not pinned to the bottom of the window. Opening and closing a toolbar list does not jump to the first page.

### Insert → Page Break in a cell marks the wrong paragraph and jumps to the top-left cell

- Fixed in 1.0.18.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::on_page_break`, `MainWindow::caret_in_table`
- Trigger: Put the caret in a table cell and choose Insert → Page Break. The Insert menu may already have taken the keyboard. The specification puts `\page` on the paragraph after the caret, or on the caret’s own paragraph when the caret is at its start, and that paragraph is not in a table.
- Outcome: The command writes nothing and leaves the caret in that cell. The status bar says “Cannot break the page inside a table.” A page break already stored on a cell paragraph does not start a sheet. Ordinary text still does.

### Insert → Page Break in ordinary text starts the next sheet

- Fixed in 1.0.17.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::on_page_break`; `src/editor.cpp` `MainWindow::replace_buffer`, `MainWindow::update_page_seams`, `MainWindow::capture`
- Trigger: In a paragraph that is not in a table, choose Insert → Page Break. The caret may be at the end of the document, at the start of a paragraph, in the middle of a paragraph, or at the end of a paragraph that has another after it.
- Outcome: The marked paragraph starts the next sheet. Save still writes `\page`. The status counts that sheet, in Page and in Draft. At the end of the document the appended paragraph is the new sheet. Typing on it keeps the break and does not store the layout pad. Enter in that paragraph does not give the new line a break of its own.

### A footnote on a later page sits with the notes on the first page

- Fixed in 1.0.16.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::place_notes`, `MainWindow::update_page_seams`; `src/page_ui.cpp` `MainWindow::on_note_view_end`, `MainWindow::align_note_numbers`
- Trigger: Insert a footnote on a later sheet than an earlier one. The marker is on page 2 and the note is footnote 3.
- Outcome: That note sits at the foot of the sheet that holds its marker. Earlier notes stay on their own sheet. The number beside the note is that footnote's number. A blank line still splits notes. Typing in the note keeps the page on the line being typed.

### A footnote number slips off its note once the note takes more than one line

- Fixed in 1.0.16.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::align_note_numbers`, `MainWindow::note_block_px`, `MainWindow::on_notes_end`; `src/styles.cpp` `MainWindow::commit_document`; `src/main_window.cpp` `MainWindow::scroll_to_caret`
- Trigger: Insert two footnotes. In the first, type enough that the line wraps, or press Enter once so the note has two paragraphs. A blank line still splits notes, as specified. Type the note in a document that already holds a table or a picture.
- Outcome: Each number sits beside the first line of its note. A note that wraps, or that has another paragraph, keeps the following number with the next note. A blank line still splits notes. Typing in the note keeps the page on the line being typed. A table or a picture already in the document stays where it is.

### Insert → Picture in a cell lands in the first cell

- Fixed in 1.0.15.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::insert_picture`
- Trigger: Put the caret in a table cell, not at the start of the top-left cell, and choose Insert → Picture….
- Outcome: The picture is inserted at that cell’s caret. The keyboard returns to the cell, just after the picture. A table that is not at the start of the document keeps the caret and the view. Insert → Footnote’s cell caret is unchanged.

### Edit → Paste and the paste button insert into the body while a cell is edited

- Fixed in 1.0.14.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::edit_at_caret`, `MainWindow::update_actions`; `src/page_ui.cpp` `MainWindow::note_cell_marks`
- Trigger: Put the caret in a table cell and paste with Edit → Paste or the toolbar paste button. Ctrl+V in the cell is the cell’s own binding and is left alone.
- Outcome: Edit → Paste and the toolbar paste button insert into that cell. The body is unchanged. Cut, Copy, and Delete on the Edit menu and the toolbar follow a selection in the cell, including after the menu has taken the keyboard. The body’s context menu stays on the body.

### Table → Insert Row, Insert Column, Delete Row, and Delete Column change the first row or column

- Fixed in 1.0.13.
- Severity: data-loss
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::change_table_at_caret`
- Trigger: Put the caret in a cell that is not in the first row, or not in the first column, and choose one of those four. The specification adds or removes the caret’s row or column. Opening the Table menu leaves the cell; the cell that was edited stays in `cell_edit_`, which Table → Table Width… already uses.
- Outcome: Insert Row adds a row after the caret’s row. Insert Column adds a column after the caret’s column. Delete Row removes the caret’s row. Delete Column removes the caret’s column. The keyboard returns to that cell when it is still there. A table that is not at the start of the document keeps the caret and the view; they do not jump to the first line.

### A picture is a gray rectangle

- Fixed in 1.0.12.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `pixbuf_from_bytes`; `src/styles.cpp` `MainWindow::retag_paragraphs`
- Trigger: Insert → Picture… of a PNG or a JPEG, including into a document that was just opened, or open a file that already has one.
- Outcome: The picture is drawn from its full bytes, including when the document has no table and no earlier picture. A picture wider than 480 px is scaled down, and the file keeps the original bytes. The 48×24 gray rectangle is only the stand-in when those bytes are not a picture.

### Insert → Footnote drops a full-size digit and types into an off-screen note

- Fixed in 1.0.12.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::format_tag_on`, `MainWindow::place_notes`; `src/page_ui.cpp` `MainWindow::on_footnote`
- Trigger: Insert → Footnote in the body, or in a table cell, including a document that is already more than one page.
- Outcome: The marker is a raised number, in the cell when the caret is in a cell. The notes sit at the foot of the sheet that holds the first marker, with the note numbers beside the text, and the keyboard moves there. Draft hides the notes. The file still writes the marker inside `\super`.

### Text after a table is laid out inside the table

- Fixed in 1.0.11.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::sync_table_lines`
- Trigger: Open a document that has a table, from a fresh launch.
- Outcome: The line that holds the table is as tall as the table. The page seam and the text after the table start below it.

### The font face does not change inside a table cell

- Fixed in 1.0.10.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::on_font_changed`; `src/page_ui.cpp` `MainWindow::edit_cell_runs`
- Trigger: Select text in a table cell, or place the caret there, and choose a font.
- Outcome: A selection takes that face, and the direct font bit stays when a later style is applied. With only the caret, the next character typed there takes the face. Text outside the cell is left alone.

### The font size does not change inside a table cell

- Fixed in 1.0.10.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::on_size_changed`; `src/page_ui.cpp` `MainWindow::edit_cell_runs`
- Trigger: Select text in a table cell, or place the caret there, and choose or type a size.
- Outcome: A selection takes that size. With only the caret, the next character typed there takes the size. A size the box refuses leaves the cell's size and keeps the entry selected.

### A table has no command for column width or table width

- Fixed in 1.0.10.
- Severity: incorrect
- Confidence: high
- Where: `src/page.cpp` `fit_table_widths`; `src/page_ui.cpp` `MainWindow::on_table_width`
- Trigger: Put the caret in a table and choose Table → Table Width….
- Outcome: The caret's column and the whole table take the widths from the dialog, and dragging a column border resizes that column. Each column is at least 200 twips. The table may extend through the right margin to the page edge, and a table that is already wider can keep that width. OK on an untouched dialog changes nothing.

### A heading in a table changes text outside the cell

- Fixed in 1.0.9.
- Severity: incorrect
- Confidence: high
- Where: `src/styles.cpp` `MainWindow::paragraph_index`; `src/page_ui.cpp` `MainWindow::apply_cell_style`
- Trigger: Select text in a table cell and choose a heading. Or choose a heading in the paragraph after a table.
- Outcome: The heading applies to that cell, or to the paragraph after the table. A heading in a cell has the same space above and below as one in the body, and an empty cell shows that style's face and size. Bold, italic, or underline set directly in the cell stays when a later style is applied. The body paragraph is left alone.

### Bold, italic, and underline do nothing in a table cell

- Fixed in 1.0.9.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::toggle_cell_flag`
- Trigger: Select text in a cell, or place the caret there, and toggle bold, italic, or underline.
- Outcome: A selection is retagged in the cell. With only the caret, the next character typed there takes the toggle.

### A table selection stays highlighted after a click elsewhere

- Fixed in 1.0.9.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::release_other_selections`
- Trigger: Select text in a cell, then click another cell or the body.
- Outcome: The other buffer's selection collapses. It no longer stays painted until a new selection is made.

### A new page is only as tall as the lines on it

- Fixed in 1.0.9.
- Severity: incorrect
- Confidence: high
- Where: `src/main_window.cpp` `MainWindow::page_height`
- Trigger: Type enough that the document needs another page.
- Outcome: Each page is a full sheet. The gray pasteboard sits between those sheets. A one-page document stays the height of its text.

### Choosing a style jumps to the top of the first page

- Fixed in 1.0.9.
- Severity: incorrect
- Confidence: high
- Where: `src/styles.cpp` `MainWindow::apply_named_style`; `src/main_window.cpp` `hold_view`
- Trigger: Scroll down and apply a heading or another named style.
- Outcome: The pasteboard stays where it was. The caret remains in view.

### The page number changes halfway down the next sheet

- Fixed in 1.0.9.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::page_content_y`
- Trigger: Move the caret onto the first line of the next visual page, including a page that started because a tall block was pushed down.
- Outcome: That line is the new page. The number does not stay on the previous page until the caret has walked through the unused room at the bottom of the previous sheet. The gray gaps stay where they are.

### Spelling treats every non-ASCII byte as a letter

- Fixed in 1.0.8.
- Severity: incorrect
- Confidence: high
- Where: `src/spelling.cpp` `code_at`, `fold_word`
- Trigger: A word broken by an em dash, such as `hello—world`. Or Ignore All on `Éclair`, then `éclair`.
- Outcome: A word is a run of Unicode letters, so an em dash splits `hello` and `world`. An apostrophe or an ASCII hyphen still joins when a letter follows. `fold_word` folds every letter, so `Éclair` and `éclair` match. `Café` already matched `café`, because that é was already lowercase.

### Spelling skips the header, the footer, and the notes

- Fixed in 1.0.7.
- Severity: incorrect
- Confidence: high
- Where: `src/spelling.cpp` `spelling_misses`; `src/editor.cpp` `MainWindow::on_spelling`
- Trigger: Misspell a word in the header, the footer, or a footnote. Press F7.
- Outcome: The check walks the body, including table cells, then the header, the footer, and each note. A change is written back into the story the miss came from.

### The first window can be taller than the work area

- Fixed in 1.0.6.
- Severity: incorrect
- Confidence: high
- Where: `src/settings.cpp` `fit_window_frame`; `src/main_window.cpp` `MainWindow::fit_frame_to_work_area`
- Trigger: First launch, or a saved size, on a short screen. The plan’s case is 1024×576.
- Outcome: The client is still clamped to the work area first. Once the window manager reports a frame, the client shrinks so the frame stays inside that area. A frame that has not been reported is not guessed. A 960×700 window on a large screen is left as it is.

### Indents from a file can lay the paragraph off the page

- Fixed in 1.0.5.
- Severity: incorrect
- Confidence: high
- Where: `src/document.cpp` `clamp_indents`; `src/rtf.cpp` `Reader::merge`
- Trigger: Open RTF with `\li31680\ri31680`, or any left and right indents that leave less than a quarter inch of the text area. The Paragraph dialog refuses that pair.
- Outcome: After the 22-inch cap, the reader reduces a side that is too wide on its own, then the right indent, then a positive first line, until a quarter inch of that page's text area remains.

### An empty paragraph after `\par` loses the size in force

- Fixed in 1.0.4.
- Severity: incorrect
- Confidence: high
- Where: `src/rtf.cpp` `finish_paragraph`, `finish_story_paragraph`
- Trigger: Open RTF such as `\fs40 Big\par\par`. The empty paragraph has no character control word of its own.
- Outcome: `\par` keeps the character format in force, so the empty paragraph stays 20 pt, and so does a following empty paragraph. Bold carries the same way. `\pard` clears the carried format, so `\pard\par` still has none.

### Two tables with nothing between them become one

- Not a defect. No release.
- Where: `src/rtf.cpp` `begin_row`
- A `\trowd` is a row, not a table. The rows of one table are consecutive `\trowd` groups with no paragraph between them. The sample 4×4 table is written that way, and `page` requires it to stay one table. A paragraph between two `\trowd` groups is what starts the next table, which the reader already does. Splitting on every `\trowd` would turn every multi-row table into one-row tables. `DEVELOPMENT.md` states this on purpose.

### A pasted heading word marks the whole paragraph as a heading

- Fixed in 1.0.3.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::capture`, `MainWindow::finish_pending`
- Trigger: Copy a word from a Heading 1 paragraph and paste it into a Normal paragraph. Save and reopen.
- Outcome: The outline level comes from the paragraph’s style. A paste with no paragraph end keeps the landing paragraph’s style, and the copied heading tag is cleared. The paragraph stays Normal, and the file has no `\outlinelevel`.

### A markdown body row of hyphens is dropped on import

- Fixed in 1.0.2.
- Severity: data-loss
- Confidence: high
- Where: `src/document.cpp` `append_table`
- Trigger: A table cell whose text is three or more hyphens, optionally with a colon at either end (`---`, `:---`, `---:`). Export, then open that Markdown.
- Outcome: The delimiter is only the line under the header. A later row of hyphens stays a row of cells, and the rows after it stay in the same table. A header that is not itself hyphens, with a delimiter on the next line, still starts another table.

### Undo of a deleted picture drops its bytes

- Fixed in 1.0.1.
- Severity: data-loss
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::on_erase`; `src/undo.cpp` `UndoHistory::on_erase`
- Trigger: Insert a PNG or JPEG, delete it, then Undo, then Save.
- Outcome: The erase removes the picture from `image_pix_` while the pixbuf is still alive. The undo step keeps that pixbuf and its bytes. Undo inserts the pixbuf and puts the bytes back, so Save writes the picture. Redo removes it again.
