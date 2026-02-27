# Freestand Visualization Fixes

## Issues Fixed

### 1. Red Line Broken in Single Mode (Auto Pitch)
**Problem**: When pitch was changed from "up" to "auto", the red line disappeared or displayed incorrectly.

**Root Cause**: The circle center Z height (`vCircleCenter`) was using `m_vHeadCenter.z` (the original head Z from when bones were set up), but `GetHeadPosForYaw()` now returns points using the pitch-appropriate Z height (`m_flHeadCenterUpZ` or `m_flHeadCenterDownZ`). This Z mismatch caused the line to be drawn between two points at different heights.

**Fix**: Calculate the correct circle center Z based on current pitch:
```cpp
const bool bCurrentIsUp = (m_flCurrentPitch < 0.f);
const float flCircleZ = bCurrentIsUp ? m_flHeadCenterUpZ : m_flHeadCenterDownZ;
Vec3 vCircleCenter = Vec3(m_vViewPos.x, m_vViewPos.y, flCircleZ);
```

### 2. Green Line Showed Wrong Position
**Problem**: The green line (safest yaw) pointed to a location that would expose the head, not hide it. The visualization didn't match the actual safety logic.

**Root Cause**: We were rendering the DESIRED world yaw position (where we WANT the head), not where the head ACTUALLY ends up after applying the body yaw. The animation system's response to body yaw changes means the head doesn't always end up exactly at the calculated position.

**Fix**: After finding the safest yaw, verify where the head actually ends up by:
1. Converting the safest world yaw to body yaw using `SolveBodyYawForHeadTarget()`
2. Setting up bones with that body yaw
3. Getting the actual hitbox center position
4. Storing this verified position in `m_vSafestHeadPos`
5. Rendering using the ACTUAL verified position, not the theoretical one

```cpp
// Verify where head actually ends up at safest yaw
if (m_bHasSafeYaw)
{
    const float flBodyYaw = SolveBodyYawForHeadTarget(pLocal, m_flSafestYaw, false);
    if (SetupBonesForYaw(pLocal, flBodyYaw, m_aTempBones))
    {
        m_vSafestHeadPos = pLocal->As<CBaseAnimating>()->GetHitboxCenter(m_aTempBones, HEAD_HITBOX);
    }
}
```

### 3. Circle Radius Accuracy
**Problem**: The down pitch circle radius didn't accurately represent the horizontal distance of the head from the body center.

**Status**: Already correct in code - the radius is measured directly from the bones at pitch down (89°). The visual issue was a rendering artifact from the Z height mismatch, not the radius calculation.

### 4. Red Line Calculation Simplified
**Problem**: The red line calculation was unnecessarily complex and error-prone.

**Fix**: Simplified to directly calculate the delta from circle center:
```cpp
Vec3 vDelta = m_vActualHeadPos - vCircleCenter;
vDelta.z = 0.f;  // Project to horizontal plane
float flActualHeadYaw = RAD2DEG(atan2f(vDelta.y, vDelta.x));
```

## Key Insight

The critical realization was that **the heatmap stores DESIRED head yaw positions, but the animation system doesn't place the head exactly where calculated**. The yaw offset provides an approximation, but there are small errors.

By verifying where the head ACTUALLY ends up (by setting up bones and checking the hitbox center), we can:
1. Render the correct position (matching what the logic tested)
2. Show the user where the head will truly be
3. Ensure visual feedback matches the safety calculations

## Testing

1. **Red Line**: Should now appear correctly in both up and auto pitch modes
2. **Green Line**: Should point to where the head actually is hidden, not where we think it should be
3. **Circle Switching**: Single circle should smoothly switch between up/down based on current pitch
4. **Dual Mode**: Both circles render correctly with lines only on the current pitch circle

## Files Modified

1. **Freestand.h**
   - Added `m_vSafestHeadPos` to store verified head position
   
2. **Freestand.cpp**
   - Fixed circle center Z calculation in single mode rendering
   - Added head position verification after finding safest yaw (both modes)
   - Updated rendering to use verified positions instead of theoretical ones
   - Simplified red line calculation
