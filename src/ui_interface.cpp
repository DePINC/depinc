// Copyright (c) 2010-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <ui_interface.h>

#include <boost/signals2/last_value.hpp>
#include <boost/signals2/signal.hpp>

CClientUIInterface uiInterface;

struct UISignals {
    boost::signals2::signal<CClientUIInterface::ThreadSafeMessageBoxSig, boost::signals2::last_value<bool>> ThreadSafeMessageBox;
    boost::signals2::signal<CClientUIInterface::ThreadSafeQuestionSig, boost::signals2::last_value<bool>> ThreadSafeQuestion;
    boost::signals2::signal<CClientUIInterface::InitMessageSig> InitMessage;
    boost::signals2::signal<CClientUIInterface::NotifyNumConnectionsChangedSig> NotifyNumConnectionsChanged;
    boost::signals2::signal<CClientUIInterface::NotifyNetworkActiveChangedSig> NotifyNetworkActiveChanged;
    boost::signals2::signal<CClientUIInterface::NotifyAlertChangedSig> NotifyAlertChanged;
    boost::signals2::signal<CClientUIInterface::ShowProgressSig> ShowProgress;
    boost::signals2::signal<CClientUIInterface::NotifyBlockTipSig> NotifyBlockTip;
    boost::signals2::signal<CClientUIInterface::NotifyHeaderTipSig> NotifyHeaderTip;
    boost::signals2::signal<CClientUIInterface::BannedListChangedSig> BannedListChanged;
    boost::signals2::signal<CClientUIInterface::NotifyBestDeadlineChangedSig> NotifyBestDeadlineChanged;
    boost::signals2::signal<CClientUIInterface::OmniStateChangedSig> OmniStateChanged;
    boost::signals2::signal<CClientUIInterface::OmniPendingChangedSig> OmniPendingChanged;
    boost::signals2::signal<CClientUIInterface::OmniBalanceChangedSig> OmniBalanceChanged;
    boost::signals2::signal<CClientUIInterface::OmniStateInvalidatedSig> OmniStateInvalidated;
};
static UISignals g_ui_signals;

#define ADD_SIGNALS_IMPL_WRAPPER(signal_name)                                                                 \
    boost::signals2::connection CClientUIInterface::signal_name##_connect(std::function<signal_name##Sig> fn) \
    {                                                                                                         \
        return g_ui_signals.signal_name.connect(fn);                                                          \
    }

ADD_SIGNALS_IMPL_WRAPPER(ThreadSafeMessageBox);
ADD_SIGNALS_IMPL_WRAPPER(ThreadSafeQuestion);
ADD_SIGNALS_IMPL_WRAPPER(InitMessage);
ADD_SIGNALS_IMPL_WRAPPER(NotifyNumConnectionsChanged);
ADD_SIGNALS_IMPL_WRAPPER(NotifyNetworkActiveChanged);
ADD_SIGNALS_IMPL_WRAPPER(NotifyAlertChanged);
ADD_SIGNALS_IMPL_WRAPPER(ShowProgress);
ADD_SIGNALS_IMPL_WRAPPER(NotifyBlockTip);
ADD_SIGNALS_IMPL_WRAPPER(NotifyHeaderTip);
ADD_SIGNALS_IMPL_WRAPPER(BannedListChanged);
ADD_SIGNALS_IMPL_WRAPPER(NotifyBestDeadlineChanged);
ADD_SIGNALS_IMPL_WRAPPER(OmniStateChanged);
ADD_SIGNALS_IMPL_WRAPPER(OmniPendingChanged);
ADD_SIGNALS_IMPL_WRAPPER(OmniBalanceChanged);
ADD_SIGNALS_IMPL_WRAPPER(OmniStateInvalidated);

bool CClientUIInterface::ThreadSafeMessageBox(const std::string& message, const std::string& caption, unsigned int style) { return g_ui_signals.ThreadSafeMessageBox(message, caption, style); }
bool CClientUIInterface::ThreadSafeQuestion(const std::string& message, const std::string& non_interactive_message, const std::string& caption, unsigned int style) { return g_ui_signals.ThreadSafeQuestion(message, non_interactive_message, caption, style); }
void CClientUIInterface::InitMessage(const std::string& message) { return g_ui_signals.InitMessage(message); }
void CClientUIInterface::NotifyNumConnectionsChanged(int newNumConnections) { return g_ui_signals.NotifyNumConnectionsChanged(newNumConnections); }
void CClientUIInterface::NotifyNetworkActiveChanged(bool networkActive) { return g_ui_signals.NotifyNetworkActiveChanged(networkActive); }
void CClientUIInterface::NotifyAlertChanged() { return g_ui_signals.NotifyAlertChanged(); }
void CClientUIInterface::ShowProgress(const std::string& title, int nProgress, bool resume_possible) { return g_ui_signals.ShowProgress(title, nProgress, resume_possible); }
void CClientUIInterface::NotifyBlockTip(bool b, const CBlockIndex* i) { return g_ui_signals.NotifyBlockTip(b, i); }
void CClientUIInterface::NotifyHeaderTip(bool b, const CBlockIndex* i) { return g_ui_signals.NotifyHeaderTip(b, i); }
void CClientUIInterface::BannedListChanged() { return g_ui_signals.BannedListChanged(); }
void CClientUIInterface::NotifyBestDeadlineChanged(int32_t nHeight, uint64_t nPlotterId, uint64_t nNonce, uint64_t nNewDeadline) { return g_ui_signals.NotifyBestDeadlineChanged(nHeight, nPlotterId, nNonce, nNewDeadline); }
void CClientUIInterface::OmniStateChanged() { return g_ui_signals.OmniStateChanged(); }
void CClientUIInterface::OmniPendingChanged(bool b) { return g_ui_signals.OmniPendingChanged(b); }
void CClientUIInterface::OmniBalanceChanged() { return g_ui_signals.OmniBalanceChanged(); }
void CClientUIInterface::OmniStateInvalidated() { return g_ui_signals.OmniStateInvalidated(); }


bool InitError(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

void InitWarning(const std::string& str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
}
