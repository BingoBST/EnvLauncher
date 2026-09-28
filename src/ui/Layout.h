#pragma once

#include "ui/Controls.h"
#include <vector>

namespace envlauncher {

class Layout {
public:
    static void CalculateLayout(
        float clientWidth,
        float clientHeight,
        float scrollOffset,
        ButtonControl& outAddButton,
        std::vector<AppCardView>& outCards,
        ButtonControl& outManageEnvsButton,
        float& outTotalContentHeight,
        float& outMaxScrollOffset
    );
};

} // namespace envlauncher
