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
    std::string baseurl;
    std::string output_name;
  };

  VideoDownloader();
  ~VideoDownloader();

  bool loadConfig(const std::string &config_path);
  bool downloadM3U8(const std::string &url, const std::string &output_name);
  bool loadM3U8FromFile(const std::string &file_path, const std::string &output_name);
  const Config &getConfig() const { return config_; }

private:
  struct EncryptionInfo
  {
    bool enabled = false;
    std::string method;
    std::string key_uri;
    std::vector<uint8_t> key_data;
  };

  bool parseM3U8(const std::string &content, std::vector<std::string> &segments);
  bool downloadSegment(const std::string &url, const std::string &output_path);
  bool mergeSegments(const std::vector<std::string> &segments, const std::string &output_file);
  static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);
  void setupCurlProxy(CURL *curl);
  void setupCurlSSL(CURL *curl);
  void setupCurlCommonOpts(CURL *curl, char *error_buffer);

  bool downloadKey(const std::string &key_url, std::vector<uint8_t> &key_data);
  bool decryptSegment(const std::string &input_file, const std::string &output_file,
                      const std::vector<uint8_t> &key_data);

  struct DownloadTask
  {
    std::string url;
    std::string output_path;
    size_t index;
  };

  void downloadSegmentsParallel(const std::vector<DownloadTask> &tasks);
  bool processDownloadTasks(std::vector<DownloadTask> &tasks);
  bool isSegmentComplete(const std::string &filepath) const;

  Config config_;
  std::shared_ptr<CURL> curl_;
  EncryptionInfo encryption_;
};
