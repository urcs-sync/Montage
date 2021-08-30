#!/bin/bash
# go to where plot.sh locates
cd "$( dirname "${BASH_SOURCE[0]}" )"

# Figure 4
Rscript mt_queue_plotting.R

# Figure 5 and 6
Rscript mt_map_plotting.R

# Figure 7(a)
Rscript sz_queue_plotting.R
# Figure 7(b)
Rscript sz_map_plotting.R

# Figure 8
Rscript sync_map_plotting.R

# Figure 9
Rscript threadcached_plotting.R

# Figure 10
Rscript mt_graph_plotting.R

# Figure 11
Rscript rec_orkut_plotting.R