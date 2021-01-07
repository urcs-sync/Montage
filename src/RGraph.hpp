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

    virtual bool add_vertex(int vid) = 0;

    virtual bool has_edge(int v1, int v2) = 0;

    /**
     * Removes an edge from the graph. Acquires the unique_lock.
     * @param src The integer id of the source node of the edge.
     * @param dest The integer id of the destination node of the edge
     * @return True if the edge exists
     */
    virtual bool remove_edge(int src, int dest) = 0;
    
    /**
     * @brief Removes vertex from graph, along with the incoming and outgoing edges.
     * 
     * Removes vertex from graph, along with the incoming and outgoing edges. The vertex
     * will be deleted, and so any subsequent calls to remove this vertex will return false,
     * and any calls to add an edge will create this vertex anew.
     * 
     * @param vid Identifier of the vertex to be removed.
     * @return true Vertex was removed.
     * @return false Vertex was already removed from the map.
     */
    virtual bool remove_vertex(int vid) = 0;
 
    /**
     * @brief Obtains statistics including (|V|, |E|, average degree, vertex degrees, vertex degrees length)
     * 
     * @return std::tuple<int, int, double, int *> Tuple of |V|, |E|, average degree, and histogram
     */
    virtual std::tuple<int, int, double, int *, int> grab_stats() = 0; 
};


#endif
