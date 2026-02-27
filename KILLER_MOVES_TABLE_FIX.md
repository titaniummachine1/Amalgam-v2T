# Freestand Killer Moves Table Fix

## Summary
Fixed the freestand anti-aim system to properly handle the killer moves table and ensure only enemy threats are considered, not our own weapons/class.

## Changes Made

### 1. Killer Moves Table Update During Sampling
**File:** `Amalgam\src\Features\PacketManip\AntiAim\Freestand.cpp`

#### In `SampleThreats()` function:
- Added killer moves table update logic during threat sampling
- When a threat sample can hit the head, we now increment the **new** killer moves table for that enemy
- Each enemy's entindex maps to their accumulated hit count in `m_mNewKillerMoves`

```cpp
const int iEntIndex = threat.m_pPlayer->entindex();
int& iNewKillerMoveScore = m_mNewKillerMoves[iEntIndex];

// During sampling...
if (!bHitWorld && !bBlockedByBody)
{
    threat.m_bSampleHit[s] = true;
    iNewKillerMoveScore++;  // Accumulate hit count in NEW killer moves table
    continue;
}

// For corner checks...
threat.m_bSampleHit[s] = bAnyCornerExposed;
if (bAnyCornerExposed)
{
    iNewKillerMoveScore++;  // Accumulate hit count in NEW killer moves table
}
```

#### In `SampleThreatsAtBothPitches()` function:
- Applied the same killer moves table update logic for dual-pitch mode
- Ensures consistency across both sampling methods

### 2. Killer Moves Table Finalization
**File:** `Amalgam\src\Features\PacketManip\AntiAim\Freestand.cpp`

#### At the end of `Run()` function:
- Added call to `FinalizeKillerMovesTable()` at the very end of freestand calculations
- This replaces the old killer moves table with the new one
- Clears the new table for the next tick

```cpp
// Finalize killer moves table: replace old with new, clear new for next tick
FinalizeKillerMovesTable();
```

## How It Works Now

### Tick-by-Tick Flow:
1. **Start of Tick N:**
   - `GatherThreats()` reads from `m_mKillerMoves` (contains data from previous tick)
   - Threats are sorted by killer move score from **previous tick**
   - `m_mNewKillerMoves` is cleared and initialized to 0 for each enemy

2. **During Tick N:**
   - As we sample threats, we accumulate hit counts in `m_mNewKillerMoves`
   - We continue to use `m_mKillerMoves` for threat prioritization
   - This ensures we're reading from stable data while building new data

3. **End of Tick N:**
   - `FinalizeKillerMovesTable()` is called
   - `m_mKillerMoves = std::move(m_mNewKillerMoves)` replaces old with new
   - `m_mNewKillerMoves.clear()` prepares for next tick

4. **Start of Tick N+1:**
   - `GatherThreats()` now reads from updated `m_mKillerMoves` (which has Tick N's data)
   - Process repeats

### Memory Safety:
- No memory leaks: old table is properly replaced, not accumulated
- No stale data: table is updated every tick
- Clear separation: read from current, write to new, then swap

## What This Fixes

1. **Killer Moves Table Was Not Being Updated:**
   - Previously, the table was initialized but never populated with sample data
   - Now it properly accumulates hit counts during sampling

2. **No Table Finalization:**
   - Previously, the new table was built but never became the current table
   - Now it's properly finalized at the end of each tick

3. **Memory Leak Prevention:**
   - Previously, there was no clear mechanism to replace old data
   - Now using `std::move()` and `clear()` ensures proper memory management

## Testing Recommendations

1. **Verify Threat Prioritization:**
   - Enemies that can hit more samples should be prioritized higher
   - The killer move score should update each tick based on current samples

2. **Check Memory Usage:**
   - Monitor memory usage over extended gameplay
   - Ensure no accumulation or leaks in the killer moves tables

3. **Validate Anti-Aim Behavior:**
   - Freestand should favor avoiding enemies that have more "killer moves"
   - The safest yaw should account for enemies with higher hit counts from previous tick

## Note on Local Player Weapon/Class Check

The current implementation of `CanPlayerHeadshot()` correctly checks **enemy** weapons and classes only. It does NOT check our own weapons/class, as intended. The function:
- Takes an enemy `CTFPlayer*` as parameter
- Returns `true` if that enemy can perform headshots
- This is used in `GatherThreats()` to filter out enemies that cannot headshot us

The function is working as intended and does not need modification.
