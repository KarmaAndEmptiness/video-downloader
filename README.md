直接下载 mp4 视频：

```bash
  ./download_mp4.sh <URL> [output_file]
```

下载 m3u8 视频：

```json
{
  //视频存放目录
  "download_path": "./downloads/",
  "log_path": "./download.log",
  //线程数
  "thread_count": 8,
  //超时时间
  "timeout_seconds": 60,
  //重试次数
  "retry_count": 100,
  "user_agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36",
  //配置代理
  "proxy": {
    "enabled": true,
    "type": "http", //or socks5
    "host": "192.168.65.157",
    "port": 10809
  },
  "video": {
    //配置segments的baseurl
    "baseurl": "",
    //配置key文件uri的baseurl
    "key_baseurl": "",
    //m3u8文件地址
    "url": "",
    "output_name": "output_video"
  }
}
```

通过 url 下载 m3u8 文件后下载：

```bash
cd build && cmake .. && make && ./video_downloader
```

直接加载本地 m3u8 文件：

```bash
./video_downloader -f <m3u8_file_path>
```
