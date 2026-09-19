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

#ifndef SRC_DBUS_WLCOM_WLCOM_DBUS_MANAGER_P_H_
#define SRC_DBUS_WLCOM_WLCOM_DBUS_MANAGER_P_H_

#include <absl/log/absl_log.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <malloc.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

extern "C" {
#include <wlr/interfaces/wlr_keyboard.h>
}

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeySequence>
#include <QMap>
#include <QPainter>
#include <QProcess>
#include <QStandardPaths>
#include <QUuid>

#ifdef FLAKEWM_HAS_QGSETTINGS
#include <QGSettings/QGSettings>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

#include "src/core/compositor_private/compositor_private.h"
#include "src/dbus/wlcom/wlcom_dbus_manager.h"
#include "src/input/key_binding_manager.h"
#include "src/render/backdrop_blur_renderer.h"
#include "src/view/ssd/ssd_buffer/ssd_buffer.h"

namespace flakewm {
namespace dbus {

namespace {

uint32_t MonotonicTimeMsec() {
  timespec now = {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint32_t>(now.tv_sec * 1000ULL + now.tv_nsec / 1000000ULL);
}

struct ClipboardSource;

struct ClipboardWriter {
  ClipboardSource* source = nullptr;
  size_t offset = 0;
  int fd = -1;
  wl_event_source* event = nullptr;
  wl_list link = {};
};

struct ClipboardSource {
  // Must remain first: wlroots passes this member back to the implementation.
  wlr_data_source base = {};
  wl_event_loop* loop = nullptr;
  QByteArray data;
  wl_list writers = {};
};

void DestroyClipboardWriter(ClipboardWriter* writer) {
  if (writer == nullptr) return;
  if (writer->event != nullptr) wl_event_source_remove(writer->event);
  if (writer->fd >= 0) close(writer->fd);
  wl_list_remove(&writer->link);
  delete writer;
}

bool FlushClipboardWriter(ClipboardWriter* writer) {
  while (writer->offset < static_cast<size_t>(writer->source->data.size())) {
    const char* data = writer->source->data.constData() + writer->offset;
    const size_t remaining =
        static_cast<size_t>(writer->source->data.size()) - writer->offset;
    const ssize_t written = write(writer->fd, data, remaining);
    if (written > 0) {
      writer->offset += static_cast<size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) continue;
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false;
    return true;
  }
  return true;
}

int ClipboardWriterReady(int, uint32_t mask, void* data) {
  auto* writer = static_cast<ClipboardWriter*>(data);
  if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) != 0 ||
      FlushClipboardWriter(writer))
    DestroyClipboardWriter(writer);
  return 0;
}

void SendClipboardData(wlr_data_source* base, const char*, int32_t fd) {
  auto* source = reinterpret_cast<ClipboardSource*>(base);
  auto* writer = new ClipboardWriter;
  writer->source = source;
  writer->fd = fd;
  wl_list_insert(&source->writers, &writer->link);

  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  if (FlushClipboardWriter(writer)) {
    DestroyClipboardWriter(writer);
    return;
  }
  writer->event = wl_event_loop_add_fd(source->loop, fd, WL_EVENT_WRITABLE,
                                       ClipboardWriterReady, writer);
  if (writer->event == nullptr) DestroyClipboardWriter(writer);
}

void DestroyClipboardSource(wlr_data_source* base) {
  auto* source = reinterpret_cast<ClipboardSource*>(base);
  ClipboardWriter* writer = nullptr;
  ClipboardWriter* temporary = nullptr;
  wl_list_for_each_safe(writer, temporary, &source->writers, link) {
    DestroyClipboardWriter(writer);
  }
  delete source;
}

const wlr_data_source_impl kClipboardSourceImpl = {
    .send = SendClipboardData,
    .destroy = DestroyClipboardSource,
};

bool SetClipboardPng(wl_display* display, wlr_seat* seat,
                     const QByteArray& png) {
  if (display == nullptr || seat == nullptr || png.isEmpty()) return false;
  auto* source = new ClipboardSource;
  source->loop = wl_display_get_event_loop(display);
  source->data = png;
  wl_list_init(&source->writers);
  wlr_data_source_init(&source->base, &kClipboardSourceImpl);
  auto** mime = static_cast<char**>(
      wl_array_add(&source->base.mime_types, sizeof(char*)));
  if (mime == nullptr || (*mime = strdup("image/png")) == nullptr) {
    if (mime != nullptr) source->base.mime_types.size -= sizeof(char*);
    wlr_data_source_destroy(&source->base);
    return false;
  }
  wlr_seat_set_selection(seat, &source->base, wl_display_next_serial(display));
  return true;
}

bool GtkThemeInstalled(const QString& name) {
  if (name.isEmpty() || name.contains(QDir::separator())) return false;
  QStringList roots =
      QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
  roots.prepend(QDir::homePath() + QStringLiteral("/.themes"));
  for (const QString& root : roots) {
    const QString directory = root.endsWith(QStringLiteral(".themes"))
                                  ? root + QLatin1Char('/') + name
                                  : root + QStringLiteral("/themes/") + name;
    for (const QString& file :
         {QStringLiteral("gtk-2.0/gtkrc"), QStringLiteral("gtk-3.0/gtk.css"),
          QStringLiteral("gtk-4.0/gtk.css")}) {
      if (QFileInfo::exists(directory + QLatin1Char('/') + file)) return true;
    }
  }
  return false;
}

bool SetAvailableGtkSettings(
    const std::initializer_list<std::pair<QString, QString>>& settings,
    const QString& value) {
  bool available = false;
  bool success = true;
  for (const auto& [schema, key] : settings) {
#ifdef FLAKEWM_HAS_QGSETTINGS
    const QByteArray schema_name = schema.toUtf8();
    if (!QGSettings::isSchemaInstalled(schema_name)) continue;
    available = true;
    QString camel_key;
    bool upper = false;
    for (const QChar character : key) {
      if (character == QLatin1Char('-')) {
        upper = true;
      } else {
        camel_key += upper ? character.toUpper() : character;
        upper = false;
      }
    }
    QGSettings object(schema_name);
    if (!object.trySet(camel_key, value)) success = false;
#else
    QProcess writable;
    writable.start(QStringLiteral("gsettings"),
                   {QStringLiteral("writable"), schema, key});
    if (!writable.waitForFinished(3000) || writable.exitCode() != 0) continue;
    available = true;
    QString escaped = value;
    escaped.replace('\'', QStringLiteral("\\'"));
    if (writable.readAllStandardOutput().trimmed() !=
            QByteArrayLiteral("true") ||
        QProcess::execute(QStringLiteral("gsettings"),
                          {QStringLiteral("set"), schema, key,
                           QStringLiteral("'%1'").arg(escaped)}) != 0)
      success = false;
#endif
  }
  return available && success;
}

}  // namespace

namespace types {

struct StringPair {
  QString first;
  QString second;
};
struct Effect {
  QString name;
  quint32 priority;
  bool enabled;
};
struct Plugin {
  QString name;
  bool loaded;
  bool enabled;
};
struct View {
  QString app_id;
  QString uuid;
  qint32 pid;
};
struct State {
  QString name;
  QList<qint32> values;
};
struct NamedBool {
  QString name;
  bool enabled;
};
struct Binding {
  QString shortcut;
  QString description;
  QString type;
};
struct Input {
  QString name;
  quint32 type;
};
struct KeySequence {
  QList<qint32> keys;
};
struct ShortcutInfo {
  QString context;
  QString context_friendly;
  QString component;
  QString component_friendly;
  QString action;
  QString action_friendly;
  QList<qint32> keys;
  QList<qint32> defaults;
};

#define FLAKEWM_DBUS_STREAM_PAIR(Type, A, B)                                \
  inline QDBusArgument& operator<<(QDBusArgument& out, const Type& value) { \
    out.beginStructure();                                                   \
    out << value.A << value.B;                                              \
    out.endStructure();                                                     \
    return out;                                                             \
  }                                                                         \
  inline const QDBusArgument& operator>>(const QDBusArgument& in,           \
                                         Type& value) {                     \
    in.beginStructure();                                                    \
    in >> value.A >> value.B;                                               \
    in.endStructure();                                                      \
    return in;                                                              \
  }

FLAKEWM_DBUS_STREAM_PAIR(StringPair, first, second)
FLAKEWM_DBUS_STREAM_PAIR(NamedBool, name, enabled)
FLAKEWM_DBUS_STREAM_PAIR(State, name, values)
FLAKEWM_DBUS_STREAM_PAIR(Input, name, type)
#undef FLAKEWM_DBUS_STREAM_PAIR

inline QDBusArgument& operator<<(QDBusArgument& out, const KeySequence& value) {
  out.beginStructure();
  out << value.keys;
  out.endStructure();
  return out;
}
inline const QDBusArgument& operator>>(const QDBusArgument& in,
                                       KeySequence& value) {
  in.beginStructure();
  in >> value.keys;
  in.endStructure();
  return in;
}

inline QDBusArgument& operator<<(QDBusArgument& out, const Effect& value) {
  out.beginStructure();
  out << value.name << value.priority << value.enabled;
  out.endStructure();
  return out;
}
inline const QDBusArgument& operator>>(const QDBusArgument& in, Effect& value) {
  in.beginStructure();
  in >> value.name >> value.priority >> value.enabled;
  in.endStructure();
  return in;
}
inline QDBusArgument& operator<<(QDBusArgument& out, const Plugin& value) {
  out.beginStructure();
  out << value.name << value.loaded << value.enabled;
  out.endStructure();
  return out;
}
inline const QDBusArgument& operator>>(const QDBusArgument& in, Plugin& value) {
  in.beginStructure();
  in >> value.name >> value.loaded >> value.enabled;
  in.endStructure();
  return in;
}
inline QDBusArgument& operator<<(QDBusArgument& out, const View& value) {
  out.beginStructure();
  out << value.app_id << value.uuid << value.pid;
  out.endStructure();
  return out;
}
inline const QDBusArgument& operator>>(const QDBusArgument& in, View& value) {
  in.beginStructure();
  in >> value.app_id >> value.uuid >> value.pid;
  in.endStructure();
  return in;
}
inline QDBusArgument& operator<<(QDBusArgument& out, const Binding& value) {
  out.beginStructure();
  out << value.shortcut << value.description << value.type;
  out.endStructure();
  return out;
}
inline const QDBusArgument& operator>>(const QDBusArgument& in,
                                       Binding& value) {
  in.beginStructure();
  in >> value.shortcut >> value.description >> value.type;
  in.endStructure();
  return in;
}
inline QDBusArgument& operator<<(QDBusArgument& out,
                                 const ShortcutInfo& value) {
  out.beginStructure();
  out << value.context << value.context_friendly << value.component
      << value.component_friendly << value.action << value.action_friendly
      << value.keys << value.defaults;
  out.endStructure();
  return out;
}
inline const QDBusArgument& operator>>(const QDBusArgument& in,
                                       ShortcutInfo& value) {
  in.beginStructure();
  in >> value.context >> value.context_friendly >> value.component >>
      value.component_friendly >> value.action >> value.action_friendly >>
      value.keys >> value.defaults;
  in.endStructure();
  return in;
}

}  // namespace types
}  // namespace dbus
}  // namespace flakewm

Q_DECLARE_METATYPE(flakewm::dbus::types::StringPair)
Q_DECLARE_METATYPE(flakewm::dbus::types::Effect)
Q_DECLARE_METATYPE(flakewm::dbus::types::Plugin)
Q_DECLARE_METATYPE(flakewm::dbus::types::View)
Q_DECLARE_METATYPE(flakewm::dbus::types::State)
Q_DECLARE_METATYPE(flakewm::dbus::types::NamedBool)
Q_DECLARE_METATYPE(flakewm::dbus::types::Binding)
Q_DECLARE_METATYPE(flakewm::dbus::types::Input)
Q_DECLARE_METATYPE(flakewm::dbus::types::KeySequence)
Q_DECLARE_METATYPE(flakewm::dbus::types::ShortcutInfo)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::StringPair>)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::Effect>)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::Plugin>)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::View>)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::State>)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::NamedBool>)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::Binding>)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::Input>)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::KeySequence>)
Q_DECLARE_METATYPE(QList<flakewm::dbus::types::ShortcutInfo>)

namespace flakewm {
namespace dbus {
namespace {

constexpr char kKylinService[] = "com.kylin.Wlcom";
constexpr char kInvalidArgs[] = "org.freedesktop.DBus.Error.InvalidArgs";
constexpr char kFailed[] = "org.freedesktop.DBus.Error.Failed";

QString Method(const char* name, const char* in = "", const char* out = "") {
  QString xml = QStringLiteral("<method name=\"") + QString::fromLatin1(name) +
                QStringLiteral("\">");
  for (const QByteArray sig : QByteArray(in).split(' ')) {
    if (!sig.isEmpty())
      xml += QStringLiteral("<arg type=\"") + sig +
             QStringLiteral("\" direction=\"in\"/>");
  }
  for (const QByteArray sig : QByteArray(out).split(' ')) {
    if (!sig.isEmpty())
      xml += QStringLiteral("<arg type=\"") + sig +
             QStringLiteral("\" direction=\"out\"/>");
  }
  return xml + QStringLiteral("</method>");
}

QString Interface(const char* name, const QString& body) {
  return QStringLiteral("<interface name=\"") + QString::fromLatin1(name) +
         QStringLiteral("\">") + body + QStringLiteral("</interface>");
}

QString Property(const char* name, const char* type, const char* access,
                 const char* emits_changed = nullptr) {
  QString xml = QStringLiteral("<property name=\"") +
                QString::fromLatin1(name) + QStringLiteral("\" type=\"") +
                QString::fromLatin1(type) + QStringLiteral("\" access=\"") +
                QString::fromLatin1(access) + QStringLiteral("\">");
  if (emits_changed != nullptr) {
    xml += QStringLiteral(
               "<annotation "
               "name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" "
               "value=\"") +
           QString::fromLatin1(emits_changed) + QStringLiteral("\"/>");
  }
  return xml + QStringLiteral("</property>");
}

QString Signal(const char* name, const char* args = "") {
  QString xml = QStringLiteral("<signal name=\"") + QString::fromLatin1(name) +
                QStringLiteral("\">");
  for (const QByteArray sig : QByteArray(args).split(' ')) {
    if (!sig.isEmpty())
      xml += QStringLiteral("<arg type=\"") + sig + QStringLiteral("\"/>");
  }
  return xml + QStringLiteral("</signal>");
}

template <typename T>
QVariant DbusArray(const QList<T>& value) {
  return QVariant::fromValue(value);
}

QVariant UnwrapVariant(const QVariant& value) {
  return value.metaType() == QMetaType::fromType<QDBusVariant>()
             ? value.value<QDBusVariant>().variant()
             : value;
}

QStringList ReadStringList(const QVariant& value) {
  if (value.canConvert<QStringList>()) return value.toStringList();
  QStringList result;
  if (value.metaType() != QMetaType::fromType<QDBusArgument>()) return result;
  const QDBusArgument argument = value.value<QDBusArgument>();
  argument.beginArray();
  while (!argument.atEnd()) {
    QString item;
    argument >> item;
    result << item;
  }
  argument.endArray();
  return result;
}

types::KeySequence ReadKeySequence(const QVariant& value) {
  types::KeySequence result;
  if (value.metaType() != QMetaType::fromType<QDBusArgument>()) return result;
  const QDBusArgument argument = value.value<QDBusArgument>();
  argument.beginStructure();
  argument.beginArray();
  while (!argument.atEnd()) {
    qint32 key = 0;
    argument >> key;
    result.keys << key;
  }
  argument.endArray();
  argument.endStructure();
  return result;
}

QList<types::KeySequence> ReadKeySequences(const QVariant& value) {
  QList<types::KeySequence> result;
  if (value.metaType() != QMetaType::fromType<QDBusArgument>()) return result;
  const QDBusArgument argument = value.value<QDBusArgument>();
  argument.beginArray();
  while (!argument.atEnd()) {
    types::KeySequence item;
    argument >> item;
    result << item;
  }
  argument.endArray();
  return result;
}

int SurfacePid(wlr_surface* surface) {
  if (surface == nullptr || surface->resource == nullptr) return 0;
  pid_t pid = 0;
  wl_client_get_credentials(wl_resource_get_client(surface->resource), &pid,
                            nullptr, nullptr);
  return static_cast<int>(pid);
}

wl_output_transform TransformForDegrees(int degrees) {
  switch (degrees) {
    case 90:
      return WL_OUTPUT_TRANSFORM_90;
    case 180:
      return WL_OUTPUT_TRANSFORM_180;
    case 270:
      return WL_OUTPUT_TRANSFORM_270;
    default:
      return WL_OUTPUT_TRANSFORM_NORMAL;
  }
}

bool ValidDegrees(int degrees) {
  return degrees == 0 || degrees == 90 || degrees == 180 || degrees == 270;
}

inline libinput_device* Libinput(wlr_input_device* input) {
  return input != nullptr && wlr_input_device_is_libinput(input)
             ? wlr_libinput_get_device_handle(input)
             : nullptr;
}

}  // namespace

struct WlcomDbusManager::InputDevice {
  InputDevice(WlcomDbusManager* new_owner, wlr_input_device* new_device)
      : owner(new_owner),
        device(new_device),
        destroy(this, OnDestroy),
        modifiers(this, OnModifiers) {}
  static void OnDestroy(InputDevice* input, void*) {
    input->destroy.Disconnect();
    input->modifiers.Disconnect();
    input->device = nullptr;
    input->owner->RemoveInput(input);
  }
  static void OnModifiers(InputDevice* input, void*) {
    if (input->device == nullptr ||
        input->device->type != WLR_INPUT_DEVICE_KEYBOARD)
      return;
    wlr_keyboard* keyboard = wlr_keyboard_from_input_device(input->device);
    if (keyboard->modifiers.group == input->keymap_group) return;
    input->keymap_group = keyboard->modifiers.group;
    input->owner->EmitSignal(QStringLiteral("/com/kylin/Wlcom/Input"),
                             QStringLiteral("com.kylin.Wlcom.Input"),
                             QStringLiteral("KeymapGroupChanged"),
                             {input->name, input->keymap_group});
  }
  WlcomDbusManager* owner;
  wlr_input_device* device;
  QString name;
  QString sys_name;
  quint32 properties = 0;
  quint32 keymap_group = 0;
  utils::SignalListener<InputDevice, void> destroy;
  utils::SignalListener<InputDevice, void> modifiers;
};

struct WlcomDbusManager::GlobalShortcut {
  QString unique_name;
  QString friendly_name;
  qint32 key = 0;
  qint32 default_key = 0;
  bool present = false;
  bool fresh = true;
  std::optional<input::KeyBindingManager::BindingId> binding_id;
};

struct WlcomDbusManager::GlobalShortcutComponent {
  QString unique_name;
  QString friendly_name;
  QString path;
  QString context = QStringLiteral("default");
  QString context_friendly = QStringLiteral("Default Context");
  std::vector<GlobalShortcut> shortcuts;
};

struct WlcomDbusManager::Watermark {
  struct Entry {
    wlr_output* output = nullptr;
    wlr_scene_buffer* scene = nullptr;
    wlr_buffer* buffer = nullptr;
  };

  QString id;
  QJsonObject info;
  std::vector<Entry> entries;
};

}  // namespace dbus
}  // namespace flakewm

#endif  // SRC_DBUS_WLCOM_WLCOM_DBUS_MANAGER_P_H_
