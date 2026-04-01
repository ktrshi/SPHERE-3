#include "BackgroundOperator.hh"
#include "FastBackgroundSampler.hh"
#include "MoshitReader.hh"
#include "MoshitWriter.hh"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <vector>

namespace {

struct CommandResult {
    int exit_code = -1;
    std::string output;
};

bool nearly_equal(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

bool check_or_fail(bool condition, const std::string& message, bool& ok) {
    if (condition) {
        return true;
    }
    std::cerr << "test_background_calibration: " << message << '\n';
    ok = false;
    return false;
}

std::string shell_quote(const std::string& input) {
    std::string quoted = "'";
    for (const char ch : input) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

CommandResult run_command(const std::vector<std::string>& args) {
    std::ostringstream cmd;
    for (size_t idx = 0; idx < args.size(); ++idx) {
        if (idx > 0) {
            cmd << ' ';
        }
        cmd << shell_quote(args[idx]);
    }
    cmd << " 2>&1";

    CommandResult result;
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (pipe == nullptr) {
        result.output = "popen failed";
        return result;
    }

    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result.output += buffer;
    }

    const int status = pclose(pipe);
    if (status == -1) {
        result.output += "\npclose failed";
        return result;
    }

    result.exit_code = WEXITSTATUS(status);
    return result;
}

struct TempTreeCleanup {
    std::filesystem::path path;

    ~TempTreeCleanup() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

std::filesystem::path make_temp_root() {
    const auto unique =
        std::to_string(static_cast<unsigned long long>(std::rand())) + "-" +
        std::to_string(static_cast<unsigned long long>(std::rand()));
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("bgcal-" + unique);
    std::filesystem::create_directories(root);
    return root;
}

void write_event(const std::filesystem::path& path,
                 const std::vector<uint16_t>& pixels,
                 float t_ns) {
    std::filesystem::create_directories(path.parent_path());
    MoshitWriter writer;
    writer.Begin(-1000.0f, 0.0f, 0.0f);
    for (const uint16_t pixel : pixels) {
        writer.AddHit(pixel, t_ns, 2, 0, 0, 0, t_ns);
    }
    writer.Flush(path.string());
}

double sampled_mean_hits_per_event(const BackgroundOperator& op, int samples) {
    auto shared = std::make_shared<BackgroundOperator>(op);
    FastBackgroundSampler sampler(shared, 123456u);
    MoshitWriter writer;
    double total_hits = 0.0;
    for (int event_index = 0; event_index < samples; ++event_index) {
        writer.Begin(-1000.0f, 0.0f, 0.0f);
        const FastBackgroundSampleStats stats = sampler.SampleInto(writer);
        total_hits += static_cast<double>(stats.injected_hits);
    }
    return total_hits / static_cast<double>(samples);
}

double total_seed_rate(const BackgroundOperator& op) {
    double total = 0.0;
    for (const float rate : op.pixel_rate_per_event) {
        total += static_cast<double>(rate);
    }
    return total;
}

double parse_metric(const std::string& output, const std::string& key) {
    const std::string prefix = key + "=";
    const size_t pos = output.find(prefix);
    if (pos == std::string::npos) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const size_t start = pos + prefix.size();
    size_t end = start;
    while (end < output.size() && output[end] != '\n' && output[end] != '\r') {
        ++end;
    }
    return std::stod(output.substr(start, end - start));
}

}  // namespace

int main(int argc, char** argv) {
    bool ok = true;

    try {
        const std::filesystem::path exe_dir =
            std::filesystem::weakly_canonical(std::filesystem::absolute(argv[0])).parent_path();
        const std::filesystem::path build_bgop = exe_dir / "build_background_operator";
        const std::filesystem::path compare = exe_dir / "compare_moshits";

        check_or_fail(std::filesystem::exists(build_bgop),
                      "build_background_operator executable is missing",
                      ok);
        check_or_fail(std::filesystem::exists(compare),
                      "compare_moshits executable is missing",
                      ok);
        if (!ok) {
            return 1;
        }

        const std::filesystem::path temp_root = make_temp_root();
        const TempTreeCleanup cleanup{temp_root};

        const std::filesystem::path roundtrip_dir = temp_root / "roundtrip_top_level";
        std::filesystem::create_directories(roundtrip_dir);
        for (int event_index = 0; event_index < 64; ++event_index) {
            write_event(roundtrip_dir / ("event_" + std::to_string(event_index) + ".moshit.zst"),
                        {0, 1, 2, 3, 4, 5, 6},
                        100.0f);
        }

        const std::filesystem::path roundtrip_bgop = temp_root / "roundtrip.bgop.zst";
        const CommandResult roundtrip_build = run_command({
            build_bgop.string(),
            roundtrip_dir.string(),
            roundtrip_bgop.string(),
            "1",
            "-1000",
            "0",
            "0",
            "256",
            "64",
        });
        check_or_fail(roundtrip_build.exit_code == 0,
                      "build_background_operator failed for top-level calibration data:\n" +
                          roundtrip_build.output,
                      ok);
        if (roundtrip_build.exit_code == 0) {
            const BackgroundOperator roundtrip_op = read_background_operator(roundtrip_bgop.string());
            const double mean_hits = sampled_mean_hits_per_event(roundtrip_op, 4000);
            const double seed_rate = total_seed_rate(roundtrip_op);
            check_or_fail(seed_rate > 0.8 && seed_rate < 1.2,
                          "expected latent seed rate near 1 for a 7-pixel coincidence, got " +
                              std::to_string(seed_rate),
                          ok);
            check_or_fail(mean_hits > 6.0 && mean_hits < 8.5,
                          "round-trip mean hits/event should stay near the observed 7 hits, got " +
                              std::to_string(mean_hits),
                          ok);
        }

        const std::filesystem::path nested_calibration_root = temp_root / "nested_calibration";
        write_event(nested_calibration_root / "batchA" / "subrun1" / "event_0.moshit.zst",
                    {77},
                    42.0f);
        const std::filesystem::path nested_bgop = temp_root / "nested.bgop.zst";
        const CommandResult nested_build = run_command({
            build_bgop.string(),
            nested_calibration_root.string(),
            nested_bgop.string(),
            "1",
            "-1000",
            "0",
            "0",
            "256",
            "1",
        });
        check_or_fail(nested_build.exit_code == 0,
                      "build_background_operator failed on nested calibration data:\n" +
                          nested_build.output,
                      ok);
        if (nested_build.exit_code == 0) {
            const BackgroundOperator nested_op = read_background_operator(nested_bgop.string());
            check_or_fail(total_seed_rate(nested_op) > 0.5,
                          "nested calibration tree was not consumed by build_background_operator",
                          ok);
        }

        const std::filesystem::path empty_calibration_root = temp_root / "empty_calibration";
        std::filesystem::create_directories(empty_calibration_root);
        const CommandResult empty_build = run_command({
            build_bgop.string(),
            empty_calibration_root.string(),
            (temp_root / "empty.bgop.zst").string(),
            "1",
            "-1000",
            "0",
            "0",
            "256",
            "1",
        });
        check_or_fail(empty_build.exit_code != 0,
                      "empty calibration tree should fail, but command succeeded:\n" +
                          empty_build.output,
                      ok);
        check_or_fail(empty_build.output.find(".moshit.zst") != std::string::npos,
                      "empty calibration failure should mention missing .moshit.zst files:\n" +
                          empty_build.output,
                      ok);

        const std::filesystem::path compare_ref = temp_root / "compare_ref";
        const std::filesystem::path compare_cand = temp_root / "compare_cand";
        write_event(compare_ref / "nested" / "ref_0.moshit.zst", {9}, 10.0f);
        write_event(compare_cand / "nested" / "cand_0.moshit.zst", {9, 10}, 10.0f);
        const CommandResult nested_compare =
            run_command({compare.string(), compare_ref.string(), compare_cand.string()});
        check_or_fail(nested_compare.exit_code == 0,
                      "compare_moshits failed on nested inputs:\n" + nested_compare.output,
                      ok);
        if (nested_compare.exit_code == 0) {
            const double total_delta = parse_metric(nested_compare.output, "total_delta_pct");
            check_or_fail(!std::isnan(total_delta) && !nearly_equal(total_delta, 0.0),
                          "compare_moshits did not detect nested files; output was:\n" +
                              nested_compare.output,
                          ok);
        }

        const std::filesystem::path empty_compare_a = temp_root / "empty_compare_a";
        const std::filesystem::path empty_compare_b = temp_root / "empty_compare_b";
        std::filesystem::create_directories(empty_compare_a);
        std::filesystem::create_directories(empty_compare_b);
        const CommandResult empty_compare =
            run_command({compare.string(), empty_compare_a.string(), empty_compare_b.string()});
        check_or_fail(empty_compare.exit_code != 0,
                      "compare_moshits should fail on empty trees, but command succeeded:\n" +
                          empty_compare.output,
                      ok);
        check_or_fail(empty_compare.output.find(".moshit.zst") != std::string::npos,
                      "empty compare failure should mention missing .moshit.zst files:\n" +
                          empty_compare.output,
                      ok);
    } catch (const std::exception& e) {
        std::cerr << "test_background_calibration: unexpected exception: " << e.what() << '\n';
        return 1;
    }

    if (!ok) {
        return 1;
    }

    std::cout << "background_calibration ok\n";
    return 0;
}
