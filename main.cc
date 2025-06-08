#include "video_downloader.h"
#include <iostream>

int main(int argc, char *argv[])
{
  VideoDownloader downloader;
  if (!downloader.loadConfig("config.json"))
  {
    std::cerr << "Failed to load config" << std::endl;
    return 1;
  }

  const auto &config = downloader.getConfig();
  if (!downloader.downloadM3U8(config.url, config.output_name))
  {
    std::cerr << "Failed to download video" << std::endl;
    return 1;
  }

  std::cout << "Download completed successfully" << std::endl;
  return 0;
}
