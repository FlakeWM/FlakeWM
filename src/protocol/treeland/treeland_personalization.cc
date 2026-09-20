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
 * Originally copyright by (C) 2024-2026 UnionTech Software Technology Co., Ltd.
 * Original license: Apache-2.0 OR LGPL-3.0-only OR GPL-2.0-only OR
 *                   GPL-3.0-only.
 * Redistributed with GPL-3.0-only.
 * Original code is modified to adapt C++ and Wlroots 0.20.2.
 */

#include <absl/log/absl_log.h>
#include <glob.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/treeland-personalization-manager-v1-protocol.h"
#include "src/protocol/treeland/treeland_protocol_manager_internal.h"
#include "src/utils/signal_listener.h"

namespace flakewm {
namespace protocol {
namespace {

constexpr uint32_t kManagerVersion = 2;

bool EnvironmentEnabled(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr) return false;
  std::string text(value);
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return text == "1" || text == "on" || text == "yes" || text == "true";
}

bool FileContains(const char* path, std::string_view needle) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;
  std::string data((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  return data.find(needle) != std::string::npos;
}

bool UseLegacyLayout() {
  if (const char* forced = std::getenv("GXDE_WLCOM_PERSONALIZATION");
      forced != nullptr) {
    const std::string_view value(forced);
    if (value == "058" || value == "0.5.8") return true;
    if (value == "059" || value == "0.5.9") return false;
  }
  constexpr std::array<const char*, 4> patterns = {
      "/usr/lib/*/libdtk6gui.so.*", "/usr/lib/*/libdtkgui.so.*",
      "/usr/lib/libdtk6gui.so.*", "/usr/lib/libdtkgui.so.*"};
  bool found = false;
  bool wallpaper = false;
  for (const char* pattern : patterns) {
    glob_t matches = {};
    if (glob(pattern, GLOB_NOSORT, nullptr, &matches) == 0) {
      for (size_t i = 0; i < matches.gl_pathc; ++i) {
        found = true;
        wallpaper |=
            FileContains(matches.gl_pathv[i],
                         "treeland_personalization_wallpaper_context_v1");
      }
    }
    globfree(&matches);
  }
  if (found) return wallpaper;
  return FileContains(
      "/usr/share/treeland-protocols/treeland-personalization-manager-v1.xml",
      "get_wallpaper_context");
}

class PersonalizationGlobal final : public TreelandGlobal {
 public:
  enum class ContextKind : uint8_t { kWallpaper, kCursor, kFont, kAppearance };

  struct Context {
    PersonalizationGlobal* global;
    wl_resource* resource;
    ContextKind kind;
    std::string pending_theme;
    bool pending_theme_set = false;
    uint32_t pending_size = 0;
    bool pending_size_set = false;
    int fd = -1;
    std::string metadata;
    std::string identifier;
    std::string output;
    uint32_t options = 0;
    uint32_t is_dark = 0;
  };

  struct WindowContext {
    WindowContext(PersonalizationGlobal* new_global, wl_resource* new_resource,
                  wlr_surface* new_surface)
        : global(new_global), resource(new_resource), surface(new_surface) {}

    PersonalizationGlobal* global;
    wl_resource* resource;
    wlr_surface* surface;
    int32_t blend_mode =
        TREELAND_PERSONALIZATION_WINDOW_CONTEXT_V1_BLEND_MODE_TRANSPARENT;
    int32_t radius = -1;
    int32_t titlebar = -1;
    bool shadow_enabled = false;
    bool shadow_set = false;
    utils::SignalListener<WindowContext, void> map{this, OnMap};
    utils::SignalListener<WindowContext, void> commit{this, OnCommit};
    utils::SignalListener<WindowContext, void> destroy{this, OnSurfaceDestroy};

    static void OnMap(WindowContext* context, void*) { context->Apply(); }
    static void OnCommit(WindowContext* context, void*) { context->Apply(); }
    static void OnSurfaceDestroy(WindowContext* context, void*) {
      // The surface is going away; drop its blur registration before clearing
      // `surface`, otherwise the backdrop-blur renderer keeps a dangling
      // wlr_surface* whose stale entry can match an unrelated texture and smear
      // blur across the whole output (seen when a DTK menu closes after
      // clicking outside the window).
      if (context->surface != nullptr) {
        context->global->owner_->SetBlur(context->surface, false);
      }
      context->map.Disconnect();
      context->commit.Disconnect();
      context->destroy.Disconnect();
      context->surface = nullptr;
      if (context->resource != nullptr) wl_resource_destroy(context->resource);
    }
    void Apply() const {
      if (surface == nullptr) return;
      global->owner_->SetBlur(
          surface,
          blend_mode ==
              TREELAND_PERSONALIZATION_WINDOW_CONTEXT_V1_BLEND_MODE_BLUR);
      if (titlebar >= 0) {
        global->owner_->SetTitlebar(
            surface,
            titlebar ==
                TREELAND_PERSONALIZATION_WINDOW_CONTEXT_V1_ENABLE_MODE_ENABLE);
      }
      if (radius >= 0) {
        global->owner_->SetRoundCorner(
            surface, radius > 0 ? radius : global->owner_->round_corner_radius);
      }
      if (shadow_set) {
        global->owner_->SetShadow(surface, shadow_enabled);
      }
    }
  };

  struct ManagerImplementation059 {
    void (*get_window_context)(wl_client*, wl_resource*, uint32_t,
                               wl_resource*);
    void (*get_cursor_context)(wl_client*, wl_resource*, uint32_t);
    void (*get_font_context)(wl_client*, wl_resource*, uint32_t);
    void (*get_appearance_context)(wl_client*, wl_resource*, uint32_t);
    void (*destroy)(wl_client*, wl_resource*);
  };

  PersonalizationGlobal(TreelandProtocolManagerImpl* owner, wl_display* display)
      : owner_(owner), legacy_layout_(UseLegacyLayout()) {
    if (EnvironmentEnabled("GXWM_DONOT_BROADCAST_TLPM")) {
      disabled_ = true;
      ABSL_LOG(WARNING) << "Treeland personalization global disabled by "
                           "GXWM_DONOT_BROADCAST_TLPM";
      return;
    }
    const wl_interface* interface =
        &treeland_personalization_manager_v1_interface;
    if (!legacy_layout_) {
      for (int i = 0; i < interface->method_count; ++i) {
        if (std::strcmp(interface->methods[i].name, "get_wallpaper_context") !=
            0) {
          methods_059_.push_back(interface->methods[i]);
        }
      }
      interface_059_ = *interface;
      interface_059_.method_count = static_cast<int>(methods_059_.size());
      interface_059_.methods = methods_059_.data();
      interface = &interface_059_;
    }
    global_ = wl_global_create(display, interface, kManagerVersion, this, Bind);
    ABSL_LOG(INFO) << "Treeland personalization wire layout 0.5."
                   << (legacy_layout_ ? "8" : "9");
  }

  ~PersonalizationGlobal() override {
    if (global_ != nullptr) wl_global_destroy(global_);
  }

  bool IsValid() const override { return disabled_ || global_ != nullptr; }

  bool ClientHasWindowContext(wl_client* client) const override {
    if (client == nullptr) {
      return false;
    }
    for (const WindowContext* context : windows_) {
      if (context->resource != nullptr &&
          wl_resource_get_client(context->resource) == client) {
        return true;
      }
    }
    return false;
  }

 private:
  static void DestroyRequest(wl_client*, wl_resource* resource) {
    wl_resource_destroy(resource);
  }

  static void Bind(wl_client* client, void* data, uint32_t version,
                   uint32_t id) {
    static const struct treeland_personalization_manager_v1_interface impl058 =
        {
            .get_window_context = GetWindowContext,
            .get_wallpaper_context = GetWallpaperContext,
            .get_cursor_context = GetCursorContext,
            .get_font_context = GetFontContext,
            .get_appearance_context = GetAppearanceContext,
            .destroy = DestroyRequest,
        };
    static const ManagerImplementation059 impl059 = {
        .get_window_context = GetWindowContext,
        .get_cursor_context = GetCursorContext,
        .get_font_context = GetFontContext,
        .get_appearance_context = GetAppearanceContext,
        .destroy = DestroyRequest,
    };
    auto* self = static_cast<PersonalizationGlobal*>(data);
    const wl_interface* interface =
        self->legacy_layout_ ? &treeland_personalization_manager_v1_interface
                             : &self->interface_059_;
    wl_resource* resource = wl_resource_create(
        client, interface, static_cast<int>(std::min(version, kManagerVersion)),
        id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    wl_resource_set_implementation(resource,
                                   self->legacy_layout_
                                       ? static_cast<const void*>(&impl058)
                                       : static_cast<const void*>(&impl059),
                                   self, nullptr);
  }

  static void GetWindowContext(wl_client* client, wl_resource* manager_resource,
                               uint32_t id, wl_resource* surface_resource) {
    static const struct treeland_personalization_window_context_v1_interface
        impl = {
            .set_blend_mode = SetBlendMode,
            .set_round_corner_radius = SetWindowRadius,
            .set_shadow = SetShadow,
            .set_border = SetBorder,
            .set_titlebar = SetTitlebar,
            .destroy = DestroyRequest,
        };
    auto* global = static_cast<PersonalizationGlobal*>(
        wl_resource_get_user_data(manager_resource));
    wlr_surface* surface = wlr_surface_from_resource(surface_resource);
    if (surface == nullptr) return;
    wl_resource* resource = wl_resource_create(
        client, &treeland_personalization_window_context_v1_interface, 1, id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return;
    }
    auto* context = new WindowContext(global, resource, surface);
    context->map.Connect(&surface->events.map);
    context->commit.Connect(&surface->events.commit);
    context->destroy.Connect(&surface->events.destroy);
    global->windows_.push_back(context);
    wl_resource_set_implementation(resource, &impl, context, DestroyWindow);
  }

  static void DestroyWindow(wl_resource* resource) {
    auto* context =
        static_cast<WindowContext*>(wl_resource_get_user_data(resource));
    if (context == nullptr) return;
    context->resource = nullptr;
    context->map.Disconnect();
    context->commit.Disconnect();
    context->destroy.Disconnect();
    if (context->surface != nullptr) {
      context->global->owner_->SetBlur(context->surface, false);
    }
    std::erase(context->global->windows_, context);
    delete context;
  }

  static void SetBlendMode(wl_client*, wl_resource* resource, int32_t mode) {
    auto* context =
        static_cast<WindowContext*>(wl_resource_get_user_data(resource));
    context->blend_mode = mode;
    context->Apply();
  }

  static void SetWindowRadius(wl_client*, wl_resource* resource,
                              int32_t radius) {
    auto* context =
        static_cast<WindowContext*>(wl_resource_get_user_data(resource));
    context->radius = radius;
    context->Apply();
  }

  static void SetShadow(wl_client*, wl_resource* resource, int32_t radius,
                        int32_t offset_x, int32_t offset_y, int32_t r,
                        int32_t g, int32_t b, int32_t a) {
    auto* context =
        static_cast<WindowContext*>(wl_resource_get_user_data(resource));
    (void)offset_x;
    (void)offset_y;
    (void)r;
    (void)g;
    (void)b;
    (void)a;

    // radius == 0 disables the shadow
    // -1 (default) and >0 both enable the Chameleon shadow.
    context->shadow_set = true;
    context->shadow_enabled = radius != 0;
    context->Apply();
  }
  static void SetBorder(wl_client*, wl_resource*, int32_t, int32_t, int32_t,
                        int32_t, int32_t) {}

  static void SetTitlebar(wl_client*, wl_resource* resource, int32_t mode) {
    auto* context =
        static_cast<WindowContext*>(wl_resource_get_user_data(resource));
    context->titlebar = mode;
    context->Apply();
  }

  static Context* CreateContext(wl_client* client,
                                wl_resource* manager_resource, uint32_t id,
                                ContextKind kind, const wl_interface* interface,
                                const void* implementation) {
    auto* global = static_cast<PersonalizationGlobal*>(
        wl_resource_get_user_data(manager_resource));
    wl_resource* resource = wl_resource_create(client, interface, 1, id);
    if (resource == nullptr) {
      wl_client_post_no_memory(client);
      return nullptr;
    }
    auto* context =
        new Context{.global = global, .resource = resource, .kind = kind};
    global->contexts_.push_back(context);
    wl_resource_set_implementation(resource, implementation, context,
                                   DestroyContext);
    return context;
  }

  static void DestroyContext(wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    if (context == nullptr) return;
    if (context->fd >= 0) close(context->fd);
    std::erase(context->global->contexts_, context);
    delete context;
  }

  static void GetWallpaperContext(wl_client* client, wl_resource* manager,
                                  uint32_t id) {
    static const struct treeland_personalization_wallpaper_context_v1_interface
        impl = {
            .set_fd = WallpaperSetFd,
            .set_identifier = WallpaperSetIdentifier,
            .set_output = WallpaperSetOutput,
            .set_on = WallpaperSetOn,
            .set_isdark = WallpaperSetDark,
            .commit = WallpaperCommit,
            .get_metadata = WallpaperGetMetadata,
            .destroy = DestroyRequest,
        };
    CreateContext(client, manager, id, ContextKind::kWallpaper,
                  &treeland_personalization_wallpaper_context_v1_interface,
                  &impl);
  }

  static void WallpaperSetFd(wl_client*, wl_resource* resource, int32_t fd,
                             const char* metadata) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    if (context->fd >= 0) close(context->fd);
    context->fd = fd;
    context->metadata = metadata == nullptr ? "" : metadata;
  }
  static void WallpaperSetIdentifier(wl_client*, wl_resource* resource,
                                     const char* value) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    context->identifier = value == nullptr ? "" : value;
  }
  static void WallpaperSetOutput(wl_client*, wl_resource* resource,
                                 const char* value) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    context->output = value == nullptr ? "" : value;
  }
  static void WallpaperSetOn(wl_client*, wl_resource* resource,
                             uint32_t options) {
    static_cast<Context*>(wl_resource_get_user_data(resource))->options =
        options;
  }
  static void WallpaperSetDark(wl_client*, wl_resource* resource,
                               uint32_t dark) {
    static_cast<Context*>(wl_resource_get_user_data(resource))->is_dark = dark;
  }
  static void WallpaperCommit(wl_client*, wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    context->global->owner_->wallpaper_metadata = context->metadata;
    if (context->fd >= 0) {
      close(context->fd);
      context->fd = -1;
    }
  }
  static void WallpaperGetMetadata(wl_client*, wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    treeland_personalization_wallpaper_context_v1_send_metadata(
        resource, context->global->owner_->wallpaper_metadata.c_str());
  }

  static void GetCursorContext(wl_client* client, wl_resource* manager,
                               uint32_t id) {
    static const struct treeland_personalization_cursor_context_v1_interface
        impl = {
            .set_theme = CursorSetTheme,
            .get_theme = CursorGetTheme,
            .set_size = CursorSetSize,
            .get_size = CursorGetSize,
            .commit = CursorCommit,
            .destroy = DestroyRequest,
        };
    CreateContext(client, manager, id, ContextKind::kCursor,
                  &treeland_personalization_cursor_context_v1_interface, &impl);
  }
  static void CursorSetTheme(wl_client*, wl_resource* resource,
                             const char* value) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    context->pending_theme = value == nullptr ? "" : value;
    context->pending_theme_set = true;
  }
  static void CursorGetTheme(wl_client*, wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    treeland_personalization_cursor_context_v1_send_theme(
        resource, context->global->owner_->cursor_theme.c_str());
  }
  static void CursorSetSize(wl_client*, wl_resource* resource, uint32_t size) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    context->pending_size = size;
    context->pending_size_set = true;
  }
  static void CursorGetSize(wl_client*, wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    treeland_personalization_cursor_context_v1_send_size(
        resource, context->global->owner_->cursor_size);
  }
  static void CursorCommit(wl_client*, wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    const bool valid =
        (!context->pending_theme_set || !context->pending_theme.empty()) &&
        (!context->pending_size_set || context->pending_size > 0);
    if (valid) {
      auto* owner = context->global->owner_;
      if (context->pending_theme_set) {
        owner->cursor_theme = context->pending_theme;
      }
      if (context->pending_size_set) owner->cursor_size = context->pending_size;
      for (Context* other : context->global->contexts_) {
        if (other->kind != ContextKind::kCursor) continue;
        if (context->pending_theme_set) {
          treeland_personalization_cursor_context_v1_send_theme(
              other->resource, owner->cursor_theme.c_str());
        }
        if (context->pending_size_set) {
          treeland_personalization_cursor_context_v1_send_size(
              other->resource, owner->cursor_size);
        }
      }
    }
    treeland_personalization_cursor_context_v1_send_verfity(resource,
                                                            valid ? 1 : 0);
    context->pending_theme.clear();
    context->pending_theme_set = false;
    context->pending_size_set = false;
  }

  static void GetFontContext(wl_client* client, wl_resource* manager,
                             uint32_t id) {
    static const struct treeland_personalization_font_context_v1_interface
        impl = {
            .set_font_size = FontSetSize,
            .get_font_size = FontGetSize,
            .set_font = FontSet,
            .get_font = FontGet,
            .set_monospace_font = MonospaceFontSet,
            .get_monospace_font = MonospaceFontGet,
            .destroy = DestroyRequest,
        };
    CreateContext(client, manager, id, ContextKind::kFont,
                  &treeland_personalization_font_context_v1_interface, &impl);
  }
  static void BroadcastFont(PersonalizationGlobal* global, int field) {
    for (Context* context : global->contexts_) {
      if (context->kind != ContextKind::kFont) continue;
      if (field == 0) {
        treeland_personalization_font_context_v1_send_font(
            context->resource, global->owner_->font.c_str());
      } else if (field == 1) {
        treeland_personalization_font_context_v1_send_monospace_font(
            context->resource, global->owner_->monospace_font.c_str());
      } else {
        treeland_personalization_font_context_v1_send_font_size(
            context->resource, global->owner_->font_size);
      }
    }
  }
  static void FontSetSize(wl_client*, wl_resource* resource, uint32_t value) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    context->global->owner_->font_size = value;
    BroadcastFont(context->global, 2);
  }
  static void FontGetSize(wl_client*, wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    treeland_personalization_font_context_v1_send_font_size(
        resource, context->global->owner_->font_size);
  }
  static void FontSet(wl_client*, wl_resource* resource, const char* value) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    context->global->owner_->font = value == nullptr ? "" : value;
    BroadcastFont(context->global, 0);
  }
  static void FontGet(wl_client*, wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    treeland_personalization_font_context_v1_send_font(
        resource, context->global->owner_->font.c_str());
  }
  static void MonospaceFontSet(wl_client*, wl_resource* resource,
                               const char* value) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    context->global->owner_->monospace_font = value == nullptr ? "" : value;
    BroadcastFont(context->global, 1);
  }
  static void MonospaceFontGet(wl_client*, wl_resource* resource) {
    auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
    treeland_personalization_font_context_v1_send_monospace_font(
        resource, context->global->owner_->monospace_font.c_str());
  }

  static void GetAppearanceContext(wl_client* client, wl_resource* manager,
                                   uint32_t id) {
    static const struct treeland_personalization_appearance_context_v1_interface
        impl = {
            .set_round_corner_radius = AppearanceSetRadius,
            .get_round_corner_radius = AppearanceGetRadius,
            .set_icon_theme = AppearanceSetIconTheme,
            .get_icon_theme = AppearanceGetIconTheme,
            .set_active_color = AppearanceSetActiveColor,
            .get_active_color = AppearanceGetActiveColor,
            .set_window_opacity = AppearanceSetOpacity,
            .get_window_opacity = AppearanceGetOpacity,
            .set_window_theme_type = AppearanceSetThemeType,
            .get_window_theme_type = AppearanceGetThemeType,
            .set_window_titlebar_height = AppearanceSetTitlebarHeight,
            .get_window_titlebar_height = AppearanceGetTitlebarHeight,
            .destroy = DestroyRequest,
        };
    CreateContext(client, manager, id, ContextKind::kAppearance,
                  &treeland_personalization_appearance_context_v1_interface,
                  &impl);
  }
  static void BroadcastAppearance(PersonalizationGlobal* global, int field) {
    auto* owner = global->owner_;
    for (Context* context : global->contexts_) {
      if (context->kind != ContextKind::kAppearance) continue;
      switch (field) {
        case 0:
          treeland_personalization_appearance_context_v1_send_round_corner_radius(
              context->resource, owner->round_corner_radius);
          break;
        case 1:
          treeland_personalization_appearance_context_v1_send_icon_theme(
              context->resource, owner->icon_theme.c_str());
          break;
        case 2:
          treeland_personalization_appearance_context_v1_send_active_color(
              context->resource, owner->active_color.c_str());
          break;
        case 3:
          treeland_personalization_appearance_context_v1_send_window_opacity(
              context->resource, owner->window_opacity);
          break;
        case 4:
          treeland_personalization_appearance_context_v1_send_window_theme_type(
              context->resource, owner->window_theme_type);
          break;
        default:
          treeland_personalization_appearance_context_v1_send_window_titlebar_height(
              context->resource, owner->window_titlebar_height);
      }
    }
  }
  static Context* Appearance(wl_resource* resource) {
    return static_cast<Context*>(wl_resource_get_user_data(resource));
  }
  static void AppearanceSetRadius(wl_client*, wl_resource* resource,
                                  int32_t value) {
    if (value < 0) {
      wl_resource_post_error(
          resource,
          TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_ROUND_CORNER_RADIUS,
          "invalid round corner radius %d", value);
      return;
    }
    Context* context = Appearance(resource);
    context->global->owner_->round_corner_radius = value;
    BroadcastAppearance(context->global, 0);
  }
  static void AppearanceGetRadius(wl_client*, wl_resource* resource) {
    Context* context = Appearance(resource);
    treeland_personalization_appearance_context_v1_send_round_corner_radius(
        resource, context->global->owner_->round_corner_radius);
  }
  static void AppearanceSetIconTheme(wl_client*, wl_resource* resource,
                                     const char* value) {
    if (value == nullptr || value[0] == '\0') {
      wl_resource_post_error(
          resource,
          TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_ICON_THEME,
          "invalid icon theme");
      return;
    }
    Context* context = Appearance(resource);
    context->global->owner_->icon_theme = value;
    BroadcastAppearance(context->global, 1);
  }
  static void AppearanceGetIconTheme(wl_client*, wl_resource* resource) {
    Context* context = Appearance(resource);
    treeland_personalization_appearance_context_v1_send_icon_theme(
        resource, context->global->owner_->icon_theme.c_str());
  }
  static void AppearanceSetActiveColor(wl_client*, wl_resource* resource,
                                       const char* value) {
    if (value == nullptr || value[0] == '\0') {
      wl_resource_post_error(
          resource,
          TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_ACTIVE_COLOR,
          "invalid active color");
      return;
    }
    Context* context = Appearance(resource);
    context->global->owner_->active_color = value;
    BroadcastAppearance(context->global, 2);
  }
  static void AppearanceGetActiveColor(wl_client*, wl_resource* resource) {
    Context* context = Appearance(resource);
    treeland_personalization_appearance_context_v1_send_active_color(
        resource, context->global->owner_->active_color.c_str());
  }
  static void AppearanceSetOpacity(wl_client*, wl_resource* resource,
                                   uint32_t value) {
    if (value > 255) {
      wl_resource_post_error(
          resource,
          TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_WINDOW_OPACITY,
          "invalid window opacity %u", value);
      return;
    }
    Context* context = Appearance(resource);
    context->global->owner_->window_opacity = value;
    BroadcastAppearance(context->global, 3);
  }
  static void AppearanceGetOpacity(wl_client*, wl_resource* resource) {
    Context* context = Appearance(resource);
    treeland_personalization_appearance_context_v1_send_window_opacity(
        resource, context->global->owner_->window_opacity);
  }
  static void AppearanceSetThemeType(wl_client*, wl_resource* resource,
                                     uint32_t value) {
    if (value !=
            TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_AUTO &&
        value !=
            TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_LIGHT &&
        value !=
            TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_THEME_TYPE_DARK) {
      wl_resource_post_error(
          resource,
          TREELAND_PERSONALIZATION_APPEARANCE_CONTEXT_V1_ERROR_INVALID_WINDOW_THEME_TYPE,
          "invalid window theme type %u", value);
      return;
    }
    Context* context = Appearance(resource);
    context->global->owner_->window_theme_type = value;
    BroadcastAppearance(context->global, 4);
  }
  static void AppearanceGetThemeType(wl_client*, wl_resource* resource) {
    Context* context = Appearance(resource);
    treeland_personalization_appearance_context_v1_send_window_theme_type(
        resource, context->global->owner_->window_theme_type);
  }
  static void AppearanceSetTitlebarHeight(wl_client*, wl_resource* resource,
                                          uint32_t value) {
    Context* context = Appearance(resource);
    context->global->owner_->window_titlebar_height = value;
    BroadcastAppearance(context->global, 5);
  }
  static void AppearanceGetTitlebarHeight(wl_client*, wl_resource* resource) {
    Context* context = Appearance(resource);
    treeland_personalization_appearance_context_v1_send_window_titlebar_height(
        resource, context->global->owner_->window_titlebar_height);
  }

  TreelandProtocolManagerImpl* owner_;
  wl_global* global_ = nullptr;
  bool disabled_ = false;
  bool legacy_layout_ = false;
  wl_interface interface_059_ = {};
  std::vector<wl_message> methods_059_;
  std::vector<Context*> contexts_;
  std::vector<WindowContext*> windows_;
};

}  // namespace

std::unique_ptr<TreelandGlobal> CreateTreelandPersonalizationGlobal(
    TreelandProtocolManagerImpl* owner, wl_display* display) {
  return std::make_unique<PersonalizationGlobal>(owner, display);
}

}  // namespace protocol
}  // namespace flakewm
