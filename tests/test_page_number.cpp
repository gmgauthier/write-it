/* SPDX-License-Identifier: Unlicense */

// A page number in the header or footer is that sheet's number. A section
// can restart the count. One section with no field still saves as 1.0.

#include "check.hpp"
#include "document.hpp"

#include <cctype>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool has_word(const std::string& text, const std::string& word)
{
  for (size_t at = 0; (at = text.find(word, at)) != std::string::npos;) {
    const size_t end = at + word.size();
    if (end >= text.size() || std::isalpha(static_cast<unsigned char>(text[end])) == 0)
      return true;
    at = end;
  }
  return false;
}

bool has(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

writeit::Run text_run(const char* text)
{
  writeit::Run run;
  run.text = text;
  return run;
}

writeit::Paragraph body(const char* text)
{
  writeit::Paragraph paragraph;
  paragraph.runs.push_back(text_run(text));
  return paragraph;
}

writeit::Run page_run(writeit::PageSlot slot)
{
  writeit::Run run;
  run.page = true;
  run.page_slot = slot;
  return run;
}

const writeit::Run* field_in(const std::vector<writeit::Paragraph>& story)
{
  for (const writeit::Paragraph& paragraph : story)
    for (const writeit::Run& run : paragraph.runs)
      if (run.page)
        return &run;
  return nullptr;
}

std::string story(const writeit::Document& doc);

bool same_document(const writeit::Document& doc, const char* name)
{
  const std::string rtf = writeit::rtf_export(doc);
  writeit::Document again;
  if (writeit::rtf_import(rtf, again) && again == doc)
    return true;
  std::cerr << name << " did not round-trip\n" << rtf << "\n";
  return false;
}

void plain_file_gains_nothing()
{
  writeit::Document doc;
  doc.paragraphs.push_back(body("Hello"));
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(!has_word(rtf, "\\chpgn"));
  CHECK(!has_word(rtf, "\\pgplace"));
  CHECK(!has_word(rtf, "\\pgnrestart"));
  CHECK(!has_word(rtf, "\\pgnstarts"));
  CHECK(!has_word(rtf, "\\titlepg"));
  CHECK(!has_word(rtf, "\\pgndec"));
  CHECK(!has_word(rtf, "\\sect"));
  CHECK(!has_word(rtf, "\\sectd"));
  CHECK(!has(rtf, "\\paperw"));
  CHECK(same_document(doc, "plain"));
  writeit::Document other = doc;
  other.numbering.start = 9;
  CHECK(doc == other);
}

void footer_field_round_trip()
{
  writeit::Document doc;
  doc.paragraphs.push_back(body("Hello"));
  writeit::Paragraph field;
  field.align = writeit::Align::Right;
  field.runs.push_back(page_run(writeit::PageSlot::Right));
  doc.footer.push_back(field);
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(has(rtf, "\\pgplace2\\chpgn"));
  CHECK(has(rtf, "\\qr"));
  CHECK(!has_word(rtf, "\\sect"));
  CHECK(!has_word(rtf, "\\sectd"));
  CHECK(!has(rtf, "\\paperw"));
  CHECK(!has_word(rtf, "\\pgnrestart"));
  CHECK(same_document(doc, "footer field"));
  writeit::Document again;
  CHECK(writeit::rtf_import(rtf, again));
  const writeit::Run* run = field_in(again.footer);
  CHECK(run != nullptr && run->page && run->page_slot == writeit::PageSlot::Right);
  CHECK(again.footer.size() == 1 && again.footer[0].align == writeit::Align::Right);
  CHECK(again.header.empty());
}

void place_and_align()
{
  writeit::Document right;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi{\\footer \\pard\\qr\\chpgn\\par}}", right));
  const writeit::Run* from_align = field_in(right.footer);
  CHECK(from_align != nullptr && from_align->page_slot == writeit::PageSlot::Right);

  writeit::Document centre;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi{\\footer \\pard\\qc\\pgplace1\\chpgn\\par}}", centre));
  const writeit::Run* from_place = field_in(centre.footer);
  CHECK(from_place != nullptr && from_place->page_slot == writeit::PageSlot::Center);

  writeit::Document inside;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi{\\footer \\pard\\qr\\chpgn\\pgplace3\\par}}", inside));
  const writeit::Run* after = field_in(inside.footer);
  CHECK(after != nullptr && after->page_slot == writeit::PageSlot::Inside);

  writeit::Document left;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi{\\footer \\pard\\pgplace0\\chpgn\\par}}", left));
  const writeit::Run* at_left = field_in(left.footer);
  CHECK(at_left != nullptr && at_left->page_slot == writeit::PageSlot::Left);
  CHECK(left.footer.size() == 1 && left.footer[0].align == writeit::Align::Left);
}

void numbering_words()
{
  writeit::Document doc;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi\\pgnrestart\\pgnstarts5\\pgnlcrm\\pard Hi\\par}", doc));
  CHECK(doc.numbering.restart);
  CHECK(doc.numbering.start == 5);
  CHECK(doc.numbering.numeral == writeit::PageNumeral::LowerRoman);
  CHECK(doc.numbering.show_first);
  CHECK(doc.sections.empty());
  CHECK(same_document(doc, "restart roman"));

  writeit::Document hidden;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi\\titlepg\\pard Hi\\par}", hidden));
  CHECK(!hidden.numbering.show_first);
  CHECK(!hidden.numbering.restart);
  CHECK(same_document(hidden, "titlepg"));

  writeit::Document letters;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi\\pgnucltr\\pard Hi\\par}", letters));
  CHECK(letters.numbering.numeral == writeit::PageNumeral::UpperLetter);
  CHECK(!letters.numbering.restart);
}

void page_field()
{
  writeit::Document doc;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi{\\footer \\pard\\qr{\\field{\\*\\fldinst PAGE}{\\fldrslt 1}}\\par}Hi\\par}",
      doc));
  const writeit::Run* run = field_in(doc.footer);
  CHECK(run != nullptr && run->page && run->text.empty());
  CHECK(run->page_slot == writeit::PageSlot::Right);
  CHECK(story(doc) == "Hi");
  CHECK(doc.paragraphs.size() == 1);
}

std::string story(const writeit::Document& doc)
{
  std::string out;
  for (const writeit::Paragraph& paragraph : doc.paragraphs)
    for (const writeit::Run& run : paragraph.runs)
      out += run.text;
  return out;
}

void formats()
{
  CHECK(writeit::format_page_number(1, writeit::PageNumeral::Arabic) == "1");
  CHECK(writeit::format_page_number(0, writeit::PageNumeral::Arabic) == "0");
  CHECK(writeit::format_page_number(1, writeit::PageNumeral::LowerLetter) == "a");
  CHECK(writeit::format_page_number(26, writeit::PageNumeral::LowerLetter) == "z");
  CHECK(writeit::format_page_number(27, writeit::PageNumeral::LowerLetter) == "aa");
  // Word 97 repeats the letter. 28 is bb, not the spreadsheet column ab.
  CHECK(writeit::format_page_number(28, writeit::PageNumeral::LowerLetter) == "bb");
  CHECK(writeit::format_page_number(52, writeit::PageNumeral::LowerLetter) == "zz");
  CHECK(writeit::format_page_number(53, writeit::PageNumeral::LowerLetter) == "aaa");
  CHECK(writeit::format_page_number(0, writeit::PageNumeral::LowerLetter) == "0");
  CHECK(writeit::format_page_number(27, writeit::PageNumeral::UpperLetter) == "AA");
  CHECK(writeit::format_page_number(28, writeit::PageNumeral::UpperLetter) == "BB");
  CHECK(writeit::format_page_number(1, writeit::PageNumeral::LowerRoman) == "i");
  CHECK(writeit::format_page_number(4, writeit::PageNumeral::LowerRoman) == "iv");
  CHECK(writeit::format_page_number(9, writeit::PageNumeral::LowerRoman) == "ix");
  CHECK(writeit::format_page_number(40, writeit::PageNumeral::LowerRoman) == "xl");
  CHECK(writeit::format_page_number(1990, writeit::PageNumeral::LowerRoman) == "mcmxc");
  CHECK(writeit::format_page_number(4, writeit::PageNumeral::UpperRoman) == "IV");
  CHECK(writeit::format_page_number(0, writeit::PageNumeral::UpperRoman) == "0");
  CHECK(writeit::clamp_page_start(-3) == 0);
  CHECK(writeit::clamp_page_start(40000) == 32767);
  writeit::PageNumbering loose;
  loose.restart = false;
  loose.start = 12;
  CHECK(writeit::canonical_page_numbering(loose).start == 1);
}

void counts()
{
  writeit::PageNumbering plain;
  auto labels = [](const std::vector<writeit::PrintedPage>& pages) {
    std::string out;
    for (const writeit::PrintedPage& page : pages) {
      if (!out.empty())
        out.push_back(',');
      out += page.label.empty() ? "#" : page.label;
    }
    return out;
  };
  CHECK(labels(writeit::printed_pages(3, {0}, {plain})) == "1,2,3");

  writeit::PageNumbering restart;
  restart.restart = true;
  restart.start = 1;
  CHECK(labels(writeit::printed_pages(4, {0, 2}, {plain, restart})) == "1,2,1,2");

  writeit::PageNumbering cont;
  CHECK(labels(writeit::printed_pages(4, {0, 2}, {plain, cont})) == "1,2,3,4");

  writeit::PageNumbering hidden = plain;
  hidden.show_first = false;
  CHECK(labels(writeit::printed_pages(3, {0}, {hidden})) == "#,2,3");

  writeit::PageNumbering at_five = restart;
  at_five.start = 5;
  at_five.numeral = writeit::PageNumeral::LowerRoman;
  CHECK(labels(writeit::printed_pages(2, {0}, {at_five})) == "v,vi");

  writeit::PageNumbering later;
  later.restart = true;
  later.start = 8;
  CHECK(labels(writeit::printed_pages(2, {0, 0}, {plain, later})) == "8,9");

  writeit::PageNumbering first;
  first.restart = true;
  first.start = 4;
  CHECK(labels(writeit::printed_pages(2, {0}, {first})) == "4,5");

  // A later section that begins on the same sheet and does not restart
  // leaves that sheet with the section that opened it.
  writeit::PageNumbering at_ten = first;
  at_ten.start = 10;
  writeit::PageNumbering shared;
  CHECK(labels(writeit::printed_pages(1, {0, 0}, {at_ten, shared})) == "10");
  CHECK(labels(writeit::printed_pages(2, {0, 0}, {at_ten, shared})) == "10,11");
  writeit::PageNumbering shared_roman = shared;
  shared_roman.numeral = writeit::PageNumeral::LowerRoman;
  CHECK(labels(writeit::printed_pages(2, {0, 0}, {at_ten, shared_roman})) == "10,xi");
  writeit::PageNumbering hidden_ten = at_ten;
  hidden_ten.show_first = false;
  CHECK(labels(writeit::printed_pages(2, {0, 0}, {hidden_ten, shared})) == "#,11");
  writeit::PageNumbering next_one = first;
  next_one.start = 1;
  CHECK(labels(writeit::printed_pages(3, {0, 0, 2}, {at_ten, shared, next_one})) == "10,11,1");
  // A continuing section that begins on the next sheet numbers that sheet.
  // One that begins on the sheet after the one it sits on leaves that sheet
  // alone and numbers the sheet after it.
  CHECK(labels(writeit::printed_pages(2, {0, 1}, {at_ten, shared_roman})) == "10,xi");
  CHECK(labels(writeit::printed_pages(3, {0, 2}, {at_ten, shared_roman})) == "10,11,xii");
  writeit::PageNumbering took = shared_roman;
  took.restart = true;
  took.start = 1;
  CHECK(labels(writeit::printed_pages(3, {0, 1}, {at_ten, took})) == "10,i,ii");
}

void sides()
{
  CHECK(writeit::page_number_side(writeit::PageSlot::Inside, 1) == writeit::PageSide::Left);
  CHECK(writeit::page_number_side(writeit::PageSlot::Inside, 2) == writeit::PageSide::Right);
  CHECK(writeit::page_number_side(writeit::PageSlot::Inside, 0) == writeit::PageSide::Right);
  CHECK(writeit::page_number_side(writeit::PageSlot::Outside, 2) == writeit::PageSide::Left);
  CHECK(writeit::page_number_side(writeit::PageSlot::Outside, 0) == writeit::PageSide::Left);
  CHECK(writeit::page_number_side(writeit::PageSlot::Left, 2) == writeit::PageSide::Left);
  CHECK(writeit::page_number_side(writeit::PageSlot::Center, 1) == writeit::PageSide::Center);
  CHECK(writeit::page_slot_align(writeit::PageSlot::Inside) == writeit::Align::Left);
  CHECK(writeit::page_slot_align(writeit::PageSlot::Outside) == writeit::Align::Right);
  CHECK(writeit::page_slot_align(writeit::PageSlot::Center) == writeit::Align::Center);
}

void new_section_continues()
{
  writeit::Document doc;
  doc.paragraphs.push_back(body("Hello"));
  doc.numbering.restart = true;
  doc.numbering.start = 3;
  doc.numbering.numeral = writeit::PageNumeral::LowerRoman;
  doc.numbering.show_first = false;
  writeit::Paragraph field;
  field.runs.push_back(page_run(writeit::PageSlot::Right));
  doc.footer.push_back(field);
  const int at = 5;
  CHECK(writeit::break_section(doc, at, writeit::SectionBreak::NextPage) >= 0);
  CHECK(doc.sections.size() == 1);
  CHECK(!doc.sections[0].numbering.restart);
  CHECK(doc.sections[0].numbering.start == 1);
  CHECK(doc.sections[0].numbering.numeral == writeit::PageNumeral::LowerRoman);
  CHECK(!doc.sections[0].numbering.show_first);
  CHECK(field_in(doc.sections[0].footer) != nullptr);
  CHECK(doc.numbering.restart && doc.numbering.start == 3);
}

void field_text()
{
  writeit::Paragraph title;
  title.runs.push_back(text_run("Title"));
  writeit::Paragraph field;
  field.runs.push_back(page_run(writeit::PageSlot::Right));
  std::vector<writeit::Paragraph> story;
  story.push_back(title);
  story.push_back(field);
  CHECK(writeit::story_field_text(story, "1") == "Title\n1");
  CHECK(writeit::story_field_text(story, "") == "Title\n#");
  // The bar shows the field's result. A one-character stand-in shifts the
  // paragraph that follows once the number passes 9.
  CHECK(writeit::story_paragraph_text(field, "10") == "10");
  CHECK(writeit::story_paragraph_text(field, "") == "#");
  CHECK(writeit::story_has_page(story));
  CHECK(!writeit::story_has_page({title}));
}

void choice_from()
{
  // No field keeps Footer and Right, and still shows this section's count.
  writeit::PageNumbering roman;
  roman.restart = true;
  roman.start = 4;
  roman.numeral = writeit::PageNumeral::LowerRoman;
  roman.show_first = false;
  const writeit::PageNumberChoice none = writeit::page_number_choice_from({}, {}, roman);
  CHECK(!none.in_header);
  CHECK(none.slot == writeit::PageSlot::Right);
  CHECK(none.numbering.numeral == writeit::PageNumeral::LowerRoman);
  CHECK(none.numbering.restart);
  CHECK(none.numbering.start == 4);
  CHECK(!none.numbering.show_first);

  writeit::Paragraph footer;
  footer.runs.push_back(page_run(writeit::PageSlot::Center));
  const writeit::PageNumberChoice from_footer = writeit::page_number_choice_from({}, {footer}, {});
  CHECK(!from_footer.in_header);
  CHECK(from_footer.slot == writeit::PageSlot::Center);

  writeit::Paragraph header;
  header.runs.push_back(page_run(writeit::PageSlot::Inside));
  const writeit::PageNumberChoice from_header = writeit::page_number_choice_from({header}, {}, {});
  CHECK(from_header.in_header);
  CHECK(from_header.slot == writeit::PageSlot::Inside);
  CHECK(!from_header.numbering.restart);
  CHECK(from_header.numbering.start == 1);

  // A field in both stories is the footer, which is what the page draws last.
  const writeit::PageNumberChoice both = writeit::page_number_choice_from({header}, {footer}, {});
  CHECK(!both.in_header);
  CHECK(both.slot == writeit::PageSlot::Center);
}

void normal_font_round_trip()
{
  // Insert stores Normal's face on the run. The file has to bring that face
  // back, or the number is drawn in Sans 11.
  writeit::Document doc;
  doc.styles = writeit::builtin_styles("Times", 12);
  doc.paragraphs.push_back(body("Hello"));
  writeit::Paragraph field;
  field.align = writeit::Align::Right;
  writeit::Run run = page_run(writeit::PageSlot::Right);
  run.font = "Times";
  run.size = 12;
  field.runs.push_back(run);
  doc.footer.push_back(field);
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(has(rtf, "\\pgplace2\\chpgn"));
  CHECK(same_document(doc, "times page"));
  writeit::Document again;
  CHECK(writeit::rtf_import(rtf, again));
  const writeit::Run* imported = field_in(again.footer);
  CHECK(imported != nullptr && imported->font == "Times");
  CHECK(imported != nullptr && imported->size == 12);
}

}  // namespace

// Exactly the checks this suite runs. Update it with the tests.
constexpr int kChecks = 129;

int main()
{
  plain_file_gains_nothing();
  footer_field_round_trip();
  place_and_align();
  numbering_words();
  page_field();
  formats();
  counts();
  sides();
  new_section_continues();
  field_text();
  choice_from();
  normal_font_round_trip();
  return suite_test::done("page-number", kChecks);
}
