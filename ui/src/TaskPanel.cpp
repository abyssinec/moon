#include "TaskPanel.h"

#if MOON_HAS_JUCE
namespace moon::ui
{
TaskPanel::TaskPanel(moon::engine::TaskManager& taskManager, moon::engine::Logger&)
    : taskManager_(taskManager)
{
}

void TaskPanel::updateContentSize(int availableWidth)
{
    const auto recentTasks = taskManager_.recentTasks();
    const int failureHeight = taskManager_.latestFailedTask().has_value() ? 50 : 0;
    const int taskHeight = static_cast<int>(recentTasks.size()) * 34;
    const int contentHeight = 66 + failureHeight + taskHeight;
    setSize(juce::jmax(300, availableWidth), juce::jmax(120, contentHeight));
}

void TaskPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(19, 21, 25));
    auto area = getLocalBounds().reduced(10, 8);

    g.setColour(juce::Colours::white.withAlpha(0.92f));
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.drawText("Tasks", area.removeFromTop(20), juce::Justification::centredLeft);

    const auto activeTasks = taskManager_.activeTaskCount();
    g.setColour(activeTasks > 0 ? juce::Colours::orange : juce::Colours::lightgreen);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("Active " + juce::String(static_cast<int>(activeTasks)), area.removeFromTop(16), juce::Justification::centredLeft);
    g.setColour(juce::Colours::lightgrey.withAlpha(0.72f));
    g.drawText(
        "Done " + juce::String(static_cast<int>(taskManager_.completedTaskCount()))
            + "  |  Failed " + juce::String(static_cast<int>(taskManager_.failedTaskCount())),
        area.removeFromTop(16),
        juce::Justification::centredLeft);

    if (const auto latestFailure = taskManager_.latestFailedTask(); latestFailure.has_value())
    {
        auto failureArea = area.removeFromTop(46);
        g.setColour(juce::Colour::fromRGB(72, 28, 28));
        g.fillRoundedRectangle(failureArea.toFloat(), 6.0f);
        g.setColour(juce::Colours::salmon);
        g.drawText(
            "Latest failure: " + latestFailure->type + " [" + latestFailure->id + "]",
            failureArea.removeFromTop(20).reduced(8, 1),
            juce::Justification::centredLeft);
        g.setColour(juce::Colours::white);
        g.drawText(
            latestFailure->message.empty() ? "No backend error message was provided." : latestFailure->message,
            failureArea.reduced(8, 2),
            juce::Justification::topLeft,
            true);
        area.removeFromTop(2);
    }

    const auto recentTasks = taskManager_.recentTasks();
    auto tasksArea = area.removeFromTop(juce::jmax(44, static_cast<int>(recentTasks.size()) * 34 + 12));
    int y = tasksArea.getY();
    for (const auto& task : recentTasks)
    {
        juce::Colour statusColour = juce::Colours::lightblue;
        if (task.status == "completed")
        {
            statusColour = juce::Colours::lightgreen;
        }
        else if (task.status == "failed")
        {
            statusColour = juce::Colours::red;
        }
        else if (task.status == "running")
        {
            statusColour = juce::Colours::orange;
        }

        g.setColour(juce::Colour::fromRGB(29, 33, 39));
        g.fillRoundedRectangle(8.0f, static_cast<float>(y), static_cast<float>(getWidth() - 16), 28.0f, 6.0f);
        g.setColour(statusColour);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText(task.id + " [" + task.status + "] " + task.message, 16, y + 3, getWidth() - 32, 12, juce::Justification::centredLeft);

        const auto progressWidth = static_cast<int>((getWidth() - 32) * juce::jlimit(0.0, 1.0, task.progress));
        g.setColour(juce::Colour::fromRGB(28, 31, 36));
        g.fillRoundedRectangle(16.0f, static_cast<float>(y + 17), static_cast<float>(getWidth() - 32), 6.0f, 3.0f);
        g.setColour(statusColour);
        g.fillRoundedRectangle(16.0f, static_cast<float>(y + 17), static_cast<float>(progressWidth), 6.0f, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(juce::String(task.progress * 100.0, 0) + "%", getWidth() - 56, y + 2, 40, 12, juce::Justification::centredRight);

        y += 34;
        if (y > tasksArea.getBottom() - 24)
        {
            break;
        }
    }
}

void TaskPanel::mouseUp(const juce::MouseEvent&)
{
    repaint();
}
}
#endif
