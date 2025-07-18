/***************************************************************************
 *   Copyright (C) 2005-2022 by the Quassel Project                        *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3.                                           *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#pragma once

#include <utility>
#include <vector>

#include <QHash>
#include <QSet>
#include <QString>
#include <QVariant>

#include "backgroundtaskhandler.h"
#include "corealiasmanager.h"
#include "corehighlightrulemanager.h"
#include "coreignorelistmanager.h"
#include "coreinfo.h"
#include "message.h"
#include "metricsserver.h"
#include "peer.h"
#include "protocol.h"
#include "storage.h"

class CoreBacklogManager;
class CoreBufferSyncer;
class CoreBufferViewManager;
class CoreDccConfig;
class CoreIdentity;
class CoreIrcListHelper;
class CoreNetwork;
class CoreNetworkConfig;
class CoreSessionEventProcessor;
class CoreTransferManager;
class CtcpParser;
class EventManager;
class EventStringifier;
class InternalPeer;
class IrcParser;
class MessageEvent;
class RemotePeer;
class SignalProxy;

struct NetworkInfo;

class CoreSession : public QObject
{
    Q_OBJECT

public:
    CoreSession(UserId, bool restoreState, bool strictIdentEnabled, QObject* parent = nullptr);

    std::vector<BufferInfo> buffers() const;
    inline UserId user() const { return _user; }
    CoreNetwork* network(NetworkId) const;
    CoreIdentity* identity(IdentityId) const;

    /**
     * Returns the optionally strict-compliant ident for the given user identity
     *
     * If strict mode is enabled, this will return the user's Quassel username for any identity,
     * otherwise this will return the given identity's ident, whatever it may be.
     *
     * @return The user's ident, compliant with strict mode (when enabled)
     */
    const QString strictCompliantIdent(const CoreIdentity* identity);

    inline CoreNetworkConfig* networkConfig() const { return _networkConfig; }

    Protocol::SessionState sessionState() const;

    inline SignalProxy* signalProxy() const { return _signalProxy; }

    const AliasManager& aliasManager() const { return _aliasManager; }
    AliasManager& aliasManager() { return _aliasManager; }

    inline EventManager* eventManager() const { return _eventManager; }
    inline EventStringifier* eventStringifier() const { return _eventStringifier; }
    inline CoreSessionEventProcessor* sessionEventProcessor() const { return _sessionEventProcessor; }
    inline CtcpParser* ctcpParser() const { return _ctcpParser; }
    inline IrcParser* ircParser() const { return _ircParser; }

    inline CoreIrcListHelper* ircListHelper() const { return _ircListHelper; }

    inline CoreIgnoreListManager* ignoreListManager() { return &_ignoreListManager; }
    inline HighlightRuleManager* highlightRuleManager() { return &_highlightRuleManager; }
    inline CoreTransferManager* transferManager() const { return _transferManager; }
    inline CoreDccConfig* dccConfig() const { return _dccConfig; }
    inline BackgroundTaskHandler* backgroundTaskHandler() const { return _backgroundTaskHandler; }

    //   void attachNetworkConnection(NetworkConnection *conn);

    //! Return necessary data for restoring the session after restarting the core
    void restoreSessionState();

public slots:
    void addClient(RemotePeer* peer);
    void addClient(InternalPeer* peer);

    /**
     * Shuts down the session and deletes itself afterwards.
     */
    void shutdown();

    void msgFromClient(BufferInfo, QString message);

    //! Create an identity and propagate the changes to the clients.
    /** \param identity The identity to be created.
     */
    void createIdentity(const Identity& identity, const QVariantMap& additional);
    void createIdentity(const CoreIdentity& identity);

    //! Remove identity and propagate that fact to the clients.
    /** \param identity The identity to be removed.
     */
    void removeIdentity(IdentityId identity);

    //! Create a network and propagate the changes to the clients.
    /** \param info The network's settings.
     */
    void createNetwork(const NetworkInfo& info, const QStringList& persistentChannels = QStringList());

    //! Remove network and propagate that fact to the clients.
    /** \param network The id of the network to be removed.
     */
    void removeNetwork(NetworkId network);

    //! Rename a Buffer for a given network
    /* \param networkId The id of the network the buffer belongs to
     * \param newName   The new name of the buffer
     * \param oldName   The old name of the buffer
     */
    void renameBuffer(const NetworkId& networkId, const QString& newName, const QString& oldName);

    void changePassword(PeerPtr peer, const QString& userName, const QString& oldPassword, const QString& newPassword);

    void kickClient(int peerId);

    QHash<QString, QString> persistentChannels(NetworkId) const;

    QHash<QString, QByteArray> bufferCiphers(NetworkId id) const;
    void setBufferCipher(NetworkId id, const QString& bufferName, const QByteArray& cipher) const;

    /**
     * Marks us away (or unaway) on all networks
     *
     * @param[in] msg             Away message, or blank to set unaway
     * @param[in] skipFormatting  If true, skip timestamp formatting codes (e.g. if already done)
     */
    void globalAway(const QString& msg = QString(), bool skipFormatting = false);

signals:
    void initialized();
    void sessionStateReceived(const Protocol::SessionState& sessionState);

    // void msgFromGui(uint netid, QString buf, QString message);
    void displayMsg(Message message);
    void displayStatusMsg(QString, QString);

    //! Identity has been created.
    /** This signal is propagated to the clients to tell them that the given identity has been created.
     *  \param identity The new identity.
     */
    void identityCreated(const Identity& identity);

    //! Identity has been removed.
    /** This signal is propagated to the clients to inform them about the removal of the given identity.
     *  \param identity The identity that has been removed.
     */
    void identityRemoved(IdentityId identity);

    void networkCreated(NetworkId);
    void networkRemoved(NetworkId);
    void networkDisconnected(NetworkId);

    void passwordChanged(PeerPtr peer, bool success);

    void disconnectFromCore();

    void bufferRemoved(BufferId);

protected:
    void customEvent(QEvent* event) override;

private slots:
    void removeClient(Peer* peer);

    void recvStatusMsgFromServer(QString msg);
    void recvMessageFromServer(RawMessage msg);

    void destroyNetwork(NetworkId);

    void clientsConnected();
    void clientsDisconnected();

    void updateIdentityBySender();

    void saveSessionState() const;

    void onNetworkDisconnected(NetworkId networkId);

private:
    void processMessages();

    void loadSettings();

    /// Hook for converting events to the old displayMsg() handlers
    Q_INVOKABLE void processMessageEvent(MessageEvent* event);

    BackgroundTaskHandler* _backgroundTaskHandler;

    UserId _user;

    /// Whether or not strict ident mode is enabled, locking users' idents to Quassel username
    bool _strictIdentEnabled;

    SignalProxy* _signalProxy;
    CoreAliasManager _aliasManager;

    QHash<IdentityId, CoreIdentity*> _identities;
    QHash<NetworkId, CoreNetwork*> _networks;
    QSet<NetworkId> _networksPendingDisconnect;

    CoreBufferSyncer* _bufferSyncer;
    CoreBacklogManager* _backlogManager;
    CoreBufferViewManager* _bufferViewManager;
    CoreDccConfig* _dccConfig;
    CoreIrcListHelper* _ircListHelper;
    CoreNetworkConfig* _networkConfig;
    CoreInfo* _coreInfo;
    CoreTransferManager* _transferManager;

    EventManager* _eventManager;
    EventStringifier* _eventStringifier;  // should eventually move into client
    CoreSessionEventProcessor* _sessionEventProcessor;
    CtcpParser* _ctcpParser;
    IrcParser* _ircParser;

    /**
     * This method obtains the prefixes of the message's sender within a channel, by looking up their channelmodes, and
     * processing them to prefixes based on the network's settings.
     * @param sender The hostmask of the sender
     * @param bufferInfo The BufferInfo object of the buffer
     */
    QString senderPrefixes(const QString& sender, const BufferInfo& bufferInfo) const;

    /**
     * This method obtains the realname of the message's sender.
     * @param sender The hostmask of the sender
     * @param networkId The network the user is on
     */
    QString realName(const QString& sender, NetworkId networkId) const;

    /**
     * This method obtains the avatar of the message's sender.
     * @param sender The hostmask of the sender
     * @param networkId The network the user is on
     */
    QString avatarUrl(const QString& sender, NetworkId networkId) const;
    QList<RawMessage> _messageQueue;
    bool _processMessages;
    CoreIgnoreListManager _ignoreListManager;
    CoreHighlightRuleManager _highlightRuleManager;
    MetricsServer* _metricsServer{nullptr};
};

struct NetworkInternalMessage
{
    Message::Type type;
    BufferInfo::Type bufferType;
    QString target;
    QString text;
    QString sender;
    Message::Flags flags;
    NetworkInternalMessage(Message::Type type,
                           BufferInfo::Type bufferType,
                           QString target,
                           QString text,
                           QString sender = "",
                           Message::Flags flags = Message::None)
        : type(type)
        , bufferType(bufferType)
        , target(std::move(target))
        , text(std::move(text))
        , sender(std::move(sender))
        , flags(flags)
    {}
};

struct RawMessage
{
    QDateTime timestamp;
    NetworkId networkId;
    Message::Type type;
    BufferInfo::Type bufferType;
    QString target;
    QString text;
    QString sender;
    Message::Flags flags;

    RawMessage(QDateTime timestamp,
               NetworkId networkId,
               Message::Type type,
               BufferInfo::Type bufferType,
               QString target,
               QString text,
               QString sender,
               Message::Flags flags)
        : timestamp(std::move(timestamp))
        , networkId(networkId)
        , type(type)
        , bufferType(bufferType)
        , target(std::move(target))
        , text(std::move(text))
        , sender(std::move(sender))
        , flags(flags)
    {}

    RawMessage(NetworkId networkId,
               const NetworkInternalMessage& msg)
        : timestamp(QDateTime::currentDateTimeUtc())
        , networkId(networkId)
        , type(msg.type)
        , bufferType(msg.bufferType)
        , target(msg.target)
        , text(msg.text)
        , sender(msg.sender)
        , flags(msg.flags)
    {}
};
