/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

#include <string>

namespace {

writeit::Run text_run(const char* text)
{
  writeit::Run run;
  run.text = text;
  return run;
}

std::string text_of(const writeit::Paragraph& paragraph)
{
  std::string text;
  for (const writeit::Run& run : paragraph.runs)
    text += run.text;
  return text;
}

// The group that opens with {\word, braces matched, or empty.
std::string group_of(const std::string& rtf, const std::string& word)
{
  const std::string key = "{\\" + word;
  const size_t at = rtf.find(key);
  if (at == std::string::npos)
    return {};
  int depth = 0;
  for (size_t i = at; i < rtf.size(); ++i) {
    if (rtf[i] == '{')
      ++depth;
    else if (rtf[i] == '}') {
      --depth;
      if (depth == 0)
        return rtf.substr(at, i + 1 - at);
    }
  }
  return {};
}

void plain_story()
{
  writeit::Document doc;
  writeit::Paragraph body;
  body.runs.push_back(text_run("Hello"));
  doc.paragraphs.push_back(body);
  writeit::Paragraph header;
  header.runs.push_back(text_run("Head"));
  doc.header.push_back(header);
  writeit::Paragraph footer;
  footer.runs.push_back(text_run("Foot"));
  doc.footer.push_back(footer);
  const std::string rtf = writeit::rtf_export(doc);
  const std::string head = group_of(rtf, "header");
  const std::string foot = group_of(rtf, "footer");
  CHECK(head.find("\\s") == std::string::npos);
  CHECK(head.find("\\ls") == std::string::npos);
  CHECK(foot.find("\\s") == std::string::npos);
  CHECK(foot.find("\\ls") == std::string::npos);
  CHECK(rtf.find("\\listtable") == std::string::npos);
  writeit::Document again;
  CHECK(writeit::rtf_import(rtf, again) && again == doc);
}

void header_style()
{
  writeit::Document doc;
  writeit::Paragraph body;
  body.runs.push_back(text_run("Hello"));
  doc.paragraphs.push_back(body);
  writeit::Paragraph header;
  header.style = "Heading 1";
  header.heading = 1;
  header.runs.push_back(text_run("Title"));
  doc.header.push_back(header);
  const std::string rtf = writeit::rtf_export(doc);
  const std::string head = group_of(rtf, "header");
  CHECK(rtf.find("{\\stylesheet") != std::string::npos);
  CHECK(head.find("\\s1\\outlinelevel0") != std::string::npos);
  writeit::Document again;
  CHECK(writeit::rtf_import(rtf, again));
  CHECK(again.header.size() == 1);
  CHECK(again.header[0].style == "Heading 1");
  CHECK(again.header[0].heading == 1);
  CHECK(text_of(again.header[0]) == "Title");
}

void footer_bullet()
{
  writeit::Document doc;
  writeit::Paragraph body;
  body.runs.push_back(text_run("Hello"));
  doc.paragraphs.push_back(body);
  writeit::Paragraph item;
  item.list = writeit::ListFormat{writeit::ListKind::Bullet, 0};
  item.runs.push_back(text_run("Dot"));
  doc.footer.push_back(item);
  const std::string rtf = writeit::rtf_export(doc);
  const std::string foot = group_of(rtf, "footer");
  CHECK(foot.find("\\ls1\\ilvl0") != std::string::npos);
  writeit::Document again;
  CHECK(writeit::rtf_import(rtf, again));
  CHECK(again.footer.size() == 1);
  CHECK(again.footer[0].list.kind == writeit::ListKind::Bullet);
  CHECK(again.footer[0].list.level == 0);
  CHECK(text_of(again.footer[0]) == "Dot");
}

void separate_numbers()
{
  writeit::Document doc;
  writeit::Paragraph body;
  body.list = writeit::ListFormat{writeit::ListKind::Number, 0};
  body.list.list = 1;
  writeit::Run see = text_run("Body ");
  writeit::Run mark = text_run("1");
  mark.note = 1;
  body.runs.push_back(see);
  body.runs.push_back(mark);
  doc.paragraphs.push_back(body);
  writeit::Paragraph note;
  note.list = writeit::ListFormat{writeit::ListKind::Number, 0};
  note.list.list = 1;
  note.runs.push_back(text_run("Note"));
  doc.notes.push_back({note});
  const std::string rtf = writeit::rtf_export(doc);
  const std::string footnote = group_of(rtf, "footnote");
  CHECK(rtf.find("\\ls2\\ilvl0") != std::string::npos);
  CHECK(footnote.find("\\ls3\\ilvl0") != std::string::npos);
  CHECK(footnote.find("\\ls2") == std::string::npos);
  writeit::Document again;
  CHECK(writeit::rtf_import(rtf, again));
  CHECK(again.notes.size() == 1 && again.notes[0].size() == 1);
  CHECK(again.notes[0][0].list.kind == writeit::ListKind::Number);
  const std::vector<int> body_numbers = writeit::list_numbers(again.paragraphs);
  const std::vector<int> note_numbers = writeit::list_numbers(again.notes[0]);
  CHECK(body_numbers.size() == 1 && body_numbers[0] == 1);
  CHECK(note_numbers.size() == 1 && note_numbers[0] == 1);
}

void story_start()
{
  writeit::Document doc;
  writeit::Paragraph body;
  body.runs.push_back(text_run("Hello"));
  doc.paragraphs.push_back(body);
  writeit::Paragraph item;
  item.list = writeit::ListFormat{writeit::ListKind::Number, 0};
  item.list.list = 1;
  item.list.start = 4;
  item.runs.push_back(text_run("Four"));
  doc.footer.push_back(item);
  const std::string rtf = writeit::rtf_export(doc);
  const std::string foot = group_of(rtf, "footer");
  CHECK(rtf.find("\\levelstartat4") != std::string::npos);
  CHECK(foot.find("\\ls2\\ilvl0") != std::string::npos);
  writeit::Document again;
  CHECK(writeit::rtf_import(rtf, again));
  CHECK(again.footer.size() == 1);
  CHECK(again.footer[0].list.kind == writeit::ListKind::Number);
  CHECK(again.footer[0].list.start == 4);
  const std::vector<int> numbers = writeit::list_numbers(again.footer);
  CHECK(numbers.size() == 1 && numbers[0] == 4);
}

void header_overrides()
{
  writeit::Document doc = writeit::blank_document("Sans", 11);
  for (writeit::Style& style : doc.styles)
    if (style.name == "Block Text")
      style.align = writeit::Align::Center;
  writeit::Paragraph header;
  header.style = "Block Text";
  header.align = writeit::Align::Left;
  header.runs.push_back(text_run("Head"));
  doc.header = {header};
  const std::string head = group_of(writeit::rtf_export(doc), "header");
  CHECK(head.find("\\li0") != std::string::npos);
  CHECK(head.find("\\ri0") != std::string::npos);
  CHECK(head.find("\\ql") != std::string::npos);
  writeit::Document back;
  CHECK(writeit::rtf_import(writeit::rtf_export(doc), back));
  CHECK(back.header.size() == 1);
  CHECK(back.header[0].align == writeit::Align::Left);
  CHECK(back.header[0].indents.left == 0 && back.header[0].indents.right == 0);

  writeit::Paragraph titled;
  titled.style = "Heading 1";
  titled.heading = 0;
  titled.runs.push_back(text_run("Title"));
  doc.header = {titled};
  const std::string titled_head = group_of(writeit::rtf_export(doc), "header");
  CHECK(titled_head.find("\\outlinelevel9") != std::string::npos);
  writeit::Document titled_back;
  CHECK(writeit::rtf_import(writeit::rtf_export(doc), titled_back));
  CHECK(titled_back.header.size() == 1 && titled_back.header[0].heading == 0);
}

}  // namespace

// plain 6, header style 7, footer bullet 6, separate numbers 8, story start 7,
// header overrides 10.
constexpr int kChecks = 44;

int main()
{
  plain_story();
  header_style();
  footer_bullet();
  separate_numbers();
  story_start();
  header_overrides();
  return suite_test::done("rtf-story-format", kChecks);
}
