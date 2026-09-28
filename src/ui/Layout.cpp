#include "ui/Layout.h"
#include <algorithm>

namespace envlauncher {

void Layout::CalculateLayout(
    float clientWidth,
    float clientHeight,
    float scrollOffset,
    ButtonControl& outAddButton,
    std::vector<AppCardView>& outCards,
    ButtonControl& outManageEnvsButton,
    float& outTotalContentHeight,
    float& outMaxScrollOffset
) {
    const float marginX = 28.0f;
    const float contentWidth = std::max(200.0f, clientWidth - marginX * 2.0f);

    // 1. Add Button in Header (fixed, unaffected by scroll)
    const float headerHeight = 60.0f;
    const float addBtnWidth = 140.0f;
    const float addBtnHeight = 34.0f;
    const float addBtnTop = (headerHeight - addBtnHeight) * 0.5f;
    outAddButton.text = L"+ Add Application";
    outAddButton.isAccent = true;
    outAddButton.bounds = D2D1::RectF(
        clientWidth - marginX - addBtnWidth,
        addBtnTop,
        clientWidth - marginX,
        addBtnTop + addBtnHeight
    );

    // 2. Card Stack (affected by scroll)
    const float cardsStartY = headerHeight + 16.0f;
    const float cardHeight = 106.0f;
    const float cardGap = 12.0f;

    float currentY = cardsStartY - scrollOffset;

    for (auto& card : outCards) {
        card.bounds = D2D1::RectF(
            marginX,
            currentY,
            marginX + contentWidth,
            currentY + cardHeight
        );

        // Position Action Buttons in card (right aligned: Delete, Edit, Launch)
        const float btnHeight = 32.0f;
        const float btnTop = card.bounds.top + (cardHeight - btnHeight) * 0.5f;
        const float btnBottom = btnTop + btnHeight;
        float curBtnRight = card.bounds.right - 16.0f;

        // 1. Launch Button
        const float launchBtnWidth = 80.0f;
        card.launchButton.text = L"Launch";
        card.launchButton.targetId = card.id;
        card.launchButton.isAccent = card.executableExists;
        card.launchButton.state = card.executableExists ? card.launchButton.state : InteractionState::Disabled;
        card.launchButton.bounds = D2D1::RectF(
            curBtnRight - launchBtnWidth,
            btnTop,
            curBtnRight,
            btnBottom
        );
        curBtnRight -= launchBtnWidth + 8.0f;

        // 2. Edit Button
        const float editBtnWidth = 58.0f;
        card.editButton.text = L"Edit";
        card.editButton.targetId = card.id;
        card.editButton.isAccent = false;
        card.editButton.bounds = D2D1::RectF(
            curBtnRight - editBtnWidth,
            btnTop,
            curBtnRight,
            btnBottom
        );
        curBtnRight -= editBtnWidth + 8.0f;

        // 3. Delete Button
        const float deleteBtnWidth = 64.0f;
        card.deleteButton.text = L"Delete";
        card.deleteButton.targetId = card.id;
        card.deleteButton.isAccent = false;
        card.deleteButton.bounds = D2D1::RectF(
            curBtnRight - deleteBtnWidth,
            btnTop,
            curBtnRight,
            btnBottom
        );

        currentY += cardHeight + cardGap;
    }

    if (outCards.empty()) {
        const float emptyStateHeight = 140.0f;
        currentY = cardsStartY + emptyStateHeight;
    }

    // 3. Manage Environments Button at bottom of card list
    const float manageBtnWidth = 180.0f;
    const float manageBtnHeight = 34.0f;
    outManageEnvsButton.text = L"Manage Environments";
    outManageEnvsButton.isAccent = false;
    outManageEnvsButton.bounds = D2D1::RectF(
        marginX,
        currentY + 4.0f,
        marginX + manageBtnWidth,
        currentY + 4.0f + manageBtnHeight
    );

    // 4. Calculate total content height and max scroll
    outTotalContentHeight = currentY + manageBtnHeight + 40.0f;
    outMaxScrollOffset = std::max(0.0f, outTotalContentHeight - clientHeight);
}

} // namespace envlauncher
