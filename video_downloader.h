#pragma once
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

class VideoDownloader
{
public:
  struct ProxyConfig
  {
    bool enabled;
    std::string type;
    std::string host;
    int port;
  };

  struct Config
  {
    std::string download_path;
    int thread_count;
    int timeout_seconds;
    int retry_count;
    std::string user_agent;
    ProxyConfig proxy;
    std::string url;
    std::string output_name;
  };

  VideoDownloader();
  ~VideoDownloader();

  bool loadConfig(const std::string &config_path);
  bool downloadM3U8(const std::string &url, const std::string &output_name);
  const Config &getConfig() const { return config_; }

private:
  bool parseM3U8(const std::string &content, std::vector<std::string> &segments);
  bool downloadSegment(const std::string &url, const std::string &output_path);
  bool mergeSegments(const std::vector<std::string> &segments, const std::string &output_file);
  static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
  void setupCurlProxy(CURL *curl);
  void setupCurlSSL(CURL *curl);
  void setupCurlCommonOpts(CURL *curl, char *error_buffer);

  Config config_;
  std::shared_ptr<CURL> curl_;
};
