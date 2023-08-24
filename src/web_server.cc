#include <folly/Memory.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/Unistd.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/executors/GlobalExecutor.h>
#include <glog/logging.h>
#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <string>
#include <memory>
#include <chrono>

#include "url_shortening.h"

// request handlers
#include "url_shortener_handler.h"
#include "static_handler.h"

DEFINE_int32(http_port, 11000, "Port to listen on with HTTP protocol");
DEFINE_int32(spdy_port, 11001, "Port to listen on with SPDY protocol");
DEFINE_int32(h2_port, 11002, "Port to listen on with HTTP/2 protocol");
DEFINE_string(ip, "localhost", "IP/Hostname to bind to");
DEFINE_int32(threads, 0,
             "Number of threads to listen on. Numbers <= 0 "
             "will use the number of cores on this machine.");

struct ReadOnlyAppState {
  const uint64_t* highwayhash_key{nullptr};
};


class NotFoundHandler : public proxygen::RequestHandler {
public:
  void onRequest(std::unique_ptr<proxygen::HTTPMessage> req) noexcept override {
    DLOG(INFO) << "Responded with 404";
    proxygen::ResponseBuilder(downstream_).status(404, "Not Found").sendWithEOM();
  }
  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override {}
  void onUpgrade(proxygen::UpgradeProtocol _) noexcept override {}
  void onEOM() noexcept override {}
  void requestComplete() noexcept override {
    delete this;
  }
  void onError(proxygen::ProxygenError err) noexcept override {
    delete this;
  }
};

class MyRequestHandlerFactory : public proxygen::RequestHandlerFactory {
public:
  MyRequestHandlerFactory(const ReadOnlyAppState* app_state, std::shared_ptr<::ec_prv::url_shortener::db::ShortenedUrlsDatabase> db): app_state_(app_state), db_(db) {
  }
  void onServerStart(folly::EventBase* evb) noexcept override  {
  }
  void onServerStop() noexcept override {
  }
  proxygen::RequestHandler* onRequest(proxygen::RequestHandler* request_handler, proxygen::HTTPMessage* msg) noexcept override {
    if (msg->getPath() == "/" && msg->getMethod() == proxygen::HTTPMethod::GET) {
      // serve home page
      DLOG(INFO) << "Detected route \"/\". Serving home page.";
      // TODO: adapt StaticHandler to serve a specific file
      return new NotFoundHandler();
    } else if (msg->getPath().starts_with("/static/") && msg->getMethod() == proxygen::HTTPMethod::GET) {
      // serve static files
      DLOG(INFO) << "Route \"static\" found. Serving static files.";
      return new ::ec_prv::url_shortener::web::StaticHandler();
    } // else if (ec_prv::url_shortener::url_shortening::is_ok_request_path(msg->getPath()) && msg->getMethod() == proxygen::HTTPMethod::GET) {
    if (std::string_view parsed = ec_prv::url_shortener::url_shortening::parse_out_request_str(msg->getPath()); parsed.length() > 0) {
      return new ::ec_prv::url_shortener::web::UrlShortenerApiRequestHandler(db_.get() /* TODO: fix unsafe pointer passing */, app_state_->highwayhash_key);
    }
    return new NotFoundHandler();
  }
private:
  const ReadOnlyAppState* app_state_; // TODO: make this a folly::ThreadLocalPtr
  std::shared_ptr<::ec_prv::url_shortener::db::ShortenedUrlsDatabase> db_;
};

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();

  std::vector<proxygen::HTTPServer::IPConfig> IPs = {
    {folly::SocketAddress(FLAGS_ip, FLAGS_http_port, true), proxygen::HTTPServer::Protocol::HTTP},
    {folly::SocketAddress(FLAGS_ip, FLAGS_spdy_port, true), proxygen::HTTPServer::Protocol::SPDY},
    {folly::SocketAddress(FLAGS_ip, FLAGS_h2_port, true), proxygen::HTTPServer::Protocol::HTTP2},
  };
  // TODO(zds): add support for http3?

  if (FLAGS_threads <= 0) {
    FLAGS_threads = sysconf(_SC_NPROCESSORS_ONLN);
    CHECK(FLAGS_threads > 0);
  }

  std::shared_ptr<::ec_prv::url_shortener::db::ShortenedUrlsDatabase> db = ::ec_prv::url_shortener::db::ShortenedUrlsDatabase::open();

  const char* highwayhash_key_inp = std::getenv("EC_PRV_URL_SHORTENER__HIGHWAYHASH_KEY");
  CHECK(nullptr != highwayhash_key_inp) << "missing environment variable for highwayhash key";
  if (nullptr == highwayhash_key_inp) {
    std::cerr << "Missing highwayhash key\n";
    return 1;
  }
  const uint64_t* highwayhash_key = ::ec_prv::url_shortener::url_shortening::create_highwayhash_key(highwayhash_key_inp);
  ReadOnlyAppState ro_app_state{highwayhash_key};

  proxygen::HTTPServerOptions options;
  options.threads = static_cast<size_t>(FLAGS_threads);
  options.idleTimeout = std::chrono::milliseconds(60000);
  options.shutdownOn = {SIGINT, SIGTERM};
  options.enableContentCompression = false;
  options.handlerFactories =
    proxygen::RequestHandlerChain().addThen<MyRequestHandlerFactory>(&ro_app_state, db).build();
  // Increase the default flow control to 1MB/10MB
  options.initialReceiveWindow = uint32_t(1 << 20);
  options.receiveStreamWindowSize = uint32_t(1 << 20);
  options.receiveSessionWindowSize = 10 * (1 << 20);
  options.h2cEnabled = true;

  auto disk_io_thread_pool = std::make_shared<folly::CPUThreadPoolExecutor>(FLAGS_threads, std::make_shared<folly::NamedThreadFactory>("url_shortener_web_server_disk_io_thread"));
  folly::setUnsafeMutableGlobalCPUExecutor(disk_io_thread_pool);

  proxygen::HTTPServer server(std::move(options));
  server.bind(IPs);

  // Start HTTPServer mainloop in a separate thread
  std::thread t([&]() { server.start(); });
  t.join();
 
  return 0;
}
