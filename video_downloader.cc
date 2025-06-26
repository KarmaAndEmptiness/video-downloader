#include "video_downloader.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <filesystem>
#include <regex>
#include <mutex>
#include <openssl/evp.h>

VideoDownloader::VideoDownloader()
{
  curl_global_init(CURL_GLOBAL_ALL);
  curl_ = std::shared_ptr<CURL>(curl_easy_init(), curl_easy_cleanup);
}

VideoDownloader::~VideoDownloader()
{
  curl_global_cleanup();
}

bool VideoDownloader::loadConfig(const std::string &config_path)
{
  try
  {
    std::ifstream f(config_path);
    nlohmann::json j;
    f >> j;

    config_.download_path = j["download_path"];
    config_.thread_count = j["thread_count"];
    config_.timeout_seconds = j["timeout_seconds"];
    config_.retry_count = j["retry_count"];
    config_.user_agent = j["user_agent"];

    // Load proxy settings
    config_.proxy.enabled = j["proxy"]["enabled"];
    config_.proxy.type = j["proxy"]["type"];
    config_.proxy.host = j["proxy"]["host"];
    config_.proxy.port = j["proxy"]["port"];

    // Load video settings
    config_.url = j["video"]["url"];
    config_.baseurl = j["video"]["baseurl"];
    config_.output_name = j["video"]["output_name"];

    std::filesystem::create_directories(config_.download_path);
    return true;
  }
  catch (const std::exception &e)
  {
    std::cerr << "Failed to load config: " << e.what() << std::endl;
    return false;
  }
}

void VideoDownloader::setupCurlSSL(CURL *curl)
{
  // 完全禁用SSL验证和检查
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

  // 允许不安全的加密方式
  curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "DEFAULT@SECLEVEL=1");

  // 使用系统默认的SSL版本，而不是强制TLS版本
  curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_DEFAULT);

  // 禁用其他SSL相关选项
  curl_easy_setopt(curl, CURLOPT_SSL_ENABLE_ALPN, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_SESSIONID_CACHE, 0L);

  // 设置SSL选项为最大兼容模式
  curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS,
                   CURLSSLOPT_ALLOW_BEAST |
                       CURLSSLOPT_NO_REVOKE |
                       CURLSSLOPT_NO_PARTIALCHAIN);
}

void VideoDownloader::setupCurlCommonOpts(CURL *curl, char *error_buffer)
{
  // 基本选项
  curl_easy_setopt(curl, CURLOPT_USERAGENT, config_.user_agent.c_str());
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  // 增加超时时间
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, config_.timeout_seconds ? config_.timeout_seconds : 60L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, config_.timeout_seconds);

  // 其他连接选项
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);
  curl_easy_setopt(curl, CURLOPT_TCP_FASTOPEN, 1L);

  // 缓冲区设置
  curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
  curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);

  // TCP keep-alive
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
  curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);

  // 设置代理先于SSL
  setupCurlProxy(curl);
  setupCurlSSL(curl);
}

void VideoDownloader::setupCurlProxy(CURL *curl)
{
  if (!config_.proxy.enabled)
    return;

  std::string proxy_url = config_.proxy.host + ":" + std::to_string(config_.proxy.port);
  curl_easy_setopt(curl, CURLOPT_PROXY, proxy_url.c_str());
  curl_easy_setopt(curl, CURLOPT_PROXYTYPE,
                   (config_.proxy.type == "http") ? CURLPROXY_HTTP : CURLPROXY_SOCKS5);

  // 禁用所有代理SSL验证
  curl_easy_setopt(curl, CURLOPT_PROXY_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_PROXY_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_PROXYAUTH, CURLAUTH_NONE);

  // 设置代理连接选项
  curl_easy_setopt(curl, CURLOPT_HTTPPROXYTUNNEL, 1L);
  curl_easy_setopt(curl, CURLOPT_SUPPRESS_CONNECT_HEADERS, 0L);

  std::cout << "Using proxy: " << proxy_url << " (Type: " << config_.proxy.type << ")" << std::endl;
}

size_t VideoDownloader::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  ((std::string *)userp)->append((char *)contents, size * nmemb);
  return size * nmemb;
}

bool VideoDownloader::downloadKey(const std::string &key_url, std::vector<uint8_t> &key_data)
{
  std::string key_content;
  char error_buffer[CURL_ERROR_SIZE] = {0};

  CURL *curl = curl_easy_init();
  if (!curl)
    return false;

  curl_easy_setopt(curl, CURLOPT_URL, key_url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &key_content);

  setupCurlCommonOpts(curl, error_buffer);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK && res != CURLE_SSL_CONNECT_ERROR)
  {
    std::cerr << "Failed to download key: " << curl_easy_strerror(res) << std::endl;
    return false;
  }

  key_data.assign(key_content.begin(), key_content.end());
  return true;
}

bool VideoDownloader::decryptSegment(const std::string &input_file, const std::string &output_file,
                                     const std::vector<uint8_t> &key_data)
{
  std::ifstream in(input_file, std::ios::binary);
  std::ofstream out(output_file, std::ios::binary);

  if (!in || !out)
    return false;

  // Initialize OpenSSL
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (!ctx)
    return false;

  // Initialize decryption
  if (!EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                          key_data.data(), nullptr))
  {
    EVP_CIPHER_CTX_free(ctx);
    return false;
  }

  std::vector<unsigned char> inbuf(1024), outbuf(1024 + EVP_MAX_BLOCK_LENGTH);
  int outlen;

  // Read and decrypt
  while (in.good())
  {
    in.read(reinterpret_cast<char *>(inbuf.data()), inbuf.size());
    std::streamsize bytes_read = in.gcount();

    if (bytes_read > 0)
    {
      if (!EVP_DecryptUpdate(ctx, outbuf.data(), &outlen,
                             inbuf.data(), bytes_read))
      {
        EVP_CIPHER_CTX_free(ctx);
        return false;
      }
      out.write(reinterpret_cast<char *>(outbuf.data()), outlen);
    }
  }

  // Finalize decryption
  if (EVP_DecryptFinal_ex(ctx, outbuf.data(), &outlen))
    out.write(reinterpret_cast<char *>(outbuf.data()), outlen);

  EVP_CIPHER_CTX_free(ctx);
  return true;
}

bool VideoDownloader::parseM3U8(const std::string &content, std::vector<std::string> &segments)
{
  std::istringstream stream(content);
  std::string line;
  bool isValidM3U8 = false;

  // Reset encryption info
  encryption_ = EncryptionInfo();

  if (std::getline(stream, line))
  {
    if (line.find("#EXTM3U") == std::string::npos)
      return false;
    isValidM3U8 = true;
  }

  while (std::getline(stream, line))
  {
    line.erase(0, line.find_first_not_of(" \t\r\n"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);

    if (line.empty())
      continue;

    if (line[0] == '#')
    {
      // Handle encryption key
      if (line.find("#EXT-X-KEY:") != std::string::npos)
      {
        encryption_.enabled = true;

        // Parse encryption method
        size_t method_start = line.find("METHOD=") + 7;
        size_t method_end = line.find(",", method_start);
        encryption_.method = line.substr(method_start, method_end - method_start);

        // Parse key URI
        size_t uri_start = line.find("URI=\"") + 5;
        size_t uri_end = line.find("\"", uri_start);
        encryption_.key_uri = line.substr(uri_start, uri_end - uri_start);

        // Download key
        if (!downloadKey(encryption_.key_uri, encryption_.key_data))
        {
          std::cerr << "Failed to download decryption key" << std::endl;
          return false;
        }
      }
      continue;
    }

    // Handle segment URL (same as before)
    if (line.find("://") != std::string::npos)
      segments.push_back(line);
    else if (!config_.baseurl.empty())
    {
      if (!config_.baseurl.empty() && config_.baseurl.back() == '/' && !line.empty() && line[0] == '/')
        line = line.substr(1);
      segments.push_back(config_.baseurl + line);
    }
    else
      segments.push_back(line);
  }

  return isValidM3U8 && !segments.empty();
}

bool VideoDownloader::downloadSegment(const std::string &url, const std::string &output_path)
{
  for (int retry = 0; retry < config_.retry_count; ++retry)
  {
    CURL *curl = curl_easy_init();
    if (!curl)
      continue;

    std::string temp_path = output_path + ".temp";
    FILE *fp = fopen(temp_path.c_str(), "wb");
    if (!fp)
    {
      curl_easy_cleanup(curl);
      continue;
    }

    char error_buffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    setupCurlCommonOpts(curl, error_buffer);

    CURLcode res = curl_easy_perform(curl);
    long response_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    fclose(fp);
    curl_easy_cleanup(curl);

    if ((res == CURLE_OK || res == CURLE_SSL_CONNECT_ERROR) && response_code == 200)
    {
      if (encryption_.enabled)
      {
        if (!decryptSegment(temp_path, output_path, encryption_.key_data))
        {
          std::cerr << "Failed to decrypt segment: " << url << std::endl;
          std::filesystem::remove(temp_path);
          continue;
        }
        std::filesystem::remove(temp_path);
      }
      else
      {
        std::filesystem::rename(temp_path, output_path);
      }
      return true;
    }

    std::cerr << "Failed to download segment: " << url
              << " (Attempt " << (retry + 1) << "/" << config_.retry_count << ")" << std::endl;
    std::cerr << "Error: " << curl_easy_strerror(res) << std::endl;
    std::cerr << "Detailed error: " << error_buffer << std::endl;
    std::cerr << "HTTP response code: " << response_code << std::endl;

    std::filesystem::remove(temp_path);

    if (retry < config_.retry_count - 1)
    {
      std::cout << "Retrying in 3 second..." << std::endl;
      std::this_thread::sleep_for(std::chrono::seconds(3));
    }
  }
  return false;
}

bool VideoDownloader::mergeSegments(const std::vector<std::string> &segments, const std::string &output_file)
{
  std::ofstream out(output_file, std::ios::binary);
  if (!out)
    return false;

  for (const auto &segment : segments)
  {
    std::ifstream in(segment, std::ios::binary);
    if (!in)
      return false;
    out << in.rdbuf();
    in.close();
    std::filesystem::remove(segment);
  }
  out.close();
  return true;
}

bool VideoDownloader::isSegmentComplete(const std::string &filepath) const
{
  if (!std::filesystem::exists(filepath))
  {
    return false;
  }
  // 检查文件大小是否大于0
  return std::filesystem::file_size(filepath) > 0;
}

bool VideoDownloader::downloadM3U8(const std::string &url, const std::string &output_name)
{
  std::string m3u8_content;
  char error_buffer[CURL_ERROR_SIZE] = {0};

  // Download M3U8 file
  CURL *curl = curl_easy_init();
  if (!curl)
  {
    std::cerr << "Failed to initialize CURL" << std::endl;
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &m3u8_content);

  setupCurlCommonOpts(curl, error_buffer);

  // Perform request
  CURLcode res = curl_easy_perform(curl);

  // Get response code
  long response_code;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

  curl_easy_cleanup(curl);

  if (res != CURLE_OK && res != CURLE_SSL_CONNECT_ERROR)
  {
    std::cerr << "Failed to download M3U8 file: " << curl_easy_strerror(res) << std::endl;
    std::cerr << "Detailed error: " << error_buffer << std::endl;
    std::cerr << "URL: " << url << std::endl;
    return false;
  }

  if (response_code != 200)
  {
    std::cerr << "Server returned HTTP code: " << response_code << std::endl;
    std::cerr << "Response content: " << m3u8_content << std::endl;
    return false;
  }

  std::cout << "M3U8 content received: " << m3u8_content.substr(0, 100) << "..." << std::endl;

  // Parse M3U8
  std::vector<std::string> segments;
  if (!parseM3U8(m3u8_content, segments))
  {
    std::cerr << "Failed to parse M3U8 file" << std::endl;
    return false;
  }

  // Prepare download tasks
  std::vector<DownloadTask> tasks;
  size_t segment_index = 0;
  std::vector<std::string> segment_files;

  for (const auto &segment_url : segments)
  {
    std::string segment_path = config_.download_path + "segment_" +
                               std::to_string(segment_index) + ".ts";
    segment_files.push_back(segment_path);

    // 只有当片段未下载或下载不完整时才添加到任务列表
    if (!isSegmentComplete(segment_path))
    {
      tasks.push_back({segment_url, segment_path, segment_index});
    }
    else
    {
      std::cout << "Segment " << segment_index + 1 << " already downloaded, skipping..." << std::endl;
    }
    segment_index++;
  }

  // Download segments in parallel
  if (!processDownloadTasks(tasks))
  {
    std::cerr << "Failed to download segments" << std::endl;
    return false;
  }

  // Merge segments
  std::string output_path = config_.download_path + output_name + ".ts";
  if (!mergeSegments(segment_files, output_path))
  {
    std::cerr << "Failed to merge segments" << std::endl;
    return false;
  }

  return true;
}
bool VideoDownloader::loadM3U8FromFile(const std::string &file_path, const std::string &output_name)
{
  // 读取M3U8文件内容
  std::ifstream file(file_path);
  if (!file.is_open())
  {
    std::cerr << "Failed to open M3U8 file: " << file_path << std::endl;
    return false;
  }

  std::string m3u8_content((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  file.close();

  // 解析M3U8内容
  std::vector<std::string> segments;
  if (!parseM3U8(m3u8_content, segments))
  {
    std::cerr << "Failed to parse M3U8 content from file" << std::endl;
    return false;
  }

  // 准备下载任务
  std::vector<DownloadTask> tasks;
  size_t segment_index = 0;
  std::vector<std::string> segment_files;

  for (const auto &segment_url : segments)
  {
    std::string segment_path = config_.download_path + "segment_" +
                               std::to_string(segment_index) + ".ts";
    segment_files.push_back(segment_path);

    // 只有当片段未下载或下载不完整时才添加到任务列表
    if (!isSegmentComplete(segment_path))
    {
      tasks.push_back({segment_url, segment_path, segment_index});
    }
    else
    {
      std::cout << "Segment " << segment_index + 1 << " already downloaded, skipping..." << std::endl;
    }
    segment_index++;
  }

  // 下载所有片段
  if (!processDownloadTasks(tasks))
  {
    std::cerr << "Failed to download segments" << std::endl;
    return false;
  }

  // 合并片段
  std::string output_path = config_.download_path + output_name + ".ts";
  if (!mergeSegments(segment_files, output_path))
  {
    std::cerr << "Failed to merge segments" << std::endl;
    return false;
  }

  std::cout << "Successfully downloaded and merged video to: " << output_path << std::endl;
  return true;
}
void VideoDownloader::downloadSegmentsParallel(const std::vector<DownloadTask> &tasks)
{
  std::mutex cout_mutex;
  auto worker = [this, &cout_mutex](const DownloadTask &task)
  {
    if (downloadSegment(task.url, task.output_path))
    {
      std::lock_guard<std::mutex> lock(cout_mutex);
      std::cout << "Successfully downloaded segment " << task.index + 1 << ":" << task.url << std::endl;
    }
  };

  std::vector<std::thread> threads;
  for (const auto &task : tasks)
  {
    if (threads.size() >= static_cast<size_t>(config_.thread_count))
    {
      threads.front().join();
      threads.erase(threads.begin());
    }
    threads.emplace_back(worker, task);
  }

  for (auto &thread : threads)
  {
    thread.join();
  }
}

bool VideoDownloader::processDownloadTasks(std::vector<DownloadTask> &tasks)
{
  const size_t total_segments = tasks.size();
  size_t processed = 0;
  size_t batch_size = config_.thread_count;

  while (processed < total_segments)
  {
    size_t current_batch_size = std::min(batch_size, total_segments - processed);
    std::vector<DownloadTask> current_batch(
        tasks.begin() + processed,
        tasks.begin() + processed + current_batch_size);

    downloadSegmentsParallel(current_batch);

    // Verify all files in the batch were downloaded
    bool batch_success = true;
    for (const auto &task : current_batch)
    {
      if (!std::filesystem::exists(task.output_path))
      {
        std::cerr << "Failed to download segment: " << task.url << std::endl;
        batch_success = false;
        break;
      }
    }

    if (!batch_success)
    {
      return false;
    }

    processed += current_batch_size;
    std::cout << "Progress: " << processed << "/" << total_segments << " segments" << std::endl;
  }

  return true;
}
