#ifndef _INCLUDE_EC_PRV_URL_SHORTENER_APP_CONFIG_H
#define _INCLUDE_EC_PRV_URL_SHORTENER_APP_CONFIG_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace ec_prv {
namespace url_shortener {
namespace app_config {

struct ReadOnlyAppConfig {
  // Random 256-bit key with which to hash input long URLs into short slugs.
  const uint64_t *highwayhash_key{nullptr};

  uint16_t grpc_service_port{50051};

  uint16_t web_server_port{60022};

  std::filesystem::path static_file_doc_root;

  std::filesystem::path frontend_doc_root;

  const char *static_file_request_path_prefix{"/static/"};

  // This is the base URL for your URL shortening service, after which
  // the shortened URL slug is appended. For example,
  // "https://prv.ec/" or "https://bit.ly/"
  const char *url_shortener_service_base_url{"https://prv.ec/"};

  // host to bind web server to, e.g., localhost, 0.0.0.0
  std::string web_server_bind_host{"127.0.0.1"};

  std::filesystem::path trusted_certificates_path{
      "/etc/ssl/certs/ca-certificates.crt"};

  std::string captcha_service_api_key;

  // User agent the server uses when initiating requests to external services
  // (e.g., like reCAPTCHA)
  std::string server_user_agent{
      "prv.ec - an open source url shortener web service"};

  struct ReadOnlyAppConfigDeleter {
    void operator()(ReadOnlyAppConfig *that) noexcept;
  };

  [[nodiscard]] static auto new_from_env()
      -> std::unique_ptr<ReadOnlyAppConfig, ReadOnlyAppConfigDeleter>;
};

} // namespace app_config
} // namespace url_shortener
} // namespace ec_prv

#endif // _INCLUDE_EC_PRV_URL_SHORTENER_APP_CONFIG_H
