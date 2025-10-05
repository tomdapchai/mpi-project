# FFQ Optimization Plan for Distributed MPI Environment

## Current Performance Bottlenecks

### 1. MPI Datatype Recreation
**Problem**: Creating/freeing `MPI_Datatype` on every operation
**Cost**: ~100-1000 CPU cycles per create/free pair
**Fix**: Create once globally, reuse

### 2. Excessive Synchronization
**Problem**: Multiple lock/unlock/flush cycles per operation
**Current Enqueue**: ~3-4 synchronization points per attempt
**Current Dequeue**: ~5-8 synchronization points per attempt
**Fix**: Batch operations, reduce lock granularity

### 3. Sequential RMA Operations
**Problem**: Get → flush → Put → flush pattern
**Cost**: 2N network round-trips for N operations
**Fix**: Use request-based operations, batch flushes

### 4. No Local Caching
**Problem**: Reading queue->size, queue->tail remotely every time
**Cost**: Unnecessary network traffic
**Fix**: Cache read-only/rarely-changing data locally

### 5. Busy-Wait with Fixed Backoff
**Problem**: `do_work(10)` with fixed 10ms wait
**Fix**: Exponential backoff, adaptive waiting

## Optimization Strategy

### Phase 1: Code-Level Optimizations (Easy wins)
1. Global MPI_Datatype
2. Reduce flush operations
3. Cache immutable data locally
4. Better backoff strategy

### Phase 2: Algorithmic Improvements (Medium difficulty)
1. Batch operations where possible
2. Use MPI_Request for asynchronous operations
3. Separate metadata and data windows
4. Implement epoch-based synchronization

### Phase 3: Structural Redesign (Advanced)
1. Hybrid shared memory + MPI (for multi-node)
2. Lock-free using MPI_Compare_and_swap
3. Pre-allocated communication buffers
4. NUMA-aware placement

## Implementation Plan

### Optimization 1: Global Weather Type (5-10x speedup on this operation)
```c
static MPI_Datatype g_weather_type = MPI_DATATYPE_NULL;

void ffq_init_datatypes() {
    if (g_weather_type == MPI_DATATYPE_NULL) {
        // Create once
    }
}

void ffq_cleanup_datatypes() {
    if (g_weather_type != MPI_DATATYPE_NULL) {
        MPI_Type_free(&g_weather_type);
    }
}
```

### Optimization 2: Local Caching (Eliminates ~30% of network calls)
```c
typedef struct {
    FFQueue* queue;
    MPI_Win win;
    int local_size;      // Cached: never changes
    int local_rank;      // Process rank
} FFQHandle;
```

### Optimization 3: Batch RMA Operations (2-3x speedup)
**Before**:
```c
MPI_Get(...); 
MPI_Win_flush(...);  // Network wait
MPI_Put(...); 
MPI_Win_flush(...);  // Network wait
```

**After**:
```c
MPI_Get(...);
MPI_Put(...);
MPI_Win_flush(...);  // Single network wait
```

### Optimization 4: Reduce Lock Scope
**Current**: Lock entire window for every operation
**Better**: Use lock-free MPI_Compare_and_swap where possible
```c
MPI_Compare_and_swap(&new_val, &expected, &result, 
                     MPI_INT, 0, offset, win);
```

### Optimization 5: Adaptive Backoff
```c
int backoff_us = 10;
const int MAX_BACKOFF = 10000;  // 10ms max

while (!success) {
    // Try operation
    if (!success) {
        usleep(backoff_us);
        backoff_us = (backoff_us * 2 > MAX_BACKOFF) ? 
                     MAX_BACKOFF : backoff_us * 2;
    }
}
```

### Optimization 6: Split Metadata/Data Windows
**Rationale**: Metadata (head, tail) accessed frequently with small size
             Data (cells) accessed less frequently but larger

```c
typedef struct {
    MPI_Win metadata_win;  // For head, tail, size
    MPI_Win data_win;      // For cells array
} FFQWindows;
```

Benefits:
- Smaller lock granularity
- Better cache locality
- Parallel metadata/data operations

### Optimization 7: Passive Target Epochs (Advanced)
Replace lock/unlock pairs with fence-based epochs:
```c
MPI_Win_fence(0, win);
// Multiple RMA operations
MPI_Win_fence(MPI_MODE_NOSUCCEED, win);
```

**Trade-off**: Less fine-grained but much faster for batch operations

### Optimization 8: Hybrid Shared Memory + MPI
For multi-node clusters:
- Use `MPI_Win_allocate_shared` for intra-node communication
- Use regular MPI_Win for inter-node communication
- Can achieve 10-100x speedup for local operations

## Expected Performance Improvements

### Conservative Estimates:
- **Phase 1**: 3-5x overall speedup
  - Global datatype: 5-10x on create/free overhead
  - Batched RMA: 2-3x on communication
  - Local caching: 1.5-2x on metadata access

- **Phase 2**: Additional 2-3x speedup
  - Reduced synchronization: 2x
  - Better backoff: 1.2-1.5x

- **Phase 3**: Additional 5-10x speedup (node-local only)
  - Hybrid approach: 10-100x for intra-node
  - Overall: 5-10x when including inter-node

### Total Expected Improvement: 30-150x depending on configuration

## Testing Strategy

1. **Baseline Benchmark**: Current implementation
2. **Incremental Testing**: Apply optimizations one at a time
3. **Metrics to Track**:
   - Throughput (items/sec)
   - Latency per operation (enqueue/dequeue time)
   - Network traffic (MPI profiling)
   - Lock contention (time spent in locks)
   - Scalability (performance vs. number of consumers)

## Priority Order

**High Priority** (Implement first):
1. Global MPI_Datatype ✓ (Easy, big impact)
2. Batch RMA operations ✓ (Medium, big impact)
3. Local caching ✓ (Easy, good impact)

**Medium Priority**:
4. Adaptive backoff (Easy, moderate impact)
5. Reduce lock scope (Medium, good impact)
6. Split windows (Medium, moderate impact)

**Low Priority** (Advanced features):
7. Epoch-based sync (Hard, situation-dependent)
8. Hybrid shared memory (Hard, only for multi-node)

## Implementation Considerations

### Memory Ordering
MPI RMA operations need careful attention to memory ordering:
- Use `MPI_Win_flush` after writes that others must see
- Use `MPI_Win_flush_local` for local completion only
- Consider `MPI_Win_sync` for memory barrier

### Error Handling
Add proper error checking:
```c
int err = MPI_Get(...);
if (err != MPI_SUCCESS) {
    char error_string[MPI_MAX_ERROR_STRING];
    int len;
    MPI_Error_string(err, error_string, &len);
    fprintf(stderr, "MPI Error: %s\n", error_string);
}
```

### Debugging
Add compile-time debugging:
```c
#ifdef FFQ_DEBUG
    #define FFQ_LOG(fmt, ...) \
        fprintf(stderr, "[FFQ][Rank %d] " fmt "\n", my_rank, ##__VA_ARGS__)
#else
    #define FFQ_LOG(fmt, ...)
#endif
```

## Next Steps

1. Create `ffq_optimized.h` and `ffq_optimized.c`
2. Implement Phase 1 optimizations
3. Run benchmarks comparing original vs optimized
4. Iteratively add Phase 2 and 3 optimizations
5. Profile and tune based on results
