#!/usr/bin/python
import matplotlib
import csv
matplotlib.use('Agg') # This lets it run without an X backend
import matplotlib.pyplot as plt
import numpy as np

ring_x = []
ring_y = []

q_x = []
q_y = []

#fname = "vacation_durable_low_2048_unpinned"
#fname = "vacation_durable_low_2048_pinned"
#fname = "vacation_volatile_low_2048_unpinned"
fname = "vacation_volatile_low_2048_pinned"

with open(fname+'.csv','r') as csvfile:
	plots = csv.reader(csvfile, delimiter=',')
	for row in plots:
		if row[0] == "ringstm":
			ring_x.append(int(row[1]))
			ring_y.append(float(row[2]))
		if row[0] == "qstm":
			q_x.append(int(row[1]))
			q_y.append(float(row[2]))

ring_thpt = [671088/i for i in ring_y]
q_thpt = [671088/i for i in q_y]

# Time plot
#plt.plot(ring_x, ring_y, label='RingSTM')
#plt.plot(q_x, q_y, label='QSTM')

# Throughput plot
plt.plot(ring_x, ring_thpt, label='RingSTM')
plt.plot(q_x, q_thpt, label='QSTM')

plt.xlabel('Threads')
#plt.ylabel('Seconds')
plt.ylabel('Throughput (txn/sec)')

#plt.title('Durable, low, 2x18a, Vacation, 2048, unpinned')
#plt.title('Durable, low, 2x18a, Vacation, 2048, pinned')
#plt.title('Non-durable, low, 2x18a, Vacation, 2048, unpinned')
plt.title('Non-durable, low, 2x18a, Vacation, 2048, pinned')

plt.xlim(0,82)
#plt.ylim(0,30)
plt.ylim(0,200000)

plt.xticks((1, 2, 4, 6, 8, 12, 16, 24, 32, 40, 48, 64, 72, 80))

plt.legend(loc='lower right')
#plt.show()
plt.savefig(fname+".png",dpi=300)
