#pragma once

#include "Logger.h"
#include "TaskManager.h"

#if MOON_HAS_JUCE
#include <juce_gui_basics/juce_gui_basics.h>

namespace moon::ui
{
class TaskPanel final : public juce::Component
{
public:
    TaskPanel(moon::engine::TaskManager& taskManager, moon::engine::Logger& logger);
    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void updateContentSize(int availableWidth);

private:
    moon::engine::TaskManager& taskManager_;
};
}
#else
namespace moon::ui
{
class TaskPanel
{
public:
    TaskPanel(moon::engine::TaskManager&, moon::engine::Logger&) {}
};
}
#endif
