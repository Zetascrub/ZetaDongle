// The Zeta-themed script-editor single-page app served at "/". Generated
// from a plain HTML/CSS/JS source (see git history / devices/t-dongle-s3/
// README.md for the source and regeneration steps) with the mascot header
// image inlined as a base64 PNG (from
// /mnt/Storage/Coding/Misc/Mascot/Zeta_Mascot_Headshot_transparent.png) so the
// whole page is one self-contained response - no separate asset fetch, no
// LittleFS entry competing with script storage.
#pragma once

static const char kWebUiPage[] PROGMEM = R"ZETAHTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Zetascrub // T-Dongle-S3</title>
<style>
  :root {
    --navy-deep: #04141f;
    --navy: #08283e;
    --navy-light: #0f3a58;
    --cyan: #22e0f2;
    --cyan-dim: #1596a3;
    --blue: #2274b5;
    --cream: #f8eed0;
    --tan: #b48353;
    --orange: #f6a102;
    color-scheme: dark;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    background: var(--navy-deep);
    color: var(--cream);
    font-family: "Segoe UI", system-ui, sans-serif;
    padding: 16px;
    padding-block: 20px;
  }
  .app { max-width: 960px; margin: 0 auto; }
  header {
    display: flex;
    align-items: center;
    gap: 16px;
    border-bottom: 2px solid var(--navy-light);
    padding-bottom: 14px;
    margin-bottom: 16px;
    flex-wrap: wrap;
  }
  header img {
    width: 64px;
    height: 64px;
    border-radius: 50%;
    box-shadow: 0 0 0 2px var(--cyan), 0 0 14px rgba(34, 224, 242, 0.5);
    flex: none;
  }
  header h1 {
    font-size: 1.3rem;
    letter-spacing: 0.04em;
    margin: 0;
    color: var(--cyan);
    text-shadow: 0 0 12px rgba(34, 224, 242, 0.35);
  }
  header .tagline {
    margin: 4px 0 0;
    font-size: 0.8rem;
    color: var(--tan);
    letter-spacing: 0.12em;
  }
  .status {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: 10px;
    background: var(--navy);
    border: 1px solid var(--navy-light);
    border-radius: 10px;
    padding: 12px 14px;
    margin-bottom: 18px;
    font-size: 0.85rem;
  }
  .status .stat b {
    display: block;
    color: var(--cyan);
    font-size: 0.7rem;
    text-transform: uppercase;
    letter-spacing: 0.08em;
    margin-bottom: 2px;
  }
  main {
    display: grid;
    grid-template-columns: minmax(160px, 220px) 1fr;
    gap: 18px;
  }
  @media (max-width: 620px) {
    main { grid-template-columns: 1fr; }
  }
  .script-list {
    background: var(--navy);
    border: 1px solid var(--navy-light);
    border-radius: 10px;
    padding: 10px;
    min-height: 120px;
  }
  .script-list h2 {
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.1em;
    color: var(--tan);
    margin: 2px 6px 8px;
  }
  .script-item {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 6px;
    padding: 7px 8px;
    border-radius: 6px;
    cursor: pointer;
    font-size: 0.88rem;
    word-break: break-all;
  }
  .script-item:hover { background: var(--navy-light); }
  .script-item.active { background: var(--navy-light); box-shadow: inset 2px 0 0 var(--cyan); }
  .script-item .badge {
    font-size: 0.62rem;
    background: var(--orange);
    color: var(--navy-deep);
    padding: 1px 5px;
    border-radius: 4px;
    font-weight: 700;
    letter-spacing: 0.05em;
    flex: none;
  }
  .empty-hint { color: var(--tan); font-size: 0.82rem; padding: 8px; }
  .editor {
    background: var(--navy);
    border: 1px solid var(--navy-light);
    border-radius: 10px;
    padding: 14px;
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  .editor input[type=text] {
    background: var(--navy-deep);
    border: 1px solid var(--navy-light);
    color: var(--cream);
    padding: 8px 10px;
    border-radius: 6px;
    font-size: 0.95rem;
  }
  .editor textarea {
    background: var(--navy-deep);
    border: 1px solid var(--navy-light);
    color: var(--cream);
    padding: 10px;
    border-radius: 6px;
    font-family: "Cascadia Code", "Fira Code", ui-monospace, Consolas, monospace;
    font-size: 0.88rem;
    min-height: 260px;
    resize: vertical;
    line-height: 1.4;
  }
  .editor input:focus, .editor textarea:focus {
    outline: none;
    border-color: var(--cyan);
    box-shadow: 0 0 0 2px rgba(34, 224, 242, 0.25);
  }
  .actions { display: flex; gap: 8px; flex-wrap: wrap; }
  button {
    border: none;
    border-radius: 6px;
    padding: 9px 14px;
    font-size: 0.85rem;
    font-weight: 600;
    letter-spacing: 0.03em;
    cursor: pointer;
    color: var(--navy-deep);
    background: var(--cyan);
    transition: transform 0.08s ease;
  }
  button:hover { transform: translateY(-1px); }
  button:active { transform: translateY(0); }
  button.primary { background: var(--orange); }
  button.ghost { background: transparent; color: var(--cream); border: 1px solid var(--navy-light); }
  button.danger { background: transparent; color: #ff6b6b; border: 1px solid #7a2b2b; }
  button:disabled { opacity: 0.45; cursor: not-allowed; transform: none; }
  .note {
    margin: 0;
    font-size: 0.78rem;
    color: var(--tan);
    border-left: 3px solid var(--tan);
    padding-left: 8px;
  }
  details.cheatsheet {
    font-size: 0.82rem;
    background: var(--navy-deep);
    border: 1px solid var(--navy-light);
    border-radius: 6px;
    padding: 8px 10px;
  }
  details.cheatsheet summary {
    cursor: pointer;
    color: var(--cyan);
    font-weight: 600;
  }
  details.cheatsheet table { width: 100%; border-collapse: collapse; margin-top: 8px; }
  details.cheatsheet td { padding: 3px 6px; vertical-align: top; border-top: 1px solid var(--navy-light); }
  details.cheatsheet td:first-child { color: var(--cyan); font-family: ui-monospace, monospace; white-space: nowrap; }
  .toast {
    position: fixed;
    bottom: 18px;
    left: 50%;
    transform: translateX(-50%);
    background: var(--navy-light);
    border: 1px solid var(--cyan);
    color: var(--cream);
    padding: 9px 16px;
    border-radius: 8px;
    font-size: 0.85rem;
    opacity: 0;
    pointer-events: none;
    transition: opacity 0.2s ease;
  }
  .toast.show { opacity: 1; }
  .toast.error { border-color: #ff6b6b; }
</style>
</head>
<body>
<div class="app">
  <header>
    <img src="data:image/png;base64,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" alt="Zetascrub">
    <div class="titles">
      <h1>ZETASCRUB :: RECONCLAVE T-DONGLE-S3</h1>
      <p class="tagline">TINKER &middot; HACK &middot; FLASH &middot; REPEAT</p>
    </div>
  </header>

  <section class="status" id="status">
    <div class="stat"><b>Access point</b><span id="statSsid">&hellip;</span></div>
    <div class="stat"><b>Armed script</b><span id="statAssigned">&hellip;</span></div>
    <div class="stat"><b>Fires this boot</b><span id="statFires">&hellip;</span></div>
    <div class="stat"><b>Uptime</b><span id="statUptime">&hellip;</span></div>
  </section>

  <main>
    <div class="script-list">
      <h2>Saved scripts</h2>
      <div id="scriptList"><p class="empty-hint">Loading&hellip;</p></div>
    </div>

    <div class="editor">
      <input type="text" id="nameInput" placeholder="script-name (letters, digits, - and _)">
      <textarea id="bodyInput" spellcheck="false" placeholder="REM my payload&#10;DELAY 500&#10;STRING hello from zetascrub&#10;ENTER"></textarea>
      <div class="actions">
        <button class="primary" id="saveBtn">Save</button>
        <button class="ghost" id="newBtn">New</button>
        <button id="assignBtn">Assign to button</button>
        <button class="ghost" id="unassignBtn">Unassign</button>
        <button class="danger" id="deleteBtn">Delete</button>
      </div>
      <p class="note">Scripts only ever fire from the physical button on the device itself &mdash; this page can edit and arm them, but there is deliberately no remote "run" button here.</p>
      <details class="cheatsheet">
        <summary>Supported commands</summary>
        <table>
          <tr><td>REM text</td><td>Comment, ignored.</td></tr>
          <tr><td>STRING text</td><td>Types the rest of the line literally.</td></tr>
          <tr><td>STRINGLN text</td><td>Same, then presses Enter.</td></tr>
          <tr><td>DELAY ms</td><td>Pauses (capped at 60000ms).</td></tr>
          <tr><td>DEFAULTDELAY ms</td><td>Pause applied after every later line.</td></tr>
          <tr><td>REPEAT n</td><td>Repeats the previous line n more times (capped at 50).</td></tr>
          <tr><td>GUI / CTRL / ALT / SHIFT</td><td>Modifiers, combine with a key: <code>CTRL ALT DELETE</code>, <code>GUI r</code>.</td></tr>
          <tr><td>ENTER TAB ESC SPACE BACKSPACE DELETE HOME END INSERT PAGEUP PAGEDOWN UP DOWN LEFT RIGHT CAPSLOCK F1-F12</td><td>Named keys, or any single character.</td></tr>
        </table>
      </details>
    </div>
  </main>
</div>
<div class="toast" id="toast"></div>

<script>
(function () {
  var state = { scripts: [], assigned: "", loadedName: null };
  var nameInput = document.getElementById("nameInput");
  var bodyInput = document.getElementById("bodyInput");
  var toastEl = document.getElementById("toast");

  function toast(msg, isError) {
    toastEl.textContent = msg;
    toastEl.className = "toast show" + (isError ? " error" : "");
    setTimeout(function () { toastEl.className = "toast"; }, 2600);
  }

  function fmtUptime(ms) {
    var s = Math.floor(ms / 1000);
    var h = Math.floor(s / 3600);
    var m = Math.floor((s % 3600) / 60);
    var sec = s % 60;
    return (h > 0 ? h + "h " : "") + m + "m " + sec + "s";
  }

  function refreshStatus() {
    fetch("/api/status").then(function (r) { return r.json(); }).then(function (d) {
      document.getElementById("statSsid").textContent = d.ap_ssid + " (" + d.ap_ip + ")";
      document.getElementById("statAssigned").textContent = d.assigned || "none";
      document.getElementById("statFires").textContent = d.fire_count +
        (d.last_fired_ms_ago != null ? " (last " + Math.floor(d.last_fired_ms_ago / 1000) + "s ago)" : "");
      document.getElementById("statUptime").textContent = fmtUptime(d.uptime_ms);
    }).catch(function () {});
  }

  function renderList() {
    var el = document.getElementById("scriptList");
    if (state.scripts.length === 0) {
      el.innerHTML = '<p class="empty-hint">No scripts yet &mdash; write one and hit Save.</p>';
      return;
    }
    el.innerHTML = "";
    state.scripts.forEach(function (s) {
      var item = document.createElement("div");
      item.className = "script-item" + (s.name === state.loadedName ? " active" : "");
      var label = document.createElement("span");
      label.textContent = s.name;
      item.appendChild(label);
      if (s.name === state.assigned) {
        var badge = document.createElement("span");
        badge.className = "badge";
        badge.textContent = "ARMED";
        item.appendChild(badge);
      }
      item.addEventListener("click", function () { loadScript(s.name); });
      el.appendChild(item);
    });
  }

  function refreshList() {
    return fetch("/api/scripts").then(function (r) { return r.json(); }).then(function (d) {
      state.scripts = d.scripts || [];
      state.assigned = d.assigned || "";
      renderList();
    }).catch(function () { toast("Couldn't reach the device", true); });
  }

  function loadScript(name) {
    fetch("/api/script?name=" + encodeURIComponent(name)).then(function (r) {
      if (!r.ok) throw new Error("not found");
      return r.json();
    }).then(function (d) {
      state.loadedName = d.name;
      nameInput.value = d.name;
      bodyInput.value = d.body;
      renderList();
    }).catch(function () { toast("Couldn't load " + name, true); });
  }

  function newScript() {
    state.loadedName = null;
    nameInput.value = "";
    bodyInput.value = "";
    nameInput.focus();
    renderList();
  }

  function save() {
    var name = nameInput.value.trim();
    if (!name) { toast("Name it first", true); return; }
    fetch("/api/script", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: name, body: bodyInput.value })
    }).then(function (r) { return r.json().then(function (d) { return { ok: r.ok, d: d }; }); })
      .then(function (res) {
        if (!res.ok) { toast(res.d.error || "save failed", true); return; }
        state.loadedName = name;
        toast("Saved " + name);
        return refreshList();
      }).catch(function () { toast("Couldn't reach the device", true); });
  }

  function assign(clear) {
    var name = clear ? "" : (state.loadedName || nameInput.value.trim());
    if (!clear && !name) { toast("Save it first", true); return; }
    fetch("/api/assign", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: name })
    }).then(function (r) { return r.json().then(function (d) { return { ok: r.ok, d: d }; }); })
      .then(function (res) {
        if (!res.ok) { toast(res.d.error || "assign failed", true); return; }
        toast(clear ? "Unassigned" : ("Armed " + name + " on the button"));
        return Promise.all([refreshList(), refreshStatus()]);
      }).catch(function () { toast("Couldn't reach the device", true); });
  }

  function del() {
    var name = state.loadedName || nameInput.value.trim();
    if (!name) return;
    if (!confirm('Delete "' + name + '"?')) return;
    fetch("/api/script/delete", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name: name })
    }).then(function (r) { return r.json().then(function (d) { return { ok: r.ok, d: d }; }); })
      .then(function (res) {
        if (!res.ok) { toast(res.d.error || "delete failed", true); return; }
        toast("Deleted " + name);
        newScript();
        return Promise.all([refreshList(), refreshStatus()]);
      }).catch(function () { toast("Couldn't reach the device", true); });
  }

  document.getElementById("saveBtn").addEventListener("click", save);
  document.getElementById("newBtn").addEventListener("click", newScript);
  document.getElementById("assignBtn").addEventListener("click", function () { assign(false); });
  document.getElementById("unassignBtn").addEventListener("click", function () { assign(true); });
  document.getElementById("deleteBtn").addEventListener("click", del);

  refreshList();
  refreshStatus();
  setInterval(refreshStatus, 4000);
})();
</script>
</body>
</html>

)ZETAHTML";
