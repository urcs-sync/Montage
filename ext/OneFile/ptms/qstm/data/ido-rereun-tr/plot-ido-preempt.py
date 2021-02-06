#!/usr/bin/python
import matplotlib
import csv
matplotlib.use('Agg') # This lets it run without an X backend
import matplotlib.pyplot as plt
import numpy as np

linestyles = ['--', '-.','-']
markerstyles = ['^', 'o', '.']
##########################
# Stack, List, Queue, Map
##########################

qstm2k_x = []
qstm2k_y = []

qstm_x = []
qstm_y = []

mnem_x = []
mnem_y = []
font = {'family' : 'sans-serif',
        'size'   : 20}
matplotlib.rc('font', **font)
fname = "stack"
scale = 100000
with open(fname+'-preempt.csv','r') as csvfile:
	plots = csv.reader(csvfile, delimiter=',')
	for row in plots:
		if row[0] == "qstm2048":
			qstm2k_x.append(int(row[1]))
			qstm2k_y.append(float(row[2])/scale)
		if row[0] == "qstm8":
			qstm_x.append(int(row[1]))
			qstm_y.append(float(row[2])/scale)
		if row[0] == "mnem":
			mnem_x.append(int(row[1]))
			mnem_y.append(float(row[2])/scale)

plt.plot(qstm2k_x, qstm2k_y, label='QSTM-2k', linestyle=linestyles[0], linewidth=2, marker=markerstyles[0], markersize=10)
plt.plot(qstm_x, qstm_y, label='QSTM-8', linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=10)
plt.plot(mnem_x, mnem_y, label='Mnemosyne', linestyle=linestyles[2], linewidth=2, marker=markerstyles[2], markersize=10)

plt.xlabel('Threads')
plt.ylabel('Throughput (x$\mathregular{10^5}$ txn/sec)')

plt.xlim(0,74)
plt.ylim(0,1900000/scale)

plt.xticks((1, 4, 8, 16, 24, 32, 40, 48, 64, 72))

plt.legend(loc='upper right')
plt.tight_layout()
plt.savefig(fname+"-preempt.png",dpi=300)



plt.clf()
plt.cla()
plt.close()


qstm2k_x = []
qstm2k_y = []

qstm_x = []
qstm_y = []

mnem_x = []
mnem_y = []

fname = "list"
scale = 10000
with open(fname+'-preempt.csv','r') as csvfile:
	plots = csv.reader(csvfile, delimiter=',')
	for row in plots:
		if row[0] == "qstm2048":
			qstm2k_x.append(int(row[1]))
			qstm2k_y.append(float(row[2])/scale)
		if row[0] == "qstm1024":
			qstm_x.append(int(row[1]))
			qstm_y.append(float(row[2])/scale)
		if row[0] == "mnem":
			mnem_x.append(int(row[1]))
			mnem_y.append(float(row[2])/scale)

plt.plot(qstm2k_x, qstm2k_y, label='QSTM-2k', linestyle=linestyles[0], linewidth=2, marker=markerstyles[0], markersize=10)
plt.plot(qstm_x, qstm_y, label='QSTM-1k', linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=10)
plt.plot(mnem_x, mnem_y, label='Mnemosyne', linestyle=linestyles[2], linewidth=2, marker=markerstyles[2], markersize=10)

plt.xlabel('Threads')
plt.ylabel('Throughput (x$\mathregular{10^4}$ txn/sec)')

plt.xlim(0,74)
plt.ylim(0,500000/scale)

plt.xticks((1, 4, 8, 16, 24, 32, 40, 48, 64, 72))

plt.legend(loc='upper right')
plt.tight_layout()
plt.savefig(fname+"-preempt.png",dpi=300)



plt.clf()
plt.cla()
plt.close()

qstm2k_x = []
qstm2k_y = []

qstm_x = []
qstm_y = []

mnem_x = []
mnem_y = []

fname = "queue"
scale = 100000
with open(fname+'-preempt.csv','r') as csvfile:
	plots = csv.reader(csvfile, delimiter=',')
	for row in plots:
		if row[0] == "qstm2048":
			qstm2k_x.append(int(row[1]))
			qstm2k_y.append(float(row[2])/scale)
		if row[0] == "qstm32":
			qstm_x.append(int(row[1]))
			qstm_y.append(float(row[2])/scale)
		if row[0] == "mnem":
			mnem_x.append(int(row[1]))
			mnem_y.append(float(row[2])/scale)

plt.plot(qstm2k_x, qstm2k_y, label='QSTM-2k', linestyle=linestyles[0], linewidth=2, marker=markerstyles[0], markersize=10)
plt.plot(qstm_x, qstm_y, label='QSTM-32', linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=10)
plt.plot(mnem_x, mnem_y, label='Mnemosyne', linestyle=linestyles[2], linewidth=2, marker=markerstyles[2], markersize=10)

plt.xlabel('Threads')
plt.ylabel('Throughput (x$\mathregular{10^5}$ txn/sec)')

plt.xlim(0,74)
plt.ylim(0,3800000/scale)

plt.xticks((1, 4, 8, 16, 24, 32, 40, 48, 64, 72))

plt.legend(loc='upper right')
plt.tight_layout()
plt.savefig(fname+"-preempt.png",dpi=300)



plt.clf()
plt.cla()
plt.close()


qstm2k_x = []
qstm2k_y = []

qstm_x = []
qstm_y = []

mnem_x = []
mnem_y = []

fname = "map"
scale = 1000000
with open(fname+'-preempt.csv','r') as csvfile:
	plots = csv.reader(csvfile, delimiter=',')
	for row in plots:
		if row[0] == "qstm2048":
			qstm2k_x.append(int(row[1]))
			qstm2k_y.append(float(row[2])/scale)
		if row[0] == "qstm128":
			qstm_x.append(int(row[1]))
			qstm_y.append(float(row[2])/scale)
		if row[0] == "mnem":
			mnem_x.append(int(row[1]))
			mnem_y.append(float(row[2])/scale)

plt.plot(qstm2k_x, qstm2k_y, label='QSTM-2k', linestyle=linestyles[0], linewidth=2, marker=markerstyles[0], markersize=10)
plt.plot(qstm_x, qstm_y, label='QSTM-128', linestyle=linestyles[1], linewidth=2, marker=markerstyles[1], markersize=10)
plt.plot(mnem_x, mnem_y, label='Mnemosyne', linestyle=linestyles[2], linewidth=2, marker=markerstyles[2], markersize=10)

plt.xlabel('Threads')
plt.ylabel('Throughput (x$\mathregular{10^6}$ txn/sec)')

plt.xlim(0,74)
plt.ylim(0,22000000/scale)

plt.xticks((1, 4, 8, 16, 24, 32, 40, 48, 64, 72))

plt.legend(loc='upper right')
plt.tight_layout()
plt.savefig(fname+"-preempt.png",dpi=300)




