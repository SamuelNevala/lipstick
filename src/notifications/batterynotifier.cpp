/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** Copyright (C) 2012 Jolla Ltd.
** Contact: Robin Burchell <robin.burchell@jollamobile.com>
**
** This file is part of lipstick.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/
#include <QCoreApplication>
#include <QStringList>
#include <QTimer>
#include "lowbatterynotifier.h"
#include "notificationmanager.h"
#include "batterynotifier.h"

BatteryNotifier::BatteryNotifier(QObject *parent) :
    QObject(parent),
    lowBatteryNotifier(0),
    notificationId(0),
    touchScreenLockActive(false),
    batteryInfo(new QBatteryInfo(this)),
    qmDeviceMode(new MeeGo::QmDeviceMode(this)),
    qmLed(new MeeGo::QmLED(this)),
    chargerType(QBatteryInfo::UnknownCharger)
{
#ifndef UNIT_TEST
    connect(batteryInfo, SIGNAL(batteryStatusChanged(int,QBatteryInfo::BatteryStatus)), this, SLOT(applyBatteryStatus(int,QBatteryInfo::BatteryStatus)));
    connect(batteryInfo, SIGNAL(chargingStateChanged(int,QBatteryInfo::ChargingState)), this, SLOT(applyChargingState(int,QBatteryInfo::ChargingState)));
    connect(batteryInfo, SIGNAL(chargerTypeChanged(QBatteryInfo::ChargerType)), this, SLOT(applyChargerType(QBatteryInfo::ChargerType)));
#endif
    connect(qmDeviceMode, SIGNAL(devicePSMStateChanged(MeeGo::QmDeviceMode::PSMState)), this, SLOT(applyPSMState(MeeGo::QmDeviceMode::PSMState)));

    notificationTimer.setInterval(5000);
    notificationTimer.setSingleShot(true);

    QTimer::singleShot(0, this, SLOT(initBattery()));
}

BatteryNotifier::~BatteryNotifier()
{
}

void BatteryNotifier::initBattery()
{
    applyChargingState(0, batteryInfo->chargingState(1));
    applyBatteryStatus(0, batteryInfo->batteryStatus(1));
}

void BatteryNotifier::lowBatteryAlert()
{
    sendNotification(NotificationLowBattery);
}

void BatteryNotifier::applyChargingState(int, QBatteryInfo::ChargingState state)
{
    switch(state) {
    case QBatteryInfo::Charging:
        if (batteryInfo->chargerType() == QBatteryInfo::USBCharger && batteryInfo->currentFlow(1) <= 100) {
            sendNotification(NotificationNoEnoughPower);
        } else {
            // The low battery notifications should not be sent when the battery is charging
            stopLowBatteryNotifier();

            removeNotification(QStringList() << "x-nemo.battery.removecharger" << "x-nemo.battery.chargingcomplete" << "x-nemo.battery.lowbattery");
            sendNotification(NotificationCharging);
        }
        break;

    case QBatteryInfo::NotCharging:
        sendNotification(NotificationChargingNotStarted);
        break;

    default:
        removeNotification(QStringList() << "x-nemo.battery");
        utiliseLED(false, QString("PatternBatteryCharging"));
        break;

    }
}

void BatteryNotifier::applyBatteryStatus(int, QBatteryInfo::BatteryStatus status)
{
    switch(status) {
    case QBatteryInfo::BatteryFull:
        stopLowBatteryNotifier();
        removeNotification(QStringList() << "x-nemo.battery");
        sendNotification(NotificationChargingComplete);
        break;

    case QBatteryInfo::BatteryOk:
        stopLowBatteryNotifier();
        break;

    case QBatteryInfo::BatteryLow:
        if (batteryInfo->chargingState(1) != QBatteryInfo::Charging) {
            // The low battery notifications should be sent only if the battery is not charging
            startLowBatteryNotifier();
        }
        break;

    case QBatteryInfo::BatteryEmpty:
        sendNotification(NotificationRechargeBattery);
        break;

    default:
        break;
    }
}

void BatteryNotifier::applyChargerType(QBatteryInfo::ChargerType type)
{
    switch(type) {
    case QBatteryInfo::UnknownCharger:
        /*
         * After the user plugs out the charger from the device, this system
         * banner is displayed to remind the users to unplug charger from
         * the power supply for conserving energy.  Remove charger
         * notification should not be shown in case if USB cable is used for
         * charging the device.
         */
        if (chargerType == QBatteryInfo::WallCharger) {
            removeNotification(QStringList() << "x-nemo.battery" << "x-nemo.battery.chargingcomplete");
            sendNotification(NotificationRemoveCharger);
        }

        if (chargerType != QBatteryInfo::UnknownCharger && chargerType != QBatteryInfo::USBCharger && batteryInfo->batteryStatus(1) == QBatteryInfo::BatteryLow && batteryInfo->chargingState(1) != QBatteryInfo::Charging) {
            // A charger was connected but is no longer connected and the battery is low, so start low battery notifier
            startLowBatteryNotifier();
        }
        break;

    default:
        break;
    }

    chargerType = type;
}

void BatteryNotifier::applyPSMState(MeeGo::QmDeviceMode::PSMState psmState)
{
    if (psmState == MeeGo::QmDeviceMode::PSMStateOff) {
        sendNotification(NotificationExitingPSM);
    } else if (psmState == MeeGo::QmDeviceMode::PSMStateOn) {
        sendNotification(NotificationEnteringPSM);
    }
}

void BatteryNotifier::utiliseLED(bool activate, const QString &pattern)
{
    if (activate) {
        qmLed->activate(pattern);
    } else {
        qmLed->deactivate(pattern);
    }
}

void BatteryNotifier::sendNotification(BatteryNotifier::NotificationID id)
{
    switch(id) {
    case NotificationCharging:
        utiliseLED(true, QString("PatternBatteryCharging"));
        sendNotification("x-nemo.battery",
                //% "Charging"
                qtTrId("qtn_ener_charging"), chargingImageId());
        break;

    case NotificationChargingComplete:
        utiliseLED(true, QString("PatternBatteryFull"));
        sendNotification("x-nemo.battery.chargingcomplete",
                //% "Charging complete"
                qtTrId("qtn_ener_charcomp"));
        break;

    case NotificationRemoveCharger:
        sendNotification("x-nemo.battery.removecharger",
                //% "Disconnect charger from power supply to save energy"
                qtTrId("qtn_ener_remcha"));
        break;

    case NotificationChargingNotStarted:
        utiliseLED(false, QString("PatternBatteryCharging"));
        sendNotification("x-nemo.battery.chargingnotstarted",
                //% "Charging not started. Replace charger."
                qtTrId("qtn_ener_repcharger"));
        break;

    case NotificationRechargeBattery:
        sendNotification("x-nemo.battery.recharge",
                //% "Recharge battery"
                qtTrId("qtn_ener_rebatt"));
        break;

    case NotificationEnteringPSM:
        sendNotification("x-nemo.battery.enterpsm",
                //% "Entering power save mode"
                qtTrId("qtn_ener_ent_psnote"), chargingImageId());
        break;

    case NotificationExitingPSM:
        sendNotification("x-nemo.battery.exitpsm",
                //% "Exiting power save mode"
                qtTrId("qtn_ener_exit_psnote"), chargingImageId());
        break;

    case NotificationLowBattery:
        sendNotification("x-nemo.battery.lowbattery",
                //% "Low battery"
                qtTrId("qtn_ener_lowbatt"));
        break;

    case NotificationNoEnoughPower:
        sendNotification("x-nemo.battery.notenoughpower",
                //% "Not enough power to charge"
                qtTrId("qtn_ener_nopowcharge"), "icon-m-energy-management-insufficient-power");
        break;
    }
}

void BatteryNotifier::sendNotification(const QString &category, const QString &text, const QString &icon)
{
    NotificationManager *manager = NotificationManager::instance();
    QVariantHash hints;
    hints.insert(NotificationManager::HINT_CATEGORY, category);
    hints.insert(NotificationManager::HINT_PREVIEW_BODY, text);
    notificationId = manager->Notify(qApp->applicationName(), 0, icon, QString(), QString(), QStringList(), hints, -1);
    notificationCategory = category;
    notificationTimer.start();
}

void BatteryNotifier::removeNotification(const QStringList &categories)
{
    NotificationManager *manager = NotificationManager::instance();
    if (notificationId != 0 && categories.contains(notificationCategory) && notificationTimer.isActive()) {
        manager->CloseNotification(notificationId);
        notificationId = 0;
        notificationCategory.clear();
        notificationTimer.stop();
    }
}

QString BatteryNotifier::chargingImageId()
{
    int maximumCapacity = batteryInfo->maximumCapacity(1);
    int percentage = batteryInfo->remainingCapacity(1) * 100 / (maximumCapacity != 0 ? maximumCapacity : 1);

    if (percentage >= 84) {
        return QString("icon-m-energy-management-charging8");
    } else if (percentage >= 73) {
        return QString("icon-m-energy-management-charging7");
    } else if (percentage >= 62) {
        return QString("icon-m-energy-management-charging6");
    } else if (percentage >= 51) {
        return QString("icon-m-energy-management-charging5");
    } else if (percentage >= 39) {
        return QString("icon-m-energy-management-charging4");
    } else if (percentage >= 28) {
        return QString("icon-m-energy-management-charging3");
    } else if (percentage >= 17) {
        return QString("icon-m-energy-management-charging2");
    } else if (percentage >= 5) {
        return QString("icon-m-energy-management-charging1");
    }
    return QString("icon-m-energy-management-charging-low");
}

void BatteryNotifier::setTouchScreenLockActive(bool active)
{
    touchScreenLockActive = active;
    if (lowBatteryNotifier != NULL) {
        lowBatteryNotifier->setTouchScreenLockActive(active);
    }
}

void BatteryNotifier::startLowBatteryNotifier()
{
    if (lowBatteryNotifier == NULL) {
        lowBatteryNotifier = new LowBatteryNotifier();
        connect(lowBatteryNotifier, SIGNAL(lowBatteryAlert()), this, SLOT(lowBatteryAlert()));
    }

    lowBatteryNotifier->setTouchScreenLockActive(touchScreenLockActive);
    lowBatteryNotifier->sendLowBatteryAlert();
}

void BatteryNotifier::stopLowBatteryNotifier()
{
    if (lowBatteryNotifier != NULL) {
        delete lowBatteryNotifier;
        lowBatteryNotifier = NULL;
    }
}
