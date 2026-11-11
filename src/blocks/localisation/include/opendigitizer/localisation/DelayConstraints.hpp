#ifndef OPENDIGITIZER_LOCALISATION_DELAYCONSTRAINTS_HPP
#define OPENDIGITIZER_LOCALISATION_DELAYCONSTRAINTS_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace opendigitizer::localisation {

using Point3 = std::array<double, 3>;
using Point2 = std::array<double, 2>;

enum class Mode : std::uint8_t { tdoa, bistatic };

enum class Status : std::uint8_t { valid, insufficientData, invalidInput, poorGeometry, ambiguous, noConsistentSolution };

inline constexpr std::array<std::string_view, 6> kStatusNames{"Valid", "InsufficientData", "InvalidInput", "PoorGeometry", "Ambiguous", "NoConsistentSolution"};
static_assert(std::to_underlying(Status::noConsistentSolution) + 1U == kStatusNames.size());

[[nodiscard]] constexpr std::string_view statusName(Status status) noexcept { return kStatusNames[std::to_underlying(status)]; }

/// receivers are focus indices 0, 1, …; transmitter k is focus −1 − k
[[nodiscard]] constexpr int transmitterFocus(std::size_t k) noexcept { return -1 - static_cast<int>(k); }

/// |p − a| + sign·|p − b| = value: sign −1 a TDOA range difference (a hyperboloid sheet), +1 a bistatic path sum (a spheroid)
struct Constraint {
    Point3      a{};
    Point3      b{};
    double      sign       = -1.0;
    double      value      = 0.0; // m
    int         focusA     = 0;
    int         focusB     = 0;
    bool        usable     = true;
    bool        degenerate = false; // on the edge of the possible range: a ray (TDOA) or the segment between the foci (bistatic)
    std::string refusal{};
};

struct Settings {
    std::array<double, 4> roi{-2.0, 2.0, -2.0, 2.0}; // x_min, x_max, y_min, y_max in m
    double                viewZ                  = 0.0;
    std::size_t           curveSegments          = 192UZ;
    double                candidateMergeDistance = 0.02; // m
    double                consensusDistance      = 0.15; // m
    double                maxRangeResidual       = 0.05; // m
    double                minCrossingAngle       = 5.0 * std::numbers::pi / 180.0;
};

struct Intersection {
    Point2                     point{};
    std::array<std::size_t, 2> constraints{};
    bool                       accepted = false;
    int                        cluster  = -1;
};

struct Solution {
    Status                           status = Status::insufficientData;
    Point3                           mean{std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    double                           rmsRangeResidual = std::numeric_limits<double>::quiet_NaN();
    double                           candidateSpread  = std::numeric_limits<double>::quiet_NaN();
    std::vector<Intersection>        intersections{};
    std::vector<Point3>              alternatives{}; // cluster means unless exactly one cluster is valid
    std::vector<std::size_t>         supporting{};   // constraint indices behind `mean`
    std::vector<std::vector<Point2>> curves{};       // per constraint, NaN-separated pieces in the view plane
    std::string                      diagnostics{};  // refused constraints and missing data, "; "-separated
};

[[nodiscard]] inline double distance(const Point3& p, const Point3& q) noexcept { return std::hypot(p[0] - q[0], p[1] - q[1], p[2] - q[2]); }

[[nodiscard]] inline double residualOf(const Constraint& constraint, const Point3& p) noexcept { return distance(p, constraint.a) + constraint.sign * distance(p, constraint.b) - constraint.value; }

[[nodiscard]] inline double scaleOf(std::span<const Point3> foci) noexcept {
    double extent = 0.0;
    for (const Point3& p : foci) {
        for (const Point3& q : foci) {
            extent = std::max(extent, distance(p, q));
        }
    }
    return std::max(extent, 1.0);
}

/// a TDOA constraint from c·(t_i − t_j) = `rangeDifference`; impossible or uninformative geometry leaves it unusable with the reason
[[nodiscard]] inline Constraint tdoaConstraint(const Point3& ri, const Point3& rj, int i, int j, double rangeDifference, double tolerance) {
    Constraint   constraint{.a = ri, .b = rj, .sign = -1.0, .value = rangeDifference, .focusA = i, .focusB = j};
    const double baseline = distance(ri, rj);
    if (!std::isfinite(rangeDifference)) {
        constraint.usable  = false;
        constraint.refusal = "not a finite delay";
    } else if (baseline <= tolerance) {
        constraint.usable  = false;
        constraint.refusal = "coincident receivers";
    } else if (std::abs(rangeDifference) > baseline + tolerance) {
        constraint.usable  = false;
        constraint.refusal = "range difference beyond the baseline";
    } else {
        constraint.degenerate = std::abs(rangeDifference) > baseline - tolerance;
    }
    return constraint;
}

[[nodiscard]] inline Constraint bistaticConstraint(const Point3& s, const Point3& ri, int k, int i, double pathSum, double tolerance) {
    Constraint   constraint{.a = s, .b = ri, .sign = 1.0, .value = pathSum, .focusA = k, .focusB = i};
    const double baseline = distance(s, ri);
    if (!std::isfinite(pathSum)) {
        constraint.usable  = false;
        constraint.refusal = "not a finite delay";
    } else if (pathSum < baseline - tolerance) {
        constraint.usable  = false;
        constraint.refusal = "path shorter than the direct path";
    } else {
        constraint.degenerate = pathSum < baseline + tolerance;
    }
    return constraint;
}

namespace detail {

[[nodiscard]] inline Point3 inPlane(const Point2& p, double z) noexcept { return {p[0], p[1], z}; }

[[nodiscard]] inline bool insideRoi(const Point2& p, const std::array<double, 4>& roi) noexcept { return p[0] >= roi[0] && p[0] <= roi[1] && p[1] >= roi[2] && p[1] <= roi[3]; }

/// the ray origin from which the section with the plane z = `z` is crossed at most once: the point of the foci axis' projection
/// deepest inside the convex region the curve bounds (around the measured branch's focus, or inside the spheroid); nullopt when the
/// plane misses that region
[[nodiscard]] inline std::optional<Point2> rayOrigin(const Constraint& constraint, double z) {
    constexpr std::size_t kAxisSamples = 256UZ;
    constexpr double      kAxisReach   = 4.0; // baselines beyond either focus
    const double          baseline     = distance(constraint.a, constraint.b);
    const double          inward       = constraint.sign > 0.0 || constraint.value <= 0.0 ? 1.0 : -1.0;
    const auto            insideness   = [&](const Point2& p) { return inward * residualOf(constraint, inPlane(p, z)); };
    Point2                best{constraint.a[0], constraint.a[1]};
    double                deepest = insideness(best);
    for (std::size_t k = 0UZ; k <= kAxisSamples && baseline > 0.0; ++k) {
        const double s = -kAxisReach + (1.0 + 2.0 * kAxisReach) * static_cast<double>(k) / static_cast<double>(kAxisSamples);
        const Point2 p{constraint.a[0] + s * (constraint.b[0] - constraint.a[0]), constraint.a[1] + s * (constraint.b[1] - constraint.a[1])};
        if (const double depth = insideness(p); depth < deepest) {
            deepest = depth;
            best    = p;
        }
    }
    return deepest < 0.0 ? std::optional{best} : std::nullopt;
}

/// the first root of the constraint's residual along origin + t·(cos θ, sin θ), 0 < t ≤ tMax, by a scan and bisection
[[nodiscard]] inline std::optional<double> firstRoot(const Constraint& constraint, const Point2& origin, double theta, double tMax, double z) {
    constexpr std::size_t kScanSteps      = 96UZ;
    constexpr std::size_t kBisectionSteps = 60UZ;
    const Point2          direction{std::cos(theta), std::sin(theta)};
    const auto            residualAt = [&](double t) { return residualOf(constraint, inPlane({origin[0] + t * direction[0], origin[1] + t * direction[1]}, z)); };
    double                lo         = 0.0;
    double                fLo        = residualAt(lo);
    for (std::size_t k = 1UZ; k <= kScanSteps; ++k) {
        const double hi  = tMax * static_cast<double>(k) / static_cast<double>(kScanSteps);
        const double fHi = residualAt(hi);
        if (fLo == 0.0) {
            return lo > 0.0 ? std::optional{lo} : std::nullopt;
        }
        if ((fLo < 0.0) != (fHi < 0.0)) {
            double a = lo, b = hi, fa = fLo;
            for (std::size_t n = 0UZ; n < kBisectionSteps; ++n) {
                const double middle = (a + b) / 2.0;
                const double fm     = residualAt(middle);
                if ((fa < 0.0) == (fm < 0.0)) {
                    a  = middle;
                    fa = fm;
                } else {
                    b = middle;
                }
            }
            return (a + b) / 2.0;
        }
        lo  = hi;
        fLo = fHi;
    }
    return std::nullopt;
}

[[nodiscard]] inline Point2 gradientOf(const Constraint& constraint, const Point3& p) noexcept {
    const double da = std::max(distance(p, constraint.a), 1e-12);
    const double db = std::max(distance(p, constraint.b), 1e-12);
    return {(p[0] - constraint.a[0]) / da + constraint.sign * (p[0] - constraint.b[0]) / db, (p[1] - constraint.a[1]) / da + constraint.sign * (p[1] - constraint.b[1]) / db};
}

/// Gauss-Newton on the two constraints' residuals in the view plane; nullopt when singular or not converged
[[nodiscard]] inline std::optional<Point2> refine(const Constraint& first, const Constraint& second, Point2 p, double z) {
    constexpr std::size_t kMaxSteps = 20UZ;
    for (std::size_t step = 0UZ; step < kMaxSteps; ++step) {
        const Point3 q   = inPlane(p, z);
        const Point2 g1  = gradientOf(first, q);
        const Point2 g2  = gradientOf(second, q);
        const double det = g1[0] * g2[1] - g1[1] * g2[0];
        if (std::abs(det) < 1e-10) {
            return std::nullopt;
        }
        const double r1 = residualOf(first, q);
        const double r2 = residualOf(second, q);
        const Point2 dp{(g2[1] * r1 - g1[1] * r2) / det, (-g2[0] * r1 + g1[0] * r2) / det};
        p = {p[0] - dp[0], p[1] - dp[1]};
        if (std::hypot(dp[0], dp[1]) < 1e-12) {
            return p;
        }
    }
    const Point3 q = inPlane(p, z);
    return std::abs(residualOf(first, q)) < 1e-9 && std::abs(residualOf(second, q)) < 1e-9 ? std::optional{p} : std::nullopt;
}

[[nodiscard]] inline std::optional<Point2> segmentCrossing(const Point2& p0, const Point2& p1, const Point2& q0, const Point2& q1) noexcept {
    const Point2 r{p1[0] - p0[0], p1[1] - p0[1]};
    const Point2 s{q1[0] - q0[0], q1[1] - q0[1]};
    const double denominator = r[0] * s[1] - r[1] * s[0];
    if (denominator == 0.0) {
        return std::nullopt;
    }
    const Point2 qp{q0[0] - p0[0], q0[1] - p0[1]};
    const double t = (qp[0] * s[1] - qp[1] * s[0]) / denominator;
    const double u = (qp[0] * r[1] - qp[1] * r[0]) / denominator;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) {
        return std::nullopt;
    }
    return Point2{p0[0] + t * r[0], p0[1] + t * r[1]};
}

[[nodiscard]] inline double pointSegmentDistance(const Point2& p, const Point2& a, const Point2& b) noexcept {
    const Point2 ab{b[0] - a[0], b[1] - a[1]};
    const double length2 = ab[0] * ab[0] + ab[1] * ab[1];
    const double t       = length2 > 0.0 ? std::clamp(((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1]) / length2, 0.0, 1.0) : 0.0;
    return std::hypot(p[0] - a[0] - t * ab[0], p[1] - a[1] - t * ab[1]);
}

[[nodiscard]] inline bool isBreak(const Point2& p) noexcept { return std::isnan(p[0]); }

struct Chunk {
    std::size_t           first = 0UZ; // segment index
    std::size_t           last  = 0UZ; // one past
    std::array<double, 4> box{};       // x_min, x_max, y_min, y_max, widened by the merge distance
};

/// a polyline's segments in runs of kChunkSegments without breaks, each with its bounding box, to skip far-apart curve pieces
[[nodiscard]] inline std::vector<Chunk> chunksOf(const std::vector<Point2>& curve, double widening) {
    constexpr std::size_t kChunkSegments = 16UZ;
    std::vector<Chunk>    chunks;
    for (std::size_t a = 0UZ; a + 1UZ < curve.size();) {
        if (isBreak(curve[a]) || isBreak(curve[a + 1UZ])) {
            ++a;
            continue;
        }
        Chunk chunk{.first = a, .last = a, .box = {curve[a][0], curve[a][0], curve[a][1], curve[a][1]}};
        while (chunk.last < a + kChunkSegments && chunk.last + 1UZ < curve.size() && !isBreak(curve[chunk.last + 1UZ])) {
            ++chunk.last;
            const Point2& p = curve[chunk.last];
            chunk.box       = {std::min(chunk.box[0], p[0]), std::max(chunk.box[1], p[0]), std::min(chunk.box[2], p[1]), std::max(chunk.box[3], p[1])};
        }
        chunk.box = {chunk.box[0] - widening, chunk.box[1] + widening, chunk.box[2] - widening, chunk.box[3] + widening};
        chunks.push_back(chunk);
        a = chunk.last;
    }
    return chunks;
}

/// where two segments cross, or the middle of the two when they come within `gap` (a touching curve need not cross)
[[nodiscard]] inline std::optional<Point2> seedOf(const Point2& u0, const Point2& u1, const Point2& v0, const Point2& v1, double gap) {
    if (const auto crossing = segmentCrossing(u0, u1, v0, v1)) {
        return crossing;
    }
    if (std::min({pointSegmentDistance(u0, v0, v1), pointSegmentDistance(u1, v0, v1), pointSegmentDistance(v0, u0, u1), pointSegmentDistance(v1, u0, u1)}) < gap) {
        return Point2{(u0[0] + u1[0] + v0[0] + v1[0]) / 4.0, (u0[1] + u1[1] + v0[1] + v1[1]) / 4.0};
    }
    return std::nullopt;
}

} // namespace detail

/// the constraint's section with the plane z = settings.viewZ inside the roi, as polyline pieces separated by NaN points
[[nodiscard]] inline std::vector<Point2> sectionOf(const Constraint& constraint, const Settings& settings) {
    std::vector<Point2> curve;
    if (!constraint.usable || settings.curveSegments == 0UZ) {
        return curve;
    }
    const auto originFound = detail::rayOrigin(constraint, settings.viewZ);
    if (!originFound) {
        return curve;
    }
    const Point2 origin = *originFound;
    double       tMax   = 0.0; // 2 × the farthest roi corner, so the ray leaving the roi still finds the curve outside it
    for (const Point2& corner : {Point2{settings.roi[0], settings.roi[2]}, Point2{settings.roi[0], settings.roi[3]}, Point2{settings.roi[1], settings.roi[2]}, Point2{settings.roi[1], settings.roi[3]}}) {
        tMax = std::max(tMax, 2.0 * std::hypot(corner[0] - origin[0], corner[1] - origin[1]));
    }
    constexpr Point2      kBreak{std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN()};
    constexpr std::size_t kEdgeBisections = 16UZ;
    const auto            pointAt         = [&](double theta) -> std::optional<Point2> {
        const auto t = detail::firstRoot(constraint, origin, theta, tMax, settings.viewZ);
        if (!t) {
            return std::nullopt;
        }
        const Point2 point{origin[0] + *t * std::cos(theta), origin[1] + *t * std::sin(theta)};
        return detail::insideRoi(point, settings.roi) ? std::optional{point} : std::nullopt;
    };
    const auto edgeBetween = [&](double inside, double outside) { // the curve's last point in the roi between two rays
        for (std::size_t n = 0UZ; n < kEdgeBisections; ++n) {
            const double middle                  = (inside + outside) / 2.0;
            (pointAt(middle) ? inside : outside) = middle;
        }
        return *pointAt(inside);
    };
    curve.reserve(settings.curveSegments + 4UZ);
    double previousTheta  = 0.0;
    bool   previousInside = false;
    for (std::size_t k = 0UZ; k <= settings.curveSegments; ++k) {
        const double theta = 2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(settings.curveSegments);
        const auto   point = pointAt(theta);
        if (point && !previousInside && k > 0UZ) {
            curve.push_back(edgeBetween(theta, previousTheta));
        }
        if (point) {
            curve.push_back(*point);
        } else if (previousInside) {
            curve.push_back(edgeBetween(previousTheta, theta));
            curve.push_back(kBreak);
        }
        previousTheta  = theta;
        previousInside = point.has_value();
    }
    while (!curve.empty() && detail::isBreak(curve.back())) {
        curve.pop_back();
    }
    return curve;
}

/// independent measurements among `indices`: TDOA the rank of the receiver-pair graph (receivers spanned − connected components,
/// as t_j − t_k follows from t_i − t_j and t_i − t_k), bistatic the number of distinct transmitter-receiver paths
[[nodiscard]] inline std::size_t supportRank(std::span<const Constraint> constraints, std::span<const std::size_t> indices) {
    std::vector<std::pair<int, int>> parents;
    const auto                       root = [&](int focus) {
        auto it = std::ranges::find(parents, focus, &std::pair<int, int>::first);
        if (it == parents.end()) {
            parents.emplace_back(focus, focus);
            return focus;
        }
        while (it->second != it->first) {
            it = std::ranges::find(parents, it->second, &std::pair<int, int>::first);
        }
        return it->first;
    };
    std::size_t rank = 0UZ;
    for (const std::size_t index : indices) {
        const int a = root(constraints[index].focusA);
        const int b = root(constraints[index].focusB);
        if (a != b || constraints[index].sign > 0.0) {
            std::ranges::find(parents, a, &std::pair<int, int>::first)->second = b;
            ++rank;
        }
    }
    if (std::ranges::any_of(indices, [&](std::size_t index) { return constraints[index].sign > 0.0; })) {
        std::vector<std::pair<int, int>> paths;
        for (const std::size_t index : indices) {
            paths.emplace_back(constraints[index].focusA, constraints[index].focusB);
        }
        std::ranges::sort(paths);
        rank = static_cast<std::size_t>(std::ranges::distance(paths.begin(), std::ranges::unique(paths).begin()));
    }
    return rank;
}

/// two independent measurements fix a point in the plane; a candidate is accepted only at the full rank of all usable constraints
/// (every receiver agrees), since two curves always cross somewhere and a third receiver's implied pair holds at every TDOA crossing
inline constexpr std::size_t kRequiredRank = 2UZ;

/// each constraint pair is one estimate: of its crossings only those agreeing best with all usable constraints (in RMS range
/// residual, within this many metres of the best, so a mirror image ties) are candidates
inline constexpr double kTieDistance = 1e-6;

[[nodiscard]] inline Solution solve(std::span<const Constraint> constraints, const Settings& settings) {
    Solution solution;
    solution.curves.reserve(constraints.size());
    for (const Constraint& constraint : constraints) {
        solution.curves.push_back(sectionOf(constraint, settings));
    }
    std::vector<std::size_t> usable;
    for (std::size_t i = 0UZ; i < constraints.size(); ++i) {
        if (constraints[i].usable) {
            usable.push_back(i);
        }
    }
    const bool anyRefused = usable.size() < constraints.size();
    for (const Constraint& constraint : constraints) {
        if (!constraint.usable) {
            solution.diagnostics += std::format("{}{}-{}: {}", solution.diagnostics.empty() ? "" : "; ", constraint.focusA, constraint.focusB, constraint.refusal);
        }
    }
    if (supportRank(constraints, usable) < kRequiredRank || usable.size() < 2UZ) {
        solution.status = anyRefused && usable.empty() ? Status::invalidInput : Status::insufficientData;
        return solution;
    }

    std::vector<Intersection>                     raw;
    std::vector<Point2>                           crossings;
    const double                                  gap    = settings.candidateMergeDistance;
    const std::vector<std::vector<detail::Chunk>> chunks = usable | std::views::transform([&](std::size_t k) { return detail::chunksOf(solution.curves[k], gap); }) | std::ranges::to<std::vector>();
    for (std::size_t m = 0UZ; m < usable.size(); ++m) {
        for (std::size_t n = m + 1UZ; n < usable.size(); ++n) {
            const std::size_t          i = usable[m];
            const std::size_t          j = usable[n];
            const std::vector<Point2>& u = solution.curves[i];
            const std::vector<Point2>& v = solution.curves[j];
            for (const auto& [uFirst, uLast, uBox] : chunks[m]) {
                for (const auto& [vFirst, vLast, vBox] : chunks[n]) {
                    if (uBox[1] < vBox[0] || vBox[1] < uBox[0] || uBox[3] < vBox[2] || vBox[3] < uBox[2]) {
                        continue;
                    }
                    for (std::size_t a = uFirst; a < uLast; ++a) {
                        for (std::size_t b = vFirst; b < vLast; ++b) {
                            if (const auto seed = detail::seedOf(u[a], u[a + 1UZ], v[b], v[b + 1UZ], gap)) {
                                const auto refined = detail::refine(constraints[i], constraints[j], *seed, settings.viewZ);
                                if (refined && detail::insideRoi(*refined, settings.roi)) {
                                    crossings.push_back(*refined);
                                }
                            }
                        }
                    }
                }
            }
            const auto rmsOf = [&](const Point2& p) {
                double sum = 0.0;
                for (const std::size_t index : usable) {
                    sum += std::pow(residualOf(constraints[index], detail::inPlane(p, settings.viewZ)), 2);
                }
                return std::sqrt(sum / static_cast<double>(usable.size()));
            };
            double best = std::numeric_limits<double>::infinity();
            for (const Point2& crossing : crossings) {
                best = std::min(best, rmsOf(crossing));
            }
            for (const Point2& crossing : crossings) {
                const bool known = std::ranges::any_of(raw, [&](const Intersection& other) { return std::hypot(other.point[0] - crossing[0], other.point[1] - crossing[1]) < settings.candidateMergeDistance; });
                if (rmsOf(crossing) <= best + kTieDistance && !known) {
                    raw.push_back({.point = crossing, .constraints = {i, j}});
                }
            }
            crossings.clear();
        }
    }

    const auto supportAt = [&](const Point2& p) {
        std::vector<std::size_t> within;
        for (const std::size_t index : usable) {
            if (std::abs(residualOf(constraints[index], detail::inPlane(p, settings.viewZ))) <= settings.maxRangeResidual) {
                within.push_back(index);
            }
        }
        return within;
    };
    std::vector<std::size_t> spans;
    for (const Intersection& candidate : raw) {
        spans.push_back(supportRank(constraints, supportAt(candidate.point)));
    }
    const std::size_t fullRank = supportRank(constraints, usable);
    for (std::size_t m = 0UZ; m < raw.size(); ++m) {
        raw[m].accepted = spans[m] == fullRank;
    }

    int clusters = 0;
    for (std::size_t m = 0UZ; m < raw.size(); ++m) {
        if (!raw[m].accepted || raw[m].cluster >= 0) {
            continue;
        }
        raw[m].cluster = clusters;
        for (bool grew = true; grew;) {
            grew = false;
            for (Intersection& other : raw) {
                if (!other.accepted || other.cluster >= 0) {
                    continue;
                }
                const bool near = std::ranges::any_of(raw, [&](const Intersection& member) { return member.cluster == clusters && std::hypot(member.point[0] - other.point[0], member.point[1] - other.point[1]) <= settings.consensusDistance; });
                if (near) {
                    other.cluster = clusters;
                    grew          = true;
                }
            }
        }
        ++clusters;
    }

    std::vector<Point2>      means(static_cast<std::size_t>(clusters), Point2{0.0, 0.0});
    std::vector<std::size_t> counts(static_cast<std::size_t>(clusters), 0UZ);
    for (const Intersection& candidate : raw) {
        if (candidate.cluster >= 0) {
            const auto c = static_cast<std::size_t>(candidate.cluster);
            means[c][0] += candidate.point[0];
            means[c][1] += candidate.point[1];
            ++counts[c];
        }
    }
    for (std::size_t c = 0UZ; c < means.size(); ++c) {
        means[c] = {means[c][0] / static_cast<double>(counts[c]), means[c][1] / static_cast<double>(counts[c])};
    }
    solution.intersections = std::move(raw);

    if (clusters == 0) {
        solution.status = Status::noConsistentSolution;
        return solution;
    }
    if (clusters > 1) {
        solution.status = Status::ambiguous;
        for (const Point2& mean : means) {
            solution.alternatives.push_back(detail::inPlane(mean, settings.viewZ));
        }
        return solution;
    }

    const Point2                   mean      = means.front();
    const std::vector<std::size_t> supported = supportAt(mean);
    const Point3                   at        = detail::inPlane(mean, settings.viewZ);
    if (supportRank(constraints, supported) < fullRank) {
        solution.status = Status::noConsistentSolution;
        solution.alternatives.push_back(at);
        return solution;
    }
    double sumSquares = 0.0;
    for (const std::size_t index : supported) {
        sumSquares += std::pow(residualOf(constraints[index], at), 2);
    }
    double spread = 0.0;
    for (const Intersection& candidate : solution.intersections) {
        if (candidate.cluster == 0) {
            spread += std::pow(candidate.point[0] - mean[0], 2) + std::pow(candidate.point[1] - mean[1], 2);
        }
    }
    double widestCrossing = 0.0;
    bool   degenerate     = false;
    for (std::size_t m = 0UZ; m < supported.size(); ++m) {
        degenerate = degenerate || constraints[supported[m]].degenerate;
        for (std::size_t n = m + 1UZ; n < supported.size(); ++n) {
            const Point2 g1   = detail::gradientOf(constraints[supported[m]], at);
            const Point2 g2   = detail::gradientOf(constraints[supported[n]], at);
            const double sine = std::abs(g1[0] * g2[1] - g1[1] * g2[0]) / std::max(std::hypot(g1[0], g1[1]) * std::hypot(g2[0], g2[1]), 1e-300);
            widestCrossing    = std::max(widestCrossing, std::asin(std::min(sine, 1.0)));
        }
    }
    solution.supporting       = supported;
    solution.rmsRangeResidual = std::sqrt(sumSquares / static_cast<double>(supported.size()));
    solution.candidateSpread  = std::sqrt(spread / static_cast<double>(counts.front()));
    if (degenerate || widestCrossing < settings.minCrossingAngle) {
        solution.status = Status::poorGeometry;
        solution.alternatives.push_back(at);
        return solution;
    }
    solution.status = Status::valid;
    solution.mean   = at;
    return solution;
}

} // namespace opendigitizer::localisation

#endif // OPENDIGITIZER_LOCALISATION_DELAYCONSTRAINTS_HPP
