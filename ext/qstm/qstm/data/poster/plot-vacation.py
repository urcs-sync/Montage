#!/usr/bin/python
import matplotlib
import csv
matplotlib.use('Agg') # This lets it run without an X backend
import matplotlib.pyplot as plt
import numpy as np

qstm2k_x = []
qstm2k_y = []

qstm_x = []
qstm_y = []

mnem_x = []
mnem_y = []

font = {'family' : 'sans-serif',
        'size'   : 16}

matplotlib.rc('font', **font)

fname = "vacation"
scale = 100000
linestyles = ['--', '-.','-']
markerstyles = ['^', 'o', '.']
with open(fname+'.csv','r') as csvfile:
	plots = csv.reader(csvfile, delimiter=',')
	for row in plots:
		if row[0] == "qstm2k":
			qstm2k_x.append(int(row[1]))
			qstm2k_y.append(float(row[2]))
		if row[0] == "qstm8k":
			qstm_x.append(int(row[1]))
			qstm_y.append(float(row[2]))
		if row[0] == "mnemosyne":
			mnem_x.append(int(row[1]))
			mnem_y.append(float(row[2]))

qstm2k_thpt = [1000000/scale/i for i in qstm2k_y]
qstm_thpt = [1000000/scale/i for i in qstm_y]
mnem_thpt = [1000000/scale/i for i in mnem_y]

plt.plot(qstm2k_x, qstm2k_thpt, label='QSTM-2k',linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=8)
plt.plot(qstm_x, qstm_thpt, label='QSTM-8k',linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=8)
plt.plot(mnem_x, mnem_thpt, label='Mnemosyne',linestyle=linestyles[2], linewidth=2, marker=markerstyles[2], markersize=10)

plt.xlabel('Threads')
plt.ylabel('Throughput (x$\mathregular{10^5}$ txn/sec)')

plt.xlim(0,74)
plt.ylim(0,1000000/scale)

plt.xticks((1, 4, 8, 16, 24, 32, 40, 48, 64, 72))

#plt.title("STAMP Vacation")

#plt.legend(loc='center right')
plt.tight_layout()
plt.savefig(fname+".png",dpi=300)
