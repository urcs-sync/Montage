This folder contains infrastructures for Montage.

## Environment variables and values
usage: add argument:
```
-d<env>=<val>
```
to command line.

* `PersistStrat`: specify persist strategies
    * `DirWB`: directly write back every update to persistent blocks, and only issue an `sfence` on epoch advance
    * `PerEpoch`: keep to-be-persisted records of _whole epochs_ on a per-cache-line basis and flush them together
        * `Persister` = {`PerThreadWait`, `Advancer`}
        * `Container` = {`CircBuffer`, `Vector`, `HashSet`}
    * `BufferedWB`: keep to-be-persisted records of an epoch in a fixed-sized buffer and dump a (older) portion of them when it's full
        * `Persister` = {`PerThreadWait`, `PerThreadBusy`, `Worker`}
        * `BufferSize`
        * `DumpSize`
    * `No`: No persistence operations. NOTE: epoch advancing and all epoch-related persistency will be shut down. Overrides other environments.
* `TransTracker`: specify the type of active (data structure and bookkeeping) transaction tracker that prevents epoch advances if there are active transactions
    * `AtomicCounter`: a global atomic int active transaction counter for each epoch. lock-prefixed instruction on each update.
    * `ActiveThread`: per-thread true-false indicator of active threads on each recent epoch
    * `CurrEpoch`: per-thread indicator of current epoch on the thread
* `EpochLength`: specify epoch length.
* `EpochLengthUnit`: specify epoch length unit: `Second` (default) `Millisecond` or `Microsecond`.
