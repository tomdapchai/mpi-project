# Fast Forward Queue (FFQ) Algorithm

## Overview

FFQ implements a concurrent FIFO queue designed for a single producer and multiple consumers. The rationale behind the single-producer assumption is to maximize speed and minimize synchronization overhead. The queue is optimized for scenarios where consumer processing time greatly exceeds producer time, ensuring the single producer is never a bottleneck. A key design goal is that the enqueue operation should ideally be wait-free, or at least lock-free.

## Data Structures

FFQ relies on the following data structures:

### Cell
```
Type cell:
  data: NULL        // Actual data (initially empty)
  rank: -1          // Rank of item (or -1 if cell unused)
  gap: -1           // Gap in numbering (skipped item)
```

### Queue Variables
```
cells[N]: array of cell  // Bounded array of cells
tail: 0                  // Tail counter (monotonically increasing)
head: 0                  // Head counter (monotonically increasing)
```

- **cells**: A bounded array large enough that there will always be empty slots for the producer
- **tail**: Managed by the single producer (not shared)
- **head**: Shared atomic variable accessed by multiple consumers

The array is managed as a circular buffer, with indexes computed modulo N. Two integer variables (head and tail) track the rank of the first and last data items in the queue. Ranks are mapped to array elements using modulo arithmetic: the item with rank k is located at position (k mod N).

## Algorithm Operations

### Enqueue Operation (Single Producer)
```
function FFQ_ENQ(data)
  success ← FALSE
  while ¬success do                  // Find empty cell...
    c ← cells[tail(mod N)]           // Try next cell
    if c.rank ≥ 0 then               // Cell used?
      c.gap ← tail                   // Yes: skip it (gap in rank)
    else
      c.data ← data                  // No: grab it
      c.rank ← tail                  // Remember rank
      success ← TRUE
    tail ← tail + 1                  // Move to next cell
```

### Dequeue Operation (Multiple Consumers)
```
function FFQ_DEQ
  rank ← fetch-and-inc(head)         // Get rank of next item
  c ← cells[rank(mod N)]             // Check associated cell
  success ← FALSE
  while ¬success do                  // Find next used cell...
    if c.rank = rank then            // Cell used for rank?
      data ← c.data                  // Yes: get item
      c.rank ← -1                    // Recycle cell
      success ← TRUE
    else if c.gap ≥ rank ∧ c.rank ≠ rank then
      rank ← fetch-and-inc(head)     // Cell skipped: ...
      c ← cells[rank(mod N)]         // ...move to next cell
    else 
      wait()                         // Back off (producer still writing cell)
  return data                        // Return item
```

## Implementation Details

### Gap Handling

Gaps occur when the next slot where a producer tries to store an element (at rank r1) is not free because a consumer started but did not complete dequeuing an older element at rank r2 < r1 with (r2 mod N) = (r1 mod N). In such cases:

1. The producer skips r1 and moves to the next rank (r1 + 1), creating a gap
2. The gap is announced in the cell by updating the gap field
3. The same cell might be skipped multiple times due to a very slow consumer, in which case gap is set to the last rank that was skipped

### Synchronization Points

- **Enqueue**: Setting the rank field to the current value of tail announces availability of data to consumers and represents a synchronization point
- **Dequeue**: Resetting the cell's rank to -1 represents a synchronization point

### Concurrency Considerations

- **Enqueue**: Simple and fast with minimal synchronization since there's a single producer
- **Dequeue**: Requires additional synchronization operations to manage conflicts between multiple consumer threads
- The operation order is important, particularly:
  - Writing data before updating the rank field during enqueue
  - Reading data before resetting the rank field during dequeue

### Performance Characteristics

The FFQ algorithm maintains the efficient "modulo" mapping of ranks to array cells for performance reasons. Instead of changing this mapping when cells need to be skipped, the algorithm uses the gap field to announce skipped ranks, allowing consumers to efficiently navigate the queue. 