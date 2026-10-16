/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

constexpr int kChecks = 23;

writeit::Paragraph text_paragraph(const std::string& text)
{
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.text = text;
  paragraph.runs.push_back(run);
  return paragraph;
}

writeit::Paragraph note_marker(int note, const std::string& text)
{
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.note = note;
  run.text = text;
  paragraph.runs.push_back(run);
  return paragraph;
}

int main()
{
  CHECK(writeit::paragraph_plain(writeit::Paragraph{}) == " ");
  CHECK(writeit::story_plain_text({}) == "");
  CHECK(writeit::story_plain_text({writeit::Paragraph{}}) == " ");

  writeit::Paragraph spaced = text_paragraph("  ");
  CHECK(writeit::paragraph_plain(spaced) == "  ");

  writeit::Paragraph picture;
  writeit::Run image;
  image.image = writeit::Image{};
  picture.runs.push_back(image);
  CHECK(writeit::paragraph_plain(picture) == " ");
  writeit::Run beside;
  beside.text = "sat";
  picture.runs.push_back(beside);
  CHECK(writeit::paragraph_plain(picture) == "sat");

  const std::string joined = writeit::story_plain_text(
      {text_paragraph("Before"), writeit::Paragraph{}, text_paragraph("After")});
  CHECK(joined == "Before\n \nAfter");
  CHECK(writeit::note_break_offsets(joined).size() == 1);
  CHECK(writeit::note_break_offsets("Before\n\nAfter").size() == 2);

  writeit::Document only_second = writeit::blank_document("Sans", 11);
  only_second.paragraphs = {note_marker(2, "2")};
  only_second.notes = {{text_paragraph("gone")}, {text_paragraph("kept")}};
  const writeit::NoteDrop dropped = writeit::drop_unmarked_notes(only_second);
  CHECK(dropped.changed);
  CHECK(dropped.notes.size() == 1);
  CHECK(dropped.remap.size() == 3);
  CHECK(dropped.remap[1] == 0);
  CHECK(dropped.remap[2] == 1);
  CHECK(dropped.notes[0][0].runs[0].text == "kept");
  writeit::apply_note_remap(only_second, dropped.remap);
  CHECK(only_second.paragraphs[0].runs[0].note == 1);
  CHECK(only_second.paragraphs[0].runs[0].text == "1");

  writeit::Document both = writeit::blank_document("Sans", 11);
  both.paragraphs = {note_marker(1, "1"), note_marker(2, "2")};
  both.notes = {{text_paragraph("one")}, {text_paragraph("two")}};
  const writeit::NoteDrop kept = writeit::drop_unmarked_notes(both);
  CHECK(!kept.changed);
  CHECK(kept.notes.size() == 2);
  CHECK(kept.remap[1] == 1 && kept.remap[2] == 2);

  writeit::Document none = writeit::blank_document("Sans", 11);
  none.paragraphs = {text_paragraph("body")};
  none.notes = {{text_paragraph("one")}, {text_paragraph("two")}};
  const writeit::NoteDrop empty = writeit::drop_unmarked_notes(none);
  CHECK(empty.changed);
  CHECK(empty.notes.empty());
  CHECK(empty.remap[1] == 0 && empty.remap[2] == 0);

  return suite_test::done("note-marks", kChecks);
}
