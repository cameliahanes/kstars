/***************************************************************************
                          wiview.cpp  -  K Desktop Planetarium
                             -------------------
    begin                : 2012/26/05
    copyright            : (C) 2012 by Samikshan Bairagya
    email                : samikshan@gmail.com
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "wiview.h"
#include "QGraphicsObject"
#include "skymap.h"

#include "kstandarddirs.h"

WIView::WIView ( QObject *parent, ObsConditions *obs) : QObject(parent)
{

    m = new ModelManager(obs);

    QDeclarativeView *baseView = new QDeclarativeView();

    baseView->setAttribute(Qt::WA_TranslucentBackground);
    baseView->setStyleSheet("background: transparent;");

    ctxt = baseView->rootContext();

    baseView->setSource(KStandardDirs::locate("appdata","tools/whatsinteresting/qml/wiview.qml"));

    m_BaseObj = dynamic_cast<QObject *> (baseView->rootObject());

    //soTypeTextObj = m_BaseObj->findChild<QObject *>("soTypeTextObj");

    m_ViewsRowObj = m_BaseObj->findChild<QObject *>("viewsRowObj");
    connect(m_ViewsRowObj, SIGNAL(categorySelected(int)), this, SLOT(onCategorySelected(int)));

    m_SoListObj = m_BaseObj->findChild<QObject *>("soListObj");
    connect(m_SoListObj, SIGNAL(soListItemClicked(int, QString, int)), this, SLOT(onSoListItemClicked(int, QString, int)));

    m_DetailsViewObj = m_BaseObj->findChild<QObject *>("detailsViewObj");
    m_NextObj  = m_BaseObj->findChild<QObject *>("nextObj");
    connect(m_NextObj, SIGNAL(nextObjTextClicked()), this, SLOT(onNextObjTextClicked()));

    m_OptMag = obs->getOptimumMAG();

    baseView->setResizeMode(QDeclarativeView::SizeRootObjectToView);
    baseView->show();
}

WIView::~WIView()
{
    delete m;
    delete m_CurSoItem;
}

void WIView::onCategorySelected(int type)
{
    QString oMagText = QString("Suggested optimum magnification : ") + QString::number(m_OptMag);
    QObject* oMagTextObj = m_BaseObj->findChild<QObject *>("oMagTextObj");

    switch(type)
    {
    case 0:
    case 1:
    case 2:
        ctxt->setContextProperty("soListModel", m->returnModel( type ));
        break;
    case 3:
    case 4:
    case 5:
        ctxt->setContextProperty("soListModel", m->returnModel( type ));
        oMagTextObj->setProperty("text", oMagText);
        break;
    }
}

void WIView::onSoListItemClicked(int type, QString typeName, int index)
{
    SkyObjItem *soitem = m->returnModel(type)->getSkyObjItem(index);

    kDebug()<<soitem->getName()<<soitem->getType();
//    soTypeTextObj->setProperty("text", typeName);
//    soTypeTextObj->setProperty("visible", true);

//    soListObj->setProperty("visible", false);
    loadDetailsView(soitem , index);
}

void WIView::loadDetailsView(SkyObjItem* soitem, int index)
{
    QObject* sonameObj = m_DetailsViewObj->findChild<QObject *>("sonameObj");
    QObject* posTextObj = m_DetailsViewObj->findChild<QObject *>("posTextObj");
    QObject* descTextObj = m_DetailsViewObj->findChild<QObject *>("descTextObj");
    QObject* magTextObj = m_DetailsViewObj->findChild<QObject *>("magTextObj");
    sonameObj->setProperty("text", soitem->getName());
    posTextObj->setProperty("text", soitem->getPosition());
    descTextObj->setProperty("text", soitem->getDesc());
    magTextObj->setProperty("text", soitem->getMagnitude());

    //Slew map to selected sky object
    SkyObject* so = soitem->getSkyObject();
    KStars* data = KStars::Instance();
    if (so != 0) {
        data->map()->setFocusPoint( so );
        data->map()->setFocusObject( so );
        data->map()->setDestination( *data->map()->focusPoint() );
    }

    m_CurSoItem = soitem;
    m_CurIndex = index;
}

void WIView::onNextObjTextClicked()
{
    int modelSize = m->returnModel(m_CurSoItem->getType())->rowCount();
    SkyObjItem *nextItem = m->returnModel(m_CurSoItem->getType())->getSkyObjItem((m_CurIndex+1)%modelSize);
    loadDetailsView(nextItem, (m_CurIndex+1)%modelSize);
}
