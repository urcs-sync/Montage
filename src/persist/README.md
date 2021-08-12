This folder contains infrastructures for Montage.

## Environment variables and values
usage: add argument:
```
-d<env>=<val>
```
to command line.

### Montage configurations:

* `PersistStrat`: specify persist strategies
    * `DirWB`: directly write back every update to persistent blocks, and only issue an `sfence` on epoch advance
    * `BufferedWB`: keep to-be-persisted records of an epoch in a fixed-sized buffer and dump a (older) portion of them when it's full
        * `BufferSize`: change the size of write-back buffer on each thread
    * `No`: No persistence operations. NOTE: epoch advancing and all epoch-related persistency will be shut down. Overrides other environments
* `TransTracker`: specify the type of active (data structure and bookkeeping) transaction tracker that prevents epoch advances if there are active transactions
    * `AtomicCounter`: a global atomic int active transaction counter for each epoch. lock-prefixed instruction on each update.
    * `ActiveThread`: per-thread true-false indicator of active threads on each recent epoch
    * `CurrEpoch`: per-thread indicator of current epoch on the thread
* `PersistTracker`: specify the data structure used to coordinate cache line writes-back among sync() participants
    * `IncreasingMindicator`: a (simplified) variant of Mindicator, with which every thread needs to check on every epoch for writes-back. Tend to be faster to access
    * `Mindicator`: original Mindicator. If a thread doesn't have anything to persist in an epoch, it will be skipped. Slower to access
* `EpochLength`: specify epoch length.
* `EpochLengthUnit`: specify epoch length unit: `Second` (default) `Millisecond` or `Microsecond`.

### SyncTest:

* `SyncFreq`: The frequency of sync operation. On average one sync per x operations. Default is 5.