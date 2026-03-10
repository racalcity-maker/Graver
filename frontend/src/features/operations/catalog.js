export const OPERATION_TYPES = [
  { value: "engrave_fill", label: "Engrave Fill", defaultOrder: 10, speedMmMin: 2200, power: 100, passes: 1 },
  { value: "engrave_line", label: "Engrave Line", defaultOrder: 30, speedMmMin: 1800, power: 120, passes: 1 },
  { value: "image_raster", label: "Image Raster", defaultOrder: 40, speedMmMin: 1600, power: 110, passes: 1 },
  { value: "score_line", label: "Score Line", defaultOrder: 70, speedMmMin: 900, power: 180, passes: 1 },
  { value: "cut_line", label: "Cut Line", defaultOrder: 90, speedMmMin: 450, power: 255, passes: 2 }
];

export function getOperationPreset(type) {
  return OPERATION_TYPES.find((item) => item.value === type) || OPERATION_TYPES[0];
}

export function buildOperation(type, sequenceNumber = 1) {
  const preset = getOperationPreset(type);
  return {
    id: `op-${Date.now()}-${Math.floor(Math.random() * 1000)}`,
    name: `${preset.label} ${sequenceNumber}`,
    type: preset.value,
    speedMmMin: preset.speedMmMin,
    power: preset.power,
    passes: preset.passes,
    order: preset.defaultOrder
  };
}
