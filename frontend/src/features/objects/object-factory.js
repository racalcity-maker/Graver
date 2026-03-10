export const NEW_OBJECT_TYPES = [
  { value: "text", label: "Text", operationType: "engrave_fill" },
  { value: "image", label: "Image", operationType: "image_raster" },
  { value: "cut", label: "Cut contour", operationType: "cut_line" },
  { value: "line", label: "Line", operationType: "engrave_line" },
  { value: "primitive", label: "Primitive", operationType: "engrave_fill" },
  { value: "frame", label: "Frame (library)", operationType: "cut_line" }
];

export const TEXT_FONT_OPTIONS = [
  "Arial",
  "Verdana",
  "Tahoma",
  "Trebuchet MS",
  "Georgia",
  "Times New Roman",
  "Courier New"
];

export const TEXT_FILL_MODES = [
  { value: "solid", label: "Solid" },
  { value: "hatch", label: "Hatch" },
  { value: "outline", label: "Outline" }
];

export const PRIMITIVE_SHAPES = [
  { value: "rect", label: "Rectangle" },
  { value: "circle", label: "Circle" },
  { value: "triangle", label: "Triangle" },
  { value: "star", label: "Star" },
  { value: "diamond", label: "Diamond" }
];

function makeId(prefix) {
  return `${prefix}-${Date.now()}-${Math.floor(Math.random() * 1000)}`;
}

export function getObjectTypeConfig(type) {
  return NEW_OBJECT_TYPES.find((item) => item.value === type) || NEW_OBJECT_TYPES[0];
}

export function defaultTextStyle() {
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

export function buildObjectByType(type, sequenceNumber) {
  const index = sequenceNumber || 1;

  if (type === "text") {
    return {
      id: makeId("obj"),
      kind: "text",
      name: `Text ${index}`,
      text: "New text",
      textStyle: defaultTextStyle(),
      bbox: { x: 8, y: 8, w: 40, h: 16 }
    };
  }

  if (type === "line") {
    return {
      id: makeId("obj"),
      kind: "line",
      name: `Line ${index}`,
      line: { strokeMm: 0.4, angleDeg: 0 },
      bbox: { x: 8, y: 8, w: 50, h: 6 }
    };
  }

  if (type === "frame") {
    return {
      id: makeId("obj"),
      kind: "primitive",
      name: `Frame ${index}`,
      primitive: { shape: "rect", strokeMm: 1.2, fillMode: "outline" },
      bbox: { x: 5, y: 5, w: 80, h: 50 }
    };
  }

  if (type === "cut") {
    return {
      id: makeId("obj"),
      kind: "primitive",
      name: `Cut contour ${index}`,
      primitive: { shape: "rect", strokeMm: 0.8, fillMode: "outline" },
      bbox: { x: 8, y: 8, w: 50, h: 30 }
    };
  }

  return {
    id: makeId("obj"),
    kind: "primitive",
    name: `Primitive ${index}`,
    primitive: { shape: "rect", strokeMm: 0.5, fillMode: "solid" },
    bbox: { x: 8, y: 8, w: 40, h: 30 }
  };
}
