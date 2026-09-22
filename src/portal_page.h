#pragma once
#include <Arduino.h>

const char PORTAL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Departure board setup</title>
<style>
 :root{color-scheme:light dark}
 body{font:15px system-ui,sans-serif;margin:0;padding:16px;max-width:760px}
 h1{font-size:20px;margin:0 0 16px}
 fieldset{border:1px solid #8886;border-radius:6px;margin:0 0 14px;padding:12px}
 legend{padding:0 6px;font-weight:600}
 label{display:block;font-size:13px;opacity:.8;margin:8px 0 2px}
 input[type=text]{width:100%;box-sizing:border-box;padding:6px;font:inherit;
   border:1px solid #8886;border-radius:4px;background:transparent;color:inherit}
 .row{display:flex;gap:10px;flex-wrap:wrap}
 .row>div{flex:1 1 150px}
 button{font:inherit;padding:6px 12px;border-radius:4px;border:1px solid #8886;
   background:transparent;color:inherit;cursor:pointer}
 button:disabled{opacity:.5;cursor:default}
 button.primary{background:#2563eb;border-color:#2563eb;color:#fff;padding:8px 20px}
 .note{font-size:12px;opacity:.7;margin:4px 0 0}
 .ok{color:#15803d}.bad{color:#b91c1c}
 #status{margin-left:12px;font-size:13px}
</style>
</head>
<body>
<h1>Departure board setup</h1>

<fieldset><legend>Board</legend>
 <label for="loc">Location shown on the panel</label>
 <input type="text" id="loc" maxlength="23">
 <label for="theme">Theme</label>
 <select id="theme"></select>
 <p class="note">The theme changes on the panel straight away. Everything else
  applies when you save, which restarts the board.</p>
</fieldset>

<fieldset><legend>Groups</legend>
 <div id="groups"></div>
 <button type="button" id="addgroup">Add group</button>
 <p class="note">A group is a saved set of watches &mdash; a weekday commute, a
  weekend one. Maximum four. <b>Active</b> is the one the board fetches and
  shows; only one can be active at a time, and you can also switch it from the
  cog on the panel. <b>Edit</b> picks which group's watches you are editing
  below, which does not have to be the active one.</p>
</fieldset>

<fieldset><legend>Watches in <span id="editing-name"></span></legend>
 <div id="watches"></div>
 <button type="button" id="add">Add watch</button>
 <p class="note">Maximum four per group. Stop code is the number on the pole,
  letters and digits only. Leave the route blank to show every route at that
  stop. The toward stop is one further along in the direction you care about
  &mdash; it is not a compass direction. Every group needs at least one watch
  switched on.</p>
</fieldset>

<button type="button" class="primary" id="save">Save and restart</button>
<span id="status"></span>

<script>
const $ = s => document.querySelector(s);
let groups = [];       // [{name, watches: [...]}] - the whole config, not one group
let activeGroup = 0;   // the group the BOARD fetches and shows
let editing = 0;       // the group whose watches this page is editing
let checking = false;  // true while a /api/stop request is in flight
let nextWatchId = 1;  // stable per-row id, since array indexes shift on removal

// Active and editing are deliberately separate: you need to be able to set up
// next week's group without pointing the board at it yet.
const watchesOf = () => (groups[editing] ? groups[editing].watches : []);

// The board serves both the departure fetcher and this portal from one heap,
// and a stop check opens its own TLS session to Auckland Transport. Two of
// those at once measurably starves the fetcher (occasional 502s and a
// backoff on hardware). The fix lives here, not in the firmware: never let
// more than one check be in flight, so every Check button is disabled while
// any check runs and re-enabled once it settles.
function setChecking(v) {
  checking = v;
  document.querySelectorAll('[data-a=check]').forEach(b => b.disabled = v);
}

// Group rows. The name input deliberately does NOT re-render on every
// keystroke - that would drop focus mid-word - so it updates the model and
// the one heading that echoes it.
function renderGroups() {
  const box = $('#groups');
  box.innerHTML = '';
  groups.forEach((g, i) => {
    const d = document.createElement('div');
    d.className = 'row';
    d.style.cssText = 'border-top:1px solid #8883;padding:10px 0;align-items:center';

    const pick = document.createElement('div');
    pick.style.cssText = 'flex:0 0 auto';
    const edit = document.createElement('button');
    edit.type = 'button';
    edit.textContent = i === editing ? 'Editing' : 'Edit';
    edit.disabled = i === editing;
    edit.onclick = () => { editing = i; renderGroups(); render(); };
    pick.appendChild(edit);

    const nameBox = document.createElement('div');
    const nameLabel = document.createElement('label');
    nameLabel.textContent = 'Name';
    const name = document.createElement('input');
    name.type = 'text';
    name.maxLength = 23;
    name.value = g.name || '';
    name.oninput = () => { g.name = name.value; showEditingName(); };
    nameBox.appendChild(nameLabel);
    nameBox.appendChild(name);

    const act = document.createElement('div');
    act.style.cssText = 'flex:0 0 auto';
    const actLabel = document.createElement('label');
    const radio = document.createElement('input');
    radio.type = 'radio';
    radio.name = 'active';
    radio.checked = i === activeGroup;
    radio.onchange = () => { activeGroup = i; };
    actLabel.appendChild(radio);
    actLabel.appendChild(document.createTextNode(' Active'));
    act.appendChild(actLabel);

    const rm = document.createElement('div');
    rm.style.cssText = 'flex:0 0 auto';
    const del = document.createElement('button');
    del.type = 'button';
    del.textContent = 'Remove';
    // The board needs somewhere to point: the last group cannot go.
    del.disabled = groups.length <= 1;
    del.onclick = () => {
      groups.splice(i, 1);
      // Both indexes have to survive the hole the splice left behind. Order
      // matters: test for "this was the one" FIRST, because a later index is
      // out of range only by virtue of the shift, and wants shifting down
      // rather than resetting.
      if (activeGroup === i) activeGroup = 0;
      else if (activeGroup > i) activeGroup--;
      if (editing === i) editing = 0;
      else if (editing > i) editing--;
      renderGroups();
      render();
    };
    rm.appendChild(del);

    d.appendChild(pick);
    d.appendChild(nameBox);
    d.appendChild(act);
    d.appendChild(rm);
    box.appendChild(d);
  });
  $('#addgroup').disabled = groups.length >= 4;
  showEditingName();
}

function showEditingName() {
  const g = groups[editing];
  // textContent, never innerHTML: a group name is user input.
  $('#editing-name').textContent = g && g.name ? g.name : 'this group';
}

function render() {
  const watches = watchesOf();
  $('#watches').innerHTML = '';
  watches.forEach((w, i) => {
    const d = document.createElement('div');
    d.className = 'row';
    d.dataset.id = w.id;
    d.style.cssText = 'border-top:1px solid #8883;padding:10px 0;align-items:flex-end';
    d.innerHTML =
      '<div><label>Label</label><input type="text" maxlength="31" data-k="label"></div>' +
      '<div><label>Stop code</label><input type="text" maxlength="31" data-k="stop_code"></div>' +
      '<div><label>Route</label><input type="text" maxlength="31" data-k="route_short_name"></div>' +
      '<div><label>Toward stop</label><input type="text" maxlength="31" data-k="toward_stop_code"></div>' +
      '<div style="flex:0 0 auto"><label><input type="checkbox" data-k="enabled"> On</label></div>' +
      '<div style="flex:0 0 auto"><button type="button" data-a="check">Check</button> ' +
      '<button type="button" data-a="del">Remove</button></div>' +
      '<div style="flex:1 1 100%" class="note" data-r=""></div>';
    d.querySelectorAll('[data-k]').forEach(el => {
      const k = el.dataset.k;
      if (k === 'enabled') { el.checked = w.enabled; el.onchange = () => w.enabled = el.checked; }
      else { el.value = w[k] || ''; el.oninput = () => w[k] = el.value; }
    });
    d.querySelector('[data-a=del]').onclick = () => { watchesOf().splice(i, 1); render(); };
    d.querySelector('[data-a=check]').onclick = () => check(w.id);
    $('#watches').appendChild(d);
  });
  $('#add').disabled = watchesOf().length >= 4;
  // A fresh render() rebuilds every Check button, so re-apply the in-flight
  // disable in case a check is still running when watches re-render.
  if (checking) document.querySelectorAll('[data-a=check]').forEach(b => b.disabled = true);
}

// Looks the result <div> up by watch id at the moment there is something to
// show, instead of holding a reference captured at click time: add/remove
// re-renders #watches while a check is in flight (the button stays disabled
// meanwhile, but the row list itself can still change), which would detach
// a captured node and swallow the answer silently. If the row is gone by
// the time this runs, there is genuinely nothing to report.
function showCheckResult(id, text, cls) {
  const out = document.querySelector('[data-id="' + id + '"] [data-r]');
  if (!out) return;
  out.textContent = text;
  out.className = cls ? 'note ' + cls : 'note';
}

async function check(id) {
  if (checking) return;  // one /api/stop at a time, board-wide
  const w = watchesOf().find(x => x.id === id);
  if (!w) return;
  setChecking(true);
  showCheckResult(id, 'checking…', '');
  try {
    const r = await fetch('/api/stop', {
      method: 'POST', body: JSON.stringify({stop_code: w.stop_code})
    });
    const j = await r.json();
    showCheckResult(id, r.ok ? j.stop_name : j.error, r.ok ? 'ok' : 'bad');
  } catch (e) { showCheckResult(id, 'the board did not answer', 'bad'); }
  setChecking(false);
}

async function load() {
  const cfg = await (await fetch('/api/config')).json();
  $('#loc').value = cfg.location;
  groups = (cfg.groups || []).map(g => ({
    name: g.name,
    // The client-only id keeps a row's check result attached to it across
    // re-renders; it is stripped again on save.
    watches: (g.watches || []).map(w => Object.assign({id: nextWatchId++}, w))
  }));
  if (!groups.length) groups = [{name: 'Main', watches: []}];
  activeGroup = cfg.active_group < groups.length ? cfg.active_group : 0;
  editing = activeGroup;  // the group you most likely came here to change
  renderGroups();
  render();
  const t = await (await fetch('/api/themes')).json();
  const sel = $('#theme');
  sel.innerHTML = '';
  // Built with createElement/textContent, not innerHTML string concatenation,
  // so a theme name can never be parsed as markup - the one sink in this page
  // that used to differ from every other data path here.
  t.themes.forEach((n, i) => {
    const opt = document.createElement('option');
    opt.value = i;
    opt.textContent = n;
    sel.appendChild(opt);
  });
  sel.value = t.selected;
}

$('#add').onclick = () => {
  const watches = watchesOf();
  if (watches.length >= 4) return;
  watches.push({id: nextWatchId++, label: '', stop_code: '', route_short_name: '',
                toward_stop_code: '', enabled: true});
  render();
};

$('#addgroup').onclick = () => {
  if (groups.length >= 4) return;
  groups.push({name: 'Group ' + (groups.length + 1), watches: []});
  editing = groups.length - 1;  // you just made it; you want to fill it in
  renderGroups();
  render();
};

$('#theme').onchange = async e => {
  await fetch('/api/theme', {
    method: 'POST', body: JSON.stringify({theme: Number(e.target.value)})
  });
};

$('#save').onclick = async () => {
  const s = $('#status');
  s.textContent = 'saving…';
  s.className = '';
  // The board rejects a group with nothing switched on, but its error cannot
  // say which group - so catch it here, where the group has a name, rather
  // than bouncing back something the page cannot point at.
  const empty = groups.findIndex(g => !g.watches.some(w => w.enabled && w.stop_code));
  if (empty >= 0) {
    s.textContent = '"' + (groups[empty].name || 'group ' + (empty + 1)) +
                    '" needs at least one watch switched on, with a stop code';
    s.className = 'bad';
    editing = empty;  // take them to the group that needs fixing
    renderGroups();
    render();
    return;
  }

  const body = JSON.stringify({
    v: 2, location: $('#loc').value, theme: Number($('#theme').value),
    active_group: activeGroup,
    // Drop the client-only "id" used to track rows across re-renders; the
    // board's schema only knows the five watch fields below.
    groups: groups.map(g => ({
      name: g.name,
      watches: g.watches.map(w => ({
        label: w.label, stop_code: w.stop_code,
        route_short_name: w.route_short_name, toward_stop_code: w.toward_stop_code,
        enabled: w.enabled
      }))
    }))
  });
  let r, j;
  try { r = await fetch('/api/config', {method: 'POST', body: body}); j = await r.json(); }
  catch (e) { s.textContent = 'the board did not answer'; s.className = 'bad'; return; }
  if (!r.ok) { s.textContent = j.error; s.className = 'bad'; return; }
  s.textContent = 'restarting…';
  const wait = setInterval(async () => {
    try {
      await fetch('/api/config', {cache: 'no-store'});
      clearInterval(wait);
      s.textContent = 'back up';
      s.className = 'ok';
      load();
    } catch (e) { /* still down - keep waiting */ }
  }, 1000);
};

load();
</script>
</body>
</html>)HTML";
