#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace distributed_consistency
{

enum class StepResult
{
    kSuccess,
    kRetryableFailure,
    kPermanentFailure,
};

struct SagaContext
{
    std::unordered_map<std::string, std::string> attributes;
};

struct SagaExecutionReport
{
    bool committed = false;
    std::vector<std::string> completed_steps;
    std::vector<std::string> compensated_steps;
    std::optional<std::string> failed_step;
};

class SagaCoordinator
{
  public:
    using StepAction = std::function<StepResult(SagaContext &)>;
    using CompensationAction = std::function<void(SagaContext &)>;

    void add_step(std::string name, StepAction action, CompensationAction compensation)
    {
        steps_.push_back(SagaStep{
            .name = std::move(name),
            .action = std::move(action),
            .compensation = std::move(compensation),
        });
    }

    [[nodiscard]] SagaExecutionReport execute(SagaContext &context) const
    {
        SagaExecutionReport report;

        for (const auto &step : steps_)
        {
            const StepResult result = step.action(context);
            if (result == StepResult::kSuccess)
            {
                report.completed_steps.push_back(step.name);
                continue;
            }

            report.failed_step = step.name;
            compensate(context, report);
            return report;
        }

        report.committed = true;
        return report;
    }

  private:
    struct SagaStep
    {
        std::string name;
        StepAction action;
        CompensationAction compensation;
    };

    void compensate(SagaContext &context, SagaExecutionReport &report) const
    {
        for (auto iterator = report.completed_steps.rbegin(); iterator != report.completed_steps.rend(); ++iterator)
        {
            auto step_iterator = std::find_if(steps_.begin(), steps_.end(),
                                              [&](const SagaStep &step) { return step.name == *iterator; });

            if (step_iterator != steps_.end())
            {
                step_iterator->compensation(context);
                report.compensated_steps.push_back(step_iterator->name);
            }
        }
    }

    std::vector<SagaStep> steps_;
};

} // namespace distributed_consistency
