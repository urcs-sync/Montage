# Yahoo! Cloud System Benchmark
# Workload A: Update heavy workload
#   Application example: Session store recording recent actions
#                        
#   Read/update ratio: 50/50
#   Request distribution: zipfian

recordcount=1000000
operationcount=5000000
workload=com.yahoo.ycsb.workloads.CoreWorkload

readallfields=true

readproportion=0.50
updateproportion=0.50
scanproportion=0
insertproportion=0

requestdistribution=zipfian

# custom
# fieldcount=1
# fieldlength=1000
