# Changelog

## 0.9.4 - 2026-10-19

- Added asynchronous rendering for remote Markdown images returned by models.
- Added image download limits, basic local-network URL blocking, and API-key isolation for image requests.
- Added a persistent, touch-accessible web-search toggle.
- Added provider-aware search requests: MiniMax Responses Web Search and Qwen-compatible `enable_search`.
- Added streaming parser support for Responses API `response.output_text.delta` events.
- Kept the UI responsive while image downloads and model output are in progress.

## 0.9.3 - 2026-10-16

First public open-source release.

- Added OpenAI-compatible Base URL, API Key, model discovery, and model selection.
- Added responsive SSE streaming, cancellation, error states, and persistent sessions.
- Added Markdown, tables, code blocks, basic Mermaid previews, and emoji fallback.
- Added touch controls, Chinese keyboard input, smooth scrolling, and chat bubbles.
- Added conversation creation, deletion, rename, timestamps, empty-state handling, and sidebar actions.
- Added six themes and a custom hbmenu icon.
- Added Homebrew ABI revision 1 compatibility metadata.
- Added portable Windows build script, legal disclaimer, API-key guidance, security policy, and third-party notices.

SHA-256 (`switch-ai-chat.nro`): `67F61C1BE81DD377765E8672D3A142BDED6CE2EABA9D0422188A703E443EB9F5`

