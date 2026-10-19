# Switch AI Chat 0.9.4

本版本加入联网搜索与模型返回图片显示，并继续保持网络处理非阻塞。

## 新功能

- 聊天页新增可轻触的“联网搜索”开关，状态会写入本地配置。
- MiniMax 开启搜索时自动使用 Responses API 的 `web_search` 工具。
- 千问及兼容接口开启搜索时使用 `enable_search: true`。
- 支持异步显示模型回复中的 Markdown 远程图片与 `image_url.url`。
- 支持 PNG、JPEG、GIF 首帧和 BMP；图片加载不会阻塞流式回复与界面操作。

## 安全与限制

- 图片下载请求不会携带 API Key。
- 单张图片最多 5 MiB，最大 2048×2048 且总像素不超过约 200 万。
- 本地网络字面地址会被拒绝；只接受 HTTP/HTTPS URL。
- 其他服务商若不支持 `enable_search`，请关闭联网搜索。
- 该功能显示模型提供的远程图片，不直接调用独立文生图接口。

## 安装

将 `switch-ai-chat.nro` 覆盖到 SD 卡的 `/switch/SwitchAIChat/`。现有 `config.ini` 与 `sessions.bin` 会保留；旧配置首次保存后会自动加入联网搜索开关。

SHA-256：`301C4B54E5EB7255E4C54B8715948672AE8DAAD9322ABFEBF1C8B18C96D6C722`
