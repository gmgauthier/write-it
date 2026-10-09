/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"
#include "font_sizes.hpp"

namespace {

writeit::Run run(const char* text, const char* font, int size, bool bold, bool italic, bool underline)
{
  writeit::Run item;
  item.text = text;
  item.font = font;
  item.size = size;
  item.bold = bold;
  item.italic = italic;
  item.underline = underline;
  return item;
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 52;

int main()
{
  writeit::Document doc;
  writeit::Paragraph first;
  first.runs.push_back(run("Hello ", "Sans", 11, false, false, false));
  first.runs.push_back(run("bold", "Sans", 11, true, false, false));
  first.runs.push_back(run(" ", "Sans", 11, false, false, false));
  first.runs.push_back(run("italic", "Times New Roman", 18, false, true, false));
  first.runs.push_back(run(" under", "Sans", 11, false, false, true));
  writeit::Paragraph second;
  second.runs.push_back(run("caf\u00e9 {braces} \\slash", "Sans", 11, false, false, false));
  doc.paragraphs.push_back(first);
  doc.paragraphs.push_back(second);

  const std::string rtf = writeit::rtf_export(doc);
  CHECK(rtf.find("\\b ") != std::string::npos || rtf.find("\\b\\") != std::string::npos ||
        rtf.find("\\b ") != std::string::npos);
  CHECK(rtf.find("Times New Roman") != std::string::npos);
  CHECK(rtf.find("\\fs36") != std::string::npos);
  CHECK(rtf.find("\\ul") != std::string::npos);

  writeit::Document loaded;
  CHECK(writeit::rtf_import(rtf, loaded));
  CHECK(loaded == doc);

  const std::string wordish =
      "{\\rtf1\\ansi\\ansicpg1252{\\fonttbl{\\f0\\fswiss\\fcharset0 Sans;}}"
      "\\pard\\f0\\fs22 caf\\'e9\\par}";
  writeit::Document accent;
  CHECK(writeit::rtf_import(wordish, accent));
  CHECK(accent.paragraphs.size() == 1);
  CHECK(accent.paragraphs[0].runs.size() == 1);
  CHECK(accent.paragraphs[0].runs[0].text == "caf\u00e9");

  // A size past Word's 1638 pt (\fs3276) reads as 1638 pt, the largest the
  // toolbar's size box can show, rather than \fs4000's 2000 pt.
  for (const char* fs : {"\\fs4000", "\\fs3277", "\\fs3276", "\\fs999999999"}) {
    writeit::Document huge;
    CHECK(writeit::rtf_import(std::string("{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\f0") + fs +
                                  " Huge\\par}",
                              huge));
    CHECK(huge.paragraphs.size() == 1 && huge.paragraphs[0].runs.size() == 1 &&
          huge.paragraphs[0].runs[0].size == writeit::kMaxFontSize);
  }
  CHECK(writeit::kMaxFontSize == 1638);

  writeit::Document rejected;
  CHECK(!writeit::rtf_import("not rtf", rejected));

  const std::string markdown = "# Title\n\nA **bold** and *italic* word.\n\nNext";
  const writeit::Document from_md = writeit::markdown_import(markdown, "Sans", 11);
  CHECK(from_md.paragraphs.size() == 3);
  CHECK(from_md.paragraphs[0].heading == 1);
  CHECK(from_md.paragraphs[0].runs.size() == 1);
  CHECK(from_md.paragraphs[0].runs[0].text == "Title");
  CHECK(from_md.paragraphs[1].runs.size() == 5);
  CHECK(from_md.paragraphs[1].runs[1].text == "bold");
  CHECK(from_md.paragraphs[1].runs[1].bold);
  CHECK(!from_md.paragraphs[1].runs[1].italic);
  CHECK(from_md.paragraphs[1].runs[3].text == "italic");
  CHECK(from_md.paragraphs[1].runs[3].italic);
  CHECK(writeit::markdown_export(from_md) == markdown);

  const std::string both = "***both***";
  const writeit::Document both_doc = writeit::markdown_import(both, "Sans", 11);
  CHECK(both_doc.paragraphs[0].runs.size() == 1);
  CHECK(both_doc.paragraphs[0].runs[0].bold);
  CHECK(both_doc.paragraphs[0].runs[0].italic);
  CHECK(both_doc.paragraphs[0].runs[0].text == "both");
  CHECK(writeit::markdown_export(both_doc) == both);

  writeit::Document styled = from_md;
  styled.paragraphs[1].runs[1].underline = true;
  styled.paragraphs[1].runs[1].size = 24;
  styled.paragraphs[1].runs[1].font = "Times New Roman";
  const writeit::Document again = writeit::markdown_import(writeit::markdown_export(styled), "Sans", 11);
  CHECK(again.paragraphs[1].runs[1].text == "bold");
  CHECK(again.paragraphs[1].runs[1].bold);
  CHECK(!again.paragraphs[1].runs[1].underline);
  CHECK(again.paragraphs[1].runs[1].font == "Sans");
  CHECK(again.paragraphs[1].runs[1].size == 11);

  const writeit::Document plain = writeit::plain_import("one\n\ntwo\n- item\n", "Sans", 11);
  CHECK(plain.paragraphs.size() == 4);
  CHECK(plain.paragraphs[0].runs[0].text == "one");
  CHECK(plain.paragraphs[1].runs.empty());
  CHECK(plain.paragraphs[2].runs[0].text == "two");
  CHECK(plain.paragraphs[3].runs[0].text == "- item");
  CHECK(plain.paragraphs[3].heading == 0);

  const writeit::Document empty = writeit::plain_import("", "Sans", 11);
  CHECK(empty.paragraphs.size() == 1);
  CHECK(empty.paragraphs[0].runs.empty());

  writeit::Document heading;
  heading.paragraphs.push_back(writeit::Paragraph{});
  heading.paragraphs[0].heading = 2;
  heading.paragraphs[0].runs.push_back(run("Section", "Sans", 11, false, false, false));
  writeit::Document heading_rtf;
  CHECK(writeit::rtf_import(writeit::rtf_export(heading), heading_rtf));
  CHECK(heading_rtf.paragraphs[0].heading == 2);
  CHECK(heading_rtf.paragraphs[0].runs[0].text == "Section");

  return suite_test::done("document", kChecks);
}
