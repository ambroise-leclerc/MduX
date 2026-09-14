// Pure helpers over mdux-preview's screen document (documentSchemaVersion 1, ADR-025).
//
// The document is the compiler's unresolved AST: nodes with a component, annotations, fields and
// children. Nothing here decides whether a screen is valid; the service's compile is authoritative.
// These helpers only keep edits well-formed enough to reach it.

export const documentSchemaVersion = 1;

const identifier = /^[A-Za-z_][A-Za-z0-9_-]*$/;
const colourToken = /^[A-Za-z_][A-Za-z0-9_-]*(\.[A-Za-z_][A-Za-z0-9_-]*)+$/;
const maxMagnitude = 2147483647;

export function clone(value) {
    return structuredClone(value);
}

/// Visits every node depth-first with the array that holds it.
export function eachNode(document, visit) {
    const walk = (nodes, parent) => {
        for (let index = 0; index < nodes.length; ++index) {
            visit(nodes[index], nodes, index, parent);
            walk(nodes[index].children, nodes[index]);
        }
    };
    walk(document.nodes, null);
}

export function nodeId(node) {
    const field = node.fields.find((f) => f.name === "id");
    return field && field.value.kind === "Identifier" ? field.value.text : null;
}

export function findNode(document, id) {
    let found = null;
    eachNode(document, (node, siblings, index, parent) => {
        if (found === null && nodeId(node) === id) {
            found = { node, siblings, index, parent };
        }
    });
    return found;
}

export function getField(node, name) {
    const field = node.fields.find((f) => f.name === name);
    return field ? field.value : null;
}

/// Replaces a field in place, keeping its authored order, or appends it (ADR-023 decision 4).
export function setField(node, name, value) {
    const field = node.fields.find((f) => f.name === name);
    if (field) {
        field.value = value;
    } else {
        node.fields.push({ name, value });
    }
}

export function removeField(node, name) {
    node.fields = node.fields.filter((f) => f.name !== name);
}

export const px = (pixels) => ({ kind: "Size", fill: false, pixels });
export const point = (x, y) => ({ kind: "Point", x, y });

export function isSafetyRelevant(node) {
    return node.annotations.length > 0 || node.fields.some((f) => f.name === "requirement");
}

/// True when the node or anything beneath it carries safety metadata.
export function carriesSafetyMetadata(node) {
    return isSafetyRelevant(node) || node.children.some(carriesSafetyMetadata);
}

/// `.medui` spelling of a value, for read-only display.
export function valueText(value) {
    switch (value.kind) {
        case "Size": return value.fill ? "Fill" : `${value.pixels}px`;
        case "Point": return `${value.x}px, ${value.y}px`;
        case "String": return JSON.stringify(value.text);
        case "TextKey": return `t(${JSON.stringify(value.text)})`;
        case "ImageRef": return `img(${JSON.stringify(value.text)})`;
        case "Number": return String(value.number);
        case "List": return `[${value.items.map(valueText).join(", ")}]`;
        default: return value.text;
    }
}

export function annotationText(annotation) {
    const args = annotation.arguments.map((a) => `${a.name}: ${valueText(a.value)}`).join(", ");
    return `@${annotation.name}${args ? `(${args})` : ""}`;
}

/// The text an inspector input shows for a value; the inverse of `parseValue` for its domain.
export function editText(value) {
    switch (value.kind) {
        case "Size": return value.fill ? "Fill" : `${value.pixels}px`;
        case "Point": return `${value.x}px, ${value.y}px`;
        case "Number": return String(value.number);
        case "List": return value.items.map(editText).join(", ");
        default: return value.text;
    }
}

function magnitude(text, what) {
    const n = Number(text);
    if (!Number.isInteger(n) || n < 0 || n > maxMagnitude) {
        throw new Error(`expected ${what}`);
    }
    return n;
}

function unwrap(text, prefix) {
    const match = new RegExp(`^${prefix}\\(\\s*"(.*)"\\s*\\)$`).exec(text);
    return match ? match[1] : text;
}

/// Parses inspector text into a document value for one of the catalog's field domains.
export function parseValue(domain, raw) {
    const text = raw.trim();
    switch (domain) {
        case "Size": {
            if (/^fill$/i.test(text)) return { kind: "Size", fill: true, pixels: 0 };
            const match = /^(\d+)\s*(px)?$/.exec(text);
            if (!match) throw new Error("expected Npx or Fill");
            return px(magnitude(match[1], "a pixel count"));
        }
        case "Point": {
            const match = /^(\d+)\s*(?:px)?\s*,\s*(\d+)\s*(?:px)?$/.exec(text);
            if (!match) throw new Error("expected Xpx, Ypx");
            return point(magnitude(match[1], "a coordinate"), magnitude(match[2], "a coordinate"));
        }
        case "String":
            if (/[\u0000-\u0008\u000b-\u001f\u007f]/.test(raw)) throw new Error("control characters are not allowed");
            return { kind: "String", text: raw };
        case "TextKey": {
            const key = unwrap(text, "t");
            if (!key) throw new Error("expected a text key");
            return { kind: "TextKey", text: key };
        }
        case "ImageRef": {
            const id = unwrap(text, "img");
            if (!id) throw new Error("expected an image id");
            return { kind: "ImageRef", text: id };
        }
        case "ColorToken":
            if (!colourToken.test(text)) throw new Error("expected Theme.Colors.<Token>");
            return { kind: "ColorToken", text };
        case "Identifier":
        case "ClockFormatName":
        case "SystemEventName":
            if (!identifier.test(text)) throw new Error("expected an unquoted name");
            return { kind: "Identifier", text };
        case "Number":
            if (!/^\d+$/.test(text)) throw new Error("expected a positive integer");
            return { kind: "Number", number: magnitude(text, "a positive integer") };
        case "TextKeyList":
        case "ColorTokenList": {
            const element = domain === "TextKeyList" ? "TextKey" : "ColorToken";
            const parts = text.split(",").map((p) => p.trim()).filter((p) => p.length > 0);
            if (parts.length === 0) throw new Error("expected a non-empty list");
            return { kind: "List", items: parts.map((p) => parseValue(element, p)) };
        }
        default:
            throw new Error(`the catalog domain ${domain} is not editable in this Studio build`);
    }
}

export function uniqueId(document, component) {
    const stem = component.replace(/([a-z0-9])([A-Z])/g, "$1-$2").toLowerCase();
    for (let n = 1; ; ++n) {
        const id = `${stem}-${n}`;
        if (findNode(document, id) === null) return id;
    }
}

/// A new node carrying only an id and geometry. Content fields such as text keys, colours and
/// requirement ids are never invented: the compiler reports them missing until the author sets them.
export function newNode(spec, document) {
    const node = { component: spec.name, annotations: [], fields: [], children: [] };
    const declares = (name) => spec.fields.some((f) => f.name === name);
    setField(node, "id", { kind: "Identifier", text: uniqueId(document, spec.name) });
    if (declares("width")) setField(node, "width", px(160));
    if (declares("height")) setField(node, "height", px(spec.name === "Row" ? 72 : 48));
    if (declares("position")) setField(node, "position", point(16, 16));
    return node;
}

/// Bounded undo/redo over whole documents. Documents are small, so snapshots are cheap and exact.
export class History {
    constructor(present, cap = 100) {
        this.past = [];
        this.future = [];
        this.present = present;
        this.cap = cap;
    }
    commit(next) {
        this.past.push(this.present);
        if (this.past.length > this.cap) this.past.shift();
        this.future = [];
        this.present = next;
    }
    undo() {
        if (this.past.length === 0) return false;
        this.future.push(this.present);
        this.present = this.past.pop();
        return true;
    }
    redo() {
        if (this.future.length === 0) return false;
        this.past.push(this.present);
        this.present = this.future.pop();
        return true;
    }
}
