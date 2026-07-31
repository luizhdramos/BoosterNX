#include "library_tab.hpp"

#include "app_state.hpp"
#include "game_card_view.hpp"
#include "game_detail_view.hpp"
#include "library_sort.hpp"
#include "localization.hpp"
#include "LoginView.hpp"
#include "play_history.hpp"
#include "ui_helpers.hpp"

#include <algorithm>
#include <cctype>

namespace opennow
{
namespace
{

constexpr size_t kCardsPerRow = 5;
constexpr size_t kInitialVisibleLimit = 15;

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

const char* SortModeLabel(LibrarySortMode mode)
{
    switch (mode)
    {
        case LibrarySortMode::LastPlayed: return "Sort: Last played";
        case LibrarySortMode::LastAdded: return "Sort: Last added";
        case LibrarySortMode::Title: return "Sort: Title";
    }
    return "Sort";
}

} // namespace

LibraryTab::LibraryTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(28, 40, 28, 40);

    auto* header = new brls::Header();
    header->setTitle("Library");
    header->setSubtitle("Your Boosteroid library");
    addView(header);

    auto* account_row = new brls::Box(brls::Axis::ROW);
    account_row->setMarginBottom(10);
    account_row->setAlignItems(brls::AlignItems::CENTER);

    account_label_ = new brls::Label();
    account_label_->setFontSize(16);
    account_label_->setTextColor(nvgRGB(151, 159, 170));
    account_label_->setGrow(1.0f);
    account_row->addView(account_label_);

    sign_in_button_ = new brls::Button();
    sign_in_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    sign_in_button_->setText(Tr("Sign in"));
    sign_in_button_->setMarginRight(10);
    sign_in_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        BeginLogin();
        return true;
    });
    account_row->addView(sign_in_button_);

    logout_button_ = new brls::Button();
    logout_button_->setText(Tr("Sign out"));
    logout_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        Logout();
        return true;
    });
    account_row->addView(logout_button_);
    addView(account_row);

    status_label_ = new brls::Label();
    status_label_->setFontSize(15);
    status_label_->setTextColor(nvgRGB(151, 159, 170));
    status_label_->setMarginBottom(12);
    addView(status_label_);

    auto* toolbar = new brls::Box(brls::Axis::ROW);
    toolbar->setMarginBottom(14);

    search_button_ = new brls::Button();
    search_button_->setText(Tr("Search library"));
    search_button_->setMarginRight(10);
    search_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        BeginSearch();
        return true;
    });
    toolbar->addView(search_button_);

    sort_button_ = new brls::Button();
    sort_button_->setText(Tr(SortModeLabel(LibrarySortMode::LastPlayed)));
    sort_button_->setMarginRight(10);
    sort_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        CycleSortMode();
        return true;
    });
    toolbar->addView(sort_button_);

    refresh_button_ = new brls::Button();
    refresh_button_->setText(Tr("Refresh"));
    refresh_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        ReloadLibrary(false);
        return true;
    });
    toolbar->addView(refresh_button_);
    addView(toolbar);

    scrolling_frame_ = new brls::ScrollingFrame();
    scrolling_frame_->setGrow(1.0f);
    scrolling_frame_->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    grid_container_ = new brls::Box(brls::Axis::COLUMN);
    grid_container_->setPadding(0, 0, 30, 0);
    scrolling_frame_->setContentView(grid_container_);
    addView(scrolling_frame_);

    registerAction(Tr("Search"), brls::BUTTON_Y, [this](brls::View* view) {
        (void)view;
        BeginSearch();
        return true;
    }, false, true);

    registerAction(Tr("Refresh"), brls::BUTTON_X, [this](brls::View* view) {
        (void)view;
        ReloadLibrary(false);
        return true;
    }, false, true);
}

LibraryTab::~LibraryTab()
{
    alive_->store(false);
}

void LibraryTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    EnsureSessionLoaded();
    UpdateSessionUi();

    auto& state = AppState::Instance();
    if (state.HasLibraryGames())
    {
        games_ = state.library_games();
        ApplyPlayHistory(games_, LoadPlayHistory());
        RebuildGrid();
    }
    else if (state.HasSession())
    {
        ReloadLibrary(true);
    }
    else
    {
        RebuildGrid();
    }
}

void LibraryTab::EnsureSessionLoaded()
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

void LibraryTab::UpdateSessionUi()
{
    auto& state = AppState::Instance();
    const bool signed_in = state.HasSession();

    if (signed_in)
    {
        const auto& user = state.session()->user;
        account_label_->setText(Tr("Signed in as") + " " +
            (user.display_name.empty() ? user.email : user.display_name));
    }
    else
    {
        account_label_->setText(Tr("Not signed in"));
    }

    sign_in_button_->setVisibility(signed_in ? brls::Visibility::GONE : brls::Visibility::VISIBLE);
    logout_button_->setVisibility(signed_in ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
    refresh_button_->setVisibility(signed_in ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
}

void LibraryTab::BeginLogin()
{
    brls::Application::pushActivity(new brls::Activity(new LoginView([this](AuthSession session) {
        AppState::Instance().SetSession(std::move(session));
        brls::Application::popActivity();
        UpdateSessionUi();
        ReloadLibrary(false);
    })));
}

void LibraryTab::Logout()
{
    client_.ClearSavedSession();
    AppState::Instance().ClearSession();
    games_.clear();
    UpdateSessionUi();
    RebuildGrid();
}

void LibraryTab::ReloadLibrary(bool background)
{
    auto& state = AppState::Instance();
    if (loading_ || !state.HasSession())
        return;

    loading_ = true;
    if (!background)
        status_label_->setText(Tr("Refreshing library..."));

    BoosteroidClient bg_client = client_;
    AuthSession bg_auth = *state.session();
    const auto alive = alive_;

    brls::async([this, alive, bg_client, bg_auth]() mutable {
        std::vector<GameInfo> games;
        std::string error;
        try
        {
            CookieJar cookies(bg_auth.tokens.session_cookies.begin(), bg_auth.tokens.session_cookies.end());
            games = bg_client.FetchLibrary(cookies);
        }
        catch (const std::exception& ex)
        {
            error = ex.what();
        }

        brls::sync([this, alive, games, error]() mutable {
            if (!alive->load())
                return;
            loading_ = false;
            if (!error.empty())
            {
                status_label_->setText(Tr("Could not refresh library") + ": " + error);
                return;
            }
            ApplyPlayHistory(games, LoadPlayHistory());
            games_ = std::move(games);
            AppState::Instance().SetLibraryGames(games_);
            status_label_->setText("");
            RebuildGrid();
        });
    }, false);
}

void LibraryTab::BeginSearch()
{
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            search_query_ = std::move(text);
            visible_limit_ = kInitialVisibleLimit;
            RebuildGrid();
        },
        Tr("Search library"),
        Tr("Filter your library by title"),
        64,
        search_query_);
}

void LibraryTab::CycleSortMode()
{
    sort_mode_index_ = (sort_mode_index_ + 1) % 3;
    sort_button_->setText(Tr(SortModeLabel(static_cast<LibrarySortMode>(sort_mode_index_))));
    RebuildGrid();
}

void LibraryTab::RebuildGrid()
{
    grid_container_->clearViews();
    load_more_button_ = nullptr;

    if (!AppState::Instance().HasSession())
    {
        auto* empty_label = new brls::Label();
        empty_label->setText(Tr("Sign in to Boosteroid to see your library."));
        empty_label->setFontSize(18);
        grid_container_->addView(empty_label);
        return;
    }

    std::vector<size_t> indices;
    const std::string query = ToLower(search_query_);
    for (size_t i = 0; i < games_.size(); ++i)
    {
        if (!query.empty() && ToLower(games_[i].title).find(query) == std::string::npos)
            continue;
        indices.push_back(i);
    }

    SortLibraryIndices(indices, games_, static_cast<LibrarySortMode>(sort_mode_index_));

    if (indices.empty())
    {
        auto* empty_label = new brls::Label();
        empty_label->setText(Tr(games_.empty() ? "No games in your library yet." : "No games match your search."));
        empty_label->setFontSize(18);
        grid_container_->addView(empty_label);
        return;
    }

    const size_t visible_count = std::min(indices.size(), visible_limit_);
    for (size_t start = 0; start < visible_count; start += kCardsPerRow)
    {
        auto* row = new brls::Box(brls::Axis::ROW);
        row->setMarginBottom(2);

        const size_t end = std::min(start + kCardsPerRow, visible_count);
        for (size_t i = start; i < end; ++i)
        {
            const size_t index = indices[i];
            const GameInfo& game = games_[index];

            GameCardDisplay display;
            display.title = game.title;
            display.subtitle = game.last_played.empty() ? "" : (Tr("Last played") + " " + game.last_played);
            display.image_url = game.icon_url;

            row->addView(new GameCardView(display, [this, index]() { OpenGameDialog(index); }));
        }
        grid_container_->addView(row);
    }

    if (indices.size() > visible_count)
    {
        load_more_button_ = new brls::Button();
        load_more_button_->setText(Tr("Show more") + " (" + std::to_string(indices.size() - visible_count) + ")");
        load_more_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
        load_more_button_->setMarginTop(14);
        load_more_button_->registerClickAction([this](brls::View* view) {
            (void)view;
            // Deferred via brls::sync: LoadMore() -> RebuildGrid() calls
            // grid_container_->clearViews(), which frees this very button
            // (freeView(), see Box::clearViews) — calling it synchronously
            // from inside the button's own click handler frees the object
            // whose member function is still on the call stack, crashing
            // once the click dispatch returns. CONFIRMED crash on real
            // hardware 2026-07-31 (crash on tapping "Show more"). Deferring
            // to the next frame lets the click dispatch unwind first.
            brls::sync([this]() { LoadMore(); });
            return true;
        });
        grid_container_->addView(load_more_button_);
    }
}

void LibraryTab::LoadMore()
{
    visible_limit_ += kInitialVisibleLimit;
    RebuildGrid();
}

bool LibraryTab::OpenGameDialog(size_t index)
{
    if (index >= games_.size())
        return false;

    brls::Application::pushActivity(new brls::Activity(new GameDetailView(
        client_, MakeGameDetail(games_[index]))));
    return true;
}

} // namespace opennow
