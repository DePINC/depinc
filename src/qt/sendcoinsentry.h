// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_SENDCOINSENTRY_H
#define BITCOIN_QT_SENDCOINSENTRY_H

#include <qt/walletmodel.h>

#include <QStackedWidget>

#include <QStringList>
#include <QStringListModel>

#include "pointitemmodel.h"

class WalletModel;
class PlatformStyle;

class QMenu;

namespace Ui {
    class SendCoinsEntry;
}

/**
 * A single entry in the dialog for sending bitcoins.
 * Stacked widget, with different UIs for payment requests
 * with a strong payee identity.
 */
class SendCoinsEntry : public QStackedWidget
{
    Q_OBJECT

public:
    explicit SendCoinsEntry(PayOperateMethod payOperateMethod, const PlatformStyle *platformStyle, int chainHeight, QWidget *parent = nullptr);
    ~SendCoinsEntry();

    void setModel(WalletModel *model);
    bool validate(interfaces::Node& node);
    SendCoinsRecipient getValue();

    /** Return whether the entry is still empty and unedited */
    bool isClear();

    void setValue(const SendCoinsRecipient &value);
    void setAddress(const QString &address);
    void setAmount(const CAmount &amount);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases
     *  (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setFocus();

public Q_SLOTS:
    void clear();
    void checkSubtractFeeFromAmount();

Q_SIGNALS:
    void removeEntry(SendCoinsEntry *entry);
    void useAvailableBalance(SendCoinsEntry* entry);
    void payAmountChanged();
    void subtractFeeFromAmountChanged();

private Q_SLOTS:
    void deleteClicked();
    void useAvailableBalanceClicked();
    void on_payTo_textChanged(const QString &address);
    void on_addressBookButton_clicked();
    void on_pasteButton_clicked();
    void on_refreshPointsButton_clicked();
    void updateDisplayUnit();

    void customRetargetContextMenu(QPoint const&);
    void copyAddrActionTriggered();
    void copyTxHashActionTriggered();

private:
    const PayOperateMethod payOperateMethod;
    SendCoinsRecipient recipient;
    Ui::SendCoinsEntry *ui;
    WalletModel *model;
    const PlatformStyle *platformStyle;
    PointItemModel *pointsListModel;
    QMenu *retargetContextMenu;

    bool updateLabel(const QString &address);
};

#endif // BITCOIN_QT_SENDCOINSENTRY_H
