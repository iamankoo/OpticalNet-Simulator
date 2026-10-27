// Systematic invalid-input testing. Every expected failure must be a deterministic Result error (never an exception, never a
// crash), carry the documented code, and leave the system unchanged.
#include <limits>

#include "ValidationTestUtils.hpp"

using namespace opticalnet;
using namespace opticalnet::testing;

namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

ConnectionRequest req(std::uint32_t id, std::uint32_t from, std::uint32_t to, std::uint32_t demand, RoutingConstraints c = {}) {
    return ConnectionRequest{ConnectionId{id}, NodeId{from}, NodeId{to}, demand, std::move(c)};
}

}  // namespace

TEST(ValNegativeDomain, InvalidElementsAreRejected) {
    EXPECT_NO_THROW({
        ASSERT_ERROR(Node::create(NodeId{1}, ""), ErrorCode::InvalidArgument);
        ASSERT_ERROR(Transceiver::create(TransceiverId{1}, "t", 0, 10), ErrorCode::InvalidArgument);
        ASSERT_ERROR(Transceiver::create(TransceiverId{1}, "t", 10, kNaN), ErrorCode::InvalidArgument);
        ASSERT_ERROR(SwitchingElement::create(SwitchingElementId{1}, "s", SwitchingType::Roadm, 0), ErrorCode::InvalidArgument);
        ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{1}, 1, 0.2, 1), ErrorCode::InvalidArgument) << "self-link";
        ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, 0, 0.2, 1), ErrorCode::InvalidArgument) << "zero length";
        ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, kInf, 0.2, 1), ErrorCode::InvalidArgument);
        ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, 1, -0.1, 1), ErrorCode::InvalidArgument);
        ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, 1, 0.2, 0), ErrorCode::InvalidArgument) << "zero capacity";
        ASSERT_ERROR(FiberLink::create(LinkId{1}, "l", NodeId{1}, NodeId{2}, 1, 0.2, 1, LinkDirection::Bidirectional, 0), ErrorCode::InvalidArgument) << "zero cost";
        ASSERT_ERROR(FiberLink::create(LinkId{1}, " ", NodeId{1}, NodeId{2}, 1, 0.2, 1), ErrorCode::InvalidArgument);
    });
}

TEST(ValNegativeTopology, InvalidTopologyOperationsAreRejectedAndChangeNothing) {
    Topology t = fixture("simple_line");
    const std::string before = fingerprint(t);
    EXPECT_NO_THROW({
        ASSERT_ERROR(t.addNode(makeNode(1)), ErrorCode::DuplicateId);
        ASSERT_ERROR(t.addLink(routedLink(1, 1, 2, 1, 1)), ErrorCode::DuplicateId);
        ASSERT_ERROR(t.addLink(routedLink(50, 1, 99, 1, 1)), ErrorCode::NotFound) << "unknown NodeId";
        ASSERT_ERROR(t.addLink(routedLink(50, 99, 1, 1, 1)), ErrorCode::NotFound);
        ASSERT_ERROR(t.removeNode(NodeId{99}), ErrorCode::NotFound);
        ASSERT_ERROR(t.removeNode(NodeId{2}), ErrorCode::ConstraintViolation) << "still has links";
        ASSERT_ERROR(t.removeLink(LinkId{99}), ErrorCode::NotFound) << "unknown LinkId";
        ASSERT_ERROR(t.attachTransceiver(NodeId{1}, TransceiverId{9}), ErrorCode::NotFound);
        ASSERT_ERROR(t.attachSwitchingElement(NodeId{99}, SwitchingElementId{1}), ErrorCode::NotFound);
        ASSERT_ERROR(t.removeTransceiver(TransceiverId{9}), ErrorCode::NotFound);
    });
    EXPECT_EQ(fingerprint(t), before);
    EXPECT_TRUE(t.outgoing(NodeId{99}).empty()) << "lookups of unknown nodes are safe";
    EXPECT_EQ(t.findLink(LinkId{99}), nullptr);
    ASSERT_VALID(checkTopology(t));
}

TEST(ValNegativeTopology, InvalidTopologyFilesAreParseErrors) {
    for (const char* text : {"", "{", "[]", "{}", R"({"nodes": [{"id": 1}], "bogus": 1})", R"({"nodes": [{"id": -1}]})",
                             R"({"nodes": [{"id": 1}], "links": [{"id": 1, "source": 1, "target": 9, "length_km": 1, "capacity_channels": 1}]})"}) {
        Result<Topology> r = Error{ErrorCode::InternalError, "unset"};
        EXPECT_NO_THROW(r = loadTopologyFromJson(text)) << text;
        EXPECT_FALSE(r.ok()) << text;
    }
    ASSERT_ERROR(loadTopologyFromFile("this/file/does/not/exist.json"), ErrorCode::NotFound);
}

TEST(ValNegativeRouting, InvalidRoutingRequestsAreErrors) {
    const Topology t = fixture("simple_line");
    RoutingEngine engine;
    EXPECT_NO_THROW({
        ASSERT_ERROR(engine.findPath(t, NodeId{99}, NodeId{1}), ErrorCode::NotFound);
        ASSERT_ERROR(engine.findPath(t, NodeId{1}, NodeId{99}), ErrorCode::NotFound);
        RoutingConstraints c;
        for (double bad : {-1.0, kNaN, kInf}) {
            c.maxCost = bad;
            ASSERT_ERROR(engine.findPath(t, NodeId{1}, NodeId{2}, c), ErrorCode::InvalidArgument) << bad;
        }
        c = {};
        c.maxHops = 0;
        ASSERT_ERROR(engine.findPath(t, NodeId{1}, NodeId{2}, c), ErrorCode::NoFeasibleRoute);
        c = {};
        c.blockedNodes.insert(NodeId{1});
        ASSERT_ERROR(engine.findPath(t, NodeId{1}, NodeId{2}, c), ErrorCode::NoFeasibleRoute);
        ASSERT_ERROR(Path::create({}, {}, 0, CostMetric::HopCount), ErrorCode::InvalidArgument);
        ASSERT_ERROR(Path::create(nodeIds({1, 2}), {}, 1, CostMetric::HopCount), ErrorCode::InvalidArgument);
        ASSERT_ERROR(Path::create(nodeIds({1, 2}), linkIds({1}), -1, CostMetric::HopCount), ErrorCode::InvalidArgument);
    });
    Topology split = makeTopology({1, 2, 3}, {routedLink(1, 1, 2, 1, 1)});
    ASSERT_ERROR(engine.findPath(split, NodeId{1}, NodeId{3}), ErrorCode::NoRoute);
    EXPECT_THROW(RoutingEngine{std::unique_ptr<IRoutingAlgorithm>{}}, std::invalid_argument) << "a missing algorithm is a programming error";
}

TEST(ValNegativeResources, InvalidAllocationsAndReleasesAreErrors) {
    const Topology t = fixture("simple_line");
    ResourceManager rm{t};
    EXPECT_NO_THROW({
        ASSERT_ERROR(rm.usage(LinkId{99}), ErrorCode::NotFound);
        ASSERT_ERROR(rm.allocate(linkIds({1}), 0), ErrorCode::InvalidArgument) << "zero demand";
        ASSERT_ERROR(rm.allocate({}, 3), ErrorCode::InvalidArgument) << "empty path";
        ASSERT_ERROR(rm.allocate(linkIds({99}), 1), ErrorCode::NotFound);
        ASSERT_ERROR(rm.allocate(linkIds({1}), 11), ErrorCode::InsufficientCapacity) << "impossible capacity";
        ASSERT_ERROR(rm.allocate(linkIds({1}), 4'000'000'000u), ErrorCode::InsufficientCapacity) << "no overflow";
        ASSERT_ERROR(rm.release(linkIds({1}), 1), ErrorCode::InconsistentState) << "nothing allocated";
        ASSERT_ERROR(rm.release(linkIds({1}), 0), ErrorCode::InvalidArgument);
        ASSERT_OK(rm.allocate(linkIds({1}), 5));
        ASSERT_ERROR(rm.release(linkIds({1}), 6), ErrorCode::InconsistentState) << "more than was allocated";
    });
    EXPECT_EQ(rm.totalAllocated(), 5u);
    ASSERT_VALID(checkResources(rm, t));
}

TEST(ValNegativeConnections, InvalidConnectionOperationsAreErrors) {
    const Topology t = fixture("link_failure");
    ConnectionManager m{t};
    ASSERT_OK(m.establish(req(1, 1, 3, 2)));
    const std::uint64_t allocated = m.resources().totalAllocated();
    const auto path = [](std::initializer_list<std::uint32_t> n, std::initializer_list<std::uint32_t> l) {
        return must(Path::create(nodeIds(n), linkIds(l), static_cast<double>(l.size()), CostMetric::Administrative));
    };
    EXPECT_NO_THROW({
        ASSERT_ERROR(m.establish(req(1, 1, 3, 1)), ErrorCode::DuplicateId);
        ASSERT_ERROR(m.establish(req(2, 99, 3, 1)), ErrorCode::NotFound) << "unknown source";
        ASSERT_ERROR(m.establish(req(2, 1, 99, 1)), ErrorCode::NotFound) << "unknown destination";
        ASSERT_ERROR(m.establish(req(2, 1, 3, 0)), ErrorCode::InvalidArgument) << "zero capacity";
        ASSERT_ERROR(m.establish(req(2, 1, 1, 1)), ErrorCode::InvalidArgument) << "source == destination";
        ASSERT_ERROR(m.establish(req(2, 1, 3, 11)), ErrorCode::InsufficientCapacity);
        ASSERT_ERROR(m.release(ConnectionId{99}), ErrorCode::NotFound) << "unknown ConnectionId";
        ASSERT_ERROR(m.detach(ConnectionId{99}), ErrorCode::NotFound);
        ASSERT_ERROR(m.establishOnPath(req(2, 1, 3, 1), path({1}, {})), ErrorCode::InvalidArgument) << "trivial path";
        ASSERT_ERROR(m.establishOnPath(req(2, 1, 3, 1), path({1, 2}, {1})), ErrorCode::InvalidArgument) << "wrong destination";
        ASSERT_ERROR(m.establishOnPath(req(2, 1, 3, 1), path({1, 2, 3}, {1, 77})), ErrorCode::NotFound) << "unknown link in path";
        ASSERT_ERROR(m.establishOnPath(req(2, 1, 3, 1), path({1, 2, 3}, {1, 3})), ErrorCode::InvalidArgument) << "links do not chain";
        ASSERT_OK(m.release(ConnectionId{1}));
        ASSERT_ERROR(m.release(ConnectionId{1}), ErrorCode::NotFound) << "double release";
    });
    EXPECT_EQ(allocated, 4u);
    EXPECT_EQ(m.resources().totalAllocated(), 0u);
    const Topology directed = fixture("directed_graph");
    ConnectionManager dm{directed};
    ASSERT_ERROR(dm.establishOnPath(req(1, 2, 1, 1), path({2, 1}, {1})), ErrorCode::InvalidArgument) << "a directed link used backwards";
}

TEST(ValNegativeFailures, InvalidFailureTransitionsAreErrors) {
    const Topology t = fixture("link_failure");
    FailureState f(t);
    EXPECT_NO_THROW({
        ASSERT_ERROR(f.failLink(LinkId{99}), ErrorCode::NotFound);
        ASSERT_ERROR(f.recoverLink(LinkId{99}), ErrorCode::NotFound);
        ASSERT_ERROR(f.failNode(NodeId{99}), ErrorCode::NotFound);
        ASSERT_ERROR(f.recoverNode(NodeId{99}), ErrorCode::NotFound);
        ASSERT_ERROR(f.recoverLink(LinkId{1}), ErrorCode::InvalidTransition) << "recovering a healthy link";
        ASSERT_OK(f.failLink(LinkId{1}));
        ASSERT_ERROR(f.failLink(LinkId{1}), ErrorCode::InvalidTransition) << "repeated failure";
        ASSERT_ERROR(f.recoverNode(NodeId{1}), ErrorCode::InvalidTransition);
    });
    EXPECT_EQ(f.failedLinks().size(), 1u);
    using V = std::vector<ScriptedFailure>;
    ASSERT_ERROR(validateFailureSchedule(t, V{{-1, FailureAction::FailLink, 1}}), ErrorCode::InvalidArgument);
    ASSERT_ERROR(validateFailureSchedule(t, V{{1, FailureAction::FailLink, 99}}), ErrorCode::NotFound);
    ASSERT_ERROR(validateFailureSchedule(t, V{{1, FailureAction::RecoverNode, 1}}), ErrorCode::InvalidTransition);
    ASSERT_VALID(checkTopology(t));
}

TEST(ValNegativeSimulation, InvalidSimulationInputsAreErrors) {
    const Topology t = fixture("ring");
    const auto bad = [&t](auto mutate, ErrorCode code) {
        SimulationConfig c;
        mutate(c);
        Result<SimulationResult> r = Error{ErrorCode::InternalError, "unset"};
        EXPECT_NO_THROW(r = SimulationEngine{}.run(t, c));
        EXPECT_FALSE(r.ok());
        if (!r.ok()) EXPECT_EQ(r.error().code, code) << r.error().describe();
    };
    bad([](SimulationConfig& c) { c.endTime = -1; }, ErrorCode::InvalidArgument);
    bad([](SimulationConfig& c) { c.endTime = kNaN; }, ErrorCode::InvalidArgument);
    bad([](SimulationConfig& c) { c.arrivalRate = -1; }, ErrorCode::InvalidArgument);
    bad([](SimulationConfig& c) { c.meanLifetime = 0; }, ErrorCode::InvalidArgument);
    bad([](SimulationConfig& c) { c.minCapacityChannels = 0; }, ErrorCode::InvalidArgument);
    bad([](SimulationConfig& c) { c.minCapacityChannels = 3; c.maxCapacityChannels = 2; }, ErrorCode::InvalidArgument);
    bad([](SimulationConfig& c) { c.maxRequests = 5'000'000'000ULL; }, ErrorCode::InvalidArgument);
    bad([](SimulationConfig& c) { c.randomFailures = RandomFailureConfig{}; c.failureSchedule = {{1, FailureAction::FailLink, 1}}; }, ErrorCode::InvalidArgument);
    bad([](SimulationConfig& c) { c.randomFailures = RandomFailureConfig{-1, 10, 0}; }, ErrorCode::InvalidArgument);
    bad([](SimulationConfig& c) { c.failureSchedule = {{1, FailureAction::FailLink, 99}}; }, ErrorCode::NotFound);
    bad([](SimulationConfig& c) { c.failureSchedule = {{1, FailureAction::RecoverLink, 1}}; }, ErrorCode::InvalidTransition);
    bad([](SimulationConfig& c) { c.failureSchedule = {{-5, FailureAction::FailLink, 1}}; }, ErrorCode::InvalidArgument);

    EXPECT_NO_THROW({
        ASSERT_ERROR(SimulationEngine{}.runScript(t, std::vector{scripted(-1, 1, 1, 2, 1, 1)}, tracedConfig(10)), ErrorCode::InvalidArgument);
        ASSERT_ERROR(SimulationEngine{}.runScript(t, std::vector{scripted(1, 1, 1, 2, 1, 0)}, tracedConfig(10)), ErrorCode::InvalidArgument);
        ASSERT_ERROR(SimulationEngine{}.runScript(t, {}, tracedConfig(kNaN)), ErrorCode::InvalidArgument);
    });
    // Invalid *requests* inside a valid run are rejections, not errors: the run completes.
    const auto r = runScript(t, {scripted(1, 1, 1, 99, 1, 5), scripted(2, 2, 3, 3, 1, 5), scripted(3, 3, 1, 2, 0, 5)}, tracedConfig(10));
    EXPECT_EQ(r.metrics.totalRequests, 3u);
    EXPECT_EQ(r.metrics.acceptedRequests, 0u);
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::NotFound), 1u);
    EXPECT_EQ(r.metrics.rejectionsByReason.at(ErrorCode::InvalidArgument), 2u);
    ASSERT_VALID(checkSimulationTrace(t, r));
}

TEST(ValNegativeEventsAndStudies, InvalidEventAndStudyConfigurationsAreErrors) {
    EventQueue q;
    EXPECT_NO_THROW({
        ASSERT_ERROR(q.schedule(-1, EventType::ConnectionArrival, ConnectionId{1}), ErrorCode::InvalidArgument);
        ASSERT_ERROR(q.schedule(kNaN, EventType::ConnectionRelease, ConnectionId{1}), ErrorCode::InvalidArgument);
        ASSERT_ERROR(q.scheduleResource(kInf, EventType::LinkFailure, 1), ErrorCode::InvalidArgument);
    });
    EXPECT_TRUE(q.empty());
    EXPECT_THROW((void)q.pop(), std::logic_error) << "popping an empty queue is a programming error";

    const Topology t = fixture("ring");
    StudyConfig s;
    s.replications = 0;
    ASSERT_ERROR(SimulationStudy::run(t, SimulationConfig{}, s), ErrorCode::InvalidArgument);
    s.replications = StudyConfig::kMaxReplications + 1;
    ASSERT_ERROR(SimulationStudy::run(t, SimulationConfig{}, s), ErrorCode::InvalidArgument);
    s.replications = 2;
    SimulationConfig bad;
    bad.meanLifetime = -1;
    ASSERT_ERROR(SimulationStudy::run(t, bad, s), ErrorCode::InvalidArgument);
}
