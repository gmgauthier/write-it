/* SPDX-License-Identifier: Unlicense */

#include "check.hpp"
#include "document.hpp"

constexpr int kChecks = 4;

int pictures(const writeit::Document& doc)
{
  int count = 0;
  for (const writeit::Paragraph& paragraph : doc.paragraphs) {
    for (const writeit::Run& run : paragraph.runs) {
      if (run.image)
        ++count;
    }
  }
  return count;
}

int main()
{
  writeit::Document doc;
  CHECK(writeit::rtf_import(
      "{\\rtf1\\ansi{\\*\\shppict{\\pict\\pngblip 89504e470d0a1a0a}}{\\nonshppict{\\pict\\wmetafile8 "
      "01020304}}}",
      doc));
  CHECK(pictures(doc) == 1);
  CHECK(doc.paragraphs[0].runs[0].image->type == "png" ||
        doc.paragraphs[0].runs[0].image->data.size() >= 8);

  writeit::Document plain;
  CHECK(writeit::rtf_import("{\\rtf1\\ansi{\\pict\\pngblip 89504e470d0a1a0a}}", plain) &&
        pictures(plain) == 1);

  return suite_test::done("rtf-metafile", kChecks);
}
