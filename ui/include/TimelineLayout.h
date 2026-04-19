#pragma once

namespace moon::ui::layout
{
inline constexpr int kTrackRowStartY = 4;
inline constexpr int kTrackRowStep = 72;
inline constexpr int kTrackRowHeight = 66;
inline constexpr int kClipRowHeight = 58;
inline constexpr int kTrackContentBottomPadding = 16;

inline constexpr int trackRowY(int rowIndex)
{
    return kTrackRowStartY + rowIndex * kTrackRowStep;
}

inline constexpr int trackContentHeight(int trackCount)
{
    return kTrackRowStartY + trackCount * kTrackRowStep + kTrackContentBottomPadding;
}
}
