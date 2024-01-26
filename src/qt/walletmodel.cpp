// Copyright (c) 2011-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <qt/walletmodel.h>

#include <qt/addresstablemodel.h>
#include <qt/guiconstants.h>
#include <qt/optionsmodel.h>
#include <qt/paymentserver.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/sendcoinsdialog.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodeltransaction.h>

#include <consensus/tx_verify.h>
#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <ui_interface.h>
#include <util/system.h> // for GetBoolArg
#include <validation.h>
#include <wallet/coincontrol.h>
#include <wallet/uniformer.h>
#include <wallet/wallet.h>

#include <stdint.h>

#include <QDebug>
#include <QMessageBox>
#include <QSet>
#include <QTimer>

#include <chiapos/kernel/utils.h>
#include <chiapos/kernel/bls_key.h>

WalletModel::WalletModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node& node, const PlatformStyle *platformStyle, OptionsModel *_optionsModel, QObject *parent) :
    QObject(parent), m_wallet(std::move(wallet)), m_node(node), optionsModel(_optionsModel), addressTableModel(nullptr),
    transactionTableModel(nullptr),
    recentRequestsTableModel(nullptr),
    cachedEncryptionStatus(Unencrypted),
    cachedNumBlocks(0)
{
    fHaveWatchOnly = m_wallet->haveWatchOnly();
    addressTableModel = new AddressTableModel(this);
    transactionTableModel = new TransactionTableModel(platformStyle, this);
    recentRequestsTableModel = new RecentRequestsTableModel(this);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel()
{
    unsubscribeFromCoreSignals();
}

void WalletModel::startPollBalance()
{
    // This timer will be fired repeatedly to update the balance
    QTimer* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &WalletModel::pollBalanceChanged);
    timer->start(MODEL_UPDATE_DELAY);
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if(cachedEncryptionStatus != newEncryptionStatus) {
        Q_EMIT encryptionStatusChanged();
    }
}

void WalletModel::pollBalanceChanged()
{
    // Try to get balances and return early if locks can't be acquired. This
    // avoids the GUI from getting stuck on periodical polls if the core is
    // holding the locks for a longer time - for example, during a wallet
    // rescan.
    interfaces::WalletBalances new_balances;
    int numBlocks = -1;
    if (!m_wallet->tryGetBalances(new_balances, numBlocks)) {
        return;
    }

    if(fForceCheckBalanceChanged || m_node.getNumBlocks() != cachedNumBlocks)
    {
        fForceCheckBalanceChanged = false;

        // Balance and number of transactions might have changed
        cachedNumBlocks = m_node.getNumBlocks();

        checkBalanceChanged(new_balances);
        if(transactionTableModel)
            transactionTableModel->updateConfirmations();
    }
}

void WalletModel::checkBalanceChanged(const interfaces::WalletBalances& new_balances)
{
    if(new_balances.balanceChanged(m_cached_balances)) {
        m_cached_balances = new_balances;
        Q_EMIT balanceChanged(new_balances);
    }
}

void WalletModel::updateTransaction()
{
    // Balance and number of transactions might have changed
    fForceCheckBalanceChanged = true;
}

void WalletModel::updateAddressBook(const QString &address, const QString &label,
        bool isMine, const QString &purpose, int status)
{
    if(addressTableModel)
        addressTableModel->updateEntry(address, label, isMine, purpose, status);
}

void WalletModel::updateWatchOnlyFlag(bool fHaveWatchonly)
{
    fHaveWatchOnly = fHaveWatchonly;
    Q_EMIT notifyWatchonlyChanged(fHaveWatchonly);
}

bool WalletModel::validateAddress(const QString &address)
{
    return IsValidDestinationString(address.toStdString());
}

WalletModel::SendCoinsReturn WalletModel::prepareTransaction(PayOperateMethod payOperateMethod, WalletModelTransaction &transaction, CCoinControl& coinControl)
{
    CAmount total = 0;
    bool fSubtractFeeFromAmount = false;
    QList<SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector<CRecipient> vecSend;

    if(recipients.empty())
    {
        return OK;
    }

    QSet<QString> setAddress; // Used to detect duplicates
    int nAddresses = 0;

    // Pre-check input data for validity
    for (const SendCoinsRecipient &rcp : recipients)
    {
        if (rcp.fSubtractFeeFromAmount)
            fSubtractFeeFromAmount = true;

#ifdef ENABLE_BIP70
        if (rcp.paymentRequest.IsInitialized())
        {   // PaymentRequest...
            CAmount subtotal = 0;
            const payments::PaymentDetails& details = rcp.paymentRequest.getDetails();
            for (int i = 0; i < details.outputs_size(); i++)
            {
                const payments::Output& out = details.outputs(i);
                if (out.amount() <= 0) continue;
                subtotal += out.amount();
                const unsigned char* scriptStr = (const unsigned char*)out.script().data();
                CScript scriptPubKey(scriptStr, scriptStr+out.script().size());
                CAmount nAmount = out.amount();
                CRecipient recipient = {scriptPubKey, nAmount, rcp.fSubtractFeeFromAmount};
                vecSend.push_back(recipient);
            }
            if (subtotal <= 0)
            {
                return InvalidAmount;
            }
            total += subtotal;
        }
        else
#endif
        {   // User-entered bitcoin address / amount:
            if(!validateAddress(rcp.address))
            {
                return InvalidAddress;
            }
            if(rcp.amount <= 0)
            {
                return InvalidAmount;
            }
            setAddress.insert(rcp.address);
            ++nAddresses;

            CScript scriptPubKey = GetScriptForDestination(DecodeDestination(rcp.address.toStdString()));
            CRecipient recipient = {scriptPubKey, rcp.amount, rcp.fSubtractFeeFromAmount};
            vecSend.push_back(recipient);

            total += rcp.amount;
        }
    }
    if(setAddress.size() != nAddresses)
    {
        return DuplicateAddress;
    }

    CAmount nBalance = m_wallet->getAvailableBalance(coinControl);

    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    {
        CAmount nFeeRequired = 0;
        int nChangePosRet = -1;
        std::string strFailReason;
        int32_t nTxVersion = 0;

        // update tx params
        if (payOperateMethod == PayOperateMethod::Point) {
            if (vecSend.size() != 1)
                return TransactionCreationFailed;
            vecSend.push_back({GetPointScriptForDestination(ExtractDestination(vecSend[0].scriptPubKey), DATACARRIER_TYPE_POINT), 0, false});
            vecSend[0].scriptPubKey = GetScriptForDestination(coinControl.m_pick_dest);
            nChangePosRet = 1;
            nTxVersion = CTransaction::UNIFORM_VERSION;
        } else if (payOperateMethod == PayOperateMethod::BindPlotter) {
            if (vecSend.size() != 1 || recipients[0].plotterPassphrase.isEmpty())
                return TransactionCreationFailed;
            const SendCoinsRecipient &rcp = recipients[0];
            if (rcp.plotterPassphrase.size() == PROTOCOL_BINDPLOTTER_SCRIPTSIZE * 2 && IsHex(rcp.plotterPassphrase.toStdString())) {
                // Hex data
                std::vector<unsigned char> bindData(ParseHex(rcp.plotterPassphrase.toStdString()));
                vecSend.push_back({CScript(bindData.cbegin(), bindData.cend()), 0, false});
            } else {
                // Passphrase
                int nTipHeight = m_wallet->chain().lock()->getHeight().get_value_or(0);
                vecSend.push_back({GetBindPlotterScriptForDestination(coinControl.m_pick_dest, rcp.plotterPassphrase.toStdString(), nTipHeight + rcp.plotterDataAliveHeight), 0, false});
            }
            nChangePosRet = 1;
            nTxVersion = CTransaction::UNIFORM_VERSION;
        } else if (payOperateMethod == PayOperateMethod::ChiaBindFarmerPk) {
            if (vecSend.size() != 1 || recipients[0].plotterPassphrase.isEmpty())
                return TransactionCreationFailed;
            const SendCoinsRecipient &rcp = recipients[0];
            // Passphrase only
            int nTipHeight = m_wallet->chain().lock()->getHeight().get_value_or(0);
            std::string mnemonic = rcp.plotterPassphrase.toStdString();
            chiapos::CWallet wallet(chiapos::CKey::CreateKeyWithMnemonicWords(mnemonic, ""));
            auto farmerSk = wallet.GetFarmerKey(0);
            vecSend.push_back({GetBindChiaPlotterScriptForDestination(coinControl.m_pick_dest, farmerSk, nTipHeight + rcp.plotterDataAliveHeight)});
            nChangePosRet = 1;
            nTxVersion = CTransaction::UNIFORM_VERSION;
        } else if (payOperateMethod == PayOperateMethod::ChiaPoint) {
            if (vecSend.size() != 1)
                return TransactionCreationFailed;
            vecSend.push_back({GetPointScriptForDestination(ExtractDestination(vecSend[0].scriptPubKey), DATACARRIER_TYPE_CHIA_POINT), 0, false});
            vecSend[0].scriptPubKey = GetScriptForDestination(coinControl.m_pick_dest);
            nChangePosRet = 1;
            nTxVersion = CTransaction::UNIFORM_VERSION;
        } else if (payOperateMethod == PayOperateMethod::ChiaPointT1) {
            if (vecSend.size() != 1)
                return TransactionCreationFailed;
            vecSend.push_back({GetPointScriptForDestination(ExtractDestination(vecSend[0].scriptPubKey), DATACARRIER_TYPE_CHIA_POINT_TERM_1), 0, false});
            vecSend[0].scriptPubKey = GetScriptForDestination(coinControl.m_pick_dest);
            nChangePosRet = 1;
            nTxVersion = CTransaction::UNIFORM_VERSION;
        } else if (payOperateMethod == PayOperateMethod::ChiaPointT2) {
            if (vecSend.size() != 1)
                return TransactionCreationFailed;
            vecSend.push_back({GetPointScriptForDestination(ExtractDestination(vecSend[0].scriptPubKey), DATACARRIER_TYPE_CHIA_POINT_TERM_2), 0, false});
            vecSend[0].scriptPubKey = GetScriptForDestination(coinControl.m_pick_dest);
            nChangePosRet = 1;
            nTxVersion = CTransaction::UNIFORM_VERSION;
        } else if (payOperateMethod == PayOperateMethod::ChiaPointT3) {
            if (vecSend.size() != 1)
                return TransactionCreationFailed;
            vecSend.push_back({GetPointScriptForDestination(ExtractDestination(vecSend[0].scriptPubKey), DATACARRIER_TYPE_CHIA_POINT_TERM_3), 0, false});
            vecSend[0].scriptPubKey = GetScriptForDestination(coinControl.m_pick_dest);
            nChangePosRet = 1;
            nTxVersion = CTransaction::UNIFORM_VERSION;
        } else if (payOperateMethod == PayOperateMethod::ChiaPointRetarget) {
            if (vecSend.size() != 1)
                return TransactionCreationFailed;
            // prepare COutPoint
            COutPoint previousOutPoint(recipients[0].retargetTxid, 0);
            coinControl.Select(previousOutPoint);
            Coin const& coin = m_wallet->chain().accessCoin(previousOutPoint);
            // check before creating the tx
            auto const& params = Params().GetConsensus();
            LOCK(cs_main);
            auto pindex = ::ChainActive().Tip();
            int nTargetHeight = pindex->nHeight + 1;
            if (coin.nHeight + params.BHDIP009PledgeRetargetMinHeights > nTargetHeight) {
                // cannot create the tx for retargeting
                return SendCoinsReturn(RetargetTooEarlier, QString::fromStdString(tinyformat::format("Retarget a tx too earlier, you need to wait for %d blocks before retargeting it, please wait until height %d", params.BHDIP009PledgeRetargetMinHeights, coin.nHeight + params.BHDIP009PledgeRetargetMinHeights)));
            }
            // prepare transaction
            DatacarrierType pointType = recipients[0].pointType;
            int nPointHeight = recipients[0].pointHeight;
            vecSend.push_back({GetPointRetargetScriptForDestination(ExtractDestination(vecSend[0].scriptPubKey), pointType, nPointHeight), 0, false});
            vecSend[0] = { GetScriptForDestination(coinControl.m_pick_dest), coin.out.nValue, false };
            coinControl.fAllowOtherInputs = true;
            nChangePosRet = 1;
            nTxVersion = CTransaction::UNIFORM_VERSION;
        }

        auto& newTx = transaction.getWtx();
        newTx = m_wallet->createTransaction(vecSend, coinControl, true /* sign */, nChangePosRet, nFeeRequired, strFailReason, nTxVersion);
        transaction.setTransactionFee(nFeeRequired);
        if (fSubtractFeeFromAmount && newTx)
            transaction.reassignAmounts(nChangePosRet);

        if(!newTx)
        {
            if(!fSubtractFeeFromAmount && (total + nFeeRequired) > nBalance)
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance);
            }
            Q_EMIT message(tr("Send Coins"), QString::fromStdString(strFailReason),
                         CClientUIInterface::MSG_ERROR);
            return TransactionCreationFailed;
        }

        // Reject absurdly high fee. (This can never happen because the
        // wallet never creates transactions with fee greater than
        // m_default_max_tx_fee. This merely a belt-and-suspenders check).
        if (nFeeRequired > m_wallet->getDefaultMaxTxFee()) {
            return AbsurdFee;
        }
    }

    return SendCoinsReturn(OK);
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(WalletModelTransaction &transaction)
{
    QByteArray transaction_array; /* store serialized transaction */

    {
        std::vector<std::pair<std::string, std::string>> vOrderForm;
        for (const SendCoinsRecipient &rcp : transaction.getRecipients())
        {
#ifdef ENABLE_BIP70
            if (rcp.paymentRequest.IsInitialized())
            {
                // Make sure any payment requests involved are still valid.
                if (PaymentServer::verifyExpired(rcp.paymentRequest.getDetails())) {
                    return PaymentRequestExpired;
                }

                // Store PaymentRequests in wtx.vOrderForm in wallet.
                std::string value;
                rcp.paymentRequest.SerializeToString(&value);
                vOrderForm.emplace_back("PaymentRequest", std::move(value));
            }
            else
#endif
            if (!rcp.message.isEmpty()) // Message from normal btchd:URI (btchd:123...?message=example)
                vOrderForm.emplace_back("Message", rcp.message.toStdString());
        }

        auto& newTx = transaction.getWtx();
        std::string rejectReason;
        if (!wallet().commitTransaction(newTx, {} /* mapValue */, std::move(vOrderForm), rejectReason))
            return SendCoinsReturn(TransactionCommitFailed, QString::fromStdString(rejectReason));

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
       ssTx << *newTx;
        transaction_array.append(&(ssTx[0]), ssTx.size());
    }

    // Add addresses / update labels that we've sent to the address book,
    // and emit coinsSent signal for each recipient
    for (const SendCoinsRecipient &rcp : transaction.getRecipients())
    {
        // Don't touch the address book when we have a payment request
#ifdef ENABLE_BIP70
        if (!rcp.paymentRequest.IsInitialized())
#endif
        {
            std::string strAddress = rcp.address.toStdString();
            CTxDestination dest = DecodeDestination(strAddress);
            std::string strLabel = rcp.label.toStdString();
            {
                // Check if we have a new address or an updated label
                std::string name;
                if (!m_wallet->getAddress(
                     dest, &name, /* is_mine= */ nullptr, /* purpose= */ nullptr))
                {
                    m_wallet->setAddressBook(dest, strLabel, "send");
                }
                else if (name != strLabel)
                {
                    m_wallet->setAddressBook(dest, strLabel, ""); // "" means don't change purpose
                }
            }
        }
        Q_EMIT coinsSent(this, rcp, transaction_array);
    }

    checkBalanceChanged(m_wallet->getBalances()); // update balance immediately, otherwise there could be a short noticeable delay until pollBalanceChanged hits

    return SendCoinsReturn(OK);
}

OptionsModel *WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

RecentRequestsTableModel *WalletModel::getRecentRequestsTableModel()
{
    return recentRequestsTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{
    if(!m_wallet->isCrypted())
    {
        return Unencrypted;
    }
    else if(m_wallet->isLocked())
    {
        return Locked;
    }
    else
    {
        return Unlocked;
    }
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase)
{
    if(encrypted)
    {
        // Encrypt
        return m_wallet->encryptWallet(passphrase);
    }
    else
    {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase)
{
    if(locked)
    {
        // Lock
        return m_wallet->lock();
    }
    else
    {
        // Unlock
        return m_wallet->unlock(passPhrase);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    m_wallet->lock(); // Make sure wallet is locked before attempting pass change
    return m_wallet->changeWalletPassphrase(oldPass, newPass);
}

// Handlers for core signals
static void NotifyUnload(WalletModel* walletModel)
{
    qDebug() << "NotifyUnload";
    bool invoked = QMetaObject::invokeMethod(walletModel, "unload");
    assert(invoked);
}

static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel)
{
    qDebug() << "NotifyKeyStoreStatusChanged";
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
    assert(invoked);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel,
        const CTxDestination &address, const std::string &label, bool isMine,
        const std::string &purpose, ChangeType status)
{
    QString strAddress = QString::fromStdString(EncodeDestination(address));
    QString strLabel = QString::fromStdString(label);
    QString strPurpose = QString::fromStdString(purpose);

    qDebug() << "NotifyAddressBookChanged: " + strAddress + " " + strLabel + " isMine=" + QString::number(isMine) + " purpose=" + strPurpose + " status=" + QString::number(status);
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                              Q_ARG(QString, strAddress),
                              Q_ARG(QString, strLabel),
                              Q_ARG(bool, isMine),
                              Q_ARG(QString, strPurpose),
                              Q_ARG(int, status));
    assert(invoked);
}

static void NotifyTransactionChanged(WalletModel *walletmodel, const uint256 &hash, ChangeType status)
{
    Q_UNUSED(hash);
    Q_UNUSED(status);
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection);
    assert(invoked);
}

static void ShowProgress(WalletModel *walletmodel, const std::string &title, int nProgress)
{
    // emits signal "showProgress"
    bool invoked = QMetaObject::invokeMethod(walletmodel, "showProgress", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(title)),
                              Q_ARG(int, nProgress));
    assert(invoked);
}

static void NotifyWatchonlyChanged(WalletModel *walletmodel, bool fHaveWatchonly)
{
    bool invoked = QMetaObject::invokeMethod(walletmodel, "updateWatchOnlyFlag", Qt::QueuedConnection,
                              Q_ARG(bool, fHaveWatchonly));
    assert(invoked);
}

static void NotifyCanGetAddressesChanged(WalletModel* walletmodel)
{
    bool invoked = QMetaObject::invokeMethod(walletmodel, "canGetAddressesChanged");
    assert(invoked);
}

static void NotifyPrimaryAddressChanged(WalletModel* walletmodel)
{
    bool invoked = QMetaObject::invokeMethod(walletmodel, "primaryAddressChanged");
    assert(invoked);
}

void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    m_handler_unload = m_wallet->handleUnload(std::bind(&NotifyUnload, this));
    m_handler_status_changed = m_wallet->handleStatusChanged(std::bind(&NotifyKeyStoreStatusChanged, this));
    m_handler_address_book_changed = m_wallet->handleAddressBookChanged(std::bind(NotifyAddressBookChanged, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5));
    m_handler_transaction_changed = m_wallet->handleTransactionChanged(std::bind(NotifyTransactionChanged, this, std::placeholders::_1, std::placeholders::_2));
    m_handler_show_progress = m_wallet->handleShowProgress(std::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2));
    m_handler_watch_only_changed = m_wallet->handleWatchOnlyChanged(std::bind(NotifyWatchonlyChanged, this, std::placeholders::_1));
    m_handler_can_get_addrs_changed = m_wallet->handleCanGetAddressesChanged(boost::bind(NotifyCanGetAddressesChanged, this));
    m_handler_primary_address_changed = m_wallet->handlePrimaryAddressChanged(boost::bind(NotifyPrimaryAddressChanged, this));
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    m_handler_unload->disconnect();
    m_handler_status_changed->disconnect();
    m_handler_address_book_changed->disconnect();
    m_handler_transaction_changed->disconnect();
    m_handler_show_progress->disconnect();
    m_handler_watch_only_changed->disconnect();
    m_handler_can_get_addrs_changed->disconnect();
    m_handler_primary_address_changed->disconnect();
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    bool was_locked = getEncryptionStatus() == Locked;
    if(was_locked)
    {
        // Request UI to unlock wallet
        Q_EMIT requireUnlock();
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = getEncryptionStatus() != Locked;

    return UnlockContext(this, valid, was_locked);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *_wallet, bool _valid, bool _relock):
        wallet(_wallet),
        valid(_valid),
        relock(_relock)
{
}

WalletModel::UnlockContext::~UnlockContext()
{
    if(valid && relock)
    {
        wallet->setWalletLocked(true);
    }
}

void WalletModel::UnlockContext::CopyFrom(UnlockContext&& rhs)
{
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

void WalletModel::loadReceiveRequests(std::vector<std::string>& vReceiveRequests)
{
    vReceiveRequests = m_wallet->getDestValues("rr"); // receive request
}

bool WalletModel::saveReceiveRequest(const std::string &sAddress, const int64_t nId, const std::string &sRequest)
{
    CTxDestination dest = DecodeDestination(sAddress);

    std::stringstream ss;
    ss << nId;
    std::string key = "rr" + ss.str(); // "rr" prefix = "receive request" in destdata

    if (sRequest.empty())
        return m_wallet->eraseDestData(dest, key);
    else
        return m_wallet->addDestData(dest, key, sRequest);
}

bool WalletModel::bumpFee(uint256 hash, uint256& new_hash)
{
    CCoinControl coin_control;
    coin_control.m_signal_bip125_rbf = true;
    std::vector<std::string> errors;
    CAmount old_fee;
    CAmount new_fee;
    CMutableTransaction mtx;
    if (!m_wallet->createBumpTransaction(hash, coin_control, 0 /* totalFee */, errors, old_fee, new_fee, mtx)) {
        QMessageBox::critical(nullptr, tr("Fee bump error"), tr("Increasing transaction fee failed") + "<br />(" +
            (errors.size() ? QString::fromStdString(errors[0]) : "") +")");
         return false;
    }

    // allow a user based fee verification
    QString questionString = tr("Do you want to increase the fee?");
    questionString.append("<br />");
    questionString.append("<table style=\"text-align: left;\">");
    questionString.append("<tr><td>");
    questionString.append(tr("Current fee:"));
    questionString.append("</td><td>");
    questionString.append(BitcoinUnits::formatHtmlWithUnit(getOptionsModel()->getDisplayUnit(), old_fee));
    questionString.append("</td></tr><tr><td>");
    questionString.append(tr("Increase:"));
    questionString.append("</td><td>");
    questionString.append(BitcoinUnits::formatHtmlWithUnit(getOptionsModel()->getDisplayUnit(), new_fee - old_fee));
    questionString.append("</td></tr><tr><td>");
    questionString.append(tr("New fee:"));
    questionString.append("</td><td>");
    questionString.append(BitcoinUnits::formatHtmlWithUnit(getOptionsModel()->getDisplayUnit(), new_fee));
    questionString.append("</td></tr></table>");
    SendConfirmationDialog confirmationDialog(tr("Confirm fee bump"), questionString);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = static_cast<QMessageBox::StandardButton>(confirmationDialog.result());

    // cancel sign&broadcast if user doesn't want to bump the fee
    if (retval != QMessageBox::Yes) {
        return false;
    }

    WalletModel::UnlockContext ctx(requestUnlock());
    if(!ctx.isValid())
    {
        return false;
    }

    // sign bumped transaction
    if (!m_wallet->signBumpTransaction(mtx)) {
        QMessageBox::critical(nullptr, tr("Fee bump error"), tr("Can't sign transaction."));
        return false;
    }
    // commit the bumped transaction
    if(!m_wallet->commitBumpTransaction(hash, std::move(mtx), errors, new_hash)) {
        QMessageBox::critical(nullptr, tr("Fee bump error"), tr("Could not commit transaction") + "<br />(" +
            QString::fromStdString(errors[0])+")");
         return false;
    }
    return true;
}

bool WalletModel::unfreezeTransaction(uint256 hash)
{
    const COutPoint outpoint(hash, 0);

    std::vector<std::string> errors;
    CAmount total_fee;
    CMutableTransaction mtx;
    if (!m_wallet->createUnfreezeTransaction(outpoint, errors, total_fee, mtx)) {
        QMessageBox::critical(0, tr("Unfreeze error"), tr("Create transaction failed") + "<br />(" +
                (errors.size() ? QString::fromStdString(errors[0]) : "") +")");
        return false;
    }
    interfaces::WalletTx wtx = m_wallet->getWalletTx(hash);
    if (!wtx.value_map.count("lock") || !wtx.value_map.count("type")) {
        return false;
    }

    // Ask
    if (wtx.value_map["type"] == "bindplotter") {
        QString questionString = tr("Are you sure you want to unbind plotter?");
        questionString.append("<br />");
        questionString.append("<table style=\"text-align: left;\">");
        questionString.append("<tr><td width=100>").append(tr("Address:")).append("</td><td>").append(QString::fromStdString(wtx.value_map["from"]));
        questionString.append("</td></tr>");
        questionString.append("<tr><td>").append(tr("Farmer PubKey:")).append("</td><td>").append(QString::fromStdString(wtx.value_map["plotter_id"])).append("</td></tr>");
        questionString.append("<tr><td>").append(tr("Return amount:")).append("</td><td>").append(BitcoinUnits::formatHtmlWithUnit(getOptionsModel()->getDisplayUnit(), mtx.vout[0].nValue)).append("</td></tr>");
        questionString.append("<tr style='color:#aa0000;'><td>").append(tr("Transaction fee:")).append("</td><td>").append(BitcoinUnits::formatHtmlWithUnit(getOptionsModel()->getDisplayUnit(), total_fee)).append("</td></tr>");
        questionString.append("</table>");

        SendConfirmationDialog confirmationDialog(tr("Unbind plotter"), questionString);
        if (confirmationDialog.exec() != QMessageBox::Yes) {
            return false;
        }
    } else if (wtx.value_map["type"] == "pledge") {
        QString questionString = tr("Are you sure you want to withdraw point?");
        questionString.append("<br />");
        questionString.append("<table style=\"text-align: left;\">");
        questionString.append("<tr><td width=100>").append(tr("From address:")).append("</td><td>").append(QString::fromStdString(wtx.value_map["from"]));
        questionString.append("</td></tr>");
        questionString.append("<tr><td>").append(tr("To address:")).append("</td><td>").append(QString::fromStdString(wtx.value_map["to"]));
        questionString.append("</td></tr>");
        questionString.append("<tr><td>").append(tr("Return amount:")).append("</td><td>").append(BitcoinUnits::formatHtmlWithUnit(getOptionsModel()->getDisplayUnit(), mtx.vout[0].nValue)).append("</td></tr>");
        questionString.append("<tr style='color:#aa0000;'><td>").append(tr("Transaction fee:")).append("</td><td>").append(BitcoinUnits::formatHtmlWithUnit(getOptionsModel()->getDisplayUnit(), total_fee)).append("</td></tr>");
        questionString.append("</table>");

        SendConfirmationDialog confirmationDialog(tr("Withdraw point"), questionString);
        if (confirmationDialog.exec() != QMessageBox::Yes) {
            return false;
        }
    } else if (wtx.value_map["type"] == "retarget") {
        QString questionString = tr("Are you sure you want to withdraw retarget point?");
        questionString.append("<br />");
        questionString.append("<table style=\"text-align: left;\">");
        questionString.append("<tr><td width=100>").append(tr("From address:")).append("</td><td>").append(QString::fromStdString(wtx.value_map["from"]));
        questionString.append("</td></tr>");
        questionString.append("<tr><td>").append(tr("To address:")).append("</td><td>").append(QString::fromStdString(wtx.value_map["to"]));
        questionString.append("</td></tr>");
        questionString.append("<tr><td>").append(tr("Return amount:")).append("</td><td>").append(BitcoinUnits::formatHtmlWithUnit(getOptionsModel()->getDisplayUnit(), mtx.vout[0].nValue)).append("</td></tr>");
        questionString.append("<tr style='color:#aa0000;'><td>").append(tr("Transaction fee:")).append("</td><td>").append(BitcoinUnits::formatHtmlWithUnit(getOptionsModel()->getDisplayUnit(), total_fee)).append("</td></tr>");
        questionString.append("</table>");

        SendConfirmationDialog confirmationDialog(tr("Withdraw retarget point"), questionString);
        if (confirmationDialog.exec() != QMessageBox::Yes) {
            return false;
        }
    } else {
        return false;
    }

    // Sign and commit
    WalletModel::UnlockContext ctx(requestUnlock());
    if(!ctx.isValid())
        return false;
    if (!m_wallet->signAndCommitUnfreezeTransaction(std::move(mtx), errors)) {
        QMessageBox::critical(0, tr("Unfreeze error"), tr("Could not commit transaction") + "<br />(" +
                (errors.size() ? QString::fromStdString(errors[0]) : "") +")");
        return false;
    }
    return true;
}

bool WalletModel::isWalletEnabled()
{
   return !gArgs.GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET);
}

bool WalletModel::privateKeysDisabled() const
{
    return m_wallet->IsWalletFlagSet(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
}

bool WalletModel::canGetAddresses() const
{
    return m_wallet->canGetAddresses();
}

QString WalletModel::getWalletName() const
{
    return QString::fromStdString(m_wallet->getWalletName());
}

QString WalletModel::getDisplayName() const
{
    const QString name = getWalletName();
    return name.isEmpty() ? "["+tr("default wallet")+"]" : name;
}

bool WalletModel::isMultiwallet()
{
    return m_node.getWallets().size() > 1;
}
