# Critical Heatmap and Head Positioning Fixes

## Issues Fixed

### 1. **Heatmap Accumulation Using Desired Yaw Instead of Actual Yaw**
**Problem**: When sampling threats, we calculated a target world yaw and accumulated threats at that yaw. However, due to animation system behavior, the head didn't actually end up exactly at that yaw. This caused a mismatch between where we thought the head was and where it actually was, making the heatmap colors incorrect (green areas were visible when they should have been red).

**Solution**: 
- Added `m_vActualSampleYaw` vector to `FreestandThreat_t` to store where head actually ended up
- After applying body yaw and setting up bones, calculate the ACTUAL world yaw of the head position:
  ```cpp
  Vec3 vHeadDelta = vHeadCenter - vCircleCenter;
  vHeadDelta.z = 0.f;
  const float flActualHeadYaw = RAD2DEG(atan2f(vHeadDelta.y, vHeadDelta.x));
  threat.m_vActualSampleYaw[s] = flActualHeadYaw;
  ```
- Use the ACTUAL yaw for heatmap accumulation, not the desired yaw

### 2. **Refinement Iterations Using Wrong Yaw**
**Problem**: During refinement (`RefineHeatmap()`), we verified the safest candidates but didn't check where the head actually ended up. We used the theoretical yaw from the heatmap visualization.

**Solution**:
- Before counting hits, verify where head actually ends up at the candidate yaw
- Update the bestPoint's yaw to the ACTUAL yaw after bone setup
- This ensures the final safest yaw is where the head truly ends up

### 3. **Dual Mode Sampling Not Verifying Actual Yaw**
**Problem**: `SampleThreatsAtBothPitches()` had the same issue as single mode - using desired yaw instead of actual yaw.

**Solution**:
- Added actual yaw calculation for both pitch modes
- Store and use actual yaw for dual heatmap accumulation
- This fixes the issue where safest yaw on down pitch didn't hide the head

### 4. **Memory Cleanup for New Vector**
**Problem**: Added new vector that needs proper cleanup.

**Solution**:
- Update `GatherThreats()` to clear and shrink `m_vActualSampleYaw` each tick
- Prevents memory accumulation

## Key Insight

The fundamental issue was: **We were treating the yaw offset as a simple constant, but the animation system's response is non-linear and varies based on body yaw and target direction.**

By verifying where the head ACTUALLY ends up after applying body yaw (via `SetupBones` and `GetHitboxCenter`), we now:
1. **Accumulate threats at the correct yaw** - where head truly is
2. **Generate accurate heatmap data** - colors match reality
3. **Find yaws that actually hide the head** - not theoretical positions

## Testing

The heatmap colors should now correctly reflect safety:
- **Red areas** = Head is visible (can be shot)
- **Green areas** = Head is hidden (safe)
- **Green line** = Points to where head actually ends up at safest yaw
- **Red line** = Shows current head position accurately

## Technical Details

### Yaw Calculation
```cpp
// Circle center at appropriate Z height
Vec3 vCircleCenter = Vec3(m_vViewPos.x, m_vViewPos.y, flCircleZ);

// Get actual head position from bones
Vec3 vHeadCenter = pLocal->As<CBaseAnimating>()->GetHitboxCenter(bones, HEAD_HITBOX);

// Calculate actual world yaw
Vec3 vHeadDelta = vHeadCenter - vCircleCenter;
vHeadDelta.z = 0.f;  // Project to horizontal
float flActualYaw = RAD2DEG(atan2f(vHeadDelta.y, vHeadDelta.x));
```

This gives us the true world-space yaw angle of where the head ended up, regardless of what yaw we requested.

### Why This Matters

When we request body yaw for target head yaw 45°:
1. Calculate body yaw: `bodyYaw = 45° - offset`
2. Apply to animation system
3. Animation system responds with its own interpretation
4. **Head might end up at 42° or 48°, not exactly 45°**

By measuring the actual result, we ensure our heatmap reflects reality, not theory.
