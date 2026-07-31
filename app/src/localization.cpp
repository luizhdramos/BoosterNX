#include "localization.hpp"

#include <algorithm>
#include <unordered_map>

namespace opennow
{
namespace
{

using Dictionary = std::unordered_map<std::string, std::string>;

std::string g_language = "en";

const Dictionary& DictionaryFor(const std::string& code)
{
    static const Dictionary empty;
    static const std::unordered_map<std::string, Dictionary> dictionaries = {
        {"ru", {
            {"Store", "Магазин"}, {"Library", "Библиотека"}, {"Settings", "Настройки"},
            {"My Library", "Моя библиотека"}, {"Search", "Поиск"}, {"Close", "Закрыть"},
            {"Cancel", "Отмена"}, {"Account", "Аккаунт"}, {"Stream", "Поток"},
            {"Game", "Игра"}, {"Controls", "Управление"}, {"Audio", "Аудио"},
            {"Storage", "Хранилище"}, {"Interface", "Интерфейс"},
            {"Language", "Язык интерфейса"}, {"Choose the launcher language.", "Выберите язык интерфейса приложения."},
            {"CATEGORIES", "КАТЕГОРИИ"}, {"All changes saved", "Все изменения сохранены"},
            {"Unsaved changes  |  X Save", "Есть изменения  |  X Сохранить"},
            {"Settings are already up to date", "Настройки уже актуальны"},
            {"Settings saved; changes apply to the next stream", "Настройки сохранены; параметры потока применятся к следующему сеансу"},
            {"Unsaved changes reverted", "Несохраненные изменения отменены"},
            {"Quality profile", "Профиль качества"}, {"Preset", "Профиль"},
            {"Resolution", "Разрешение"}, {"Frame rate", "Частота кадров"},
            {"Maximum bitrate", "Максимальный битрейт"}, {"Motion clarity", "Четкость в движении"},
            {"Video backend", "Видеодекодер"}, {"Decoder & delivery", "Декодирование и доставка"},
            {"Recovery", "Восстановление"}, {"Diagnostics", "Диагностика"},
            {"Debug diagnostics", "Отладочная диагностика"}, {"Enabled", "Включено"},
            {"Disabled", "Выключено"}, {"Muted", "Без звука"},
            {"Stream audio", "Звук трансляции"}, {"Audio output", "Вывод звука"},
            {"Volume boost", "Усиление громкости"}, {"Playback buffer", "Буфер воспроизведения"},
            {"Output format", "Формат вывода"}, {"Game preferences", "Настройки игры"},
            {"Game language", "Язык игры"}, {"Save in-game graphics settings", "Сохранять настройки графики игры"},
            {"Controller layout", "Раскладка контроллера"}, {"Face buttons", "Кнопки ABXY"},
            {"Connected account", "Подключенный аккаунт"}, {"Status", "Статус"},
            {"Not connected", "Не подключен"}, {"User", "Пользователь"},
            {"Membership", "Подписка"}, {"Provider", "Провайдер"},
            {"Authentication", "Авторизация"}, {"Quick sign-in", "Быстрый вход"},
            {"Saved accounts", "Сохраненные аккаунты"}, {"Session", "Сессия"},
            {"Session details", "Данные сессии"}, {"Refresh authorization", "Обновить авторизацию"},
            {"Choose saved account", "Выбрать сохраненный аккаунт"}, {"Forget saved passwords", "Забыть сохраненные пароли"},
            {"Account removal", "Удаление аккаунта"}, {"Remove active account", "Удалить активный аккаунт"},
            {"Remove every account", "Удалить все аккаунты"}, {"View", "Открыть"},
            {"Refresh", "Обновить"}, {"Choose", "Выбрать"}, {"Forget", "Забыть"},
            {"Remove", "Удалить"}, {"Remove all", "Удалить все"}, {"Restore", "Восстановить"},
            {"Cover cache", "Кэш обложек"}, {"Cover files", "Файлы обложек"},
            {"Disk usage", "Использование диска"}, {"Public catalog", "Публичный каталог"},
            {"Owned library", "Моя библиотека"}, {"Loaded", "Загружено"}, {"Not loaded", "Не загружено"},
            {"Inspect shared cache", "Проверить общий кэш"}, {"Clear cover artwork", "Очистить обложки"},
            {"Inspect", "Проверить"}, {"Clear", "Очистить"}, {"System", "Система"},
            {"Open diagnostics", "Открыть диагностику"}, {"Open", "Открыть"},
            {"Sign in to GeForce NOW", "Войти в GeForce NOW"}, {"Refresh library", "Обновить библиотеку"},
            {"Sign out", "Выйти"}, {"Add another account", "Добавить аккаунт"},
            {"Reconnect this account", "Переподключить аккаунт"}, {"No account", "Нет аккаунта"},
            {"Play on GeForce NOW", "Играть в GeForce NOW"}, {"Play from Store", "Играть из магазина"},
            {"In your library", "В вашей библиотеке"}, {"Supported in GeForce NOW", "Поддерживается в GeForce NOW"},
            {"Choose game store", "Выберите магазин игры"}, {"Not Logged In", "Вход не выполнен"},
            {"Launch Error", "Ошибка запуска"}, {"NOW LOADING", "ЗАПУСК"},
            {"Checking your NVIDIA account", "Проверка аккаунта NVIDIA"},
            {"Requesting a cloud rig", "Запрос облачного компьютера"},
            {"Waiting in queue...", "Ожидание в очереди..."}, {"Preparing your cloud rig", "Подготовка облачного компьютера"},
            {"Waiting for an available cloud rig", "Ожидание свободного облачного компьютера"},
            {"Session could not start", "Не удалось запустить сессию"}, {"Cancel session", "Отменить сессию"},
            {"Choose how to sign in", "Выберите способ входа"}, {"Welcome back", "С возвращением"},
            {"Connect your account", "Подключите аккаунт"}, {"NVIDIA account", "Аккаунт NVIDIA"},
            {"No saved account yet", "Сохраненного аккаунта пока нет"}, {"QUICK SIGN-IN READY", "БЫСТРЫЙ ВХОД ГОТОВ"},
            {"NEW ACCOUNT", "НОВЫЙ АККАУНТ"}, {"Use another NVIDIA account", "Использовать другой аккаунт NVIDIA"},
            {"Enter email and password", "Ввести почту и пароль"}, {"Phone / PC fallback for CAPTCHA or passkey", "Вход через телефон / ПК для CAPTCHA или ключа доступа"},
            {"SIGN-IN PREFERENCES", "НАСТРОЙКИ ВХОДА"}, {"Remember sign-in: ON", "Запоминать вход: ВКЛ"},
            {"Cancel active sign-in", "Отменить текущий вход"}, {"Approve the NVIDIA sign-in by email", "Подтвердите вход NVIDIA по электронной почте"},
            {"Cancel sign-in", "Отменить вход"}, {"Signing in directly with NVIDIA...", "Вход напрямую через NVIDIA..."},
            {"Sign-in cancelled", "Вход отменен"}, {"Waiting for NVIDIA verification", "Ожидание подтверждения NVIDIA"},
            {"Login failed", "Ошибка входа"}, {"Checking saved sign-in", "Проверка сохраненного входа"},
            {"SAVED NVIDIA ACCOUNT", "СОХРАНЕННЫЙ АККАУНТ NVIDIA"}
        }},
        {"uk", {
            {"Store", "Магазин"}, {"Library", "Бібліотека"}, {"Settings", "Налаштування"},
            {"My Library", "Моя бібліотека"}, {"Search", "Пошук"}, {"Close", "Закрити"},
            {"Cancel", "Скасувати"}, {"Account", "Обліковий запис"}, {"Stream", "Трансляція"},
            {"Game", "Гра"}, {"Controls", "Керування"}, {"Audio", "Аудіо"},
            {"Storage", "Сховище"}, {"Interface", "Інтерфейс"}, {"Language", "Мова інтерфейсу"},
            {"Choose the launcher language.", "Виберіть мову інтерфейсу застосунку."},
            {"CATEGORIES", "КАТЕГОРІЇ"}, {"All changes saved", "Усі зміни збережено"},
            {"Unsaved changes  |  X Save", "Є зміни  |  X Зберегти"},
            {"Quality profile", "Профіль якості"}, {"Preset", "Профіль"}, {"Resolution", "Роздільна здатність"},
            {"Frame rate", "Частота кадрів"}, {"Maximum bitrate", "Максимальний бітрейт"},
            {"Diagnostics", "Діагностика"}, {"Enabled", "Увімкнено"}, {"Disabled", "Вимкнено"},
            {"Stream audio", "Звук трансляції"}, {"Volume boost", "Підсилення гучності"},
            {"Game preferences", "Налаштування гри"}, {"Game language", "Мова гри"},
            {"Controller layout", "Розкладка контролера"}, {"Face buttons", "Кнопки ABXY"},
            {"Connected account", "Підключений обліковий запис"}, {"Status", "Стан"},
            {"Not connected", "Не підключено"}, {"User", "Користувач"}, {"Membership", "Підписка"},
            {"Authentication", "Авторизація"}, {"Quick sign-in", "Швидкий вхід"},
            {"Saved accounts", "Збережені облікові записи"}, {"Session", "Сеанс"},
            {"Refresh", "Оновити"}, {"Choose", "Вибрати"}, {"Remove", "Видалити"},
            {"Cover cache", "Кеш обкладинок"}, {"System", "Система"},
            {"Sign in to GeForce NOW", "Увійти в GeForce NOW"}, {"Refresh library", "Оновити бібліотеку"},
            {"Sign out", "Вийти"}, {"Play on GeForce NOW", "Грати в GeForce NOW"},
            {"In your library", "У вашій бібліотеці"}, {"NOW LOADING", "ЗАПУСК"},
            {"Checking your NVIDIA account", "Перевірка облікового запису NVIDIA"},
            {"Waiting in queue...", "Очікування в черзі..."}, {"Preparing your cloud rig", "Підготовка хмарного комп'ютера"},
            {"Cancel session", "Скасувати сеанс"}, {"Choose how to sign in", "Виберіть спосіб входу"},
            {"Welcome back", "З поверненням"}, {"Use another NVIDIA account", "Інший обліковий запис NVIDIA"},
            {"Enter email and password", "Ввести пошту та пароль"}, {"Login failed", "Помилка входу"}
        }},
        {"es", {
            {"Store", "Tienda"}, {"Library", "Biblioteca"}, {"Settings", "Ajustes"}, {"My Library", "Mi biblioteca"},
            {"Search", "Buscar"}, {"Close", "Cerrar"}, {"Cancel", "Cancelar"}, {"Account", "Cuenta"},
            {"Stream", "Transmisión"}, {"Game", "Juego"}, {"Controls", "Controles"}, {"Audio", "Audio"},
            {"Storage", "Almacenamiento"}, {"Interface", "Interfaz"}, {"Language", "Idioma de la interfaz"},
            {"Choose the launcher language.", "Elige el idioma de la aplicación."}, {"CATEGORIES", "CATEGORÍAS"},
            {"All changes saved", "Todos los cambios guardados"}, {"Unsaved changes  |  X Save", "Cambios sin guardar  |  X Guardar"},
            {"Quality profile", "Perfil de calidad"}, {"Preset", "Preajuste"}, {"Resolution", "Resolución"},
            {"Frame rate", "Frecuencia de imagen"}, {"Maximum bitrate", "Bitrate máximo"},
            {"Diagnostics", "Diagnóstico"}, {"Enabled", "Activado"}, {"Disabled", "Desactivado"},
            {"Stream audio", "Audio de transmisión"}, {"Volume boost", "Amplificación de volumen"},
            {"Game preferences", "Preferencias del juego"}, {"Game language", "Idioma del juego"},
            {"Controller layout", "Distribución del mando"}, {"Face buttons", "Botones ABXY"},
            {"Connected account", "Cuenta conectada"}, {"Status", "Estado"}, {"Not connected", "Sin conexión"},
            {"Quick sign-in", "Inicio rápido"}, {"Saved accounts", "Cuentas guardadas"},
            {"Refresh", "Actualizar"}, {"Choose", "Elegir"}, {"Remove", "Eliminar"},
            {"Sign in to GeForce NOW", "Iniciar sesión en GeForce NOW"}, {"Refresh library", "Actualizar biblioteca"},
            {"Sign out", "Cerrar sesión"}, {"Play on GeForce NOW", "Jugar en GeForce NOW"},
            {"In your library", "En tu biblioteca"}, {"NOW LOADING", "CARGANDO"},
            {"Checking your NVIDIA account", "Comprobando tu cuenta NVIDIA"}, {"Waiting in queue...", "Esperando en la cola..."},
            {"Preparing your cloud rig", "Preparando tu equipo en la nube"}, {"Cancel session", "Cancelar sesión"},
            {"Choose how to sign in", "Elige cómo iniciar sesión"}, {"Welcome back", "Bienvenido de nuevo"},
            {"Enter email and password", "Introducir correo y contraseña"}, {"Login failed", "Error de inicio de sesión"}
        }},
        {"it", {
            {"Store", "Negozio"}, {"Library", "Libreria"}, {"Settings", "Impostazioni"}, {"My Library", "La mia libreria"},
            {"Search", "Cerca"}, {"Close", "Chiudi"}, {"Cancel", "Annulla"}, {"Account", "Account"},
            {"Stream", "Streaming"}, {"Game", "Gioco"}, {"Controls", "Comandi"}, {"Audio", "Audio"},
            {"Storage", "Archiviazione"}, {"Interface", "Interfaccia"}, {"Language", "Lingua dell'interfaccia"},
            {"Choose the launcher language.", "Scegli la lingua dell'applicazione."}, {"CATEGORIES", "CATEGORIE"},
            {"All changes saved", "Tutte le modifiche sono salvate"}, {"Unsaved changes  |  X Save", "Modifiche non salvate  |  X Salva"},
            {"Quality profile", "Profilo qualità"}, {"Preset", "Profilo"}, {"Resolution", "Risoluzione"},
            {"Frame rate", "Frequenza fotogrammi"}, {"Maximum bitrate", "Bitrate massimo"},
            {"Diagnostics", "Diagnostica"}, {"Enabled", "Attivo"}, {"Disabled", "Disattivato"},
            {"Game language", "Lingua del gioco"}, {"Controller layout", "Layout controller"},
            {"Connected account", "Account collegato"}, {"Quick sign-in", "Accesso rapido"},
            {"Refresh", "Aggiorna"}, {"Choose", "Scegli"}, {"Remove", "Rimuovi"},
            {"Sign in to GeForce NOW", "Accedi a GeForce NOW"}, {"Refresh library", "Aggiorna libreria"},
            {"Sign out", "Esci"}, {"Play on GeForce NOW", "Gioca su GeForce NOW"}, {"In your library", "Nella tua libreria"},
            {"NOW LOADING", "CARICAMENTO"}, {"Checking your NVIDIA account", "Verifica dell'account NVIDIA"},
            {"Waiting in queue...", "In attesa in coda..."}, {"Preparing your cloud rig", "Preparazione del PC cloud"},
            {"Cancel session", "Annulla sessione"}, {"Choose how to sign in", "Scegli come accedere"},
            {"Welcome back", "Bentornato"}, {"Login failed", "Accesso non riuscito"}
        }},
        {"fr", {
            {"Store", "Boutique"}, {"Library", "Bibliothèque"}, {"Settings", "Paramètres"}, {"My Library", "Ma bibliothèque"},
            {"Search", "Rechercher"}, {"Close", "Fermer"}, {"Cancel", "Annuler"}, {"Account", "Compte"},
            {"Stream", "Streaming"}, {"Game", "Jeu"}, {"Controls", "Commandes"}, {"Audio", "Audio"},
            {"Storage", "Stockage"}, {"Interface", "Interface"}, {"Language", "Langue de l'interface"},
            {"Choose the launcher language.", "Choisissez la langue de l'application."}, {"CATEGORIES", "CATÉGORIES"},
            {"All changes saved", "Toutes les modifications sont enregistrées"}, {"Unsaved changes  |  X Save", "Modifications non enregistrées  |  X Enregistrer"},
            {"Quality profile", "Profil de qualité"}, {"Preset", "Préréglage"}, {"Resolution", "Résolution"},
            {"Frame rate", "Fréquence d'images"}, {"Maximum bitrate", "Débit maximal"},
            {"Diagnostics", "Diagnostic"}, {"Enabled", "Activé"}, {"Disabled", "Désactivé"},
            {"Stream audio", "Audio du streaming"}, {"Game language", "Langue du jeu"},
            {"Controller layout", "Disposition de la manette"}, {"Connected account", "Compte connecté"},
            {"Quick sign-in", "Connexion rapide"}, {"Refresh", "Actualiser"}, {"Choose", "Choisir"}, {"Remove", "Supprimer"},
            {"Sign in to GeForce NOW", "Se connecter à GeForce NOW"}, {"Refresh library", "Actualiser la bibliothèque"},
            {"Sign out", "Se déconnecter"}, {"Play on GeForce NOW", "Jouer sur GeForce NOW"}, {"In your library", "Dans votre bibliothèque"},
            {"NOW LOADING", "CHARGEMENT"}, {"Checking your NVIDIA account", "Vérification de votre compte NVIDIA"},
            {"Waiting in queue...", "En attente dans la file..."}, {"Preparing your cloud rig", "Préparation de votre machine cloud"},
            {"Cancel session", "Annuler la session"}, {"Choose how to sign in", "Choisissez comment vous connecter"},
            {"Welcome back", "Bon retour"}, {"Login failed", "Échec de la connexion"}
        }},
        {"pl", {
            {"Store", "Sklep"}, {"Library", "Biblioteka"}, {"Settings", "Ustawienia"}, {"My Library", "Moja biblioteka"},
            {"Search", "Szukaj"}, {"Close", "Zamknij"}, {"Cancel", "Anuluj"}, {"Account", "Konto"},
            {"Stream", "Strumień"}, {"Game", "Gra"}, {"Controls", "Sterowanie"}, {"Audio", "Dźwięk"},
            {"Storage", "Pamięć"}, {"Interface", "Interfejs"}, {"Language", "Język interfejsu"},
            {"Choose the launcher language.", "Wybierz język aplikacji."}, {"CATEGORIES", "KATEGORIE"},
            {"All changes saved", "Wszystkie zmiany zapisano"}, {"Unsaved changes  |  X Save", "Niezapisane zmiany  |  X Zapisz"},
            {"Quality profile", "Profil jakości"}, {"Preset", "Profil"}, {"Resolution", "Rozdzielczość"},
            {"Frame rate", "Liczba klatek"}, {"Maximum bitrate", "Maksymalny bitrate"},
            {"Diagnostics", "Diagnostyka"}, {"Enabled", "Włączone"}, {"Disabled", "Wyłączone"},
            {"Game language", "Język gry"}, {"Controller layout", "Układ kontrolera"},
            {"Connected account", "Połączone konto"}, {"Quick sign-in", "Szybkie logowanie"},
            {"Refresh", "Odśwież"}, {"Choose", "Wybierz"}, {"Remove", "Usuń"},
            {"Sign in to GeForce NOW", "Zaloguj do GeForce NOW"}, {"Refresh library", "Odśwież bibliotekę"},
            {"Sign out", "Wyloguj"}, {"Play on GeForce NOW", "Graj w GeForce NOW"}, {"In your library", "W twojej bibliotece"},
            {"NOW LOADING", "URUCHAMIANIE"}, {"Checking your NVIDIA account", "Sprawdzanie konta NVIDIA"},
            {"Waiting in queue...", "Oczekiwanie w kolejce..."}, {"Preparing your cloud rig", "Przygotowywanie komputera w chmurze"},
            {"Cancel session", "Anuluj sesję"}, {"Choose how to sign in", "Wybierz sposób logowania"},
            {"Welcome back", "Witaj ponownie"}, {"Login failed", "Logowanie nie powiodło się"}
        }},
        {"zh-CN", {
            {"Store", "商店"}, {"Library", "游戏库"}, {"Settings", "设置"}, {"My Library", "我的游戏库"},
            {"Search", "搜索"}, {"Close", "关闭"}, {"Cancel", "取消"}, {"Account", "账户"},
            {"Stream", "串流"}, {"Game", "游戏"}, {"Controls", "控制"}, {"Audio", "音频"},
            {"Storage", "存储"}, {"Interface", "界面"}, {"Language", "界面语言"},
            {"Choose the launcher language.", "选择应用界面语言。"}, {"CATEGORIES", "分类"},
            {"All changes saved", "所有更改已保存"}, {"Unsaved changes  |  X Save", "有未保存的更改  |  X 保存"},
            {"Quality profile", "画质预设"}, {"Preset", "预设"}, {"Resolution", "分辨率"},
            {"Frame rate", "帧率"}, {"Maximum bitrate", "最大码率"}, {"Diagnostics", "诊断"},
            {"Enabled", "已启用"}, {"Disabled", "已禁用"}, {"Stream audio", "串流音频"},
            {"Game language", "游戏语言"}, {"Controller layout", "手柄布局"},
            {"Connected account", "已连接账户"}, {"Quick sign-in", "快速登录"},
            {"Refresh", "刷新"}, {"Choose", "选择"}, {"Remove", "删除"},
            {"Sign in to GeForce NOW", "登录 GeForce NOW"}, {"Refresh library", "刷新游戏库"},
            {"Sign out", "退出登录"}, {"Play on GeForce NOW", "在 GeForce NOW 上游玩"}, {"In your library", "已在游戏库中"},
            {"NOW LOADING", "正在启动"}, {"Checking your NVIDIA account", "正在检查 NVIDIA 账户"},
            {"Waiting in queue...", "正在排队..."}, {"Preparing your cloud rig", "正在准备云端设备"},
            {"Cancel session", "取消会话"}, {"Choose how to sign in", "选择登录方式"},
            {"Welcome back", "欢迎回来"}, {"Login failed", "登录失败"}
        }}
    };

    const auto found = dictionaries.find(code);
    return found == dictionaries.end() ? empty : found->second;
}

const Dictionary& ServerDictionaryFor(const std::string& code)
{
    static const Dictionary empty;
    static const std::unordered_map<std::string, Dictionary> dictionaries = {
        {"ru", {
            {"Server location", "Местоположение сервера"}, {"Use automatic routing or choose a specific GeForce NOW data center.", "Используйте автоматическую маршрутизацию или выберите конкретный центр обработки данных GeForce NOW."},
            {"Location", "Сервер"}, {"Locations are ranked by direct latency from this console.", "Серверы отсортированы по прямой задержке с этой консоли."},
            {"Latency test", "Тест задержки"}, {"Reload available locations and test each connection again.", "Повторно загрузить доступные серверы и проверить каждое подключение."},
            {"Testing locations...", "Проверка серверов..."}, {"Auto (best)", "Авто (лучший)"}, {"Auto", "Авто"},
            {"Selected server", "Выбранный сервер"}, {"Current server (not advertised)", "Текущий сервер (не опубликован)"},
            {"Unavailable", "Недоступен"}, {"Best", "Лучший"}, {"Server location test is already running", "Проверка серверов уже выполняется"},
            {"Server Locations Unavailable", "Серверы недоступны"}, {"Connect a GeForce NOW account before loading server locations.", "Подключите аккаунт GeForce NOW перед загрузкой серверов."},
            {"GeForce NOW did not return any server locations.", "GeForce NOW не вернул список серверов."}, {"Server location latency test complete", "Проверка задержки серверов завершена"},
            {"Latency test failed; automatic routing is still available", "Не удалось проверить задержку; автоматическая маршрутизация по-прежнему доступна"},
            {"Server Location Test Failed", "Ошибка проверки серверов"}, {"Testing GeForce NOW server locations...", "Проверка серверов GeForce NOW..."}
        }},
        {"uk", {
            {"Server location", "Розташування сервера"}, {"Use automatic routing or choose a specific GeForce NOW data center.", "Використовуйте автоматичну маршрутизацію або виберіть центр обробки даних GeForce NOW."},
            {"Location", "Сервер"}, {"Locations are ranked by direct latency from this console.", "Сервери впорядковано за прямою затримкою з цієї консолі."},
            {"Latency test", "Тест затримки"}, {"Reload available locations and test each connection again.", "Повторно завантажити доступні сервери та перевірити кожне з'єднання."},
            {"Testing locations...", "Перевірка серверів..."}, {"Auto (best)", "Авто (найкращий)"}, {"Auto", "Авто"},
            {"Selected server", "Вибраний сервер"}, {"Current server (not advertised)", "Поточний сервер (не опублікований)"},
            {"Unavailable", "Недоступний"}, {"Best", "Найкращий"}, {"Server location test is already running", "Перевірка серверів уже виконується"},
            {"Server Locations Unavailable", "Сервери недоступні"}, {"Connect a GeForce NOW account before loading server locations.", "Підключіть обліковий запис GeForce NOW перед завантаженням серверів."},
            {"GeForce NOW did not return any server locations.", "GeForce NOW не повернув список серверів."}, {"Server location latency test complete", "Перевірку затримки серверів завершено"},
            {"Latency test failed; automatic routing is still available", "Не вдалося перевірити затримку; автоматична маршрутизація все ще доступна"},
            {"Server Location Test Failed", "Помилка перевірки серверів"}, {"Testing GeForce NOW server locations...", "Перевірка серверів GeForce NOW..."}
        }},
        {"es", {
            {"Server location", "Ubicación del servidor"}, {"Use automatic routing or choose a specific GeForce NOW data center.", "Usa el enrutamiento automático o elige un centro de datos de GeForce NOW."},
            {"Location", "Servidor"}, {"Locations are ranked by direct latency from this console.", "Los servidores se ordenan por latencia directa desde esta consola."},
            {"Latency test", "Prueba de latencia"}, {"Reload available locations and test each connection again.", "Vuelve a cargar los servidores disponibles y prueba cada conexión."},
            {"Testing locations...", "Probando servidores..."}, {"Auto (best)", "Auto (mejor)"}, {"Auto", "Auto"},
            {"Selected server", "Servidor seleccionado"}, {"Current server (not advertised)", "Servidor actual (no anunciado)"},
            {"Unavailable", "No disponible"}, {"Best", "Mejor"}, {"Server location test is already running", "La prueba de servidores ya está en curso"},
            {"Server Locations Unavailable", "Servidores no disponibles"}, {"Connect a GeForce NOW account before loading server locations.", "Conecta una cuenta de GeForce NOW antes de cargar los servidores."},
            {"GeForce NOW did not return any server locations.", "GeForce NOW no devolvió ningún servidor."}, {"Server location latency test complete", "Prueba de latencia completada"},
            {"Latency test failed; automatic routing is still available", "La prueba falló; el enrutamiento automático sigue disponible"},
            {"Server Location Test Failed", "Falló la prueba de servidores"}, {"Testing GeForce NOW server locations...", "Probando servidores de GeForce NOW..."}
        }},
        {"it", {
            {"Server location", "Posizione server"}, {"Use automatic routing or choose a specific GeForce NOW data center.", "Usa l'instradamento automatico o scegli un data center GeForce NOW."},
            {"Location", "Server"}, {"Locations are ranked by direct latency from this console.", "I server sono ordinati per latenza diretta da questa console."},
            {"Latency test", "Test latenza"}, {"Reload available locations and test each connection again.", "Ricarica i server disponibili e verifica ogni connessione."},
            {"Testing locations...", "Test dei server..."}, {"Auto (best)", "Auto (migliore)"}, {"Auto", "Auto"},
            {"Selected server", "Server selezionato"}, {"Current server (not advertised)", "Server attuale (non pubblicizzato)"},
            {"Unavailable", "Non disponibile"}, {"Best", "Migliore"}, {"Server location test is already running", "Il test dei server è già in corso"},
            {"Server Locations Unavailable", "Server non disponibili"}, {"Connect a GeForce NOW account before loading server locations.", "Collega un account GeForce NOW prima di caricare i server."},
            {"GeForce NOW did not return any server locations.", "GeForce NOW non ha restituito alcun server."}, {"Server location latency test complete", "Test della latenza completato"},
            {"Latency test failed; automatic routing is still available", "Test non riuscito; l'instradamento automatico è ancora disponibile"},
            {"Server Location Test Failed", "Test dei server non riuscito"}, {"Testing GeForce NOW server locations...", "Test dei server GeForce NOW..."}
        }},
        {"fr", {
            {"Server location", "Emplacement du serveur"}, {"Use automatic routing or choose a specific GeForce NOW data center.", "Utilisez le routage automatique ou choisissez un centre de données GeForce NOW."},
            {"Location", "Serveur"}, {"Locations are ranked by direct latency from this console.", "Les serveurs sont classés selon la latence directe depuis cette console."},
            {"Latency test", "Test de latence"}, {"Reload available locations and test each connection again.", "Rechargez les serveurs disponibles et testez chaque connexion."},
            {"Testing locations...", "Test des serveurs..."}, {"Auto (best)", "Auto (meilleur)"}, {"Auto", "Auto"},
            {"Selected server", "Serveur sélectionné"}, {"Current server (not advertised)", "Serveur actuel (non publié)"},
            {"Unavailable", "Indisponible"}, {"Best", "Meilleur"}, {"Server location test is already running", "Le test des serveurs est déjà en cours"},
            {"Server Locations Unavailable", "Serveurs indisponibles"}, {"Connect a GeForce NOW account before loading server locations.", "Connectez un compte GeForce NOW avant de charger les serveurs."},
            {"GeForce NOW did not return any server locations.", "GeForce NOW n'a renvoyé aucun serveur."}, {"Server location latency test complete", "Test de latence terminé"},
            {"Latency test failed; automatic routing is still available", "Le test a échoué; le routage automatique reste disponible"},
            {"Server Location Test Failed", "Échec du test des serveurs"}, {"Testing GeForce NOW server locations...", "Test des serveurs GeForce NOW..."}
        }},
        {"pl", {
            {"Server location", "Lokalizacja serwera"}, {"Use automatic routing or choose a specific GeForce NOW data center.", "Użyj automatycznego routingu lub wybierz centrum danych GeForce NOW."},
            {"Location", "Serwer"}, {"Locations are ranked by direct latency from this console.", "Serwery są sortowane według bezpośredniego opóźnienia z tej konsoli."},
            {"Latency test", "Test opóźnienia"}, {"Reload available locations and test each connection again.", "Załaduj ponownie dostępne serwery i sprawdź każde połączenie."},
            {"Testing locations...", "Testowanie serwerów..."}, {"Auto (best)", "Auto (najlepszy)"}, {"Auto", "Auto"},
            {"Selected server", "Wybrany serwer"}, {"Current server (not advertised)", "Bieżący serwer (niepublikowany)"},
            {"Unavailable", "Niedostępny"}, {"Best", "Najlepszy"}, {"Server location test is already running", "Test serwerów już trwa"},
            {"Server Locations Unavailable", "Serwery niedostępne"}, {"Connect a GeForce NOW account before loading server locations.", "Połącz konto GeForce NOW przed załadowaniem serwerów."},
            {"GeForce NOW did not return any server locations.", "GeForce NOW nie zwrócił listy serwerów."}, {"Server location latency test complete", "Test opóźnienia zakończony"},
            {"Latency test failed; automatic routing is still available", "Test nie powiódł się; routing automatyczny jest nadal dostępny"},
            {"Server Location Test Failed", "Test serwerów nie powiódł się"}, {"Testing GeForce NOW server locations...", "Testowanie serwerów GeForce NOW..."}
        }},
        {"zh-CN", {
            {"Server location", "服务器位置"}, {"Use automatic routing or choose a specific GeForce NOW data center.", "使用自动路由或选择特定的 GeForce NOW 数据中心。"},
            {"Location", "服务器"}, {"Locations are ranked by direct latency from this console.", "服务器按此主机的直连延迟排序。"},
            {"Latency test", "延迟测试"}, {"Reload available locations and test each connection again.", "重新加载可用服务器并再次测试每个连接。"},
            {"Testing locations...", "正在测试服务器..."}, {"Auto (best)", "自动（最佳）"}, {"Auto", "自动"},
            {"Selected server", "已选服务器"}, {"Current server (not advertised)", "当前服务器（未公开）"},
            {"Unavailable", "不可用"}, {"Best", "最佳"}, {"Server location test is already running", "服务器测试已在进行"},
            {"Server Locations Unavailable", "服务器不可用"}, {"Connect a GeForce NOW account before loading server locations.", "请先连接 GeForce NOW 账户再加载服务器。"},
            {"GeForce NOW did not return any server locations.", "GeForce NOW 未返回服务器列表。"}, {"Server location latency test complete", "服务器延迟测试完成"},
            {"Latency test failed; automatic routing is still available", "延迟测试失败；自动路由仍可使用"},
            {"Server Location Test Failed", "服务器测试失败"}, {"Testing GeForce NOW server locations...", "正在测试 GeForce NOW 服务器..."}
        }}
    };
    const auto found = dictionaries.find(code);
    return found == dictionaries.end() ? empty : found->second;
}

const Dictionary& CompletionDictionaryFor(const std::string& code)
{
    static const Dictionary empty;
    static const std::unordered_map<std::string, Dictionary> dictionaries = {
        {"ru", {
            {"Input", "Ввод"}, {"By", "Разработчик:"}, {"Unknown studio", "Неизвестная студия"},
            {"Unknown store", "Неизвестный магазин"}, {"About this game", "Об игре"},
            {"Up / Down  Scroll", "Вверх / вниз  Прокрутка"},
            {"Up / Down  Scroll  |  B  Back", "Вверх / вниз  Прокрутка  |  B  Назад"},
            {"No description is available yet for this title.", "Описание этой игры пока недоступно."},
            {"Browse screenshots", "Листать скриншоты"}, {"Publisher", "Издатель"},
            {"Last played", "Последний запуск"}, {"Unknown", "Неизвестно"}, {"Never", "Никогда"},
            {"library title", "игра библиотеки"}, {"catalog title", "игра каталога"},
            {"selected in library", "выбрано в библиотеке"},
            {"Previous screenshot", "Предыдущий скриншот"}, {"Next screenshot", "Следующий скриншот"},
            {"NTE Auto-login: Ready", "Автовход NTE: готов"},
            {"NTE Auto-login: Set email and password", "Автовход NTE: укажите почту и пароль"},
            {"Free To Play", "Бесплатная"}, {"Action", "Экшен"}, {"Adventure", "Приключения"},
            {"Role Playing", "Ролевая игра"}, {"Mouse", "Мышь"}, {"Keyboard", "Клавиатура"},
            {"Gamepad Partial", "Частичная поддержка геймпада"}, {"Gamepad", "Геймпад"},
            {"All stores", "Все магазины"}, {"Last Played", "Последний запуск"},
            {"More / Refresh", "Ещё / обновить"}, {"A-Z", "А-Я"},
            {"Loaded {0} games.", "Загружено игр: {0}."},
            {"Loaded {0} supported games.", "Загружено поддерживаемых игр: {0}."},
            {"Filter: {0}.", "Фильтр: {0}."}, {"Sort: {0}.", "Сортировка: {0}."},
            {"Found {0} matches.", "Найдено совпадений: {0}."},
            {"Showing first {0}. Press X or select Show more.", "Показаны первые {0}. Нажмите X или выберите «Показать ещё»."},
            {"Press X to refresh.", "Нажмите X для обновления."},
            {"Showing {0}-{1} of {2} (page {3}/{4}).", "Показано {0}-{1} из {2} (страница {3}/{4})."},
            {"Show more games ({0} left)", "Показать ещё ({0} осталось)"},
            {"Refresh catalog", "Обновить каталог"}, {"Position in queue:{0}", "Позиция в очереди:{0}"},
            {"GeForce NOW is allocating hardware for your game.", "GeForce NOW выделяет оборудование для вашей игры."},
            {"Native GeForce NOW client for Nintendo Switch.", "Нативный клиент GeForce NOW для Nintendo Switch."},
            {"Version", "Версия"}, {"Languages", "Языки"},
            {"Manage your GeForce NOW identity and persistent sign-in.", "Управление аккаунтом GeForce NOW и постоянным входом."},
            {"Balance image quality, latency and decoder stability.", "Баланс качества изображения, задержки и стабильности декодера."},
            {"Choose in-game localization and NVIDIA graphics persistence.", "Выбор языка игры и сохранения настроек графики NVIDIA."},
            {"Choose whether face buttons follow Xbox positions or Switch labels.", "Выбор раскладки лицевых кнопок Xbox или Switch."},
            {"Tune stream sound without changing the console volume.", "Настройка звука трансляции без изменения громкости консоли."},
            {"Inspect cache usage, diagnostics and local app data.", "Просмотр кэша, диагностики и локальных данных приложения."}
        }},
        {"uk", {
            {"Input", "Введення"}, {"By", "Розробник:"}, {"Unknown studio", "Невідома студія"},
            {"Unknown store", "Невідомий магазин"}, {"About this game", "Про гру"},
            {"Up / Down  Scroll", "Вгору / вниз  Прокрутка"},
            {"Up / Down  Scroll  |  B  Back", "Вгору / вниз  Прокрутка  |  B  Назад"},
            {"No description is available yet for this title.", "Опис цієї гри поки недоступний."},
            {"Browse screenshots", "Гортати знімки"}, {"Publisher", "Видавець"},
            {"Last played", "Останній запуск"}, {"Unknown", "Невідомо"}, {"Never", "Ніколи"},
            {"library title", "гра бібліотеки"}, {"catalog title", "гра каталогу"},
            {"selected in library", "вибрано в бібліотеці"},
            {"Previous screenshot", "Попередній знімок"}, {"Next screenshot", "Наступний знімок"},
            {"NTE Auto-login: Ready", "Автовхід NTE: готовий"},
            {"NTE Auto-login: Set email and password", "Автовхід NTE: вкажіть пошту й пароль"},
            {"Free To Play", "Безкоштовна"}, {"Action", "Екшен"}, {"Adventure", "Пригоди"},
            {"Role Playing", "Рольова гра"}, {"Mouse", "Миша"}, {"Keyboard", "Клавіатура"},
            {"Gamepad Partial", "Часткова підтримка геймпада"}, {"Gamepad", "Геймпад"},
            {"All stores", "Усі магазини"}, {"Last Played", "Останній запуск"},
            {"More / Refresh", "Ще / оновити"}, {"A-Z", "А-Я"},
            {"Loaded {0} games.", "Завантажено ігор: {0}."},
            {"Loaded {0} supported games.", "Завантажено підтримуваних ігор: {0}."},
            {"Filter: {0}.", "Фільтр: {0}."}, {"Sort: {0}.", "Сортування: {0}."},
            {"Found {0} matches.", "Знайдено збігів: {0}."},
            {"Showing first {0}. Press X or select Show more.", "Показано перші {0}. Натисніть X або виберіть «Показати ще»."},
            {"Press X to refresh.", "Натисніть X для оновлення."},
            {"Showing {0}-{1} of {2} (page {3}/{4}).", "Показано {0}-{1} із {2} (сторінка {3}/{4})."},
            {"Show more games ({0} left)", "Показати ще ({0} залишилось)"},
            {"Refresh catalog", "Оновити каталог"}, {"Position in queue:{0}", "Позиція в черзі:{0}"},
            {"GeForce NOW is allocating hardware for your game.", "GeForce NOW виділяє обладнання для вашої гри."},
            {"Native GeForce NOW client for Nintendo Switch.", "Нативний клієнт GeForce NOW для Nintendo Switch."},
            {"Version", "Версія"}, {"Languages", "Мови"},
            {"Manage your GeForce NOW identity and persistent sign-in.", "Керування обліковим записом GeForce NOW і постійним входом."},
            {"Balance image quality, latency and decoder stability.", "Баланс якості зображення, затримки та стабільності декодера."},
            {"Choose in-game localization and NVIDIA graphics persistence.", "Вибір мови гри та збереження налаштувань графіки NVIDIA."},
            {"Choose whether face buttons follow Xbox positions or Switch labels.", "Вибір розкладки лицьових кнопок Xbox або Switch."},
            {"Tune stream sound without changing the console volume.", "Налаштування звуку трансляції без зміни гучності консолі."},
            {"Inspect cache usage, diagnostics and local app data.", "Перегляд кешу, діагностики та локальних даних застосунку."}
        }},
        {"es", {
            {"Input", "Entrada"}, {"By", "Desarrollador:"}, {"Unknown studio", "Estudio desconocido"},
            {"Unknown store", "Tienda desconocida"}, {"About this game", "Acerca del juego"},
            {"Up / Down  Scroll", "Arriba / abajo  Desplazar"},
            {"Up / Down  Scroll  |  B  Back", "Arriba / abajo  Desplazar  |  B  Volver"},
            {"No description is available yet for this title.", "Todavía no hay descripción disponible para este juego."},
            {"Browse screenshots", "Ver capturas"}, {"Publisher", "Editor"},
            {"Last played", "Última partida"}, {"Unknown", "Desconocido"}, {"Never", "Nunca"},
            {"library title", "juego de la biblioteca"}, {"catalog title", "juego del catálogo"},
            {"selected in library", "seleccionado en la biblioteca"},
            {"Previous screenshot", "Captura anterior"}, {"Next screenshot", "Captura siguiente"},
            {"NTE Auto-login: Ready", "Inicio automático NTE: listo"},
            {"NTE Auto-login: Set email and password", "Inicio automático NTE: introduce correo y contraseña"},
            {"Free To Play", "Gratis"}, {"Action", "Acción"}, {"Adventure", "Aventura"},
            {"Role Playing", "Rol"}, {"Mouse", "Ratón"}, {"Keyboard", "Teclado"},
            {"Gamepad Partial", "Mando parcial"}, {"Gamepad", "Mando"},
            {"All stores", "Todas las tiendas"}, {"Last Played", "Última partida"},
            {"More / Refresh", "Más / actualizar"}, {"A-Z", "A-Z"},
            {"Loaded {0} games.", "{0} juegos cargados."},
            {"Loaded {0} supported games.", "{0} juegos compatibles cargados."},
            {"Filter: {0}.", "Filtro: {0}."}, {"Sort: {0}.", "Orden: {0}."},
            {"Found {0} matches.", "{0} resultados encontrados."},
            {"Showing first {0}. Press X or select Show more.", "Se muestran los primeros {0}. Pulsa X o elige Mostrar más."},
            {"Press X to refresh.", "Pulsa X para actualizar."},
            {"Showing {0}-{1} of {2} (page {3}/{4}).", "Mostrando {0}-{1} de {2} (página {3}/{4})."},
            {"Show more games ({0} left)", "Mostrar más ({0} restantes)"},
            {"Refresh catalog", "Actualizar catálogo"}, {"Position in queue:{0}", "Posición en la cola:{0}"},
            {"GeForce NOW is allocating hardware for your game.", "GeForce NOW está asignando hardware para tu juego."},
            {"Native GeForce NOW client for Nintendo Switch.", "Cliente nativo de GeForce NOW para Nintendo Switch."},
            {"Version", "Versión"}, {"Languages", "Idiomas"},
            {"Manage your GeForce NOW identity and persistent sign-in.", "Gestiona tu identidad de GeForce NOW y el inicio de sesión persistente."},
            {"Balance image quality, latency and decoder stability.", "Equilibra la calidad de imagen, la latencia y la estabilidad del decodificador."},
            {"Choose in-game localization and NVIDIA graphics persistence.", "Elige el idioma del juego y la persistencia de gráficos de NVIDIA."},
            {"Choose whether face buttons follow Xbox positions or Switch labels.", "Elige la disposición de botones de Xbox o las etiquetas de Switch."},
            {"Tune stream sound without changing the console volume.", "Ajusta el audio de la transmisión sin cambiar el volumen de la consola."},
            {"Inspect cache usage, diagnostics and local app data.", "Consulta la caché, los diagnósticos y los datos locales."}
        }},
        {"it", {
            {"Input", "Input"}, {"By", "Sviluppatore:"}, {"Unknown studio", "Studio sconosciuto"},
            {"Unknown store", "Negozio sconosciuto"}, {"About this game", "Informazioni sul gioco"},
            {"Up / Down  Scroll", "Su / giù  Scorri"},
            {"Up / Down  Scroll  |  B  Back", "Su / giù  Scorri  |  B  Indietro"},
            {"No description is available yet for this title.", "La descrizione di questo gioco non è ancora disponibile."},
            {"Browse screenshots", "Sfoglia schermate"}, {"Publisher", "Editore"},
            {"Last played", "Ultima partita"}, {"Unknown", "Sconosciuto"}, {"Never", "Mai"},
            {"library title", "gioco della libreria"}, {"catalog title", "gioco del catalogo"},
            {"selected in library", "selezionato nella libreria"},
            {"Previous screenshot", "Schermata precedente"}, {"Next screenshot", "Schermata successiva"},
            {"NTE Auto-login: Ready", "Accesso automatico NTE: pronto"},
            {"NTE Auto-login: Set email and password", "Accesso automatico NTE: inserisci email e password"},
            {"Free To Play", "Gratuito"}, {"Action", "Azione"}, {"Adventure", "Avventura"},
            {"Role Playing", "Gioco di ruolo"}, {"Mouse", "Mouse"}, {"Keyboard", "Tastiera"},
            {"Gamepad Partial", "Gamepad parziale"}, {"Gamepad", "Gamepad"},
            {"All stores", "Tutti i negozi"}, {"Last Played", "Ultima partita"},
            {"More / Refresh", "Altro / aggiorna"}, {"A-Z", "A-Z"},
            {"Loaded {0} games.", "{0} giochi caricati."},
            {"Loaded {0} supported games.", "{0} giochi supportati caricati."},
            {"Filter: {0}.", "Filtro: {0}."}, {"Sort: {0}.", "Ordine: {0}."},
            {"Found {0} matches.", "{0} risultati trovati."},
            {"Showing first {0}. Press X or select Show more.", "Visualizzati i primi {0}. Premi X o scegli Mostra altro."},
            {"Press X to refresh.", "Premi X per aggiornare."},
            {"Showing {0}-{1} of {2} (page {3}/{4}).", "Visualizzati {0}-{1} di {2} (pagina {3}/{4})."},
            {"Show more games ({0} left)", "Mostra altro ({0} rimanenti)"},
            {"Refresh catalog", "Aggiorna catalogo"}, {"Position in queue:{0}", "Posizione in coda:{0}"},
            {"GeForce NOW is allocating hardware for your game.", "GeForce NOW sta assegnando l'hardware per il gioco."},
            {"Native GeForce NOW client for Nintendo Switch.", "Client GeForce NOW nativo per Nintendo Switch."},
            {"Version", "Versione"}, {"Languages", "Lingue"},
            {"Manage your GeForce NOW identity and persistent sign-in.", "Gestisci l'identità GeForce NOW e l'accesso persistente."},
            {"Balance image quality, latency and decoder stability.", "Bilancia qualità dell'immagine, latenza e stabilità del decoder."},
            {"Choose in-game localization and NVIDIA graphics persistence.", "Scegli la lingua di gioco e la persistenza grafica NVIDIA."},
            {"Choose whether face buttons follow Xbox positions or Switch labels.", "Scegli la disposizione dei tasti Xbox o le etichette Switch."},
            {"Tune stream sound without changing the console volume.", "Regola l'audio dello streaming senza cambiare il volume della console."},
            {"Inspect cache usage, diagnostics and local app data.", "Controlla cache, diagnostica e dati locali dell'app."}
        }},
        {"fr", {
            {"Input", "Entrée"}, {"By", "Développeur :"}, {"Unknown studio", "Studio inconnu"},
            {"Unknown store", "Boutique inconnue"}, {"About this game", "À propos du jeu"},
            {"Up / Down  Scroll", "Haut / bas  Défiler"},
            {"Up / Down  Scroll  |  B  Back", "Haut / bas  Défiler  |  B  Retour"},
            {"No description is available yet for this title.", "Aucune description n'est encore disponible pour ce jeu."},
            {"Browse screenshots", "Parcourir les captures"}, {"Publisher", "Éditeur"},
            {"Last played", "Dernière partie"}, {"Unknown", "Inconnu"}, {"Never", "Jamais"},
            {"library title", "jeu de la bibliothèque"}, {"catalog title", "jeu du catalogue"},
            {"selected in library", "sélectionné dans la bibliothèque"},
            {"Previous screenshot", "Capture précédente"}, {"Next screenshot", "Capture suivante"},
            {"NTE Auto-login: Ready", "Connexion auto NTE : prête"},
            {"NTE Auto-login: Set email and password", "Connexion auto NTE : saisissez l'e-mail et le mot de passe"},
            {"Free To Play", "Gratuit"}, {"Action", "Action"}, {"Adventure", "Aventure"},
            {"Role Playing", "Jeu de rôle"}, {"Mouse", "Souris"}, {"Keyboard", "Clavier"},
            {"Gamepad Partial", "Manette partielle"}, {"Gamepad", "Manette"},
            {"All stores", "Toutes les boutiques"}, {"Last Played", "Dernière partie"},
            {"More / Refresh", "Plus / actualiser"}, {"A-Z", "A-Z"},
            {"Loaded {0} games.", "{0} jeux chargés."},
            {"Loaded {0} supported games.", "{0} jeux compatibles chargés."},
            {"Filter: {0}.", "Filtre : {0}."}, {"Sort: {0}.", "Tri : {0}."},
            {"Found {0} matches.", "{0} résultats trouvés."},
            {"Showing first {0}. Press X or select Show more.", "Affichage des {0} premiers. Appuyez sur X ou choisissez Afficher plus."},
            {"Press X to refresh.", "Appuyez sur X pour actualiser."},
            {"Showing {0}-{1} of {2} (page {3}/{4}).", "Affichage de {0}-{1} sur {2} (page {3}/{4})."},
            {"Show more games ({0} left)", "Afficher plus ({0} restants)"},
            {"Refresh catalog", "Actualiser le catalogue"}, {"Position in queue:{0}", "Position dans la file :{0}"},
            {"GeForce NOW is allocating hardware for your game.", "GeForce NOW attribue du matériel à votre jeu."},
            {"Native GeForce NOW client for Nintendo Switch.", "Client GeForce NOW natif pour Nintendo Switch."},
            {"Version", "Version"}, {"Languages", "Langues"},
            {"Manage your GeForce NOW identity and persistent sign-in.", "Gérez votre identité GeForce NOW et la connexion persistante."},
            {"Balance image quality, latency and decoder stability.", "Équilibrez la qualité d'image, la latence et la stabilité du décodeur."},
            {"Choose in-game localization and NVIDIA graphics persistence.", "Choisissez la langue du jeu et la persistance graphique NVIDIA."},
            {"Choose whether face buttons follow Xbox positions or Switch labels.", "Choisissez la disposition Xbox ou les libellés Switch."},
            {"Tune stream sound without changing the console volume.", "Réglez le son du flux sans modifier le volume de la console."},
            {"Inspect cache usage, diagnostics and local app data.", "Consultez le cache, les diagnostics et les données locales."}
        }},
        {"pl", {
            {"Input", "Sterowanie"}, {"By", "Twórca:"}, {"Unknown studio", "Nieznane studio"},
            {"Unknown store", "Nieznany sklep"}, {"About this game", "O grze"},
            {"Up / Down  Scroll", "Góra / dół  Przewijanie"},
            {"Up / Down  Scroll  |  B  Back", "Góra / dół  Przewijanie  |  B  Wstecz"},
            {"No description is available yet for this title.", "Opis tej gry nie jest jeszcze dostępny."},
            {"Browse screenshots", "Przeglądaj zrzuty"}, {"Publisher", "Wydawca"},
            {"Last played", "Ostatnia gra"}, {"Unknown", "Nieznane"}, {"Never", "Nigdy"},
            {"library title", "gra z biblioteki"}, {"catalog title", "gra z katalogu"},
            {"selected in library", "wybrano w bibliotece"},
            {"Previous screenshot", "Poprzedni zrzut"}, {"Next screenshot", "Następny zrzut"},
            {"NTE Auto-login: Ready", "Autologowanie NTE: gotowe"},
            {"NTE Auto-login: Set email and password", "Autologowanie NTE: podaj e-mail i hasło"},
            {"Free To Play", "Darmowa"}, {"Action", "Akcja"}, {"Adventure", "Przygodowa"},
            {"Role Playing", "RPG"}, {"Mouse", "Mysz"}, {"Keyboard", "Klawiatura"},
            {"Gamepad Partial", "Częściowa obsługa pada"}, {"Gamepad", "Gamepad"},
            {"All stores", "Wszystkie sklepy"}, {"Last Played", "Ostatnia gra"},
            {"More / Refresh", "Więcej / odśwież"}, {"A-Z", "A-Z"},
            {"Loaded {0} games.", "Wczytano gier: {0}."},
            {"Loaded {0} supported games.", "Wczytano obsługiwanych gier: {0}."},
            {"Filter: {0}.", "Filtr: {0}."}, {"Sort: {0}.", "Sortowanie: {0}."},
            {"Found {0} matches.", "Znaleziono: {0}."},
            {"Showing first {0}. Press X or select Show more.", "Wyświetlono pierwsze {0}. Naciśnij X lub wybierz Pokaż więcej."},
            {"Press X to refresh.", "Naciśnij X, aby odświeżyć."},
            {"Showing {0}-{1} of {2} (page {3}/{4}).", "Wyświetlono {0}-{1} z {2} (strona {3}/{4})."},
            {"Show more games ({0} left)", "Pokaż więcej (pozostało {0})"},
            {"Refresh catalog", "Odśwież katalog"}, {"Position in queue:{0}", "Pozycja w kolejce:{0}"},
            {"GeForce NOW is allocating hardware for your game.", "GeForce NOW przydziela sprzęt do gry."},
            {"Native GeForce NOW client for Nintendo Switch.", "Natywny klient GeForce NOW dla Nintendo Switch."},
            {"Version", "Wersja"}, {"Languages", "Języki"},
            {"Manage your GeForce NOW identity and persistent sign-in.", "Zarządzaj kontem GeForce NOW i trwałym logowaniem."},
            {"Balance image quality, latency and decoder stability.", "Zrównoważ jakość obrazu, opóźnienie i stabilność dekodera."},
            {"Choose in-game localization and NVIDIA graphics persistence.", "Wybierz język gry i zachowywanie grafiki NVIDIA."},
            {"Choose whether face buttons follow Xbox positions or Switch labels.", "Wybierz układ przycisków Xbox lub oznaczenia Switch."},
            {"Tune stream sound without changing the console volume.", "Dostosuj dźwięk transmisji bez zmiany głośności konsoli."},
            {"Inspect cache usage, diagnostics and local app data.", "Sprawdź pamięć podręczną, diagnostykę i dane lokalne."}
        }},
        {"zh-CN", {
            {"Input", "输入"}, {"By", "开发者："}, {"Unknown studio", "未知工作室"},
            {"Unknown store", "未知商店"}, {"About this game", "关于此游戏"},
            {"Up / Down  Scroll", "上 / 下  滚动"},
            {"Up / Down  Scroll  |  B  Back", "上 / 下  滚动  |  B  返回"},
            {"No description is available yet for this title.", "此游戏暂无说明。"},
            {"Browse screenshots", "浏览截图"}, {"Publisher", "发行商"},
            {"Last played", "上次游玩"}, {"Unknown", "未知"}, {"Never", "从未"},
            {"library title", "库中游戏"}, {"catalog title", "目录游戏"},
            {"selected in library", "已在游戏库中选择"},
            {"Previous screenshot", "上一张截图"}, {"Next screenshot", "下一张截图"},
            {"NTE Auto-login: Ready", "NTE 自动登录：已就绪"},
            {"NTE Auto-login: Set email and password", "NTE 自动登录：设置邮箱和密码"},
            {"Free To Play", "免费游玩"}, {"Action", "动作"}, {"Adventure", "冒险"},
            {"Role Playing", "角色扮演"}, {"Mouse", "鼠标"}, {"Keyboard", "键盘"},
            {"Gamepad Partial", "部分手柄支持"}, {"Gamepad", "手柄"},
            {"All stores", "所有商店"}, {"Last Played", "上次游玩"},
            {"More / Refresh", "更多 / 刷新"}, {"A-Z", "A-Z"},
            {"Loaded {0} games.", "已加载 {0} 款游戏。"},
            {"Loaded {0} supported games.", "已加载 {0} 款支持的游戏。"},
            {"Filter: {0}.", "筛选：{0}。"}, {"Sort: {0}.", "排序：{0}。"},
            {"Found {0} matches.", "找到 {0} 个结果。"},
            {"Showing first {0}. Press X or select Show more.", "显示前 {0} 项。按 X 或选择显示更多。"},
            {"Press X to refresh.", "按 X 刷新。"},
            {"Showing {0}-{1} of {2} (page {3}/{4}).", "显示 {0}-{1} / {2}（第 {3}/{4} 页）。"},
            {"Show more games ({0} left)", "显示更多（剩余 {0}）"},
            {"Refresh catalog", "刷新目录"}, {"Position in queue:{0}", "队列位置：{0}"},
            {"GeForce NOW is allocating hardware for your game.", "GeForce NOW 正在为游戏分配硬件。"},
            {"Native GeForce NOW client for Nintendo Switch.", "Nintendo Switch 原生 GeForce NOW 客户端。"},
            {"Version", "版本"}, {"Languages", "语言"},
            {"Manage your GeForce NOW identity and persistent sign-in.", "管理 GeForce NOW 身份和持久登录。"},
            {"Balance image quality, latency and decoder stability.", "平衡画质、延迟和解码器稳定性。"},
            {"Choose in-game localization and NVIDIA graphics persistence.", "选择游戏语言和 NVIDIA 图形设置保存。"},
            {"Choose whether face buttons follow Xbox positions or Switch labels.", "选择 Xbox 按键位置或 Switch 按键标签。"},
            {"Tune stream sound without changing the console volume.", "调整串流声音而不改变主机音量。"},
            {"Inspect cache usage, diagnostics and local app data.", "查看缓存、诊断和本地应用数据。"}
        }}
    };
    const auto found = dictionaries.find(code);
    return found == dictionaries.end() ? empty : found->second;
}

} // namespace

const std::vector<InterfaceLanguageOption>& InterfaceLanguageOptions()
{
    static const std::vector<InterfaceLanguageOption> options = {
        {"en", "English"}, {"zh-CN", "简体中文"}, {"es", "Español"},
        {"ru", "Русский"}, {"it", "Italiano"}, {"fr", "Français"},
        {"pl", "Polski"}, {"uk", "Українська"},
    };
    return options;
}

bool IsSupportedInterfaceLanguage(const std::string& code)
{
    const auto& options = InterfaceLanguageOptions();
    return std::any_of(options.begin(), options.end(), [&code](const auto& option) {
        return option.code == code;
    });
}

std::string InterfaceLanguageLabel(const std::string& code)
{
    const auto& options = InterfaceLanguageOptions();
    const auto found = std::find_if(options.begin(), options.end(), [&code](const auto& option) {
        return option.code == code;
    });
    return found == options.end() ? std::string("English") : found->label;
}

void SetInterfaceLanguage(const std::string& code)
{
    g_language = IsSupportedInterfaceLanguage(code) ? code : "en";
}

const std::string& GetInterfaceLanguage()
{
    return g_language;
}

std::string Tr(const std::string& english)
{
    if (g_language == "en" || english.empty())
        return english;
    const auto& dictionary = DictionaryFor(g_language);
    const auto found = dictionary.find(english);
    if (found != dictionary.end())
        return found->second;
    const auto& completion_dictionary = CompletionDictionaryFor(g_language);
    const auto completion_found = completion_dictionary.find(english);
    if (completion_found != completion_dictionary.end())
        return completion_found->second;
    const auto& server_dictionary = ServerDictionaryFor(g_language);
    const auto server_found = server_dictionary.find(english);
    return server_found == server_dictionary.end() ? english : server_found->second;
}

std::string Tr(const char* english)
{
    return Tr(english ? std::string(english) : std::string {});
}

std::string TrFormat(const std::string& english, const std::vector<std::string>& values)
{
    std::string result = Tr(english);
    for (size_t index = 0; index < values.size(); ++index)
    {
        const std::string marker = "{" + std::to_string(index) + "}";
        size_t position = 0;
        while ((position = result.find(marker, position)) != std::string::npos)
        {
            result.replace(position, marker.size(), values[index]);
            position += values[index].size();
        }
    }
    return result;
}

} // namespace opennow
