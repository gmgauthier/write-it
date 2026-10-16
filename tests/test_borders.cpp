/* SPDX-License-Identifier: Unlicense */

#include "border_draw.hpp"
#include "check.hpp"
#include "document.hpp"

#include <cctype>
#include <string>

constexpr int kChecks = 95;

namespace {

const char* kHead = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}}";

bool has_word(const std::string& text, const std::string& control)
{
  size_t at = 0;
  while ((at = text.find(control, at)) != std::string::npos) {
    const size_t next = at + control.size();
    if (next >= text.size() || !std::isalpha(static_cast<unsigned char>(text[next])))
      return true;
    at = next;
  }
  return false;
}

writeit::Run text_run(const char* text)
{
  writeit::Run run;
  run.font = "Sans";
  run.size = 11;
  run.text = text;
  return run;
}

writeit::Paragraph body(const char* text)
{
  writeit::Paragraph paragraph;
  paragraph.runs.push_back(text_run(text));
  return paragraph;
}

writeit::Paragraph cell(const char* text)
{
  writeit::Paragraph paragraph = body(text);
  paragraph.cell.table = 1;
  paragraph.cell.row = 0;
  paragraph.cell.column = 0;
  paragraph.cell.rows = 1;
  paragraph.cell.columns = 1;
  paragraph.cell.widths = {5000};
  return paragraph;
}

writeit::Style* heading(writeit::Document& doc)
{
  for (writeit::Style& style : doc.styles) {
    if (style.name == "Heading 1")
      return &style;
  }
  return nullptr;
}

}  // namespace

int main()
{
  using namespace writeit;

  {
    Document doc = blank_document("Sans", 11);
    Paragraph paragraph = body("Box");
    paragraph.borders = border_box(BorderStyle::Single, 20, Ink::Red, 40);
    doc.paragraphs = {paragraph};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\brdrt"));
    CHECK(has_word(rtf, "\\brdrl"));
    CHECK(has_word(rtf, "\\brdrb"));
    CHECK(has_word(rtf, "\\brdrr"));
    CHECK(has_word(rtf, "\\brdrw20"));
    CHECK(has_word(rtf, "\\brdrcf6"));
    CHECK(has_word(rtf, "\\brsp40"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1);
    CHECK(back.paragraphs[0].borders == paragraph.borders);
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph paragraph = body("Top");
    paragraph.borders.top.style = BorderStyle::Single;
    paragraph.borders.top.width = 10;
    paragraph.borders.top.color = Ink::Blue;
    doc.paragraphs = {paragraph};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\brdrt"));
    CHECK(!has_word(rtf, "\\brdrl"));
    CHECK(!has_word(rtf, "\\brdrb"));
    CHECK(!has_word(rtf, "\\brdrr"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1);
    CHECK(back.paragraphs[0].borders.top.style == BorderStyle::Single &&
          back.paragraphs[0].borders.top.width == 10 &&
          back.paragraphs[0].borders.top.color == Ink::Blue);
    CHECK(back.paragraphs[0].borders.left.style == BorderStyle::None &&
          back.paragraphs[0].borders.bottom.style == BorderStyle::None &&
          back.paragraphs[0].borders.right.style == BorderStyle::None);
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph paragraph = body("Shade");
    paragraph.shading.pattern = 2500;
    paragraph.shading.foreground = Ink::Blue;
    paragraph.shading.background = Ink::Yellow;
    doc.paragraphs = {paragraph};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\shading2500"));
    CHECK(has_word(rtf, "\\cfpat2"));
    CHECK(has_word(rtf, "\\cbpat7"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1 && back.paragraphs[0].shading == paragraph.shading);
    Document plain = blank_document("Sans", 11);
    plain.paragraphs = {body("Plain")};
    const std::string bare = rtf_export(plain);
    CHECK(!has_word(bare, "\\brdrt"));
    CHECK(!has_word(bare, "\\shading"));
  }

  {
    Document doc = blank_document("Sans", 11);
    doc.paragraphs = {cell("Old")};
    const std::string rtf = rtf_export(doc);
    CHECK(!has_word(rtf, "\\clbrdrt"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1 && !back.paragraphs[0].cell.borders_set);
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph paragraph = cell("Grid");
    paragraph.cell.borders_set = true;
    paragraph.cell.borders = border_box(BorderStyle::Single, kDefaultBorderWidth, Ink::Auto);
    doc.paragraphs = {paragraph};
    const std::string rtf = rtf_export(doc);
    const size_t edge = rtf.find("\\clbrdrt\\brdrs\\brdrw10");
    const size_t cellx = rtf.find("\\cellx");
    CHECK(edge != std::string::npos && cellx != std::string::npos && edge < cellx);
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1 && back.paragraphs[0].cell.borders_set &&
          back.paragraphs[0].cell.borders == paragraph.cell.borders);
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph paragraph = cell("None");
    paragraph.cell.borders_set = true;
    doc.paragraphs = {paragraph};
    const std::string rtf = rtf_export(doc);
    CHECK(rtf.find("\\clbrdrt\\brdrnone") != std::string::npos);
    CHECK(rtf.find("\\clbrdrl\\brdrnone") != std::string::npos);
    CHECK(rtf.find("\\clbrdrb\\brdrnone") != std::string::npos);
    CHECK(rtf.find("\\clbrdrr\\brdrnone") != std::string::npos);
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1 && back.paragraphs[0].cell.borders_set &&
          borders_plain(back.paragraphs[0].cell.borders));
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph paragraph = cell("Tint");
    paragraph.cell.shading.pattern = 2500;
    paragraph.cell.shading.background = Ink::Yellow;
    doc.paragraphs = {paragraph};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\clshdng2500"));
    CHECK(has_word(rtf, "\\clcbpat7"));
    CHECK(!has_word(rtf, "\\clbrdrt"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1 && !back.paragraphs[0].cell.borders_set &&
          back.paragraphs[0].cell.shading == paragraph.cell.shading);
  }

  {
    Document doc = blank_document("Sans", 11);
    Style* style = heading(doc);
    CHECK(style != nullptr);
    style->borders.bottom.style = BorderStyle::Single;
    style->borders.bottom.width = 10;
    Paragraph keep = body("Keep");
    keep.style = "Heading 1";
    keep.borders = style->borders;
    Paragraph clear = body("Clear");
    clear.style = "Heading 1";
    doc.paragraphs = {keep, clear};
    const std::string rtf = rtf_export(doc);
    CHECK(rtf.find("\\brdrb\\brdrnone") != std::string::npos);
    Document back;
    CHECK(rtf_import(rtf, back));
    const Style* read = find_style(style_sheet(back), "Heading 1");
    CHECK(read && read->borders.bottom.style == BorderStyle::Single &&
          read->borders.bottom.width == 10);
    const Borders from_style = read ? read->borders : Borders{};
    CHECK(back.paragraphs.size() == 2 && back.paragraphs[0].borders == from_style);
    CHECK(back.paragraphs.size() == 2 && borders_plain(back.paragraphs[1].borders));
  }

  {
    const std::string rtf =
        std::string(kHead) + "\\pard\\brdrt\\brdrs\\brdrw10 one\\par\\pard two\\par}";
    Document doc;
    CHECK(rtf_import(rtf, doc));
    CHECK(doc.paragraphs.size() == 2 && doc.paragraphs[0].borders.top.style == BorderStyle::Single &&
          doc.paragraphs[0].borders.top.width == 10);
    CHECK(doc.paragraphs.size() == 2 && borders_plain(doc.paragraphs[1].borders));
  }

  {
    const std::string rtf = std::string(kHead) + "\\pard\\brdrt\\brdrs\\brdrw99999 wide\\par}";
    Document doc;
    CHECK(rtf_import(rtf, doc));
    CHECK(doc.paragraphs.size() == 1 && doc.paragraphs[0].borders.top.width == kMaxIndent);
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph hair = body("Hair");
    hair.borders.top.style = BorderStyle::Hairline;
    hair.borders.top.width = 10;
    Paragraph doubled = body("Double");
    doubled.borders.left.style = BorderStyle::Double;
    doubled.borders.left.width = 30;
    Paragraph dotted = body("Dotted");
    dotted.borders.bottom.style = BorderStyle::Dotted;
    dotted.borders.bottom.width = 15;
    Paragraph shadow = body("Shadow");
    shadow.borders = border_box(BorderStyle::Single, 10, Ink::Auto, 0, true);
    Paragraph raised = body("Three");
    raised.borders = border_threed(20, Ink::Black);
    doc.paragraphs = {hair, doubled, dotted, shadow, raised};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\brdrhair"));
    CHECK(has_word(rtf, "\\brdrdb"));
    CHECK(has_word(rtf, "\\brdrdot"));
    CHECK(has_word(rtf, "\\brdrsh"));
    CHECK(has_word(rtf, "\\brdrinset"));
    CHECK(has_word(rtf, "\\brdroutset"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 5);
    CHECK(back.paragraphs[0].borders == hair.borders);
    CHECK(back.paragraphs[1].borders == doubled.borders);
    CHECK(back.paragraphs[2].borders == dotted.borders);
    CHECK(back.paragraphs[3].borders == shadow.borders);
    CHECK(back.paragraphs[4].borders == raised.borders);
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph paragraph = cell("Own");
    paragraph.cell.borders_set = true;
    paragraph.cell.borders = border_box(BorderStyle::Single, 10, Ink::Auto);
    paragraph.borders.top.style = BorderStyle::Double;
    paragraph.borders.top.width = 30;
    doc.paragraphs = {paragraph};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\clbrdrt"));
    CHECK(has_word(rtf, "\\brdrdb"));
    const size_t cell_edge = rtf.find("\\clbrdrt");
    const size_t para_edge = rtf.find("\\brdrt\\brdrdb");
    CHECK(cell_edge != std::string::npos && para_edge != std::string::npos && cell_edge < para_edge);
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 1 && back.paragraphs[0].cell.borders == paragraph.cell.borders &&
          back.paragraphs[0].borders.top.style == BorderStyle::Double &&
          back.paragraphs[0].borders.top.width == 30);
  }

  {
    const std::string rtf = std::string(kHead) +
                            "\\trowd\\trgaph108\\clbrdrt\\brdrs\\brdrw10\\cellx5000"
                            "\\pard\\intbl one\\par\\pard\\intbl two\\cell\\row}";
    Document doc;
    CHECK(rtf_import(rtf, doc));
    CHECK(doc.paragraphs.size() == 2);
    CHECK(doc.paragraphs[0].cell.borders_set && doc.paragraphs[1].cell.borders_set);
    CHECK(doc.paragraphs[0].cell.borders.top.style == BorderStyle::Single &&
          doc.paragraphs[0].cell.borders.top.width == 10 &&
          doc.paragraphs[1].cell.borders == doc.paragraphs[0].cell.borders);
  }

  {
    Document doc = blank_document("Sans", 11);
    doc.paragraphs[0] = body("Before");
    CHECK(insert_table(doc.paragraphs, 1, 2, 2, 6000));
    const std::string rtf = rtf_export(doc);
    CHECK(!has_word(rtf, "\\clbrdrt"));
    Document back;
    CHECK(rtf_import(rtf, back));
    int cells = 0;
    bool legacy = true;
    for (const Paragraph& paragraph : back.paragraphs) {
      if (paragraph.cell.table == 0)
        continue;
      ++cells;
      if (paragraph.cell.borders_set)
        legacy = false;
    }
    CHECK(cells == 4 && legacy);
  }

  {
    Document doc = blank_document("Sans", 11);
    Document same = doc;
    CHECK(doc == same);
    same.paragraphs[0].borders = border_box(BorderStyle::Single, 10, Ink::Auto);
    CHECK(!(doc == same));
    Document framed = doc;
    framed.paragraphs[0].cell.borders_set = true;
    CHECK(!(doc == framed));
  }

  {
    // get_line_yrange's top includes the page seam. The border starts at the
    // text. A tall seam must not pull it onto the sheet above, and border
    // space stops at the top of this sheet.
    CHECK(border_top_y(1000, 0, 0, 40) == 1000);
    CHECK(border_top_y(1000, 0, 4, 40) == 996);
    CHECK(border_top_y(1000, 500, 0, 40) == 1500);
    CHECK(border_top_y(1000, 500, 10, 40) == 1490);
    CHECK(border_top_y(1000, 500, 100, 40) == 1460);
    CHECK(border_top_y(100, 2000, 0, 36) == 2100);
  }

  {
    Document doc = blank_document("Sans", 11);
    Paragraph above = body("Above");
    Paragraph top = body("Top");
    top.page_break = true;
    top.borders = border_box(BorderStyle::Single, kDefaultBorderWidth, Ink::Auto);
    doc.paragraphs = {above, top};
    const std::string rtf = rtf_export(doc);
    CHECK(has_word(rtf, "\\page"));
    CHECK(has_word(rtf, "\\brdrt"));
    Document back;
    CHECK(rtf_import(rtf, back));
    CHECK(back.paragraphs.size() == 2);
    CHECK(back.paragraphs[1].page_break);
    CHECK(back.paragraphs[1].borders.top.style == BorderStyle::Single);
    CHECK(borders_plain(back.paragraphs[0].borders));
  }

  suite_test::done("borders", kChecks);
  return 0;
}
