const SVG_NS = "http://www.w3.org/2000/svg";

function defaultTextStyle() {
  return {
    fontFamily: "Arial",
    fontSizeMm: 6,
    align: "left",
    lineHeight: 1.2,
    bold: false,
    italic: false,
    fillMode: "solid",
    hatchSpacingMm: 0.8,
    hatchAngleDeg: 45
  };
}

function parseViewBox(svgElement) {
  const raw = (svgElement.getAttribute("viewBox") || "").trim();
  if (!raw) {
    return null;
  }
  const parts = raw.split(/[,\s]+/).map(Number);
  if (parts.length !== 4 || parts.some((value) => !Number.isFinite(value))) {
    return null;
  }
  return {
    minX: parts[0],
    minY: parts[1],
    width: parts[2],
    height: parts[3]
  };
}

function parseLength(rawValue, fallback) {
  const value = Number.parseFloat(String(rawValue || "").trim());
  return Number.isFinite(value) && value > 0 ? value : fallback;
}

function resolveSourceViewport(svgElement) {
  const viewBox = parseViewBox(svgElement);
  if (viewBox && viewBox.width > 0 && viewBox.height > 0) {
    return viewBox;
  }

  const width = parseLength(svgElement.getAttribute("width"), 100);
  const height = parseLength(svgElement.getAttribute("height"), 100);
  return {
    minX: 0,
    minY: 0,
    width,
    height
  };
}

function createMeasureRoot(viewport) {
  const svg = document.createElementNS(SVG_NS, "svg");
  svg.setAttribute("width", String(viewport.width));
  svg.setAttribute("height", String(viewport.height));
  svg.setAttribute(
    "viewBox",
    `${viewport.minX} ${viewport.minY} ${viewport.width} ${viewport.height}`
  );
  svg.style.position = "fixed";
  svg.style.left = "-100000px";
  svg.style.top = "-100000px";
  svg.style.visibility = "hidden";
  svg.style.pointerEvents = "none";
  svg.style.opacity = "0";
  document.body.appendChild(svg);
  return svg;
}

function cloneSvgElement(source) {
  const clone = document.createElementNS(SVG_NS, source.tagName);
  for (const attribute of source.attributes) {
    clone.setAttribute(attribute.name, attribute.value);
  }
  return clone;
}

function extractAppearance(element) {
  const fill = (element.getAttribute("fill") || "").trim().toLowerCase();
  const stroke = (element.getAttribute("stroke") || "").trim().toLowerCase();
  const strokeWidthRaw = element.getAttribute("stroke-width");
  return {
    fill,
    stroke,
    strokeWidthRaw
  };
}

function hasVisibleFill(appearance) {
  return !!appearance.fill && appearance.fill !== "none" && appearance.fill !== "transparent";
}

function hasVisibleStroke(appearance) {
  return !!appearance.stroke && appearance.stroke !== "none" && appearance.stroke !== "transparent";
}

function operationHint(appearance) {
  if (hasVisibleFill(appearance)) {
    return "engrave_fill";
  }
  if (hasVisibleStroke(appearance)) {
    return "engrave_line";
  }
  return "engrave_line";
}

export function parseSvgObjects(svgText, scene) {
  const parser = new DOMParser();
  const documentNode = parser.parseFromString(svgText, "image/svg+xml");

  const parseError = documentNode.querySelector("parsererror");
  if (parseError) {
    throw new Error("Invalid SVG file.");
  }

  const svgElement = documentNode.documentElement;
  if (!svgElement || svgElement.tagName.toLowerCase() !== "svg") {
    throw new Error("Root <svg> element is missing.");
  }

  const viewport = resolveSourceViewport(svgElement);
  const scaleX = scene.widthMm / viewport.width;
  const scaleY = scene.heightMm / viewport.height;

  const measureRoot = createMeasureRoot(viewport);
  const elementNodes = documentNode.querySelectorAll(
    "path,rect,circle,ellipse,line,polyline,polygon,text"
  );

  const objects = [];
  let index = 0;

  for (const node of elementNodes) {
    const cloned = cloneSvgElement(node);
    measureRoot.appendChild(cloned);
    let bbox = null;
    try {
      bbox = cloned.getBBox();
    } catch {
      bbox = null;
    }
    measureRoot.removeChild(cloned);

    if (!bbox || bbox.width <= 0 || bbox.height <= 0) {
      continue;
    }

    const appearance = extractAppearance(node);
    const tagName = node.tagName.toLowerCase();
    const sourceText = node.textContent || "";
    const xMm = (bbox.x - viewport.minX) * scaleX;
    const yMm = (bbox.y - viewport.minY) * scaleY;
    const wMm = bbox.width * scaleX;
    const hMm = bbox.height * scaleY;
    const strokeWidthSource = parseLength(appearance.strokeWidthRaw, 0);

    objects.push({
      id: `svg-${Date.now()}-${index}`,
      kind: tagName === "text" ? "text" : "path",
      name: tagName === "text" ? (sourceText.trim().slice(0, 48) || `text ${index + 1}`) : `${tagName} ${index + 1}`,
      bbox: {
        x: Number(xMm.toFixed(3)),
        y: Number(yMm.toFixed(3)),
        w: Number(wMm.toFixed(3)),
        h: Number(hMm.toFixed(3))
      },
      source: {
        tag: tagName,
        d: node.getAttribute("d") || "",
        points: node.getAttribute("points") || "",
        text: sourceText
      },
      text: tagName === "text" ? sourceText : undefined,
      textStyle: tagName === "text" ? defaultTextStyle() : undefined,
      appearance: {
        fill: appearance.fill || "none",
        stroke: appearance.stroke || "none",
        strokeWidthMm: Number((strokeWidthSource * scaleX).toFixed(3))
      },
      operationHint: operationHint(appearance)
    });
    index += 1;
  }

  document.body.removeChild(measureRoot);

  return {
    objects,
    sourceViewport: viewport
  };
}
