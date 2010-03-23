#include "PublicHubs.h"
#include "MainWindow.h"
#include "WulforSettings.h"

#include <QApplication>
#include <QClipboard>
#include <QItemSelectionModel>

using namespace dcpp;

PublicHubs::PublicHubs(QWidget *parent) :
    QWidget(parent)
{
    setupUi(this);

    setUnload(false);

    model = new PublicHubModel();

    treeView->setModel(model);
    treeView->header()->restoreState(QByteArray::fromBase64(WSGET(WS_PUBLICHUBS_STATE).toAscii()));

    FavoriteManager::getInstance()->addListener(this);

    MainWindow *MW = MainWindow::getInstance();
    MW->addArenaWidget(this);

    entries = FavoriteManager::getInstance()->getPublicHubs();

    updateList();

    if(FavoriteManager::getInstance()->isDownloading()) {
        label_STATUS->setText(tr("Downloading public hub list..."));
    } else if(entries.empty()) {
        FavoriteManager::getInstance()->refresh();
    }

    treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    treeView->header()->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(treeView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotContextMenu()));
    connect(treeView->header(), SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(slotHeaderMenu()));
}

PublicHubs::~PublicHubs(){
    delete model;

    FavoriteManager::getInstance()->removeListener(this);
}

void PublicHubs::closeEvent(QCloseEvent *e){
    if (isUnload()){
        MainWindow::getInstance()->remArenaWidgetFromToolbar(this);
        MainWindow::getInstance()->remWidgetFromArena(this);
        MainWindow::getInstance()->remArenaWidget(this);

        WSSET(WS_PUBLICHUBS_STATE, treeView->header()->saveState().toBase64());

        e->accept();
    }
    else {
        MainWindow::getInstance()->remArenaWidgetFromToolbar(this);
        MainWindow::getInstance()->remWidgetFromArena(this);

        e->ignore();
    }
}

void PublicHubs::customEvent(QEvent *e){
    if (e->type() == PublicHubsCustomEvent::Event){
        PublicHubsCustomEvent *c_e = reinterpret_cast<PublicHubsCustomEvent*>(e);

        c_e->func()->call();
    }

    e->accept();
}

void PublicHubs::setStatus(QString stat){
    label_STATUS->setText(stat);
}

void PublicHubs::updateList(){
    if (!model)
        return;

    model->clearModel();
    QList<QVariant> data;

    for(HubEntryList::const_iterator i = entries.begin(); i != entries.end(); ++i) {
        HubEntry *entry = const_cast<HubEntry*>(&(*i));
        data.clear();

        data << _q(entry->getName())         << _q(entry->getDescription())  << entry->getUsers()
             << _q(entry->getServer())       << _q(entry->getCountry())      << (qulonglong)entry->getShared()
             << (qint64)entry->getMinShare() << (qint64)entry->getMinSlots() << (qint64)entry->getMaxHubs()
             << (qint64)entry->getMaxUsers() << static_cast<double>(entry->getReliability()) << _q(entry->getRating());

        model->addResult(data, entry);
    }
}

void PublicHubs::onFinished(QString stat){
    setStatus(stat);

    entries = FavoriteManager::getInstance()->getPublicHubs();

    updateList();
}

void PublicHubs::slotContextMenu(){
    QItemSelectionModel *sel_model = treeView->selectionModel();
    QModelIndexList indexes = sel_model->selectedRows(0);

    if (indexes.isEmpty())
        return;

    WulforUtil *WU = WulforUtil::getInstance();

    QMenu *m = new QMenu();
    QAction *connect = new QAction(WU->getPixmap(WulforUtil::eiCONNECT), tr("Connect"), m);
    QAction *add_fav = new QAction(WU->getPixmap(WulforUtil::eiBOOKMARK_ADD), tr("Add to favorites"), m);
    QAction *copy    = new QAction(WU->getPixmap(WulforUtil::eiEDITCOPY), tr("Copy &address to clipboard"), m);

    m->addActions(QList<QAction*>() << connect << add_fav << copy);

    QAction *ret = m->exec(QCursor::pos());

    m->deleteLater();

    if (ret == connect){
        PublicHubItem * item = NULL;
        MainWindow *MW = MainWindow::getInstance();

        foreach (QModelIndex i, indexes){
            item = reinterpret_cast<PublicHubItem*>(i.internalPointer());

            if (item)
                MW->newHubFrame(item->data(COLUMN_PHUB_ADDRESS).toString(), "");

            item = NULL;
        }
    }
    else if (ret == add_fav){
        PublicHubItem * item = NULL;

        foreach (QModelIndex i, indexes){
            item = reinterpret_cast<PublicHubItem*>(i.internalPointer());

            if (item && item->entry){
                try{
                    FavoriteManager::getInstance()->addFavorite(*item->entry);
                }
                catch (const std::exception&){}
            }

            item = NULL;
        }
    }
    else if (ret == copy){
        PublicHubItem * item = NULL;
        QString out = "";

        foreach (QModelIndex i, indexes){
            item = reinterpret_cast<PublicHubItem*>(i.internalPointer());

            if (item)
                out += item->data(COLUMN_PHUB_ADDRESS).toString() + "\n";

            item = NULL;
        }

        if (!out.isEmpty())
            qApp->clipboard()->setText(out, QClipboard::Clipboard);
    }
}

void PublicHubs::slotHeaderMenu(){
    WulforUtil::headerMenu(treeView);
}

void PublicHubs::on(DownloadStarting, const std::string& l) throw(){
    typedef Func1<PublicHubs, QString> FUNC;
    FUNC *f = new FUNC(this, &PublicHubs::setStatus, tr("Downloading public hub list... (%1)").arg(_q(l)));

    QApplication::postEvent(this, new PublicHubsCustomEvent(f));
}

void PublicHubs::on(DownloadFailed, const std::string& l) throw(){
    typedef Func1<PublicHubs, QString> FUNC;
    FUNC *f = new FUNC(this, &PublicHubs::setStatus, tr("Download failed: %1").arg(_q(l)));

    QApplication::postEvent(this, new PublicHubsCustomEvent(f));
}

void PublicHubs::on(DownloadFinished, const std::string& l) throw(){
    typedef Func1<PublicHubs, QString> FUNC;
    FUNC *f = new FUNC(this, &PublicHubs::onFinished, tr("Hub list downloaded... (%1)").arg(_q(l)));

    QApplication::postEvent(this, new PublicHubsCustomEvent(f));
}

void PublicHubs::on(LoadedFromCache, const std::string& l) throw(){
    typedef Func1<PublicHubs, QString> FUNC;
    FUNC *f = new FUNC(this, &PublicHubs::onFinished, tr("Hub list loaded from cache...").arg(_q(l)));

    QApplication::postEvent(this, new PublicHubsCustomEvent(f));
}
