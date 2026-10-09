/* SPDX-License-Identifier: Unlicense */

// \uN in RTF is a signed 16-bit UTF-16 code unit followed by \ucN fallback
// characters. Whatever the file says, the reader must hand GTK valid UTF-8.

#include "check.hpp"
#include "document.hpp"

#include <glib.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

const std::string kHead = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}}\\pard ";
const std::string kFffd = "\xEF\xBF\xBD";

// An independent strict check: no overlong forms, no surrogates, nothing
// past U+10FFFF, no truncated sequences.
bool valid_utf8(const std::string& s)
{
  size_t i = 0;
  while (i < s.size()) {
    const auto c = static_cast<unsigned char>(s[i]);
    size_t len = 0;
    uint32_t cp = 0;
    uint32_t min = 0;
    if (c < 0x80) {
      ++i;
      continue;
    } else if ((c & 0xE0) == 0xC0) {
      len = 2;
      cp = c & 0x1F;
      min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
      cp = c & 0x0F;
      min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
      cp = c & 0x07;
      min = 0x10000;
    } else {
      return false;
    }
    if (i + len > s.size())
      return false;
    for (size_t k = 1; k < len; ++k) {
      const auto b = static_cast<unsigned char>(s[i + k]);
      if ((b & 0xC0) != 0x80)
        return false;
      cp = (cp << 6) | (b & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
      return false;
    i += len;
  }
  return true;
}

std::string text_of(const writeit::Document& doc)
{
  std::string out;
  for (size_t p = 0; p < doc.paragraphs.size(); ++p) {
    if (p > 0)
      out += "\n";
    for (const auto& run : doc.paragraphs[p].runs)
      out += run.text;
  }
  return out;
}

// No NUL and no control character but tab, in text or font names.
bool no_controls(const std::string& s)
{
  for (const char c : s) {
    const auto b = static_cast<unsigned char>(c);
    if ((b < 0x20 && b != '\t') || b == 0x7F)
      return false;
  }
  // C1 controls, U+0080..U+009F, are C2 80..C2 9F in UTF-8.
  for (size_t i = 0; i + 1 < s.size(); ++i) {
    if (static_cast<unsigned char>(s[i]) == 0xC2 && static_cast<unsigned char>(s[i + 1]) >= 0x80 &&
        static_cast<unsigned char>(s[i + 1]) <= 0x9F)
      return false;
  }
  return true;
}

bool all_valid(const writeit::Document& doc)
{
  for (const auto& paragraph : doc.paragraphs) {
    for (const auto& run : paragraph.runs) {
      if (!valid_utf8(run.text) || !valid_utf8(run.font))
        return false;
      // What GTK will be handed: GLib's check, which also rejects NUL.
      if (!g_utf8_validate(run.text.c_str(), static_cast<gssize>(run.text.size()), nullptr) ||
          !g_utf8_validate(run.font.c_str(), static_cast<gssize>(run.font.size()), nullptr))
        return false;
      if (!no_controls(run.text) || !no_controls(run.font))
        return false;
    }
  }
  return true;
}

writeit::Document read_doc(const std::string& body, const std::string& fonts = "")
{
  writeit::Document doc;
  const std::string head = "{\\rtf1\\ansi\\ansicpg1252\\deff0{\\fonttbl{\\f0\\fswiss Sans;}" + fonts +
                           "}\\pard ";
  CHECK(writeit::rtf_import(head + body + "}", doc));
  CHECK(all_valid(doc));
  return doc;
}

std::string read(const std::string& body)
{
  writeit::Document doc;
  CHECK(writeit::rtf_import(kHead + body + "}", doc));
  CHECK(all_valid(doc));
  return text_of(doc);
}

void scalar_values()
{
  CHECK(read("a\\u233?b") == "a\u00e9b");
  // Out of 16-bit range: one replacement character, and the fallback is skipped.
  CHECK(read("a\\u9999999999?b") == "a" + kFffd + "b");
  CHECK(read("a\\u99999999999999999999?b") == "a" + kFffd + "b");
  CHECK(read("a\\u65536?b") == "a" + kFffd + "b");
  CHECK(read("a\\u-32769?b") == "a" + kFffd + "b");
  CHECK(read("a\\u-9999999999?b") == "a" + kFffd + "b");
  // Negative values fold: N + 65536.
  CHECK(read("\\u-4064?") == "\uF020");
  CHECK(read("\\u-1?") == "\xEF\xBF\xBF");  // U+FFFF
  CHECK(read("\\u-32768?") == "\u8000");
  CHECK(read("\\u65535?") == "\xEF\xBF\xBF");
  CHECK(read("\\u0?x") == std::string("x"));  // NUL is dropped, never emitted
}

void surrogates()
{
  const std::string grin = "\xF0\x9F\x98\x80";  // U+1F600
  CHECK(read("\\u55357?\\u56832?") == grin);
  CHECK(read("\\u-10179?\\u-8704?") == grin);
  // Word writes the fallback as a hex escape.
  CHECK(read("\\u55357\\'3f\\u56832\\'3f") == grin);
  CHECK(read("x\\u55357?\\u56832?y") == "x" + grin + "y");
  // Lone surrogates become U+FFFD.
  CHECK(read("\\u55357?x") == kFffd + "x");
  CHECK(read("\\u56832?x") == kFffd + "x");
  CHECK(read("\\u55357?") == kFffd);
  CHECK(read("\\u55357?\\par z") == kFffd + "\nz");
  CHECK(read("\\u55357?\\u233?") == kFffd + "\u00e9");
  CHECK(read("\\u55357?\\u55357?\\u56832?") == kFffd + grin);
  CHECK(read("\\u56832?\\u55357?") == kFffd + kFffd);
}

void fallback_skips()
{
  CHECK(read("\\uc0\\u233 x") == "\u00e9x");
  CHECK(read("\\uc0 \\u233\\u233 x") == "\u00e9\u00e9x");
  CHECK(read("\\uc2\\u233 xyz") == "\u00e9z");
  CHECK(read("\\uc2\\u233\\'e9\\'e9z") == "\u00e9z");
  // A control word is one fallback character.
  CHECK(read("\\u233\\tab x") == "\u00e9x");
  // The count stops at a group boundary.
  CHECK(read("{\\uc2\\u233 x}y") == "\u00e9y");
  // \uc is scoped by the group.
  CHECK(read("{\\uc2 \\u233 xyz}\\u233 ab") == "\u00e9z\u00e9b");
  // A fallback count larger than the text left does not run off the end.
  CHECK(read("\\uc9\\u233 ab") == "\u00e9");
}

void raw_bytes()
{
  // Bytes outside ASCII in the file itself are not valid RTF, but they must
  // not get through as broken UTF-8 either.
  CHECK(read("a\xED\xA0\x80" "b").find('b') != std::string::npos);  // encoded surrogate
  CHECK(read("a\xFF" "b").find('b') != std::string::npos);
  CHECK(read("a\xE2\x82").find('a') != std::string::npos);  // truncated
  CHECK(read("a\xC0\xAF" "b").find('b') != std::string::npos);  // overlong
  CHECK(read("a\xF4\x90\x80\x80" "b").find('b') != std::string::npos);  // past U+10FFFF
  CHECK(read("caf\xC3\xA9") == "caf\u00e9");  // valid UTF-8 passes through
}

// Bug Basher on #5: \'hh reads up to two real hex digits and nothing more.
void hex_escapes()
{
  CHECK(read("a\\'00b") == "ab");      // NUL never reaches the text
  CHECK(read("a\\'zzb") == "azzb");    // no digits: dropped, zz kept
  CHECK(read("a\\' b") == "a b");      // no 0x0B, and the space and b kept
  CHECK(read("a\\'4g") == "ag");       // one digit: 0x04, a control, dropped
  CHECK(read("a\\'e9b") == "a\u00e9b"); // two digits, as before
  CHECK(read("a\\'E9b") == "a\u00e9b");
  CHECK(read("a\\'e9fb") == "a\u00e9fb"); // a third hex digit is text
  CHECK(read("a\\'4") == "a");          // at the very end
  CHECK(read("a\\'") == "a");
  // The brace after a short escape is not eaten, so nesting stays right.
  {
    const auto doc = read_doc("{\\b a\\'4}c");
    CHECK(doc.paragraphs.size() == 1);
    CHECK(doc.paragraphs[0].runs.size() == 2);
    if (doc.paragraphs[0].runs.size() == 2) {
      CHECK(doc.paragraphs[0].runs[0].text == "a");
      CHECK(doc.paragraphs[0].runs[0].bold);
      CHECK(doc.paragraphs[0].runs[1].text == "c");
      CHECK(!doc.paragraphs[0].runs[1].bold);
    }
  }
  CHECK(read("{\\b a\\'}c") == "ac");
  CHECK(read("a\\'\\'41b") == "aAb"); // a bare \' then a real one
  // \'hh is one fallback character, even when short.
  CHECK(read("\\u233\\'4x") == "\u00e9x");
}

// Bug Basher on #5: font names decode \u and honour \uc, like body text.
void font_table()
{
  auto font_of = [](const std::string& fonts) {
    const auto doc = read_doc("\\f1 x", fonts);
    if (doc.paragraphs.empty() || doc.paragraphs[0].runs.empty())
      return std::string("?none?");
    return doc.paragraphs[0].runs[0].font;
  };
  const std::string nihon = "\u65e5\u672c";  // 日本
  CHECK(font_of("{\\f1 \\u26085?\\u26412?Gothic;}") == nihon + "Gothic");
  CHECK(font_of("{\\f1\\uc2 \\u26085\\'93\\'fa\\u26412\\'96\\'7bGothic;}") == nihon + "Gothic");
  CHECK(font_of("{\\f1\\uc0 \\u26085\\u26412 Gothic;}") == nihon + "Gothic");
  CHECK(font_of("{\\f1 Caf\\'e9;}") == "Caf\u00e9");
  CHECK(font_of("{\\f1 \\u55357?\\u56832?Emoji;}") == "\xF0\x9F\x98\x80" "Emoji");
  CHECK(font_of("{\\f1 \\u9999999999?X;}") == kFffd + "X");
  CHECK(font_of("{\\f1 A\\'00\\u0?B;}") == "AB");
  // Word's font entries carry \\*\\panose and \\falt groups; they are not the name.
  CHECK(font_of("{\\f1\\froman\\fcharset0\\fprq2{\\*\\panose 02020603050405020304}Times New "
                "Roman;}") == "Times New Roman");
  CHECK(font_of("{\\f1 MS Mincho{\\*\\falt \\'82\\'6c\\'82\\'72 \\'96\\'be\\'92\\'a9};}") == "MS Mincho");
  // Font numbers are not indices: Word's theme fonts are \\f31500 and up, and a
  // huge number must not allocate a table that size.
  CHECK(read_doc("\\f31507 x", "{\\f31507 Theme;}").paragraphs[0].runs[0].font == "Theme");
  CHECK(read_doc("\\f99999999 x", "{\\f99999999 Big;}").paragraphs[0].runs[0].font == "Big");
  CHECK(read_doc("\\f-5 x", "{\\f-5 Neg;}").paragraphs[0].runs[0].font == "Neg");
  // \uc in the font table does not leak into the body.
  {
    const auto doc = read_doc("\\u233 ab", "{\\f1\\uc2 \\u26085??X;}");
    CHECK(doc.paragraphs[0].runs[0].text == "\u00e9b");
  }
  // Round trip: the writer escapes the name, the reader decodes it.
  writeit::Document doc;
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.text = "x";
  run.font = nihon + "Gothic";
  paragraph.runs.push_back(run);
  doc.paragraphs.push_back(paragraph);
  writeit::Document back;
  CHECK(writeit::rtf_import(writeit::rtf_export(doc), back));
  CHECK(back == doc);
}

std::string shape(const writeit::Document& doc)
{
  std::string out;
  for (size_t p = 0; p < doc.paragraphs.size(); ++p) {
    if (p > 0)
      out += "|";
    for (const auto& run : doc.paragraphs[p].runs)
      out += run.text;
  }
  return out;
}

// Grok Bot: a newline written as \u10 did not survive save and reopen.
// \u10 and \u13 are line breaks, which this reader makes paragraphs, as it
// does \line. \u9 is a tab. Other control characters are dropped.
void control_characters()
{
  CHECK(shape(read_doc("a\\u10?b")) == "a|b");
  CHECK(shape(read_doc("a\\u13?b")) == "a|b");
  CHECK(shape(read_doc("a\\u13?\\u10?b")) == "a|b");  // CR LF is one break
  CHECK(shape(read_doc("a\\u10?\\u10?b")) == "a||b");
  CHECK(shape(read_doc("a\\'0d\\'0ab")) == "a|b");
  CHECK(shape(read_doc("a\\'0ab")) == "a|b");
  CHECK(shape(read_doc("a\\u9?b")) == "a\tb");
  CHECK(shape(read_doc("a\\'09b")) == "a\tb");
  CHECK(shape(read_doc("a\\u7?b\\u27?c\\u127?d\\u133?e\\u159?f")) == "abcdef");
  CHECK(shape(read_doc("a\\'07b\\'1bc\\'7fd\\'81e")) == "abcde");
  CHECK(shape(read_doc("a\\u0?b")) == "ab");
  // Raw control bytes in the file (not valid RTF) do not get through either.
  CHECK(shape(read_doc(std::string("a\x01" "b\x7f") + "c")) == "abc");
  // Paragraph properties carry over the break, as with \line.
  {
    const auto doc = read_doc("\\li720 a\\u10?b");
    CHECK(doc.paragraphs.size() == 2);
    if (doc.paragraphs.size() == 2)
      CHECK(doc.paragraphs[1].indents.left == 720);
  }
  // Save and reopen keeps the break.
  const auto once = read_doc("one\\u10?two\\u9?three");
  writeit::Document back;
  CHECK(writeit::rtf_import(writeit::rtf_export(once), back));
  CHECK(back == once);
  CHECK(shape(back) == "one|two\tthree");
}

// The writer never emits a raw control character.
void writer_controls()
{
  writeit::Document doc;
  writeit::Paragraph paragraph;
  writeit::Run run;
  run.text = std::string("a\nb\r\nc\rd\te\x01" "f\x7f" "g\x1b") + "h";
  run.font = std::string("Odd\x02") + "Font";
  paragraph.runs.push_back(run);
  doc.paragraphs.push_back(paragraph);
  const std::string rtf = writeit::rtf_export(doc);
  bool raw = false;
  for (const char c : rtf) {
    const auto b = static_cast<unsigned char>(c);
    if ((b < 0x20 && b != '\n') || b == 0x7F)
      raw = true;
  }
  CHECK(!raw);
  // Newlines inside a run are written as breaks, so they come back as
  // paragraphs; the other controls are dropped; the tab is \tab.
  writeit::Document back;
  CHECK(writeit::rtf_import(rtf, back));
  CHECK(shape(back) == "a|b|c|d\tefgh");
  CHECK(back.paragraphs[0].runs[0].font == "OddFont");
  // The writer's own line breaks between control words stay, they are RTF
  // whitespace, but no newline lands inside text.
  CHECK(rtf.find("a\nb") == std::string::npos);
}

void fuzz()
{
  // Deterministic, offline: a small LCG over RTF-ish tokens and raw bytes.
  const char* tokens[] = {"\\u",  "\\uc", "-",     "0",    "1",     "9",   "5",      "6",
                          "?",    " ",    "{",     "}",    "\\'",   "e9",  "3f",     "\\par ",
                          "a",    "\\",   "55357", "56832", "65535", "\\tab", "\xED\xA0", "\xFF",
                          "\xC3", "\x80", "\\*",   "\\f",  "x",     "\xF0\x9F", "\\'", "0", "a", "d", "z", "13", "10", "\x01", "{\\fonttbl{\\f1 ", ";}}", "\\'0", "\\'00", "\x7f"};
  const size_t count = sizeof(tokens) / sizeof(tokens[0]);
  uint32_t seed = 12345;
  auto next = [&]() {
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7FFF;
  };
  int bad = 0;
  int raw = 0;
  for (int round = 0; round < 200000; ++round) {
    std::string body;
    const int length = 1 + static_cast<int>(next() % 24);
    for (int k = 0; k < length; ++k)
      body += tokens[next() % count];
    writeit::Document doc;
    writeit::rtf_import(kHead + body + "}", doc);
    if (!all_valid(doc))
      ++bad;
    // Whatever was read, the writer emits no raw control character.
    for (const char c : writeit::rtf_export(doc)) {
      const auto b = static_cast<unsigned char>(c);
      if ((b < 0x20 && b != '\n') || b == 0x7F) {
        ++raw;
        break;
      }
    }
  }
  std::printf("fuzz: invalid=%d raw-controls-written=%d of 200000\n", bad, raw);
  CHECK(bad == 0);
  CHECK(raw == 0);
}

}  // namespace

// Exactly the checks this suite runs, loops included. Update it with the tests.
constexpr int kChecks = 245;

int main()
{
  scalar_values();
  surrogates();
  fallback_skips();
  raw_bytes();
  hex_escapes();
  font_table();
  control_characters();
  writer_controls();
  fuzz();
  return suite_test::done("rtf-unicode", kChecks);
}
