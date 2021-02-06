#!/usr/bin/python
import matplotlib
import csv
matplotlib.use('Agg') # This lets it run without an X backend
import matplotlib.pyplot as plt
import numpy as np

ring_p_x = []
ring_p_y = []

ring_u_x = []
ring_u_y = []

rstm_x = []
rstm_y = []

fname = "rstm_ringsw_low"

with open(fname+'.csv','r') as csvfile:
	plots = csv.reader(csvfile, delimiter=',')
	for row in plots:
		if row[0] == "pinnedring":
			ring_p_x.append(int(row[1]))
			ring_p_y.append(float(row[2]))
		if row[0] == "unpinnedring":
			ring_u_x.append(int(row[1]))
			ring_u_y.append(float(row[2]))
		if row[0] == "ringsw":
			rstm_x.append(int(row[1]))
			rstm_y.append(float(row[2]))

ring_p_thpt = [671088/i for i in ring_p_y]
ring_u_thpt = [671088/i for i in ring_u_y]
rstm_thpt = [671088/i for i in rstm_y]

# Throughput plot
plt.plot(ring_p_x, ring_p_thpt, label='RingSTM Pinned')
plt.plot(ring_u_x, ring_u_thpt, label='RingSTM Unpinned')
plt.plot(rstm_x, rstm_thpt, label='RSTM RingSW')

plt.xlabel('Threads')
plt.ylabel('Throughput (txn/sec)')

plt.title('Non-durable, low, 2x18a, Vacation, 2048')

plt.xlim(0,82)
plt.ylim(0,300000)

plt.xticks((1, 2, 4, 6, 8, 12, 16, 24, 32, 40, 48, 64, 72, 80))

plt.legend(loc='lower right')
#plt.show()
plt.savefig(fname+".png",dpi=300)
