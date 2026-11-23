# Аудит управляемого (.NET) кода MediaPortal-1 и оценка перехода на современный .NET

| | |
|---|---|
| Дата | 2026-11-23 |
| Ветка | `MP1.39_Final_Update_I` (коммит `57d5c5f370`) |
| Объём | 116 проектов `.csproj` (все старого формата, не SDK-style), 3 096 файлов `.cs`, около 1 019 000 строк. Нативные фильтры (`DirectShowFilters`) — в отдельном документе `DirectShowFilters/SECURITY_AUDIT.md` |
| Платформа | .NET Framework 4.7.2 (105 проектов; ещё 11 без явной версии), C# 7.3 (по умолчанию для net472), x86/x64 |
| Метод | автоматический обход всего кода по 30 шаблонам, сверка пакетов с GitHub Advisory Database и NVD, чтение кода в найденных местах |
| Статус | **результат автоматизированного анализа**. Сборка, запуск, тесты и проверка эксплуатации не проводились |

Документ написан так, чтобы задачи из раздела 4 мог выполнить исполнитель без полного контекста (в том числе более простая модель). Каждая задача содержит место, суть, точные шаги, проверку и ограничения.

---

## 1. Краткие выводы

1. **Критично: TV Server принимает .NET Remoting из сети без аутентификации с `BinaryFormatter` и `TypeFilterLevel=Full`** (TCP 31456). Это известный класс уязвимостей: любой, кто достучится до порта, может выполнить код с правами службы TV Server. Обратный канал клиента MediaPortal (`RemoteControl.cs`) открыт так же. Это проблема №1 и одновременно главный блокер перехода на новый .NET.
2. **Высокий риск**:
   - служба WatchDog без пароля выполняет по HTTP (порт 9997) перезагрузку, выключение, остановку TV Server, выгрузку и очистку журналов Windows;
   - PowerScheduler открывает SOAP-Remoting (31457 на сервере, 31458 на клиенте);
   - C#-скрипты граббера фильмов скачиваются **по HTTP** и компилируются и исполняются без проверки целостности;
   - распаковка ZIP старой библиотекой Ionic.Zip 1.7.2 с уязвимостью Zip Slip (CVE-2018-1002205), в том числе в службе TV Server при импорте XMLTV;
   - DeployTool по умолчанию ставит MySQL/MariaDB (`root`) и SQL Server (`sa`) с паролем `MediaPortal` и открывает порты СУБД и всех программ в брандмауэре для **любых** адресов.
3. **Уязвимые сторонние компоненты**: Ionic.Zip 1.7.2 (CVE-2018-1002205), log4net 1.2.9/1.2.10 (CVE-2018-1285, XXE; CVE-2026-40021), FFmpeg 4.1.1 (63 CVE в NVD, из них 2 критические и 24 высоких), NHibernate 2.0.1 (CVE-2024-39677, только в утилите MPRepository). Много заброшенных библиотек: Gentle.NET (2007), MySql.Data 6.x, CS-Script (старый), SharpDX 4.2.0, edtFTPnet 1.2.2, Castle 1.0.
4. **Ошибки поведения**:
   - деление на ноль при обновлении грабберов (3 места);
   - около 15 ошибочных строк формата: потерянные строки лога и `FormatException` вместо нужного исключения;
   - 26 случаев `throw e;` (теряется стек);
   - установщик проверяет .NET 4.0/4.5, хотя всё собрано под 4.7.2;
   - 344 пустых `catch`, 57 вызовов `Thread.Abort`, 236 `lock(this)`/`lock(typeof)`.
5. **Переход на .NET 8/10 реален, но это проект на 6–12 месяцев**, а не перенацеливание:
   - нужно заменить .NET Remoting (вся связь TV Server ↔ клиенты), `AppDomain`, `BinaryFormatter`/`SoapFormatter`, CS-Script, 208 устаревших элементов WinForms, `Thread.Abort`, async-делегаты, Gentle.NET/MySql.Data;
   - нужно отказаться от Windows 7/8.1;
   - главный внешний риск — совместимость сторонних плагинов, собранных под .NET Framework.
   Рекомендуемый путь: сначала закрыть безопасность и перейти на .NET Framework 4.8 с SDK-style проектами (4.8 — последняя версия для Windows 7, раздел 6.6), затем поэтапно (раздел 6).

---

## 2. Правила для исполнителя задач

1. **Одна задача — одна ветка и один коммит** (или небольшая серия). Не исправлять «попутно» другой код.
2. **Язык C# 7.3**: проекты старого формата под net472. Нельзя: `using var`, switch-выражения, nullable reference types, `??=`, диапазоны `..`, record. Можно: `out var`, кортежи (`System.ValueTuple` подключён), интерполяция строк.
3. **Не добавлять пакеты NuGet**, если в задаче явно не сказано. Пакеты подключаются через `packages.config` и `<Reference>` с `HintPath` на `..\..\Packages\...`.
4. **Сборка**:
   - MediaPortal: `Build\MSBUILD_Rebuild_Release_MediaPortal.bat` (решение `mediaportal\MediaPortal.sln`);
   - TV Server и плагин клиента: `Build\MSBUILD_Rebuild_Release_TVServer_Client.bat` (`TvEngine3\TVLibrary\TvLibrary.sln`, `TvEngine3\TVLibrary\TvPlugin\TvPlugin.sln`);
   - инструменты: соответствующее `.sln` в `Tools\...` через `msbuild <sln> /p:Configuration=Release /p:Platform=x86`;
   - перед сборкой: `Build\RestorePackages.bat`.
5. **Тесты**: `mediaportal\MediaPortal.Tests` (NUnit 2.x), `TvEngine3\TVLibrary\UnitTests\*`. Если тестов на изменённое место нет и их легко добавить — добавить. Существующие тесты не менять, кроме случаев, когда задача прямо меняет проверяемое поведение.
6. **Совместимость с плагинами сторонних авторов**: не менять сигнатуры `public` классов в `Core`, `Utils`, `Databases`, `TvControl`, `TvLibrary.Interfaces` без пометки в задаче. Плагины компилируются против этих сборок.
7. **Изменения протокола TV Server ↔ клиент** (Remoting) делать только в рамках задачи SEC-01x, одновременно на сервере и клиенте.
8. Если шаг задачи не совпадает с кодом (номер строки сдвинулся, метода нет), найти место по фрагменту кода из задачи. Если найти не удалось — остановиться и сообщить, не угадывать.

---

## 3. Сводная таблица находок

Серьёзность: **Критическая** — удалённое выполнение кода без аутентификации; **Высокая** — выполнение кода при MITM/вредоносных данных или удалённое управление без пароля; **Средняя** — нужны особые условия или ограниченный ущерб; **Низкая** — надёжность, диагностика, долг.
Достоверность: **Подтв.** — подтверждено чтением кода/конфигурации; **Эвр.** — найдено шаблоном, выборочно проверено; **Гип.** — нужна проверка тестом.

| ID | Серьёзность | Достов. | Кратко | Сложность |
|---|---|---|---|---|
| SEC-01 | Критическая | Подтв. | Remoting TV Server: BinaryFormatter, `TypeFilterLevel=Full`, без аутентификации, TCP 31456; обратный канал клиента так же | эпик (+ быстрые меры) |
| SEC-02 | Высокая | Подтв. | WatchDogService: перезагрузка/выключение/логи/очистка журналов по HTTP 9997 без пароля | средняя |
| SEC-03 | Высокая | Подтв. | PowerScheduler: SOAP-Remoting на 31457 (сервер) и 31458 (клиент) без аутентификации | средняя |
| SEC-04 | Высокая | Подтв. | C#-скрипты грабберов скачиваются по HTTP и исполняются без проверки | простая (HTTPS) + средняя (хеши) |
| SEC-05 | Высокая | Подтв. | Zip Slip: Ionic.Zip 1.7.2, `ExtractAll`/`Extract` без проверки путей (служба TV Server, PostSetup, MPE) | простая |
| SEC-06 | Высокая | Подтв. | Пароль СУБД по умолчанию `MediaPortal` (root/sa), порты СУБД открыты для всех | простая–средняя |
| SEC-07 | Высокая | Подтв. | Брандмауэр: все программы разрешены для `NET_FW_SCOPE_ALL` | простая |
| SEC-08 | Средняя | Эвр./Гип. | XML: `DtdProcessing.Parse` и `XmlTextReader` на данных из сети (XMLTV, RSS) | простая |
| SEC-09 | Средняя | Подтв. | `BinaryFormatter`/`SoapFormatter` для файлов кешей и представлений | средняя |
| SEC-10 | Средняя | Подтв. | Скачивание по HTTP метаданных и файлов настройки (`satellites.xml`, парсер-строки) | простая |
| SEC-11 | Средняя | Подтв. | DeployTool/MPE запускают скачанные установщики и расширения без проверки подписи/хеша | средняя |
| SEC-12 | Средняя | Подтв. | Пароль СУБД в командной строке (`--password=`) и в INI-файле | простая |
| SEC-13 | Низкая | Подтв. | Жёсткий ключ API TMDB в коде; мёртвые API (Amazon SOAP 2005, TMDB 2.1) | простая |
| SEC-14 | Низкая | Подтв. | `EncryptDecrypt`: TripleDES-ECB с ключом `"MPcrypto"` в коде (публичный API) | простая |
| SEC-15 | Низкая | Подтв. | `loadFromRemoteSources enabled="true"` в `TvService/App.config` | простая |
| SEC-16 | Средняя | Подтв. | TLS 1.2 не включён по умолчанию на Windows 7: после перехода на HTTPS загрузки там сломаются | простая |
| DEP-01 | Высокая | Подтв. | Ionic.Zip 1.7.2.18 — CVE-2018-1002205 | простая |
| DEP-02 | Средняя | Подтв. | log4net 1.2.9/1.2.10 — CVE-2018-1285 (XXE), CVE-2026-40021 | простая–средняя |
| DEP-03 | Высокая | Подтв. | FFmpeg 4.1.1 (NuGet `FFmpeg.Win32/64.Static`) — 63 CVE | простая |
| DEP-04 | Средняя | Подтв. | Заброшенные компоненты: Gentle.NET, MySql.Data 6.x, CS-Script, edtFTPnet, SharpDX, Castle/NHibernate (MPRepository) | эпик |
| DEP-05 | Низкая | Подтв. | Неиспользуемые/лишние бинарники в репозитории (`HttpServer.dll`, провайдеры Gentle для Oracle/DB2/Sybase/Postgres, Typemock) | простая |
| BUG-01 | Средняя | Подтв. | Деление на ноль при обновлении грабберов (`100 / (Count - 1)`) | простая |
| BUG-02 | Низкая–средняя | Подтв. | Строки формата с отсутствующими аргументами (список в приложении Б) | простая |
| BUG-03 | Низкая | Подтв. | `throw e;` / `throw ex;` — 26 мест (приложение В) | простая |
| BUG-04 | Средняя | Подтв. | Установщик проверяет .NET 4.0/4.5 вместо 4.7.2 | простая |
| BUG-05 | Низкая | Подтв. | `DatabaseUtility.FilterText` превращает пустую строку в `"unknown"` и делает `Trim()` (искажение данных) | анализ |
| BUG-06 | Средняя | Эвр. | SQL собирается строками (525 мест); экранирование есть почти везде, но 7 мест без него | средняя |
| QUAL-01 | Средняя | Эвр. | 57 вызовов `Thread.Abort`, 7 `TerminateThread`-подобных паттернов | средняя |
| QUAL-02 | Низкая | Эвр. | 344 пустых `catch`, 236 `lock(this)`/`lock(typeof)`, 61 `GC.Collect`, 71 `Application.DoEvents`, 52 `.Result`/`.Wait()` | долгосрочно |

---

## 4. Задачи по исправлению

### SEC-01. .NET Remoting TV Server с `BinaryFormatter` и `TypeFilterLevel=Full` (Критическая)

**Где**
- `TvEngine3/TVLibrary/TvService/App.config:7-9`:
  ```xml
  <channel ref="tcp server" port="31456" name="TvControl">
    <serverProviders>
      <formatter ref="binary" typeFilterLevel="Full"/>
  ```
  Применяется в `TvEngine3/TVLibrary/TvService/Service1.cs:430-432` (`RemotingConfiguration.Configure(...)`), объект публикуется в `Service1.cs:760` (`RemotingServices.Marshal(_controller, "TvControl", typeof(IController))`).
- `TvEngine3/TVLibrary/SetupTv/App.config:7-9` — то же самое.
- Клиент: `TvEngine3/TVLibrary/TvControl/RemoteControl.cs:329-361` — обратный канал `TcpChannel` с `TypeFilterLevel.Full`, порт `0` (случайный), на всех интерфейсах, `RegisterChannel(_callbackChannel, false)` (без защиты).

**Риск.** Канал Remoting по TCP без `secure="true"` не аутентифицирует отправителя, а двоичный форматтер с `TypeFilterLevel=Full` десериализует любые типы. Известные цепочки (ysoserial.net, ExploitRemotingService) позволяют выполнить код с правами процесса. Для TV Server — это учётная запись службы (обычно с высокими привилегиями); для клиента — пользователь MediaPortal. Нужен только сетевой доступ к порту; DeployTool открывает эти программы в брандмауэре для любых адресов (SEC-07).

**Почему нельзя просто поставить `typeFilterLevel="Low"`.** Комментарий в `RemoteControl.cs:344` («needed for passing objref!»): обратные вызовы (события CI-меню, `OnCiMenu` и др.) передают `ObjRef`, что требует `Full`. Простое понижение сломает события.

**Шаги (по этапам).**

*SEC-01a (простая, сразу).* Ограничить сетевую доступность — см. SEC-07 (брандмауэр только «локальная подсеть»). Это уменьшает поверхность, но не устраняет уязвимость.

*SEC-01b (средняя, требует проверки на стенде).* Включить аутентификацию и шифрование канала Remoting (Windows-аутентификация NTLM/Kerberos):
1. Сервер, `TvService/App.config` и `SetupTv/App.config`:
   ```xml
   <channel ref="tcp server" port="31456" name="TvControl" secure="true" impersonate="false">
   ```
2. Клиент: везде, где регистрируется TCP-канал для вызовов TV Server (найти `new TcpChannel(` и `RegisterChannel(` в `TvEngine3/TVLibrary/TvControl` и `mediaportal`), добавить в свойства канала `secure = true`, `tokenImpersonationLevel = "Identification"`, и в `ChannelServices.RegisterChannel(channel, true)` передать `true`.
3. Обратный канал клиента (`RemoteControl.cs:345-352`): добавить `channelProperties.Add("secure", true);` и `channelProperties.Add("impersonate", false);`, `RegisterChannel(_callbackChannel, true)`.
4. Добавить настройку (`TvService.exe.config` → `appSettings`) для отключения `secure` в смешанных средах и описать её в release notes.
5. Проверка: клиент в той же рабочей группе с тем же пользователем подключается; клиент с другим пользователем — подключение отклоняется (ожидаемо), это нужно отразить в документации. Нужна ручная проверка на двух машинах.

**Ограничение.** Даже с аутентификацией `TypeFilterLevel=Full` опасен для аутентифицированных пользователей сети. Полное устранение — SEC-01c.

*SEC-01c (эпик, архитектура).* Заменить .NET Remoting на явный протокол с контрактом и аутентификацией: gRPC (HTTP/2 + TLS + токен или Windows-аутентификация) или WCF NetTcp (сейчас) → CoreWCF (после миграции). Это обязательная часть перехода на новый .NET (раздел 6). Не для простой модели: требуется дизайн интерфейса `IController`, событий и обратных вызовов.

**Готово, когда**: SEC-01a выполнено; SEC-01b проверено на стенде; для SEC-01c утверждён дизайн.

---

### SEC-02. WatchDogService: удалённое управление без пароля (Высокая)

**Где**
- `TvEngine3/TVLibrary/WatchDogService/WatchDogService.cs:51,61` — `new HttpChannel(9997)` и публикация `WatchDogServer` как `SingleCall`.
- `TvEngine3/TVLibrary/WatchDogService/WatchDogServer.cs`: `StartTVService()` (37), `StopTVService()` (68), `Reboot()` (99), `Shutdown()` (120), `PowerOff()` (141), `ReadLog()` (162, архив логов TV Server и журналов Windows Application/System), `ClearWindowsEventLogs()` (211), `ClearTVserverLogs()` (230).
- Клиент: `mediaportal/WatchDog/TVServerManager.cs` (URL `http://{0}:9997/WatchDogServer`).

**Риск.** Любой в сети: перезагрузить или выключить сервер, остановить запись ТВ, скачать журналы (имена, пути, адреса), стереть журналы событий Windows (сокрытие следов). Канал HTTP с SOAP-форматтером дополнительно несёт риск десериализации.

**Шаги**
1. Удалить из `WatchDogServer` и интерфейса `WatchDogServiceInterface` метод `ClearWindowsEventLogs()` и его вызовы на клиенте (`mediaportal/WatchDog`). Очистка журналов Windows не нужна для сбора логов и опасна.
2. Добавить общий секрет:
   - в `WatchDogService` при старте читать ключ из файла `%ProgramData%\Team MediaPortal\MediaPortal TV Server\watchdog.key` (создавать случайный 32-байтный ключ Base64 при первом запуске; ACL — только Administrators и SYSTEM);
   - каждый публичный метод `WatchDogServer` получает параметр `string token` и сравнивает его с ключом постоянным по времени сравнением (`CryptographicOperations.FixedTimeEquals` нет в net472 — сравнивать в цикле по всем байтам без раннего выхода);
   - клиент `TVServerManager` берёт ключ из настройки MediaPortal (поле в конфигурации WatchDog) — пользователь копирует ключ с сервера.
   Это ломающее изменение протокола WatchDog: сервер и клиент обновлять вместе.
3. Ограничить брандмауэр (SEC-07).

**Проверка.** Вызов без ключа возвращает ошибку и пишет предупреждение в журнал; с ключом — работает.

---

### SEC-03. PowerScheduler: SOAP-Remoting без аутентификации (Высокая)

**Где**
- TV Server: `TvEngine3/TVLibrary/Plugins/PowerScheduler/PowerScheduler.cs:1617-1638` — `HttpChannel` на порту 31457 с `SoapServerFormatterSinkProvider`, публикуется `IPowerController` как `PowerControl`.
- Клиент: `mediaportal/PowerSchedulerClientPlugin/PowerScheduler.cs:1495-1500` — `new HttpChannel(31458)` на всех интерфейсах, `RemotingServices.Marshal(this)`.
- Клиентский доступ: `Common-MP-TVE3/PowerScheduler.Interfaces/RemotePowerControl.cs:79`.

**Риск.** Удалённое управление режимами питания (запрет/разрешение сна, пробуждение) любым узлом сети; SOAP-десериализация по сети.

**Шаги**
1. Клиент (`PowerSchedulerClientPlugin/PowerScheduler.cs:1495`): канал нужен только для локального доступа (`_remotingURI = "http://localhost:31458"...`). Создавать его с привязкой к loopback:
   ```csharp
   IDictionary props = new Hashtable();
   props["port"] = 31458;
   props["bindTo"] = "127.0.0.1";
   ChannelServices.RegisterChannel(new HttpChannel(props, null, new SoapServerFormatterSinkProvider()), false);
   ```
   Перед изменением найти все использования `_remotingURI`/порта 31458 и убедиться, что обращения идут только с той же машины. Если удалённые вызовы есть — не менять и отметить в отчёте.
2. Сервер (31457): клиенты обращаются удалённо, поэтому только брандмауэр (SEC-07) и в рамках SEC-01c — замена протокола.

---

### SEC-04. Скрипты грабберов по HTTP исполняются без проверки (Высокая)

**Где** (константы URL):
- `mediaportal/Configuration/Sections/MovieDatabase.cs:135` (`GrabberIndexUrl`), `:2634`, `:2636`, `:2638`;
- `mediaportal/WindowPlugins/GUISettings/MyVideos/GUISettingsMyVideos_Database.cs:92` (`_grabberIndexUrl`), `:483`, `:485`, `:487`;
- `mediaportal/WindowPlugins/GUIVideos/GUIVideoFiles.cs:190`, `:2712`, `:2714`, `:2716`;
- `mediaportal/Core/Util/VideoDatabaseParserStrings.cs:49`.
- Загрузка и исполнение: в индексе `MovieInfoGrabber_V16.xml` для каждого `grabber` берётся атрибут `url` (`GUISettingsMyVideos_Database.cs:560-569`, аналогично в двух других файлах), файл `.csscript` сохраняется в каталог скриптов, затем компилируется и исполняется через CS-Script (`mediaportal/Databases/Video/IMDB.cs:245,310`, `mediaportal/Core/Util/InternalCSScriptGrabbersLoader.cs:95`, `MovieDatabase.cs:2768`).

**Риск.** Атакующий на пути трафика (публичный Wi-Fi, подменённый DNS, прокси) подменяет индекс или скрипт и получает выполнение кода в MediaPortal и в программе конфигурации.

**Шаги (часть 1, простая)**
1. Во всех перечисленных константах заменить `http://install.team-mediaportal.com/` на `https://install.team-mediaportal.com/`. Этот же хост уже используется по HTTPS (`Tools/MediaPortal.DeployTool/Utils.cs:126`, `mediaportal/MPE/MpeCore/Classes/ExtensionUpdateDownloader.cs:35`).
2. В циклах скачивания грабберов (по одному в каждом из трёх файлов: `string url = sectionNodes[i].Attributes["url"].Value;`) добавить сразу после чтения `url`:
   ```csharp
   if (url.StartsWith("http://install.team-mediaportal.com/", StringComparison.OrdinalIgnoreCase))
   {
     url = "https://" + url.Substring("http://".Length);
   }
   if (!url.StartsWith("https://", StringComparison.OrdinalIgnoreCase))
   {
     Log.Warn("Grabber download skipped, only https is allowed: {0}", url);
     continue;
   }
   ```
3. Приложения, собранные под .NET Framework 4.7+, берут версии TLS из настроек ОС (TLS 1.2 на Windows 10/11). Сканирование не нашло в коде принудительной установки `SecurityProtocolType.Ssl3/Tls/Tls11`. Если после перехода на HTTPS загрузка падает с ошибкой TLS на старых ОС — не менять `ServicePointManager` в этой задаче, а сообщить.

**Шаги (часть 2, средняя)**
4. Добавить в индекс `MovieInfoGrabber_V16.xml` (на стороне сервера) атрибут `sha256` для каждого граббера и проверять хеш после скачивания перед записью в каталог скриптов; файл с несовпадающим хешем удалять, ошибку логировать. Требует изменения файла на сервере `install.team-mediaportal.com` — согласовать с командой.

**Проверка.** Обновление грабберов работает; подмена URL на `http://` в индексе (вручную в локальной копии) приводит к пропуску файла.

---

### SEC-05. Zip Slip при распаковке (Высокая)

**Где**
- `TvEngine3/TVLibrary/Plugins/XmlTvImport/XmlTvImporter.cs:341-343` — `zip.ExtractAll(newLoc, true)` для ZIP, скачанного с настроенного URL. Выполняется в службе TV Server.
- `mediaportal/PostSetup/MP3PartInstaller.cs:361-370` — `e.Extract(targetDirectory, true)` для скачанного ZIP.
- `mediaportal/MPE/MpeCore/Classes/ZipProvider/ZipProviderClass.cs:44,87` и `mediaportal/MPE/MpeInstaller/MainForm.cs:537` — пакеты расширений.
- Библиотека: `MediaPortal.Base/Ionic.Zip.dll` версии 1.7.2.18 (DEP-01), уязвима к CVE-2018-1002205.

**Риск.** Элемент архива с именем `..\..\..\Windows\...` или абсолютным путём записывается вне каталога назначения: подмена DLL или исполняемых файлов. Для XMLTV — с правами службы TV Server.

**Шаги**
1. Добавить вспомогательный метод (C# 7.3) в каждый из трёх проектов (`XmlTvImport`, `PostSetup`, `MpeCore`), например как `private static` в том же файле, где идёт распаковка:
   ```csharp
   private static void ExtractEntrySafe(Ionic.Zip.ZipEntry entry, string targetDirectory, bool overwrite)
   {
     string root = Path.GetFullPath(targetDirectory);
     if (!root.EndsWith(Path.DirectorySeparatorChar.ToString()))
     {
       root += Path.DirectorySeparatorChar;
     }
     string destination = Path.GetFullPath(Path.Combine(root, entry.FileName));
     if (!destination.StartsWith(root, StringComparison.OrdinalIgnoreCase))
     {
       throw new InvalidDataException("Zip entry points outside the target directory: " + entry.FileName);
     }
     entry.Extract(targetDirectory, overwrite);
   }
   ```
2. `XmlTvImporter.cs:343`: заменить `zip.ExtractAll(newLoc, true);` на
   ```csharp
   foreach (ZipEntry entry in zip)
   {
     ExtractEntrySafe(entry, newLoc, true);
   }
   ```
   Исключение `InvalidDataException` уже перехватывается общим `catch (Exception ex2)` ниже — проверить, что сообщение пишется в лог, и не повторять попытку 10 раз для этого случая: добавить перед общим `catch` отдельный `catch (InvalidDataException ex3) { Log.Error("xmltv: rejected zip file {0}: {1}", path, ex3.Message); waitingForFileAccess = false; break; }`.
3. `MP3PartInstaller.cs:368`: `e.Extract(targetDirectory, true);` → `ExtractEntrySafe(e, targetDirectory, true);`. В том же методе `throw e;` (строка 377) → `throw;`.
4. MPE (`ZipProviderClass.cs:87`): там распаковка идёт в `FileStream` по пути назначения из описания пакета. Добавить проверку, что итоговый путь назначения (`destination` в `CopyFile.cs:76`, `GenericSkinFile.cs:76`) после `Path.GetFullPath` находится внутри одного из ожидаемых каталогов MediaPortal (`Config.GetFolder(...)` для Base, Plugins, Skin, Config, Cache, Thumbs). Если нет — прерывать установку с понятным сообщением.
5. Долгосрочно — DEP-01 (замена Ionic.Zip).

**Проверка.** Тестовый архив с элементом `..\evil.txt` и элементом `C:\Temp\evil.txt` (создать вручную скриптом) отклоняется, файл вне каталога не создаётся; обычный архив XMLTV распаковывается.

---

### SEC-06. Пароль СУБД по умолчанию и открытые порты (Высокая)

**Где**
- `Tools/MediaPortal.DeployTool/Sections/BaseInstallationTypeDlg.cs:87` и `Tools/MediaPortal.DeployTool/Sections/MySQLChoice.cs:81` — `InstallationProperties.Instance.Set("DBMSPassword", "MediaPortal");`
- `Tools/MediaPortal.DeployTool/Sections/DBMSSettingsDlg.Designer.cs:104` — `textBoxPassword.Text = "MediaPortal";`
- `Tools/MediaPortal.DeployTool/InstallationChecks/MSSQLExpressChecker.cs:53-54` — режим SQL-аутентификации и пароль `sa`.
- Порты: `Tools/MediaPortal.DeployTool/InstallationChecks/WindowsFirewallChecker.cs:142-` — 1433/TCP, 1434/UDP, 3306 открываются с `NET_FW_SCOPE_ALL`.

**Риск.** Все установки «в один клик» имеют одинаковый пароль администратора СУБД, доступный из сети. Для SQL Server `sa` даёт выполнение команд ОС (`xp_cmdshell`), для MySQL/MariaDB `root` — полный доступ к данным.

**Шаги**
1. В `BaseInstallationTypeDlg.cs:87` и `MySQLChoice.cs:81` вместо `"MediaPortal"` генерировать случайный пароль:
   ```csharp
   private static string GeneratePassword()
   {
     const string chars = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789";
     byte[] data = new byte[20];
     using (var rng = new System.Security.Cryptography.RNGCryptoServiceProvider())
     {
       rng.GetBytes(data);
     }
     var sb = new StringBuilder(20);
     foreach (byte b in data)
     {
       sb.Append(chars[b % chars.Length]);
     }
     return sb.ToString();
   }
   ```
   Поместить метод в `Tools/MediaPortal.DeployTool/Utils.cs` как `public static`. Пароль нигде не логировать.
2. Показывать сгенерированный пароль пользователю в окне итогов установки и записывать его только туда, куда его уже пишет установщик для TV Server (строка подключения в `Gentle.config` — проверить, где DeployTool её формирует, и не менять формат).
3. `DBMSSettingsDlg.Designer.cs:104`: убрать значение по умолчанию (`""`), в `DBMSSettingsDlg.cs:81` уже есть проверка на пустой пароль.
4. Порты СУБД в брандмауэре — см. SEC-07 (только локальная подсеть).
5. Удалять INI-файл с паролем `sa` после установки SQL Server (найти, где создаётся `iniFile` в `MSSQLExpressChecker.cs`, и добавить `File.Delete(iniFile)` в `finally` после запуска установщика).

**Проверка.** Новая установка: TV Server подключается к СУБД; пароль не `MediaPortal`; INI удалён.

---

### SEC-07. Брандмауэр разрешает доступ из любых сетей (Высокая)

**Где.** `Tools/MediaPortal.DeployTool/InstallationChecks/WindowsFirewallChecker.cs:118, 122, 126, 134, 138` (`AuthorizeApplication(..., NET_FW_SCOPE_.NET_FW_SCOPE_ALL, ...)`) и `:149, :154` и далее для портов СУБД (`GloballyOpenPort(..., NET_FW_SCOPE_.NET_FW_SCOPE_ALL, ...)`).

**Шаги.** Во всех вызовах `AuthorizeApplication` и `GloballyOpenPort` в этом файле заменить `NET_FW_SCOPE_.NET_FW_SCOPE_ALL` на `NET_FW_SCOPE_.NET_FW_SCOPE_LOCAL_SUBNET`. Больше ничего не менять.

**Проверка.** После установки в «Брандмауэр Защитника Windows → Разрешённые приложения» область для правил MediaPortal — «Локальная подсеть».

**Ограничение.** Клиенты в другой подсети (VPN, маршрутизируемые VLAN) перестанут подключаться; указать это в release notes и в справке DeployTool.

---

### SEC-08. Обработка XML с DTD из сети (Средняя)

**Где**
- `TvEngine3/TVLibrary/Plugins/XmlTvImport/XMLTVImport.cs:229, 400` — `XmlReader.Create(fileName, new XmlReaderSettings() { DtdProcessing = DtdProcessing.Parse })` для скачанных XMLTV.
- `mediaportal/WindowPlugins/GUIRSSFeed/RssReader.cs:96, 112, 128` — `new XmlTextReader(url|textReader|stream)` для RSS из интернета.
- `TvEngine3/TVLibrary/Plugins/XmlTvImport/XmlTvSetup.cs:606`, `mediaportal/Core/SerialUIR/SerialUIR.cs:284`.
- Остальные `XmlTextReader` (около 30 в `MiniDisplayLibrary`, `Tools/SkinEditor`) читают локальные файлы конфигурации — низкий риск, менять не обязательно.

**Риск.** Разбор DTD позволяет атаку «billion laughs» (исчерпание памяти/CPU). `XmlTextReader` по умолчанию обрабатывает DTD и, в зависимости от рантайма, может разрешать внешние сущности (XXE: чтение локальных файлов, запросы на внутренние адреса). Для `XmlReaderSettings` в .NET 4.5.2+ `XmlResolver` по умолчанию `null`, поэтому для XMLTV основной риск — DoS. Для RSS — **проверить тестом** (Гип.).

**Шаги**
1. `XMLTVImport.cs:229` и `:400`: заменить настройки на
   ```csharp
   new XmlReaderSettings() { DtdProcessing = DtdProcessing.Ignore, XmlResolver = null, MaxCharactersFromEntities = 1024 * 1024 }
   ```
   XMLTV-файлы часто содержат `<!DOCTYPE tv SYSTEM "xmltv.dtd">` — `Ignore` их пропускает без ошибки. Проверить импорт на реальном файле XMLTV с DOCTYPE.
2. `RssReader.cs:96, 112, 128`: вместо `new XmlTextReader(x)` использовать
   ```csharp
   var settings = new XmlReaderSettings { DtdProcessing = DtdProcessing.Ignore, XmlResolver = null };
   reader = XmlReader.Create(x, settings);
   ```
   Если тип переменной `reader` — `XmlTextReader`, поменять его на `XmlReader` и проверить, что не используются члены, которых нет у `XmlReader` (например `WhitespaceHandling`). Если используются — оставить `XmlTextReader`, но сразу после создания добавить `reader.DtdProcessing = DtdProcessing.Ignore; reader.XmlResolver = null;`.
3. `XmlTvSetup.cs:606` и `SerialUIR.cs:284`: тот же способ, что в шаге 2 (с `DtdProcessing`/`XmlResolver` на `XmlTextReader`).

**Проверка.** Тестовый RSS с `<!DOCTYPE r [<!ENTITY x SYSTEM "file:///C:/Windows/win.ini">]><rss>&x;</rss>` не раскрывает содержимое файла; обычные ленты читаются.

---

### SEC-09. `BinaryFormatter`/`SoapFormatter` (Средняя; блокер миграции)

**Где**
- `BinaryFormatter`: `mediaportal/Core/guilib/TexturePacker.cs:260,275,300,317`; `mediaportal/Core/guilib/GUIFont.cs:1184`; `Common-MP-TVE3/PowerScheduler.Interfaces/EPGWakeupConfig.cs:63,79`; `TvEngine3/TVLibrary/Plugins/ServerBlaster/ServerBlaster/ServerBlaster.cs:167`; `ServerBlaster (Learn)/Form1.cs:756,805`; `Common-MP-TVE3/External/Gentle.NET/Source/Gentle.Common/Reflection/TypeConverter.cs:198`.
- `SoapFormatter`: `mediaportal/Configuration/Sections/BaseViews.cs:462,779,822`; `mediaportal/WindowPlugins/Common.GUIPlugins/Music/MusicViewHandler.cs:79`; `mediaportal/WindowPlugins/GUIVideos/VideoViewHandler.cs:61`; `Tools/SkinEditor/src/Controls/MpeFont.cs:382,465`; `ServerBlaster (Learn)/Form1.cs:787`.

**Риск.** Файлы кешей шрифтов и текстур, представления (views) и настройки ИК-бластера десериализуются форматтерами, исполняющими код при вредоносных данных. Файлы лежат в каталогах пользователя и в скинах (скины распространяются третьими лицами). Кроме того, `BinaryFormatter` удалён из .NET 9, `SoapFormatter` отсутствует в .NET Core вообще.

**Шаги (по одному файлу на задачу).**
1. **Кеши** (`TexturePacker`, `GUIFont`): заменить на собственный простой двоичный формат через `BinaryWriter`/`BinaryReader` с номером версии в начале файла. При чтении старого кеша (другая сигнатура) — удалить файл и перестроить кеш (кеш одноразовый, потеря допустима).
2. **`EPGWakeupConfig`**: заменить на явную сериализацию в строку (например, `string.Join` по полям или XML через `XmlSerializer`). Для чтения старого формата оставить в коде ветку с `BinaryFormatter`, но с `SerializationBinder`, разрешающим только тип `EPGWakeupConfig` и примитивы. Отметить ветку `// TODO: remove after migration` нельзя — по правилам проекта комментарии только по делу: в описании задачи зафиксировать срок удаления.
3. **Представления (views)** в `BaseViews`, `MusicViewHandler`, `VideoViewHandler`: перейти на `XmlSerializer` с новым именем файла (например, `MusicViews.v2.xml`); при отсутствии нового файла один раз прочитать старый через `SoapFormatter` с `SerializationBinder` (только классы представлений из `MediaPortal.GUI.View`) и сохранить в новом формате.
4. **ServerBlaster**: аналогично п. 3 для файла настроек ИК-кодов.
5. Gentle `TypeConverter.cs:198` — в рамках DEP-04.

---

### SEC-10. Загрузка данных по HTTP (Средняя)

**Где (основное)**
- `TvEngine3/TVLibrary/SetupTv/Sections/CardDvbS.cs:267` (`satellites.xml`) и `:319` (подмена адреса `fastsatfinder.com` на `install.team-mediaportal.com` по HTTP в загруженном файле);
- `mediaportal/Core/Util/VideoDatabaseParserStrings.cs:49` и константы из SEC-04;
- `mediaportal/LastFMLibrary/LastFMLibrary.cs` (`http://ws.audioscrobbler.com/2.0/`), `mediaportal/Core/Util` (Amazon SOAP), `mediaportal/Databases/Music` (allmusic), `mediaportal/PostSetup` (sourceforge `http://`);
- всего 62 различных `http://`-адреса в коде (полный список можно повторить поиском `"http://` по `*.cs`).

**Шаги**
1. Для `install.team-mediaportal.com` — заменить на `https://` (хост поддерживает HTTPS).
2. Для `ws.audioscrobbler.com` — заменить на `https://ws.audioscrobbler.com/2.0/` (Last.fm поддерживает HTTPS).
3. Для мёртвых сервисов (Amazon SOAP 2005, TMDB API 2.1, allmusic search, sourceforge-зеркала xmltv 0.5.37) — не исправлять адрес, а завести отдельную задачу на удаление функции (SEC-13).
4. Для `CardDvbS.cs:319` заменить строку замены на `https://install.team-mediaportal.com/tvsetup/DVBS`.

---

### SEC-11. Запуск скачанных установщиков и расширений без проверки (Средняя)

**Где.** `Tools/MediaPortal.DeployTool/HTTPDownload.cs`, `Tools/MediaPortal.DeployTool/Utils.cs:382` (`Process.Start(TempFullPathName, " /S _?=" + Directory)`), проверки установки компонентов в `InstallationChecks/*`; MPE: `mediaportal/MPE/MpeCore/Classes/ExtensionUpdateDownloader.cs`, адрес обновлений пакета задаётся автором расширения (`MpeMaker/Sections/GeneralSection.cs:92`, `Package.GeneralInfo.UpdateUrl`).

**Риск.** Целостность зависит только от TLS и внешних хостов; для расширений адрес обновлений может быть `http://`. DeployTool запускает установщики с правами администратора.

**Шаги**
1. DeployTool: перед запуском скачанного `.exe`/`.msi` проверять подпись Authenticode через `WinVerifyTrust` (P/Invoke `wintrust.dll`) и издателя (список ожидаемых издателей по компонентам). При ошибке — не запускать, показать сообщение.
2. MPE: при скачивании обновлений расширений отклонять `http://`-адреса (по аналогии с SEC-04 шаг 2), кроме `install.team-mediaportal.com`, который переписывать на HTTPS.

---

### SEC-12. Пароль СУБД в командной строке и файлах (Средняя)

**Где.** `Tools/MediaPortal.DeployTool/InstallationChecks/MySQLChecker.cs:367,381`, `MySQLCheckerOutdated.cs:344,367`, `MariaDBChecker.cs:281` (`"-u root --password=" + пароль`).

**Риск.** Пароль виден в списке процессов и может попасть в журналы.

**Шаги.** Передавать пароль через переменную окружения процесса `MYSQL_PWD` (поддерживается клиентами MySQL/MariaDB) в `ProcessStartInfo.EnvironmentVariables["MYSQL_PWD"]`, а аргумент `--password=` убрать. Для команды смены пароля (`mysqladmin ... password <новый>`) новый пароль остаётся аргументом — допустимо, т.к. выполняется один раз при установке; отметить в задаче.

---

### SEC-13. Жёсткий ключ API и мёртвые интеграции (Низкая)

**Где.** `mediaportal/WindowPlugins/GUIVideos` — URL `http://api.themoviedb.org/2.1/...` с ключом `2ed40b5d82aa804a2b1fcedb5ca8d97a`; `mediaportal/Core/Util/AmazonReference.cs` (Amazon E-Commerce SOAP 2005); `mediaportal/Databases/Music/InternetLookups/AmazonWebservice.cs`.

**Шаги.** Найти использования и удалить мёртвый код TMDB 2.1 и Amazon SOAP (API давно отключены). Ключ из кода удалить. Если функция видна пользователю — убрать соответствующий пункт меню/настройки.

---

### SEC-14. `EncryptDecrypt` с ключом в коде (Низкая)

**Где.** `mediaportal/Core/Util/EncryptDecrypt.cs:38,86` — TripleDES-ECB, ключ `"MPcrypto"`. В репозитории не используется, но класс публичный — им могут пользоваться сторонние плагины.

**Шаги.** Пометить класс атрибутом `[Obsolete("Insecure: fixed key, ECB mode. Use System.Security.Cryptography.ProtectedData (DPAPI).")]`. Логику не менять (иначе сломаются данные плагинов).

---

### SEC-15. `loadFromRemoteSources` (Низкая)

**Где.** `TvEngine3/TVLibrary/TvService/App.config:43`.

**Шаги.** Проверить, нужно ли загружать плагины из сетевых путей. Если нет — удалить строку `<loadFromRemoteSources enabled="true"/>`. Проверка: служба стартует, плагины из `plugins` загружаются.

---

### SEC-16. TLS 1.2 на Windows 7 (Средняя)

**Контекст.** Приложения под .NET Framework 4.7+ берут версии TLS из настроек ОС. На Windows 7 SP1 по умолчанию SChannel для клиентских соединений через WinHTTP/.NET не включает TLS 1.1/1.2, а большинство серверов (включая `install.team-mediaportal.com`, Last.fm) принимают только TLS 1.2+. Задачи SEC-04, SEC-10, SEC-11 переводят загрузки на HTTPS — на Windows 7 без настройки они перестанут работать. Подробнее о поддержке Windows 7 — раздел 6.6.

**Шаги**
1. В установщиках MediaPortal и TV Server (`mediaportal/Setup/setup.nsi`, `TvEngine3/TVLibrary/Setup/setup.nsi`) и в `Tools/InstallationScripts/DeployToolUnPacker.nsi` при обнаружении Windows 7 (макрос `${IsWin7}` уже используется в `Tools/InstallationScripts/include/MediaPortalMacros.nsh:774`) записать в реестр (обе ветки — 32 и 64 бит):
   ```
   HKLM\SOFTWARE\Microsoft\.NETFramework\v4.0.30319            SchUseStrongCrypto       DWORD 1
   HKLM\SOFTWARE\Microsoft\.NETFramework\v4.0.30319            SystemDefaultTlsVersions DWORD 1
   HKLM\SOFTWARE\WOW6432Node\Microsoft\.NETFramework\v4.0.30319 SchUseStrongCrypto       DWORD 1
   HKLM\SOFTWARE\WOW6432Node\Microsoft\.NETFramework\v4.0.30319 SystemDefaultTlsVersions DWORD 1
   HKLM\SYSTEM\CurrentControlSet\Control\SecurityProviders\SCHANNEL\Protocols\TLS 1.2\Client  Enabled DWORD 1, DisabledByDefault DWORD 0
   ```
   Ключи не удалять при деинсталляции (их могут использовать другие программы).
2. Проверять наличие обновления KB3140245 (TLS 1.1/1.2 для WinHTTP) и, если его нет, показывать предупреждение со ссылкой на страницу Microsoft. Само обновление не скачивать.
3. В коде не добавлять `ServicePointManager.SecurityProtocol = ...` — настройка через реестр действует на все соединения процесса и не мешает Windows 10/11.

**Проверка.** На Windows 7 SP1 со всеми обновлениями после установки обновление грабберов (SEC-04) по HTTPS проходит; на Windows 10/11 поведение не меняется.

---

### DEP-01. Ionic.Zip 1.7.2.18 (Высокая)

**Где.** `MediaPortal.Base/Ionic.Zip.dll`, 8 ссылок `HintPath` в проектах (`XmlTvImport`, `WatchDogService`, `MpeCore`, `MpeInstaller`, `MediaPortal.Support`, `PostSetup` и др.).

**Шаги.** Сначала SEC-05 (немедленная защита). Затем заменить Ionic.Zip на встроенный `System.IO.Compression` (`ZipArchive`, ссылки `System.IO.Compression` и `System.IO.Compression.FileSystem` есть в net472) по одному проекту за задачу, сохраняя проверку путей из SEC-05. DotNetZip как пакет обновлять не нужно: проект заброшен.

### DEP-02. log4net 1.2.9/1.2.10 (Средняя)

**Где.** `TvEngine3/TVLibrary/TVDatabase/references/log4net.dll` и другие `HintPath` (10 ссылок).

**Риск.** CVE-2018-1285 (XXE при разборе конфигурации, если конфиг может изменить атакующий), CVE-2026-40021 (потеря событий в `XmlLayout`).

**Шаги.** Обновить до log4net 3.3.0+ через NuGet (`packages.config`) во всех проектах, где есть ссылка; проверить, что конфигурационные файлы `log4net.config` совместимы (у log4net 2.x/3.x изменилось имя сборки `log4net` с другим `PublicKeyToken` — обновить `bindingRedirect` и `assemblyIdentity`, если есть).

### DEP-03. FFmpeg 4.1.1 (Высокая)

**Где.** `mediaportal/Core/packages.config`, `TvEngine3/TVLibrary/TvThumbnails/packages.config` — `FFmpeg.Win32.Static`/`FFmpeg.Win64.Static` 4.1.1.1; используется в `mediaportal/Core/Util/VideoThumbCreator.cs` и `TvEngine3/TVLibrary/TvThumbnails/VideoThumbCreator/VideoThumbCreator.cs` (создание миниатюр из видеофайлов — недоверенные данные).

**Риск.** 63 CVE в NVD для 4.1.1 (2 критических: CVE-2019-17539, CVE-2019-17542; 24 высоких). Обработка вредоносного видеофайла — выполнение кода в MediaPortal или TV Server.

**Шаги.** Заменить пакет на актуальную сборку FFmpeg (ветка 7.x/8.x, статическая `ffmpeg.exe`), обновить пути в `setup.nsi` (MediaPortal и TV Server) и вызов в `VideoThumbCreator.cs`, если изменились параметры командной строки. Проверить создание миниатюр для `.ts`, `.mkv`, `.mp4`.

### DEP-04. Заброшенные компоненты (Средняя, эпик)

| Компонент | Где | Проблема | Замена |
|---|---|---|---|
| Gentle.NET 1.2.9/1.5.0 (исходники в `Common-MP-TVE3/External/Gentle.NET`) | слой БД TV Server | 2007 год, ORM на рефлексии, `BinaryFormatter` | Dapper или EF Core (после миграции) |
| MySql.Data 6.6.7 / 6.0.3 / 1.0.7 | провайдер Gentle для MySQL | старый TLS, нет поддержки | MySqlConnector |
| CSScriptLibrary (старая, CodeDom) | грабберы, MPE | не работает на .NET Core | CS-Script для .NET (Roslyn) или отказ от скриптов |
| edtFTPnet 1.2.2 | `MediaPortal.Base` | 2006 год | FluentFTP |
| SharpDX 4.2.0 | рендеринг | архивирован с 2019 | Vortice.Windows |
| NHibernate 2.0.1, Castle 1.0 | `Tools/MPRepository` | CVE-2024-39677 (SQL-инъекция) | удалить инструмент или обновить |
| Npgsql, Oracle, DB2, Sybase в `Gentle.NET/.../lib` | не используются TV Server | CVE-2024-32655 (Npgsql) | удалить из репозитория |

### DEP-05. Лишние бинарники (Низкая)

`TVServer.Base/HttpServer.dll` — нет ни одного использования в коде; провайдеры Gentle для Oracle/DB2/Sybase/Postgres; `Typemock.ArrangeActAssert` по абсолютному пути `C:/Program Files (x86)/...`. Удалить ссылки и файлы после проверки, что сборки не используются установщиком (`setup.nsi`).

---

### BUG-01. Деление на ноль при обновлении грабберов (Средняя)

**Где.**
- `mediaportal/Configuration/Sections/MovieDatabase.cs:2588`
- `mediaportal/WindowPlugins/GUISettings/MyVideos/GUISettingsMyVideos_Database.cs:566`
- `mediaportal/WindowPlugins/GUIVideos/GUIVideoFiles.cs:2804`

Код: `percent += 100 / (sectionNodes.Count - 1);` — при одном грабере в индексе `DivideByZeroException` (целочисленное деление) и обновление обрывается.

**Шаг.** Заменить на
```csharp
percent = (int)((i + 1) * 100L / sectionNodes.Count);
```
(переменная цикла — `i`, проверить имя). Проверка: индекс с одним `grabber` обрабатывается без исключения.

Связанное: `mediaportal/Core/guilib/GUIListControl.cs:1228` — `(float)SelectedListItemIndex / (_listItems.Count - 1) * 100.0f` даёт `NaN` при одном элементе. Добавить ветку: если `_listItems.Count <= 1`, то `fPercent = 0`.

### BUG-02. Ошибочные строки формата (Низкая–средняя)

Список в приложении Б. Логгеры MediaPortal (`LogImpl.cs:339-347`) и TV Server (`Log.cs:414-456`) перехватывают `FormatException` — строка лога просто теряется. Но в двух местах ошибка формата заменяет настоящее исключение:
- `TvEngine3/TVLibrary/TVLibrary/Implementations/Dri/Parser/NttParser.cs:536` — `{2}` при двух аргументах: вместо сообщения о повреждении данных выбрасывается `FormatException`. Исправить `{2}` → `{1}`.
- `Common-MP-TVE3/External/Gentle.NET/Source/Gentle.Framework/Validation/RangeValidatorAttribute.cs:144` — `String.Format("... {0} ...")` без аргумента. Передать проверяемое значение.

Для остальных строк: привести номера `{n}` в соответствие с числом аргументов (обычно `{1}` → `{0}`), либо добавить пропущенный аргумент, если по смыслу он есть в коде рядом.

### BUG-03. `throw e;` (Низкая)

26 мест (приложение В). Заменить `throw e;`/`throw ex;` на `throw;`, если исключение перебрасывается из того же `catch` без изменений. Если переменная исключения не используется в `catch` после замены, убрать её имя (`catch (Exception)`), чтобы не было предупреждения CS0168.

### BUG-04. Проверка .NET Framework в установщике (Средняя)

**Где.** `Tools/InstallationScripts/DeployToolUnPacker.nsi:164-345` — проверяется и при необходимости ставится .NET 4.0/4.5 (`Release` из `HKLM\SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full`, загрузка `dotnetfx45_full_x86_x64.exe`). Все проекты требуют 4.7.2.

**Шаги.** Проверять `Release >= 461808` (4.7.2). Если меньше — предлагать установить .NET Framework 4.8 (автономный установщик с сайта Microsoft по HTTPS) вместо 4.5. Сообщения обновить. Проверка: на чистой Windows 10 1607 без 4.7.2 установщик предлагает 4.8.

### BUG-05. `FilterText` искажает данные (Низкая, анализ)

`mediaportal/Databases/DatabaseUtility.cs:575-584`: пустая или пробельная строка превращается в `Strings.Unknown`, остальные строки обрезаются (`Trim()`). Функция используется для экранирования SQL почти во всех запросах, поэтому пустые значения тегов сохраняются как «unknown». Не исправлять без анализа: часть логики (группировка по «unknown») может на этом строиться. Задача — описать, где это поведение нужно, и вынести экранирование (`Replace("'", "''")`) в отдельный метод для новых мест.

### BUG-06. SQL, собранный строками (Средняя)

525 мест (в основном `VideoDatabaseSqlLite.cs` — 171, `PictureDatabaseSqlLite.cs` — 94, `MusicDatabase.*` — 77). Экранирование `'`→`''` (`DatabaseUtility.RemoveInvalidChars`) корректно для строковых литералов SQLite. Обёртка `mediaportal/Databases/SQLiteClient.cs` не поддерживает параметры (только `Execute(string)`).

Места без экранирования (найдены эвристикой):
- `mediaportal/Databases/DatabaseUtility.cs:126, 166, 189-191, 267-269` — имена таблиц/представлений/индексов/триггеров (значения из кода, риск низкий);
- `mediaportal/Databases/Pictures/SqlLite/PictureDatabaseSqlLite.cs:1875` — `Year` в `WHERE ... = '" + Year + "'`.

**Шаги.**
1. `PictureDatabaseSqlLite.cs:1875`: обернуть `Year` в `DatabaseUtility.RemoveInvalidChars(...)` или, если это число, привести к `int` через `int.TryParse` и подставлять число.
2. Эпик: добавить в `SQLiteClient` метод `Execute(string sql, params object[] parameters)` на `sqlite3_bind_*` и переводить запросы на параметры по мере изменений кода.

### QUAL-01. `Thread.Abort` (Средняя; блокер миграции)

57 вызовов (больше всего — `TvEngine3/TVLibrary` 17, `mediaportal/WindowPlugins` 15, `mediaportal/Databases/Video/IMDBFetcher.cs` 8). `Thread.Abort` может прервать поток внутри `lock`/`finally`, оставив данные и блокировки в несогласованном состоянии; в .NET 5+ он выбрасывает `PlatformNotSupportedException`.

**Шаги (по одному классу за задачу).** Ввести флаг остановки (`volatile bool _stopRequested` или `CancellationTokenSource`), проверять его в цикле потока, вместо `Abort()` — установить флаг и `Join(timeout)`. Если поток блокирован в чтении сокета/порта — закрыть сокет/порт, чтобы чтение завершилось.

### QUAL-02. Прочий технический долг (Низкая)

- 344 пустых `catch {}`: при правке соседнего кода добавлять хотя бы `Log.Debug`.
- 236 `lock(this)`/`lock(typeof(...))`: заменять на `private readonly object _lock = new object();` только в классах, которые меняются по другой задаче.
- 61 `GC.Collect`, 71 `Application.DoEvents`, 52 блокирующих `.Result`/`.Wait()`: не трогать массово; учитывать при миграции.

---

## 5. Проверенные зависимости

| Компонент | Версия | Источник | Статус |
|---|---|---|---|
| Ionic.Zip (DotNetZip) | 1.7.2.18 | `MediaPortal.Base` | **CVE-2018-1002205** (Zip Slip) |
| log4net | 1.2.9 / 1.2.10 | `TVDatabase/references` | **CVE-2018-1285**, CVE-2026-40021 |
| FFmpeg (static) | 4.1.1.1 | NuGet | **63 CVE** (2 критических, 24 высоких) |
| NHibernate | 2.0.1 | `Tools/MPRepository/Libs` | **CVE-2024-39677** |
| Npgsql | ? | `Gentle.NET/.../lib` | CVE-2024-32655 (если версия ≤ 4.0.13); не используется |
| MySql.Data | 6.6.7 / 6.0.3 / 1.0.7 | `Gentle.Provider.MySQL/lib`, `Libs` | CVE в GHSA нет; не поддерживается |
| Newtonsoft.Json | 13.0.3 | NuGet | уязвимостей нет |
| HtmlAgilityPack | 1.12.4 | NuGet | уязвимостей нет |
| MediaInfo.Native/Wrapper | 26.1.0 | NuGet | актуальна |
| Sqlite | 3.53.3 | NuGet | актуальна |
| SharpDX | 4.2.0 | NuGet | уязвимостей нет; проект архивирован |
| NAudio / CSCore | 1.10.0 / 1.2.1.2 | NuGet | уязвимостей нет; CSCore не поддерживается |
| BASS.NET / BASS | 2.4.17 | NuGet | нативная, закрытая; проверять на сайте un4seen |
| MetadataExtractor / XmpCore | 2.9.3 / 6.1.10.1 | NuGet | уязвимостей нет |
| System.* (Buffers, Memory, ValueTuple и др.) | актуальные | NuGet | уязвимостей нет |

Проверка: GitHub Advisory Database (запрос `affects=` по каждому пакету и версии) и NVD (FFmpeg по CPE). Нативные компоненты (BASS, MediaInfo, iMON, LAV) проверены только по версии.

---

## 6. Оценка перехода на современный .NET

### 6.1 Текущее состояние
- 116 проектов старого формата, все под .NET Framework 4.7.2; около 1 млн строк C#.
- .NET Framework 4.7.2/4.8/4.8.1 — часть Windows и поддерживается вместе с ОС; срочной причины по сроку поддержки нет. Причины переходить: производительность, современный C#, библиотеки (многие перестают выпускать сборки под net4x), безопасность (`BinaryFormatter`, Remoting).
- Установщик проверяет Windows 7/8/8.1/10 (`Tools/InstallationScripts/include/MediaPortalMacros.nsh:774-787`). **.NET 8+ не поддерживает Windows 7/8.1** — переход означает отказ от них (подробно — раздел 6.6).

### 6.2 Блокеры (то, что не работает в .NET 8/10)

| Блокер | Где | Объём | Что делать |
|---|---|---|---|
| .NET Remoting | TvService, SetupTv, TvControl, PowerScheduler (сервер и клиент), WatchDogService/WatchDog | 6 точек, протокол всей системы | gRPC (рекомендуется) или CoreWCF; одновременно закрывает SEC-01–03 |
| `AppDomain.CreateDomain` | `mediaportal/MPE/MpeCore/Classes/ActionType/ConfigurePlugin.cs:77`, `mediaportal/MPE/MpeInstaller/MainForm.cs:326` | 2 | отдельный процесс или `AssemblyLoadContext` |
| `BinaryFormatter` / `SoapFormatter` | см. SEC-09 | 19 мест | SEC-09 (до миграции) |
| CS-Script (CodeDom) | грабберы, MPE, Configuration | ~15 мест | CS-Script на Roslyn или перевод грабберов в обычные плагины |
| Устаревшие элементы WinForms (`MainMenu`, `ContextMenu`, `MenuItem`, `DataGrid`, `StatusBar`, `ToolBar`) | `Tools/SkinEditor` 86, `mediaportal/WatchDog` 56, `MediaPortal.Application` 33 и др. | ~208 упоминаний (эвристика) | `MenuStrip`, `ContextMenuStrip`, `DataGridView`, `StatusStrip`, `ToolStrip` |
| `Thread.Abort`, `Thread.Suspend/Resume` | QUAL-01 | 57+ | кооперативная отмена |
| Async-делегаты (`BeginInvoke`/`EndInvoke` на делегатах) | `mediaportal/Core/Dispatcher/Job.cs:113`, `mediaportal/Core/guilib/GUIListItem.cs:696`, `TvEngine3/TVLibrary/TvControl/RemoteControl.cs:492`; `AmazonReference.cs` (удалить по SEC-13) | 3 | `Task.Run` |
| Атрибуты CAS (`SecurityPermission` и др.) | `TvEngine3/TVLibrary` 15 и др. | 17 | удалить |
| ASMX-клиент `SoapHttpClientProtocol` | `mediaportal/Core/Util/AmazonReference.cs` | 1 | удалить (SEC-13) |
| Gentle.NET (`System.Web.UI.WebControls` в `ObjectView.cs`), MySql.Data 6.x | слой БД TV Server | эпик | DEP-04 |
| Сторонние плагины под .NET Framework | экосистема | неизвестно | см. 6.4 |

Не являются блокерами: COM-интероп DirectShow (`Common-MP-TVE3/DirectShowLib`), ActiveX (`AxInterop.WMPLib`), WMI (`System.Management`), реестр, службы Windows (`System.ServiceProcess`), `System.Configuration` (пакет `System.Configuration.ConfigurationManager`), `HttpUtility`, SharpDX (netstandard2.0), x86-сборки. Managed DirectX (`Microsoft.DirectX`) встречается только в закомментированном коде `mediaportal/Core/Drawing/Rect.cs` и в файле `MediaPortal.Application/Direct3D.cs`, который не включён в проект — удалить.

### 6.3 Оценка трудозатрат (порядок величины)

| Этап | Содержание | Оценка |
|---|---|---|
| 0. Безопасность | SEC-01a/b, SEC-02–SEC-08, DEP-01–03, BUG-01–04 | 3–5 недель |
| 1. Подготовка на .NET Framework | перевод 116 проектов в SDK-style с `net48`/`net481`, `PackageReference`, центральное управление пакетами; SEC-09; QUAL-01; замена устаревших WinForms; удаление CAS, async-делегатов, мёртвого кода (Amazon, TMDB, MDX) | 2–3 месяца |
| 2. Протокол TV Server | замена Remoting на gRPC с аутентификацией; версия протокола; совместимый режим на переходный период | 2–3 месяца |
| 3. Слой данных TV Server | замена Gentle.NET + MySql.Data (Dapper/EF Core + MySqlConnector, Microsoft.Data.SqlClient) | 1–2 месяца |
| 4. Многоцелевая сборка | библиотеки `netstandard2.0` или `net48;net8.0-windows`; приложения и службы на `net8.0-windows`/`net10.0-windows` (x86 и x64) | 1–2 месяца |
| 5. Установщики и плагины | NSIS: проверка/установка рантайма .NET; совместимость плагинов; документация для авторов плагинов | 1 месяц |

Итого: **около 6–12 месяцев** для команды из 1–2 разработчиков при параллельном сопровождении текущей версии.

### 6.4 Главный внешний риск — плагины
MediaPortal держится на сторонних плагинах, скомпилированных против `Core.dll`, `Utils.dll`, `Databases.dll`, `TvControl.dll` под .NET Framework. Процесс на .NET 8 может загрузить сборку .NET Framework, только если она использует API, которые есть в .NET 8; иначе — `MissingMethodException`/`TypeLoadException` в рантайме. Нужна программа совместимости: публичный список изменённых API, сборки-заглушки для типов, которых больше нет, и период параллельных выпусков.

### 6.5 Рекомендация
1. **Сейчас** — этап 0 (безопасность). Он не зависит от миграции и закрывает критические риски.
2. **Далее** — этап 1 на .NET Framework 4.8 (4.8.1, только если Windows 7 больше не поддерживается, см. 6.6): перевод проектов в SDK-style и устранение блокеров без смены рантайма. Каждая задача даёт пользу сама по себе, и после неё перенацеливание на .NET 8+ становится механическим.
3. **Решение о самом переходе** принимать после этапа 2 (протокол TV Server) и опроса авторов ключевых плагинов. Если поддержка Windows 7/8.1 и старых плагинов важнее — оставаться на .NET Framework 4.8 (не 4.8.1, см. 6.6) со всеми исправлениями этапов 0–2 и SDK-style проектами: это закрывает риски безопасности без разрыва экосистемы.

### 6.6 Поддержка Windows 7

**Что работает на Windows 7 SP1**

| Рантайм | Windows 7 SP1 | Поддержка Microsoft |
|---|---|---|
| .NET Framework 4.7.2 (текущая цель проектов) | да | поддерживается вместе с ОС |
| **.NET Framework 4.8** | **да, последняя версия** | поддерживается вместе с ОС |
| .NET Framework 4.8.1 | **нет** (нужна Windows 10 20H2+ или Windows 11) | — |
| .NET 6 / .NET 7 | да | не поддерживаются с ноября и мая 2024 года |
| .NET 8, 9, 10 | **нет** (нужна Windows 10 1607+) | — |

Windows 7 не получает исправлений безопасности с января 2023 года (конец платных ESU). Поддержка Windows 7 — решение политики проекта, а не техническое ограничение кода.

**Следствия для плана**

1. **Целевая платформа при сохранении Windows 7 — `net48`**, а не `net481`. Этап 1 (раздел 6.3) выполнять под `net48`: SDK-style проекты, `PackageReference`, замена `BinaryFormatter`/`SoapFormatter`, `Thread.Abort`, устаревших элементов WinForms и Remoting (gRPC на .NET Framework работает через `Grpc.Net.Client` + `WinHttpHandler` только на Windows 11/Server 2022, поэтому для Windows 7 протокол TV Server придётся строить на WCF NetTcp с аутентификацией или на собственном протоколе поверх TCP/TLS; это нужно учесть при дизайне SEC-01c).
2. **Смена рантайма на .NET 8+ означает отказ от Windows 7 и 8.1.** Держать две сборки (`net48` для Windows 7 и `net8.0-windows` для остальных) технически возможно через многоцелевые проекты, но удваивает тестирование и требует, чтобы весь код и зависимости собирались под обе цели. Рекомендуется вместо этого объявить последнюю мажорную версию на `net48` последней с поддержкой Windows 7/8.1 и переходить на .NET 8+ в следующей.
3. **Зависимости.** При обновлениях (DEP-01–DEP-04) выбирать версии библиотек, у которых есть сборка под `net48`/`netstandard2.0`. Многие современные пакеты (например, новые версии Microsoft.Data.SqlClient, EF Core 6+) уже не поддерживают .NET Framework или Windows 7 — проверять перед заменой.
4. **Установщик.** Задача BUG-04: на Windows 7 SP1 ставить .NET Framework 4.8 (автономный установщик). Для установки 4.8 на Windows 7 SP1 нужны обновления KB4019990 (D3DCompiler) и действующие корневые сертификаты; установщик должен сообщать об ошибке понятным текстом, если .NET 4.8 не встал.
5. **TLS 1.2 и HTTPS** — см. задачу SEC-16. Без неё перевод загрузок на HTTPS (SEC-04, SEC-10, SEC-11) сломает эти функции на Windows 7 без обновлений.
6. **Нативные компоненты.** Актуальные сборки FFmpeg (DEP-03), Visual C++ Redistributable 2015–2022 (для фильтров `DirectShowFilters`) и драйверы тюнеров нужно проверять на Windows 7 отдельно: часть современных сборок требует Windows 10.

---

## Приложение А. Статистика сканирования

| Категория | Найдено | Комментарий |
|---|---|---|
| SQL, собранный строками | 525 | в основном медиабазы SQLite |
| Вызовы `Execute*` | 390 | |
| `Process.Start` и `ProcessStartInfo` | 182 | большинство — открытие URL/файлов справки |
| Строки `"http://..."` | 296 (62 различных адреса) | |
| `WebClient`/`HttpWebRequest` | 53 | |
| `Assembly.Load*`, CS-Script, `CreateInstance(Type.GetType)` | 51 | загрузка плагинов по дизайну |
| Блоки `unsafe`/`fixed` | 164 | интероп, графика |
| Пустой `catch` | 344 | |
| `lock(this)`/`lock(typeof)` | 236 | |
| `Thread.Abort/Suspend/Resume` | 64 | |
| `GC.Collect` | 61 | |
| `Application.DoEvents` | 71 | |
| `.Result`/`.Wait()` | 52 | |
| `BinaryFormatter`/`SoapFormatter` | 19 | |
| .NET Remoting | 38 | |
| `XmlTextReader`/`DtdProcessing.Parse`/`XPathDocument` | 40 | |
| Устаревшие элементы WinForms | 208 | эвристика, возможны ложные срабатывания на свои классы `MenuItem` |
| Атрибуты CAS | 17 | |

## Приложение Б. Строки формата с недостающими аргументами

Проверены однострочные вызовы `string.Format`, `Log.*`, `Console.WriteLine`. Вызовы с массивом `new object[] {...}` — ложные срабатывания скрипта, в список не включены.

| Файл:строка | Проблема |
|---|---|
| `TvEngine3/TVLibrary/TVLibrary/Implementations/Dri/Parser/NttParser.cs:536` | `{2}` при 2 аргументах в `throw new Exception(string.Format(...))` |
| `Common-MP-TVE3/External/Gentle.NET/Source/Gentle.Framework/Validation/RangeValidatorAttribute.cs:144` | `{0}` без аргумента |
| `TvEngine3/TVLibrary/TvService/CardManagement/CardHandler/CardTuner.cs:221` | `{0} to {1}`, передан 1 аргумент |
| `TvEngine3/TVLibrary/TvService/CardManagement/CardHandler/TimeShifterBase.cs:62` | `{2}` и `{1}` при 2 аргументах |
| `TvEngine3/TVLibrary/TvService/DiskManagement/RecordingFileHandler.cs:204` | `{1}` при 1 аргументе |
| `TvEngine3/TVLibrary/TVLibrary/Implementations/Dri/TunerDri.cs:355` | 5 мест подстановки, 4 аргумента (проверить) |
| `TvEngine3/TVLibrary/Plugins/WebEPG/WebEPG/MPCode/Xml.cs:184` | `{0} {1}` при 1 аргументе |
| `mediaportal/Databases/Music/InternetLookups/AmazonWebservice.cs:305` | `{1}` при 1 аргументе |
| `mediaportal/Databases/Pictures/SqlLite/PictureDatabaseSqlLite.cs:749` | `{1} ... {2}` при 2 аргументах |
| `mediaportal/MiniDisplayLibrary/MiniDisplayPlugin/Drivers/LCDHypeWrapper.cs:96` | `{1}` при 1 аргументе |
| `mediaportal/WindowPlugins/GUIPictures/GUIPictureExif.cs:209` | `{1}` при 1 аргументе |
| `mediaportal/WindowPlugins/GUIVideos/GUIVideoInfo.cs:892` | `{1}` при 1 аргументе |
| `mediaportal/MiniDisplayLibrary/MiniDisplayPlugin/Drivers/iMON-Legacy/iMONLCDg.cs:3077, 3116` | `{0}` без аргумента (в лог попадёт `{0}` как текст) |
| `mediaportal/ProcessPlugins/LastFMScrobbler/LastFMScrobbler.cs:390` | `{0}` без аргумента |
| `Common-MP-TVE3/PowerScheduler.Interfaces/WakeOnLanManager.cs:366` | `Console.WriteLine("System {0} ...")` без аргумента |

## Приложение В. `throw e;` / `throw ex;`

`Common-MP-TVE3/External/Gentle.NET/Source/Gentle.Common/Logging/Check.cs:144`;
`mediaportal/Core/DShowNET/Helper/AudioMixerHelper.cs:400, 426, 467, 496, 547`;
`mediaportal/Core/Player/DVDPlayer.cs:1434`;
`mediaportal/Core/ThumbDbLib/ThumbnailCreator.cs:387, 410, 427, 445, 566`;
`mediaportal/Core/Util/ThumbExtractor.cs:390, 413, 431, 507, 586`;
`mediaportal/Core/Util/UNCTools.cs:172`;
`mediaportal/Core/Util/Util.cs:3154, 3227`;
`mediaportal/ExternalPlayers/Winamp/WinampController.cs:173`;
`mediaportal/PostSetup/MP3PartInstaller.cs:377, 723`;
`mediaportal/RemotePlugins/InputMapper/HidInputMappingForm.cs:1081`;
`mediaportal/WindowPlugins/GUIRSSFeed/GUIRSSFeed.cs:633`;
`TvEngine3/TVLibrary/TVLibrary/Implementations/DVB/Graphs/DVBIP/TvCardDVBIP.cs:110`.

Перед заменой убедиться, что исключение перебрасывается из того же блока `catch`. Если перед `throw` объект исключения меняется или создаётся новый — не менять.

## Приложение Г. Порядок выполнения

1. SEC-07, SEC-05, SEC-16, SEC-04 (часть 1), BUG-01, BUG-03, BUG-02 — простые и независимые, можно параллельно. SEC-16 выпускать не позже SEC-04/SEC-10, если Windows 7 остаётся поддерживаемой.
2. SEC-06, SEC-12, SEC-08, SEC-10, SEC-15, BUG-04, DEP-03.
3. SEC-03 (клиентская часть), SEC-02, SEC-01b — требуют проверки на двух машинах.
4. DEP-01, DEP-02, SEC-09, SEC-11, SEC-13, SEC-14, BUG-06 (п. 1).
5. Эпики: SEC-01c, DEP-04, QUAL-01, миграция (раздел 6).
