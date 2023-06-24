/*****************************************************************************
* Copyright 2015-2020 Alexander Barthel alex@littlenavmap.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "atools.h"
#include "common/aircrafttrack.h"
#include "common/constants.h"
#include "common/formatter.h"
#include "common/maptypes.h"
#include "common/maptypes.h"
#include "common/proctypes.h"
#include "common/settingsmigrate.h"
#include "common/unit.h"
#include "db/databasemanager.h"
#include "exception.h"
#include "fs/sc/simconnectdata.h"
#include "fs/sc/simconnectreply.h"
#include "fs/weather/metarparser.h"
#include "geo/calculations.h"
#include "gui/application.h"
#include "gui/dockwidgethandler.h"
#include "gui/mainwindow.h"
#include "gui/mapposhistory.h"
#include "gui/translator.h"
#include "logging/logginghandler.h"
#include "logging/loggingutil.h"
#include "navapp.h"
#include "options/optionsdialog.h"
#include "routeexport/routeexportformat.h"
#include "settings/settings.h"
#include "userdata/userdataicons.h"

#include <QCommandLineParser>
#include <QDebug>
#include <QSplashScreen>
#include <QSslSocket>
#include <QStyleFactory>
#include <QSharedMemory>
#include <QMessageBox>
#include <QLibrary>
#include <QPixmapCache>
#include <QSettings>
#include <QScreen>
#include <QProcess>
#include <QStandardPaths>

#include <marble/MarbleGlobal.h>
#include <marble/MarbleDirs.h>
#include <marble/MarbleDebug.h>

using namespace Marble;
using atools::gui::Application;
using atools::logging::LoggingHandler;
using atools::logging::LoggingUtil;
using atools::settings::Settings;
using atools::gui::Translator;

int main(int argc, char *argv[])
{
  // Initialize the resources from atools static library
  Q_INIT_RESOURCE(atools);

  // Register all types to allow conversion from/to QVariant and thus reading/writing into settings
  atools::geo::registerMetaTypes();
  atools::fs::sc::registerMetaTypes();
  atools::gui::MapPosHistory::registerMetaTypes();
  atools::gui::DockWidgetHandler::registerMetaTypes();

  qRegisterMetaTypeStreamOperators<FsPathType>();
  qRegisterMetaTypeStreamOperators<SimulatorTypeMap>();

  qRegisterMetaTypeStreamOperators<map::DistanceMarker>();
  qRegisterMetaTypeStreamOperators<QList<map::DistanceMarker> >();

  qRegisterMetaTypeStreamOperators<map::PatternMarker>();
  qRegisterMetaTypeStreamOperators<QList<map::PatternMarker> >();

  qRegisterMetaTypeStreamOperators<map::HoldingMarker>();
  qRegisterMetaTypeStreamOperators<QList<map::HoldingMarker> >();

  qRegisterMetaTypeStreamOperators<map::MsaMarker>();
  qRegisterMetaTypeStreamOperators<QList<map::MsaMarker> >();

  qRegisterMetaTypeStreamOperators<map::RangeMarker>();
  qRegisterMetaTypeStreamOperators<QList<map::RangeMarker> >();

  qRegisterMetaTypeStreamOperators<at::AircraftTrackPos>();
  qRegisterMetaTypeStreamOperators<QList<at::AircraftTrackPos> >();

  qRegisterMetaTypeStreamOperators<RouteExportFormat>();
  qRegisterMetaTypeStreamOperators<RouteExportFormatMap>();

  qRegisterMetaTypeStreamOperators<map::MapAirspaceFilter>();
  qRegisterMetaTypeStreamOperators<map::MapObjectRef>();
  qRegisterMetaTypeStreamOperators<map::MapTypes>();

  // Register types and load process environment
  atools::fs::FsPaths::intitialize();

  // Tasks that have to be done before creating the application object and logging system =================
  QStringList messages;
  QSettings earlySettings(QSettings::IniFormat, QSettings::UserScope, "ABarthel", "little_navmap");
  // The loading mechanism can be configured through the QT_OPENGL environment variable and the following application attributes:
  // Qt::AA_UseDesktopOpenGL Equivalent to setting QT_OPENGL to desktop.
  // Qt::AA_UseOpenGLES Equivalent to setting QT_OPENGL to angle.
  // Qt::AA_UseSoftwareOpenGL Equivalent to setting QT_OPENGL to software.
  QString renderOpt = earlySettings.value("Options/RenderOpt", "none").toString();
  if(!renderOpt.isEmpty())
  {
    // QT_OPENGL does not work - so do this ourselves
    if(renderOpt == "desktop")
    {
      QApplication::setAttribute(Qt::AA_UseDesktopOpenGL, true);
      QApplication::setAttribute(Qt::AA_UseOpenGLES, false);
      QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL, false);
    }
    else if(renderOpt == "angle")
    {
      QApplication::setAttribute(Qt::AA_UseDesktopOpenGL, false);
      QApplication::setAttribute(Qt::AA_UseOpenGLES, true);
      QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL, false);
    }
    else if(renderOpt == "software")
    {
      QApplication::setAttribute(Qt::AA_UseDesktopOpenGL, false);
      QApplication::setAttribute(Qt::AA_UseOpenGLES, false);
      QApplication::setAttribute(Qt::AA_UseSoftwareOpenGL, true);
    }
    else if(renderOpt != "none")
      messages.append("Wrong renderer " + renderOpt);
  }
  messages.append("RenderOpt " + renderOpt);

  int checkState = earlySettings.value("OptionsDialog/Widget_checkBoxOptionsGuiHighDpi", 2).toInt();
  if(checkState == 2)
  {
    messages.append("High DPI scaling enabled");
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
  }
  else
  {
    messages.append("High DPI scaling disabled");
    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling, false);
    // QGuiApplication::setAttribute(Qt::AA_DisableHighDpiScaling, true); // Freezes with QT_SCALE_FACTOR=2 on Linux
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, false);
    QGuiApplication::setAttribute(Qt::AA_Use96Dpi, true);
  }

  // Show dialog on exception in main event queue - can be disabled for debugging purposes
  NavApp::setShowExceptionDialog(earlySettings.value("Options/ExceptionDialog", true).toBool());

  // Create application object ===========================================================
  int retval = 0;
  NavApp app(argc, argv);

  DatabaseManager *dbManager = nullptr;

#if defined(Q_OS_WIN32)
  QApplication::addLibraryPath(QApplication::applicationDirPath() + QDir::separator() + "plugins");
#endif

  try
  {
    QApplication::processEvents();

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption settingsDirOpt({"s", "settings-directory"},
                                      QObject::tr("Use <settings-directory> instead of \"%1\". This does *not* override the full path. "
                                                  "Spaces are replaced with underscores.").arg(NavApp::organizationName()),
                                      QObject::tr("settings-directory"));
    parser.addOption(settingsDirOpt);

    QCommandLineOption settingsPathOpt({"p", "settings-path"},
                                       QObject::tr("Use <settings-path> to store options and databases into the given directory. "
                                                   "<settings-path> can be relative or absolute. "
                                                     "Missing directories are created. Path can be on any drive."),
                                       QObject::tr("settings-path"));
    parser.addOption(settingsPathOpt);

    QCommandLineOption logPathOpt({"l", "log-path"},
                                  QObject::tr("Use <log-path> to store log files into the given directory. "
                                              "<log-path> can be relative or absolute. "
                                                "Missing directories are created. Path can be on any drive."),
                                  QObject::tr("settings-path"));
    parser.addOption(logPathOpt);

    QCommandLineOption cachePathOpt({"c", "cache-path"},
                                    QObject::tr("Use <cache-path> to store tiles from online maps. "
                                                "Missing directories are created. Path can be on any drive."),
                                    QObject::tr("cache-path"));
    parser.addOption(cachePathOpt);

    QCommandLineOption flightplanOpt({"f", lnm::STARTUP_FLIGHTPLAN},
                                     QObject::tr("Load the given <%1> file on startup. Can be one of the supported formats like "
                                                 "\".lnmpln\", \".pln\", \".fms\", "
                                                 "\".fgfp\", \".fpl\", \".gfp\" or others.").arg(lnm::STARTUP_FLIGHTPLAN),
                                     lnm::STARTUP_FLIGHTPLAN);
    parser.addOption(flightplanOpt);

    QCommandLineOption flightplanDescrOpt({"d", lnm::STARTUP_FLIGHTPLAN_DESCR},
                                          QObject::tr("Parse and load the given <%1> flight plan route description on startup. "
                                                      "Example \"EDDF BOMBI LIRF\".").arg(lnm::STARTUP_FLIGHTPLAN_DESCR),
                                          lnm::STARTUP_FLIGHTPLAN_DESCR);
    parser.addOption(flightplanDescrOpt);

    QCommandLineOption performanceOpt({"a", lnm::STARTUP_AIRCRAFT_PERF},
                                      QObject::tr("Load the given <%1> aircraft performance file "
                                                  "\".lnmperf\" on startup.").arg(lnm::STARTUP_AIRCRAFT_PERF),
                                      lnm::STARTUP_AIRCRAFT_PERF);
    parser.addOption(performanceOpt);

    QCommandLineOption languageOpt({"g", "language"},
                                   QObject::tr("Use language code <language> like \"de\" or \"en_US\" for the user interface. "
                                               "The code is not checked for existence or validity and "
                                               "is saved for the next startup."), "language");
    parser.addOption(languageOpt);

    // ==============================================
    // Process the actual command line arguments given by the user
    parser.process(*QCoreApplication::instance());
    QString logPath;

    // Settings directory
    if(parser.isSet(settingsDirOpt) && parser.isSet(settingsPathOpt))
      qWarning() << QObject::tr("Only one of options -s and -p can be used");

    if(parser.isSet(settingsDirOpt) && !parser.value(settingsDirOpt).isEmpty())
      Settings::setOverrideOrganisation(parser.value(settingsDirOpt));

    if(parser.isSet(settingsPathOpt) && !parser.value(settingsPathOpt).isEmpty())
      Settings::setOverridePath(parser.value(settingsPathOpt));

    if(parser.isSet(logPathOpt) && !parser.value(logPathOpt).isEmpty())
      logPath = parser.value(logPathOpt);

    // File loading
    if(parser.isSet(flightplanOpt) && parser.isSet(flightplanDescrOpt))
      qWarning() << QObject::tr("Only one of options -f and -d can be used");

    if(parser.isSet(flightplanOpt) && !parser.value(flightplanOpt).isEmpty())
      NavApp::addStartupOption(lnm::STARTUP_FLIGHTPLAN, parser.value(flightplanOpt));

    if(parser.isSet(flightplanDescrOpt) && !parser.value(flightplanDescrOpt).isEmpty())
      NavApp::addStartupOption(lnm::STARTUP_FLIGHTPLAN_DESCR, parser.value(flightplanDescrOpt));

    if(parser.isSet(performanceOpt) && !parser.value(performanceOpt).isEmpty())
      NavApp::addStartupOption(lnm::STARTUP_AIRCRAFT_PERF, parser.value(performanceOpt));

    // ==============================================
    // Initialize logging and force logfiles into the system or user temp directory
    // This will prefix all log files with orgranization and application name and append ".log"
    QString logCfg = Settings::getOverloadedPath(":/littlenavmap/resources/config/logging.cfg");
    if(logPath.isEmpty())
      LoggingHandler::initializeForTemp(logCfg);
    else
    {
      QDir().mkpath(logPath);
      LoggingHandler::initialize(logCfg, logPath);
    }

    // ==============================================
    // Start splash screen
    if(Settings::instance().valueBool(lnm::OPTIONS_DIALOG_SHOW_SPLASH, true))
      NavApp::initSplashScreen();

    // ==============================================
    // Set language from command line into options - will be saved
    if(parser.isSet(languageOpt) && !parser.value(languageOpt).isEmpty())
      Settings::instance().setValue(lnm::OPTIONS_DIALOG_LANGUAGE, parser.value(languageOpt));

    // ==============================================
    // Print some information which can be useful for debugging
    LoggingUtil::logSystemInformation();
    for(const QString& message : messages)
      qInfo() << message;

    // Log system information
    qInfo().noquote().nospace() << "atools revision " << atools::gitRevision() << " "
                                << Application::applicationName() << " revision " << GIT_REVISION_LITTLENAVMAP;

    LoggingUtil::logStandardPaths();
    Settings::logSettingsInformation();

    qInfo() << "SSL supported" << QSslSocket::supportsSsl()
            << "build library" << QSslSocket::sslLibraryBuildVersionString()
            << "library" << QSslSocket::sslLibraryVersionString();

    qInfo() << "Available styles" << QStyleFactory::keys();

    qInfo() << "SimConnectData Version" << atools::fs::sc::SimConnectData::getDataVersion()
            << "SimConnectReply Version" << atools::fs::sc::SimConnectReply::getReplyVersion();

    qInfo() << "QT_OPENGL" << QProcessEnvironment::systemEnvironment().value("QT_OPENGL");
    qInfo() << "QT_SCALE_FACTOR" << QProcessEnvironment::systemEnvironment().value("QT_SCALE_FACTOR");
    if(QApplication::testAttribute(Qt::AA_UseDesktopOpenGL))
      qInfo() << "Using Qt desktop renderer";
    if(QApplication::testAttribute(Qt::AA_UseOpenGLES))
      qInfo() << "Using Qt angle renderer";
    if(QApplication::testAttribute(Qt::AA_UseSoftwareOpenGL))
      qInfo() << "Using Qt software renderer";

    qInfo() << "UI default font" << QApplication::font();
    for(const QScreen *screen: QGuiApplication::screens())
      qInfo() << "Screen" << screen->name()
              << "size" << screen->size()
              << "physical size" << screen->physicalSize()
              << "DPI ratio" << screen->devicePixelRatio()
              << "DPI x" << screen->logicalDotsPerInchX()
              << "y" << screen->logicalDotsPerInchX();

    // Start settings and file migration
    migrate::checkAndMigrateSettings();

    Settings& settings = Settings::instance();

    qInfo() << "Settings dir name" << Settings::getDirName();

    int pixmapCache = settings.valueInt(lnm::OPTIONS_PIXMAP_CACHE, -1);
    qInfo() << "QPixmapCache cacheLimit" << QPixmapCache::cacheLimit() << "KB";
    if(pixmapCache != -1)
    {
      qInfo() << "Overriding pixmap cache" << pixmapCache << "KB";
      QPixmapCache::setCacheLimit(pixmapCache);
    }

    // Load font from options settings ========================================
    QString fontStr = settings.valueStr(lnm::OPTIONS_DIALOG_FONT, QString());
    QFont font;
    if(!fontStr.isEmpty())
    {
      font.fromString(fontStr);

      if(font != QApplication::font())
        QApplication::setFont(font);
    }
    qInfo() << "Loaded font" << font.toString() << "from options. Stored font info" << fontStr;

    // Load available translations ============================================
    qInfo() << "Loading translations for" << OptionsDialog::getLocale();
    Translator::load(OptionsDialog::getLocale());

    // Load region override ============================================
    // Forcing the English locale if the user has chosen it this way
    if(OptionsDialog::isOverrideRegion())
    {
      qInfo() << "Overriding region settings";
      QLocale::setDefault(QLocale("en"));
    }

    qDebug() << "Locale after setting to" << OptionsDialog::getLocale() << QLocale()
             << "decimal point" << QString(QLocale().decimalPoint())
             << "group separator" << QString(QLocale().groupSeparator());

    // Add paths here to allow translation =================================
    Application::addReportPath(QObject::tr("Log files:"), LoggingHandler::getLogFiles());

    Application::addReportPath(QObject::tr("Database directory:"), {Settings::getPath() + QDir::separator() + lnm::DATABASE_DIR});
    Application::addReportPath(QObject::tr("Configuration:"), {Settings::getFilename()});
    Application::setEmailAddresses({"alex@littlenavmap.org"});

    // Load help URLs from urls.cfg =================================
    lnm::loadHelpUrls();

    // Load simulator paths =================================
    atools::fs::FsPaths::loadAllPaths();
    atools::fs::FsPaths::logAllPaths();

    // Avoid static translations and load these dynamically now  =================================
    Unit::initTranslateableTexts();
    UserdataIcons::initTranslateableTexts();
    map::initTranslateableTexts();
    proc::initTranslateableTexts();
    atools::fs::weather::initTranslateableTexts();
    formatter::initTranslateableTexts();

#if defined(Q_OS_WIN32)
    // Detect other running application instance - this is unsafe on Unix since shm can remain after crashes
    QSharedMemory shared("203abd54-8a6a-4308-a654-6771efec62cd" + Settings::instance().getDirName()); // generated GUID
    if(!shared.create(512, QSharedMemory::ReadWrite))
    {
      NavApp::closeSplashScreen();
      QMessageBox::critical(nullptr, QObject::tr("%1 - Error").arg(QApplication::applicationName()),
                            QObject::tr("%1 is already running.").arg(QApplication::applicationName()));
      return 1;
    }
#endif

    // =============================================================================================
    // Set up Marble widget and print debugging information
    MarbleGlobal::Profiles profiles = MarbleGlobal::detectProfiles();
    MarbleGlobal::getInstance()->setProfiles(profiles);

    qDebug() << "Marble Local Path:" << MarbleDirs::localPath();
    qDebug() << "Marble Plugin Local Path:" << MarbleDirs::pluginLocalPath();
    qDebug() << "Marble Data Path (Run Time) :" << MarbleDirs::marbleDataPath();
    qDebug() << "Marble Plugin Path (Run Time) :" << MarbleDirs::marblePluginPath();
    qDebug() << "Marble System Path:" << MarbleDirs::systemPath();
    qDebug() << "Marble Plugin System Path:" << MarbleDirs::pluginSystemPath();

    MarbleDirs::setMarbleDataPath(QApplication::applicationDirPath() + QDir::separator() + "data");

    if(parser.isSet(cachePathOpt) && !parser.value(cachePathOpt).isEmpty())
    {
      // "/home/USER/.local/share" ("/home/USER/.local/share/marble/maps/earth/openstreetmap")
      // "C:/Users/USER/AppData/Local" ("C:\Users\USER\AppData\Local\.marble\data\maps\earth\openstreetmap")

      ///home/USER/.local/share/marble
      QFileInfo cacheFileinfo(parser.value(cachePathOpt));

      QString marbleCache;

      if(cacheFileinfo.isRelative())
        marbleCache = QApplication::applicationDirPath() + QDir::separator() + cacheFileinfo.filePath();
      else
        marbleCache = cacheFileinfo.absoluteFilePath();

      marbleCache = marbleCache + QDir::separator() + "marble";

      // Have to create full path to avoid Marble showing a migration dialog
      QString marbleCachePath = marbleCache + QDir::separator() + "maps" + QDir::separator() + "earth";
      qDebug() << Q_FUNC_INFO << "Creating" << marbleCachePath;
      QDir().mkpath(marbleCachePath);

      qDebug() << Q_FUNC_INFO << "Setting Marble cache to" << marbleCache;
      MarbleDirs::setMarbleLocalPath(marbleCache);
    }

#if defined(Q_OS_MACOS)
    QDir pluginsDir(QApplication::applicationDirPath());
    pluginsDir.cdUp();
    pluginsDir.cd("PlugIns");
    MarbleDirs::setMarblePluginPath(pluginsDir.absolutePath());
#else
    MarbleDirs::setMarblePluginPath(QApplication::applicationDirPath() + QDir::separator() + "plugins");
#endif

    MarbleDebug::setEnabled(settings.getAndStoreValue(lnm::OPTIONS_MARBLE_DEBUG, false).toBool());

    qDebug() << "New Marble Local Path:" << MarbleDirs::localPath();
    qDebug() << "New Marble Plugin Local Path:" << MarbleDirs::pluginLocalPath();
    qDebug() << "New Marble Data Path (Run Time) :" << MarbleDirs::marbleDataPath();
    qDebug() << "New Marble Plugin Path (Run Time) :" << MarbleDirs::marblePluginPath();
    qDebug() << "New Marble System Path:" << MarbleDirs::systemPath();
    qDebug() << "New Marble Plugin System Path:" << MarbleDirs::pluginSystemPath();

    // =============================================================================================
    // Disable tooltip effects since these do not work well with tooltip updates while displaying
    QApplication::setEffectEnabled(Qt::UI_FadeTooltip, false);
    QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);

#if QT_VERSION > QT_VERSION_CHECK(5, 10, 0)
    QApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);
#endif

    // =============================================================================================
    // Check if database is compatible and ask the user to erase all incompatible ones
    // If erasing databases is refused exit application
    bool databasesErased = false;
    dbManager = new DatabaseManager(nullptr);

    /* Copy from application directory to settings directory if newer and create indexes if missing */
    dbManager->checkCopyAndPrepareDatabases();

    if(dbManager->checkIncompatibleDatabases(&databasesErased))
    {
      delete dbManager;
      dbManager = nullptr;

      MainWindow mainWindow;

      // Show database dialog if something was removed
      mainWindow.setDatabaseErased(databasesErased);

      mainWindow.show();

      // Hide splash once main window is shown
      NavApp::finishSplashScreen();

      // =============================================================================================
      // Run application
      qDebug() << "Before app.exec()";
      retval = QApplication::exec();
    }

    qInfo() << "app.exec() done, retval is" << retval << (retval == 0 ? "(ok)" : "(error)");
  }
  catch(atools::Exception& e)
  {
    ATOOLS_HANDLE_EXCEPTION(e);
    // Does not return in case of fatal error
  }
  catch(...)
  {
    ATOOLS_HANDLE_UNKNOWN_EXCEPTION;
    // Does not return in case of fatal error
  }

  delete dbManager;
  dbManager = nullptr;

  qInfo() << "About to shut down logging";
  atools::logging::LoggingHandler::shutdown();

  return retval;
}
