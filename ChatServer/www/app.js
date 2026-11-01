/* ============================================================================
   AI Chat · 前端逻辑
   ----------------------------------------------------------------------------
   仅依赖浏览器原生 API，无构建步骤、无第三方库。
   API 契约（与 ChatServer.cpp 保持一致，勿改）：
     GET    /api/models                 -> { data: [{ name, desc }] }
     POST   /api/session                -> { data: { session_id } }
     GET    /api/sessions               -> { data: [{ id, model, created_at,
                                                       updated_at, message_count,
                                                       first_user_message }] }
     GET    /api/session/{id}/history   -> { data: [{ id, role, content, timestamp }] }
     DELETE /api/session/{id}
     POST   /api/message/async          -> SSE：`data: "<json 字符串>"` … `data: [DONE]`
   ========================================================================== */

(() => {
  "use strict";

  const THEME_KEY = "ai-chat-theme";
  const SSE_DONE = "[DONE]";

  const state = {
    models: [],
    sessions: [],
    messages: [],
    activeSessionId: null,
    activeModel: "",
    isStreaming: false,
    pendingDeleteId: null,
  };

  const elements = {
    body: document.body,
    sidebar: document.getElementById("sidebar"),
    scrim: document.getElementById("scrim"),
    openSidebar: document.getElementById("open-sidebar"),
    closeSidebar: document.getElementById("close-sidebar"),
    newChat: document.getElementById("new-chat"),
    sessionList: document.getElementById("session-list"),
    sessionCount: document.getElementById("session-count"),
    activeModel: document.getElementById("active-model"),
    connectionStatus: document.getElementById("connection-status"),
    connectionText: document.getElementById("connection-text"),
    themeToggle: document.getElementById("theme-toggle"),
    conversation: document.getElementById("conversation"),
    welcomeState: document.getElementById("welcome-state"),
    welcomeNewChat: document.getElementById("welcome-new-chat"),
    messageList: document.getElementById("message-list"),
    emptyHint: document.getElementById("empty-hint"),
    composer: document.getElementById("composer"),
    messageInput: document.getElementById("message-input"),
    sendButton: document.getElementById("send-button"),
    requestStatus: document.getElementById("request-status"),
    modelDialog: document.getElementById("model-dialog"),
    modelOptions: document.getElementById("model-options"),
    modelError: document.getElementById("model-error"),
    newSessionForm: document.getElementById("new-session-form"),
    createSession: document.getElementById("create-session"),
    deleteDialog: document.getElementById("delete-dialog"),
    deleteSessionForm: document.getElementById("delete-session-form"),
    confirmDelete: document.getElementById("confirm-delete"),
    toastRegion: document.getElementById("toast-region"),
  };

  const ICONS = {
    trash: '<path d="M4 7h16M9.5 7V5h5v2M6.5 7l1 13h9l1-13M10 11v6M14 11v6"/>',
    copy: '<rect x="9" y="9" width="11" height="11"/><path d="M15 5.5H5.5A1.5 1.5 0 0 0 4 7v9.5"/>',
    check: '<path d="m5 13 5 5L19 7"/>',
    sun: '<circle cx="12" cy="12" r="4.2"/><path d="M12 2.6v2.6M12 18.8v2.6M2.6 12h2.6M18.8 12h2.6M5.4 5.4l1.8 1.8M16.8 16.8l1.8 1.8M5.4 18.6l1.8-1.8M16.8 7.2l1.8-1.8"/>',
    moon: '<path d="M20 14.6A8.2 8.2 0 0 1 9.4 4a8.4 8.4 0 1 0 10.6 10.6Z"/>',
  };

  function svgIcon(name) {
    return `<svg viewBox="0 0 24 24" aria-hidden="true">${ICONS[name] || ""}</svg>`;
  }

  function nowSeconds() {
    return Math.floor(Date.now() / 1000);
  }

  function pad(value) {
    return String(value).padStart(2, "0");
  }

  function formatTime(unixSeconds) {
    if (!unixSeconds) return "";
    const date = new Date(unixSeconds * 1000);
    if (Number.isNaN(date.getTime())) return "";
    const today = new Date();
    if (date.toDateString() === today.toDateString()) {
      return `${pad(date.getHours())}:${pad(date.getMinutes())}`;
    }
    return `${pad(date.getMonth() + 1)}/${pad(date.getDate())}`;
  }

  /* ---------- 通用 UI 反馈 ---------------------------------------------- */

  function setConnection(mode, text) {
    elements.connectionStatus.dataset.state = mode;
    elements.connectionText.textContent = text;
  }

  function setRequestStatus(text, isError = false) {
    elements.requestStatus.textContent = text || "";
    elements.requestStatus.style.color = isError ? "var(--danger)" : "";
  }

  function showToast(message, isError = false) {
    const toast = document.createElement("div");
    toast.className = `toast${isError ? " toast--error" : ""}`;
    toast.textContent = message;
    elements.toastRegion.append(toast);
    while (elements.toastRegion.children.length > 4) {
      elements.toastRegion.firstElementChild.remove();
    }
    window.setTimeout(() => {
      toast.style.opacity = "0";
      toast.style.transition = "opacity .2s ease";
      window.setTimeout(() => toast.remove(), 220);
    }, 3800);
  }

  async function request(url, options = {}) {
    const response = await fetch(url, {
      headers: { "Content-Type": "application/json" },
      ...options,
    });
    let payload = null;
    try {
      payload = await response.json();
    } catch {
      payload = null;
    }
    if (!response.ok || (payload && payload.success === false)) {
      const detail =
        (payload && (payload.message || payload.error)) ||
        `请求失败（HTTP ${response.status}）`;
      throw new Error(detail);
    }
    return payload;
  }

  /* ---------- 会话索引 -------------------------------------------------- */

  function sessionTitle(session) {
    const text = (session.first_user_message || "").replace(/\s+/g, " ").trim();
    if (!text) return "未命名对话";
    return text.length > 34 ? `${text.slice(0, 34)}…` : text;
  }

  function renderSessions() {
    elements.sessionList.innerHTML = "";
    elements.sessionCount.textContent = String(state.sessions.length);

    if (state.sessions.length === 0) {
      const empty = document.createElement("p");
      empty.className = "session-empty";
      empty.textContent = "还没有会话，点上方按钮新建。";
      elements.sessionList.append(empty);
      return;
    }

    state.sessions.forEach((session, index) => {
      const isActive = session.id === state.activeSessionId;
      const item = document.createElement("div");
      item.className = `session-item${isActive ? " is-active" : ""}`;

      const select = document.createElement("button");
      select.type = "button";
      select.className = "session-select";
      if (isActive) select.setAttribute("aria-current", "true");

      const indexEl = document.createElement("span");
      indexEl.className = "session-index";
      indexEl.textContent = pad(index + 1);

      const body = document.createElement("span");
      body.className = "session-body";

      const title = document.createElement("span");
      title.className = "session-title";
      title.textContent = sessionTitle(session);

      const meta = document.createElement("span");
      meta.className = "session-meta";
      meta.textContent = [session.model, formatTime(session.updated_at)]
        .filter(Boolean)
        .join(" · ");

      body.append(title, meta);
      select.append(indexEl, body);
      select.addEventListener("click", () => {
        selectSession(session.id);
        closeSidebar();
      });

      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "session-delete";
      remove.setAttribute("aria-label", `删除会话：${sessionTitle(session)}`);
      remove.innerHTML = svgIcon("trash");
      remove.addEventListener("click", (event) => {
        event.stopPropagation();
        openDeleteDialog(session.id);
      });

      item.append(select, remove);
      elements.sessionList.append(item);
    });
  }

  async function loadSessions() {
    const payload = await request("/api/sessions");
    state.sessions = Array.isArray(payload && payload.data) ? payload.data : [];
    renderSessions();
  }

  /* ---------- Markdown 渲染 --------------------------------------------- */

  const HTML_ESCAPES = { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" };

  function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, (character) => HTML_ESCAPES[character]);
  }

  function safeUrl(url) {
    const value = String(url).trim();
    if (/^https?:\/\//i.test(value)) return value;
    if (value.startsWith("/") || value.startsWith("#")) return value;
    return "#";
  }

  function renderInline(text) {
    const codes = [];
    let source = String(text).replace(/`([^`\n]+)`/g, (match, code) => {
      codes.push(code);
      return `\u0000${codes.length - 1}\u0000`;
    });

    let out = escapeHtml(source);

    out = out.replace(/!\[([^\]]*)\]\(([^)\s]+)\)/g, (match, alt, href) => {
      const label = alt || href;
      return `<a href="${safeUrl(href)}" target="_blank" rel="noopener noreferrer">${label}</a>`;
    });
    out = out.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (match, label, href) => {
      return `<a href="${safeUrl(href)}" target="_blank" rel="noopener noreferrer">${label}</a>`;
    });

    out = out.replace(/\*\*([^*\n]+)\*\*/g, "<strong>$1</strong>");
    out = out.replace(/__([^_\n]+)__/g, "<strong>$1</strong>");
    out = out.replace(/(^|[^\w*])\*([^*\n]+)\*/g, "$1<em>$2</em>");
    out = out.replace(/(^|[^\w_])_([^_\n]+)_/g, "$1<em>$2</em>");
    out = out.replace(/~~([^~\n]+)~~/g, "<del>$1</del>");

    out = out.replace(/\u0000(\d+)\u0000/g, (match, index) => {
      return `<code>${escapeHtml(codes[Number(index)] || "")}</code>`;
    });
    return out;
  }

  function isBlockStart(line) {
    return (
      /^\s*(```|~~~)/.test(line) ||
      /^\s{0,3}#{1,6}\s/.test(line) ||
      /^\s*>\s?/.test(line) ||
      /^\s*[-*+]\s+/.test(line) ||
      /^\s*\d+[.)]\s+/.test(line) ||
      /^\s*([-*_])\s*(\1\s*){2,}$/.test(line)
    );
  }

  function renderMarkdown(source) {
    const lines = String(source || "").replace(/\r\n?/g, "\n").split("\n");
    const html = [];
    let index = 0;

    while (index < lines.length) {
      const line = lines[index];

      if (/^\s*$/.test(line)) {
        index += 1;
        continue;
      }

      const fence = line.match(/^\s*(```|~~~)\s*([\w+#.-]*)\s*$/);
      if (fence) {
        const marker = fence[1];
        const language = fence[2] || "";
        index += 1;
        const buffer = [];
        while (index < lines.length && lines[index].trim() !== marker) {
          buffer.push(lines[index]);
          index += 1;
        }
        index += 1;
        const attribute = language ? ` data-lang="${escapeHtml(language)}"` : "";
        html.push(`<pre class="md-code"${attribute}><code>${escapeHtml(buffer.join("\n"))}</code></pre>`);
        continue;
      }

      if (/^\s*([-*_])\s*(\1\s*){2,}$/.test(line)) {
        html.push("<hr />");
        index += 1;
        continue;
      }

      const heading = line.match(/^\s{0,3}(#{1,6})\s+(.*\S)\s*$/);
      if (heading) {
        const level = heading[1].length;
        html.push(`<h${level} class="md-h">${renderInline(heading[2])}</h${level}>`);
        index += 1;
        continue;
      }

      if (/^\s*>\s?/.test(line)) {
        const buffer = [];
        while (index < lines.length && /^\s*>\s?/.test(lines[index])) {
          buffer.push(lines[index].replace(/^\s*>\s?/, ""));
          index += 1;
        }
        html.push(`<blockquote>${renderMarkdown(buffer.join("\n"))}</blockquote>`);
        continue;
      }

      if (/^\s*[-*+]\s+/.test(line)) {
        const items = [];
        while (index < lines.length && /^\s*[-*+]\s+/.test(lines[index])) {
          items.push(lines[index].replace(/^\s*[-*+]\s+/, ""));
          index += 1;
        }
        html.push(`<ul>${items.map((item) => `<li>${renderInline(item)}</li>`).join("")}</ul>`);
        continue;
      }

      if (/^\s*\d+[.)]\s+/.test(line)) {
        const items = [];
        while (index < lines.length && /^\s*\d+[.)]\s+/.test(lines[index])) {
          items.push(lines[index].replace(/^\s*\d+[.)]\s+/, ""));
          index += 1;
        }
        html.push(`<ol>${items.map((item) => `<li>${renderInline(item)}</li>`).join("")}</ol>`);
        continue;
      }

      const buffer = [];
      while (index < lines.length && !/^\s*$/.test(lines[index]) && !isBlockStart(lines[index])) {
        buffer.push(lines[index]);
        index += 1;
      }
      if (buffer.length === 0) {
        buffer.push(line);
        index += 1;
      }
      html.push(`<p>${renderInline(buffer.join("\n")).replace(/\n/g, "<br />")}</p>`);
    }

    return html.join("");
  }

  /* ---------- 消息渲染 -------------------------------------------------- */

  function createMessageElement({ role, content, timestamp }) {
    const isUser = role === "user";
    const article = document.createElement("article");
    article.className = `message ${isUser ? "user" : "assistant"}`;

    const roleEl = document.createElement("div");
    roleEl.className = "msg-role";
    roleEl.textContent = isUser ? "你" : "AI";

    const time = document.createElement("span");
    time.className = "msg-time";
    time.textContent = formatTime(timestamp || nowSeconds());
    roleEl.append(time);

    const body = document.createElement("div");
    body.className = "msg-body";

    const contentEl = document.createElement("div");
    contentEl.className = "msg-content";
    contentEl.innerHTML = renderMarkdown(content || "");
    body.append(contentEl);

    if (!isUser) {
      const tools = document.createElement("div");
      tools.className = "msg-tools";
      const copy = document.createElement("button");
      copy.type = "button";
      copy.className = "copy-button";
      copy.innerHTML = `${svgIcon("copy")}<span>复制</span>`;
      copy.addEventListener("click", () => copyMessage(contentEl, copy));
      tools.append(copy);
      body.append(tools);
    }

    article.append(roleEl, body);
    return article;
  }

  function updateLayout() {
    const hasSession = Boolean(state.activeSessionId);
    const hasMessages = state.messages.length > 0;
    elements.welcomeState.hidden = hasSession;
    elements.messageList.hidden = !hasSession;
    elements.emptyHint.hidden = !(hasSession && !hasMessages);
  }

  function renderMessages(messages) {
    state.messages = Array.isArray(messages) ? messages.slice() : [];
    elements.messageList.innerHTML = "";
    state.messages.forEach((message) => {
      elements.messageList.append(createMessageElement(message));
    });
    updateLayout();
    scrollMessagesToBottom(true);
  }

  /* ---------- 滚动与输入框 ---------------------------------------------- */

  let stickToBottom = true;

  function isNearBottom() {
    const el = elements.conversation;
    return el.scrollHeight - el.scrollTop - el.clientHeight < 140;
  }

  function scrollMessagesToBottom(force = false) {
    const el = elements.conversation;
    if (!force && !stickToBottom) return;
    // .conversation 上的 scroll-behavior: smooth 会让每帧的 scrollTop 赋值互相打断，
    // 流式输出时表现为"根本不往下走"，所以这里临时切成 auto 做瞬时滚动。
    const previous = el.style.scrollBehavior;
    el.style.scrollBehavior = "auto";
    el.scrollTop = el.scrollHeight;
    el.style.scrollBehavior = previous;
    stickToBottom = true;
  }

  function autoResize() {
    const el = elements.messageInput;
    el.style.height = "auto";
    el.style.height = `${Math.min(el.scrollHeight, 180)}px`;
  }

  function syncComposer() {
    const ready = Boolean(state.activeSessionId) && !state.isStreaming;
    elements.messageInput.disabled = !ready;
    elements.sendButton.disabled = !ready || elements.messageInput.value.trim() === "";
  }

  /* ---------- 剪贴板 ---------------------------------------------------- */

  async function copyText(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
      try {
        await navigator.clipboard.writeText(text);
        return true;
      } catch {
        /* 非安全上下文会拒绝，走兜底方案 */
      }
    }
    try {
      const area = document.createElement("textarea");
      area.value = text;
      area.setAttribute("readonly", "");
      area.style.position = "fixed";
      area.style.top = "-1000px";
      area.style.opacity = "0";
      document.body.append(area);
      area.select();
      const ok = document.execCommand("copy");
      area.remove();
      return ok;
    } catch {
      return false;
    }
  }

  async function copyMessage(contentEl, button) {
    const text = contentEl.textContent || "";
    if (!text) return;
    const ok = await copyText(text);
    if (!ok) {
      showToast("复制失败，请手动选择文本", true);
      return;
    }
    const original = button.innerHTML;
    button.innerHTML = `${svgIcon("check")}<span>已复制</span>`;
    window.setTimeout(() => {
      button.innerHTML = original;
    }, 1600);
  }

  /* ---------- 会话操作 -------------------------------------------------- */

  async function selectSession(sessionId) {
    if (state.isStreaming) {
      showToast("正在生成回复，请稍后再切换", true);
      return;
    }

    const session = state.sessions.find((item) => item.id === sessionId);
    state.activeSessionId = sessionId;
    if (session && session.model) {
      state.activeModel = session.model;
      elements.activeModel.textContent = session.model;
    }
    renderSessions();
    setRequestStatus("正在载入对话…");

    try {
      const payload = await request(
        `/api/session/${encodeURIComponent(sessionId)}/history`
      );
      renderMessages(payload && payload.data);
      setRequestStatus("");
    } catch (error) {
      setRequestStatus(error.message, true);
      showToast(error.message, true);
    }

    syncComposer();
    elements.messageInput.focus();
  }

  function renderModelOptions() {
    elements.modelOptions.innerHTML = "";

    if (state.models.length === 0) {
      const note = document.createElement("p");
      note.className = "inline-error";
      note.textContent = "服务端没有可用模型，请检查启动参数（--ollama_model_name 等）。";
      elements.modelOptions.append(note);
      elements.createSession.disabled = true;
      return;
    }

    elements.createSession.disabled = false;
    state.models.forEach((model) => {
      const label = document.createElement("label");
      label.className = "model-option";

      const radio = document.createElement("input");
      radio.type = "radio";
      radio.name = "model";
      radio.value = model.name;

      const body = document.createElement("span");
      const name = document.createElement("span");
      name.className = "option-name";
      name.textContent = model.name;
      const desc = document.createElement("span");
      desc.className = "option-desc";
      desc.textContent = model.desc || "";
      body.append(name, desc);

      label.append(radio, body);
      elements.modelOptions.append(label);
    });
  }

  function openNewChatDialog() {
    if (state.models.length === 0) {
      showToast("服务端没有可用模型", true);
      return;
    }

    elements.modelError.textContent = "";
    const radios = Array.from(
      elements.modelOptions.querySelectorAll('input[name="model"]')
    );
    const preferred = radios.find((radio) => radio.value === state.activeModel);
    (preferred || radios[0]).checked = true;
    elements.modelDialog.showModal();
  }

  async function handleNewSessionSubmit(event) {
    event.preventDefault();

    if (event.submitter && event.submitter.value === "cancel") {
      elements.modelDialog.close();
      return;
    }

    const field = elements.newSessionForm.elements.model;
    const model = field && field.value;
    if (!model) {
      elements.modelError.textContent = "请先选择一个模型。";
      return;
    }

    elements.modelError.textContent = "";
    elements.createSession.disabled = true;
    elements.createSession.textContent = "正在创建…";

    try {
      const payload = await request("/api/session", {
        method: "POST",
        body: JSON.stringify({ model }),
      });
      const sessionId = payload && payload.data && payload.data.session_id;
      if (!sessionId) throw new Error("服务端没有返回会话 ID");

      state.activeSessionId = sessionId;
      state.activeModel = model;
      elements.activeModel.textContent = model;
      renderMessages([]);
      elements.modelDialog.close();

      await loadSessions();
      renderSessions();
      syncComposer();
      elements.messageInput.focus();
      showToast("新对话已创建");
    } catch (error) {
      elements.modelError.textContent = error.message;
    } finally {
      elements.createSession.disabled = false;
      elements.createSession.textContent = "开始对话";
    }
  }

  function openDeleteDialog(sessionId) {
    state.pendingDeleteId = sessionId;
    elements.deleteDialog.showModal();
  }

  async function handleDeleteSubmit(event) {
    event.preventDefault();

    if (event.submitter && event.submitter.value === "cancel") {
      elements.deleteDialog.close();
      return;
    }

    const sessionId = state.pendingDeleteId;
    if (!sessionId) {
      elements.deleteDialog.close();
      return;
    }

    elements.confirmDelete.disabled = true;
    elements.confirmDelete.textContent = "正在删除…";

    try {
      await request(`/api/session/${encodeURIComponent(sessionId)}`, {
        method: "DELETE",
      });
      elements.deleteDialog.close();

      if (state.activeSessionId === sessionId) {
        state.activeSessionId = null;
        state.messages = [];
        elements.activeModel.textContent = "选择模型后开始聊天";
        renderMessages([]);
      }

      await loadSessions();
      renderSessions();
      syncComposer();
      showToast("会话已删除");
    } catch (error) {
      showToast(error.message, true);
    } finally {
      state.pendingDeleteId = null;
      elements.confirmDelete.disabled = false;
      elements.confirmDelete.textContent = "删除对话";
    }
  }

  /* ---------- 发送与流式接收 -------------------------------------------- */

  async function handleSend(event) {
    event.preventDefault();

    const text = elements.messageInput.value.trim();
    if (!text || state.isStreaming) return;
    if (!state.activeSessionId) {
      showToast("请先创建一段对话", true);
      return;
    }

    elements.messageInput.value = "";
    autoResize();

    const userMessage = {
      role: "user",
      content: text,
      timestamp: nowSeconds(),
    };
    state.messages.push(userMessage);
    elements.messageList.append(createMessageElement(userMessage));
    updateLayout();
    syncComposer();
    scrollMessagesToBottom(true);

    await streamReply(text);
  }

  // 服务端在模型调用失败时会直接掐断 SSE 连接，浏览器抛的是 TypeError("network error")，
  // 直接展示会让用户以为是自己的网络问题，这里换成能指向根因的提示。
  function describeStreamError(error) {
    if (error instanceof TypeError) {
      return "服务端中断了连接：模型调用失败（API Key 无效或上游超时），请查看服务端日志";
    }
    return (error && error.message) || "未知错误";
  }

  async function streamReply(message) {
    const placeholder = createMessageElement({
      role: "assistant",
      content: "",
      timestamp: nowSeconds(),
    });
    placeholder.classList.add("streaming");
    const contentEl = placeholder.querySelector(".msg-content");
    elements.messageList.append(placeholder);

    const assistantMessage = { role: "assistant", content: "", timestamp: nowSeconds() };
    state.messages.push(assistantMessage);
    updateLayout();

    state.isStreaming = true;
    syncComposer();
    setConnection("busy", "生成中");
    setRequestStatus("等待模型响应…");
    elements.conversation.setAttribute("aria-busy", "true");

    let full = "";
    let started = false;
    let finished = false;

    try {
      const response = await fetch("/api/message/async", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          session_id: state.activeSessionId,
          message,
        }),
      });

      if (!response.ok || !response.body) {
        throw new Error(`服务端返回 HTTP ${response.status}`);
      }

      const reader = response.body.getReader();
      const decoder = new TextDecoder("utf-8");
      let buffer = "";

      while (!finished) {
        const { value, done } = await reader.read();
        if (done) break;

        buffer += decoder.decode(value, { stream: true });

        let boundary = buffer.indexOf("\n\n");
        while (boundary !== -1) {
          const frame = buffer.slice(0, boundary).trim();
          buffer = buffer.slice(boundary + 2);
          boundary = buffer.indexOf("\n\n");

          if (!frame.startsWith("data:")) continue;
          const data = frame.slice(5).trim();

          if (data === SSE_DONE) {
            finished = true;
            break;
          }

          let chunk = "";
          try {
            chunk = JSON.parse(data);
          } catch {
            chunk = data;
          }

          if (typeof chunk !== "string") continue;
          // Ollama 的首帧 content 常为空或只有换行，直接拼接会让回答开头顶出空行。
          const piece = started ? chunk : chunk.replace(/^\s+/, "");
          if (piece === "") continue;
          started = true;
          full += piece;
          contentEl.innerHTML = renderMarkdown(full);
          scrollMessagesToBottom();
        }
      }

      if (!finished) throw new Error("连接中断，回答可能不完整");
      full = full.replace(/\s+$/, "");
      contentEl.innerHTML = renderMarkdown(full);
      setRequestStatus("");
    } catch (error) {
      const reason = describeStreamError(error);
      setRequestStatus(reason, true);
      showToast(reason, true);
      if (full) {
        contentEl.innerHTML = renderMarkdown(full);
      }
    } finally {
      state.isStreaming = false;
      placeholder.classList.remove("streaming");
      assistantMessage.content = full;

      if (!full) {
        placeholder.remove();
        state.messages.pop();
        updateLayout();
      }

      elements.conversation.setAttribute("aria-busy", "false");
      setConnection("online", "服务在线");
      syncComposer();
      elements.messageInput.focus();
      scrollMessagesToBottom();

      try {
        await loadSessions();
      } catch {
        /* 列表刷新失败不影响本轮对话 */
      }
    }
  }

  /* ---------- 主题与侧栏 ------------------------------------------------ */

  function applyTheme(theme, persist = true) {
    const isDark = theme === "dark";
    document.documentElement.dataset.theme = isDark ? "dark" : "light";
    elements.themeToggle.innerHTML = svgIcon(isDark ? "sun" : "moon");
    elements.themeToggle.setAttribute("aria-label", isDark ? "切换日间版" : "切换夜间版");
    elements.themeToggle.title = isDark ? "切换日间版" : "切换夜间版";
    if (persist) {
      try {
        window.localStorage.setItem(THEME_KEY, isDark ? "dark" : "light");
      } catch {
        /* 隐私模式下忽略 */
      }
    }
  }

  function initialTheme() {
    try {
      const stored = window.localStorage.getItem(THEME_KEY);
      if (stored === "dark" || stored === "light") return stored;
    } catch {
      /* ignore */
    }
    return window.matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
  }

  function openSidebar() {
    elements.body.classList.add("sidebar-open");
    elements.openSidebar.setAttribute("aria-expanded", "true");
  }

  function closeSidebar() {
    elements.body.classList.remove("sidebar-open");
    elements.openSidebar.setAttribute("aria-expanded", "false");
  }

  /* ---------- 事件绑定 -------------------------------------------------- */

  function bindEvents() {
    elements.composer.addEventListener("submit", handleSend);

    elements.conversation.addEventListener(
      "scroll",
      () => {
        stickToBottom = isNearBottom();
      },
      { passive: true }
    );

    elements.messageInput.addEventListener("input", () => {
      autoResize();
      syncComposer();
    });

    elements.messageInput.addEventListener("keydown", (event) => {
      if (event.key !== "Enter" || event.shiftKey || event.isComposing) return;
      event.preventDefault();
      if (!elements.sendButton.disabled) elements.composer.requestSubmit();
    });

    elements.newChat.addEventListener("click", openNewChatDialog);
    elements.welcomeNewChat.addEventListener("click", openNewChatDialog);
    elements.newSessionForm.addEventListener("submit", handleNewSessionSubmit);
    elements.deleteSessionForm.addEventListener("submit", handleDeleteSubmit);

    elements.deleteDialog.addEventListener("close", () => {
      state.pendingDeleteId = null;
    });

    elements.themeToggle.addEventListener("click", () => {
      const next = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
      applyTheme(next);
    });

    elements.openSidebar.addEventListener("click", openSidebar);
    elements.closeSidebar.addEventListener("click", closeSidebar);
    elements.scrim.addEventListener("click", closeSidebar);

    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape" && elements.body.classList.contains("sidebar-open")) {
        closeSidebar();
      }
    });

    window.addEventListener("offline", () => setConnection("offline", "网络断开"));
    window.addEventListener("online", () => setConnection("online", "服务在线"));
  }

  /* ---------- 启动 ------------------------------------------------------ */

  async function initialise() {
    applyTheme(initialTheme(), false);
    bindEvents();
    renderModelOptions();
    syncComposer();

    try {
      const [modelsPayload] = await Promise.all([request("/api/models"), loadSessions()]);
      state.models = Array.isArray(modelsPayload && modelsPayload.data)
        ? modelsPayload.data
        : [];
      renderModelOptions();
      setConnection("online", "服务在线");
    } catch (error) {
      setConnection("offline", "服务未就绪");
      showToast(error.message || "无法连接服务端", true);
    }
  }

  initialise();
})();
