/* SPDX-License-Identifier: Unlicense */

// Endnotes are a separate sequence from footnotes. A footnote-only file still
// saves without \ftnalt and without \fet.

#include "check.hpp"
#include "document.hpp"
#include "spelling.hpp"

#include <string>

constexpr int kChecks = 70;

std::string text_of(const writeit::Paragraph& paragraph)
{
  std::string text;
  for (const writeit::Run& run : paragraph.runs) {
    if (run.note == 0 && run.endnote == 0)
      text += run.text;
  }
  return text;
}

std::string note_text(const std::vector<writeit::Paragraph>& note)
{
  std::string text;
  for (const writeit::Paragraph& paragraph : note)
    for (const writeit::Run& run : paragraph.runs)
      text += run.text;
  return text;
}

const writeit::Run* end_marker(const writeit::Document& doc, int number)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs)
    for (const writeit::Run& run : paragraph.runs)
      if (run.endnote == number)
        return &run;
  return nullptr;
}

const writeit::Run* foot_marker(const writeit::Document& doc, int number)
{
  for (const writeit::Paragraph& paragraph : doc.paragraphs)
    for (const writeit::Run& run : paragraph.runs)
      if (run.note == number)
        return &run;
  return nullptr;
}

writeit::Paragraph words(const char* text)
{
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.text = text;
  paragraph.runs.push_back(run);
  return paragraph;
}

int main()
{
  writeit::Document end;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi See i{\\footnote \\ftnalt The end.}}", end));
  CHECK(end.notes.empty());
  CHECK(end.endnotes.size() == 1);
  CHECK(note_text(end.endnotes[0]) == "The end.");
  CHECK(end.paragraphs.size() == 1);
  const writeit::Run* mark = end_marker(end, 1);
  CHECK(mark != nullptr);
  CHECK(mark && mark->text == "i");
  CHECK(mark && mark->endnote == 1);
  CHECK(mark && mark->note == 0);
  CHECK(text_of(end.paragraphs[0]) == "See ");
  const std::string saved = writeit::rtf_export(end);
  CHECK(saved.find("\\ftnalt") != std::string::npos);
  writeit::Document again;
  CHECK(writeit::rtf_import(saved, again));
  CHECK(again.notes.empty());
  CHECK(again.endnotes.size() == 1);
  CHECK(note_text(again.endnotes[0]) == "The end.");
  CHECK(end_marker(again, 1) && end_marker(again, 1)->text == "i");
  CHECK(writeit::rtf_export(again) == saved);

  writeit::Document foot;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi See 1{\\footnote The note.}}", foot));
  CHECK(foot.notes.size() == 1);
  CHECK(foot.endnotes.empty());
  CHECK(note_text(foot.notes[0]) == "The note.");
  const std::string foot_saved = writeit::rtf_export(foot);
  CHECK(foot_saved.find("\\ftnalt") == std::string::npos);
  CHECK(foot_saved.find("\\fet") == std::string::npos);
  CHECK(foot_saved.find("{\\super 1{\\footnote ") != std::string::npos);

  writeit::Document built;
  writeit::Paragraph paragraph;
  writeit::Run see;
  see.text = "See ";
  writeit::Run number;
  number.text = "1";
  number.note = 1;
  paragraph.runs.push_back(see);
  paragraph.runs.push_back(number);
  built.paragraphs.push_back(paragraph);
  built.notes.push_back({words("The note.")});
  const std::string once = writeit::rtf_export(built);
  writeit::Document loaded;
  CHECK(writeit::rtf_import(once, loaded));
  CHECK(loaded.notes.size() == 1);
  CHECK(loaded.endnotes.empty());
  CHECK(writeit::rtf_export(loaded) == once);

  writeit::Document mixed;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi One 1{\\footnote Foot.} Two i{\\footnote \\ftnalt End.} Three 2{\\footnote "
      "More.}}",
      mixed));
  CHECK(mixed.notes.size() == 2);
  CHECK(mixed.endnotes.size() == 1);
  CHECK(note_text(mixed.notes[0]) == "Foot.");
  CHECK(note_text(mixed.notes[1]) == "More.");
  CHECK(note_text(mixed.endnotes[0]) == "End.");
  const writeit::Run* first = foot_marker(mixed, 1);
  const writeit::Run* second = end_marker(mixed, 1);
  const writeit::Run* third = foot_marker(mixed, 2);
  CHECK(first && first->text == "1" && first->endnote == 0);
  CHECK(second && second->text == "i" && second->note == 0);
  CHECK(third && third->text == "2" && third->endnote == 0);

  writeit::Document chapter;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi Chapter 1\\par{\\footnote \\ftnalt The end.}\\par}",
                            chapter));
  CHECK(!chapter.paragraphs.empty());
  CHECK(text_of(chapter.paragraphs[0]) == "Chapter 1");
  CHECK(end_marker(chapter, 1) && end_marker(chapter, 1)->text == "i");
  CHECK(chapter.endnotes.size() == 1);
  CHECK(note_text(chapter.endnotes[0]) == "The end.");

  writeit::Document automatic;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi See section 1{\\chftn{\\footnote \\ftnalt The end.}}}",
                            automatic));
  CHECK(!automatic.paragraphs.empty());
  CHECK(text_of(automatic.paragraphs[0]) == "See section 1");
  CHECK(end_marker(automatic, 1) && end_marker(automatic, 1)->text == "i");

  std::string many = "{\\rtf1\\ansi ";
  for (int i = 0; i < 200; ++i)
    many += "n{\\footnote N.}";
  many += "e{\\footnote \\ftnalt E.}}";
  writeit::Document capped;
  CHECK(writeit::rtf_import(many, capped));
  CHECK(capped.notes.size() == 200);
  CHECK(capped.endnotes.empty());

  std::string both = "{\\rtf1\\ansi ";
  for (int i = 0; i < 199; ++i)
    both += "n{\\footnote N.}";
  both += "e{\\footnote \\ftnalt E.}}";
  writeit::Document room;
  CHECK(writeit::rtf_import(both, room));
  CHECK(room.notes.size() == 199);
  CHECK(room.endnotes.size() == 1);
  CHECK(note_text(room.endnotes[0]) == "E.");

  writeit::Document orphan;
  writeit::Run kept;
  kept.text = "ii";
  kept.endnote = 2;
  writeit::Paragraph body;
  body.runs.push_back(kept);
  orphan.paragraphs.push_back(body);
  orphan.endnotes.push_back({words("First")});
  orphan.endnotes.push_back({words("Second")});
  const writeit::NoteDrop drop = writeit::drop_unmarked_endnotes(orphan);
  CHECK(drop.changed);
  CHECK(drop.notes.size() == 1);
  CHECK(note_text(drop.notes[0]) == "Second");
  orphan.endnotes = drop.notes;
  writeit::apply_endnote_remap(orphan, drop.remap);
  CHECK(orphan.paragraphs[0].runs[0].endnote == 1);
  CHECK(orphan.paragraphs[0].runs[0].note == 0);
  CHECK(orphan.paragraphs[0].runs[0].text == "i");

  CHECK(writeit::format_page_number(1, writeit::PageNumeral::LowerRoman) == "i");
  CHECK(writeit::format_page_number(4, writeit::PageNumeral::LowerRoman) == "iv");
  CHECK(writeit::format_page_number(9, writeit::PageNumeral::LowerRoman) == "ix");
  CHECK(writeit::format_page_number(200, writeit::PageNumeral::LowerRoman) == "cc");

  writeit::Document spelled;
  spelled.endnotes.push_back({words("Quxx")});
  const std::vector<writeit::SpellMiss> misses = writeit::spelling_misses(
      spelled, [](const std::string&) { return false; });
  CHECK(misses.size() == 1);
  CHECK(!misses.empty() && misses[0].where == writeit::SpellWhere::Endnote);
  CHECK(!misses.empty() && misses[0].note == 0);
  CHECK(!misses.empty() && misses[0].word == "Quxx");
  writeit::Paragraph* hit = misses.empty() ? nullptr : writeit::spell_paragraph(spelled, misses[0]);
  CHECK(hit != nullptr);
  CHECK(hit && hit->runs[0].text == "Quxx");

  return suite_test::done("rtf-endnote", kChecks);
}
