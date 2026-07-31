#pragma once

#include "boosteroid_client.hpp"
#include "models.hpp"

#include <borealis.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

// MARK: - LoginView (Nintendo Switch port, Boosteroid protocol)
//
// Boosteroid's direct email/password login (CONFIRMED, see boosteroid_client.hpp's
// Login() doc comment) is dramatically simpler than GFN's device-flow QR+PIN
// login (SwitchNOW's original qr_login_dialog.cpp/hpp, kept in
// _legacy_gfn_reference/ but unused here): there is no polling loop, no
// external browser step, and no PIN — just two text fields and a submit
// button, filled in with borealis's built-in IME.
//
// TODO(port): borealis's ImeManager::openForText has no password-masking
// flag in this SDK version, so the password is shown in plaintext while
// typing (same as every other text field here) — acceptable for a first
// pass but worth revisiting if a masked variant becomes available.
namespace opennow
{

class LoginView : public brls::Box
{
  public:
    // `on_success` fires (on the UI thread) once Login() succeeds and the
    // session has been persisted via BoosteroidClient::SaveSession.
    explicit LoginView(std::function<void(AuthSession)> on_success);
    ~LoginView() override;

  private:
    void OpenEmailIme();
    void OpenPasswordIme();
    void SubmitLogin();
    void UpdateFieldLabels();
    void SetBusy(bool busy, const std::string& status);

    BoosteroidClient client_;
    std::string email_;
    std::string password_;
    brls::Button* email_button_ = nullptr;
    brls::Button* password_button_ = nullptr;
    brls::Button* login_button_ = nullptr;
    brls::Label* status_label_ = nullptr;
    std::function<void(AuthSession)> on_success_;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
};

} // namespace opennow
