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
 * The layout is adapted from GXDE KWin's thumbnail_grid window switcher.
 */

#include <absl/log/absl_log.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <QCoreApplication>
#include <QIcon>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QUrl>
#include <QVariant>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "src/view/ssd/ssd_buffer/ssd_buffer.h"
#include "src/view/ssd/window_icon_provider/window_icon_provider.h"
#include "src/view/app_switcher/app_switcher.h"

namespace flakewm {
namespace view {
namespace {

constexpr int kMinimumItemBox = 128;
constexpr int kColumnSpacing = 20;
constexpr int kBoxMargin = 32;
constexpr int kPopupPadding = 70;
constexpr int kMaximumRows = 2;
constexpr int kPanelRadius = 6;
constexpr float kBlurOffset = 5.0F;

bool IgnoreInput(wlr_scene_buffer*, double*, double*) { return false; }

QQmlEngine* Engine() {
  static auto* engine = [] {
    if (QIcon::themeName().isEmpty()) QIcon::setThemeName("hicolor");
    QIcon::setFallbackThemeName("hicolor");
    auto* result = new QQmlEngine(QCoreApplication::instance());
    result->addImageProvider("window-icons", new WindowIconProvider());
    return result;
  }();
  return engine;
}

bool IsForwardedModifier(xkb_keysym_t symbol) {
  switch (symbol) {
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R:
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R:
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R:
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R:
      return true;
    default:
      return false;
  }
}

}  // namespace

class AppSwitcherRenderer final {
 public:
  AppSwitcherRenderer()
      : render_control_(std::make_unique<QQuickRenderControl>()),
        window_(std::make_unique<QQuickWindow>(render_control_.get())) {
    QQmlComponent component(Engine(), QUrl("qrc:/flakewm/app_switcher.qml"));
    if (component.isError()) {
      ABSL_LOG(ERROR) << "Failed to load app switcher QML: "
                      << component.errorString().toStdString();
      return;
    }
    QObject* object = component.create();
    root_item_ = qobject_cast<QQuickItem*>(object);
    if (root_item_ == nullptr) {
      ABSL_LOG(ERROR) << "App switcher QML root is not a QQuickItem";
      delete object;
      return;
    }
    root_item_->setParent(window_.get());
    root_item_->setParentItem(window_->contentItem());
    window_->setColor(Qt::transparent);
    render_requested_ = QObject::connect(render_control_.get(),
                                         &QQuickRenderControl::renderRequested,
                                         [this]() { dirty_ = true; });
    scene_changed_ = QObject::connect(render_control_.get(),
                                      &QQuickRenderControl::sceneChanged,
                                      [this]() { dirty_ = true; });
  }

  ~AppSwitcherRenderer() {
    QObject::disconnect(render_requested_);
    QObject::disconnect(scene_changed_);
    if (initialized_) render_control_->invalidate();
    DropBuffers();
  }

  bool Resize(int width, int height) {
    if (root_item_ == nullptr || width <= 0 || height <= 0) return false;
    if (buffers_[0] != nullptr && buffers_[0]->Handle()->width == width &&
        buffers_[0]->Handle()->height == height) {
      return true;
    }
    std::array<SsdBuffer*, 2> next = {SsdBuffer::Create(width, height),
                                      SsdBuffer::Create(width, height)};
    if (next[0] == nullptr || next[1] == nullptr) {
      for (SsdBuffer* buffer : next) {
        if (buffer != nullptr) wlr_buffer_drop(buffer->Handle());
      }
      return false;
    }
    DropBuffers();
    buffers_ = next;
    current_buffer_ = 0;
    has_frame_ = false;
    window_->setGeometry(0, 0, width, height);
    window_->contentItem()->setSize(QSizeF(width, height));
    root_item_->setSize(QSizeF(width, height));
    if (!initialized_) {
      window_->create();
      initialized_ = true;
    }
    dirty_ = true;
    return true;
  }

  void SetState(const std::vector<AppSwitcher::Entry>& entries,
                int current_index, int cell_size) {
    QVariantList model;
    model.reserve(static_cast<qsizetype>(entries.size()));
    for (const AppSwitcher::Entry& entry : entries) {
      model.push_back(
          QVariantMap{{QStringLiteral("title"), entry.title},
                      {QStringLiteral("appId"), entry.app_id},
                      {QStringLiteral("minimized"), entry.minimized}});
    }
    SetProperty("entries", model);
    SetProperty("currentIndex", current_index);
    SetProperty("cellSize", cell_size);
  }

  bool Render() {
    if (!dirty_ || !initialized_ || buffers_[0] == nullptr) return false;
    dirty_ = false;
    const int next_buffer = has_frame_ ? 1 - current_buffer_ : current_buffer_;
    SsdBuffer* buffer = buffers_[next_buffer];
    buffer->Image().fill(Qt::transparent);
    window_->setRenderTarget(
        QQuickRenderTarget::fromPaintDevice(&buffer->Image()));
    root_item_->update();
    render_control_->polishItems();
    render_control_->sync();
    render_control_->render();
    current_buffer_ = next_buffer;
    has_frame_ = true;
    return true;
  }

  wlr_buffer* Buffer() const {
    return buffers_[current_buffer_] == nullptr
               ? nullptr
               : buffers_[current_buffer_]->Handle();
  }

 private:
  void SetProperty(const char* name, const QVariant& value) {
    if (root_item_ == nullptr || root_item_->property(name) == value) return;
    root_item_->setProperty(name, value);
    dirty_ = true;
  }

  void DropBuffers() {
    for (SsdBuffer*& buffer : buffers_) {
      if (buffer == nullptr) continue;
      wlr_buffer_drop(buffer->Handle());
      buffer = nullptr;
    }
  }

  std::unique_ptr<QQuickRenderControl> render_control_;
  std::unique_ptr<QQuickWindow> window_;
  QQuickItem* root_item_ = nullptr;
  std::array<SsdBuffer*, 2> buffers_ = {};
  QMetaObject::Connection render_requested_;
  QMetaObject::Connection scene_changed_;
  int current_buffer_ = 0;
  bool has_frame_ = false;
  bool initialized_ = false;
  bool dirty_ = true;
};

AppSwitcher::AppSwitcher(wlr_scene_tree* overlay_parent,
                         EntriesProvider entries_provider, Activate activate,
                         ScreenGeometry screen_geometry, SetBlur set_blur,
                         ClearBlur clear_blur)
    : renderer_(std::make_unique<AppSwitcherRenderer>()),
      entries_provider_(std::move(entries_provider)),
      activate_(std::move(activate)),
      screen_geometry_(std::move(screen_geometry)),
      set_blur_(std::move(set_blur)),
      clear_blur_(std::move(clear_blur)) {
  if (overlay_parent == nullptr) return;
  tree_ = wlr_scene_tree_create(overlay_parent);
  if (tree_ == nullptr) return;
  blur_node_ = wlr_scene_buffer_create(tree_, nullptr);
  node_ = wlr_scene_buffer_create(tree_, nullptr);
  if (blur_node_ == nullptr || node_ == nullptr) {
    wlr_scene_node_destroy(&tree_->node);
    tree_ = nullptr;
    blur_node_ = nullptr;
    node_ = nullptr;
    return;
  }
  blur_node_->point_accepts_input = IgnoreInput;
  node_->point_accepts_input = IgnoreInput;
  blur_node_sample_.Connect(&blur_node_->events.output_sample);
  wlr_scene_node_set_enabled(&tree_->node, false);
}

AppSwitcher::~AppSwitcher() {
  active_ = false;
  entries_.clear();
  focus_history_.clear();
  blur_node_sample_.Disconnect();
  ClearRegisteredBlur();
  if (tree_ != nullptr) {
    wlr_scene_node_destroy(&tree_->node);
    tree_ = nullptr;
    blur_node_ = nullptr;
    node_ = nullptr;
  }
  if (blur_buffer_ != nullptr) {
    wlr_buffer_drop(blur_buffer_->Handle());
    blur_buffer_ = nullptr;
  }
  renderer_.reset();
}

bool AppSwitcher::Cycle(bool reverse) {
  if (!entries_provider_ || !activate_ || !screen_geometry_) return false;
  wlr_surface* preferred = nullptr;
  if (active_ && current_index_ >= 0 &&
      current_index_ < static_cast<int>(entries_.size())) {
    preferred = entries_[current_index_].surface;
  }
  RefreshEntries(preferred);
  if (entries_.size() < 2) {
    if (active_) Finish(false);
    return false;
  }
  active_ = true;
  Step(reverse);
  UpdateView();
  return true;
}

bool AppSwitcher::HandleKey(wlr_keyboard* keyboard,
                            const wlr_keyboard_key_event& event) {
  if (!active_) return false;
  if (keyboard == nullptr || keyboard->xkb_state == nullptr) return true;
  const xkb_keysym_t symbol =
      xkb_state_key_get_one_sym(keyboard->xkb_state, event.keycode + 8);

  // Tab must still reach KeyBindingManager so it can pair each press/release
  // and invoke the configured forward or reverse action.
  if (symbol == XKB_KEY_Tab || symbol == XKB_KEY_ISO_Left_Tab) return false;

  if (symbol == XKB_KEY_Escape) {
    if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED) Finish(false);
    return true;
  }
  if (symbol == XKB_KEY_Return || symbol == XKB_KEY_KP_Enter) {
    if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED) Finish(true);
    return true;
  }
  if (event.state == WL_KEYBOARD_KEY_STATE_RELEASED &&
      (symbol == XKB_KEY_Alt_L || symbol == XKB_KEY_Alt_R)) {
    Finish(true);
    return false;
  }
  return !IsForwardedModifier(symbol);
}

void AppSwitcher::SurfaceActivated(wlr_surface* surface) {
  if (surface == nullptr) return;
  std::erase(focus_history_, surface);
  focus_history_.insert(focus_history_.begin(), surface);
}

void AppSwitcher::SurfaceUnavailable(wlr_surface* surface) {
  if (surface == nullptr) return;
  std::erase(focus_history_, surface);
  if (!active_) return;
  wlr_surface* selected = nullptr;
  if (current_index_ >= 0 &&
      current_index_ < static_cast<int>(entries_.size())) {
    selected = entries_[current_index_].surface;
  }
  std::erase_if(entries_, [surface](const Entry& entry) {
    return entry.surface == surface;
  });
  if (entries_.empty()) {
    Finish(false);
    return;
  }
  const auto current = std::find_if(
      entries_.begin(), entries_.end(),
      [selected](const Entry& entry) { return entry.surface == selected; });
  current_index_ =
      current == entries_.end()
          ? std::min(current_index_, static_cast<int>(entries_.size()) - 1)
          : static_cast<int>(current - entries_.begin());
  UpdateView();
}

void AppSwitcher::Cancel() {
  if (active_) Finish(false);
}

bool AppSwitcher::IsActive() const { return active_; }

void AppSwitcher::RefreshEntries(wlr_surface* preferred) {
  std::vector<Entry> available = entries_provider_();
  std::erase_if(available,
                [](const Entry& entry) { return entry.surface == nullptr; });

  entries_.clear();
  entries_.reserve(available.size());
  for (wlr_surface* surface : focus_history_) {
    const auto entry = std::find_if(
        available.begin(), available.end(),
        [surface](const Entry& item) { return item.surface == surface; });
    if (entry == available.end()) continue;
    entries_.push_back(*entry);
    available.erase(entry);
  }
  entries_.insert(entries_.end(), available.begin(), available.end());

  const auto selected = std::find_if(
      entries_.begin(), entries_.end(),
      [preferred](const Entry& entry) { return entry.surface == preferred; });
  if (selected != entries_.end()) {
    current_index_ = static_cast<int>(selected - entries_.begin());
    return;
  }
  const auto focused =
      std::find_if(entries_.begin(), entries_.end(),
                   [](const Entry& entry) { return entry.active; });
  current_index_ = focused == entries_.end()
                       ? 0
                       : static_cast<int>(focused - entries_.begin());
}

void AppSwitcher::Step(bool reverse) {
  const int count = static_cast<int>(entries_.size());
  if (count <= 0) {
    current_index_ = -1;
    return;
  }
  current_index_ = reverse ? (current_index_ + count - 1) % count
                           : (current_index_ + 1) % count;
}

void AppSwitcher::UpdateView() {
  if (!active_ || tree_ == nullptr || node_ == nullptr ||
      renderer_ == nullptr || entries_.empty()) {
    if (tree_ != nullptr) wlr_scene_node_set_enabled(&tree_->node, false);
    ClearRegisteredBlur();
    return;
  }
  const wlr_box screen = screen_geometry_();
  if (screen.width <= 0 || screen.height <= 0) {
    wlr_scene_node_set_enabled(&tree_->node, false);
    ClearRegisteredBlur();
    return;
  }

  const int count = static_cast<int>(entries_.size());
  const int maximum_width =
      std::max(kMinimumItemBox, screen.width - 2 * kPopupPadding);
  const int content_width = std::max(1, maximum_width - 2 * kBoxMargin);
  const int default_cell = kMinimumItemBox + kColumnSpacing;
  int columns = std::max(1, content_width / default_cell);
  columns = std::min(columns, count);
  if (count > columns * kMaximumRows) {
    columns = (count + kMaximumRows - 1) / kMaximumRows;
  }
  const int cell_size = columns * default_cell <= content_width
                            ? default_cell
                            : std::max(48, content_width / columns);
  const int rows = (count + columns - 1) / columns;
  const int width =
      std::min(screen.width, columns * cell_size + 2 * kBoxMargin);
  const int height = std::min(screen.height, rows * cell_size + 2 * kBoxMargin);

  if (!renderer_->Resize(width, height)) {
    wlr_scene_node_set_enabled(&tree_->node, false);
    ClearRegisteredBlur();
    return;
  }
  if (!EnsureBlurBuffer(width, height)) {
    wlr_scene_node_set_enabled(&tree_->node, false);
    ClearRegisteredBlur();
    return;
  }
  renderer_->SetState(entries_, current_index_, cell_size);
  if (renderer_->Render()) {
    wlr_scene_buffer_set_buffer_with_damage(node_, renderer_->Buffer(),
                                            nullptr);
  }
  const int x = screen.x + (screen.width - width) / 2;
  const int y = screen.y + (screen.height - height) / 2;
  wlr_scene_node_set_position(&blur_node_->node, x, y);
  wlr_scene_node_set_position(&node_->node, x, y);
  wlr_scene_node_set_enabled(&tree_->node, true);
  wlr_scene_node_raise_to_top(&tree_->node);
}

void AppSwitcher::Finish(bool activate) {
  wlr_surface* selected = nullptr;
  if (activate && current_index_ >= 0 &&
      current_index_ < static_cast<int>(entries_.size())) {
    selected = entries_[current_index_].surface;
  }
  active_ = false;
  current_index_ = -1;
  entries_.clear();
  if (tree_ != nullptr) wlr_scene_node_set_enabled(&tree_->node, false);
  ClearRegisteredBlur();
  if (selected != nullptr) activate_(selected);
}

bool AppSwitcher::EnsureBlurBuffer(int width, int height) {
  if (blur_node_ == nullptr) return false;
  if (blur_buffer_ != nullptr && blur_buffer_->Handle()->width == width &&
      blur_buffer_->Handle()->height == height) {
    return true;
  }

  SsdBuffer* next = SsdBuffer::Create(width, height);
  if (next == nullptr) return false;
  next->Image().fill(Qt::transparent);
  ClearRegisteredBlur();
  wlr_scene_buffer_set_buffer(blur_node_, next->Handle());
  if (blur_buffer_ != nullptr) wlr_buffer_drop(blur_buffer_->Handle());
  blur_buffer_ = next;
  return true;
}

void AppSwitcher::ClearRegisteredBlur() {
  if (registered_blur_texture_ == nullptr) return;
  registered_blur_texture_ = nullptr;
  if (clear_blur_) clear_blur_(this);
}

void AppSwitcher::OnBlurNodeSample(AppSwitcher* switcher,
                                   wlr_scene_output_sample_event*) {
  if (!switcher->active_ || !switcher->set_blur_ ||
      switcher->blur_node_ == nullptr || switcher->blur_buffer_ == nullptr) {
    return;
  }
  wlr_texture* texture = switcher->blur_node_->WLR_PRIVATE.texture;
  if (texture == nullptr || texture == switcher->registered_blur_texture_) {
    return;
  }

  const int width = switcher->blur_buffer_->Handle()->width;
  const int height = switcher->blur_buffer_->Handle()->height;
  pixman_region32_t region;
  pixman_region32_init(&region);
  for (int y = 0; y < height; ++y) {
    int inset = 0;
    if (y < kPanelRadius) {
      const double dy = static_cast<double>(kPanelRadius - y) - 0.5;
      inset = static_cast<int>(std::ceil(
          kPanelRadius - std::sqrt(kPanelRadius * kPanelRadius - dy * dy)));
    } else if (y >= height - kPanelRadius) {
      const double dy = static_cast<double>(y - (height - kPanelRadius)) + 0.5;
      inset = static_cast<int>(std::ceil(
          kPanelRadius - std::sqrt(kPanelRadius * kPanelRadius - dy * dy)));
    }
    pixman_region32_union_rect(&region, &region, inset, y,
                               std::max(0, width - 2 * inset), 1);
  }
  if (switcher->set_blur_(switcher, texture, &region, kBlurOffset)) {
    switcher->registered_blur_texture_ = texture;
  }
  pixman_region32_fini(&region);
}

}  // namespace view
}  // namespace flakewm
