#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

TEST(ValTopology, RandomTopologiesSatisfyEveryInvariant) {
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        RandomTopologyConfig cfg;
        cfg.minNodes = 2;
        cfg.maxNodes = 9;
        const Topology t = generateRandomTopology(cfg, seed);
        ASSERT_VALID(checkTopology(t));
        EXPECT_TRUE(t.isConnected()) << "the generator promises a weakly connected topology";
        EXPECT_TRUE(t.validate().valid()) << "the production validator agrees there is no error";
        EXPECT_GE(t.linkCount(), t.nodeCount() - 1);
    }
}

TEST(ValTopology, LargerRandomTopologiesSatisfyEveryInvariant) {
    for (std::uint64_t seed = 1; seed <= 15; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        RandomTopologyConfig cfg;
        cfg.minNodes = 30;
        cfg.maxNodes = 80;
        cfg.extraLinksPerNode = 1.5;
        cfg.parallelProbability = 0.3;
        const Topology t = generateRandomTopology(cfg, 1000 + seed);
        ASSERT_VALID(checkTopology(t));
    }
}

TEST(ValTopology, RandomMutationSequencesPreserveTheInvariants) {
    int accepted = 0, rejected = 0;
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        Rng rng(seed * 7919);
        std::uint32_t nextNode = 100, nextLink = 1000;
        for (int step = 0; step < 60; ++step) {
            const std::string before = fingerprint(t);
            std::vector<NodeId> nodes;
            for (const auto& e : t.network().nodes()) nodes.push_back(e.first);
            std::vector<LinkId> links;
            for (const auto& e : t.network().links()) links.push_back(e.first);
            if (nodes.empty()) {
                ASSERT_OK(t.addNode(makeNode(nextNode++)));
                continue;
            }
            Result<void> result;
            switch (rng.uniformInt(0, 4)) {
                case 0: result = t.addNode(makeNode(nextNode++)); break;
                case 1: {
                    const NodeId a = nodes[rng.uniformIndex(nodes.size())];
                    const NodeId b = nodes[rng.uniformIndex(nodes.size())];  // may equal a: must be rejected by FiberLink::create
                    auto link = FiberLink::create(LinkId{nextLink++}, "x", a, b, 5, 0.2, 4);
                    result = link.ok() ? t.addLink(link.value()) : Result<void>{link.error()};
                    break;
                }
                case 2: result = links.empty() ? Result<void>{} : t.removeLink(links[rng.uniformIndex(links.size())]); break;
                case 3: result = t.removeNode(nodes[rng.uniformIndex(nodes.size())]); break;  // fails while links remain
                default: result = t.addLink(routedLink(nextLink++, nodes[0].value(), 9999, 1, 1)); break;  // unknown endpoint: always rejected
            }
            if (result.ok()) ++accepted;
            else {
                ++rejected;
                EXPECT_EQ(fingerprint(t), before) << "a rejected operation must not change anything (step " << step << ")";
            }
            ASSERT_VALID(checkTopology(t, /*checkPorts=*/false)) << "after step " << step;
        }
    }
    EXPECT_GT(accepted, 300);
    EXPECT_GT(rejected, 300);
}

TEST(ValTopology, JsonRoundTripPreservesStructureAndInvariants) {
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        const Topology back = must(loadTopologyFromJson(topologyToJson(t)));
        EXPECT_EQ(fingerprint(back), fingerprint(t));
        ASSERT_VALID(checkTopology(back));
    }
}

TEST(ValTopology, DirectedAndBidirectionalTraversalRulesOnTheFixtures) {
    const Topology directed = fixture("directed_graph");  // 1->2, 2->3, 3->1
    for (std::uint32_t n = 1; n <= 3; ++n) {
        ASSERT_EQ(directed.outgoing(NodeId{n}).size(), 1u) << "node " << n;
        EXPECT_EQ(directed.outgoing(NodeId{n})[0].neighbor, NodeId{n % 3 + 1});
        EXPECT_EQ(directed.incident(NodeId{n}).size(), 2u) << "a one-way link still occupies a port at both ends";
    }
    const Topology line = fixture("simple_line");
    EXPECT_EQ(line.outgoing(NodeId{2}).size(), 2u);
    EXPECT_TRUE(line.hasDirectLink(NodeId{3}, NodeId{2}) && line.hasDirectLink(NodeId{2}, NodeId{3}));
    ASSERT_VALID(checkTopology(directed));
    ASSERT_VALID(checkTopology(line));
}

TEST(ValTopology, EquipmentRelationshipsInGeneratedTopologies) {
    const Topology t = generateRandomTopology(RandomTopologyConfig{}, 5);
    for (const auto& [id, node] : t.network().nodes()) {
        ASSERT_TRUE(node.switchingElement().has_value());
        const SwitchingElement* sw = t.network().findSwitchingElement(*node.switchingElement());
        ASSERT_NE(sw, nullptr);
        EXPECT_GE(sw->portCount(), t.degree(id)) << "ports cover the node's links";
        EXPECT_EQ(node.transceivers().size(), 1u);
    }
    RandomTopologyConfig bare;
    bare.attachEquipment = false;
    const Topology plain = generateRandomTopology(bare, 5);
    EXPECT_TRUE(plain.network().switchingElements().empty());
    ASSERT_VALID(checkTopology(plain));
}

TEST(ValTopology, FailureOverlaysNeverChangeTheTopology) {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        SCOPED_TRACE(seedContext(seed));
        const Topology t = generateRandomTopology(RandomTopologyConfig{}, seed);
        const std::string before = fingerprint(t);
        FailureState failures(t);
        for (const ScriptedFailure& f : generateRandomFailureScript(t, 40, 100, seed)) {
            switch (f.action) {
                case FailureAction::FailLink: ASSERT_OK(failures.failLink(LinkId{f.id})); break;
                case FailureAction::RecoverLink: ASSERT_OK(failures.recoverLink(LinkId{f.id})); break;
                case FailureAction::FailNode: ASSERT_OK(failures.failNode(NodeId{f.id})); break;
                case FailureAction::RecoverNode: ASSERT_OK(failures.recoverNode(NodeId{f.id})); break;
            }
            ASSERT_VALID(checkFailureState(failures, t));
        }
        EXPECT_EQ(fingerprint(t), before);
        ASSERT_VALID(checkTopology(t));
    }
}

TEST(ValTopology, TheRealTopologyFilesPassToo) {
    const Topology nsfnet = must(loadTopologyFromFile(std::string(OPTICALNET_CONFIG_DIR_FOR_VALIDATION) + "/nsfnet.json"));
    ASSERT_VALID(checkTopology(nsfnet));
    EXPECT_EQ(nsfnet.nodeCount(), 14u);
    const Topology ring = must(loadTopologyFromFile(std::string(OPTICALNET_CONFIG_DIR_FOR_VALIDATION) + "/ring4.json"));
    ASSERT_VALID(checkTopology(ring));
}
