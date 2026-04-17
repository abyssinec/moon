#pragma once

namespace moon::app
{
enum CommandIDs
{
    commandNewProject = 0x2000,
    commandOpenProject,
    commandSaveProject,
    commandImportAudio,
    commandSeparateStems,
    commandRewriteRegion,
    commandAddLayer,
    commandPlay,
    commandPause,
    commandStop
};
}
