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

#include "chatitem.h"

#include <algorithm>
#include <iterator>

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QTextLayout>

#include "action.h"
#include "buffermodel.h"
#include "bufferview.h"
#include "chatline.h"
#include "chatlinemodel.h"
#include "chatview.h"
#include "contextmenuactionprovider.h"
#include "icon.h"
#include "mainwin.h"
#include "qtui.h"
#include "qtuistyle.h"

ChatItem::ChatItem(const QRectF& boundingRect, ChatLine* parent)
    : _parent(parent)
    , _boundingRect(boundingRect)
    , _selectionMode(NoSelection)
    , _selectionStart(-1)
    , _cachedLayout(nullptr)
{}

ChatItem::~ChatItem()
{
    delete _cachedLayout;
}

ChatLine* ChatItem::chatLine() const
{
    return _parent;
}

ChatScene* ChatItem::chatScene() const
{
    return chatLine()->chatScene();
}

ChatView* ChatItem::chatView() const
{
    return chatScene()->chatView();
}

const QAbstractItemModel* ChatItem::model() const
{
    return chatLine()->model();
}

int ChatItem::row() const
{
    return chatLine()->row();
}

QPointF ChatItem::mapToLine(const QPointF& p) const
{
    return p + pos();
}

QPointF ChatItem::mapFromLine(const QPointF& p) const
{
    return p - pos();
}

// relative to the ChatLine
QPointF ChatItem::mapToScene(const QPointF& p) const
{
    return chatLine()->mapToScene(p /* + pos() */);
}

QPointF ChatItem::mapFromScene(const QPointF& p) const
{
    return chatLine()->mapFromScene(p) /* - pos() */;
}

QVariant ChatItem::data(int role) const
{
    QModelIndex index = model()->index(row(), column());
    if (!index.isValid()) {
        qWarning() << "ChatItem::data(): model index is invalid!" << index;
        return QVariant();
    }
    return model()->data(index, role);
}

QTextLayout* ChatItem::layout() const
{
    if (_cachedLayout)
        return _cachedLayout;

    _cachedLayout = new QTextLayout;
    initLayout(_cachedLayout);
    chatView()->setHasCache(chatLine());
    return _cachedLayout;
}

void ChatItem::clearCache()
{
    delete _cachedLayout;
    _cachedLayout = nullptr;
}

void ChatItem::initLayoutHelper(QTextLayout* layout, QTextOption::WrapMode wrapMode, Qt::Alignment alignment) const
{
    Q_ASSERT(layout);

    layout->setText(data(MessageModel::DisplayRole).toString());

    QTextOption option;
    option.setWrapMode(wrapMode);
    option.setAlignment(alignment);
    layout->setTextOption(option);

    UiStyle::FormatContainer formatRanges = QtUi::style()->toTextLayoutList(
        formatList(),
        layout->text().length(),
        data(ChatLineModel::MsgLabelRole).value<UiStyle::MessageLabel>()
    );
    UiStyle::setTextLayoutFormats(*layout, formatRanges);
}

void ChatItem::initLayout(QTextLayout* layout) const
{
    initLayoutHelper(layout, QTextOption::NoWrap);
    doLayout(layout);
}

void ChatItem::doLayout(QTextLayout* layout) const
{
    layout->beginLayout();
    QTextLine line = layout->createLine();
    if (line.isValid()) {
        line.setLineWidth(width());
        line.setPosition(QPointF(0, 0));
    }
    layout->endLayout();
}

UiStyle::FormatList ChatItem::formatList() const
{
    return data(MessageModel::FormatRole).value<UiStyle::FormatList>();
}

qint16 ChatItem::posToCursor(const QPointF& posInLine) const
{
    QPointF pos = mapFromLine(posInLine);
    if (pos.y() > height())
        return data(MessageModel::DisplayRole).toString().length();
    if (pos.y() < 0)
        return 0;

    for (int l = layout()->lineCount() - 1; l >= 0; l--) {
        QTextLine line = layout()->lineAt(l);
        if (pos.y() >= line.y()) {
            return line.xToCursor(pos.x(), QTextLine::CursorOnCharacter);
        }
    }
    return 0;
}

void ChatItem::paintBackground(QPainter* painter)
{
    QVariant bgBrush;
    if (_selectionMode == FullSelection)
        bgBrush = data(ChatLineModel::SelectedBackgroundRole);
    else
        bgBrush = data(ChatLineModel::BackgroundRole);
    if (bgBrush.isValid())
        painter->fillRect(boundingRect(), bgBrush.value<QBrush>());
}

// NOTE: This is not the most time-efficient implementation, but it saves space by not caching unnecessary data
//       This is a deliberate trade-off. (-> selectFmt creation, data() call)
void ChatItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    painter->save();
    painter->setClipRect(boundingRect());
    paintBackground(painter);

    layout()->draw(painter, pos(), additionalFormats(), boundingRect());

    //  layout()->draw(painter, QPointF(0,0), formats, boundingRect());

    // Debugging Stuff
    // uncomment partially or all of the following stuff:
    //
    // 0) alternativ painter color for debug stuff
    //   if(row() % 2)
    //     painter->setPen(Qt::red);
    //   else
    //     painter->setPen(Qt::blue);
    // 1) draw wordwrap points in the first line
    //   if(column() == 2) {
    //     ChatLineModel::WrapList wrapList = data(ChatLineModel::WrapListRole).value<ChatLineModel::WrapList>();
    //     foreach(ChatLineModel::Word word, wrapList) {
    //       if(word.endX > width())
    //      break;
    //       painter->drawLine(word.endX, 0, word.endX, height());
    //     }
    //   }
    // 2) draw MsgId over the time column
    //   if(column() == 0) {
    //     QString msgIdString = QString::number(data(MessageModel::MsgIdRole).value<MsgId>().toLongLong());
    //     QPointF bottomPoint = boundingRect().bottomLeft();
    //     bottomPoint.ry() -= 2;
    //     painter->drawText(bottomPoint, msgIdString);
    //   }
    // 3) draw bounding rect
    //   painter->drawRect(_boundingRect.adjusted(0, 0, -1, -1));

    painter->restore();
}

void ChatItem::overlayFormat(UiStyle::FormatList& fmtList, quint16 start, quint16 end, UiStyle::FormatType overlayFmt) const
{
    for (size_t i = 0; i < fmtList.size(); i++) {
        int fmtStart = fmtList.at(i).first;
        int fmtEnd = (i < fmtList.size() - 1 ? fmtList.at(i + 1).first : data(MessageModel::DisplayRole).toString().length());

        if (fmtEnd <= start)
            continue;
        if (fmtStart >= end)
            break;

        // split the format if necessary
        if (fmtStart < start) {
            fmtList.insert(fmtList.begin() + i, fmtList.at(i));
            fmtList[++i].first = start;
        }
        if (end < fmtEnd) {
            fmtList.insert(fmtList.begin() + i, fmtList.at(i));
            fmtList[i + 1].first = end;
        }

        fmtList[i].second.type |= overlayFmt;
    }
}

QVector<QTextLayout::FormatRange> ChatItem::additionalFormats() const
{
    // Calculate formats to overlay (only) if there's a selection, and/or a hovered clickable
    if (!hasSelection() && !hasActiveClickable()) {
        return {};
    }

    using Label = UiStyle::MessageLabel;
    using Format = UiStyle::Format;

    auto itemLabel = data(ChatLineModel::MsgLabelRole).value<Label>();
    const auto& fmtList = formatList();

    struct LabelFormat
    {
        quint16 offset;
        Format format;
        Label label;
    };

    // Transform formatList() into an extended list of LabelFormats
    std::vector<LabelFormat> labelFmtList;
    std::transform(fmtList.cbegin(), fmtList.cend(), std::back_inserter(labelFmtList), [itemLabel](const std::pair<quint16, Format>& f) {
        return LabelFormat{f.first, f.second, itemLabel};
    });
    // Append dummy element to avoid special-casing handling the last real format
    labelFmtList.push_back(LabelFormat{quint16(data(MessageModel::DisplayRole).toString().length()), Format(), itemLabel});

    // Apply the given label to the given range in the format list, splitting formats as necessary
    auto applyLabel = [&labelFmtList](quint16 start, quint16 end, Label label) {
        size_t i = 0;

        // Skip unaffected formats
        for (; i < labelFmtList.size() - 1; ++i) {
            if (labelFmtList[i + 1].offset > start)
                break;
        }
        // Range start doesn't align; split affected format and let the index point to the newly inserted copy
        if (labelFmtList[i].offset < start) {
            labelFmtList.insert(labelFmtList.begin() + i, labelFmtList[i]);
            labelFmtList[++i].offset = start;
        }

        // Apply label to formats fully affected
        for (; i < labelFmtList.size() - 1; ++i) {
            if (labelFmtList[i + 1].offset <= end) {
                labelFmtList[i].label |= label;
                continue;
            }
            // Last affected format, split if end of range doesn't align
            if (labelFmtList[i + 1].offset > end) {
                labelFmtList.insert(labelFmtList.begin() + i, labelFmtList[i]);
                labelFmtList[i].label |= label;
                labelFmtList[i + 1].offset = end;
            }
            break;
        }
    };

    // Apply selection label
    if (hasSelection()) {
        quint16 start, end;
        if (_selectionMode == FullSelection) {
            start = 0;
            end = data(MessageModel::DisplayRole).toString().length();
        }
        else {
            start = qMin(_selectionStart, _selectionEnd);
            end = qMax(_selectionStart, _selectionEnd);
        }
        applyLabel(start, end, Label::Selected);
    }

    // Apply hovered label
    if (hasActiveClickable()) {
        applyLabel(activeClickableRange().first, activeClickableRange().second, Label::Hovered);
    }

    // Add all formats that have an extra label to the additionalFormats list
    UiStyle::FormatContainer additionalFormats;
    for (size_t i = 0; i < labelFmtList.size() - 1; ++i) {
        if (labelFmtList[i].label != itemLabel) {
            additionalFormats << QtUi::style()->toTextLayoutList({std::make_pair(labelFmtList[i].offset, labelFmtList[i].format)},
                                                                   labelFmtList[i + 1].offset,
                                                                   labelFmtList[i].label);
        }
    }

    return UiStyle::containerToVector(additionalFormats);
}

bool ChatItem::hasSelection() const
{
    if (_selectionMode == NoSelection)
        return false;
    if (_selectionMode == FullSelection)
        return true;
    // partial
    return _selectionStart != _selectionEnd;
}

QString ChatItem::selection() const
{
    if (_selectionMode == FullSelection)
        return data(MessageModel::DisplayRole).toString();
    if (_selectionMode == PartialSelection)
        return data(MessageModel::DisplayRole).toString().mid(qMin(_selectionStart, _selectionEnd), qAbs(_selectionStart - _selectionEnd));
    return QString();
}

void ChatItem::setSelection(SelectionMode mode, qint16 start, qint16 end)
{
    _selectionMode = mode;
    _selectionStart = start;
    _selectionEnd = end;
    chatLine()->update();
}

void ChatItem::setFullSelection()
{
    if (_selectionMode != FullSelection) {
        _selectionMode = FullSelection;
        chatLine()->update();
    }
}

void ChatItem::clearSelection()
{
    if (_selectionMode != NoSelection) {
        _selectionMode = NoSelection;
        chatLine()->update();
    }
}

void ChatItem::continueSelecting(const QPointF& pos)
{
    _selectionMode = PartialSelection;
    _selectionEnd = posToCursor(pos);
    chatLine()->update();
}

bool ChatItem::isPosOverSelection(const QPointF& pos) const
{
    if (_selectionMode == FullSelection)
        return true;
    if (_selectionMode == PartialSelection) {
        int cursor = posToCursor(pos);
        return cursor >= qMin(_selectionStart, _selectionEnd) && cursor <= qMax(_selectionStart, _selectionEnd);
    }
    return false;
}

bool ChatItem::hasActiveClickable() const
{
    return false;
}

std::pair<quint16, quint16> ChatItem::activeClickableRange() const
{
    return {};
}

QList<QRectF> ChatItem::findWords(const QString& searchWord, Qt::CaseSensitivity caseSensitive)
{
    QList<QRectF> resultList;
    const QAbstractItemModel* model_ = model();
    if (!model_)
        return resultList;

    QString plainText = model_->data(model_->index(row(), column()), MessageModel::DisplayRole).toString();
    QList<int> indexList;
    int searchIdx = plainText.indexOf(searchWord, 0, caseSensitive);
    while (searchIdx != -1) {
        indexList << searchIdx;
        searchIdx = plainText.indexOf(searchWord, searchIdx + 1, caseSensitive);
    }

    foreach (int idx, indexList) {
        QTextLine line = layout()->lineForTextPosition(idx);
        qreal x = line.cursorToX(idx);
        qreal width = line.cursorToX(idx + searchWord.count()) - x;
        qreal height = line.height();
        qreal y = height * line.lineNumber();
        resultList << QRectF(x, y, width, height);
    }

    return resultList;
}

void ChatItem::handleClick(const QPointF& pos, ChatScene::ClickMode clickMode)
{
    // single clicks are already handled by the scene (for clearing the selection)
    if (clickMode == ChatScene::DragStartClick) {
        chatScene()->setSelectingItem(this);
        _selectionStart = _selectionEnd = posToCursor(pos);
        _selectionMode = NoSelection;  // will be set to PartialSelection by mouseMoveEvent
        chatLine()->update();
    }
}

void ChatItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->buttons() == Qt::LeftButton) {
        if (boundingRect().contains(event->pos())) {
            qint16 end = posToCursor(event->pos());
            if (end != _selectionEnd) {
                _selectionEnd = end;
                _selectionMode = (_selectionStart != _selectionEnd ? PartialSelection : NoSelection);
                chatLine()->update();
            }
        }
        else {
            setFullSelection();
            chatScene()->startGlobalSelection(this, event->pos());
        }
        event->accept();
    }
    else {
        event->ignore();
    }
}

void ChatItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->buttons() == Qt::LeftButton)
        event->accept();
    else
        event->ignore();
}

void ChatItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (_selectionMode != NoSelection && event->button() == Qt::LeftButton) {
        chatScene()->selectionToClipboard(QClipboard::Selection);
        event->accept();
    }
    else
        event->ignore();
}

void ChatItem::addActionsToMenu(QMenu* menu, const QPointF& pos)
{
    Q_UNUSED(pos);

    GraphicalUi::contextMenuActionProvider()->addActions(menu, chatScene()->filter(), data(MessageModel::BufferIdRole).value<BufferId>());
}

// ************************************************************
// SenderChatItem
// ************************************************************

void SenderChatItem::initLayout(QTextLayout* layout) const
{
    initLayoutHelper(layout, QTextOption::ManualWrap, Qt::AlignRight);
    doLayout(layout);
}

void SenderChatItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    painter->save();
    painter->setClipRect(boundingRect());
    paintBackground(painter);

    qreal layoutWidth = layout()->minimumWidth();
    qreal offset = 0;
    if (chatScene()->senderCutoffMode() == ChatScene::CutoffLeft)
        offset = qMin(width() - layoutWidth, (qreal)0);
    else
        offset = qMax(layoutWidth - width(), (qreal)0);

    if (layoutWidth > width()) {
        // Draw a nice gradient for longer items
        // Qt's text drawing with a gradient brush sucks, so we use compositing instead
        QPixmap pixmap(layout()->boundingRect().toRect().size());
        pixmap.fill(Qt::transparent);

        QPainter pixPainter(&pixmap);
        layout()->draw(&pixPainter, QPointF(qMax(offset, (qreal)0), 0), additionalFormats());

        // Create alpha channel mask
        QLinearGradient gradient;
        if (offset < 0) {
            gradient.setStart(0, 0);
            gradient.setFinalStop(12, 0);
            gradient.setColorAt(0, Qt::transparent);
            gradient.setColorAt(1, Qt::white);
        }
        else {
            gradient.setStart(width() - 10, 0);
            gradient.setFinalStop(width(), 0);
            gradient.setColorAt(0, Qt::white);
            gradient.setColorAt(1, Qt::transparent);
        }
        pixPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);  // gradient's alpha gets applied to the pixmap
        pixPainter.fillRect(pixmap.rect(), gradient);
        painter->drawPixmap(pos(), pixmap);
    }
    else {
        layout()->draw(painter, pos(), additionalFormats(), boundingRect());
    }
    painter->restore();
}

void SenderChatItem::handleClick(const QPointF& pos, ChatScene::ClickMode clickMode)
{
    if (clickMode == ChatScene::DoubleClick) {
        BufferInfo curBufInfo = Client::networkModel()->bufferInfo(data(MessageModel::BufferIdRole).value<BufferId>());
        QString nick = data(MessageModel::EditRole).toString();
        // check if the nick is a valid ircUser
        if (!nick.isEmpty() && Client::network(curBufInfo.networkId())->ircUser(nick))
            Client::bufferModel()->switchToOrStartQuery(curBufInfo.networkId(), nick);
    }
    else
        ChatItem::handleClick(pos, clickMode);
}

// ************************************************************
// ContentsChatItem
// ************************************************************

ContentsChatItem::ActionProxy ContentsChatItem::_actionProxy;

ContentsChatItem::ContentsChatItem(const QPointF& pos, const qreal& width, ChatLine* parent)
    : ChatItem(QRectF(pos, QSizeF(width, 0)), parent)
    , _data(nullptr)
{
    setPos(pos);
    setGeometryByWidth(width);
}

QFontMetricsF* ContentsChatItem::fontMetrics() const
{
    return QtUi::style()->fontMetrics(data(ChatLineModel::FormatRole).value<UiStyle::FormatList>().at(0).second.type,
                                      UiStyle::MessageLabel::None);
}

ContentsChatItem::~ContentsChatItem()
{
    delete _data;
}

void ContentsChatItem::clearCache()
{
    delete _data;
    _data = nullptr;
    ChatItem::clearCache();
}

ContentsChatItemPrivate* ContentsChatItem::privateData() const
{
    if (!_data) {
        auto* that = const_cast<ContentsChatItem*>(this);
        that->_data = new ContentsChatItemPrivate(ClickableList::fromString(data(ChatLineModel::DisplayRole).toString()), that);
    }
    return _data;
}

qreal ContentsChatItem::setGeometryByWidth(qreal w)
{
    // We use this for reloading layout info as well, so we can't bail out if the width doesn't change

    // compute height
    int lines = 1;
    WrapColumnFinder finder(this);
    while (finder.nextWrapColumn(w) > 0)
        lines++;
    qreal spacing = qMax(fontMetrics()->lineSpacing(), fontMetrics()->height());  // cope with negative leading()
    qreal h = lines * spacing;
    delete _data;
    _data = nullptr;

    if (w != width() || h != height())
        setGeometry(w, h);

    return h;
}

void ContentsChatItem::initLayout(QTextLayout* layout) const
{
    initLayoutHelper(layout, QTextOption::WrapAtWordBoundaryOrAnywhere);
    doLayout(layout);
}

void ContentsChatItem::doLayout(QTextLayout* layout) const
{
    ChatLineModel::WrapList wrapList = data(ChatLineModel::WrapListRole).value<ChatLineModel::WrapList>();
    if (!wrapList.count())
        return;  // empty chatitem

    qreal h = 0;
    qreal spacing = qMax(fontMetrics()->lineSpacing(), fontMetrics()->height());  // cope with negative leading()
    WrapColumnFinder finder(this);
    layout->beginLayout();
    forever
    {
        QTextLine line = layout->createLine();
        if (!line.isValid())
            break;

        int col = finder.nextWrapColumn(width());
        if (col < 0)
            col = layout->text().length();
        int num = col - line.textStart();

        line.setNumColumns(num);

        // Sometimes, setNumColumns will create a line that's too long (cf. Qt bug 238249)
        // We verify this and try setting the width again, making it shorter each time until the lengths match.
        // Dead fugly, but seems to work…
        for (int i = line.textLength() - 1; i >= 0 && line.textLength() > num; i--) {
            line.setNumColumns(i);
        }
        if (num != line.textLength()) {
            qWarning() << "WARNING: Layout engine couldn't workaround Qt bug 238249, please report!";
            // qDebug() << num << line.textLength() << t.mid(line.textStart(), line.textLength()) << t.mid(line.textStart() + line.textLength());
        }

        line.setPosition(QPointF(0, h));
        h += spacing;
    }
    layout->endLayout();
}

bool ContentsChatItem::hasActiveClickable() const
{
    return privateData()->currentClickable.isValid();
}

std::pair<quint16, quint16> ContentsChatItem::activeClickableRange() const
{
    const auto& clickable = privateData()->currentClickable;
    if (clickable.isValid()) {
        return {clickable.start(), clickable.start() + clickable.length()};
    }
    return {};
}

Clickable ContentsChatItem::clickableAt(const QPointF& pos) const
{
    return privateData()->clickables.atCursorPos(posToCursor(pos));
}

UiStyle::FormatList ContentsChatItem::formatList() const
{
    UiStyle::FormatList fmtList = ChatItem::formatList();
    for (size_t i = 0; i < privateData()->clickables.size(); i++) {
        Clickable click = privateData()->clickables.at(i);
        if (click.type() == Clickable::Url) {
            overlayFormat(fmtList, click.start(), click.start() + click.length(), UiStyle::FormatType::Url);
        }
    }
    return fmtList;
}

void ContentsChatItem::endHoverMode()
{
    if (privateData()) {
        if (privateData()->currentClickable.isValid()) {
            chatLine()->unsetCursor();
            privateData()->currentClickable = Clickable();
        }
        clearWebPreview();
        chatLine()->update();
    }
}

void ContentsChatItem::handleClick(const QPointF& pos, ChatScene::ClickMode clickMode)
{
    if (clickMode == ChatScene::SingleClick) {
        qint16 idx = posToCursor(pos);
        Clickable foo = privateData()->clickables.atCursorPos(idx);
        if (foo.isValid()) {
            NetworkId networkId = Client::networkModel()->networkId(data(MessageModel::BufferIdRole).value<BufferId>());
            QString text = data(ChatLineModel::DisplayRole).toString();
            foo.activate(networkId, text);
        }
    }
    else if (clickMode == ChatScene::DoubleClick) {
        chatScene()->setSelectingItem(this);
        setSelectionMode(PartialSelection);
        Clickable click = clickableAt(pos);
        if (click.isValid()) {
            setSelectionStart(click.start());
            setSelectionEnd(click.start() + click.length());
        }
        else {
            // find word boundary
            QString str = data(ChatLineModel::DisplayRole).toString();
            qint16 cursor = posToCursor(pos);
            qint16 start = str.lastIndexOf(QRegExp("\\W"), cursor) + 1;
            qint16 end = qMin(str.indexOf(QRegExp("\\W"), cursor), str.length());
            if (end < 0)
                end = str.length();
            setSelectionStart(start);
            setSelectionEnd(end);
        }
        chatLine()->update();
    }
    else if (clickMode == ChatScene::TripleClick) {
        setSelection(PartialSelection, 0, data(ChatLineModel::DisplayRole).toString().length());
    }
    ChatItem::handleClick(pos, clickMode);
}

void ContentsChatItem::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    // mouse move events always mean we're not hovering anymore...
    endHoverMode();
    ChatItem::mouseMoveEvent(event);
}

void ContentsChatItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event)
{
    endHoverMode();
    event->accept();
}

void ContentsChatItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event)
{
    bool onClickable = false;
    Clickable click = clickableAt(event->pos());
    if (click.isValid()) {
        if (click.type() == Clickable::Url) {
            onClickable = true;
            showWebPreview(click);
        }
        else if (click.type() == Clickable::Channel) {
            QString name = data(ChatLineModel::DisplayRole).toString().mid(click.start(), click.length());
            // don't make clickable if it's our own name
            BufferId myId = data(MessageModel::BufferIdRole).value<BufferId>();
            if (Client::networkModel()->bufferName(myId) != name)
                onClickable = true;
        }
        if (onClickable) {
            chatLine()->setCursor(Qt::PointingHandCursor);
            privateData()->currentClickable = click;
            chatLine()->update();
            return;
        }
    }
    if (!onClickable)
        endHoverMode();
    event->accept();
}

void ContentsChatItem::addActionsToMenu(QMenu* menu, const QPointF& pos)
{
    if (privateData()->currentClickable.isValid()) {
        Clickable click = privateData()->currentClickable;
        switch (click.type()) {
        case Clickable::Url: {
            privateData()->activeClickable = click;
            auto action = new Action{icon::get("edit-copy"), tr("Copy Link Address"), menu, &_actionProxy, &ActionProxy::copyLinkToClipboard};
            action->setData(QVariant::fromValue(static_cast<void*>(this)));
            menu->addAction(action);
            break;
        }
        case Clickable::Channel: {
            // Remove existing menu actions, they confuse us when right-clicking on a clickable
            menu->clear();
            QString name = data(ChatLineModel::DisplayRole).toString().mid(click.start(), click.length());
            GraphicalUi::contextMenuActionProvider()->addActions(menu,
                                                                 chatScene()->filter(),
                                                                 data(MessageModel::BufferIdRole).value<BufferId>(),
                                                                 name);
            break;
        }
        default:
            break;
        }
    }
    else {
        // Buffer-specific actions
        ChatItem::addActionsToMenu(menu, pos);
    }
}

void ContentsChatItem::copyLinkToClipboard()
{
    Clickable click = privateData()->activeClickable;
    if (click.isValid() && click.type() == Clickable::Url) {
        QString url = data(ChatLineModel::DisplayRole).toString().mid(click.start(), click.length());
        if (!url.contains("://"))
            url = "http://" + url;
        chatScene()->stringToClipboard(url);
    }
}

/******** WEB PREVIEW *****************************************************************************/

void ContentsChatItem::showWebPreview(const Clickable& click)
{
#if !defined HAVE_WEBENGINE
    Q_UNUSED(click);
#else
    QTextLine line = layout()->lineForTextPosition(click.start());
    qreal x = line.cursorToX(click.start());
    qreal width = line.cursorToX(click.start() + click.length()) - x;
    qreal height = line.height();
    qreal y = height * line.lineNumber();

    QPointF topLeft = mapToScene(pos()) + QPointF(x, y);
    QRectF urlRect = QRectF(topLeft.x(), topLeft.y(), width, height);

    QString urlstr = data(ChatLineModel::DisplayRole).toString().mid(click.start(), click.length());
    if (!urlstr.contains("://"))
        urlstr = "http://" + urlstr;
    QUrl url = QUrl::fromEncoded(urlstr.toUtf8(), QUrl::TolerantMode);
    chatScene()->loadWebPreview(this, url, urlRect);
#endif
}

void ContentsChatItem::clearWebPreview()
{
#if defined HAVE_WEBENGINE
    chatScene()->clearWebPreview(this);
#endif
}

/*************************************************************************************************/

ContentsChatItem::WrapColumnFinder::WrapColumnFinder(const ChatItem* _item)
    : item(_item)
    , wrapList(item->data(ChatLineModel::WrapListRole).value<ChatLineModel::WrapList>())
    , wordidx(0)
    , lineCount(0)
    , choppedTrailing(0)
{}

qint16 ContentsChatItem::WrapColumnFinder::nextWrapColumn(qreal width)
{
    if (wordidx >= wrapList.count())
        return -1;

    lineCount++;
    qreal targetWidth = lineCount * width + choppedTrailing;

    qint16 start = wordidx;
    qint16 end = wrapList.count() - 1;

    // check if the whole line fits
    if (wrapList.at(end).endX <= targetWidth)  //  || start == end)
        return -1;

    // check if we have a very long word that needs inter word wrap
    if (wrapList.at(start).endX > targetWidth) {
        if (!line.isValid()) {
            item->initLayoutHelper(&layout, QTextOption::NoWrap);
            layout.beginLayout();
            line = layout.createLine();
            layout.endLayout();
        }
        return line.xToCursor(targetWidth, QTextLine::CursorOnCharacter);
    }

    while (true) {
        if (start + 1 == end) {
            wordidx = end;
            const ChatLineModel::Word& lastWord = wrapList.at(start);  // the last word we were able to squeeze in

            // both cases should be cought preliminary
            Q_ASSERT(lastWord.endX <= targetWidth);  // ensure that "start" really fits in
            Q_ASSERT(end < wrapList.count());        // ensure that start isn't the last word

            choppedTrailing += lastWord.trailing - (targetWidth - lastWord.endX);
            return wrapList.at(wordidx).start;
        }

        qint16 pivot = (end + start) / 2;
        if (wrapList.at(pivot).endX > targetWidth) {
            end = pivot;
        }
        else {
            start = pivot;
        }
    }
    Q_ASSERT(false);
    return -1;
}

/*************************************************************************************************/
