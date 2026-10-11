# Bug backlog

Reviewed 2026-10-11 against the v1.0.0 sources (`cc05f40`). The specification is `DEVELOPMENT.md`. A claim there that the program breaks is a bug. Font colour, highlight, strikethrough, superscript, sections, page numbers, grammar, mail merge, and `.doc` / `.docx` / `.odt` / `.abw` / HTML / LaTeX import are later majors. They are not listed here.

`meson test` runs the file and document suites (`document`, `rtf-unicode`, `rtf-unicode-fuzz`, `import-clean`, `page`, `lists`, `styles`, `indents`, `alignment`, `spelling`, `open`, `filename`, `settings`, `units`, `font-sizes`, `view`, `page-count`), the window suites (`page-ui`, `justify-ui`, `status-bar`, `page-width`, `page-sheet`, `list-labels`, `list-tabs`, `list-draw`, `caret-scroll`, `window-width`, `window-memory`, `style-direct`, `style-ui`, `style-size-ui`, `font-size-ui`, `paragraph-dialog`, `capture`, `capture-count`, `paste-format`, `clipboard-paste`, `dirty-state`, `fast-typing`, `toolbar-overflow`, `print`, `template`, `undo`, `undo-fuzz`, `heading-paragraphs`, `open-identity`, `window-lifetime`), `smoke-open`, `desktop-file`, and the `guard-*` checks. The three `guard-*-fails` targets are expected to fail. The package build of this tag reported 50 passed, 3 expected failures, and 0 failures. This review did not re-run the suite. It read the 1.0 sources against the specification.

Checked and left out of Open: a short page stays still on Enter (`MainWindow::scroll_to_caret`). Style boxes ellipsize at 32 characters (`narrow_name_box` in `src/styles.cpp`), so the status note that Format → Style… widens for a long name is stale. Save of an imported `.txt` or `.md` goes through Save As and leaves the original alone. Open’s missing-file and unreadable-file sentences name the file.

## Open

### Typing in a long document misses the 50 ms gate

- Severity: incorrect
- Confidence: medium
- Where: `src/editor.cpp` `MainWindow::on_user_end`; `DEVELOPMENT.md` M2 typing-speed gate
- Trigger: Type in paragraph 500 of a 1000-paragraph document. The gate is under 50 ms of CPU per keystroke, median of three, on a release build, for `plain1000.rtf` and `list1000.rtf`.
- Outcome: Each keystroke still normalises the touched paragraphs, updates the title and the actions, applies the page size, syncs the format controls, and queues list layout. `DEVELOPMENT.md` says the gate is not met, and it records about 1.4 s per keystroke on commit `de03251`. That figure was not remeasured on v1.0.0. The measurement is `tests/bench_key.cpp`, described in `tests/README.md`.

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
