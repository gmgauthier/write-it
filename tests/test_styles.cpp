/* SPDX-License-Identifier: Unlicense */

// M2 named styles: the built-in sheet, applying a style and editing one
// (attribute by attribute, so direct formatting survives), RTF's \stylesheet,
// \s, \sbasedon and \snext written and read, the malformed style sheets a
// hostile file can carry, and Markdown's headings.

#include "check.hpp"
#include "document.hpp"

#include <string>
#include <vector>

namespace {

using writeit::Align;
using writeit::Document;
using writeit::Indents;
using writeit::ListFormat;
using writeit::ListKind;
using writeit::Paragraph;
using writeit::Run;
using writeit::Style;

Run run(const std::string& text, const std::string& font = "Sans", int size = 11, bool bold = false,
        bool italic = false, bool underline = false)
{
  Run r;
  r.text = text;
  r.font = font;
  r.size = size;
  r.bold = bold;
  r.italic = italic;
  r.underline = underline;
  return r;
}

Paragraph para(const std::string& text, const std::string& style = "Normal")
{
  Paragraph p;
  p.style = style;
  if (!text.empty())
    p.runs.push_back(run(text));
  return p;
}

std::string text_of(const Paragraph& paragraph)
{
  std::string out;
  for (const Run& r : paragraph.runs)
    out += r.text;
  return out;
}

bool contains(const std::string& haystack, const std::string& needle)
{
  return haystack.find(needle) != std::string::npos;
}

Document import(const std::string& rtf)
{
  Document doc;
  CHECK(writeit::rtf_import(rtf, doc));
  return doc;
}

bool valid_utf8(const std::string& s)
{
  size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    size_t len = c < 0x80             ? 1
                 : (c & 0xE0) == 0xC0 ? 2
                 : (c & 0xF0) == 0xE0 ? 3
                 : (c & 0xF8) == 0xF0 ? 4
                                      : 0;
    if (len == 0 || i + len > s.size())
      return false;
    for (size_t k = 1; k < len; ++k) {
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
        return false;
    }
    if (c < 0x20)
      return false;
    i += len;
  }
  return true;
}

const char* kHead = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Sans;}{\\f1\\froman Serif;}}";

// A document in the built-in sheet with a heading, body text, and a quote.
Document sample()
{
  Document doc;
  doc.styles = writeit::builtin_styles("Sans", 11);
  doc.paragraphs.push_back(para("Title"));
  doc.paragraphs.push_back(para("Body text"));
  CHECK(writeit::apply_style(doc, 0, 0, "Heading 1"));
  return doc;
}

void builtins()
{
  const std::vector<Style> sheet = writeit::builtin_styles("Serif", 12);
  const std::vector<std::string> names = {"Normal",    "Heading 1",  "Heading 2",
                                          "Heading 3", "Heading 4",  "Heading 5",
                                          "Heading 6", "Block Text", "Plain Text"};
  CHECK(sheet.size() == names.size());
  for (size_t i = 0; i < names.size() && i < sheet.size(); ++i)
    CHECK(sheet[i].name == names[i]);
  if (sheet.size() != names.size())
    return;
  const Style& normal = sheet[0];
  CHECK(normal.format.font == "Serif" && normal.format.size == 12 && !normal.format.bold);
  CHECK(normal.based_on.empty() && normal.heading == 0 && normal.indents == Indents{});
  for (int level = 1; level <= 6; ++level) {
    const Style& h = sheet[static_cast<size_t>(level)];
    CHECK(h.heading == level);
    CHECK(h.based_on == "Normal" && h.next == "Normal");
    CHECK(h.format.font == "Serif");
    CHECK(h.format.size >= 12);
  }
  // Each level is at least as large as the next, and Heading 1 stands out.
  for (size_t level = 1; level < 6; ++level)
    CHECK(sheet[level].format.size >= sheet[level + 1].format.size);
  CHECK(sheet[1].format.size > 12 && sheet[1].format.bold);
  CHECK(sheet[7].indents.left == 1440 && sheet[7].indents.right == 1440);
  CHECK(sheet[8].format.font == "Monospace" && sheet[8].format.size == 11);

  // A document without a sheet of its own uses the built-in one in Sans 11.
  const Document empty;
  CHECK(writeit::style_sheet(empty) == writeit::builtin_styles("Sans", 11));
  CHECK(Paragraph{}.style == "Normal");
  CHECK(writeit::blank_document("Serif", 12).styles == writeit::builtin_styles("Serif", 12));
  CHECK(writeit::plain_import("a\nb", "Serif", 12).styles == writeit::builtin_styles("Serif", 12));

  // Names: found ignoring case, cleaned, and made unique.
  CHECK(writeit::find_style(sheet, "heading 2") == &sheet[2]);
  CHECK(writeit::find_style(sheet, "Nope") == nullptr);
  CHECK(writeit::clean_style_name("  Quote\t\x01 ") == "Quote");
  CHECK(writeit::clean_style_name("\x01\x02").empty());
  CHECK(writeit::clean_style_name(std::string(1000, 'x')).size() == writeit::kMaxStyleName);
  // A cut never splits a character.
  const std::string long_e = writeit::clean_style_name(std::string(400, 'a') + "\xC3\xA9");
  CHECK(valid_utf8(writeit::clean_style_name(std::string(254, 'a') + "\xC3\xA9")));
  CHECK(long_e.size() <= writeit::kMaxStyleName);
  CHECK(writeit::unique_style_name(sheet, "Quote") == "Quote");
  CHECK(writeit::unique_style_name(sheet, "heading 1") == "heading 1 (2)");
  CHECK(writeit::next_style(sheet, "Heading 3") == "Normal");
  CHECK(writeit::next_style(sheet, "Normal") == "Normal");
  CHECK(writeit::next_style(sheet, "Nope") == "Nope");

  // A sheet missing built-ins gets them, in the size of its Normal.
  std::vector<Style> partial(1, sheet[0]);
  partial[0].format.size = 10;
  const std::vector<Style> completed = writeit::complete_sheet(partial);
  CHECK(completed.size() == names.size());
  CHECK(completed[0].format.size == 10);
  const Style* h1 = writeit::find_style(completed, "Heading 1");
  CHECK(h1 != nullptr && h1->format.size > 10 && h1->heading == 1);
  // A sheet without Normal gets one, first.
  std::vector<Style> no_normal(1, sheet[7]);
  const std::vector<Style> fixed = writeit::complete_sheet(no_normal);
  CHECK(!fixed.empty() && fixed[0].name == "Normal");
}

void applying()
{
  Document doc;
  doc.styles = writeit::builtin_styles("Sans", 11);
  Paragraph p = para("plain ");
  p.runs.push_back(run("italic", "Sans", 11, false, true));
  p.runs.push_back(run("big", "Sans", 20));
  doc.paragraphs.push_back(p);
  doc.paragraphs.push_back(para("other"));
  const Style h1 = *writeit::find_style(doc.styles, "Heading 1");

  CHECK(writeit::apply_style(doc, 0, 0, "Heading 1"));
  const Paragraph& a = doc.paragraphs[0];
  CHECK(a.style == "Heading 1");
  CHECK(a.heading == 1);
  // Runs in Normal's format take Heading 1's; direct formatting stays.
  CHECK(a.runs.size() == 3);
  if (a.runs.size() == 3) {
    CHECK(a.runs[0].size == h1.format.size && a.runs[0].bold);
    CHECK(a.runs[1].size == h1.format.size && a.runs[1].bold && a.runs[1].italic);
    CHECK(a.runs[2].size == 20 && a.runs[2].bold);
  }
  // Only the paragraphs asked for change.
  CHECK(doc.paragraphs[1].style == "Normal" && doc.paragraphs[1].runs[0].size == 11);

  // And back again.
  CHECK(writeit::apply_style(doc, 0, 0, "Normal"));
  CHECK(doc.paragraphs[0].style == "Normal" && doc.paragraphs[0].heading == 0);
  CHECK(doc.paragraphs[0].runs.size() == 3);
  if (doc.paragraphs[0].runs.size() == 3) {
    CHECK(doc.paragraphs[0].runs[0].size == 11 && !doc.paragraphs[0].runs[0].bold);
    CHECK(doc.paragraphs[0].runs[1].italic && !doc.paragraphs[0].runs[1].bold);
    CHECK(doc.paragraphs[0].runs[2].size == 20);
  }

  // Paragraph format: indents and alignment, field by field.
  Document block;
  block.styles = writeit::builtin_styles("Sans", 11);
  block.paragraphs.push_back(para("a"));
  block.paragraphs.push_back(para("b"));
  block.paragraphs[1].indents = Indents{300, 0, 0};
  CHECK(writeit::apply_style(block, 0, 1, "Block Text"));
  CHECK(block.paragraphs[0].indents == (Indents{1440, 1440, 0}));
  CHECK(block.paragraphs[1].indents == (Indents{300, 1440, 0}));
  Style centred;
  centred.name = "Centred";
  centred.based_on = "Normal";
  centred.format = run("", "Sans", 11);
  centred.align = Align::Center;
  CHECK(writeit::add_style(block, centred));
  CHECK(writeit::apply_style(block, 0, 0, "Centred"));
  CHECK(block.paragraphs[0].align == Align::Center);
  CHECK(block.paragraphs[0].indents == Indents{});

  // A list item keeps its list indents; the indents it gets back on leaving
  // the list take the style instead.
  Document list;
  list.styles = writeit::builtin_styles("Sans", 11);
  list.paragraphs.push_back(para("item"));
  writeit::toggle_list(list.paragraphs, ListKind::Bullet);
  const Indents item = list.paragraphs[0].indents;
  CHECK(writeit::apply_style(list, 0, 0, "Block Text"));
  CHECK(list.paragraphs[0].indents == item);
  CHECK(list.paragraphs[0].list.kind == ListKind::Bullet);
  writeit::toggle_list(list.paragraphs, ListKind::Bullet);
  CHECK(list.paragraphs[0].indents == (Indents{1440, 1440, 0}));

  // An empty paragraph takes the style too.
  Document empty;
  empty.paragraphs.push_back(Paragraph{});
  CHECK(writeit::apply_style(empty, 0, 0, "Heading 2"));
  CHECK(empty.paragraphs[0].style == "Heading 2" && empty.paragraphs[0].heading == 2);

  // Unknown styles and out-of-range paragraphs change nothing.
  Document before = sample();
  Document same = before;
  CHECK(!writeit::apply_style(same, 0, 0, "Nope"));
  CHECK(same == before);
  CHECK(writeit::apply_style(same, 1, 99, "Heading 2"));
  CHECK(same.paragraphs[1].style == "Heading 2");
  CHECK(!writeit::apply_style(same, 5, 9, "Heading 2"));
  CHECK(!writeit::apply_style(same, 1, 0, "Heading 2"));
}

void editing()
{
  Document doc = sample();
  doc.paragraphs.push_back(para("Second"));
  CHECK(writeit::apply_style(doc, 2, 2, "Heading 1"));
  doc.paragraphs[2].runs[0].size = 30;  // direct formatting
  doc.paragraphs.push_back(para("Sub"));
  CHECK(writeit::apply_style(doc, 3, 3, "Heading 2"));

  // Heading 1 grows: its paragraphs follow, the direct 30 stays.
  Style h1 = *writeit::find_style(doc.styles, "Heading 1");
  h1.format.size = 22;
  h1.format.italic = true;
  CHECK(writeit::update_style(doc, "Heading 1", h1));
  CHECK(writeit::find_style(doc.styles, "Heading 1")->format.size == 22);
  CHECK(doc.paragraphs[0].runs[0].size == 22 && doc.paragraphs[0].runs[0].italic);
  CHECK(doc.paragraphs[2].runs[0].size == 30 && doc.paragraphs[2].runs[0].italic);
  CHECK(doc.paragraphs[1].runs[0].size == 11 && !doc.paragraphs[1].runs[0].italic);

  // Normal's font changes: Normal's paragraphs and every style based on it
  // that shares the font follow.
  Style normal = *writeit::find_style(doc.styles, "Normal");
  normal.format.font = "Serif";
  CHECK(writeit::update_style(doc, "Normal", normal));
  CHECK(doc.paragraphs[1].runs[0].font == "Serif");
  CHECK(writeit::find_style(doc.styles, "Heading 2")->format.font == "Serif");
  CHECK(doc.paragraphs[3].runs[0].font == "Serif");
  CHECK(doc.paragraphs[0].runs[0].font == "Serif");

  // Paragraph format carries too.
  Style block = *writeit::find_style(doc.styles, "Block Text");
  CHECK(writeit::apply_style(doc, 1, 1, "Block Text"));
  block.indents.left = 2000;
  block.align = Align::Right;
  CHECK(writeit::update_style(doc, "Block Text", block));
  CHECK(doc.paragraphs[1].indents.left == 2000 && doc.paragraphs[1].align == Align::Right);

  // Renaming a style carries to its paragraphs and to the styles naming it.
  Style quote;
  quote.name = "Quote";
  quote.based_on = "Normal";
  quote.next = "Quote";
  quote.format = run("", "Serif", 11, false, true);
  CHECK(writeit::add_style(doc, quote));
  Style child = quote;
  child.name = "Quote Small";
  child.based_on = "Quote";
  child.next = "Quote";
  child.format.size = 9;
  CHECK(writeit::add_style(doc, child));
  CHECK(writeit::apply_style(doc, 1, 1, "Quote"));
  Style renamed = *writeit::find_style(doc.styles, "Quote");
  renamed.name = "Citation";
  CHECK(writeit::update_style(doc, "Quote", renamed));
  CHECK(doc.paragraphs[1].style == "Citation");
  CHECK(writeit::find_style(doc.styles, "Quote") == nullptr);
  const Style* small = writeit::find_style(doc.styles, "Quote Small");
  CHECK(small != nullptr && small->based_on == "Citation" && small->next == "Citation");
  CHECK(writeit::find_style(doc.styles, "Citation")->next == "Citation");

  // Refusals change nothing.
  const Document before = doc;
  Style bad = *writeit::find_style(doc.styles, "Normal");
  bad.name = "Body";
  CHECK(!writeit::update_style(doc, "Normal", bad));  // Normal keeps its name
  bad = *writeit::find_style(doc.styles, "Heading 2");
  bad.name = "heading 1";
  CHECK(!writeit::update_style(doc, "Heading 2", bad));  // taken, ignoring case
  bad.name = "";
  CHECK(!writeit::update_style(doc, "Heading 2", bad));
  bad = *writeit::find_style(doc.styles, "Normal");
  bad.based_on = "Heading 1";  // Heading 1 is based on Normal: a circle
  CHECK(!writeit::update_style(doc, "Normal", bad));
  bad = *writeit::find_style(doc.styles, "Heading 3");
  bad.based_on = "Heading 3";
  CHECK(!writeit::update_style(doc, "Heading 3", bad));
  bad.based_on = "Nope";
  CHECK(!writeit::update_style(doc, "Heading 3", bad));
  bad = *writeit::find_style(doc.styles, "Heading 3");
  bad.next = "Nope";
  CHECK(!writeit::update_style(doc, "Heading 3", bad));
  CHECK(!writeit::update_style(doc, "Nope", bad));
  CHECK(doc == before);
  Style dup = quote;
  dup.name = "CITATION";
  CHECK(!writeit::add_style(doc, dup));
  dup.name = "\x01";
  CHECK(!writeit::add_style(doc, dup));
  dup.name = "Fine";
  dup.based_on = "Nope";
  CHECK(!writeit::add_style(doc, dup));
  CHECK(doc == before);

  // A document with no sheet of its own takes the built-in one on its first
  // style edit.
  Document bare;
  bare.paragraphs.push_back(para("x"));
  Style h2 = *writeit::find_style(writeit::style_sheet(bare), "Heading 2");
  h2.format.size = 40;
  CHECK(writeit::update_style(bare, "Heading 2", h2));
  CHECK(bare.styles.size() == writeit::builtin_styles("Sans", 11).size());

  // The sheet and the paragraphs' styles are part of the document.
  Document a = sample();
  Document b = sample();
  CHECK(a == b);
  b.paragraphs[1].style = "Heading 2";
  CHECK(!(a == b));
  b = a;
  b.styles[1].format.size = 99;
  CHECK(!(a == b));
  Document c;
  Document d;
  d.styles = writeit::builtin_styles("Sans", 11);
  CHECK(c == d);
}

void rtf_write()
{
  // No styles beyond Normal in the built-in sheet: no \stylesheet, as before.
  Document plain;
  plain.paragraphs.push_back(para("x"));
  const std::string bare = writeit::rtf_export(plain);
  CHECK(!contains(bare, "stylesheet"));
  CHECK(!contains(bare, "\\s0"));
  plain.styles = writeit::builtin_styles("Sans", 11);
  CHECK(writeit::rtf_export(plain) == bare);

  const std::string rtf = writeit::rtf_export(sample());
  CHECK(contains(rtf, "{\\stylesheet"));
  CHECK(contains(rtf, "Normal;}"));
  CHECK(contains(rtf, "{\\s1\\sbasedon0\\snext0"));
  CHECK(contains(rtf, "Heading 1;}"));
  CHECK(contains(rtf, "\\pard\\s1"));
  CHECK(contains(rtf, "\\outlinelevel0"));
  CHECK(contains(rtf, "Plain Text;}"));
  // The style sheet comes after the font table, which names its fonts.
  CHECK(rtf.find("{\\stylesheet") > rtf.find("{\\fonttbl"));
  CHECK(contains(rtf, "Monospace;}"));

  // A heading style with no outline level on the paragraph says so.
  Document flat = sample();
  flat.paragraphs[0].heading = 0;
  CHECK(contains(writeit::rtf_export(flat), "\\outlinelevel9"));

  // A ';' or brace in a name cannot end the entry early.
  Document odd = sample();
  Style semi;
  semi.name = "A;B {x}";
  semi.based_on = "Normal";
  semi.format = run("");
  CHECK(writeit::add_style(odd, semi));
  CHECK(writeit::apply_style(odd, 1, 1, "A;B {x}"));
  const Document back = import(writeit::rtf_export(odd));
  CHECK(back.paragraphs.size() == 2 && back.paragraphs[1].style == "A;B {x}");
  CHECK(text_of(back.paragraphs[1]) == "Body text");
}

void rtf_round_trip()
{
  Document doc = sample();
  Style quote;
  quote.name = "Quote";
  quote.based_on = "Normal";
  quote.next = "Normal";
  quote.format = run("", "Serif", 12, false, true, true);
  quote.indents = Indents{720, 360, -360};
  quote.align = Align::Center;
  CHECK(writeit::add_style(doc, quote));
  Style sub = quote;
  sub.name = "Quote Sub";
  sub.based_on = "Quote";
  sub.next = "";
  sub.format.size = 10;
  sub.heading = 3;
  CHECK(writeit::add_style(doc, sub));
  doc.paragraphs.push_back(para("Said"));
  CHECK(writeit::apply_style(doc, 2, 2, "Quote"));
  doc.paragraphs.push_back(para("Smaller"));
  CHECK(writeit::apply_style(doc, 3, 3, "Quote Sub"));
  // Direct formatting over a style.
  doc.paragraphs[0].runs.push_back(run(" big", "Sans", 30, true));
  doc.paragraphs[3].align = Align::Right;
  // A list item in a heading style.
  doc.paragraphs.push_back(para("Point"));
  CHECK(writeit::apply_style(doc, 4, 4, "Heading 2"));
  std::vector<Paragraph> one(1, doc.paragraphs[4]);
  writeit::toggle_list(one, ListKind::Number);
  doc.paragraphs[4] = one[0];
  doc.paragraphs[4].list.has_own = false;
  // An empty paragraph in a style.
  doc.paragraphs.push_back(Paragraph{});
  CHECK(writeit::apply_style(doc, 5, 5, "Heading 3"));

  const std::string rtf = writeit::rtf_export(doc);
  const Document back = import(rtf);
  CHECK(back.paragraphs.size() == doc.paragraphs.size());
  CHECK(back.styles == doc.styles);
  for (size_t i = 0; i < doc.paragraphs.size() && i < back.paragraphs.size(); ++i) {
    CHECK(back.paragraphs[i].style == doc.paragraphs[i].style);
    CHECK(back.paragraphs[i].heading == doc.paragraphs[i].heading);
  }
  CHECK(back == doc);
  CHECK(writeit::rtf_export(back) == rtf);

  // A sheet in another font and size round-trips too.
  const Document serif = writeit::blank_document("Serif", 12);
  CHECK(contains(writeit::rtf_export(serif), "{\\stylesheet"));
  CHECK(import(writeit::rtf_export(serif)) == serif);

  // A style without something its base has (found by fuzzing): the base
  // here is bold, centred, indented and a heading; the child is none of them.
  {
    Document off = writeit::blank_document("Sans", 11);
    Style base;
    base.name = "Loud";
    base.based_on = "Normal";
    base.format.font = "Sans";
    base.format.size = 11;
    base.format.bold = true;
    base.format.italic = true;
    base.format.underline = true;
    base.indents = Indents{720, 360, -360};
    base.align = Align::Center;
    base.heading = 2;
    Style quiet;
    quiet.name = "Quiet";
    quiet.based_on = "Loud";
    quiet.format.font = "Sans";
    quiet.format.size = 11;
    CHECK(writeit::add_style(off, base));
    CHECK(writeit::add_style(off, quiet));
    const Document read = import(writeit::rtf_export(off));
    const Style* q = writeit::find_style(read.styles, "Quiet");
    CHECK(q != nullptr && !q->format.bold && !q->format.italic && !q->format.underline);
    CHECK(q != nullptr && q->indents == Indents{} && q->align == Align::Left && q->heading == 0);
    CHECK(read == off);
    // A paragraph without what its style has: left in a centred, indented
    // Normal (also found by fuzzing).
    Document plain = writeit::blank_document("Sans", 11);
    Style centred = plain.styles.front();
    centred.align = Align::Center;
    centred.indents = Indents{720, 0, 0};
    CHECK(writeit::update_style(plain, "Normal", centred));
    plain.paragraphs[0].align = Align::Left;
    plain.paragraphs[0].indents = Indents{};
    const Document plain_back = import(writeit::rtf_export(plain));
    CHECK(plain_back == plain);
    // A huge Normal (fuzzing again): style sizes stop at Word's largest, so
    // the headings built from it still round-trip.
    const Document huge =
        import(std::string(kHead) + "{\\stylesheet{\\fs999999999 Normal;}}\\pard x\\par}");
    CHECK(!huge.styles.empty() && huge.styles[0].format.size == writeit::kMaxStyleSize);
    const Style* huge_h1 = writeit::find_style(huge.styles, "Heading 1");
    CHECK(huge_h1 != nullptr && huge_h1->format.size == writeit::kMaxStyleSize);
    CHECK(import(writeit::rtf_export(huge)) == huge);
    // "Heading 1" made body text stays body text, though its name says
    // otherwise to a reader.
    const Document unheaded =
        import(std::string(kHead) +
               "{\\stylesheet{Normal;}{\\s1\\outlinelevel9 Heading 1;}}\\pard x\\par}");
    const Style* unheaded_h1 = writeit::find_style(unheaded.styles, "Heading 1");
    CHECK(unheaded_h1 != nullptr && unheaded_h1->heading == 0);
    CHECK(import(writeit::rtf_export(unheaded)) == unheaded);
  }

  // A file without a style sheet reads as before: no sheet, all Normal.
  const Document old = import(std::string(kHead) + "\\pard\\outlinelevel1 Sec\\par\\pard x\\par}");
  CHECK(old.styles.empty());
  CHECK(old.paragraphs.size() == 2 && old.paragraphs[0].style == "Normal");
  CHECK(old.paragraphs[0].heading == 2);
}

void rtf_read()
{
  // Word 97: complete definitions, lower-case built-in names, a character
  // style, keycodes, and \plain before \s.
  const Document word =
      import(std::string(kHead) +
             "{\\stylesheet{\\ql \\li0\\ri0\\widctlpar\\f0\\fs24 \\snext0 Normal;}"
             "{\\s1\\ql \\keepn\\outlinelevel0\\b\\f1\\fs32 \\sbasedon0 \\snext0 heading 1;}"
             "{\\s2\\ql \\li1440\\ri1440\\f0\\fs24 \\sbasedon0 \\snext2 {\\*\\keycode "
             "\\shift\\ctrl b}Block Text;}"
             "{\\*\\cs10 \\additive Default Paragraph Font;}}"
             "\\pard\\plain \\s1\\b\\f1\\fs32 Title\\par"
             "\\pard\\plain \\s2\\li1440\\ri1440\\f0\\fs24 Quoted\\par"
             "\\pard\\plain \\f0\\fs24 Body {\\cs10 text}\\par}");
  CHECK(word.paragraphs.size() == 3);
  if (word.paragraphs.size() == 3) {
    CHECK(word.paragraphs[0].style == "Heading 1" && word.paragraphs[0].heading == 1);
    CHECK(word.paragraphs[1].style == "Block Text");
    CHECK(word.paragraphs[2].style == "Normal");
    CHECK(text_of(word.paragraphs[2]) == "Body text");
  }
  const Style* normal = writeit::find_style(word.styles, "Normal");
  CHECK(normal != nullptr && normal->format.size == 12);
  const Style* h1 = writeit::find_style(word.styles, "Heading 1");
  CHECK(h1 != nullptr && h1->name == "Heading 1" && h1->format.bold && h1->format.size == 16);
  CHECK(h1 != nullptr && h1->format.font == "Serif" && h1->heading == 1);
  CHECK(h1 != nullptr && h1->based_on == "Normal" && h1->next == "Normal");
  const Style* block = writeit::find_style(word.styles, "Block Text");
  CHECK(block != nullptr && writeit::next_style(word.styles, "Block Text") == "Block Text" &&
        block->indents.left == 1440);
  CHECK(writeit::find_style(word.styles, "Default Paragraph Font") == nullptr);
  // The built-ins the file lacks are there, and Normal is first.
  CHECK(!word.styles.empty() && word.styles[0].name == "Normal");
  CHECK(writeit::find_style(word.styles, "Heading 6") != nullptr);
  CHECK(writeit::find_style(word.styles, "Plain Text") != nullptr);

  // LibreOffice: definitions that only list what differs from the base,
  // with the base defined later in the file.
  const Document lo = import(std::string(kHead) +
                             "{\\stylesheet{\\s0\\snext0\\f0\\fs22 Default Paragraph Style;}"
                             "{\\s5\\sbasedon6\\snext0\\i Child;}"
                             "{\\s6\\sbasedon0\\snext0\\b\\fs40\\qc Parent;}}"
                             "\\pard\\plain\\s5 a\\par}");
  const Style* child = writeit::find_style(lo.styles, "Child");
  CHECK(child != nullptr && child->format.bold && child->format.italic);
  CHECK(child != nullptr && child->format.size == 20 && child->align == Align::Center);
  CHECK(child != nullptr && child->based_on == "Parent");
  // \s0 is Normal, whatever the file calls it.
  CHECK(!lo.styles.empty() && lo.styles[0].name == "Normal");
  CHECK(writeit::find_style(lo.styles, "Default Paragraph Style") == nullptr);

  // \s on its own applies the style; \pard goes back to Normal.
  const Document bare = import(std::string(kHead) +
                               "{\\stylesheet{\\fs22 Normal;}"
                               "{\\s1\\b\\fs32\\li720\\qc\\outlinelevel0 heading 1;}}"
                               "\\pard\\s1 Title\\par\\pard Body\\par}");
  CHECK(bare.paragraphs.size() == 2);
  if (bare.paragraphs.size() == 2) {
    const Paragraph& title = bare.paragraphs[0];
    CHECK(!title.runs.empty() && title.runs[0].bold && title.runs[0].size == 16);
    CHECK(title.indents.left == 720 && title.align == Align::Center && title.heading == 1);
    const Paragraph& body = bare.paragraphs[1];
    CHECK(!body.runs.empty() && !body.runs[0].bold && body.runs[0].size == 11);
    CHECK(body.indents == Indents{} && body.align == Align::Left && body.heading == 0);
    CHECK(body.style == "Normal");
  }
  // Direct formatting after \s wins, and outline level 9 is body text.
  const Document direct =
      import(std::string(kHead) +
             "{\\stylesheet{\\fs22 Normal;}{\\s1\\b\\fs32\\outlinelevel0 heading 1;}}"
             "\\pard\\s1\\b0\\fs20\\outlinelevel9 x\\par}");
  CHECK(!direct.paragraphs.empty() && !direct.paragraphs[0].runs.empty());
  if (!direct.paragraphs.empty() && !direct.paragraphs[0].runs.empty()) {
    CHECK(!direct.paragraphs[0].runs[0].bold && direct.paragraphs[0].runs[0].size == 10);
    CHECK(direct.paragraphs[0].heading == 0 && direct.paragraphs[0].style == "Heading 1");
  }
  // A heading style's name gives the outline level when the file has none.
  const Document named = import(std::string(kHead) +
                                "{\\stylesheet{\\fs22 Normal;}{\\s3\\b heading 3;}}"
                                "\\pard\\s3 x\\par}");
  CHECK(!named.paragraphs.empty() && named.paragraphs[0].heading == 3);

  // Styles and lists on one paragraph.
  const Document both =
      import(std::string(kHead) + "{\\stylesheet{\\fs22 Normal;}{\\s2\\fs28 heading 2;}}" +
             "{\\*\\listtable{\\list{\\listlevel\\levelnfc23}{\\listlevel\\levelnfc23}"
             "\\listid7}}{\\*\\listoverridetable{\\listoverride\\listid7\\ls1}}"
             "\\pard\\s2\\ls1\\ilvl1 item\\par}");
  CHECK(!both.paragraphs.empty());
  if (!both.paragraphs.empty()) {
    CHECK(both.paragraphs[0].style == "Heading 2");
    CHECK(both.paragraphs[0].list.kind == ListKind::Bullet && both.paragraphs[0].list.level == 1);
    CHECK(both.paragraphs[0].indents == writeit::list_indents(1));
  }
}

void rtf_hostile()
{
  const std::string sheet = "{\\stylesheet{\\fs22 Normal;}{\\s1\\b Strong;}}";
  // Unknown, huge, and negative style numbers read as Normal.
  for (const char* word : {"\\s99", "\\s99999999999999", "\\s-5", "\\s"}) {
    const Document doc = import(std::string(kHead) + sheet + "\\pard" + word + " x\\par}");
    CHECK(doc.paragraphs.size() == 1 && doc.paragraphs[0].style == "Normal");
    CHECK(text_of(doc.paragraphs[0]) == "x");
  }
  // \s without a style sheet at all.
  const Document none = import(std::string(kHead) + "\\pard\\s3 x\\par}");
  CHECK(none.paragraphs.size() == 1 && none.paragraphs[0].style == "Normal");

  // Circles of \sbasedon, and a style based on itself, end.
  const Document circle = import(std::string(kHead) +
                                 "{\\stylesheet{\\fs22 Normal;}"
                                 "{\\s1\\sbasedon2\\b A;}{\\s2\\sbasedon3\\i B;}"
                                 "{\\s3\\sbasedon1\\ul C;}{\\s4\\sbasedon4 D;}"
                                 "{\\s5\\sbasedon99999999\\snext-3 E;}}"
                                 "\\pard\\s1 x\\par}");
  const Style* a = writeit::find_style(circle.styles, "A");
  const Style* b = writeit::find_style(circle.styles, "B");
  const Style* c = writeit::find_style(circle.styles, "C");
  const Style* d = writeit::find_style(circle.styles, "D");
  const Style* e = writeit::find_style(circle.styles, "E");
  CHECK(a && b && c && d && e);
  if (a && b && c && d && e) {
    CHECK(a->based_on.empty() || b->based_on.empty() || c->based_on.empty());
    CHECK(d->based_on.empty());
    CHECK(e->based_on.empty() && e->next.empty());
    CHECK(a->format.bold);
  }
  // The resulting sheet has no circle either: walking up from any style ends.
  for (const Style& s : circle.styles) {
    std::string at = s.name;
    int steps = 0;
    while (!at.empty() && steps < 100) {
      const Style* up = writeit::find_style(circle.styles, at);
      at = up ? up->based_on : "";
      ++steps;
    }
    CHECK(steps < 100);
  }

  // Duplicate names, a second Normal, a duplicate number, empty and dirty
  // names, and a name that is far too long.
  const Document names = import(std::string(kHead) +
                                "{\\stylesheet{\\fs22 Normal;}{\\s1 Quote;}{\\s2 quote;}"
                                "{\\s3 normal;}{\\s4\\b First;}{\\s4\\i Second;}{\\s5 ;}"
                                "{\\s6 Caf\\'e9 \\u8364?\x01x;}{\\s7 " +
                                std::string(5000, 'L') + ";}}\\pard\\s4 x\\par}");
  CHECK(writeit::find_style(names.styles, "Quote") != nullptr);
  CHECK(writeit::find_style(names.styles, "quote (2)") != nullptr);
  CHECK(writeit::find_style(names.styles, "normal (2)") != nullptr);
  int normals = 0;
  for (const Style& s : names.styles) {
    if (s.name == "Normal")
      ++normals;
    CHECK(!s.name.empty());
    CHECK(s.name.size() <= writeit::kMaxStyleName);
    CHECK(valid_utf8(s.name));
  }
  CHECK(normals == 1);
  CHECK(writeit::find_style(names.styles, "Style 5") != nullptr);
  CHECK(writeit::find_style(names.styles, "Caf\xC3\xA9 \xE2\x82\xACx") != nullptr);
  CHECK(!names.paragraphs.empty() && names.paragraphs[0].style == "First");
  CHECK(writeit::find_style(names.styles, "Second") == nullptr);
  // Names are unique ignoring case.
  for (size_t i = 0; i < names.styles.size(); ++i) {
    for (size_t j = i + 1; j < names.styles.size(); ++j)
      CHECK(writeit::find_style(names.styles, names.styles[j].name) == &names.styles[j]);
  }

  // Unclosed groups: the file ends inside an entry, inside the sheet, or
  // inside a keycode. Nothing of the sheet leaks into the text.
  for (const std::string& tail :
       {std::string("{\\stylesheet{\\s1\\b Unfinished"), std::string("{\\stylesheet{\\s1 A;}"),
        std::string("{\\stylesheet{\\s1 A;}{\\s2{\\*\\keycode \\shift"),
        std::string("{\\stylesheet{\\s1 A;}}\\pard\\s1 x"),
        std::string("{\\stylesheet}}\\pard\\s0 y\\par}"), std::string("{\\stylesheet{{{{{")}) {
    Document doc;
    CHECK(writeit::rtf_import(std::string(kHead) + tail, doc));
    for (const Paragraph& p : doc.paragraphs) {
      CHECK(!contains(text_of(p), "Unfinished"));
      CHECK(!contains(text_of(p), "A;"));
    }
  }
  const Document after = import(std::string(kHead) + "{\\stylesheet{\\s1 A;}}\\pard\\s1 x");
  CHECK(!after.paragraphs.empty() && text_of(after.paragraphs.back()) == "x");
  CHECK(!after.paragraphs.empty() && after.paragraphs.back().style == "A");

  // Thousands of styles: capped, and fast enough.
  std::string many = std::string(kHead) + "{\\stylesheet{\\fs22 Normal;}";
  for (int i = 1; i <= 6000; ++i)
    many += "{\\s" + std::to_string(i) + "\\sbasedon" + std::to_string(i - 1) + " S" +
            std::to_string(i) + ";}";
  many += "}\\pard\\s5999 x\\par}";
  const Document big = import(many);
  CHECK(big.styles.size() <= writeit::kMaxStyles + 16);
  CHECK(!big.paragraphs.empty() && text_of(big.paragraphs[0]) == "x");

  // A deep chain of bases resolves without recursion trouble.
  std::string deep = std::string(kHead) + "{\\stylesheet{\\fs22 Normal;}";
  for (int i = 4000; i >= 1; --i)
    deep += "{\\s" + std::to_string(i) + "\\sbasedon" + std::to_string(i + 1) + " D" +
            std::to_string(i) + ";}";
  deep += "{\\s4001\\b Root;}}\\pard\\s1 x\\par}";
  const Document chain = import(deep);
  const Style* d1 = writeit::find_style(chain.styles, "D1");
  CHECK(d1 != nullptr && d1->format.bold);

  // Words for styles outside the style sheet do nothing harmful.
  const Document stray = import(std::string(kHead) + "\\sbasedon3\\snext4\\cs5 x\\par}");
  CHECK(stray.paragraphs.size() == 1 && text_of(stray.paragraphs[0]) == "x");
  CHECK(stray.styles.empty());
}

void markdown()
{
  const Document doc = writeit::markdown_import("# Title\n\nBody *it*\n\n### Small", "Serif", 12);
  CHECK(doc.styles == writeit::builtin_styles("Serif", 12));
  CHECK(doc.paragraphs.size() == 3);
  if (doc.paragraphs.size() == 3) {
    const Style* h1 = writeit::find_style(doc.styles, "Heading 1");
    CHECK(doc.paragraphs[0].style == "Heading 1" && doc.paragraphs[0].heading == 1);
    CHECK(h1 != nullptr && doc.paragraphs[0].runs[0].size == h1->format.size);
    CHECK(doc.paragraphs[0].runs[0].bold == h1->format.bold);
    CHECK(doc.paragraphs[1].style == "Normal" && doc.paragraphs[1].runs[0].size == 12);
    CHECK(doc.paragraphs[2].style == "Heading 3");
  }
  // Emphasis is written where it differs from the style, so a bold heading
  // does not come back wrapped in **.
  CHECK(writeit::markdown_export(doc) == "# Title\n\nBody *it*\n\n### Small");
  // Italic inside a heading survives.
  const Document it = writeit::markdown_import("## A *b*", "Sans", 11);
  CHECK(writeit::markdown_export(it) == "## A *b*");
  // A heading style without the outline level is still body text in
  // Markdown, and a style that is bold writes no extra **.
  Document flat = sample();
  flat.paragraphs[0].heading = 0;
  CHECK(writeit::markdown_export(flat) == "Title\n\nBody text");
}

}  // namespace

int main()
{
  builtins();
  applying();
  editing();
  rtf_write();
  rtf_round_trip();
  rtf_read();
  rtf_hostile();
  markdown();
  return suite_test::done("styles");
}
