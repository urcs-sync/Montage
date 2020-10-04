This folder contains data structures persisted by the Epoch System.

TODO:

Trivial:
    [ ] Reorganize DSs, separating transient, persistent, and previous work to different folders

Implementation:
    [ ] Single-threaded DSs
        [ ] Queue
            [ ] Transient
            [X] Existing Persistent : Pronto
            [ ] Existing Persistent : MOD
            [ ] Montage
        [ ] Map
            [ ] Transient
            [X] Existing Persistent : Pronto
            [ ] Existing Persistent : MOD
            [ ] Montage
    [ ] Lock-based DSs
        [ ] Tree
            [ ] Transient
            [X] Montage : Unbalanced HOH Tree
        [ ] Hash Table
            [ ] Transient
            [X] Existing Persistent : Pronto
            [X] Existing Persistent : modified Dali
            [X] Montage : list-based HOH & per bucket
    [ ] Lock-free DSs
        [X] Queue
            [X] Transient : MS Q
            [X] Existing Persistent : Friedman's Q
            [X] Montage : MS Q
        [ ] Tree
            [X] Transient : Natarajan's T
            [ ] Existing Persistent : strictly DL?
            [X] Montage : Najarajan's T
        [X] Hash Table
            [X] Transient : list-based HT
            [ ] Existing Persistent : strictly DL?
            [X] Montage : list-based HT

Experiment:
    [X] Single threaded testing (Q, Tree, and HT): Transient vs Montage vs Pronto for various sizes
    [ ] KV Store (Hash Tables) : Transient vs Montage vs Pronto vs Dali for different #threads
        [ ] microbenchmarking 
        [X] YCSB
    [ ] Practice of Lock-free DSs made persistent : Transient vs Montage vs strictly durably linearizable
        (Thought) Maybe we don't need to include all LF DSs; instead only Q would be sufficient
    [ ] Recovery cost