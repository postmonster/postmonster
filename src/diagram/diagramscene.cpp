/* PostMonster, universal HTTP automation tool
 * Copyright (C) 2015 by Paul Artsishevsky <polter.rnd@gmail.com>
 *
 * This file is part of PostMonster.
 *
 * PostMonster is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * PostMonster is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with PostMonster.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include "diagramscene.h"

#include <QGraphicsSceneMouseEvent>
#include <QtDebug>
#include <QMimeData>
#include <QPainter>
#include <QTimer>

#include "common.h"
#include "arrow.h"
#include "startitem.h"
#include "taskitem.h"

DiagramScene::DiagramScene(QObject *parent)
    : QGraphicsScene(parent), m_currentItem(nullptr)
{
    m_mode = InsertItem;
}

void DiagramScene::setMode(Mode mode, PostMonster::ToolPluginInterface *tool)
{
    m_mode = mode;
    m_tool = tool;
}

DiagramScene::Mode DiagramScene::mode()
{
    return m_mode;
}

void DiagramScene::setCurrentItem(DiagramItem *item)
{
    if (m_currentItem)
        m_currentItem->setCurrent(false);

    if (item)
        item->setCurrent(true);

    m_currentItem = item;
    emit currentItemChanged();
}

DiagramItem *DiagramScene::currentItem()
{
    return m_currentItem;
}

void DiagramScene::dragMoveEvent(QGraphicsSceneDragDropEvent *event)
{
    event->acceptProposedAction();
}

void DiagramScene::dragEnterEvent(QGraphicsSceneDragDropEvent *event)
{
    if (event->mimeData()->hasFormat("application/vnd.request.row"))
        event->acceptProposedAction();
}

void DiagramScene::dropEvent(QGraphicsSceneDragDropEvent *event)
{
    emit requestDropped(event->mimeData()->data("application/vnd.request.row").toInt(),
                        event->scenePos());
    event->acceptProposedAction();
}

DiagramItem *DiagramScene::drawStart()
{
    DiagramItem *item = new StartItem();
    addItem(item);

    return item;
}

void DiagramScene::menuConnect()
{
    if (m_mode == InsertLine)
        return;

    m_prevMode = m_mode;
    m_mode = InsertLine;
    m_lineStatus = PostMonster::Default;

    m_line = new QGraphicsLineItem(QLineF(m_clickPos, m_clickPos));
    m_line->setPen(QPen(Qt::gray, 1.2, Qt::DashLine));
    addItem(m_line);
}

void DiagramScene::menuConnectOk()
{
    menuConnect();
    m_lineStatus = PostMonster::Ok;
}

void DiagramScene::menuConnectFail()
{
    menuConnect();
    m_lineStatus = PostMonster::Fail;
}

void DiagramScene::menuDisconnectOk()
{
    removeLine(PostMonster::Ok);
}

void DiagramScene::menuDisconnectFail()
{
    removeLine(PostMonster::Fail);
}

void DiagramScene::removeLine(PostMonster::TaskStatus status)
{
    /*DiagramItem *diagramItem = itemFromAction(qobject_cast<const QAction *>(sender()));
    if (diagramItem && !diagramItem->isSelected()) {
        diagramItem->removeArrow(status);
    } else {*/
        foreach (QGraphicsItem *item, selectedItems()) {
            DiagramItem *diagramItem = qgraphicsitem_cast<DiagramItem *>(item);
            if (diagramItem)
                diagramItem->removeArrow(status);
        }
    //}
}

DiagramItem *DiagramScene::itemFromAction(const QAction *action)
{
    DiagramItem *result = nullptr;

    if (action) {
        std::function<QUuid (const QWidget *widget)> findUUID = [&findUUID](const QWidget *widget) -> QUuid {
            QUuid uuid = widget->property("itemUUID").toUuid();
            QWidget *parent = widget->parentWidget();

            if (uuid.isNull())
                return findUUID(parent);

            return uuid;
        };

        QUuid uuid;
        foreach (const QWidget *widget, action->associatedWidgets()) {
            uuid = findUUID(widget);
            if (!uuid.isNull())
                break;
        }

        foreach (QGraphicsItem *item, items()) {
            DiagramItem *diagramItem = qgraphicsitem_cast<DiagramItem *>(item);

            if (diagramItem && diagramItem->uuid() == uuid)
               result = diagramItem;
        }
    }

    return result;
}


void DiagramScene::insertItem(DiagramItem *item)
{
    QMenu *menu = new QMenu;
    QMenu *connectMenu, *disconnectMenu;
    QAction *action;

    switch (item->diagramType()) {
    case DiagramItem::TypeTask:
        connectMenu = menu->addMenu(QIcon(":/icons/connect"), tr("Connnect"));
        disconnectMenu = menu->addMenu(QIcon(":/icons/disconnect"), tr("Disconnect"));

        foreach (PostMonster::TaskStatus status, static_cast<TaskItem *>(item)->tool()->statuses()) {
            if (status == PostMonster::Ok) {
                connectMenu->addAction(QIcon(":/icons/flag-green"), tr("Success"), this, SLOT(menuConnectOk()));
                disconnectMenu->addAction(QIcon(":/icons/flag-green"), tr("Success"), this, SLOT(menuDisconnectOk()));
            } else if (status == PostMonster::Fail) {
                connectMenu->addAction(QIcon(":/icons/flag-red"), tr("Failure"), this, SLOT(menuConnectFail()));
                disconnectMenu->addAction(QIcon(":/icons/flag-red"), tr("Failure"), this, SLOT(menuDisconnectFail()));
            }
        }

        menu->addAction(QIcon(":/icons/remove"), tr("Delete"), this, SLOT(menuDelete()));

        connect(static_cast<TaskItem *>(item)->task(), &PostMonster::TaskInterface::dataChanged,
                [this, item]() {
                    static_cast<TaskItem *>(item)->updatePixmap();
                    item->update();
                }
        );

        break;

    case DiagramItem::TypeStart:
        menu->addAction(QIcon(":/icons/connect"), tr("Connect"), this, SLOT(menuConnect()));
        menu->addAction(QIcon(":/icons/disconnect"), tr("Disconnect"), this, SLOT(menuDisconnect()));
        break;
    }

    menu->addSeparator();
    menu->addAction(QIcon(":/icons/to-front"), tr("To front"), this, SLOT(menuToFront()));
    menu->addAction(QIcon(":/icons/to-back"), tr("To back"), this, SLOT(menuToBack()));
    menu->addSeparator();

    action = menu->addAction(tr("Breakpoint"));
    action->setCheckable(true);
    connect(action, &QAction::triggered, [action, item]() {
        item->setBreakpoint(action->isChecked());
    });

    connect(menu, &QMenu::aboutToShow, [action, item]() {
        action->setChecked(item->hasBreakpoint());
    });

    item->setMenu(menu);

    addItem(item);
    emit itemInserted(item);
}

void DiagramScene::destroyItem(QGraphicsItem *item)
{
    DiagramItem *diagramItem = qgraphicsitem_cast<DiagramItem *>(item);
    if (diagramItem) {
        diagramItem->removeArrows();

        QHash<Arrow *, DiagramItem *> arrows;
        foreach (QGraphicsItem *i, items()) {
            DiagramItem *di = qgraphicsitem_cast<DiagramItem *>(i);
            if (!di) continue;

            for (QList<Arrow *>::const_iterator j = di->arrows()->constBegin(),
                 end = di->arrows()->constEnd(); j != end; ++j)
                if ((*j)->endItem() == item)
                    arrows[(*j)] = di;

        }

        for (QHash<Arrow *, DiagramItem *>::iterator i = arrows.begin(),
             end = arrows.end(); i != end; ++i)
            (*i)->removeArrow(i.key());

        if (diagramItem->diagramType() == DiagramItem::TypeTask) {
            TaskItem *taskItem = static_cast<TaskItem *>(diagramItem);
            taskItem->tool()->destroyTask(taskItem->task());
        }
    }

    removeItem(item);
    delete item;
}

void DiagramScene::menuDelete()
{
    /*DiagramItem *diagramItem = itemFromAction(qobject_cast<const QAction *>(sender()));
    if (diagramItem && !diagramItem->isSelected()) {
        destroyItem(diagramItem);
    } else {*/
    QList<QGraphicsItem *> selected = selectedItems();
    clearSelection();

    foreach (QGraphicsItem *item, selected)
        destroyItem(item);
    //}
}

void DiagramScene::menuDisconnect()
{
    /*DiagramItem *diagramItem = itemFromAction(qobject_cast<const QAction *>(sender()));

    if (diagramItem && !diagramItem->isSelected()) {
        diagramItem->removeArrows();
    } else {*/
        foreach (QGraphicsItem *item, selectedItems()) {
            DiagramItem *diagramItem = qgraphicsitem_cast<DiagramItem *>(item);
            if (diagramItem)
                diagramItem->removeArrows();
        }
    //}
}

void DiagramScene::destroyItems()
{
    while (!items().empty())
        destroyItem(items().first());
}

void DiagramScene::menuToFront()
{
    /*DiagramItem *diagramItem = itemFromAction(qobject_cast<const QAction *>(sender()));
    if (diagramItem && !diagramItem->isSelected()) {
        diagramItem->toFront();
    } else {*/
        foreach (QGraphicsItem *item, selectedItems()) {
            DiagramItem *diagramItem = qgraphicsitem_cast<DiagramItem *>(item);
            if (diagramItem)
                diagramItem->toFront();
        }
    //}
}

void DiagramScene::menuToBack()
{
    /*DiagramItem *diagramItem = itemFromAction(qobject_cast<const QAction *>(sender()));
    if (diagramItem && !diagramItem->isSelected()) {
        diagramItem->toBack();
    } else {*/
        foreach (QGraphicsItem *item, selectedItems()) {
            DiagramItem *diagramItem = qgraphicsitem_cast<DiagramItem *>(item);
            if (diagramItem)
                diagramItem->toBack();
        }
    //}
}

void DiagramScene::insertArrow(DiagramItem *start, DiagramItem *end)
{
    Arrow *arrow = new Arrow(m_lineStatus, start, end);

    // If there is and opposite line with the same status,
    // make new line transparent, otherwise the line will look bold
    for (QList<Arrow *>::const_iterator i = end->arrows()->constBegin(),
         e = end->arrows()->constEnd(); i != e; ++i) {
        if ((*i)->status() == m_lineStatus && (*i)->endItem() == start) {
            arrow->setPen(QPen(Qt::transparent));
            break;
        }
    }

    start->addArrow(arrow);
    arrow->setZValue(-1000.0);

    addItem(arrow);
    arrow->updatePosition();
}

void DiagramScene::insertArrow(PostMonster::TaskStatus status, DiagramItem *start, DiagramItem *end)
{
    m_lineStatus = status;
    insertArrow(start, end);
}

void DiagramScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    m_clickPos = event->scenePos();
    if (event->button() != Qt::LeftButton)
        return;

    DiagramItem *item;
    switch (m_mode) {
    case InsertItem:
        item = new TaskItem(m_tool->createTask());
        item->setPos(event->scenePos());
        item->setSelected(true);

        clearSelection();
        insertItem(item);
        return;

    case InsertLine:
        if (m_line != 0 && m_mode == InsertLine) {
            QList<QGraphicsItem *> startItems = items(m_line->line().p1());
            if (startItems.count() && startItems.first() == m_line)
                startItems.removeFirst();
            QList<QGraphicsItem *> endItems = items(m_line->line().p2());
            if (endItems.count() && endItems.first() == m_line)
                endItems.removeFirst();

            removeItem(m_line);
            delete m_line;
            if (startItems.count() > 0 && endItems.count() > 0 &&
                startItems.first() != endItems.first()) {
                DiagramItem *startItem = qgraphicsitem_cast<DiagramItem *>(startItems.first());
                DiagramItem *endItem = qgraphicsitem_cast<DiagramItem *>(endItems.first());

                if (startItem && endItem) {
                    if (startItem->diagramType() == DiagramItem::TypeStart)
                        startItem->removeArrows();

                    insertArrow(startItem, endItem);
                }
            }
        }

        m_line = 0;
        m_mode = m_prevMode;
        break;

    default:
        if (!itemAt(event->scenePos(), QTransform()))
            emit clickedOnBackground();
    }

    return QGraphicsScene::mousePressEvent(event);
}

void DiagramScene::mouseMoveEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    if (m_mode == InsertLine && m_line != 0) {
        QLineF newLine(m_line->line().p1(), mouseEvent->scenePos());
        m_line->setLine(newLine);
    } else if (m_mode == MoveItem) {
        QGraphicsScene::mouseMoveEvent(mouseEvent);
    }
}

void DiagramScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    QGraphicsScene::mouseReleaseEvent(mouseEvent);
}

void DiagramScene::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    DiagramItem *item = qgraphicsitem_cast<DiagramItem *>(itemAt(event->scenePos(), QTransform()));
    if (!item)
        return;

    setCurrentItem(item->isCurrent() ? nullptr : item);
}

void DiagramScene::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    if (m_mode != InsertLine) {
        QGraphicsScene::contextMenuEvent(event);
    }
}
