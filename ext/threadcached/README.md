# Threadcached
Threadcache is a high performance user-space, shared, key/value cache
store. It is a modification of upstream memcached using work from
Mohammad Hedayati(Hodor) & Wentao Cai(RPMalloc).

# Make
```
cd hodor/libhodor
make
cd ../../ralloc/test
make
cd ../..
make
```


# Notes
  - Turn hodor off if you're not using an OS with Hodor. Do this by
  commenting out line 21 in Makefile.

