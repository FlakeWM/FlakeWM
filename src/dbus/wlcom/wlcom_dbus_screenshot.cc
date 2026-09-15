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
 * Originally copyright by (C) 2024 KylinSoft Co., Ltd.
 * Originally copyright by (C) 2026 GXDE Team.
 * Original license: GPL-3.0-or-later, see GXDE Wayland Compositor.
 * Redistributed with GPL-3.0-or-later.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#include "src/dbus/wlcom/wlcom_dbus_manager_p.h"

namespace flakewm {
namespace dbus {

QString WlcomDbusManager::CaptureScreenshot(const QString& output_name,
                                            const wlr_box* requested_area,
                                            bool include_cursor,
                                            bool unscaled) {
  wlr_output* handle =
      output_name.isEmpty() ? nullptr : FindOutput(output_name);
  if (!output_name.isEmpty() && handle == nullptr) return {};

  std::vector<core::CompositorPrivate::Output*> outputs;
  wlr_box capture_box = requested_area == nullptr ? wlr_box{} : *requested_area;
  bool have_capture_box = requested_area != nullptr;
  float capture_scale = 1.0F;
  for (const auto& candidate : compositor_->outputs_) {
    if (candidate->handle != nullptr && candidate->handle->enabled &&
        (handle == nullptr || candidate->handle == handle)) {
      wlr_box output_box = {};
      wlr_output_layout_get_box(compositor_->output_layout_, candidate->handle,
                                &output_box);
      wlr_box intersection = {};
      if (requested_area != nullptr &&
          !wlr_box_intersection(&intersection, requested_area, &output_box)) {
        continue;
      }
      outputs.push_back(candidate.get());
      capture_scale =
          unscaled ? std::max(capture_scale, candidate->handle->scale) : 1.0F;
      if (!have_capture_box) {
        capture_box = output_box;
        have_capture_box = true;
      } else if (requested_area == nullptr) {
        const int x1 = std::min(capture_box.x, output_box.x);
        const int y1 = std::min(capture_box.y, output_box.y);
        const int x2 = std::max(capture_box.x + capture_box.width,
                                output_box.x + output_box.width);
        const int y2 = std::max(capture_box.y + capture_box.height,
                                output_box.y + output_box.height);
        capture_box = {x1, y1, x2 - x1, y2 - y1};
      }
    }
  }
  if (outputs.empty() || !have_capture_box || capture_box.width <= 0 ||
      capture_box.height <= 0) {
    return {};
  }

  const int canvas_width = std::max(
      1, static_cast<int>(std::lround(capture_box.width * capture_scale)));
  const int canvas_height = std::max(
      1, static_cast<int>(std::lround(capture_box.height * capture_scale)));
  QImage canvas(canvas_width, canvas_height, QImage::Format_ARGB32);
  canvas.fill(Qt::transparent);
  QPainter painter(&canvas);
  bool captured = false;

  for (core::CompositorPrivate::Output* output : outputs) {
    if (output->scene_output == nullptr) continue;
    wlr_box output_box = {};
    wlr_output_layout_get_box(compositor_->output_layout_, output->handle,
                              &output_box);
    wlr_box visible = {};
    if (!wlr_box_intersection(&visible, &capture_box, &output_box)) continue;

    compositor_->DamageOutputForBackdropBlur(output, false);
    if (include_cursor) wlr_output_lock_software_cursors(output->handle, true);
    wlr_output_state state;
    wlr_output_state_init(&state);
    const bool built =
        wlr_scene_output_build_state(output->scene_output, &state, nullptr) &&
        (state.committed & WLR_OUTPUT_STATE_BUFFER) != 0 &&
        state.buffer != nullptr;
    if (include_cursor) wlr_output_lock_software_cursors(output->handle, false);
    if (!built) {
      wlr_output_state_finish(&state);
      continue;
    }

    wlr_texture* texture =
        wlr_texture_from_buffer(compositor_->renderer_, state.buffer);
    if (texture == nullptr) {
      wlr_output_state_finish(&state);
      continue;
    }
    QImage source(state.buffer->width, state.buffer->height,
                  QImage::Format_ARGB32);
    const wlr_box source_box{0, 0, state.buffer->width, state.buffer->height};
    const wlr_texture_read_pixels_options options = {
        .data = source.bits(),
        .format = DRM_FORMAT_ARGB8888,
        .stride = static_cast<uint32_t>(source.bytesPerLine()),
        .src_box = source_box};
    const bool read = wlr_texture_read_pixels(texture, &options);
    wlr_texture_destroy(texture);
    wlr_output_state_finish(&state);
    if (!read || output_box.width <= 0 || output_box.height <= 0) continue;

    const QRectF source_rect(
        (visible.x - output_box.x) * source.width() /
            static_cast<double>(output_box.width),
        (visible.y - output_box.y) * source.height() /
            static_cast<double>(output_box.height),
        visible.width * source.width() / static_cast<double>(output_box.width),
        visible.height * source.height() /
            static_cast<double>(output_box.height));
    const QRectF destination_rect((visible.x - capture_box.x) * capture_scale,
                                  (visible.y - capture_box.y) * capture_scale,
                                  visible.width * capture_scale,
                                  visible.height * capture_scale);
    painter.drawImage(destination_rect, source, source_rect);
    captured = true;
  }
  painter.end();
  if (!captured) return {};

  const QString directory = QDir::tempPath();
  QDir().mkpath(directory);
  const QString path =
      directory + QStringLiteral("/flakewm_screenshot_%1.png")
                      .arg(QDateTime::currentDateTime().toString(
                          QStringLiteral("yyyyMMdd_hhmmss_zzz")));
  return canvas.save(path, "PNG") ? path : QString{};
}

bool WlcomDbusManager::HandleScreenshot(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  const QString interface = message.interface();
  if (message.member() == QStringLiteral("CopyFullscreenToClipboard")) {
    const QString path = CaptureScreenshot({}, nullptr, false, false);
    if (path.isEmpty()) {
      Error(message, QStringLiteral("top.gxde.Wlcom.Screenshot.Error.Failed"),
            QStringLiteral("Failed to capture fullscreen"));
      return true;
    }
    QFile file(path);
    const bool copied = file.open(QIODevice::ReadOnly) &&
                        SetClipboardPng(compositor_->display_,
                                        compositor_->seat_, file.readAll());
    QFile::remove(path);
    if (!copied)
      Error(message, QStringLiteral("top.gxde.Wlcom.Screenshot.Error.Failed"),
            QStringLiteral("Failed to copy fullscreen to clipboard"));
    else {
      clipboard_pid_ = static_cast<int>(getpid());
      EmitSignal(QStringLiteral("/Clipboard"),
                 QStringLiteral("org.kde.KWin.Clipboard"),
                 QStringLiteral("clipboardSelectionPidChanged"),
                 {clipboard_pid_});
      Reply(message);
    }
    return true;
  }
  QString output_name;
  bool include_cursor = false;
  bool unscaled = false;
  std::optional<wlr_box> area;
  if (message.member() == QStringLiteral("screenshotFullscreen")) {
    include_cursor = !args.isEmpty() && args[0].toBool();
  } else if (message.member() == QStringLiteral("screenshotFull")) {
    unscaled = args.value(0).toBool();
    include_cursor = args.value(1).toBool();
  } else if (message.member() == QStringLiteral("screenshotOutput")) {
    output_name = args.value(0).toString();
    unscaled = args.value(1).toBool();
    include_cursor = args.value(2).toBool();
    if (FindOutput(output_name) == nullptr) {
      Error(message,
            QStringLiteral("org.ukui.kwin.Screenshot.Error.InvalidOutput"),
            QStringLiteral("Invalid output requested"));
      return true;
    }
  } else if (message.member() == QStringLiteral("screenshotArea")) {
    if (args.size() != 6 || args[2].toInt() <= 0 || args[3].toInt() <= 0) {
      Error(message, kInvalidArgs, "Invalid screenshot area.");
      return true;
    }
    area = wlr_box{args[0].toInt(), args[1].toInt(), args[2].toInt(),
                   args[3].toInt()};
    unscaled = args[4].toBool();
    include_cursor = args[5].toBool();
  } else {
    return false;
  }
  const QString path = CaptureScreenshot(output_name, area ? &*area : nullptr,
                                         include_cursor, unscaled);
  if (path.isEmpty()) {
    const QString prefix =
        interface == QStringLiteral("org.kde.kwin.Screenshot")
            ? QStringLiteral("org.kde.kwin.Screenshot.Error.Failed")
            : QStringLiteral("org.ukui.kwin.Screenshot.Error.Cancelled");
    Error(message, prefix, QStringLiteral("Screenshot got cancelled"));
  } else {
    Reply(message, {path});
  }
  return true;
}

WlcomDbusManager::Watermark* WlcomDbusManager::FindWatermark(
    const QString& id) const {
  for (const auto& watermark : watermarks_)
    if (watermark->id == id) return watermark.get();
  return nullptr;
}

void WlcomDbusManager::RebuildWatermark(Watermark* watermark) {
  if (watermark == nullptr || compositor_ == nullptr) return;
  for (Watermark::Entry& entry : watermark->entries) {
    if (entry.scene != nullptr) wlr_scene_node_destroy(&entry.scene->node);
    if (entry.buffer != nullptr) wlr_buffer_drop(entry.buffer);
  }
  watermark->entries.clear();

  QString file = watermark->info.value(QStringLiteral("file")).toString();
  if (file.startsWith(QStringLiteral("~/")))
    file = QDir::homePath() + file.sliced(1);
  if (file.isEmpty()) return;
  QImageReader reader(file);
  reader.setAutoTransform(true);
  QImage source = reader.read();
  if (source.isNull()) {
    ABSL_LOG(WARNING) << "Unable to load watermark image " << file.toStdString()
                      << ": " << reader.errorString().toStdString();
    return;
  }
  source = source.convertToFormat(QImage::Format_ARGB32_Premultiplied);

  const int expand = watermark->info.value(QStringLiteral("expand")).toInt();
  const bool topmost =
      watermark->info.value(QStringLiteral("topmost")).toBool();
  const float opacity = static_cast<float>(std::clamp(
      watermark->info.value(QStringLiteral("opacity")).toDouble(), 0.0, 1.0));
  wlr_scene_tree* parent =
      compositor_
          ->shell_layer_trees_[topmost ? ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY
                                       : ZWLR_LAYER_SHELL_V1_LAYER_TOP];
  if (parent == nullptr) return;

  for (const auto& wrapper : compositor_->outputs_) {
    wlr_output* output = wrapper->handle;
    if (output == nullptr || !output->enabled) continue;
    wlr_box box = {};
    wlr_output_layout_get_box(compositor_->output_layout_, output, &box);
    if (box.width <= 0 || box.height <= 0) continue;

    QImage rendered = source;
    int x = box.x;
    int y = box.y;
    int width = source.width();
    int height = source.height();
    if (expand == 0) {
      x += watermark->info.value(QStringLiteral("x")).toInt();
      y += watermark->info.value(QStringLiteral("y")).toInt();
    } else if (expand == 1) {
      rendered =
          QImage(box.width, box.height, QImage::Format_ARGB32_Premultiplied);
      rendered.fill(Qt::transparent);
      QPainter painter(&rendered);
      for (int tile_y = 0; tile_y < rendered.height();
           tile_y += source.height())
        for (int tile_x = 0; tile_x < rendered.width();
             tile_x += source.width())
          painter.drawImage(tile_x, tile_y, source);
      width = box.width;
      height = box.height;
    } else if (expand == 2) {
      width = box.width;
      height = box.height;
    } else if (expand == 3) {
      const double output_ratio = static_cast<double>(box.width) / box.height;
      const double image_ratio =
          static_cast<double>(source.width()) / std::max(1, source.height());
      if (output_ratio < image_ratio) {
        width = box.width;
        height =
            std::max(1, static_cast<int>(std::lround(width / image_ratio)));
        y += (box.height - height) / 2;
      } else {
        height = box.height;
        width =
            std::max(1, static_cast<int>(std::lround(height * image_ratio)));
        x += (box.width - width) / 2;
      }
    } else {
      continue;
    }

    view::SsdBuffer* image_buffer =
        view::SsdBuffer::Create(rendered.width(), rendered.height());
    if (image_buffer == nullptr) continue;
    image_buffer->Image() = rendered;
    wlr_scene_buffer* scene =
        wlr_scene_buffer_create(parent, image_buffer->Handle());
    if (scene == nullptr) {
      wlr_buffer_drop(image_buffer->Handle());
      continue;
    }
    wlr_scene_node_set_position(&scene->node, x, y);
    wlr_scene_buffer_set_dest_size(scene, width, height);
    wlr_scene_buffer_set_opacity(scene, opacity);
    wlr_scene_node_raise_to_top(&scene->node);
    watermark->entries.push_back(
        Watermark::Entry{output, scene, image_buffer->Handle()});
    wlr_output_schedule_frame(output);
  }
}

void WlcomDbusManager::RebuildWatermarks() {
  for (auto& watermark : watermarks_) RebuildWatermark(watermark.get());
}

bool WlcomDbusManager::HandleWatermark(const QDBusMessage& message) {
  const QVariantList args = message.arguments();
  auto value = [&](int file_index) {
    return QJsonObject{{"file", args.value(file_index).toString()},
                       {"opacity", args.value(file_index + 1).toDouble()},
                       {"x", args.value(file_index + 2).toInt()},
                       {"y", args.value(file_index + 3).toInt()},
                       {"expand", args.value(file_index + 4).toInt()},
                       {"topmost", args.value(file_index + 5).toBool()}};
  };
  if (message.interface() == QStringLiteral("org.ukui.kwin.Watermark")) {
    Watermark* watermark = FindWatermark(QStringLiteral("legacy"));
    if (watermark == nullptr) {
      auto item = std::make_unique<Watermark>();
      item->id = QStringLiteral("legacy");
      watermark = item.get();
      watermarks_.push_back(std::move(item));
    }
    if (message.member() == QStringLiteral("updateWatermark") &&
        args.size() == 2) {
      watermark->info = QJsonObject{{"file", args[0].toString()},
                                    {"opacity", args[1].toDouble()},
                                    {"x", 0},
                                    {"y", 0},
                                    {"expand", 2},
                                    {"topmost", true}};
    } else if (message.member() == QStringLiteral("updateWatermarkEx") &&
               args.size() == 6) {
      watermark->info = value(0);
    } else
      return false;
    RebuildWatermark(watermark);
    Reply(message);
    return true;
  }
  if (message.member() == QStringLiteral("createWatermark")) {
    if (args.size() != 6) {
      Error(message, kInvalidArgs, "Invalid watermark.");
      return true;
    }
    auto watermark = std::make_unique<Watermark>();
    watermark->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    watermark->info = value(0);
    const QString id = watermark->id;
    RebuildWatermark(watermark.get());
    watermarks_.push_back(std::move(watermark));
    Reply(message, {id});
    return true;
  }
  if (message.member() == QStringLiteral("updateWatermark")) {
    const QString id = args.value(0).toString();
    Watermark* watermark = FindWatermark(id);
    if (args.size() != 7 || watermark == nullptr)
      Reply(message, {false});
    else {
      watermark->info = value(1);
      RebuildWatermark(watermark);
      Reply(message, {true});
    }
    return true;
  }
  if (message.member() == QStringLiteral("destroyWatermark")) {
    const QString id = args.value(0).toString();
    auto it = std::find_if(
        watermarks_.begin(), watermarks_.end(),
        [&](const auto& watermark) { return watermark->id == id; });
    if (it != watermarks_.end()) {
      (*it)->info = QJsonObject{};
      RebuildWatermark(it->get());
      watermarks_.erase(it);
    }
    Reply(message);
    return true;
  }
  return false;
}

}  // namespace dbus
}  // namespace flakewm
