#pragma once

#include <string>
#include <unordered_set>
#include <boost/container/static_vector.hpp>
#include <boost/graph/adjacency_list.hpp>

#include "shader_recompiler/instruction.h"
#include "shader_recompiler/instruction_iterator.h"

namespace Shader::Gcn {

struct GcnBlockTerminator {
    enum Kind {
        Unconditional,
        Conditional,
        Sink,
    };

    Kind kind;

    // A hack, this should be static_vector<GcnCfgVertex, 2>
    // but that will produce a loop dependency,
    // since vertex_descriptor is just an integer,
    // we just choose the underlying type.
    // Note that we can only do so when we choose
    // vecS as the vertex container.

    // We guarantee that successors[0] is true label
    // and successors[1] is false label
    boost::container::static_vector<size_t, 2> successors;
};

struct GcnBasicBlock {
    u32 pcBegin;
    u32 pcEnd; // point to next instruction right after this block
    GcnBlockTerminator terminator;
    std::vector<size_t> predecessors;
    GcnInstructionList insList;
};

using GcnControlFlowGraph =
    boost::adjacency_list<boost::hash_setS, boost::vecS, boost::bidirectionalS, GcnBasicBlock>;
using GcnCfgVertex = GcnControlFlowGraph::vertex_descriptor;
using GcnCfgEdge = GcnControlFlowGraph::edge_descriptor;
using GcnAdjacencyIterator = boost::graph_traits<GcnControlFlowGraph>::adjacency_iterator;

/**
 * \brief Control flow graph pass
 *
 * Generate the CFG of the shader module.
 */
class GcnCfgPass : public GcnInstructionIterator {
public:
    GcnCfgPass();
    virtual ~GcnCfgPass();

    const GcnControlFlowGraph& generateCfg(const GcnInstructionList& insList);

    static std::string dumpDot(const GcnControlFlowGraph& cfg);

private:
    void generateLabels(const GcnInstructionList& insList);
    void generateVertices(const GcnInstructionList& insList);
    void generateEdges();
    void generateTerminators();
    void generatePredecessors();

    std::optional<GcnControlFlowGraph::vertex_descriptor> findVertex(u32 pc);

    GcnBlockTerminator::Kind getTerminatorKind(const GcnShaderInstruction& ins);

private:
    std::unordered_set<u32> m_labels;
    GcnControlFlowGraph m_cfg;
};
} // namespace Shader::Gcn
