#include "main_activity.hpp"

#include "app_state.hpp"
#include "boosteroid_client.hpp"
#include "main_tabs_view.hpp"
#include "LoginView.hpp"
#include "localization.hpp"
#include "runtime_journal.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>

// MARK: - MainActivity / StartupGateView (Nintendo Switch port, Boosteroid protocol)
//
// Much simpler than SwitchNOW's original: Boosteroid's cookie-session auth
// has no CONFIRMED refresh-token exchange (see models.hpp's AuthTokens and
// boosteroid_client.hpp's LoadSavedSession doc comment), so there is no
// ReauthenticationRequired exception to catch and no automatic-credentialed
// re-login path to drive here. This just does a best-effort validation of a
// saved session's cookies (GET /api/v1/user) and always proceeds to
// MainTabsView either way — a truly dead session surfaces naturally the
// first time a tab makes an authenticated call (see LibraryTab::ReloadLibrary),
// at which point the user can sign back in via LoginView from the Library tab.
//
// UX fix 2026-07-31 (user feedback on real hardware): when there is NO saved
// session at all (first launch, or after a logout), this used to drop
// straight into MainTabsView/LibraryTab with a small "Sign in" button
// buried in its account row — confusing on a fresh install. Now it shows
// LoginView directly as the startup screen in that case, and only proceeds
// to MainTabsView once login actually succeeds (see ShowLogin()/ShowMainTabs()
// below). A saved-but-unvalidated session still goes through BeginValidation
// and MainTabsView as before, since LibraryTab's own error handling already
// covers a session that turns out to be dead.
namespace opennow
{
namespace
{

class StartupPulseView final : public brls::View
{
  public:
    StartupPulseView()
    {
        setWidth(76);
        setHeight(76);
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override
    {
        (void)style;
        (void)ctx;
        constexpr float kPi = 3.14159265358979323846f;
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const float cx = x + width * 0.5f;
        const float cy = y + height * 0.5f;
        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(seconds * 3.0));

        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 28.0f + pulse * 4.0f);
        nvgFillColor(vg, nvgRGBA(77, 218, 130, 18 + static_cast<int>(pulse * 22.0f)));
        nvgFill(vg);

        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, 21.0f);
        nvgStrokeWidth(vg, 5.0f);
        nvgStrokeColor(vg, nvgRGB(35, 43, 48));
        nvgStroke(vg);

        const float angle = static_cast<float>(
            std::fmod(seconds * 3.2, static_cast<double>(kPi) * 2.0));
        nvgBeginPath(vg);
        nvgArc(vg, cx, cy, 21.0f, angle, angle + kPi * 1.42f, NVG_CW);
        nvgStrokeWidth(vg, 5.0f);
        nvgLineCap(vg, NVG_ROUND);
        nvgStrokeColor(vg, nvgRGB(77, 218, 130));
        nvgStroke(vg);

        const float head_angle = angle + kPi * 1.42f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx + std::cos(head_angle) * 21.0f,
                  cy + std::sin(head_angle) * 21.0f, 3.4f);
        nvgFillColor(vg, nvgRGB(123, 242, 166));
        nvgFill(vg);
    }
};

class StartupGateView final : public brls::Box
{
  public:
    StartupGateView()
        : brls::Box(brls::Axis::COLUMN)
    {
        setGrow(1.0f);
        setAlignItems(brls::AlignItems::CENTER);
        setJustifyContent(brls::JustifyContent::CENTER);
        setBackgroundColor(nvgRGB(10, 12, 15));

        BoosteroidClient client;
        AuthSession saved;
        if (!client.LoadSavedSession(saved))
        {
            LogRuntimeEvent("auth", "startup_no_saved_session");
            AppState::Instance().MarkSessionLoaded();
            ShowLogin();
            return;
        }

        LogRuntimeEvent("auth", "startup_saved_session_found");
        BuildStatusView(saved);
        BeginValidation(std::move(client), std::move(saved));
    }

    ~StartupGateView() override
    {
        alive_->store(false);
    }

  private:
    void BuildStatusView(const AuthSession& saved)
    {
        auto* brand = new brls::Label();
        brand->setText("Boosteroid");
        brand->setFontSize(30);
        brand->setTextColor(nvgRGB(77, 218, 130));
        brand->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        brand->setMarginBottom(34);
        addView(brand);

        auto* card = new brls::Box(brls::Axis::COLUMN);
        card->setWidth(620);
        card->setPadding(28, 38, 30, 38);
        card->setAlignItems(brls::AlignItems::CENTER);
        card->setBackgroundColor(nvgRGB(18, 21, 25));
        card->setCornerRadius(18);

        auto* eyebrow = new brls::Label();
        eyebrow->setText(Tr("SAVED ACCOUNT"));
        eyebrow->setFontSize(13);
        eyebrow->setTextColor(nvgRGB(77, 218, 130));
        eyebrow->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        eyebrow->setMarginBottom(8);
        card->addView(eyebrow);

        auto* account = new brls::Label();
        account->setText(saved.user.display_name.empty() ? saved.user.email : saved.user.display_name);
        account->setFontSize(25);
        account->setTextColor(nvgRGB(242, 246, 248));
        account->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        account->setMarginBottom(14);
        card->addView(account);

        card->addView(new StartupPulseView());

        status_label_ = new brls::Label();
        status_label_->setText(Tr("Checking saved sign-in"));
        status_label_->setFontSize(21);
        status_label_->setTextColor(nvgRGB(238, 242, 245));
        status_label_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        status_label_->setMarginTop(8);
        status_label_->setMarginBottom(7);
        card->addView(status_label_);

        detail_label_ = new brls::Label();
        detail_label_->setText(Tr("Validating the saved cookies before loading your library."));
        detail_label_->setFontSize(15);
        detail_label_->setTextColor(nvgRGB(145, 153, 164));
        detail_label_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        card->addView(detail_label_);
        addView(card);
    }

    void Complete(AuthSession session)
    {
        const auto alive = alive_;
        brls::sync([this, alive, session = std::move(session)]() mutable {
            if (!alive->load() || finished_)
                return;
            AppState::Instance().SetSession(std::move(session));
            ShowMainTabs();
        });
    }

    void BeginValidation(BoosteroidClient client, AuthSession saved)
    {
        const auto alive = alive_;
        const std::uint64_t operation_id =
            BeginRuntimeOperation("auth", "startup_session_validation");

        const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (saved.tokens.IsExpired(now_ms))
        {
            saved.reauthentication_required = true;
            EndRuntimeOperation(operation_id, "auth", "startup_session_validation", "expired");
            Complete(std::move(saved));
            return;
        }

        brls::async([this, alive, operation_id, client = std::move(client),
                     saved = std::move(saved)]() mutable {
            try
            {
                CookieJar cookies(saved.tokens.session_cookies.begin(), saved.tokens.session_cookies.end());
                AuthUser refreshed_user = client.FetchCurrentUser(cookies);
                if (!alive->load())
                {
                    EndRuntimeOperation(operation_id, "auth", "startup_session_validation", "cancelled");
                    return;
                }
                saved.user = refreshed_user;
                saved.reauthentication_required = false;
                client.SaveSession(saved);
                EndRuntimeOperation(operation_id, "auth", "startup_session_validation", "ok");
                Complete(std::move(saved));
            }
            catch (const std::exception& ex)
            {
                if (!alive->load())
                {
                    EndRuntimeOperation(operation_id, "auth", "startup_session_validation", "cancelled");
                    return;
                }
                // Keep the saved account on a temporary/network failure — the
                // Library tab's own error handling is the real signal to the
                // user if the cookies are actually dead, not this best-effort
                // startup probe.
                EndRuntimeOperation(operation_id, "auth", "startup_session_validation",
                                     "temporary_error", ex.what());
                Complete(std::move(saved));
            }
        }, false);
    }

    void ShowMainTabs()
    {
        if (finished_)
            return;
        finished_ = true;
        clearViews();
        setAlignItems(brls::AlignItems::STRETCH);
        setJustifyContent(brls::JustifyContent::FLEX_START);
        setPadding(0);
        auto* tabs = new MainTabsView();
        tabs->setWidth(brls::Application::windowWidth);
        tabs->setHeight(brls::Application::windowHeight);
        tabs->setGrow(1.0f);
        addView(tabs);
        invalidate();
        brls::Application::giveFocus(tabs);
    }

    void ShowLogin()
    {
        if (finished_)
            return;
        clearViews();
        setAlignItems(brls::AlignItems::CENTER);
        setJustifyContent(brls::JustifyContent::CENTER);
        setPadding(0);
        auto* login = new LoginView([this](AuthSession session) {
            AppState::Instance().SetSession(std::move(session));
            ShowMainTabs();
        });
        addView(login);
        invalidate();
        brls::Application::giveFocus(login);
    }

    brls::Label* status_label_ = nullptr;
    brls::Label* detail_label_ = nullptr;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    bool finished_ = false;
};

} // namespace

MainActivity::MainActivity()
    : brls::Activity(new StartupGateView())
{
}

} // namespace opennow
