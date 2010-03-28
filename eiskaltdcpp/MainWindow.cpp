#include "MainWindow.h"

#include <stdlib.h>
#include <string>
#include <iostream>

#include <QPushButton>
#include <QSize>
#include <QModelIndex>
#include <QItemSelectionModel>
#include <QtDebug>
#include <QTextCodec>
#include <QMessageBox>
#include <QKeyEvent>
#include <QFileDialog>
#include <QProgressBar>
#include <QFileDialog>
#include <QRegExp>
#ifdef FREE_SPACE_BAR
    #include <boost/filesystem.hpp>
#endif //FREE_SPACE_BAR
#ifdef FREE_SPACE_BAR_C
    #ifdef WIN32
        #include <io.h>
    #else //WIN32
        extern "C" {
        #include "gnulib/fsusage.h"
        }
    #endif //WIN32
#endif
#include "HubFrame.h"
#include "HubManager.h"
#include "HashProgress.h"
#include "PMWindow.h"
#include "TransferView.h"
#include "ShareBrowser.h"
#include "QuickConnect.h"
#include "SearchFrame.h"
#include "Settings.h"
#include "FavoriteHubs.h"
#include "PublicHubs.h"
#include "FavoriteUsers.h"
#include "DownloadQueue.h"
#include "FinishedTransfers.h"
#include "AntiSpamFrame.h"
#include "IPFilterFrame.h"
#include "ToolBar.h"
#include "Magnet.h"
#include "SpyFrame.h"

#include "UPnPMapper.h"
#include "WulforSettings.h"
#include "WulforUtil.h"

#include "Version.h"

using namespace std;

MainWindow::MainWindow (QWidget *parent):
        QMainWindow(parent),
        statusLabel(NULL)
{
    exitBegin = false;
    
    arenaMap.clear();
    arenaWidgets.clear();

    init();

    retranslateUi();

    LogManager::getInstance()->addListener(this);
    TimerManager::getInstance()->addListener(this);
    QueueManager::getInstance()->addListener(this);

    startSocket();

    setStatusMessage(tr("Ready"));

    TransferView::newInstance();

    transfer_dock->setWidget(TransferView::getInstance());

    blockSignals(true);
    toolsTransfers->setChecked(transfer_dock->isVisible());
    blockSignals(false);

    if (WBGET(WB_ANTISPAM_ENABLED)){
        AntiSpam::newInstance();

        AntiSpam::getInstance()->loadLists();
        AntiSpam::getInstance()->loadSettings();
    }

    if (WBGET(WB_IPFILTER_ENABLED)){
        IPFilter::newInstance();

        IPFilter::getInstance()->loadList();
    }

    QFont f;

    if (!WSGET(WS_APP_FONT).isEmpty() && f.fromString(WSGET(WS_APP_FONT)))
        qApp->setFont(f);

    if (!WSGET(WS_APP_THEME).isEmpty())
        qApp->setStyle(WSGET(WS_APP_THEME));
}

MainWindow::~MainWindow(){
    LogManager::getInstance()->removeListener(this);
    TimerManager::getInstance()->removeListener(this);
    QueueManager::getInstance()->removeListener(this);

    if (AntiSpam::getInstance()){
        AntiSpam::getInstance()->saveLists();
        AntiSpam::getInstance()->saveSettings();
        AntiSpam::deleteInstance();
    }

    if (IPFilter::getInstance()){
        IPFilter::getInstance()->saveList();
        IPFilter::deleteInstance();
    }

    delete arena;

    delete fBar;
    delete tBar;
}

void MainWindow::closeEvent(QCloseEvent *c_e){
    if (!isUnload && WBGET(WB_TRAY_ENABLED)){
        hide();
        c_e->ignore();

        return;
    }

    if (isUnload && WBGET(WB_EXIT_CONFIRM) && !exitBegin){
        QMessageBox::StandardButton ret;

        ret = QMessageBox::question(this, tr("Exit confirm"),
                                    tr("Exit program?"),
                                    QMessageBox::Yes | QMessageBox::No,
                                    QMessageBox::Yes);

        if (ret == QMessageBox::Yes){
            exitBegin = true;
        }
        else{
            setUnload(false);

            c_e->ignore();

            return;
        }
    }

    saveSettings();

    blockSignals(true);

    if (TransferView::getInstance()){
        TransferView::getInstance()->close();
        TransferView::deleteInstance();
    }

    if (FavoriteHubs::getInstance()){
        FavoriteHubs::getInstance()->setUnload(true);
        FavoriteHubs::getInstance()->close();

        FavoriteHubs::deleteInstance();
    }

    if (PublicHubs::getInstance()){
        PublicHubs::getInstance()->setUnload(true);
        PublicHubs::getInstance()->close();

        PublicHubs::deleteInstance();
    }

    if (FinishedDownloads::getInstance()){
        FinishedDownloads::getInstance()->setUnload(true);
        FinishedDownloads::getInstance()->close();

        FinishedDownloads::deleteInstance();
    }

    if (FinishedUploads::getInstance()){
        FinishedUploads::getInstance()->setUnload(true);
        FinishedUploads::getInstance()->close();

        FinishedUploads::deleteInstance();
    }

    if (FavoriteUsers::getInstance()){
        FavoriteUsers::getInstance()->setUnload(true);
        FavoriteUsers::getInstance()->close();

        FavoriteUsers::deleteInstance();
    }

    if (DownloadQueue::getInstance()){
        DownloadQueue::getInstance()->setUnload(true);
        DownloadQueue::getInstance()->close();

        DownloadQueue::deleteInstance();
    }

    if (SpyFrame::getInstance()){
        SpyFrame::getInstance()->setUnload(true);
        SpyFrame::getInstance()->close();

        SpyFrame::deleteInstance();
    }

    QMap< ArenaWidget*, QWidget* > map = arenaMap;
    QMap< ArenaWidget*, QWidget* >::iterator it = map.begin();

    for(; it != map.end(); ++it){
        if (arenaMap.contains(it.key()))//some widgets can autodelete itself from arena widgets
            it.value()->close();
    }

    c_e->accept();
}

void MainWindow::showEvent(QShowEvent *e){
    if (e->spontaneous())
        redrawToolPanel();

    QWidget *wg = arena->widget();

    bool pmw = false;

    if (wg != 0)
        pmw = (typeid(*wg) == typeid(PMWindow));

    HubFrame *fr = HubManager::getInstance()->activeHub();

    bool enable = (fr && (fr == arena->widget()));

    chatClear->setEnabled(enable || pmw);
    findInChat->setEnabled(enable);
    chatDisable->setEnabled(enable);

    e->accept();
}

void MainWindow::customEvent(QEvent *e){
    if (e->type() == MainWindowCustomEvent::Event){
        MainWindowCustomEvent *c_e = reinterpret_cast<MainWindowCustomEvent*>(e);

        c_e->func()->call();
    }

    e->accept();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e){
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::init(){
    arena = new QDockWidget();
    arena->setFloating(false);
    arena->setAllowedAreas(Qt::RightDockWidgetArea);
    arena->setFeatures(QDockWidget::NoDockWidgetFeatures);
    arena->setContextMenuPolicy(Qt::CustomContextMenu);
    arena->setTitleBarWidget(new QWidget(arena));

    transfer_dock = new QDockWidget(this);
    transfer_dock->setObjectName("transfer_dock");
    transfer_dock->setFloating(false);
    transfer_dock->setAllowedAreas(Qt::BottomDockWidgetArea);
    transfer_dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    transfer_dock->setContextMenuPolicy(Qt::CustomContextMenu);
    transfer_dock->setTitleBarWidget(new QWidget(transfer_dock));

    setCentralWidget(arena);
    //addDockWidget(Qt::RightDockWidgetArea, arena);
    addDockWidget(Qt::BottomDockWidgetArea, transfer_dock);

    transfer_dock->hide();

    history.setSize(30);

    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));

    setWindowIcon(WulforUtil::getInstance()->getPixmap(WulforUtil::eiICON_APPL));

    setWindowTitle(QString("%1").arg(EISKALTDCPP_WND_TITLE));

    initActions();

    initMenuBar();

    initStatusBar();

    initToolbar();

    initHotkeys();

    loadSettings();

    connect(qApp, SIGNAL(aboutToQuit()), this, SLOT(slotExit()));
}

void MainWindow::loadSettings(){
    WulforSettings *WS = WulforSettings::getInstance();

    bool showMax = WS->getBool(WB_MAINWINDOW_MAXIMIZED);
    int w = WS->getInt(WI_MAINWINDOW_WIDTH);
    int h = WS->getInt(WI_MAINWINDOW_HEIGHT);
    int xPos = WS->getInt(WI_MAINWINDOW_X);
    int yPos = WS->getInt(WI_MAINWINDOW_Y);

    QPoint p(xPos, yPos);
    QSize  sz(w, h);

    if (p.x() >= 0 || p.y() >= 0)
        this->move(p);

    if (sz.width() > 0 || sz.height() > 0)
        this->resize(sz);

    if (showMax)
        this->showMaximized();

    QString wstate = WSGET(WS_MAINWINDOW_STATE);

    if (!wstate.isEmpty())
        this->restoreState(QByteArray::fromBase64(wstate.toAscii()));
}

void MainWindow::saveSettings(){
    static bool stateIsSaved = false;

    if (stateIsSaved)
        return;

    WISET(WI_MAINWINDOW_HEIGHT, height());
    WISET(WI_MAINWINDOW_WIDTH, width());
    WISET(WI_MAINWINDOW_X, x());
    WISET(WI_MAINWINDOW_Y, y());

    WBSET(WB_MAINWINDOW_MAXIMIZED, isMaximized());
    WBSET(WB_MAINWINDOW_HIDE, !isVisible());

    WSSET(WS_MAINWINDOW_STATE, saveState().toBase64());

    stateIsSaved = true;
}

void MainWindow::initActions(){

    WulforUtil *WU = WulforUtil::getInstance();

    {
        fileFileListBrowserLocal = new QAction("", this);
        fileFileListBrowserLocal->setShortcut(tr("Ctrl+L"));
        fileFileListBrowserLocal->setIcon(WU->getPixmap(WulforUtil::eiOWN_FILELIST));
        connect(fileFileListBrowserLocal, SIGNAL(triggered()), this, SLOT(slotFileBrowseOwnFilelist()));

        fileFileListBrowser = new QAction("", this);
        fileFileListBrowser->setShortcut(tr("Shift+L"));
        fileFileListBrowser->setIcon(WU->getPixmap(WulforUtil::eiOPENLIST));
        connect(fileFileListBrowser, SIGNAL(triggered()), this, SLOT(slotFileBrowseFilelist()));

        fileOpenLogFile = new QAction("", this);
        fileOpenLogFile->setIcon(WU->getPixmap(WulforUtil::eiOPEN_LOG_FILE));
        connect(fileOpenLogFile, SIGNAL(triggered()), this, SLOT(slotFileOpenLogFile()));

        fileFileListRefresh = new QAction("", this);
        fileFileListRefresh->setShortcut(tr("Ctrl+R"));
        fileFileListRefresh->setIcon(WU->getPixmap(WulforUtil::eiREFRLIST));
        connect(fileFileListRefresh, SIGNAL(triggered()), this, SLOT(slotFileRefreshShare()));

        fileHashProgress = new QAction("", this);
        fileHashProgress->setIcon(WU->getPixmap(WulforUtil::eiHASHING));
        connect(fileHashProgress, SIGNAL(triggered()), this, SLOT(slotFileHashProgress()));

        fileHideWindow = new QAction(tr("Hide window"), this);
        fileHideWindow->setShortcut(tr("Esc"));
        fileHideWindow->setIcon(WU->getPixmap(WulforUtil::eiHIDEWINDOW));
        connect(fileHideWindow, SIGNAL(triggered()), this, SLOT(slotHideWindow()));

        if (!WBGET(WB_TRAY_ENABLED))
            fileHideWindow->setText(tr("Show/hide find frame"));

        fileQuit = new QAction("", this);
        fileQuit->setShortcut(tr("Ctrl+Q"));
        fileQuit->setIcon(WU->getPixmap(WulforUtil::eiEXIT));
        connect(fileQuit, SIGNAL(triggered()), this, SLOT(slotExit()));

        hubsHubReconnect = new QAction("", this);
        hubsHubReconnect->setIcon(WU->getPixmap(WulforUtil::eiRECONNECT));
        connect(hubsHubReconnect, SIGNAL(triggered()), this, SLOT(slotHubsReconnect()));

        hubsQuickConnect = new QAction("", this);
        hubsQuickConnect->setShortcut(tr("Ctrl+H"));
        hubsQuickConnect->setIcon(WU->getPixmap(WulforUtil::eiCONNECT));
        connect(hubsQuickConnect, SIGNAL(triggered()), this, SLOT(slotQC()));

        hubsFavoriteHubs = new QAction("", this);
        hubsFavoriteHubs->setIcon(WU->getPixmap(WulforUtil::eiFAVSERVER));
        connect(hubsFavoriteHubs, SIGNAL(triggered()), this, SLOT(slotHubsFavoriteHubs()));

        hubsPublicHubs = new QAction("", this);
        hubsPublicHubs->setIcon(WU->getPixmap(WulforUtil::eiSERVER));
        connect(hubsPublicHubs, SIGNAL(triggered()), this, SLOT(slotHubsPublicHubs()));

        hubsFavoriteUsers = new QAction("", this);
        hubsFavoriteUsers->setIcon(WU->getPixmap(WulforUtil::eiFAVUSERS));
        connect(hubsFavoriteUsers, SIGNAL(triggered()), this, SLOT(slotHubsFavoriteUsers()));

        toolsOptions = new QAction("", this);
        toolsOptions->setShortcut(tr("Ctrl+O"));
        toolsOptions->setIcon(WU->getPixmap(WulforUtil::eiCONFIGURE));
        connect(toolsOptions, SIGNAL(triggered()), this, SLOT(slotToolsSettings()));

        toolsTransfers = new QAction("", this);
        toolsTransfers->setShortcut(tr("Ctrl+T"));
        toolsTransfers->setIcon(WU->getPixmap(WulforUtil::eiTRANSFER));
        toolsTransfers->setCheckable(true);
        connect(toolsTransfers, SIGNAL(toggled(bool)), this, SLOT(slotToolsTransfer(bool)));
        //transfer_dock->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);

        toolsDownloadQueue = new QAction("", this);
        toolsDownloadQueue->setShortcut(tr("Ctrl+D"));
        toolsDownloadQueue->setIcon(WU->getPixmap(WulforUtil::eiDOWNLOAD));
        connect(toolsDownloadQueue, SIGNAL(triggered()), this, SLOT(slotToolsDownloadQueue()));

        toolsFinishedDownloads = new QAction("", this);
        toolsFinishedDownloads->setIcon(WU->getPixmap(WulforUtil::eiDOWNLIST));
        connect(toolsFinishedDownloads, SIGNAL(triggered()), this, SLOT(slotToolsFinishedDownloads()));

        toolsFinishedUploads = new QAction("", this);
        toolsFinishedUploads->setIcon(WU->getPixmap(WulforUtil::eiUPLIST));
        connect(toolsFinishedUploads, SIGNAL(triggered()), this, SLOT(slotToolsFinishedUploads()));

        toolsSpy = new QAction("", this);
        toolsSpy->setIcon(WU->getPixmap(WulforUtil::eiSPY));
        connect(toolsSpy, SIGNAL(triggered()), this, SLOT(slotToolsSpy()));

        toolsAntiSpam = new QAction("", this);
        toolsAntiSpam->setIcon(WU->getPixmap(WulforUtil::eiSPAM));
        connect(toolsAntiSpam, SIGNAL(triggered()), this, SLOT(slotToolsAntiSpam()));

        toolsIPFilter = new QAction("", this);
        toolsIPFilter->setIcon(WU->getPixmap(WulforUtil::eiFILTER));
        connect(toolsIPFilter, SIGNAL(triggered()), this, SLOT(slotToolsIPFilter()));

        toolsSearch = new QAction("", this);
        toolsSearch->setShortcut(tr("Ctrl+S"));
        toolsSearch->setIcon(WU->getPixmap(WulforUtil::eiFILEFIND));
        connect(toolsSearch, SIGNAL(triggered()), this, SLOT(slotToolsSearch()));

        toolsHideProgressSpace = new QAction(tr("Hide free space bar"), this);
        if (!WBGET(WB_SHOW_FREE_SPACE))
            toolsHideProgressSpace->setText(tr("Show free space bar"));
#if (!defined FREE_SPACE_BAR && !defined FREE_SPACE_BAR_C)
        toolsHideProgressSpace->setVisible(false);
#endif
        toolsHideProgressSpace->setIcon(WU->getPixmap(WulforUtil::eiFREESPACE));
        connect(toolsHideProgressSpace, SIGNAL(triggered()), this, SLOT(slotHideProgressSpace()));

        chatClear = new QAction("", this);
        chatClear->setIcon(WU->getPixmap(WulforUtil::eiCLEAR));
        connect(chatClear, SIGNAL(triggered()), this, SLOT(slotChatClear()));

        findInChat = new QAction("", this);
        findInChat->setShortcut(tr("Ctrl+F"));
        findInChat->setIcon(WU->getPixmap(WulforUtil::eiFIND));
        connect(findInChat, SIGNAL(triggered()), this, SLOT(slotFindInChat()));

        chatDisable = new QAction("", this);
        chatDisable->setIcon(WU->getPixmap(WulforUtil::eiEDITDELETE));
        connect(chatDisable, SIGNAL(triggered()), this, SLOT(slotChatDisable()));

        QAction *separator0 = new QAction("", this);
        separator0->setSeparator(true);
        QAction *separator1 = new QAction("", this);
        separator1->setSeparator(true);
        QAction *separator2 = new QAction("", this);
        separator2->setSeparator(true);
        QAction *separator3 = new QAction("", this);
        separator3->setSeparator(true);
        QAction *separator4 = new QAction("", this);
        separator4->setSeparator(true);
        QAction *separator5 = new QAction("", this);
        separator5->setSeparator(true);
        QAction *separator6 = new QAction("", this);
        separator6->setSeparator(true);

        fileMenuActions << fileFileListBrowser
                << fileFileListBrowserLocal
                << fileFileListRefresh
                << fileHashProgress
                << separator0
                << fileOpenLogFile
                << separator1
                << fileHideWindow
                << separator2
                << fileQuit;

        hubsMenuActions << hubsHubReconnect
                << hubsQuickConnect
                << hubsFavoriteHubs
                << hubsPublicHubs
                << separator0
                << hubsFavoriteUsers;

        toolsMenuActions << toolsSearch
                << separator0
                << toolsTransfers
                << toolsDownloadQueue
                << toolsFinishedDownloads
                << toolsFinishedUploads
                << separator1
                << toolsSpy
                << toolsAntiSpam
                << toolsIPFilter
                << separator2
                << toolsHideProgressSpace
                << separator3
                << toolsOptions;

        toolBarActions << toolsOptions
                << separator0
                << fileFileListBrowserLocal
                << fileFileListRefresh
                << fileHashProgress
                << separator1
                << hubsHubReconnect
                << hubsQuickConnect
                << separator2
                << hubsFavoriteHubs
                << hubsFavoriteUsers
                << toolsSearch
                << hubsPublicHubs
                << separator3
                << toolsTransfers
                << toolsDownloadQueue
                << toolsFinishedDownloads
                << toolsFinishedUploads
                << separator4
                << chatClear
                << findInChat
                << chatDisable
                << separator5
                << toolsSpy
                << toolsAntiSpam
                << toolsIPFilter
                << separator6
                << fileQuit;
    }
    {
        menuWidgets = new QMenu("", this);
    }
    {
        aboutClient = new QAction("", this);
        aboutClient->setIcon(WU->getPixmap(WulforUtil::eiICON_APPL));
        connect(aboutClient, SIGNAL(triggered()), this, SLOT(slotAboutClient()));

        aboutQt = new QAction("", this);
        aboutQt->setIcon(WU->getPixmap(WulforUtil::eiQT_LOGO));
        connect(aboutQt, SIGNAL(triggered()), this, SLOT(slotAboutQt()));
    }
}

void MainWindow::initHotkeys(){
    ctrl_pgdown = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_PageDown), this);
    ctrl_pgup   = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_PageUp), this);
    ctrl_w      = new QShortcut(QKeySequence(Qt::CTRL + Qt::Key_W), this);

    ctrl_pgdown->setContext(Qt::WindowShortcut);
    ctrl_pgup->setContext(Qt::WindowShortcut);
    ctrl_w->setContext(Qt::WindowShortcut);

    connect(ctrl_pgdown, SIGNAL(activated()), tBar, SLOT(nextTab()));
    connect(ctrl_pgup,   SIGNAL(activated()), tBar, SLOT(prevTab()));
    connect(ctrl_w,      SIGNAL(activated()), this, SLOT(slotCloseCurrentWidget()));
}

void MainWindow::initMenuBar(){
    {
        menuFile = new QMenu("", this);

        menuFile->addActions(fileMenuActions);
    }
    {
        menuHubs = new QMenu("", this);

        menuHubs->addActions(hubsMenuActions);
    }
    {
        menuTools = new QMenu("", this);

        menuTools->addActions(toolsMenuActions);
    }
    {
        menuAbout = new QMenu("", this);

        menuAbout->addAction(aboutClient);
        menuAbout->addAction(aboutQt);
    }

    menuBar()->addMenu(menuFile);
    menuBar()->addMenu(menuHubs);
    menuBar()->addMenu(menuTools);
    menuBar()->addMenu(menuWidgets);
    menuBar()->addMenu(menuAbout);
    menuBar()->setContextMenuPolicy(Qt::CustomContextMenu);
}

void MainWindow::initStatusBar(){
    statusLabel = new QLabel(statusBar());
    statusLabel->setFrameShadow(QFrame::Plain);
    statusLabel->setFrameShape(QFrame::NoFrame);
    statusLabel->setAlignment(Qt::AlignRight);
    statusLabel->setToolTip(tr("Counts"));

    statusDSPLabel = new QLabel(statusBar());
    statusDSPLabel->setFrameShadow(QFrame::Plain);
    statusDSPLabel->setFrameShape(QFrame::NoFrame);
    statusDSPLabel->setAlignment(Qt::AlignRight);
    statusDSPLabel->setToolTip(tr("Download speed (per sec.)"));

    statusUSPLabel = new QLabel(statusBar());
    statusUSPLabel->setFrameShadow(QFrame::Plain);
    statusUSPLabel->setFrameShape(QFrame::NoFrame);
    statusUSPLabel->setAlignment(Qt::AlignRight);
    statusUSPLabel->setToolTip(tr("Upload speed (per sec.)"));

    statusDLabel = new QLabel(statusBar());
    statusDLabel->setFrameShadow(QFrame::Plain);
    statusDLabel->setFrameShape(QFrame::NoFrame);
    statusDLabel->setAlignment(Qt::AlignRight);
    statusDLabel->setToolTip(tr("Downloaded"));

    statusULabel = new QLabel(statusBar());
    statusULabel->setFrameShadow(QFrame::Plain);
    statusULabel->setFrameShape(QFrame::NoFrame);
    statusULabel->setAlignment(Qt::AlignRight);
    statusULabel->setToolTip(tr("Uploaded"));

    msgLabel = new QLabel(statusBar());
    msgLabel->setFrameShadow(QFrame::Plain);
    msgLabel->setFrameShape(QFrame::NoFrame);
    msgLabel->setAlignment(Qt::AlignLeft);
#if (defined FREE_SPACE_BAR || defined FREE_SPACE_BAR_C)
    progressSpace = new QProgressBar(this);
    progressSpace->setMaximum(100);
    progressSpace->setMinimum(0);
    progressSpace->setMinimumWidth(100);
    progressSpace->setMaximumWidth(250);
    progressSpace->setFixedHeight(18);
    progressSpace->setToolTip(tr("Space free"));

    if (!WBGET(WB_SHOW_FREE_SPACE))
        progressSpace->hide();
#else //FREE_SPACE_BAR || FREE_SPACE_BAR_C
    WBSET(WB_SHOW_FREE_SPACE, false);
#endif //FREE_SPACE_BAR || FREE_SPACE_BAR_C

    statusBar()->addWidget(msgLabel);
    statusBar()->addPermanentWidget(statusDLabel);
    statusBar()->addPermanentWidget(statusULabel);
    statusBar()->addPermanentWidget(statusDSPLabel);
    statusBar()->addPermanentWidget(statusUSPLabel);
    statusBar()->addPermanentWidget(statusLabel);
#if (defined FREE_SPACE_BAR || defined FREE_SPACE_BAR_C)
    statusBar()->addPermanentWidget(progressSpace);
#endif //FREE_SPACE_BAR
}

void MainWindow::retranslateUi(){
    //Retranslate menu actions
    {
        menuFile->setTitle(tr("&File"));

        fileOpenLogFile->setText(tr("Open log file"));

        fileFileListBrowser->setText(tr("Open filelist..."));

        fileFileListBrowserLocal->setText(tr("Open own filelist"));

        fileFileListRefresh->setText(tr("Refresh share"));

        fileHashProgress->setText(tr("Hash progress"));

        fileQuit->setText(tr("Quit"));

        menuHubs->setTitle(tr("&Hubs"));

        hubsHubReconnect->setText(tr("Reconnect to hub"));

        hubsFavoriteHubs->setText(tr("Favourite hubs"));

        hubsPublicHubs->setText(tr("Public hubs"));

        hubsFavoriteUsers->setText(tr("Favourite users"));

        hubsQuickConnect->setText(tr("Quick connect"));

        menuTools->setTitle(tr("&Tools"));

        toolsTransfers->setText(tr("Transfers"));

        toolsDownloadQueue->setText(tr("Download queue"));

        toolsFinishedDownloads->setText(tr("Finished downloads"));

        toolsFinishedUploads->setText(tr("Finished uploads"));

        toolsSpy->setText(tr("Search Spy"));

        toolsAntiSpam->setText(tr("AntiSpam module"));

        toolsIPFilter->setText(tr("IPFilter module"));

        toolsOptions->setText(tr("Options"));

        toolsSearch->setText(tr("Search"));

        chatClear->setText(tr("Clear chat"));

        findInChat->setText(tr("Find in chat"));

        chatDisable->setText(tr("Disable/enable chat"));

        menuWidgets->setTitle(tr("&Widgets"));

        menuAbout->setTitle(tr("&Help"));

        aboutClient->setText(tr("About EiskaltDC++"));

        aboutQt->setText(tr("About Qt"));
    }
    {
        arena->setWindowTitle(tr("Main layout"));
    }
}

void MainWindow::initToolbar(){
    fBar = new ToolBar(this);
    fBar->setObjectName("fBar");
    fBar->addActions(toolBarActions);
    fBar->setContextMenuPolicy(Qt::CustomContextMenu);
    fBar->setMovable(true);
    fBar->setFloatable(true);
    fBar->setAllowedAreas(Qt::AllToolBarAreas);

    tBar = new ToolBar(this);
    tBar->setObjectName("tBar");
    tBar->initTabs();
    tBar->setContextMenuPolicy(Qt::CustomContextMenu);
    tBar->setMovable(true);
    tBar->setFloatable(true);
    tBar->setAllowedAreas(Qt::AllToolBarAreas);

    addToolBar(fBar);
    addToolBar(tBar);
}

void MainWindow::newHubFrame(QString address, QString enc){
    if (address.isEmpty())
        return;

    HubFrame *fr = NULL;

    if (fr = HubManager::getInstance()->getHub(address)){
        mapWidgetOnArena(fr);

        return;
    }

    fr = new HubFrame(NULL, address, enc);
    fr->setAttribute(Qt::WA_DeleteOnClose);

    addArenaWidget(fr);
    mapWidgetOnArena(fr);

    addArenaWidgetOnToolbar(fr);
}

void MainWindow::updateStatus(QMap<QString, QString> map){
    if (!statusLabel)
        return;

    statusLabel->setText(map["STATS"]);
    statusUSPLabel->setText(map["USPEED"]);
    statusDSPLabel->setText(map["DSPEED"]);
    statusDLabel->setText(map["DOWN"]);
    statusULabel->setText(map["UP"]);

    QFontMetrics metrics(font());

    statusUSPLabel->setFixedWidth(metrics.width(statusUSPLabel->text()) > statusUSPLabel->width()? metrics.width(statusUSPLabel->text()) + 10 : statusUSPLabel->width());
    statusDSPLabel->setFixedWidth(metrics.width(statusDSPLabel->text()) > statusDSPLabel->width()? metrics.width(statusDSPLabel->text()) + 10 : statusDSPLabel->width());
    statusDLabel->setFixedWidth(metrics.width(statusDLabel->text()) > statusDLabel->width()? metrics.width(statusDLabel->text()) + 10 : statusDLabel->width());
    statusULabel->setFixedWidth(metrics.width(statusULabel->text()) > statusULabel->width()? metrics.width(statusULabel->text()) + 10 : statusULabel->width());

    if (WBGET(WB_SHOW_FREE_SPACE)) {
#ifdef FREE_SPACE_BAR
        boost::filesystem::space_info info;
        if (boost::filesystem::exists(SETTING(DOWNLOAD_DIRECTORY)))
            info = boost::filesystem::space(boost::filesystem::path(SETTING(DOWNLOAD_DIRECTORY)));
        else if (boost::filesystem::exists(Util::getPath(Util::PATH_USER_CONFIG)))
            info = boost::filesystem::space(boost::filesystem::path(Util::getPath(Util::PATH_USER_CONFIG)));

        if (info.capacity) {
            float total = info.capacity;
            float percent = 100.0f*(total-info.available)/total;
            QString format = tr("Free %1")
                             .arg(_q(dcpp::Util::formatBytes(info.available)));

            QString tooltip = tr("Free %1 of %2")
                              .arg(_q(dcpp::Util::formatBytes(info.available)))
                              .arg(_q(dcpp::Util::formatBytes(total)));

            progressSpace->setFormat(format);
            progressSpace->setToolTip(tooltip);
            progressSpace->setValue(static_cast<unsigned>(percent));
        }
#elif defined FREE_SPACE_BAR_C
    std::string s = SETTING(DOWNLOAD_DIRECTORY);
    unsigned long long available = 0;
    unsigned long long total = 0;
    if (!s.empty()) {
        if (MainWindow::FreeDiscSpace(s.c_str() , &available, &total) == false) {
            s = Util::getPath(Util::PATH_USER_CONFIG);
            if (MainWindow::FreeDiscSpace(s.c_str() , &available, &total) == false) {
            available = 0;
            total = 0;
            }
        }
    }
    float percent = 100.0f*(total-available)/total;
    QString format = tr("Free %1")
                         .arg(_q(dcpp::Util::formatBytes(available)));

    QString tooltip = tr("Free %1 of %2")
                         .arg(_q(dcpp::Util::formatBytes(available)))
                         .arg(_q(dcpp::Util::formatBytes(total)));

            progressSpace->setFormat(format);
            progressSpace->setToolTip(tooltip);
            progressSpace->setValue(static_cast<unsigned>(percent));
#endif //FREE_SPACE_BAR
    }
}

void MainWindow::setStatusMessage(QString msg){
    msgLabel->setText(msg);
}

void MainWindow::autoconnect(){
    const FavoriteHubEntryList& fl = FavoriteManager::getInstance()->getFavoriteHubs();

    for(FavoriteHubEntryList::const_iterator i = fl.begin(); i != fl.end(); ++i) {
        FavoriteHubEntry* entry = *i;

        if (entry->getConnect()) {
            if (entry->getNick().empty() && SETTING(NICK).empty())
                continue;

            QString encoding = WulforUtil::getInstance()->dcEnc2QtEnc(QString::fromStdString(entry->getEncoding()));

            newHubFrame(QString::fromStdString(entry->getServer()), encoding);
        }
    }
}

void MainWindow::parseCmdLine(){
    QStringList args = qApp->arguments();

    foreach (QString arg, args){
        if (arg.startsWith("magnet:?xt=urn:tree:tiger:")){
            Magnet m(this);
            m.setLink(arg);

            m.exec();
        }
        else if (arg.startsWith("dchub://")){
            newHubFrame(arg, "");
        }
        else if (arg.startsWith("adc://") || arg.startsWith("adcs://")){
            newHubFrame(arg, "UTF-8");
        }
    }
}

void MainWindow::parseInstanceLine(QString data){
    QStringList args = data.split("\n", QString::SkipEmptyParts);

    foreach (QString arg, args){
        if (arg.startsWith("magnet:?xt=urn:tree:tiger:")){
            Magnet m(this);
            m.setLink(arg);

            m.exec();
        }
        else if (arg.startsWith("dchub://")){
            newHubFrame(arg, "");
        }
        else if (arg.startsWith("adc://") || arg.startsWith("adcs://")){
            newHubFrame(arg, "UTF-8");
        }
    }
}

void MainWindow::browseOwnFiles(){
    slotFileBrowseOwnFilelist();
}

void MainWindow::slotFileBrowseFilelist(){
    static ShareBrowser *local_share = NULL;
    QString file = QFileDialog::getOpenFileName(this, tr("Choose file to open"), QString::fromStdString(Util::getPath(Util::PATH_FILE_LISTS)),
            tr("Modern XML Filelists") + " (*.xml.bz2);;" +
            tr("Modern XML Filelists uncompressed") + " (*.xml);;" +
            tr("All files") + " (*)");
    UserPtr user = DirectoryListing::getUserFromFilename(_tq(file));
    if (user) {
        local_share = new ShareBrowser(user, file, "");
    } else {
        setStatusMessage(tr("Unable to load file list: Invalid file list name"));
    }
}

void MainWindow::redrawToolPanel(){
    tBar->redraw();

    QHash<QAction*, ArenaWidget*>::iterator it = menuWidgetsHash.begin();
    QHash<QAction*, ArenaWidget*>::iterator end = menuWidgetsHash.end();

    for(; it != end; ++it){//also redraw all widget menu items
        it.key()->setText(it.value()->getArenaShortTitle());
        it.key()->setIcon(it.value()->getPixmap());
    }
}

void MainWindow::addArenaWidget(ArenaWidget *wgt){
    if (!arenaWidgets.contains(wgt) && wgt && wgt->getWidget()){
        arenaWidgets.push_back(wgt);
        arenaMap[wgt] = wgt->getWidget();
    }
}

void MainWindow::remArenaWidget(ArenaWidget *awgt){
    if (arenaWidgets.contains(awgt)){
        arenaWidgets.removeAt(arenaWidgets.indexOf(awgt));
        arenaMap.erase(arenaMap.find(awgt));

        if (arena->widget() == awgt->getWidget()){
            arena->setWidget(NULL);

            chatClear->setEnabled(false);
            findInChat->setEnabled(false);
            chatDisable->setEnabled(false);
        }
    }
}

void MainWindow::mapWidgetOnArena(ArenaWidget *awgt){
    if (!arenaWidgets.contains(awgt))
        return;

    if (arena->widget())
        arena->widget()->hide();

    arena->setWidget(arenaMap[awgt]);

    setWindowTitle(awgt->getArenaTitle() + " :: " + QString("%1").arg(EISKALTDCPP_WND_TITLE));

    tBar->mapped(awgt);

    QWidget *wg = arenaMap[awgt];
    
    if (awgt->toolButton())
        awgt->toolButton()->setChecked(true);

    bool pmw = false;

    if (wg != 0)
        pmw = (typeid(*wg) == typeid(PMWindow));

    HubFrame *fr = HubManager::getInstance()->activeHub();

    chatClear->setEnabled(fr == arena->widget() || pmw);
    findInChat->setEnabled(fr == arena->widget());
    chatDisable->setEnabled(fr == arena->widget());

    if (fr == arena->widget()){
        fr->plainTextEdit_INPUT->setFocus();
    }
    else if(pmw){
        PMWindow *pm = qobject_cast<PMWindow *>(wg);
        if (pm)
            pm->plainTextEdit_INPUT->setFocus();
    }
    else{
        arenaMap[awgt]->setFocus();
    }
}

void MainWindow::remWidgetFromArena(ArenaWidget *awgt){
    if (!arenaWidgets.contains(awgt))
        return;

    if (awgt->toolButton())
        awgt->toolButton()->setChecked(false);

    if (arena->widget() == awgt->getWidget())
        arena->widget()->hide();

    /*chatClear->setEnabled(false);
    findInChat->setEnabled(false);
    chatDisable->setEnabled(false);*/
}

void MainWindow::addArenaWidgetOnToolbar(ArenaWidget *awgt, bool keepFocus){
    if (!arenaWidgets.contains(awgt))
        return;

    QAction *act = new QAction(awgt->getArenaShortTitle(), this);
    act->setIcon(awgt->getPixmap());

    connect(act, SIGNAL(triggered()), this, SLOT(slotWidgetsToggle()));

    menuWidgetsActions.push_back(act);
    menuWidgetsHash.insert(act, awgt);

    menuWidgets->clear();
    menuWidgets->addActions(menuWidgetsActions);

    if (awgt->toolButton())
        awgt->toolButton()->setChecked(true);

    tBar->insertWidget(awgt, keepFocus);
}

void MainWindow::remArenaWidgetFromToolbar(ArenaWidget *awgt){
    QHash<QAction*, ArenaWidget*>::iterator it = menuWidgetsHash.begin();
    for (; it != menuWidgetsHash.end(); ++it){
        if (it.value() == awgt){
            menuWidgetsActions.removeAt(menuWidgetsActions.indexOf(it.key()));
            menuWidgetsHash.erase(it);

            menuWidgets->clear();

            menuWidgets->addActions(menuWidgetsActions);

            break;
        }
    }

    if (awgt->toolButton())
        awgt->toolButton()->setChecked(false);

    tBar->removeWidget(awgt);
}

void MainWindow::toggleSingletonWidget(ArenaWidget *a){
    if (!a)
        return;

    if (sender() && typeid(*sender()) == typeid(QAction) && a->getWidget()){
        QAction *act = reinterpret_cast<QAction*>(sender());;

        act->setCheckable(true);

        a->setToolButton(act);
    }

    if (tBar->hasWidget(a)){
        QHash<QAction*, ArenaWidget*>::iterator it = menuWidgetsHash.begin();
        for (; it != menuWidgetsHash.end(); ++it){
            if (it.value() == a){
                menuWidgetsActions.removeAt(menuWidgetsActions.indexOf(it.key()));
                menuWidgetsHash.erase(it);

                menuWidgets->clear();

                menuWidgets->addActions(menuWidgetsActions);

                break;
            }
        }

        tBar->removeWidget(a);
        remWidgetFromArena(a);
    }
    else {
        tBar->insertWidget(a);

        QAction *act = new QAction(a->getArenaShortTitle(), this);
        act->setIcon(a->getPixmap());

        connect(act, SIGNAL(triggered()), this, SLOT(slotWidgetsToggle()));

        menuWidgetsActions.push_back(act);
        menuWidgetsHash.insert(act, a);

        menuWidgets->clear();
        menuWidgets->addActions(menuWidgetsActions);

        mapWidgetOnArena(a);
    }
}

void MainWindow::startSocket(){
    SearchManager::getInstance()->disconnect();
    ConnectionManager::getInstance()->disconnect();

    if (ClientManager::getInstance()->isActive()) {
        QString msg = "";
        try {
            ConnectionManager::getInstance()->listen();
        } catch(const Exception &e) {
            msg = tr("Cannot listen socket because: \n") + QString::fromStdString(e.getError()) + tr("\n\nPlease check your connection settings");

            QMessageBox::warning(this, tr("Connection Manager: Warning"), msg, QMessageBox::Ok);
        }
        try {
            SearchManager::getInstance()->listen();
        } catch(const Exception &e) {
            msg = tr("Cannot listen socket because: \n") + QString::fromStdString(e.getError()) + tr("\n\nPlease check your connection settings");

            QMessageBox::warning(this, tr("Search Manager: Warning"), msg, QMessageBox::Ok);
        }
    }

    UPnPMapper::getInstance()->forward();
}

void MainWindow::showShareBrowser(dcpp::UserPtr usr, QString file, QString jump_to){
    ShareBrowser *sb = new ShareBrowser(usr, file, jump_to);
}

void MainWindow::slotFileOpenLogFile(){
    QString f = QFileDialog::getOpenFileName(this, tr("Open log file"),_q(SETTING(LOG_DIRECTORY)), tr("Log files (*.log);;All files (*.*)"));

    if (!f.isEmpty()){
        if (f.startsWith("/"))
            f = "file://" + f;
        else
            f = "file:///" + f;

        QDesktopServices::openUrl(f);
    }
}

void MainWindow::slotFileBrowseOwnFilelist(){
    static ShareBrowser *local_share = NULL;

    if (arenaWidgets.contains(local_share)){
        mapWidgetOnArena(local_share);

        return;
    }

    UserPtr user = ClientManager::getInstance()->getMe();
    QString file = QString::fromStdString(ShareManager::getInstance()->getOwnListFile());

    local_share = new ShareBrowser(user, file, "");
}

void MainWindow::slotFileRefreshShare(){
    ShareManager *SM = ShareManager::getInstance();

    SM->setDirty();
    SM->refresh(true);

    HashProgress progress(this);
    progress.slotAutoClose(true);

    progress.exec();
}

void MainWindow::slotFileHashProgress(){
    HashProgress progress(this);

    progress.exec();
}

void MainWindow::slotHubsReconnect(){
    HubFrame *fr = HubManager::getInstance()->activeHub();

    if (fr)
        fr->reconnect();
}

void MainWindow::slotToolsSearch(){
    SearchFrame *sf = new SearchFrame();

    sf->setAttribute(Qt::WA_DeleteOnClose);
}

void MainWindow::slotToolsDownloadQueue(){
    if (!DownloadQueue::getInstance())
        DownloadQueue::newInstance();

    toggleSingletonWidget(DownloadQueue::getInstance());
}

void MainWindow::slotToolsFinishedDownloads(){
    if (!FinishedDownloads::getInstance())
        FinishedDownloads::newInstance();

    toggleSingletonWidget(FinishedDownloads::getInstance());
}

void MainWindow::slotToolsFinishedUploads(){
    if (!FinishedUploads::getInstance())
        FinishedUploads::newInstance();

    toggleSingletonWidget(FinishedUploads::getInstance());
}

void MainWindow::slotToolsSpy(){
    if (!SpyFrame::getInstance())
        SpyFrame::newInstance();

    toggleSingletonWidget(SpyFrame::getInstance());
}

void MainWindow::slotToolsAntiSpam(){
    AntiSpamFrame fr(this);

    fr.exec();
}

void MainWindow::slotToolsIPFilter(){
    IPFilterFrame fr(this);

    fr.exec();
}

void MainWindow::slotHubsFavoriteHubs(){
    if (!FavoriteHubs::getInstance())
        FavoriteHubs::newInstance();

    toggleSingletonWidget(FavoriteHubs::getInstance());
}

void MainWindow::slotHubsPublicHubs(){
    if (!PublicHubs::getInstance())
        PublicHubs::newInstance();

    toggleSingletonWidget(PublicHubs::getInstance());
}

void MainWindow::slotHubsFavoriteUsers(){
    if (!FavoriteUsers::getInstance())
        FavoriteUsers::newInstance();

    toggleSingletonWidget(FavoriteUsers::getInstance());
}

void MainWindow::slotToolsSettings(){
    Settings s;

    s.exec();

    //reload some settings
    if (!WBGET(WB_TRAY_ENABLED))
        fileHideWindow->setText(tr("Show/hide find frame"));
    else
        fileHideWindow->setText(tr("Hide window"));
}

void MainWindow::slotToolsTransfer(bool toggled){
    if (toggled){
        transfer_dock->setVisible(true);
        transfer_dock->setWidget(TransferView::getInstance());
    }
    else {
        transfer_dock->setWidget(NULL);
        transfer_dock->setVisible(false);
    }
}

void MainWindow::slotChatClear(){
    HubFrame *fr = HubManager::getInstance()->activeHub();

    if (fr)
        fr->clearChat();
    else{
        QWidget *wg = arena->widget();

        bool pmw = false;

        if (wg != 0)
            pmw = (typeid(*wg) == typeid(PMWindow));

        if(pmw){
            PMWindow *pm = qobject_cast<PMWindow *>(wg);

            if (pm){
                pm->textEdit_CHAT->setHtml("");

                pm->addStatus(tr("Chat cleared."));
            }
        }
    }
}

void MainWindow::slotFindInChat(){
    HubFrame *fr = HubManager::getInstance()->activeHub();

    if (fr)
        fr->slotHideFindFrame();
}

void MainWindow::slotChatDisable(){
    HubFrame *fr = HubManager::getInstance()->activeHub();

    if (fr)
        fr->disableChat();
}

void MainWindow::slotWidgetsToggle(){
    QAction *act = reinterpret_cast<QAction*>(sender());
    QHash<QAction*, ArenaWidget*>::iterator it = menuWidgetsHash.find(act);

    if (it == menuWidgetsHash.end())
        return;

    mapWidgetOnArena(it.value());
}

void MainWindow::slotQC(){
    QuickConnect qc;

    qc.exec();
}

void MainWindow::slotHideWindow(){
    HubFrame *fr = HubManager::getInstance()->activeHub();
    if (fr){
        if (fr->lineEdit_FIND->hasFocus() && WBGET(WB_TRAY_ENABLED)){
            fr->slotHideFindFrame();
            return;
        }
        else if (!WBGET(WB_TRAY_ENABLED)){
            fr->slotHideFindFrame();
            return;
        }
    }
    if (!isUnload && isActiveWindow() && WBGET(WB_TRAY_ENABLED)) {
        hide();
    }
}

void MainWindow::slotHideProgressSpace() {
    if (WBGET(WB_SHOW_FREE_SPACE)) {
        progressSpace->hide();
        toolsHideProgressSpace->setText(tr("Show free space bar"));

        WBSET(WB_SHOW_FREE_SPACE, false);
    } else {
        progressSpace->show();
        toolsHideProgressSpace->setText(tr("Hide free space bar"));

        WBSET(WB_SHOW_FREE_SPACE, true);
    }
}

void MainWindow::slotExit(){
    setUnload(true);

    close();
}

void MainWindow::slotAboutClient(){
    About a(this);

    qulonglong app_total_down = WSGET(WS_APP_TOTAL_DOWN).toULongLong();
    qulonglong app_total_up   = WSGET(WS_APP_TOTAL_UP).toULongLong();

#ifndef DCPP_REVISION
    a.label->setText(QString("<b>%1</b> %2 (%3)")
                     .arg(EISKALTDCPP_WND_TITLE)
                     .arg(EISKALTDCPP_VERSION)
                     .arg(EISKALTDCPP_VERSION_SFX));
#else
    a.label->setText(QString("<b>%1</b> %2 - %3 %4")
                     .arg(EISKALTDCPP_WND_TITLE)
                     .arg(EISKALTDCPP_VERSION)
                     .arg(EISKALTDCPP_VERSION_SFX)
                     .arg(DCPP_REVISION));
#endif
    a.label_ABOUT->setTextFormat(Qt::RichText);
    a.label_ABOUT->setText(QString("%1<br/><br/> %2 %3 %4<br/><br/> %5 %6<br/><br/> %7 <b>%8</b> <br/> %9 <b>%10</b>")
                           .arg(tr("EiskaltDC++ is a graphical client for Direct Connect and ADC protocols."))
                           .arg(tr("DC++ core version:"))
                           .arg(DCVERSIONSTRING)
                           .arg(tr("(modified)"))
                           .arg(tr("Home page:"))
                           .arg("<a href=\"http://code.google.com/p/eiskaltdc/\">"
                                "http://code.google.com/p/eiskaltdc/</a>")
                           .arg(tr("Total up:"))
                           .arg(_q(Util::formatBytes(app_total_up)))
                           .arg(tr("Total down:"))
                           .arg(_q(Util::formatBytes(app_total_down))));

    a.exec();
}

void MainWindow::slotUnixSignal(int sig){
    printf("%i\n");
}

void MainWindow::slotCloseCurrentWidget(){
    if (arena->widget())
        arena->widget()->close();
}

void MainWindow::slotAboutQt(){
    QMessageBox::aboutQt(this);
}

void MainWindow::on(dcpp::LogManagerListener::Message, time_t t, const std::string& m) throw(){
    QTextCodec *codec = QTextCodec::codecForLocale();

    typedef Func1<MainWindow, QString> FUNC;
    FUNC *func = new FUNC(this, &MainWindow::setStatusMessage, codec->toUnicode(m.c_str()));

    QApplication::postEvent(this, new MainWindowCustomEvent(func));
}

void MainWindow::on(dcpp::QueueManagerListener::Finished, QueueItem *item, const std::string &dir, int64_t) throw(){
    if (item->isSet(QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_USER_LIST)){
        UserPtr user = item->getDownloads()[0]->getUser();
        QString listName = QString::fromStdString(item->getListName());

        typedef Func3<MainWindow, UserPtr, QString, QString> FUNC;
        FUNC *func = new FUNC(this, &MainWindow::showShareBrowser, user, listName, QString::fromStdString(dir));

        QApplication::postEvent(this, new MainWindowCustomEvent(func));
    }
}

void MainWindow::on(dcpp::TimerManagerListener::Second, uint32_t ticks) throw(){
    static quint32 lastUpdate = 0;
    static quint64 lastUp = 0, lastDown = 0;

    quint64 now = GET_TICK();

    quint64 diff = now - lastUpdate;
    quint64 downBytes = 0;
    quint64 upBytes = 0;

    if (diff < 100U)
        diff = 1U;

    quint64 downDiff = Socket::getTotalDown() - lastDown;
    quint64 upDiff = Socket::getTotalUp() - lastUp;

    downBytes = (downDiff * 1000) / diff;
    upBytes = (upDiff * 1000) / diff;

    QMap<QString, QString> map;

    map["STATS"]    = _q(Client::getCounts());
    map["DSPEED"]   = _q(Util::formatBytes(downBytes));
    map["DOWN"]     = _q(Util::formatBytes(Socket::getTotalDown()));
    map["USPEED"]   = _q(Util::formatBytes(upBytes));
    map["UP"]       = _q(Util::formatBytes(Socket::getTotalUp()));

    qulonglong app_total_down = WSGET(WS_APP_TOTAL_DOWN).toULongLong()+downDiff;
    qulonglong app_total_up   = WSGET(WS_APP_TOTAL_UP).toULongLong()+upDiff;

    WSSET(WS_APP_TOTAL_DOWN, QString().setNum(app_total_down));
    WSSET(WS_APP_TOTAL_UP, QString().setNum(app_total_up));

    lastUpdate = ticks;
    lastUp = Socket::getTotalUp();
    lastDown = Socket::getTotalDown();

    typedef Func1<MainWindow, QMap<QString, QString> > FUNC;
    FUNC *func = new FUNC(this, &MainWindow::updateStatus, map);

    QApplication::postEvent(this, new MainWindowCustomEvent(func));
}
#ifdef FREE_SPACE_BAR_C
bool MainWindow::FreeDiscSpace ( std::string path,  unsigned long long * res, unsigned long long * res2) {
        if ( !res ) {
            return false;
        }

#ifdef WIN32
        ULARGE_INTEGER lpFreeBytesAvailableToCaller; // receives the number of bytes on
                                               // disk available to the caller
        ULARGE_INTEGER lpTotalNumberOfBytes;    // receives the number of bytes on disk
        ULARGE_INTEGER lpTotalNumberOfFreeBytes; // receives the free bytes on disk

        if ( GetDiskFreeSpaceEx( path.c_str(), &lpFreeBytesAvailableToCaller,
                                &lpTotalNumberOfBytes,
                                &lpTotalNumberOfFreeBytes ) == true ) {
                *res = lpTotalNumberOfFreeBytes.QuadPart;
                *res2 = lpTotalNumberOfBytes.QuadPart;
                return true;
        } else {
            return false;
        }
#else //WIN32
        struct fs_usage fsp;
        if ( get_fs_usage(path.c_str(),path.c_str(),&fsp) == 0 ) {
                // printf("ok %d\n",fsp.fsu_bavail_top_bit_set);
                *res = fsp.fsu_bavail*fsp.fsu_blocksize;
                *res2 =fsp.fsu_blocks*fsp.fsu_blocksize;
                return true;
        } else {
                printf("ERROR: no info for free space");
                return false;
        }
#endif //WIN32
}
#endif //FREE_SPACE_BAR_C
