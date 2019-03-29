/**
 * @file contig_graph.h
 * @brief ContigGraph Class.
 * @author Yu Peng (ypeng@cs.hku.hk)
 * @version 1.0.0
 * @date 2011-08-16
 */

#ifndef __GRAPH_CONTIG_GRAPH_H_

#define __GRAPH_CONTIG_GRAPH_H_

#include <algorithm>
#include <deque>
#include <map>

#include "bit_operation.h"
#include "idba/kmer.h"
#include "idba/hash_map.h"
#include "idba/contig_graph_path.h"
#include "idba/contig_graph_vertex.h"
#include "idba/contig_info.h"
#include "idba/hash_graph.h"
#include "idba/sequence.h"


/**
 * @brief It is compact version de Bruijn graph in which each vertex is a contig 
 * and each edge between contigs means they are connected in de Bruijn graph.
 */
class ContigGraph
{
public:
    explicit ContigGraph(uint32_t kmer_size = 0)
        : num_edges_(0), kmer_size_(kmer_size)
    {}

    ~ContigGraph() { clear(); }

    void Initialize(const std::deque<Sequence> &contigs, const std::deque<ContigInfo> &contig_infos);

    void Refresh();
    void RefreshVertices();
    void RefreshEdges();

    void AddEdge(ContigGraphVertexAdaptor from, ContigGraphVertexAdaptor to)
    {
        from.out_edges().Add(to.contig()[kmer_size_-1]);
        from.ReverseComplement();
        to.ReverseComplement();
        std::swap(from, to);
        from.out_edges().Add(to.contig()[kmer_size_-1]);
    }

    void RemoveEdge(ContigGraphVertexAdaptor current, int x)
    {
        current.out_edges().Remove(x);
        ContigGraphVertexAdaptor next = GetNeighbor(current, x);
        next.ReverseComplement();
        next.out_edges().Remove(3 - current.contig()[0]);
    }

    void ClearStatus();

    void MergeSimplePaths();

    int64_t Trim(int min_length);
    int64_t Trim(int min_length, double min_cover);

    int64_t RemoveDeadEnd(int min_length);
    int64_t RemoveBubble();
    
    double IterateCoverage(int min_length, double min_cover, double max_cover, double factor = 1.1);

    bool RemoveLowCoverage(double min_cover, int min_length);
    bool RemoveLocalLowCoverage(double min_cover, int min_length, double ratio);
    bool RemoveComponentLowCoverage(double min_cover, int min_length, double ratio, int max_component_size);

    double LocalCoverage(ContigGraphVertexAdaptor current, int region_length);
    double LocalCoverageSingle(ContigGraphVertexAdaptor current, int region_length, double &num_count, int &num_kmer);

    int64_t Assemble(std::deque<Sequence> &contigs, std::deque<ContigInfo> &contig_infos);

    ContigGraphVertexAdaptor GetNeighbor(const ContigGraphVertexAdaptor &current, int x)
    {
        IdbaKmer kmer = current.end_kmer(kmer_size_);
        kmer.ShiftAppend(x);
        return FindVertexAdaptorByBeginIdbaKmer(kmer);
    }
    
    void GetNeighbors(const ContigGraphVertexAdaptor &current, std::deque<ContigGraphVertexAdaptor> &neighbors)
    {
        neighbors.clear();
        for (int x = 0; x < 4; ++x)
        {
            if (current.out_edges()[x])
                neighbors.push_back(GetNeighbor(current, x));
        }
    }

    bool IsConverged(ContigGraphVertexAdaptor current);
    int64_t SplitBranches();
    void GetComponents(std::deque<std::deque<ContigGraphVertexAdaptor> > &components, std::deque<std::string> &component_strings);

    std::deque<ContigGraphVertex> &vertices() { return vertices_; }
    const std::deque<ContigGraphVertex> &vertices() const { return vertices_; }

    void swap(ContigGraph &contig_graph)
    {
        begin_kmer_map_.swap(contig_graph.begin_kmer_map_);
        vertices_.swap(contig_graph.vertices_);
        std::swap(num_edges_, contig_graph.num_edges_);
        std::swap(kmer_size_, contig_graph.kmer_size_);
    }

    uint32_t kmer_size() const { return kmer_size_; }
    void set_kmer_size(uint32_t kmer_size) { kmer_size_ = kmer_size; }

    uint64_t num_vertices() const { return vertices_.size(); }
    uint64_t num_edges() const { return num_edges_; }

    void clear()
    {
        num_edges_ = 0;
        vertices_.clear();
        begin_kmer_map_.clear();
        in_kmer_count_table_.clear();
    }

private:
    ContigGraph(const ContigGraph &);
    const ContigGraph &operator =(const ContigGraph &);

    static bool CompareContigLength(const ContigGraphVertex &x, const ContigGraphVertex &y)
    { return x.contig_size() > y.contig_size(); }

    static bool CompareContigCoverage(const ContigGraphVertexAdaptor &x, const ContigGraphVertexAdaptor &y)
    { return x.coverage() > y.coverage(); }

    static double GetSimilarity(ContigGraphVertexAdaptor &x, ContigGraphVertexAdaptor &y)
    {
        Sequence a = x.contig();
        Sequence b = y.contig();
        return GetSimilarity(a, b);
    }
    static double GetSimilarity(const Sequence &x, const Sequence &y);

    void BuildBeginIdbaKmerMap();

    bool GetNextVertexAdaptor(ContigGraphVertexAdaptor &current, ContigGraphVertexAdaptor &next)
    {
        if (current.out_edges().size() != 1)
            return false;

        next = GetNeighbor(current, bit_operation::BitToIndex(current.out_edges()));
        return next.in_edges().size() == 1 && !(next.contig_size() == kmer_size_ && next.contig().IsPalindrome());
    }

    bool IsLoop(const ContigGraphPath &path, const ContigGraphVertexAdaptor &next)
    { return path.front().id() == next.id(); }

    bool IsPalindromeLoop(const ContigGraphPath &path, const ContigGraphVertexAdaptor &next)
    { return path.back().id() == next.id(); }

    ContigGraphVertexAdaptor FindVertexAdaptorByBeginIdbaKmer(const IdbaKmer &begin_kmer)
    {
        IdbaKmer key = begin_kmer.unique_format();

        HashMap<IdbaKmer, uint32_t>::iterator iter = begin_kmer_map_.find(key);
        if (iter != begin_kmer_map_.end())
        {
            ContigGraphVertexAdaptor current(&vertices_[iter->second]);
            if (current.begin_kmer(kmer_size_) == begin_kmer)
                return current;
            current.ReverseComplement();
            if (current.begin_kmer(kmer_size_) == begin_kmer)
                return current;
        }

        return ContigGraphVertexAdaptor();
    }

    ContigGraphVertexAdaptor GetBeginVertexAdaptor(std::deque<ContigGraphVertexAdaptor> &component)
    {
        ContigGraphVertexAdaptor begin;
        for (unsigned i = 0; i < component.size(); ++i)
        {
            if (component[i].in_edges() == 0)
            {
                if (begin.is_null())
                    begin = component[i];
                else
                    return ContigGraphVertexAdaptor(NULL);
            }
        }
        return begin;
    }

    ContigGraphVertexAdaptor GetEndVertexAdaptor(std::deque<ContigGraphVertexAdaptor> &component)
    {
        ContigGraphVertexAdaptor end;
        for (unsigned i = 0; i < component.size(); ++i)
        {
            if (component[i].out_edges() == 0)
            {
                if (end.is_null())
                    end = component[i];
                else
                    return ContigGraphVertexAdaptor(NULL);
            }
        }
        return end;
    }

    bool IsValid(std::deque<ContigGraphVertexAdaptor> &component);
    bool CycleDetect(ContigGraphVertexAdaptor current, std::map<int, int> &status);
    void FindLongestPath(std::deque<ContigGraphVertexAdaptor> &component, ContigGraphPath &path);
    void TopSort(std::deque<ContigGraphVertexAdaptor> &component, std::deque<ContigGraphVertexAdaptor> &order);
    void TopSortDFS(std::deque<ContigGraphVertexAdaptor> &order, ContigGraphVertexAdaptor current, std::map<int, int> &status);
    int GetDepth(ContigGraphVertexAdaptor current, int length, int &maximum, int min_length);
    double FindSimilarPath(ContigGraphVertexAdaptor target, ContigGraphPath &path, int &time);

    HashMap<IdbaKmer, uint32_t> begin_kmer_map_;
    std::deque<ContigGraphVertex> vertices_;
    uint64_t num_edges_;
    uint32_t kmer_size_;

    HashMap<IdbaKmer, uint32_t> in_kmer_count_table_;
};

#endif
