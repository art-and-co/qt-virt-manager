#ifndef SPICE_VIEWER_H
#define SPICE_VIEWER_H

#include <QMainWindow>
#include <QDomDocument>
#include <QShortcut>
#include <QTimerEvent>
#include "vm_viewer/vm_viewer.h"
#include "vm_viewer/qspice_widgets/qspicewidget.h"

class Spice_Viewer : public VM_Viewer
{
    Q_OBJECT
public:
    explicit Spice_Viewer(
            QWidget *parent = NULL,
            virConnect *conn = NULL,
            QString arg1 = QString(),
            QString arg2 = QString());

private:
    QString          runXmlDesc;
    QString          addr;
    uint             port = 0;
    QSpiceWidget    *spiceWdg = NULL;
    QShortcut       *actFullScreen = NULL;

public slots:
    void             reconnectToDomain();
    void             sendKeySeqToDomain(Qt::Key);

private slots:
    void             timerEvent(QTimerEvent*);
    void             DisplayResize(const QSize&);
    void             FullScreenTriggered();
};

#endif // SPICE_VIEWER_H
