#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <vector>
#include <algorithm>
#include <fstream>
#include <iostream>

#define MAX_SOCKETS 2

bool core_info_compare(std::pair<long,long> a, std::pair<long,long> b) {
    if (a.first != b.first) return a.first < b.first;
    return a.second < b.second;
}

void get_cpu_info(uint8_t *core_map, int *map_size) {
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[1024];
    char tag[128];
    char value[1024];

    long processor; // core id (with HT)
    long core_id; // physical core id (no HT)
    long physical_id; // socket id
    long cpu_cores; // physical cores per socket

    *map_size = 0;
    std::vector<std::pair<long,long>> sockets[MAX_SOCKETS];

    while (fgets(line, sizeof(line), f) != NULL) {
        if (strlen(line) == 1) continue;
        sscanf(line, "%[^\t:] : %[^\t\n]", tag, value);

        if (strcmp(tag, "core id") == 0) {
            core_id = strtol(value, NULL, 10);
        }
        else if (strcmp(tag, "physical id") == 0) {
            physical_id = strtol(value, NULL, 10);
        }
        else if (strcmp(tag, "processor") == 0) {
            processor = strtol(value, NULL, 10);
        }
        else if (strcmp(tag, "cpu cores") == 0) {
            cpu_cores = strtol(value, NULL, 10);
        }
        else if (strcmp(tag, "flags") == 0) {
            assert(physical_id < MAX_SOCKETS);
            sockets[physical_id].push_back(std::pair<long,long>(core_id, processor));
            *map_size = *map_size + 1;
        }
    }

    fclose(f);

    for (int i = 0; i < MAX_SOCKETS; i++) {
        assert(i < 7); // 3 bits
        std::sort(sockets[i].begin(), sockets[i].end(), core_info_compare);

        // <first, second> = <core_id, processor>
        for (auto core = sockets[i].begin(); core != sockets[i].end(); core++) {
            assert(core->first < 32); // 5 bits
            core_map[core->second] = core->first | (i << 5);
        }

        sockets[i].clear();
    }
}
