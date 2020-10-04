#ifndef RGRAPH_HPP
#define RGRAPH_HPP

// Louis Jenkins & Benjamin Valpey

#include <string>
#include <functional>
#include "Rideable.hpp"

class RGraph : public Rideable{
public:
    /**
     * Adds an edge to the graph, given two node IDs
     * @param src A pointer to the source node
     * @param dest A pointer to the destination node
     * @return Whether or not adding the edge was successful
     */
    virtual bool add_edge(int src, int dest, int weight) = 0;

    virtual bool has_edge(int v1, int v2) = 0;

    virtual bool set_weight(int src, int dest, int weight) = 0;

    virtual bool set_lbl(int id, int l) = 0;
    /**
     * Removes an edge from the graph. Acquires the unique_lock.
     * @param src The integer id of the source node of the edge.
     * @param dest The integer id of the destination node of the edge
     * @return True if the edge exists
     */
    virtual bool remove_edge(int src, int dest) = 0;
	
    virtual bool clear_vertex(int v) = 0;

    virtual void for_each_edge(int v, std::function<bool(int)> fn) = 0;
};


#endif
