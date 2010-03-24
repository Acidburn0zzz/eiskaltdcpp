#ifndef TOOLBAR_H
#define TOOLBAR_H

#include <QToolBar>
#include <QTabBar>
#include <QEvent>
#include <QShowEvent>

#include "ArenaWidget.h"

class ArenaWidget;
class MainWindow;

class ToolBar :
    public QToolBar
{
    Q_OBJECT

    typedef QMap<ArenaWidget*, unsigned> WidgetMap;

public:
    ToolBar(QWidget* = NULL);
    virtual ~ToolBar();

    void insertWidget(ArenaWidget*, bool keepFocus = false);
    void removeWidget(ArenaWidget*);
    void redraw();
    void initTabs();

    bool hasWidget(ArenaWidget*);
    void mapWidget(ArenaWidget*);

protected:
    virtual bool eventFilter(QObject *, QEvent *);
    virtual void showEvent(QShowEvent *);

public Q_SLOTS:
    void mapped(ArenaWidget*);
    void nextTab();
    void prevTab();

private Q_SLOTS:
    void slotIndexChanged(int);
    void slotTabMoved(int, int);
    void slotClose(int);
    void slotContextMenu(const QPoint&);

    QString compactToolTipText(QString text);

private:
    ArenaWidget *findWidgetForIndex(int);
    void rebuildIndexes(int);

    QTabBar *tabbar;
    WidgetMap map;
};

#endif // TOOLBAR_H
