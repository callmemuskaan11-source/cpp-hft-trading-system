Event-Driven C++20 HFT Trading Simulator
Two projects in one repository, sharing one common/ library:
Project	Folder	Roadmap stages	One-line summary
1. Market Data Feed Handler & Limit Order Book	market-data-feed-handler/	1-6	Read a stream of market messages, validate it, and rebuild an exchange order book from it.
2. Low-Latency Tick-to-Trade Pipeline	low-latency-tick-to-trade/	7-12	Turn book updates into trading decisions and simulated orders, fast, across threads.


Stage 10 (integration) and Stage 12 (benchmarks) combine both projects.
Rule for this README: it describes what the project is designed to do. Numbers (latency, throughput) are only added after they have been measured.

1. What are we actually building?
A simulator of the software that sits inside a trading firm. There is no real exchange and no real money. We invent the market ourselves, then build the full chain of software that would react to it:
Synthetic Market Data
  -> Feed Handler          (decode messages)
  -> Sequence Validator    (detect lost / duplicate / out-of-order messages)
  -> Limit Order Book      (rebuild the market's state)
  -> Strategy Engine       (decide BUY / SELL / NO_ACTION)
  -> Risk Manager          (approve or reject the decision)
  -> Order Management System (track our orders)
  -> Simulated Execution Gateway (pretend exchange fills/rejects our orders)
  -> Execution Reports -> back to OMS, Positions, P&L, Risk
The last arrow matters: results of our orders flow back to the OMS, position tracking and risk. The system is a loop, not a straight line.

"Tick-to-trade" means the time from a market data tick arriving to our order being submitted. That is the number HFT firms care about, and what Stage 12 measures.
Two different kinds of data (do not mix them up)
- Market data: what everyone else is doing (orders added, changed, cancelled, trades). It arrives from the exchange. We only read it.
- Our orders: what we decide to send. These go out through risk, OMS and the gateway, and come back as execution reports.
In this simulator the synthetic market data is generated independently, and our own orders do not change it. That keeps the project tractable. Stage 9 decides the fill model (see Open Decisions).
2. Trading vocabulary you need
- Bid: a buy order (someone wants to buy at this price or lower).
- Ask / offer: a sell order.
- Best bid: highest price a buyer will pay. Best ask: lowest price a seller will accept.
- Spread = best ask - best bid. Small spread = liquid market.
- Limit order: "buy/sell at this price or better". It can sit in the book waiting.
- Market order: "buy/sell now at whatever price is available". Executes immediately, never rests.
- Price level: all resting orders at one price. Depth: how much quantity sits at each level.
- Price-time priority: the matching rule. Better price first; at the same price, the order that arrived earlier is filled first (FIFO queue per level).
- Tick price: prices stored as integers (e.g. 10025 = 100.25 with tick 0.01). Never use float/`double` for prices: rounding errors break equality and book keys.
- Position: how much of an instrument we hold (long = positive, short = negative).
- P&L: profit and loss. Realized = closed trades; unrealized = open position marked to the current price.
- Order book imbalance: (bid qty - ask qty) / (bid qty + ask qty) near the top of the book. Positive means more buying pressure.
Tiny worked example
        ASK (sellers)
  10003   qty 40
  10002   qty 25   <- best ask
  ---------------- spread = 10002 - 10000 = 2 ticks
  10000   qty 30   <- best bid
   9999   qty 50
        BID (buyers)
A buyer sends a market buy 40. It takes 25 at 10002, then 15 at 10003. The book now has best ask 10003 with 25 left. A feed would publish this as TRADE messages and the book updates.
3. The four market messages
Event	Meaning	Book effect
ADD	New resting order (id, side, price, qty)	Insert into the price level (back of the queue), index by order id
MODIFY	Change an existing order's qty and/or price	Reduce qty keeps queue position; price change or qty increase usually loses priority (move to back)
CANCEL	Remove an order	Look up by order id, remove, delete empty level
TRADE	An order was (partially) executed	Reduce the resting order's qty; remove it if fully filled


Every message carries a sequence number (1, 2, 3, ...). A receiver uses it to know whether it missed something.
4. Stage-by-stage guide
Each stage lists: what you build, the concept to learn first, what "done" means.
Stage 1 - Market message model (DONE)
- Build: Price, Quantity, OrderId, SequenceNumber, Timestamp, Side, Order, MarketEvent, EventType, ExecutionReport.
- Learn: integer tick prices, enums, why message structs are small, flat and trivially copyable.
- Done when: types compile and the test passes in both Debug and Release.
- Pending polish: OrderType {Limit, Market}, static_assert(is_trivially_copyable), document what quantity means in CANCEL/TRADE, tests that run in Release.
Stage 2 - Synthetic market data generator
- Build: a seeded generator that produces ADD/MODIFY/CANCEL/TRADE events with valid order IDs; write/read them to a file; a replay reader.
- Learn: std::mt19937_64 with a fixed seed, state tracking (a MODIFY/CANCEL must reference an order that currently exists), binary vs text serialization, why you write fields individually rather than dumping a padded struct.
- Rules to enforce: sequence numbers strictly increase from 1; prices stay in a sensible band around a mid price; never cancel an order twice; TRADE quantity never exceeds the resting quantity.
- Done when: 100,000+ events generated; same seed gives byte-identical file; reading it back gives identical events.
Stage 3 - Basic limit order book
- Build: bid side and ask side as std::map<Price, PriceLevel> (bids sorted descending), std::unordered_map<OrderId, OrderLocation> for O(1) lookup, ADD only. Queries: best bid, best ask, spread, depth for N levels.
- Learn: why a map per side, why bids need a reverse comparator, what a price level stores (FIFO queue of orders + total quantity).
- Done when: after replaying generated ADDs the book answers best bid/ask/spread correctly and matches a simple brute-force check.
Stage 4 - Complete limit order book
- Build: MODIFY, CANCEL, TRADE; partial fills; full removals; empty-level cleanup; invalid-input handling (unknown id, duplicate id, zero qty, wrong side).
- Learn: priority rules on modify, iterator/pointer invalidation when you erase from containers, returning a result code instead of crashing.
- Done when: tests cover every event type and every invalid case, and a 100k-event replay never corrupts the book (add an internal consistency check: sum of level quantities equals sum of order quantities).
- Note: this book is built from the feed (it mirrors the exchange). It does not match orders itself; the TRADE messages tell it what happened.
Stage 5 - Feed handler and sequence validation
- Build: decode a raw message, validate its sequence number, dispatch to the book. Detect gap (missed messages), duplicate (seen already), out-of-order. Book state is Valid or Stale.
- Learn: why real feeds lose packets, expected-next-sequence logic, what to do on each anomaly (drop duplicate, flag gap and stop trusting the book).
- Done when: injected gaps/duplicates/reorders are each detected and counted; the book goes Stale on a gap and downstream code can see it.
Stage 6 - Snapshot and recovery
- Build: snapshot of the full book tagged with the sequence number it reflects; recovery = load snapshot, then replay buffered messages with sequence greater than the snapshot's.
- Learn: deterministic replay, the "snapshot + incremental" pattern used by real exchanges.
- Done when: drop messages on purpose, recover, and the recovered book is identical to a book that saw the full stream.
- Milestone for Project 1: this completes the feed handler / order book project.
Stage 7 - Strategy engine
- Build: on each book update, compute spread and imbalance and emit BUY, SELL or NO_ACTION.
- Learn: event-driven evaluation (react to updates, do not poll), keeping strategies pure functions of book state so they are testable.
- Done when: hand-built books with known imbalance produce the expected signal, including edge cases (empty side, huge spread, equal quantities).
- Honesty note: the strategy is a toy. It exists to exercise the pipeline, not to make money. Do not claim profitability.
Stage 8 - Risk manager
- Build: pre-trade checks between strategy and OMS: max order quantity, position limit, notional exposure limit, market data freshness (reject if book is Stale), kill switch.
- Learn: risk is a gate, every rejection has a reason code, kill switch stops all new orders.
- Done when: each limit has a pass test and a reject test, and a Stale book always blocks orders.
Stage 9 - OMS and execution simulator
- Build: unique order IDs; lifecycle state machine New -> Acknowledged -> PartiallyFilled -> Filled, plus Rejected and Cancelled; a simulated gateway that acks, rejects, cancels, partially fills; execution reports; position and P&L tracking.
- Learn: why illegal state transitions must be rejected (e.g. Filled -> Acknowledged), average-price and realized/unrealized P&L math.
- Done when: tests walk every legal transition, reject every illegal one, and position/P&L match hand-calculated values.
Stage 10 - Single-threaded integration (Milestone A)
- Build: one sequential event loop connecting everything, including the execution-report return path into OMS, position and risk.
- Learn: how components talk (direct calls or queues), deterministic ordering, failure injection.
- Done when: end-to-end tests pass, failure scenarios behave (gap -> Stale -> risk blocks orders -> recovery resumes), and you have a baseline latency measurement. This single-threaded result is your reference for correctness later.
Stage 11A - Multithreading fundamentals
- Build: small exercises, not project code: std::jthread, mutex, condition variable, producer-consumer queue.
- Learn: data races, thread ownership, why shared mutable state needs one owner or synchronization.
- Done when: you can explain and demonstrate a data race, then fix it (use ThreadSanitizer).
Stage 11B - Three-threaded pipeline
- Thread 1: Feed Handler + Sequence Validator + Order Book.
- Thread 2: Strategy + Risk.
- Thread 3: OMS + Simulated Execution Gateway.
- Threads communicate through queues; execution reports return from thread 3 to the components that need them.
- Learn: what happens when a queue is full, clean startup and shutdown, event ordering across threads.
- Done when: the three-threaded run produces the same orders/positions/P&L as the single-threaded run on the same input, and ThreadSanitizer is clean.
- Design question to answer early: Risk (thread 2) needs positions, but positions are updated by fills (thread 3). Decide how thread 2 learns about them (execution reports flowing back through a queue) rather than sharing a variable.
Stage 11C - SPSC ring buffers and atomics
- Build: a fixed-capacity single-producer single-consumer ring buffer using std::atomic head/tail with acquire/release ordering, cache-line-aligned indices, wraparound and backpressure handling.
- Learn: memory ordering, false sharing, why SPSC needs no locks.
- Done when: stress tests with millions of items show no loss, duplication or reordering; swap it in for the mutex queues and the output still matches the single-threaded reference.
Stage 12 - Optimization and benchmarking (Milestone B)
- Build/measure: CPU pinning, preallocated memory pools, cache-friendly structures (e.g. replace std::map price levels with a flat array indexed by tick), removing allocations from the hot path, per-stage and end-to-end latency, p50/p99/p99.9, single- vs three-threaded comparison, reproducible results documented in the repo.
- Learn: measure first (profiler), change one thing at a time, report the machine, compiler flags and method.
- Done when: results can be reproduced from a script and are written down honestly.
5. How the resume bullets map to the work
Resume claim	Where it becomes true
Add/Modify/Cancel/Trade handling, in-memory book	Stages 2-4
Hash maps, arrays, fewer allocations	Stage 3 (hash map), Stage 12 (arrays, pools)
Sequence validation, gap detection, recovery	Stages 5-6
Unit and benchmark tests for correctness and speed	Every stage + Stage 12
Modular pipeline: data, strategy, risk, OMS, gateway	Stages 7-10
SPSC ring buffers, atomics, memory ordering	Stage 11C
CPU affinity, cache-line alignment, fewer allocations	Stages 11C-12
p50 / p99 / p99.9 latency	Stage 12


Only keep a bullet on the resume once the matching stage is finished and measured.
6. Repository layout
common/                      shared types: Order, MarketEvent, ExecutionReport
market-data-feed-handler/    Project 1 (generator, book, feed handler, recovery)
low-latency-tick-to-trade/   Project 2 (strategy, risk, OMS, gateway, SPSC, threads)
integration-tests/           end-to-end and failure-scenario tests
docs/                        architecture notes
scripts/                     build and benchmark scripts
PROJECT_PROGRESS.md          current stage and next task
Only add a folder to CMake when its stage starts, so the build stays green.
7. Open decisions (answer these before the stage that needs them)
1. Quantity meaning in CANCEL/TRADE (Stage 1/2): remaining quantity, or quantity removed/traded?
2. MODIFY priority rules (Stage 4): which modifications keep queue position?
3. Fill model for our orders (Stage 9): fill against the current top of book, partially by displayed size, or by probability? And do our fills affect the market data? (Simplest: no.)
4. Single instrument or multiple (Stage 2): recommended: start with one.
5. Message wire format (Stage 2): text/CSV for readability, or binary for speed. Binary is more realistic; a text mode helps debugging.
6. Test framework (now): plain CHECK macro, Catch2 or GoogleTest. Do not use bare assert, which disappears in Release builds.
8. Build and test
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
(Update this once new targets are added.)