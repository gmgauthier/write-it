/* SPDX-License-Identifier: Unlicense */

// M2 bulleted and numbered lists: the model built on the indents, the
// numbering rule, the list edits the Format menu makes, RTF write and read
// (\listtable, \listoverridetable, \ls, \ilvl, and Word 97's \pn with its
// \pntext fallback), and the malformed list RTF a hostile file can carry.

#include "check.hpp"
#include "document.hpp"

#include <string>
#include <vector>

namespace {

using writeit::Indents;
using writeit::ListFormat;
using writeit::ListKind;

writeit::Paragraph item(const char* text, ListKind kind, int level = 0)
{
  writeit::Paragraph paragraph;
  if (text[0] != '\0') {
    writeit::Run run;
    run.text = text;
    paragraph.runs.push_back(run);
  }
  if (kind != ListKind::None) {
    paragraph.list.kind = kind;
    paragraph.list.level = level;
    paragraph.indents = writeit::list_indents(level);
  }
  return paragraph;
}

// A numbered item in list `list`: 0 continues the numbered list above.
writeit::Paragraph numbered(const char* text, int list, int level = 0)
{
  writeit::Paragraph paragraph = item(text, ListKind::Number, level);
  paragraph.list.list = list;
  return paragraph;
}

std::string text_of(const writeit::Paragraph& paragraph)
{
  std::string out;
  for (const writeit::Run& run : paragraph.runs)
    out += run.text;
  return out;
}

bool contains(const std::string& haystack, const std::string& needle)
{
  return haystack.find(needle) != std::string::npos;
}

size_t count_of(const std::string& haystack, const std::string& needle)
{
  size_t n = 0;
  for (size_t at = haystack.find(needle); at != std::string::npos;
       at = haystack.find(needle, at + needle.size()))
    ++n;
  return n;
}

writeit::Document import(const std::string& rtf)
{
  writeit::Document doc;
  CHECK(writeit::rtf_import(rtf, doc));
  return doc;
}

bool is_list(const writeit::Paragraph& paragraph, ListKind kind, int level)
{
  return paragraph.list.kind == kind && paragraph.list.level == level;
}

const char* kHead = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}}";

// One bullet list (\ls1) and one numbered list (\ls2), as Word 2000 writes them.
const char* kTables =
    "{\\*\\listtable"
    "{\\list\\listtemplateid100"
    "{\\listlevel\\levelnfc23\\levelnfcn23\\leveljc0\\levelstartat1{\\leveltext\\'01\\u8226 ?;}"
    "{\\levelnumbers;}\\fi-360\\li720}"
    "{\\listlevel\\levelnfc23{\\leveltext\\'01o;}{\\levelnumbers;}\\fi-360\\li1440}"
    "{\\listname ;}\\listid11}"
    "{\\list\\listtemplateid200"
    "{\\listlevel\\levelnfc0\\levelnfcn0\\levelstartat1{\\leveltext\\'02\\'00.;}"
    "{\\levelnumbers\\'01;}\\fi-360\\li720}"
    "{\\listlevel\\levelnfc4{\\leveltext\\'02\\'01.;}{\\levelnumbers\\'01;}\\fi-360\\li1440}"
    "{\\listname Numbers;}\\listid22}}"
    "{\\*\\listoverridetable{\\listoverride\\listid11\\listoverridecount0\\ls1}"
    "{\\listoverride\\listid22\\listoverridecount0\\ls2}}";

void model()
{
  // A new paragraph is not in a list.
  const writeit::Paragraph blank;
  CHECK(blank.list.kind == ListKind::None);
  CHECK(blank.list.level == 0);
  CHECK(blank.list == ListFormat{});
  CHECK(writeit::blank_document("Sans", 11).paragraphs[0].list == ListFormat{});

  // The list is part of paragraph equality, so undo snapshots and the dirty
  // title see a list change.
  CHECK(!(item("a", ListKind::Bullet) == item("a", ListKind::Number)));
  CHECK(!(item("a", ListKind::Bullet, 0) == item("a", ListKind::Bullet, 1)));
  writeit::Paragraph same_indents = item("a", ListKind::Bullet);
  same_indents.list = ListFormat{};
  CHECK(!(same_indents == item("a", ListKind::Bullet)));
  CHECK(item("a", ListKind::Number, 2) == item("a", ListKind::Number, 2));

  // Nine levels, 0 through 8, as RTF's \ilvl allows.
  CHECK(writeit::kListLevels == 9);
  CHECK(writeit::clamp_list(ListFormat{ListKind::Bullet, -3}).level == 0);
  CHECK(writeit::clamp_list(ListFormat{ListKind::Bullet, 99}).level == 8);
  CHECK(writeit::clamp_list(ListFormat{ListKind::Number, 4}).level == 4);
  CHECK(writeit::clamp_list(ListFormat{ListKind::None, 4}) == ListFormat{});

  // The list indents: a quarter-inch hang, half an inch per level.
  CHECK(writeit::list_indents(0) == (Indents{720, 0, -360}));
  CHECK(writeit::list_indents(1) == (Indents{1440, 0, -360}));
  CHECK(writeit::list_indents(8) == (Indents{6480, 0, -360}));
  CHECK(writeit::list_indents(99) == writeit::list_indents(8));
  CHECK(writeit::list_indents(-1) == writeit::list_indents(0));
}

void labels()
{
  using writeit::list_label;
  // Bullets: a disc, a circle, a square, round again.
  CHECK(list_label(ListFormat{ListKind::Bullet, 0}, 0) == "\u2022");
  CHECK(list_label(ListFormat{ListKind::Bullet, 1}, 0) == "\u25E6");
  CHECK(list_label(ListFormat{ListKind::Bullet, 2}, 0) == "\u25AA");
  CHECK(list_label(ListFormat{ListKind::Bullet, 3}, 0) == "\u2022");
  // Numbers: 1. then a. then i., round again.
  CHECK(list_label(ListFormat{ListKind::Number, 0}, 1) == "1.");
  CHECK(list_label(ListFormat{ListKind::Number, 0}, 42) == "42.");
  CHECK(list_label(ListFormat{ListKind::Number, 1}, 1) == "a.");
  CHECK(list_label(ListFormat{ListKind::Number, 1}, 26) == "z.");
  CHECK(list_label(ListFormat{ListKind::Number, 1}, 27) == "aa.");
  CHECK(list_label(ListFormat{ListKind::Number, 1}, 28) == "ab.");
  CHECK(list_label(ListFormat{ListKind::Number, 2}, 4) == "iv.");
  CHECK(list_label(ListFormat{ListKind::Number, 2}, 1994) == "mcmxciv.");
  CHECK(list_label(ListFormat{ListKind::Number, 3}, 3) == "3.");
  // Roman numerals stop at 3999; past it the label is decimal.
  CHECK(list_label(ListFormat{ListKind::Number, 2}, 4000) == "4000.");
  // Nonsense numbers still give a label.
  CHECK(list_label(ListFormat{ListKind::Number, 0}, 0) == "1.");
  CHECK(list_label(ListFormat{ListKind::Number, 1}, -5) == "a.");
  CHECK(list_label(ListFormat{ListKind::None, 0}, 1).empty());
  CHECK(!list_label(ListFormat{ListKind::Number, 1}, 2000000000).empty());
}

void numbering()
{
  using writeit::list_numbers;
  // Numbers count up through a list and, as in Word 97, keep counting past a
  // plain paragraph, empty or not.
  {
    std::vector<writeit::Paragraph> p;
    p.push_back(item("one", ListKind::Number));
    p.push_back(item("two", ListKind::Number));
    p.push_back(item("plain", ListKind::None));
    p.push_back(item("three", ListKind::Number));
    p.push_back(item("", ListKind::None));
    p.push_back(item("four", ListKind::Number));
    CHECK(list_numbers(p) == (std::vector<int>{1, 2, 0, 3, 0, 4}));
  }
  // Nested levels count on their own and restart under each new parent.
  {
    std::vector<writeit::Paragraph> p;
    p.push_back(item("1", ListKind::Number, 0));
    p.push_back(item("a", ListKind::Number, 1));
    p.push_back(item("b", ListKind::Number, 1));
    p.push_back(item("i", ListKind::Number, 2));
    p.push_back(item("2", ListKind::Number, 0));
    p.push_back(item("a", ListKind::Number, 1));
    p.push_back(item("c?", ListKind::Number, 8));
    CHECK(list_numbers(p) == (std::vector<int>{1, 1, 2, 1, 2, 1, 1}));
  }
  // Bullets do not count and do not interrupt the numbers around them.
  {
    std::vector<writeit::Paragraph> p;
    p.push_back(item("1", ListKind::Number, 0));
    p.push_back(item("bullet", ListKind::Bullet, 1));
    p.push_back(item("2", ListKind::Number, 0));
    p.push_back(item("bullet", ListKind::Bullet, 0));
    p.push_back(item("3", ListKind::Number, 0));
    CHECK(list_numbers(p) == (std::vector<int>{1, 0, 2, 0, 3}));
  }
  // Deleting the last item, or one in the middle, renumbers what is left.
  {
    std::vector<writeit::Paragraph> p;
    p.push_back(item("one", ListKind::Number));
    p.push_back(item("two", ListKind::Number));
    p.push_back(item("three", ListKind::Number));
    p.pop_back();
    CHECK(list_numbers(p) == (std::vector<int>{1, 2}));
    p.push_back(item("three", ListKind::Number));
    p.erase(p.begin() + 1);
    CHECK(list_numbers(p) == (std::vector<int>{1, 2}));
    CHECK(text_of(p[1]) == "three");
    p.clear();
    CHECK(list_numbers(p).empty());
  }
  // A level that a hostile document set out of range is read as clamped.
  {
    std::vector<writeit::Paragraph> p;
    p.push_back(item("x", ListKind::Number, 0));
    p.back().list.level = 1000;
    p.push_back(item("y", ListKind::Number, 0));
    p.back().list.level = -7;
    CHECK(list_numbers(p) == (std::vector<int>{1, 1}));
  }
}

void edits()
{
  using writeit::toggle_list;
  // Bullets on a plain paragraph: the list indents.
  {
    std::vector<writeit::Paragraph> p{item("plain", ListKind::None)};
    CHECK(toggle_list(p, ListKind::Bullet) == ListKind::Bullet);
    CHECK(is_list(p[0], ListKind::Bullet, 0));
    CHECK(p[0].indents == writeit::list_indents(0));
    CHECK(text_of(p[0]) == "plain");
    // The same button again turns the list off and gives the indents back.
    CHECK(toggle_list(p, ListKind::Bullet) == ListKind::None);
    CHECK(p[0] == item("plain", ListKind::None));
  }
  // A right indent survives a list going on and off.
  {
    writeit::Paragraph paragraph = item("r", ListKind::None);
    paragraph.indents.right = 1440;
    std::vector<writeit::Paragraph> p{paragraph};
    toggle_list(p, ListKind::Number);
    CHECK(p[0].indents == (Indents{720, 1440, -360}));
    toggle_list(p, ListKind::Number);
    CHECK(p[0] == paragraph);
  }
  // A list on an already-indented paragraph keeps the text where it was and
  // hangs the label in front of it.
  {
    writeit::Paragraph paragraph = item("indented", ListKind::None);
    paragraph.indents = Indents{1440, 0, 0};
    std::vector<writeit::Paragraph> p{paragraph};
    toggle_list(p, ListKind::Bullet);
    CHECK(p[0].indents == (Indents{1440, 0, -360}));
    toggle_list(p, ListKind::Bullet);
    CHECK(p[0].indents == (Indents{1440, 0, 0}));
    CHECK(p[0].list == ListFormat{});
  }
  // A left indent too small for the hang grows to fit it.
  {
    writeit::Paragraph paragraph = item("narrow", ListKind::None);
    paragraph.indents = Indents{0, 0, 720};
    std::vector<writeit::Paragraph> p{paragraph};
    toggle_list(p, ListKind::Number);
    CHECK(p[0].indents == (Indents{360, 0, -360}));
  }
  // Numbering on a bulleted list switches it, keeping levels and indents.
  {
    std::vector<writeit::Paragraph> p{item("a", ListKind::Bullet, 0),
                                      item("b", ListKind::Bullet, 1)};
    p[1].indents.right = 500;
    CHECK(toggle_list(p, ListKind::Number) == ListKind::Number);
    CHECK(is_list(p[0], ListKind::Number, 0));
    CHECK(is_list(p[1], ListKind::Number, 1));
    CHECK(p[0].indents == writeit::list_indents(0));
    CHECK(p[1].indents == (Indents{1440, 500, -360}));
    // And back.
    CHECK(toggle_list(p, ListKind::Bullet) == ListKind::Bullet);
    CHECK(is_list(p[1], ListKind::Bullet, 1));
  }
  // A mixed selection: anything not yet bulleted makes the button apply,
  // not remove.
  {
    std::vector<writeit::Paragraph> p{item("a", ListKind::Bullet), item("b", ListKind::None),
                                      item("c", ListKind::Number)};
    CHECK(toggle_list(p, ListKind::Bullet) == ListKind::Bullet);
    for (const auto& paragraph : p)
      CHECK(is_list(paragraph, ListKind::Bullet, 0));
  }
  // Turning a list off removes it from every selected paragraph.
  {
    std::vector<writeit::Paragraph> p{item("a", ListKind::Number, 0),
                                      item("b", ListKind::Number, 2)};
    CHECK(toggle_list(p, ListKind::Number) == ListKind::None);
    CHECK(p[0] == item("a", ListKind::None));
    CHECK(p[1] == item("b", ListKind::None));
  }
  // Toggling with None, or on nothing, changes nothing.
  {
    std::vector<writeit::Paragraph> p{item("a", ListKind::Number)};
    CHECK(toggle_list(p, ListKind::None) == ListKind::Number);
    CHECK(p[0] == item("a", ListKind::Number));
    std::vector<writeit::Paragraph> none;
    CHECK(toggle_list(none, ListKind::Bullet) == ListKind::None);
  }
  // Undo is a document snapshot: the edit and its reverse are exact.
  {
    writeit::Document before;
    before.paragraphs.push_back(item("one", ListKind::None));
    before.paragraphs.push_back(item("two", ListKind::None));
    writeit::Document after = before;
    toggle_list(after.paragraphs, ListKind::Number);
    CHECK(!(after == before));
    writeit::Document redone = after;
    toggle_list(redone.paragraphs, ListKind::Number);
    CHECK(redone == before);
  }
  // Levels: down a level moves half an inch in, up moves back; the ends hold.
  {
    writeit::Paragraph paragraph = item("x", ListKind::Bullet, 0);
    writeit::set_list_level(paragraph, 1);
    CHECK(is_list(paragraph, ListKind::Bullet, 1));
    CHECK(paragraph.indents == writeit::list_indents(1));
    writeit::set_list_level(paragraph, 50);
    CHECK(paragraph.list.level == 8);
    CHECK(paragraph.indents == writeit::list_indents(8));
    writeit::set_list_level(paragraph, -1);
    CHECK(paragraph.list.level == 0);
    CHECK(paragraph.indents == writeit::list_indents(0));
    // Custom indents shift by the same step.
    paragraph.indents = Indents{1000, 0, -360};
    writeit::set_list_level(paragraph, 1);
    CHECK(paragraph.indents == (Indents{1720, 0, -360}));
    // A paragraph not in a list has no level to change.
    writeit::Paragraph plain = item("p", ListKind::None);
    writeit::set_list_level(plain, 3);
    CHECK(plain == item("p", ListKind::None));
  }
}

// Turning a list off gives the paragraph back exactly the indents it had
// before it joined, whatever they were, after level changes and switches too.
void restore()
{
  const int lefts[] = {0, 360, 720, 1080, 1440, 2000, writeit::kMaxIndent};
  const int firsts[] = {0, 360, 720, -360, -720, -1440};
  const int rights[] = {0, 720};
  const ListKind kinds[] = {ListKind::Bullet, ListKind::Number};
  int failures = 0;
  for (const ListKind kind : kinds) {
    for (const int left : lefts) {
      for (const int first : firsts) {
        for (const int right : rights) {
          const Indents own = writeit::clamp_indents(Indents{left, right, first});
          if (own.first != first)
            continue;  // a hang past the left margin is not a paragraph's own indent
          writeit::Paragraph original = item("text", ListKind::None);
          original.indents = own;
          // On and straight off.
          {
            std::vector<writeit::Paragraph> p{original};
            toggle_list(p, kind);
            toggle_list(p, kind);
            if (!(p[0] == original) || p[0].indents != own)
              ++failures;
          }
          // On, down two levels, up one, then off.
          {
            std::vector<writeit::Paragraph> p{original};
            toggle_list(p, kind);
            writeit::set_list_level(p[0], 2);
            writeit::set_list_level(p[0], 1);
            toggle_list(p, kind);
            if (p[0].indents != own || p[0].list != ListFormat{})
              ++failures;
          }
          // On, switched to the other kind, then off with that kind.
          {
            const ListKind other = kind == ListKind::Bullet ? ListKind::Number : ListKind::Bullet;
            std::vector<writeit::Paragraph> p{original};
            toggle_list(p, kind);
            toggle_list(p, other);
            toggle_list(p, other);
            if (p[0].indents != own || p[0].list != ListFormat{})
              ++failures;
          }
          // An undo snapshot is a copy: the copy remembers the indents too.
          {
            writeit::Document doc;
            doc.paragraphs.push_back(original);
            toggle_list(doc.paragraphs, kind);
            writeit::Document snapshot = doc;
            toggle_list(snapshot.paragraphs, kind);
            if (snapshot.paragraphs[0].indents != own)
              ++failures;
          }
        }
      }
    }
  }
  CHECK(failures == 0);
  // The cases the review found, spelled out.
  {
    writeit::Paragraph p = item("l720", ListKind::None);
    p.indents = Indents{720, 0, 0};
    std::vector<writeit::Paragraph> v{p};
    toggle_list(v, ListKind::Bullet);
    CHECK(v[0].indents == writeit::list_indents(0) || v[0].indents == (Indents{720, 0, -360}));
    toggle_list(v, ListKind::Bullet);
    CHECK(v[0].indents == (Indents{720, 0, 0}));
  }
  {
    writeit::Paragraph p = item("first", ListKind::None);
    p.indents = Indents{0, 0, 360};
    std::vector<writeit::Paragraph> v{p};
    toggle_list(v, ListKind::Number);
    toggle_list(v, ListKind::Number);
    CHECK(v[0].indents == (Indents{0, 0, 360}));
  }
  {
    writeit::Paragraph p = item("hanging", ListKind::None);
    p.indents = Indents{1440, 0, -720};
    std::vector<writeit::Paragraph> v{p};
    toggle_list(v, ListKind::Bullet);
    toggle_list(v, ListKind::Bullet);
    CHECK(v[0].indents == (Indents{1440, 0, -720}));
  }
  // The remembered indents are editing memory, not document content: they
  // are not written to the file and two paragraphs differing only there are
  // equal, so a saved and reopened list item is the same document.
  {
    std::vector<writeit::Paragraph> v{item("x", ListKind::None)};
    v[0].indents = Indents{360, 0, 360};
    toggle_list(v, ListKind::Bullet);
    writeit::Document doc;
    doc.paragraphs = v;
    writeit::Document loaded;
    CHECK(writeit::rtf_import(writeit::rtf_export(doc), loaded));
    CHECK(loaded == doc);
    // Without the memory (a reopened file), off falls back to removing the
    // list's own indents.
    std::vector<writeit::Paragraph> reopened{item("y", ListKind::Bullet)};
    toggle_list(reopened, ListKind::Bullet);
    CHECK(reopened[0].indents == Indents{});
  }
}

void rtf_write()
{
  writeit::Document doc;
  doc.paragraphs.push_back(item("Apples", ListKind::Bullet));
  doc.paragraphs.push_back(item("First", ListKind::Number));
  doc.paragraphs.push_back(item("Sub", ListKind::Number, 1));
  doc.paragraphs.push_back(item("Plain", ListKind::None));
  const std::string rtf = writeit::rtf_export(doc);
  // Word 2000 and later: the two tables, then \ls and \ilvl on the paragraph.
  CHECK(contains(rtf, "{\\*\\listtable"));
  CHECK(contains(rtf, "{\\*\\listoverridetable"));
  CHECK(contains(rtf, "\\levelnfc23"));
  CHECK(contains(rtf, "\\levelnfc0"));
  CHECK(contains(rtf, "\\levelnfc4"));
  CHECK(contains(rtf, "\\levelnfc2"));
  CHECK(contains(rtf, "\\li720\\fi-360\\ls1\\ilvl0"));
  CHECK(contains(rtf, "\\ls2\\ilvl0"));
  CHECK(contains(rtf, "\\li1440\\fi-360\\ls2\\ilvl1"));
  // WordPad and older readers: the label as plain text in \pntext, ahead of
  // the paragraph as Word puts it. No Word 6 \pn: LibreOffice lets it win
  // over \ls and loses the levels.
  CHECK(contains(rtf, "{\\pntext\\f0\\fs22 \\u8226\\'95\\tab}\\pard\\li720"));
  CHECK(!contains(rtf, "\\pnlvl"));
  CHECK(contains(rtf, "{\\pntext\\f0\\fs22 \\u8226\\'95\\tab}"));
  CHECK(contains(rtf, "{\\pntext\\f0\\fs22 1.\\tab}"));
  CHECK(contains(rtf, "{\\pntext\\f0\\fs22 a.\\tab}"));
  // The plain paragraph writes no list words.
  CHECK(contains(rtf, "\\pard\\f0\\fs22\\b0\\i0\\ulnone Plain\\par"));
  CHECK(count_of(rtf, "\\ilvl") == 3 + 0);

  // A document with no list writes no list tables, as M1 did.
  writeit::Document plain;
  plain.paragraphs.push_back(item("Just text", ListKind::None));
  const std::string bare = writeit::rtf_export(plain);
  CHECK(!contains(bare, "listtable"));
  CHECK(!contains(bare, "\\ls"));
  CHECK(!contains(bare, "pntext"));

  // A plain paragraph does not end a list: both items are in \ls2, one
  // Word list, so Word and LibreOffice keep counting as Write-It does.
  writeit::Document two;
  two.paragraphs.push_back(item("one", ListKind::Number));
  two.paragraphs.push_back(item("plain", ListKind::None));
  two.paragraphs.push_back(item("two", ListKind::Number));
  const std::string across = writeit::rtf_export(two);
  CHECK(count_of(across, "\\ls2\\ilvl0") == 2);
  CHECK(!contains(across, "\\ls3"));
  CHECK(count_of(across, "{\\listoverride\\listid") == 2);
  CHECK(count_of(across, "{\\pntext\\f0\\fs22 1.\\tab}") == 1);
  CHECK(count_of(across, "{\\pntext\\f0\\fs22 2.\\tab}") == 1);

  // Each list is its own Word list: a restarted list is \ls3, and going
  // back to the first list goes back to \ls2.
  writeit::Document lists;
  lists.paragraphs.push_back(numbered("a1", 1));
  lists.paragraphs.push_back(numbered("b1", 2));
  lists.paragraphs.push_back(numbered("a2", 1));
  const std::string separate = writeit::rtf_export(lists);
  CHECK(contains(separate, "\\ls2\\ilvl0\\f0\\fs22 a1"));
  CHECK(contains(separate, "\\ls3\\ilvl0\\f0\\fs22 b1"));
  CHECK(contains(separate, "\\ls2\\ilvl0\\f0\\fs22 a2"));
  CHECK(count_of(separate, "{\\listoverride\\listid") == 3);
  CHECK(count_of(separate, "{\\pntext\\f0\\fs22 1.\\tab}") == 2);
  CHECK(count_of(separate, "{\\pntext\\f0\\fs22 2.\\tab}") == 1);
}

void rtf_round_trip()
{
  // One bulleted list and one numbered list: the M2 done line.
  writeit::Document doc;
  doc.paragraphs.push_back(item("Shopping", ListKind::None));
  doc.paragraphs.push_back(item("Bread", ListKind::Bullet));
  doc.paragraphs.push_back(item("Milk", ListKind::Bullet));
  doc.paragraphs.push_back(item("Skimmed", ListKind::Bullet, 1));
  doc.paragraphs.push_back(item("Steps", ListKind::None));
  doc.paragraphs.push_back(item("Open the door", ListKind::Number));
  doc.paragraphs.push_back(item("Look both ways", ListKind::Number, 1));
  doc.paragraphs.push_back(item("Close the door", ListKind::Number));
  doc.paragraphs.push_back(item("", ListKind::Number));  // an empty item
  doc.paragraphs.push_back(item("The end", ListKind::None));

  const std::string rtf = writeit::rtf_export(doc);
  const writeit::Document loaded = import(rtf);
  CHECK(loaded.paragraphs.size() == doc.paragraphs.size());
  CHECK(loaded == doc);
  // The labels stay out of the text.
  CHECK(text_of(loaded.paragraphs[1]) == "Bread");
  CHECK(text_of(loaded.paragraphs[5]) == "Open the door");
  // Saving the loaded file again writes the same bytes.
  CHECK(writeit::rtf_export(loaded) == rtf);

  // A list with its own indents, character format and a deep level.
  writeit::Document mixed;
  writeit::Paragraph bold = item("bold ", ListKind::Number, 0);
  bold.indents = Indents{2000, 300, -500};
  writeit::Run italic;
  italic.text = "italic";
  italic.italic = true;
  italic.font = "Serif";
  italic.size = 14;
  bold.runs[0].bold = true;
  bold.runs.push_back(italic);
  mixed.paragraphs.push_back(bold);
  mixed.paragraphs.push_back(item("deep", ListKind::Bullet, 8));
  mixed.paragraphs.push_back(item("{braces} \\ and \u00e9", ListKind::Number, 3));
  CHECK(import(writeit::rtf_export(mixed)) == mixed);

  // Alignment (M2's \qc and \qr) rides beside the list, and a list going
  // on or off leaves it alone.
  writeit::Document aligned;
  aligned.paragraphs.push_back(item("centred", ListKind::Bullet));
  aligned.paragraphs.back().align = writeit::Align::Center;
  aligned.paragraphs.push_back(item("right", ListKind::Number, 1));
  aligned.paragraphs.back().align = writeit::Align::Right;
  aligned.paragraphs.push_back(item("left", ListKind::Number));
  const std::string aligned_rtf = writeit::rtf_export(aligned);
  CHECK(contains(aligned_rtf, "\\qc\\ls1\\ilvl0"));
  CHECK(contains(aligned_rtf, "\\qr\\ls2\\ilvl1"));
  CHECK(import(aligned_rtf) == aligned);
  CHECK(import("{\\rtf1" + std::string(kTables) + "\\pard\\qc\\ls1 a\\par}").paragraphs[0].align ==
        writeit::Align::Center);
  std::vector<writeit::Paragraph> one{item("x", ListKind::None)};
  one[0].align = writeit::Align::Right;
  writeit::toggle_list(one, ListKind::Bullet);
  CHECK(one[0].align == writeit::Align::Right);
  writeit::toggle_list(one, ListKind::Bullet);
  CHECK(one[0].align == writeit::Align::Right && one[0].list.kind == ListKind::None);

  // The last paragraph of the file may be a list item.
  writeit::Document last;
  last.paragraphs.push_back(item("only", ListKind::Number));
  CHECK(import(writeit::rtf_export(last)) == last);
}

void rtf_read()
{
  // Word 2000 and later: tables, \ls and \ilvl, and \listtext labels that
  // are not text.
  {
    const writeit::Document doc =
        import(std::string(kHead) + kTables +
               "{\\listtext\\pard\\plain\\f0 \\u8226 ?\\tab}\\pard\\fi-360\\li720\\ls1 Bread\\par"
               "{\\listtext\\pard\\plain\\f0 o\\tab}\\pard\\fi-360\\li1440\\ls1\\ilvl1 Brown\\par"
               "{\\listtext\\pard\\plain\\f0 1.\\tab}\\pard\\fi-360\\li720\\ls2\\ilvl0 Up\\par"
               "{\\listtext\\pard\\plain\\f0 a.\\tab}\\pard\\fi-360\\li1440\\ls2\\ilvl1 Sub\\par"
               "\\pard Plain\\par}");
    CHECK(doc.paragraphs.size() == 5);
    CHECK(is_list(doc.paragraphs[0], ListKind::Bullet, 0));
    CHECK(is_list(doc.paragraphs[1], ListKind::Bullet, 1));
    CHECK(is_list(doc.paragraphs[2], ListKind::Number, 0));
    CHECK(is_list(doc.paragraphs[3], ListKind::Number, 1));
    CHECK(doc.paragraphs[4].list == ListFormat{});
    CHECK(text_of(doc.paragraphs[0]) == "Bread");
    CHECK(text_of(doc.paragraphs[1]) == "Brown");
    CHECK(text_of(doc.paragraphs[2]) == "Up");
    CHECK(text_of(doc.paragraphs[3]) == "Sub");
    CHECK(doc.paragraphs[1].indents == (Indents{1440, 0, -360}));
  }
  // Word 97: {\*\pn ...} with the label in {\pntext ...}.
  {
    const writeit::Document doc = import(
        std::string(kHead) +
        "{\\pntext\\f0 \\'b7\\tab}\\pard\\fi-360\\li720{\\*\\pn\\pnlvlblt\\pnf1\\pnindent360"
        "{\\pntxtb\\'b7}}Dot\\par"
        "{\\pntext\\f0 1.\\tab}\\pard\\fi-360\\li720{\\*\\pn\\pnlvlbody\\pndec\\pnstart1"
        "\\pnindent360{\\pntxta .}}One\\par"
        "{\\pntext\\f0 a)\\tab}\\pard\\fi-360\\li1440{\\*\\pn\\pnlvl2\\pnlcltr{\\pntxta )}}Two\\par"
        "{\\*\\pntext ignored too}\\pard\\li720{\\*\\pn\\pnlvlcont}Continued\\par"
        "\\pard Plain\\par}");
    CHECK(doc.paragraphs.size() == 5);
    CHECK(is_list(doc.paragraphs[0], ListKind::Bullet, 0));
    CHECK(is_list(doc.paragraphs[1], ListKind::Number, 0));
    CHECK(is_list(doc.paragraphs[2], ListKind::Number, 1));
    // A continuation paragraph has no label of its own; it keeps its indent.
    CHECK(doc.paragraphs[3].list == ListFormat{});
    CHECK(doc.paragraphs[3].indents.left == 720);
    CHECK(doc.paragraphs[4].list == ListFormat{});
    CHECK(text_of(doc.paragraphs[0]) == "Dot");
    CHECK(text_of(doc.paragraphs[1]) == "One");
    CHECK(text_of(doc.paragraphs[2]) == "Two");
    CHECK(text_of(doc.paragraphs[3]) == "Continued");
  }
  // Word 97-2003 writes both. \ls wins; the \pn and the labels are not text.
  {
    const writeit::Document doc =
        import(std::string(kHead) + kTables +
               "{\\pntext\\f0 1.\\tab}\\pard\\fi-360\\li720\\ls2{\\*\\pn\\pnlvlblt{\\pntxtb x}}"
               "Both\\par}");
    CHECK(doc.paragraphs.size() == 1);
    CHECK(is_list(doc.paragraphs[0], ListKind::Number, 0));
    CHECK(text_of(doc.paragraphs[0]) == "Both");
  }
  // \pard ends the list; without it the next paragraph stays in the list.
  // A group restores the list it changed.
  {
    const writeit::Document doc = import(std::string(kHead) + kTables +
                                         "\\pard\\ls2 one\\par two\\par"
                                         "\\pard three\\par"
                                         "\\pard {\\ls1 in}\\par}");
    CHECK(doc.paragraphs.size() == 4);
    CHECK(is_list(doc.paragraphs[0], ListKind::Number, 0));
    CHECK(is_list(doc.paragraphs[1], ListKind::Number, 0));
    CHECK(doc.paragraphs[2].list == ListFormat{});
    CHECK(doc.paragraphs[3].list == ListFormat{});
  }
  // \ilvl after the text still counts: the value in force at \par wins.
  {
    const writeit::Document doc =
        import(std::string(kHead) + kTables + "\\pard\\ls2 late\\ilvl1\\par}");
    CHECK(is_list(doc.paragraphs[0], ListKind::Number, 1));
  }
  // The last paragraph with no \par, and a truncated file, keep the list.
  {
    const writeit::Document closed =
        import(std::string(kHead) + kTables + "\\pard x\\par\\pard\\ls2\\ilvl1 y}");
    CHECK(closed.paragraphs.size() == 2);
    CHECK(is_list(closed.paragraphs[1], ListKind::Number, 1));
    const writeit::Document cut = import(std::string(kHead) + kTables + "\\pard\\ls1 cut");
    CHECK(cut.paragraphs.size() == 1);
    CHECK(is_list(cut.paragraphs[0], ListKind::Bullet, 0));
  }
  // Level kinds come from the definition: a list may mix bullets and numbers.
  {
    const writeit::Document doc =
        import(std::string(kHead) +
               "{\\*\\listtable{\\list{\\listlevel\\levelnfc0}{\\listlevel\\levelnfc23}"
               "{\\listlevel\\levelnfc255}{\\listlevel\\levelnfc2}\\listid5}}"
               "{\\*\\listoverridetable{\\listoverride\\listid5\\ls7}}"
               "\\pard\\ls7\\ilvl0 a\\par\\pard\\ls7\\ilvl1 b\\par\\pard\\ls7\\ilvl2 c\\par"
               "\\pard\\ls7\\ilvl3 d\\par\\pard\\ls7\\ilvl6 e\\par}");
    CHECK(is_list(doc.paragraphs[0], ListKind::Number, 0));
    CHECK(is_list(doc.paragraphs[1], ListKind::Bullet, 1));
    // No number at all (255) is closest to a bullet.
    CHECK(is_list(doc.paragraphs[2], ListKind::Bullet, 2));
    CHECK(is_list(doc.paragraphs[3], ListKind::Number, 3));
    // A level the definition left out is numbered, RTF's default \levelnfc0.
    CHECK(is_list(doc.paragraphs[4], ListKind::Number, 6));
  }
  // \listsimple: one level, used for every \ilvl.
  {
    const writeit::Document doc =
        import(std::string(kHead) +
               "{\\*\\listtable{\\list\\listsimple1{\\listlevel\\levelnfc23}\\listid9}}"
               "{\\*\\listoverridetable{\\listoverride\\listid9\\ls1}}\\pard\\ls1\\ilvl4 s\\par}");
    CHECK(is_list(doc.paragraphs[0], ListKind::Bullet, 4));
  }
  // The table text (\leveltext, \listname) never reaches the document.
  {
    const writeit::Document doc =
        import(std::string(kHead) + kTables + "\\pard\\ls2 only this\\par}");
    CHECK(doc.paragraphs.size() == 1);
    CHECK(text_of(doc.paragraphs[0]) == "only this");
  }
}

void rtf_hostile()
{
  // A hand-written list item with no indents takes the list's indents for its
  // level, so its label has somewhere to hang. Given indents are kept.
  {
    const writeit::Document doc =
        import(std::string(kHead) + kTables +
               "\\pard\\ls1 a\\par\\pard\\ls2\\ilvl2\\ri300 b\\par"
               "\\pard\\ls1\\li1000 c\\par\\pard\\ls1\\fi200 d\\par"
               "\\pard{\\*\\pn\\pnlvlblt}e\\par\\pard\\ls77 f\\par\\pard g\\par}");
    CHECK(doc.paragraphs.size() == 7);
    CHECK(doc.paragraphs[0].indents == writeit::list_indents(0));
    CHECK(doc.paragraphs[1].indents == (Indents{2160, 300, -360}));
    CHECK(doc.paragraphs[2].indents == (Indents{1000, 0, 0}));
    CHECK(doc.paragraphs[3].indents == (Indents{0, 0, 200}));
    CHECK(doc.paragraphs[4].indents == writeit::list_indents(0));
    CHECK(doc.paragraphs[5].indents == writeit::list_indents(0));
    CHECK(doc.paragraphs[6].indents == Indents{});
  }
  // \ls pointing at no override: still a list, as a bullet, rather than lost.
  {
    const writeit::Document doc = import(std::string(kHead) + "\\pard\\ls3 orphan\\par}");
    CHECK(doc.paragraphs.size() == 1);
    CHECK(is_list(doc.paragraphs[0], ListKind::Bullet, 0));
    CHECK(text_of(doc.paragraphs[0]) == "orphan");
  }
  // An override naming a list that is not in the table: a bullet too.
  {
    const writeit::Document doc =
        import(std::string(kHead) +
               "{\\*\\listoverridetable{\\listoverride\\listid404\\ls1}}\\pard\\ls1 lost\\par}");
    CHECK(is_list(doc.paragraphs[0], ListKind::Bullet, 0));
  }
  // \ls0, negative \ls and \ls with no number are not lists.
  {
    const writeit::Document doc = import(std::string(kHead) + kTables +
                                         "\\pard\\ls0 a\\par\\pard\\ls-5 b\\par"
                                         "\\pard\\ls c\\par}");
    CHECK(doc.paragraphs.size() == 3);
    for (const auto& paragraph : doc.paragraphs)
      CHECK(paragraph.list == ListFormat{});
  }
  // Huge and negative numbers clamp; a digit run too long for an int is
  // just "too big".
  {
    const writeit::Document doc = import(std::string(kHead) + kTables +
                                         "\\pard\\ls2\\ilvl9 a\\par"
                                         "\\pard\\ls2\\ilvl-3 b\\par"
                                         "\\pard\\ls2\\ilvl99999999999999999999 c\\par"
                                         "\\pard\\ls99999999999999999999 d\\par"
                                         "\\pard\\ls-2147483648\\ilvl2147483647 e\\par}");
    CHECK(doc.paragraphs.size() == 5);
    CHECK(is_list(doc.paragraphs[0], ListKind::Number, 8));
    CHECK(is_list(doc.paragraphs[1], ListKind::Number, 0));
    CHECK(is_list(doc.paragraphs[2], ListKind::Number, 8));
    CHECK(is_list(doc.paragraphs[3], ListKind::Bullet, 0));
    CHECK(doc.paragraphs[4].list == ListFormat{});
    CHECK(text_of(doc.paragraphs[4]) == "e");
  }
  // Tables that are broken in every way: empty, unclosed, out of order,
  // more than nine levels, huge ids, levels outside a list, overrides with
  // no list id, a list table nested in itself.
  {
    std::string levels;
    for (int n = 0; n < 40; ++n)
      levels += "{\\listlevel\\levelnfc23}";
    const writeit::Document doc =
        import(std::string(kHead) + "{\\*\\listtable}{\\*\\listoverridetable}" +
               "{\\*\\listtable{\\listlevel\\levelnfc0}{\\list\\listid}" + "{\\list" + levels +
               "\\listid2147483647}" + "{\\list\\listid-4{\\listlevel\\levelnfc-9}}" +
               "{\\list{\\listlevel\\levelnfc99999999999}\\listid3}" +
               "{\\*\\listtable{\\list\\listid8}}}" +
               "{\\*\\listoverridetable{\\listoverride\\ls4}{\\listoverride\\listid2147483647"
               "\\ls5}{\\listoverride\\listid-4\\ls6}{\\listoverride\\listid3\\ls7}"
               "{\\listoverride\\listid3}}"
               "\\pard\\ls4 a\\par\\pard\\ls5\\ilvl8 b\\par\\pard\\ls6 c\\par"
               "\\pard\\ls7 d\\par}");
    CHECK(doc.paragraphs.size() == 4);
    CHECK(is_list(doc.paragraphs[0], ListKind::Bullet, 0));
    CHECK(is_list(doc.paragraphs[1], ListKind::Bullet, 8));
    CHECK(doc.paragraphs[2].list.kind != ListKind::None);
    CHECK(doc.paragraphs[3].list.kind != ListKind::None);
    CHECK(text_of(doc.paragraphs[0]) == "a");
    CHECK(text_of(doc.paragraphs[3]) == "d");
  }
  // A list table that never closes swallows the rest of the file, but the
  // reader returns.
  {
    writeit::Document doc;
    CHECK(writeit::rtf_import(std::string(kHead) + "{\\*\\listtable{\\list{\\listlevel", doc));
    CHECK(doc.paragraphs.size() == 1);
  }
  // Thousands of definitions and overrides are bounded, and still parse.
  {
    std::string tables = "{\\*\\listtable";
    std::string overrides = "{\\*\\listoverridetable";
    for (int n = 1; n <= 5000; ++n) {
      tables += "{\\list{\\listlevel\\levelnfc0}\\listid" + std::to_string(n) + "}";
      overrides += "{\\listoverride\\listid" + std::to_string(n) + "\\ls" + std::to_string(n) + "}";
    }
    tables += "}";
    overrides += "}";
    const writeit::Document doc = import(std::string(kHead) + tables + overrides +
                                         "\\pard\\ls1 a\\par\\pard\\ls5000 b\\par}");
    CHECK(doc.paragraphs.size() == 2);
    CHECK(is_list(doc.paragraphs[0], ListKind::Number, 0));
    CHECK(doc.paragraphs[1].list.kind != ListKind::None);
  }
  // \pn without \*, \pn in the middle of a paragraph, an empty \pn, \pn
  // words outside \pn, and a \pnlvl level out of range.
  {
    const writeit::Document doc = import(std::string(kHead) +
                                         "\\pard{\\pn\\pnlvlblt}a\\par"
                                         "\\pard b\\pn\\pnlvlbody c\\par"
                                         "\\pard{\\*\\pn}d\\par"
                                         "\\pard\\pnlvlblt\\pndec e\\par"
                                         "\\pard{\\*\\pn\\pnlvl99\\pndec}f\\par"
                                         "\\pard{\\*\\pn\\pnlvl-1\\pndec}g\\par}");
    CHECK(doc.paragraphs.size() == 6);
    CHECK(is_list(doc.paragraphs[0], ListKind::Bullet, 0));
    CHECK(text_of(doc.paragraphs[1]) == "bc");
    CHECK(doc.paragraphs[2].list == ListFormat{});
    CHECK(doc.paragraphs[3].list == ListFormat{});
    CHECK(text_of(doc.paragraphs[3]) == "e");
    CHECK(is_list(doc.paragraphs[4], ListKind::Number, 8));
    CHECK(is_list(doc.paragraphs[5], ListKind::Number, 0));
  }
  // Labels with stray braces and nested groups stay out of the text.
  {
    const writeit::Document doc =
        import(std::string(kHead) + "{\\pntext{{{1.}}}\\tab}{\\listtext{\\b x}}\\pard text\\par}");
    CHECK(doc.paragraphs.size() == 1);
    CHECK(text_of(doc.paragraphs[0]) == "text");
  }
  // A list inside the stylesheet describes a style, not the paragraph.
  {
    const writeit::Document doc = import(std::string(kHead) + kTables +
                                         "{\\stylesheet{\\s1\\ls2\\ilvl3 List;}}"
                                         "\\pard text\\par}");
    CHECK(doc.paragraphs[0].list == ListFormat{});
  }
}

// Word 97's rule: a numbered list keeps counting until it is restarted.
void continuing()
{
  using writeit::list_ids;
  using writeit::list_numbers;
  // Plain paragraphs and bullets in between do not stop the count, at any
  // level: a plain paragraph no longer resets the levels below either.
  {
    std::vector<writeit::Paragraph> p;
    p.push_back(item("1", ListKind::Number, 0));
    p.push_back(item("a", ListKind::Number, 1));
    p.push_back(item("plain", ListKind::None));
    p.push_back(item("bullet", ListKind::Bullet, 1));
    p.push_back(item("b", ListKind::Number, 1));
    p.push_back(item("plain", ListKind::None));
    p.push_back(item("2", ListKind::Number, 0));
    p.push_back(item("a", ListKind::Number, 1));
    CHECK(list_numbers(p) == (std::vector<int>{1, 1, 0, 0, 2, 0, 2, 1}));
  }
  // Each list counts on its own; a list restarts only where a new list
  // begins. 0 continues the numbered list above, and the document's first
  // numbered item with 0 begins list 1.
  {
    std::vector<writeit::Paragraph> p;
    p.push_back(numbered("a1", 0));
    p.push_back(numbered("a2", 0));
    p.push_back(numbered("b1", 2));
    p.push_back(item("plain", ListKind::None));
    p.push_back(numbered("b2", 0));
    p.push_back(numbered("a3", 1));
    p.push_back(numbered("b3", 2));
    p.push_back(numbered("a4", 0));
    CHECK(list_numbers(p) == (std::vector<int>{1, 2, 1, 0, 2, 3, 3, 4}));
    // list_ids: each numbered item's list, numbered by first appearance.
    CHECK(list_ids(p) == (std::vector<int>{1, 1, 2, 0, 2, 1, 2, 1}));
  }
  // Ids are labels: any numbers, in any order, give the same lists.
  {
    std::vector<writeit::Paragraph> p{numbered("x", 70), item("b", ListKind::Bullet),
                                      numbered("y", 3), numbered("z", 70)};
    CHECK(list_ids(p) == (std::vector<int>{1, 0, 2, 1}));
    CHECK(list_numbers(p) == (std::vector<int>{1, 0, 1, 2}));
    writeit::canonical_lists(p);
    CHECK(p[0].list.list == 1 && p[2].list.list == 2 && p[3].list.list == 1);
    CHECK(p[1].list.list == 0);
    // A negative id is read as 0; a bullet or a plain paragraph has none.
    std::vector<writeit::Paragraph> q{numbered("x", -5), numbered("y", 0)};
    CHECK(list_ids(q) == (std::vector<int>{1, 1}));
    writeit::Paragraph bullet = item("b", ListKind::Bullet);
    bullet.list.list = 9;
    CHECK(writeit::clamp_list(bullet.list).list == 0);
    CHECK(list_ids(std::vector<writeit::Paragraph>{}).empty());
  }
  // A paragraph tells its list apart; a document compares the lists it
  // makes, not the labels: [0, 0] and [1, 1] are the same single list.
  {
    CHECK(numbered("x", 1).list != numbered("x", 2).list);
    writeit::Document zeros;
    zeros.paragraphs = {numbered("x", 0), numbered("y", 0)};
    writeit::Document ones;
    ones.paragraphs = {numbered("x", 1), numbered("y", 1)};
    writeit::Document split;
    split.paragraphs = {numbered("x", 0), numbered("y", 2)};
    CHECK(zeros == ones);
    CHECK(!(zeros == split));
  }
  // Restart Numbering: the item and the rest of its list become a new
  // list. Another list in between is left alone. Continue Previous List
  // joins them back.
  {
    std::vector<writeit::Paragraph> p;
    p.push_back(item("1", ListKind::Number));
    p.push_back(item("2", ListKind::Number));
    p.push_back(item("plain", ListKind::None));
    p.push_back(item("3", ListKind::Number));
    p.push_back(numbered("other", 7));
    p.push_back(numbered("4", 1));
    CHECK(list_numbers(p) == (std::vector<int>{1, 2, 0, 3, 1, 4}));
    CHECK(writeit::restart_numbering(p, 3));
    CHECK(list_numbers(p) == (std::vector<int>{1, 2, 0, 1, 1, 2}));
    CHECK(list_ids(p) == (std::vector<int>{1, 1, 0, 2, 3, 2}));
    // Restarting where a list already starts, or off a numbered item, does
    // nothing.
    CHECK(!writeit::restart_numbering(p, 3));
    CHECK(!writeit::restart_numbering(p, 0));
    CHECK(!writeit::restart_numbering(p, 2));
    CHECK(!writeit::restart_numbering(p, 99));
    CHECK(writeit::continue_numbering(p, 3));
    CHECK(list_numbers(p) == (std::vector<int>{1, 2, 0, 3, 1, 4}));
    // Nothing above to continue: false.
    CHECK(!writeit::continue_numbering(p, 0));
    CHECK(!writeit::continue_numbering(p, 2));
    // "other" continues the list above it, the first one.
    CHECK(writeit::continue_numbering(p, 4));
    CHECK(list_numbers(p) == (std::vector<int>{1, 2, 0, 3, 4, 5}));
  }
  // Format → Numbering on a paragraph joins the list above (id 0), and
  // switching an item to bullets and back does too.
  {
    std::vector<writeit::Paragraph> p{item("x", ListKind::None)};
    writeit::toggle_list(p, ListKind::Number);
    CHECK(p[0].list.kind == ListKind::Number && p[0].list.list == 0);
    std::vector<writeit::Paragraph> q{numbered("y", 4)};
    writeit::toggle_list(q, ListKind::Bullet);
    CHECK(q[0].list.kind == ListKind::Bullet && q[0].list.list == 0);
    writeit::toggle_list(q, ListKind::Number);
    CHECK(q[0].list.kind == ListKind::Number && q[0].list.list == 0);
  }
  // RTF round trip: the lists, the restarts and the return to an earlier
  // list all come back, and the file is stable.
  {
    writeit::Document doc;
    doc.paragraphs.push_back(item("a1", ListKind::Number));
    doc.paragraphs.push_back(item("plain", ListKind::None));
    doc.paragraphs.push_back(item("a2", ListKind::Number));
    doc.paragraphs.push_back(numbered("b1", 2));
    doc.paragraphs.push_back(item("bullet", ListKind::Bullet));
    doc.paragraphs.push_back(numbered("b2", 2, 1));
    doc.paragraphs.push_back(numbered("a3", 1));
    const std::string rtf = writeit::rtf_export(doc);
    const writeit::Document back = import(rtf);
    CHECK(back == doc);
    CHECK(list_numbers(back.paragraphs) == (std::vector<int>{1, 0, 2, 1, 0, 1, 3}));
    CHECK(writeit::rtf_export(back) == rtf);
    CHECK(contains(rtf, "{\\pntext\\f0\\fs22 3.\\tab}"));
  }
  // Reading: the list is the \listid the \ls points at. The same \ls keeps
  // counting across plain paragraphs, and two \ls of one list are one list.
  {
    const writeit::Document doc =
        import(std::string(kHead) + kTables +
               "{\\*\\listoverridetable{\\listoverride\\listid22\\listoverridecount0\\ls3}}"
               "\\pard\\ls2 one\\par\\pard plain\\par\\pard\\ls2 two\\par"
               "\\pard\\ls3 three\\par}");
    CHECK(list_numbers(doc.paragraphs) == (std::vector<int>{1, 0, 2, 3}));
  }
  // Two numbered \listid are two lists, and an override that starts its
  // list again (\listoverridestartat) is a new list too.
  {
    const std::string tables =
        "{\\*\\listtable{\\list{\\listlevel\\levelnfc0}\\listid5}"
        "{\\list{\\listlevel\\levelnfc0}\\listid6}}"
        "{\\*\\listoverridetable{\\listoverride\\listid5\\listoverridecount0\\ls1}"
        "{\\listoverride\\listid6\\listoverridecount0\\ls2}"
        "{\\listoverride\\listid5\\listoverridecount1{\\lfolevel\\listoverridestartat"
        "{\\listlevel\\levelstartat1}}\\ls3}}";
    const writeit::Document doc = import(std::string(kHead) + tables +
                                         "\\pard\\ls1 a1\\par\\pard\\ls2 b1\\par"
                                         "\\pard\\ls1 a2\\par\\pard\\ls3 c1\\par"
                                         "\\pard\\ls1 a3\\par}");
    CHECK(list_numbers(doc.paragraphs) == (std::vector<int>{1, 1, 2, 1, 3}));
    CHECK(list_ids(doc.paragraphs) == (std::vector<int>{1, 2, 1, 3, 1}));
  }
  // Word 6/95's \pn numbers are one list, counting past a plain paragraph.
  {
    const writeit::Document doc =
        import(std::string(kHead) +
               "\\pard{\\*\\pn\\pnlvlbody\\pndec}one\\par\\pard plain\\par"
               "\\pard{\\*\\pn\\pnlvlbody\\pndec}two\\par}");
    CHECK(list_numbers(doc.paragraphs) == (std::vector<int>{1, 0, 2}));
  }
  // Hostile: an \ls near INT_MAX and thousands of one-item lists stay
  // defined. A document has at most kMaxLists numbered lists; items of later
  // ones join the last, so the file Write-It writes reads back the same.
  {
    std::string body = std::string(kHead) + "{\\*\\listtable";
    for (int n = 1; n <= 5000; ++n)
      body += "{\\list{\\listlevel\\levelnfc0}\\listid" + std::to_string(n) + "}";
    body += "}{\\*\\listoverridetable";
    for (int n = 1; n <= 5000; ++n)
      body += "{\\listoverride\\listid" + std::to_string(n) + "\\ls" + std::to_string(n) + "}";
    body += "}";
    for (int n = 1; n <= 5000; ++n)
      body += "\\pard\\ls" + std::to_string(n) + " x\\par";
    body += "\\pard\\ls2147483647 y\\par}";
    const writeit::Document doc = import(body);
    CHECK(doc.paragraphs.size() == 5001);
    const std::vector<int> numbers = list_numbers(doc.paragraphs);
    CHECK(writeit::kMaxLists == 4000);
    CHECK(numbers.front() == 1 && numbers[3999] == 1 && numbers[4000] == 2);
    CHECK(writeit::list_ids(doc.paragraphs)[4000] == writeit::kMaxLists);
    const writeit::Document again = import(writeit::rtf_export(doc));
    CHECK(again == doc);
  }
}

void markdown()
{
  // Markdown lists wait until M4: a list item exports as its paragraph.
  writeit::Document doc;
  doc.paragraphs.push_back(item("Bread", ListKind::Bullet));
  doc.paragraphs.push_back(item("Step", ListKind::Number));
  CHECK(writeit::markdown_export(doc) == "Bread\n\nStep");
  const writeit::Document back = writeit::markdown_import("- Bread\n", "Sans", 11);
  CHECK(back.paragraphs[0].list == ListFormat{});
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 299;

int main()
{
  model();
  labels();
  numbering();
  edits();
  restore();
  rtf_write();
  rtf_round_trip();
  rtf_read();
  rtf_hostile();
  continuing();
  markdown();
  return suite_test::done("lists", kChecks);
}
