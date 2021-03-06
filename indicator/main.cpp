/*
 * SPDX-FileCopyrightText: 2016 Aleix Pol Gonzalez <aleixpol@kde.org>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#include <QApplication>
#include <QProcess>
#include <QThread>

#ifdef QSYSTRAY
#include <QSystemTrayIcon>
#else
#include <KStatusNotifierItem>
#endif

#include <KDBusService>
#include <KAboutData>
#include <KCMultiDialog>
#include <KLocalizedString>

#include "interfaces/devicesmodel.h"
#include "interfaces/dbusinterfaces.h"
#include "kdeconnect-version.h"
#include "deviceindicator.h"

#include <dbushelper.h>

#include "indicatorhelper.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    KAboutData about(QStringLiteral("kdeconnect-indicator"),
                     i18n("KDE Connect Indicator"),
                     QStringLiteral(KDECONNECT_VERSION_STRING),
                     i18n("KDE Connect Indicator tool"),
                     KAboutLicense::GPL,
                     i18n("(C) 2016 Aleix Pol Gonzalez"));
    KAboutData::setApplicationData(about);

    IndicatorHelper helper;

    helper.preInit();

    // Run Daemon initialization step
    QProcess kdeconnectd;
    if (helper.daemonHook(kdeconnectd)) {
        return -1;
    }

    KDBusService dbusService(KDBusService::Unique);

    DevicesModel model;
    model.setDisplayFilter(DevicesModel::Reachable | DevicesModel::Paired);
    QMenu* menu = new QMenu;

    DaemonDbusInterface iface;
    auto refreshMenu = [&iface, &model, &menu, &helper]() {
        menu->clear();
        auto configure = menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Configure..."));
        QObject::connect(configure, &QAction::triggered, configure, [](){
            KCMultiDialog* dialog = new KCMultiDialog;
            dialog->addModule(QStringLiteral("kcm_kdeconnect"));
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->show();
        });
        for (int i=0, count = model.rowCount(); i<count; ++i) {
            DeviceDbusInterface* device = model.getDevice(i);
            auto indicator = new DeviceIndicator(device);
            QObject::connect(device, &DeviceDbusInterface::destroyed, indicator, &QObject::deleteLater);

            menu->addMenu(indicator);
        }
        const QStringList requests = iface.pairingRequests();
        if (!requests.isEmpty()) {
            menu->addSection(i18n("Pairing requests"));

            for(const auto& req: requests) {
                DeviceDbusInterface* dev = new DeviceDbusInterface(req, menu);
                auto pairMenu = menu->addMenu(dev->name());
                pairMenu->addAction(i18n("Pair"), dev, &DeviceDbusInterface::acceptPairing);
                pairMenu->addAction(i18n("Reject"), dev, &DeviceDbusInterface::rejectPairing);
            }
        }
        // Add quit menu
#if defined Q_OS_MAC

        menu->addAction(i18n("Quit"), [](){
            auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kdeconnect.daemon"),
                                                        QStringLiteral("/MainApplication"),
                                                        QStringLiteral("org.qtproject.Qt.QCoreApplication"),
                                                        QStringLiteral("quit"));
            DBusHelper::sessionBus().call(message, QDBus::NoBlock); // Close our daemon
            message = QDBusMessage::createMethodCall(qApp->applicationName(),
                                                        QStringLiteral("/MainApplication"),
                                                        QStringLiteral("org.qtproject.Qt.QCoreApplication"),
                                                        QStringLiteral("quit"));
            DBusHelper::sessionBus().call(message, QDBus::NoBlock); // Close our indicator
        });
#elif defined Q_OS_WIN

        menu->addAction(i18n("Quit"), [&helper](){
            const QUrl indicatorUrl = QUrl::fromLocalFile(qApp->applicationDirPath());
            helper.terminateProcess(processes::dbus_daemon, indicatorUrl);
            helper.terminateProcess(processes::kdeconnect_daemon, indicatorUrl);
            qApp->quit();
        });
#endif
    };

    QObject::connect(&iface, &DaemonDbusInterface::pairingRequestsChangedProxy, &model, refreshMenu);
    QObject::connect(&model, &DevicesModel::rowsInserted, &model, refreshMenu);
    QObject::connect(&model, &DevicesModel::rowsRemoved, &model, refreshMenu);

    // Run icon to add icon path (if necessary)
    helper.iconPathHook();

#ifdef QSYSTRAY
    QSystemTrayIcon systray;
    helper.systrayIconHook(systray);
    systray.setVisible(true);
    systray.setToolTip(QStringLiteral("KDE Connect"));
    QObject::connect(&model, &DevicesModel::rowsChanged, &model, [&systray, &model]() {
        systray.setToolTip(i18np("%1 device connected", "%1 devices connected", model.rowCount()));
    });

    systray.setContextMenu(menu);
#else
    KStatusNotifierItem systray;
    helper.systrayIconHook(systray);
    systray.setToolTip(QStringLiteral("kdeconnect"), QStringLiteral("KDE Connect"), QStringLiteral("KDE Connect"));
    systray.setCategory(KStatusNotifierItem::Communications);
    systray.setStatus(KStatusNotifierItem::Passive);
    systray.setStandardActionsEnabled(false);
    QObject::connect(&model, &DevicesModel::rowsChanged, &model, [&systray, &model]() {
        const auto count = model.rowCount();
        systray.setStatus(count == 0 ? KStatusNotifierItem::Passive : KStatusNotifierItem::Active);
        systray.setToolTip(QStringLiteral("kdeconnect"), QStringLiteral("KDE Connect"), i18np("%1 device connected", "%1 devices connected", count));
    });

    systray.setContextMenu(menu);
#endif

    refreshMenu();

    app.setQuitOnLastWindowClosed(false);

    // Finish init
    helper.postInit();

    return app.exec();
}
