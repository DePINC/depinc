// Copyright (c) 2011-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_CLIENTMODEL_H
#define BITCOIN_QT_CLIENTMODEL_H

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h>
#endif

#include <QObject>
#include <QDateTime>

#include <atomic>
#include <memory>

class BanTableModel;
class OptionsModel;
class PeerTableModel;

class CBlockIndex;

namespace interfaces {
class Handler;
class Node;
}

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

enum class BlockSource {
    NONE,
    REINDEX,
    DISK,
    NETWORK
};

enum NumConnections {
    CONNECTIONS_NONE = 0,
    CONNECTIONS_IN   = (1U << 0),
    CONNECTIONS_OUT  = (1U << 1),
    CONNECTIONS_ALL  = (CONNECTIONS_IN | CONNECTIONS_OUT),
};

/** Model for Bitcoin network client. */
class ClientModel : public QObject
{
    Q_OBJECT

public:
    explicit ClientModel(interfaces::Node& node, OptionsModel *optionsModel, QObject *parent = nullptr);
    ~ClientModel();

    interfaces::Node& node() const { return m_node; }
    OptionsModel *getOptionsModel();
    PeerTableModel *getPeerTableModel();
    BanTableModel *getBanTableModel();

    //! Return number of connections, default is in- and outbound (total)
    int getNumConnections(unsigned int flags = CONNECTIONS_ALL) const;
    int getHeaderTipHeight() const;
    int64_t getHeaderTipTime() const;

    //! Returns enum BlockSource of the current importing/syncing state
    enum BlockSource getBlockSource() const;
    //! Return warnings to be displayed in status bar
    QString getStatusBarWarnings() const;

    QString formatFullVersion() const;
    QString formatSubVersion() const;
    bool isReleaseVersion() const;
    QString formatClientStartupTime() const;
    QString dataDir() const;
    QString blocksDir() const;

    bool getProxyInfo(std::string& ip_port) const;

#ifdef ENABLE_OMNICORE
    // Try to avoid Omni queuing too many messages
    bool tryLockOmniStateChanged();
    bool tryLockOmniBalanceChanged();
#endif

    // caches for the best header
    mutable std::atomic<int> cachedBestHeaderHeight;
    mutable std::atomic<int64_t> cachedBestHeaderTime;

private:
    interfaces::Node& m_node;
    std::unique_ptr<interfaces::Handler> m_handler_show_progress;
    std::unique_ptr<interfaces::Handler> m_handler_notify_num_connections_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_network_active_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_alert_changed;
    std::unique_ptr<interfaces::Handler> m_handler_banned_list_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_header_tip;
#ifdef ENABLE_OMNICORE
    std::unique_ptr<interfaces::Handler> m_handler_omni_state_changed;
    std::unique_ptr<interfaces::Handler> m_handler_omni_pending_changed;
    std::unique_ptr<interfaces::Handler> m_handler_omni_balance_changed;
    std::unique_ptr<interfaces::Handler> m_handler_omni_state_invalidated;
#endif

    OptionsModel *optionsModel;
    PeerTableModel *peerTableModel;
    BanTableModel *banTableModel;

    //! A thread to interact with m_node asynchronously
    QThread* const m_thread;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

#ifdef ENABLE_OMNICORE
    //! Locks for Omni state changes
    bool lockedOmniStateChanged;
    bool lockedOmniBalanceChanged;
#endif

Q_SIGNALS:
    void numConnectionsChanged(int count);
    void numBlocksChanged(int count, const QDateTime& blockDate, double nVerificationProgress, bool header);
    void mempoolSizeChanged(long count, size_t mempoolSizeInBytes);
    void networkActiveChanged(bool networkActive);
    void alertsChanged(const QString &warnings);
    void bytesChanged(quint64 totalBytesIn, quint64 totalBytesOut);

#ifdef ENABLE_OMNICORE
    // Additional Omni signals
    void refreshOmniState();
    void refreshOmniPending(bool pending);
    void refreshOmniBalance();
    void reinitOmniState();
#endif

    //! Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);

    // Show progress dialog e.g. for verifychain
    void showProgress(const QString &title, int nProgress);

public Q_SLOTS:
    void updateNumConnections(int numConnections);
    void updateNetworkActive(bool networkActive);
    void updateAlert();
    void updateBanlist();

#ifdef ENABLE_OMNICORE
    //! Additional Omni slots
    void updateOmniState();
    void updateOmniPending(bool pending);
    void updateOmniBalance();
    void invalidateOmniState();
#endif
};

#endif // BITCOIN_QT_CLIENTMODEL_H
