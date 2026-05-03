#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

namespace httplib::API {

/// 统一 JSON 别名，避免在模板与元数据结构中重复书写长类型名。
using Json = nlohmann::json;
/// Router 未命名时使用的默认标签名，也会用于 OpenAPI 的 tags。
inline constexpr const char *DEFAULT_ROUTER_NAME = "default";
/// 元信息更新频率：首次更新后每 N 次响应尝试更新一次。
inline constexpr std::size_t METADATA_UPDATE_RESPONSE_INTERVAL = 1024;
/// 元信息最小更新时间间隔。
inline constexpr auto METADATA_UPDATE_MIN_INTERVAL =
  std::chrono::milliseconds(2000);

/// 路由响应缓存策略：按端点声明，空策略（std::nullopt）表示不缓存。
struct CachePolicy {
  std::chrono::milliseconds ttl = std::chrono::milliseconds(0);
  std::vector<std::string> query_fields;
  std::vector<std::string> header_fields;
  std::size_t max_entries = 256;
  std::size_t max_payload_bytes = 1024 * 1024;
  bool cache_error_response = false;
};

template <typename TContext> class Router;

/// 类型特征主模板：将 C++ 类型映射为文档类型名、Schema 与 JSON 序列化行为。
template <typename T, typename Enable = void> struct ApiTypeTraits {
  static std::string CppType() { return "custom"; }
  static Json Schema() { return Json::object(); }

  template <typename TValue> static Json ToJson(TValue &&value) {
    return Json(std::forward<TValue>(value));
  }
};

template <> struct ApiTypeTraits<Json, void> {
  static std::string CppType() { return "nlohmann::json"; }
  static Json Schema() { return Json::object(); }

  template <typename TValue> static Json ToJson(TValue &&value) {
    return std::forward<TValue>(value);
  }
};

template <> struct ApiTypeTraits<std::string, void> {
  static std::string CppType() { return "std::string"; }
  static Json Schema() { return {{"type", "string"}}; }

  template <typename TValue> static Json ToJson(TValue &&value) {
    return Json(std::forward<TValue>(value));
  }
};

template <> struct ApiTypeTraits<const char *, void> {
  static std::string CppType() { return "char*"; }
  static Json Schema() { return {{"type", "string"}}; }

  static Json ToJson(const char *value) {
    return Json(value == nullptr ? "" : value);
  }
};

template <> struct ApiTypeTraits<char *, void> {
  static std::string CppType() { return "char*"; }
  static Json Schema() { return {{"type", "string"}}; }

  static Json ToJson(const char *value) {
    return Json(value == nullptr ? "" : value);
  }

  static Json ToJson(char *value) {
    return Json(value == nullptr ? "" : value);
  }
};

template <> struct ApiTypeTraits<bool, void> {
  static std::string CppType() { return "bool"; }
  static Json Schema() { return {{"type", "boolean"}}; }

  template <typename TValue> static Json ToJson(TValue &&value) {
    return Json(std::forward<TValue>(value));
  }
};

template <typename T>
struct ApiTypeTraits<
    T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
  static std::string CppType() { return "integral"; }
  static Json Schema() { return {{"type", "integer"}}; }

  template <typename TValue> static Json ToJson(TValue &&value) {
    return Json(std::forward<TValue>(value));
  }
};

template <typename T>
struct ApiTypeTraits<T, std::enable_if_t<std::is_floating_point_v<T>>> {
  static std::string CppType() { return "floating_point"; }
  static Json Schema() { return {{"type", "number"}}; }

  template <typename TValue> static Json ToJson(TValue &&value) {
    return Json(std::forward<TValue>(value));
  }
};

template <typename TValue, typename TAllocator>
struct ApiTypeTraits<std::vector<TValue, TAllocator>, void> {
  static std::string CppType() {
    return "std::vector<" + ApiTypeTraits<std::decay_t<TValue>>::CppType() +
           ">";
  }
  static Json Schema() {
    return {{"type", "array"},
            {"items", ApiTypeTraits<std::decay_t<TValue>>::Schema()}};
  }

  static Json ToJson(const std::vector<TValue, TAllocator> &values) {
    Json arr = Json::array();
    for (const auto &item : values) {
      arr.push_back(ApiTypeTraits<std::decay_t<TValue>>::ToJson(item));
    }
    return arr;
  }

  static Json ToJson(std::vector<TValue, TAllocator> &&values) {
    Json arr = Json::array();
    for (auto &item : values) {
      arr.push_back(
          ApiTypeTraits<std::decay_t<TValue>>::ToJson(std::move(item)));
    }
    return arr;
  }
};

template <typename TValue> struct ApiTypeTraits<std::optional<TValue>, void> {
  static std::string CppType() {
    return "std::optional<" + ApiTypeTraits<std::decay_t<TValue>>::CppType() +
           ">";
  }
  static Json Schema() {
    Json schema = ApiTypeTraits<std::decay_t<TValue>>::Schema();
    schema["nullable"] = true;
    return schema;
  }

  static Json ToJson(const std::optional<TValue> &value) {
    if (!value.has_value()) {
      return nullptr;
    }
    return ApiTypeTraits<std::decay_t<TValue>>::ToJson(*value);
  }

  static Json ToJson(std::optional<TValue> &&value) {
    if (!value.has_value()) {
      return nullptr;
    }
    return ApiTypeTraits<std::decay_t<TValue>>::ToJson(std::move(*value));
  }
};

template <typename T> using ApiType = ApiTypeTraits<std::decay_t<T>>;

/// 获取类型的可读名称（用于调试与文档展示）。
template <typename T> inline std::string CppTypeName() {
  return ApiType<T>::CppType();
}

/// 获取类型声明对应的 OpenAPI Schema 片段。
template <typename T> inline Json JsonSchemaFromType() {
  return ApiType<T>::Schema();
}

/// 单个响应元数据：描述状态码、媒体类型、Schema 与最近一次响应样例。
struct ResponseRecord {
  int status = 200;
  std::string content_type = "application/json; charset=utf-8";
  std::string cpp_type;
  std::string description;
  Json declared_schema = Json::object();
  Json latest_body = Json::object();
};

/// 单条路由元数据：用于索引、文档构建与运行时响应回填。
struct RouteRecord {
  std::string method;
  std::string path;
  std::string router_name = DEFAULT_ROUTER_NAME;
  std::string summary;
  std::string description;
  std::string callback_result_type;
  std::vector<ResponseRecord> responses;
};

/// 将 HTTP 方法统一转为大写，保证索引 key 一致。
inline std::string NormalizeMethod(std::string method) {
  std::transform(
      method.begin(), method.end(), method.begin(),
      [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
  return method;
}

/// 将 method + path 组装为唯一 key。
inline std::string MakeRouteKey(const std::string &method,
                                const std::string &path) {
  return method + " " + path;
}

/// 归一化 Router 名称，避免空标签污染文档。
inline std::string NormalizeRouterName(std::string router_name) {
  if (router_name.empty()) {
    return DEFAULT_ROUTER_NAME;
  }
  return router_name;
}

/// 从 Content-Type 中提取纯 mime（剥离 charset 等参数）。
inline std::string ExtractMimeType(const std::string &content_type) {
  const auto sep = content_type.find(';');
  if (sep == std::string::npos) {
    return content_type;
  }
  return content_type.substr(0, sep);
}

/// 判断 Content-Type 是否为 JSON（含 +json 族）。
inline bool IsJsonMimeType(const std::string &content_type) {
  const std::string mime = ExtractMimeType(content_type);
  if (mime == "application/json") {
    return true;
  }

  constexpr std::string_view json_suffix = "+json";
  return mime.size() >= json_suffix.size() &&
         mime.compare(mime.size() - json_suffix.size(), json_suffix.size(),
                      json_suffix) == 0;
}

/// 直出通道的文档样例采用轻量摘要，避免将大响应体再次深拷贝到元数据。
inline Json MakeDirectBodySample(const std::string &content_type,
                                 const std::size_t payload_bytes,
                                 std::string_view preview = {}) {
  Json sample = {
      {"direct_stream", true},
      {"content_type", ExtractMimeType(content_type)},
      {"payload_bytes", payload_bytes},
  };

  if (!preview.empty()) {
    sample["preview"] = std::string(preview);
  }
  if (preview.size() < payload_bytes) {
    sample["preview_truncated"] = true;
  }
  return sample;
}

/// 基于样例 JSON 推导一个尽量简洁的 Schema（用于兜底场景）。
inline Json JsonTypeSchema(const Json &value) {
  if (value.is_object()) {
    Json properties = Json::object();
    std::vector<std::string> required;
    for (const auto &[key, child] : value.items()) {
      properties[key] = JsonTypeSchema(child);
      required.push_back(key);
    }
    Json schema = {{"type", "object"}, {"properties", properties}};
    if (!required.empty()) {
      schema["required"] = required;
    }
    return schema;
  }

  if (value.is_array()) {
    Json schema = {{"type", "array"}};
    if (!value.empty()) {
      schema["items"] = JsonTypeSchema(value.front());
    } else {
      schema["items"] = Json::object();
    }
    return schema;
  }

  if (value.is_string()) {
    return {{"type", "string"}};
  }
  if (value.is_boolean()) {
    return {{"type", "boolean"}};
  }
  if (value.is_number_integer()) {
    return {{"type", "integer"}};
  }
  if (value.is_number_float()) {
    return {{"type", "number"}};
  }
  if (value.is_null()) {
    return {{"nullable", true}};
  }
  return Json::object();
}

/// 统一入口：委托给对应 ApiTypeTraits 执行 JSON 转换。
template <typename TValue> Json ToJson(TValue &&value) {
  using Type = std::decay_t<TValue>;
  return ApiTypeTraits<Type>::ToJson(std::forward<TValue>(value));
}

/// 路由元数据注册中心：负责声明、索引与 OpenAPI 生成。
class Registry {
public:
  /// 声明或覆盖一条路由元数据，并重建索引。
  std::size_t Declare(RouteRecord route) {
    std::lock_guard<std::mutex> lock(mutex_);

    route.method = NormalizeMethod(route.method);
    route.router_name = NormalizeRouterName(std::move(route.router_name));
    const auto route_key = MakeRouteKey(route.method, route.path);
    auto existing = route_key_index_.find(route_key);
    if (existing != route_key_index_.end()) {
      routes_[existing->second] = std::move(route);
      RebuildIndexesUnlocked();
      return existing->second;
    }

    const std::size_t index = routes_.size();
    routes_.push_back(std::move(route));
    RebuildIndexesUnlocked();
    return index;
  }

  /// 记录最近一次真实响应，供文档 example 与动态 schema 推断使用。
  void UpdateLatestResponse(const std::string &method, const std::string &path,
                            int status, const std::string &content_type,
                            Json body) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto normalized_method = NormalizeMethod(method);
    const auto route_it =
        route_key_index_.find(MakeRouteKey(normalized_method, path));
    if (route_it == route_key_index_.end()) {
      return;
    }

    auto &route = routes_[route_it->second];
    auto response_it = std::find_if(
        route.responses.begin(), route.responses.end(),
        [status](const ResponseRecord &item) { return item.status == status; });

    if (response_it == route.responses.end()) {
      Json inferred_schema = JsonTypeSchema(body);
      route.responses.push_back(ResponseRecord{status, content_type, "runtime",
                                               "", std::move(inferred_schema),
                                               std::move(body)});
      return;
    }

    response_it->content_type = content_type;
    if (response_it->declared_schema.empty()) {
      response_it->declared_schema = JsonTypeSchema(body);
    }
    response_it->latest_body = std::move(body);
  }

  /// 导出所有路由元数据快照。
  std::vector<RouteRecord> AllRoutes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return routes_;
  }

  /// 按 HTTP 方法查询路由元数据快照。
  std::vector<RouteRecord> RoutesByMethod(std::string method) const {
    std::lock_guard<std::mutex> lock(mutex_);
    method = NormalizeMethod(std::move(method));

    std::vector<RouteRecord> result;
    const auto index_it = method_index_.find(method);
    if (index_it == method_index_.end()) {
      return result;
    }

    result.reserve(index_it->second.size());
    for (const std::size_t index : index_it->second) {
      result.push_back(routes_[index]);
    }
    return result;
  }

  /// 将当前注册信息构建为 OpenAPI 3.0 文档。
  Json BuildOpenApi(const std::string &title, const std::string &version,
                    const std::string &description) const {
    std::lock_guard<std::mutex> lock(mutex_);

    Json paths = Json::object();
    std::vector<std::string> tag_names;
    for (const auto &route : routes_) {
      if (std::find(tag_names.begin(), tag_names.end(), route.router_name) ==
          tag_names.end()) {
        tag_names.push_back(route.router_name);
      }

      Json responses = Json::object();
      for (const auto &response : route.responses) {
        const auto media_type = ExtractMimeType(response.content_type);

        Json response_node = {{"description", response.description.empty()
                                                  ? "Success"
                                                  : response.description}};
        const Json effective_schema = response.declared_schema.empty()
                                          ? JsonTypeSchema(response.latest_body)
                                          : response.declared_schema;
        response_node["content"][media_type]["schema"] = effective_schema;
        response_node["content"][media_type]["example"] = response.latest_body;
        response_node["x-cpp-type"] = response.cpp_type;

        responses[std::to_string(response.status)] = std::move(response_node);
      }

      if (responses.empty()) {
        responses["200"] = {{"description", "Success"}};
      }

      const std::string lower_method = [&route] {
        std::string m = route.method;
        std::transform(m.begin(), m.end(), m.begin(), [](unsigned char c) {
          return static_cast<char>(std::tolower(c));
        });
        return m;
      }();

      paths[route.path][lower_method] = {
          {"summary", route.summary},
          {"description", route.description},
          {"operationId", route.method + route.path},
          {"tags", Json::array({route.router_name})},
          {"x-router-name", route.router_name},
          {"responses", std::move(responses)}};
    }

    std::sort(tag_names.begin(), tag_names.end());
    Json tags = Json::array();
    for (const auto &tag_name : tag_names) {
      tags.push_back(
          {{"name", tag_name}, {"description", "Router: " + tag_name}});
    }

    return {{"openapi", "3.0.3"},
            {"info",
             {{"title", title},
              {"version", version},
              {"description", description}}},
            {"tags", std::move(tags)},
            {"paths", std::move(paths)}};
  }

private:
  /// 维护 method 与 route-key 双索引，提升查询与更新效率。
  void RebuildIndexesUnlocked() {
    method_index_.clear();
    route_key_index_.clear();

    for (std::size_t index = 0; index < routes_.size(); ++index) {
      const auto &route = routes_[index];
      const std::string route_key = MakeRouteKey(route.method, route.path);

      route_key_index_[route_key] = index;
      method_index_[route.method].push_back(index);
    }
  }

  mutable std::mutex mutex_;
  std::vector<RouteRecord> routes_;
  std::unordered_map<std::string, std::vector<std::size_t>> method_index_;
  std::unordered_map<std::string, std::size_t> route_key_index_;
};

/// Router：封装 httplib 注册流程并自动采集返回类型与文档元数据。
template <typename TContext> class Router {
public:
  using CachePolicyResolver =
      std::function<std::optional<CachePolicy>(const std::string &,
                                               const std::string &)>;

  /// 可通过 router_name 控制该 Router 注册出的 OpenAPI tag 名称。
  Router(httplib::Server &server, TContext &context,
         std::shared_ptr<Registry> registry = std::make_shared<Registry>(),
         std::string router_name = DEFAULT_ROUTER_NAME,
         CachePolicyResolver cache_policy_resolver = CachePolicyResolver{})
      : server_(server), context_(context), registry_(std::move(registry)),
        router_name_(NormalizeRouterName(std::move(router_name))),
        cache_policy_resolver_(std::move(cache_policy_resolver)) {}

  template <typename THandler>
  void Route(std::string method, const std::string &path, std::string summary,
             std::string description, std::string response_description,
             THandler &&handler, int status = 200,
             std::string content_type = "application/json; charset=utf-8") {
    std::string normalized_method = NormalizeMethod(std::move(method));
    std::optional<CachePolicy> cache_policy = std::nullopt;
    if (cache_policy_resolver_) {
      cache_policy = cache_policy_resolver_(normalized_method, path);
    }

    RegisterRoute(
        normalized_method, path, std::move(summary), std::move(description),
        std::move(response_description), std::forward<THandler>(handler),
        [this, method_name = normalized_method,
         route_path = std::string(path)](auto &&native_handler) mutable {
          RegisterNativeHandler(
              method_name, route_path,
              std::forward<decltype(native_handler)>(native_handler));
        },
          status, std::move(content_type), std::move(cache_policy));
  }

  template <typename THandler>
  void Get(const std::string &path, std::string summary,
           std::string description, std::string response_description,
           THandler &&handler, int status = 200,
           std::string content_type = "application/json; charset=utf-8") {
    Route("GET", path, std::move(summary), std::move(description),
          std::move(response_description), std::forward<THandler>(handler),
          status, std::move(content_type));
  }

  template <typename THandler>
  void Post(const std::string &path, std::string summary,
            std::string description, std::string response_description,
            THandler &&handler, int status = 200,
            std::string content_type = "application/json; charset=utf-8") {
    Route("POST", path, std::move(summary), std::move(description),
          std::move(response_description), std::forward<THandler>(handler),
          status, std::move(content_type));
  }

  template <typename THandler>
  void Put(const std::string &path, std::string summary,
           std::string description, std::string response_description,
           THandler &&handler, int status = 200,
           std::string content_type = "application/json; charset=utf-8") {
    Route("PUT", path, std::move(summary), std::move(description),
          std::move(response_description), std::forward<THandler>(handler),
          status, std::move(content_type));
  }

  template <typename THandler>
  void Patch(const std::string &path, std::string summary,
             std::string description, std::string response_description,
             THandler &&handler, int status = 200,
             std::string content_type = "application/json; charset=utf-8") {
    Route("PATCH", path, std::move(summary), std::move(description),
          std::move(response_description), std::forward<THandler>(handler),
          status, std::move(content_type));
  }

  template <typename THandler>
  void Delete(const std::string &path, std::string summary,
              std::string description, std::string response_description,
              THandler &&handler, int status = 200,
              std::string content_type = "application/json; charset=utf-8") {
    Route("DELETE", path, std::move(summary), std::move(description),
          std::move(response_description), std::forward<THandler>(handler),
          status, std::move(content_type));
  }

  /// 注册 Swagger UI 与 OpenAPI JSON 路由（本地页面或外部重定向二选一）。
  void
  RegisterSwaggerUI(const std::string &title, const std::string &version,
                    const std::string &description,
                    const std::string &docs_path = "/docs",
                    const std::string &openapi_path = "/docs/openapi.json",
                    const std::string &swagger_ui_endpoint = "",
                    const std::string &docs_html_path = "docs/swagger.html") {
    registry_->Declare(RouteRecord{
        .method = "GET",
        .path = openapi_path,
        .router_name = "docs",
        .summary = "OpenAPI document",
        .description = "Generate OpenAPI JSON from registered routes",
        .callback_result_type = "nlohmann::json",
        .responses = {ResponseRecord{
            200, "application/json; charset=utf-8", "nlohmann::json",
            "OpenAPI 3.0 specification", Json::object(), Json::object()}},
    });

    server_.Get(openapi_path, [registry = registry_, title, version,
                               description](const httplib::Request &req,
                                            httplib::Response &res) {
      const Json openapi = registry->BuildOpenApi(title, version, description);
      res.set_content(openapi.dump(2), "application/json; charset=utf-8");
    });

    registry_->Declare(RouteRecord{
        .method = "GET",
        .path = docs_path,
        .router_name = "docs",
        .summary = "Swagger UI",
        .description = swagger_ui_endpoint.empty()
                           ? "Serve local Swagger UI page"
                           : "Redirect to external Swagger UI with OpenAPI URL",
        .callback_result_type =
            swagger_ui_endpoint.empty() ? "std::string" : "redirect",
        .responses = {ResponseRecord{
            swagger_ui_endpoint.empty() ? 200 : 302,
            swagger_ui_endpoint.empty() ? "text/html; charset=utf-8"
                                        : "text/plain; charset=utf-8",
            swagger_ui_endpoint.empty() ? "std::string" : "redirect",
            swagger_ui_endpoint.empty() ? "Swagger UI HTML page"
                                        : "HTTP redirect",
            Json::object(), Json::object()}},
    });

    if (swagger_ui_endpoint.empty()) {
      const auto last_sep = docs_html_path.find_last_of("/\\");
      const std::string docs_dir = last_sep == std::string::npos
                                       ? "docs"
                                       : docs_html_path.substr(0, last_sep);
      std::string docs_entry = last_sep == std::string::npos
                                   ? docs_html_path
                                   : docs_html_path.substr(last_sep + 1);
      if (docs_entry.empty()) {
        docs_entry = "swagger.html";
      }

      if (!server_.set_mount_point(docs_path, docs_dir)) {
        const auto mount_error = [docs_dir](const httplib::Request &,
                                            httplib::Response &res) {
          res.status = 500;
          res.set_content("Swagger UI mount failed: " + docs_dir,
                          "text/plain; charset=utf-8");
        };
        server_.Get(docs_path, mount_error);
        server_.Get(docs_path + "/", mount_error);
        return;
      }

      std::string docs_entry_url = docs_path;
      if (docs_entry_url.empty() || docs_entry_url.back() != '/') {
        docs_entry_url += '/';
      }
      docs_entry_url += docs_entry;

      server_.Get(docs_path, [docs_entry_url](const httplib::Request &,
                                              httplib::Response &res) {
        res.set_redirect(docs_entry_url.c_str());
      });
      server_.Get(docs_path + "/", [docs_entry_url](const httplib::Request &,
                                                    httplib::Response &res) {
        res.set_redirect(docs_entry_url.c_str());
      });
      return;
    }

    const auto build_redirect_target =
        [openapi_path, swagger_ui_endpoint](const httplib::Request &req) {
          const std::string openapi_url =
              BuildOpenApiUrlFromRequest(req, openapi_path);
          return swagger_ui_endpoint +
                 "?url=" + httplib::detail::encode_url(openapi_url);
        };

    server_.Get(docs_path, [build_redirect_target](const httplib::Request &req,
                                                   httplib::Response &res) {
      const std::string target = build_redirect_target(req);
      res.set_redirect(target.c_str());
    });
    server_.Get(docs_path + "/",
                [build_redirect_target](const httplib::Request &req,
                                        httplib::Response &res) {
                  const std::string target = build_redirect_target(req);
                  res.set_redirect(target.c_str());
                });
  }

  std::shared_ptr<Registry> GetRegistry() const { return registry_; }

private:
  template <typename> static constexpr bool kAlwaysFalseHandler = false;

  static bool ShouldUseDirectOutput(const std::string &content_type) {
    return !IsJsonMimeType(content_type);
  }

  struct MetadataUpdateState {
    bool has_first_update = false;
    std::size_t pending_responses = 0;
    std::chrono::steady_clock::time_point last_update_time{};
  };

  static bool ShouldUpdateMetadataForRoute(const std::string &route_key,
                                           const bool force_update) {
    static std::mutex state_mutex;
    static std::unordered_map<std::string, MetadataUpdateState> state_map;

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(state_mutex);
    auto &state = state_map[route_key];

    if (force_update) {
      state.has_first_update = true;
      state.pending_responses = 0;
      state.last_update_time = now;
      return true;
    }

    if (!state.has_first_update) {
      state.has_first_update = true;
      state.pending_responses = 0;
      state.last_update_time = now;
      return true;
    }

    if (state.pending_responses < METADATA_UPDATE_RESPONSE_INTERVAL) {
      ++state.pending_responses;
    }
    if (state.pending_responses < METADATA_UPDATE_RESPONSE_INTERVAL) {
      return false;
    }
    if (now - state.last_update_time < METADATA_UPDATE_MIN_INTERVAL) {
      return false;
    }

    state.pending_responses = 0;
    state.last_update_time = now;
    return true;
  }

  struct CachedResponse {
    int status = 200;
    std::string content_type;
    std::string body;
  };

  struct RouteCacheEntry {
    CachedResponse response;
    std::chrono::steady_clock::time_point expires_at{};
    std::chrono::steady_clock::time_point last_access{};
  };

  struct RouteCacheStore {
    std::mutex mutex;
    std::unordered_map<std::string, RouteCacheEntry> entries;
  };

  static CachePolicy NormalizeCachePolicy(CachePolicy policy) {
    auto compact_fields = [](std::vector<std::string> &fields) {
      fields.erase(std::remove_if(fields.begin(), fields.end(),
                                  [](const std::string &item) {
                                    return item.empty();
                                  }),
                   fields.end());
    };

    compact_fields(policy.query_fields);
    compact_fields(policy.header_fields);

    if (policy.max_entries == 0) {
      policy.max_entries = 1;
    }
    return policy;
  }

  static bool IsCacheEnabled(const std::optional<CachePolicy> &cache_policy) {
    return cache_policy.has_value() && cache_policy->ttl.count() > 0;
  }

  static std::string BuildCacheKey(const httplib::Request &req,
                                   const CachePolicy &cache_policy) {
    if (cache_policy.query_fields.empty() &&
        cache_policy.header_fields.empty()) {
      return "_";
    }

    std::string key;
    key.reserve(128);

    const auto append_item = [&key](const char prefix, std::string_view field,
                                    std::string_view value) {
      key.push_back(prefix);
      key.append(field.data(), field.size());
      key.push_back('=');
      key.append(std::to_string(value.size()));
      key.push_back(':');
      key.append(value.data(), value.size());
      key.push_back('|');
    };

    for (const auto &field : cache_policy.query_fields) {
      std::string value;
      if (req.has_param(field.c_str())) {
        value = req.get_param_value(field.c_str());
      }
      append_item('q', field, value);
    }

    for (const auto &field : cache_policy.header_fields) {
      append_item('h', field, req.get_header_value(field.c_str()));
    }

    return key;
  }

  static std::optional<CachedResponse>
  TryReadCachedResponse(RouteCacheStore &cache_store, const std::string &key) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(cache_store.mutex);

    const auto it = cache_store.entries.find(key);
    if (it == cache_store.entries.end()) {
      return std::nullopt;
    }

    if (it->second.expires_at <= now) {
      cache_store.entries.erase(it);
      return std::nullopt;
    }

    it->second.last_access = now;
    return it->second.response;
  }

  static void TryStoreCachedResponse(RouteCacheStore &cache_store,
                                     const CachePolicy &cache_policy,
                                     const std::string &key,
                                     const CachedResponse &response) {
    if (cache_policy.max_payload_bytes > 0 &&
        response.body.size() > cache_policy.max_payload_bytes) {
      return;
    }
    if (!cache_policy.cache_error_response && response.status >= 500) {
      return;
    }

    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(cache_store.mutex);

    for (auto it = cache_store.entries.begin(); it != cache_store.entries.end();) {
      if (it->second.expires_at <= now) {
        it = cache_store.entries.erase(it);
      } else {
        ++it;
      }
    }

    if (cache_store.entries.size() >= cache_policy.max_entries &&
        !cache_store.entries.empty()) {
      auto oldest_it = cache_store.entries.begin();
      for (auto it = cache_store.entries.begin(); it != cache_store.entries.end();
           ++it) {
        if (it->second.last_access < oldest_it->second.last_access) {
          oldest_it = it;
        }
      }
      cache_store.entries.erase(oldest_it);
    }

    RouteCacheEntry entry;
    entry.response = response;
    entry.last_access = now;
    entry.expires_at = now + cache_policy.ttl;
    cache_store.entries[key] = std::move(entry);
  }

  template <typename TResult>
  static constexpr bool kIsByteVectorResult =
      std::is_same_v<std::decay_t<TResult>, std::vector<std::uint8_t>> ||
      std::is_same_v<std::decay_t<TResult>, std::vector<std::byte>> ||
      std::is_same_v<std::decay_t<TResult>, std::vector<unsigned char>> ||
      std::is_same_v<std::decay_t<TResult>, std::vector<char>>;

  template <typename THandler>
  static constexpr bool kIsSupportedHandler =
      std::is_invocable_v<THandler, const httplib::Request &, TContext &> ||
      std::is_invocable_v<THandler, const httplib::Request &> ||
      std::is_invocable_v<THandler, TContext &> ||
      std::is_invocable_v<THandler>;

  template <typename THandler>
  static decltype(auto) InvokeHandler(THandler &handler,
                                      const httplib::Request &req,
                                      TContext &context) {
    if constexpr (std::is_invocable_v<THandler, const httplib::Request &,
                                      TContext &>) {
      return handler(req, context);
    } else if constexpr (std::is_invocable_v<THandler,
                                             const httplib::Request &>) {
      return handler(req);
    } else if constexpr (std::is_invocable_v<THandler, TContext &>) {
      return handler(context);
    } else if constexpr (std::is_invocable_v<THandler>) {
      return handler();
    } else {
      static_assert(kAlwaysFalseHandler<THandler>,
                    "Handler signature must be one of: (req, ctx), (req), "
                    "(ctx), ().");
    }
  }

  template <typename TNativeHandler>
  void RegisterNativeHandler(const std::string &method, const std::string &path,
                             TNativeHandler &&native_handler) {
    if (method == "GET") {
      server_.Get(path, std::forward<TNativeHandler>(native_handler));
      return;
    }
    if (method == "POST") {
      server_.Post(path, std::forward<TNativeHandler>(native_handler));
      return;
    }
    if (method == "PUT") {
      server_.Put(path, std::forward<TNativeHandler>(native_handler));
      return;
    }
    if (method == "PATCH") {
      server_.Patch(path, std::forward<TNativeHandler>(native_handler));
      return;
    }
    if (method == "DELETE") {
      server_.Delete(path, std::forward<TNativeHandler>(native_handler));
      return;
    }

    throw std::invalid_argument("Unsupported HTTP method: " + method);
  }

  template <typename THandler, typename TRegister>
  /// 统一路由注册入口：声明元数据并绑定 httplib 回调。
  void RegisterRoute(const std::string &method, const std::string &path,
                     std::string summary, std::string description,
                     std::string response_description, THandler &&handler,
                     TRegister &&register_native, int status,
                     std::string content_type,
                     std::optional<CachePolicy> cache_policy) {
    using Handler = std::remove_reference_t<THandler>;
    static_assert(
        kIsSupportedHandler<Handler>,
        "Handler signature must be one of: (req, ctx), (req), (ctx), ().");

    using TResult = std::decay_t<decltype(InvokeHandler(
        std::declval<Handler &>(), std::declval<const httplib::Request &>(),
        std::declval<TContext &>()))>;
    static_assert(!std::is_void_v<TResult>,
                  "Handler return type cannot be void.");

    const auto result_type_name = CppTypeName<TResult>();
    RouteRecord route = {
        .method = method,
        .path = path,
        .router_name = router_name_,
        .summary = std::move(summary),
        .description = std::move(description),
        .callback_result_type = result_type_name,
        .responses = {ResponseRecord{status, content_type, result_type_name,
                                     std::move(response_description),
                                     JsonSchemaFromType<TResult>(),
                                     Json::object()}},
    };
    registry_->Declare(std::move(route));

    if (cache_policy.has_value()) {
      cache_policy = NormalizeCachePolicy(std::move(*cache_policy));
    }
    const bool cache_enabled = IsCacheEnabled(cache_policy);
    auto route_cache_store =
        cache_enabled ? std::make_shared<RouteCacheStore>() : nullptr;

    register_native([ctx = &context_, registry = registry_,
                     method_name = std::string(method),
                     route_path = std::string(path), response_status = status,
                     route_key = MakeRouteKey(method, path),
                     response_content_type = std::move(content_type),
                     cache_policy = std::move(cache_policy),
                     cache_store = std::move(route_cache_store),
                     fn = std::forward<THandler>(handler)](
                        const httplib::Request &req,
                        httplib::Response &res) mutable {
      std::optional<std::string> cache_key = std::nullopt;
      if (cache_store != nullptr && cache_policy.has_value()) {
        cache_key = BuildCacheKey(req, *cache_policy);
        if (const auto cached_response =
                TryReadCachedResponse(*cache_store, *cache_key);
            cached_response.has_value()) {
          res.status = cached_response->status;
          res.set_content(cached_response->body.data(),
                          cached_response->body.size(),
                          cached_response->content_type);
          return;
        }
      }

      const auto maybe_update_metadata =
          [&](const int status_code, const std::string &content_type,
              Json body, const bool force_update = false) {
            if (ShouldUpdateMetadataForRoute(route_key, force_update)) {
              registry->UpdateLatestResponse(method_name, route_path,
                                             status_code, content_type,
                                             std::move(body));
            }
          };

      const auto maybe_store_cache = [&](const CachedResponse &response) {
        if (cache_store == nullptr || !cache_policy.has_value() ||
            !cache_key.has_value()) {
          return;
        }
        TryStoreCachedResponse(*cache_store, *cache_policy, *cache_key,
                               response);
      };

      try {
      using CapturedHandler = std::remove_reference_t<decltype(fn)>;
      using CapturedResult = std::decay_t<decltype(InvokeHandler(
          std::declval<CapturedHandler &>(),
          std::declval<const httplib::Request &>(),
          std::declval<TContext &>()))>;

      CapturedResult value = InvokeHandler(fn, req, *ctx);
      res.status = response_status;
      if constexpr (std::is_same_v<CapturedResult, std::string>) {
        std::string body_text = std::move(value);
        CachedResponse response{response_status, response_content_type,
                                std::move(body_text)};

        if (ShouldUseDirectOutput(response_content_type)) {
          res.set_content(response.body.data(), response.body.size(),
                          response.content_type);
          maybe_store_cache(response);
          return;
        }

        Json body = ToJson(response.body);
        std::string serialized_body = body.dump();
        response.body = std::move(serialized_body);
        res.set_content(response.body.data(), response.body.size(),
                        response.content_type);
        maybe_store_cache(response);
        maybe_update_metadata(response_status, response_content_type,
                              std::move(body));
        return;
      } else if constexpr (std::is_same_v<CapturedResult, std::string_view>) {
        CachedResponse response{response_status, response_content_type,
                                std::string(value)};

        if (ShouldUseDirectOutput(response_content_type)) {
          res.set_content(response.body.data(), response.body.size(),
                          response.content_type);
          maybe_store_cache(response);
          return;
        }

        Json body = ToJson(response.body);
        std::string serialized_body = body.dump();
        response.body = std::move(serialized_body);
        res.set_content(response.body.data(), response.body.size(),
                        response.content_type);
        maybe_store_cache(response);
        maybe_update_metadata(response_status, response_content_type,
                              std::move(body));
        return;
      } else if constexpr (std::is_same_v<CapturedResult, const char *> ||
                           std::is_same_v<CapturedResult, char *>) {
        const char *raw = value == nullptr ? "" : value;
        CachedResponse response{response_status, response_content_type,
                                std::string(raw)};

        if (ShouldUseDirectOutput(response_content_type)) {
          res.set_content(response.body.data(), response.body.size(),
                          response.content_type);
          maybe_store_cache(response);
          return;
        }

        Json body = ToJson(response.body);
        std::string serialized_body = body.dump();
        response.body = std::move(serialized_body);
        res.set_content(response.body.data(), response.body.size(),
                        response.content_type);
        maybe_store_cache(response);
        maybe_update_metadata(response_status, response_content_type,
                              std::move(body));
        return;
      } else if constexpr (kIsByteVectorResult<CapturedResult>) {
        const std::size_t payload_bytes =
            value.size() * sizeof(typename CapturedResult::value_type);
        std::string payload_bytes_text;
        if (payload_bytes > 0) {
          payload_bytes_text.assign(
              reinterpret_cast<const char *>(value.data()), payload_bytes);
        }

        CachedResponse response{response_status, response_content_type,
                                std::move(payload_bytes_text)};
        res.set_content(response.body.data(), response.body.size(),
                        response.content_type);
        maybe_store_cache(response);
        return;
      }

      Json body = ToJson(std::move(value));
      std::string serialized_body = body.dump();
      CachedResponse response{response_status, response_content_type,
                              std::move(serialized_body)};
      res.set_content(response.body.data(), response.body.size(),
                      response.content_type);
      maybe_store_cache(response);

      maybe_update_metadata(response_status, response_content_type,
                            std::move(body));
      } catch (const std::exception &ex) {
        constexpr const char *error_content_type =
            "application/json; charset=utf-8";
        res.status = 500;
        Json error_body = {{"error", "internal_server_error"},
                           {"message", ex.what()}};
        std::string serialized_error = error_body.dump();
        CachedResponse response{500, error_content_type,
                                std::move(serialized_error)};
        res.set_content(response.body.data(), response.body.size(),
                        response.content_type);
        maybe_store_cache(response);
        maybe_update_metadata(500, error_content_type, std::move(error_body),
                              true);
      } catch (...) {
        constexpr const char *error_content_type =
            "application/json; charset=utf-8";
        res.status = 500;
        Json error_body = {{"error", "internal_server_error"},
                           {"message", "unknown exception"}};
        std::string serialized_error = error_body.dump();
        CachedResponse response{500, error_content_type,
                                std::move(serialized_error)};
        res.set_content(response.body.data(), response.body.size(),
                        response.content_type);
        maybe_store_cache(response);
        maybe_update_metadata(500, error_content_type, std::move(error_body),
                              true);
      }
    });
  }

  // ---- 预处理匹配器（热路径缓存） ----
  // 说明：将正则在启动阶段构建一次，降低每次请求的重复开销。
  static const auto &Preprocessed() {
    static const auto cache = [] {
      struct {
        std::regex edge_space_pattern;
        std::regex first_token_pattern;
        std::regex quote_wrap_pattern;
        std::regex absolute_url_pattern;
        std::regex slash_prefix_pattern;
        std::regex forwarded_host_kv_pattern;
        std::regex forwarded_proto_kv_pattern;
      } data;

      const auto flags =
          std::regex_constants::ECMAScript | std::regex_constants::optimize;

      data.edge_space_pattern = std::regex(R"(^\s+|\s+$)", flags);
      data.first_token_pattern = std::regex(R"(^\s*([^,]*))", flags);
      data.quote_wrap_pattern = std::regex("^\\\"(.*)\\\"$", flags);
      data.absolute_url_pattern =
          std::regex(R"(^[A-Za-z][A-Za-z0-9+.-]*://)", flags);
      data.slash_prefix_pattern = std::regex(R"(^/)", flags);
      data.forwarded_host_kv_pattern =
          std::regex(R"((^|;)\s*host\s*=\s*("[^"]*"|[^;]*))",
                     std::regex_constants::icase | flags);
      data.forwarded_proto_kv_pattern =
          std::regex(R"((^|;)\s*proto\s*=\s*("[^"]*"|[^;]*))",
                     std::regex_constants::icase | flags);

      return data;
    }();
    return cache;
  }

  // ---- 基于预处理缓存的运行时工具函数 ----
  static std::string TrimAscii(std::string value) {
    return std::regex_replace(value, Preprocessed().edge_space_pattern, "");
  }

  static std::string FirstCsvToken(const std::string &value) {
    std::smatch match;
    if (!std::regex_search(value, match, Preprocessed().first_token_pattern)) {
      return "";
    }
    return TrimAscii(match[1].str());
  }

  static std::string ForwardedHeaderValue(const httplib::Request &req,
                                          const std::regex &kv_pattern) {
    std::string forwarded = req.get_header_value("Forwarded");
    if (forwarded.empty()) {
      return "";
    }

    forwarded = FirstCsvToken(forwarded);
    const auto &cache = Preprocessed();

    std::smatch match;
    if (!std::regex_search(forwarded, match, kv_pattern)) {
      return "";
    }

    std::string value = TrimAscii(match[2].str());
    value = std::regex_replace(value, cache.quote_wrap_pattern, "$1");
    return value;
  }

  static std::string
  BuildOpenApiUrlFromRequest(const httplib::Request &req,
                             const std::string &openapi_path) {
    const auto &cache = Preprocessed();
    if (std::regex_search(openapi_path, cache.absolute_url_pattern)) {
      return openapi_path;
    }

    std::string host = FirstCsvToken(req.get_header_value("X-Forwarded-Host"));
    if (host.empty()) {
      host = FirstCsvToken(
          ForwardedHeaderValue(req, cache.forwarded_host_kv_pattern));
    }
    if (host.empty()) {
      host = FirstCsvToken(req.get_header_value("Host"));
    }
    if (host.empty()) {
      return openapi_path;
    }

    std::string scheme =
        FirstCsvToken(req.get_header_value("X-Forwarded-Proto"));
    if (scheme.empty()) {
      scheme = FirstCsvToken(
          ForwardedHeaderValue(req, cache.forwarded_proto_kv_pattern));
    }
    if (scheme.empty()) {
      scheme = "http";
    }

    if (openapi_path.empty()) {
      return scheme + "://" + host;
    }
    if (std::regex_search(openapi_path, cache.slash_prefix_pattern)) {
      return scheme + "://" + host + openapi_path;
    }
    return scheme + "://" + host + "/" + openapi_path;
  }

  httplib::Server &server_;
  TContext &context_;
  std::shared_ptr<Registry> registry_;
  std::string router_name_;
  CachePolicyResolver cache_policy_resolver_;
};

} // namespace httplib::API