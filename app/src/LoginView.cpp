#include "LoginView.hpp"

#include "localization.hpp"
#include "ui_helpers.hpp"

namespace opennow
{
namespace
{

std::string MaskedPassword(const std::string& password)
{
    return password.empty() ? std::string() : std::string(password.size(), '*');
}

} // namespace

LoginView::LoginView(std::function<void(AuthSession)> on_success)
    : brls::Box(brls::Axis::COLUMN), on_success_(std::move(on_success))
{
    setPadding(40, 60, 40, 60);
    setGrow(1.0f);
    setAlignItems(brls::AlignItems::CENTER);
    setJustifyContent(brls::JustifyContent::CENTER);
    setBackgroundColor(nvgRGB(10, 12, 15));

    // BUG FIXED 2026-07-31: see game_detail_view.cpp's identical fix — this
    // view extends plain brls::Box, so it never got a default B/Back action.
    // When pushed as its own Activity (LibraryTab::BeginLogin), B now closes
    // it and returns to the Library tab, matching every other pushed screen.
    // When shown as MainActivity's own startup content (no saved session —
    // see main_activity.cpp's ShowLogin()) there's nothing to pop back to;
    // Application::popActivity() is a documented no-op in that case, so this
    // is safe to register unconditionally either way.
    registerAction(Tr("Back"), brls::BUTTON_B, [](brls::View* view) {
        (void)view;
        brls::Application::popActivity();
        return true;
    });

    auto* card = new brls::Box(brls::Axis::COLUMN);
    card->setWidth(640);
    card->setPadding(30, 42, 34, 42);
    card->setBackgroundColor(nvgRGB(18, 21, 25));
    card->setCornerRadius(18);

    auto* brand = new brls::Label();
    brand->setText("Boosteroid");
    brand->setFontSize(30);
    brand->setTextColor(nvgRGB(77, 218, 130));
    brand->setMarginBottom(6);
    card->addView(brand);

    auto* subtitle = new brls::Label();
    subtitle->setText(Tr("Sign in with your Boosteroid account"));
    subtitle->setFontSize(15);
    subtitle->setTextColor(nvgRGB(151, 159, 170));
    subtitle->setMarginBottom(26);
    card->addView(subtitle);

    email_button_ = new brls::Button();
    email_button_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
    email_button_->setMarginBottom(12);
    email_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        OpenEmailIme();
        return true;
    });
    card->addView(email_button_);

    password_button_ = new brls::Button();
    password_button_->setStyle(&brls::BUTTONSTYLE_DEFAULT);
    password_button_->setMarginBottom(22);
    password_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        OpenPasswordIme();
        return true;
    });
    card->addView(password_button_);

    login_button_ = new brls::Button();
    login_button_->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    login_button_->setText(Tr("Sign in"));
    login_button_->registerClickAction([this](brls::View* view) {
        (void)view;
        SubmitLogin();
        return true;
    });
    card->addView(login_button_);

    status_label_ = new brls::Label();
    status_label_->setFontSize(14);
    status_label_->setTextColor(nvgRGB(255, 150, 150));
    status_label_->setMarginTop(16);
    status_label_->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    card->addView(status_label_);

    addView(card);
    UpdateFieldLabels();
}

LoginView::~LoginView()
{
    alive_->store(false);
}

void LoginView::UpdateFieldLabels()
{
    email_button_->setText(
        (email_.empty() ? Tr("Email") + "..." : email_));
    password_button_->setText(
        password_.empty() ? Tr("Password") + "..." : MaskedPassword(password_));
}

void LoginView::OpenEmailIme()
{
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            email_ = std::move(text);
            UpdateFieldLabels();
        },
        Tr("Email"),
        Tr("Your Boosteroid account email"),
        128,
        email_,
        brls::KEYBOARD_DISABLE_NONE);
}

void LoginView::OpenPasswordIme()
{
    brls::Application::getImeManager()->openForText(
        [this](std::string text) {
            password_ = std::move(text);
            UpdateFieldLabels();
        },
        Tr("Password"),
        Tr("Your Boosteroid account password"),
        128,
        password_,
        brls::KEYBOARD_DISABLE_NONE);
}

void LoginView::SetBusy(bool busy, const std::string& status)
{
    login_button_->setText(Tr(busy ? "Signing in..." : "Sign in"));
    status_label_->setTextColor(nvgRGB(151, 159, 170));
    status_label_->setText(Tr(status));
}

void LoginView::SubmitLogin()
{
    if (email_.empty() || password_.empty())
    {
        status_label_->setTextColor(nvgRGB(255, 150, 150));
        status_label_->setText(Tr("Enter your email and password first."));
        return;
    }

    SetBusy(true, "Contacting Boosteroid...");

    BoosteroidClient bg_client = client_;
    std::string bg_email = email_;
    std::string bg_password = password_;
    const auto alive = alive_;

    brls::async([this, alive, bg_client, bg_email, bg_password]() mutable {
        try
        {
            AuthSession session = bg_client.Login(bg_email, bg_password);
            bg_client.SaveSession(session);

            brls::sync([this, alive, session]() mutable {
                if (!alive->load())
                    return;
                SetBusy(false, "Signed in");
                if (on_success_)
                    on_success_(std::move(session));
            });
        }
        catch (const std::exception& ex)
        {
            std::string message = ex.what();
            brls::sync([this, alive, message]() {
                if (!alive->load())
                    return;
                SetBusy(false, "");
                status_label_->setTextColor(nvgRGB(255, 150, 150));
                status_label_->setText(message);
            });
        }
    }, false);
}

} // namespace opennow
