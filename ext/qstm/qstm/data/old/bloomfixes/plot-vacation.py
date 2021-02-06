#!/usr/bin/python
import matplotlib
import csv
matplotlib.use('Agg') # This lets it run without an X backend
import matplotlib.pyplot as plt
import numpy as np

ring64_x = []
ring64_y = []

ring8_x = []
ring8_y = []

mnem_x = []
mnem_y = []

font = {'family' : 'sans-serif',
        'size'   : 12}

matplotlib.rc('font', **font)

fname = "test-ringstm-with-better-bloom-intersections-6-18-18"
scale = 100000
linestyles = ['--', '-.','-']
markerstyles = ['^', 'o', '.']
with open(fname+'.csv','r') as csvfile:
	plots = csv.reader(csvfile, delimiter=',')
	for row in plots:
		if row[0] == "ringstm64":
			ring64_x.append(int(row[1]))
			ring64_y.append(float(row[2]))
		if row[0] == "ringstm8":
			ring8_x.append(int(row[1]))
			ring8_y.append(float(row[2]))
		if row[0] == "mnemosyne":
			mnem_x.append(int(row[1]))
			mnem_y.append(float(row[2]))

ring64_thpt = [1000000/scale/i for i in ring64_y]
ring8_thpt = [1000000/scale/i for i in ring8_y]
mnem_thpt = [1000000/scale/i for i in mnem_y]

plt.plot(ring64_x, ring64_thpt, label='PRingSTM-64', linestyle=linestyles[0], linewidth=2, marker=markerstyles[0], markersize=10)
plt.plot(ring8_x, ring8_thpt, label='PRingSTM-8',linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=10)
plt.plot(mnem_x, mnem_thpt, label='Mnemosyne',linestyle=linestyles[2], linewidth=2, marker=markerstyles[2], markersize=10)

plt.xlabel('Threads')
plt.ylabel('Throughput (x$\mathregular{10^5}$ txn/sec)')

plt.xlim(0,74)
plt.ylim(0,400000/scale)

plt.xticks((1, 4, 8, 16, 24, 32, 40, 48, 64, 72))

plt.legend(loc='upper right')
plt.tight_layout()
plt.savefig(fname+".png",dpi=300)


plt.xlim(0,24)
plt.ylim(0,400000/scale)
plt.xticks((1, 2, 4, 8, 12, 16, 24))
plt.savefig(fname+"zoomed"+".png",dpi=300)
