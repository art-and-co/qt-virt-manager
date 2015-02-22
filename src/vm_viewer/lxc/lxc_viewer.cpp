#include "lxc_viewer.h"

#define BLOCK_SIZE  1024*100
#define TIMEOUT     60*1000
#define PERIOD      333

LXC_Viewer::LXC_Viewer(
        QWidget *parent,
        virConnect *conn,
        QString arg1,
        QString arg2,
        const QString& work_dir,
        const QString& command) :
    TermMainWindow(parent, conn, arg1, arg2, work_dir, command)
{
    qRegisterMetaType<QString>("QString&");
    // unused toolbar
    viewerToolBar->setVisible(false);
    if ( jobConnect!=NULL ) {
        domainPtr = getDomainPtr();
    };
    QString msg;
    if ( domainPtr!=NULL ) {
        stream = virStreamNew( jobConnect, VIR_STREAM_NONBLOCK );
        if ( stream==NULL ) {
            msg = QString("In '<b>%1</b>': Create virtual stream failed...").arg(domain);
            receiveErrMsg(msg);
        } else {
            /*
             * Older servers did not support either flag,
             * and also did not forbid simultaneous clients on a console,
             * with potentially confusing results.
             * When passing @flags of 0 in order to support a wider range of server versions,
             * it is up to the client to ensure mutual exclusion.
             */
            int ret = -1;
            if ( ret=virDomainOpenConsole( domainPtr, NULL, stream, VIR_DOMAIN_CONSOLE_SAFE)+1 ) {
                msg = QString("In '<b>%1</b>': Console opened in SAFE-mode...").arg(domain);
                receiveErrMsg(msg);
            } else if ( ret=virDomainOpenConsole( domainPtr, NULL, stream, VIR_DOMAIN_CONSOLE_FORCE )+1 ) {
                msg = QString("In '<b>%1</b>': Console opened in FORCE-mode...").arg(domain);
                receiveErrMsg(msg);
            } else if ( ret=virDomainOpenConsole( domainPtr, NULL, stream, 0 )+1 ) {
                msg = QString("In '<b>%1</b>': Console opened in ZIRO-mode...").arg(domain);
                receiveErrMsg(msg);
            } else {
                msg = QString("In '<b>%1</b>': Open console failed...").arg(domain);
                receiveErrMsg(msg);
                sendConnErrors();
                getCurrentTerminal()->m_term->sendText(msg);
                killTimerId = startTimer(PERIOD);
                closeProcess = new QProgressBar(this);
                statusBar()->addWidget(closeProcess);
                closeProcess->setRange(0, TIMEOUT);
            };
            if ( ret ) {
                // don't start (default) shell program,
                // because take the data from VM Stream
                timerId = startTimer(PERIOD);
            };
        };
    } else {
        msg = QString("In '<b>%1</b>': Connect or Domain is NULL...").arg(domain);
        receiveErrMsg(msg);
        getCurrentTerminal()->m_term->sendText(msg);
        killTimerId = startTimer(PERIOD);
        closeProcess = new QProgressBar(this);
        statusBar()->addWidget(closeProcess);
        closeProcess->setRange(0, TIMEOUT);
    };
    sendConnErrors();
    //qDebug()<<msg<<"term inits";
}
LXC_Viewer::~LXC_Viewer()
{
    if ( timerId>0 ) killTimer(timerId);
    if ( killTimerId>0 ) killTimer(killTimerId);
    closeStream();
    if ( NULL!=closeProcess ) {
        delete closeProcess;
        closeProcess = NULL;
    };
    qDebug()<<domain<< "Display destroyed";
}

/* public slots */
void LXC_Viewer::closeTerminal()
{
    closeStream();
    emit finished();
}

/* private slots */
void LXC_Viewer::timerEvent(QTimerEvent *ev)
{
    QString msg;
    if ( ev->timerId()==timerId ) {
        ptySlaveFd = this->getPtySlaveFd();
        counter++;
        //qDebug()<<counter<<ptySlaveFd;
        if ( ptySlaveFd>0 ) {
            killTimer(timerId);
            timerId = 0;
            counter = 0;
            if ( registerStreamEvents()<0 ) {
                QString msg = QString("In '<b>%1</b>': Stream Registation fail.").arg(domain);
                receiveErrMsg(msg);
                getCurrentTerminal()->impl()->sendText(msg);
            } else {
                QString msg = QString("In '<b>%1</b>': Stream Registation success. \
PTY opened. Terminal is active.").arg(domain);
                receiveErrMsg(msg);
                setTerminalParameters();
            };
        } else if ( TIMEOUT<counter*PERIOD ) {
            killTimer(timerId);
            timerId = 0;
            counter = 0;
            QString msg = QString("In '<b>%1</b>': Open PTY Error...").arg(domain);
            receiveErrMsg(msg);
            getCurrentTerminal()->impl()->sendText(msg);
        }
    } else if ( ev->timerId()==killTimerId ) {
        counter++;
        closeProcess->setValue(counter*PERIOD*6);
        if ( TIMEOUT<counter*PERIOD*6 ) {
            killTimer(killTimerId);
            killTimerId = 0;
            counter = 0;
            closeTerminal();
        };
    }
}
void LXC_Viewer::setTerminalParameters()
{
    // reserved for set terminal parameters
    TermWidget *t = getCurrentTerminal();
    if ( NULL!=t ) {
        connect(t->impl(), SIGNAL(sendData(const char*,int)),
                this, SLOT(sendDataToVMachine(const char*,int)));
        /*
         * So usually terminals don't support
         * the Window manipulation through CSI,
         * then zoomOut it to standard 80x24.
         */
        bool recognized = false;
        do {
            int l = t->impl()->screenLinesCount();
            int c = t->impl()->screenColumnsCount();
            if (l<24 || c<80) t->impl()->zoomOut();
            else recognized = true;
        } while ( !recognized );
        t->impl()->startTerminalTeletype();
    };
}
void LXC_Viewer::closeEvent(QCloseEvent *ev)
{
    closeTerminal();
}
virDomain* LXC_Viewer::getDomainPtr() const
{
    return virDomainLookupByName(jobConnect, domain.toUtf8().data());
}
int LXC_Viewer::registerStreamEvents()
{
    int ret = virStreamEventAddCallback(stream,
                                        VIR_STREAM_EVENT_READABLE |
                                        VIR_STREAM_EVENT_HANGUP |
                                        VIR_STREAM_EVENT_ERROR,
                                        streamEventCallBack, this,
    //  don't register freeCallback, because it remove viewer
                                        NULL);
    if (ret<0) sendConnErrors();
    return ret;
}
int LXC_Viewer::unregisterStreamEvents()
{
    int ret = virStreamEventRemoveCallback(stream);
    if (ret<0) sendConnErrors();
    return ret;
}
void LXC_Viewer::freeData(void *opaque)
{
    if ( opaque!=NULL ) {
        void *data = opaque;
        free(data);
    }
}
void LXC_Viewer::streamEventCallBack(virStreamPtr _stream, int events, void *opaque)
{
    LXC_Viewer *obj = static_cast<LXC_Viewer*>(opaque);
    if ( events & VIR_STREAM_EVENT_ERROR ||
         events & VIR_STREAM_EVENT_HANGUP ) {
        // Received stream ERROR/HANGUP, closing console
        obj->closeTerminal();
        return;
    };
    if ( events & VIR_STREAM_EVENT_READABLE ) {
        obj->sendDataToDisplay(_stream);
    };
    if ( events & VIR_STREAM_EVENT_WRITABLE ) {
        //obj->sendDataToVMachine(_stream);
    };
}
void LXC_Viewer::updateStreamEvents(virStreamPtr _stream, int type)
{
    int ret = virStreamEventUpdateCallback(_stream, type);
    if (ret<0) sendConnErrors();
}
void LXC_Viewer::sendDataToDisplay(virStreamPtr _stream)
{
    char buff[BLOCK_SIZE];
    memset( buff, '\0', BLOCK_SIZE );
    int got = virStreamRecv(_stream, buff, BLOCK_SIZE);
    switch ( got ) {
    case -2:
        // This is basically EAGAIN
        return;
    case 0:
        // Received EOF from stream, closing
        closeTerminal();
        return;
    case -1:
        // Error stream
        return;
    default:
        // send to TermEmulator stdout useing ptySlaveFd
        for ( int i=0; i<got; i++ ) {
            write(ptySlaveFd, &buff[i], 1);
        };
        break;
    };
}
void LXC_Viewer::sendDataToVMachine(const char *buff, int got)
{
    if ( got<=0 ) return;
    int saved = virStreamSend(stream, buff, got);
    if ( saved==-2 ) {
        // This is basically EAGAIN
        return;
    } else if ( saved==-1 ) {
        sendConnErrors();
    } else {
        //qDebug()<<saved<<"sent";
    };
    updateStreamEvents(stream,
                       VIR_STREAM_EVENT_READABLE |
                       VIR_STREAM_EVENT_ERROR |
                       VIR_STREAM_EVENT_HANGUP);
}
void LXC_Viewer::closeStream()
{
    if ( stream!=NULL ) {
        if ( virStreamEventUpdateCallback(
                 stream,
                 VIR_STREAM_EVENT_READABLE |
                 VIR_STREAM_EVENT_WRITABLE |
                 VIR_STREAM_EVENT_ERROR |
                 VIR_STREAM_EVENT_HANGUP) +1 ) {
            unregisterStreamEvents();
        } else sendConnErrors();
        virStreamFinish(stream);
        virStreamFree(stream);
        stream = NULL;
    };
    qDebug()<<"stream closed";
}
