/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

#include <string>

constexpr int kChecks = 66;

namespace {

bool has(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

const writeit::Run* only_run(const writeit::Document& doc)
{
  if (doc.paragraphs.size() != 1 || doc.paragraphs[0].runs.size() != 1)
    return nullptr;
  return &doc.paragraphs[0].runs[0];
}

bool imported(const char* rtf, writeit::Document& doc)
{
  return writeit::rtf_import(rtf, doc) && only_run(doc) != nullptr;
}

void palette()
{
  using writeit::Ink;
  CHECK(std::string(writeit::ink_name(Ink::Auto, false)) == "Automatic");
  CHECK(std::string(writeit::ink_name(Ink::Auto, true)) == "None");
  CHECK(std::string(writeit::ink_name(Ink::Red, false)) == "Red");
  CHECK(std::string(writeit::ink_name(Ink::Cyan, false)) == "Turquoise");
  CHECK(std::string(writeit::ink_name(Ink::Green, false)) == "Bright Green");
  CHECK(std::string(writeit::ink_name(Ink::Magenta, false)) == "Pink");
  CHECK(std::string(writeit::ink_name(Ink::Gray25, false)) == "Gray-25%");
  const writeit::Rgb red = writeit::ink_rgb(Ink::Red);
  CHECK(red.red == 255 && red.green == 0 && red.blue == 0);
  const writeit::Rgb gray = writeit::ink_rgb(Ink::Gray25);
  CHECK(gray.red == 192 && gray.green == 192 && gray.blue == 192);
  CHECK(writeit::ink_from_rgb(255, 0, 0) == Ink::Red);
  CHECK(writeit::ink_from_rgb(250, 5, 5) == Ink::Red);
  CHECK(writeit::ink_from_rgb(0, 0, 0) == Ink::Black);
  // Dark Yellow and Gray-50% are the same distance. The lower index wins.
  CHECK(writeit::ink_from_rgb(128, 128, 64) == Ink::DarkYellow);
  CHECK(writeit::ink_clamp(0) == Ink::Auto);
  CHECK(writeit::ink_clamp(6) == Ink::Red);
  CHECK(writeit::ink_clamp(99) == Ink::Auto);
  CHECK(writeit::ink_clamp(-1) == Ink::Auto);
  CHECK(writeit::ink_hex(Ink::Red) == "#FF0000");
}

void round_trip()
{
  using namespace writeit;
  Document doc = blank_document("Sans", 11);
  Run up;
  up.text = "Up";
  up.color = Ink::Red;
  up.highlight = Ink::Yellow;
  up.strike = true;
  up.script = Script::Super;
  Run down;
  down.text = "Down";
  down.color = Ink::Blue;
  down.script = Script::Sub;
  doc.paragraphs[0].runs.push_back(up);
  doc.paragraphs[0].runs.push_back(down);
  const std::string written = rtf_export(doc);
  CHECK(has(written, "{\\colortbl;"));
  CHECK(has(written, "\\red255\\green0\\blue0;"));
  CHECK(has(written, "\\cf6"));
  CHECK(has(written, "\\highlight7"));
  CHECK(has(written, "\\strike"));
  CHECK(has(written, "\\super"));
  CHECK(has(written, "\\cf2"));
  CHECK(has(written, "\\sub"));
  Document back;
  CHECK(rtf_import(written, back));
  CHECK(back == doc);
}

void empty_mark()
{
  using namespace writeit;
  Document doc = blank_document("Sans", 11);
  Run mark;
  mark.color = Ink::Red;
  mark.highlight = Ink::Yellow;
  mark.strike = true;
  mark.script = Script::Sub;
  doc.paragraphs[0].mark = mark;
  Document back;
  CHECK(rtf_import(rtf_export(doc), back));
  CHECK(back.paragraphs.size() == 1 && back.paragraphs[0].runs.empty() && back.paragraphs[0].mark &&
        back.paragraphs[0].mark->color == Ink::Red &&
        back.paragraphs[0].mark->highlight == Ink::Yellow && back.paragraphs[0].mark->strike &&
        back.paragraphs[0].mark->script == Script::Sub);
}

void foreign()
{
  using namespace writeit;
  Document doc;
  CHECK(imported("{\\rtf1\\ansi{\\colortbl;\\red0\\green255\\blue255;}\\cf1 Turq\\par}", doc));
  CHECK(only_run(doc) && only_run(doc)->color == Ink::Cyan);
  CHECK(imported("{\\rtf1\\ansi{\\colortbl;\\red200\\green0\\blue0;}\\cf1 Near\\par}", doc));
  CHECK(only_run(doc) && only_run(doc)->color == Ink::Red);
  CHECK(imported("{\\rtf1\\ansi{\\colortbl;\\red255\\green0\\blue0;}\\cf0 Auto\\par}", doc));
  CHECK(only_run(doc) && only_run(doc)->color == Ink::Auto);
  CHECK(imported("{\\rtf1\\ansi{\\colortbl;\\red255\\green0\\blue0;}\\highlight7 Hi\\par}", doc));
  CHECK(only_run(doc) && only_run(doc)->highlight == Ink::Yellow);
  CHECK(only_run(doc) && only_run(doc)->color == Ink::Auto);
  CHECK(imported("{\\rtf1\\ansi\\cf6 Red\\par}", doc));
  CHECK(only_run(doc) && only_run(doc)->color == Ink::Red);
}

void words()
{
  using namespace writeit;
  Document doc;
  CHECK(rtf_import("{\\rtf1\\ansi On\\strike Off\\strike0\\par}", doc));
  CHECK(doc.paragraphs.size() == 1 && doc.paragraphs[0].runs.size() == 2 &&
        doc.paragraphs[0].runs[0].text == "On" && doc.paragraphs[0].runs[0].strike &&
        doc.paragraphs[0].runs[1].text == "Off" && !doc.paragraphs[0].runs[1].strike);

  CHECK(rtf_import("{\\rtf1\\ansi Up\\super Down\\nosupersub\\par}", doc));
  CHECK(doc.paragraphs.size() == 1 && doc.paragraphs[0].runs.size() == 2 &&
        doc.paragraphs[0].runs[0].text == "Up" &&
        doc.paragraphs[0].runs[0].script == Script::Super &&
        doc.paragraphs[0].runs[1].text == "Down" &&
        doc.paragraphs[0].runs[1].script == Script::None);

  CHECK(rtf_import("{\\rtf1\\ansi\\sub Down\\super0 Still\\par}", doc));
  CHECK(doc.paragraphs.size() == 1 && doc.paragraphs[0].runs.size() == 1 &&
        doc.paragraphs[0].runs[0].text == "DownStill" &&
        doc.paragraphs[0].runs[0].script == Script::Sub);

  CHECK(rtf_import("{\\rtf1\\ansi Up\\super Down\\sub\\par}", doc));
  CHECK(doc.paragraphs.size() == 1 && doc.paragraphs[0].runs.size() == 2 &&
        doc.paragraphs[0].runs[0].text == "Up" && doc.paragraphs[0].runs[0].script == Script::Super &&
        doc.paragraphs[0].runs[1].text == "Down" &&
        doc.paragraphs[0].runs[1].script == Script::Sub);

  CHECK(rtf_import("{\\rtf1\\ansi\\cf6\\highlight7\\strike\\super Red\\plain Plain\\par}", doc));
  CHECK(doc.paragraphs.size() == 1 && doc.paragraphs[0].runs.size() == 2 &&
        doc.paragraphs[0].runs[0].text == "Red" && doc.paragraphs[0].runs[0].color == Ink::Red &&
        doc.paragraphs[0].runs[0].highlight == Ink::Yellow && doc.paragraphs[0].runs[0].strike &&
        doc.paragraphs[0].runs[0].script == Script::Super &&
        doc.paragraphs[0].runs[1].text == "Plain" && doc.paragraphs[0].runs[1].color == Ink::Auto &&
        doc.paragraphs[0].runs[1].highlight == Ink::Auto && !doc.paragraphs[0].runs[1].strike &&
        doc.paragraphs[0].runs[1].script == Script::None);
}

void marker()
{
  using namespace writeit;
  Document doc;
  CHECK(rtf_import("{\\rtf1\\ansi see{\\super 1{\\footnote The note.}}", doc));
  CHECK(doc.paragraphs.size() == 1);
  bool saw_see = false;
  bool saw_mark = false;
  if (doc.paragraphs.size() == 1) {
    for (const Run& run : doc.paragraphs[0].runs) {
      if (run.note == 0 && run.text.find("see") != std::string::npos)
        saw_see = run.script == Script::None;
      if (run.note != 0)
        saw_mark = run.text == "1" && run.script == Script::None;
    }
  }
  CHECK(saw_see);
  CHECK(saw_mark);
  bool body = !doc.notes.empty();
  for (const std::vector<Paragraph>& note : doc.notes) {
    for (const Paragraph& paragraph : note) {
      for (const Run& run : paragraph.runs) {
        if (run.script != Script::None)
          body = false;
      }
    }
  }
  CHECK(body);
}

void style_round()
{
  using namespace writeit;
  Document doc = blank_document("Sans", 11);
  const Style* found = find_style(style_sheet(doc), "Block Text");
  Style block = found ? *found : Style{};
  block.format.color = Ink::Red;
  block.format.highlight = Ink::Yellow;
  block.format.strike = true;
  block.format.script = Script::Sub;
  CHECK(found && update_style(doc, "Block Text", block));
  Document back;
  CHECK(rtf_import(rtf_export(doc), back));
  const Style* read = find_style(style_sheet(back), "Block Text");
  CHECK(read && read->format.color == Ink::Red && read->format.highlight == Ink::Yellow &&
        read->format.strike && read->format.script == Script::Sub);
}

void keeps_colour()
{
  using namespace writeit;
  Document doc = blank_document("Sans", 11);
  Run run;
  run.text = "Blue";
  run.color = Ink::Blue;
  doc.paragraphs[0].runs.push_back(run);
  CHECK(apply_style(doc, 0, 0, "Heading 1"));
  CHECK(doc.paragraphs[0].runs.size() == 1 && doc.paragraphs[0].runs[0].color == Ink::Blue &&
        (doc.paragraphs[0].runs[0].direct & kDirectColor) != 0);
  Document back;
  CHECK(rtf_import(rtf_export(doc), back));
  const Run* again = back.paragraphs.empty() ? nullptr : only_run(back);
  CHECK(again && again->color == Ink::Blue);
}

void takes_colour()
{
  using namespace writeit;
  Document doc = blank_document("Sans", 11);
  const Style* found = find_style(style_sheet(doc), "Block Text");
  Style block = found ? *found : Style{};
  block.format.color = Ink::Red;
  CHECK(found && update_style(doc, "Block Text", block));
  Run run;
  run.text = "Body";
  doc.paragraphs[0].runs.push_back(run);
  CHECK(apply_style(doc, 0, 0, "Block Text"));
  CHECK(doc.paragraphs[0].runs.size() == 1 && doc.paragraphs[0].runs[0].color == Ink::Red &&
        (doc.paragraphs[0].runs[0].direct & kDirectColor) == 0);
}

}  // namespace

int main()
{
  palette();
  round_trip();
  empty_mark();
  foreign();
  words();
  marker();
  style_round();
  keeps_colour();
  takes_colour();
  return suite_test::done("character", kChecks);
}
