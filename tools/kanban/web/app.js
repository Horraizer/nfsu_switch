const state = {
  board: null,
  draggedTask: null,
  filter: "",
  moving: new Set(),
};

const elements = {
  activeCount: document.querySelector("#active-count"),
  board: document.querySelector("#board"),
  cardTemplate: document.querySelector("#card-template"),
  columnTemplate: document.querySelector("#column-template"),
  connection: document.querySelector("#connection"),
  openCount: document.querySelector("#open-count"),
  prCount: document.querySelector("#pr-count"),
  refresh: document.querySelector("#refresh"),
  repositoryLink: document.querySelector("#repository-link"),
  search: document.querySelector("#search"),
  toast: document.querySelector("#toast"),
  updatedAt: document.querySelector("#updated-at"),
};

const escapeColor = (color) => (/^[0-9a-f]{6}$/i.test(color) ? `#${color}` : "#8190a5");

function showToast(message, isError = false) {
  elements.toast.textContent = message;
  elements.toast.classList.toggle("error", isError);
  elements.toast.classList.add("visible");
  clearTimeout(showToast.timer);
  showToast.timer = setTimeout(() => elements.toast.classList.remove("visible"), 2600);
}

function taskMatches(task) {
  if (!state.filter) {
    return true;
  }
  const haystack = [
    task.number,
    task.title,
    task.assignee,
    task.milestone,
    ...task.labels.map((label) => label.name),
  ].join(" ").toLowerCase();
  return haystack.includes(state.filter);
}

function visibleLabels(task) {
  return task.labels.filter((label) =>
    label.name.startsWith("area/") || label.name.startsWith("type/"),
  ).slice(0, 4);
}

function labelValue(task, prefix) {
  return task.labels.find((label) => label.name.startsWith(prefix))?.name.slice(prefix.length) ?? "";
}

function createCard(task) {
  const fragment = elements.cardTemplate.content.cloneNode(true);
  const card = fragment.querySelector(".card");
  const issueLink = fragment.querySelector(".issue-number");
  const risk = fragment.querySelector(".risk-chip");
  const title = fragment.querySelector("h3");
  const chips = fragment.querySelector(".card-labels");
  const agent = fragment.querySelector(".agent");
  const moveSelect = fragment.querySelector("select");

  card.dataset.issue = String(task.number);
  card.classList.toggle("moving", state.moving.has(task.number));
  issueLink.href = task.url;
  issueLink.textContent = `#${task.number}`;
  title.textContent = task.title;

  const riskValue = labelValue(task, "risk/");
  risk.textContent = riskValue || "unrated";
  risk.classList.add(riskValue || "unrated");

  for (const label of visibleLabels(task)) {
    const chip = document.createElement("span");
    chip.className = "label-chip";
    chip.style.setProperty("--chip-color", escapeColor(label.color));
    chip.textContent = label.name;
    chips.append(chip);
  }

  const agentValue = labelValue(task, "agent/");
  agent.textContent = agentValue ? `agent: ${agentValue}` : "unassigned";

  for (const column of state.board.columns) {
    const option = document.createElement("option");
    option.value = column.id;
    option.textContent = column.title;
    option.selected = task.column === column.id;
    moveSelect.append(option);
  }
  moveSelect.disabled = state.moving.has(task.number);
  moveSelect.addEventListener("change", () => moveTask(task.number, moveSelect.value));

  card.addEventListener("dragstart", (event) => {
    if (state.moving.has(task.number)) {
      event.preventDefault();
      return;
    }
    state.draggedTask = task;
    card.classList.add("dragging");
    event.dataTransfer.effectAllowed = "move";
    event.dataTransfer.setData("text/plain", String(task.number));
  });
  card.addEventListener("dragend", () => {
    state.draggedTask = null;
    card.classList.remove("dragging");
    document.querySelectorAll(".drag-over").forEach((node) => node.classList.remove("drag-over"));
  });
  return fragment;
}

function render() {
  if (!state.board) {
    return;
  }

  elements.board.replaceChildren();
  const tasks = state.board.tasks.filter(taskMatches);

  for (const column of state.board.columns) {
    const fragment = elements.columnTemplate.content.cloneNode(true);
    const section = fragment.querySelector(".column");
    const list = fragment.querySelector(".card-list");
    const columnTasks = tasks.filter((task) => task.column === column.id);

    section.dataset.column = column.id;
    section.dataset.tone = column.tone;
    fragment.querySelector("h2").textContent = column.title;
    fragment.querySelector(".column-count").textContent = String(columnTasks.length);

    for (const task of columnTasks) {
      list.append(createCard(task));
    }
    if (columnTasks.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-state";
      empty.textContent = state.filter ? "No matching tasks" : "Drop a task here";
      list.append(empty);
    }

    section.addEventListener("dragover", (event) => {
      if (!state.draggedTask || state.draggedTask.column === column.id) {
        return;
      }
      event.preventDefault();
      event.dataTransfer.dropEffect = "move";
      section.classList.add("drag-over");
    });
    section.addEventListener("dragleave", (event) => {
      if (!section.contains(event.relatedTarget)) {
        section.classList.remove("drag-over");
      }
    });
    section.addEventListener("drop", (event) => {
      event.preventDefault();
      section.classList.remove("drag-over");
      if (state.draggedTask && state.draggedTask.column !== column.id) {
        void moveTask(state.draggedTask.number, column.id);
      }
    });

    elements.board.append(fragment);
  }
}

async function requestJson(url, options) {
  const response = await fetch(url, options);
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(payload.error ?? `Request failed with status ${response.status}`);
  }
  return payload;
}

async function loadBoard({ quiet = false } = {}) {
  elements.refresh.disabled = true;
  elements.connection.className = "connection";
  elements.connection.lastChild.textContent = " Connecting to Gitea";
  try {
    state.board = await requestJson("/api/board");
    elements.repositoryLink.href = state.board.repositoryUrl;
    elements.openCount.textContent = state.board.summary.open;
    elements.activeCount.textContent = state.board.summary.active;
    elements.prCount.textContent = state.board.summary.pullRequests;
    elements.updatedAt.textContent = `Updated ${new Date(state.board.generatedAt).toLocaleTimeString()}`;
    elements.connection.className = "connection online";
    elements.connection.lastChild.textContent = " Gitea connected";
    elements.board.setAttribute("aria-busy", "false");
    render();
    if (!quiet) {
      showToast("Board refreshed");
    }
    return state.board;
  } catch (error) {
    elements.connection.className = "connection error";
    elements.connection.lastChild.textContent = " Gitea connection failed";
    showToast(error.message, true);
    throw error;
  } finally {
    elements.refresh.disabled = false;
  }
}

async function moveTask(number, column) {
  const task = state.board.tasks.find((candidate) => candidate.number === number);
  if (!task || task.column === column || state.moving.has(number)) {
    return task;
  }

  const previousColumn = task.column;
  task.column = column;
  state.moving.add(number);
  render();

  try {
    const payload = await requestJson(`/api/issues/${number}/move`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ column }),
    });
    Object.assign(task, payload.task);
    showToast(`#${number} moved to ${column.slice("state/".length)}`);
    await loadBoard({ quiet: true });
    return payload.task;
  } catch (error) {
    task.column = previousColumn;
    showToast(error.message, true);
    render();
    throw error;
  } finally {
    state.moving.delete(number);
    render();
  }
}

function registerModelContextTools() {
  const context = document.modelContext;
  if (!context?.registerTool) {
    return;
  }

  void Promise.resolve(context.registerTool({
    name: "refresh_board",
    title: "Refresh task board",
    description: "Reload tasks and pull request counts from the configured Gitea repository.",
    inputSchema: { type: "object", properties: {}, additionalProperties: false },
    annotations: { readOnlyHint: true, untrustedContentHint: true },
    async execute() {
      const board = await loadBoard({ quiet: true });
      return { summary: board.summary, generatedAt: board.generatedAt };
    },
  })).catch(() => {});

  void Promise.resolve(context.registerTool({
    name: "move_issue",
    title: "Move task",
    description: "Move one Gitea issue to a board status. Moving to done closes it; moving elsewhere opens it.",
    inputSchema: {
      type: "object",
      properties: {
        number: { type: "integer", minimum: 1 },
        column: { type: "string", enum: [
          "state/proposed", "state/backlog", "state/ready", "state/working",
          "state/review", "state/verify", "state/blocked", "state/done",
        ] },
      },
      required: ["number", "column"],
      additionalProperties: false,
    },
    annotations: { readOnlyHint: false, untrustedContentHint: false },
    async execute(input) {
      if (!Number.isInteger(input?.number) || typeof input?.column !== "string") {
        throw new Error("A valid issue number and column are required");
      }
      const task = await moveTask(input.number, input.column);
      return { number: task.number, column: task.column, state: task.state };
    },
  })).catch(() => {});
}

elements.refresh.addEventListener("click", () => void loadBoard());
elements.search.addEventListener("input", () => {
  state.filter = elements.search.value.trim().toLowerCase();
  render();
});

registerModelContextTools();
void loadBoard({ quiet: true }).catch(() => {});
