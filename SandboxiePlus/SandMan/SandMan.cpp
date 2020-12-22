#include "stdafx.h"
#include "SandMan.h"
#include "../MiscHelpers/Common/Common.h"
#include "../MiscHelpers/Common/ExitDialog.h"
#include "../MiscHelpers/Common/SortFilterProxyModel.h"
#include "Views/SbieView.h"
#include "../MiscHelpers/Common/CheckableMessageBox.h"
#include <QWinEventNotifier>
#include "ApiLog.h"
#include "./Dialogs/MultiErrorDialog.h"
#include "../QSbieAPI/SbieUtils.h"
#include "../QSbieAPI/Sandboxie/BoxBorder.h"
#include "../QSbieAPI/Sandboxie/SbieTemplates.h"
#include "Windows/SettingsWindow.h"
#include "Windows/RecoveryWindow.h"
#include <QtConcurrent>
#include "../MiscHelpers/Common/SettingsWidgets.h"
#include "Windows/NewBoxWindow.h"

CSbiePlusAPI* theAPI = NULL;

#if defined(Q_OS_WIN)
#include <wtypes.h>
#include <QAbstractNativeEventFilter>
#include <dbt.h>

//BOOLEAN OnWM_Notify(NMHDR *Header, LRESULT *Result);

class CNativeEventFilter : public QAbstractNativeEventFilter
{
public:
	virtual bool nativeEventFilter(const QByteArray &eventType, void *message, long *result)
	{
		if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG")
		{
			MSG *msg = static_cast<MSG *>(message);

			//if(msg->message != 275 && msg->message != 1025)
			//	qDebug() << msg->message;

			if (msg->message == WM_NOTIFY)
			{
				//LRESULT ret;
				//if (OnWM_Notify((NMHDR*)msg->lParam, &ret))
				//	*result = ret;
				return true;
			}
			else if (msg->message == WM_DEVICECHANGE)
			{
				if (msg->wParam == DBT_DEVICEARRIVAL // Drive letter added
				 || msg->wParam == DBT_DEVICEREMOVECOMPLETE) // Drive letter removed
				{
					/*DEV_BROADCAST_HDR* deviceBroadcast = (DEV_BROADCAST_HDR*)msg->lParam;
					if (deviceBroadcast->dbch_devicetype == DBT_DEVTYP_VOLUME) {
					}*/
					if (theAPI)
						theAPI->UpdateDriveLetters();
				}
				/*else if ((msg->wParam & 0xFF80) == 0xAA00 && msg->lParam == 'xobs') 
				{
					UCHAR driveNumber = (UCHAR)(msg->wParam & 0x1F);
					if (driveNumber < 26) {		
					}
				}
				else if (msg->wParam == DBT_DEVNODES_CHANGED) // hardware changed
				{
				}*/
			}
		}
		return false;
	}
};

HWND MainWndHandle = NULL;
#endif

CSandMan* theGUI = NULL;

CSandMan::CSandMan(QWidget *parent)
	: QMainWindow(parent)
{
#if defined(Q_OS_WIN)
	MainWndHandle = (HWND)QWidget::winId();

	QApplication::instance()->installNativeEventFilter(new CNativeEventFilter);
#endif

	theGUI = this;

	m_DefaultStyle = QApplication::style()->objectName();
	m_DefaultPalett = QApplication::palette();

	m_LanguageId = 1033; // lang en_us
	LoadLanguage();
	if (theConf->GetBool("Options/DarkTheme", false))
		SetDarkTheme(true);

	m_bExit = false;

	theAPI = new CSbiePlusAPI(this);
	connect(theAPI, SIGNAL(StatusChanged()), this, SLOT(OnStatusChanged()));
	connect(theAPI, SIGNAL(BoxClosed(const QString&)), this, SLOT(OnBoxClosed(const QString&)));

	m_RequestManager = NULL;

	QString appTitle = tr("Sandboxie-Plus v%1").arg(GetVersion());
	this->setWindowTitle(appTitle);

	m_pBoxBorder = new CBoxBorder(theAPI, this);

	m_SbieTemplates = new CSbieTemplates(theAPI, this);

	m_ApiLog = NULL;

	m_bConnectPending = false;
	m_bStopPending = false;

	CreateMenus();

	m_pMainWidget = new QWidget();
	m_pMainLayout = new QVBoxLayout(m_pMainWidget);
	m_pMainLayout->setMargin(2);
	m_pMainLayout->setSpacing(0);
	this->setCentralWidget(m_pMainWidget);

	CreateToolBar();

	m_pLogSplitter = new QSplitter();
	m_pLogSplitter->setOrientation(Qt::Vertical);
	m_pMainLayout->addWidget(m_pLogSplitter);

	m_pPanelSplitter = new QSplitter();
	m_pPanelSplitter->setOrientation(Qt::Horizontal);
	m_pLogSplitter->addWidget(m_pPanelSplitter);


	m_pBoxView = new CSbieView();
	m_pPanelSplitter->addWidget(m_pBoxView);

	connect(m_pBoxView->GetTree()->selectionModel(), SIGNAL(currentChanged(QModelIndex, QModelIndex)), this, SLOT(OnSelectionChanged()));
	connect(m_pBoxView, SIGNAL(RecoveryRequested(const QString&)), this, SLOT(OpenRecovery(const QString&)));

	//m_pPanelSplitter->addWidget();

	m_pLogTabs = new QTabWidget();
	m_pLogSplitter->addWidget(m_pLogTabs);

	// Message Log
	m_pMessageLog = new CPanelWidgetEx();

	//m_pMessageLog->GetView()->setItemDelegate(theGUI->GetItemDelegate());
	((QTreeWidgetEx*)m_pMessageLog->GetView())->setHeaderLabels(tr("Time|Message").split("|"));

	m_pMessageLog->GetView()->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_pMessageLog->GetView()->setSortingEnabled(false);

	
	m_pLogTabs->addTab(m_pMessageLog, tr("Sbie Messages"));
	//

	// Res Log
	m_pResMonModel = new CResMonModel();
	//m_pResMonModel->SetUseIcons(true);

	m_pResourceLog = new CPanelViewEx(m_pResMonModel);

	//m_pResourceLog->GetView()->setItemDelegate(theGUI->GetItemDelegate());

	m_pResourceLog->GetView()->setSelectionMode(QAbstractItemView::ExtendedSelection);

	m_pLogTabs->addTab(m_pResourceLog, tr("Resource Monitor"));
	//

	// Api Log
	m_pApiMonModel = new CApiMonModel();
	//m_pApiMonModel->SetUseIcons(true);

	m_pApiCallLog = new CPanelViewEx(m_pApiMonModel);

	//m_pApiCallLog->GetView()->setItemDelegate(theGUI->GetItemDelegate());

	m_pApiCallLog->GetView()->setSelectionMode(QAbstractItemView::ExtendedSelection);

	m_pLogTabs->addTab(m_pApiCallLog, tr("Api Call Log"));
	m_pApiCallLog->setEnabled(false);
	//


	// Tray
	QIcon Icon;
	Icon.addFile(":/SandMan.png");
	m_pTrayIcon = new QSystemTrayIcon(Icon, this);
	m_pTrayIcon->setToolTip("Sandboxie-Plus");
	connect(m_pTrayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(OnSysTray(QSystemTrayIcon::ActivationReason)));
	m_bIconEmpty = false;
	m_bIconDisabled = false;

	m_pTrayMenu = new QMenu();
	QAction* pShowHide = m_pTrayMenu->addAction(tr("Show/Hide"), this, SLOT(OnShowHide()));
	QFont f = pShowHide->font();
	f.setBold(true);
	pShowHide->setFont(f);
	m_pTrayMenu->addSeparator();
	m_pTrayMenu->addAction(m_pEmptyAll);
	m_pDisableForce2 = m_pTrayMenu->addAction(tr("Disable Forced Programs"), this, SLOT(OnDisableForce2()));
	m_pDisableForce2->setCheckable(true);
	m_pTrayMenu->addSeparator();
	m_pTrayMenu->addAction(m_pExit);

	bool bAutoRun = QApplication::arguments().contains("-autorun");

	m_pTrayIcon->show(); // Note: qt bug; without a first show hide does not work :/
	if(!bAutoRun && !theConf->GetBool("Options/ShowSysTray", true))
		m_pTrayIcon->hide();
	//

	LoadState();

	bool bAdvanced = theConf->GetBool("Options/AdvancedView", true);
	foreach(QAction * pAction, m_pViewMode->actions())
		pAction->setChecked(pAction->data().toBool() == bAdvanced);
	SetViewMode(bAdvanced);

	m_pKeepTerminated->setChecked(theConf->GetBool("Options/KeepTerminated"));

	m_pProgressDialog = new CProgressDialog("", this);
	m_pProgressDialog->setWindowModality(Qt::ApplicationModal);
	connect(m_pProgressDialog, SIGNAL(Cancel()), this, SLOT(OnCancelAsync()));

	m_pPopUpWindow = new CPopUpWindow(this);
	connect(m_pPopUpWindow, SIGNAL(RecoveryRequested(const QString&)), this, SLOT(OpenRecovery(const QString&)));

	if (!bAutoRun)
		show();

	//connect(theAPI, SIGNAL(LogMessage(const QString&, bool)), this, SLOT(OnLogMessage(const QString&, bool)));
	connect(theAPI, SIGNAL(LogSbieMessage(quint32, const QStringList&, quint32)), this, SLOT(OnLogSbieMessage(quint32, const QStringList&, quint32)));
	connect(theAPI, SIGNAL(NotAuthorized(bool, bool&)), this, SLOT(OnNotAuthorized(bool, bool&)), Qt::DirectConnection);
	connect(theAPI, SIGNAL(QueuedRequest(quint32, quint32, quint32, const QVariantMap&)), this, SLOT(OnQueuedRequest(quint32, quint32, quint32, const QVariantMap&)), Qt::QueuedConnection);
	connect(theAPI, SIGNAL(FileToRecover(const QString&, const QString&, quint32)), this, SLOT(OnFileToRecover(const QString&, const QString&, quint32)), Qt::QueuedConnection);
	connect(theAPI, SIGNAL(ConfigReloaded()), this, SLOT(OnIniReloaded()));

	m_uTimerID = startTimer(250);

	if (CSbieUtils::IsRunning(CSbieUtils::eAll) || theConf->GetBool("Options/StartIfStopped", true))
	{
		SB_STATUS Status = ConnectSbie();
		CheckResults(QList<SB_STATUS>() << Status);
	}
}

CSandMan::~CSandMan()
{
	if(m_pEnableMonitoring->isChecked())
		theAPI->EnableMonitor(false);

	killTimer(m_uTimerID);

	m_pTrayIcon->hide();

	StoreState();

	theAPI = NULL;

	theGUI = NULL;
}

void CSandMan::LoadState()
{
	restoreGeometry(theConf->GetBlob("MainWindow/Window_Geometry"));
	//m_pBoxTree->restoreState(theConf->GetBlob("MainWindow/BoxTree_Columns"));
	m_pMessageLog->GetView()->header()->restoreState(theConf->GetBlob("MainWindow/LogList_Columns"));
	QByteArray Columns = theConf->GetBlob("MainWindow/ResMonList_Columns");
	if (!Columns.isEmpty())
		((QTreeViewEx*)m_pResourceLog->GetView())->OnResetColumns();
	else
		((QTreeViewEx*)m_pResourceLog->GetView())->restoreState(Columns);
	Columns = theConf->GetBlob("MainWindow/ApiLogList_Columns");
	if (!Columns.isEmpty())
		((QTreeViewEx*)m_pApiCallLog->GetView())->OnResetColumns();
	else
		((QTreeViewEx*)m_pApiCallLog->GetView())->restoreState(Columns);
	m_pLogSplitter->restoreState(theConf->GetBlob("MainWindow/Log_Splitter"));
	m_pPanelSplitter->restoreState(theConf->GetBlob("MainWindow/Panel_Splitter"));
	m_pLogTabs->setCurrentIndex(theConf->GetInt("MainWindow/LogTab", 0));
}

void CSandMan::StoreState()
{
	theConf->SetBlob("MainWindow/Window_Geometry", saveGeometry());
	//theConf->SetBlob("MainWindow/BoxTree_Columns", m_pBoxTree->saveState());
	theConf->SetBlob("MainWindow/LogList_Columns", m_pMessageLog->GetView()->header()->saveState());
	theConf->SetBlob("MainWindow/ResMonList_Columns", m_pResourceLog->GetView()->header()->saveState());
	theConf->SetBlob("MainWindow/ApiLogList_Columns", m_pApiCallLog->GetView()->header()->saveState());
	theConf->SetBlob("MainWindow/Log_Splitter", m_pLogSplitter->saveState());
	theConf->SetBlob("MainWindow/Panel_Splitter", m_pPanelSplitter->saveState());
	theConf->SetValue("MainWindow/LogTab", m_pLogTabs->currentIndex());
}

QIcon CSandMan::GetIcon(const QString& Name)
{
	QString Path = QApplication::applicationDirPath() + "/Icons/" + Name + ".png";
	if(QFile::exists(Path))
		return QIcon(Path);
	return QIcon(":/Actions/" + Name + ".png");
}

void CSandMan::CreateMenus()
{
	connect(menuBar(), SIGNAL(hovered(QAction*)), this, SLOT(OnMenuHover(QAction*)));

	m_pMenuFile = menuBar()->addMenu(tr("&Sandbox"));
		m_pNew = m_pMenuFile->addAction(CSandMan::GetIcon("NewBox"), tr("Create New Box"), this, SLOT(OnNewBox()));
		m_pMenuFile->addSeparator();
		m_pEmptyAll = m_pMenuFile->addAction(CSandMan::GetIcon("EmptyAll"), tr("Terminate All Processes"), this, SLOT(OnEmptyAll()));
		m_pDisableForce = m_pMenuFile->addAction(tr("Disable Forced Programs"), this, SLOT(OnDisableForce()));
		m_pDisableForce->setCheckable(true);
		m_pMenuFile->addSeparator();
		m_pMaintenance = m_pMenuFile->addMenu(CSandMan::GetIcon("Maintenance"), tr("&Maintenance"));
			m_pConnect = m_pMaintenance->addAction(CSandMan::GetIcon("Connect"), tr("Connect"), this, SLOT(OnMaintenance()));
			m_pDisconnect = m_pMaintenance->addAction(CSandMan::GetIcon("Disconnect"), tr("Disconnect"), this, SLOT(OnMaintenance()));
			m_pMaintenance->addSeparator();
			m_pStopAll = m_pMaintenance->addAction(CSandMan::GetIcon("Stop"), tr("Stop All"), this, SLOT(OnMaintenance()));
			m_pMaintenance->addSeparator();
			m_pMaintenanceItems = m_pMaintenance->addMenu(CSandMan::GetIcon("ManMaintenance"), tr("&Advanced"));
				m_pInstallDrv = m_pMaintenanceItems->addAction(tr("Install Driver"), this, SLOT(OnMaintenance()));
				m_pStartDrv = m_pMaintenanceItems->addAction(tr("Start Driver"), this, SLOT(OnMaintenance()));
				m_pStopDrv = m_pMaintenanceItems->addAction(tr("Stop Driver"), this, SLOT(OnMaintenance()));
				m_pUninstallDrv = m_pMaintenanceItems->addAction(tr("Uninstall Driver"), this, SLOT(OnMaintenance()));
				m_pMaintenanceItems->addSeparator();
				m_pInstallSvc = m_pMaintenanceItems->addAction(tr("Install Service"), this, SLOT(OnMaintenance()));
				m_pStartSvc = m_pMaintenanceItems->addAction(tr("Start Service"), this, SLOT(OnMaintenance()));
				m_pStopSvc = m_pMaintenanceItems->addAction(tr("Stop Service"), this, SLOT(OnMaintenance()));
				m_pUninstallSvc = m_pMaintenanceItems->addAction(tr("Uninstall Service"), this, SLOT(OnMaintenance()));
				
		m_pMenuFile->addSeparator();
		m_pExit = m_pMenuFile->addAction(CSandMan::GetIcon("Exit"), tr("Exit"), this, SLOT(OnExit()));


	m_pMenuView = menuBar()->addMenu(tr("&View"));

		m_pViewMode = new QActionGroup(m_pMenuView);
		MakeAction(m_pViewMode, m_pMenuView, tr("Simple View"), false);
		MakeAction(m_pViewMode, m_pMenuView, tr("Advanced View"), true);
		connect(m_pViewMode, SIGNAL(triggered(QAction*)), this, SLOT(OnViewMode(QAction*)));
		m_iMenuViewPos = m_pMenuView->actions().count();
		m_pMenuView->addSeparator();

		m_pCleanUpMenu = m_pMenuView->addMenu(CSandMan::GetIcon("Clean"), tr("Clean Up"));
			m_pCleanUpProcesses = m_pCleanUpMenu->addAction(tr("Cleanup Processes"), this, SLOT(OnCleanUp()));
			m_pCleanUpMenu->addSeparator();
			m_pCleanUpMsgLog = m_pCleanUpMenu->addAction(tr("Cleanup Message Log"), this, SLOT(OnCleanUp()));
			m_pCleanUpResLog = m_pCleanUpMenu->addAction(tr("Cleanup Resource Log"), this, SLOT(OnCleanUp()));
			m_pCleanUpApiLog = m_pCleanUpMenu->addAction(tr("Cleanup Api Call Log"), this, SLOT(OnCleanUp()));

		m_pKeepTerminated = m_pMenuView->addAction(CSandMan::GetIcon("Keep"), tr("Keep terminated"), this, SLOT(OnSetKeep()));
		m_pKeepTerminated->setCheckable(true);

	m_pMenuOptions = menuBar()->addMenu(tr("&Options"));
		m_pMenuSettings = m_pMenuOptions->addAction(CSandMan::GetIcon("Settings"), tr("Global Settings"), this, SLOT(OnSettings()));
		m_pMenuOptions->addSeparator();
		m_pEditIni = m_pMenuOptions->addAction(CSandMan::GetIcon("EditIni"), tr("Edit ini file"), this, SLOT(OnEditIni()));
		m_pReloadIni = m_pMenuOptions->addAction(CSandMan::GetIcon("ReloadIni"), tr("Reload ini file"), this, SLOT(OnReloadIni()));
		m_pMenuOptions->addSeparator();
		m_pEnableMonitoring = m_pMenuOptions->addAction(CSandMan::GetIcon("SetLogging"), tr("Resource Logging"), this, SLOT(OnSetMonitoring()));
		m_pEnableMonitoring->setCheckable(true);
		m_pMenuOptions->addSeparator();
		m_pEnableLogging = m_pMenuOptions->addAction(CSandMan::GetIcon("LogAPI"), tr("API Call Logging"), this, SLOT(OnSetLogging()));
		m_pEnableLogging->setCheckable(true);
		

	m_pMenuHelp = menuBar()->addMenu(tr("&Help"));
		//m_pMenuHelp->addAction(tr("Support Sandboxie-Plus on Patreon"), this, SLOT(OnHelp()));
		m_pSupport = m_pMenuHelp->addAction(tr("Support Sandboxie-Plus with a Donation"), this, SLOT(OnHelp()));
		m_pForum = m_pMenuHelp->addAction(tr("Visit Support Forum"), this, SLOT(OnHelp()));
		m_pManual = m_pMenuHelp->addAction(tr("Online Documentation"), this, SLOT(OnHelp()));
		m_pMenuHelp->addSeparator();
		m_pUpdate = m_pMenuHelp->addAction(tr("Check for Updates"), this, SLOT(CheckForUpdates()));
		m_pMenuHelp->addSeparator();
		m_pAboutQt = m_pMenuHelp->addAction(tr("About the Qt Framework"), this, SLOT(OnAbout()));
		m_pAbout = m_pMenuHelp->addAction(QIcon(":/SandMan.png"), tr("About Sandboxie-Plus"), this, SLOT(OnAbout()));
}

void CSandMan::CreateToolBar()
{
	m_pToolBar = new QToolBar();
	m_pMainLayout->insertWidget(0, m_pToolBar);

	m_pToolBar->addAction(m_pMenuSettings);
	m_pToolBar->addSeparator();

	//m_pToolBar->addAction(m_pMenuNew);
	//m_pToolBar->addAction(m_pMenuEmptyAll);
	//m_pToolBar->addSeparator();
	m_pToolBar->addAction(m_pKeepTerminated);
	//m_pToolBar->addAction(m_pCleanUp);

	m_pCleanUpButton = new QToolButton();
	m_pCleanUpButton->setIcon(CSandMan::GetIcon("Clean"));
	m_pCleanUpButton->setToolTip(tr("Cleanup"));
	m_pCleanUpButton->setPopupMode(QToolButton::MenuButtonPopup);
	m_pCleanUpButton->setMenu(m_pCleanUpMenu);
	//QObject::connect(m_pCleanUpButton, SIGNAL(triggered(QAction*)), , SLOT());
	QObject::connect(m_pCleanUpButton, SIGNAL(pressed()), this, SLOT(OnCleanUp()));
	m_pToolBar->addWidget(m_pCleanUpButton);


	m_pToolBar->addSeparator();
	m_pToolBar->addAction(m_pEditIni);
	m_pToolBar->addSeparator();
	m_pToolBar->addAction(m_pEnableMonitoring);
	m_pToolBar->addAction(m_pEnableLogging);
	m_pToolBar->addSeparator();
	

	QWidget* pSpacer = new QWidget();
	pSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
	m_pToolBar->addWidget(pSpacer);

	//m_pToolBar->addAction(m_pMenuElevate);

	m_pToolBar->addSeparator();
	m_pToolBar->addWidget(new QLabel("        "));
	QLabel* pSupport = new QLabel("<a href=\"https://www.patreon.com/DavidXanatos\">Support Sandboxie-Plus on Patreon</a>");
	pSupport->setTextInteractionFlags(Qt::TextBrowserInteraction);
	connect(pSupport, SIGNAL(linkActivated(const QString&)), this, SLOT(OnHelp()));
	m_pToolBar->addWidget(pSupport);
	m_pToolBar->addWidget(new QLabel("        "));
}

void CSandMan::OnExit()
{
	m_bExit = true;
	close();
}

void CSandMan::closeEvent(QCloseEvent *e)
{
	if (!m_bExit)
	{
		QString OnClose = theConf->GetString("Options/OnClose", "ToTray");
		if (m_pTrayIcon->isVisible() && OnClose.compare("ToTray", Qt::CaseInsensitive) == 0)
		{
			StoreState();
			hide();

			if (theAPI->GetGlobalSettings()->GetBool("ForgetPassword", false))
				theAPI->ClearPassword();

			e->ignore();
			return;
		}
		else if(OnClose.compare("Prompt", Qt::CaseInsensitive) == 0)
		{
			CExitDialog ExitDialog(tr("Do you want to close Sandboxie Manager?"));
			if (!ExitDialog.exec())
			{
				e->ignore();
				return;
			}
		}
	}

	if (IsFullyPortable() && theAPI->IsConnected())
	{
		int PortableStop = theConf->GetInt("Options/PortableStop", -1);
		if (PortableStop == -1)
		{
			bool State = false;
			PortableStop = CCheckableMessageBox::question(this, "Sandboxie-Plus", tr("Sandboxie-Plus was running in portable mode, now it has to clean up the created services, this will prompt for administrative privileges.")
				, tr("Don't show this message again."), &State, QDialogButtonBox::Ok | QDialogButtonBox::Cancel, QDialogButtonBox::Ok, QMessageBox::Information) == QDialogButtonBox::Ok ? 1 : 0;

			if (!PortableStop)
			{
				e->ignore();
				return;
			}

			if (State)
				theConf->SetValue("Options/PortableStop", PortableStop);
		}

		StopSbie(true);
	}

	QApplication::quit();
}

bool CSandMan::IsFullyPortable()
{
	QString SbiePath = theAPI->GetSbiePath();
	QString AppPath = QApplication::applicationDirPath().replace("/", "\\");
	return (theConf->IsPortable() && SbiePath.compare(AppPath, Qt::CaseInsensitive) == 0);
}

void CSandMan::OnMessage(const QString& Message)
{
	if (Message == "ShowWnd")
	{
		if (!isVisible())
			show();
		setWindowState(Qt::WindowActive);
		SetForegroundWindow(MainWndHandle);
	}
	else if (Message.left(6) == "Status")
	{
		QString Status = Message.mid(7);
		if (Status != "OK")
		{
			if(m_bStopPending)
				QMessageBox::warning(NULL, tr("Sandboxie-Plus - Error"), tr("Failed to stop all sandboxie components"));
			else if(m_bConnectPending)
				QMessageBox::warning(NULL, tr("Sandboxie-Plus - Error"), tr("Failed to start required sandboxie components"));

			OnLogMessage(tr("Maintenance operation %1").arg(Status));
			CheckResults(QList<SB_STATUS>() << SB_ERR(Status));
		}
		else
		{
			OnLogMessage(tr("Maintenance operation Successful"));
			if (m_bConnectPending)
				ConnectSbieImpl();
		}
		m_pProgressDialog->hide();
		m_bConnectPending = false;
		m_bStopPending = false;
	}
}

void CSandMan::timerEvent(QTimerEvent* pEvent)
{
	if (pEvent->timerId() != m_uTimerID)
		return;

	if (theAPI->IsConnected())
	{
		theAPI->ReloadBoxes();
		theAPI->UpdateProcesses(m_pKeepTerminated->isChecked());

		m_pDisableForce->setChecked(theAPI->AreForceProcessDisabled());
		m_pDisableForce2->setChecked(theAPI->AreForceProcessDisabled());
	}

	if (m_bIconEmpty != (theAPI->TotalProcesses() == 0) || m_bIconDisabled != theAPI->AreForceProcessDisabled())
	{
		m_bIconEmpty = (theAPI->TotalProcesses() == 0);
		m_bIconDisabled = theAPI->AreForceProcessDisabled();

		QString IconFile = ":/SandMan";
		if (m_bIconEmpty)
			IconFile += "2";
		if(m_bIconDisabled)
			IconFile += "N";

		QIcon Icon;
		Icon.addFile(IconFile + ".png");
		m_pTrayIcon->setIcon(Icon);
	}

	if (!isVisible() || windowState().testFlag(Qt::WindowMinimized))
		return;

	m_pBoxView->Refresh();

	OnSelectionChanged();

	int iCheckUpdates = theConf->GetInt("Options/CheckForUpdates", 2);
	if (iCheckUpdates != 0)
	{
		time_t NextUpdateCheck = theConf->GetUInt64("Options/NextCheckForUpdates", 0);
		if (NextUpdateCheck == 0)
			theConf->SetValue("Options/NextCheckForUpdates", QDateTime::currentDateTime().addDays(7).toTime_t());
		else if(QDateTime::currentDateTime().toTime_t() >= NextUpdateCheck)
		{
			if (iCheckUpdates == 2)
			{
				bool bCheck = false;
				iCheckUpdates = CCheckableMessageBox::question(this, "Sandboxie-Plus", tr("Do you want to check if there is a new version of Sandboxie-Plus?")
					, tr("Don't show this message again."), &bCheck, QDialogButtonBox::Yes | QDialogButtonBox::No, QDialogButtonBox::Yes, QMessageBox::Information) == QDialogButtonBox::Ok ? 1 : 0;

				if (bCheck)
					theConf->SetValue("Options/CheckForUpdates", iCheckUpdates);
			}

			if (iCheckUpdates == 0)
				theConf->SetValue("Options/NextCheckForUpdates", QDateTime::currentDateTime().addDays(7).toTime_t());
			else
			{
				theConf->SetValue("Options/NextCheckForUpdates", QDateTime::currentDateTime().addDays(1).toTime_t());
				
				CheckForUpdates(false);
			}
		}
	}

	if (!m_pUpdateProgress.isNull() && m_RequestManager != NULL) {
		if (m_pUpdateProgress->IsCanceled()) {
			m_pUpdateProgress->Finish(SB_OK);
			m_pUpdateProgress.clear();

			m_RequestManager->AbortAll();
		}
	}
}

void CSandMan::OnBoxClosed(const QString& BoxName)
{
	CSandBoxPtr pBox = theAPI->GetBoxByName(BoxName);
	if (!pBox)
		return;

	if (!pBox->GetBool("NeverDelete", false) && pBox->GetBool("AutoDelete", false))
	{
		CRecoveryWindow* pRecoveryWindow = new CRecoveryWindow(pBox, this);
		if (pRecoveryWindow->FindFiles() == 0)
			delete pRecoveryWindow;
		else if (pRecoveryWindow->exec() != 1)
			return;

		SB_PROGRESS Status = pBox->CleanBox();
		if (Status.GetStatus() == OP_ASYNC)
			AddAsyncOp(Status.GetValue());
	}
}

void CSandMan::OnSelectionChanged()
{
	QList<CBoxedProcessPtr>	Processes = m_pBoxView->GetSelectedProcesses();
	/*if (Processes.isEmpty())
	{
		QList<CSandBoxPtr>Boxes = m_pBoxView->GetSelectedBoxes();
		foreach(const CSandBoxPtr& pBox, Boxes)
			Processes.append(pBox->GetProcessList().values());
	}*/

	QSet<quint64> Pids;
	foreach(const CBoxedProcessPtr& pProcess, Processes)
		Pids.insert(pProcess->GetProcessId());

	QList<CResLogEntryPtr> ResourceLog = theAPI->GetResLog();
	m_pResMonModel->Sync(ResourceLog, Pids);

	if (m_ApiLog) {
		QList<CApiLogEntryPtr> ApiCallLog = m_ApiLog->GetApiLog();
		m_pApiMonModel->Sync(ApiCallLog, Pids);
	}
}

void CSandMan::OnStatusChanged()
{
	QString appTitle = tr("Sandboxie-Plus v%1").arg(GetVersion());
	if (theAPI->IsConnected())
	{
		statusBar()->showMessage(tr("Driver version: %1").arg(theAPI->GetVersion()));

		//appTitle.append(tr("   -   Driver: v%1").arg(theAPI->GetVersion()));
		if(IsFullyPortable())
			appTitle.append(tr("   -   Portable"));

		OnLogMessage(tr("Sbie Directory: %1").arg(theAPI->GetSbiePath()));
		OnLogMessage(tr("Loaded Config: %1").arg(theAPI->GetIniPath()));

		if (theConf->GetBool("Options/AutoRunSoftCompat", true))
		{
			if (m_SbieTemplates->RunCheck())
			{
				CSettingsWindow* pSettingsWindow = new CSettingsWindow(this);
				//connect(pSettingsWindow, SIGNAL(OptionsChanged()), this, SLOT(UpdateSettings()));
				pSettingsWindow->showCompat();
			}
		}

		OnIniReloaded();

		if (theConf->GetBool("Options/WatchIni", true))
			theAPI->WatchIni(true);
	}
	else
	{
		appTitle.append(tr("   -   Driver NOT connected").arg(theAPI->GetVersion()));

		theAPI->WatchIni(false);
	}
	this->setWindowTitle(appTitle);
}

void CSandMan::OnMenuHover(QAction* action)
{
	//if (!menuBar()->actions().contains(action))
	//	return; // ignore sub menus


	if (menuBar()->actions().at(0) == action)
	{
		bool bConnected = theAPI->IsConnected();
		m_pConnect->setEnabled(!bConnected);
		m_pDisconnect->setEnabled(bConnected);

		m_pMaintenanceItems->setEnabled(!bConnected);

		bool DrvInstalled = CSbieUtils::IsInstalled(CSbieUtils::eDriver);
		bool DrvLoaded = CSbieUtils::IsRunning(CSbieUtils::eDriver);
		m_pInstallDrv->setEnabled(!DrvInstalled);
		m_pStartDrv->setEnabled(!DrvLoaded);
		m_pStopDrv->setEnabled(DrvLoaded);
		m_pUninstallDrv->setEnabled(DrvInstalled);

		bool SvcInstalled = CSbieUtils::IsInstalled(CSbieUtils::eService);
		bool SvcStarted = CSbieUtils::IsRunning(CSbieUtils::eService);
		m_pInstallSvc->setEnabled(!SvcInstalled);
		m_pStartSvc->setEnabled(!SvcStarted && DrvInstalled);
		m_pStopSvc->setEnabled(SvcStarted);
		m_pUninstallSvc->setEnabled(SvcInstalled);

		//m_pMenuStopAll - always enabled
	}
}

void CSandMan::OnLogMessage(const QString& Message, bool bNotify)
{
	QTreeWidgetItem* pItem = new QTreeWidgetItem(); // Time|Message
	pItem->setText(0, QDateTime::currentDateTime().toString("hh:mm:ss.zzz"));
	pItem->setText(1, Message);
	m_pMessageLog->GetTree()->addTopLevelItem(pItem);

	m_pMessageLog->GetView()->verticalScrollBar()->setValue(m_pMessageLog->GetView()->verticalScrollBar()->maximum());

	if (bNotify) {
		statusBar()->showMessage(Message);
		m_pTrayIcon->showMessage("Sandboxie-Plus", Message);
	}
}

void CSandMan::OnLogSbieMessage(quint32 MsgCode, const QStringList& MsgData, quint32 ProcessId)
{
	if ((MsgCode & 0xFFFF) == 2198) // file migration progress
	{
		m_pPopUpWindow->ShowProgress(MsgCode, MsgData, ProcessId);
		return;
	}

	QString Message = MsgCode != 0 ? theAPI->GetSbieMsgStr(MsgCode, m_LanguageId) : (MsgData.size() > 0 ? MsgData[0] : QString());

	for (int i = 1; i < MsgData.size(); i++)
		Message = Message.arg(MsgData[i]);

	if (ProcessId != 4) // if its not from the driver add the pid
	{
		CBoxedProcessPtr pProcess = theAPI->GetProcessById(ProcessId);
		if(pProcess.isNull())
			Message.prepend(tr("PID %1: ").arg(ProcessId));
		else
			Message.prepend(tr("%1 (%2): ").arg(pProcess->GetProcessName()).arg(ProcessId));
	}

	OnLogMessage(Message);

	if(MsgCode != 0 && theConf->GetBool("Options/ShowNotifications", true))
		m_pPopUpWindow->AddLogMessage(Message, MsgCode, MsgData, ProcessId);
}

void CSandMan::OnQueuedRequest(quint32 ClientPid, quint32 ClientTid, quint32 RequestId, const QVariantMap& Data)
{
	m_pPopUpWindow->AddUserPrompt(RequestId, Data, ClientPid);
}

void CSandMan::OnFileToRecover(const QString& BoxName, const QString& FilePath, quint32 ProcessId)
{
	m_pPopUpWindow->AddFileToRecover(FilePath, BoxName, ProcessId);
}

void CSandMan::OpenRecovery(const QString& BoxName)
{
	CSandBoxPtr pBox = theAPI->GetBoxByName(BoxName);
	if (!pBox)
		return;

	CRecoveryWindow* pRecoveryWindow = new CRecoveryWindow(pBox, this);
	pRecoveryWindow->FindFiles();
	pRecoveryWindow->show();
}

SB_PROGRESS CSandMan::RecoverFiles(const QList<QPair<QString, QString>>& FileList)
{
	CSbieProgressPtr pProgress = CSbieProgressPtr(new CSbieProgress());
	QtConcurrent::run(CSandMan::RecoverFilesAsync, pProgress, FileList);
	return SB_PROGRESS(OP_ASYNC, pProgress);
}

void CSandMan::RecoverFilesAsync(const CSbieProgressPtr& pProgress, const QList<QPair<QString, QString>>& FileList)
{
	SB_STATUS Status = SB_OK;

	QStringList Unrecovered;
	for (QList<QPair<QString, QString>>::const_iterator I = FileList.begin(); I != FileList.end(); ++I)
	{
		QString BoxPath = I->first;
		QString RecoveryPath = I->second;
		QString FileName = BoxPath.mid(BoxPath.lastIndexOf("\\") + 1);
		QString RecoveryFolder = RecoveryPath.left(RecoveryPath.lastIndexOf("\\") + 1);

		pProgress->ShowMessage(tr("Recovering file %1 to %2").arg(FileName).arg(RecoveryFolder));

		QDir().mkpath(RecoveryFolder);
		if (!QFile::rename(BoxPath, RecoveryPath))
			Unrecovered.append(BoxPath);
	}

	if (!Unrecovered.isEmpty())
		Status = SB_ERR(tr("Failed to recovery some files: \n") + Unrecovered.join("\n"));

	pProgress->Finish(Status);
}

void CSandMan::OnNotAuthorized(bool bLoginRequired, bool& bRetry)
{
	if (!bLoginRequired)
	{
		QMessageBox::warning(this, "Sandboxie-Plus", tr("Only Administrators can change the config."));
		return;
	}

	static bool LoginOpen = false;
	if (LoginOpen)
		return;
	LoginOpen = true;
	for (;;)
	{
		QString Value = QInputDialog::getText(this, "Sandboxie-Plus", tr("Please enter the configuration password."), QLineEdit::Password);
		if (Value.isEmpty())
			break;
		SB_STATUS Status = theAPI->UnlockConfig(Value);
		if (!Status.IsError()) {
			bRetry = true;
			break;
		}
		QMessageBox::warning(this, "Sandboxie-Plus", tr("Login Failed: %1").arg(Status.GetText()));
	}
	LoginOpen = false;
}

void CSandMan::OnNewBox()
{
	CNewBoxWindow NewBoxWindow;
	NewBoxWindow.exec();
}

void CSandMan::OnEmptyAll()
{
	theAPI->TerminateAll();
}

void CSandMan::OnDisableForce()
{
	bool Status = m_pDisableForce->isChecked();
	int Seconds = 0;
	if (Status)
	{
		bool bOK = false;
		Seconds = QInputDialog::getInt(this, "Sandboxie-Plus", tr("Please enter the duration for which disable forced programs."), 10, 0, 3600, 1, &bOK);
		if (!bOK)
			return;
	}
	theAPI->DisableForceProcess(Status, Seconds);
}

void CSandMan::OnDisableForce2()
{
	bool Status = m_pDisableForce2->isChecked();
	theAPI->DisableForceProcess(Status);
}

SB_STATUS CSandMan::ConnectSbie()
{
	SB_STATUS Status;

	if (!CSbieUtils::IsRunning(CSbieUtils::eAll)) 
	{
		if (!CSbieUtils::IsInstalled(CSbieUtils::eAll))
		{
			int PortableStart = theConf->GetInt("Options/PortableStart", -1);
			if (PortableStart == -1)
			{
				bool State = false;
				PortableStart = CCheckableMessageBox::question(this, "Sandboxie-Plus", tr("Sandboxie-Plus was started in portable mode and it needs to create nececery services, this will prompt for administrative privileges.")
					, tr("Don't show this message again."), &State, QDialogButtonBox::Ok | QDialogButtonBox::Cancel, QDialogButtonBox::Ok, QMessageBox::Information) == QDialogButtonBox::Ok ? 1 : 0;

				if (State)
					theConf->SetValue("Options/PortableStart", PortableStart);
			}

			if(!PortableStart)
				return SB_OK;
		}

		Status = CSbieUtils::Start(CSbieUtils::eAll);
	}

	if (Status.GetStatus() == OP_ASYNC)
		m_bConnectPending = true;
	else if (!Status.IsError())
		Status = ConnectSbieImpl();

	return Status;
}

SB_STATUS CSandMan::ConnectSbieImpl()
{
	SB_STATUS Status = theAPI->Connect(theConf->GetBool("Options/UseInteractiveQueue", true));

	if (Status && !CSbieAPI::IsSbieCtrlRunning()) // don't take over when SbieCtrl is up and running
		Status = theAPI->TakeOver();

	if (Status)
		Status = theAPI->ReloadBoxes();

	if (!Status)
		return Status;

	if (theAPI->GetAllBoxes().count() == 0) {
		OnLogMessage(tr("No sandboxes found; creating: %1").arg("DefaultBox"));
		theAPI->CreateBox("DefaultBox");
	}

	bool bIsMonitoring = theAPI->IsMonitoring();
	m_pResourceLog->setEnabled(bIsMonitoring);
	m_pEnableMonitoring->setChecked(bIsMonitoring);

	return SB_OK;
}

SB_STATUS CSandMan::DisconnectSbie()
{
	return theAPI->Disconnect();
}

SB_STATUS CSandMan::StopSbie(bool andRemove)
{
	SB_STATUS Status;

	if (theAPI->IsConnected()) {
		Status = theAPI->TerminateAll();
		theAPI->Disconnect();
	}
	if (!Status.IsError()) {
		if(andRemove)
			Status = CSbieUtils::Uninstall(CSbieUtils::eAll); // it stops it first ofcause
		else
			Status = CSbieUtils::Stop(CSbieUtils::eAll);
		if (Status.GetStatus() == OP_ASYNC)
			m_bStopPending = true;
	}

	return Status;
}

void CSandMan::OnMaintenance()
{
	SB_STATUS Status;
	if (sender() == m_pConnect)
		Status = ConnectSbie();
	else if (sender() == m_pDisconnect)
		Status = DisconnectSbie();
	else if (sender() == m_pStopAll)
		Status = StopSbie();

	// advanced
	else if (sender() == m_pInstallDrv)
		Status = CSbieUtils::Install(CSbieUtils::eDriver);
	else if (sender() == m_pStartDrv)
		Status = CSbieUtils::Start(CSbieUtils::eDriver);
	else if (sender() == m_pStopDrv)
		Status = CSbieUtils::Stop(CSbieUtils::eDriver);
	else if (sender() == m_pUninstallDrv)
		Status = CSbieUtils::Uninstall(CSbieUtils::eDriver);

	else if (sender() == m_pInstallSvc)
		Status = CSbieUtils::Install(CSbieUtils::eService);
	else if(sender() == m_pStartSvc)
		Status = CSbieUtils::Start(CSbieUtils::eService);
	else if(sender() == m_pStopSvc)
		Status = CSbieUtils::Stop(CSbieUtils::eService);
	else if (sender() == m_pUninstallSvc)
		Status = CSbieUtils::Uninstall(CSbieUtils::eService);

	if (Status.GetStatus() == OP_ASYNC) {
		statusBar()->showMessage(tr("Executing maintenance operation, please wait..."));
		m_pProgressDialog->show();
		return;
	}

	CheckResults(QList<SB_STATUS>() << Status);
}

void CSandMan::OnViewMode(QAction* pAction)
{
	bool bAdvanced = pAction->data().toBool();
	theConf->SetValue("Options/AdvancedView", bAdvanced);
	SetViewMode(bAdvanced);
}

void CSandMan::SetViewMode(bool bAdvanced)
{
	if (bAdvanced)
	{
		for (int i = m_iMenuViewPos; i < m_pMenuView->actions().count(); i++)
			m_pMenuView->actions().at(i)->setVisible(true);

		if (m_pMenuHelp->actions().first() != m_pSupport) {
			m_pMenuHelp->insertAction(m_pMenuHelp->actions().first(), m_pSupport);
			menuBar()->removeAction(m_pSupport);
		}

		m_pToolBar->show();
		m_pLogTabs->show();
		if (theConf->GetBool("Options/NoStatusBar", false))
			statusBar()->hide();
		else {
			statusBar()->show();
			//if (theConf->GetBool("Options/NoSizeGrip", false))
			//	statusBar()->setSizeGripEnabled(false);
		}
	}
	else
	{
		for (int i = m_iMenuViewPos; i < m_pMenuView->actions().count(); i++)
			m_pMenuView->actions().at(i)->setVisible(false);

		m_pMenuHelp->removeAction(m_pSupport);
		menuBar()->addAction(m_pSupport);

		m_pToolBar->hide();
		m_pLogTabs->hide();
		statusBar()->hide();
	}
}

void CSandMan::OnCleanUp()
{
	if (sender() == m_pCleanUpMsgLog || sender() == m_pCleanUpButton)
		m_pMessageLog->GetTree()->clear();
	
	if (sender() == m_pCleanUpResLog || sender() == m_pCleanUpButton)
		theAPI->ClearResLog();
	
	if (sender() == m_pCleanUpApiLog || sender() == m_pCleanUpButton)
		if(m_ApiLog) m_ApiLog->ClearApiLog();
	
	if (sender() == m_pCleanUpProcesses || sender() == m_pCleanUpButton)
		theAPI->UpdateProcesses(false);
}

void CSandMan::OnSetKeep()
{
	theConf->SetValue("Options/KeepTerminated", m_pKeepTerminated->isChecked());

	if(!m_pKeepTerminated->isChecked()) // clear on disable
		theAPI->UpdateProcesses(false);
}

void CSandMan::OnSettings()
{
	CSettingsWindow* pSettingsWindow = new CSettingsWindow(this);
	connect(pSettingsWindow, SIGNAL(OptionsChanged()), this, SLOT(UpdateSettings()));
	pSettingsWindow->show();
}

void CSandMan::UpdateSettings()
{
	SetDarkTheme(theConf->GetBool("Options/DarkTheme", false));

	//m_pBoxView->UpdateRunMenu();

	if (theConf->GetBool("Options/ShowSysTray", true))
		m_pTrayIcon->show();
	else
		m_pTrayIcon->hide();
}

void CSandMan::OnEditIni()
{
	if (theConf->GetBool("Options/NoEditInfo", true))
	{
		bool State = false;
		CCheckableMessageBox::question(this, "Sandboxie-Plus", tr("The changes will be applied automatically as soon as the editor is closed.")
			, tr("Don't show this message again."), &State, QDialogButtonBox::Ok, QDialogButtonBox::Ok, QMessageBox::Information);

		if (State)
			theConf->SetValue("Options/NoEditInfo", false);
	}

	wstring IniPath = theAPI->GetIniPath().toStdWString();

	SHELLEXECUTEINFO si = { 0 };
	si.cbSize = sizeof(SHELLEXECUTEINFO);
	si.fMask = SEE_MASK_NOCLOSEPROCESS;
	si.hwnd = NULL;
	si.lpVerb = L"runas";
	si.lpFile = L"notepad.exe";
	si.lpParameters = IniPath.c_str();
	si.lpDirectory = NULL;
	si.nShow = SW_SHOW;
	si.hInstApp = NULL;
	ShellExecuteEx(&si);
	//WaitForSingleObject(si.hProcess, INFINITE);
	//CloseHandle(si.hProcess);

	if (theConf->GetBool("Options/WatchIni", true))
		return; // if the ini s watched dont double reload
	
	QWinEventNotifier* processFinishedNotifier = new QWinEventNotifier(si.hProcess);
	processFinishedNotifier->setEnabled(true);
	connect(processFinishedNotifier, &QWinEventNotifier::activated, this, [processFinishedNotifier, this, si]() {
		processFinishedNotifier->setEnabled(false);
		processFinishedNotifier->deleteLater();
		this->OnReloadIni();
		CloseHandle(si.hProcess);
	});
}

void CSandMan::OnReloadIni()
{
	theAPI->ReloadConfig();
}

void CSandMan::OnIniReloaded()
{
	m_pBoxView->ReloadGroups();
	m_pPopUpWindow->ReloadHiddenMessages();
}

void CSandMan::OnSetMonitoring()
{
	theAPI->EnableMonitor(m_pEnableMonitoring->isChecked());

	if(m_pEnableMonitoring->isChecked() && !m_pToolBar->isVisible())
		m_pLogTabs->show();

	m_pResourceLog->setEnabled(m_pEnableMonitoring->isChecked());
}

void CSandMan::OnSetLogging()
{
	if (m_pEnableLogging->isChecked())
	{
		if (theConf->GetBool("Options/ApiLogInfo", true))
		{
			QString Message = tr("To use API logging you must first set up the LogApiDll from https://github.com/sandboxie-plus/LogApiDll with one or more sand boxes.\n"
				"Please download the latest release and set it up with the sandboxie.ini as instructed in the README.md of the project.");

			bool State = false;
			CCheckableMessageBox::question(this, "Sandboxie-Plus", Message
				, tr("Don't show this message again."), &State, QDialogButtonBox::Ok, QDialogButtonBox::Ok, QMessageBox::Information);

			if (State)
				theConf->SetValue("Options/ApiLogInfo", false);
		}

		if (!m_pToolBar->isVisible())
			m_pLogTabs->show();

		if (!m_ApiLog) {
			m_ApiLog = new CApiLog();
			m_pApiCallLog->setEnabled(true);
		}
	}
	else
	{
		if (m_ApiLog) {
			m_pApiCallLog->setEnabled(false);
			m_ApiLog->deleteLater();
			m_ApiLog = NULL;
		}
	}
}

void CSandMan::AddAsyncOp(const CSbieProgressPtr& pProgress)
{
	m_pAsyncProgress.insert(pProgress.data(), pProgress);
	connect(pProgress.data(), SIGNAL(Message(const QString&)), this, SLOT(OnAsyncMessage(const QString&)));
	connect(pProgress.data(), SIGNAL(Progress(int)), this, SLOT(OnAsyncProgress(int)));
	connect(pProgress.data(), SIGNAL(Finished()), this, SLOT(OnAsyncFinished()));

	m_pProgressDialog->OnStatusMessage("");
	m_pProgressDialog->show();

	if (pProgress->IsFinished()) // Note: the operation runs asynchroniusly it may have already finished so we need to test for that
		OnAsyncFinished(pProgress.data());
}

void CSandMan::OnAsyncFinished()
{
	OnAsyncFinished(qobject_cast<CSbieProgress*>(sender()));
}

void CSandMan::OnAsyncFinished(CSbieProgress* pSender)
{
	CSbieProgressPtr pProgress = m_pAsyncProgress.take(pSender);
	if (pProgress.isNull())
		return;
	disconnect(pProgress.data() , SIGNAL(Finished()), this, SLOT(OnAsyncFinished()));

	SB_STATUS Status = pProgress->GetStatus();
	if(Status.IsError())
		CSandMan::CheckResults(QList<SB_STATUS>() << Status);

	if(m_pAsyncProgress.isEmpty())
		m_pProgressDialog->hide();
}

void CSandMan::OnAsyncMessage(const QString& Text)
{
	m_pProgressDialog->OnStatusMessage(Text);
}

void CSandMan::OnAsyncProgress(int Progress)
{
	m_pProgressDialog->OnProgressMessage("", Progress);
}

void CSandMan::OnCancelAsync()
{
	foreach(const CSbieProgressPtr& pProgress, m_pAsyncProgress)
		pProgress->Cancel();
}

void CSandMan::CheckResults(QList<SB_STATUS> Results)
{
	for (QList<SB_STATUS>::iterator I = Results.begin(); I != Results.end(); )
	{
		if (!I->IsError() || I->GetStatus() == OP_CANCELED)
			I = Results.erase(I);
		else
			I++;
	}

	if (Results.count() == 1)
		QMessageBox::warning(NULL, tr("Sandboxie-Plus - Error"), Results[0].GetText());
	else if (Results.count() > 1)
	{
		CMultiErrorDialog Dialog(tr("Operation failed for %1 item(s).").arg(Results.size()), Results);
		Dialog.exec();
	}
}

void CSandMan::OnShowHide()
{
	if (isVisible()) {
		StoreState();
		hide();
	} else
		show();
}

void CSandMan::OnSysTray(QSystemTrayIcon::ActivationReason Reason)
{
	static bool TriggerSet = false;
	static bool NullifyTrigger = false;
	switch(Reason)
	{
		case QSystemTrayIcon::Context:
			m_pTrayMenu->popup(QCursor::pos());	
			break;
		case QSystemTrayIcon::DoubleClick:
			if (isVisible())
			{
				if(TriggerSet)
					NullifyTrigger = true;
				
				StoreState();
				hide();
				
				if (theAPI->GetGlobalSettings()->GetBool("ForgetPassword", false))
					theAPI->ClearPassword();

				break;
			}
			show();
		case QSystemTrayIcon::Trigger:
#ifdef WIN32
			if (isVisible() && !TriggerSet)
			{
				TriggerSet = true;
				QTimer::singleShot(100, [this]() { 
					TriggerSet = false;
					if (NullifyTrigger) {
						NullifyTrigger = false;
						return;
					}
					setWindowState(Qt::WindowActive);
					SetForegroundWindow(MainWndHandle);
				} );
			}
#endif
			break;
	}
}


QString CSandMan::GetVersion()
{
	QString Version = QString::number(VERSION_MJR) + "." + QString::number(VERSION_MIN) //.rightJustified(2, '0')
#if VERSION_REV > 0 || VERSION_MJR == 0
		+ "." + QString::number(VERSION_REV)
#endif
#if VERSION_UPD > 0
		+ QString('a' + VERSION_UPD - 1)
#endif
		;
	return Version;
}

void CSandMan::CheckForUpdates(bool bManual)
{
	if (!m_pUpdateProgress.isNull())
		return;

	m_pUpdateProgress = CSbieProgressPtr(new CSbieProgress());
	AddAsyncOp(m_pUpdateProgress);
	m_pUpdateProgress->ShowMessage(tr("Checking for updates..."));

	if (m_RequestManager == NULL) 
		m_RequestManager = new CNetworkAccessManager(30 * 1000, this);


	QUrlQuery Query;
	Query.addQueryItem("software", "sandboxie-plus");
	//QString Branche = theConf->GetString("Options/ReleaseBranche");
	//if (!Branche.isEmpty())
	//	Query.addQueryItem("branche", Branche);
	Query.addQueryItem("version", GetVersion());
	//Query.addQueryItem("version", QString::number(VERSION_MJR) + "." + QString::number(VERSION_MIN) + "." + QString::number(VERSION_REV) + "." + QString::number(VERSION_UPD));
	Query.addQueryItem("system", "windows-" + QSysInfo::kernelVersion() + "-" + QSysInfo::currentCpuArchitecture());
	Query.addQueryItem("language", QString::number(m_LanguageId));
	QString UpdateKey = theAPI->GetGlobalSettings()->GetText("UpdateKey"); // theConf->GetString("Options/UpdateKey");
	if (!UpdateKey.isEmpty())
		Query.addQueryItem("update_key", UpdateKey);
	Query.addQueryItem("auto", bManual ? "0" : "1");

	QUrl Url("https://xanasoft.com/update.php");
	Url.setQuery(Query);

	QNetworkRequest Request = QNetworkRequest(Url);
	Request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
	//Request.setRawHeader("Accept-Encoding", "gzip");
	QNetworkReply* pReply = m_RequestManager->get(Request);
	pReply->setProperty("manual", bManual);
	connect(pReply, SIGNAL(finished()), this, SLOT(OnUpdateCheck()));
}

void CSandMan::OnUpdateCheck()
{
	if (m_pUpdateProgress.isNull())
		return;

	QNetworkReply* pReply = qobject_cast<QNetworkReply*>(sender());
	QByteArray Reply = pReply->readAll();
	bool bManual = pReply->property("manual").toBool();
	pReply->deleteLater();

	m_pUpdateProgress->Finish(SB_OK);
	m_pUpdateProgress.clear();

	QVariantMap Data = QJsonDocument::fromJson(Reply).toVariant().toMap();
	if (Data.isEmpty() || Data["error"].toBool())
	{
		QString Error = Data.isEmpty() ? tr("server not reachable") : Data["errorMsg"].toString();
		OnLogMessage(tr("Failed to check for updates, error: %1").arg(Error), !bManual);
		if (bManual)
			QMessageBox::critical(this, "Sandboxie-Plus", tr("Failed to check for updates, error: %1").arg(Error));
		return;
	}

	bool bNothing = true;

	QStringList IgnoredUpdates = theConf->GetStringList("Options/IgnoredUpdates");

	QString UserMsg = Data["userMsg"].toString();
	if (!UserMsg.isEmpty())
	{
		QString MsgHash = QCryptographicHash::hash(Data["userMsg"].toByteArray(), QCryptographicHash::Md5).toHex().left(8);
		if (!IgnoredUpdates.contains(MsgHash))
		{
			CCheckableMessageBox mb(this);
			mb.setWindowTitle("Sandboxie-Plus");
			QIcon ico(QLatin1String(":/SandMan.png"));
			mb.setIconPixmap(ico.pixmap(64, 64));
			mb.setText(UserMsg);
			mb.setCheckBoxText(tr("Don't show this announcement in future."));
			mb.setStandardButtons(QDialogButtonBox::Close);
			mb.exec();

			if (mb.isChecked())
				theConf->SetValue("Options/IgnoredUpdates", IgnoredUpdates << MsgHash);

			bNothing = false;
		}
	}

	QString Version = Data["version"].toString();
	if (!Version.isEmpty() && Version != GetVersion())
	{
		if (bManual || !IgnoredUpdates.contains(Version)) // when checked manually always show result
		{
			bNothing = false;
			//QDateTime Updated = QDateTime::fromTime_t(Data["updated"].toULongLong());

			QString UpdateMsg = Data["updateMsg"].toString();
			QString UpdateUrl = Data["updateUrl"].toString();

			QString DownloadUrl = Data["downloadUrl"].toString();
			//	'sha256'
			//	'signature'

			QString FullMessage = UpdateMsg.isEmpty() ? tr("<p>There is a new version of Sandboxie-Plus available.<br /><font color='red'>New version:</font> <b>%1</b></p>").arg(Version) : UpdateMsg;
			if (!DownloadUrl.isEmpty())
				FullMessage += tr("<p>Do you want to download the latest version?</p>");
			else if (!UpdateUrl.isEmpty())
				FullMessage += tr("<p>Do you want to go to the <a href=\"%1\">download page</a>?</p>").arg(UpdateUrl);

			CCheckableMessageBox mb(this);
			mb.setWindowTitle("Sandboxie-Plus");
			QIcon ico(QLatin1String(":/SandMan.png"));
			mb.setIconPixmap(ico.pixmap(64, 64));
			//mb.setTextFormat(Qt::RichText);
			mb.setText(FullMessage);
			mb.setCheckBoxText(tr("Ignore this update, notify me anout the next one."));
			mb.setCheckBoxVisible(!bManual);

			if (!UpdateUrl.isEmpty() || !DownloadUrl.isEmpty()) {
				mb.setStandardButtons(QDialogButtonBox::Yes | QDialogButtonBox::No);
				mb.setDefaultButton(QDialogButtonBox::Yes);
			}
			else
				mb.setStandardButtons(QDialogButtonBox::Ok);

			mb.exec();

			if (mb.isChecked())
				theConf->SetValue("Options/IgnoredUpdates", IgnoredUpdates << Version);

			if (mb.clickedStandardButton() == QDialogButtonBox::Yes)
			{
				if (!DownloadUrl.isEmpty())
				{
					QNetworkRequest Request = QNetworkRequest(DownloadUrl);
					Request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
					//Request.setRawHeader("Accept-Encoding", "gzip");
					QNetworkReply* pReply = m_RequestManager->get(Request);
					connect(pReply, SIGNAL(finished()), this, SLOT(OnUpdateDownload()));
					connect(pReply, SIGNAL(downloadProgress(qint64, qint64)), this, SLOT(OnUpdateProgress(qint64, qint64)));

					m_pUpdateProgress = CSbieProgressPtr(new CSbieProgress());
					AddAsyncOp(m_pUpdateProgress);
					m_pUpdateProgress->ShowMessage(tr("Downloading new version..."));
				}
				else
					QDesktopServices::openUrl(UpdateUrl);
			}
		}
	}

	if (bNothing) 
	{
		theConf->SetValue("Options/NextCheckForUpdates", QDateTime::currentDateTime().addDays(7).toTime_t());

		if (bManual)
			QMessageBox::information(this, "Sandboxie-Plus", tr("No new updates found, your Sandboxie-Plus is up to date."));
	}
}

void CSandMan::OnUpdateProgress(qint64 bytes, qint64 bytesTotal)
{
	if (bytesTotal != 0 && !m_pUpdateProgress.isNull())
		m_pUpdateProgress->Progress(100 * bytes / bytesTotal);
}

void CSandMan::OnUpdateDownload()
{
	if (m_pUpdateProgress.isNull())
		return;

	QString TempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	if (TempDir.right(1) != "/")
		TempDir += "/";

	m_pUpdateProgress->Progress(-1);

	QNetworkReply* pReply = qobject_cast<QNetworkReply*>(sender());
	quint64 Size = pReply->bytesAvailable();
	QString Name = pReply->request().url().fileName();
	if (Name.isEmpty() || Name.right(4).compare(".exe", Qt::CaseInsensitive) != 0)
		Name = "Sandboxie-Plus-Install.exe";

	QString FilePath = TempDir + Name;

	QFile File(FilePath);
	if (File.open(QFile::WriteOnly)) {
		while (pReply->bytesAvailable() > 0)
			File.write(pReply->read(4096));
		File.close();
	}

	pReply->deleteLater();

	m_pUpdateProgress->Finish(SB_OK);
	m_pUpdateProgress.clear();

	if (File.size() != Size) {
		QMessageBox::critical(this, "Sandboxie-Plus", tr("Failed to download update from: %1").arg(pReply->request().url().toString()));
		return;
	}

	QString Message = tr("<p>New Sandboxie-Plus has been downloaded to the following location:</p><p><a href=\"%2\">%1</a></p><p>Do you want to begin the installation. If any programs are running sandboxed, they will be terminated.</p>")
		.arg(FilePath).arg("File:///" + TempDir);
	if (QMessageBox("Sandboxie-Plus", Message, QMessageBox::Information, QMessageBox::Yes | QMessageBox::Default, QMessageBox::No | QMessageBox::Escape, QMessageBox::NoButton).exec() == QMessageBox::Yes)
		QProcess::startDetached(FilePath);
}

void CSandMan::OnHelp()
{
	if (sender() == m_pSupport)
		QDesktopServices::openUrl(QUrl("https://xanasoft.com/go.php?to=donate"));
	else if (sender() == m_pForum)
		QDesktopServices::openUrl(QUrl("https://xanasoft.com/go.php?to=forum"));
	else if (sender() == m_pManual)
		QDesktopServices::openUrl(QUrl("https://xanasoft.com/go.php?to=sbie-docs"));
	else
		QDesktopServices::openUrl(QUrl("https://www.patreon.com/DavidXanatos"));
}

void CSandMan::OnAbout()
{
	if (sender() == m_pAbout)
	{
		QString AboutCaption = tr(
			"<h3>About Sandboxie-Plus</h3>"
			"<p>Version %1</p>"
			"<p>Copyright (c) 2020-2021 by DavidXanatos</p>"
		).arg(GetVersion());
		QString AboutText = tr(
			"<p>Sandboxie-Plus is an open source continuation of the well known Sandboxie.</p>"
			"<p></p>"
			"<p>Visit <a href=\"https://sandboxie-plus.com\">sandboxie-plus.com</a> for more information.</p>"
			"<p></p>"
			"<p></p>"
			"<p></p>"
			"<p>Icons from <a href=\"https://icons8.com\">icons8.com</a></p>"
			"<p></p>"
		);
		QMessageBox *msgBox = new QMessageBox(this);
		msgBox->setAttribute(Qt::WA_DeleteOnClose);
		msgBox->setWindowTitle(tr("About Sandboxie-Plus"));
		msgBox->setText(AboutCaption);
		msgBox->setInformativeText(AboutText);

		QIcon ico(QLatin1String(":/SandMan.png"));
		msgBox->setIconPixmap(ico.pixmap(128, 128));

		msgBox->exec();
	}
	else if (sender() == m_pAboutQt)
		QMessageBox::aboutQt(this);
}

void CSandMan::SetDarkTheme(bool bDark)
{
	if (bDark)
	{
		QApplication::setStyle(QStyleFactory::create("Fusion"));
		QPalette palette;
		palette.setColor(QPalette::Window, QColor(53, 53, 53));
		palette.setColor(QPalette::WindowText, Qt::white);
		palette.setColor(QPalette::Base, QColor(25, 25, 25));
		palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
		palette.setColor(QPalette::ToolTipBase, Qt::white);
		palette.setColor(QPalette::ToolTipText, Qt::white);
		palette.setColor(QPalette::Text, Qt::white);
		palette.setColor(QPalette::Button, QColor(53, 53, 53));
		palette.setColor(QPalette::ButtonText, Qt::white);
		palette.setColor(QPalette::BrightText, Qt::red);
		palette.setColor(QPalette::Link, QColor(218, 130, 42));
		palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
		palette.setColor(QPalette::HighlightedText, Qt::black);
		QApplication::setPalette(palette);
	}
	else
	{
		QApplication::setStyle(QStyleFactory::create(m_DefaultStyle));
		QApplication::setPalette(m_DefaultPalett);
	}

	CTreeItemModel::SetDarkMode(bDark);
	CListItemModel::SetDarkMode(bDark);
	CPopUpWindow::SetDarkMode(bDark);
}

void CSandMan::LoadLanguage()
{
	qApp->removeTranslator(&m_Translator);
	m_Translation.clear();
	m_LanguageId = 0;

	QString Lang = theConf->GetString("Options/Language");
	if (!Lang.isEmpty())
	{
		m_LanguageId = LocaleNameToLCID(Lang.toStdWString().c_str(), 0);

		QString LangAux = Lang; // Short version as fallback
		LangAux.truncate(LangAux.lastIndexOf('_'));

		QString LangPath = QApplication::applicationDirPath() + "/translations/sandman_";
		bool bAux = false;
		if (QFile::exists(LangPath + Lang + ".qm") || (bAux = QFile::exists(LangPath + LangAux + ".qm")))
		{
			QFile File(LangPath + (bAux ? LangAux : Lang) + ".qm");
			File.open(QFile::ReadOnly);
			m_Translation = File.readAll();
		}

		if (!m_Translation.isEmpty() && m_Translator.load((const uchar*)m_Translation.data(), m_Translation.size()))
			qApp->installTranslator(&m_Translator);
	}

	if (!m_LanguageId) 
		m_LanguageId = 1033; // default to englich
}

//////////////////////////////////////////////////////////////////////////////////////////
//
/*
#include <windows.h>
#include <shellapi.h>

#define RFF_NOBROWSE 0x0001
#define RFF_NODEFAULT 0x0002
#define RFF_CALCDIRECTORY 0x0004
#define RFF_NOLABEL 0x0008
#define RFF_NOSEPARATEMEM 0x0020
#define RFF_OPTRUNAS 0x0040

#define RFN_VALIDATE (-510)
#define RFN_LIMITEDRUNAS (-511)

#define RF_OK 0x0000
#define RF_CANCEL 0x0001
#define RF_RETRY 0x0002

typedef struct _NMRUNFILEDLGW
{
	NMHDR hdr;
	PWSTR lpszFile;
	PWSTR lpszDirectory;
	UINT ShowCmd;
} NMRUNFILEDLGW, *LPNMRUNFILEDLGW, *PNMRUNFILEDLGW;

QString g_RunDialogCommand;

BOOLEAN OnWM_Notify(NMHDR *Header, LRESULT *Result)
{
	LPNMRUNFILEDLGW runFileDlg = (LPNMRUNFILEDLGW)Header;
	if (Header->code == RFN_VALIDATE)
	{
		g_RunDialogCommand = QString::fromWCharArray(runFileDlg->lpszFile);

		*Result = RF_CANCEL;
		return TRUE;
	}
	//else if (Header->code == RFN_LIMITEDRUNAS)
	//{
	//
	//}
	return FALSE;
}

extern "C"
{
	NTSYSCALLAPI NTSTATUS NTAPI LdrGetProcedureAddress(IN PVOID DllHandle, IN VOID* ProcedureName OPTIONAL, IN ULONG ProcedureNumber OPTIONAL, OUT PVOID *ProcedureAddress, IN BOOLEAN RunInitRoutines);
	//NTSTATUS(NTAPI *LdrGetProcedureAddress)(HMODULE ModuleHandle, PANSI_STRING FunctionName, WORD Oridinal, PVOID *FunctionAddress);
}

BOOLEAN NTAPI ShowRunFileDialog(HWND WindowHandle, HICON WindowIcon, LPCWSTR WorkingDirectory, LPCWSTR WindowTitle, LPCWSTR WindowDescription, ULONG Flags)
{
	typedef BOOL(WINAPI *RunFileDlg_I)(HWND hwndOwner, HICON hIcon, LPCWSTR lpszDirectory, LPCWSTR lpszTitle, LPCWSTR lpszDescription, ULONG uFlags);

	BOOLEAN result = FALSE;

	if (HMODULE shell32Handle = LoadLibrary(L"shell32.dll"))
	{
		RunFileDlg_I dialog = NULL;
		if (LdrGetProcedureAddress(shell32Handle, NULL, 61, (void**)&dialog, TRUE) == 0)
			result = !!dialog(WindowHandle, WindowIcon, WorkingDirectory, WindowTitle, WindowDescription, Flags);

		FreeLibrary(shell32Handle);
	}

	return result;
}

QString ShowRunDialog(const QString& BoxName)
{
	g_RunDialogCommand.clear();
	wstring boxName = BoxName.toStdWString();
	ShowRunFileDialog(MainWndHandle, NULL, NULL, boxName.c_str(), L"Enter the path of a program that will be created in a sandbox.", 0); // RFF_OPTRUNAS);
	return g_RunDialogCommand;
}
*/
