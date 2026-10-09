/* SPDX-License-Identifier: Unlicense */

// \uN in RTF is a signed 16-bit UTF-16 code unit followed by \ucN fallback
// characters. Whatever the file says, the reader must hand GTK valid UTF-8.

#include "check.hpp"
#include "document.hpp"

#include <cstdint>
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

bool all_valid(const writeit::Document& doc)
{
  for (const auto& paragraph : doc.paragraphs) {
    for (const auto& run : paragraph.runs) {
      if (!valid_utf8(run.text) || !valid_utf8(run.font))
        return false;
    }
  }
  return true;
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

void fuzz()
{
  // Deterministic, offline: a small LCG over RTF-ish tokens and raw bytes.
  const char* tokens[] = {"\\u",  "\\uc", "-",     "0",    "1",     "9",   "5",      "6",
                          "?",    " ",    "{",     "}",    "\\'",   "e9",  "3f",     "\\par ",
                          "a",    "\\",   "55357", "56832", "65535", "\\tab", "\xED\xA0", "\xFF",
                          "\xC3", "\x80", "\\*",   "\\f",  "x",     "\xF0\x9F"};
  const size_t count = sizeof(tokens) / sizeof(tokens[0]);
  uint32_t seed = 12345;
  auto next = [&]() {
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) & 0x7FFF;
  };
  int bad = 0;
  for (int round = 0; round < 20000; ++round) {
    std::string body;
    const int length = 1 + static_cast<int>(next() % 24);
    for (int k = 0; k < length; ++k)
      body += tokens[next() % count];
    writeit::Document doc;
    writeit::rtf_import(kHead + body + "}", doc);
    if (!all_valid(doc))
      ++bad;
  }
  CHECK(bad == 0);
}

}  // namespace

int main()
{
  scalar_values();
  surrogates();
  fallback_skips();
  raw_bytes();
  fuzz();
  return suite_test::done("rtf-unicode");
}
