#include "search_tab.hpp"

#include "app_state.hpp"
#include "game_card_view.hpp"
#include "game_detail_view.hpp"
#include "localization.hpp"
#include "ui_helpers.hpp"

#include <algorithm>
#include <cctype>

namespace opennow
{
namespace
{

constexpr size_t kCardsPerRow = 5;
constexpr size_t kInitialVisibleResultLimit = 30;

brls::Label* MakeParagraph(const std::string& text, float bottom_margin = 16.0f)
{
    auto* label = new brls::Label();
    label->setText(Tr(text));
    label->setFontSize(18);
    label->setMarginBottom(bottom_margin);
    return label;
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

SearchTab::SearchTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(28, 40, 28, 40);

    auto* header = new brls::Header();
    header->setTitle("Search");
    header->setSubtitle("Search your Boosteroid library. Y searches.");
    addView(header);

    status_label_ = MakeParagraph("Press Y to search your library.");
    addView(status_label_);

    search_button_ = new brls::Button();
    search_button_->setText(Tr("Search library"));
    search_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    search_button_->setMarginBottom(14);
    search_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        OpenSearchIme();
        return true;
    });
    addView(search_button_);

    scrolling_frame_ = new brls::ScrollingFrame();
    scrolling_frame_->setGrow(1.0f);
    scrolling_frame_->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    results_container_ = new brls::Box(brls::Axis::COLUMN);
    results_container_->setPadding(0, 0, 30, 0);
    scrolling_frame_->setContentView(results_container_);
    addView(scrolling_frame_);

    registerAction(Tr("Search"), brls::BUTTON_Y, [this](brls::View* view) {
        (void)view;
        OpenSearchIme();
        return true;
    }, false, true);
}

void SearchTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    EnsureSessionLoaded();

    const auto& state = AppState::Instance();
    if (state.HasLibraryGames())
        library_games_ = state.library_games();

    RebuildResults();
}

void SearchTab::EnsureSessionLoaded()
{
    auto& state = AppState::Instance();
    if (state.IsSessionLoaded())
        return;

    AuthSession session;
    if (client_.LoadSavedSession(session))
        state.SetSession(std::move(session));
    else
        state.MarkSessionLoaded();
}

void SearchTab::OpenSearchIme()
{
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            search_query_ = std::move(text);
            visible_limit_ = kInitialVisibleResultLimit;
            RebuildResults();
        },
        Tr("Search Library"),
        Tr("Enter a title from your library"),
        64,
        search_query_);
}

void SearchTab::RebuildResults()
{
    results_container_->clearViews();
    load_more_button_ = nullptr;
    result_count_ = 0;

    const std::string query = ToLower(search_query_);
    if (query.empty())
    {
        status_label_->setText(
            Tr("Press Y to search.") + " " + Tr("Library games cached") + ": " + std::to_string(library_games_.size()));
        results_container_->addView(MakeParagraph(
            "Search looks through your signed-in Boosteroid library. Open the Library tab to sign in or refresh first.",
            0.0f));
        return;
    }

    std::vector<size_t> results;
    for (size_t i = 0; i < library_games_.size(); ++i)
    {
        if (ToLower(library_games_[i].title).find(query) != std::string::npos)
            results.push_back(i);
    }

    result_count_ = results.size();
    const size_t visible_count = std::min(result_count_, visible_limit_);

    status_label_->setText(
        Tr("Query") + ": " + search_query_ + " | " + Tr("Results") + ": " + std::to_string(result_count_));

    if (results.empty())
    {
        results_container_->addView(MakeParagraph("No matching library games found.", 0.0f));
        return;
    }

    for (size_t start = 0; start < visible_count; start += kCardsPerRow)
    {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(2);

        const size_t end = std::min(start + kCardsPerRow, visible_count);
        for (size_t i = start; i < end; ++i)
        {
            const size_t index = results[i];
            const GameInfo& game = library_games_[index];

            GameCardDisplay display;
            display.title = game.title;
            display.subtitle = game.last_played;
            display.image_url = game.icon_url;

            row->addView(new GameCardView(display, [this, index]() { OpenResult(index); }));
        }
        results_container_->addView(row);
    }

    if (result_count_ > visible_count)
    {
        load_more_button_ = new brls::Button();
        load_more_button_->setText(Tr("Show more") + " (" + std::to_string(result_count_ - visible_count) + ")");
        load_more_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        load_more_button_->setMarginTop(14);
        load_more_button_->setMarginBottom(24);
        load_more_button_->registerClickAction([this](brls::View* view) {
            (void)view;
            // See library_tab.cpp's identical fix: LoadMoreResults() ->
            // RebuildResults() -> results_container_->clearViews() frees
            // this button while its own click handler is still executing —
            // defer to the next frame via brls::sync to avoid the crash.
            brls::sync([this]() { LoadMoreResults(); });
            return true;
        });
        results_container_->addView(load_more_button_);
    }
}

void SearchTab::LoadMoreResults()
{
    if (result_count_ > visible_limit_)
    {
        visible_limit_ += kInitialVisibleResultLimit;
        RebuildResults();
    }
}

bool SearchTab::OpenResult(size_t index)
{
    if (index >= library_games_.size())
        return false;

    brls::Application::pushActivity(new brls::Activity(new GameDetailView(
        client_, MakeGameDetail(library_games_[index]))));
    return true;
}

} // namespace opennow
