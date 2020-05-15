#pragma once

#include <Functions/TargetSpecific.h>
#include <Functions/IFunctionImpl.h>

#include <Common/Stopwatch.h>

#include <random>

/// This file contains Adaptors which help to combine several implementations of the function.
/// Adaptors check that implementation can be executed on the current platform and choose
/// that one which works faster according to previous runs. 

namespace DB
{

// TODO(dakovalkov): This is copied and pasted struct from LZ4_decompress_faster.h with little changes.
struct PerformanceStatistics
{
    struct Element
    {
        double count = 0;
        double sum = 0;

        double adjustedCount() const
        {
            return count - NUM_INVOCATIONS_TO_THROW_OFF;
        }

        double mean() const
        {
            return sum / adjustedCount();
        }

        /// For better convergence, we don't use proper estimate of stddev.
        /// We want to eventually separate between two algorithms even in case
        ///  when there is no statistical significant difference between them.
        double sigma() const
        {
            return mean() / sqrt(adjustedCount());
        }

        void update(double seconds, double bytes)
        {
            ++count;

            if (count > NUM_INVOCATIONS_TO_THROW_OFF)
                sum += seconds / bytes;
        }

        double sample(pcg64 & stat_rng) const
        {
            /// If there is a variant with not enough statistics, always choose it.
            /// And in that case prefer variant with less number of invocations.

            if (adjustedCount() < 2)
                return adjustedCount() - 1;
            else
                return std::normal_distribution<>(mean(), sigma())(stat_rng);
        }
    };

    /// Cold invocations may be affected by additional memory latencies. Don't take first invocations into account.
    static constexpr double NUM_INVOCATIONS_TO_THROW_OFF = 2;

    /// How to select method to run.
    /// -1 - automatically, based on statistics (default);
    /// -2 - choose methods in round robin fashion (for performance testing).
    /// >= 0 - always choose specified method (for performance testing);
    ssize_t choose_method = -1;

    std::vector<Element> data;

    /// It's Ok that generator is not seeded.
    pcg64 rng;

    /// To select from different algorithms we use a kind of "bandits" algorithm.
    /// Sample random values from estimated normal distributions and choose the minimal.
    size_t select()
    {
        if (choose_method < 0)
        {
            std::vector<double> samples(data.size());
            for (size_t i = 0; i < data.size(); ++i)
                samples[i] = choose_method == -1
                    ? data[i].sample(rng)
                    : data[i].adjustedCount();

            return std::min_element(samples.begin(), samples.end()) - samples.begin();
        }
        else
            return choose_method;
    }

    size_t size() {
        return data.size();
    }

    void emplace_back() {
        data.emplace_back();
    }

    PerformanceStatistics() {}
    PerformanceStatistics(ssize_t choose_method_) : choose_method(choose_method_) {}
};

struct PerformanceAdaptorOptions
{

};

/// Combine several IExecutableFunctionImpl into one.
/// All the implementations should be equivalent.
/// Implementation to execute will be selected based on performance on previous runs.
/// DefaultFunction should be executable on every supported platform, while alternative implementations
/// could use extended set of instructions (AVX, NEON, etc).
/// It's convenient to inherit your func from this and register all alternative implementations in the constructor.
template <typename DefaultFunction>
class ExecutableFunctionPerformanceAdaptor : public DefaultFunction
{
public:
    template <typename ...Params>
    ExecutableFunctionPerformanceAdaptor(Params ...params) : DefaultFunction(params...)
    {
        statistics.emplace_back();
    }

    virtual void execute(Block & block, const ColumnNumbers & arguments, size_t result, size_t input_rows_count) override
    {
        auto id = statistics.select();
        Stopwatch watch;
        if (id == 0) {
            DefaultFunction::execute(block, arguments, result, input_rows_count);
        } else {
            impls[id - 1]->execute(block, arguments, result, input_rows_count);
        }
        watch.stop();
        // TODO(dakovalkov): Calculate something more informative.
        size_t rows_summary = 0;
        for (auto i : arguments) {
            rows_summary += block.getByPosition(i).column->size();
        }
        if (rows_summary >= 1000) {
            statistics.data[id].update(watch.elapsedSeconds(), rows_summary);
        }
    }

    // Register alternative implementation.
    template<typename Function, typename ...Params>
    void registerImplementation(TargetArch arch, Params... params) {
        if (arch == TargetArch::Default || IsArchSupported(arch)) {
            impls.emplace_back(std::make_shared<Function>(params...));
            statistics.emplace_back();
        }
    }

private:
    std::vector<ExecutableFunctionImplPtr> impls; // Alternative implementations.
    PerformanceStatistics statistics;
    PerformanceAdaptorOptions options;
};

/// The same as ExecutableFunctionPerformanceAdaptor, but combine via IFunction interface.
template <typename DefaultFunction>
class FunctionPerformanceAdaptor : public DefaultFunction
{
public:
    template <typename ...Params>
    FunctionPerformanceAdaptor(Params ...params) : DefaultFunction(params...)
    {
        statistics.emplace_back();
    }

    virtual void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result, size_t input_rows_count) override
    {
        auto id = statistics.select();
        Stopwatch watch;
        if (id == 0) {
            DefaultFunction::executeImpl(block, arguments, result, input_rows_count);
        } else {
            impls[id - 1]->executeImpl(block, arguments, result, input_rows_count);
        }
        watch.stop();
        // TODO(dakovalkov): Calculate something more informative.
        size_t rows_summary = 0;
        for (auto i : arguments) {
            rows_summary += block.getByPosition(i).column->size();
        }
        if (rows_summary >= 1000) {
            statistics.data[id].update(watch.elapsedSeconds(), rows_summary);
        }
    }

    // Register alternative implementation.
    template<typename Function, typename ...Params>
    void registerImplementation(TargetArch arch, Params... params) {
        if (arch == TargetArch::Default || IsArchSupported(arch)) {
            impls.emplace_back(std::make_shared<Function>(params...));
            statistics.emplace_back();
        }
    }

private:
    std::vector<FunctionPtr> impls; // Alternative implementations.
    PerformanceStatistics statistics;
    PerformanceAdaptorOptions options;
};

} // namespace DB
