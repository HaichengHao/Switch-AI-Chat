# Switch AI Chat

![Switch AI Chat icon](assets/switch-ai-chat-icon.png)

一个面向 Nintendo Switch 自制软件环境的非官方、开源 AI 对话客户端。它通过用户自行提供的兼容 API 与模型通信，支持流式回复、Markdown、远程图片、联网搜索、会话管理、触屏操作和六套主题。

> [!IMPORTANT]
> 本项目是独立的社区自制软件，与 Nintendo Co., Ltd.、其关联公司以及任何 AI 服务提供商均无隶属、授权、赞助或认可关系。Nintendo Switch 是 Nintendo 的商标。本项目不包含 Nintendo SDK、固件、密钥、游戏内容、商标图形或其他专有素材，也不提供盗版、解密或规避版权保护的功能。请仅在你拥有并有权控制的设备上合法使用。

## 功能

- OpenAI 兼容的 `/models` 与 `/chat/completions` 接口
- 用户自定义 Base URL、API Key 和模型
- 获取服务端可用模型并直接点选
- SSE 流式回复、停止生成和错误提示
- 模型回复中的远程 Markdown 图片异步下载与显示（PNG、JPEG、GIF 首帧和 BMP）
- 可持久化的联网搜索开关：MiniMax 自动使用 Responses Web Search，千问兼容接口使用 `enable_search`
- Markdown 标题、粗体、列表、引用、代码块、表格及基础 Mermaid 流程图
- 会话新建、删除、重命名、时间显示与本地持久化
- 简体中文系统键盘、触屏和 Joy-Con 操作
- 深海夜色、明亮纸张、海洋蓝绿、暖阳珊瑚、紫藤星云、薄荷森林六套主题
- 自定义 hbmenu 图标及 Homebrew ABI revision 1

## 安装

1. 从 [Releases](https://github.com/HaichengHao/Switch-AI-Chat/releases) 下载 `switch-ai-chat.nro`。
2. 在 SD 卡创建目录 `/switch/SwitchAIChat/`。
3. 将文件放置为 `/switch/SwitchAIChat/switch-ai-chat.nro`。
4. 通过 hbmenu 启动。为降低内存不足风险，建议在可用时使用完整内存模式，而不是受限的 Applet Mode。

升级时只需覆盖 `.nro`。不要删除 `config.ini` 或 `sessions.bin`，否则配置和会话记录会丢失。

## 配置自己的 API Key

本项目不附带、共享或出售任何 API Key。用户需要从自己选择的、支持 OpenAI 兼容接口的服务商处获取密钥，并自行承担服务费用和使用条款责任。

1. 启动应用后按 `ZR`，或轻触侧栏底部的“设置”。
2. 选择 **Base URL**，输入服务商提供的 API 根地址，例如 `https://api.openai.com/v1`。不要附加 `/models` 或 `/chat/completions`，应用会自动拼接这些路径。
3. 选择 **API Key**，通过系统密码键盘输入自己的密钥。
4. 选择 **获取可用模型**。应用会请求 `GET {Base URL}/models`。
5. 从返回列表中选择模型并确认。
6. 如需实时资料，可在设置中开启“联网搜索”，或轻触聊天页右上角的联网搜索按钮。
7. 返回聊天页面，新建会话后即可发送消息。

如果返回 `HTTP 401`，通常表示 API Key 无效、已过期或没有权限。如果提示无法解析主机名，请检查 Base URL 拼写和网络连接。

### API Key 安全提醒

- API Key 会以明文保存在 SD 卡的 `/switch/SwitchAIChat/config.ini` 中。
- 应用不会把密钥发送给项目作者；它只会作为 `Authorization: Bearer ...` 请求头发送到你填写的 Base URL。
- 请使用额度受限、权限最小化且可随时撤销的专用密钥，不要使用主账户密钥。
- 不要分享 `config.ini`、SD 卡镜像、包含设置页面的完整截图或带密钥的日志。
- 如果密钥曾出现在公开仓库、聊天记录或截图中，请立即在服务商后台撤销并重新生成。
- 删除 `config.ini` 可以清除本地保存的服务配置和 API Key。

## 本地数据

| 文件 | 用途 |
|---|---|
| `/switch/SwitchAIChat/config.ini` | Base URL、API Key、模型、主题和联网搜索开关；明文存储 |
| `/switch/SwitchAIChat/sessions.bin` | 会话标题、消息和时间等本地历史记录 |

本项目没有遥测、广告或开发者中转服务器。实际数据处理规则取决于用户选择的 API 服务商，请阅读对应服务商的隐私政策。

## 联网搜索与图片

- MiniMax：开启联网搜索后，应用调用 `{Base URL}/responses` 并启用 `web_search` 工具；关闭时仍使用 `/chat/completions`。
- 千问 / Model Studio：应用继续调用 `/chat/completions`，并附加 `enable_search: true`。
- 其他兼容服务：该服务若不认识 `enable_search`，可能返回参数错误；此时请关闭开关。联网搜索可能增加服务商计费。
- 图片显示针对模型正文中的标准 Markdown 图片 `![说明](https://...)`，以及流式响应中的 `image_url.url`。这不是独立的文生图接口客户端。
- 图片通过 HTTP(S) 直接下载，不携带你的 API Key；单张上限 5 MiB、最大解码尺寸 2048×2048 且不超过约 200 万像素。本地网络字面地址会被阻止。远程图片服务器仍可能看到你的公网 IP。

## 操作

| 操作 | 按键 |
|---|---|
| 编辑输入框 | `A` |
| 发送 / 停止生成 | `B` |
| 新建会话 | `X` 或侧栏“新建” |
| 删除当前会话 | `Y` |
| 上一个 / 下一个会话 | `L` / `R` |
| 重命名会话 | `ZL` 或轻触标题 |
| 打开设置 | `ZR` 或侧栏“设置” |
| 开关联网搜索 | 轻触聊天页“联网搜索”按钮，或在设置中切换 |
| 收起 / 展开侧栏 | 十字键左 / 右 |
| 滚动消息 | 十字键上 / 下、左摇杆或触屏拖动 |
| 退出 | `+` |

允许删除最后一个会话。会话列表为空时可以轻触“新建”，也可以直接输入并发送，应用会自动创建会话。

## 从源码构建

需要 Windows 上的 devkitPro/devkitA64，以及 libnx、switch-curl、switch-sdl2 和 switch-sdl2_ttf。安装依赖后执行：

```powershell
./build.ps1
```

脚本会自动定位项目目录，并尝试从 `DEVKITPRO`、`D:\devkitPro` 或 `C:\devkitPro` 查找工具链。构建结果为项目根目录下的 `switch-ai-chat.nro`。

```text
source/       正式 C/ASM 源码及 stb_image
romfs/        Noto Emoji 字体及其许可证
assets/       图标源文件
```

## 发布与校验

正式二进制仅通过 GitHub Releases 提供。Release 页面会同时提供独立 NRO、带许可证文件的完整压缩包和 SHA-256 校验值。Git 仓库不提交本地编译产物、用户配置、API Key 或会话文件。

## 法律与安全

- 项目仅提供通用网络客户端功能，不提供 Nintendo 专有文件或第三方 AI 服务凭据。
- 用户必须遵守所在地法律、设备与网络服务的条款，以及所选 API 服务商的使用政策。
- 项目名称中的“Switch”仅用于说明兼容平台，不表示官方关系。
- 请勿在 Issue 中发布 API Key、个人会话内容、设备密钥或其他敏感信息。
- 这不是法律意见；公开分发者应根据其司法辖区自行评估合规要求。

第三方组件及许可证见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 许可证

项目自有代码采用 [MIT License](LICENSE)。第三方组件和字体继续遵循各自许可证。

