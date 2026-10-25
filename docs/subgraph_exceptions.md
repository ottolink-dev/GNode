# SubGraph Exception Handling and Asynchronous Execution

This document details exception propagation, error handling, and thread synchronization guarantees when evaluating `SubGraphNode` instances in both synchronous and asynchronous modes.

## Overview

Hierarchical graphs in GNode encapsulate an inner `Graph` inside a `SubGraphNode`. When a graph contains nested subgraphs, exceptions occurring anywhere within the inner graph hierarchy must be safely captured, coordinated across worker threads, and cleanly rethrown to the caller.

```mermaid
flowchart TD
    subgraph Root["Root Graph (update / update_async)"]
        A["Node A"] --> SG["SubGraphNode"]
        SG --> B["Node B"]
    end

    subgraph Inner["Child Graph (subgraph->update)"]
        IN["NodeIn<T>"] --> OP["Throwing Node"]
        OP --> OUT["NodeOut<T>"]
    end

    SG -.->|"1. pull inputs"| IN
    SG -.->|"2. evaluate & catch error"| Inner
    OUT -.->|"3. push outputs (aborted on throw)"| SG
```

## Exception Flow in SubGraphNode

The lifecycle of `SubGraphNode::compute()` operates in three distinct phases:

1. **Pull Step**: Reads input values from external parent ports into internal boundary nodes (`NodeIn<T>`).
2. **Evaluation Step**: Invokes `this->subgraph->update()`.
3. **Push Step**: Pushes resulting values from child boundary nodes (`NodeOut<T>`) out to external parent ports.

If any node inside the child graph throws an exception during **Step 2**:
- The exception immediately interrupts child graph traversal.
- **Step 3 is bypassed**, preventing partial, corrupted, or stale outputs from being published to the parent graph.
- The exception escapes `SubGraphNode::compute()` and unwinds into the parent graph execution engine.

```mermaid
sequenceDiagram
    participant Parent as Parent Graph
    participant SG as SubGraphNode
    participant Child as Inner Graph
    participant Node as Failing Inner Node

    Parent->>SG: compute()
    SG->>Child: pull_from_parent()
    SG->>Child: subgraph->update()
    Child->>Node: update() / compute()
    Note over Node: Exception thrown!
    Node-->>Child: throw std::runtime_error
    Child-->>SG: propagate exception
    Note over SG: Output push skipped
    SG-->>Parent: throw std::runtime_error
```

## Asynchronous Catch and Rethrow Mechanics

In asynchronous or parallel graph updates (`Graph::update_async()` and `GraphWorker`), execution is coordinated across a worker pool. A failure within any node (including a `SubGraphNode` or an inner node within it) undergoes strict catch-and-rethrow coordination.

```mermaid
flowchart TD
    Start["Worker Thread: Node Throws"] --> Catch["catch (...) in ThreadPool task"]
    Catch --> Capture["Capture std::current_exception()<br/>store in captured_error (mutex-guarded)"]
    Capture --> Cancel["cancelled.store(true)<br/>notify sync_cv condition variable"]
    Cancel --> Drain["pool.stop_and_join()<br/>drain running tasks"]
    Drain --> Rethrow["std::rethrow_exception(captured_error)"]
    Rethrow --> Future["Captured in std::future<void>"]
    Future --> Caller["caller: future.get() throws"]
```

### 1. Worker Thread Capture
Inside `Graph::execute_parallel()`:
- Each node task is wrapped in a `try / catch (...)` block.
- When an exception escapes a node's `update()` method, `std::current_exception()` captures the exception pointer into `captured_error`.
- The first thrown exception is preserved; subsequent exceptions from concurrently completing tasks are discarded.

### 2. Immediate Graph Cancellation
Upon catching an exception:
- The atomic flag `cancelled` is set to `true`.
- Any queued tasks that have not yet started execution are skipped immediately.
- `sync_cv.notify_all()` awakens the supervising thread waiting in `sync_cv.wait()`.

### 3. Clean Thread Synchronization
- Before the exception is propagated out, `pool.stop_and_join()` joins all worker threads.
- This guarantees no dangling threads or orphaned memory references remain active when stack variables unwind.

### 4. Rethrow via Future
- The supervising thread checks `captured_error` and invokes `std::rethrow_exception(captured_error)`.
- Because `update_async` executes this pipeline inside `std::async(std::launch::async, ...)`, the rethrown exception is transported directly into the returned `std::future<void>`.
- When the caller calls `future.get()`, the original exception is rethrown on the caller's thread with its type and message intact.

## Nested Subgraphs

The exception guarantee holds recursively across arbitrary depths ($N$-level hierarchical nesting).

```mermaid
flowchart TD
    Root["Root Graph (update_async)"] -->|"Worker Thread"| L2["SubGraphNode (Level 2)"]
    L2 -->|"subgraph->update()"| L3["SubGraphNode (Level 3)"]
    L3 -->|"subgraph->update()"| ErrNode["Inner Failing Node: throw"]

    ErrNode -.->|"unwinds"| L3
    L3 -.->|"unwinds"| L2
    L2 -.->|"unwinds to worker catch"| Root
    Root -.->|"captured & rethrown"| Fut["future.get()"]
```

Even with multiple subgraphs evaluating concurrently on different threads, thread safety is maintained:
- Thread synchronization primitives prevent race conditions when multiple subgraphs fail simultaneously.
- Downstream nodes dependent on the failing subgraph are prevented from executing.
- Fixing the failing condition allows subsequent `update_async()` invocations to proceed without needing graph recreation.
