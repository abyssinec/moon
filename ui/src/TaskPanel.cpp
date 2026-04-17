#include "TaskPanel.h"

#if MOON_HAS_JUCE
namespace moon::ui
{
TaskPanel::TaskPanel(moon::engine::TaskManager& taskManager, moon::engine::Logger& logger)
    : taskManager_(taskManager)
    , logger_(logger)
{
}

void TaskPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour::fromRGB(38, 42, 48));
    auto area = getLocalBounds().reduced(8);

    g.setColour(juce::Colours::white);
    g.drawText("Tasks & Logs", area.removeFromTop(24), juce::Justification::centredLeft);

    const auto activeTasks = taskManager_.activeTaskCount();
    g.setColour(activeTasks > 0 ? juce::Colours::orange : juce::Colours::lightgreen);
    g.drawText("Active tasks: " + juce::String(static_cast<int>(activeTasks)), area.removeFromTop(20), juce::Justification::centredLeft);
    g.setColour(juce::Colours::lightgrey);
    g.drawText(
        "Completed: " + juce::String(static_cast<int>(taskManager_.completedTaskCount()))
            + " | Failed: " + juce::String(static_cast<int>(taskManager_.failedTaskCount())),
        area.removeFromTop(20),
        juce::Justification::centredLeft);

    const auto logPathText = "Log file: " + logger_.logFilePath().string();
    g.setColour(juce::Colours::lightblue);
    g.drawText(logPathText, area.removeFromTop(20), juce::Justification::centredLeft);

    if (const auto latestFailure = taskManager_.latestFailedTask(); latestFailure.has_value())
    {
        auto failureArea = area.removeFromTop(52);
        g.setColour(juce::Colour::fromRGB(92, 32, 32));
        g.fillRoundedRectangle(failureArea.toFloat(), 6.0f);
        g.setColour(juce::Colours::salmon);
        g.drawText(
            "Latest failure: " + latestFailure->type + " [" + latestFailure->id + "]",
            failureArea.removeFromTop(22).reduced(8, 2),
            juce::Justification::centredLeft);
        g.setColour(juce::Colours::white);
        g.drawText(
            latestFailure->message.empty() ? "No backend error message was provided." : latestFailure->message,
            failureArea.reduced(8, 2),
            juce::Justification::topLeft,
            true);
        area.removeFromTop(4);
    }

    auto tasksArea = area.removeFromTop(getHeight() / 2 - 24);
    int y = tasksArea.getY();
    for (const auto& task : taskManager_.recentTasks())
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

        g.setColour(juce::Colour::fromRGB(55, 60, 68));
        g.fillRoundedRectangle(8.0f, static_cast<float>(y), static_cast<float>(getWidth() - 16), 34.0f, 6.0f);
        g.setColour(statusColour);
        g.drawText(task.id + " [" + task.status + "] " + task.message, 16, y + 4, getWidth() - 32, 14, juce::Justification::centredLeft);

        const auto progressWidth = static_cast<int>((getWidth() - 32) * juce::jlimit(0.0, 1.0, task.progress));
        g.setColour(juce::Colour::fromRGB(28, 31, 36));
        g.fillRoundedRectangle(16.0f, static_cast<float>(y + 20), static_cast<float>(getWidth() - 32), 8.0f, 3.0f);
        g.setColour(statusColour);
        g.fillRoundedRectangle(16.0f, static_cast<float>(y + 20), static_cast<float>(progressWidth), 8.0f, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.drawText(juce::String(task.progress * 100.0, 0) + "%", getWidth() - 56, y + 4, 40, 14, juce::Justification::centredRight);

        y += 40;
        if (y > tasksArea.getBottom() - 30)
        {
            break;
        }
    }

    auto logsArea = area;
    y = logsArea.getY() + 8;
    g.setColour(juce::Colours::white);
    g.drawText("Recent logs", logsArea.removeFromTop(20), juce::Justification::centredLeft);
    for (const auto& line : logger_.recent())
    {
        juce::Colour lineColour = juce::Colours::lightgrey;
        if (line.find("[ERROR]") != std::string::npos)
        {
            lineColour = juce::Colours::salmon;
        }
        else if (line.find("[WARN]") != std::string::npos)
        {
            lineColour = juce::Colours::khaki;
        }

        g.setColour(lineColour);
        g.drawText(line, 8, y, getWidth() - 16, 18, juce::Justification::centredLeft);
        y += 16;
        if (y > getHeight() - 18)
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
