# Storage Engine: WAL, Transactions and Crash Recovery

Layer owner: Teammate A. Code lives in `storage_engine/`.

## Overview
`KVStore` is a hash-based key-value store (key `uint64_t`, value `std::string`, max 1000 bytes)
built on the BufferPool. Each page is a bucket; overflow uses linear probing.
It is single-threaded: at most one transaction is active at a time.

## Write-ahead log (`wal.hpp`)
- The log is a separate append-only file. It does not use the Pager; records are written
  with `write()` and made durable with `fsync()`.
- Records are **logical**: a CHANGE record stores the key plus the value before and after
  the change, not a page id.

| Operation  | hasBefore | hasAfter |
|------------|-----------|----------|
| insert new | false     | true     |
| overwrite  | true      | true     |
| remove     | true      | false    |

- Record types: BEGIN, COMMIT, ABORT, CHANGE, CHECKPOINT. Every record gets a log
  sequence number (LSN) from `Wal::append`.
- Records are checksummed. On open, a half-written record at the end of the file
  (a torn write from a crash) is detected and cut off.
- **Redo** applies the `after` value; **undo** applies the `before` value. Both are
  idempotent, so recovery can safely run more than once.

## Transactions
- `begin()` / `commit()` / `abort()` give explicit transactions.
  `insert()` and `remove()` outside a transaction run as autocommit (BEGIN, CHANGE, COMMIT).
- Write-ahead rule: the CHANGE record is flushed to disk before the page is touched.
- `commit()` appends COMMIT and calls `flush()` (fsync); the transaction is durable once
  it returns.
- `abort()` undoes the transaction's changes newest first. Each undo is logged as a
  compensation CHANGE record (before/after swapped) and the log is flushed before any
  page is touched; then ABORT is logged.
- Misuse (commit with no transaction, nested begin, WAL not enabled) throws.

## Checkpoint
`checkpoint()` flushes every dirty page to the data file, then empties the log down to a
single CHECKPOINT record. Not allowed while a transaction is active.

## Crash recovery
Call `recover()` once at startup, right after `enableWal()`. It runs three phases over the
log records since the last checkpoint:
1. **Analysis**: find which transactions committed, aborted, or were still running
   at the crash ("losers").
2. **Redo**: re-apply the logged changes (repeating history).
3. **Undo**: roll back the changes of loser transactions.

It finishes with a checkpoint, and returns a `RecoveryReport`
(`recordsScanned`, `committedTxns`, `abortedTxns`, `loserTxns`, `changesRedone`,
`changesUndone`). Running `recover()` again is harmless.

## Testing
- `recovery_tests`: simulates a crash with `fork()` + `_exit()` (no cleanup), then recovers.
  Covers committed-data redo, rollback of an uncommitted transaction, checkpoint followed
  by a crash, and repeated recovery.
- `crash_demo`: a real `kill -9` test.

```bash
./build/crash_demo run &          # commits transactions of 2 keys each, writes ACK lines
kill -9 <pid>                     # crash it at a random moment
./build/crash_demo verify         # recover, then check every acknowledged transaction
```

`verify` checks that every acknowledged transaction survived with both keys, and that the
in-flight transaction is all-or-nothing. In five runs with kills at different times it
passed every time, exercising both redo-only and undo outcomes.

## Limits
- Verified against **process** crashes. Commit calls `fsync` on the log before returning,
  but power-loss behaviour was not tested.
- A crash *during* recovery itself has not been tested (the design is idempotent, so it
  should be safe).
- Single-threaded, one active transaction at a time.
