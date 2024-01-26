// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/sendcoinsentry.h>
#include <qt/forms/ui_sendcoinsentry.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>

#include <chainparams.h>
#include <wallet/wallet.h>

#include <QApplication>
#include <QClipboard>
#include <QMenu>

#include <QStringListModel>

#include <array>

#include <key_io.h>

#include "sendcoinsentry.h"

static int const hour_blocks = 3600 / 180;
static const std::array<int, 5> bindActiveHeights = { {1*hour_blocks, 1*24*hour_blocks, 2*24*hour_blocks, 3*24*hour_blocks, PROTOCOL_BINDPLOTTER_MAXALIVE} };
int getPlotterDataValidHeightForIndex(int index) {
    if (index+1 > static_cast<int>(bindActiveHeights.size())) {
        return bindActiveHeights.back();
    }
    if (index < 0) {
        return bindActiveHeights[0];
    }
    return bindActiveHeights[index];
}
int getIndexForPlotterDataValidHeight(int height) {
    for (unsigned int i = 0; i < bindActiveHeights.size(); i++) {
        if (bindActiveHeights[i] >= height) {
            return i;
        }
    }
    return bindActiveHeights.size() - 1;
}

SendCoinsEntry::SendCoinsEntry(PayOperateMethod payOperateMethod, PlatformStyle const* _platformStyle, int chainHeight, QWidget* parent)
        : QStackedWidget(parent),
          payOperateMethod(payOperateMethod),
          ui(new Ui::SendCoinsEntry),
          model(nullptr),
          platformStyle(_platformStyle) {
    ui->setupUi(this);

    ui->plotterPassphraseLabel->setVisible(false);
    ui->plotterPassphrase->setVisible(false);
    ui->plotterDataAliveHeightLabel->setVisible(false);
    ui->plotterDataValidHeightSelector->setVisible(false);

    ui->pointsLabel->setVisible(false);

    ui->pointsList->setVisible(false);
    ui->refreshPointsButton->setVisible(false);

    pointsListModel = new PointItemModel(GetWallets()[0].get(), chainHeight, Params().GetConsensus());
    ui->pointsList->setModel(pointsListModel);
    ui->pointsList->header()->setVisible(true);
    ui->pointsList->header()->setStretchLastSection(true);

    QAction *copyAddrAction = new QAction(tr("Copy address"));
    connect(copyAddrAction, &QAction::triggered, this, std::bind(&SendCoinsEntry::copyAddrActionTriggered, this));

    QAction *copyTxHashAction = new QAction(tr("Copy tx hash"));
    connect(copyTxHashAction, &QAction::triggered, copyTxHashAction, std::bind(&SendCoinsEntry::copyTxHashActionTriggered, this));

    retargetContextMenu = new QMenu(tr("Retarget context menu"), this);
    retargetContextMenu->addAction(copyAddrAction);
    retargetContextMenu->addAction(copyTxHashAction);
    ui->pointsList->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);
    connect(ui->pointsList, SIGNAL(customContextMenuRequested(QPoint const&)), this, SLOT(customRetargetContextMenu(QPoint const&)));

    ui->addressBookButton->setIcon(platformStyle->SingleColorIcon(":/icons/address-book"));
    ui->pasteButton->setIcon(platformStyle->SingleColorIcon(":/icons/editpaste"));
    ui->deleteButton->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));
    ui->deleteButton_is->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));
    ui->deleteButton_s->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));
    ui->payAmount->setContentsMargins(0, 0, 6, 0);

    setCurrentWidget(ui->SendCoins);

    if (platformStyle->getUseExtraSpacing())
        ui->payToLayout->setSpacing(4);
    ui->addAsLabel->setPlaceholderText(tr("Enter a label for this address to add it to your address book"));

    // normal bitcoin address field
    GUIUtil::setupAddressWidget(ui->payTo, this);
    // just a label for displaying bitcoin address(es)
    ui->payTo_is->setFont(GUIUtil::fixedPitchFont());

    // Connect signals
    connect(ui->payAmount, &BitcoinAmountField::valueChanged, this, &SendCoinsEntry::payAmountChanged);
    connect(ui->checkboxSubtractFeeFromAmount, &QCheckBox::toggled, this, &SendCoinsEntry::subtractFeeFromAmountChanged);
    connect(ui->deleteButton, &QPushButton::clicked, this, &SendCoinsEntry::deleteClicked);
    connect(ui->deleteButton_is, &QPushButton::clicked, this, &SendCoinsEntry::deleteClicked);
    connect(ui->deleteButton_s, &QPushButton::clicked, this, &SendCoinsEntry::deleteClicked);
    connect(ui->useAvailableBalanceButton, &QPushButton::clicked, this, &SendCoinsEntry::useAvailableBalanceClicked);
    connect(ui->refreshPointsButton, &QPushButton::clicked, this, &SendCoinsEntry::on_refreshPointsButton_clicked);

    // Pay method
    if (payOperateMethod == PayOperateMethod::Point || payOperateMethod == PayOperateMethod::ChiaPoint || payOperateMethod == PayOperateMethod::ChiaPoint || payOperateMethod == PayOperateMethod::ChiaPointT1 || payOperateMethod == PayOperateMethod::ChiaPointT2 || payOperateMethod == PayOperateMethod::ChiaPointT3) {
        ui->payToLabel->setText(tr("Point &To:"));
    } else if (payOperateMethod == PayOperateMethod::BindPlotter || payOperateMethod == PayOperateMethod::ChiaBindFarmerPk) {
        ui->payToLabel->setText(tr("Bind &To:"));
        ui->labellLabel->setVisible(false);
        ui->addAsLabel->setVisible(false);
        ui->amountLabel->setVisible(false);
        ui->payAmount->setVisible(false);
        ui->checkboxSubtractFeeFromAmount->setVisible(false);
        ui->useAvailableBalanceButton->setVisible(false);
        ui->plotterPassphraseLabel->setVisible(true);
        ui->plotterPassphrase->setVisible(true);
        if (payOperateMethod == PayOperateMethod::BindPlotter) {
            ui->plotterPassphraseLabel->setText(tr("Plotter:"));
            ui->plotterPassphrase->setPlaceholderText(tr("Enter your plotter passphrase or bind hex data"));
        } else {
            ui->plotterPassphraseLabel->setText(tr("Farmer:"));
            ui->plotterPassphrase->setPlaceholderText(tr("Enter your farmer passphrase"));
        }
        ui->plotterDataAliveHeightLabel->setVisible(true);
        ui->plotterDataValidHeightSelector->setVisible(true);
        for (int const n : bindActiveHeights) {
            assert(n > 0 && n <= PROTOCOL_BINDPLOTTER_MAXALIVE);
            ui->plotterDataValidHeightSelector->addItem(tr("%1 (%2 blocks)")
                .arg(GUIUtil::formatNiceTimeOffset(n*Params().GetConsensus().nPowTargetSpacing))
                .arg(n));
        }
        ui->plotterDataValidHeightSelector->setCurrentIndex(getIndexForPlotterDataValidHeight(PROTOCOL_BINDPLOTTER_DEFAULTMAXALIVE));
    } else if (payOperateMethod == PayOperateMethod::ChiaPointRetarget) {
        ui->pointsLabel->setVisible(true);
        ui->pointsList->setVisible(true);
        ui->refreshPointsButton->setVisible(true);
        ui->amountLabel->setVisible(false);
        ui->checkboxSubtractFeeFromAmount->setVisible(false);
        ui->payAmount->setVisible(false);
        ui->useAvailableBalanceButton->setVisible(false);
    }
}

SendCoinsEntry::~SendCoinsEntry()
{
    delete ui;
}

void SendCoinsEntry::on_pasteButton_clicked()
{
    // Paste text from clipboard into recipient field
    ui->payTo->setText(QApplication::clipboard()->text());
}

void SendCoinsEntry::on_refreshPointsButton_clicked() {
    pointsListModel->reload();
}

void SendCoinsEntry::on_addressBookButton_clicked() {
    if(!model)
        return;
    AddressBookPage::Tabs tab = (payOperateMethod == PayOperateMethod::BindPlotter ? AddressBookPage::ReceivingTab : AddressBookPage::SendingTab);
    AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, tab, this);
    dlg.setModel(model->getAddressTableModel());
    if(dlg.exec())
    {
        ui->payTo->setText(dlg.getReturnValue());
        ui->payAmount->setFocus();
    }
}

void SendCoinsEntry::on_payTo_textChanged(QString const& address) { updateLabel(address); }

void SendCoinsEntry::setModel(WalletModel *_model)
{
    this->model = _model;

    if (_model && _model->getOptionsModel())
        connect(_model->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &SendCoinsEntry::updateDisplayUnit);

    clear();
}

void SendCoinsEntry::clear()
{
    // clear UI elements for normal payment
    ui->payTo->clear();
    ui->addAsLabel->clear();
    ui->plotterPassphrase->clear();
    ui->payAmount->clear();
    ui->checkboxSubtractFeeFromAmount->setCheckState(Qt::Unchecked);
    ui->messageTextLabel->clear();
    ui->messageTextLabel->hide();
    ui->messageLabel->hide();
    // clear UI elements for unauthenticated payment request
    ui->payTo_is->clear();
    ui->memoTextLabel_is->clear();
    ui->payAmount_is->clear();
    // clear UI elements for authenticated payment request
    ui->payTo_s->clear();
    ui->memoTextLabel_s->clear();
    ui->payAmount_s->clear();

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();

    // Update for bind plotter
    if (payOperateMethod == PayOperateMethod::BindPlotter ||
        payOperateMethod == PayOperateMethod::ChiaBindFarmerPk ||
        payOperateMethod == PayOperateMethod::ChiaPointRetarget) {
        ui->payAmount->setValue(PROTOCOL_BINDPLOTTER_LOCKAMOUNT);
        ui->checkboxSubtractFeeFromAmount->setCheckState(Qt::Unchecked);
    }
}

void SendCoinsEntry::checkSubtractFeeFromAmount()
{
    ui->checkboxSubtractFeeFromAmount->setChecked(true);
}

void SendCoinsEntry::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

void SendCoinsEntry::useAvailableBalanceClicked()
{
    Q_EMIT useAvailableBalance(this);
}

bool SendCoinsEntry::validate(interfaces::Node& node)
{
    if (!model)
        return false;

    // Check input validity
    bool retval = true;

#ifdef ENABLE_BIP70
    // Skip checks for payment request
    if (recipient.paymentRequest.IsInitialized())
        return retval;
#endif

    if (!model->validateAddress(ui->payTo->text()))
    {
        ui->payTo->setValid(false);
        retval = false;
    }

    if (!ui->payAmount->validate())
    {
        retval = false;
    }

    // Sending a zero amount is invalid
    if (ui->payAmount->value(nullptr) <= 0)
    {
        ui->payAmount->setValid(false);
        retval = false;
    }

    // Reject dust outputs:
    if (retval && GUIUtil::isDust(node, ui->payTo->text(), ui->payAmount->value())) {
        ui->payAmount->setValid(false);
        retval = false;
    }

    // Special tx amount
    if (payOperateMethod == PayOperateMethod::Point ||
        payOperateMethod == PayOperateMethod::ChiaPoint ||
        payOperateMethod == PayOperateMethod::ChiaPointT1 ||
        payOperateMethod == PayOperateMethod::ChiaPointT2 ||
        payOperateMethod == PayOperateMethod::ChiaPointT3)
    {
        if (ui->payAmount->value() < PROTOCOL_POINT_AMOUNT_MIN ||
                (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked && ui->payAmount->value() <= PROTOCOL_POINT_AMOUNT_MIN)) {
            ui->payAmount->setValid(false);
            retval = false;
        }
    }
    else if (payOperateMethod == PayOperateMethod::BindPlotter || payOperateMethod == PayOperateMethod::ChiaBindFarmerPk)
    {
        QString passphrase = ui->plotterPassphrase->text().trimmed();
        if (!IsValidPassphrase(passphrase.toStdString())) {
            ui->plotterPassphrase->setValid(false);
            retval = false;
        }
    }

    return retval;
}

SendCoinsRecipient SendCoinsEntry::getValue()
{
#ifdef ENABLE_BIP70
    // Payment request
    if (recipient.paymentRequest.IsInitialized())
        return recipient;
#endif

    // Normal payment
    recipient.address = ui->payTo->text();
    recipient.label = ui->addAsLabel->text();
    if (payOperateMethod == PayOperateMethod::BindPlotter || payOperateMethod == PayOperateMethod::ChiaBindFarmerPk) {
        recipient.plotterPassphrase = ui->plotterPassphrase->text().trimmed();
        recipient.plotterDataAliveHeight = getPlotterDataValidHeightForIndex(ui->plotterDataValidHeightSelector->currentIndex());
    } else if (payOperateMethod == PayOperateMethod::ChiaPointRetarget) {
        if (ui->pointsList->selectionModel()->hasSelection()) {
            auto curr = ui->pointsList->selectionModel()->currentIndex();
            assert(curr.isValid());
            TxPledge pledge = pointsListModel->pledgeFromIndex(curr);
            recipient.retargetTxid = pledge.txid;
            if (pledge.payloadType == DATACARRIER_TYPE_CHIA_POINT_RETARGET) {
                recipient.pointType = pledge.pointType;
                recipient.pointHeight = pledge.nPointHeight;
            } else {
                recipient.pointType = pledge.payloadType;
                recipient.pointHeight = pledge.nBlockHeight;
            }
        } else {
            recipient.pointType = DATACARRIER_TYPE_UNKNOWN;
            recipient.pointHeight = 0;
        }
        QModelIndex curr = ui->pointsList->selectionModel()->currentIndex();
    }
    recipient.amount = ui->payAmount->value();
    recipient.message = ui->messageTextLabel->text();
    recipient.fSubtractFeeFromAmount = (ui->checkboxSubtractFeeFromAmount->checkState() == Qt::Checked);

    return recipient;
}

QWidget *SendCoinsEntry::setupTabChain(QWidget *prev)
{
    QWidget::setTabOrder(prev, ui->payTo);
    QWidget::setTabOrder(ui->payTo, ui->addAsLabel);
    QWidget::setTabOrder(ui->addAsLabel, ui->plotterPassphrase);
    QWidget *w = ui->payAmount->setupTabChain(ui->plotterPassphrase);
    QWidget::setTabOrder(w, ui->checkboxSubtractFeeFromAmount);
    QWidget::setTabOrder(ui->checkboxSubtractFeeFromAmount, ui->addressBookButton);
    QWidget::setTabOrder(ui->addressBookButton, ui->pasteButton);
    QWidget::setTabOrder(ui->pasteButton, ui->deleteButton);
    return ui->deleteButton;
}

void SendCoinsEntry::setValue(SendCoinsRecipient const& value) {
    recipient = value;

#ifdef ENABLE_BIP70
    if (recipient.paymentRequest.IsInitialized()) // payment request
    {
        if (recipient.authenticatedMerchant.isEmpty()) // unauthenticated
        {
            ui->payTo_is->setText(recipient.address);
            ui->memoTextLabel_is->setText(recipient.message);
            ui->payAmount_is->setValue(recipient.amount);
            ui->payAmount_is->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_UnauthenticatedPaymentRequest);
        }
        else // authenticated
        {
            ui->payTo_s->setText(recipient.authenticatedMerchant);
            ui->memoTextLabel_s->setText(recipient.message);
            ui->payAmount_s->setValue(recipient.amount);
            ui->payAmount_s->setReadOnly(true);
            setCurrentWidget(ui->SendCoins_AuthenticatedPaymentRequest);
        }
    }
    else // normal payment
#endif
    {
        // message
        ui->messageTextLabel->setText(recipient.message);
        ui->messageTextLabel->setVisible(!recipient.message.isEmpty());
        ui->messageLabel->setVisible(!recipient.message.isEmpty());

        ui->addAsLabel->clear();
        ui->plotterPassphrase->clear();
        ui->payTo->setText(recipient.address); // this may set a label from addressbook
        if (!recipient.label.isEmpty()) // if a label had been set from the addressbook, don't overwrite with an empty label
            ui->addAsLabel->setText(recipient.label);
        if (!recipient.plotterPassphrase.isEmpty())
            ui->plotterPassphrase->setText(recipient.plotterPassphrase);
        ui->payAmount->setValue(recipient.amount);
    }
}

void SendCoinsEntry::setAddress(QString const& address) {
    ui->payTo->setText(address);
    ui->payAmount->setFocus();
}

void SendCoinsEntry::setAmount(CAmount const& amount) { ui->payAmount->setValue(amount); }

bool SendCoinsEntry::isClear()
{
    return ui->payTo->text().isEmpty() && ui->payTo_is->text().isEmpty() && ui->payTo_s->text().isEmpty();
}

void SendCoinsEntry::setFocus()
{
    ui->payTo->setFocus();
}

void SendCoinsEntry::updateDisplayUnit()
{
    if(model && model->getOptionsModel())
    {
        // Update payAmount with the current unit
        ui->payAmount->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_is->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
        ui->payAmount_s->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
    }
}

void SendCoinsEntry::customRetargetContextMenu(QPoint const& pt)
{
    retargetContextMenu->popup(ui->pointsList->mapToGlobal(pt));
}

void SendCoinsEntry::copyAddrActionTriggered()
{
    auto indexes = ui->pointsList->selectionModel()->selectedIndexes();
    if (indexes.size() > 0) {
        // only get the first item
        auto pledgetx = pointsListModel->pledgeFromIndex(indexes.at(0));
        QClipboard *clipboard = QGuiApplication::clipboard();
        QString addr = QString::fromStdString(EncodeDestination(pledgetx.toDest));
        clipboard->setText(addr);
        QMessageBox::information(this, tr("Copied"), tr("Address %1 is copied to system clipboard").arg(addr));
    }
}

void SendCoinsEntry::copyTxHashActionTriggered()
{
    auto indexes = ui->pointsList->selectionModel()->selectedIndexes();
    if (indexes.size() > 0) {
        // only get the first item
        auto pledgetx = pointsListModel->pledgeFromIndex(indexes.at(0));
        QClipboard *clipboard = QGuiApplication::clipboard();
        QString txhash = QString::fromStdString(pledgetx.txid.GetHex());
        clipboard->setText(txhash);
        QMessageBox::information(this, tr("Copied"), tr("Tx hash %1 is copied to system clipboard").arg(txhash));
    }
}

bool SendCoinsEntry::updateLabel(QString const& address) {
    if(!model)
        return false;

    // Fill in label from address book, if address has an associated label
    QString associatedLabel = model->getAddressTableModel()->labelForAddress(address);
    if(!associatedLabel.isEmpty())
    {
        ui->addAsLabel->setText(associatedLabel);
        return true;
    }

    return false;
}
