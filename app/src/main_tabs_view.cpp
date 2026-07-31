#include "main_tabs_view.hpp"

#include "library_tab.hpp"
#include "search_tab.hpp"
#include "settings_tab.hpp"
#include "status_tab.hpp"
#include "localization.hpp"

namespace opennow
{

MainTabsView::MainTabsView()
{
    addTab(Tr("Library"), []() { return new LibraryTab(); });
    addTab(Tr("Search"), []() { return new SearchTab(); });
    addTab(Tr("Settings"), []() { return new SettingsTab(); });
    addTab(Tr("Status"), []() { return new StatusTab(); });
    focusTab(0);
}

} // namespace opennow
