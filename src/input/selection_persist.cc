/*
 * Copyright (C) 2026 CharOfString <root@charofstring.cc>
 *
 * This file is part of FLAKEWM.
 *
 * FLAKEWM is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * FLAKEWM is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * FLAKEWM. If not, see <https://www.gnu.org/licenses/>.
 * ----------------------------------------------------------------------------
 * Originally copyright by (C) 2026 GXDE Team.
 * Original license: GPL-3.0-or-later, see GXDE Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#include "src/input/selection_persist.h"

#include <absl/log/absl_log.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace flakewm {
namespace input {
namespace {

constexpr std::size_t kMaxSelectionSize = std::size_t{64} * 1024 * 1024;

// X11 pseudo-targets that XWayland bridges into the offer
constexpr std::array<std::string_view, 7> kIgnoredMimeTypes = {
    "TARGETS", "MULTIPLE",         "SAVE_TARGETS",    "TIMESTAMP",
    "DELETE",  "INSERT_SELECTION", "INSERT_PROPERTY",
};

struct Entry;

// Streams a cached copy into one reader's pipe
struct Writer {
  Entry* entry;
  int fd;
  std::size_t offset = 0;
  wl_event_source* event = nullptr;
};

// One mime type of the selection, read from its owner through a pipe.
struct Entry {
  SelectionPersist::Selection* selection;
  std::string mime_type;
  std::string data;
  bool complete = false;
  bool failed = false;
  int fd = -1;  // Read end while the owner is still writing.
  wl_event_source* event = nullptr;
  std::vector<std::unique_ptr<Writer>> writers;
};

}  // namespace

struct SelectionPersist::Selection {
  wlr_data_source base;  // Must stay first; see FromSource().
  SelectionPersist* persist;
  wl_event_loop* loop;
  std::vector<std::unique_ptr<Entry>> entries;
  std::size_t total = 0;
  bool owned = false;  // Installed as the seat's selection.
  wl_event_source* install = nullptr;

  static Selection* FromSource(wlr_data_source* source) {
    return reinterpret_cast<Selection*>(source);
  }
};

namespace {

using Selection = SelectionPersist::Selection;

void DestroyWriter(Writer* writer) {
  if (writer->event != nullptr) {
    wl_event_source_remove(writer->event);
  }
  close(writer->fd);
  std::erase_if(writer->entry->writers,
                [writer](const auto& item) { return item.get() == writer; });
}

// Returns true once the writer is done
bool FlushWriter(Writer* writer) {
  const std::string& data = writer->entry->data;
  while (writer->offset < data.size()) {
    const ssize_t n = write(writer->fd, data.data() + writer->offset,
                            data.size() - writer->offset);
    if (n > 0) {
      writer->offset += static_cast<std::size_t>(n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else {
      return !(n < 0 && errno == EAGAIN);
    }
  }
  return true;
}

int OnWritable(int, uint32_t mask, void* data) {
  auto* writer = static_cast<Writer*>(data);
  if (FlushWriter(writer) || (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) != 0) {
    DestroyWriter(writer);
  }
  return 0;
}

void FinishRead(Entry* entry) {
  if (entry->event != nullptr) {
    wl_event_source_remove(entry->event);
    entry->event = nullptr;
  }
  if (entry->fd >= 0) {
    close(entry->fd);
    entry->fd = -1;
  }
}

// Reads whatever the owner has written so far
// EOF -> complete
void Drain(Entry* entry) {
  Selection* selection = entry->selection;
  std::array<char, 16384> chunk;
  while (entry->fd >= 0) {
    const ssize_t n = read(entry->fd, chunk.data(), chunk.size());
    if (n > 0) {
      if (selection->total + static_cast<std::size_t>(n) > kMaxSelectionSize) {
        ABSL_LOG(WARNING) << "Clipboard: " << entry->mime_type
                          << " exceeds the cache limit, not keeping it";
        entry->failed = true;
        FinishRead(entry);
        return;
      }
      entry->data.append(chunk.data(), static_cast<std::size_t>(n));
      selection->total += static_cast<std::size_t>(n);
    } else if (n < 0 && errno == EINTR) {
      continue;
    } else if (n < 0 && errno == EAGAIN) {
      return;  // The owner has not finished writing yet.
    } else {
      entry->complete = n == 0;
      entry->failed = n < 0;
      FinishRead(entry);
    }
  }
}

int OnReadable(int, uint32_t, void* data) {
  Drain(static_cast<Entry*>(data));
  return 0;
}

void DestroyEntry(Entry* entry) {
  while (!entry->writers.empty()) {
    DestroyWriter(entry->writers.back().get());
  }
  FinishRead(entry);
}

void SendSelection(wlr_data_source* source, const char* mime_type, int32_t fd) {
  Selection* selection = Selection::FromSource(source);
  auto found = std::find_if(
      selection->entries.begin(), selection->entries.end(),
      [mime_type](const auto& entry) { return entry->mime_type == mime_type; });
  if (found == selection->entries.end()) {
    close(fd);
    return;
  }

  // Only installed copies are sent, and their data no longer changes.
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  auto writer =
      std::make_unique<Writer>(Writer{.entry = found->get(), .fd = fd});
  if (FlushWriter(writer.get())) {
    close(fd);
    return;
  }
  writer->event = wl_event_loop_add_fd(selection->loop, fd, WL_EVENT_WRITABLE,
                                       OnWritable, writer.get());
  if (writer->event == nullptr) {
    close(fd);
    return;
  }
  (*found)->writers.push_back(std::move(writer));
}

void DestroySelection(wlr_data_source* source) {
  Selection* selection = Selection::FromSource(source);
  if (selection->install != nullptr) {
    wl_event_source_remove(selection->install);
  }
  if (selection->persist != nullptr) {
    selection->persist->ForgetSelection(selection);
  }
  for (const auto& entry : selection->entries) {
    DestroyEntry(entry.get());
  }
  delete selection;
}

const wlr_data_source_impl kSelectionImpl = {
    .send = SendSelection,
    .destroy = DestroySelection,
};

// Starts reading every mime type of `source`; nullptr if there is none.
Selection* CreateSelection(SelectionPersist* persist, wl_event_loop* loop,
                           wlr_data_source* source) {
  auto* selection = new Selection{.persist = persist, .loop = loop};
  wlr_data_source_init(&selection->base, &kSelectionImpl);

  const auto* mime_types = static_cast<char* const*>(source->mime_types.data);
  const std::size_t count = source->mime_types.size / sizeof(char*);
  for (const char* mime_type : std::span(mime_types, count)) {
    if (std::find(kIgnoredMimeTypes.begin(), kIgnoredMimeTypes.end(),
                  mime_type) != kIgnoredMimeTypes.end()) {
      continue;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) {
      ABSL_LOG(WARNING) << "Clipboard: failed to create a pipe for "
                        << mime_type;
      continue;
    }
    // Only the read end is ours; the owner keeps blocking-write semantics.
    const int flags = fcntl(fds[0], F_GETFL, 0);
    if (flags >= 0) fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);

    auto entry = std::make_unique<Entry>(
        Entry{.selection = selection, .mime_type = mime_type, .fd = fds[0]});
    entry->event = wl_event_loop_add_fd(loop, fds[0], WL_EVENT_READABLE,
                                        OnReadable, entry.get());
    if (entry->event == nullptr) {
      close(fds[0]);
      close(fds[1]);
      continue;
    }
    selection->entries.push_back(std::move(entry));
    // wlroots closes the write end once it has handed it to the owner.
    wlr_data_source_send(source, mime_type, fds[1]);
  }

  if (selection->entries.empty()) {
    selection->persist = nullptr;
    wlr_data_source_destroy(&selection->base);
    return nullptr;
  }
  return selection;
}

}  // namespace

SelectionPersist::SelectionPersist(wl_display* display, wlr_seat* seat)
    : display_(display),
      loop_(wl_display_get_event_loop(display)),
      seat_(seat) {
  set_selection_.Connect(&seat_->events.set_selection);
}

SelectionPersist::~SelectionPersist() {
  set_selection_.Disconnect();
  if (cache_ == nullptr) {
    return;
  }
  if (cache_->owned) {
    cache_->persist = nullptr;
  } else {
    Selection* cache = cache_;
    cache_ = nullptr;
    cache->persist = nullptr;
    wlr_data_source_destroy(&cache->base);
  }
}

void SelectionPersist::ForgetSelection(const Selection* selection) {
  if (cache_ == selection) {
    cache_ = nullptr;
  }
}

void SelectionPersist::OnSetSelection(SelectionPersist* persist, void*) {
  wlr_data_source* source = persist->seat_->selection_source;
  if (source != nullptr && source->impl == &kSelectionImpl) {
    return;
  }

  if (source == nullptr) {
    // The owner is gone.
    Selection* cache = persist->cache_;
    if (cache != nullptr && !cache->owned && cache->install == nullptr) {
      cache->install = wl_event_loop_add_idle(persist->loop_, Install, cache);
    }
    return;
  }

  if (persist->cache_ != nullptr) {
    wlr_data_source_destroy(&persist->cache_->base);
  }
  persist->cache_ = CreateSelection(persist, persist->loop_, source);
}

void SelectionPersist::Install(void* data) {
  auto* selection = static_cast<Selection*>(data);
  SelectionPersist* persist = selection->persist;
  selection->install = nullptr;
  if (persist == nullptr || persist->seat_->selection_source != nullptr) {
    return;
  }

  // The owner's exit closed its write ends
  for (const auto& entry : selection->entries) {
    Drain(entry.get());
  }
  std::erase_if(selection->entries, [](const auto& entry) {
    const bool keep = entry->complete && !entry->failed && !entry->data.empty();
    if (!keep) DestroyEntry(entry.get());
    return !keep;
  });
  for (const auto& entry : selection->entries) {
    char* mime_type = strdup(entry->mime_type.c_str());
    auto** slot = static_cast<char**>(
        wl_array_add(&selection->base.mime_types, sizeof(char*)));
    if (mime_type == nullptr || slot == nullptr) {
      free(mime_type);
      if (slot != nullptr) selection->base.mime_types.size -= sizeof(char*);
      break;
    }
    *slot = mime_type;
  }
  if (selection->base.mime_types.size == 0) {
    wlr_data_source_destroy(&selection->base);
    return;
  }

  selection->owned = true;
  wlr_seat_set_selection(persist->seat_, &selection->base,
                         wl_display_next_serial(persist->display_));
  ABSL_LOG(INFO) << "Clipboard kept "
                 << selection->base.mime_types.size / sizeof(char*)
                 << " mime type(s), " << selection->total
                 << " bytes after its owner exited";
}

}  // namespace input
}  // namespace flakewm
