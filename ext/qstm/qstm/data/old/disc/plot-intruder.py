#!/usr/bin/python
import matplotlib
import csv
matplotlib.use('Agg') # This lets it run without an X backend
import matplotlib.pyplot as plt
import numpy as np

ring_x = []
ring_y = []
ring_z = []

qstm_x = []
qstm_y = []
qstm_z = []

mnem_x = []
mnem_y = []
mnem_z = []
font = {'family' : 'sans-serif',
        'size'   : 20}
matplotlib.rc('font', **font)
fname = "intruder"
scale = 100000
linestyles = ['--', '-.','-']
markerstyles = ['^', 'o', '.']
with open(fname+'.csv','r') as csvfile:
	plots = csv.reader(csvfile, delimiter=',')
	for row in plots:
		if row[0] == "pringstm":
			ring_x.append(int(row[1]))
			ring_y.append(float(row[2]))
			ring_z.append(float(row[3]))
		if row[0] == "qstm":
			qstm_x.append(int(row[1]))
			qstm_y.append(float(row[2]))
			qstm_z.append(float(row[3]))
		if row[0] == "mnemosyne":
			mnem_x.append(int(row[1]))
			mnem_y.append(float(row[2]))
			mnem_z.append(float(row[3]))

ring_thpt = [(x/y)/scale for x, y in zip(ring_z, ring_y)]
qstm_thpt = [(x/y)/scale for x, y in zip(qstm_z, qstm_y)]
mnem_thpt = [(x/y)/scale for x, y in zip(mnem_z, mnem_y)]

plt.plot(ring_x, ring_thpt, label='PRingSTM', linestyle=linestyles[0], linewidth=2, marker=markerstyles[0], markersize=10)
plt.plot(qstm_x, qstm_thpt, label='QSTM', linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=10)
plt.plot(mnem_x, mnem_thpt, label='Mnemosyne', linestyle=linestyles[2], linewidth=2, marker=markerstyles[2], markersize=10)

plt.xlabel('Threads')
plt.ylabel('Throughput (x$\mathregular{10^5}$ txn/sec)')

plt.xlim(0,74)
plt.ylim(0,1000000/scale)

plt.xticks((1, 4, 8, 16, 24, 32, 40, 48, 64, 72))

plt.legend(loc='upper right')
plt.tight_layout()
plt.savefig(fname+".png",dpi=300)
