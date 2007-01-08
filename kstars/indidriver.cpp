/***************************************************************************
                          INDI Driver
                             -------------------
    begin                : Wed May 7th 2003
    copyright            : (C) 2001 by Jasem Mutlaq
    email                : mutlaqja@ikarustech.com
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "indidriver.h"
#include "indimenu.h"
#include "ui_indihostconf.h"
#include "devicemanager.h"
#include "indidevice.h"
#include "indi/indicom.h"
#include "Options.h"

#include "kstars.h"
#include "kstarsdata.h"
#include "ksutils.h"

#include <QRadioButton>
#include <QFile>
#include <QTextStream>
#include <QTreeWidget>
#include <QIcon>
#include <QDialog>
#include <kactioncollection.h>
#include <kiconloader.h>
#include <k3listview.h>
#include <kmenu.h>
#include <kprocess.h>
#include <kmessagebox.h>
#include <kpushbutton.h>
#include <klineedit.h>
#include <kstandarddirs.h>
#include <kaction.h>
#include <kserversocket.h>
#include <kapplication.h>

//#include <kextsock.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define  MAX_RETRIES 3

DeviceManagerUI::DeviceManagerUI(QWidget *parent) : QFrame(parent)
{

  setupUi(this);

  localTreeWidget->setSortingEnabled(false);
  localTreeWidget->setRootIsDecorated(true);

  clientTreeWidget->setSortingEnabled(false);

  KIconLoader *icons = KIconLoader::global();
  runningPix = QIcon(icons->loadIcon( "exec", K3Icon::Small));
  stopPix    = QIcon(icons->loadIcon( "button_cancel", K3Icon::Small));
  localMode  = QIcon(icons->loadIcon( "network_local", K3Icon::Small));
  serverMode = QIcon(icons->loadIcon( "network", K3Icon::Small));

  connected           = QIcon(icons->loadIcon( "connect_established", K3Icon::Small));
  disconnected        = QIcon(icons->loadIcon( "connect_no", K3Icon::Small));
  establishConnection = QIcon(icons->loadIcon( "connect_creating", K3Icon::Small));

}

INDIDriver::INDIDriver( KStars *_ks )
    : KDialog( _ks ),  ksw( _ks )
{

    currentPort = Options::portStart().toInt()-1;
    lastGroup = NULL;
    lastDevice = NULL;

    ui = new DeviceManagerUI( this );
    setMainWidget( ui );
    setCaption( i18n( "Device Manager" ) );
    setButtons( KDialog::Close );

    foreach ( INDIHostsInfo * host, ksw->data()->INDIHostsList )
    {
  	QTreeWidgetItem *item = new QTreeWidgetItem(ui->clientTreeWidget, lastGroup);
	lastGroup = item;
	item->setIcon(0, ui->disconnected);
        item->setText(1, host->name);
	item->setText(2, host->portnumber);
    }

  lastGroup = NULL;

  QObject::connect(ui->addB, SIGNAL(clicked()), this, SLOT(addINDIHost()));
  QObject::connect(ui->modifyB, SIGNAL(clicked()), this, SLOT(modifyINDIHost()));
  QObject::connect(ui->removeB, SIGNAL(clicked()), this, SLOT(removeINDIHost()));


  //QObject::connect(ui->ClientpopMenu, SIGNAL(activated(int)), this, SLOT(processHostStatus(int)));
  //QObject::connect(ui->LocalpopMenu, SIGNAL(activated(int)), this, SLOT(processDeviceStatus(int)));

  QObject::connect(ksw->getINDIMenu(), SIGNAL(driverDisconnected(int)), this, SLOT(shutdownHost(int)));
  QObject::connect(ui->connectHostB, SIGNAL(clicked()), this, SLOT(activateHostConnection()));
  QObject::connect(ui->disconnectHostB, SIGNAL(clicked()), this, SLOT(activateHostDisconnection()));
  QObject::connect(ui->runServiceB, SIGNAL(clicked()), this, SLOT(activateRunService()));
  QObject::connect(ui->stopServiceB, SIGNAL(clicked()), this, SLOT(activateStopService()));
  QObject::connect(ui->localTreeWidget, SIGNAL(itemClicked(QTreeWidgetItem *, int)), this, SLOT(updateLocalButtons()));
  QObject::connect(ui->clientTreeWidget, SIGNAL(itemClicked(QTreeWidgetItem *, int)), this, SLOT(updateClientButtons()));

  readXMLDriver();
}

void INDIDriver::shutdownHost(int mgrID)
{
  QTreeWidgetItem *affectedItem;
  QList<QTreeWidgetItem *> found;

   foreach (INDIHostsInfo * host, ksw->data()->INDIHostsList)
   {
     if (host->mgrID == mgrID)
     {
	found = ui->clientTreeWidget->findItems(host->name, Qt::MatchExactly, 1);
        if (found.empty()) return;
	affectedItem = found.first();
	host->mgrID = -1;
	host->isConnected = false;
        affectedItem->setIcon(0, ui->disconnected);
	ui->connectHostB->setEnabled(true);
        ui->disconnectHostB->setEnabled(false);
	updateMenuActions();
	return;
     }
   }

  //return;

  //for (uint i=0; i < devices.size(); i++)
  foreach (IDevice *dev, devices)
  {
    if (dev->mgrID == mgrID)
    {
      //return;
      found = ui->localTreeWidget->findItems(dev->label, Qt::MatchExactly | Qt::MatchRecursive);
      if (found.empty()) return;
      affectedItem = found.first();
      //return;
      affectedItem->setIcon(1, ui->stopPix);
      affectedItem->setIcon(2, QIcon());
      affectedItem->setText(4, QString());
      ui->runServiceB->setEnabled(true);
      ui->stopServiceB->setEnabled(false);
      dev->managed = false;
      dev->restart();
      updateMenuActions();
      return;
    }
  }
}

void INDIDriver::activateRunService()
{
  processDeviceStatus(0);
}

void INDIDriver::activateStopService()
{
  processDeviceStatus(1);
}

void INDIDriver::activateHostConnection()
{
  processHostStatus(0);
}

void INDIDriver::activateHostDisconnection()
{
  processHostStatus(1);
}

void INDIDriver::updateLocalButtons()
{

  if (ui->localTreeWidget->currentItem() == NULL)
   return;

   foreach (IDevice *dev, devices)
   {
	
     if (ui->localTreeWidget->currentItem()->text(0) == dev->label)
     {
	ui->runServiceB->setEnabled(dev->state == 0);
	ui->stopServiceB->setEnabled(dev->state == 1);

	ui->serverLogText->clear();
        ui->serverLogText->append(dev->serverBuffer);

	return;
     }
   }

}

void INDIDriver::updateClientButtons()
{
 //INDIHostsInfo *hostInfo;
 if (ui->clientTreeWidget->currentItem() == NULL)
  return;


//for (uint i=0; i < ksw->data()->INDIHostsList.count(); i++)
   foreach (INDIHostsInfo * host, ksw->data()->INDIHostsList)
   {
     //hostInfo = ksw->data()->INDIHostsList.at(i);
     if (ui->clientTreeWidget->currentItem()->text(1) == host->name && ui->clientTreeWidget->currentItem()->text(2) == host->portnumber)
     {
       ui->connectHostB->setEnabled(!host->isConnected);
       ui->disconnectHostB->setEnabled(host->isConnected);
       break;
     }
    }

}


void INDIDriver::processDeviceStatus(int id)
{
  if (ui->localTreeWidget->currentItem() == NULL)
    return;

   int retries=0;

  foreach (IDevice *dev, devices)
     if (ui->localTreeWidget->currentItem()->text(0) == dev->label)
     {
	dev->state = (id == 0) ? 1 : 0;
	if (dev->state)
	{

	  ksw->getINDIMenu()->setCustomLabel(dev->label);
	  dev->label = ksw->getINDIMenu()->currentLabel;

	  dev->serverBuffer.clear();

	  if (!runDevice(dev))
	  {
	   dev->restart();
	   return;
	  }

	  if (dev->mode == IDevice::M_LOCAL)
	  {
	       //Allow time for the INDI server to listen
	  	usleep(50000);

		for (retries=0; retries <  MAX_RETRIES ; retries++)
		{
	  		if (!ksw->getINDIMenu()->processServer())
				usleep(50000);
			else
				break;
		}

		// Failed to connect to INDI server
		if (retries ==  MAX_RETRIES)
	  	{
			 KMessageBox::error(0, i18n("Connection to INDI host at localhost on port %1 failed.", dev->indiPort));
	   		dev->restart();
	   		return;
	  	}
	  }
	  

	  ui->localTreeWidget->currentItem()->setIcon(1, ui->runningPix);
	  ui->localTreeWidget->currentItem()->setText(4, QString::number(dev->indiPort));
	  ui->runServiceB->setEnabled(false);
	  ui->stopServiceB->setEnabled(true);

	  return;
	}

	  if (dev->mode == IDevice::M_LOCAL)
	  	ksw->getINDIMenu()->processServer();

	  ui->localTreeWidget->currentItem()->setIcon(1, ui->stopPix);
	  ui->localTreeWidget->currentItem()->setIcon(2, QIcon());
	  ui->localTreeWidget->currentItem()->setText(4, QString());
	  ui->runServiceB->setEnabled(true);
	  ui->stopServiceB->setEnabled(false);
	  dev->restart();

	  // TODO Do I need this? updateMenuActions();

     }
}

// Host Status = Remote Client
void INDIDriver::processHostStatus(int id)
{
   int mgrID;
   int retries=0;
   bool toConnect = (id == 0);
   QTreeWidgetItem *currentItem = ui->clientTreeWidget->currentItem();
   if (!currentItem) return;
   //INDIHostsInfo *hostInfo;

   //for (uint i=0; i < ksw->data()->INDIHostsList.count(); i++)
   foreach (INDIHostsInfo * host, ksw->data()->INDIHostsList)
   {
     //hostInfo = ksw->data()->INDIHostsList.at(i);
     if (currentItem->text(1) == host->name && currentItem->text(2) == host->portnumber)
     {
        // Nothing changed, return
        if (host->isConnected == toConnect)
	 return;

	// connect to host
	if (toConnect)
	{
	   // if connection successful
	  for (retries=0; retries < MAX_RETRIES; retries++)
	  {
          	if ( (mgrID = ksw->getINDIMenu()->processClient(host->hostname, host->portnumber)) >= 0)
	  	{
	    		currentItem->setIcon(0, ui->connected);
	    		host->isConnected = true;
	    		host->mgrID = mgrID;
	    		ui->connectHostB->setEnabled(false);
	    		ui->disconnectHostB->setEnabled(true);
			break;
		}
		else
			usleep(50000);
	   }

	  if (retries ==  MAX_RETRIES)
	  {
		 KMessageBox::error(0, i18n("Connection to INDI host at %1 on port %2 failed.", host->hostname, host->portnumber));
	   }
	}
	else
	{
	  ksw->getINDIMenu()->removeDeviceMgr(host->mgrID);
	  host->mgrID = mgrID = -1;
	  host->isConnected = false;
	  currentItem->setIcon(0, ui->disconnected);
	  ui->connectHostB->setEnabled(true);
	  ui->disconnectHostB->setEnabled(false);
	  updateMenuActions();
	}



       }
    }
}

void INDIDriver::updateMenuActions()
{


  // We iterate over devices, we enable INDI Control Panel if we have any active device
  // We enable capture image sequence if we have any imaging device

  QAction *tmpAction = NULL;
  INDIMenu *devMenu = ksw->getINDIMenu();
  bool activeDevice = false;
  bool activeImaging = false;
  INDI_P *imgProp = NULL;

  if (devMenu == NULL)
     return;

  if (devMenu->mgr.count() > 0)
   activeDevice = true;

  foreach(DeviceManager *dev_mgr, devMenu->mgr)
  {
	foreach (INDI_D *device, dev_mgr->indi_dev)
	{

        		imgProp = device->findProp("CCD_EXPOSE_DURATION");
			if (imgProp && device->isOn())
			{
			  activeImaging = true;
			  break;
			}
	}
  }

  tmpAction = ksw->actionCollection()->action("capture_sequence");

  if (!tmpAction)
  	kDebug() << "Warning: capture_sequence action not found" << endl;
  else
  	tmpAction->setEnabled(activeImaging);

  /* FIXME The following seems to cause a crash in KStars when we use
     the telescope wizard to automatically search for scopes. I can't
     find any correlation! */

  // Troubled Code START
  tmpAction = ksw->actionCollection()->action("indi_cpl");
  if (!tmpAction)
  	kDebug() << "Warning: indi_cpl action not found" << endl;
  else
    	tmpAction->setEnabled(activeDevice);
  // Troubled Code END


}

bool INDIDriver::runDevice(IDevice *dev)
{
  dev->indiPort = getINDIPort();

  if (dev->indiPort < 0)
  {
   KMessageBox::error(0, i18n("Cannot start INDI server: port error."));
   return false;
  }

  dev->proc = new KProcess;

  *dev->proc << "indiserver";
  *dev->proc << "-v" << "-r" << "0" << "-p" << QString::number(dev->indiPort) << dev->driver;

  // Check Mode
  dev->mode = ui->localR->isChecked() ? IDevice::M_LOCAL : IDevice::M_SERVER;

  if (dev->mode == IDevice::M_LOCAL)
    ui->localTreeWidget->currentItem()->setIcon(2, ui->localMode);
  else
    ui->localTreeWidget->currentItem()->setIcon(2, ui->serverMode);

  connect(dev->proc, SIGNAL(receivedStderr (KProcess *, char *, int)),  dev, SLOT(processstd(KProcess *, char*, int)));

  dev->proc->start(KProcess::NotifyOnExit, KProcess::Stderr);
  //dev->proc->start();

  return (dev->proc->isRunning());
}

void INDIDriver::removeDevice(IDevice *dev)
{

  //for (unsigned int i=0 ; i < devices.size(); i++)
    foreach (IDevice *device, devices)
     if (dev->label == device->label)
     	device->restart();
}

void INDIDriver::removeDevice(const QString &deviceLabel)
{
  //for (unsigned int i=0 ; i < devices.size(); i++)
    foreach (IDevice *device, devices)
     if (deviceLabel == device->label)
     	device->restart();

}

void INDIDriver::saveDevicesToDisk()
{

 QFile file;
 QString elementData;

 file.setFileName( KStandardDirs::locateLocal( "appdata", "drivers.xml" ) ); //determine filename in local user KDE directory tree.

 if ( !file.open( QIODevice::WriteOnly))
 {
  QString message = i18n( "unable to write to file 'drivers.xml'\nAny changes to INDI device drivers will not be saved." );
 KMessageBox::sorry( 0, message, i18n( "Could Not Open File" ) );
  return;
 }

 QTextStream outstream(&file);

 // Let's write drivers first
 outstream << "<ScopeDrivers>" << endl;
 for (int i=0; i < driversList.count(); i++)
  outstream << "       <driver>" << driversList[i] << "</driver>" << endl;
 outstream << "</ScopeDrivers>" << endl;

 // Next we write devices, in the following order:
 // Telescopes, CCDs, Filter Wheels, Video, Dome, GPS

  // #1 Telescopes
  outstream << "<devGroup group='Telescopes'>" << endl;
  //for (unsigned i=0; i < devices.size(); i++)
  foreach (IDevice *dev, devices)
  {
     if (dev->deviceType == KSTARS_TELESCOPE)
     {
	outstream << QString("<device label='%1' focal_length='%2' aperture='%3'>").arg(dev->label).arg(dev->focal_length > 0 ? dev->focal_length : -1).arg(dev->aperture > 0 ? dev->aperture : -1) << endl;

	outstream << "       <driver>" << dev->driver << "</driver>" << endl;
	outstream << "       <version>" << dev->version << "</version>" << endl;
        outstream << "</device>" << endl;
      }
   }
   outstream << "</devGroup>" << endl;

   // #2 CCDs
   outstream << "<devGroup group='CCDs'>" << endl;
   //for (unsigned i=0; i < devices.size(); i++)
   foreach (IDevice *dev, devices)
   {
     if (dev->deviceType == KSTARS_CCD)
     {
	outstream << QString("<device label='%1'>").arg(dev->label) << endl;
	outstream << "       <driver>" << dev->driver << "</driver>" << endl;
	outstream << "       <version>" << dev->version << "</version>" << endl;
        outstream << "</device>" << endl;
     }
  }
  outstream << "</devGroup>" << endl;

  // #3 Filter wheels
  outstream << "<devGroup group='Filter Wheels'>" << endl;

   foreach (IDevice *dev, devices)
   //for (unsigned i=0; i < devices.size(); i++)
   {
     if (dev->deviceType == KSTARS_FILTER)
     {
	outstream << QString("<device label='%1'>").arg(dev->label) << endl;
	outstream << "       <driver>" << dev->driver << "</driver>" << endl;
	outstream << "       <version>" << dev->version << "</version>" << endl;
        outstream << "</device>" << endl;
     }
  }
  outstream << "</devGroup>" << endl;

   // #4 Video
   outstream << "<devGroup group='Video'>" << endl;

   foreach (IDevice *dev, devices)
   //for (unsigned i=0; i < devices.size(); i++)
   {
     if (dev->deviceType == KSTARS_VIDEO)
     {
	outstream << QString("<device label='%1'>").arg(dev->label) << endl;
	outstream << "       <driver>" << dev->driver << "</driver>" << endl;
	outstream << "       <version>" << dev->version << "</version>" << endl;
        outstream << "</device>" << endl;
     }
   }
   outstream << "</devGroup>" << endl;

   file.close();

}

bool INDIDriver::isDeviceRunning(const QString &deviceLabel)
{

    foreach (IDevice *dev, devices)
    //for (unsigned int i=0 ; i < devices.size(); i++)
     if (deviceLabel == dev->label)
     {
       if (!dev->proc)
        return false;
       else return (dev->proc->isRunning());
     }

    return false;

}

int INDIDriver::getINDIPort()
{

  int lastPort  = Options::portEnd().toInt();;
  currentPort++;

   // recycle
   if (currentPort > lastPort) currentPort = Options::portStart().toInt();

  KNetwork::KServerSocket ss;
  ss.setFamily(KNetwork::KResolver::InetFamily);
  for(; currentPort <= lastPort; currentPort++)
  {
      ss.setAddress( QString::number( currentPort ) );
      bool success = ss.listen();
      if(success && ss.error() == KNetwork::KSocketBase::NoError )
      {
		ss.close();
		return currentPort;
      }
  }
   return -1;
}

bool INDIDriver::readXMLDriver()
{
  QString indiFile("drivers.xml");
  QFile file;
  char errmsg[1024];

  if ( !KSUtils::openDataFile( file, indiFile ) )
  {
     KMessageBox::error(0, i18n("Unable to find device driver file 'drivers.xml'. Please locate the file and place it in one of the following locations:\n\n \t$(KDEDIR)/share/apps/kstars/ \n\t~/.kde/share/apps/kstars/"));

    return false;
 }

 char c;
 LilXML *xmlParser = newLilXML();
 XMLEle *root = NULL;

 //while ( (c = (signed char) file.getch()) != -1)
 while ( file.getChar(&c))
 {
    root = readXMLEle(xmlParser, c, errmsg);

    if (root)
    {
      if (!buildDeviceGroup(root, errmsg))
         prXMLEle(stderr, root, 0);

      delXMLEle(root);
    }
    else if (errmsg[0])
    {
      kDebug() << QString(errmsg) << endl;
      return false;
    }
  }

 delLilXML(xmlParser);
 return true;

}

bool INDIDriver::buildDriversList( XMLEle *root, char* /*errmsg[]*/)
{

  XMLEle *ep;

  for (ep = nextXMLEle (root, 1); ep != NULL; ep = nextXMLEle (root, 0))
      driversList << pcdataXMLEle(ep);

  return true;

}

bool INDIDriver::buildDeviceGroup(XMLEle *root, char errmsg[])
{

  XMLAtt *ap;
  XMLEle *ep;
  QString groupName;
  int groupType = KSTARS_TELESCOPE;

  if (!strcmp(tagXMLEle(root), "ScopeDrivers"))
    return buildDriversList(root, errmsg);

  // avoid overflow
  if (strlen(tagXMLEle(root)) > 1024)
   return false;

  // Get device grouping name
  ap = findXMLAtt(root, "group");

  if (!ap)
  {
    snprintf(errmsg, ERRMSG_SIZE, "Tag %.64s does not have a group attribute", tagXMLEle(root));
    return false;
  }

  groupName = valuXMLAtt(ap);

  kDebug() << "Group Name: " << groupName << endl;

  if (groupName.indexOf("Telescopes") != -1)
    groupType = KSTARS_TELESCOPE;
  else if (groupName.indexOf("CCDs") != -1)
    groupType = KSTARS_CCD;
  else if (groupName.indexOf("Filter") != -1)
    groupType = KSTARS_FILTER;
  else if (groupName.indexOf("Video") != -1)
    groupType = KSTARS_VIDEO;
  else if (groupName.indexOf("Focusers") != -1)
    groupType = KSTARS_FOCUSER;
  else if (groupName.indexOf("Domes") != -1)
    groupType = KSTARS_DOME;
  else if (groupName.indexOf("GPS") != -1)
    groupType = KSTARS_GPS;


  //K3ListViewItem *group = new K3ListViewItem(topItem, lastGroup);
  QTreeWidgetItem *group = new QTreeWidgetItem(ui->localTreeWidget, lastGroup);
  group->setText(0, groupName);
  lastGroup = group;
  //group->setOpen(true);


  for (ep = nextXMLEle(root, 1) ; ep != NULL ; ep = nextXMLEle(root, 0))
  /*for (int i = 0; i < root->nel; i++)*/
	if (!buildDriverElement(ep, group, groupType, errmsg))
	  return false;

  return true;
}

bool INDIDriver::buildDriverElement(XMLEle *root, QTreeWidgetItem *DGroup, int groupType, char errmsg[])
{
  XMLAtt *ap;
  XMLEle *el;
  IDevice *dv;
  QString label;
  QString driver;
  QString version;
  double focal_length (-1), aperture (-1);

  ap = findXMLAtt(root, "label");
  if (!ap)
  {
    snprintf(errmsg, ERRMSG_SIZE, "Tag %.64s does not have a label attribute", tagXMLEle(root));
    return false;
  }

  label = valuXMLAtt(ap);

  // Let's look for telescope-specific attributes: focal length and aperture
  ap = findXMLAtt(root, "focal_length");
  if (ap)
   focal_length = QString(valuXMLAtt(ap)).toDouble();

  ap = findXMLAtt(root, "aperture");
  if (ap)
   aperture = QString(valuXMLAtt(ap)).toDouble();


  el = findXMLEle(root, "driver");

  if (!el)
   return false;

  driver = pcdataXMLEle(el);

  el = findXMLEle(root, "version");

  if (!el)
   return false;

  version = pcdataXMLEle(el);

  QTreeWidgetItem *device = new QTreeWidgetItem(DGroup, lastDevice);

  device->setText(0, QString(label));
  device->setIcon(1, ui->stopPix);
  device->setText(3, QString(version));

  lastDevice = device;

  dv = new IDevice(label, driver, version);
  dv->deviceType = groupType;
  connect(dv, SIGNAL(newServerInput()), this, SLOT(updateLocalButtons()));
  if (focal_length > 0)
   dv->focal_length = focal_length;
  if (aperture > 0)
   dv->aperture = aperture;

  devices.append(dv);

  // SLOTS/SIGNAL, pop menu, indi server logic
  return true;
}

int INDIDriver::activeDriverCount()
{
  int count = 0;

  //for (uint i=0; i < devices.size(); i++)
   foreach (IDevice *dev, devices)
    if (dev->state && dev->mode == IDevice::M_LOCAL)
      count++;

  //for (uint i=0; i < ksw->data()->INDIHostsList.count(); i++)
   foreach (INDIHostsInfo * host, ksw->data()->INDIHostsList)
    if (host->isConnected)
      count++;


  return count;

}

void INDIDriver::addINDIHost()
{
  QDialog hostConfDialog;
  Ui::INDIHostConf hostConf;
  hostConf.setupUi(&hostConfDialog);
  hostConfDialog.setWindowTitle(i18n("Add Host"));
  bool portOk = false;

  if (hostConfDialog.exec() == QDialog::Accepted)
  {
    INDIHostsInfo *hostItem = new INDIHostsInfo;
    hostItem->name        = hostConf.nameIN->text();
    hostItem->hostname    = hostConf.hostname->text();
    hostItem->portnumber  = hostConf.portnumber->text();
    hostItem->isConnected = false;
    hostItem->mgrID       = -1;

    hostItem->portnumber.toInt(&portOk);

    if (portOk == false)
    {
     KMessageBox::error(0, i18n("Error: the port number is invalid."));
     delete hostItem;
     return;
    }

    //search for duplicates
    //for (uint i=0; i < ksw->data()->INDIHostsList.count(); i++)
     foreach (INDIHostsInfo * host, ksw->data()->INDIHostsList)
     if (hostItem->name   == host->name &&  hostItem->portnumber == host->portnumber)
     {
       KMessageBox::error(0, i18n("Host: %1 Port: %2 already exists.", hostItem->name, hostItem->portnumber));
       delete hostItem;
       return;
     }

    ksw->data()->INDIHostsList.append(hostItem);

    QTreeWidgetItem *item = new QTreeWidgetItem(ui->clientTreeWidget);
    item->setIcon(0, ui->disconnected);
    item->setText(1, hostConf.nameIN->text());
    item->setText(2, hostConf.portnumber->text());

  }

    saveHosts();
}



void INDIDriver::modifyINDIHost()
{

  QDialog hostConfDialog;
  Ui::INDIHostConf hostConf;
  hostConf.setupUi(&hostConfDialog);
  hostConfDialog.setWindowTitle(i18n("Modify Host"));

  QTreeWidgetItem *currentItem = ui->clientTreeWidget->currentItem();

  if (currentItem == NULL)
   return;

  //for (uint i=0; i < ksw->data()->INDIHostsList.count(); i++)
   foreach (INDIHostsInfo * host, ksw->data()->INDIHostsList)
   {
     if (currentItem->text(1) == host->name && currentItem->text(2) == host->portnumber)
     {
        hostConf.nameIN->setText(host->name);
  	hostConf.hostname->setText(host->hostname);
  	hostConf.portnumber->setText(host->portnumber);

  	if (hostConfDialog.exec() == QDialog::Accepted)
  	{
    	//INDIHostsInfo *hostItem = new INDIHostsInfo;
	host->name       = hostConf.nameIN->text();
    	host->hostname   = hostConf.hostname->text();
    	host->portnumber = hostConf.portnumber->text();

    	currentItem->setText(1, hostConf.nameIN->text());
    	currentItem->setText(2, hostConf.portnumber->text());

    	//ksw->data()->INDIHostsList.replace(i, hostItem);

    	saveHosts();
	return;
  	}
     }
   }
}

void INDIDriver::removeINDIHost()
{

 if (ui->clientTreeWidget->currentItem() == NULL)
  return;

 for (int i=0; i < ksw->data()->INDIHostsList.count(); i++)
      if (ui->clientTreeWidget->currentItem()->text(1) == ksw->data()->INDIHostsList[i]->name &&
         ui->clientTreeWidget->currentItem()->text(2) == ksw->data()->INDIHostsList[i]->portnumber)
   {
        if (ksw->data()->INDIHostsList[i]->isConnected)
        {
           KMessageBox::error( 0, i18n("You need to disconnect the client before removing it."));
           return;
        }

        if (KMessageBox::warningContinueCancel( 0, i18n("Are you sure you want to remove the %1 client?", ui->clientTreeWidget->currentItem()->text(1)), i18n("Delete Confirmation"),KStandardGuiItem::del())!=KMessageBox::Continue)
           return;

 	delete ksw->data()->INDIHostsList.takeAt(i);
	//ui->clientTreeWidget->takeItem(ui->clientTreeWidget->currentItem());
	delete (ui->clientTreeWidget->currentItem());
	break;
   }



 saveHosts();

}

void INDIDriver::saveHosts()
{

 QFile file;
 QString hostData;

 file.setFileName( KStandardDirs::locateLocal( "appdata", "indihosts.xml" ) ); //determine filename in local user KDE directory tree.

 if ( !file.open( QIODevice::WriteOnly))
 {
  QString message = i18n( "unable to write to file 'indihosts.xml'\nAny changes to INDI hosts configurations will not be saved." );
 KMessageBox::sorry( 0, message, i18n( "Could Not Open File" ) );
  return;
 }

 QTextStream outstream(&file);

 //for (uint i= 0; i < ksw->data()->INDIHostsList.count(); i++)
  foreach (INDIHostsInfo * host, ksw->data()->INDIHostsList)
 {

 hostData  = "<INDIHost name='";
 hostData += host->name;
 hostData += "' hostname='";
 hostData += host->hostname;
 hostData += "' port='";
 hostData += host->portnumber;
 hostData += "' />\n";

 outstream << hostData;

 }

  file.close();

}

INDIDriver::~INDIDriver()
{

  //for (uint i=0; i < devices.size(); i++)
   //delete (devices[i]);
  while (!devices.isEmpty())
	delete devices.takeFirst();

}

IDevice::IDevice(const QString &inLabel, const QString &inDriver, const QString &inVersion)
{
  label = inLabel;;
  driver = inDriver;;
  version = inVersion;

  // Initially off
  state = 0;

  // No port initially
  indiPort = -1;

  // not yet managed by DeviceManager
  managed = false;

  mgrID = -1;

  focal_length = -1;
  aperture = -1;

  proc = NULL;

}

void IDevice::processstd(KProcess* /*proc*/, char* buffer, int /*buflen*/)
{
  serverBuffer.append(buffer);
  emit newServerInput();
}


IDevice::~IDevice()
{
  if (proc)
      proc->kill();

}

void IDevice::restart()
{

  mgrID = -1;

  state = 0;

  indiPort = -1;

  if (proc)
  	proc->kill();

  proc = NULL;

}

#include "indidriver.moc"
