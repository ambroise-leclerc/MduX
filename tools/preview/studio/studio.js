// MedUI Studio (#327, ADR-025): edits a screen document, previews it through mdux-preview's production
// renderer, and submits reviewable change proposals. The service is authoritative for every decision
// about validity; this page never renders, validates or writes anything on its own.

import {
    History, annotationText, carriesSafetyMetadata, clone, editText, findNode, getField, isSafetyRelevant,
    newNode, nodeId, parseValue, point, px, removeField, setField,
} from "./model.js";

const $ = (id) => document.getElementById(id);
const tokenKey = "mdux-preview-token";
const clearColor = { r: 0, g: 0, b: 0, a: 255 };
const fixtureTables = {
    NumericDisplay: "readings", StatusIndicator: "statuses", TextInput: "fields",
    SignalTrace: "signals", VulkanViewport: "viewports", Clock: "clock",
};

const state = {
    token: null,
    catalog: null,
    known: new Set(),
    recipe: null,
    history: null,
    base: null,
    baseDigest: null,
    commentLines: false,
    locales: [],
    locale: null,
    required: [],
    selected: null,
    edited: null,
    compile: null,   // the latest compile: { ok, ir, source, diagnostics }
    validIr: null,   // IR of the last document that compiled
    frame: null,     // the latest frame attempt: { ok, message }
    image: null,     // PNG of the last rendered frame
    revision: 0,
    reviewed: null,  // { key, writes, commentLoss, safety } for the exact request that was reviewed
    reviewTicket: 0, // bumped by every review and form change; a response for an older ticket is dropped
    loadGeneration: 0, // bumped by every screen load; a document response for an older load is dropped
    inflight: false,
    queued: false,
    timer: null,
    scale: 1,
};

// ---------------------------------------------------------------------------------------------------
// Small DOM and storage helpers
// ---------------------------------------------------------------------------------------------------

function el(tag, attributes = {}, children = []) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attributes)) {
        if (key === "class") node.className = value;
        else if (key === "text") node.textContent = value;
        else if (key.startsWith("on")) node.addEventListener(key.slice(2), value);
        else if (value === true) node.setAttribute(key, "");
        else if (value !== false && value !== null && value !== undefined) node.setAttribute(key, String(value));
    }
    for (const child of [].concat(children)) {
        if (child === null || child === undefined || child === "") continue;
        node.append(child instanceof Node ? child : document.createTextNode(String(child)));
    }
    return node;
}

function stored(key) {
    try { return sessionStorage.getItem(key); } catch { return null; }
}

function store(key, value) {
    try {
        if (value === null) sessionStorage.removeItem(key);
        else sessionStorage.setItem(key, value);
    } catch { /* storage unavailable: the value lives for this page only */ }
}

function setStatus(text) {
    $("status").textContent = text;
}

function confirmDialog(text) {
    const dialog = $("confirm");
    $("confirm-text").textContent = text;
    return new Promise((resolve) => {
        dialog.addEventListener("close", () => resolve(dialog.returnValue === "ok"), { once: true });
        dialog.returnValue = "";
        dialog.showModal();
    });
}

// ---------------------------------------------------------------------------------------------------
// Service calls
// ---------------------------------------------------------------------------------------------------

async function api(path, body) {
    for (let attempt = 0; ; ++attempt) {
        const response = await fetch(`/api/${path}`, {
            method: body === undefined ? "GET" : "POST",
            headers: body === undefined
                ? { Authorization: `Bearer ${state.token}` }
                : { Authorization: `Bearer ${state.token}`, "Content-Type": "application/json" },
            body: body === undefined ? undefined : JSON.stringify(body),
            cache: "no-store",
        });
        const text = await response.text();
        let json = null;
        try { json = text ? JSON.parse(text) : null; } catch { json = null; }
        if (response.status === 401) {
            signOut("The service refused the token.");
            throw new Error("unauthorized");
        }
        // One backend request runs at a time; this page never overlaps its own, so wait briefly.
        if (response.status === 503 && findings(json).some((f) => f.code === "PRV007") && attempt < 40) {
            await new Promise((resolve) => setTimeout(resolve, 250));
            continue;
        }
        return { status: response.status, body: json };
    }
}

function findings(body) {
    return body?.diagnostics?.findings ?? body?.findings ?? [];
}

function failureText(result) {
    const list = findings(result.body);
    return list.length ? list.map((f) => `${f.code}: ${f.message}`).join("; ") : `HTTP ${result.status}`;
}

// ---------------------------------------------------------------------------------------------------
// Connection and screen loading
// ---------------------------------------------------------------------------------------------------

function signOut(message) {
    state.token = null;
    store(tokenKey, null);
    $("workspace").hidden = true;
    $("login").hidden = false;
    $("login-error").textContent = message ?? "";
}

function fatal(message) {
    $("workspace").hidden = true;
    $("login").hidden = true;
    $("fatal").hidden = false;
    $("fatal").textContent = message;
}

async function connect() {
    const result = await api("catalog");
    if (result.status !== 200) return signOut(failureText(result));
    const catalog = result.body;
    // ADR-023 decision 2: refuse, rather than guess at, a schema this build was not written against.
    if (catalog.schemaVersion !== 1 || catalog.grammar?.schemaVersion !== 1 || catalog.irSchemaVersion !== 1
        || catalog.documentSchemaVersion !== 1) {
        return fatal("This Studio build does not recognise the service's catalog, IR or document schema version. Editing is disabled.");
    }
    state.catalog = catalog;
    state.known = new Set([
        ...catalog.grammar.diagnostics.map((d) => d.code),
        ...(catalog.grammar.retiredDiagnostics ?? []).map((d) => (typeof d === "string" ? d : d.code)),
        ...catalog.previewDiagnostics.map((d) => d.code),
    ]);
    $("login").hidden = true;
    $("workspace").hidden = false;
    renderPalette();
    const screens = await api("screens");
    if (screens.status !== 200) return setStatus(failureText(screens));
    const select = $("screen");
    select.replaceChildren(...screens.body.screens.map((s) => el("option", { value: s, text: s })));
    select.disabled = screens.body.screens.length === 0;
    if (screens.body.screens.length === 0) return setStatus("No screen recipes under recipes/screen/.");
    await loadScreen(select.value);
}

function dirty() {
    return state.history !== null && JSON.stringify(state.history.present) !== JSON.stringify(state.base);
}

async function loadScreen(recipe, force = false) {
    if (!force && dirty() && !(await confirmDialog("Discard the unproposed edits to this screen?"))) {
        $("screen").value = state.recipe;
        return;
    }
    Object.assign(state, {
        recipe, history: null, base: null, baseDigest: null, selected: null, edited: null, compile: null,
        validIr: null, frame: null, image: null, reviewed: null, required: [],
    });
    state.revision += 1;
    const generation = ++state.loadGeneration;
    setStatus(`Loading ${recipe}…`);
    const result = await api("document", { schemaVersion: 1, recipe });
    // Another screen was selected while this one loaded: installing this response would pair its
    // document and digest with the newer recipe.
    if (generation !== state.loadGeneration) return;
    if (result.status !== 200) {
        state.compile = { ok: false, ir: null, source: null, diagnostics: findings(result.body) };
        // An edit pending from the previous screen will not run without a history, so settle here.
        document.body.dataset.busy = "false";
        document.body.dataset.settled = String(state.revision);
        setStatus("The committed source cannot be edited until it parses cleanly.");
        render();
        return;
    }
    state.base = result.body.document;
    state.history = new History(clone(state.base));
    state.baseDigest = result.body.sourceDigest;
    state.commentLines = result.body.commentLines;
    $("fixture").value = stored(`mdux-preview-fixture:${recipe}`) ?? "";
    setStatus(`${recipe} loaded`);
    await refresh();
}

// ---------------------------------------------------------------------------------------------------
// The compile/frame loop
// ---------------------------------------------------------------------------------------------------

function commit(mutate, edited) {
    if (!state.history) return;
    const next = clone(state.history.present);
    if (mutate(next) === false) return;
    state.history.commit(next);
    afterChange(edited ?? null);
}

function afterChange(edited) {
    state.edited = edited;
    state.revision += 1;
    state.reviewed = null;
    if (state.selected && !findNode(state.history.present, state.selected)) state.selected = null;
    // Pending work counts as busy from the edit itself, not from when the debounced compile starts.
    document.body.dataset.busy = "true";
    render();
    clearTimeout(state.timer);
    state.timer = setTimeout(refresh, 120);
}

function undo() {
    if (state.history?.undo()) afterChange(null);
}

function redo() {
    if (state.history?.redo()) afterChange(null);
}

function parseFixture() {
    const text = $("fixture").value.trim();
    if (!text) return { ok: false, message: "no fixture supplied" };
    try {
        return { ok: true, value: JSON.parse(text) };
    } catch (error) {
        return { ok: false, message: `the fixture is not valid JSON (${error.message})` };
    }
}

/// Lists every binding the compiled screen needs, with null where the author must supply a value.
function fixtureSkeleton(required) {
    const fixture = {};
    for (const binding of required) {
        const table = fixtureTables[binding.kind];
        if (table === "clock") fixture.clock = null;
        else if (table) (fixture[table] ??= {})[binding.key] = null;
    }
    return JSON.stringify(fixture, null, 2);
}

async function refresh() {
    if (!state.history) return;
    if (state.inflight) {
        state.queued = true;
        return;
    }
    state.inflight = true;
    document.body.dataset.busy = "true";
    const revision = state.revision;
    try {
        const request = { schemaVersion: 1, recipe: state.recipe, document: state.history.present };
        const compiled = await api("compile", request);
        if (revision !== state.revision) return;
        const body = compiled.body ?? {};
        if (compiled.status !== 200) {
            state.compile = { ok: false, ir: body.ir ?? null, source: body.source ?? null, diagnostics: findings(body) };
            state.frame = null;
            if (compiled.status !== 422) setStatus(failureText(compiled));
            return;
        }
        state.compile = { ok: true, ir: body.ir, source: body.source, diagnostics: findings(body) };
        state.validIr = body.ir;
        state.required = body.requiredBindings;
        updateLocales(body.package);
        if (!$("fixture").value.trim()) $("fixture").value = fixtureSkeleton(body.requiredBindings);
        const fixture = parseFixture();
        if (!fixture.ok) {
            state.frame = { ok: false, message: fixture.message };
            return;
        }
        const framed = await api("frame", { ...request, locale: state.locale, fixture: fixture.value, clearColor });
        if (revision !== state.revision) return;
        if (framed.status === 200) {
            state.image = `data:image/png;base64,${framed.body.pngBase64}`;
            state.frame = { ok: true, backend: framed.body.backend, locale: framed.body.locale };
        } else {
            state.frame = { ok: false, message: failureText(framed) };
        }
    } catch (error) {
        if (error.message !== "unauthorized") setStatus(`Request failed: ${error.message}`);
    } finally {
        state.inflight = false;
        if (state.queued || revision !== state.revision) {
            state.queued = false;
            refresh();
        } else {
            document.body.dataset.busy = "false";
            document.body.dataset.settled = String(state.revision);
        }
        render();
    }
}

function updateLocales(pkg) {
    const locales = pkg.approvedTextPackages.map((p) => p.locale);
    state.locales = locales.length ? locales : [null];
    if (!state.locales.includes(state.locale)) state.locale = state.locales[0];
    const select = $("locale");
    select.replaceChildren(...state.locales.map((l) => el("option", { value: l ?? "", text: l ?? "(no text packages)" })));
    select.value = state.locale ?? "";
    select.disabled = state.locales.length < 2;
}

// ---------------------------------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------------------------------

function render() {
    const loaded = state.history !== null;
    const invalid = state.compile !== null && !state.compile.ok;
    const unknown = (state.compile?.diagnostics ?? []).filter((d) => !state.known.has(d.code));
    $("undo").disabled = !loaded || state.history.past.length === 0;
    $("redo").disabled = !loaded || state.history.future.length === 0;
    $("reload").disabled = state.recipe === null;
    $("propose").disabled = !loaded || invalid || !dirty() || unknown.length > 0 || state.compile === null;
    document.body.dataset.compile = state.compile === null ? "none" : invalid ? "invalid" : "valid";
    document.body.dataset.frame = state.frame?.ok ? "rendered" : "none";
    document.body.dataset.selected = state.selected ?? "";

    const banner = $("banner");
    banner.hidden = !invalid;
    if (invalid) {
        const count = state.compile.diagnostics.length;
        banner.textContent = state.validIr
            ? `Showing the last valid preview. The current edit is invalid (${count} diagnostic${count === 1 ? "" : "s"}): it is not rendered and cannot be proposed.`
            : `This screen does not compile (${count} diagnostic${count === 1 ? "" : "s"}).`;
    }
    renderStage(invalid);
    renderInspector();
    renderDiagnostics(unknown);
    renderSource();
    renderBindings();
}

function renderStage(invalid) {
    const stage = $("stage");
    const overlay = $("overlay");
    const image = $("frame");
    const ir = state.compile?.ok ? state.compile.ir : state.validIr;
    overlay.replaceChildren();
    if (!ir) {
        stage.style.width = "0px";
        stage.style.height = "0px";
        image.removeAttribute("src");
        $("frame-note").textContent = "";
        return;
    }
    const { width, height } = ir.surface;
    const available = Math.max(200, $("canvas-panel").clientWidth - 24);
    state.scale = Math.min(1, available / width);
    stage.style.width = `${width * state.scale}px`;
    stage.style.height = `${height * state.scale}px`;
    if (state.image && image.getAttribute("src") !== state.image) image.src = state.image;
    const stale = !invalid && state.frame !== null && !state.frame.ok;
    stage.classList.toggle("invalid", invalid);
    stage.classList.toggle("frame-stale", stale);
    $("frame-note").textContent = invalid
        ? "The image and boxes are from the last edit that compiled."
        : stale ? `No frame for this edit: ${state.frame.message}.${state.image ? " The image is from an earlier edit." : ""}`
            : state.frame?.ok ? `Rendered by ${state.frame.backend} from synthetic fixture values${state.frame.locale ? ` in ${state.frame.locale}` : ""}; not device readings.` : "";

    const document_ = state.history?.present;
    for (const node of ir.nodes) {
        const id = node.synthetic ? node.fields.find((f) => f.name === "id")?.spelling : node.id;
        const found = document_ && id ? findNode(document_, id) : null;
        const position = found ? getField(found.node, "position") : null;
        const classes = ["box"];
        if (node.synthetic) classes.push("synthetic");
        if (position?.kind === "Point") classes.push("positioned");
        if (found && isSafetyRelevant(found.node)) classes.push("safety");
        if (id && id === state.selected) classes.push("selected");
        const box = el("div", { class: classes.join(" "), "data-id": id ?? "", title: `${node.component} ${id ?? ""}` });
        place(box, node.bounds);
        box.addEventListener("pointerdown", (event) => startDrag(event, id, box));
        const width = found ? getField(found.node, "width") : null;
        const heightField = found ? getField(found.node, "height") : null;
        if (id === state.selected && width?.kind === "Size" && !width.fill && heightField?.kind === "Size" && !heightField.fill) {
            const handle = el("div", { class: "handle", title: "Resize" });
            handle.addEventListener("pointerdown", (event) => startResize(event, id, box));
            box.append(handle);
        }
        overlay.append(box);
    }
    if (invalid && state.edited && document_) {
        const found = findNode(document_, state.edited);
        const previous = ir.nodes.find((n) => n.id === state.edited);
        if (found) {
            const position = getField(found.node, "position");
            const width = getField(found.node, "width");
            const heightField = getField(found.node, "height");
            const bounds = {
                x: position?.kind === "Point" ? position.x : previous?.bounds.x ?? 0,
                y: position?.kind === "Point" ? position.y : previous?.bounds.y ?? 0,
                width: width?.kind === "Size" && !width.fill ? width.pixels : previous?.bounds.width ?? 0,
                height: heightField?.kind === "Size" && !heightField.fill ? heightField.pixels : previous?.bounds.height ?? 0,
            };
            const proposed = el("div", { class: "proposed", title: "Proposed geometry of the invalid edit" });
            place(proposed, bounds);
            overlay.append(proposed);
        }
    }
}

function place(element, bounds) {
    element.style.left = `${bounds.x * state.scale}px`;
    element.style.top = `${bounds.y * state.scale}px`;
    element.style.width = `${bounds.width * state.scale}px`;
    element.style.height = `${bounds.height * state.scale}px`;
}

function componentSpec(name) {
    return state.catalog.grammar.components.find((c) => c.name === name) ?? null;
}

function renderInspector() {
    const root = $("inspector");
    root.replaceChildren();
    if (!state.history) {
        root.append(el("p", { class: "muted", text: "No editable screen is loaded." }));
        return;
    }
    const document_ = state.history.present;
    if (!state.selected) {
        root.append(
            el("h3", {}, ["Screen ", el("code", { text: document_.name })]),
            el("p", { class: "muted", text: `Surface ${document_.surface ? `${document_.surface.x}×${document_.surface.y}px` : "from the recipe"}; layout ${document_.layoutKind || "none"}.` }),
            el("p", { class: "muted", text: "Select a box on the preview, or add a component from the palette." }),
        );
        return;
    }
    const found = findNode(document_, state.selected);
    if (!found) return;
    const node = found.node;
    const spec = componentSpec(node.component);
    root.append(el("h3", {}, [node.component, " ", el("code", { text: state.selected })]));
    if (isSafetyRelevant(node)) {
        root.append(el("p", { class: "safety-note", text: "This node carries safety metadata. Any change to its annotations or requirement is listed and must be acknowledged before it can be proposed." }));
    }
    for (const annotation of node.annotations) {
        root.append(el("code", { class: "annotation", text: annotationText(annotation) }));
    }
    const rows = [];
    if (!spec) {
        root.append(el("p", { class: "muted", text: "This component is not in the compiler's catalog; its fields are shown read-only and preserved." }));
    }
    for (const field of spec?.fields ?? []) rows.push(fieldRow(node, field));
    for (const field of node.fields) {
        if (!spec?.fields.some((f) => f.name === field.name)) {
            rows.push(el("div", { class: "field readonly" }, [
                el("label", { text: field.name }),
                el("input", { value: editText(field.value), readonly: true, title: "Not in the catalog for this component; preserved unchanged" }),
            ]));
        }
    }
    root.append(el("div", { class: "fields" }, rows));
    const actions = el("div", { class: "actions" });
    if (spec?.fields.some((f) => f.name === "position") && !getField(node, "position")) {
        const bounds = state.validIr?.nodes.find((n) => n.id === state.selected)?.bounds;
        if (bounds) {
            actions.append(el("button", {
                type: "button", id: "pin-position", text: "Pin at current position",
                onclick: () => commit((d) => setField(findNode(d, state.selected).node, "position", point(bounds.x, bounds.y)), state.selected),
            }));
        }
    }
    actions.append(el("button", { type: "button", id: "delete-node", text: "Delete node", onclick: deleteSelected }));
    root.append(actions);
}

function fieldRow(node, spec) {
    const value = getField(node, spec.name);
    const current = value ? editText(value) : "";
    const inputId = `field-${spec.name}`;
    const grammar = state.catalog.grammar;
    const choices = spec.domain === "ColorToken" ? grammar.themeTokens
        : spec.domain === "ClockFormatName" ? grammar.namedValues.format
            : spec.domain === "SystemEventName" ? grammar.namedValues.on_press : null;
    let input;
    if (choices) {
        const options = [...choices];
        if (current && !options.includes(current)) options.push(current);
        input = el("select", { id: inputId, "data-field": spec.name }, [
            el("option", { value: "", text: spec.required ? "(required)" : "(none)" }),
            ...options.map((c) => el("option", { value: c, text: c })),
        ]);
        input.value = current;
    } else {
        const form = state.catalog.grammar.fieldDomains.find((d) => d.name === spec.domain)?.form ?? spec.domain;
        input = el("input", { id: inputId, "data-field": spec.name, value: current, placeholder: form, spellcheck: "false" });
    }
    const error = el("span", { class: "field-error" });
    const apply = () => {
        // Enter re-renders the inspector; a late change event from the detached input must not
        // commit a second copy of the same edit.
        if (!input.isConnected || !state.history) return;
        const text = input.value.trim();
        input.classList.remove("invalid");
        error.textContent = "";
        const selected = state.selected;
        const live = findNode(state.history.present, selected);
        const now = live ? getField(live.node, spec.name) : null;
        if (!live || text === (now ? editText(now) : "")) return;
        if (text === "") {
            if (spec.required) {
                // Guard: a field the compiler requires, such as a requirement id, is never removed.
                error.textContent = "required by the compiler; set a value instead of removing it";
                input.value = current;
                return;
            }
            commit((d) => removeField(findNode(d, selected).node, spec.name), selected);
            return;
        }
        let parsed;
        try {
            parsed = parseValue(spec.domain, input.value);
        } catch (failure) {
            input.classList.add("invalid");
            error.textContent = failure.message;
            return;
        }
        if (spec.name === "id" && parsed.text !== selected && findNode(state.history.present, parsed.text)) {
            input.classList.add("invalid");
            error.textContent = "another node already uses this id";
            return;
        }
        if (spec.name === "id") state.selected = parsed.text;
        commit((d) => setField(findNode(d, selected).node, spec.name, parsed), state.selected);
    };
    input.addEventListener("change", apply);
    input.addEventListener("keydown", (event) => {
        if (event.key === "Enter") {
            event.preventDefault();
            apply();
        }
    });
    const missing = spec.required && !value;
    return el("div", { class: `field${missing ? " missing" : ""}` }, [
        el("label", { for: inputId, text: `${spec.name}${spec.required ? " *" : ""}`, title: spec.domain }),
        input,
        error,
    ]);
}

function renderDiagnostics(unknown) {
    const list = $("diagnostics");
    list.replaceChildren();
    const items = state.compile?.diagnostics ?? [];
    for (const d of items) {
        const unrecognised = unknown.includes(d);
        const item = el("li", { class: `diagnostic ${d.severity}${unrecognised ? " unknown" : ""}`, "data-code": d.code }, [
            el("code", { text: d.code }), " ",
            d.line ? `${d.line}:${d.column} ` : "",
            d.message,
            d.fixHint ? el("small", { class: "muted", text: ` (${d.fixHint})` }) : null,
            unrecognised ? el("strong", { text: " Unrecognised code: proposals are disabled until this Studio is updated." }) : null,
        ]);
        if (d.line) item.addEventListener("click", () => highlightLine(d.line));
        list.append(item);
    }
    if (items.length === 0 && state.compile?.ok) list.append(el("li", { class: "muted", text: "No diagnostics." }));
}

function renderSource() {
    const source = state.compile?.source ?? "";
    $("source").replaceChildren(...source.split("\n").map((line, index) =>
        el("span", { class: "line", "data-line": index + 1 }, [`${String(index + 1).padStart(4)}  ${line}\n`])));
}

function highlightLine(line) {
    for (const span of $("source").querySelectorAll(".line")) span.classList.toggle("hit", Number(span.dataset.line) === line);
    $("source").closest("details").open = true;
    $("source").querySelector(".hit")?.scrollIntoView({ block: "center" });
}

function renderBindings() {
    $("bindings").replaceChildren(...state.required.map((b) =>
        el("li", {}, [el("code", { text: fixtureTables[b.kind] ?? b.kind }), ` ${b.key} (${b.kind} ${b.nodeId})`])));
}

function renderPalette() {
    $("palette").replaceChildren(...state.catalog.grammar.components.map((spec) =>
        el("li", {}, [el("button", { type: "button", "data-component": spec.name, text: spec.name, onclick: () => addNode(spec) })])));
}

// ---------------------------------------------------------------------------------------------------
// Editing interactions
// ---------------------------------------------------------------------------------------------------

function select(id) {
    state.selected = id;
    render();
}

function addNode(spec) {
    if (!state.history) return;
    const present = state.history.present;
    const target = state.selected ? findNode(present, state.selected) : null;
    const intoRow = spec.name !== "Row" && target?.node.component === "Row" ? state.selected : null;
    const node = newNode(spec, present);
    state.selected = nodeId(node);
    commit((d) => {
        (intoRow ? findNode(d, intoRow).node.children : d.nodes).push(node);
    }, state.selected);
}

async function deleteSelected() {
    const id = state.selected;
    const found = id && state.history ? findNode(state.history.present, id) : null;
    if (!found) return;
    if (carriesSafetyMetadata(found.node)
        && !(await confirmDialog(`${id} carries safety annotations or requirement ids. Deleting it removes them; the proposal will list the removal for acknowledgement. Delete?`))) {
        return;
    }
    state.selected = null;
    commit((d) => {
        const target = findNode(d, id);
        target.siblings.splice(target.index, 1);
    }, null);
}

function startDrag(event, id, box) {
    if (event.button !== 0 || !state.history || !id) return;
    event.preventDefault();
    event.stopPropagation();
    if (state.selected !== id) {
        state.selected = id;
        for (const other of $("overlay").querySelectorAll(".box")) other.classList.toggle("selected", other === box);
        renderInspector();
    }
    const found = findNode(state.history.present, id);
    const position = found ? getField(found.node, "position") : null;
    const startX = event.clientX;
    const startY = event.clientY;
    const delta = (e) => [Math.round((e.clientX - startX) / state.scale), Math.round((e.clientY - startY) / state.scale)];
    const move = (e) => {
        if (position?.kind !== "Point") return;
        const [dx, dy] = delta(e);
        box.style.transform = `translate(${dx * state.scale}px, ${dy * state.scale}px)`;
    };
    const up = (e) => {
        window.removeEventListener("pointermove", move);
        window.removeEventListener("pointerup", up);
        box.style.transform = "";
        const [dx, dy] = delta(e);
        if (position?.kind === "Point" && (dx !== 0 || dy !== 0)) {
            commit((d) => setField(findNode(d, id).node, "position", point(Math.max(0, position.x + dx), Math.max(0, position.y + dy))), id);
        } else {
            render();
        }
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
}

function startResize(event, id, box) {
    if (event.button !== 0) return;
    event.preventDefault();
    event.stopPropagation();
    const node = findNode(state.history.present, id).node;
    const width = getField(node, "width").pixels;
    const height = getField(node, "height").pixels;
    const startX = event.clientX;
    const startY = event.clientY;
    const size = (e) => [Math.max(1, width + Math.round((e.clientX - startX) / state.scale)), Math.max(1, height + Math.round((e.clientY - startY) / state.scale))];
    const move = (e) => {
        const [w, h] = size(e);
        box.style.width = `${w * state.scale}px`;
        box.style.height = `${h * state.scale}px`;
    };
    const up = (e) => {
        window.removeEventListener("pointermove", move);
        window.removeEventListener("pointerup", up);
        const [w, h] = size(e);
        if (w !== width || h !== height) {
            commit((d) => {
                const target = findNode(d, id).node;
                setField(target, "width", px(w));
                setField(target, "height", px(h));
            }, id);
        } else {
            render();
        }
    };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerup", up);
}

function onKey(event) {
    const target = event.target;
    if (target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement || target instanceof HTMLSelectElement) return;
    if (document.querySelector("dialog[open]")) return;
    const modifier = event.metaKey || event.ctrlKey;
    const key = event.key.toLowerCase();
    if (modifier && key === "z") {
        event.preventDefault();
        if (event.shiftKey) redo();
        else undo();
    } else if (modifier && key === "y") {
        event.preventDefault();
        redo();
    } else if (event.key === "Escape") {
        select(null);
    } else if (state.selected && (event.key === "Delete" || event.key === "Backspace")) {
        event.preventDefault();
        deleteSelected();
    } else if (state.selected && event.key.startsWith("Arrow")) {
        const found = findNode(state.history.present, state.selected);
        const position = found ? getField(found.node, "position") : null;
        if (position?.kind !== "Point") return;
        event.preventDefault();
        const step = event.shiftKey ? 10 : 1;
        const dx = event.key === "ArrowLeft" ? -step : event.key === "ArrowRight" ? step : 0;
        const dy = event.key === "ArrowUp" ? -step : event.key === "ArrowDown" ? step : 0;
        const id = state.selected;
        commit((d) => setField(findNode(d, id).node, "position", point(Math.max(0, position.x + dx), Math.max(0, position.y + dy))), id);
    }
}

// ---------------------------------------------------------------------------------------------------
// Proposals
// ---------------------------------------------------------------------------------------------------

function slugify(text) {
    return text.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "").slice(0, 60).replace(/-+$/g, "");
}

function proposalRequest(dryRun) {
    return {
        schemaVersion: 1,
        recipe: state.recipe,
        document: state.history.present,
        baseSourceDigest: state.baseDigest,
        issue: Number($("proposal-issue").value),
        slug: $("proposal-slug").value.trim(),
        base: $("proposal-base").value.trim(),
        title: $("proposal-title").value.trim(),
        description: $("proposal-description").value,
        acknowledgeCommentLoss: $("ack-comments").checked,
        acknowledgeSafetyChanges: $("ack-safety").checked,
        dryRun,
    };
}

/// Everything a review depends on: the recipe, document, digest and every form field. The
/// acknowledgements gate submission of a review rather than changing what was reviewed.
function snapshotKey(request) {
    return JSON.stringify({ ...request, acknowledgeCommentLoss: undefined, acknowledgeSafetyChanges: undefined, dryRun: undefined });
}

function reviewMatchesForm() {
    return state.reviewed !== null && state.history !== null && state.reviewed.key === snapshotKey(proposalRequest(false));
}

function updateSubmit() {
    const review = state.reviewed;
    $("proposal-submit").disabled = !reviewMatchesForm() || !review.writes
        || (review.commentLoss && !$("ack-comments").checked) || (review.safety && !$("ack-safety").checked);
}

/// Any change to the proposal form invalidates the displayed review and any review still in flight.
function invalidateReview() {
    const hadReview = state.reviewed !== null || $("proposal-review").childElementCount > 0
        || $("proposal-result").textContent === "Reviewing…";
    state.reviewTicket += 1;
    state.reviewed = null;
    $("proposal-review").replaceChildren();
    $("ack-comments-row").hidden = true;
    $("ack-safety-row").hidden = true;
    if (hadReview) resultMessage("The proposal changed; review it again before submitting.", false);
    updateSubmit();
}

function resultMessage(text, error) {
    const result = $("proposal-result");
    result.className = error ? "error" : "";
    result.replaceChildren(text);
    return result;
}

function openProposal() {
    const writes = state.catalog.proposals;
    $("proposal-writes").textContent = !writes.enabled
        ? "Proposal writes are disabled on this service. You can review a proposal but not submit it."
        : writes.pullRequests
            ? "Submitting pushes a new branch and opens a draft pull request. Nothing is merged."
            : "Submitting pushes a new branch; open the pull request from it. Nothing is merged.";
    state.reviewTicket += 1;
    state.reviewed = null;
    $("proposal-review").replaceChildren();
    $("ack-comments-row").hidden = true;
    $("ack-safety-row").hidden = true;
    $("ack-comments").checked = false;
    $("ack-safety").checked = false;
    resultMessage("", false);
    updateSubmit();
    $("proposal").showModal();
}

async function reviewProposal() {
    invalidateReview();
    const request = proposalRequest(true);
    const key = snapshotKey(request);
    const ticket = state.reviewTicket;
    resultMessage("Reviewing…", false);
    const result = await api("proposals", request);
    // A form change or a newer review superseded this one; its result must not enable Submit.
    if (ticket !== state.reviewTicket || state.history === null || key !== snapshotKey(proposalRequest(true))) return;
    if (result.status !== 200) {
        updateSubmit();
        resultMessage(failureText(result), true);
        return;
    }
    const body = result.body;
    state.reviewed = { key, writes: body.writesEnabled, commentLoss: body.commentLoss, safety: body.safetyChanges.length > 0 };
    const review = $("proposal-review");
    review.replaceChildren(
        el("p", {}, ["Branch ", el("code", { text: `${body.branchPrefix}-<commit>` }), " from ", el("code", { text: body.base }), "."]),
        body.safetyChanges.length
            ? el("div", {}, [
                el("p", { class: "safety-note", text: `${body.safetyChanges.length} safety metadata change(s):` }),
                el("pre", { id: "safety-changes", text: body.safetyChanges.map((c) => `${c.nodeId}: ${c.change}\n  before: ${JSON.stringify(c.before)}\n  after:  ${JSON.stringify(c.after)}`).join("\n") }),
            ])
            : el("p", { class: "muted", text: "No safety annotation or requirement changes." }),
        el("details", {}, [el("summary", { text: "Proposed canonical source" }), el("pre", { text: body.source })]),
    );
    $("ack-comments-row").hidden = !body.commentLoss;
    $("ack-safety-row").hidden = body.safetyChanges.length === 0;
    resultMessage(body.writesEnabled ? "Review complete. Submit when every acknowledgement is given." : "Review complete. This service cannot submit proposals.", false);
    updateSubmit();
}

async function submitProposal() {
    // Submit exactly what was reviewed: re-check at click time rather than trusting the button state.
    const request = proposalRequest(false);
    if (!state.reviewed || state.reviewed.key !== snapshotKey(request)) {
        invalidateReview();
        return;
    }
    $("proposal-submit").disabled = true;
    resultMessage("Submitting…", false);
    const result = await api("proposals", request);
    if (result.status === 201) {
        const body = result.body;
        const message = resultMessage("", false);
        message.append(`Pushed branch `, el("code", { id: "proposal-branch", text: body.branch }), ` at ${body.commit.slice(0, 12)}. `);
        if (body.pullRequestUrl) message.append(el("a", { href: body.pullRequestUrl, target: "_blank", rel: "noopener noreferrer", text: body.pullRequestUrl }));
        if (body.warning) message.append(el("span", { class: "safety-note", text: ` ${body.warning}` }));
        state.reviewed = null;
        setStatus(`Proposed ${body.branch}`);
        return;
    }
    const stale = findings(result.body).some((f) => f.code === "PRV009");
    const message = resultMessage(failureText(result), true);
    if (stale) {
        message.append(" ", el("button", {
            type: "button", id: "proposal-reload", text: "Reload screen",
            onclick: () => { $("proposal").close(); loadScreen(state.recipe, true); },
        }));
    }
    updateSubmit();
}

// ---------------------------------------------------------------------------------------------------
// Start-up
// ---------------------------------------------------------------------------------------------------

function bind() {
    $("login-form").addEventListener("submit", (event) => {
        event.preventDefault();
        state.token = $("login-token").value.trim();
        store(tokenKey, state.token);
        connect().catch((error) => setStatus(error.message));
    });
    $("screen").addEventListener("change", () => loadScreen($("screen").value));
    $("locale").addEventListener("change", () => {
        state.locale = $("locale").value || null;
        state.revision += 1;
        refresh();
    });
    $("undo").addEventListener("click", undo);
    $("redo").addEventListener("click", redo);
    $("reload").addEventListener("click", () => loadScreen(state.recipe));
    $("propose").addEventListener("click", openProposal);
    $("apply-fixture").addEventListener("click", () => {
        store(`mdux-preview-fixture:${state.recipe}`, $("fixture").value);
        state.revision += 1;
        refresh();
    });
    $("overlay").addEventListener("pointerdown", (event) => {
        if (event.target === $("overlay")) select(null);
    });
    $("proposal-review-button").addEventListener("click", reviewProposal);
    $("proposal-submit").addEventListener("click", submitProposal);
    let slugEdited = false;
    $("proposal-slug").addEventListener("input", () => { slugEdited = true; });
    $("proposal-title").addEventListener("input", () => {
        if (!slugEdited) $("proposal-slug").value = slugify($("proposal-title").value);
    });
    for (const id of ["proposal-issue", "proposal-base", "proposal-title", "proposal-slug", "proposal-description"]) {
        $(id).addEventListener("input", invalidateReview);
    }
    $("ack-comments").addEventListener("change", updateSubmit);
    $("ack-safety").addEventListener("change", updateSubmit);
    document.addEventListener("keydown", onKey);
    window.addEventListener("resize", () => renderStage(state.compile !== null && !state.compile.ok));
}

async function start() {
    bind();
    const fragment = new URLSearchParams(location.hash.slice(1));
    if (fragment.get("token")) {
        state.token = fragment.get("token");
        store(tokenKey, state.token);
        // The fragment never reaches the server; drop it from the address bar and history too.
        history.replaceState(null, "", location.pathname);
    } else {
        state.token = stored(tokenKey);
    }
    if (!state.token) {
        signOut("");
        return;
    }
    await connect();
}

start().catch((error) => setStatus(`Studio failed to start: ${error.message}`));
