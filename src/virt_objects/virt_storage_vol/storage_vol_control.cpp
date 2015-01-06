#include "storage_vol_control.h"

VirtStorageVolControl::VirtStorageVolControl(QWidget *parent) :
    QMainWindow(parent)
{
    setObjectName("VirtStorageVolControl");
    setWindowTitle("StorageVol Control");
    setSizePolicy(QSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::MinimumExpanding));
    setWindowIcon(QIcon::fromTheme("storageVol"));
    storageVolModel = new StorageVolModel();
    storageVolList = new QTreeView(this);
    storageVolList->setItemsExpandable(false);
    storageVolList->setRootIsDecorated(false);
    storageVolList->setModel(storageVolModel);
    storageVolList->setFocus();
    storageVolList->setContextMenuPolicy(Qt::CustomContextMenu);
    //connect(storageVolList, SIGNAL(doubleClicked(const QModelIndex&)),
    //        this, SLOT(storageVolDoubleClicked(const QModelIndex&)));
    connect(storageVolList, SIGNAL(customContextMenuRequested(const QPoint&)),
            this, SLOT(storageVolClicked(const QPoint&)));
    setCentralWidget(storageVolList);
    settings.beginGroup("VirtStorageVolControl");
    storageVolList->setColumnWidth(0, settings.value("column0", 132).toInt());
    storageVolList->setColumnWidth(1, settings.value("column1", 32).toInt());
    storageVolList->setColumnWidth(2, settings.value("column2", 32).toInt());
    storageVolList->setColumnWidth(3, settings.value("column3", 32).toInt());
    int area_int = settings.value("ToolBarArea", 4).toInt();
    settings.endGroup();
    toolBar = new StorageVolToolBar(this);
    addToolBar(toolBar->get_ToolBarArea(area_int), toolBar);
    connect(toolBar, SIGNAL(fileForMethod(const QStringList&)),
            this, SLOT(newVirtStorageVolFromXML(const QStringList&)));
    connect(toolBar, SIGNAL(execMethod(const QStringList&)),
            this, SLOT(execAction(const QStringList&)));
}
VirtStorageVolControl::~VirtStorageVolControl()
{
    settings.beginGroup("VirtStorageVolControl");
    settings.setValue("column0", storageVolList->columnWidth(0));
    settings.setValue("column1", storageVolList->columnWidth(1));
    settings.setValue("column2", storageVolList->columnWidth(2));
    settings.setValue("column3", storageVolList->columnWidth(3));
    settings.setValue("ToolBarArea", toolBarArea(toolBar));
    settings.endGroup();
    settings.sync();
    //disconnect(storageVolList, SIGNAL(doubleClicked(const QModelIndex&)),
    //           this, SLOT(storageVolDoubleClicked(const QModelIndex&)));
    disconnect(storageVolList, SIGNAL(customContextMenuRequested(const QPoint&)),
               this, SLOT(storageVolClicked(const QPoint&)));
    disconnect(toolBar, SIGNAL(fileForMethod(const QStringList&)),
               this, SLOT(newVirtStorageVolFromXML(const QStringList&)));
    disconnect(toolBar, SIGNAL(execMethod(const QStringList&)),
               this, SLOT(execAction(const QStringList&)));

    stopProcessing();

    if ( currWorkConnect!=NULL ) {
        virConnectClose(currWorkConnect);
        currWorkConnect = NULL;
    };

    delete toolBar;
    toolBar = NULL;

    if (storageVolModel!=NULL) {
        delete storageVolModel;
        storageVolModel = NULL;
    };

    if (storageVolList!=NULL) {
        delete storageVolList;
        storageVolList = NULL;
    };
}

/* public slots */
bool VirtStorageVolControl::getThreadState() const
{
    return true;
}
void VirtStorageVolControl::stopProcessing()
{
    setEnabled(false);
    toolBar->stopProcessing();

    if ( currWorkConnect!=NULL ) {
        virConnectClose(currWorkConnect);
        currWorkConnect = NULL;
    };

    // clear StorageVol list
    while ( storageVolModel->DataList.count() ) {
        storageVolModel->removeRow(0);
    };
    storageVolModel->setHeaderData(0, Qt::Horizontal, QString("Name"), Qt::EditRole);
}
bool VirtStorageVolControl::setCurrentStoragePool(virConnect *conn, QString &connName, QString &poolName)
{
    stopProcessing();
    currWorkConnect = conn;
    int ret = virConnectRef(currWorkConnect);
    if ( ret<0 ) {
        virErrorPtr virtErrors = virGetLastError();
        if ( virtErrors!=NULL && virtErrors->code>0 ) {
            QString time = QTime::currentTime().toString();
            QString msg = QString("%3 VirtError(%1) : %2")
                    .arg(virtErrors->code)
                    .arg(virtErrors->message)
                    .arg(time);
            emit storageVolMsg( msg );
            virResetError(virtErrors);
        };
        currWorkConnect = NULL;
        return false;
    } else {
        setEnabled(true);
        currConnName = connName;
        currPoolName = poolName;
        storageVolModel->setHeaderData(
                    0,
                    Qt::Horizontal,
                    QString("Name (Pool: \"%1\")").arg(poolName),
                    Qt::EditRole);
        toolBar->enableAutoReload();
        // for initiation content
        QStringList args;
        args.prepend("reloadVirtStorageVol");
        args.prepend(QString::number(GET_ALL_ENTITY));
        args.prepend(currConnName);
        args.append(currPoolName);
        emit addNewTask(currWorkConnect, args);
        return true;
    };
}
QString VirtStorageVolControl::getCurrentVolumeName() const
{
    QModelIndex index = storageVolList->currentIndex();
    if ( !index.isValid() ) return QString();
    return storageVolModel->DataList.at(index.row())->getName();
}
void VirtStorageVolControl::resultReceiver(Result data)
{
    QStringList args;
    //qDebug()<<act<<data<<"result";
    if ( data.action == GET_ALL_ENTITY ) {
        int chain  = storageVolModel->columnCount();
        int chains = data.msg.count()/chain;
        if ( chains > storageVolModel->DataList.count() ) {
            int _diff = chains - storageVolModel->DataList.count();
            for ( int i = 0; i<_diff; i++ ) {
                storageVolModel->insertRow(1);
                //qDebug()<<i<<"insert";
            };
        };
        if ( storageVolModel->DataList.count() > chains ) {
            int _diff = storageVolModel->DataList.count() - chains;
            for ( int i = 0; i<_diff; i++ ) {
                storageVolModel->removeRow(0);
                //qDebug()<<i<<"remove";
            };
        };
        for (int i = 0; i<chains; i++) {
            for (int j = 0; j<chain; j++) {
                storageVolModel->setData(storageVolModel->index(i,j), data.msg.at(i*chain+j), Qt::EditRole);
            };
        };
    } else if ( data.action == CREATE_ENTITY ) {
        if ( !data.msg.isEmpty() ) {
            msgRepeater(data.msg.join(" "));
            args.prepend("reloadVirtStorageVol");
            args.prepend(QString::number(GET_ALL_ENTITY));
            args.prepend(currConnName);
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        };
    } else if ( data.action == DELETE_ENTITY ) {
        if ( !data.msg.isEmpty() ) {
            msgRepeater(data.msg.join(" "));
            args.prepend("reloadVirtStorageVol");
            args.prepend(QString::number(GET_ALL_ENTITY));
            args.prepend(currConnName);
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        };
    } else if ( data.action == DOWNLOAD_ENTITY ) {
        if ( !data.msg.isEmpty() ) msgRepeater(data.msg.join(" "));
    } else if ( data.action == UPLOAD_ENTITY ) {
        if ( !data.msg.isEmpty() ) msgRepeater(data.msg.join(" "));
    } else if ( data.action == RESIZE_ENTITY ) {
        if ( !data.msg.isEmpty() ) {
            msgRepeater(data.msg.join(" "));
            args.prepend("reloadVirtStorageVol");
            args.prepend(QString::number(GET_ALL_ENTITY));
            args.prepend(currConnName);
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        };
    } else if ( data.action == WIPE_ENTITY ) {
        if ( !data.msg.isEmpty() ) msgRepeater(data.msg.join(" "));
    } else if ( data.action == GET_XML_DESCRIPTION ) {
        if ( !data.msg.isEmpty() ) {
            QString xml = data.msg.first();
            data.msg.removeFirst();
            data.msg.append(QString("to <a href='%1'>%1</a>").arg(xml));
            msgRepeater(data.msg.join(" "));
            QDesktopServices::openUrl(QUrl(xml));
        };
    };
}

/* private slots */
void VirtStorageVolControl::msgRepeater(QString msg)
{
    QString time = QTime::currentTime().toString();
    QString title = QString("Conn '%1'").arg(currConnName);
    QString errorMsg = QString("<b>%1 %2:</b><br>%3").arg(time).arg(title).arg(msg);
    emit storageVolMsg(errorMsg);
}
void VirtStorageVolControl::changeDockVisibility()
{
    toolBar->setEnabled( !toolBar->isEnabled() );
    storageVolList->setEnabled( !storageVolList->isEnabled() );
}

void VirtStorageVolControl::storageVolClicked(const QPoint &p)
{
    //qDebug()<<"custom Menu request";
    QModelIndex idx = storageVolList->indexAt(p);
    QStringList params;
    if ( idx.isValid() ) {
        //qDebug()<<storageVolModel->DataList.at(idx.row())->getName();
        params<<storageVolModel->DataList.at(idx.row())->getName();
        //params<<storageVolModel->DataList.at(idx.row())->getPath();
        //params<<storageVolModel->DataList.at(idx.row())->getType();
        //params<<storageVolModel->DataList.at(idx.row())->getCurrSize();
        //params<<storageVolModel->DataList.at(idx.row())->getLogicSize();
    } else {
        storageVolList->clearSelection();
    };
    bool state = toolBar->getAutoReloadState();
    StorageVolControlMenu *storageVolControlMenu = new StorageVolControlMenu(this, params, state);
    connect(storageVolControlMenu, SIGNAL(execMethod(const QStringList&)),
            this, SLOT(execAction(const QStringList&)));
    storageVolControlMenu->move(QCursor::pos());
    storageVolControlMenu->exec();
    disconnect(storageVolControlMenu, SIGNAL(execMethod(const QStringList&)),
               this, SLOT(execAction(const QStringList&)));
    storageVolControlMenu->deleteLater();
}
void VirtStorageVolControl::storageVolDoubleClicked(const QModelIndex &index)
{
    if ( index.isValid() ) {
        qDebug()<<storageVolModel->DataList.at(index.row())->getName();
    }
}
void VirtStorageVolControl::execAction(const QStringList &l)
{
    QStringList args;
    QModelIndex idx = storageVolList->currentIndex();
    if ( idx.isValid() && storageVolModel->DataList.count() ) {
        QString storageVolName = storageVolModel->DataList.at(idx.row())->getName();
        args.append(storageVolName);
        if        ( l.first()=="reloadVirtStorageVol" ) {
            args.prepend(l.first());
            args.prepend(QString::number(GET_ALL_ENTITY));
            args.prepend(currConnName);
            // append current Pool name in same places
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        } else if ( l.first()=="deleteVirtStorageVol" ) {
            args.prepend(l.first());
            args.prepend(QString::number(DELETE_ENTITY));
            args.prepend(currConnName);
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        } else if ( l.first()=="downloadVirtStorageVol" ) {
            QString path = QFileDialog::getSaveFileName(this, "Save to", "~");
            if ( !path.isEmpty() ) {
                args.append(path);
                args.append(storageVolModel->DataList.at(idx.row())->getCurrSize());
                args.prepend(l.first());
                args.prepend(QString::number(DOWNLOAD_ENTITY));
                args.prepend(currConnName);
                args.append(currPoolName);
                emit addNewTask(currWorkConnect, args);
            } else return;
        } else if ( l.first()=="resizeVirtStorageVol" ) {
            ResizeDialog *resizeDialog = new ResizeDialog(this,
                storageVolModel->DataList.at(idx.row())->getLogicSize().toULongLong());
            int res = resizeDialog->exec();
            unsigned long long size = resizeDialog->getNewSize();
            resizeDialog->deleteLater();
            if ( res ) {
                args.append( QString("%1").arg(size) );
            } else {
                return;
            };
            args.prepend(l.first());
            args.prepend(QString::number(RESIZE_ENTITY));
            args.prepend(currConnName);
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        } else if ( l.first()=="uploadVirtStorageVol" ) {
            QString path = QFileDialog::getOpenFileName(this, "Read from", "~");
            if ( !path.isEmpty() ) {
                args.append(path);
                args.prepend(l.first());
                args.prepend(QString::number(UPLOAD_ENTITY));
                args.prepend(currConnName);
                args.append(currPoolName);
                emit addNewTask(currWorkConnect, args);
            } else return;
        } else if ( l.first()=="wipeVirtStorageVol" ) {
            args.append( (l.count()>1) ? l.at(1) : "0" );
            args.prepend(l.first());
            args.prepend(QString::number(WIPE_ENTITY));
            args.prepend(currConnName);
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        } else if ( l.first()=="getVirtStorageVolXMLDesc" ) {
            args.prepend(l.first());
            args.prepend(QString::number(GET_XML_DESCRIPTION));
            args.prepend(currConnName);
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        } else if ( l.first()=="stopOverViewVirtStoragePool" ) {
            stopProcessing();
        } else if ( l.first()=="reloadVirtStorageVol" ) {
            args.prepend(l.first());
            args.prepend(QString::number(GET_ALL_ENTITY));
            args.prepend(currConnName);
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        };
    } else if ( l.first()=="stopOverViewVirtStoragePool" ) {
        stopProcessing();
    } else if ( l.first()=="reloadVirtStorageVol" ) {
        args.prepend(l.first());
        args.prepend(QString::number(GET_ALL_ENTITY));
        args.prepend(currConnName);
        args.append(currPoolName);
        emit addNewTask(currWorkConnect, args);
    };
}
void VirtStorageVolControl::newVirtStorageVolFromXML(const QStringList &_args)
{
    Actions act;
    if ( !_args.isEmpty() ) {
        if ( _args.first().startsWith("create") ) act = CREATE_ENTITY;
        else act = _EMPTY_ACTION;
        QStringList args = _args;
        args.removeFirst();
        if ( !args.isEmpty() ) {
            if ( args.first()=="manually" ) {
                args.removeFirst();
                QString source = args.first();
                args.removeFirst();
                QString path, _poolType;
                bool show = false;
                // show SRC Creator widget
                // get path for method
                virStoragePoolPtr _pool = virStoragePoolLookupByName(
                            currWorkConnect, currPoolName.toUtf8().data());
                QDomDocument doc;
                doc.setContent(
                            QString(
                                virStoragePoolGetXMLDesc(_pool, VIR_STORAGE_XML_INACTIVE))
                            );
                _poolType = doc.firstChildElement("pool").attribute("type");
                CreateVolume *createVolumeDialog = new CreateVolume(this, _poolType);
                if ( createVolumeDialog->exec()==QDialog::Accepted ) {
                    path = createVolumeDialog->getStorageXMLDescFileName();
                    show = createVolumeDialog->showXMLDescription();
                };
                delete createVolumeDialog;
                createVolumeDialog = NULL;
                args.prepend(path);
                if ( show ) QDesktopServices::openUrl(QUrl(path));
            };
            args.prepend(QString::number(act));
            args.prepend(currConnName);
            args.append(currPoolName);
            emit addNewTask(currWorkConnect, args);
        };
    };
}
