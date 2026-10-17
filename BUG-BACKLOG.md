# Bug backlog

Reviewed 2026-10-16 against the 2.2.0 sources. The specification is `DEVELOPMENT.md`. A claim there that the program breaks is a bug. Later 2.0 work (tabs, row and column selection, row and column colour, format painter) and later majors (sections, page numbers, grammar, mail merge, and `.doc` / `.docx` / `.odt` / `.abw` / HTML / LaTeX) are not listed here.

The 2026-10-15 review against 2.1.0 is in Closed. Its product items were fixed in 2.1.1. The product items from this 2.2.0 review were fixed in 2.2.1. Measured on Plato, release build, with `tests/bench_key.cpp`. On 2.1.0, `plain1000` was 55.6 ms and `list1000` was 65.8 ms. On 2.1.1, 2026-10-16, `plain1000` was 58.7 ms and `list1000` was 68.8 ms. Both gate lines are under 100 ms. The 50 ms specification is still not met. The standing item in Open owns the periodic rerun. Closed records the defects shipped through 2.2.1.

Checked and left out of Open: a short page stays still on Enter (`MainWindow::scroll_to_caret`). Style boxes ellipsize at 32 characters (`narrow_name_box` in `src/styles.cpp`), so the status note that Format → Style… widens for a long name is stale. Save of an imported `.txt` or `.md` goes through Save As and leaves the original alone. Open’s missing-file and unreadable-file sentences name the file. Columns do not snake the text. The ruler draws a tick in each gap, and RTF round-trips `\cols`. Per-section columns are 3.0. Print draws the page widget. Exactly, and a multiple below one line, stay stored and round-trip, and the screen keeps the font’s line. That limit is specified. A cell paragraph tag draws indents, alignment, and spacing, and no list. Header, footer, and note pictures stay in the file until that area is typed in. Editing replaces the story with plain paragraphs. Highlight is the toolbar control. The Options size list is whole points. Half points belong to the format size box. `paragraph_style()` uses the first style when the name and Normal are both missing, and `style_sheet()` substitutes the built-in sheet when the document’s styles are empty. Spelling does walk the header, the footer, and the notes. The draft page-break gap is 18 px at every zoom, and the page count subtracts it. Cell line spacing is measured as Sans on the body context. `editor.cpp` is long, and `MainWindow` owns the document, the view, the dialogs, and pagination. Past 100% zoom, pictures and footnote type stay at their 100% size. This review also left these out. The Borders and Shading dialog’s settings, widths, colours, 3-D, and Apply to match the specification, and OK on a cell that has never had a border stores the 1/2 pt box the dialog shows. Enter, paste, undo, a style, and Paragraph… keep a border. A new table writes no cell borders. A cell’s borders round-trip, and a paragraph border inside a cell round-trips with them. A style’s borders round-trip. A box at the start of a page stays on that sheet. Insert Row still inserts below the caret’s row.

## Open

### Rerun the typing-speed bench, and optimize above 100 ms

- Severity: incorrect
- Confidence: high
- Where: `tests/bench_key.cpp` (`write-it-bench-key`). How to build and run it is `tests/README.md`.
- Trigger: Periodically, on a release build, run `xvfb-run -a ./build-release/write-it-bench-key`. Read the `plain1000` and `list1000` gate lines. Each line is the median of three runs of 50 keys at the end of paragraph 500, process CPU, refresh included. This item stays open. A run at or under 100 ms is recorded here and does not close it.
- Outcome: The specification in `DEVELOPMENT.md` stays under 50 ms. This item is the later check. When either gate line is over 100 ms, optimize the key path, rerun the bench, and record the new pair here. A pair at or under 100 ms needs no code change. Last run, 2026-10-17, release build of 2.2.2 on Plato: `plain1000` 19.2 ms, `list1000` 18.2 ms. The handlers were about 14.1 ms and 13.4 ms. Both gate lines are under 50 ms. The 2.2.1 pair on 2026-10-16 was `plain1000` 69.2 ms and `list1000` 80.4 ms, handlers 36.5 ms and 40.2 ms. The 2.1.1 pair that day was `plain1000` 58.7 ms and `list1000` 68.8 ms, handlers 31.2 ms and 34.6 ms. The 2.1.0 pair on 2026-10-15 was `plain1000` 55.6 ms and `list1000` 65.8 ms, handlers 25.4 ms and 28.3 ms. The 2.0.0 pair that day was `plain1000` 55.7 ms and `list1000` 66.6 ms, handlers 25.4 ms and 28.5 ms. The 1.0.23 pair the same day was `plain1000` 51.4 ms and `list1000` 60.5 ms, handlers 22.7 ms and 26.0 ms. Both gate lines are under 100 ms. The page is measured by paragraph. The next run at or under 100 ms is recorded and does not change the code. The 2026-10-14 run of 1.0.22 (`176f90b`) was `plain1000` 154.6 ms and `list1000` 178.4 ms, handlers 48.9 ms and 52.2 ms. The 2026-10-13 run of 1.0.16 (`f53ad21`) was `plain1000` 128.7 ms and `list1000` 145.1 ms, handlers 39.4 ms and 42.0 ms. The 1.0.8 pair in `tests/README.md` (4.35 ms and 6.93 ms on this machine, 4.63 ms and 7.31 ms on the 4-vCPU LCOS guest) still stands for that release.

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

### Typing in a long document misses the 50 ms gate

- Fixed in 2.2.2.
- Severity: incorrect
- Confidence: medium
- Where: `src/editor.cpp` `MainWindow::on_user_end`; `src/main_window.cpp` `MainWindow::apply_page_size`; `DEVELOPMENT.md` M2 typing-speed gate
- Trigger: Type in paragraph 500 of a 1000-paragraph document. The gate is under 50 ms of CPU per keystroke, median of three, on a release build, for `plain1000.rtf` and `list1000.rtf`.
- Outcome: A letter that does not move its line skips a note-free capture, reuses the page height, and does not renumber a list whose tag is already on the paragraph. Enter still paginates, and removing a tag still renumbers. A 2026-10-17 Plato release build met the gate: `plain1000` 19.2 ms and `list1000` 18.2 ms, handlers about 14.1 ms and 13.4 ms. Earlier release builds did not. A 1.0.16 build on 2026-10-13 was `plain1000` 128.7 ms and `list1000` 145.1 ms. A 1.0.22 build on 2026-10-14 was `plain1000` 154.6 ms and `list1000` 178.4 ms. A 1.0.23 build on 2026-10-15 was `plain1000` 51.4 ms and `list1000` 60.5 ms. A 2.0.0 build the same day was `plain1000` 55.7 ms and `list1000` 66.6 ms. A 2.1.0 build the same day was `plain1000` 55.6 ms and `list1000` 65.8 ms. A 2.1.1 build on 2026-10-16 was `plain1000` 58.7 ms and `list1000` 68.8 ms. A 2.2.1 build the same day was `plain1000` 69.2 ms and `list1000` 80.4 ms. The standing item in Open owns the periodic rerun. The 1.0.8 numbers are in `tests/README.md`. `DEVELOPMENT.md` had recorded about 1.4 s per keystroke on commit `de03251`.

### Insert Row and Insert Column copy one cell’s borders and shading onto every new cell

- Fixed in 2.2.1.
- Severity: incorrect
- Confidence: medium
- Where: `src/page.cpp` `insert_table_row`, `insert_table_column`, `chrome_from`.
- Trigger: Give two cells in one row different borders or shading. Table → Insert Row, or Insert Column, with the caret in one of those cells.
- Outcome: A new row gives each cell the borders and shading of the cell above it in that column. A new column gives each cell the borders and shading of the cell beside it in that row. A cell with no stored border stays that way. Insert Row still inserts below the caret’s row.

### A header, a footer, or a footnote does not draw its border or its shading

- Fixed in 2.2.1.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::tag_story_spacing`, `MainWindow::tag_note_spacing`; `src/editor.cpp` `MainWindow::paint_story_border`; `src/main_window.cpp` story draw handlers; `src/page_ui.cpp` `MainWindow::wire_note_band`.
- Trigger: Put the caret in the header, the footer, or a footnote. Format → Borders and Shading…, set a box or a fill, and press OK. The words stay as they were.
- Outcome: The box is drawn around that paragraph’s text, and the shading fills it. The story stays stored and written. Space before and space after stay outside the box.

### A body paragraph’s borders and shading are left out of the file

- Fixed in 2.2.1.
- Severity: incorrect
- Confidence: high
- Where: `src/rtf.cpp` `rtf_export`, the body paragraph after `write_spacing`. Header, footer, note, and cell paragraphs already call `write_borders` and `write_shading`.
- Trigger: Format → Borders and Shading… on a body paragraph, OK, save, and reopen.
- Outcome: The box and the shading are in the file and come back. A side that turns the style’s border off is written `\brdrnone`. A plain paragraph still omits both. The borders suite returns its failure count, so a miss fails `meson test`.

### Paragraph commands and the ruler follow the body caret while a cell or a story is edited

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::apply_paragraphs`, `MainWindow::focused_para`; `src/page_ui.cpp` `MainWindow::apply_cell_paragraphs`; `src/main_window.cpp` `MainWindow::on_ruler_draw`.
- Trigger: Click in a table cell that is not the first paragraph, or put the caret in the header, the footer, or a footnote. Format → Paragraph…, or Center, or Bullets, and look at the ruler.
- Outcome: The cell or the story takes the change. The ruler, the alignment buttons, and the list buttons show that paragraph. OK and Cancel return the keyboard there. The body paragraph at the body caret stays as it was. Bold, the font, colour, highlight, and the style combo still change the cell.

### Replace in a table cell drops the match’s character format

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::replace_once`, the branch for a view other than the body.
- Trigger: Find a formatted word inside a table cell and Replace it.
- Outcome: The new text keeps the match’s font, size, colour, strike, and script. Replace in the body still does. Replace in a header, footer, or note stays on the plain-story path.

### Line spacing of 0 comes back as single

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/document.cpp` `apply_line_spacing`; `src/rtf.cpp` `write_spacing`.
- Trigger: Format → Paragraph…, set At least, Exactly, or Multiple to 0, save, and reopen.
- Outcome: At least 0, Exactly 0, and Multiple 0 come back as those rules. Exactly 0 is written `\sl-0`, At least 0 is `\sl0\slmult0`, and Multiple 0 writes `\slmult1` before `\sl0`. Bare `\sl0` and `\sl0\slmult1` stay Single. A non-zero Single, 1.5, or Double stays that rule. A shorter-than-font Exactly or Multiple is stored, and the screen keeps the font’s line.

### Restart Numbering writes the list onto the wrong paragraph when a table sits in it

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/document.cpp` `list_retag_offsets`; `src/editor.cpp` `MainWindow::renumber_list`.
- Trigger: Two items of the same numbered list, with a table of more than one cell between them. Restart Numbering on the first item, or Continue Previous List.
- Outcome: The first item and the item after the table take the new list. The cells stay off the list. A later paragraph is left alone. A table is one anchor, so the map uses `chars_before`. A list still counts past the paragraphs between its items.

### A heading that is also a list item hides its space before and space after

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::apply_spacing_gap`, `MainWindow::raise_headings`.
- Trigger: A heading paragraph that is also bulleted or numbered, with space before or space after set.
- Outcome: The paragraph’s space before and space after are drawn, added to the heading’s own gaps, and the page count includes them. The list’s indent still wins over the paragraph margin. A list item that is not a heading still shows its spacing.

### Header, footer, and footnote spacing is not drawn

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: medium
- Where: `src/page_ui.cpp` `MainWindow::tag_story_spacing`, `MainWindow::tag_note_spacing`, `MainWindow::refresh_stories`.
- Trigger: Space before, space after, or line spacing on a paragraph in the header, the footer, or a footnote.
- Outcome: That spacing, and a heading’s gaps, are drawn. The room a footnote takes on the sheet includes it. The story stays plain text until it is typed in, so a picture there stays in the file until then. Gap tags draw the spacing and leave the stored story as it was.

### Applying a style in a cell drops formatting the file stored

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::apply_cell_style`; `src/document.cpp` `restyle_paragraph_kept`.
- Trigger: Open an RTF cell whose run or spacing differs from its style. With the caret in that cell, apply another style, then Normal.
- Outcome: A value the file stored that differs from the paragraph’s style is kept. A later style moves only what is not that direct formatting. A bold bit set from the toolbar in the cell is still kept.

### Spelling treats the letters on either side of a picture as one word

- Fixed in 2.1.1.
- Severity: data-loss
- Confidence: high
- Where: `src/spelling.cpp` `paragraph_text`, `replace_word`.
- Trigger: A paragraph has letters, then a picture, then more letters. Spelling… and Change that hit.
- Outcome: The two sides are two words when the run before the picture ends on a letter. Change replaces one side and leaves the letters on the other. The picture stays. A picture that follows a space still sits in the following word.

### Typing in a cell, a header, a footer, or a note is one undo step per key

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::on_cell_end`; `src/styles.cpp` `MainWindow::commit_document`; `src/undo.cpp` `UndoHistory::fold_last_custom`.
- Trigger: Type several letters quickly in a table cell, or in the header, the footer, or a note. Press Ctrl+Z once.
- Outcome: The run comes back. Typing in a cell, a header, a footer, or a note is one step until a pause of more than a second, a moved caret, an Undo or Redo, or a Save, as it is in the body.

### One window’s save writes the other window’s options over the ini

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/settings.cpp` `Settings::save_fields`.
- Trigger: Two documents are open. In one, change the measurement units, the dictionary, the default font or size, the recent-file count, the zoom, or a toolbar. In the other, Save, change zoom, toggle a toolbar, or close that window last.
- Outcome: The second window writes the options it changed and reloads the rest from the file. The first window’s units, dictionary, font, size, recent-file count, zoom, toolbars, and frame stay in the ini.

### A markdown image path that contains ) does not round-trip

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/document.cpp` `escape_markdown_extra`, `parse_inlines`.
- Trigger: Export a picture whose path contains `)`, such as `Screenshot (1).png`, and open that markdown.
- Outcome: The path round-trips, and so does an alt that contains `](`. A path that exists is loaded. A missing path is still kept as `![alt](path)`.

### Editing a style leaves the header, the footer, and the notes as they were

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/document.cpp` `carry_style`.
- Trigger: A header, footer, or note paragraph uses a named style. Format → Style… changes that style’s font, size, bold, indents, or spacing, then Save.
- Outcome: The header, the footer, and the notes in that style follow, and so do the styles based on it. Save writes the new values. Body paragraphs, cells included, still follow.

### A curly apostrophe splits a spelling word

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: medium
- Where: `src/spelling.cpp` `word_join`.
- Trigger: Spell a word that uses U+2019, as in `don’t`.
- Outcome: The curly apostrophe stays inside the word when a letter follows, as an ASCII apostrophe does. A hyphen still joins a word. An em dash still ends it.

### Draft’s page count ignores ordinary overflow

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::page_content_y`.
- Trigger: Draft view, with no manual page break and no footnote. Enough ordinary paragraphs that Page view has started a second sheet.
- Outcome: Draft and Page view report the same page. Ordinary overflow uses the same sheet break as Page view. A short document stays on page 1 in both. A manual page break or a footnote still agrees.

### Find Next repeats a match outside the body

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::find_next`; `src/view.hpp` `find_resume`.
- Trigger: Find, then Next or Enter in the Find field. The match is in a cell, the header, the footer, or a footnote.
- Outcome: The next Next or Enter selects the following match. The pasteboard scrolls so that match is in view. With the Find dialog focused, the search continues from the last hit.

### At least and Exactly above the font stay on the font’s line

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/view.cpp` `spacing_line_extra_px`; `src/editor.cpp` `MainWindow::apply_spacing_pixels`.
- Trigger: Format → Paragraph…, Line spacing At least or Exactly, with a point size larger than the paragraph’s font. 11 pt text and Exactly 14 pt or 16 pt is enough.
- Outcome: The line grows by the extra over the font’s line, and the page count follows. A value shorter than the font adds nothing, and the screen keeps the font’s line. Single, 1.5, double, and multiple still grow from that line.

### Deleting a footnote marker leaves the note on the page

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/document.cpp` `drop_unmarked_notes`; `src/editor.cpp` `MainWindow::compact_body_notes`, `MainWindow::compact_cell_notes`.
- Trigger: Backspace or Delete of the raised footnote marker in the body or a cell.
- Outcome: The last marker for a note removes that note from the page and from what Save writes. A marker that remains is renumbered. Undo puts the note and its marker back. A note still sits at the foot of the sheet that holds its marker, and a wrapped note keeps its number.

### A picture in a later paragraph of a cell puts the caret in an earlier one

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/page_ui.cpp` `MainWindow::insert_picture`; `src/view.hpp` `picture_caret_in_cell`.
- Trigger: Enter in a cell, so the cell has a second paragraph. Put the caret in that later paragraph and Insert → Picture….
- Outcome: The picture is stored in the later paragraph, and the caret comes back just after it there. The next character edits that paragraph. The body path still returns the caret just after the picture.

### A blank paragraph inside an unedited note draws an extra number

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/document.cpp` `story_plain_text`, `paragraph_plain`; `src/page_ui.cpp` `story_text`.
- Trigger: Open a document whose footnote has a blank paragraph, or a paragraph that is only a picture, between two text paragraphs. Leave the notes area unedited.
- Outcome: That blank paragraph, or that picture-only paragraph, is not drawn as another note. A later note keeps its number. The file is not rewritten until the notes text changes. A blank line typed in the notes area still separates notes.

### A narrow table column is drawn at 36 px

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/view.cpp` `column_display_px`; `src/page_ui.cpp` `CellFrame::set_column_px`, `TableSheet::relayout`.
- Trigger: Insert a 16-column table on the default page, or any column at the 200 twip minimum. Zoom to 50% or 75% shows the same floor.
- Outcome: A column is drawn from its stored width, and no narrower than 200 twips. Sixteen minimum columns stay inside the sheet. Zooming out shrinks a column that is under 36 px. Table Width still shows the real twips.

### Save drops a left alignment or a zero indent that overrides the style

- Fixed in 2.1.1.
- Severity: incorrect
- Confidence: high
- Where: `src/rtf.cpp` `write_plain`, and the cell writer beside it.
- Trigger: A cell, header, footer, or footnote paragraph uses a style that is not left-aligned or not zero-indented. Block Text is 1 inch on each side. Set that paragraph to left, or to a zero indent. Save, then open the file again.
- Outcome: The left alignment, the zero indent, and a cleared heading come back. The writer emits `\ql`, `\li0`, `\ri0`, and the outline level after the style word, as the body writer does. Spacing on these paths is still written.

### A selection on the first line of a later page runs up the previous sheet

- Fixed in 1.0.20.
- Severity: incorrect
- Confidence: high
- Where: `src/editor.cpp` `MainWindow::draw_page_seams`
- Trigger: Place the caret on the first line of any page after the first and select a block of text on that line.
- Outcome: The highlight stops at the top of that line. It does not run up the previous sheet, through the gap, or behind a footnote. The selected words stay highlighted. The gap between sheets stays gray.

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
