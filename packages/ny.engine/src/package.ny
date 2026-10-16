@pragma once
# =============================================================================
# ny.engine — Game/Rendering Engine Package
# =============================================================================
# 3D math, ECS, AI, input, UI, and Vulkan experiments.
#
# Provides:
#   - Math: vec4, quat, mat4, perspective, look_at (math.ny)
#   - ECS: Entity-Component-System (ecs.ny)
#   - AI: pathfinding, behaviour trees (ai.ny)
#   - Input: keyboard, mouse, gamepad (input.ny)
#   - UI: in-engine immediate mode UI (ui.ny)
#   - Vulkan: low-level Vulkan bindings (vulkan.ny)
#   - Native: platform abstraction layer (native.ny)
#
# Cross-refs:
#   ny.lang.math — ny.engine.math extends vec2/vec3 with vec4/quat/mat4
#   ny.ui.mimic  — engine may run in a Mimic window
#   nilaix         — editor experiments may use ny.engine for a 3D viewport
#
# Quarantined in experimental/: scene, renderer, camera, and physics.
# =============================================================================

package ny.engine

import ny.engine.math
import ny.engine.ecs
import ny.engine.ai
import ny.engine.input
import ny.engine.ui
import ny.engine.vulkan
import ny.engine.native
