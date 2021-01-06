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

    /**
     * Removes an edge from the graph. Acquires the unique_lock.
     * @param src The integer id of the source node of the edge.
     * @param dest The integer id of the destination node of the edge
     * @return True if the edge exists
     */
    virtual bool remove_edge(int src, int dest) = 0;

    /**
     * @brief Removes any edge from the selected vertex.
     * 
     * @param src The integer id of the source node.
     * @return true If an edge can be removed.
     * @return false If an edge cannot be removed, i.e. non-allocated vertex
     */
    virtual bool remove_any_edge(int src) = 0;
    
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
     * @brief Calls function fn on vertex of each outgoing edge
     * 
     * Calls the function fn on the vertex of each outgoing edge. This is to implement something
     * similar to a for-each loop, but in an implementation-defined way. Will run over all outgoing
     * edges or until fn returns 'false' indicating no further processing is required. This acquires the
     * lock on vid, and so users should be cautious of deadlock.
     * 
     * @param vid Identifier of the vertex
     * @param fn Predicate function that performs work on outgoing edge and returns whether or not to continue processing.
     */
    virtual void for_each_outgoing(int vid, std::function<bool(int)> fn) = 0;

    /**
     * @brief Calls function fn on vertex of each outgoing edge
     * 
     * Calls the function fn on the vertex of each outgoing edge. This is to implement something
     * similar to a for-each loop, but in an implementation-defined way. Will run over all outgoing
     * edges or until fn returns 'false' indicating no further processing is required. This acquires the
     * lock on vid, and so users should be cautious of deadlock.
     * 
     * @param vid Identifier of the vertex
     * @param fn Predicate function that performs work on outgoing edge and returns whether or not to continue processing.
     */
    virtual void for_each_incoming(int vid, std::function<bool(int)> fn) = 0;

    /**
     * @brief Obtains statistics including (|V|, |E|, average degree, vertex degrees, vertex degrees length)
     * 
     * @return std::tuple<int, int, double, int *> Tuple of |V|, |E|, average degree, and histogram
     */
    virtual std::tuple<int, int, double, int *, int> grab_stats() = 0; 
};


#endif
