# Switch AI Chat 0.9.3

首个公开开源版本。

## 主要功能

- 用户自行配置 OpenAI 兼容 Base URL 与 API Key
- 自动获取并选择服务端模型
- SSE 流式回复、停止生成和错误提示
- Markdown、表格、代码块、基础 Mermaid 与 Emoji 回退
- 会话新建、删除、重命名、时间和本地持久化
- 简体中文键盘、Joy-Con、触屏和侧栏快捷按钮
- 六套内置主题和自定义 hbmenu 图标
- Homebrew ABI revision 1

## 安装

将 `switch-ai-chat.nro` 放到 SD 卡：

```text
/switch/SwitchAIChat/switch-ai-chat.nro
```

详细 API Key 配置、安全说明和操作方式请阅读仓库 README。API Key 由用户自行提供，并以明文保存在 SD 卡的 `/switch/SwitchAIChat/config.ini` 中；请使用可撤销、额度受限的专用密钥。

## 校验

```text
SHA-256: 67F61C1BE81DD377765E8672D3A142BDED6CE2EABA9D0422188A703E443EB9F5
```

本项目是非官方社区自制软件，与 Nintendo 及任何 AI 服务提供商均无关联。本项目不包含 Nintendo SDK、固件、密钥、游戏内容或商标图形。

