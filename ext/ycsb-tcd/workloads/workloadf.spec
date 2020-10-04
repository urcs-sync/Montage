# Yahoo! Cloud System Benchmark
# Workload F: Read-modify-write workload
#   Application example: user database, where user records are read and modified by the user or to record user activity.
#                        
#   Read/read-modify-write ratio: 50/50
#   Default data size: 1 KB records (10 fields, 100 bytes each, plus key)
#   Request distribution: zipfian

recordcount=2
operationcount=2
workload=com.yahoo.ycsb.workloads.CoreWorkload

readallfields=true

readproportion=1.0
updateproportion=0
scanproportion=0
insertproportion=0

requestdistribution=zipfian

