#!/usr/bin/python
import matplotlib
import csv
matplotlib.use('Agg') # This lets it run without an X backend
import matplotlib.pyplot as plt
import numpy as np

qstm2k_x = []
qstm2k_y = []
qstm2k_z = []

qstm128_x = []
qstm128_y = []
qstm128_z = []

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
		if row[0] == "qstm2k":
			qstm2k_x.append(int(row[1]))
			qstm2k_y.append(float(row[2]))
			qstm2k_z.append(float(row[3]))
		if row[0] == "qstm128":
			qstm128_x.append(int(row[1]))
			qstm128_y.append(float(row[2]))
			qstm128_z.append(float(row[3]))
		if row[0] == "mnemosyne":
			mnem_x.append(int(row[1]))
			mnem_y.append(float(row[2]))
			mnem_z.append(float(row[3]))

qstm2k_thpt = [(x/y)/scale for x, y in zip(qstm2k_z, qstm2k_y)]
qstm128_thpt = [(x/y)/scale for x, y in zip(qstm128_z, qstm128_y)]
mnem_thpt = [(x/y)/scale for x, y in zip(mnem_z, mnem_y)]

#plt.plot(qstm2k_x, qstm2k_thpt, label='QSTM-2k', linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=10)
plt.plot(qstm128_x, qstm128_thpt, label='QSTM-128', linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=10)
plt.plot(mnem_x, mnem_thpt, label='Mnemosyne', linestyle=linestyles[2], linewidth=2, marker=markerstyles[2], markersize=10)

plt.xlabel('Threads')
plt.ylabel('Throughput (x$\mathregular{10^5}$ txn/sec)')

plt.xlim(0,74)
plt.ylim(0,1100000/scale)

plt.xticks((1, 4, 8, 16, 24, 32, 40, 48, 64, 72))

plt.legend(loc='upper right')
plt.tight_layout()
plt.savefig(fname+"-preempt.png",dpi=300)
