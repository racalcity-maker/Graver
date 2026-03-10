import { buildJobItemForObject } from "./job-item-builder.js";

function safeNumber(value, fallback = 0) {
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function operationTypePriority(type) {
  switch (String(type || "")) {
    case "engrave_fill":
      return 10;
    case "engrave_line":
      return 20;
    case "image_raster":
      return 30;
    case "score_line":
      return 40;
    case "cut_line":
      return 90;
    default:
      return 50;
  }
}

function buildStageTemplate(layer, operation) {
  return {
    stageId: `${operation.id}::${layer.id}`,
    layerId: layer.id,
    layerName: layer.name,
    operationId: operation.id,
    operationName: operation.name,
    operationType: operation.type,
    order: safeNumber(operation.order, 100),
    opPriority: operationTypePriority(operation.type),
    speedMmMin: Math.max(1, safeNumber(operation.speedMmMin, 1000)),
    power: Math.max(0, Math.min(255, Math.round(safeNumber(operation.power, 128)))),
    passes: Math.max(1, Math.round(safeNumber(operation.passes, 1))),
    objectIds: [],
    items: [],
    metrics: {
      distanceMm: 0,
      laserDistanceMm: 0
    }
  };
}

function estimateStageDurationSec(stage) {
  const speed = Math.max(1, stage.speedMmMin);
  const baseDistance = Math.max(0.5, stage.metrics.distanceMm);
  return ((baseDistance * stage.passes) / speed) * 60;
}

export function buildMultiStageJobPlan(state) {
  const layerById = new Map(state.layers.map((layer) => [layer.id, layer]));
  const operationById = new Map(state.operations.map((operation) => [operation.id, operation]));
  const stageMap = new Map();

  for (const object of state.objects) {
    const layer = layerById.get(object.layerId);
    if (!layer || layer.visible === false) {
      continue;
    }
    const operation = operationById.get(layer.operationId);
    if (!operation) {
      continue;
    }

    const stageKey = `${operation.id}::${layer.id}`;
    if (!stageMap.has(stageKey)) {
      stageMap.set(stageKey, buildStageTemplate(layer, operation));
    }

    const stage = stageMap.get(stageKey);
    const converted = buildJobItemForObject(object);
    stage.objectIds.push(object.id);
    stage.items.push(converted.item);
    stage.metrics.distanceMm += converted.distanceMm;
    stage.metrics.laserDistanceMm += converted.laserDistanceMm;
  }

  const stages = Array.from(stageMap.values())
    .sort((a, b) => {
      if (a.order !== b.order) {
        return a.order - b.order;
      }
      if (a.opPriority !== b.opPriority) {
        return a.opPriority - b.opPriority;
      }
      return a.stageId.localeCompare(b.stageId);
    })
    .map((stage) => {
      const { opPriority, ...exportedStage } = stage;
      return {
        ...exportedStage,
        metrics: {
          distanceMm: Number(stage.metrics.distanceMm.toFixed(3)),
          laserDistanceMm: Number(stage.metrics.laserDistanceMm.toFixed(3))
        },
        estimatedSec: estimateStageDurationSec(stage)
      };
    });

  const totalSec = stages.reduce((sum, stage) => sum + stage.estimatedSec, 0);
  const totalDistance = stages.reduce((sum, stage) => sum + stage.metrics.distanceMm, 0);
  const totalLaserDistance = stages.reduce((sum, stage) => sum + stage.metrics.laserDistanceMm, 0);

  return {
    version: 2,
    kind: "multi-stage-native",
    scene: {
      widthMm: safeNumber(state.scene.widthMm, 180),
      heightMm: safeNumber(state.scene.heightMm, 130)
    },
    meta: {
      generatedAt: new Date().toISOString(),
      objectCount: state.objects.length,
      stageCount: stages.length,
      totalDistanceMm: Number(totalDistance.toFixed(3)),
      totalLaserDistanceMm: Number(totalLaserDistance.toFixed(3))
    },
    stages,
    estimatedSec: totalSec
  };
}
