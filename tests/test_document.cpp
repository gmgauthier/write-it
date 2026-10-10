/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"
#include "font_sizes.hpp"

#include <string>

namespace {

writeit::Run run(const char* text, const char* font, double size, bool bold, bool italic, bool underline)
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

const std::string kHead = "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl{\\f0\\fswiss Sans;}}\n";

bool read_size(const std::string& fs, writeit::Document& doc)
{
  return writeit::rtf_import("{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\f0" + fs + " Text\\par}", doc) &&
         doc.paragraphs.size() == 1 && doc.paragraphs[0].runs.size() == 1;
}

bool has(const std::string& text, const std::string& part)
{
  return text.find(part) != std::string::npos;
}

// Bug Basher's live pass: \fs21 read as 10 pt and saved back as \fs20. Sizes
// are half points end to end now, so every \fsN Word can write comes back as
// itself, and junk is clamped to Word's 1 to 1638 pt as before.
void half_points()
{
  struct Case {
    const char* fs;
    double size;
    const char* written;
  };
  for (const Case& c : {Case{"\\fs21", 10.5, "\\fs21\\b0"}, Case{"\\fs17", 8.5, "\\fs17\\b0"},
                        Case{"\\fs1", 1, "\\fs2\\b0"}, Case{"\\fs0", 1, "\\fs2\\b0"},
                        Case{"\\fs-21", 1, "\\fs2\\b0"}, Case{"\\fs3275", 1637.5, "\\fs3275\\b0"},
                        Case{"\\fs3277", 1638, "\\fs3276\\b0"},
                        Case{"\\fs999999999", 1638, "\\fs3276\\b0"}}) {
    writeit::Document doc;
    CHECK(read_size(c.fs, doc) && doc.paragraphs[0].runs[0].size == c.size);
    const std::string out = writeit::rtf_export(doc);
    CHECK(has(out, c.written));
    writeit::Document again;
    CHECK(writeit::rtf_import(out, again) && again == doc);
  }
  // Every size in Word's range, \fs2 (1 pt) to \fs3276 (1638 pt).
  bool all = true;
  for (int half = 2; half <= 2 * writeit::kMaxFontSize; ++half) {
    const std::string fs = "\\fs" + std::to_string(half);
    writeit::Document doc;
    all = all && read_size(fs, doc) && doc.paragraphs[0].runs[0].size == half / 2.0 &&
          has(writeit::rtf_export(doc), fs + "\\b0");
  }
  CHECK(all);
  // A size set in the model (the size box's 10.5) is written as it is.
  writeit::Document typed;
  typed.paragraphs.emplace_back();
  typed.paragraphs[0].runs.push_back(run("Typed", "Sans", 10.5, false, false, false));
  CHECK(has(writeit::rtf_export(typed), "\\fs21\\b0\\i0\\ulnone Typed"));
  // A neighbour in another half point is its own run, not merged into 10 pt.
  writeit::Document mixed;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi{\\fonttbl{\\f0 Sans;}}\\f0\\fs20 ten\\fs21 half\\par}", mixed));
  CHECK(mixed.paragraphs.size() == 1 && mixed.paragraphs[0].runs.size() == 2 &&
        mixed.paragraphs[0].runs[0].size == 10 && mixed.paragraphs[0].runs[1].size == 10.5);
}

// An empty paragraph keeps its own character format, its paragraph mark's in
// Word: an empty 20 pt line between two others was written without \fs and
// reopened at the default size.
void empty_paragraphs()
{
  writeit::Document doc;
  doc.paragraphs.resize(3);
  doc.paragraphs[0].runs.push_back(run("Above", "Sans", 11, false, false, false));
  doc.paragraphs[1].mark = run("", "Sans", 20, false, false, false);
  doc.paragraphs[2].runs.push_back(run("Below", "Sans", 10.5, false, false, false));
  const std::string rtf = writeit::rtf_export(doc);
  CHECK(has(rtf, "\\pard\\f0\\fs40\\b0\\i0\\ulnone\\par\n"));
  writeit::Document back;
  CHECK(writeit::rtf_import(rtf, back));
  CHECK(back.paragraphs.size() == 3 && back.paragraphs[1].runs.empty() &&
        back.paragraphs[1].mark && back.paragraphs[1].mark->size == 20);
  CHECK(back == doc);

  // The other character properties go with it: font, bold, italic, underline.
  writeit::Document styled = doc;
  styled.paragraphs[1].mark = run("", "Times New Roman", 13.5, true, true, true);
  const std::string styled_rtf = writeit::rtf_export(styled);
  CHECK(has(styled_rtf, "{\\f1\\froman Times New Roman;}") &&
        has(styled_rtf, "\\pard\\f1\\fs27\\b\\i\\ul\\par\n"));
  writeit::Document styled_back;
  CHECK(writeit::rtf_import(styled_rtf, styled_back) && styled_back == styled);

  // An empty paragraph with no format of its own is written and read as before.
  writeit::Document bare = doc;
  bare.paragraphs[1].mark.reset();
  const std::string bare_rtf = writeit::rtf_export(bare);
  CHECK(has(bare_rtf, "Above\\par\n\\pard\\par\n\\pard"));
  writeit::Document bare_back;
  CHECK(writeit::rtf_import(bare_rtf, bare_back) && bare_back == bare &&
        !bare_back.paragraphs[1].mark);
  // Not the same document: the empty line's size is part of it.
  CHECK(!(bare == doc));
  // Text decides a paragraph's format; a mark beside text is not compared.
  writeit::Document text_mark = doc;
  text_mark.paragraphs[0].mark = run("", "Sans", 36, false, false, false);
  CHECK(text_mark == doc);

  // Word writes the size before the empty paragraph's \par, after \plain.
  writeit::Document word;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}}\\pard\\plain\\f0\\fs22 One\\par"
      "\\pard\\plain\\f0\\fs40\\b \\par\\pard\\plain\\f0\\fs22 Two\\par}",
      word));
  CHECK(word.paragraphs.size() == 3 && word.paragraphs[1].runs.empty() &&
        word.paragraphs[1].mark && word.paragraphs[1].mark->size == 20 &&
        word.paragraphs[1].mark->bold && word.paragraphs[1].mark->font == "Arial");

  // An empty list item's label takes the item's own size.
  writeit::Document list = doc;
  list.paragraphs[1].list = writeit::ListFormat{writeit::ListKind::Bullet, 0};
  list.paragraphs[1].indents = writeit::list_indents(0);
  const std::string list_rtf = writeit::rtf_export(list);
  CHECK(has(list_rtf, "{\\pntext\\f0\\fs40 "));
  writeit::Document list_back;
  CHECK(writeit::rtf_import(list_rtf, list_back) && list_back == list);
}

// Open, then save without an edit: the bytes come back as they were, half
// points and empty lines included.
void byte_round_trip()
{
  const std::string file = kHead +
                           "\\pard\\f0\\fs21\\b0\\i0\\ulnone Half\\par\n"
                           "\\pard\\f0\\fs40\\b0\\i0\\ulnone\\par\n"
                           "\\pard\\f0\\fs17\\b0\\i0\\ulnone Small\\fs1 tiny\\fs3276 huge\\par\n"
                           "\\pard\\par\n"
                           "\\pard\\f0\\fs27\\b\\i\\ul\\par\n"
                           "}";
  writeit::Document doc;
  CHECK(writeit::rtf_import(file, doc));
  // \fs1 is clamped to 1 pt (\fs2), Word's smallest: only that part changes.
  std::string expected = file;
  expected.replace(expected.find("\\fs1 tiny"), 9, "\\fs2 tiny");
  CHECK(writeit::rtf_export(doc) == expected);
  writeit::Document again;
  CHECK(writeit::rtf_import(expected, again) && writeit::rtf_export(again) == expected);
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 97;

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

  half_points();
  empty_paragraphs();
  byte_round_trip();

  return suite_test::done("document", kChecks);
}
