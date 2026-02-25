# Heatmap Rotation and Determinism Fix

## Problem
The heatmap visualization was not reflecting what was actually happening. The heatmap appeared to be flipped/rotated 90 degrees, and computations were not deterministic across frames due to relying on relative angles that changed with body position.

## Root Cause
The heatmap calculations had mixed coordinate systems:
- **Heatmap accumulation** used relative angles from threat directions
- **Visualization** used world yaws, but could drift based on view angles
- **Indexing** was inconsistent, causing spatial misalignment

This created a "moving" heatmap that rotated with the player's body, rather than a fixed world-space reference.

## Solution: Pure World Yaw Coordinate System

All heatmap operations now use **absolute world yaw angles exclusively**, with a consistent index mapping:
- Index 0 = Yaw -180°
- Index resolution/2 = Yaw 0°
- Index resolution-1 = Yaw +180°

### Functions Updated

1. **AccumulateThreatSample()**
   - Now normalizes input yaw to [-180, 180] range
   - Distributes threat values using consistent world yaw indexing
   - No dependency on m_flViewYaw or relative angles

2. **GetNormalizedSafety()**
   - Simplified to use world yaw directly
   - Index calculation: `(flYaw + 180) / flStep`
   - Consistent with heatmap array layout

3. **AccumulateThreatSampleDual()**
   - Updated to match main heatmap's world yaw normalization
   - Ensures pitch override mode uses same coordinate system

4. **GetNormalizedSafetyDual()**
   - Simplified index calculation
   - Uses consistent world yaw mapping as dual threat sampling

5. **BuildHeatmapVisualization()**
   - Now guaranteed to use world yaw coordinates
   - Points generated from -180° to +180° consistently
   - Visualization matches heatmap calculations exactly

## Benefits

✅ **Deterministic**: Heatmap is always the same for the same threats, regardless of player rotation  
✅ **No Rotation**: Heatmap stays fixed in world space, doesn't rotate with body  
✅ **Accurate Visuals**: Visualization now precisely reflects computed threat safety  
✅ **Simplified Logic**: Removed complex relative angle calculations  
✅ **Predictable**: SafeYaw findings are consistent and reproducible  

## Testing

To verify the fix works correctly:
1. Run freestand anti-aim with visualization enabled
2. Rotate your body - heatmap should remain stationary in world space
3. The green/orange line pointing to safest yaw should align with actual safe angles
4. No visual rotation or drift as you turn

## Code Example: New Index Mapping

```cpp
float flNormalizedYaw = Math::NormalizeAngle(flYaw);  // Ensures [-180, 180]
const float flIndex = (flNormalizedYaw + 180.f) / flStep;
const int iIndex0 = static_cast<int>(floorf(flIndex)) % iSize;
const int iIndex1 = (iIndex0 + 1) % iSize;
```

This ensures every yaw maps to the exact same heatmap indices across frames.
