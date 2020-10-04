#!/bin/python

import sys
import os
import re
import multiprocessing

def work(f):
    output = 'graph_data/' + f[:-4] + '.bin'
    with open('graph_data/' + f) as i:
        inputLines = i.readlines()
        assert len(inputLines) > 0
    with open(output, 'wb') as o:
        for line in inputLines:
            m = re.match('([0-9]+)\t([0-9]+)', line)
            x,y = m.groups()
            o.write(int(x).to_bytes(4, sys.byteorder))
            o.write(int(y).to_bytes(4, sys.byteorder))


pool = multiprocessing.Pool(multiprocessing.cpu_count())
for f in os.listdir('graph_data/'):
    tmp = re.findall("orkut-edge-list_[0-9]+.txt", f)
    if len(tmp) != 0:
        pool.apply_async(work, (f,))
pool.close()
pool.join()