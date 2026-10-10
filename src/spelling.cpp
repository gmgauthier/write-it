/* SPDX-License-Identifier: Unlicense */

#include "spelling.hpp"

#include <glibmm/spawn.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace writeit {
namespace {

std::string quoted(const std::string& text)
{
  return "\u201c" + text + "\u201d";
}

bool letter_byte(unsigned char c)
{
  return std::isalpha(c) || c >= 0x80;
}

bool word_join(unsigned char c)
{
  return c == '\'' || c == '-';
}

std::string paragraph_text(const Paragraph& paragraph)
{
  std::string text;
  for (const Run& run : paragraph.runs) {
    if (run.image.has_value())
      continue;
    text += run.text;
  }
  return text;
}

std::string first_line(const std::string& text)
{
  const size_t end = text.find('\n');
  std::string line = text.substr(0, end);
  if (!line.empty() && line.back() == '\r')
    line.pop_back();
  return line;
}

// write(2) to a pipe whose reader has exited raises SIGPIPE and kills the
// process before the call returns. Block it, then drop the pending signal,
// so a dead aspell is an error instead.
ssize_t write_pipe(int fd, const char* data, size_t len)
{
  sigset_t block;
  sigemptyset(&block);
  sigaddset(&block, SIGPIPE);
  sigset_t old;
  pthread_sigmask(SIG_BLOCK, &block, &old);
  const ssize_t n = ::write(fd, data, len);
  const int err = errno;
  sigset_t pending;
  if (sigpending(&pending) == 0 && sigismember(&pending, SIGPIPE)) {
    const timespec zero{0, 0};
    sigtimedwait(&block, nullptr, &zero);
  }
  pthread_sigmask(SIG_SETMASK, &old, nullptr);
  errno = err;
  return n;
}

std::string drain(int fd)
{
  if (fd < 0)
    return {};
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0)
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  std::string out;
  char buf[512];
  while (true) {
    const ssize_t n = ::read(fd, buf, sizeof buf);
    if (n <= 0)
      break;
    out.append(buf, static_cast<size_t>(n));
  }
  return out;
}

}  // namespace

std::string fold_word(const std::string& word)
{
  std::string out = word;
  for (char& c : out) {
    const auto u = static_cast<unsigned char>(c);
    if (u < 128)
      c = static_cast<char>(std::tolower(u));
  }
  return out;
}

bool dictionary_name_ok(const std::string& name)
{
  if (name.empty() || name.size() > 40)
    return false;
  for (unsigned char c : name) {
    if (!std::isalnum(c) && c != '_' && c != '-')
      return false;
  }
  return true;
}

std::vector<SpellMiss> spelling_misses(const std::vector<Paragraph>& paragraphs,
                                       const std::function<bool(const std::string&)>& known)
{
  std::vector<SpellMiss> misses;
  if (!known)
    return misses;
  for (size_t p = 0; p < paragraphs.size(); ++p) {
    const std::string text = paragraph_text(paragraphs[p]);
    size_t i = 0;
    while (i < text.size()) {
      if (!letter_byte(static_cast<unsigned char>(text[i]))) {
        ++i;
        continue;
      }
      const size_t begin = i;
      ++i;
      while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (letter_byte(c)) {
          ++i;
          continue;
        }
        // An apostrophe or a hyphen is part of the word only with a letter
        // after it, so a trailing mark stays punctuation.
        if (word_join(c) && i + 1 < text.size() &&
            letter_byte(static_cast<unsigned char>(text[i + 1]))) {
          i += 2;
          continue;
        }
        break;
      }
      SpellMiss miss;
      miss.paragraph = p;
      miss.begin = begin;
      miss.end = i;
      miss.word = text.substr(begin, i - begin);
      if (!known(miss.word))
        misses.push_back(std::move(miss));
    }
  }
  return misses;
}

bool replace_word(Paragraph& paragraph, size_t begin, size_t end, const std::string& replacement)
{
  if (end < begin)
    return false;
  const std::string text = paragraph_text(paragraph);
  if (end > text.size())
    return false;
  bool inserted = false;
  size_t cursor = 0;
  for (Run& run : paragraph.runs) {
    if (run.image.has_value())
      continue;
    const size_t run_begin = cursor;
    const size_t run_end = cursor + run.text.size();
    cursor = run_end;
    if (end <= run_begin || begin >= run_end)
      continue;
    const size_t local_begin = begin > run_begin ? begin - run_begin : 0;
    const size_t local_end = std::min(end, run_end) - run_begin;
    if (!inserted) {
      run.text.replace(local_begin, local_end - local_begin, replacement);
      inserted = true;
    } else {
      run.text.erase(local_begin, local_end - local_begin);
    }
  }
  return inserted || begin == end;
}

struct AspellDictionary::Impl {
  Glib::Pid pid = 0;
  int in_fd = -1;
  int out_fd = -1;
  int err_fd = -1;
  bool ok = false;
  std::string error;
  std::string pending;

  ~Impl()
  {
    if (in_fd >= 0)
      ::close(in_fd);
    if (out_fd >= 0)
      ::close(out_fd);
    if (err_fd >= 0)
      ::close(err_fd);
    if (pid > 0) {
      kill(pid, SIGTERM);
      int status = 0;
      waitpid(pid, &status, 0);
      Glib::spawn_close_pid(pid);
    }
  }

  bool read_line(std::string& line, int timeout_ms)
  {
    line.clear();
    while (true) {
      const size_t nl = pending.find('\n');
      if (nl != std::string::npos) {
        line = pending.substr(0, nl);
        if (!line.empty() && line.back() == '\r')
          line.pop_back();
        pending.erase(0, nl + 1);
        return true;
      }
      pollfd ready{};
      ready.fd = out_fd;
      ready.events = POLLIN;
      if (poll(&ready, 1, timeout_ms) <= 0)
        return false;
      char buf[512];
      const ssize_t n = ::read(out_fd, buf, sizeof buf);
      if (n <= 0)
        return false;
      pending.append(buf, static_cast<size_t>(n));
    }
  }

  std::vector<std::string> exchange(const std::string& word)
  {
    // A leading ^ is text to check, not an aspell command. A word that
    // itself started with * would otherwise be added to the dictionary.
    const std::string message = "^" + word + "\n";
    size_t wrote = 0;
    while (wrote < message.size()) {
      const ssize_t n = write_pipe(in_fd, message.data() + wrote, message.size() - wrote);
      if (n <= 0) {
        ok = false;
        return {};
      }
      wrote += static_cast<size_t>(n);
    }
    std::vector<std::string> lines;
    while (lines.size() < 32) {
      std::string line;
      if (!read_line(line, 2000)) {
        ok = false;
        break;
      }
      if (line.empty())
        break;
      lines.push_back(std::move(line));
    }
    return lines;
  }
};

AspellDictionary::AspellDictionary(const std::string& name)
    : impl_(std::make_unique<Impl>())
{
  if (!dictionary_name_ok(name)) {
    impl_->error = "Could not open the dictionary " + quoted(name) + ".";
    return;
  }
  const std::vector<std::string> argv = {"aspell", "-a", "--encoding=utf-8", "-d", name};
  try {
    Glib::spawn_async_with_pipes(
        ".", argv, Glib::SPAWN_SEARCH_PATH | Glib::SPAWN_DO_NOT_REAP_CHILD, [] {}, &impl_->pid,
        &impl_->in_fd, &impl_->out_fd, &impl_->err_fd);
  } catch (const Glib::Error&) {
    impl_->error = "Could not find aspell.";
    return;
  }
  std::string greeting;
  if (!impl_->read_line(greeting, 2000) || greeting.compare(0, 4, "@(#)") != 0) {
    const std::string detail = first_line(drain(impl_->err_fd));
    impl_->error = detail.empty() ? "Could not open the dictionary " + quoted(name) + "." : detail;
    return;
  }
  impl_->ok = true;
}

AspellDictionary::~AspellDictionary() = default;

bool AspellDictionary::available() const
{
  return impl_->ok;
}

const std::string& AspellDictionary::error() const
{
  return impl_->error;
}

bool AspellDictionary::contains(const std::string& word)
{
  if (!impl_->ok)
    return true;
  const std::vector<std::string> lines = impl_->exchange(word);
  for (const std::string& line : lines) {
    if (!line.empty() && (line[0] == '&' || line[0] == '#'))
      return false;
  }
  return true;
}

std::vector<std::string> AspellDictionary::suggestions(const std::string& word)
{
  std::vector<std::string> out;
  if (!impl_->ok)
    return out;
  const std::vector<std::string> lines = impl_->exchange(word);
  for (const std::string& line : lines) {
    if (line.empty() || line[0] != '&')
      continue;
    const size_t colon = line.find(": ");
    if (colon == std::string::npos)
      continue;
    size_t i = colon + 2;
    while (i < line.size() && out.size() < 9) {
      const size_t comma = line.find(", ", i);
      const size_t stop = comma == std::string::npos ? line.size() : comma;
      if (stop > i)
        out.push_back(line.substr(i, stop - i));
      if (comma == std::string::npos)
        break;
      i = comma + 2;
    }
  }
  return out;
}

}  // namespace writeit
