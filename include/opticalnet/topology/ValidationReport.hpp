#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace opticalnet {

enum class Severity {
    Error,    // the topology breaks a rule and must not be simulated
    Warning,  // legal but suspicious (e.g. a node nothing connects to)
};

enum class IssueCode {
    PortCountExceeded,     // Error:   node has more links than its switching element has ports
    IsolatedNode,          // Warning: node has no links in a multi-node topology
    DisconnectedTopology,  // Warning: topology splits into several components
    // Dynamic resource state (Phase 4), all Errors:
    AllocationExceedsCapacity,   // a link has more channels allocated than it has
    AllocationOnUnknownLink,     // channels are allocated on a link the topology no longer has
    ConnectionAllocationMismatch // per-link allocation differs from what the active connections hold
};

[[nodiscard]] std::string_view toString(IssueCode code) noexcept;

struct ValidationIssue {
    Severity severity;
    IssueCode code;
    std::string message;
};

// Result of Topology::validate().
//
// Structural integrity (links reference existing nodes, ids unique, no self-links) is
// guaranteed by construction and therefore never appears here. The report covers the
// rules that depend on the whole topology, and the resource-consistency checks of
// ResourceManager / ConnectionManager.
//
// valid() answers "may this topology be simulated?" (no errors).
// It is deliberately independent of connectivity: a valid topology can be disconnected,
// which is reported as a warning.
struct ValidationReport {
    std::vector<ValidationIssue> issues;

    [[nodiscard]] bool valid() const noexcept { return errorCount() == 0; }
    [[nodiscard]] bool clean() const noexcept { return issues.empty(); }
    [[nodiscard]] std::size_t errorCount() const noexcept { return count(Severity::Error); }
    [[nodiscard]] std::size_t warningCount() const noexcept { return count(Severity::Warning); }
    [[nodiscard]] bool has(IssueCode code) const noexcept {
        return std::ranges::any_of(issues, [code](const ValidationIssue& i) { return i.code == code; });
    }

private:
    [[nodiscard]] std::size_t count(Severity severity) const noexcept {
        return static_cast<std::size_t>(std::ranges::count_if(
            issues, [severity](const ValidationIssue& i) { return i.severity == severity; }));
    }
};

}  // namespace opticalnet
