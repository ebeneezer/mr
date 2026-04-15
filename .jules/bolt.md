## 2024-10-24 - [Optimize piecewiseLineIndex to leverage SIMD]
**Learning:** Found a specific bottleneck where `piecewiseLineIndex` iterated character-by-character manually instead of deferring to the highly optimized `piecewiseCountLineBreaksInRange`, which uses SSE2 SIMD instructions.
**Action:** When working on text processing functions, always check if there's an existing vectorized operation that can be reused instead of implementing loops manually.
