import sys

import uvicorn
from fastapi import FastAPI, HTTPException, Query
from fastapi.responses import HTMLResponse
from pydantic import BaseModel, Field


class TaskCreate(BaseModel):
    title: str = Field(min_length=2, max_length=80)
    priority: int = Field(default=2, ge=1, le=3)


class Task(BaseModel):
    id: int
    title: str
    priority: int
    completed: bool


app = FastAPI(
    title="XLang3 FastAPI Mission Board",
    description="An interactive FastAPI application running on the XLang3 runtime.",
    version="1.0.0",
)

tasks: list[dict] = []
next_task_id = 1


def task_stats() -> dict:
    completed = 0
    for task in tasks:
        if task["completed"]:
            completed += 1
    return {
        "total": len(tasks),
        "active": len(tasks) - completed,
        "completed": completed,
    }


@app.get("/", response_class=HTMLResponse, include_in_schema=False)
async def home() -> str:
    return PAGE


@app.get("/api/runtime")
async def runtime_info() -> dict:
    return {
        "implementation": sys.implementation.name,
        "python_version": sys.version.split()[0],
        "framework": "FastAPI",
        "status": "ready",
    }


@app.get("/api/tasks", response_model=list[Task])
async def list_tasks(status: str = Query(default="all", pattern="^(all|active|completed)$")) -> list[dict]:
    if status == "active":
        return [task for task in tasks if not task["completed"]]
    if status == "completed":
        return [task for task in tasks if task["completed"]]
    return tasks


@app.get("/api/stats")
async def get_stats() -> dict:
    return task_stats()


@app.post("/api/tasks", response_model=Task, status_code=201)
async def create_task(payload: TaskCreate) -> dict:
    global next_task_id
    task = {
        "id": next_task_id,
        "title": payload.title.strip(),
        "priority": payload.priority,
        "completed": False,
    }
    tasks.append(task)
    next_task_id += 1
    return task


def find_task(task_id: int) -> dict:
    for task in tasks:
        if task["id"] == task_id:
            return task
    raise HTTPException(status_code=404, detail="Task not found")


@app.patch("/api/tasks/{task_id}/toggle", response_model=Task)
async def toggle_task(task_id: int) -> dict:
    task = find_task(task_id)
    task["completed"] = not task["completed"]
    return task


@app.delete("/api/tasks/{task_id}", status_code=204)
async def delete_task(task_id: int) -> None:
    task = find_task(task_id)
    tasks.remove(task)


PAGE = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>XLang3 Mission Board</title>
  <style>
    :root {
      color-scheme: dark;
      --ink: #f6f7fb;
      --muted: #9fa9bc;
      --panel: rgba(17, 23, 39, .82);
      --line: rgba(255, 255, 255, .10);
      --cyan: #64e6d4;
      --violet: #aa87ff;
      --orange: #ffb86b;
      --danger: #ff7592;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      min-height: 100vh;
      color: var(--ink);
      font: 15px/1.5 Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background:
        radial-gradient(circle at 12% 10%, rgba(87, 226, 203, .16), transparent 28rem),
        radial-gradient(circle at 88% 16%, rgba(162, 120, 255, .18), transparent 30rem),
        #080c16;
    }
    body::before {
      content: "";
      position: fixed;
      inset: 0;
      pointer-events: none;
      opacity: .22;
      background-image: linear-gradient(var(--line) 1px, transparent 1px), linear-gradient(90deg, var(--line) 1px, transparent 1px);
      background-size: 40px 40px;
      mask-image: linear-gradient(to bottom, black, transparent 75%);
    }
    .shell { width: min(1080px, calc(100% - 32px)); margin: 0 auto; padding: 52px 0 72px; position: relative; }
    header { display: flex; justify-content: space-between; gap: 30px; align-items: flex-start; margin-bottom: 28px; }
    .eyebrow { color: var(--cyan); letter-spacing: .18em; text-transform: uppercase; font-size: 12px; font-weight: 800; }
    h1 { margin: 7px 0 8px; font-size: clamp(38px, 6vw, 72px); line-height: .98; letter-spacing: -.055em; max-width: 720px; }
    .lede { margin: 0; max-width: 620px; color: var(--muted); font-size: 17px; }
    .runtime { min-width: 205px; border: 1px solid var(--line); background: var(--panel); border-radius: 16px; padding: 16px; backdrop-filter: blur(18px); }
    .runtime strong { display: block; font-size: 18px; margin: 4px 0; }
    .status { display: inline-flex; align-items: center; gap: 7px; color: var(--cyan); font-size: 12px; text-transform: uppercase; letter-spacing: .1em; }
    .dot { width: 8px; height: 8px; background: var(--cyan); border-radius: 50%; box-shadow: 0 0 16px var(--cyan); }
    .grid { display: grid; grid-template-columns: minmax(0, 1fr) 280px; gap: 20px; }
    .panel { border: 1px solid var(--line); background: var(--panel); border-radius: 22px; box-shadow: 0 22px 70px rgba(0,0,0,.28); backdrop-filter: blur(18px); }
    .composer { display: grid; grid-template-columns: 1fr 140px auto; gap: 10px; padding: 16px; border-bottom: 1px solid var(--line); }
    input, select, button { font: inherit; }
    input, select { width: 100%; border: 1px solid var(--line); background: rgba(255,255,255,.055); color: var(--ink); border-radius: 12px; padding: 12px 13px; outline: none; }
    select option { background: #111827; }
    input:focus, select:focus { border-color: var(--cyan); box-shadow: 0 0 0 3px rgba(100,230,212,.10); }
    button { cursor: pointer; border: 0; }
    .add { border-radius: 12px; padding: 0 18px; color: #07120f; background: var(--cyan); font-weight: 800; }
    .filters { display: flex; gap: 8px; padding: 14px 16px; border-bottom: 1px solid var(--line); }
    .filter { background: transparent; color: var(--muted); border-radius: 999px; padding: 7px 12px; }
    .filter.active { color: var(--ink); background: rgba(255,255,255,.09); }
    #tasks { list-style: none; margin: 0; padding: 5px 16px 16px; min-height: 310px; }
    .task { display: grid; grid-template-columns: auto 1fr auto; gap: 13px; align-items: center; padding: 14px 4px; border-bottom: 1px solid var(--line); }
    .task:last-child { border-bottom: 0; }
    .check { width: 22px; height: 22px; border-radius: 7px; border: 1px solid rgba(255,255,255,.25); background: transparent; color: #07120f; }
    .done .check { background: var(--cyan); border-color: var(--cyan); }
    .done .title { text-decoration: line-through; color: var(--muted); }
    .title { font-weight: 650; overflow-wrap: anywhere; }
    .meta { display: flex; gap: 9px; align-items: center; color: var(--muted); font-size: 12px; margin-top: 3px; }
    .priority { border-radius: 999px; padding: 2px 8px; background: rgba(170,135,255,.12); color: #cbb9ff; }
    .priority.p3 { background: rgba(255,184,107,.12); color: var(--orange); }
    .delete { background: transparent; color: var(--muted); border-radius: 8px; font-size: 19px; padding: 4px 9px; }
    .delete:hover { color: var(--danger); background: rgba(255,117,146,.08); }
    .empty { color: var(--muted); text-align: center; padding: 82px 20px; }
    .side { padding: 20px; align-self: start; }
    .side h2 { font-size: 13px; text-transform: uppercase; letter-spacing: .12em; color: var(--muted); margin: 0 0 16px; }
    .stat { display: flex; align-items: baseline; justify-content: space-between; padding: 15px 0; border-bottom: 1px solid var(--line); }
    .stat:last-of-type { border-bottom: 0; }
    .stat strong { font-size: 30px; letter-spacing: -.04em; }
    .stat span { color: var(--muted); }
    .api-link { display: block; margin-top: 22px; text-align: center; color: var(--cyan); text-decoration: none; border: 1px solid rgba(100,230,212,.25); border-radius: 12px; padding: 10px; }
    #message { min-height: 22px; padding: 0 16px 12px; color: var(--danger); font-size: 13px; }
    @media (max-width: 760px) {
      header { display: block; }
      .runtime { margin-top: 22px; }
      .grid { grid-template-columns: 1fr; }
      .composer { grid-template-columns: 1fr 115px; }
      .add { grid-column: 1 / -1; padding: 11px; }
    }
  </style>
</head>
<body>
  <main class="shell">
    <header>
      <div>
        <div class="eyebrow">Live compatibility demo</div>
        <h1>Mission control,<br>powered by XLang3.</h1>
        <p class="lede">A real FastAPI app with Pydantic validation, typed routes, live state, filtering, and OpenAPI documentation.</p>
      </div>
      <aside class="runtime">
        <div class="status"><span class="dot"></span><span id="runtime-status">connecting</span></div>
        <strong id="runtime-name">Runtime</strong>
        <span id="runtime-version" class="lede">Checking version…</span>
      </aside>
    </header>

    <section class="grid">
      <div class="panel">
        <form id="composer" class="composer">
          <input id="title" aria-label="Task title" maxlength="80" placeholder="Add a mission task…" autocomplete="off" required>
          <select id="priority" aria-label="Priority">
            <option value="1">Low priority</option>
            <option value="2" selected>Normal</option>
            <option value="3">High priority</option>
          </select>
          <button class="add" type="submit">Add task</button>
        </form>
        <nav class="filters" aria-label="Task filters">
          <button class="filter active" data-status="all">All</button>
          <button class="filter" data-status="active">Active</button>
          <button class="filter" data-status="completed">Completed</button>
        </nav>
        <ul id="tasks"><li class="empty">Loading mission board…</li></ul>
        <div id="message" role="alert"></div>
      </div>

      <aside class="panel side">
        <h2>Mission telemetry</h2>
        <div class="stat"><span>Total</span><strong id="total">0</strong></div>
        <div class="stat"><span>Active</span><strong id="active">0</strong></div>
        <div class="stat"><span>Completed</span><strong id="completed">0</strong></div>
        <a class="api-link" href="/docs">Explore the API →</a>
      </aside>
    </section>
  </main>

  <script>
    let currentFilter = 'all';

    function escapeHtml(value) {
      const node = document.createElement('span');
      node.textContent = value;
      return node.innerHTML;
    }

    async function api(path, options) {
      const response = await fetch(path, options);
      if (!response.ok) {
        let detail = 'Request failed';
        try {
          const body = await response.json();
          detail = typeof body.detail === 'string' ? body.detail : body.detail[0].msg;
        } catch (_) {}
        throw new Error(detail);
      }
      return response.status === 204 ? null : response.json();
    }

    async function loadRuntime() {
      const runtime = await api('/api/runtime');
      document.querySelector('#runtime-status').textContent = runtime.status;
      document.querySelector('#runtime-name').textContent = runtime.implementation;
      document.querySelector('#runtime-version').textContent = 'Python ' + runtime.python_version + ' · ' + runtime.framework;
    }

    async function refresh() {
      const [items, stats] = await Promise.all([
        api('/api/tasks?status=' + currentFilter),
        api('/api/stats')
      ]);
      const list = document.querySelector('#tasks');
      if (!items.length) {
        list.innerHTML = '<li class="empty">No tasks in this view. Add one above.</li>';
      } else {
        list.innerHTML = items.map(task => `
          <li class="task ${task.completed ? 'done' : ''}">
            <button class="check" data-toggle="${task.id}" aria-label="Toggle ${escapeHtml(task.title)}">${task.completed ? '✓' : ''}</button>
            <div>
              <div class="title">${escapeHtml(task.title)}</div>
              <div class="meta"><span class="priority p${task.priority}">${['', 'Low', 'Normal', 'High'][task.priority]}</span><span>#${task.id}</span></div>
            </div>
            <button class="delete" data-delete="${task.id}" aria-label="Delete ${escapeHtml(task.title)}">×</button>
          </li>`).join('');
      }
      document.querySelector('#total').textContent = stats.total;
      document.querySelector('#active').textContent = stats.active;
      document.querySelector('#completed').textContent = stats.completed;
    }

    function showError(error) {
      document.querySelector('#message').textContent = error.message;
    }

    document.querySelector('#composer').addEventListener('submit', async event => {
      event.preventDefault();
      const title = document.querySelector('#title');
      document.querySelector('#message').textContent = '';
      try {
        await api('/api/tasks', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({title: title.value, priority: Number(document.querySelector('#priority').value)})
        });
        title.value = '';
        title.focus();
        await refresh();
      } catch (error) { showError(error); }
    });

    document.querySelector('.filters').addEventListener('click', async event => {
      const button = event.target.closest('[data-status]');
      if (!button) return;
      currentFilter = button.dataset.status;
      document.querySelectorAll('.filter').forEach(item => item.classList.toggle('active', item === button));
      try { await refresh(); } catch (error) { showError(error); }
    });

    document.querySelector('#tasks').addEventListener('click', async event => {
      const toggle = event.target.closest('[data-toggle]');
      const remove = event.target.closest('[data-delete]');
      try {
        if (toggle) await api('/api/tasks/' + toggle.dataset.toggle + '/toggle', {method: 'PATCH'});
        if (remove) await api('/api/tasks/' + remove.dataset.delete, {method: 'DELETE'});
        if (toggle || remove) await refresh();
      } catch (error) { showError(error); }
    });

    Promise.all([loadRuntime(), refresh()]).catch(showError);
  </script>
</body>
</html>
"""


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    uvicorn.run(app, host="127.0.0.1", port=port, log_level="info")
